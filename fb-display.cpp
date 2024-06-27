/*
Copyright (c) 2024 Carri King

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/																																																	  
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <cmath>
#include <cstdint>
#include <vector>
#include <functional>

#include "fb-display.h"

#include "vlc/vlc.h"

const uint16_t MASK_R = 0b1111100000000000;
const uint8_t SHIFT_R = (6 + 5);
const uint16_t MASK_G = 0b0000011111100000;
const uint8_t SHIFT_G = (5);
const uint16_t MASK_B = 0b0000000000011111;
const uint8_t SHIFT_B = (0);

void FBDisplay::BlitImage16BitColorDoubleScale(const uint16_t *srcImg, int width, int height, int xpos, int ypos)
{
  if((xpos + width) > m_screenWidth)
    width = m_screenWidth - xpos;
  if((ypos + height) > m_screenHeight)
    height = m_screenHeight - ypos;
  if(width < 0 || height < 0)
    return;
  
  if(m_bpp == 16) {
    uint16_t *fbp = (uint16_t *)m_realFbp;        
    for(int y = 0; y<height; y++) {
      for(int j = 0; j<2; j++) {
	uint16_t *dst = fbp + ((m_screenWidth) * ((y*2) + j + ypos)) + xpos;
	const uint16_t *src = srcImg + (y * width);
	for(int x = 0; x<width; x++) {
	  uint16_t srcPix = *src++;
	  uint16_t dstPix = ((srcPix & MASK_R) >> 11) | (srcPix & MASK_G) | ((srcPix & MASK_B) << 11);
	  *dst++ = dstPix;
	  *dst++ = dstPix;
	}
      }
    }
    return;
  }    
  
  if(m_bpp == 32) {
    int *fbp = (int *)m_realFbp;    
    for(int y = 0; y<height; y++) {
      for(int j = 0; j<2; j++) {
	int *dst = fbp + ((m_screenWidth) * ((y*2) + j + ypos)) + xpos;
	const uint16_t * src = srcImg + (y * width);
	for(int x = 0; x<width; x++) {
	  const uint32_t r = (*src & MASK_R) >> SHIFT_R;
	  const uint32_t g = (*src & MASK_G) >> SHIFT_G;
	  const uint32_t b = (*src & MASK_B) >> SHIFT_B;
	  ++src;      
	  const int pix = (int)(uint32_t)(0xff000000 | ((b << 3) << 16) | ((g << 2) << 8) | (r << 3));
	  *dst++ = pix;
	  *dst++ = pix;
	}
      }
    }
    return; 
  }
}

void FBDisplay::BlitImage16BitColor(const uint16_t *srcImg, int width, int height, int xpos, int ypos)
{
  /*
  if((xpos + width) > m_screenWidth)
    width = m_screenWidth - xpos;
  if((ypos + height) > m_screenHeight)
    height = m_screenHeight - ypos;
  if(width < 0 || height < 0)
    return;
    
  int *fbp = (int *)m_fbp;
  
  // we are foolishly ignoring any stride here.
  for(int y = 0; y<height; y++) {
    int *dst = fbp + (m_screenWidth * (y + ypos)) + xpos;
    const uint16_t * src = srcImg + (y * width);
    for(int x = 0; x<width; x++) {
      const uint32_t r = (*src & MASK_R) >> SHIFT_R;
      const uint32_t g = (*src & MASK_G) >> SHIFT_G;
      const uint32_t b = (*src & MASK_B) >> SHIFT_B;
      ++src;      
      const int pix = (int)(uint32_t)(0xff000000 | ((b << 3) << 16) | ((g << 2) << 8) | (r << 3));
      *dst++ = pix;
    }
  }
  */
}

void FBDisplay::PutPixel(int x, int y, int color)
{
  if(x < 0 || x >= m_screenWidth || y<0 || y>=m_screenHeight)
    return;
  
  *((int *)(m_fbp) + x + (y * m_screenWidth)) = color;
}

void FBDisplay::PlotLine(int x0, int y0, int x1, int y1, int color)
{
  int dx = abs(x1 - x0);
  int sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0);
  int sy = y0 < y1 ? 1 : -1;
  int error = dx + dy;

  while(true) {
    PutPixel(x0, y0, color);
    if (x0 == x1 && y0 == y1)
      break;
    int e2 = 2 * error;
    if(e2 >= dy) {
      if(x0 == x1)
	break;
      error = error + dy;
      x0 = x0 + sx;
    }
    if(e2 <= dx) {
      if(y0 == y1)
	break;
      error = error + dx;
      y0 = y0 + sy;
    }
  }
}


