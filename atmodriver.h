/*
 * Copyright (C) 2011 Andreas Auras <yak54@inkennet.de>
 *
 * This file is part of DFAtmo the driver for 'Atmolight' controllers for XBMC and xinelib based video players.
 *
 * DFAtmo is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * DFAtmo is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 * Many ideas and algorithm in this module had been derived from the fantastic
 * Atmolight-plugin for the Video Disk Recorder (VDR) that was developed by
 * Eike Edener.
 *
 *
 * This module contains common functions of the DFAtmo image analyze, color filter and color output engine.
 *
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>

#ifdef WIN32
#include <windows.h>

#define inline __inline
typedef HINSTANCE lib_handle_t;
typedef DWORD lib_error_t;

#define FILE_LOADABLE(path)     (GetFileAttributes(path) != INVALID_FILE_ATTRIBUTES)
#define LOAD_LIBRARY(path)      LoadLibrary(path)
#define FREE_LIBRARY(hdl)       FreeLibrary(hdl)
#define GET_LIB_PROC(hdl,name)  GetProcAddress(hdl, name)
#define CLEAR_LIB_ERROR()       SetLastError(0)
#define GET_LIB_ERROR()         GetLastError()
#define IS_LIB_ERROR(err)       (err != 0)
#define GET_LIB_ERR_MSG(err, buf) FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf, sizeof(buf), NULL)
#define LIB_NAME_TEMPLATE       "%.*s\\dfatmo-%s.dll"
#define LIB_SEARCH_PATH_SEP     ';'
#else
#include <unistd.h>
#include <dlfcn.h>

typedef void* lib_handle_t;
typedef const char* lib_error_t;

#define FILE_LOADABLE(path)     (access(path, R_OK) == 0)
#define LOAD_LIBRARY(path)      dlopen(path, RTLD_NOW)
#define FREE_LIBRARY(hdl)       dlclose(hdl)
#define GET_LIB_PROC(hdl,name)  dlsym(hdl, name)
#define CLEAR_LIB_ERROR()       dlerror()
#define GET_LIB_ERROR()         dlerror()
#define IS_LIB_ERROR(err)       (err != NULL)
#define GET_LIB_ERR_MSG(err,buf) { if (err != NULL) strncpy(buf, err, sizeof(buf)); else buf[0] = 0; }
#define LIB_NAME_TEMPLATE       "%.*s/dfatmo-%s.so"
#define LIB_SEARCH_PATH_SEP     ':'
#endif

#include "dfatmo.h"

/* accuracy of color calculation */
#define h_MAX   255
#define s_MAX   255
#define v_MAX   255

/* macros */
#define MIN(X, Y)  ((X) < (Y) ? (X) : (Y))
#define MAX(X, Y)  ((X) > (Y) ? (X) : (Y))
#define POS_DIV(a, b)  ( (a)/(b) + ( ((a)%(b) >= (b)/2 ) ? 1 : 0) )

#define FILTER_NONE             0
#define FILTER_PERCENTAGE       1
#define FILTER_COMBINED         2
#define NUM_FILTERS             2

typedef struct { uint8_t h, s, v; } hsv_color_t;
typedef struct { uint64_t r, g, b; } rgb_color_sum_t;

typedef struct {
    /* configuration related */
  atmo_parameters_t parm;
  atmo_parameters_t active_parm;
  int sum_channels;

    /* analyze related */
  uint64_t *hue_hist, *sat_hist;
  uint64_t *w_hue_hist, *w_sat_hist;
  uint64_t *avg_bright;
  int *most_used_hue, *last_most_used_hue, *most_used_sat, *avg_cnt;
  rgb_color_t *analyzed_colors;
  int analyze_width, analyze_height;
  int img_size, alloc_img_size;
  int edge_weighting;
  hsv_color_t *hsv_img;
  uint8_t *weight;

    /* color filter related */
  rgb_color_t *filtered_colors;
  rgb_color_t *mean_filter_values;
  rgb_color_sum_t *mean_filter_sum_values;
  int old_mean_length;

    /* delay filter related */
  rgb_color_t *filtered_output_colors;
  rgb_color_t *delay_filter_queue;
  int delay_filter_queue_length, delay_filter_queue_pos;
  int filter_delay;
  int output_rate;

    /* output related */
  lib_handle_t output_driver_lib;
  output_driver_t *output_driver;
  int driver_opened;
  rgb_color_t *output_colors, *last_output_colors;

} atmo_driver_t;


