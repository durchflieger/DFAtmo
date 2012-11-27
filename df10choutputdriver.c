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
 * This is the DFAtmo native output driver for my own designed DF10CH "next generation" 10ch RGB Controller.
 *
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <libusb.h>

#ifdef WIN32
#include <windows.h>
#define snprintf _snprintf
#else
#include <unistd.h>
#include <sys/time.h>
#endif

#ifndef LIBUSB_CALL
#define LIBUSB_CALL
#endif

#include "df10ch_usb_proto.h"
#include "dfatmo.h"

#define DF10CH_USB_CFG_VENDOR_ID     0x16c0
#define DF10CH_USB_CFG_PRODUCT_ID    0x05dc
#define DF10CH_USB_CFG_VENDOR_NAME   "yak54@gmx.net"
#define DF10CH_USB_CFG_PRODUCT       "DF10CH"
#define DF10CH_USB_CFG_SERIAL        "AP"
#define DF10CH_USB_DEFAULT_TIMEOUT   100

#define DF10CH_MAX_CHANNELS     30
#define DF10CH_SIZE_CONFIG      (18 + DF10CH_MAX_CHANNELS * 6)
#define DF10CH_CONFIG_VALID_ID  0xA0A1

enum { DF10CH_AREA_TOP, DF10CH_AREA_BOTTOM, DF10CH_AREA_LEFT, DF10CH_AREA_RIGHT, DF10CH_AREA_CENTER, DF10CH_AREA_TOP_LEFT, DF10CH_AREA_TOP_RIGHT, DF10CH_AREA_BOTTOM_LEFT, DF10CH_AREA_BOTTOM_RIGHT };

typedef struct df10ch_gamma_tab_s {
  struct df10ch_gamma_tab_s *next;
  uint8_t gamma;
  uint16_t white_cal;
  uint16_t tab[256];
} df10ch_gamma_tab_t;

typedef struct {
  int req_channel;          // Channel number in request
  int area;                 // Source area
  int area_num;             // Source area number
  int color;                // Source color
  df10ch_gamma_tab_t *gamma_tab;  // Corresponding gamma table
} df10ch_channel_config_t;

typedef struct df10ch_output_driver_s df10ch_output_driver_t;

typedef struct df10ch_ctrl_s {
  struct df10ch_ctrl_s *next;
  df10ch_output_driver_t *driver;
  libusb_device_handle *dev;
  int idx_serial_number;        // USB string index of serial number
  uint16_t config_version;      // Version number of configuration data
  uint16_t pwm_res;             // PWM resolution
  int num_req_channels;         // Number of channels in request
  df10ch_channel_config_t *channel_config;      // List of channel configurations
  char id[32];                  // ID of Controller
  struct libusb_transfer *transfer; // Prepared set brightness request for asynchrony submitting
  uint8_t *transfer_data;       // Data of set brightness request
  int pending_submit;           // Is true if a asynchrony transfer is pending
  int transfer_error;
} df10ch_ctrl_t;

struct df10ch_output_driver_s {
  output_driver_t output_driver;
  libusb_context *ctx;
  atmo_parameters_t param;          // Global channel layout
  df10ch_ctrl_t *ctrls;             // List of found controllers
  df10ch_gamma_tab_t *gamma_tabs;   // List of calculated gamma tables
  uint16_t config_version;          // (Maximum) Version number of configuration data
  int max_transmit_latency;
  int avg_transmit_latency;
  int transfer_err_cnt;             // Number of transfer errors
};

