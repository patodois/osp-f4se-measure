# SPDX-License-Identifier: MIT OR Apache-2.0
# F4SE_ROOT: the folder of your extracted official F4SE 0.6.23 package (the one holding f4se_readme.txt and src/).
# Only `make` and `make inspect` need it; `make test` and `make windows-test-build` do not.
F4SE_ROOT ?=
CROSS_CXX := x86_64-w64-mingw32-g++
HOST_CXX ?= c++
CPPFLAGS := -Iinclude -I"$(F4SE_ROOT)/src/f4se"
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Werror
export PYTHONDONTWRITEBYTECODE := 1
HEADERS := $(wildcard include/osp/*.hpp)
.PHONY: all test inspect windows-test-build verify-sdk
all: verify-sdk build/osp_measure.dll
verify-sdk:
	@if [ -z "$(F4SE_ROOT)" ]; then \
	  echo "F4SE_ROOT is not set. Download the official F4SE 0.6.23 package (for Fallout 4 1.10.163)" >&2; \
	  echo "from the F4SE team's site, extract it anywhere, and run: make F4SE_ROOT=/path/to/f4se_0_06_23" >&2; \
	  echo "The build only reads it; it is never downloaded by this repository and never committed." >&2; \
	  exit 2; \
	fi
	python3 tools/verify_sdk.py "$(F4SE_ROOT)"
build:
	mkdir -p build
build/osp_measure.dll: src/plugin.cpp src/foreign_call.S $(HEADERS) Makefile | build verify-sdk
	@test -f "$(F4SE_ROOT)/src/f4se/f4se/PluginAPI.h"
	$(CROSS_CXX) $(CPPFLAGS) $(CXXFLAGS) -shared -static -static-libgcc -static-libstdc++ -Wl,--no-insert-timestamp -Wl,--strip-debug -Wl,--exclude-all-symbols -o $@ src/plugin.cpp src/foreign_call.S -lshell32 -lbcrypt -luser32
build/unit: tests/unit.cpp $(HEADERS) | build
	$(HOST_CXX) -Iinclude $(CXXFLAGS) -o $@ $<
build/settings_unit: tests/settings_unit.cpp $(HEADERS) | build
	$(HOST_CXX) -Iinclude $(CXXFLAGS) -o $@ $<
build/console_unit: tests/console_unit.cpp $(HEADERS) | build
	$(HOST_CXX) -Iinclude $(CXXFLAGS) -o $@ $<
build/weapon_unit: tests/weapon_unit.cpp $(HEADERS) | build
	$(HOST_CXX) -Iinclude $(CXXFLAGS) -o $@ $<
build/nodes_unit: tests/nodes_unit.cpp $(HEADERS) | build
	$(HOST_CXX) -Iinclude $(CXXFLAGS) -o $@ $<
build/scene_unit: tests/scene_unit.cpp $(HEADERS) | build
	$(HOST_CXX) -Iinclude $(CXXFLAGS) -o $@ $<
build/scene3_unit: tests/scene3_unit.cpp $(HEADERS) | build
	$(HOST_CXX) -Iinclude $(CXXFLAGS) -o $@ $<
build/sequencer_unit: tests/sequencer_unit.cpp $(HEADERS) | build
	$(HOST_CXX) -Iinclude $(CXXFLAGS) -o $@ $<
test: build/unit build/settings_unit build/console_unit build/weapon_unit build/nodes_unit build/scene_unit build/scene3_unit build/sequencer_unit
	python3 tests/test.py ./build/unit
	python3 tests/test_settings.py ./build/settings_unit
	./build/console_unit
	./build/weapon_unit
	./build/nodes_unit > build/nodes-synthetic.jsonl
	./build/scene_unit > build/scene-synthetic.jsonl
	./build/scene3_unit > build/scene3-synthetic.jsonl
	python3 -m unittest discover -s tests -p 'test_node_*.py'
	python3 tests/test_legacy_snapshot.py $(HOST_CXX)
	python3 tests/test_legacy_nodes.py $(HOST_CXX)
	python3 tests/test_legacy_scene.py $(HOST_CXX)
	python3 -m unittest discover -s tests -p 'test_compare_settings.py'
	python3 tests/test_packet_format.py
	python3 tests/test_packet_analysis.py
	./build/sequencer_unit
	python3 tests/test_sequencer.py ./build/sequencer_unit
inspect: build/osp_measure.dll
	python3 tests/inspect_pe.py build/osp_measure.dll
build/windows_foreign.exe: tests/windows_foreign.cpp src/foreign_call.S | build
	$(CROSS_CXX) $(CXXFLAGS) -static -Wl,--no-insert-timestamp -o $@ $^
windows-test-build: build/windows_foreign.exe
