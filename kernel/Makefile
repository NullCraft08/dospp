# åæ ¸çæå»ºã
#
# ç¨ Makefile èä¸æ¯ mesonï¼äº¤åç¼è¯å°è£¸æºéè¦å¦ä¸å¥å·¥å·é¾è®¾å®ï¼
# å¡è¿ meson å¾å cross fileï¼åèæ´é¾è¯»ãç¨æ·ç©ºé´ç vm/ ä»ç¶ç¨ mesonã
#
# å·¥å·é¾çç°ç¶ï¼é½æ¯ç¯å¢é¼åºæ¥çï¼ä¸æ¯åå¥½ï¼ï¼
#   * ç¼è¯ï¼MSYS2 ç clangï¼--target=x86_64-unknown-none è½ç´æ¥åº ELF å¯¹è±¡ï¼
#   * é¾æ¥ï¼å Zig èªå¸¦ç lldãMSYS2 ç lld åªç¼äº COFF åç«¯ï¼
#     è¿ ELF å¯¹è±¡é½è®¤ä¸åºï¼unknown file typeï¼ï¼mingw ç GNU ld ååæ­»äº
#     PE é»è¾ï¼cannot perform PE operationsï¼ã
#
# OUT å¯ä»¥æå°ä»åºå¤é¢ï¼Windows çãåæ§æä»¶å¤¹è®¿é®ãä¼æ¦ä½æªç»åè®¸ç
# ç¨åºåãææ¡£ãç®å½ï¼Zig æ­£å¥½æ¯è¿ç§æ°ç¨åºãè¸©è¿ä¸æ¬¡ï¼è®°å¨è¿å¿ã

CC   := clang
ZIG  ?= zig
OUT  ?= build

CFLAGS  := --target=x86_64-unknown-none -std=c17 -O2 -g \
           -ffreestanding -fno-stack-protector \
           -mno-red-zone -mcmodel=kernel -mno-sse -mno-sse2 -mno-mmx \
           -Wall -Wextra -Wpedantic \
           -I. -I../include

OBJ := $(OUT)/kmain.o $(OUT)/serial.o $(OUT)/entry.o

.PHONY: all clean

all: $(OUT)/kernel.elf

$(OUT):
	@mkdir -p $(OUT)

$(OUT)/kmain.o: kmain.c | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/serial.o: arch/x86_64/serial.c | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/entry.o: boot/entry.S | $(OUT)
	$(CC) --target=x86_64-unknown-none -c $< -o $@

$(OUT)/kernel.elf: $(OBJ) link.ld
	$(ZIG) cc -target x86_64-freestanding -nostdlib -static \
	    -Wl,-T,link.ld -o $@ $(OBJ)

clean:
	rm -rf $(OUT)
