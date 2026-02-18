# ----------------------------
# Makefile Options
# ----------------------------

NAME = CHESS
ICON = icon.png
DESCRIPTION = "Chess for TI-84 Plus CE"
COMPRESSED = NO

# Opening book: none, small, medium, large, xl, xxl
BOOK ?= medium

# Map BOOK tier to BOOK_TIER define (0 = disabled)
ifeq ($(BOOK),small)
BOOK_TIER = 1
else ifeq ($(BOOK),medium)
BOOK_TIER = 2
else ifeq ($(BOOK),large)
BOOK_TIER = 3
else ifeq ($(BOOK),xl)
BOOK_TIER = 4
else ifeq ($(BOOK),xxl)
BOOK_TIER = 5
else
BOOK_TIER = 0
endif

CFLAGS = -Wall -Wextra -Oz -I engine/src -DBOOK_TIER=$(BOOK_TIER) -DPAWN_CACHE_SIZE=16
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

BOOK_BIN_small  = books/book_small.bin
BOOK_BIN_medium = books/book_medium.bin
BOOK_BIN_large  = books/book_large.bin
BOOK_BIN_xl     = books/book_xl.bin
BOOK_BIN_xxl    = books/book_xxl.bin

# ----------------------------

include $(shell cedev-config --makefile)

# ----------------------------
# Book AppVar generation
# ----------------------------

ifneq ($(BOOK),none)
book: $(BOOK_BIN_$(BOOK)) tools/gen_book_appvar.py
	python3 tools/gen_book_appvar.py $(BOOK_BIN_$(BOOK)) books/ $(BOOK)

.PHONY: book
endif
