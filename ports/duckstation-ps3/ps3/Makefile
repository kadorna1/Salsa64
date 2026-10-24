# Standalone PPU code-cache test; future builds will link the DuckStation core.
PROGRAM := duckstation-ps3-probe
TITLE := DuckStation PS3 JIT Probe
APPID := DCKS00001
CONTENTID ?= UP0001-$(APPID)_00-0000000000000000

PS3DEV ?= /usr/local/ps3dev
PSL1GHT ?= $(PS3DEV)
PPU_CC ?= $(PS3DEV)/ppu/bin/ppu-gcc
PPU_STRIP ?= $(PS3DEV)/ppu/bin/ppu-strip
OUT := $(CURDIR)/build
OBJ := $(OUT)/obj
ICON0 ?= $(PS3DEV)/bin/ICON0.PNG

PPU_CFLAGS := -std=gnu11 -O2 -Wall -Wextra -mcpu=cell -mhard-float \
              -ffunction-sections -fdata-sections -mno-relative-jumptables \
              -I$(PSL1GHT)/ppu/include -Iinclude
PPU_LDFLAGS := -mcpu=cell -mhard-float -Wl,--gc-sections \
               -Wl,-Map,$(OUT)/$(PROGRAM).map -L$(PSL1GHT)/ppu/lib \
               -T $(CURDIR)/linker/duckstation-jit-cache.ld \
               -T $(PSL1GHT)/ppu/powerpc64-ps3-elf/lib/lv2.ld
LIBS := -lio -lsysutil -llv2 -lm

SOURCES := src/main.c src/ps3_jit_probe.c src/ppc64_emit.c
OBJECTS := $(patsubst src/%.c,$(OBJ)/%.o,$(SOURCES))

.PHONY: all ppu pkg test-host clean
all: ppu
ppu: $(OUT)/$(PROGRAM).elf

$(OBJ)/%.o: src/%.c
	@mkdir -p $(@D)
	$(PPU_CC) $(PPU_CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/$(PROGRAM).raw.elf: $(OBJECTS) | $(OUT)
	$(PPU_CC) $(PPU_LDFLAGS) $^ $(LIBS) -o $@

$(OUT)/$(PROGRAM).elf: $(OUT)/$(PROGRAM).raw.elf
	$(PPU_STRIP) $< -o $@

$(OUT):
	mkdir -p $@

pkg: $(OUT)/$(PROGRAM).elf
	@test -f "$(ICON0)" || { echo "Missing ICON0.PNG: set ICON0=/path/to/ICON0.PNG" >&2; exit 1; }
	rm -rf $(OBJ)/pkg && mkdir -p $(OBJ)/pkg/USRDIR
	cp "$(ICON0)" $(OBJ)/pkg/ICON0.PNG
	$(PS3DEV)/bin/make_self_npdrm $< $(OBJ)/pkg/USRDIR/EBOOT.BIN $(CONTENTID)
	$(PS3DEV)/bin/sfo --title "$(TITLE)" --appid "$(APPID)" -f $(PS3DEV)/bin/sfo.xml $(OBJ)/pkg/PARAM.SFO
	$(PS3DEV)/bin/pkg --contentid $(CONTENTID) $(OBJ)/pkg/ $(OUT)/$(PROGRAM).pkg
	cp $(OUT)/$(PROGRAM).pkg $(OUT)/$(PROGRAM).gnpdrm.pkg
	$(PS3DEV)/bin/package_finalize $(OUT)/$(PROGRAM).gnpdrm.pkg

$(OBJ)/test_ppc64_emit: tests/test_ppc64_emit.c src/ppc64_emit.c include/ppc64_emit.h
	@mkdir -p $(@D)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/test_ppc64_emit.c src/ppc64_emit.c -o $@

test-host: $(OBJ)/test_ppc64_emit
	$<

clean:
	rm -rf $(OUT)

-include $(OBJECTS:.o=.d)