#ifndef HAVE_LIBUSB_STRERROR
static const char *libusb_strerror(int rc) {
  switch (rc) {
  case LIBUSB_SUCCESS:
    return ("Success (no error)");
  case LIBUSB_ERROR_IO:
    return ("Input/output error");
  case LIBUSB_ERROR_INVALID_PARAM:
    return ("Invalid parameter");
  case LIBUSB_ERROR_ACCESS:
    return ("Access denied (insufficient permissions)");
  case LIBUSB_ERROR_NO_DEVICE:
    return ("No such device (it may have been disconnected)");
  case LIBUSB_ERROR_NOT_FOUND:
    return ("Entity not found");
  case LIBUSB_ERROR_BUSY:
    return ("Resource busy");
  case LIBUSB_ERROR_TIMEOUT:
    return ("Operation timed out");
  case LIBUSB_ERROR_OVERFLOW:
    return ("Overflow");
  case LIBUSB_ERROR_PIPE:
    return ("Pipe error");
  case LIBUSB_ERROR_INTERRUPTED:
    return ("System call interrupted (perhaps due to signal)");
  case LIBUSB_ERROR_NO_MEM:
    return ("Insufficient memory");
  case LIBUSB_ERROR_NOT_SUPPORTED:
    return ("Operation not supported or unimplemented on this platform");
  case LIBUSB_ERROR_OTHER:
    return ("Other error");
  }
  return ("?");
}
#endif

static const char * df10ch_usb_transfer_errmsg(int s) {
  switch (s) {
  case LIBUSB_TRANSFER_COMPLETED:
    return ("Transfer completed without error");
  case LIBUSB_TRANSFER_ERROR:
    return ("Transfer failed");
  case LIBUSB_TRANSFER_TIMED_OUT:
    return ("Transfer timed out");
  case LIBUSB_TRANSFER_CANCELLED:
    return ("Transfer was cancelled");
  case LIBUSB_TRANSFER_STALL:
    return ("Control request stalled");
  case LIBUSB_TRANSFER_NO_DEVICE:
    return ("Device was disconnected");
  case LIBUSB_TRANSFER_OVERFLOW:
    return ("Device sent more data than requested");
  }
  return ("?");
}


static void df10ch_comm_errmsg(int stat, char *rc) {
  if (stat == 0)
    strcpy(rc, "OK");
  else
    *rc = 0;
  if (stat & (1<<COMM_ERR_OVERRUN))
      strcat(rc, " OVERRUN");
  if (stat & (1<<COMM_ERR_FRAME))
      strcat(rc, " FRAME");
  if (stat & (1<<COMM_ERR_TIMEOUT))
      strcat(rc, " TIMEOUT");
  if (stat & (1<<COMM_ERR_START))
      strcat(rc, " START");
  if (stat & (1<<COMM_ERR_OVERFLOW))
      strcat(rc, " OVERFLOW");
  if (stat & (1<<COMM_ERR_CRC))
      strcat(rc, " CRC");
  if (stat & (1<<COMM_ERR_DUPLICATE))
      strcat(rc, " DUPLICATE");
  if (stat & (1<<COMM_ERR_DEBUG))
      strcat(rc, " DEBUG");
}


static int df10ch_control_in_transfer(df10ch_ctrl_t *ctrl, uint8_t req, uint16_t val, uint16_t index, unsigned int timeout, uint8_t *buf, uint16_t buflen) {
    // Use a return buffer always so that the controller is able to send a USB reply status
    // This is special for VUSB at controller side
  unsigned char rcbuf[1];
  int len = buflen;
  int n = 0, retrys = 0;

  if (!len) {
      buf = rcbuf;
      len = 1;
  }

    // Because VUSB at controller sends ACK reply before CRC check of received data we have to retry sending request our self if data is corrupted
  while (retrys < 3) {
      n = libusb_control_transfer(ctrl->dev, LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE, req, val, index, buf, len, timeout);
      if (n != LIBUSB_ERROR_INTERRUPTED)
      {
          if (n < 0)
            ++ctrl->driver->transfer_err_cnt;
          if (n >= 0 || n != LIBUSB_ERROR_PIPE)
              break;
          ++retrys;
          DFATMO_LOG(DFLOG_ERROR, "%s: sending USB control transfer message %d failed (pipe error): retry %d", ctrl->id, req, retrys);
      }
  }

  if (n < 0) {
      DFATMO_LOG(DFLOG_ERROR, "%s: sending USB control transfer message %d failed: %s", ctrl->id, req, libusb_strerror(n));
      return -1;
  }

  if (n != buflen) {
      DFATMO_LOG(DFLOG_ERROR, "%s: sending USB control transfer message %d failed: read %d bytes but expected %d bytes", ctrl->id, req, n, buflen);
      return -1;
  }

  return 0;
}


