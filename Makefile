# Build tico.exe for 16-bit MS-DOS with Open Watcom.
#   make WATCOM=~/opt/watcom

WATCOM ?= $(HOME)/opt/watcom
export WATCOM
export INCLUDE := $(WATCOM)/h
export PATH := $(WATCOM)/binl64:$(WATCOM)/binl:$(PATH)

VERSION := $(shell cat VERSION)

CC     = wcl
CFLAGS = -q -bt=dos -mc -0 -os -s -wx -we -zq -k8192 '-DVERSION="$(VERSION)"'

# Host compiler, for the unit tests.
HOSTCC     = cc
HOSTCFLAGS = -std=c89 -pedantic -Wall -Wextra -Werror -O1

SRCS    = src/main.c src/editor.c src/buffer.c src/text.c src/dos.c
HEADERS = src/tico.h

all: tico.exe

# Objects go in build/, kept apart from the test build's.
tico.exe: $(SRCS) $(HEADERS) VERSION
	mkdir -p build/r
	cd build/r && $(CC) $(CFLAGS) -fe=../../$@ $(addprefix ../../,$(SRCS))

# The test build: keys from a script, screen dumps to a file.
tico_t.exe: $(SRCS) $(HEADERS) VERSION
	mkdir -p build/t
	cd build/t && $(CC) $(CFLAGS) -DTICO_TEST -fe=../../$@ \
	    $(addprefix ../../,$(SRCS))

build/test_text: tests/unit/test_text.c src/buffer.c src/text.c $(HEADERS)
	mkdir -p build
	$(HOSTCC) $(HOSTCFLAGS) -o $@ tests/unit/test_text.c src/buffer.c src/text.c

unit: build/test_text
	./build/test_text

test: unit tico.exe tico_t.exe
	sh tests/run.sh $(CURDIR)/tico.exe $(CURDIR)/tico_t.exe

clean:
	rm -rf *.o *.obj *.err tico.exe tico_t.exe build

.PHONY: all unit test clean
