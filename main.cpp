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

#include <map>

FBDisplay& DisplayInst() {
  static FBDisplay g_display;
  return g_display;
}

typedef cairo_t *CairoPtr;
CairoPtr& CairoInst() {
  static CairoPtr g_cr = nullptr;
  return g_cr;
}

/// Little container class for a data property from the page config file.
/// Total overkill :)
template<typename T> class QuanTermProp {
public:
  QuanTermProp(const std::string& name, T *pValue, T initValue)
  {
    *pValue = initValue;
    GetValueMap()[name] = pValue;
  }

  typedef std::map<std::string, T *> ValueMap;
  static ValueMap& GetValueMap() {
    static ValueMap vMap;
    return vMap;
  }
  
  static bool SetProp(const std::string& name, T value) {
    auto& vMap = GetValueMap();
    auto itr = vMap.find(name);
    if(itr == vMap.end())
      return false;
    *itr->second = value;
    return true;
  }
};

/// A vector of 3 floats making an  rgb colour
struct QRGB {
  QRGB() : r(1.0), g(1.0), b(1.0) { }  
  QRGB(float r_, float g_, float b_) : r(r_), g(g_), b(b_) { }
  float r, g, b;
};

/// Overrides the usual cairo C API to take our own colour vector.
void cairo_set_source_rgb(CairoPtr pCairo, const QRGB& colour)
{
  cairo_set_source_rgb(pCairo, colour.r, colour.g, colour.b);
}

#define DEF_Q_DOUBLE(name, initValue) double name; QuanTermProp<double> m_##name = QuanTermProp<double>( #name, &name, (double)initValue)
#define DEF_Q_COLOUR(name, initValue) QRGB name; QuanTermProp<QRGB> m_##name = QuanTermProp<QRGB>( #name, &name, initValue)

/// The configuration for the page rendering, sizes and colours etc
class QuanTermPageConfig {
public:
  DEF_Q_DOUBLE(FontSizeNormal, 14);
  DEF_Q_DOUBLE(FontSizeHeading, 18);
  DEF_Q_DOUBLE(CharHeight, 18);
  DEF_Q_DOUBLE(CharWidth, 5);
  DEF_Q_DOUBLE(MarginX, 100);
  DEF_Q_DOUBLE(MarginY, 20);
  DEF_Q_DOUBLE(ButtonHeight, 18 + 3);
  DEF_Q_DOUBLE(ButtonBorder, 3);
  DEF_Q_DOUBLE(ScrollSpeed, 5);
  DEF_Q_DOUBLE(VideoPosY, 40);  
  

  DEF_Q_COLOUR(TextColour, QRGB(0.0f, 1.0f, 0.0f));
  DEF_Q_COLOUR(TextBackgroundColour, QRGB(0.0f, 0.0f, 0.0f));
  DEF_Q_COLOUR(ImageBorderColour, QRGB(0.0, 0.4, 0.0));
  DEF_Q_COLOUR(ButtonColour, QRGB(0.0, 1.0, 1.0));
  
  bool LoadPageConfig(const char *filename);

};

bool QuanTermPageConfig::LoadPageConfig(const char *filename)
{
  std::ifstream file(filename);
  if(!file) {
    printf("Failed to read: '%s'\n", filename);
    return false;
  }

  for(std::string line; std::getline(file, line); ) {
    if(!line.length())
      continue;

    auto pos = std::string::npos;
    pos = line.find("=");
    if(pos == std::string::npos) {
      printf("Bad config line (no '='): '%s'\n", line.c_str());
      return false;
    }
    std::string name = line.substr(0, pos);
    if(!name.length()) {
      printf("Bad config line (empty name): '%s'\n", line.c_str());
      return false;      
    }
    
    std::string value = line.substr(pos+1, -1);
    if(!value.length()) {
      printf("Bad config line (empty value): '%s'\n", line.c_str());
      return false;      
    }

    printf("Read '%s' -> '%s'\n", name.c_str(), value.c_str());

    // is the a vector (color) ?
    if(value[0] == '[' && value[value.length()-1] == ']') {
      float r, g, b;
      int c = sscanf(value.c_str(), "[%f,%f,%f]", &r, &g, &b);
      if(c != 3) {
	printf("Malformed vec3 (colour): '%s'\n", value.c_str());
	return false;
      }
      if(!QuanTermProp<QRGB>::SetProp(name, QRGB(r, g, b))) {
	printf("Unknown config colour '%s'\n", name.c_str());
	return false;
      }
      continue;
      
    }

    // is this a string?
    if(value[0] == '"' && value[value.length()-1] == '"') {
      // TODO: implement string.
      continue;
    }

    // otherwise, its a number...
    if(!QuanTermProp<double>::SetProp(name, atof(value.c_str()))) {
      printf("Unknown config number '%s'\n", name.c_str());
      return false;
    }
  }

  return true;
}

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
  /// Prints text with automatic wrapping ingnoring any existing newlines.  Returns new y position.
  int ShowWrappedText(const char *txt, const int x, int y, const int maxWidth);
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
  enum {PREFORMAT_OFF, PREFORMAT_STORE, PREFORMAT_OUTPUT};
  int m_preformat = PREFORMAT_OFF;

  QuanTermPageConfig m_pageCfg;
};

