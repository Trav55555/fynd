CC ?= cc
PYTHON ?= python3
CUTILS ?= ../c-utils
BUILD ?= build
BENCH_FYND ?= $(BUILD)/fynd
BENCH_ARGS ?=
BENCH_FULL_ARGS ?=
STRICT = -std=c89 -pedantic-errors -Wall -Wextra -Werror -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes
CFLAGS ?= -O2 -g
CPPFLAGS ?= -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
PIN_INPUTS = ci/pinned_cutils.py ci/pinned_cutils.json CUTILS_REVISION
PRIVATE_SRC := $(wildcard src/*.inc)
TEST_SRC := $(wildcard tests/test_*.c)
TEST_BINS := $(patsubst tests/%.c,$(BUILD)/%,$(TEST_SRC))
.PHONY: all test cli-test bench bench-full bench-test verify clean check-cutils
all: $(BUILD)/fynd | check-cutils

check-cutils:
	$(PYTHON) -I ci/pinned_cutils.py check --cutils "$(CUTILS)"

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/fynd: cmd/main.c src/fynd.c $(PRIVATE_SRC) include/fynd.h $(PIN_INPUTS) | check-cutils $(BUILD)
	$(PYTHON) -I ci/pinned_cutils.py build --cutils "$(CUTILS)" --output "$@" -- $(CC) $(CPPFLAGS) $(STRICT) $(CFLAGS) -Iinclude cmd/main.c src/fynd.c -pthread

$(BUILD)/test_%: tests/test_%.c src/fynd.c $(PRIVATE_SRC) include/fynd.h tests/test_fynd.c $(PIN_INPUTS) | check-cutils $(BUILD)
	$(PYTHON) -I ci/pinned_cutils.py build --cutils "$(CUTILS)" --output "$@" -- $(CC) $(CPPFLAGS) $(STRICT) $(CFLAGS) -Iinclude $< -pthread

test: $(TEST_BINS) | check-cutils
	@for test in $(TEST_BINS); do "$$test" || exit $$?; done

cli-test: $(BUILD)/fynd | check-cutils
	python3 -I tests/test_cli.py $(BUILD)/fynd
	python3 -I tests/test_ignore_cli.py $(BUILD)/fynd

bench-test:
	python3 -m unittest bench.test_bench bench.test_comprehensive_fixtures bench.test_comprehensive bench.test_render_ci

bench:
	systemd-run --user --scope -q -p MemoryMax=3G -p MemorySwapMax=0 nice -n 15 bash -c 'ulimit -c 0; exec "$$@"' -- python3 -I bench/bench.py --fynd "$(BENCH_FYND)" --output "$(BUILD)/bench" $(BENCH_ARGS)

bench-full:
	systemd-run --user --scope -q -p MemoryMax=3G -p MemorySwapMax=0 nice -n 15 bash -c 'ulimit -c 0; exec "$$@"' -- python3 -I bench/comprehensive.py --fynd "$(BENCH_FYND)" --output "$(BUILD)/comprehensive" $(BENCH_FULL_ARGS)

verify:
	CUTILS="$(CUTILS)" BUILD="$(BUILD)" bash tests/verify.sh

clean:
	$(RM) $(BUILD)/fynd $(TEST_BINS)
