# Build tico.exe for 16-bit MS-DOS with Open Watcom and libc16.
#   make                         fetch the pinned libc16 release into
#                                build/libc16, then build tico.exe
#   make LIBC16_ROOT=../libc16   use a libc16 checkout built with its make
#
# tico.exe is a compact-model program (-mc), compiled and linked against
# libc16's compact library only (its ABI is in libc16's docs/ABI.md), with
# the Open Watcom snapshot libc16 was built with: the libc16 tree's
# tools/fetch-watcom fetches it into build/tools.

LIBC16_ROOT ?= build/libc16
# A release tree, or a checkout, whose make stages the same layout in
# build/stage.
LIBC16_DIR  := $(if $(wildcard $(LIBC16_ROOT)/build/stage/VERSION),$(LIBC16_ROOT)/build/stage,$(LIBC16_ROOT))
LIBC16      := $(LIBC16_DIR)/lib/LIBC16C.LIB
TOOLS_ENV   := build/tools.env
WATCOM_ENV   = . ./$(TOOLS_ENV) && PATH=$$WATCOM/binl64:$$PATH

VERSION := $(shell cat VERSION)

CC     = wcc
CFLAGS = -q -zq -zl -zld -x -mc -0 -os -s -bt=dos -wx -we \
         -I$(LIBC16_DIR)/include '-DVERSION="$(VERSION)"'
LDOPTS = option quiet, nodefaultlibs, stack=8192

# Host compiler, for the unit tests.
HOSTCC     = cc
HOSTCFLAGS = -std=c89 -pedantic -Wall -Wextra -Werror -O1

NAMES   = main editor buffer text dos
SRCS    = $(NAMES:%=src/%.c)
HEADERS = src/tico.h

all: tico.exe

# Objects go in build/r, and the test build's in build/t.
R_OBJS = $(NAMES:%=build/r/%.obj)
T_OBJS = $(NAMES:%=build/t/%.obj)

tico.exe: $(R_OBJS) $(LIBC16) $(TOOLS_ENV)
	$(WATCOM_ENV) && wlink $(LDOPTS), map=build/r/tico.map \
	  format dos name $@ $(R_OBJS:%=file %) library $(LIBC16)

# The test build: keys from a script, screen dumps to a file.
tico_t.exe: $(T_OBJS) $(LIBC16) $(TOOLS_ENV)
	$(WATCOM_ENV) && wlink $(LDOPTS), map=build/t/tico_t.map \
	  format dos name $@ $(T_OBJS:%=file %) library $(LIBC16)

build/r/%.obj: src/%.c $(HEADERS) VERSION $(LIBC16) $(TOOLS_ENV)
	mkdir -p build/r
	$(WATCOM_ENV) && $(CC) $(CFLAGS) -fo=$@ $<

build/t/%.obj: src/%.c $(HEADERS) VERSION $(LIBC16) $(TOOLS_ENV)
	mkdir -p build/t
	$(WATCOM_ENV) && $(CC) $(CFLAGS) -DTICO_TEST -fo=$@ $<

# The pinned libc16 release (scripts/fetch-libc16). A LIBC16_ROOT given
# on the command line must already be built.
ifeq ($(LIBC16_ROOT),build/libc16)
$(LIBC16): scripts/fetch-libc16
	scripts/fetch-libc16 $(LIBC16_ROOT)
endif

# The Open Watcom snapshot libc16 names, as shell assignments.
$(TOOLS_ENV): $(LIBC16)
	mkdir -p build
	watcom=$$(sh $(LIBC16_DIR)/tools/fetch-watcom $(CURDIR)/build/tools) \
	  && printf 'WATCOM=%s\nexport WATCOM\n' "$$watcom" > $@

build/test_text: tests/unit/test_text.c src/buffer.c src/text.c $(HEADERS)
	mkdir -p build
	$(HOSTCC) $(HOSTCFLAGS) -o $@ tests/unit/test_text.c src/buffer.c src/text.c

unit: build/test_text
	./build/test_text

test: unit tico.exe tico_t.exe
	sh tests/run.sh $(CURDIR)/tico.exe $(CURDIR)/tico_t.exe

clean:
	rm -rf *.o *.obj *.err tico.exe tico_t.exe build/r build/t build/test_text

.PHONY: all unit test clean
