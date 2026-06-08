#
# Makefile — minimoss kernel build system
#
# Usage:
#   make                      Build for the default target (Pi 4)
#   make ARCH=aarch64 BOARD=pi4
#   make ARCH=riscv64 BOARD=virt-riscv
#   make qemu                 Build and run in QEMU
#   make qemu-debug           Build and run with GDB stub (port 1234)
#   make clean                Remove build artifacts
#   make help                 Show this help
#
# Phase 1 scope:
#   - AArch64 / Pi 4 is the primary target (dev-log/006 decision 2)
#   - RISC-V / virt-riscv deferred to Phase 2
#   - No subsystem Makefile.partial yet — too few files to split
#

# ── Default target ───────────────────────────────────────────────────
ARCH  ?= aarch64
BOARD ?= pi4

# ── Build directories ────────────────────────────────────────────────
BUILD_DIR  := build
ARCH_DIR   := arch/$(ARCH)
BOARD_DIR  := boards/$(BOARD)

# ── Toolchain detection ──────────────────────────────────────────────
# Try several common prefixes; use the first one whose gcc is on PATH.
ifeq ($(ARCH),aarch64)
  TOOL_PREFIXES = aarch64-elf- aarch64-none-elf- aarch64-linux-gnu-
  QEMU          = qemu-system-aarch64
  QEMU_MACHINE  = raspi4b
  QEMU_FLAGS    = -nographic
  ARCH_CFLAGS   = -mgeneral-regs-only
endif

ifeq ($(ARCH),riscv64)
  TOOL_PREFIXES = riscv64-elf- riscv64-unknown-elf- riscv64-linux-gnu-
  QEMU          = qemu-system-riscv64
  QEMU_MACHINE  = virt
  QEMU_FLAGS    = -nographic -bios none
  ARCH_CFLAGS   =
endif

# Pick the first prefix whose gcc binary exists.
CROSS_COMPILE ?= $(firstword $(foreach p,$(TOOL_PREFIXES),$(if $(shell command -v $(p)gcc 2>/dev/null),$(p))))

# Tool names — derived lazily so `make help` / `make clean` don't need a
# cross-compiler.
CC      = $(CROSS_COMPILE)gcc
LD      = $(CROSS_COMPILE)ld
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJDUMP = $(CROSS_COMPILE)objdump

# ── Board → source file selection ────────────────────────────────────
# Each board picks ONE UART driver + its C preprocessor define.
ifeq ($(BOARD),pi4)
  C_SRCS       := kernel/main.c kernel/uart-pl011.c
  BOARD_DEFINE := BOARD_PI4
endif
ifeq ($(BOARD),pi5)
  C_SRCS       := kernel/main.c kernel/uart-pl011.c
  BOARD_DEFINE := BOARD_PI5
endif
ifeq ($(BOARD),virt-riscv)
  C_SRCS       := kernel/main.c kernel/uart-ns16550.c
  BOARD_DEFINE := BOARD_VIRT_RISCV
endif
ifeq ($(BOARD),sg2002)
  C_SRCS       := kernel/main.c kernel/uart-ns16550.c   # TBD: check datasheet
  BOARD_DEFINE := BOARD_SG2002
endif

ifndef C_SRCS
  $(error "Unknown BOARD=$(BOARD).  Supported: pi4, pi5, virt-riscv, sg2002")
endif