static void df10ch_dispose(df10ch_output_driver_t *this) {
  df10ch_ctrl_t *ctrl = this->ctrls;
  df10ch_gamma_tab_t *gt = this->gamma_tabs;

  while (ctrl) {
    df10ch_ctrl_t *next;

	libusb_free_transfer(ctrl->transfer);
    libusb_release_interface(ctrl->dev, 0);
    libusb_close(ctrl->dev);

    next = ctrl->next;
    free(ctrl->transfer_data);
    free(ctrl->channel_config);
    free(ctrl);
    ctrl = next;
  }

  if (this->ctx)
    libusb_exit(this->ctx);

  while (gt) {
    df10ch_gamma_tab_t *next = gt->next;
    free(gt);
    gt = next;
  }

  this->ctrls = NULL;
  this->ctx = NULL;
  this->gamma_tabs = NULL;
}


static int df10ch_wait_for_replys(df10ch_output_driver_t *this) {
  df10ch_ctrl_t *ctrl = this->ctrls;
  struct timeval timeout;

	// wait for end of all pending transfers
  timeout.tv_sec = 0;
  timeout.tv_usec = (DF10CH_USB_DEFAULT_TIMEOUT + 50) * 1000;
  while (ctrl) {
    if (ctrl->pending_submit) {
      int rc = libusb_handle_events_timeout(this->ctx, &timeout);
      if (rc && rc != LIBUSB_ERROR_INTERRUPTED) {
        snprintf(this->output_driver.errmsg, sizeof(this->output_driver.errmsg), "%s: handling USB events failed: %s", ctrl->id, libusb_strerror(rc));
        return -1;
      }
    }
    else
      ctrl = ctrl->next;
  }

  ctrl = this->ctrls;
  while (ctrl) {
    if (ctrl->transfer_error) {
      char reply_errmsg[128], request_errmsg[128];
      uint8_t data[1];
      if (df10ch_control_in_transfer(ctrl, REQ_GET_REPLY_ERR_STATUS, 0, 0, DF10CH_USB_DEFAULT_TIMEOUT, data, 1))
        strcpy(reply_errmsg, "N/A");
      else
        df10ch_comm_errmsg(data[0], reply_errmsg);
      if (df10ch_control_in_transfer(ctrl, PWM_REQ_GET_REQUEST_ERR_STATUS, 0, 0, DF10CH_USB_DEFAULT_TIMEOUT, data, 1))
        strcpy(request_errmsg, "N/A");
      else
        df10ch_comm_errmsg(data[0], request_errmsg);
      DFATMO_LOG(DFLOG_ERROR, "%s: comm error USB: %s, PWM: %s", ctrl->id, reply_errmsg, request_errmsg);
    }
    ctrl = ctrl->next;
  }

  return 0;
}


static void LIBUSB_CALL df10ch_reply_cb(struct libusb_transfer *transfer) {
  df10ch_ctrl_t *ctrl = (df10ch_ctrl_t *) transfer->user_data;
  ctrl->pending_submit = 0;
  if (transfer->status != LIBUSB_TRANSFER_COMPLETED && transfer->status != LIBUSB_TRANSFER_CANCELLED) {
    ++ctrl->driver->transfer_err_cnt;
    ctrl->transfer_error = 1;
    DFATMO_LOG(DFLOG_ERROR, "%s: submitting USB control transfer message failed: %s\n", ctrl->id, df10ch_usb_transfer_errmsg(transfer->status));
  }
}


