/*
Copyright (c) 2024 Carri King

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/																																																	  
#pragma once

class FBDisplay {
public:
  FBDisplay() { }
  ~FBDisplay() { Close(); }

  bool IsOpen() const { return m_fbp != NULL; }
  
  bool Open();
  void Close();
  void Clear();
  void Present();

  void PutPixel(int x, int y, int color);
  void PlotLine(int x0, int y0, int x1, int y1, int color);
  void DrawCircle(int x, int y, int radius, int color);
  void DrawEllipse(int x, int y, int radiusX, int radiusY, int color);
  void BlitImage16BitColorDoubleScale(const uint16_t *src, int width, int height, int xpos, int ypos);
  void BlitImage16BitColor(const uint16_t *src, int width, int height, int xpos, int ypos);    

  int GetScreenWidth() const { return m_screenWidth; }
  int GetScreenHeight() const { return m_screenHeight; }

  void SetTextColor(const int c) { m_textColor = c; }

  char *GetSurfacePtr() { return m_fbp; }
  int GetStride() const { return m_stride; }

  bool VideoPlay(const char *filename);
  void VideoStop();

  void SetVideoFrameObserver(std::function<void()> observer) {
    m_videoFrameObserver = observer;
  }

  void SetVideoStopObserver(std::function<void()> observer) {
    m_videoStopObserver = observer;
  }  
  
  void SetVideoWindowX(int x) { m_videoWindowX = x; }
  void SetVideoWindowY(int y) { m_videoWindowY = y; }
  void SetVideoWindowWidth(int w) { m_videoWindowWidth = w; }    
    

protected:
  void vlcLock(void **pPixels);
  void vlcUnlock(const uint16_t *pixels);
  void vlcDisplay();
  void vlcStopEvent();
  
private:
  void StrokeCharacterLine(float x1, float y1, float x2, float y2, int xoff, int yoff);
  
  int m_screenWidth = 0;
  int m_screenHeight = 0;
  int m_bpp = 0;
  char *m_fbp = nullptr;
  long int m_screensize = 0;
  int m_fbfd = -1;
  int m_textColor = 0xffffffff;
  int m_stride = 0;
  std::vector<char> m_tmpFbp;
  char *m_realFbp = nullptr;
  
  uint16_t *m_vlcFrame = nullptr;
  uint16_t *m_vlcPixels = nullptr;  
  int m_videoWidth = 320;
  int m_videoHeight = 240;

  std::function<void()> m_videoFrameObserver;
  std::function<void()> m_videoStopObserver;  

  int m_videoWindowWidth = 320;
  int m_videoWindowX = 0;
  int m_videoWindowY = 0;  
  
  static constexpr useconds_t MICROS = 1000000;

  struct VLCCallbacks {
    static void *lock(void *data, void **p_pixels) {
      FBDisplay *thiz = (FBDisplay *)data;
      thiz->vlcLock(p_pixels);
      return NULL; // unused id
}

    static void unlock(void *data, void* /*unused id*/, void *const *p_pixels) {
      FBDisplay *thiz = (FBDisplay *)data;
      const uint16_t *pixels = (const uint16_t *)*p_pixels;
      thiz->vlcUnlock(pixels);
    }
    
    static void display(void *data, void* /*unused id*/) {
      FBDisplay *thiz = (FBDisplay *)data;
      thiz->vlcDisplay();
    }

    static void stopEvent(const struct libvlc_event_t */*event*/, void *data) {
      FBDisplay *thiz = (FBDisplay *)data;
      thiz->vlcStopEvent();
    }
  };

  struct VLCImpl *m_vlcImpl = nullptr;
};




