# =============================================================================
# Sharak node — top-level build
#
# Targets
#   make all       -> firmware + host tests (what CI runs)
#   make firmware  -> cross-compile the node image for the LM3S6965 (Cortex-M3)
#   make test      -> build & run every host unit test in tests/test_*.c
#   make clean     -> remove the build/ tree
#
# Two compilers are involved, on purpose:
#   - $(XCC)    = arm-none-eabi-gcc: produces the bare-metal ARM image.
#   - $(HOSTCC) = plain gcc: builds the PORTABLE subset (protocol, decode,
#                 i2c_sim) together with the unit tests, natively, so the
#                 logic can be debugged on a PC with no hardware involved.
#
# The same .c files compile in both worlds. That only works because those
# files are written freestanding-clean: no libc calls, no heap, no I/O —
# just <stdint.h>/<stddef.h>. The build enforcing that split is a design
# feature, not an accident (see docs/architecture.md).
# =============================================================================

# ---- toolchain ---------------------------------------------------------------
# ?= lets you override from the command line, e.g.:
#   make firmware CROSS=/opt/xpack/bin/arm-none-eabi-
CROSS   ?= arm-none-eabi-
XCC     := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SIZE    := $(CROSS)size
HOSTCC  ?= gcc

BUILD := build

# ---- firmware (cross) --------------------------------------------------------
# Flag rationale (each one matters on bare metal):
#   -mcpu=cortex-m3 -mthumb : LM3S6965 is a Cortex-M3; M-profile cores execute
#                             ONLY Thumb-2, so -mthumb is mandatory, and there
#                             is no FPU (the compiler must not emit FP code).
#   -std=c17                : pin the C dialect; no GNU extensions by surprise.
#   -ffreestanding          : tell the compiler there is no hosted C library /
#                             OS environment (main() has no special meaning).
#   -ffunction-sections / -fdata-sections + --gc-sections : every function and
#                             object gets its own section so the linker can
#                             drop anything unreferenced (e.g. the unused
#                             i2c_stellaris backend in the QEMU build).
#   -Wall -Wextra -Werror   : warnings are bugs-in-waiting; fail loudly.
#   -O2 -g                  : optimized but still debuggable (DWARF in the ELF;
#                             objcopy strips it from the .bin automatically).
#   -fno-tree-loop-distribute-patterns : at -O2, GCC's loop-idiom pass may spot
#                             that startup.c's .data-copy loop "is" memcpy and
#                             the .bss-zero loop "is" memset, and rewrite them
#                             as CALLS to those libc symbols. Under -nostdlib
#                             those symbols don't exist -> link error. We hand-
#                             wrote the loops precisely so we'd need no libc, so
#                             we tell the optimizer not to "helpfully" undo that.
#                             (protocol.c's small byte loops are at risk too.)
FW_CFLAGS := -mcpu=cortex-m3 -mthumb -std=c17 -O2 -g \
             -Wall -Wextra -Werror \
             -ffreestanding -ffunction-sections -fdata-sections \
             -fno-tree-loop-distribute-patterns \
             -Ifirmware -Iprotocol/include

#   -nostdlib            : do not link crt0/libc; startup.c IS our runtime.
#   -T firmware/lm3s6965.ld : our memory map (256 KB flash / 64 KB SRAM).
#   -Wl,-Map=...         : the map file shows where every symbol landed —
#                          worth reading once to see .data/.bss placement.
FW_LDFLAGS := -mcpu=cortex-m3 -mthumb -nostdlib \
              -T firmware/lm3s6965.ld \
              -Wl,--gc-sections -Wl,-Map=$(BUILD)/sharak_node.map

# All firmware sources are listed explicitly (no wildcard) so that a stray
# file can never silently end up inside the image.
FW_SRCS := firmware/startup.c \
           firmware/uart.c \
           firmware/i2c_sim.c \
           firmware/i2c_stellaris.c \
           firmware/adxl345.c \
           firmware/main.c \
           protocol/src/protocol.c

ELF := $(BUILD)/sharak_node.elf
BIN := $(BUILD)/sharak_node.bin

# ---- host tests --------------------------------------------------------------
# Every test links the full portable subset; the linker simply ignores the
# objects a given test doesn't reference. Simple > clever for a build system.
HOST_CFLAGS   := -std=c17 -O2 -g -Wall -Wextra -Werror -Ifirmware -Iprotocol/include -Isim
HOST_LIB_SRCS := protocol/src/protocol.c firmware/adxl345.c firmware/i2c_sim.c sim/fleet_node.c

TEST_SRCS := $(wildcard tests/test_*.c)
TEST_BINS := $(patsubst tests/%.c,$(BUILD)/host/%,$(TEST_SRCS))

# ---- rules -------------------------------------------------------------------
.PHONY: all firmware test clean sim

all: test sim firmware

firmware: $(ELF)

$(ELF): $(FW_SRCS) firmware/lm3s6965.ld
	@mkdir -p $(BUILD)
	$(XCC) $(FW_CFLAGS) $(FW_LDFLAGS) $(FW_SRCS) -o $@
	$(OBJCOPY) -O binary $@ $(BIN)
	$(SIZE) $@

$(BUILD)/host/%: tests/%.c $(HOST_LIB_SRCS)
	@mkdir -p $(BUILD)/host
	$(HOSTCC) $(HOST_CFLAGS) $< $(HOST_LIB_SRCS) -o $@

# Run each test binary; '|| exit 1' makes the whole target (and therefore CI)
# fail on the FIRST nonzero exit status instead of plowing on.
test: $(TEST_BINS)
	@for t in $(TEST_BINS); do \
	    echo "==== $$t"; \
	    $$t || exit 1; \
	done
	@echo "ALL HOST TESTS PASSED"

clean:
	rm -rf $(BUILD)

# ---- fleet simulator (host, dev scaffolding) -------------------------------
SIM_BIN := $(BUILD)/sim/fleet_node
sim: $(SIM_BIN)
$(SIM_BIN): sim/fleet_main.c sim/fleet_node.c protocol/src/protocol.c
	@mkdir -p $(BUILD)/sim
	$(HOSTCC) $(HOST_CFLAGS) $^ -o $@
