# gpSP standalone SDL2 build
DEBUG ?= 0
OVERCLOCK_60FPS ?= 0
HAVE_DYNAREC ?= 0
FORCE_32BIT_ARCH ?= 1
MMAP_JIT_CACHE ?= 0

UNAME := $(shell uname -s)
UNAME_M := $(shell uname -m)

TARGET := gpsp
BUILDDIR := obj

CC  ?= gcc
CXX ?= g++

CORE_DIR := .

# SDL2
SDL_CFLAGS  := $(shell sdl2-config --cflags 2>/dev/null)
SDL_LDFLAGS := $(shell sdl2-config --libs 2>/dev/null)

INCFLAGS := -I$(CORE_DIR)

LDFLAGS := $(SDL_LDFLAGS) -lm

# Sources
SOURCES_ASM := $(CORE_DIR)/bios_data.S

SOURCES_CC := $(CORE_DIR)/video.cc \
              $(CORE_DIR)/cpu.cc

SOURCES_C := $(CORE_DIR)/frontend.c \
             $(CORE_DIR)/main.c \
             $(CORE_DIR)/gba_memory.c \
             $(CORE_DIR)/savestate.c \
             $(CORE_DIR)/input.c \
             $(CORE_DIR)/sound.c \
             $(CORE_DIR)/cheats.c \
             $(CORE_DIR)/memmap.c \
             $(CORE_DIR)/serial.c \
             $(CORE_DIR)/gbp.c \
             $(CORE_DIR)/rfu.c \
             $(CORE_DIR)/serial_proto.c \
             $(CORE_DIR)/gba_cc_lut.c

ifeq ($(HAVE_DYNAREC), 1)
   SOURCES_C += $(CORE_DIR)/cpu_threaded.c
   ifeq ($(CPU_ARCH), x86_32)
      SOURCES_ASM += $(CORE_DIR)/x86/x86_stub.S
   else ifeq ($(CPU_ARCH), arm)
      SOURCES_ASM += $(CORE_DIR)/arm/arm_stub.S
   else ifeq ($(CPU_ARCH), arm64)
      SOURCES_ASM += $(CORE_DIR)/arm/arm64_stub.S
   else ifeq ($(CPU_ARCH), mips)
      SOURCES_ASM += $(CORE_DIR)/mips/mips_stub.S
   endif
endif

# Platform detection
ifeq ($(UNAME),Linux)
   ifeq ($(HAVE_DYNAREC),1)
      MMAP_JIT_CACHE = 1
   endif
else ifeq ($(UNAME),Darwin)
   ifeq ($(HAVE_DYNAREC),1)
      MMAP_JIT_CACHE = 1
   endif
endif

# CPU architecture detection (for dynarec)
ifeq ($(HAVE_DYNAREC),1)
   ifeq ($(CPU_ARCH),)
      ifneq (,$(filter x86_64 amd64,$(UNAME_M)))
         CPU_ARCH := x86_32
         FORCE_32BIT_ARCH := 1
      else ifneq (,$(filter i386 i686,$(UNAME_M)))
         CPU_ARCH := x86_32
      else ifneq (,$(filter aarch64 arm64,$(UNAME_M)))
         CPU_ARCH := arm64
      else ifneq (,$(filter armv%,$(UNAME_M)))
         CPU_ARCH := arm
      else ifneq (,$(filter mips%,$(UNAME_M)))
         CPU_ARCH := mips
      endif
   endif
endif

FORCE_32BIT :=
ifeq ($(FORCE_32BIT_ARCH),1)
   FORCE_32BIT := -m32
endif

# Optimization
ifeq ($(DEBUG), 1)
   OPTIMIZE := -O0 -g
else
   OPTIMIZE := -O2 -DNDEBUG
endif

DEFINES := -DHAVE_STRINGS_H -DHAVE_STDINT_H -DHAVE_INTTYPES_H -DINLINE=inline -Wall

ifeq ($(HAVE_DYNAREC), 1)
   DEFINES += -DHAVE_DYNAREC
endif

ifeq ($(MMAP_JIT_CACHE), 1)
   DEFINES += -DMMAP_JIT_CACHE
endif

ifeq ($(OVERCLOCK_60FPS), 1)
   DEFINES += -DOVERCLOCK_60FPS
endif

ifeq ($(CPU_ARCH), arm)
   DEFINES += -DARM_ARCH
else ifeq ($(CPU_ARCH), arm64)
   DEFINES += -DARM64_ARCH
else ifeq ($(CPU_ARCH), mips)
   DEFINES += -DMIPS_ARCH
else ifeq ($(CPU_ARCH), x86_32)
   DEFINES += -DX86_ARCH
endif

GIT_VERSION := "$(shell git rev-parse --short HEAD 2>/dev/null || echo unknown)"
ifneq ($(GIT_VERSION),"unknown")
   DEFINES += -DGIT_VERSION=\"$(GIT_VERSION)\"
endif

# Objects
OBJECTS := $(patsubst %.c,$(BUILDDIR)/%.o,$(SOURCES_C)) \
           $(patsubst %.S,$(BUILDDIR)/%.o,$(SOURCES_ASM)) \
           $(patsubst %.cc,$(BUILDDIR)/%.o,$(SOURCES_CC))

CFLAGS   += $(FORCE_32BIT) $(DEFINES) $(OPTIMIZE) $(INCFLAGS) $(SDL_CFLAGS)
CXXFLAGS  = $(CFLAGS) -fno-rtti -fno-exceptions -std=c++11
LDFLAGS  += $(FORCE_32BIT)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) -o $@ $^ $(LDFLAGS)

$(BUILDDIR)/cpu_threaded.o: cpu_threaded.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -Wno-unused-variable -Wno-unused-label -c -o $@ $<

$(BUILDDIR)/%.o: %.S | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILDDIR)/%.o: %.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILDDIR)/%.o: %.cc | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILDDIR):
	@mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR) $(TARGET)
