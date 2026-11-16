# Retro FPS Engine & Tooling - build + automation entry points
#
#   make            -> tooling + test + gif
#   make tooling    -> run the Python mesh pipeline (regenerates core_c/include/asset_manifest.h)
#   make test       -> C unit tests + 60-frame HEADLESS_TEST telemetry + Python unit tests
#   make prototype  -> raylib window build (needs raylib via pkg-config)
#   make gif        -> regenerate assets/demo.gif
#   make clean

CC      ?= gcc
PYTHON  ?= python
CFLAGS  ?= -std=c99 -Wall -Wextra -pedantic -O2
INCLUDE := -Icore_c/include
BUILD   := build

CORE_SRC := core_c/src/memory_arena.c core_c/src/state_machine.c core_c/src/combat_combo.c
MANIFEST := core_c/include/asset_manifest.h
OBJ_IN   := tooling_python/sample_assets/retro_throwing_knife.obj

RAYLIB_CFLAGS ?= $(shell pkg-config --cflags raylib 2>/dev/null)
RAYLIB_LIBS   ?= $(shell pkg-config --libs raylib 2>/dev/null || echo -lraylib -lGL -lm -lpthread -ldl -lrt -lX11)

.PHONY: all tooling test test-c test-python headless prototype gif clean

all: tooling test gif

$(BUILD):
	mkdir -p $(BUILD)

tooling:
	$(PYTHON) tooling_python/mesh_and_config_pipeline.py --input $(OBJ_IN)

$(MANIFEST):
	$(PYTHON) tooling_python/mesh_and_config_pipeline.py --input $(OBJ_IN)

$(BUILD)/test_core: core_c/tests/test_core.c $(CORE_SRC) | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDE) $^ -o $@ -lm

$(BUILD)/headless_test: core_c/src/main_prototype.c $(CORE_SRC) $(MANIFEST) | $(BUILD)
	$(CC) $(CFLAGS) -DHEADLESS_TEST $(INCLUDE) core_c/src/main_prototype.c $(CORE_SRC) -o $@ -lm

test-c: $(BUILD)/test_core $(BUILD)/headless_test
	./$(BUILD)/test_core
	./$(BUILD)/headless_test

test-python:
	$(PYTHON) -m unittest discover -s tooling_python/tests -v

test: test-c test-python

headless: $(BUILD)/headless_test
	./$(BUILD)/headless_test

prototype: $(MANIFEST) | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDE) $(RAYLIB_CFLAGS) core_c/src/main_prototype.c $(CORE_SRC) -o $(BUILD)/retro_fps_prototype $(RAYLIB_LIBS)

gif:
	$(PYTHON) generate_demo_gif.py

clean:
	rm -rf $(BUILD)
