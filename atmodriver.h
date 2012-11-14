/*
 * Copyright (C) 2012 Andreas Auras <yak54@inkennet.de>
 *
 * This file is part of DFAtmo the driver for 'Atmolight' controllers for VDR, XBMC and xinelib based video players.
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

/* Only pixel that are above the minimum weight limit are calculated.  12 -> ~5% */
#define MIN_WEIGHT_LIMIT        12

/* accuracy of color calculation */
#define h_MAX   255
#define s_MAX   255
#define v_MAX   255

/* macros */
#define MIN(X, Y)  ((X) < (Y) ? (X) : (Y))
#define MAX(X, Y)  ((X) > (Y) ? (X) : (Y))
#define POS_DIV(a, b)  ( (a)/(b) + ( ((a)%(b) >= (b)/2 ) ? 1 : 0) )

enum { FILTER_NONE = 0, FILTER_PERCENTAGE, FILTER_COMBINED, NUM_FILTERS };

typedef struct { uint8_t h, s, v; } hsv_color_t;
typedef struct { int r, g, b; } rgb_color_sum_t;
typedef struct { uint32_t pos; uint16_t channel; uint8_t weight; } weight_tab_t;

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
  int weight_tab_size;
  weight_tab_t *weight_tab, *weight_tab_end;

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


#define insert_weight(_c_, _w_) { tmpw = (_w_); if (tmpw > MIN_WEIGHT_LIMIT) { wt->pos = pos; wt->channel = (_c_); wt->weight = tmpw; ++wt; }}

static void calc_weight(atmo_driver_t *self) {
  int row, col, c;
  uint8_t tmpw;
  weight_tab_t *wt = self->weight_tab;
  weight_tab_t *wte = wt + self->weight_tab_size;
  const int width = self->analyze_width;
  const int height = self->analyze_height;
  const double w = self->edge_weighting > 10 ? (double)self->edge_weighting / 10.0: 1.0;
  const int top_channels = self->active_parm.top;
  const int bottom_channels = self->active_parm.bottom;
  const int left_channels = self->active_parm.left;
  const int right_channels = self->active_parm.right;
  const int center_channel = self->active_parm.center;
  const int top_left_channel = self->active_parm.top_left;
  const int top_right_channel = self->active_parm.top_right;
  const int bottom_left_channel = self->active_parm.bottom_left;
  const int bottom_right_channel = self->active_parm.bottom_right;
  const int n = self->sum_channels;

  const int sum_top_channels = top_channels + top_left_channel + top_right_channel;
  const int sum_bottom_channels = bottom_channels + bottom_left_channel + bottom_right_channel;
  const int sum_left_channels = left_channels + bottom_left_channel + top_left_channel;
  const int sum_right_channels = right_channels + bottom_right_channel + top_right_channel;

  const int center_y = height / 2;
  const int center_x = width / 2;

  const double fheight = height - 1;
  const double fwidth = width - 1;

  uint32_t pos = 0;
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

      if ((wte - wt) <= n)
      {
        int wpos = (wt - self->weight_tab);
        int dim = self->weight_tab_size + (width * height);
        self->weight_tab_end = wt;
        wt = (weight_tab_t *) realloc(self->weight_tab, dim * sizeof(weight_tab_t));
        if (!wt)
          return;
        self->weight_tab = wt;
        self->weight_tab_size = dim;
        wte = wt + dim;
        wt += wpos;
      }

      for (c = top_left_channel; c < (top_channels + top_left_channel); ++c)
        insert_weight((c - top_left_channel),
            ((col >= ((width * c) / sum_top_channels) && col < ((width * (c + 1)) / sum_top_channels) && row < center_y) ? top: 0));

      for (c = bottom_left_channel; c < (bottom_channels + bottom_left_channel); ++c)
        insert_weight((c - bottom_left_channel + top_channels),
            ((col >= ((width * c) / sum_bottom_channels) && col < ((width * (c + 1)) / sum_bottom_channels) && row >= center_y) ? bottom: 0));

      for (c = top_left_channel; c < (left_channels + top_left_channel); ++c)
        insert_weight((c - top_left_channel + top_channels + bottom_channels),
            ((row >= ((height * c) / sum_left_channels) && row < ((height * (c + 1)) / sum_left_channels) && col < center_x) ? left: 0));

      for (c = top_right_channel; c < (right_channels + top_right_channel); ++c)
        insert_weight((c - top_right_channel + top_channels + bottom_channels + left_channels),
            ((row >= ((height * c) / sum_right_channels) && row < ((height * (c + 1)) / sum_right_channels) && col >= center_x) ? right: 0));

      if (center_channel)
        insert_weight((top_channels + bottom_channels + left_channels + right_channels), 255);

      if (top_left_channel)
      {
        int t = (col < (width / sum_top_channels) && row < center_y) ? top: 0;
        int l = (row < (height / sum_left_channels) && col < center_x) ? left: 0;
        insert_weight((top_channels + bottom_channels + left_channels + right_channels + center_channel),
            ((t > l) ? t: l));
      }

      if (top_right_channel)
      {
        int t = (col >= ((width * (top_channels + top_left_channel)) / sum_top_channels) && row < center_y) ? top: 0;
        int r = (row < (height / sum_right_channels) && col >= center_x) ? right: 0;
        insert_weight((top_channels + bottom_channels + left_channels + right_channels + center_channel + top_left_channel),
            (t > r) ? t: r);
      }

      if (bottom_left_channel)
      {
        int b = (col < (width / sum_bottom_channels) && row >= center_y) ? bottom: 0;
        int l = (row >= ((height * (left_channels + top_left_channel)) / sum_left_channels) && col < center_x) ? left: 0;
        insert_weight((top_channels + bottom_channels + left_channels + right_channels + center_channel + top_left_channel + top_right_channel),
            (b > l) ? b: l);
      }

      if (bottom_right_channel)
      {
        int b = (col >= ((width * (bottom_channels + bottom_left_channel)) / sum_bottom_channels) && row >= center_y) ? bottom: 0;
        int r = (row >= ((height * (right_channels + top_right_channel)) / sum_right_channels) && col >= center_x) ? right: 0;
        insert_weight((top_channels + bottom_channels + left_channels + right_channels + center_channel + top_left_channel + top_right_channel + bottom_left_channel),
            (b > r) ? b: r);
      }

      ++pos;
    }
  }
  self->weight_tab_end = wt;

  pos = (wt - self->weight_tab);
  c = pos + n + 1;
  wt = (weight_tab_t *) realloc(self->weight_tab, c * sizeof(weight_tab_t));
  if (wt)
  {
    self->weight_tab = wt;
    self->weight_tab_end = wt + pos;
    self->weight_tab_size = c;
  }
}


