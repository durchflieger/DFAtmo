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
 * This is the DFAtmo native serial output driver.
 *
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#ifdef WIN32
#include <windows.h>

typedef HANDLE dev_handle_t;
typedef DWORD dev_size_t;
typedef DWORD dev_speed_t;

#define INVALID_DEV_HANDLE      INVALID_HANDLE_VALUE
#define DEFAULT_PORT            "COM1"
#define SPEED_CONST(speed)      CBR_ ## speed
#define GET_SYS_ERR_MSG(buf)    FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS, NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf, sizeof(buf), NULL)
#define OPEN_DEVICE(name)       CreateFile(name, GENERIC_WRITE, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0)
#define CLOSE_DEVICE(fd)        CloseHandle(fd)
#define WRITE_DATA(fd, buf, len, written) WriteFile(fd, buf, (DWORD)len, &written, NULL)
#define FLUSH_BUFFER(fd)        FlushFileBuffers(fd)

#define strtok_r                strtok_s
#define snprintf                _snprintf
#else
#include <unistd.h>
#include <termios.h>
#include <regex.h>
#include <fcntl.h>
#include <errno.h>

typedef int dev_handle_t;
typedef ssize_t dev_size_t;
typedef speed_t dev_speed_t;

#define INVALID_DEV_HANDLE      -1
#define DEFAULT_PORT            "/dev/ttyS0"
#define SPEED_CONST(speed)      B ## speed
#define GET_SYS_ERR_MSG(buf)    strerror_r(errno, buf, sizeof(buf))
#define OPEN_DEVICE(name)       open(name, O_WRONLY|O_NOCTTY)
#define CLOSE_DEVICE(fd)        close(fd)
#define WRITE_DATA(fd, buf, len, written) ((written = write(fd, buf, (size_t)len)) != (ssize_t)-1)
#define FLUSH_BUFFER(fd)        (tcdrain(fd) != -1)
#endif

#include "dfatmo.h"

typedef struct {
  output_driver_t output_driver;
  atmo_parameters_t param;
  const char *protocol;
  const uint8_t *escapes;
  dev_handle_t devfd;
  char driver_param[SIZE_DRIVER_PARAM];
} serial_output_driver_t;


static const char *classic_proto = "255|0|0|15|Rc|Gc|Bc|Rl|Gl|Bl|Rr|Gr|Br|Rt|Gt|Bt|Rb|Gb|Bb";
static const char *df4ch_proto = "255|0|12|Rl|Gl|Bl|Rr|Gr|Br|Rt|Gt|Bt|Rb|Gb|Bb";
static const char *amblone_proto = "xF4|Rl|Gl|Bl|Rr|Gr|Br|Rt|Gt|Bt|Rb|Gb|Bb|x33";
static const char *karate_proto = "xAA|x12|CX|24|Gl|Bl|Rl|Gr|Br|Rr|Gt|Bt|Rt|Gb|Bb|Rb|Gl2|Bl2|Rl2|Gr2|Br2|Rr2|Gt2|Bt2|Rt2|Gb2|Bb2|Rb2";

static const uint8_t amblone_escapes[] = { 0x99, 6, 0xF1, 0xF2, 0xF3, 0xF4, 0x33, 0x99 };


