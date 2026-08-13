# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Cycl0o0

# SM64CoopDX v1.5.1 source-link fragment.
#
# GNU make cannot represent directory-list entries containing whitespace.
# Install or symlink this distribution to a whitespace-free path inside the
# CoopDX checkout, then include this file twice:
#
#   # Immediately before CoopDX constructs C_FILES from SRC_DIRS (v1.5.1:493):
#   SL64_MAKE_PHASE := sources
#   include src/pc/super_link_64/native/super_link_64.mk
#
#   # After CoopDX finishes constructing platform LDFLAGS (v1.5.1:971):
#   SL64_MAKE_PHASE := link
#   include src/pc/super_link_64/native/super_link_64.mk
#
# Typical command line after running tools/prepare_local.py --install:
#   make SUPER_LINK_64=1 LIBOOT_PREFIX=/opt/liboot ...
#
# LIBOOT_PREFIX must be a static liboot install made with
# -DBUILD_SHARED_LIBS=OFF. Static linking is required so Windows uses the same
# MinGW runtime/CRT as CoopDX and does not need a DLL search policy.

ifndef SL64_MAKE_PHASE
  $(error Set SL64_MAKE_PHASE to sources or link before including super_link_64.mk)
endif
SL64_ROOT ?= src/pc/super_link_64
ifndef LIBOOT_PREFIX
  $(error LIBOOT_PREFIX must name a static liboot installation prefix)
endif

ifneq ($(words $(SL64_ROOT)),1)
  $(error SL64_ROOT cannot contain whitespace; install or symlink it to a safe path)
endif
ifneq ($(words $(LIBOOT_PREFIX)),1)
  $(error LIBOOT_PREFIX cannot contain whitespace)
endif
ifeq ($(wildcard $(SL64_ROOT)/native/sl64_native.h),)
  $(error SL64_ROOT does not contain native/sl64_native.h)
endif
ifeq ($(wildcard $(SL64_ROOT)/core/sl64_core.c),)
  $(error SL64_ROOT does not contain core/sl64_core.c)
endif
ifeq ($(wildcard $(LIBOOT_PREFIX)/include/liboot/liboot_engine.h),)
  $(error LIBOOT_PREFIX does not contain include/liboot/liboot_engine.h)
endif

LIBOOT_LIBDIR ?= $(LIBOOT_PREFIX)/lib
SL64_LDFLAGS := -L$(LIBOOT_LIBDIR) -loot -lm

ifeq ($(SL64_MAKE_PHASE),sources)
  ifeq ($(TARGET_N64),1)
    $(error Super Link 64 targets CoopDX desktop builds, not TARGET_N64=1)
  endif
  ifdef SL64_MAKE_SOURCES_INCLUDED
    $(error super_link_64.mk sources phase was included more than once)
  endif
  SL64_MAKE_SOURCES_INCLUDED := 1
  SRC_DIRS += $(SL64_ROOT)/native $(SL64_ROOT)/core
  # INCLUDE_DIRS is assigned after this hook. EXTRA_CFLAGS survives that
  # assignment without polluting the level-rules preprocessor input list.
  EXTRA_CFLAGS += -I$(SL64_ROOT)/native -I$(SL64_ROOT)/core
  EXTRA_CFLAGS += -I$(LIBOOT_PREFIX)/include/liboot
  DEFINES += OOT_LIB_STATIC=1 SUPER_LINK_64=1
else ifeq ($(SL64_MAKE_PHASE),link)
  ifdef SL64_MAKE_LINK_INCLUDED
    $(error super_link_64.mk link phase was included more than once)
  endif
  SL64_MAKE_LINK_INCLUDED := 1
  ifeq ($(wildcard $(LIBOOT_LIBDIR)/liboot.a),)
    $(error LIBOOT_LIBDIR does not contain static liboot.a)
  endif
  LDFLAGS += $(SL64_LDFLAGS)
else
  $(error Unknown SL64_MAKE_PHASE '$(SL64_MAKE_PHASE)'; use sources or link)
endif