static void calc_hue_hist(atmo_driver_t *self) {
  weight_tab_t *wt = self->weight_tab;
  weight_tab_t * const wte = self->weight_tab_end;
  hsv_color_t * const hsv_img = self->hsv_img;
  uint64_t * const hue_hist = self->active_parm.hue_win_size ? self->hue_hist: self->w_hue_hist;
  const int darkness_limit = self->active_parm.darkness_limit;

  memset(hue_hist, 0, (self->sum_channels * (h_MAX+1) * sizeof(uint64_t)));

  while (wt < wte) {
    hsv_color_t *hsv = hsv_img + wt->pos;
    if (hsv->v >= darkness_limit)
      hue_hist[wt->channel * (h_MAX+1) + hsv->h] += wt->weight * hsv->v;
    ++wt;
  }
}


static void calc_windowed_hue_hist(atmo_driver_t *self) {
  int i, c, w;
  const int n = self->sum_channels;
  uint64_t * const hue_hist = self->hue_hist;
  uint64_t * const w_hue_hist = self->w_hue_hist;
  const int hue_win_size = self->active_parm.hue_win_size;
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


static void calc_most_used_hue(atmo_driver_t *self) {
  const int n = self->sum_channels;
  uint64_t * const w_hue_hist = self->w_hue_hist;
  int * const most_used_hue = self->most_used_hue;
  int * const last_most_used_hue = self->last_most_used_hue;
  const double hue_threshold = (double)self->active_parm.hue_threshold / 100.0;
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


static void calc_sat_hist(atmo_driver_t *self) {
  weight_tab_t *wt = self->weight_tab;
  weight_tab_t * const wte = self->weight_tab_end;
  hsv_color_t * const hsv_img = self->hsv_img;
  uint64_t * const sat_hist = self->active_parm.sat_win_size ? self->sat_hist: self->w_sat_hist;
  int * const most_used_hue = self->most_used_hue;
  const int darkness_limit = self->active_parm.darkness_limit;
  const int hue_win_size = self->active_parm.hue_win_size;

  memset(sat_hist, 0, (self->sum_channels * (s_MAX+1) * sizeof(uint64_t)));

  while (wt < wte) {
    hsv_color_t *hsv = hsv_img + wt->pos;
    if (hsv->v >= darkness_limit) {
      int h = hsv->h;
      int c = wt->channel;
      if (h >= (most_used_hue[c] - hue_win_size) && h <= (most_used_hue[c] + hue_win_size))
        sat_hist[c * (s_MAX+1) + hsv->s] += wt->weight * hsv->v;
    }
    ++wt;
  }
}


static void calc_windowed_sat_hist(atmo_driver_t *self) {
  int i, c, w;
  const int n = self->sum_channels;
  uint64_t * const sat_hist = self->sat_hist;
  uint64_t * const w_sat_hist = self->w_sat_hist;
  const int sat_win_size = self->active_parm.sat_win_size;
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


static void calc_most_used_sat(atmo_driver_t *self) {
  const int n = self->sum_channels;
  uint64_t * const w_sat_hist = self->w_sat_hist;
  int * const most_used_sat = self->most_used_sat;
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


static void calc_average_brightness(atmo_driver_t *self) {
  weight_tab_t *wt = self->weight_tab;
  weight_tab_t * const wte = self->weight_tab_end;
  hsv_color_t * const hsv_img = self->hsv_img;
  const int n = self->sum_channels;
  const int darkness_limit = self->active_parm.darkness_limit;
  const uint64_t bright = self->active_parm.brightness;
  uint64_t * const avg_bright = self->avg_bright;
  int * const avg_cnt = self->avg_cnt;
  int c;

  memset(avg_bright, 0, (n * sizeof(uint64_t)));
  memset(avg_cnt, 0, (n * sizeof(int)));

  while (wt < wte) {
    hsv_color_t *hsv = hsv_img + wt->pos;
    if (hsv->v >= darkness_limit) {
      avg_bright[wt->channel] += hsv->v * wt->weight;
      avg_cnt[wt->channel] += wt->weight;
    }
    ++wt;
  }

  for (c = 0; c < n; ++c) {
    if (avg_cnt[c]) {
      avg_bright[c] = (avg_bright[c] * bright) / (avg_cnt[c] * ((uint64_t)100));
      if (avg_bright[c] > v_MAX)
        avg_bright[c] = v_MAX;
    }
  }
}


static void calc_uniform_average_brightness(atmo_driver_t *self) {
  hsv_color_t *hsv = self->hsv_img;
  int img_size = self->img_size;
  const int darkness_limit = self->active_parm.darkness_limit;
  uint64_t avg = 0;
  int cnt = 0;
  uint64_t * const avg_bright = self->avg_bright;
  int c = self->sum_channels;

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

  avg = (avg * self->active_parm.brightness) / 100;
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


static void calc_rgb_values(atmo_driver_t *self) {
  const int n = self->sum_channels;
  int c;

  for (c = 0; c < n; ++c)
    hsv_to_rgb(&self->analyzed_colors[c], (double)self->most_used_hue[c], (double)self->most_used_sat[c], (double)self->avg_bright[c]);
}


static int configure_analyze_size(atmo_driver_t *self, int width, int height) {
  int size = width * height;
  int edge_weighting = self->active_parm.edge_weighting;

    /* allocate hsv and weight images */
  if (size > self->alloc_img_size) {
    free(self->hsv_img);
    free(self->weight_tab);
    self->alloc_img_size = 0;
    self->hsv_img = (hsv_color_t *) malloc(size * sizeof(hsv_color_t));
    self->weight_tab_size = size;
    self->weight_tab = (weight_tab_t *) malloc(size * sizeof(weight_tab_t));
    if (self->hsv_img == NULL || self->weight_tab == NULL) {
      DFATMO_LOG(DFLOG_ERROR, "allocating image memory failed!");
      return 1;
    }
    self->alloc_img_size = size;
    self->analyze_width = 0;
    self->analyze_height = 0;
    self->edge_weighting = 0;
  }
  self->img_size = size;

    /* calculate weight image */
  if (width != self->analyze_width || height != self->analyze_height || edge_weighting != self->edge_weighting) {
    self->edge_weighting = edge_weighting;
    self->analyze_width = width;
    self->analyze_height = height;
    calc_weight(self);
    DFATMO_LOG(DFLOG_INFO, "analyze size %dx%d, weight tab size %d", width, height, (int)(self->weight_tab_end - self->weight_tab));
  }

  return 0;
}


static void free_analyze_images (atmo_driver_t *self) {
  free(self->hsv_img);
  free(self->weight_tab);
  free(self->delay_filter_queue);
}


static void reset_filters (atmo_driver_t *self) {
  self->old_mean_length = 0;
  self->filter_delay = -1;
}


static void percent_filter(atmo_driver_t *self, rgb_color_t *act) {
  rgb_color_t *out = self->filtered_colors;
  const int old_p = self->active_parm.filter_smoothness;
  const int new_p = 100 - old_p;
  int n = self->sum_channels;

  if (self->old_mean_length) {
    while (n--) {
      out->r = (act->r * new_p + out->r * old_p) / 100;
      out->g = (act->g * new_p + out->g * old_p) / 100;
      out->b = (act->b * new_p + out->b * old_p) / 100;
      ++act;
      ++out;
    }
  } else {
    self->old_mean_length = -1;
    memcpy(out, act, n * sizeof(rgb_color_t));
  }
}


static void mean_filter(atmo_driver_t *self, rgb_color_t *act) {
  rgb_color_t *out = self->filtered_colors;
  rgb_color_t *mean_values = self->mean_filter_values;
  rgb_color_sum_t *mean_sums = self->mean_filter_sum_values;
  const double mean_threshold = self->active_parm.filter_threshold * 4.4167;
  const int old_p = self->active_parm.filter_smoothness;
  const int new_p = 100 - old_p;
  const int filter_length = self->active_parm.filter_length;
  const int output_rate = self->active_parm.output_rate;
  const int mean_length = (output_rate <= 0 || filter_length <= output_rate) ? 1: filter_length / output_rate;
  const int max_sum = mean_length * 255;
  const int reinitialize = (mean_length != self->old_mean_length);
  int n = self->sum_channels;
  int dr, dg, db;
  double dist;

  self->old_mean_length = mean_length;

  while (n--) {
    mean_sums->r += (act->r - mean_values->r);
    if (mean_sums->r < 0)
      mean_sums->r = 0;
    else if (mean_sums->r > max_sum)
      mean_sums->r = max_sum;
    mean_values->r = mean_sums->r / mean_length;

    mean_sums->g += (act->g - mean_values->g);
    if (mean_sums->g < 0)
      mean_sums->g = 0;
    else if (mean_sums->g > max_sum)
      mean_sums->g = max_sum;
    mean_values->g = mean_sums->g / mean_length;

    mean_sums->b += (act->b - mean_values->b);
    if (mean_sums->b < 0)
      mean_sums->b = 0;
    else if (mean_sums->b > max_sum)
      mean_sums->b = max_sum;
    mean_values->b = mean_sums->b / mean_length;

      /*
       * check, if there is a jump -> check if differences between actual values and filter values are too big
       */
    dr = (act->r - mean_values->r);
    dg = (act->g - mean_values->g);
    db = (act->b - mean_values->b);
    dist = (dr * dr + dg * dg + db * db);
    if (dist > 0.0)
      dist = sqrt(dist);

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
        /* apply additional percent filter */
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


static void apply_filters(atmo_driver_t *self) {
    /* Transfer analyzed colors into filtered colors */
  switch (self->active_parm.filter) {
  case FILTER_PERCENTAGE:
    percent_filter(self, self->analyzed_colors);
    break;
  case FILTER_COMBINED:
    mean_filter(self, self->analyzed_colors);
    break;
  default:
      /* no filtering */
    memcpy(self->filtered_colors, self->analyzed_colors, (self)->sum_channels * sizeof(rgb_color_t));
  }
}


static void apply_white_calibration(atmo_driver_t *self) {
  const int wc_red = self->active_parm.wc_red;
  const int wc_green = self->active_parm.wc_green;
  const int wc_blue = self->active_parm.wc_blue;

  if (wc_red < 255 || wc_green < 255 || wc_blue < 255) {
    rgb_color_t *out = self->filtered_output_colors;
    int n = self->sum_channels;
    while (n--) {
      out->r = (out->r * wc_red) / 255;
      out->g = (out->g * wc_green) / 255;
      out->b = (out->b * wc_blue) / 255;
      ++out;
    }
  }
}


static void apply_gamma_correction(atmo_driver_t *self) {
  const int igamma = self->active_parm.gamma;
  if (igamma > 10)
  {
    const double gamma = igamma / 10.0;
    rgb_color_t *out = self->filtered_output_colors;
    int n = self->sum_channels;
    while (n--) {
      out->r = (uint8_t)(pow((out->r / 255.0), gamma) * 255.0);
      out->g = (uint8_t)(pow((out->g / 255.0), gamma) * 255.0);
      out->b = (uint8_t)(pow((out->b / 255.0), gamma) * 255.0);
      ++out;
    }
  }
}


static int apply_delay_filter (atmo_driver_t *self) {
  int filter_delay = self->active_parm.filter_delay;
  int output_rate = self->active_parm.output_rate;
  int colors_size = self->sum_channels * sizeof(rgb_color_t);
  int outp;

    /* Initialize delay filter queue */
  if (self->filter_delay != filter_delay || self->output_rate != output_rate) {
    free(self->delay_filter_queue);
    self->filter_delay = -1;
    self->delay_filter_queue_length = ((filter_delay >= output_rate) ? filter_delay / output_rate + 1: 0) * self->sum_channels;
    if (self->delay_filter_queue_length) {
      self->delay_filter_queue = (rgb_color_t *) calloc(self->delay_filter_queue_length, sizeof(rgb_color_t));
      if (self->delay_filter_queue == NULL) {
        DFATMO_LOG(DFLOG_ERROR, "allocating delay filter queue failed!");
        return 1;
      }
    }
    else
      self->delay_filter_queue = NULL;

    self->filter_delay = filter_delay;
    self->output_rate = output_rate;
    self->delay_filter_queue_pos = 0;
  }

    /* Transfer filtered colors to output colors */
  if (self->delay_filter_queue) {
    outp = self->delay_filter_queue_pos + self->sum_channels;
    if (outp >= self->delay_filter_queue_length)
      outp = 0;

    memcpy(&self->delay_filter_queue[self->delay_filter_queue_pos], self->filtered_colors, colors_size);
    memcpy(self->filtered_output_colors, &self->delay_filter_queue[outp], colors_size);

    self->delay_filter_queue_pos = outp;
  } else
    memcpy(self->filtered_output_colors, self->filtered_colors, colors_size);

  return 0;
}


static int send_output_colors (atmo_driver_t *self, rgb_color_t *output_colors, int initial) {
  int colors_size = self->sum_channels * sizeof(rgb_color_t);
  int rc = 0;

  if (initial || memcmp(output_colors, self->last_output_colors, colors_size)) {
    rc = self->output_driver->output_colors(self->output_driver, output_colors, initial ? NULL: self->last_output_colors);
    if (rc)
      DFATMO_LOG(DFLOG_ERROR, "output driver error: %s", self->output_driver->errmsg);
    else
      memcpy(self->last_output_colors, output_colors, colors_size);
  }

  return rc;
}


static int turn_lights_off (atmo_driver_t *self) {
  memset(self->output_colors, 0, (self->sum_channels * sizeof(rgb_color_t)));
  return send_output_colors(self, self->output_colors, 0);
}


static int config_channels(atmo_driver_t *self) {
  int n = self->parm.top + self->parm.bottom + self->parm.left + self->parm.right +
          self->parm.center +
          self->parm.top_left + self->parm.top_right + self->parm.bottom_left + self->parm.bottom_right;
  self->sum_channels = n;

  if (n < 1) {
    DFATMO_LOG(DFLOG_ERROR, "no channels configured!");
    return 1;
  }

  self->hue_hist = (uint64_t *) calloc(n * (h_MAX + 1), sizeof(uint64_t));
  self->w_hue_hist = (uint64_t *) calloc(n * (h_MAX + 1), sizeof(uint64_t));
  self->most_used_hue = (int *) calloc(n, sizeof(int));
  self->last_most_used_hue = (int *) calloc(n, sizeof(int));

  self->sat_hist = (uint64_t *) calloc(n * (s_MAX + 1), sizeof(uint64_t));
  self->w_sat_hist = (uint64_t *) calloc(n * (s_MAX + 1), sizeof(uint64_t));
  self->most_used_sat = (int *) calloc(n, sizeof(int));

  self->avg_cnt = (int *) calloc(n, sizeof(int));
  self->avg_bright = (uint64_t *) calloc(n, sizeof(uint64_t));

  self->analyzed_colors = (rgb_color_t *) calloc(n, sizeof(rgb_color_t));
  self->filtered_colors = (rgb_color_t *) calloc(n, sizeof(rgb_color_t));
  self->filtered_output_colors = (rgb_color_t *) calloc(n, sizeof(rgb_color_t));
  self->output_colors = (rgb_color_t *) calloc(n, sizeof(rgb_color_t));
  self->last_output_colors = (rgb_color_t *) calloc(n, sizeof(rgb_color_t));
  self->mean_filter_values = (rgb_color_t *) calloc(n, sizeof(rgb_color_t));
  self->mean_filter_sum_values = (rgb_color_sum_t *) calloc(n, sizeof(rgb_color_sum_t));

  if (!(self->hue_hist &&
      self->w_hue_hist &&
      self->most_used_hue &&
      self->last_most_used_hue &&
      self->sat_hist &&
      self->w_sat_hist &&
      self->most_used_sat &&
      self->avg_cnt &&
      self->avg_bright &&
      self->analyzed_colors &&
      self->filtered_colors &&
      self->filtered_output_colors &&
      self->output_colors &&
      self->last_output_colors &&
      self->mean_filter_values &&
      self->mean_filter_sum_values)) {
    DFATMO_LOG(DFLOG_ERROR, "channel configuration fails!");
    return 1;
  }

  return 0;
}


static void free_channels(atmo_driver_t *self) {
  if (self->sum_channels)
  {
    FREE_AND_SET_NULL(self->hue_hist);
    FREE_AND_SET_NULL(self->w_hue_hist);
    FREE_AND_SET_NULL(self->most_used_hue);
    FREE_AND_SET_NULL(self->last_most_used_hue);

    FREE_AND_SET_NULL(self->sat_hist);
    FREE_AND_SET_NULL(self->w_sat_hist);
    FREE_AND_SET_NULL(self->most_used_sat);

    FREE_AND_SET_NULL(self->avg_cnt);
    FREE_AND_SET_NULL(self->avg_bright);

    FREE_AND_SET_NULL(self->analyzed_colors);
    FREE_AND_SET_NULL(self->filtered_colors);
    FREE_AND_SET_NULL(self->filtered_output_colors);
    FREE_AND_SET_NULL(self->output_colors);
    FREE_AND_SET_NULL(self->last_output_colors);
    FREE_AND_SET_NULL(self->mean_filter_values);
    FREE_AND_SET_NULL(self->mean_filter_sum_values);

    self->sum_channels = 0;
  }
}


static int null_driver_open(output_driver_t *self_gen, atmo_parameters_t *p) {
  return 0;
}

static int null_driver_configure(output_driver_t *self_gen, atmo_parameters_t *p) {
  return 0;
}

static int null_driver_close(output_driver_t *self_gen) {
  return 0;
}

static void null_driver_dispose(output_driver_t *self_gen) {
  free(self_gen);
}

static int null_driver_output_colors(output_driver_t *self_gen, rgb_color_t *colors, rgb_color_t *last_colors) {
  return 0;
}


static void unload_output_driver(atmo_driver_t *self) {
  if (self->output_driver) {
    self->output_driver->dispose(self->output_driver);
    self->output_driver = NULL;
  }
  if (self->output_driver_lib) {
    FREE_LIBRARY(self->output_driver_lib);
    self->output_driver_lib = NULL;
    DFATMO_LOG(DFLOG_INFO, "output driver unloaded");
  }
}


static int load_output_driver(atmo_driver_t *self) {
  char filename[256];
  int found;
  char *p;
  dfatmo_new_output_driver_t new_output_driver;
  lib_handle_t lib;
  lib_error_t err;
  output_driver_t *ot;

  if (!strlen(self->parm.driver))
    strcpy(self->parm.driver, "null");

  if (!strcmp(self->parm.driver, "null")) {
    self->output_driver = (output_driver_t *) calloc(1, sizeof(output_driver_t));
    if (!self->output_driver) {
      DFATMO_LOG(DFLOG_ERROR, "creating null output driver instance failed");
      return 1;
    }
    self->output_driver->version = DFATMO_OUTPUT_DRIVER_VERSION;
    self->output_driver->open = null_driver_open;
    self->output_driver->configure = null_driver_configure;
    self->output_driver->close = null_driver_close;
    self->output_driver->dispose = null_driver_dispose;
    self->output_driver->output_colors = null_driver_output_colors;
    DFATMO_LOG(DFLOG_INFO, "output driver %s loaded", self->parm.driver);
    return 0;
  }

  if (strlen(self->parm.driver_path) == 0) {
    DFATMO_LOG(DFLOG_ERROR, "output driver search path missing");
    return 1;
  }

  found = 0;
  p = self->parm.driver_path;
  while (p != NULL) {
    char *e = strchr(p, LIB_SEARCH_PATH_SEP);
    if (e == NULL) {
      snprintf(filename, sizeof(filename), LIB_NAME_TEMPLATE, (int)strlen(p), p, self->parm.driver);
      p = e;
    } else {
      snprintf(filename, sizeof(filename), LIB_NAME_TEMPLATE, (int)(e - p), p, self->parm.driver);
      p = e + 1;
    }
    DFATMO_LOG(DFLOG_DEBUG, "search output driver '%s'", filename);
    if (FILE_LOADABLE(filename)) {
      found = 1;
      break;
    }
  }
  if (!found) {
    DFATMO_LOG(DFLOG_ERROR, "output driver 'dfatmo-%s' not found", self->parm.driver);
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
    DFATMO_LOG(DFLOG_ERROR, "loading output driver failed: %s", buf);
    if (lib != NULL)
      FREE_LIBRARY(lib);
    return 1;
  }

  ot = (*new_output_driver)(dfatmo_log_level, dfatmo_log);
  if (ot == NULL) {
    DFATMO_LOG(DFLOG_ERROR, "creating output driver instance of '%s' failed", filename);
    FREE_LIBRARY(lib);
    return 1;
  }

  if (ot->version != DFATMO_OUTPUT_DRIVER_VERSION) {
    DFATMO_LOG(DFLOG_ERROR, "wrong version %d of output driver '%s'. Expected version %d", ot->version, filename, DFATMO_OUTPUT_DRIVER_VERSION);
    ot->dispose(ot);
    FREE_LIBRARY(lib);
    return 1;
  }

  self->output_driver = ot;
  self->output_driver_lib = lib;

  DFATMO_LOG(DFLOG_INFO, "output driver %s loaded", self->parm.driver);

  return 0;
}


static int open_output_driver (atmo_driver_t *self) {
  int rc = 0;

  if (!self->driver_opened) {
    if (self->output_driver == NULL)
      rc = load_output_driver(self);

    if (!rc) {
      rc = self->output_driver->open(self->output_driver, &self->parm);
      if (rc)
        DFATMO_LOG(DFLOG_ERROR, "output driver error: %s", self->output_driver->errmsg);
      else {
        self->driver_opened = 1;
        DFATMO_LOG(DFLOG_INFO, "output driver opened");
      }
    }
  } else {
    rc = self->output_driver->configure(self->output_driver, &self->parm);
    if (rc)
      DFATMO_LOG(DFLOG_ERROR, "output driver error: %s", self->output_driver->errmsg);
    else
      DFATMO_LOG(DFLOG_INFO, "output driver reconfigured");
  }
  return rc;
}


static int close_output_driver (atmo_driver_t *self) {
  int rc = 0;

  if (self->driver_opened) {
    turn_lights_off(self);
    self->driver_opened = 0;
    rc = self->output_driver->close(self->output_driver);
    if (rc)
      DFATMO_LOG(DFLOG_ERROR, "output driver error: %s", self->output_driver->errmsg);
    else
      DFATMO_LOG(DFLOG_INFO, "output driver closed");
  }
  return rc;
}


static void instant_configure (atmo_driver_t *self) {
  self->active_parm.overscan = self->parm.overscan;
  self->active_parm.darkness_limit = self->parm.darkness_limit;
  self->active_parm.edge_weighting = self->parm.edge_weighting;
  self->active_parm.hue_win_size = self->parm.hue_win_size;
  self->active_parm.sat_win_size = self->parm.sat_win_size;
  self->active_parm.hue_threshold = self->parm.hue_threshold;
  self->active_parm.uniform_brightness = self->parm.uniform_brightness;
  self->active_parm.brightness = self->parm.brightness;
  self->active_parm.filter = self->parm.filter;
  self->active_parm.filter_smoothness = self->parm.filter_smoothness;
  self->active_parm.filter_length = self->parm.filter_length;
  self->active_parm.filter_threshold = self->parm.filter_threshold;
  self->active_parm.filter_delay = self->parm.filter_delay;
  self->active_parm.wc_red = self->parm.wc_red;
  self->active_parm.wc_green = self->parm.wc_green;
  self->active_parm.wc_blue = self->parm.wc_blue;
  self->active_parm.gamma = self->parm.gamma;
  self->active_parm.output_rate = self->parm.output_rate;
  self->active_parm.analyze_size = self->parm.analyze_size;
}


static void init_configuration (atmo_driver_t *self)
{
  memset(self, 0, sizeof(atmo_driver_t));

    /* Set default values for parameters */
  strcpy(self->parm.driver, "null");
#ifdef OUTPUT_DRIVER_PATH
  strcpy(self->parm.driver_path, OUTPUT_DRIVER_PATH);
#endif
  self->parm.brightness = 100;
  self->parm.darkness_limit = 1;
  self->parm.edge_weighting = 60;
  self->parm.filter = FILTER_COMBINED;
  self->parm.filter_length = 500;
  self->parm.filter_smoothness = 50;
  self->parm.filter_threshold = 40;
  self->parm.hue_win_size = 3;
  self->parm.sat_win_size = 3;
  self->parm.hue_threshold = 93;
  self->parm.wc_red = 255;
  self->parm.wc_green = 255;
  self->parm.wc_blue = 255;
  self->parm.output_rate = 20;
  self->parm.gamma = 10;
  self->parm.analyze_rate = 35;
  self->parm.analyze_size = 1;
  self->parm.start_delay = 250;
  self->parm.enabled = 1;
}

#ifndef trNOOP
#define trNOOP(x) x
#endif

int *dfatmo_driver_log_level;


static const char *filter_enum[NUM_FILTERS] = { trNOOP("off"), trNOOP("percentage"), trNOOP("combined") };
static const char *analyze_size_enum[4] = { "64", "128", "192", "256" };

#define PARM_DESC_LIST \
PARM_DESC_BOOL(enabled, NULL, 0, 1, 0, trNOOP("Launch on startup")) \
PARM_DESC_CHAR(driver, NULL, 0, 0, 0, trNOOP("Output driver name")) \
PARM_DESC_CHAR(driver_param, NULL, 0, 0, 0, trNOOP("Driver parameters")) \
PARM_DESC_CHAR(driver_path, NULL, 0, 0, 0, trNOOP("Output driver search path")) \
PARM_DESC_INT(top, NULL, 0, MAX_BORDER_CHANNELS, 0, trNOOP("Sections at top area")) \
PARM_DESC_INT(bottom, NULL, 0, MAX_BORDER_CHANNELS, 0, trNOOP("Sections at bottom area")) \
PARM_DESC_INT(left, NULL, 0, MAX_BORDER_CHANNELS, 0, trNOOP("Sections at left area")) \
PARM_DESC_INT(right, NULL, 0, MAX_BORDER_CHANNELS, 0, trNOOP("Sections at right area")) \
PARM_DESC_BOOL(center, NULL, 0, 1, 0, trNOOP("Activate center area")) \
PARM_DESC_BOOL(top_left, NULL, 0, 1, 0, trNOOP("Activate top left area")) \
PARM_DESC_BOOL(top_right, NULL, 0, 1, 0, trNOOP("Activate top right area")) \
PARM_DESC_BOOL(bottom_left, NULL, 0, 1, 0, trNOOP("Activate bottom left area")) \
PARM_DESC_BOOL(bottom_right, NULL, 0, 1, 0, trNOOP("Activate bottom right area")) \
PARM_DESC_INT(analyze_rate, NULL, 10, 500, 0, trNOOP("Analyze rate [ms]")) \
PARM_DESC_INT(analyze_size, analyze_size_enum, 0, 3, 0, trNOOP("Size of analyze image")) \
PARM_DESC_INT(overscan, NULL, 0, 200, 0, trNOOP("Ignored overscan border [%1000]")) \
PARM_DESC_INT(darkness_limit, NULL, 0, 100, 0, trNOOP("Limit for black pixel")) \
PARM_DESC_INT(edge_weighting, NULL, 10, 200, 0, trNOOP("Power of edge weighting")) \
PARM_DESC_INT(hue_win_size, NULL, 0, 5, 0, trNOOP("Hue windowing size")) \
PARM_DESC_INT(sat_win_size, NULL, 0, 5, 0, trNOOP("Saturation windowing size")) \
PARM_DESC_INT(hue_threshold, NULL, 0, 100, 0, trNOOP("Hue threshold [%]")) \
PARM_DESC_INT(brightness, NULL, 50, 300, 0, trNOOP("Brightness [%]")) \
PARM_DESC_BOOL(uniform_brightness, NULL, 0, 1, 0, trNOOP("Uniform brightness mode")) \
PARM_DESC_INT(filter, filter_enum, 0, (NUM_FILTERS-1), 0, trNOOP("Filter mode")) \
PARM_DESC_INT(filter_smoothness, NULL, 1, 100, 0, trNOOP("Filter smoothness [%]")) \
PARM_DESC_INT(filter_length, NULL, 300, 5000, 0, trNOOP("Filter length [ms]")) \
PARM_DESC_INT(filter_threshold, NULL, 1, 100, 0, trNOOP("Filter threshold [%]")) \
PARM_DESC_INT(filter_delay, NULL, 0, 1000, 0, trNOOP("Output delay [ms]")) \
PARM_DESC_INT(output_rate, NULL, 10, 500, 0, trNOOP("Output rate [ms]")) \
PARM_DESC_INT(start_delay, NULL, 0, 5000, 0, trNOOP("Delay after stream start [ms]")) \
PARM_DESC_INT(wc_red, NULL, 0, 255, 0, trNOOP("Red white calibration")) \
PARM_DESC_INT(wc_green, NULL, 0, 255, 0, trNOOP("Green white calibration")) \
PARM_DESC_INT(wc_blue, NULL, 0, 255, 0, trNOOP("Blue white calibration")) \
PARM_DESC_INT(gamma, NULL, 0, 30, 0, trNOOP("Gamma correction"))
