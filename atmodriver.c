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
 * This is the Python native DFAtmo driver module.
 */

#include <Python.h>
#include <structmember.h>

#include "atmodriver.h"

#define DFATMO_DRIVER_VERSION   1

/* supported pixel image formats */
#define IMG_FMT_RGBA    0
#define IMG_FMT_BGRA    1

typedef struct {
  PyObject_HEAD

  atmo_driver_t ad;
  int configured;

} py_atmo_driver_t;

static PyObject *atmo_error_exception;
static PyObject *log_cb;

#define CHECK_CONFIGURED(PAD)    if (! PAD->configured) { PyErr_SetString(atmo_error_exception, "driver not configured"); return NULL; }
#define CHECK_DRIVER_OPENED(AD) if (! AD->driver_opened) { PyErr_SetString(atmo_error_exception, "output driver closed"); return NULL; }


static int act_log_level = LOG_ERROR;
dfatmo_log_level_t dfatmo_log_level = &act_log_level;

static void driver_log(int level, const char *fmt, ...) {
  char buf[512];
  PyGILState_STATE gstate;

  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  gstate = PyGILState_Ensure();

  if (log_cb == NULL) {
    PySys_WriteStderr("DFAtmo: %s\n", buf);
  } else {
    PyObject *args = Py_BuildValue("is", level, buf);
    PyObject_CallObject(log_cb, args);
    Py_DECREF(args);
  }

  PyGILState_Release(gstate);
}

dfatmo_log_t dfatmo_log = &driver_log;


static PyObject *output_driver_error(atmo_driver_t *ad) {
  char buf[256];
  snprintf(buf, sizeof(buf), "output driver error: %s", ad->output_driver->errmsg);
  PyErr_SetString(atmo_error_exception, buf);
  return NULL;
}


static void calc_hsv_image_from_rgba(hsv_color_t *hsv, uint8_t *img, int pitch, int width, int height) {
  pitch *= 4;
  while (height--) {
    uint8_t *i = img;
    int w = width;
    while (w--) {
      rgb_to_hsv(hsv, i[0], i[1], i[2]);
      ++hsv;
      i += 4;
    }
    img += pitch;
  }
}


static void calc_hsv_image_from_bgra(hsv_color_t *hsv, uint8_t *img, int pitch, int width, int height) {
  pitch *= 4;
  while (height--) {
    uint8_t *i = img;
    int w = width;
    while (w--) {
      rgb_to_hsv(hsv, i[2], i[1], i[0]);
      ++hsv;
      i += 4;
    }
    img += pitch;
  }
}


