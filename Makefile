#
# This depends on the cairo library
# You may need to install it with: sudo apt install libcairo2-dev
#

CFLAGS?=-O2 -g -Wall -W $(shell pkg-config --cflags cairo)
LDLIBS+=$(shell pkg-config --libs cairo) -lm -lvlc
CC?=gcc
PROGNAME=quanterm

UNAME_M := $(shell uname -m)
ifneq ($(filter arm%,$(UNAME_M)),)
        LDFLAGS += -lwiringPi
endif

all: quanterm

%.o: %.c
	$(CC) $(CFLAGS) -c $<

%.o: %.cpp
	$(CXX) $(CFLAGS) -c $<

OBJS=main.o fb-display.o kbhit.o
$(PROGNAME): ${OBJS}
	$(CXX) -g -o $(PROGNAME) $(OBJS) $(LDFLAGS) $(LDLIBS)

clean:
	rm -f *.o $(PROGNAME)

zip: $(PROGNAME).tgz
	tar -czvf $(PROGNAME).tgz *.c *.cpp *.h *.hpp *.txt *.md *.html Makefile
