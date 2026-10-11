# gbadual build
#
#   make                      host build (needs MGBA_BUILD and SDL2 via sdl2-config or SDL_PREFIX)
#   make CROSS=aarch64-linux-gnu- MGBA_BUILD=../mgba/build-aarch64 SDL_PREFIX=../sdl-aarch64
#
# MGBA_SRC   mGBA source tree (headers)
# MGBA_BUILD a cmake build dir of mGBA configured with -DLIBMGBA_ONLY=ON -DBUILD_STATIC=ON
# SDL_PREFIX an SDL2 install prefix (include/SDL2, lib/libSDL2.so)

CROSS      ?=
CC         := $(CROSS)gcc
MGBA_SRC   ?= ../mgba
MGBA_BUILD ?= $(MGBA_SRC)/build-host
SDL_PREFIX ?= ../sdl-host
OUT        ?= build/$(if $(CROSS),$(patsubst %-,%,$(CROSS)),host)

MGBA_DEFS  := $(shell sed -n 's/^C_DEFINES = //p' $(MGBA_BUILD)/CMakeFiles/mgba.dir/flags.make)
OPTFLAGS   ?= -O2 -g
override CFLAGS := $(OPTFLAGS) -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
              $(MGBA_DEFS) -I$(MGBA_SRC)/include -I$(MGBA_BUILD)/include -I$(SDL_PREFIX)/include/SDL2 -D_REENTRANT
LDFLAGS    += -L$(SDL_PREFIX)/lib -Wl,-rpath-link,$(SDL_PREFIX)/lib
LIBS       := $(MGBA_BUILD)/libmgba.a -lSDL2 -lpthread -lm

CORE_SRC   := src/emu.c src/mapcap.c src/profiles.c
APP_SRC    := src/main.c src/config.c src/font.c src/ui.c src/library.c src/game.c src/paths.c $(CORE_SRC)

all: $(OUT)/gbadual

$(OUT)/gbadual: $(APP_SRC) src/*.h $(MGBA_BUILD)/libmgba.a
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -o $@ $(APP_SRC) $(LDFLAGS) $(LIBS)

# development tools (host only; need libpng)
tools: $(OUT)/harness

$(OUT)/harness: tools/harness.c tools/png.c $(CORE_SRC) src/*.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -o $@ tools/harness.c tools/png.c $(CORE_SRC) $(MGBA_BUILD)/libmgba.a -lpng -lpthread -lm

clean:
	rm -rf build

.PHONY: all tools clean