int *dfatmo_driver_log_level;


static inline void rgb_to_hsv(hsv_color_t *hsv, int r, int g, int b) {
  int min_v, max_v, delta_v;
  int h = 0;
  int dr, dg, db, divisor;

  min_v = MIN(MIN(r, g), b);
  max_v = MAX(MAX(r, g), b);

  delta_v = max_v - min_v;

  hsv->v = (uint8_t) POS_DIV((max_v * v_MAX), 255);

  if (delta_v == 0) {
    h = 0;
    hsv->s = 0;
  } else {
    hsv->s = (uint8_t) POS_DIV((delta_v * s_MAX), max_v);

    dr = (max_v - r) + 3 * delta_v;
    dg = (max_v - g) + 3 * delta_v;
    db = (max_v - b) + 3 * delta_v;
    divisor = 6 * delta_v;

    if (r == max_v) {
      h = POS_DIV(( (db - dg) * h_MAX ) , divisor);
    } else if (g == max_v) {
      h = POS_DIV( ((dr - db) * h_MAX) , divisor) + (h_MAX/3);
    } else if (b == max_v) {
      h = POS_DIV(( (dg - dr) * h_MAX) , divisor) + (h_MAX/3) * 2;
    }

    if (h < 0) {
      h += h_MAX;
    }
    if (h > h_MAX) {
      h -= h_MAX;
    }
  }
  hsv->h = (uint8_t) h;
}


static void calc_weight(atmo_driver_t *this) {
  int row, col, c;
  uint8_t *weight = this->weight;
  const int width = this->analyze_width;
  const int height = this->analyze_height;
  const double w = this->edge_weighting > 10 ? (double)this->edge_weighting / 10.0: 1.0;
  const int top_channels = this->active_parm.top;
  const int bottom_channels = this->active_parm.bottom;
  const int left_channels = this->active_parm.left;
  const int right_channels = this->active_parm.right;
  const int center_channel = this->active_parm.center;
  const int top_left_channel = this->active_parm.top_left;
  const int top_right_channel = this->active_parm.top_right;
  const int bottom_left_channel = this->active_parm.bottom_left;
  const int bottom_right_channel = this->active_parm.bottom_right;

  const int sum_top_channels = top_channels + top_left_channel + top_right_channel;
  const int sum_bottom_channels = bottom_channels + bottom_left_channel + bottom_right_channel;
  const int sum_left_channels = left_channels + bottom_left_channel + top_left_channel;
  const int sum_right_channels = right_channels + bottom_right_channel + top_right_channel;

  const int center_y = height / 2;
  const int center_x = width / 2;

  const double fheight = height - 1;
  const double fwidth = width - 1;

  for (row = 0; row < height; ++row)
  {
    double row_norm = (double)row / fheight;
    int top = (int)(255.0 * pow(1.0 - row_norm, w));
    int bottom = (int)(255.0 * pow(row_norm, w));

    for (col = 0; col < width; ++col)
    {
      double col_norm = (double)col / fwidth;
      int left = (int)(255.0 * pow((1.0 - col_norm), w));
      int right = (int)(255.0 * pow(col_norm, w));

      for (c = top_left_channel; c < (top_channels + top_left_channel); ++c)
        *weight++ = (col >= ((width * c) / sum_top_channels) && col < ((width * (c + 1)) / sum_top_channels) && row < center_y) ? top: 0;

      for (c = bottom_left_channel; c < (bottom_channels + bottom_left_channel); ++c)
        *weight++ = (col >= ((width * c) / sum_bottom_channels) && col < ((width * (c + 1)) / sum_bottom_channels) && row >= center_y) ? bottom: 0;

      for (c = top_left_channel; c < (left_channels + top_left_channel); ++c)
        *weight++ = (row >= ((height * c) / sum_left_channels) && row < ((height * (c + 1)) / sum_left_channels) && col < center_x) ? left: 0;

      for (c = top_right_channel; c < (right_channels + top_right_channel); ++c)
        *weight++ = (row >= ((height * c) / sum_right_channels) && row < ((height * (c + 1)) / sum_right_channels) && col >= center_x) ? right: 0;

      if (center_channel)
        *weight++ = 255;

      if (top_left_channel)
        *weight++ = (col < center_x && row < center_y) ? ((top > left) ? top: left) : 0;

      if (top_right_channel)
        *weight++ = (col >= center_x && row < center_y) ? ((top > right) ? top: right): 0;

      if (bottom_left_channel)
        *weight++ = (col < center_x && row >= center_y) ? ((bottom > left) ? bottom: left): 0;

      if (bottom_right_channel)
        *weight++ = (col >= center_x && row >= center_y) ? ((bottom > right) ? bottom: right): 0;
    }
  }
}


