# Phase-0 PPE/JIT probe built with Salsa64's shared PS3 make/packaging rules.
PROGRAM := duckstation-ps3-probe
TITLE := DuckStation PS3 JIT Probe
APPID := DCKS00001
CFLAGS := -I$(CURDIR)/include
LIBS := -lio -lsysutil -llv2 -lm
LDFLAGS := -T $(CURDIR)/linker/duckstation-jit-cache.ld -T lv2.ld
SRCS := src/main.c src/ps3_jit_probe.c src/ppc64_emit.c

# Reuse Salsa64's common PSL1GHT build, ELF/SELF, and GNPDRM PKG targets.
include $(abspath ../../../mk/ps3.mk)
