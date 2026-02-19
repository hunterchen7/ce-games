NAME = EMUUCI
ICON = icon.png
DESCRIPTION = "Chess engine emulator UCI bridge"
COMPRESSED = NO
HAS_PRINTF = YES

CFLAGS = -Wall -Wextra -Oz -I ../engine/src -DPAWN_CACHE_SIZE=16 -DNO_BOOK
CXXFLAGS = -Wall -Wextra -Oz

EXTRA_C_SOURCES = \
    ../engine/src/board.c \
    ../engine/src/movegen.c \
    ../engine/src/zobrist.c \
    ../engine/src/eval.c \
    ../engine/src/tt.c \
    ../engine/src/search.c \
    ../engine/src/engine.c \
    ../engine/src/book.c

include $(shell cedev-config --makefile)