static void calc_hue_hist(atmo_driver_t *this) {
  hsv_color_t *hsv = this->hsv_img;
  uint8_t *weight = this->weight;
  int img_size = this->img_size;
  const int n = this->sum_channels;
  uint64_t * const hue_hist = this->hue_hist;
  const int darkness_limit = this->active_parm.darkness_limit;

  memset(hue_hist, 0, (n * (h_MAX+1) * sizeof(uint64_t)));

  while (img_size--) {
    if (hsv->v >= darkness_limit) {
      int c;
      for (c = 0; c < n; ++c)
        hue_hist[c * (h_MAX+1) + hsv->h] += weight[c] * hsv->v;
    }
    weight += n;
    ++hsv;
  }
}


static void calc_windowed_hue_hist(atmo_driver_t *this) {
  int i, c, w;
  const int n = this->sum_channels;
  uint64_t * const hue_hist = this->hue_hist;
  uint64_t * const w_hue_hist = this->w_hue_hist;
  const int hue_win_size = this->active_parm.hue_win_size;
  uint64_t win_weight;

  memset(w_hue_hist, 0, (n * (h_MAX+1) * sizeof(uint64_t)));

  for (i = 0; i < (h_MAX+1); ++i)
  {
    for (w = -hue_win_size; w <= hue_win_size; w++)
    {
      int iw = i + w;

      if (iw < 0)
        iw = iw + h_MAX + 1;
      if (iw > h_MAX)
        iw = iw - h_MAX - 1;

      win_weight = (hue_win_size + 1) - abs(w);

      for (c = 0; c < n; ++c)
        w_hue_hist[c * (h_MAX+1) + i] += hue_hist[c * (h_MAX+1) + iw] * win_weight;
    }
  }
}


static void calc_most_used_hue(atmo_driver_t *this) {
  const int n = this->sum_channels;
  uint64_t * const w_hue_hist = this->w_hue_hist;
  int * const most_used_hue = this->most_used_hue;
  int * const last_most_used_hue = this->last_most_used_hue;
  const double hue_threshold = (double)this->active_parm.hue_threshold / 100.0;
  int i, c;

  memset(most_used_hue, 0, (n * sizeof(int)));

  for (c = 0; c < n; ++c) {
    uint64_t v = 0;
    for (i = 0; i < (h_MAX + 1); ++i) {
      if (w_hue_hist[c * (h_MAX+1) + i] > v) {
        v = w_hue_hist[c * (h_MAX+1) + i];
        most_used_hue[c] = i;
      }
    }
    if (((double) w_hue_hist[c * (h_MAX+1) + last_most_used_hue[c]] / (double) v) > hue_threshold)
      most_used_hue[c] = last_most_used_hue[c];
    else
      last_most_used_hue[c] = most_used_hue[c];
  }
}


static void calc_sat_hist(atmo_driver_t *this) {
  hsv_color_t *hsv = this->hsv_img;
  uint8_t *weight = this->weight;
  int img_size = this->img_size;
  const int n = this->sum_channels;
  uint64_t * const sat_hist = this->sat_hist;
  int * const most_used_hue = this->most_used_hue;
  const int darkness_limit = this->active_parm.darkness_limit;
  const int hue_win_size = this->active_parm.hue_win_size;

  memset(sat_hist, 0, (n * (s_MAX+1) * sizeof(uint64_t)));

  while (img_size--) {
    if (hsv->v >= darkness_limit) {
      int h = hsv->h;
      int c;
      for (c = 0; c < n; ++c) {
        if (h > (most_used_hue[c] - hue_win_size) && h < (most_used_hue[c] + hue_win_size))
          sat_hist[c * (s_MAX+1) + hsv->s] += weight[c] * hsv->v;
      }
    }
    weight += n;
    ++hsv;
  }
}


