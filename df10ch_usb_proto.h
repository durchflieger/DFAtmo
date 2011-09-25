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

/* ---
 * Communication protocol related defines for DF10CH 10 channel RGB Controller.
 */

#define REQ_USB_START			0							// Start of usb controller requests
#define REQ_USB_BL_START	64						// Start of usb controller boot loader requests
#define REQ_PWM_START			128						// Start of pwm controller requests
#define REQ_PWM_BL_START	192						// Start of pwm controller boot loader requests

enum
{
		// usb controller requests
	REQ_START_BOOTLOADER = REQ_USB_START,	// start boot loader of usb controller
	REQ_READ_EE_DATA,											// read eeprom data (wLength: number of bytes, wIndex: eeprom start address)
	REQ_WRITE_EE_DATA,										// write eeprom data (wLength: number of bytes, wIndex: eeprom start address)

	REQ_STOP_PWM_CTRL,										// stop PWM controller
	REQ_RESET_PWM_CTRL,										// reset PWM controller
	REQ_BOOTLOADER_RESET_PWM_CTRL,				// reset PWM controller and signal bootloader start

	REQ_SET_REPLY_TIMEOUT,								// set reply timeout values (wValue: start timeout [ms], wIndex: timeout [ms])

	REQ_GET_REPLY_ERR_STATUS,		  				// get reply error status (COMM_ERR_...)

		// usb controller boot loader requests
	BL_REQ_WRITE_PAGE = REQ_USB_BL_START,		// write flash page
	BL_REQ_LEAVE_BOOT,											// leave boot loader and start application
	BL_REQ_GET_PAGE_SIZE,										// return flash page size of device
	BL_REQ_READ_FLASH,											// return flash contents

		// pwm controller requests
  PWM_REQ_GET_VERSION = REQ_PWM_START,		// Get firmware version (returns 1 byte)
	PWM_REQ_SET_BRIGHTNESS,									// Set channel brightness values (wLenght: number of bytes, wIndex: start channel)
	PWM_REQ_SET_BRIGHTNESS_SYNCED,					// Same as above but wait until buffer flip
	PWM_REQ_GET_BRIGHTNESS,

	PWM_REQ_SET_CHANNEL_MAP,								// Set channel to port mapping (wLength: number of bytes, wIndex: start channel)
	PWM_REQ_GET_CHANNEL_MAP,

	PWM_REQ_SET_COMMON_PWM,									// Set common pwm value (wValue.low: pwm value)
	PWM_REQ_GET_COMMON_PWM,

	PWM_REQ_STORE_SETUP,										// Store actual calibration values
	PWM_REQ_RESET_SETUP,										// Reset calibration values to default

	PWM_REQ_GET_REQUEST_ERR_STATUS,					// Get request error status (COMM_ERR_...)

	PWM_REQ_GET_MAX_PWM,										// Get maximum internal PWM value

	PWM_REQ_SET_PWM_FREQ,										// Set pwm frequency (wValue: frequency [hz])
	PWM_REQ_GET_PWM_FREQ,										// Get pwm frequency (returns word)

	PWM_REQ_ECHO_TEST,											// Reply 8 byte header

		// pwm controller boot loader requests
	BL_PWM_REQ_WRITE_PAGE = REQ_PWM_BL_START,		// write flash page
	BL_PWM_REQ_GET_PAGE_SIZE,								// return flash page size of device
	BL_PWM_REQ_READ_FLASH,									// return flash contents
	BL_PWM_REQ_GET_REQUEST_ERR_STATUS				// Get request error status (COMM_ERR_...)
};

// Data payload related
#define MAX_REQ_PAYLOAD_SIZE		128
#define MAX_REPLY_PAYLOAD_SIZE	128

// Error flag definition for communication error's of usb and pwm controller
#define COMM_ERR_OVERRUN	0
#define COMM_ERR_FRAME		1
#define COMM_ERR_TIMEOUT	2
#define COMM_ERR_START		3
#define COMM_ERR_OVERFLOW	4
#define COMM_ERR_CRC			5
#define COMM_ERR_DUPLICATE 6
#define COMM_ERR_DEBUG		7

// Port channel mapping related
#define NCHANNELS 30				// Number of supported Channels

#define NPORTS	4
#define PA_IDX	0
#define PB_IDX	1
#define PC_IDX	2
#define PD_IDX	3

#define CM_CODE(port, channel)	(((channel) << 2) | (port))
#define CM_CHANNEL(code)				((code) >> 2)
#define CM_PORT(code)						((code) & 0x03)

// PWM frequency related
#define MIN_PWM_FREQ	50
#define MAX_PWM_FREQ	400

// PWM controller version request related
#define PWM_VERS_APPL	0		// Is application firmware
#define PWM_VERS_BOOT	1		// Is bootloader firmware
