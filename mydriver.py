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
#
# This is a example for a Python script based DFAtmo output driver.
# The output driver write the color data in a human readable form to a file.
#
# The output driver scripts should be located within the DFAtmo XBMC addon
# installation in sub directory 'script.dfatmo/resources/lib/drivers'. 
#
# ---

import os, sys, time, array

# Important: Don't change order of area list!
areas = (
    'top',
    'bottom',
    'left',
    'right',
    'center',
    'top_left',
    'top_right',
    'bottom_left',
    'bottom_right' )


logger = None

def log(logLevel, msg):
    logger.log(logLevel, 'MyDriver: %s' % msg)


class MyOutputDriver:
    def __init__(self, dfatmo):
        self.dfatmo = dfatmo
        self.fd = None
        self.areaChannels = None
        self.channels = 0
        self.nullData = None
        self.startTime = 0
        self.lastColors = None

    def driverError(self, msg):
        return self.dfatmo.driverError(msg)
                
    # return supported interface version
    def getInterfaceVersion(self):
        return 1

    # open device and configure for number of channels
    def openOutputDriver(self):
        self.getAreasConfig()
        self.lastColors = None
        
        self.fileName = self.dfatmo.getParm('driver_param')
        if self.fileName == '':
            self.fileName = 'mydriver.out'
            
        try:
            self.fd = open(self.fileName, 'a')
        except IOError as err:
            raise self.driverError('Opening file %s fails: %s' % (self.fileName, err))
        
        log(logger.LOG_INFO, "file '%s' opened" % self.fileName)
        self.startTime = time.time()
            
    # close device
    def closeOutputDriver(self):
        fd = self.fd
        if fd:
            self.fd = None
            try:
                fd.close()
            except IOError as err:
                raise self.driverError('Closing file %s fails: %s' % (self.fileName, err))

    def getAreasConfig(self):
        self.channels = 0
        self.areaChannels = dict()
        for area in areas:
            n = self.dfatmo.getParm(area)
            self.areaChannels[area] = n
            self.channels = self.channels + n

        n = self.channels * 3
        self.nullData = bytearray(n)
        while n:
            n = n - 1
            self.nullData[n] = 0

    # instant configure
    def instantConfigure(self):
        pass

    # turn all lights off                   
    def turnLightsOff(self):
        self.outputColors(self.nullData)
        
    # send RGB color values to device
    # order for 'colors' is: top 1,2,3..., bottom 1,2,3..., left 1,2,3..., right 1,2,3..., center, top left, top right, bottom left, bottom right
    # order of color packet: Red Green Blue
    def outputColors(self, colors):
        if self.lastColors != None and self.lastColors == colors:
            return
        self.lastColors = None

        try:
            self.fd.write('%16.3f    R   G   B\n' % (time.time() - self.startTime))
            i = 0
            for area in areas:
                areaChannels = self.areaChannels[area]
                areaNum = 1
                while areaChannels:
                    self.fd.write('%12s[%02d]: %03d %03d %03d\n' % (area, areaNum, colors[i], colors[i+1], colors[i+2]))
                    areaChannels = areaChannels - 1
                    areaNum = areaNum + 1
                    i = i + 3
    
                self.fd.flush()
        except IOError as err:
            raise self.driverError('Writing to file %s fails: %s' % (self.fileName, err))

        self.lastColors = colors


# return new output driver instance
def newOutputDriver(dfatmo, l):
    global logger
    logger = l
    return MyOutputDriver(dfatmo)
         