static void calc_windowed_sat_hist(atmo_driver_t *this) {
  int i, c, w;
  const int n = this->sum_channels;
  uint64_t * const sat_hist = this->sat_hist;
  uint64_t * const w_sat_hist = this->w_sat_hist;
  const int sat_win_size = this->active_parm.sat_win_size;
  uint64_t win_weight;

  memset(w_sat_hist, 0, (n * (s_MAX+1) * sizeof(uint64_t)));

  for (i = 0; i < (s_MAX+1); ++i)
  {
    for (w = -sat_win_size; w <= sat_win_size; w++)
    {
      int iw = i + w;

      if (iw < 0)
        iw = iw + s_MAX + 1;
      if (iw > s_MAX)
        iw = iw - s_MAX - 1;

      win_weight = (sat_win_size + 1) - abs(w);

      for (c = 0; c < n; ++c)
        w_sat_hist[c * (s_MAX+1) + i] += sat_hist[c * (s_MAX+1) + iw] * win_weight;
    }
  }
}


static void calc_most_used_sat(atmo_driver_t *this) {
  const int n = this->sum_channels;
  uint64_t * const w_sat_hist = this->w_sat_hist;
  int * const most_used_sat = this->most_used_sat;
  int i, c;

  memset(most_used_sat, 0, (n * sizeof(int)));

  for (c = 0; c < n; ++c) {
    uint64_t v = 0;
    for (i = 0; i < (s_MAX + 1); ++i) {
      if (w_sat_hist[c * (s_MAX+1) + i] > v) {
        v = w_sat_hist[c * (s_MAX+1) + i];
        most_used_sat[c] = i;
      }
    }
  }
}


static void calc_average_brightness(atmo_driver_t *this) {
  hsv_color_t *hsv = this->hsv_img;
  uint8_t *weight = this->weight;
  int img_size = this->img_size;
  const int n = this->sum_channels;
  const int darkness_limit = this->active_parm.darkness_limit;
  const uint64_t bright = this->active_parm.brightness;
  uint64_t * const avg_bright = this->avg_bright;
  int * const avg_cnt = this->avg_cnt;
  int c;

  memset(avg_bright, 0, (n * sizeof(uint64_t)));
  memset(avg_cnt, 0, (n * sizeof(int)));

  while (img_size--) {
    const int v = hsv->v;
    if (v >= darkness_limit) {
      for (c = 0; c < n; ++c) {
        avg_bright[c] += v * weight[c];
        avg_cnt[c] += weight[c];
      }
    }
    weight += n;
    ++hsv;
  }

  for (c = 0; c < n; ++c) {
    if (avg_cnt[c]) {
      avg_bright[c] = (avg_bright[c] * bright) / (avg_cnt[c] * ((uint64_t)100));
      if (avg_bright[c] > v_MAX)
        avg_bright[c] = v_MAX;
    }
  }
}


static void calc_uniform_average_brightness(atmo_driver_t *this) {
  hsv_color_t *hsv = this->hsv_img;
  int img_size = this->img_size;
  const int darkness_limit = this->active_parm.darkness_limit;
  uint64_t avg = 0;
  int cnt = 0;
  uint64_t * const avg_bright = this->avg_bright;
  int c = this->sum_channels;

  while (img_size--) {
    const int v = hsv->v;
    if (v >= darkness_limit) {
      avg += v;
      ++cnt;
    }
    ++hsv;
  }

  if (cnt)
    avg /= cnt;
  else
    avg = darkness_limit;

  avg = (avg * this->active_parm.brightness) / 100;
  if (avg > v_MAX)
    avg = v_MAX;

  while (c)
    avg_bright[--c] = avg;
}