static PyObject *analyze_image (py_atmo_driver_t *this, PyObject *args) {
  atmo_driver_t *ad = &this->ad;
  int img_width, img_height, img_format;
  PyObject *ba_img;
  int pixel_len;
  int img_size;
  int crop_width, crop_height, analyze_width, analyze_height;
  int overscan;
  uint8_t *img;
  int edge_weighting;
  Py_ssize_t colors_size;

  CHECK_CONFIGURED(this);

  if (!PyArg_ParseTuple(args, "iiiO!", &img_width, &img_height, &img_format, &PyByteArray_Type, &ba_img))
    return NULL;

  switch (img_format) {
  case IMG_FMT_RGBA:
  case IMG_FMT_BGRA:
    pixel_len = 4;
    break;
  default:
    PyErr_SetString(atmo_error_exception, "unknown image format");
    return NULL;
  }

  img_size = img_width * img_height;
  if (PyByteArray_Size(ba_img) < (img_size * pixel_len)) {
    PyErr_SetString(atmo_error_exception, "pixel buffer to small for image size");
    return NULL;
  }

    /* calculate size of analyze (sub) window */
  overscan = ad->active_parm.overscan;
  if (overscan) {
    crop_width = (img_width * overscan + 500) / 1000;
    crop_height = (img_height * overscan + 500) / 1000;
    analyze_width = img_width - 2 * crop_width;
    analyze_height = img_height - 2 * crop_height;
    img_size = analyze_width * analyze_height;
  } else {
    crop_width = 0;
    crop_height = 0;
    analyze_width = img_width;
    analyze_height = img_height;
  }

  if (analyze_width < 8 || analyze_height < 8 || analyze_width > img_width || analyze_height > img_height) {
    PyErr_SetString(atmo_error_exception, "illegal analyze window size");
    return NULL;
  }

    /* allocate hsv and weight images */
  if (img_size > ad->alloc_img_size) {
    free(ad->hsv_img);
    free(ad->weight);
    ad->alloc_img_size = 0;
    ad->hsv_img = (hsv_color_t *) malloc(img_size * sizeof(hsv_color_t));
    ad->weight = (uint8_t *) malloc(img_size * ad->sum_channels * sizeof(uint8_t));
    if (ad->hsv_img == NULL || ad->weight == NULL)
      return PyErr_NoMemory();
    ad->alloc_img_size = img_size;
    ad->analyze_width = 0;
    ad->analyze_height = 0;
    ad->edge_weighting = 0;
  }
  ad->img_size = img_size;

    /* convert to hsv image */
  img = (uint8_t *)PyByteArray_AsString(ba_img);
  img += (crop_height * img_width + crop_width) * pixel_len;
  switch (img_format) {
  case IMG_FMT_RGBA:
    calc_hsv_image_from_rgba(ad->hsv_img, img, img_width, analyze_width, analyze_height);
    break;
  case IMG_FMT_BGRA:
    calc_hsv_image_from_bgra(ad->hsv_img, img, img_width, analyze_width, analyze_height);
  }

  Py_BEGIN_ALLOW_THREADS

    /* calculate weight image */
  edge_weighting = ad->active_parm.edge_weighting;
  if (analyze_width != ad->analyze_width || analyze_height != ad->analyze_height || edge_weighting != ad->edge_weighting) {
    ad->edge_weighting = edge_weighting;
    ad->analyze_width = analyze_width;
    ad->analyze_height = analyze_height;
    calc_weight(ad);
    DFATMO_LOG(LOG_DEBUG, "image size: %dx%d, analyze window: %d,%d %dx%d", img_width, img_height, crop_width, crop_height, analyze_width, analyze_height);
  }

  calc_hue_hist(ad);
  calc_windowed_hue_hist(ad);
  calc_most_used_hue(ad);
  calc_sat_hist(ad);
  calc_windowed_sat_hist(ad);
  calc_most_used_sat(ad);
  if (ad->active_parm.uniform_brightness)
    calc_uniform_average_brightness(ad);
  else
    calc_average_brightness(ad);
  calc_rgb_values(ad);

  Py_END_ALLOW_THREADS

  colors_size = ad->sum_channels * sizeof(rgb_color_t);
  return PyByteArray_FromStringAndSize((const char *)ad->analyzed_colors, colors_size);
}


static PyObject *reset_filters_wrapper (py_atmo_driver_t *this, PyObject *args) {
  atmo_driver_t *ad = &this->ad;

  CHECK_CONFIGURED(this);

  reset_filters(ad);

  Py_INCREF(Py_None);
  return Py_None;
}


static PyObject *filter_analyzed_colors (py_atmo_driver_t *this, PyObject *args) {
  atmo_driver_t *ad = &this->ad;
  PyObject *ba_analyzed_colors;
  int colors_size;
  rgb_color_t *analyzed_colors;

  CHECK_CONFIGURED(this);

  if (!PyArg_ParseTuple(args, "O!", &PyByteArray_Type, &ba_analyzed_colors))
    return NULL;

  colors_size = ad->sum_channels * sizeof(rgb_color_t);
  if (PyByteArray_Size(ba_analyzed_colors) < colors_size) {
    PyErr_SetString(atmo_error_exception, "color buffer to small for configured number of channels");
    return NULL;
  }

  analyzed_colors = (rgb_color_t *) PyByteArray_AsString(ba_analyzed_colors);

    /* Transfer analyzed colors into filtered colors */
  switch (ad->active_parm.filter) {
  case FILTER_PERCENTAGE:
    percent_filter(ad, analyzed_colors);
    break;
  case FILTER_COMBINED:
    mean_filter(ad, analyzed_colors);
    break;
  default:
      /* no filtering */
    memcpy(ad->filtered_colors, analyzed_colors, colors_size);
  }

  return PyByteArray_FromStringAndSize((const char *)ad->filtered_colors, colors_size);
}


