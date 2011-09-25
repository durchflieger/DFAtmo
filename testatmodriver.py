import os, sys, threading, time
import atmodriver

atmodriver.setLogLevel(atmodriver.LOG_DEBUG)

ad = atmodriver.AtmoDriver()

ad.driver = "file"
ad.driver_param = "/tmp/atmo.out"
ad.driver_path = "."
ad.left = 1
ad.right = 1
ad.top = 1
ad.bottom = 1
ad.filter = atmodriver.FILTER_NONE
ad.configure()
ad.resetFilters()

img = bytearray(range(256) * 64)

analyzedColors = ad.analyzeImage(64,64,atmodriver.IMAGE_FORMAT_RGBA,img)

outputColors = ad.filterAnalyzedColors(analyzedColors)

filteredOutputColors = ad.filterOutputColors(outputColors)

ad.outputColors(filteredOutputColors)

ad.gamma = 25
ad.instantConfigure()

filteredOutputColors = ad.filterOutputColors(outputColors)
ad.outputColors(filteredOutputColors)

ad.turnLightsOff()
ad.closeOutputDriver()