static void hsv_to_rgb(rgb_color_t *rgb, double h, double s, double v) {
  int i;
  double f, p, q, t;

  rgb->r = rgb->g = rgb->b = 0;

  h /= h_MAX;
  s /= s_MAX;
  v /= v_MAX;

  if (s == 0.0) {
    rgb->r = (uint8_t) (v * 255.0 + 0.5);
    rgb->g = rgb->r;
    rgb->b = rgb->r;
  } else {
    h = h * 6.0;
    if (h == 6.0)
      h = 0.0;
    i = (int) h;

    f = h - i;
    p = v * (1.0 - s);
    q = v * (1.0 - (s * f));
    t = v * (1.0 - (s * (1.0 - f)));

    if (i == 0) {
      rgb->r = (uint8_t) (v * 255.0 + 0.5);
      rgb->g = (uint8_t) (t * 255.0 + 0.5);
      rgb->b = (uint8_t) (p * 255.0 + 0.5);
    } else if (i == 1) {
      rgb->r = (uint8_t) (q * 255.0 + 0.5);
      rgb->g = (uint8_t) (v * 255.0 + 0.5);
      rgb->b = (uint8_t) (p * 255.0 + 0.5);
    } else if (i == 2) {
      rgb->r = (uint8_t) (p * 255.0 + 0.5);
      rgb->g = (uint8_t) (v * 255.0 + 0.5);
      rgb->b = (uint8_t) (t * 255.0 + 0.5);
    } else if (i == 3) {
      rgb->r = (uint8_t) (p * 255.0 + 0.5);
      rgb->g = (uint8_t) (q * 255.0 + 0.5);
      rgb->b = (uint8_t) (v * 255.0 + 0.5);
    } else if (i == 4) {
      rgb->r = (uint8_t) (t * 255.0 + 0.5);
      rgb->g = (uint8_t) (p * 255.0 + 0.5);
      rgb->b = (uint8_t) (v * 255.0 + 0.5);
    } else {
      rgb->r = (uint8_t) (v * 255.0 + 0.5);
      rgb->g = (uint8_t) (p * 255.0 + 0.5);
      rgb->b = (uint8_t) (q * 255.0 + 0.5);
    }
  }
}


static void calc_rgb_values(atmo_driver_t *this) {
  const int n = this->sum_channels;
  int c;

  for (c = 0; c < n; ++c)
    hsv_to_rgb(&this->analyzed_colors[c], (double)this->most_used_hue[c], (double)this->most_used_sat[c], (double)this->avg_bright[c]);
}


static void reset_filters (atmo_driver_t *this) {
  this->old_mean_length = 0;
  this->filter_delay = -1;
}


static void percent_filter(atmo_driver_t *this, rgb_color_t *act) {
  rgb_color_t *out = this->filtered_colors;
  const int old_p = this->active_parm.filter_smoothness;
  const int new_p = 100 - old_p;
  int n = this->sum_channels;

  while (n--) {
    out->r = (act->r * new_p + out->r * old_p) / 100;
    out->g = (act->g * new_p + out->g * old_p) / 100;
    out->b = (act->b * new_p + out->b * old_p) / 100;
    ++act;
    ++out;
  }
}


static void mean_filter(atmo_driver_t *this, rgb_color_t *act) {
  rgb_color_t *out = this->filtered_colors;
  rgb_color_t *mean_values = this->mean_filter_values;
  rgb_color_sum_t *mean_sums = this->mean_filter_sum_values;
  const int64_t mean_threshold = (int64_t) ((double) this->active_parm.filter_threshold * 3.6);
  const int old_p = this->active_parm.filter_smoothness;
  const int new_p = 100 - old_p;
  int n = this->sum_channels;
  const int filter_length = this->active_parm.filter_length;
  const int64_t mean_length = (filter_length < this->active_parm.output_rate) ? 1: filter_length / this->active_parm.output_rate;
  const int reinitialize = ((int)mean_length != this->old_mean_length);
  int64_t dist;
  this->old_mean_length = (int)mean_length;

  while (n--) {
    mean_sums->r += (act->r - mean_values->r);
    mean_values->r = (uint8_t) (mean_sums->r / mean_length);

    mean_sums->g += (act->g - mean_values->g);
    mean_values->g = (uint8_t) (mean_sums->g / mean_length);

    mean_sums->b += (act->b - mean_values->b);
    mean_values->b = (uint8_t) (mean_sums->b / mean_length);

      /*
       * check, if there is a jump -> check if differences between actual values and filter values are too big
       */
    dist = (int64_t)(mean_values->r - act->r) * (int64_t)(mean_values->r - act->r) +
                    (int64_t)(mean_values->g - act->g) * (int64_t)(mean_values->g - act->g) +
                    (int64_t)(mean_values->b - act->b) * (int64_t)(mean_values->b - act->b);

    if (dist > 0)
      dist = (int64_t) sqrt((double) dist);

      /* compare calculated distance with the filter threshold */
    if (dist > mean_threshold || reinitialize) {
        /* filter jump detected -> set the long filters to the result of the short filters */
      *out = *act;
      *mean_values = *act;
      mean_sums->r = act->r * mean_length;
      mean_sums->g = act->g * mean_length;
      mean_sums->b = act->b * mean_length;
    }
    else
    {
        /* apply an additional percent filter */
      out->r = (mean_values->r * new_p + out->r * old_p) / 100;
      out->g = (mean_values->g * new_p + out->g * old_p) / 100;
      out->b = (mean_values->b * new_p + out->b * old_p) / 100;
    }

    ++act;
    ++out;
    ++mean_sums;
    ++mean_values;
  }
}