static int serial_driver_open(output_driver_t *this_gen, atmo_parameters_t *p) {
  serial_output_driver_t *this = (serial_output_driver_t *) this_gen;
  const char *devname = NULL;
  const char *usb = NULL;
  const char *speed = NULL;
  char *t, *tp;
  char buf[256];
  dev_handle_t devfd;
  dev_speed_t bspeed;
  int ok;

  this->param = *p;
  this->devfd = INVALID_DEV_HANDLE;
  this->protocol = classic_proto;

  /* parse driver parameter */
  strncpy(this->driver_param, this->param.driver_param, sizeof(this->driver_param));
  t = strtok_r(this->driver_param, ";&", &tp);
  while (t != NULL) {
    char *v = strchr(t, ':');
    if (v == NULL)
      devname = t;
    else {
      *v++ = 0;
      if (!strcmp(t, "speed")) {
        speed = v;
      } else if (!strcmp(t, "proto")) {
        if (!strcmp(v, "classic"))
          this->protocol = classic_proto;
        else if (!strcmp(v, "df4ch"))
          this->protocol = df4ch_proto;
        else if (!strcmp(v, "amblone"))
          this->protocol = amblone_proto;
        else if (!strcmp(v, "karatelight"))
          this->protocol = karate_proto;
        else
          this->protocol = v;
      } else if (!strcmp(t, "amblone")) {
          this->escapes = amblone_escapes;
#ifndef WIN32
      } else if (!strcmp(t, "usb")) {
        usb = v;
#endif
      } else {
        snprintf(this->output_driver.errmsg, sizeof(this->output_driver.errmsg), "keyword '%s' unknown", t);
        return -1;
      }
    }
    t = strtok_r(NULL, ";&", &tp);
  }

  if (usb == NULL && (devname == NULL || strspn(devname, " ") == strlen(devname)))
    devname = DEFAULT_PORT;

#ifndef WIN32
  char buf1[64];
  if (usb != NULL) {
    /* Lookup serial USB device name */
    regex_t preg;
    devname = NULL;

    int rc = regcomp(&preg, usb, REG_EXTENDED | REG_NOSUB);
    if (rc) {
      regerror(rc, &preg, buf, sizeof(buf));
      snprintf(this->output_driver.errmsg, sizeof(this->output_driver.errmsg), "illegal device identification pattern '%s': %s", usb, buf);
      regfree(&preg);
      return -1;
    }

    FILE *procfd = fopen("/proc/tty/driver/usbserial", "r");
    if (!procfd) {
      strerror_r(errno, buf, sizeof(buf));
      snprintf(this->output_driver.errmsg, sizeof(this->output_driver.errmsg), "could not open '/proc/tty/driver/usbserial': %s", buf);
      regfree(&preg);
      return -1;
    }

    while (fgets(buf, sizeof(buf), procfd)) {
      char *s;
      if (!regexec(&preg, buf, 0, NULL, 0) && (s = index(buf, ':'))) {
        *s = 0;
        snprintf(buf1, sizeof(buf1), "/dev/ttyUSB%s", buf);
        devname = buf1;
        break;
      }
    }
    fclose(procfd);
    regfree(&preg);
    if (!devname) {
      snprintf(this->output_driver.errmsg, sizeof(this->output_driver.errmsg), "could not find usb device with pattern '%s' in '/proc/tty/driver/usbserial'", usb);
      return -1;
    }
  }
#endif

  DFATMO_LOG(LOG_INFO, "serial port device: '%s'", devname);

    /* open serial port device */
  devfd = OPEN_DEVICE(devname);
  if (devfd == INVALID_DEV_HANDLE) {
    GET_SYS_ERR_MSG(buf);
    snprintf(this->output_driver.errmsg, sizeof(this->output_driver.errmsg), "could not open serial port device '%s': %s", devname, buf);
    return -1;
  }

    /* configure serial port */
  bspeed = SPEED_CONST(38400);
  if (speed == NULL)
    speed = "38400";
  else {
    switch (atoi(speed)) {
    case 1200:
      bspeed = SPEED_CONST(1200);
      break;
    case 2400:
      bspeed = SPEED_CONST(2400);
      break;
    case 4800:
      bspeed = SPEED_CONST(4800);
      break;
    case 9600:
      bspeed = SPEED_CONST(9600);
      break;
    case 19200:
      bspeed = SPEED_CONST(19200);
      break;
    case 38400:
      bspeed = SPEED_CONST(38400);
      break;
    case 57600:
      bspeed = SPEED_CONST(57600);
      break;
    case 115200:
      bspeed = SPEED_CONST(115200);
      break;
#ifdef WIN32
    case 128000:
      bspeed = SPEED_CONST(128000);
      break;
    case 256000:
      bspeed = SPEED_CONST(256000);
      break;
#else
    case 230400:
      bspeed = SPEED_CONST(230400);
      break;
    case 460800:
      bspeed = SPEED_CONST(460800);
      break;
    case 500000:
      bspeed = SPEED_CONST(500000);
      break;
    case 576000:
      bspeed = SPEED_CONST(576000);
      break;
    case 921600:
      bspeed = SPEED_CONST(921600);
      break;
    case 1000000:
      bspeed = SPEED_CONST(1000000);
      break;
    case 1152000:
      bspeed = SPEED_CONST(1152000);
      break;
    case 1500000:
      bspeed = SPEED_CONST(1500000);
      break;
    case 2000000:
      bspeed = SPEED_CONST(2000000);
      break;
    case 2500000:
      bspeed = SPEED_CONST(2500000);
      break;
    case 3000000:
      bspeed = SPEED_CONST(3000000);
      break;
    case 3500000:
      bspeed = SPEED_CONST(3500000);
      break;
    case 4000000:
      bspeed = SPEED_CONST(4000000);
      break;
#endif
    default:
      snprintf(this->output_driver.errmsg, sizeof(this->output_driver.errmsg), "serial port device speed '%s' unsupported", speed);
      return -1;
    }
  }

  DFATMO_LOG(LOG_INFO, "serial port speed: %s", speed);

#ifdef WIN32
  {
    DCB dcbSerialParams = {0};
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    ok = GetCommState(devfd, &dcbSerialParams);
    if (ok) {
      dcbSerialParams.BaudRate = bspeed;
      dcbSerialParams.ByteSize = 8;
      dcbSerialParams.StopBits = TWOSTOPBITS;
      dcbSerialParams.Parity = NOPARITY;
      ok = SetCommState(devfd, &dcbSerialParams);
    }
  }
#else
  struct termios tio;
  memset(&tio, 0, sizeof(tio));
  tio.c_cflag = (CS8 | CSTOPB | CLOCAL);
  cfsetospeed(&tio, bspeed);
  ok = (tcsetattr(devfd, TCSANOW, &tio) == 0);
  if (ok)
    tcflush(devfd, TCIOFLUSH);
#endif
  if (!ok) {
    GET_SYS_ERR_MSG(buf);
    snprintf(this->output_driver.errmsg, sizeof(this->output_driver.errmsg), "configuration of serial port device '%s' failed: %s", devname, buf);
    CLOSE_DEVICE(devfd);
    return -1;
  }

  this->devfd = devfd;
  return 0;
}


