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
 */

#pragma once

#ifdef WIN32
#define HIDDEN_SYM
#else
#define HIDDEN_SYM              __attribute__ ((visibility("hidden")))
#endif

enum { DFLOG_NONE = 0, DFLOG_ERROR, DFLOG_INFO, DFLOG_DEBUG };
#define DFATMO_LOG(ll, ...)     do { if ((ll) <= *dfatmo_log_level) (*dfatmo_log)(ll, __VA_ARGS__); } while (0)
#define IS_LOG_LEVEL(ll)        ((ll) <= *dfatmo_log_level)

#define FREE_AND_SET_NULL(VAR)  { free(VAR); VAR = NULL; }

#define NUM_AREAS                9      /* Number of different areas (top, bottom ...) */
#define MAX_BORDER_CHANNELS     128     /* Maximum number of channels for a border */
#define SIZE_DRIVER_NAME        16      /* Maximum size of driver name */
#define SIZE_DRIVER_PATH        128     /* Maximum size of driver path */
#define SIZE_DRIVER_PARAM       2048    /* Maximum size of driver parameter string */

typedef struct { uint8_t r, g, b; } rgb_color_t;

typedef struct {
  char driver[SIZE_DRIVER_NAME];
  char driver_param[SIZE_DRIVER_PARAM];
  char driver_path[SIZE_DRIVER_PATH];
  int top;
  int bottom;
  int left;
  int right;
  int center;
  int top_left;
  int top_right;
  int bottom_left;
  int bottom_right;
  int overscan;
  int darkness_limit;
  int edge_weighting;
  int weight_limit;
  int hue_win_size;
  int sat_win_size;
  int hue_threshold;
  int uniform_brightness;
  int brightness;
  int filter;
  int filter_smoothness;
  int filter_length;
  int filter_threshold;
  int filter_delay;
  int wc_red;
  int wc_green;
  int wc_blue;
  int gamma;
  int output_rate;
  int analyze_rate;
  int analyze_size;
  int start_delay;
  int enabled;
} atmo_parameters_t;

/*
 * abstraction for output drivers
 */
#define DFATMO_OUTPUT_DRIVER_VERSION    3

typedef struct output_driver_s output_driver_t;
struct output_driver_s {
    /* version of driver interface */
  uint32_t version;

    /* open device and configure for number of channels */
  int (*open)(output_driver_t *self, atmo_parameters_t *param);

    /* instant configure */
  int (*configure)(output_driver_t *self, atmo_parameters_t *param);

    /* close device */
  int (*close)(output_driver_t *self);

    /* dispose driver */
  void (*dispose)(output_driver_t *self);

    /*
     * send RGB color values to device
     * last_colors is NULL when first initial color packet is send
     * order for 'colors' is: top 1,2,3..., bottom 1,2,3..., left 1,2,3..., right 1,2,3..., center, top left, top right, bottom left, bottom right
     */
  int (*output_colors)(output_driver_t *self, rgb_color_t *new_colors, rgb_color_t *last_colors);

    /* provide detailed error message here if open of device fails */
  char errmsg[128];
};

typedef int* dfatmo_log_level_t;
typedef void (*dfatmo_log_t)(int level, const char *fmt, ...);
typedef output_driver_t* (*dfatmo_new_output_driver_t)(dfatmo_log_level_t log_level, dfatmo_log_t log_fn);

extern dfatmo_log_t dfatmo_log HIDDEN_SYM;
extern dfatmo_log_level_t dfatmo_log_level HIDDEN_SYM;
extern output_driver_t* dfatmo_new_output_driver(dfatmo_log_level_t log_level, dfatmo_log_t log_fn);