static void apply_white_calibration(atmo_driver_t *this) {
  const int wc_red = this->active_parm.wc_red;
  const int wc_green = this->active_parm.wc_green;
  const int wc_blue = this->active_parm.wc_blue;
  rgb_color_t *out;
  int n;

  if (wc_red == 255 && wc_green == 255 && wc_blue == 255)
    return;

  out = this->filtered_output_colors;
  n = this->sum_channels;
  while (n--) {
    out->r = (uint8_t)((int)out->r * wc_red / 255);
    out->g = (uint8_t)((int)out->g * wc_green / 255);
    out->b = (uint8_t)((int)out->b * wc_blue / 255);
    ++out;
  }
}


static void apply_gamma_correction(atmo_driver_t *this) {
  const int igamma = this->active_parm.gamma;

  if (igamma <= 10)
    return;

  {
    const double gamma = (double)igamma / 10.0;
    rgb_color_t *out = this->filtered_output_colors;
    int n = this->sum_channels;
    while (n--) {
      out->r = (uint8_t)(pow((double)out->r / 255.0, gamma) * 255.0);
      out->g = (uint8_t)(pow((double)out->g / 255.0, gamma) * 255.0);
      out->b = (uint8_t)(pow((double)out->b / 255.0, gamma) * 255.0);
      ++out;
    }
  }
}


static int config_channels(atmo_driver_t *this) {
  int n = this->parm.top + this->parm.bottom + this->parm.left + this->parm.right +
          this->parm.center +
          this->parm.top_left + this->parm.top_right + this->parm.bottom_left + this->parm.bottom_right;
  this->sum_channels = n;

  if (n)
  {
    this->hue_hist = (uint64_t *) calloc(n * (h_MAX + 1), sizeof(uint64_t));
    this->w_hue_hist = (uint64_t *) calloc(n * (h_MAX + 1), sizeof(uint64_t));
    this->most_used_hue = (int *) calloc(n, sizeof(int));
    this->last_most_used_hue = (int *) calloc(n, sizeof(int));

    this->sat_hist = (uint64_t *) calloc(n * (s_MAX + 1), sizeof(uint64_t));
    this->w_sat_hist = (uint64_t *) calloc(n * (s_MAX + 1), sizeof(uint64_t));
    this->most_used_sat = (int *) calloc(n, sizeof(int));

    this->avg_cnt = (int *) calloc(n, sizeof(int));
    this->avg_bright = (uint64_t *) calloc(n, sizeof(uint64_t));

    this->analyzed_colors = (rgb_color_t *) calloc(n, sizeof(rgb_color_t));
    this->filtered_colors = (rgb_color_t *) calloc(n, sizeof(rgb_color_t));
    this->filtered_output_colors = (rgb_color_t *) calloc(n, sizeof(rgb_color_t));
    this->output_colors = (rgb_color_t *) calloc(n, sizeof(rgb_color_t));
    this->last_output_colors = (rgb_color_t *) calloc(n, sizeof(rgb_color_t));
    this->mean_filter_values = (rgb_color_t *) calloc(n, sizeof(rgb_color_t));
    this->mean_filter_sum_values = (rgb_color_sum_t *) calloc(n, sizeof(rgb_color_sum_t));

    if (!(this->hue_hist &&
        this->w_hue_hist &&
        this->most_used_hue &&
        this->last_most_used_hue &&
        this->sat_hist &&
        this->w_sat_hist &&
        this->most_used_sat &&
        this->avg_cnt &&
        this->avg_bright &&
        this->analyzed_colors &&
        this->filtered_colors &&
        this->filtered_output_colors &&
        this->output_colors &&
        this->last_output_colors &&
        this->mean_filter_values &&
        this->mean_filter_sum_values))
      return 1;
  }
  return 0;
}