void FBDisplay::DrawCircle(int x, int y, int radius, int color)
{
  constexpr float sin0 = sin(0.0f);
  constexpr float cos0 = cos(0.0f);
  
  int lastPx = x + sin0 * radius;
  int lastPy = y + cos0 * radius;
  
  for(int n = 1; n<=100; n++) {
    float a = (2.0 * M_PI * n) / 100.0f;
    int px = x + sin(a) * radius;
    int py = y + cos(a) * radius;
    PlotLine(lastPx, lastPy, px, py, color);
    lastPx = px;
    lastPy = py;
  }
}

void FBDisplay::DrawEllipse(int x, int y, int radiusX, int radiusY, int color)
{
  constexpr float sin0 = sin(0.0f);
  constexpr float cos0 = cos(0.0f);
  
  int lastPx = x + sin0 * radiusX;
  int lastPy = y + cos0 * radiusY;
  
  for(int n = 1; n<=100; n++) {
    float a = (2.0 * M_PI * n) / 100.0f;
    int px = x + sin(a) * radiusX;
    int py = y + cos(a) * radiusY;
    PlotLine(lastPx, lastPy, px, py, color);
    lastPx = px;
    lastPy = py;
  }
}

void FBDisplay::Clear()
{
  constexpr int clearcolor = 0xff000000;
  int *fptr = (int *)m_fbp;
  int *endPtr = fptr + (m_screenHeight * m_screenWidth);
  while(fptr < endPtr)
    *fptr++ = clearcolor;
}

void FBDisplay::Present()
{
  if(m_bpp == 32) 
    memcpy(m_realFbp, m_fbp, (m_screenHeight * m_screenWidth * m_bpp) / 8);
  else if(m_bpp == 16) {
    const int width = m_screenWidth;
    const int height = m_screenHeight;
    
    const unsigned char *src = (const unsigned char *)m_fbp;
    uint16_t *dst = (uint16_t *)m_realFbp;    
    
    // we are foolishly ignoring any stride here.
    for(int y = 0; y<height; y++) {
      uint16_t *dstRow = dst + (y * width);
      const unsigned char *srcRow = src + (y * width * 4);
      for(int x = 0; x<width; x++) {	
	const uint16_t r = (srcRow[2] >> 3) << SHIFT_R;
	const uint16_t g = (srcRow[1] >> 2) << SHIFT_G;
	const uint16_t b = (srcRow[0] >> 3) << SHIFT_B;
	srcRow += 4;
	*dstRow++ = (r | g | b);
      }
    }    
  }
}

bool FBDisplay::Open()
{
  // Open the framebuffer device file for reading and writing
  m_fbfd = open("/dev/fb0", O_RDWR);
  if (m_fbfd == -1) {
    printf("Error: cannot open framebuffer device.\n");
    return false;
  }
  printf("The framebuffer device opened.\n");

  // Get fixed screen information
  fb_fix_screeninfo finfo;  
  if (ioctl(m_fbfd, FBIOGET_FSCREENINFO, &finfo)) {
    printf("Error reading fixed information.\n");
    return false;
  }

  // Get variable screen information
  fb_var_screeninfo vinfo;  
  if (ioctl(m_fbfd, FBIOGET_VSCREENINFO, &vinfo)) {
    printf("Error reading variable screen info.\n");
    return false;
  }
  
  m_screenHeight = vinfo.yres;
  m_screenWidth = vinfo.xres;
  m_bpp = vinfo.bits_per_pixel;
  m_stride = m_screenWidth * 4;//finfo.line_length;


  if(!(vinfo.bits_per_pixel == 32 || vinfo.bits_per_pixel == 16)) {
    printf("Only supporting 32bpp or 16bpp\n");
    return false;
  }
  
  // map framebuffer to user memory 
  m_screensize = finfo.smem_len;

  m_realFbp = (char*)mmap(0, 
			  m_screensize,
			  PROT_READ | PROT_WRITE, 
			  MAP_SHARED, 
			  m_fbfd, 0);
  
  if (m_realFbp == (char *)-1) {
    printf("Failed to mmap.\n");
    return false;
  }

  m_tmpFbp.resize(m_screenWidth * m_screenHeight * 4);
  m_fbp = &m_tmpFbp[0];

  Clear();
  usleep(MICROS / 10);
  Clear();

  return true;
}

