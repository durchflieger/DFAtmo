# ---
# Copyright (C) 2011 Andreas Auras <yak54@inkennet.de>
#
# This file is part of DFAtmo the driver for 'Atmolight' controllers for XBMC and xinelib based video players.
#
# DFAtmo is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# DFAtmo is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
#
# This is the Makefile for the DFAtmo Project.
#
# ---

INSTALL ?= install

DFATMOINSTDIR ?= /usr/local
DFATMOLIBDIR ?= $(DFATMOINSTDIR)/lib/dfatmo
DFATMOINCLDIR ?= $(DFATMOINSTDIR)/include

XBMCADDONDIR ?= $(HOME)/.xbmc/addons/script.dfatmo

CFLAGS ?= -O3 -pipe -Wall -fPIC -g
LDFLAGS_SO ?= -shared -fvisibility=hidden

XINEPLUGINDIR ?= $(shell pkg-config --variable=plugindir libxine)
CFLAGS_XINE ?= $(shell pkg-config --cflags libxine )
LIBS_XINE ?= $(shell pkg-config --libs libxine)

CFLAGS_PYTHON ?= $(shell python-config --cflags)
LIBS_PYTHON ?= $(shell python-config --libs)

CFLAGS_USB ?= $(shell pkg-config --cflags libusb-1.0)
LIBS_USB ?= $(shell pkg-config --libs libusb-1.0)

XINEPOSTATMO = xineplug_post_dfatmo.so
XBMCADDON = dfatmo-xbmc-addon.zip
XBMCADDONWIN = dfatmo-xbmc-addon-win.zip
XBMCADDONFILES = dfatmo.py addon.xml settings.xml mydriver.py icon.png
OUTPUTDRIVERS = dfatmo-file.so dfatmo-serial.so dfatmo-df10ch.so

.PHONY: all xineplugin xbmcaddon xbmcaddonwin dfatmo install xineinstall xbmcinstall dfatmoinstall clean

all: dfatmo xineplugin xbmcaddon

xineplugin: $(XINEPOSTATMO)

xbmcaddon: $(XBMCADDON)

xbmcaddonwin: $(XBMCADDONWIN)

dfatmo: atmodriver.so $(OUTPUTDRIVERS)

install: xineinstall xbmcinstall dfatmoinstall

xineinstall: xineplugin
	$(INSTALL) -m 0644 -t $(XINEPLUGINDIR)/post $(XINEPOSTATMO) 

xbmcinstall:
	$(INSTALL) -m 0755 -d $(XBMCADDONDIR)
	$(INSTALL) -m 0644 -t $(XBMCADDONDIR) addon.xml dfatmo.py icon.png
	$(INSTALL) -m 0644 HISTORY $(XBMCADDONDIR)/changelog.txt
	$(INSTALL) -m 0644 COPYING $(XBMCADDONDIR)/LICENSE.txt
	$(INSTALL) -m 0755 -d $(XBMCADDONDIR)/resources
	$(INSTALL) -m 0644 -t $(XBMCADDONDIR)/resources settings.xml
	$(INSTALL) -m 0755 -d $(XBMCADDONDIR)/resources/lib/drivers
	$(INSTALL) -m 0644 -t $(XBMCADDONDIR)/resources/lib/drivers mydriver.py

dfatmoinstall: dfatmo
	$(INSTALL) -m 0755 -d $(DFATMOLIBDIR)
	$(INSTALL) -m 0644 -t $(DFATMOLIBDIR) atmodriver.so
	$(INSTALL) -m 0755 -d $(DFATMOLIBDIR)/drivers
	$(INSTALL) -m 0644 -t $(DFATMOLIBDIR)/drivers $(OUTPUTDRIVERS)
	$(INSTALL) -m 0644 -t $(DFATMOINCLDIR) dfatmo.h

clean:
	-rm -f *.so* *.o $(XBMCADDON)
	-rm -rf ./build

$(XBMCADDON): $(XBMCADDONFILES)
	-rm -f $(XBMCADDON)
	-rm -rf ./build
	$(MAKE) xbmcinstall XBMCADDONDIR=./build/script.dfatmo
	(cd ./build && zip -r ../$(XBMCADDON) script.dfatmo)

$(XBMCADDONWIN): $(XBMCADDONFILES)
	-rm -f $(XBMCADDONWIN)
	-rm -rf ./build
	$(MAKE) xbmcinstall XBMCADDONDIR=./build/script.dfatmo
	$(INSTALL) -m 0755 -d build/script.dfatmo/resources/lib.nt
	$(INSTALL) -m 0644 -t build/script.dfatmo/resources/lib.nt project/release/atmodriver.pyd
	$(INSTALL) -m 0755 -d build/script.dfatmo/resources/lib.nt/drivers
	$(INSTALL) -m 0644 -t build/script.dfatmo/resources/lib.nt/drivers project/release/dfatmo-file.dll project/release/dfatmo-serial.dll project/release/dfatmo-df10ch.dll
	(cd ./build && zip -r ../$(XBMCADDONWIN) script.dfatmo)

xineplug_post_dfatmo.o: xineplug_post_dfatmo.c atmodriver.h dfatmo.h
	$(CC) $(CFLAGS_XINE) $(CFLAGS) -DOUTPUT_DRIVER_PATH='"$(DFATMOLIBDIR)/drivers"' -c -o $@ $<

xineplug_post_dfatmo.so: xineplug_post_dfatmo.o
	$(CC) $(CFLAGS_XINE) $(CFLAGS) $(LDFLAGS_SO) $(LIBS_XINE) -lm -ldl -o $@ $<

atmodriver.o: atmodriver.c atmodriver.h dfatmo.h
	$(CC) $(CFLAGS_PYTHON) $(CFLAGS) -DOUTPUT_DRIVER_PATH='"$(DFATMOLIBDIR)/drivers"' -c -o $@ $<

atmodriver.so: atmodriver.o
	$(CC) $(CFLAGS_PYTHON) $(CFLAGS) $(LDFLAGS_SO) $(LIBS_PYTHON) -lm -ldl -o $@ $<

dfatmo-df10ch.o: df10choutputdriver.c dfatmo.h df10ch_usb_proto.h
	$(CC) $(CFLAGS_USB) $(CFLAGS) -c -o $@ $<

dfatmo-df10ch.so: dfatmo-df10ch.o
	$(CC) $(CFLAGS_USB) $(CFLAGS) $(LDFLAGS_SO) $(LIBS_USB) -lm -o $@ $<

dfatmo-file.o: fileoutputdriver.c dfatmo.h
	$(CC) $(CFLAGS) -c -o $@ $<

dfatmo-serial.o: serialoutputdriver.c dfatmo.h
	$(CC) $(CFLAGS) -c -o $@ $<

%.so: %.o
	$(CC) $(CFLAGS) $(LDFLAGS_SO) -o $@ $<