static void free_channels(atmo_driver_t *this) {
  if (this->sum_channels)
  {
    FREE_AND_SET_NULL(this->hue_hist);
    FREE_AND_SET_NULL(this->w_hue_hist);
    FREE_AND_SET_NULL(this->most_used_hue);
    FREE_AND_SET_NULL(this->last_most_used_hue);

    FREE_AND_SET_NULL(this->sat_hist);
    FREE_AND_SET_NULL(this->w_sat_hist);
    FREE_AND_SET_NULL(this->most_used_sat);

    FREE_AND_SET_NULL(this->avg_cnt);
    FREE_AND_SET_NULL(this->avg_bright);

    FREE_AND_SET_NULL(this->analyzed_colors);
    FREE_AND_SET_NULL(this->filtered_colors);
    FREE_AND_SET_NULL(this->filtered_output_colors);
    FREE_AND_SET_NULL(this->output_colors);
    FREE_AND_SET_NULL(this->last_output_colors);
    FREE_AND_SET_NULL(this->mean_filter_values);
    FREE_AND_SET_NULL(this->mean_filter_sum_values);

    this->sum_channels = 0;
  }
}


static int null_driver_open(output_driver_t *this_gen, atmo_parameters_t *p) {
  return 0;
}

static int null_driver_configure(output_driver_t *this_gen, atmo_parameters_t *p) {
  return 0;
}

static int null_driver_close(output_driver_t *this_gen) {
  return 0;
}

static void null_driver_dispose(output_driver_t *this_gen) {
  free(this_gen);
}

static int null_driver_output_colors(output_driver_t *this_gen, rgb_color_t *colors, rgb_color_t *last_colors) {
  return 0;
}


static void unload_output_driver(atmo_driver_t *this) {
  if (this->output_driver) {
    this->output_driver->dispose(this->output_driver);
    this->output_driver = NULL;
  }
  if (this->output_driver_lib) {
    FREE_LIBRARY(this->output_driver_lib);
    this->output_driver_lib = NULL;
  }
}


static int load_output_driver(atmo_driver_t *this) {
  char filename[256];
  int found;
  char *p;
  dfatmo_new_output_driver_t new_output_driver;
  lib_handle_t lib;
  lib_error_t err;
  output_driver_t *ot;

  if (!strlen(this->parm.driver))
    strcpy(this->parm.driver, "null");

  if (!strcmp(this->parm.driver, "null")) {
    this->output_driver = (output_driver_t *) calloc(1, sizeof(output_driver_t));
    if (!this->output_driver) {
      DFATMO_LOG(LOG_ERROR, "creating null output driver instance failed");
      return 1;
    }
    this->output_driver->version = DFATMO_OUTPUT_DRIVER_VERSION;
    this->output_driver->open = null_driver_open;
    this->output_driver->configure = null_driver_configure;
    this->output_driver->close = null_driver_close;
    this->output_driver->dispose = null_driver_dispose;
    this->output_driver->output_colors = null_driver_output_colors;
    return 0;
  }

  if (strlen(this->parm.driver_path) == 0) {
    DFATMO_LOG(LOG_ERROR, "output driver search path missing");
    return 1;
  }

  found = 0;
  p = this->parm.driver_path;
  while (p != NULL) {
    char *e = strchr(p, LIB_SEARCH_PATH_SEP);
    if (e == NULL) {
      snprintf(filename, sizeof(filename), LIB_NAME_TEMPLATE, (int)strlen(p), p, this->parm.driver);
      p = e;
    } else {
      snprintf(filename, sizeof(filename), LIB_NAME_TEMPLATE, (int)(e - p), p, this->parm.driver);
      p = e + 1;
    }
    DFATMO_LOG(LOG_DEBUG, "search output driver '%s'", filename);
    if (FILE_LOADABLE(filename)) {
      found = 1;
      break;
    }
  }
  if (!found) {
    DFATMO_LOG(LOG_ERROR, "output driver 'dfatmo-%s' not found", this->parm.driver);
    return 1;
  }

  new_output_driver = NULL;
  CLEAR_LIB_ERROR();
  lib = LOAD_LIBRARY(filename);
  if (lib != NULL)
    new_output_driver = (dfatmo_new_output_driver_t) GET_LIB_PROC(lib, "dfatmo_new_output_driver");
  err = GET_LIB_ERROR();
  if (new_output_driver == NULL || IS_LIB_ERROR(err)) {
    char buf[128];
    GET_LIB_ERR_MSG(err, buf);
    DFATMO_LOG(LOG_ERROR, "loading output driver failed: %s", buf);
    if (lib != NULL)
      FREE_LIBRARY(lib);
    return 1;
  }

  ot = (*new_output_driver)(dfatmo_log_level, dfatmo_log);
  if (ot == NULL) {
    DFATMO_LOG(LOG_ERROR, "creating output driver instance of '%s' failed", filename);
    FREE_LIBRARY(lib);
    return 1;
  }

  if (ot->version != DFATMO_OUTPUT_DRIVER_VERSION) {
    DFATMO_LOG(LOG_ERROR, "wrong version %d of output driver '%s'. Expected version %d", ot->version, filename, DFATMO_OUTPUT_DRIVER_VERSION);
    ot->dispose(ot);
    FREE_LIBRARY(lib);
    return 1;
  }

  this->output_driver = ot;
  this->output_driver_lib = lib;

  return 0;
}