static PyObject *filter_output_colors (py_atmo_driver_t *this, PyObject *args) {
  atmo_driver_t *ad = &this->ad;
  PyObject *ba_output_colors;
  int colors_size;
  rgb_color_t *output_colors;
  int filter_delay;
  int output_rate;

  CHECK_CONFIGURED(this);

  if (!PyArg_ParseTuple(args, "O!", &PyByteArray_Type, &ba_output_colors))
    return NULL;

  colors_size = ad->sum_channels * sizeof(rgb_color_t);
  if (PyByteArray_Size(ba_output_colors) < colors_size) {
    PyErr_SetString(atmo_error_exception, "color buffer to small for configured number of channels");
    return NULL;
  }

  output_colors = (rgb_color_t *) PyByteArray_AsString(ba_output_colors);

    /* Initialize delay filter queue */
  filter_delay = ad->active_parm.filter_delay;
  output_rate = ad->active_parm.output_rate;
  if (ad->filter_delay != filter_delay || ad->output_rate != output_rate) {
    free(ad->delay_filter_queue);
    ad->filter_delay = -1;
    ad->delay_filter_queue_length = ((filter_delay >= output_rate) ? filter_delay / output_rate + 1: 0) * ad->sum_channels;
    if (ad->delay_filter_queue_length) {
      ad->delay_filter_queue = (rgb_color_t *) calloc(ad->delay_filter_queue_length, sizeof(rgb_color_t));
      if (ad->delay_filter_queue == NULL)
        return PyErr_NoMemory();
    }
    else
      ad->delay_filter_queue = NULL;
    ad->filter_delay = filter_delay;
    ad->output_rate = output_rate;
    ad->delay_filter_queue_pos = 0;
  }

    /* Transfer filtered colors to output colors */
  if (ad->delay_filter_queue) {
    int outp = ad->delay_filter_queue_pos + ad->sum_channels;
    if (outp >= ad->delay_filter_queue_length)
      outp = 0;

    memcpy(&ad->delay_filter_queue[ad->delay_filter_queue_pos], output_colors, colors_size);
    memcpy(ad->filtered_output_colors, &ad->delay_filter_queue[outp], colors_size);

    ad->delay_filter_queue_pos = outp;
  }
  else
    memcpy(ad->filtered_output_colors, output_colors, colors_size);

  Py_BEGIN_ALLOW_THREADS
  apply_gamma_correction(ad);
  apply_white_calibration(ad);
  Py_END_ALLOW_THREADS

  return PyByteArray_FromStringAndSize((const char *)ad->filtered_output_colors, colors_size);
}


static PyObject *output_colors (py_atmo_driver_t *this, PyObject *args) {
  atmo_driver_t *ad = &this->ad;
  PyObject *ba_output_colors;
  int colors_size;
  rgb_color_t *output_colors;

  CHECK_CONFIGURED(this);
  CHECK_DRIVER_OPENED(ad);

  if (!PyArg_ParseTuple(args, "O!", &PyByteArray_Type, &ba_output_colors))
    return NULL;

  colors_size = ad->sum_channels * sizeof(rgb_color_t);
  if (PyByteArray_Size(ba_output_colors) < colors_size) {
    PyErr_SetString(atmo_error_exception, "color buffer to small for configured number of channels");
    return NULL;
  }

  output_colors = (rgb_color_t *) PyByteArray_AsString(ba_output_colors);

  if (memcmp(output_colors, ad->last_output_colors, colors_size)) {
    memcpy(ad->output_colors, output_colors, colors_size);
    Py_BEGIN_ALLOW_THREADS
    if (ad->output_driver->output_colors(ad->output_driver, ad->output_colors, ad->last_output_colors)) {
      Py_BLOCK_THREADS
      return output_driver_error(ad);
    }
    memcpy(ad->last_output_colors, ad->output_colors, colors_size);
    Py_END_ALLOW_THREADS
  }

  Py_INCREF(Py_None);
  return Py_None;
}