# ── Source files ─────────────────────────────────────────────────────
ASM_SRCS  := $(wildcard $(ARCH_DIR)/*.S)

# ── Object files (mirror source tree under build/) ───────────────────
ASM_OBJS  := $(patsubst %.S,$(BUILD_DIR)/%.o,$(ASM_SRCS))
C_OBJS    := $(patsubst %.c,$(BUILD_DIR)/%.o,$(C_SRCS))
OBJS      := $(ASM_OBJS) $(C_OBJS)

# ── Linker script ────────────────────────────────────────────────────
LDSCRIPT  := $(BOARD_DIR)/link.ld

# ── Compiler / linker flags ──────────────────────────────────────────
CFLAGS    = -nostdlib -ffreestanding -I. -D$(BOARD_DEFINE) $(ARCH_CFLAGS) \
            -Wall -Wextra -O0 -g
LDFLAGS   = -nostdlib -T $(LDSCRIPT)

# ══════════════════════════════════════════════════════════════════════
# Targets
# ══════════════════════════════════════════════════════════════════════

.PHONY: all clean qemu qemu-debug help check-toolchain

# ── check-toolchain ──────────────────────────────────────────────────
# Guard: fail early if no cross-compiler is available.  Only runs when
# a build target is invoked (not for help / clean).
check-toolchain:
	@if [ -z "$(CROSS_COMPILE)" ]; then \
	  echo "ERROR: No $(ARCH) cross-compiler found."; \
	  echo ""; \
	  echo "Options:"; \
	  echo "  1. brew install $(ARCH)-elf-gcc $(ARCH)-elf-binutils"; \
	  echo "  2. Download ARM GNU Toolchain: https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads"; \
	  echo "  3. Set CROSS_COMPILE=/path/to/toolchain-prefix-"; \
	  echo ""; \
	  exit 1; \
	fi
	@if ! command -v $(CC) >/dev/null 2>&1; then \
	  echo "ERROR: $(CC) not found (CROSS_COMPILE=$(CROSS_COMPILE))"; \
	  exit 1; \
	fi

# ── all ──────────────────────────────────────────────────────────────
all: check-toolchain $(BUILD_DIR)/vmlinux.elf

$(BUILD_DIR)/vmlinux.elf: $(OBJS)
	@mkdir -p $(BUILD_DIR)
	@test -f $(LDSCRIPT) || { echo "ERROR: Linker script not found: $(LDSCRIPT)"; exit 1; }
	$(CC) $(LDFLAGS) $(OBJS) -o $@
	@echo "  BUILD   $@ ($(ARCH) / $(BOARD))"

# ── Compile rules ────────────────────────────────────────────────────
$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
	@echo "  AS      $<"

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
	@echo "  CC      $<"

# ── clean ────────────────────────────────────────────────────────────
clean:
	rm -rf $(BUILD_DIR)
	@echo "  CLEAN   build/"

# ── qemu ─────────────────────────────────────────────────────────────
qemu: all
	$(QEMU) -machine $(QEMU_MACHINE) -kernel $(BUILD_DIR)/vmlinux.elf $(QEMU_FLAGS)

# ── qemu-debug ───────────────────────────────────────────────────────
# Start QEMU with GDB stub.  In another terminal:
#   $(CROSS_COMPILE)gdb build/vmlinux.elf -ex "target remote :1234"
qemu-debug: all
	$(QEMU) -machine $(QEMU_MACHINE) -kernel $(BUILD_DIR)/vmlinux.elf $(QEMU_FLAGS) -s -S
	@echo "  QEMU    waiting for GDB on :1234"

# ── help ─────────────────────────────────────────────────────────────
help:
	@echo "minimoss build system"
	@echo ""
	@echo "Targets:"
	@echo "  make [ARCH=aarch64|riscv64] [BOARD=pi4|pi5|virt-riscv|sg2002]"
	@echo "  make qemu          Build and run in QEMU"
	@echo "  make qemu-debug    Build + QEMU with GDB stub (:1234)"
	@echo "  make clean         Remove build/"
	@echo "  make help          This message"
	@echo ""
	@echo "Defaults:  ARCH=aarch64  BOARD=pi4"
	@echo ""
	@echo "Current:   ARCH=$(ARCH)  BOARD=$(BOARD)"
	@echo "           CROSS_COMPILE=$(CROSS_COMPILE)"
	@echo "           QEMU=$(QEMU) -machine $(QEMU_MACHINE)"
