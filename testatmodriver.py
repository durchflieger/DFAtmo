import os, sys, threading, time
import atmodriver

logLevel = atmodriver.LOG_DEBUG

def log(level, msg):
    if level <= logLevel:
        print "DFAtmo: %s" % msg

class Logger:
    def __init__(self):
        self.LOG_NONE = atmodriver.LOG_NONE
        self.LOG_ERROR = atmodriver.LOG_ERROR
        self.LOG_INFO = atmodriver.LOG_INFO
        self.LOG_DEBUG = atmodriver.LOG_DEBUG

    def log(self, level, msg):
        global log
        log(level, msg)

class CustomDriverCallbacks:
    def __init__(self, atmoDriver):
        self.atmoDriver = atmoDriver

    def getParm(self, name):
        return self.atmoDriver.getParm(name)
    
    def setParm(self, name, value):
        self.atmoDriver.setParm(name, value)
    
    def driverError(self, msg):
        return atmodriver.error(str(msg))
    
atmodriver.setLogLevel(logLevel)
ad = atmodriver.AtmoDriver()
customDriver = None

# Uncomment the output driver you want to use
# Script custom drivers should be set with customDriver
# Native output drivers should be set with ad.driver
customDriver = "mydriver"
#ad.driver = "file"

ad.driver_param = "/tmp/atmo.out"
ad.driver_path = "."
ad.left = 1
ad.right = 1
ad.top = 1
ad.bottom = 1
ad.filter = atmodriver.FILTER_NONE

od = ad
if customDriver:
    customDriverModule = __import__(customDriver)
    od = customDriverModule.newOutputDriver(CustomDriverCallbacks(ad), Logger())
    log(atmodriver.LOG_INFO, "Custom driver interface version is %d" % od.getInterfaceVersion())
    od.openOutputDriver()
    od.turnLightsOff()

ad.configure()
ad.resetFilters()

img = bytearray(range(256) * 64)

analyzedColors = ad.analyzeImage(64,64,atmodriver.IMAGE_FORMAT_RGBA,img)

outputColors = ad.filterAnalyzedColors(analyzedColors)

filteredOutputColors = ad.filterOutputColors(outputColors)

od.outputColors(filteredOutputColors)

ad.gamma = 25
if ad != od:
    od.instantConfigure()
ad.instantConfigure()

filteredOutputColors = ad.filterOutputColors(outputColors)
od.outputColors(filteredOutputColors)

od.turnLightsOff()
od.closeOutputDriver()



