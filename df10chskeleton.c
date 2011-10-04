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
 * This is a example skeleton for DF10CH how to program against the output driver interface.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "dfatmo.h"

static int act_log_level = LOG_ERROR;

static void driver_log(int level, const char *fmt, ...) {
  if (level <= act_log_level) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
  }
}


main() {
  output_driver_t *driver;
  atmo_parameters_t *parm;
  rgb_color_t *colors, *last_colors;
  int num_areas, colors_size;

    // Create a new output driver instance.
    // The driver needs a log handler to output some helpful
    // log messages.
  driver = dfatmo_new_output_driver(&act_log_level, &driver_log);

    // Open output driver. For DF10CH this means finding all controllers
    // at the USB bus and reading in the configuration.
  parm = calloc(1, sizeof(atmo_parameters_t));
  if (driver->open(driver, parm)) {
    driver_log(LOG_ERROR, "Could not open driver: %s\n", driver->errmsg);
  }

    // The driver open call returned the areas and some more parameters
    // that are configured with the DF10CH setup program. Beside the areas these
    // parameters are: overscan, analyze_size and edge_weighting.
    // Using the last three parameters by the application is up to you.

    // Calculate number of areas.
  num_areas = parm->top + parm->bottom + parm->left + parm->right + parm->center + parm->top_left + parm->top_right + parm->bottom_left + parm->bottom_right;
  if (num_areas < 0) {
    driver_log(LOG_ERROR, "Controller not configured! Please use DF10CH setup for configuration.\n");
  }

    // Allocate colors array for configured number of areas.
  colors = calloc(num_areas, sizeof(rgb_color_t));
  last_colors = calloc(num_areas, sizeof(rgb_color_t));
  colors_size = num_areas * sizeof(rgb_color_t);

    // Turn off all lights. (colors is initialized to 0)
  if (driver->output_colors(driver, colors, NULL)) {
    driver_log(LOG_ERROR, "Error while sending color data: %s\n", driver->errmsg);
  }
  memcpy(last_colors, colors, colors_size);

    // Set some color values.
    // Order for 'colors' is: top 1,2,3..., bottom 1,2,3..., left 1,2,3..., right 1,2,3..., center, top left, top right, bottom left, bottom right
  colors[0].r = 255; colors[0].g = 255; colors[0].b = 255;

    // Output colors. The output driver uses last_colors to optimize
    // the amount of data that is send to the controller.
  if (driver->output_colors(driver, colors, last_colors)) {
    driver_log(LOG_ERROR, "Error while sending color data: %s\n", driver->errmsg);
  }
  memcpy(last_colors, colors, colors_size);

    // Close output driver releasing all devices.
  if (driver->close(driver)) {
    driver_log(LOG_ERROR, "Error while closing output driver: %s\n", driver->errmsg);
  }

    // Destroy driver instance.
  driver->dispose(driver);
}