static PyObject *configure (py_atmo_driver_t *this, PyObject *args) {
  atmo_driver_t *ad = &this->ad;
  int send = 0;

  this->configured = 0;

  if (strcmp(ad->active_parm.driver, ad->parm.driver) ||
        strcmp(ad->active_parm.driver_path, ad->parm.driver_path) ||
        strcmp(ad->active_parm.driver_param, ad->parm.driver_param)) {
    if (close_output_driver(ad))
      return output_driver_error(ad);

    if (strcmp(ad->active_parm.driver, ad->parm.driver))
      unload_output_driver(ad);
  }

    /* open output driver */
  if (!ad->driver_opened) {
    if (ad->output_driver == NULL) {
      if (load_output_driver(ad)) {
        PyErr_SetString(atmo_error_exception, "loading output driver fails");
        return NULL;
      }
    }

    if (ad->output_driver->open(ad->output_driver, &ad->parm))
      return output_driver_error(ad);

    ad->driver_opened = 1;
    send = 1;
  } else if (ad->output_driver->configure(ad->output_driver, &ad->parm))
    return output_driver_error(ad);

  if (ad->active_parm.top != ad->parm.top ||
                  ad->active_parm.bottom != ad->parm.bottom ||
                  ad->active_parm.left != ad->parm.left ||
                  ad->active_parm.right != ad->parm.right ||
                  ad->active_parm.center != ad->parm.center ||
                  ad->active_parm.top_left != ad->parm.top_left ||
                  ad->active_parm.top_right != ad->parm.top_right ||
                  ad->active_parm.bottom_left != ad->parm.bottom_left ||
                  ad->active_parm.bottom_right != ad->parm.bottom_right) {
    free_channels(ad);
    if (config_channels(ad))
      return PyErr_NoMemory();
    send = 1;
  }
  if (ad->sum_channels < 1) {
    PyErr_SetString(atmo_error_exception, "no channels configured");
    return NULL;
  }

  ad->active_parm = ad->parm;
  this->configured = 1;

    /* send first initial color packet */
  if (send) {
    Py_BEGIN_ALLOW_THREADS
    if (ad->output_driver->output_colors(ad->output_driver, ad->last_output_colors, NULL)) {
      Py_BLOCK_THREADS
      return output_driver_error(ad);
    }
    Py_END_ALLOW_THREADS
  }

  Py_INCREF(Py_None);
  return Py_None;
}


static PyObject *instant_configure_wrapper (py_atmo_driver_t *this, PyObject *args, PyObject *keywds) {
  atmo_driver_t *ad = &this->ad;

  instant_configure(ad);

  Py_INCREF(Py_None);
  return Py_None;
}


static PyObject *close_output_driver_wrapper (py_atmo_driver_t *this, PyObject *args) {
  atmo_driver_t *ad = &this->ad;

  Py_BEGIN_ALLOW_THREADS
  if (close_output_driver(ad)) {
    Py_BLOCK_THREADS
    return output_driver_error(ad);
  }
  Py_END_ALLOW_THREADS

  Py_INCREF(Py_None);
  return Py_None;
}


static PyObject *turn_lights_off_wrapper (py_atmo_driver_t *this, PyObject *args) {
  atmo_driver_t *ad = &this->ad;

  CHECK_CONFIGURED(this);
  CHECK_DRIVER_OPENED(ad);

  Py_BEGIN_ALLOW_THREADS
  if (turn_lights_off(ad)) {
    Py_BLOCK_THREADS
    return output_driver_error(ad);
  }
  Py_END_ALLOW_THREADS

  Py_INCREF(Py_None);
  return Py_None;
}