int QuanTermApp::ShowWrappedText(const char *txt, const int x, int y, const int maxWidth)
{
  // save position before word
  // add a word
  // see how long
  // too long?
  //   print position before word
  //   rewind to word and repeat
  // no?
  //  repeat.

  cairo_select_font_face (CairoInst(), "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(CairoInst(), m_pageCfg.FontSizeNormal);

  // cheap and dirty way to get the line spacing.
  cairo_text_extents_t heightExtents;
  cairo_text_extents(CairoInst(), "My", &heightExtents);
  heightExtents.height += 4.0;
  
  cairo_text_extents_t extents;
  
  std::string line;
  std::string word;
  std::string extendedLine;

  auto TextOut = [&](const std::string& text) {
    cairo_text_extents(CairoInst(), text.c_str(), &extents);    
    cairo_move_to(CairoInst(), x, y);
    cairo_rectangle(CairoInst(), x, y - extents.height +2, extents.width, extents.height);    
    cairo_set_source_rgb(CairoInst(), m_pageCfg.TextBackgroundColour);
    cairo_fill(CairoInst());
    
    cairo_move_to(CairoInst(), x, y);
    cairo_set_source_rgb(CairoInst(), m_pageCfg.TextColour);
    cairo_show_text(CairoInst(), text.c_str());
    
    y += heightExtents.height;
  };
    
  do {
    // if this is not whitespace or null then add it to the line.
    if(*txt != 0 && *txt != ' ' && *txt != '\n') {
      word += *txt++;
      continue;
    }

    // skip the whitespace character if its not null
    if(*txt)
      ++txt;
    
    // do we have anything to print yet?
    if(word.length() > 0) {
      // it it longer than the space allowed?
      if(line.length() > 0)
	extendedLine = line + std::string(" ") + word;
      else
	extendedLine = word;

      cairo_text_extents(CairoInst(), extendedLine.c_str(), &extents);

      if(extents.width > maxWidth) {
	// it is too long, print from the previous word if there was one.
	// TODO: need have there not being a previous word, which would mean there is no
	// natural break in the line. The current behaviour is print nothing and hope the
	// author fixes it.
	TextOut(line);

	line = word;
      } else {
	line = extendedLine;
      }
      word = "";
    }
  } while(*txt);

  TextOut(line);// + std::string(" ") + word);
  return y;
}

/// Renders out multple text lines which are split by newline characters.
void QuanTermApp::ShowTextMultiline(const char *txt, const int xorigin, const int yorigin)
{
  int ypos = yorigin;

  // cheap and dirty way to get the line spacing.
  cairo_text_extents_t heightExtents;
  cairo_text_extents(CairoInst(), "My", &heightExtents);
  heightExtents.height += 4.0;  

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
  heightExtents.height += 4.0;  

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
  
  if(!m_bold && !m_image && !m_heading && m_preformat == PREFORMAT_OFF) {
    m_ypos = ShowWrappedText(curText.c_str(), m_xpos, m_ypos, DisplayInst().GetScreenWidth() - (m_pageCfg.MarginX * 2));
    m_xpos = m_pageCfg.MarginX;    
    return;
  }

  cairo_text_extents_t heightExtents;
  cairo_text_extents(CairoInst(), "My", &heightExtents);
  heightExtents.height += 4.0;  
  
  cairo_select_font_face (CairoInst(), "monospace", CAIRO_FONT_SLANT_NORMAL, m_bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(CairoInst(), m_heading ? m_pageCfg.FontSizeHeading : m_pageCfg.FontSizeNormal);

  cairo_text_extents_t extents;
  cairo_text_extents(CairoInst(), curText.c_str(), &extents);

  int tx = m_heading ? (DisplayInst().GetScreenWidth() - extents.width) / 2 : m_xpos;

  cairo_rectangle(CairoInst(), tx, m_ypos - extents.height +2, extents.width, extents.height);    
  cairo_set_source_rgb(CairoInst(), m_pageCfg.TextBackgroundColour);
  cairo_fill(CairoInst());
  
  cairo_move_to(CairoInst(), tx , m_ypos);
  cairo_set_source_rgb(CairoInst(), m_pageCfg.TextColour);
  cairo_show_text(CairoInst(), curText.c_str());
  m_xpos = m_pageCfg.MarginX;
  m_ypos += heightExtents.height;
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
    double targetWidth = DisplayInst().GetScreenWidth() - (m_pageCfg.MarginX * 2);
    double targetHeight = targetWidth * aspect;
    cairo_surface_set_device_scale(imageData, width / targetWidth, height / targetHeight);
    cairo_set_source_surface(CairoInst(), imageData, m_xpos, m_ypos);
    cairo_paint(CairoInst());

    cairo_rectangle(CairoInst(), m_xpos-1, m_ypos-1, targetWidth+1, targetHeight+1);    
    cairo_set_source_rgb(CairoInst(), m_pageCfg.ImageBorderColour);
    cairo_stroke(CairoInst());	  

    m_ypos += targetHeight;
    cairo_restore(CairoInst());
  }
  
  m_xpos = m_pageCfg.MarginX;
  m_ypos += m_pageCfg.CharHeight;
}

/// Renders a page onto the screen. The data in content is largely streamable so its possible to stop
/// at any point. 'howmuch' controls how many characters from content are rendered, this allows then
/// it to be animated simulating a slow update like on an old 8bit machine.
void QuanTermApp::RenderPageContent(const std::string& content, int howMuch)
{
  m_xpos = m_pageCfg.MarginX;
  m_ypos = m_pageCfg.MarginY;
  m_bold = false;
  m_heading = false;
  m_image = false;
  m_preformat = PREFORMAT_OFF;
  
  const char *ptr = content.c_str();
  const char *endP = ptr + howMuch;
  
  std::string curText;
  
  while(ptr < endP) {    
    if(m_preformat == PREFORMAT_STORE) {
      if(*ptr == '\n') {
	m_preformat = PREFORMAT_OUTPUT;
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
	  m_preformat = PREFORMAT_STORE;
	++ptr;
      }
      continue;
    case '_': // bold
      RenderText(curText);
      curText = "";
      m_bold = !m_bold;
      break;
    case '[': // start image
      RenderText(curText);
      curText = "";      
      m_image = true;
      break;
    case ']': // end image
      RenderImage(curText);
      curText = "";
      m_image = false;
      break;
    case '=': // heading
      RenderText(curText);
      curText = "";      
      m_heading = !m_heading;      
      break;
    case '\n': // new line
      if(m_preformat == PREFORMAT_OUTPUT) {
	RenderText(curText);
	m_preformat = PREFORMAT_OFF;
	curText = "";
      } else {
	if(ptr != content.c_str() && *(ptr - 1) == '\n') {
	  RenderText(curText);
	  curText = "";
	} else
	  curText += ' ';
      }
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
  cairo_set_font_size(CairoInst(), m_pageCfg.FontSizeNormal);
  cairo_set_source_rgb(CairoInst(), m_pageCfg.ButtonColour);
    
  auto DrawButton = [&](const QuanTermApp::ButtonData& btnData, const int side, const int num) {
    cairo_set_source_rgb(CairoInst(), m_pageCfg.ButtonColour);
    int xpos = (side ? DisplayInst().GetScreenWidth() - m_pageCfg.MarginX : 0) + m_pageCfg.ButtonBorder;
    int ypos = (m_pageCfg.ButtonHeight/2) + ((num * 2) * m_pageCfg.ButtonHeight) + m_pageCfg.ButtonBorder;
    int xsize = m_pageCfg.MarginX - (m_pageCfg.ButtonBorder * 2);
    int ysize = m_pageCfg.ButtonHeight - (m_pageCfg.ButtonBorder * 2);
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
  m_pageLen = m_pageData.size();
  m_pageProgress = 0;  
  
  DisplayInst().Clear();
  RenderSideButtons(m_buttons);
  cairo_surface_flush(cairo_get_target(CairoInst()));
  DisplayInst().Present();  
}

void QuanTermApp::RenderCurrentPage()
{
  if(m_pageProgress == m_pageLen) {
    DisplayInst().Clear();
    RenderSideButtons(m_buttons);
  }
  
  RenderPageContent(m_pageData, m_pageProgress);
  cairo_surface_flush(cairo_get_target(CairoInst()));  
  DisplayInst().Present();    
}

void QuanTermApp::HandleButtonPress(int n, const std::vector<QuanTermApp::ButtonData>& buttons)
{
  if(n < 0)
    return;
  if(n >= (int)buttons.size())
    return;

  const auto& cmd = buttons[n].m_cmd;
  auto dotPos = cmd.rfind('.');
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
  
  printf("Framebuffer: %i x %i\n", DisplayInst().GetScreenWidth(), DisplayInst().GetScreenHeight());

  char pcFile[512];
  sprintf(pcFile, "page-config-%ix%i.txt", DisplayInst().GetScreenWidth(), DisplayInst().GetScreenHeight());
  if(!m_pageCfg.LoadPageConfig(pcFile)) {
    printf("Failed to load page config: %s\n", pcFile);
    return false;
  }
  
  DisplayInst().SetVideoWindowX(m_pageCfg.MarginX);
  DisplayInst().SetVideoWindowY(m_pageCfg.VideoPosY);
  DisplayInst().SetVideoWindowWidth(DisplayInst().GetScreenWidth() - (m_pageCfg.MarginX * 2));

  usleep(1 * 1000000);  
  DisplayInst().Clear();
  DisplayInst().Present();    
  usleep(1 * 1000000);

  DisplayInst().Clear();
  auto DrawFilledCircle = [&](int x, int y, int radius, int color) {
    for(int n = 1; n<radius; n++) {
      DisplayInst().DrawCircle(x, y, n, color);
    }
  };
  
  const int r = 100;
  const int hw = DisplayInst().GetScreenWidth() / 2;
  const int hh = DisplayInst().GetScreenHeight() / 2;
  
  DrawFilledCircle(hw - r, hh - r, r, 0xff0000ff);
  DisplayInst().Present();
  usleep(150 * 1000);
  
  DrawFilledCircle(hw + r, hh - r, r, 0xff00ff00);
  DisplayInst().Present();
  usleep(250 * 1000);
  
  DrawFilledCircle(hw + r, hh + r, r, 0xffff0000);
  DisplayInst().Present();
  usleep(250 * 1000);
  
  DrawFilledCircle(hw - r, hh + r, r, 0xff888888);            
  DisplayInst().Present();    
  usleep(1 * 1000000);
  DisplayInst().Clear();      

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
  char lastChar = 0;
  double lastKeyTime = GetTimeMS();
  
  while(!quit) {
    if(m_pageProgress < m_pageLen) {
      m_pageProgress = std::min(m_pageLen, m_pageProgress+int(m_pageCfg.ScrollSpeed));
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
      if(c != lastChar || (GetTimeMS() - lastKeyTime) > 2000) {
	if(c == 'q') {
	  quit = true;
	} else if(c >= '1' && c <= '8') {
	  int btn = c - '1';
	  HandleButtonPress(btn, m_buttons);
	}
	lastChar = c;
	lastKeyTime = GetTimeMS();
      }
    }
    
    lastTime = GetTimeMS();    
  }
  DisableRawMode();  
  
  cairo_destroy(CairoInst());
  cairo_surface_destroy(surface);
  
  return 0;
}


int main(int, char **)
{
  QuanTermApp theApp;
  return theApp.AppMain();
}
