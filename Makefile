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

OUTPUTDRIVERPATH ?= $(DFATMOLIBDIR)/drivers

XINEDESTDIR ?=
VDRDESTDIR ?=
XBMCDESTDIR ?= $(HOME)/.xbmc/addons/script.dfatmo

XBMCADDON = dfatmo-xbmc-addon.zip
XBMCADDONWIN = windows/dfatmo-xbmc-addon-win.zip
XBMCADDONFILES = dfatmo.py service.py addon.xml settings.xml mydriver.py icon.png

OUTPUTDRIVERS = dfatmo-file.so dfatmo-serial.so

XINEPOSTATMO = xineplug_post_dfatmo.so

STD_BUILD_TARGETS = dfatmo
STD_INSTALL_TARGETS = dfatmoinstall

CFLAGS_DFATMO = -O3 -pipe -Wall -fPIC -g
LDFLAGS_SO = -shared -fvisibility=hidden

ifneq (NO, $(shell pkg-config --exists libusb-1.0 || echo NO))
HAVE_LIBUSB=1
OUTPUTDRIVERS += dfatmo-df10ch.so
CFLAGS_USB ?= $(shell pkg-config --cflags libusb-1.0)
LIBS_USB ?= $(shell pkg-config --libs libusb-1.0)
ifneq (NO, $(shell pkg-config --exists 'libusb-1.0 >= 1.0.17 libusb-1.0 < 1.0.18' || echo NO))
CFLAGS_USB += -DHAVE_LIBUSB_STRERROR=1
endif
endif

ifneq (NO, $(shell bash -c "type -p python-config || echo NO"))
HAVE_PYTHON=1
ATMODRIVER = atmodriver.so $(XBMCADDON)
CFLAGS_PYTHON ?= $(shell python-config --cflags)
LDFLAGS_PYTHON ?= $(shell python-config --ldflags)
endif

ifneq (NO, $(shell pkg-config --atleast-version=1.1.90 libxine || echo NO))
HAVE_XINELIB = 1
STD_BUILD_TARGETS += xineplugin
STD_INSTALL_TARGETS += xineinstall
XINEPLUGINDIR ?= $(shell pkg-config --variable=plugindir libxine)
CFLAGS_XINE ?= $(shell pkg-config --cflags libxine )
LIBS_XINE ?= $(shell pkg-config --libs libxine)
endif

ifneq (NO, $(shell pkg-config --exists vdr || echo NO))
HAVE_VDR=1
STD_BUILD_TARGETS += vdrplugin
STD_INSTALL_TARGETS += vdrinstall
endif

.PHONY: all xineplugin xbmcaddon xbmcaddonwin dfatmo vdrplugin install xineinstall xbmcinstall dfatmoinstall vdrinstall clean

all: $(STD_BUILD_TARGETS)

xineplugin: $(XINEPOSTATMO)

xbmcaddon: $(XBMCADDON)

xbmcaddonwin: $(XBMCADDONWIN)

dfatmo: $(ATMODRIVER) $(OUTPUTDRIVERS)

vdrplugin::
	$(MAKE) -f vdr2plug.mk all OUTPUTDRIVERPATH=$(OUTPUTDRIVERPATH)

install: $(STD_INSTALL_TARGETS)

xineinstall: xineplugin
	$(INSTALL) -D -m 0644 $(XINEPOSTATMO) $(XINEDESTDIR)$(XINEPLUGINDIR)/post/$(XINEPOSTATMO)

xbmcinstall:
	$(INSTALL) -m 0755 -d $(XBMCDESTDIR)
	$(INSTALL) -m 0644 -t $(XBMCDESTDIR) addon.xml dfatmo.py service.py icon.png
	$(INSTALL) -m 0644 HISTORY $(XBMCDESTDIR)/changelog.txt
	$(INSTALL) -m 0644 COPYING $(XBMCDESTDIR)/LICENSE.txt
	$(INSTALL) -m 0644 README $(XBMCDESTDIR)/readme.txt
	$(INSTALL) -m 0755 -d $(XBMCDESTDIR)/resources
	$(INSTALL) -m 0644 -t $(XBMCDESTDIR)/resources settings.xml
	$(INSTALL) -m 0755 -d $(XBMCDESTDIR)/resources/lib/drivers
	$(INSTALL) -m 0644 -t $(XBMCDESTDIR)/resources/lib/drivers mydriver.py