#define INTATTR(NAME, MIN, MAX) \
  static PyObject *get_ ## NAME (py_atmo_driver_t *this, void *closure) { \
    return Py_BuildValue("i", this->ad.parm.NAME ); \
  } \
  \
  static int set_ ## NAME (py_atmo_driver_t *this, PyObject *value, void *closure) { \
    long v; \
    if (value == NULL) { \
      PyErr_SetString(PyExc_TypeError, "Cannot delete '" #NAME "' attribute"); \
      return -1; \
    } \
    v = PyInt_AsLong(value); \
    if (v == -1 && PyErr_Occurred()) { \
      PyErr_SetString(PyExc_TypeError, "The '" #NAME "' attribute value must be a integer"); \
      return -1; \
    } \
    if (v < (MIN) || v > (MAX)) { \
        PyErr_SetString(atmo_error_exception, "'" #NAME "' attribute value not in range " #MIN " ... " #MAX ); \
        return -1; \
    } \
    this->ad.parm.NAME = v; \
    return 0; \
  }

#define STRATTR(NAME) \
  static PyObject *get_ ## NAME (py_atmo_driver_t *this, void *closure) { \
    return Py_BuildValue("s", this->ad.parm.NAME ); \
  } \
  \
  static int set_ ## NAME (py_atmo_driver_t *this, PyObject *value, void *closure) { \
    char *v; \
    atmo_driver_t *ad = &this->ad; \
    if (value == NULL) { \
      PyErr_SetString(PyExc_TypeError, "Cannot delete '" #NAME "' attribute"); \
      return -1; \
    } \
    v = PyString_AsString(value); \
    if (v == NULL) { \
      PyErr_SetString(PyExc_TypeError, "The '" #NAME "' attribute value must be a string"); \
      return -1; \
    } \
    if (PyString_Size(value) > (sizeof(ad->parm.NAME)-1)) { \
      char buf[128]; \
      snprintf(buf, sizeof(buf), "Maximum string length for '" #NAME "' attribute is %d",  (int)(sizeof(ad->parm.NAME)-1)); \
      PyErr_SetString(PyExc_TypeError, buf); \
      return -1; \
    } \
    strcpy(ad->parm.NAME, v); \
    return 0; \
  }

STRATTR(driver);
STRATTR(driver_param);
STRATTR(driver_path);
INTATTR(top, 0, MAX_BORDER_CHANNELS);
INTATTR(bottom, 0, MAX_BORDER_CHANNELS);
INTATTR(left, 0, MAX_BORDER_CHANNELS);
INTATTR(right, 0, MAX_BORDER_CHANNELS);
INTATTR(center, 0, 1);
INTATTR(top_left, 0, 1);
INTATTR(top_right, 0, 1);
INTATTR(bottom_left, 0, 1);
INTATTR(bottom_right, 0, 1);
INTATTR(overscan, 0, 200);
INTATTR(darkness_limit, 0, 100);
INTATTR(edge_weighting, 10, 200);
INTATTR(hue_win_size, 0, 5);
INTATTR(sat_win_size, 0, 5);
INTATTR(hue_threshold, 0, 100);
INTATTR(uniform_brightness, 0, 1);
INTATTR(brightness, 50, 300);
INTATTR(filter, 0, NUM_FILTERS);
INTATTR(filter_smoothness, 1, 100);
INTATTR(filter_length, 300, 5000);
INTATTR(filter_threshold, 1, 100);
INTATTR(filter_delay, 0, 1000);
INTATTR(wc_red, 0, 255);
INTATTR(wc_green, 0, 255);
INTATTR(wc_blue, 0, 255);
INTATTR(gamma, 0, 30);
INTATTR(output_rate, 10, 1000);
INTATTR(analyze_rate, 10, 500);
INTATTR(analyze_size, 0, 3);
INTATTR(start_delay, 0, 5000);
INTATTR(enabled, 0, 1);

#define DEFGETSET(NAME, DESC) { #NAME , (getter)get_ ## NAME , (setter)set_ ## NAME , DESC , NULL }

static PyGetSetDef atmo_driver_getseters[] = {
  DEFGETSET(driver, "output driver"),
  DEFGETSET(driver_param, "parameters for output driver"),
  DEFGETSET(driver_path, "output driver search path"),
  DEFGETSET(top, "number of areas at top border"),
  DEFGETSET(bottom, "number of areas at bottom border"),
  DEFGETSET(left, "number of areas at left border"),
  DEFGETSET(right, "number of areas at right border"),
  DEFGETSET(center, "activate center area"),
  DEFGETSET(top_left, "activate top_left area"),
  DEFGETSET(top_right, "activate top_right area"),
  DEFGETSET(bottom_left, "activate bottom_left area"),
  DEFGETSET(bottom_right, "activate bottom right area"),
  DEFGETSET(overscan, "ignored overscan border of grabbed image [%1000]"),
  DEFGETSET(darkness_limit, "limit for black pixel"),
  DEFGETSET(edge_weighting, "power of edge weighting"),
  DEFGETSET(hue_win_size, "hue windowing size"),
  DEFGETSET(sat_win_size, "saturation windowing size"),
  DEFGETSET(hue_threshold, "hue threshold [%]"),
  DEFGETSET(uniform_brightness, "calculate uniform brightness"),
  DEFGETSET(brightness, "brightness [%]"),
  DEFGETSET(filter, "filter mode"),
  DEFGETSET(filter_smoothness, "filter smoothness [%]"),
  DEFGETSET(filter_length, "filter length [ms]"),
  DEFGETSET(filter_threshold, "filter threshold [%]"),
  DEFGETSET(filter_delay, "delay for output send to controller [ms]"),
  DEFGETSET(wc_red, "white calibration correction factor of red color channel"),
  DEFGETSET(wc_green, "white calibration correction factor of green color channel"),
  DEFGETSET(wc_blue, "white calibration correction factor of blue color channel"),
  DEFGETSET(gamma, "gamma correction factor"),
  DEFGETSET(output_rate, "color output rate [ms]"),
  DEFGETSET(analyze_rate, "analyze rate [ms]"),
  DEFGETSET(analyze_size, "size of analyze image"),
  DEFGETSET(start_delay, "delay after stream start before first output is send [ms]"),
  DEFGETSET(enabled, "enable addon"),
  { NULL }
};


static PyObject *get_parm (PyObject *this_gen, PyObject *args) {
  char *name;
  if (!PyArg_ParseTuple(args, "s", &name))
    return NULL;

  if (name != NULL) {
    PyGetSetDef *p = atmo_driver_getseters;
    while (p->name) {
      if (strcmp(p->name, name) == 0)
        return (*p->get)(this_gen, p->closure);
      ++p;
    }
  }

  PyErr_SetString(PyExc_TypeError, "Parameter unknown");
  return NULL;
}


static PyObject *set_parm (PyObject *this_gen, PyObject *args) {
  char *name;
  PyObject *v;

  if (!PyArg_ParseTuple(args, "sO", &name, &v))
    return NULL;

  if (name != NULL) {
    PyGetSetDef *p = atmo_driver_getseters;
    while (p->name) {
      if (strcmp(p->name, name) == 0) {
        if ((*p->set)(this_gen, v, p->closure))
           return NULL;
        Py_INCREF(Py_None);
        return Py_None;
      }
      ++p;
    }
  }

  PyErr_SetString(PyExc_TypeError, "Parameter unknown");
  return NULL;
}


static PyObject* atmo_driver_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
  py_atmo_driver_t *this = (py_atmo_driver_t *)type->tp_alloc(type, 0);
  if (this) {
    atmo_driver_t *ad = &this->ad;

    init_configuration(ad);
    reset_filters(ad);
  }

  return (PyObject*)this;
}


static void atmo_driver_dealloc(py_atmo_driver_t *this)
{
  atmo_driver_t *ad = &this->ad;

  close_output_driver(ad);
  unload_output_driver(ad);
  free_channels(ad);
  free(ad->hsv_img);
  free(ad->weight);
  free(ad->delay_filter_queue);

  this->ob_type->tp_free((PyObject*)this);
}


static PyMethodDef atmo_driver_methods[] = {
  {"analyzeImage", (PyCFunction)analyze_image, METH_VARARGS, "analyzeImage(width,height,imgFormat,img) -- Analyze captured image"},
  {"resetFilters", (PyCFunction)reset_filters_wrapper, METH_VARARGS, "resetFilters() -- Reset all filters."},
  {"filterAnalyzedColors", (PyCFunction)filter_analyzed_colors, METH_VARARGS, "filterAnalyzedColors(analyzedColors) -- Apply percent/mean filters."},
  {"filterOutputColors", (PyCFunction)filter_output_colors, METH_VARARGS, "filterOutputColors(outputColors) -- Apply delay/white/gamma filters."},
  {"outputColors", (PyCFunction)output_colors, METH_VARARGS, "outputColors(outputColors) -- Output colors to controller devices."},
  {"configure", (PyCFunction)configure, METH_VARARGS, "configure() -- Configure driver with applied attributes." },
  {"instantConfigure", (PyCFunction)instant_configure_wrapper, METH_VARARGS, "instantConfigure() -- Configure only the instant attributes of driver"},
  {"turnLightsOff", (PyCFunction)turn_lights_off_wrapper, METH_VARARGS, "turnLightsOff() -- output all black color packet."},
  {"closeOutputDriver", (PyCFunction)close_output_driver_wrapper, METH_VARARGS, "closeOutputDriver() -- close all resources hold by output driver."},
  {"getParm", (PyCFunction)get_parm, METH_VARARGS, "getParm(name) -- get value of parameter."},
  {"setParm", (PyCFunction)set_parm, METH_VARARGS, "setParm(name, value) -- set value of parameter."},
  {NULL, NULL, 0, NULL}
};


static PyTypeObject atmo_driver_type = { PyObject_HEAD_INIT(NULL) };


static PyObject *get_driver_version (PyObject *this, PyObject *args) {
  return Py_BuildValue("i", DFATMO_DRIVER_VERSION);
}


static PyObject *set_log_level (PyObject *this, PyObject *args) {
  PyObject *cb = NULL;
  if (!PyArg_ParseTuple(args, "i|O:set_callback", &act_log_level, &cb))
    return NULL;

  if (cb != NULL) {
    Py_XDECREF(log_cb);
    if (cb == Py_None)
      log_cb = NULL;
    else {
      if (!PyCallable_Check(cb)) {
        PyErr_SetString(PyExc_TypeError, "log function is not callable");
        return NULL;
      }
      Py_INCREF(cb);
      log_cb = cb;
    }
  }

  Py_INCREF(Py_None);
  return Py_None;
}


static PyMethodDef atmo_methods[] = {
  {"setLogLevel", (PyCFunction)set_log_level, METH_VARARGS, "setLogLevel(level [, log-function]) -- Set logging level and log function"},
  {"getDriverVersion", (PyCFunction)get_driver_version, METH_VARARGS, "getDriverVersion() -- Returns version of driver"},
  {NULL, NULL, 0, NULL}
};


PyMODINIT_FUNC initatmodriver(void)
{
  PyObject *m;

  atmo_error_exception = PyErr_NewException("atmodriver.error", NULL, NULL);
  if (atmo_error_exception == NULL)
    return;

  atmo_driver_type.tp_name = "atmodriver.AtmoDriver";
  atmo_driver_type.tp_basicsize = sizeof(py_atmo_driver_t);
  atmo_driver_type.tp_dealloc = (destructor)atmo_driver_dealloc;
  atmo_driver_type.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE;
  atmo_driver_type.tp_doc = "Atmolight driver type.";
  atmo_driver_type.tp_methods = atmo_driver_methods;
  atmo_driver_type.tp_getset = atmo_driver_getseters;
  atmo_driver_type.tp_base = 0;
  atmo_driver_type.tp_new = atmo_driver_new;
  if (PyType_Ready(&atmo_driver_type) < 0)
      return;

  m = Py_InitModule3("atmodriver", atmo_methods, "Native Atmolight driver.");
  if (m == NULL)
    return;

  Py_INCREF(atmo_error_exception);
  PyModule_AddObject(m, "error", atmo_error_exception);

  Py_INCREF(&atmo_driver_type);
  PyModule_AddObject(m, "AtmoDriver", (PyObject *)&atmo_driver_type);

  PyModule_AddIntConstant(m, "IMAGE_FORMAT_RGBA", IMG_FMT_RGBA);
  PyModule_AddIntConstant(m, "IMAGE_FORMAT_BGRA", IMG_FMT_BGRA);

  PyModule_AddIntConstant(m, "FILTER_NONE", FILTER_NONE);
  PyModule_AddIntConstant(m, "FILTER_PERCENTAGE", FILTER_PERCENTAGE);
  PyModule_AddIntConstant(m, "FILTER_COMBINED", FILTER_COMBINED);

  PyModule_AddIntConstant(m, "LOG_DEBUG", LOG_DEBUG);
  PyModule_AddIntConstant(m, "LOG_INFO", LOG_INFO);
  PyModule_AddIntConstant(m, "LOG_ERROR", LOG_ERROR);
  PyModule_AddIntConstant(m, "LOG_NONE", LOG_NONE);
}
