# ---
# Copyright (C) 2012 Andreas Auras <yak54@inkennet.de>
#
# This file is part of DFAtmo the driver for 'Atmolight' controllers for VDR, XBMC and xinelib based video players.
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
# This is the Makefile for the VDR plugin of the DFAtmo Project.
#
# ---

PLUGIN = dfatmo

### The version number of this plugin (taken from the main source file):

VERSION = $(shell grep 'static const char \*VERSION *=' vdrplug_dfatmo.cpp | awk '{ print $$6 }' | sed -e 's/[";]//g')

### The C++ compiler and options:

CXX      ?= g++
CXXFLAGS ?= -g -O3 -Wall -Werror=overloaded-virtual -Wno-parentheses

### The directory environment:

VDRDIR ?= ../../..
LIBDIR ?= ../../lib
TMPDIR ?= /tmp
DFATMOINSTDIR ?= /usr/local
DFATMOLIBDIR ?= $(DFATMOINSTDIR)/lib/dfatmo

### Make sure that necessary options are included:

include $(VDRDIR)/Make.global

### Allow user defined options to overwrite defaults:

-include $(VDRDIR)/Make.config

### The version number of VDR's plugin API (taken from VDR's "config.h"):

APIVERSION = $(shell sed -ne '/define APIVERSION/s/^.*"\(.*\)".*$$/\1/p' $(VDRDIR)/config.h)

### The name of the distribution archive:

ARCHIVE = $(PLUGIN)-$(VERSION)
PACKAGE = vdr-$(ARCHIVE)

### Includes and Defines (add further entries here):

INCLUDES += -I$(VDRDIR)/include

DEFINES += -D_GNU_SOURCE -DPLUGIN_NAME_I18N='"$(PLUGIN)"' -DOUTPUT_DRIVER_PATH='"$(DFATMOLIBDIR)/drivers"'

### The object files (add further files here):

OBJS = vdrplug_dfatmo.o

### Internationalization (I18N):

PODIR     = vdrplug.po
LOCALEDIR = $(VDRDIR)/locale
I18Npo    = $(wildcard $(PODIR)/*.po)
I18Nmsgs  = $(addprefix $(LOCALEDIR)/, $(addsuffix /LC_MESSAGES/vdr-$(PLUGIN).mo, $(notdir $(foreach file, $(I18Npo), $(basename $(file))))))
I18Npot   = $(PODIR)/$(PLUGIN).pot

### Main targets:
.PHONY: all install clean

all: libvdr-$(PLUGIN).so $(I18Npot)

install: all $(I18Nmsgs)
	cp libvdr-$(PLUGIN).so $(LIBDIR)/libvdr-$(PLUGIN).so.$(APIVERSION)

clean:
	-rm -f $(OBJS) libvdr-$(PLUGIN).so $(PODIR)/*.mo $(PODIR)/*.pot

### Dependencies:

libvdr-$(PLUGIN).so: $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -shared $(OBJS) -o $@

$(OBJS): dfatmo.h atmodriver.h

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -O3 -c $(DEFINES) $(INCLUDES) $<

%.mo: %.po
	msgfmt -c -o $@ $<

$(I18Npot): $(OBJS:%.o=%.cpp) atmodriver.h
	mkdir -p $(PODIR)
	xgettext -C -cTRANSLATORS --no-wrap --no-location -k -ktr -ktrNOOP --package-name=vdr-$(PLUGIN) --package-version=$(VERSION) --msgid-bugs-address='yak54@inkennet.de' -o $@ $^

%.po: $(I18Npot)
	msgmerge -U --no-wrap --no-location --backup=none -q $@ $<
	touch $@

$(I18Nmsgs): $(LOCALEDIR)/%/LC_MESSAGES/vdr-$(PLUGIN).mo: $(PODIR)/%.mo
	mkdir -p $(dir $@)
	cp $< $@
	