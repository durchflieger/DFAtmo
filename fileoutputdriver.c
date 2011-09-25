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
 * This is the DFAtmo native file output driver.
 *
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>

#ifdef WIN32
#include <windows.h>
#define strerror_r(num, buf, size)	strerror_s(buf, size, num)
#else
#include <sys/time.h>
#endif

#include "dfatmo.h"


typedef struct {
  output_driver_t output_driver;
  atmo_parameters_t param;
  FILE *fd;
  int id;
} file_output_driver_t;


static int file_driver_open(output_driver_t *this_gen, atmo_parameters_t *p) {
  file_output_driver_t *this = (file_output_driver_t *) this_gen;

  this->param = *p;
  this->id = 0;

  if (this->param.driver_param && strlen(this->param.driver_param))
    this->fd = fopen(this->param.driver_param, "a");
  else
    this->fd = fopen("atmo_data.out", "a");

  if (this->fd == NULL) {
    strerror_r(errno, this->output_driver.errmsg, sizeof(this->output_driver.errmsg));
    return -1;
  }
  return 0;
}

static int file_driver_configure(output_driver_t *this_gen, atmo_parameters_t *p) {
  file_output_driver_t *this = (file_output_driver_t *) this_gen;
  this->param = *p;
  return 0;
}

static int file_driver_close(output_driver_t *this_gen) {
  file_output_driver_t *this = (file_output_driver_t *) this_gen;

  if (this->fd) {
    fclose(this->fd);
    this->fd = NULL;
  }
  return 0;
}

static void file_driver_dispose(output_driver_t *this_gen) {
  free(this_gen);
}

static int file_driver_output_colors(output_driver_t *this_gen, rgb_color_t *colors, rgb_color_t *last_colors) {
  file_output_driver_t *this = (file_output_driver_t *) this_gen;
  FILE *fd = this->fd;
  int c, secs, msecs;

  if (fd == NULL)
    return -1;

#ifdef WIN32
  {
    SYSTEMTIME t;
    GetSystemTime(&t);
    secs = (int)t.wSecond;
    msecs = (int)t.wMilliseconds;
  }
#else
  {
    struct timeval tvnow;
    gettimeofday(&tvnow, NULL);
    secs = (int)(tvnow.tv_sec % 60);
    msecs = (int)(tvnow.tv_usec / 1000);
  }
#endif
  fprintf(fd, "%d: %02d.%03d ---\n", this->id++, secs, msecs);

  for (c = 1; c <= this->param.top; ++c, ++colors)
    fprintf(fd,"      top %2d: %3d %3d %3d\n", c, colors->r, colors->g, colors->b);

  for (c = 1; c <= this->param.bottom; ++c, ++colors)
    fprintf(fd,"   bottom %2d: %3d %3d %3d\n", c, colors->r, colors->g, colors->b);

  for (c = 1; c <= this->param.left; ++c, ++colors)
    fprintf(fd,"     left %2d: %3d %3d %3d\n", c, colors->r, colors->g, colors->b);

  for (c = 1; c <= this->param.right; ++c, ++colors)
    fprintf(fd,"    right %2d: %3d %3d %3d\n", c, colors->r, colors->g, colors->b);

  if (this->param.center) {
    fprintf(fd,"      center: %3d %3d %3d\n", colors->r, colors->g, colors->b);
    ++colors;
  }
  if (this->param.top_left) {
    fprintf(fd,"    top left: %3d %3d %3d\n", colors->r, colors->g, colors->b);
    ++colors;
  }
  if (this->param.top_right) {
    fprintf(fd,"    top right: %3d %3d %3d\n", colors->r, colors->g, colors->b);
    ++colors;
  }
  if (this->param.bottom_left) {
    fprintf(fd,"  bottom left: %3d %3d %3d\n", colors->r, colors->g, colors->b);
    ++colors;
  }
  if (this->param.bottom_right) {
    fprintf(fd," bottom right: %3d %3d %3d\n", colors->r, colors->g, colors->b);
  }
  fflush(fd);
  if (ferror(fd)) {
    strerror_r(errno, this->output_driver.errmsg, sizeof(this->output_driver.errmsg));
    return -1;
  }
  return 0;
}


dfatmo_log_level_t dfatmo_log_level;
dfatmo_log_t dfatmo_log;

output_driver_t* dfatmo_new_output_driver(dfatmo_log_level_t log_level, dfatmo_log_t log_fn) {
  file_output_driver_t *d;

  if (dfatmo_log_level == NULL) {
    dfatmo_log_level = log_level;
    dfatmo_log = log_fn;
  }

  d = (file_output_driver_t *) calloc(1, sizeof(file_output_driver_t));
  if (d == NULL)
    return NULL;

  d->output_driver.version = DFATMO_OUTPUT_DRIVER_VERSION;
  d->output_driver.open = file_driver_open;
  d->output_driver.configure = file_driver_configure;
  d->output_driver.close = file_driver_close;
  d->output_driver.dispose = file_driver_dispose;
  d->output_driver.output_colors = file_driver_output_colors;
  return &d->output_driver;
}