void FBDisplay::Close()
{
  if(m_realFbp)
    munmap(m_realFbp, m_screensize);
  if(m_fbfd)
    close(m_fbfd);
}

void FBDisplay::vlcLock(void **pPixels)
{
  *pPixels = m_vlcPixels;
}

void FBDisplay::vlcUnlock(const uint16_t *pixels)
{
  memcpy(m_vlcFrame, pixels, m_videoWidth * m_videoHeight * sizeof(uint16_t));
}

void FBDisplay::vlcDisplay()
{
  const int ypos = m_videoWindowY;
  if(true || GetScreenHeight() >= 1024) {
    const int xpos = (GetScreenWidth() - (m_videoWidth * 2)) / 2;        
    BlitImage16BitColorDoubleScale(m_vlcFrame, m_videoWidth, m_videoHeight, xpos, ypos);
  } else {
    const int xpos = (GetScreenWidth() - (m_videoWidth)) / 2;    
    BlitImage16BitColor(m_vlcFrame, m_videoWidth, m_videoHeight, xpos, ypos);
  }
  
  m_videoFrameObserver();
}

void FBDisplay::vlcStopEvent()
{
  printf("Stop event\n");
  m_videoStopObserver();
}

struct VLCImpl {
  libvlc_instance_t *libvlc = nullptr;
  libvlc_media_player_t *mp = nullptr;
};

bool FBDisplay::VideoPlay(const char *filename)
{
  VideoStop();
  
  char const *vlc_argv[] = {
    "--no-xlib" // Don't use Xlib.
    //   "--alsa-audio-device", "hw:1,0",
  };
  
  int vlc_argc = sizeof(vlc_argv) / sizeof(*vlc_argv);
  
  if(m_vlcImpl == nullptr)
    m_vlcImpl = new VLCImpl;
  
  if(!m_vlcImpl->libvlc) {
    m_vlcImpl->libvlc = libvlc_new(vlc_argc, vlc_argv);
    if(m_vlcImpl->libvlc == nullptr) {
      printf("LibVLC initialization failure.\n");
      return false;
    }
  }

  libvlc_media_t *m = libvlc_media_new_path(m_vlcImpl->libvlc, filename);
  if(m == nullptr) {
    printf("media new path fails\n");
    return false;
  }
  
  m_vlcImpl->mp = libvlc_media_player_new_from_media(m);
  if(m_vlcImpl->mp == nullptr) {
    printf("media new player from media fails\n");
    return false;
  }
    
  libvlc_media_release(m);

  if(!m_vlcFrame)
    m_vlcFrame = new uint16_t[m_videoHeight * m_videoWidth];
  if(!m_vlcPixels)
    m_vlcPixels = new uint16_t[m_videoHeight * m_videoWidth];

  libvlc_event_manager_t *eventManager = libvlc_media_player_event_manager(m_vlcImpl->mp);
  libvlc_event_attach(eventManager, libvlc_MediaPlayerStopped, VLCCallbacks::stopEvent, this);
  libvlc_video_set_callbacks(m_vlcImpl->mp, VLCCallbacks::lock, VLCCallbacks::unlock, VLCCallbacks::display, this);
  libvlc_video_set_format(m_vlcImpl->mp, "RV16", m_videoWidth, m_videoHeight, m_videoWidth * sizeof(uint16_t));
  libvlc_media_player_play(m_vlcImpl->mp);

  return true;
}

void FBDisplay::VideoStop()
{
  if(!m_vlcImpl)
    return;

  if(m_vlcImpl->mp) {
    libvlc_media_player_stop(m_vlcImpl->mp);
    libvlc_media_player_release(m_vlcImpl->mp);
    m_vlcImpl->mp = nullptr;
  }

  if(m_vlcImpl->libvlc) {
    libvlc_release(m_vlcImpl->libvlc);
    m_vlcImpl->libvlc = nullptr;
  }

  if(m_vlcPixels) {
    delete m_vlcPixels;
    m_vlcPixels = nullptr;
  }

  if(m_vlcFrame) {
    delete m_vlcFrame;
    m_vlcFrame = nullptr;
  }
}