winxbmcinstall: xbmcinstall
	$(INSTALL) -m 0755 -d $(XBMCDESTDIR)/resources/lib.nt
	$(INSTALL) -m 0644 -t $(XBMCDESTDIR)/resources/lib.nt project/release/atmodriver.pyd
	$(INSTALL) -m 0755 -d $(XBMCDESTDIR)/resources/lib.nt/drivers
	$(INSTALL) -m 0644 -t $(XBMCDESTDIR)/resources/lib.nt/drivers project/release/dfatmo-file.dll project/release/dfatmo-serial.dll project/release/dfatmo-df10ch.dll

dfatmoinstall: dfatmo
	$(INSTALL) -D -m 0644 dfatmo.h $(DFATMOINCLDIR)/dfatmo.h
	$(INSTALL) -m 0755 -d $(DFATMOLIBDIR)/drivers
	$(INSTALL) -m 0644 -t $(DFATMOLIBDIR)/drivers $(OUTPUTDRIVERS)
ifdef ATMODRIVER
	$(INSTALL) -m 0644 -t $(DFATMOLIBDIR) $(ATMODRIVER)
endif

vdrinstall::
	$(MAKE) -f vdr2plug.mk install OUTPUTDRIVERPATH=$(OUTPUTDRIVERPATH) DESTDIR=$(VDRDESTDIR)

clean:
ifdef HAVE_VDR
	-$(MAKE) -f vdr2plug.mk clean
endif
	-rm -f *.so* *.o $(XBMCADDON)
	-rm -rf ./build

$(XBMCADDON): $(XBMCADDONFILES)
	-rm -f $@
	-rm -rf ./build
	$(MAKE) xbmcinstall XBMCDESTDIR=./build/script.dfatmo
	(cd ./build && zip -r ../$@ script.dfatmo)

$(XBMCADDONWIN): $(XBMCADDONFILES) README project/release/atmodriver.pyd project/release/dfatmo-file.dll project/release/dfatmo-serial.dll project/release/dfatmo-df10ch.dll
	-rm -f $@
	-rm -rf ./build
	$(MAKE) winxbmcinstall XBMCDESTDIR=./build/script.dfatmo
	(cd ./build && zip -r ../$@ script.dfatmo)

xineplug_post_dfatmo.o: xineplug_post_dfatmo.c atmodriver.h dfatmo.h
	$(CC) $(CFLAGS) $(CFLAGS_XINE) $(CFLAGS_DFATMO) -DOUTPUT_DRIVER_PATH='"$(OUTPUTDRIVERPATH)"' -c -o $@ $<

xineplug_post_dfatmo.so: xineplug_post_dfatmo.o
	$(CC) $(CFLAGS) $(LDFLAGS) $(CFLAGS_XINE) $(CFLAGS_DFATMO) $(LDFLAGS_SO) -o $@ $< $(LIBS_XINE) -lm -ldl

atmodriver.o: atmodriver.c atmodriver.h dfatmo.h
	$(CC) $(CFLAGS_PYTHON) $(CFLAGS_DFATMO) -DOUTPUT_DRIVER_PATH='"$(OUTPUTDRIVERPATH)"' -c -o $@ $<

atmodriver.so: atmodriver.o
	$(CC) $(CFLAGS_PYTHON) $(LDFLAGS_PYTHON) $(CFLAGS_DFATMO) $(LDFLAGS_SO) -lm -ldl -o $@ $<

dfatmo-df10ch.o: df10choutputdriver.c dfatmo.h df10ch_usb_proto.h
	$(CC) $(CFLAGS) $(CFLAGS_USB) $(CFLAGS_DFATMO) -c -o $@ $<

dfatmo-df10ch.so: dfatmo-df10ch.o
	$(CC) $(CFLAGS) $(LDFLAGS) $(CFLAGS_USB) $(CFLAGS_DFATMO) $(LDFLAGS_SO) -o $@ $< $(LIBS_USB) -lm

dfatmo-file.o: fileoutputdriver.c dfatmo.h
	$(CC) $(CFLAGS) $(CFLAGS_DFATMO) -c -o $@ $<

dfatmo-serial.o: serialoutputdriver.c dfatmo.h
	$(CC) $(CFLAGS) $(CFLAGS_DFATMO) -c -o $@ $<

%.so: %.o
	$(CC) $(CFLAGS) $(LDFLAGS) $(CFLAGS_DFATMO) $(LDFLAGS_SO) -o $@ $<