static int df10ch_driver_open(output_driver_t *this_gen, atmo_parameters_t *param) {
  df10ch_output_driver_t *this = (df10ch_output_driver_t *) this_gen;
  libusb_device **list = NULL;
  size_t cnt;
  int rc;
  size_t i;
  df10ch_ctrl_t *ctrl;

  this->config_version = 0;
  this->max_transmit_latency = 0;
  this->avg_transmit_latency = 0;
  this->transfer_err_cnt = 0;

  if (libusb_init(&this->ctx) < 0) {
    strcpy(this->output_driver.errmsg, "can't initialize USB library");
    return -1;
  }

  cnt = libusb_get_device_list(this->ctx, &list);
  if (cnt < 0) {
    snprintf(this->output_driver.errmsg, sizeof(this->output_driver.errmsg), "getting list of USB devices failed: %s", libusb_strerror(cnt));
    df10ch_dispose(this);
    return -1;
  }

    // Note: Because controller uses obdev's free USB product/vendor ID's we have to do special lookup for finding
    // the controllers. See file "USB-IDs-for-free.txt" of VUSB distribution.
  for (i = 0; i < cnt; i++) {
    libusb_device *d = list[i];
    struct libusb_device_descriptor desc;

    int busnum = libusb_get_bus_number(d);
    int devnum = libusb_get_device_address(d);

    rc = libusb_get_device_descriptor(d, &desc);
    if (rc < 0) {
      DFATMO_LOG(DFLOG_ERROR, "USB[%d,%d]: getting USB device descriptor failed: %s", busnum, devnum, libusb_strerror(rc));
    }
    else if (desc.idVendor == DF10CH_USB_CFG_VENDOR_ID && desc.idProduct == DF10CH_USB_CFG_PRODUCT_ID) {
      libusb_device_handle *hdl = NULL;
      rc = libusb_open(d, &hdl);
      if (rc < 0) {
        DFATMO_LOG(DFLOG_ERROR, "USB[%d,%d]: open of USB device failed: %s", busnum, devnum, libusb_strerror(rc));
      }
      else {
        unsigned char buf[256];
        rc = libusb_get_string_descriptor_ascii(hdl, desc.iManufacturer, buf, sizeof(buf));
        if (rc < 0) {
          DFATMO_LOG(DFLOG_ERROR, "USB[%d,%d]: getting USB manufacturer string failed: %s", busnum, devnum, libusb_strerror(rc));
        }
        else if (rc == sizeof(DF10CH_USB_CFG_VENDOR_NAME) - 1 && !memcmp(buf, DF10CH_USB_CFG_VENDOR_NAME, rc)) {
          rc = libusb_get_string_descriptor_ascii(hdl, desc.iProduct, buf, sizeof(buf));
          if (rc < 0) {
            DFATMO_LOG(DFLOG_ERROR, "USB[%d,%d]: getting USB product string failed: %s", busnum, devnum, libusb_strerror(rc));
          }
          else if (rc == sizeof(DF10CH_USB_CFG_PRODUCT) - 1 && !memcmp(buf, DF10CH_USB_CFG_PRODUCT, rc)) {
            char id[32];
            snprintf(id, sizeof(id), "DF10CH[%d,%d]", busnum, devnum);
            rc = libusb_set_configuration(hdl, 1);
            if (rc < 0) {
              DFATMO_LOG(DFLOG_ERROR, "%s: setting USB configuration failed: %s", id, libusb_strerror(rc));
            }
            else {
              rc = libusb_claim_interface(hdl, 0);
              if (rc < 0) {
                DFATMO_LOG(DFLOG_ERROR, "%s: claiming USB interface failed: %s", id, libusb_strerror(rc));
              }
              else {
                df10ch_ctrl_t *ctrl = (df10ch_ctrl_t *) calloc(1, sizeof(df10ch_ctrl_t));
                ctrl->next = this->ctrls;
                this->ctrls = ctrl;
                ctrl->driver = this;
                ctrl->dev = hdl;
                ctrl->idx_serial_number = desc.iSerialNumber;
                strcpy(ctrl->id, id);
                DFATMO_LOG(DFLOG_INFO, "%s: device opened", id);
                continue;
              }
            }
          }
        }
        libusb_close(hdl);
      }
    }
  }

  libusb_free_device_list(list, 1);

  if (!this->ctrls) {
    strcpy(this->output_driver.errmsg, "USB: no DF10CH devices found!");
    df10ch_dispose(this);
    return -1;
  }

    // Ignore channel configuration defined by plugin parameters
  param->top = 0;
  param->bottom = 0;
  param->left = 0;
  param->right = 0;
  param->center = 0;
  param->top_left = 0;
  param->top_right = 0;
  param->bottom_left = 0;
  param->bottom_right = 0;

    // Read controller configuration
  ctrl = this->ctrls;
  while (ctrl) {
    uint8_t data[256];
    uint8_t eedata[DF10CH_SIZE_CONFIG];
    int cfg_valid_id;
    int n;
    int nch;
    df10ch_channel_config_t *ccfg;
    int eei;

      // Check that USB controller is running application firmware and not bootloader
    rc = libusb_get_string_descriptor_ascii(ctrl->dev, ctrl->idx_serial_number, data, sizeof(data) - 1);
    if (rc < 0) {
      snprintf(this->output_driver.errmsg, sizeof(this->output_driver.errmsg), "%s: getting USB serial number string failed: %s", ctrl->id, libusb_strerror(rc));
      df10ch_dispose(this);
      return -1;
    }
    if (rc != sizeof(DF10CH_USB_CFG_SERIAL) - 1 || memcmp(data, DF10CH_USB_CFG_SERIAL, rc)) {
      data[rc] = 0;
      snprintf(this->output_driver.errmsg, sizeof(this->output_driver.errmsg), "%s: application firmware of USB controller is not running! Current mode is: %s", ctrl->id, data);
      df10ch_dispose(this);
      return -1;
    }

      // check that PWM controller is running application firmware and not bootloader
    if (df10ch_control_in_transfer(ctrl, PWM_REQ_GET_VERSION, 0, 0, DF10CH_USB_DEFAULT_TIMEOUT, data, 2)) {
      snprintf(this->output_driver.errmsg, sizeof(this->output_driver.errmsg), "%s: reading PWM controller version fails!", ctrl->id);
      df10ch_dispose(this);
      return -1;
    }
    if (data[0] != PWM_VERS_APPL) {
      snprintf(this->output_driver.errmsg, sizeof(this->output_driver.errmsg), "%s: application firmware of PWM controller is not running! Current mode is: %d", ctrl->id, data[0]);
      df10ch_dispose(this);
      return -1;
    }

      // read eeprom configuration data
    if (df10ch_control_in_transfer(ctrl, REQ_READ_EE_DATA, 0, 1, DF10CH_USB_DEFAULT_TIMEOUT, eedata, sizeof(eedata))) {
      snprintf(this->output_driver.errmsg, sizeof(this->output_driver.errmsg), "%s: reading eeprom config data fails!", ctrl->id);
      df10ch_dispose(this);
      return -1;
    }

      // check that configuration data is valid
    cfg_valid_id = eedata[0] + (eedata[1] << 8);
    if (cfg_valid_id != DF10CH_CONFIG_VALID_ID) {
      snprintf(this->output_driver.errmsg, sizeof(this->output_driver.errmsg), "%s: controller is not configured! Please run setup program first", ctrl->id);
      df10ch_dispose(this);
      return -1;
    }

    ctrl->config_version = eedata[2] + (eedata[3] << 8);
    if (ctrl->config_version > this->config_version)
      this->config_version = ctrl->config_version;

      // Determine channel layout
    n = eedata[4 + DF10CH_AREA_TOP];
    if (n > param->top)
      param->top = n;
    n = eedata[4 + DF10CH_AREA_BOTTOM];
    if (n > param->bottom)
      param->bottom = n;
    n = eedata[4 + DF10CH_AREA_LEFT];
    if (n > param->left)
      param->left = n;
    n = eedata[4 + DF10CH_AREA_RIGHT];
    if (n > param->right)
      param->right = n;
    n = eedata[4 + DF10CH_AREA_CENTER];
    if (n > param->center)
      param->center = n;
    n = eedata[4 + DF10CH_AREA_TOP_LEFT];
    if (n > param->top_left)
      param->top_left = n;
    n = eedata[4 + DF10CH_AREA_TOP_RIGHT];
    if (n > param->top_right)
      param->top_right = n;
    n = eedata[4 + DF10CH_AREA_BOTTOM_LEFT];
    if (n > param->bottom_left)
      param->bottom_left = n;
    n = eedata[4 + DF10CH_AREA_BOTTOM_RIGHT];
    if (n > param->bottom_right)
      param->bottom_right = n;

    ctrl->num_req_channels = eedata[13];
    if (ctrl->num_req_channels > DF10CH_MAX_CHANNELS)
      ctrl->num_req_channels = DF10CH_MAX_CHANNELS;

    if (ctrl->config_version > 1) {
      int eei = 14 + ctrl->num_req_channels * 6;
      param->overscan = eedata[eei];
      param->analyze_size = eedata[eei + 1];
      param->edge_weighting = eedata[eei + 2];
      if (ctrl->config_version > 2)
        param->weight_limit = eedata[eei + 3];
    }

      // Read PWM resolution
    if (df10ch_control_in_transfer(ctrl, PWM_REQ_GET_MAX_PWM, 0, 0, DF10CH_USB_DEFAULT_TIMEOUT, data, 2)) {
      snprintf(this->output_driver.errmsg, sizeof(this->output_driver.errmsg), "%s: reading PWM resolution data fails!", ctrl->id);
      df10ch_dispose(this);
      return -1;
    }
    ctrl->pwm_res = data[0] + (data[1] << 8);

      // Build channel configuration list
    nch = ctrl->num_req_channels;
    ccfg = (df10ch_channel_config_t *) calloc(nch, sizeof(df10ch_channel_config_t));
    ctrl->channel_config = ccfg;
    eei = 14;
    while (nch) {
      uint8_t gamma;
      uint16_t white_cal;
      df10ch_gamma_tab_t *gt;

      ccfg->req_channel = eedata[eei];
      ccfg->area = eedata[eei + 1] >> 2;
      ccfg->color = eedata[eei + 1] & 0x03;
      ccfg->area_num = eedata[eei + 2];
      gamma = eedata[eei + 3];
      if (gamma < 10)
        gamma = 10;
      white_cal = eedata[eei + 4] + (eedata[eei + 5] << 8);

        // Lookup gamma table for gamma and white calibration value
      gt = this->gamma_tabs;
      while (gt && gamma != gt->gamma && white_cal != gt->white_cal)
        gt = gt->next;
      if (!gt) {
		// Calculate new gamma table
        gt = (df10ch_gamma_tab_t *) calloc(1, sizeof(df10ch_gamma_tab_t));
        gt->next = this->gamma_tabs;
        this->gamma_tabs = gt;
        gt->gamma = gamma;
        gt->white_cal = white_cal;
		{
			const double dgamma = gamma / 10.0;
			const double dwhite_cal = white_cal;
			int v;
			for (v = 0; v < 256; ++v) {
			  gt->tab[v] = (uint16_t) (pow(((double)v / 255.0), dgamma) * dwhite_cal + 0.5);
			  if (gt->tab[v] > ctrl->pwm_res)
				gt->tab[v] = ctrl->pwm_res;
			}
		}
      }
      ccfg->gamma_tab = gt;

      ++ccfg;
      eei += 6;
      --nch;
    }

      // Prepare USB request for sending brightness values
    ctrl->transfer_data = calloc(1, (LIBUSB_CONTROL_SETUP_SIZE + ctrl->num_req_channels * 2));
    libusb_fill_control_setup(ctrl->transfer_data, LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE, PWM_REQ_SET_BRIGHTNESS, 0, 0, ctrl->num_req_channels * 2);
    ctrl->transfer = libusb_alloc_transfer(0);
    libusb_fill_control_transfer(ctrl->transfer, ctrl->dev, ctrl->transfer_data, df10ch_reply_cb, ctrl, DF10CH_USB_DEFAULT_TIMEOUT);
    ctrl->pending_submit = 0;

    ctrl = ctrl->next;
  }

  this->param = *param;
  return 0;
}


