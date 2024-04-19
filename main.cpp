/*
Copyright (c) 2024 Carri King

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/																																																	  
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <chrono>
#include <algorithm>
#include <cctype>

#include <cairo.h>

#include "fb-display.h"
#include "kbhit.h"

FBDisplay& DisplayInst() {
  static FBDisplay g_display;
  return g_display;
}

typedef cairo_t *CairoPtr;

CairoPtr& CairoInst() {
  static CairoPtr g_cr = nullptr;
  return g_cr;
}

const double FontSizeNormal = 14.0f;
const double FontSizeHeading = 18.0f;
const int CharHeight = 18;
const int CharWidth = 5;
const int MarginX = 100;
const int MarginY = 20;
const int ButtonHeight = CharHeight * 3;
const int ButtonBorder = 3;

double GetTimeMS()
{
  static const auto start = std::chrono::high_resolution_clock::now();
  const auto current = std::chrono::high_resolution_clock::now();
  const std::chrono::duration<double, std::milli> delta = current - start;
  return delta.count();
}

class QuanTermApp {
protected:
  /// holds the data for each button - the kiosk has 8, 4 down each size.
  struct ButtonData {
    std::string m_caption;
    std::string m_cmd;
  };

  /// Renders mulitple lines split by newlines
  void ShowTextMultiline(const char *txt, const int xorigin, const int yorigin);
  /// calculates the size of mulitple lines split by newlines.
  void SizeTextMultiline(const char *txt, int& width, int& height);
  /// reads a specially crafted file contain a page.
  bool ReadPageData(const std::string& filename, std::string& content, std::vector<ButtonData>& buttons);
  
private:
  /// internal to RenderPageContent - renders the current text at the current location and current formatting
  void RenderText(const std::string& curText);
  /// internal to RenderPageContent - renders the current image at the current location and current formatting  
  void RenderImage(const std::string& curText);
  
protected:
  /// renders the page text that was loaded from ReadPageData
  void RenderPageContent(const std::string& content, int howMuch);
  /// render the side buttons.
  void RenderSideButtons(const std::vector<ButtonData>& buttons);
  /// Loads a new page replacing m_pageData and m_buttons
  void LoadNewPage(const std::string& filename);
  /// renders the currently loaded pages at its current progress level.
  void RenderCurrentPage();
  /// responds to a button press.
  void HandleButtonPress(int n, const std::vector<ButtonData>& buttons);
  
public:
  int AppMain();

private:
  std::string m_pageData;
  std::vector<ButtonData> m_buttons;
  int m_pageProgress = 0;
  int m_pageLen = 0;

  int m_xpos = 0;
  int m_ypos = 0;
  bool m_bold = false;
  bool m_heading = false;
  bool m_image = false;  
};


/// Renders out multple text lines which are split by newline characters.
void QuanTermApp::ShowTextMultiline(const char *txt, const int xorigin, const int yorigin)
{
  int ypos = yorigin;

  // cheap and dirty way to get the line spacing.
  cairo_text_extents_t heightExtents;
  cairo_text_extents(CairoInst(), "My", &heightExtents);

  ypos += heightExtents.height;
  
  std::string line;
  while(*txt) {
    if(*txt == '\n') {
      if(line.length()) {
	cairo_move_to(CairoInst(), xorigin, ypos);
	cairo_show_text(CairoInst(), line.c_str());
	line = "";
      }
      ypos += heightExtents.height;
    } else {
      line += *txt;
    }
    ++txt;
  }
  
  if(line.length()) {
    cairo_move_to(CairoInst(), xorigin, ypos);
    cairo_show_text(CairoInst(), line.c_str());
  }
}

/// Gets the size of a block of text.
void QuanTermApp::SizeTextMultiline(const char *txt, int& width, int& height)
{
  width = 0;
  height = 0;
  
  cairo_text_extents_t heightExtents;
  cairo_text_extents(CairoInst(), "My", &heightExtents);

  std::string line;
  while(*txt) {
    if(*txt == '\n') {
      if(line.length()) {
	cairo_text_extents_t lineExtents;
	cairo_text_extents(CairoInst(), line.c_str(), &lineExtents);
	width = std::max(width, int(lineExtents.width));
	line = "";
      }
      height += int(heightExtents.height);
    } else {
      line += *txt;
    }
    ++txt;
  }
  
  if(line.length()) {
    cairo_text_extents_t lineExtents;
    cairo_text_extents(CairoInst(), line.c_str(), &lineExtents);
    width = std::max(width,  int(lineExtents.width));
    height += int(heightExtents.height);    
  }
}

/// Read a file containing a page of text, images, stuff and button definitions.
bool QuanTermApp::ReadPageData(const std::string& filename, std::string& content, std::vector<QuanTermApp::ButtonData>& buttons)
{
  std::ifstream file(filename);
  if(!file) {
    printf("Failed to read: '%s'\n", filename.c_str());
    return false;
  }

  const std::string newline("\n");
  content = "";
  buttons.clear();
  
  for(std::string line; std::getline(file, line); ) {
    if(!line.length()) {
      content += newline;
      continue;
    }

    // comment
    if(line[0] == '#')
      continue;

    // button
    if(line[0] == '$') {
      ButtonData btnData;
      const char *ptr = line.c_str();
      // skip the '$'
      ++ptr;
      while(*ptr && *ptr != '!') {
	if(*ptr == '\\') {
	  ++ptr;
	  if(*ptr && *ptr == 'n') {
	    ++ptr;
	    btnData.m_caption.push_back('\n');
	  }
	} else {	    
	  btnData.m_caption.push_back(*ptr++);
	}
      }
      
      // did we reach the end or a '!' ??
      if(*ptr) {
	// skip the '!'
	++ptr;
	while(*ptr)
	  btnData.m_cmd.push_back(*ptr++);
      }
      buttons.push_back(btnData);
      continue;
    }
    
    content += line;
    content += newline;
  }

  printf("--\n%s\n--\n", content.c_str());
  return true;
}

/// Renders a single text line
void QuanTermApp::RenderText(const std::string& curText)
{
  if(!curText.length())
    return;
    
  cairo_select_font_face (CairoInst(), "monospace", CAIRO_FONT_SLANT_NORMAL, m_bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(CairoInst(), m_heading ? FontSizeHeading : FontSizeNormal);

  cairo_text_extents_t extents;
  cairo_text_extents(CairoInst(), curText.c_str(), &extents);

  int tx = m_heading ? (DisplayInst().GetScreenWidth() - extents.width) / 2 : m_xpos;

  cairo_rectangle(CairoInst(), tx, m_ypos - extents.height +2, extents.width, extents.height);    
  cairo_set_source_rgb(CairoInst(), 0.0, 0.0, 0.1);    
  cairo_fill(CairoInst());
  
  cairo_move_to(CairoInst(), tx , m_ypos);
  cairo_set_source_rgb(CairoInst(), 0.0, 1.0, 0.0);    
  cairo_show_text(CairoInst(), curText.c_str());
  if(m_heading) {
    m_xpos = MarginX;
    m_ypos += CharHeight;
  } else {	 
    m_xpos += curText.length() * CharWidth;
  }
}

/// Renders an image, loading it if needs be, cached the last loaded image.
void QuanTermApp::RenderImage(const std::string& curText)
{
  static std::string currentImageFile;
  static cairo_surface_t *currentImageData = nullptr;
    
  if(!curText.length())
    return;
  
  if(curText != currentImageFile) {
    if(currentImageData) {
      cairo_surface_destroy(currentImageData);
      currentImageFile = "";
    }
    currentImageFile = curText;
    currentImageData = cairo_image_surface_create_from_png(curText.c_str());
  }
    
  if(currentImageData) {
    cairo_surface_t *imageData = currentImageData;
    cairo_save(CairoInst());	
    double height = cairo_image_surface_get_height(imageData);
    double width = cairo_image_surface_get_width(imageData);
    double aspect = height / width;
    double targetWidth = DisplayInst().GetScreenWidth() - (MarginX * 2);
    double targetHeight = targetWidth * aspect;
    cairo_surface_set_device_scale(imageData, width / targetWidth, height / targetHeight);
    cairo_set_source_surface(CairoInst(), imageData, m_xpos, m_ypos);
    cairo_paint(CairoInst());

    cairo_rectangle(CairoInst(), m_xpos-1, m_ypos-1, targetWidth+1, targetHeight+1);    
    cairo_set_source_rgb(CairoInst(), 0.0, 0.4, 0.0);    
    cairo_stroke(CairoInst());	  

    m_ypos += targetHeight;
    cairo_restore(CairoInst());
  }
  
  m_xpos = MarginX;
  m_ypos += CharHeight;
}

/// Renders a page onto the screen. The data in content is largely streamable so its possible to stop
/// at any point. 'howmuch' controls how many characters from content are rendered, this allows then
/// it to be animated simulating a slow update like on an old 8bit machine.
void QuanTermApp::RenderPageContent(const std::string& content, int howMuch)
{
  m_xpos = MarginX;
  m_ypos = MarginY;
  m_bold = false;
  m_heading = false;
  m_image = false;  
  
  const char *ptr = content.c_str();
  const char *endP = ptr + howMuch;
  
  std::string curText;
  bool preformat = false;
  
  while(ptr < endP) {    
    if(preformat) {
      if(*ptr == '\n') {
	preformat = false;
      } else {
	curText += *ptr++;
	continue;
      }
    }
    
    switch(*ptr) {
    case '\\': // escape;
      ++ptr;
      if(ptr != endP) {
	if(*ptr == 'n')
	  curText += '\n';
	else if(*ptr == '+')
	  preformat = true;
	++ptr;
      }
      continue;
    case '_': // bold
      RenderText(curText);
      curText = "";
      m_bold = !m_bold;
      break;
    case '[':
      RenderText(curText);
      m_image = true;
      break;
    case ']':
      RenderImage(curText);
      curText = "";
      m_image = false;
      break;
    case '=': // heading
      RenderText(curText);
      curText = "";      
      m_heading = !m_heading;      
      break;
    case '\n':
      RenderText(curText);
      curText = "";      
      m_ypos += CharHeight;
      m_xpos = MarginX;
      break;
    default:
      curText += *ptr;
      break;
    }
    ++ptr;
  }

  if(!m_image)
    RenderText(curText);
}

/// Render the buttons down the side of the screen.
void QuanTermApp::RenderSideButtons(const std::vector<QuanTermApp::ButtonData>& buttons)
{
  cairo_select_font_face (CairoInst(), "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(CairoInst(), FontSizeNormal);
  cairo_set_source_rgb(CairoInst(), 0.0, 1.0, 1.0);
    
  auto DrawButton = [&](const QuanTermApp::ButtonData& btnData, const int side, const int num) {
    cairo_set_source_rgb(CairoInst(), 0.0, 1.0, 1.0);
    int xpos = (side ? DisplayInst().GetScreenWidth() - MarginX : 0) + ButtonBorder;
    int ypos = ((1 + (num * 2)) * ButtonHeight) + ButtonBorder;
    int xsize = MarginX - (ButtonBorder * 2);
    int ysize = ButtonHeight - (ButtonBorder * 2);
    cairo_rectangle(CairoInst(), xpos, ypos, xsize, ysize);
    cairo_stroke(CairoInst());

    const auto caption = btnData.m_caption.c_str();
    int textWidth, textHeight;
    SizeTextMultiline(caption, textWidth, textHeight); 
    ShowTextMultiline(caption, xpos + (xsize - textWidth) / 2, ypos + (ysize/2) - (textHeight / 2));
  };
  
  for(int s = 0; s<2; s++) {
    for(int n = 0; n<4; n++) {
      size_t idx = (s * 4) + n;
      if(idx < buttons.size())
	DrawButton(buttons[idx], s, n);
      else
	return;
    }
  }
}

void QuanTermApp::LoadNewPage(const std::string& filename)
{
  m_pageData = "";
  
  if(!ReadPageData(filename, m_pageData, m_buttons))
    return;
  DisplayInst().VideoStop();  
  m_pageProgress = 0;
  m_pageLen = m_pageData.size();
  
  DisplayInst().Clear();
  RenderSideButtons(m_buttons);    
}

void QuanTermApp::RenderCurrentPage()
{
  if(m_pageProgress == m_pageLen) {
    DisplayInst().Clear();
    RenderSideButtons(m_buttons);
  }
  
  RenderPageContent(m_pageData, m_pageProgress);
}

void QuanTermApp::HandleButtonPress(int n, const std::vector<QuanTermApp::ButtonData>& buttons)
{
  if(n < 0)
    return;
  if(n >= (int)buttons.size())
    return;

  const auto& cmd = buttons[n].m_cmd;
  int dotPos = cmd.rfind('.');
  if(dotPos <= 0 || dotPos == std::string::npos) {
    if(cmd == "video_stop")  {
      // redraw the page after stopping to remove the overlaid video image.
      DisplayInst().VideoStop();
      RenderCurrentPage();
    }
  } else {
    std::string ext = cmd.substr(dotPos, std::string::npos);
    printf("%s\n", ext.c_str());
    for(auto& c : ext)
      c = std::tolower(c);
    if(ext == ".mp4") {
      // force the page load animation to finish so it doesn't interfere with the video.
      m_pageProgress = m_pageLen;
      RenderCurrentPage();    
      DisplayInst().VideoPlay(cmd.c_str());
    } else if(ext == ".txt") {
      LoadNewPage(cmd);
    }
  }
}

int QuanTermApp::AppMain()
{
  if(!DisplayInst().Open()) {
    printf("Failed to open framebuffer\n");
    return 0;
  }

  DisplayInst().SetVideoWindowX(MarginX);
  DisplayInst().SetVideoWindowY(MarginX);
  DisplayInst().SetVideoWindowWidth(DisplayInst().GetScreenWidth() - (MarginX * 2));

  printf("Framebuffer: %i x %i\n", DisplayInst().GetScreenWidth(), DisplayInst().GetScreenHeight());

  usleep(1 * 1000000);  
  DisplayInst().Clear();
  usleep(2 * 1000000);    
  
  cairo_surface_t *surface = cairo_image_surface_create_for_data((unsigned char *)DisplayInst().GetSurfacePtr(),
								 CAIRO_FORMAT_ARGB32, 
								 DisplayInst().GetScreenWidth(),
								 DisplayInst().GetScreenHeight(),
								 DisplayInst().GetStride());
  CairoInst() = cairo_create(surface);

  LoadNewPage("index.txt");

  
  EnableRawMode();
  bool quit = false;
  double lastTime = GetTimeMS();
  while(!quit) {
    if(m_pageProgress < m_pageLen) {
      m_pageProgress = std::min(m_pageLen, m_pageProgress+5);
      RenderCurrentPage();
    }
    
    double elapsed = lastTime - GetTimeMS();
    constexpr double FrameTime = 1000.0 / 60.0;
    if(elapsed < FrameTime) {
      double toSleep = FrameTime - elapsed;
      usleep(1000 * toSleep);
    }
    if(Kbhit()) {  
      char c = ReadChar();
      if(c == 'q') {
	quit = true;
      } else if(c >= '1' && c <= '8') {
	int btn = c - '1';
	HandleButtonPress(btn, m_buttons);
      }
    }
    lastTime = GetTimeMS();    
  }
  DisableRawMode();  
  
  cairo_destroy(CairoInst());
  cairo_surface_destroy(surface);
  
  return 0;
}


int main(int argc, char **argv)
{
  QuanTermApp theApp;
  return theApp.AppMain();
}
