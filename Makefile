# gpSP standalone build
# PLATFORM: LINUX (default) or DC
PLATFORM ?= LINUX

HAVE_DYNAREC ?= 0
OVERCLOCK_60FPS ?= 0
SH4_DEBUG ?= 0

TARGET   := gpsp
BUILDDIR := obj
CORE_DIR := .

# Platform-specific setup
ifeq ($(PLATFORM),DC)
   include $(KOS_BASE)/Makefile.rules
   CC       := kos-cc
   CXX      := kos-cc
   TARGET   := gpsp.elf
   CPU_ARCH := sh4
   CFLAGS   += -O3 -flto -fno-omit-frame-pointer -DDREAMCAST -DSMALL_TRANSLATION_CACHE -DROM_BUFFER_SIZE=4
   LDFLAGS  :=
   LIBS     := -lm
else
   CC       ?= gcc
   CXX      ?= g++
   CPU_ARCH := x86_32

   SDL_CFLAGS  := $(shell sdl-config --cflags 2>/dev/null)
   SDL_LDFLAGS := $(shell sdl-config --libs 2>/dev/null)
   LIBS     := $(SDL_LDFLAGS) -lm

   ifeq ($(HAVE_DYNAREC),1)
      MMAP_JIT_CACHE = 1
   endif

   CFLAGS  += -m32 $(SDL_CFLAGS)
   LDFLAGS += -m32
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
   else ifeq ($(CPU_ARCH), sh4)
      SOURCES_ASM += $(CORE_DIR)/sh4/sh4_stub.S
   endif
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
ifeq ($(SH4_DEBUG), 1)
   DEFINES += -DSH4_DYNAREC_DEBUG -DSH4_DYNAREC_HEXDUMP
endif

ifeq ($(CPU_ARCH), x86_32)
   DEFINES += -DX86_ARCH
else ifeq ($(CPU_ARCH), sh4)
   DEFINES += -DSH4_ARCH
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
