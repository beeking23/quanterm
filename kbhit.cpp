/*
Copyright (c) 2024 Carri King

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/																																																	  
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>

#if !defined(__x86_64__)

// if its not building for x86 then it must the Pi of course...
#include <wiringPi.h>

constexpr int ButtonPins[8] = {
  14, 18, 24, 8,
  3, 17, 22, 9
};

constexpr int LedPins[8] = {
  15, 23, 25, 7,
  4, 27, 10, 11
};

int g_initButtonStates[8] = {0};

bool g_idling = false;
double g_timeMS = 0.0;


// returns an character code corresponding to a GPIO connected button or 0.
char ReadGPIOEmulatedChar()
{
  static bool GPIOInit = false;
  if(!GPIOInit) {
    wiringPiSetupGpio();
    for(int n = 0; n<8; n++) {
      pinMode(ButtonPins[n], INPUT);
      pullUpDnControl(ButtonPins[n], PUD_UP);      
      pinMode(LedPins[n], OUTPUT);
      // note these are inverted
      digitalWrite(LedPins[n], HIGH);      
    }

    for(int n = 0; n<8; n++) {
      int v = digitalRead(ButtonPins[n]);
      g_initButtonStates[n] = v;
      printf("GPIO button %i(%i) state %i\n", n, ButtonPins[n], v);

      for(int t = 0; t<2; t++) {
	printf("Blinking %i %i\n", n, LedPins[n]);
	constexpr int nswait = 1000 * 100;
	digitalWrite(LedPins[n], LOW);
	usleep(nswait);
	digitalWrite(LedPins[n], HIGH);
	usleep(nswait);
      }
    }    
    
    GPIOInit = true;    
  }

  int lightToBlink = -1;
  if(g_idling)
    lightToBlink = int(g_timeMS / 200.0) % 4;
  
  for(int n = 0; n<8; n++) {
    if(g_initButtonStates[n] == 1) {
      int v = digitalRead(ButtonPins[n]);
      digitalWrite(LedPins[n], v && (lightToBlink != (n&3)) ? HIGH : LOW);
      if(!v) {
	printf("GPIO button %i down\n", n);
	return '1' + n;
      }
    }
  }

  return 0;
}

void SetGPIOAttractorState(bool idling, double timeMS)
{
  g_idling = idling;
  g_timeMS = timeMS;
}

#else

char ReadGPIOEmulatedChar()
{
  return 0;
}

void SetGPIOAttractorState(bool, double)
{

}

#endif

static bool g_headless = false;

void SetKbHeadless(bool b)
{
  g_headless = b;
  if(g_headless) {
    printf("Quanterm is headless\n");
    return;
  }  
}

void EnableRawMode()
{
  if(g_headless)
    return;
  
  termios term;
  if(tcgetattr(0, &term) < 0)
    return;
  
  term.c_lflag &= ~(ICANON | ECHO); // Disable echo as well
  tcsetattr(0, TCSANOW, &term);
  
  printf("Enabled raw mode\n");

}

void DisableRawMode()
{
  if(g_headless)
    return;  
  
  termios term;
  if(tcgetattr(0, &term) < 0)
    return;
    
  term.c_lflag |= ICANON | ECHO;
  tcsetattr(0, TCSANOW, &term);
  
  printf("Disabled raw mode\n");    
}



bool Kbhit()
{
  char ch = ReadGPIOEmulatedChar();
  if(ch != 0)
    return true;

  if(g_headless)
    return false;
    
  int byteswaiting = 0;
  if(ioctl(0, FIONREAD, &byteswaiting) < 0) {
    printf("Failed ioctl\n");
    return 0;
  }
  
  if(byteswaiting != 0)
    printf("Bytes waiting %i\n", byteswaiting);
  return byteswaiting > 0;
}

char ReadChar()
{
  char ch = ReadGPIOEmulatedChar();
  if(ch != 0)
    return ch;

  if(g_headless)
    return 0;  
  
  int s = read(0,&ch,1);
  if(s > 0)
    return ch;
  else
    return 0;
}