static int turn_lights_off (atmo_driver_t *this) {
  int rc = 0;
  int colors_size = this->sum_channels * sizeof(rgb_color_t);
  memset(this->output_colors, 0, colors_size);
  if (memcmp(this->output_colors, this->last_output_colors, colors_size)) {
    rc = this->output_driver->output_colors(this->output_driver, this->output_colors, this->last_output_colors);
    memset(this->last_output_colors, 0, colors_size);
  }
  return rc;
}


static int close_output_driver (atmo_driver_t *this) {
  int rc = 0;

  if (this->driver_opened) {
    turn_lights_off(this);
    this->driver_opened = 0;
    rc = this->output_driver->close(this->output_driver);
  }
  return rc;
}


static void instant_configure (atmo_driver_t *this) {
  this->active_parm.overscan = this->parm.overscan;
  this->active_parm.darkness_limit = this->parm.darkness_limit;
  this->active_parm.edge_weighting = this->parm.edge_weighting;
  this->active_parm.hue_win_size = this->parm.hue_win_size;
  this->active_parm.sat_win_size = this->parm.sat_win_size;
  this->active_parm.hue_threshold = this->parm.hue_threshold;
  this->active_parm.uniform_brightness = this->parm.uniform_brightness;
  this->active_parm.brightness = this->parm.brightness;
  this->active_parm.filter = this->parm.filter;
  this->active_parm.filter_smoothness = this->parm.filter_smoothness;
  this->active_parm.filter_length = this->parm.filter_length;
  this->active_parm.filter_threshold = this->parm.filter_threshold;
  this->active_parm.filter_delay = this->parm.filter_delay;
  this->active_parm.wc_red = this->parm.wc_red;
  this->active_parm.wc_green = this->parm.wc_green;
  this->active_parm.wc_blue = this->parm.wc_blue;
  this->active_parm.gamma = this->parm.gamma;
  this->active_parm.output_rate = this->parm.output_rate;
  this->active_parm.analyze_size = this->parm.analyze_size;
}


static void init_configuration (atmo_driver_t *this)
{
  memset(this, 0, sizeof(atmo_driver_t));

    /* Set default values for parameters */
  strcpy(this->parm.driver, "null");
#ifdef OUTPUT_DRIVER_PATH
  strcpy(this->parm.driver_path, OUTPUT_DRIVER_PATH);
#endif
  this->parm.brightness = 100;
  this->parm.darkness_limit = 1;
  this->parm.edge_weighting = 60;
  this->parm.filter = FILTER_COMBINED;
  this->parm.filter_length = 500;
  this->parm.filter_smoothness = 50;
  this->parm.filter_threshold = 40;
  this->parm.hue_win_size = 3;
  this->parm.sat_win_size = 3;
  this->parm.hue_threshold = 93;
  this->parm.wc_red = 255;
  this->parm.wc_green = 255;
  this->parm.wc_blue = 255;
  this->parm.output_rate = 20;
  this->parm.gamma = 10;
  this->parm.analyze_rate = 35;
  this->parm.analyze_size = 1;
  this->parm.start_delay = 250;
  this->parm.enabled = 1;
}
