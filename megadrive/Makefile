# Builds toast.bin, a Mega Drive ROM, with the Ubuntu 68000 cross-compiler (gcc-m68k-linux-gnu).
PREFIX  ?= m68k-linux-gnu-
CC      := $(PREFIX)gcc
LD      := $(PREFIX)ld
OBJCOPY := $(PREFIX)objcopy
CFLAGS  := -m68000 -mstrict-align -fno-store-merging -O2 -Wall -ffreestanding -fno-builtin -fno-pic -fomit-frame-pointer \
           -fno-tree-loop-distribute-patterns -fno-asynchronous-unwind-tables -Isrc
OBJS    := build/boot.o build/md.o build/libc.o build/assets.o build/music.o build/main.o

all: toast.bin

src/assets.c src/assets.h: tools/build_assets.py res/kitchen.png res/toasterUp.png res/toasterDown.png res/art.json
	python3 tools/build_assets.py

build/%.o: src/%.c src/md.h src/assets.h src/music.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/boot.o: src/boot.s | build
	$(CC) -m68000 -c $< -o $@

build:
	mkdir -p build

toast.bin: $(OBJS) md.ld tools/fix_header.py tools/check_align.py
	$(LD) -T md.ld -o build/toast.elf $(OBJS)
	python3 tools/check_align.py build/toast.elf
	$(OBJCOPY) -O binary build/toast.elf $@
	python3 tools/fix_header.py $@

clean:
	rm -rf build toast.bin

.PHONY: all clean