static int df10ch_driver_configure(output_driver_t *this_gen, atmo_parameters_t *param) {
  df10ch_output_driver_t *this = (df10ch_output_driver_t *) this_gen;

    // Ignore channel configuration defined by plugin parameters
  param->top = this->param.top;
  param->bottom = this->param.bottom;
  param->left = this->param.left;
  param->right = this->param.right;
  param->center = this->param.center;
  param->top_left = this->param.top_left;
  param->top_right = this->param.top_right;
  param->bottom_left = this->param.bottom_left;
  param->bottom_right = this->param.bottom_right;

  if (this->config_version > 1) {
    param->overscan = this->param.overscan;
    param->analyze_size = this->param.analyze_size;
    param->edge_weighting = this->param.edge_weighting;
    if (this->config_version > 2)
      param->weight_limit = this->param.weight_limit;
  }

  this->param = *param;
  return 0;
}


static int df10ch_driver_close(output_driver_t *this_gen) {
  df10ch_output_driver_t *this = (df10ch_output_driver_t *) this_gen;
  df10ch_ctrl_t *ctrl = this->ctrls;

    // Cancel all pending requests
  while (ctrl) {
    if (ctrl->pending_submit)
      libusb_cancel_transfer(ctrl->transfer);
    ctrl = ctrl->next;
  }

  df10ch_wait_for_replys(this);
  df10ch_dispose(this);

  DFATMO_LOG(DFLOG_INFO, "average transmit latency: %d [us]", this->avg_transmit_latency);

  if (this->transfer_err_cnt) {
    snprintf(this->output_driver.errmsg, sizeof(this->output_driver.errmsg), "%d transfer errors happen", this->transfer_err_cnt);
    return -1;
  }
  return 0;
}

