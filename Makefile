# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Super Link 64 contributors

LIBOOT_SOURCE ?= ../liboot-public
LIBOOT_INCLUDE ?= $(abspath $(LIBOOT_SOURCE)/src)

.PHONY: test scan check clean

test:
	@test -f "$(LIBOOT_INCLUDE)/liboot_engine.h" || { \
		printf '%s\n' 'liboot headers not found; set LIBOOT_SOURCE=/path/to/liboot'; \
		exit 1; \
	}
	$(MAKE) -C tests/core check LIBOOT_INCLUDE="$(LIBOOT_INCLUDE)"
	$(MAKE) -C tests/core clean
	PYTHONDONTWRITEBYTECODE=1 python3 tests/test_prepare_local.py
	PYTHONDONTWRITEBYTECODE=1 python3 tests/test_release_tree.py
	luac -p mod/super-link-64/main.lua
	luac -p mod/super-link-64/legal/license_agpl.lua
	luac -p mod/super-link-64/legal/notices.lua

scan:
	PYTHONDONTWRITEBYTECODE=1 python3 tools/check_release_tree.py .
	@if test -f "$(HOME)/.agents/skills/kill-ai-slop/scripts/scan.mjs"; then \
		node "$(HOME)/.agents/skills/kill-ai-slop/scripts/scan.mjs" . --no-color; \
	else \
		printf '%s\n' 'kill-ai-slop scanner not installed; release-tree scan passed'; \
	fi

check: test scan

clean:
	$(MAKE) -C tests/core clean
