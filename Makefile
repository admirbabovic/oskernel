ASM      = nasm
UNAME    := $(shell uname -s)

ifeq ($(UNAME), Darwin)
    CC      = x86_64-elf-gcc
    LD      = x86_64-elf-ld
    OBJCOPY = x86_64-elf-objcopy
else
    CC      = gcc
    LD      = ld
    OBJCOPY = objcopy
endif

SRC_DIR   = src
BUILD_DIR = build

CFLAGS = -m32 -ffreestanding -fno-stack-protector -nostdlib -O2

# =============================================================================
# DEFAULT TARGET
# =============================================================================
all: $(BUILD_DIR)/os.img

# =============================================================================
# STEP 1: ASSEMBLE BOOTLOADER  (boot.asm → boot.bin)
# =============================================================================
$(BUILD_DIR)/boot.bin: $(SRC_DIR)/boot.asm | $(BUILD_DIR)
	$(ASM) -f bin $< -o $@
	@BOOT_SIZE=$$(wc -c < $@); \
	if [ "$$BOOT_SIZE" -ne 512 ]; then \
		echo "ERROR: boot.bin is $$BOOT_SIZE bytes, expected 512!"; exit 1; \
	fi
	@echo "boot.bin: $$(wc -c < $@) bytes ✓"

# =============================================================================
# STEP 2+3: COMPILE AND LINK KERNEL  (kernel.c → kernel.elf → kernel.bin)
# =============================================================================
$(BUILD_DIR)/kernel.o: $(SRC_DIR)/kernel.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel.elf: $(BUILD_DIR)/kernel.o $(SRC_DIR)/linker.ld
	$(LD) -m elf_i386 -T $(SRC_DIR)/linker.ld -o $@ $<

$(BUILD_DIR)/kernel.bin: $(BUILD_DIR)/kernel.elf
	$(OBJCOPY) -O binary $< $@
	@echo "kernel.bin created ✓"

# =============================================================================
# STEP 4: CREATE DISK IMAGE  (boot.bin + kernel.bin → os.img)
# =============================================================================
$(BUILD_DIR)/os.img: $(BUILD_DIR)/boot.bin $(BUILD_DIR)/kernel.bin
	dd if=/dev/zero of=$@ bs=512 count=2880 2>/dev/null
	dd if=$(BUILD_DIR)/boot.bin of=$@ bs=512 count=1 conv=notrunc 2>/dev/null
	dd if=$(BUILD_DIR)/kernel.bin of=$@ bs=512 seek=1 conv=notrunc 2>/dev/null
	@echo "os.img created ✓"

# =============================================================================
# HELPERS
# =============================================================================
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

run: $(BUILD_DIR)/os.img
	qemu-system-i386 -drive format=raw,file=$<

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all run clean