static void df10ch_driver_dispose(output_driver_t *this_gen) {
  free(this_gen);
}


static int df10ch_driver_output_colors(output_driver_t *this_gen, rgb_color_t *colors, rgb_color_t *last_colors) {
  df10ch_output_driver_t *this = (df10ch_output_driver_t *) this_gen;
  rgb_color_t *area_map[9];
  rgb_color_t *c = colors;
  df10ch_ctrl_t *ctrl = this->ctrls;

#ifdef WIN32
  FILETIME act_time, start_time;
  if (IS_LOG_LEVEL(DFLOG_INFO))
    GetSystemTimeAsFileTime (&start_time);
#else
  struct timeval act_time, start_time, latency_time;
  if (IS_LOG_LEVEL(DFLOG_INFO))
    gettimeofday(&start_time, NULL);
#endif

    // Build area mapping table
  area_map[DF10CH_AREA_TOP] = c;
  c += this->param.top;
  area_map[DF10CH_AREA_BOTTOM] = c;
  c += this->param.bottom;
  area_map[DF10CH_AREA_LEFT] = c;
  c += this->param.left;
  area_map[DF10CH_AREA_RIGHT] = c;
  c += this->param.right;
  area_map[DF10CH_AREA_CENTER] = c;
  c += this->param.center;
  area_map[DF10CH_AREA_TOP_LEFT] = c;
  c += this->param.top_left;
  area_map[DF10CH_AREA_TOP_RIGHT] = c;
  c += this->param.top_right;
  area_map[DF10CH_AREA_BOTTOM_LEFT] = c;
  c += this->param.bottom_left;
  area_map[DF10CH_AREA_BOTTOM_RIGHT] = c;

    // Generate transfer messages and send it to controllers
  while (ctrl) {
      // Generate payload data (brightness values)
    int do_submit = 0;
    uint8_t *payload = ctrl->transfer_data + LIBUSB_CONTROL_SETUP_SIZE;
    df10ch_channel_config_t *cfg = ctrl->channel_config;
    int nch = ctrl->num_req_channels;

	while (nch) {
      rgb_color_t *lc;
      int v = 0;
      uint16_t bv;

	  c = area_map[cfg->area] + cfg->area_num;
      lc = last_colors ? last_colors + (c - colors): NULL;
      switch (cfg->color) {
      case 0: // Red
        v = c->r;
        if (!lc || v != lc->r)
          do_submit = 1;
        break;
      case 1: // Green
        v = c->g;
        if (!lc || v != lc->g)
          do_submit = 1;
        break;
      case 2: // Blue
        v = c->b;
        if (!lc || v != lc->b)
          do_submit = 1;
      }

        // gamma and white calibration correction
      bv = cfg->gamma_tab->tab[v];
      payload[cfg->req_channel * 2] = (uint8_t)bv;
      payload[cfg->req_channel * 2 + 1] = (uint8_t)(bv >> 8);

      ++cfg;
      --nch;
    }

      // initiate asynchron data transfer to controller
    if (do_submit) {
      int rc;
      ctrl->transfer_error = 0;
      rc = libusb_submit_transfer(ctrl->transfer);
      if (rc) {
        snprintf(this->output_driver.errmsg, sizeof(this->output_driver.errmsg), "%s: submitting USB transfer message failed: %s", ctrl->id, libusb_strerror(rc));
        return -1;
      }
      ctrl->pending_submit = 1;
    }

    ctrl = ctrl->next;
  }

    // wait for end of all pending transfers
  if (df10ch_wait_for_replys(this))
    return -1;

  if (IS_LOG_LEVEL(DFLOG_INFO)) {
    int latency;
#ifdef WIN32
    GetSystemTimeAsFileTime (&act_time);
    latency = (act_time.dwLowDateTime - start_time.dwLowDateTime) / 10;
#else
    gettimeofday(&act_time, NULL);
    timersub(&act_time, &start_time, &latency_time);
    latency = latency_time.tv_usec;
#endif
    this->avg_transmit_latency = (this->avg_transmit_latency + latency) / 2;
    if (latency > this->max_transmit_latency) {
      this->max_transmit_latency = latency;
      DFATMO_LOG(DFLOG_INFO, "max/avg transmit latency: %d/%d [us]", this->max_transmit_latency, this->avg_transmit_latency);
    }
  }

  return 0;
}


dfatmo_log_level_t dfatmo_log_level;
dfatmo_log_t dfatmo_log;

output_driver_t* dfatmo_new_output_driver(dfatmo_log_level_t log_level, dfatmo_log_t log_fn) {
  df10ch_output_driver_t *d;

  if (dfatmo_log_level == NULL) {
    dfatmo_log_level = log_level;
    dfatmo_log = log_fn;
  }

  d = (df10ch_output_driver_t *) calloc(1, sizeof(df10ch_output_driver_t));
  if (d == NULL)
    return NULL;

  d->output_driver.version = DFATMO_OUTPUT_DRIVER_VERSION;
  d->output_driver.open = df10ch_driver_open;
  d->output_driver.configure = df10ch_driver_configure;
  d->output_driver.close = df10ch_driver_close;
  d->output_driver.dispose = df10ch_driver_dispose;
  d->output_driver.output_colors = df10ch_driver_output_colors;

  return &d->output_driver;
}
