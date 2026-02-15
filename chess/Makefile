# ----------------------------
# Makefile Options
# ----------------------------

NAME = CHESS
ICON = icon.png
DESCRIPTION = "Chess for TI-84 Plus CE"
COMPRESSED = NO

CFLAGS = -Wall -Wextra -Oz -I engine/src
CXXFLAGS = -Wall -Wextra -Oz

EXTRA_C_SOURCES = \
    engine/src/board.c \
    engine/src/movegen.c \
    engine/src/zobrist.c \
    engine/src/eval.c \
    engine/src/tt.c \
    engine/src/search.c \
    engine/src/engine.c

# ----------------------------

include $(shell cedev-config --makefile)
