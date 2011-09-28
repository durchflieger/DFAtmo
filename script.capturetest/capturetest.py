import os, xbmc

def displayNotification(msg):
    xbmc.executebuiltin('Notification("CaptureTest","' + str(msg) + '")')

class CapturePlayer(xbmc.Player):
    def __init__(self):
        displayNotification("Service started")
    
    def onPlayBackStarted(self):
        if self.isPlayingVideo():
            if os.name == 'nt':
                openParm = "wb"
            else:
                openParm = "w"
            analyzeSize = 256
            capture = xbmc.RenderCapture()
            fmt = capture.getImageFormat()
            captureCount = 0
            pending = False
            while self.isPlayingVideo() and captureCount < 10:
                st = capture.getCaptureState()
                if st == xbmc.CAPTURE_STATE_DONE or st == xbmc.CAPTURE_STATE_FAILED:
                    if not pending:
                        aspectRatio = capture.getAspectRatio()
                        if aspectRatio > 0.0:
                            if aspectRatio >= 1.0:
                                capture.capture(analyzeSize, int(analyzeSize / aspectRatio + 0.5))
                            else:
                                capture.capture(int(analyzeSize * aspectRatio + 0.5), analyzeSize)
                            pending = True
                            continue

                    captureCount = captureCount + 1
                    pending = False
                    if st == xbmc.CAPTURE_STATE_DONE:
                        width = capture.getWidth()
                        height = capture.getHeight()
                        img = capture.getImage()
                        
                        xbmc.log("CaptureTest: Capture %s image with size %dx%d" % (fmt, width, height), xbmc.LOGINFO)
                        file = open(xbmc.translatePath('special://temp/captured%02d.ppm' % captureCount), openParm)
                        file.write("P6\n%d\n%d\n255\n" % (width, height))
                        n = width * height
                        i = 0
                        while n:
                            if fmt == 'RGBA':
                                file.write('%c%c%c' % (img[i], img[i+1], img[i+2]))
                            else: # 'BGRA'
                                file.write('%c%c%c' % (img[i+2], img[i+1], img[i]))
                            i = i + 4
                            n = n - 1
                        file.close()
                    continue

                capture.waitForCaptureStateChangeEvent(100)
            
            displayNotification("Video captured")

player = CapturePlayer()
displayNotification("Service started")
while not xbmc.abortRequested:
    xbmc.sleep(1000)
player = None
displayNotification("Service stopped")
