CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Os
ROOT := ../..
OUT := $(ROOT)/.zero/bin/zero
NODE_TS := node --experimental-strip-types --disable-warning=ExperimentalWarning
BUILD_STAMP := $(OUT).build
HOST_ID := $(shell uname -s 2>/dev/null || echo unknown)-$(shell uname -m 2>/dev/null || echo unknown)
ZERO_BUILD_HASH := $(shell git -C $(ROOT) rev-parse --short HEAD 2>/dev/null || echo unknown)
BUILD_ID := $(HOST_ID)|$(CC)|$(CFLAGS)|$(ZERO_BUILD_HASH)
SRC := $(wildcard src/*.c)
INCLUDES := $(wildcard include/*.h) $(wildcard src/*.h) $(wildcard src/*.inc)
EMBEDDED_SKILLS := src/embedded_skills.inc
EMBEDDED_STDLIB_GRAPH := src/embedded_stdlib_graph.inc
EMBEDDED_RUNTIME_SOURCES := src/embedded_runtime_sources.inc
SKILL_INPUTS := $(ROOT)/skills/zero/SKILL.md $(wildcard $(ROOT)/skill-data/*.md)
STDLIB_GRAPH_INPUTS := $(wildcard $(ROOT)/std/*.graph)
RUNTIME_SOURCE_INPUTS := $(ROOT)/native/zero-c/include/zero_runtime.h $(ROOT)/native/zero-c/runtime/zero_runtime.c $(ROOT)/native/zero-c/runtime/zero_http_curl.c

.PHONY: all clean install-local FORCE

all: $(OUT)

$(BUILD_STAMP): FORCE
	@mkdir -p $(dir $(OUT))
	@current="$$(cat "$(BUILD_STAMP)" 2>/dev/null || true)"; \
	if [ "$$current" != "$(BUILD_ID)" ]; then \
		printf '%s\n' "$(BUILD_ID)" > $(BUILD_STAMP); \
	fi

$(EMBEDDED_SKILLS): $(ROOT)/scripts/embed-skill-data.mts $(SKILL_INPUTS)
	cd $(ROOT) && $(NODE_TS) scripts/embed-skill-data.mts

$(EMBEDDED_STDLIB_GRAPH): $(ROOT)/scripts/embed-stdlib-graphs.mts $(STDLIB_GRAPH_INPUTS)
	cd $(ROOT) && $(NODE_TS) scripts/embed-stdlib-graphs.mts

$(EMBEDDED_RUNTIME_SOURCES): $(ROOT)/scripts/embed-runtime-sources.mts $(RUNTIME_SOURCE_INPUTS)
	cd $(ROOT) && $(NODE_TS) scripts/embed-runtime-sources.mts

$(OUT): $(BUILD_STAMP) $(SRC) $(INCLUDES) $(EMBEDDED_SKILLS) $(EMBEDDED_STDLIB_GRAPH) $(EMBEDDED_RUNTIME_SOURCES)
	@mkdir -p $(dir $(OUT))
	$(CC) $(CFLAGS) -DZERO_BUILD_HASH='"$(ZERO_BUILD_HASH)"' -Iinclude $(SRC) -o $(OUT)

clean:
	rm -f $(OUT) $(BUILD_STAMP)

install-local: $(OUT)
	@printf 'installed %s\n' "$(OUT)"
