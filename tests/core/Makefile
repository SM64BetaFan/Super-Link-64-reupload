# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Cycl0o0

CC ?= cc
LIBOOT_INCLUDE ?= ../../../liboot-public/src
CPPFLAGS += -I../../core -I$(LIBOOT_INCLUDE)
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror
LDLIBS += -lm

TARGET := sl64-core-tests
SOURCES := ../../core/sl64_core.c test_sl64_core.c
HEADERS := ../../core/sl64_core.h $(LIBOOT_INCLUDE)/liboot.h \
	$(LIBOOT_INCLUDE)/liboot_engine.h

.PHONY: all check clean

all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) $(LDLIBS) -o $@

check: $(TARGET)
	./$(TARGET)

clean:
	$(RM) -- $(TARGET)