static int serial_driver_configure(output_driver_t *this_gen, atmo_parameters_t *p) {
  serial_output_driver_t *this = (serial_output_driver_t *) this_gen;
  this->param = *p;
  return 0;
}


static int serial_driver_close(output_driver_t *this_gen) {
  serial_output_driver_t *this = (serial_output_driver_t *) this_gen;

  if (this->devfd != INVALID_DEV_HANDLE) {
    CLOSE_DEVICE(this->devfd);
    this->devfd = INVALID_DEV_HANDLE;
  }
  return 0;
}


static void serial_driver_dispose(output_driver_t *this_gen) {
  free(this_gen);
}

static int serial_driver_output_colors(output_driver_t *this_gen, rgb_color_t *colors, rgb_color_t *last_colors) {
  serial_output_driver_t *this = (serial_output_driver_t *) this_gen;
  uint8_t msg[512];
  uint8_t *m = msg;
  int data = 0, area = 0, area_num = 0, color = 0;
  const char *p = this->protocol;
  dev_size_t len, written;
  enum { TOP_AREA, BOTTOM_AREA, LEFT_AREA, RIGHT_AREA, CENTER_AREA, TOP_LEFT_AREA, TOP_RIGHT_AREA, BOTTOM_LEFT_AREA, BOTTOM_RIGHT_AREA };
  enum { COLOR_RED, COLOR_GREEN, COLOR_BLUE };
  enum { START_STATE, DEC_CONST_STATE, HEX_CONST_STATE, AREA_STATE, AREA_NUM_STATE, CRC_STATE };
  enum { NO_ERR, SYNTAX_ERR, DATA_ERR, LENGTH_ERR, CRC_MODE_ERR };
  enum { ERR_CRC_MODE, XOR_CRC_MODE };
  int state = START_STATE;
  int err = NO_ERR;
  int crc_mode = ERR_CRC_MODE;
  uint8_t *crc_pos = NULL;

  if (this->devfd == INVALID_DEV_HANDLE)
    return -1;

  /* parse protocol descriptor and build data packet */
  while (err == NO_ERR) {
    int c = *p++;

    switch (state) {
    case START_STATE:
      switch (c) {
      case 0:
        break;
      case 'x':
      case 'X':
        data = 0;
        state = HEX_CONST_STATE;
        break;
      case 'r':
      case 'R':
        color = COLOR_RED;
        state = AREA_STATE;
        break;
      case 'g':
      case 'G':
        color = COLOR_GREEN;
        state = AREA_STATE;
        break;
      case 'b':
      case 'B':
        color = COLOR_BLUE;
        state = AREA_STATE;
        break;
      case 'c':
      case 'C':
        state = CRC_STATE;
        break;
      default:
        if (c >= '0' && c <= '9') {
          data = c - '0';
          state = DEC_CONST_STATE;
        } else
          err = SYNTAX_ERR;
      }
      break;

    case CRC_STATE:
      switch (c)
      {
      case 'x':
      case 'X':
        crc_mode = XOR_CRC_MODE;
        break;
      case '|':
      case 0:
        if (crc_mode == ERR_CRC_MODE)
          err = CRC_MODE_ERR;
        else
        {
          crc_pos = m++;
          state = START_STATE;
        }
        break;
      default:
        err = SYNTAX_ERR;
      }
      break;

    case DEC_CONST_STATE:
      if (c >= '0' && c <= '9') {
        data = data * 10 + c - '0';
        if (data > 255)
          err = DATA_ERR;
      } else if (c == '|' || !c) {
        *m++ = data;
        state = START_STATE;
      } else
        err = SYNTAX_ERR;
      break;

    case HEX_CONST_STATE:
      if (c >= '0' && c <= '9') {
        data = data * 16 + c - '0';
        if (data > 255)
          err = DATA_ERR;
      } else if (c >= 'a' && c <= 'f') {
        data = data * 16 + c - 'a' + 10;
        if (data > 255)
          err = DATA_ERR;
      } else if (c >= 'A' && c <= 'F') {
        data = data * 16 + c - 'A' + 10;
        if (data > 255)
          err = DATA_ERR;
      } else if (c == '|' || !c) {
        *m++ = data;
        state = START_STATE;
      } else
        err = SYNTAX_ERR;
      break;

    case AREA_STATE:
      area_num = 0;
      state = AREA_NUM_STATE;
      switch (c) {
      case 't':
      case 'T':
        area = TOP_AREA;
        break;
      case 'b':
      case 'B':
        area = BOTTOM_AREA;
        break;
      case 'l':
      case 'L':
        area = LEFT_AREA;
        break;
      case 'r':
      case 'R':
        area = RIGHT_AREA;
        break;
      case 'c':
      case 'C':
        area = CENTER_AREA;
        break;
      default:
        err = SYNTAX_ERR;
      }
      break;

    case AREA_NUM_STATE:
      switch (c) {
      case 'l':
      case 'L':
        switch (area) {
        case TOP_AREA:
          area = TOP_LEFT_AREA;
          break;
        case BOTTOM_AREA:
          area = BOTTOM_LEFT_AREA;
          break;
        default:
          err = SYNTAX_ERR;
        }
        break;

      case 'r':
      case 'R':
        switch (area) {
        case TOP_AREA:
          area = TOP_RIGHT_AREA;
          break;
        case BOTTOM_AREA:
          area = BOTTOM_RIGHT_AREA;
          break;
        default:
          err = SYNTAX_ERR;
        }
        break;

      default:
        if (c >= '0' && c <= '9')
          area_num = area_num * 10 + c - '0';
        else if (c == '|' || !c) {
          int n, i;
          uint8_t v;
          switch (area) {
          case TOP_AREA:
            i = 0;
            n = this->param.top;
            break;
          case BOTTOM_AREA:
            i = this->param.top;
            n = this->param.bottom;
            break;
          case LEFT_AREA:
            i = this->param.top + this->param.bottom;
            n = this->param.left;
            break;
          case RIGHT_AREA:
            i = this->param.top + this->param.bottom + this->param.left;
            n = this->param.right;
            break;
          case CENTER_AREA:
            i = this->param.top + this->param.bottom + this->param.left + this->param.right;
            n = this->param.center;
            break;
          case TOP_LEFT_AREA:
            i = this->param.top + this->param.bottom + this->param.left + this->param.right + this->param.center;
            n = this->param.top_left;
            break;
          case TOP_RIGHT_AREA:
            i = this->param.top + this->param.bottom + this->param.left + this->param.right + this->param.center + this->param.top_left;
            n = this->param.top_right;
            break;
          case BOTTOM_LEFT_AREA:
            i = this->param.top + this->param.bottom + this->param.left + this->param.right + this->param.center + this->param.top_left + this->param.top_right;
            n = this->param.bottom_left;
            break;
          default:
            i = this->param.top + this->param.bottom + this->param.left + this->param.right + this->param.center + this->param.top_left + this->param.top_right + this->param.bottom_left;
            n = this->param.bottom_right;
          }
          if (area_num)
            --area_num;
          if (area_num < n) {
            i += area_num;
            switch (color) {
            case COLOR_RED:
              v = colors[i].r;
              break;
            case COLOR_GREEN:
              v = colors[i].g;
              break;
            default:
              v = colors[i].b;
            }
          } else
            v = 0;
          if (this->escapes) {
            int n = this->escapes[1];
            const uint8_t *p = &this->escapes[2];
            while (n) {
              if (v == *p) {
                *m++ = this->escapes[0];
                break;
              }
              ++p;
              --n;
            }
          }
          *m++ = v;
          state = START_STATE;
        } else
          err = SYNTAX_ERR;
      }
    }

    if (!c) {
      if (state != START_STATE)
        err = SYNTAX_ERR;
      break;
    }

    if ((m - msg) >= sizeof(msg))
      err = LENGTH_ERR;
  }

  if (err != NO_ERR) {
    switch (err) {
    case SYNTAX_ERR:
      snprintf(this->output_driver.errmsg, sizeof(this->output_driver.errmsg), "protocol syntax error at position: %d", (int)(p - this->protocol));
      break;
    case DATA_ERR:
      snprintf(this->output_driver.errmsg, sizeof(this->output_driver.errmsg), "bad data byte value: %d", data);
      break;
    case LENGTH_ERR:
      snprintf(this->output_driver.errmsg, sizeof(this->output_driver.errmsg), "message to long");
      break;
    case CRC_MODE_ERR:
      snprintf(this->output_driver.errmsg, sizeof(this->output_driver.errmsg), "missing crc mode");
    }
    return -1;
  }

  if (crc_pos != NULL)
  {
    uint8_t *v = msg;
    uint8_t crc = 0;
    while (v < m)
    {
      if (v != crc_pos)
        crc ^= *v;
      ++v;
    }
    *crc_pos = crc;
  }

  len = (dev_size_t)(m - msg);
  if (!WRITE_DATA(this->devfd, msg, len, written) || !FLUSH_BUFFER(this->devfd)) {
    char buf[128];
    GET_SYS_ERR_MSG(buf);
    snprintf(this->output_driver.errmsg, sizeof(this->output_driver.errmsg), "writing data to serial port failed: %s", buf);
    return -1;
  }

  return 0;
}


dfatmo_log_level_t dfatmo_log_level;
dfatmo_log_t dfatmo_log;

output_driver_t* dfatmo_new_output_driver(dfatmo_log_level_t log_level, dfatmo_log_t log_fn) {
  serial_output_driver_t *d;

  if (dfatmo_log_level == NULL) {
    dfatmo_log_level = log_level;
    dfatmo_log = log_fn;
  }

  d = (serial_output_driver_t *) calloc(1, sizeof(serial_output_driver_t));
  if (d == NULL)
    return NULL;

  d->output_driver.version = DFATMO_OUTPUT_DRIVER_VERSION;
  d->output_driver.open = serial_driver_open;
  d->output_driver.configure = serial_driver_configure;
  d->output_driver.close = serial_driver_close;
  d->output_driver.dispose = serial_driver_dispose;
  d->output_driver.output_colors = serial_driver_output_colors;
  d->devfd = INVALID_DEV_HANDLE;

  return &d->output_driver;
}
