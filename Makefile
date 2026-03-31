# gpSP standalone SDL 1.2 build
# PLATFORM: LINUX (default) or DC
PLATFORM ?= LINUX

DEBUG ?= 0
HAVE_DYNAREC ?= 0
OVERCLOCK_60FPS ?= 0
MMAP_JIT_CACHE ?= 0

TARGET   := gpsp
BUILDDIR := obj
CORE_DIR := .

# Platform-specific setup
ifeq ($(PLATFORM),DC)
   include $(KOS_BASE)/Makefile.rules
   CC       := kos-cc
   CXX      := kos-cc
   TARGET   := gpsp.elf
   HAVE_DYNAREC := 0
   CFLAGS   += -Os -ffast-math -DDREAMCAST
   LDFLAGS  :=
   LIBS     := -lm -lSDL
else
   CC       ?= gcc
   CXX      ?= g++
   FORCE_32BIT ?= 1

   SDL_CFLAGS  := $(shell sdl-config --cflags 2>/dev/null)
   SDL_LDFLAGS := $(shell sdl-config --libs 2>/dev/null)
   LIBS     := $(SDL_LDFLAGS) -lm

   # Dynarec needs mmap JIT cache on Linux/Mac
   ifeq ($(HAVE_DYNAREC),1)
      MMAP_JIT_CACHE = 1
   endif

   # CPU architecture detection
   ifeq ($(HAVE_DYNAREC),1)
      UNAME_M := $(shell uname -m)
      ifeq ($(CPU_ARCH),)
         ifneq (,$(filter x86_64 amd64,$(UNAME_M)))
            CPU_ARCH := x86_32
            FORCE_32BIT := 1
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

   ifeq ($(FORCE_32BIT),1)
      CFLAGS  += -m32
      LDFLAGS += -m32
   endif

   CFLAGS += $(SDL_CFLAGS)
endif

INCFLAGS := -I$(CORE_DIR)

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

CFLAGS   += $(DEFINES) $(OPTIMIZE) $(INCFLAGS)
CXXFLAGS  = $(CFLAGS) -fno-rtti -fno-exceptions -std=c++11

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS)

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
