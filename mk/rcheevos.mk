# SPDX-License-Identifier: GPL-2.0-or-later
# Fuentes y opciones de third_party/rcheevos (docs/THIRD_PARTY.md), para todo
# Makefile que las use. Antes de incluir este archivo: RC_TOP := la raíz del
# repositorio (relativa o absoluta).
#   RC_FILES  los .c, relativos a RC_TOP
#   RC_DEFS   las definiciones (todo el que incluya rc_client.h las usa igual)
#   RC_INC    las rutas de inclusión
RC_DIR   := third_party/rcheevos
RC_FILES := $(RC_DIR)/src/rc_client.c $(RC_DIR)/src/rc_client_external.c $(RC_DIR)/src/rc_compat.c \
            $(RC_DIR)/src/rc_util.c $(RC_DIR)/src/rc_version.c \
            $(patsubst $(RC_TOP)/%,%,$(sort $(wildcard $(RC_TOP)/$(RC_DIR)/src/rcheevos/*.c $(RC_TOP)/$(RC_DIR)/src/rapi/*.c))) \
            $(RC_DIR)/src/rhash/hash.c $(RC_DIR)/src/rhash/hash_rom.c $(RC_DIR)/src/rhash/md5.c
RC_DEFS  := -DRC_CLIENT_SUPPORTS_HASH -DRC_NO_THREADS -DRC_HASH_NO_DISC -DRC_HASH_NO_ZIP -DRC_HASH_NO_ENCRYPTED
RC_INC   := -I$(abspath $(RC_TOP))/$(RC_DIR)/include -I$(abspath $(RC_TOP))/$(RC_DIR)/src
