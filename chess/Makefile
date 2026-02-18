# ----------------------------
# Makefile Options
# ----------------------------

NAME = CHESS
ICON = icon.png
DESCRIPTION = "Chess for TI-84 Plus CE"
COMPRESSED = NO

CFLAGS = -Wall -Wextra -Oz -I engine/src -DPAWN_CACHE_SIZE=16
CXXFLAGS = -Wall -Wextra -Oz

EXTRA_C_SOURCES = \
    engine/src/board.c \
    engine/src/movegen.c \
    engine/src/zobrist.c \
    engine/src/eval.c \
    engine/src/tt.c \
    engine/src/search.c \
    engine/src/engine.c \
    engine/src/book.c

# ----------------------------

include $(shell cedev-config --makefile)

# ----------------------------
# Book AppVar generation
# ----------------------------

BOOK_BIN_small  = books/book_small.bin
BOOK_BIN_medium = books/book_medium.bin
BOOK_BIN_large  = books/book_large.bin
BOOK_BIN_xl     = books/book_xl.bin
BOOK_BIN_xxl    = books/book_xxl.bin

book: tools/gen_book_appvar.py
	@echo "Usage: make book TIER=<small|medium|large|xl|xxl>"
	@test -n "$(TIER)" || (echo "Error: TIER not set" && exit 1)
	python3 tools/gen_book_appvar.py $(BOOK_BIN_$(TIER)) books/ $(TIER)

.PHONY: book
