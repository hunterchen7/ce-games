CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11
SRCDIR = src
TESTDIR = test
UCIDIR = uci
BUILDDIR = build

SRCS = $(SRCDIR)/board.c $(SRCDIR)/movegen.c $(SRCDIR)/zobrist.c \
       $(SRCDIR)/eval.c $(SRCDIR)/tt.c $(SRCDIR)/search.c $(SRCDIR)/engine.c
OBJS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRCS))

.PHONY: all clean perft uci test-search

all: perft uci test-search

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Perft test binary
perft: $(OBJS) $(TESTDIR)/perft.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(OBJS) $(TESTDIR)/perft.c -o $(BUILDDIR)/perft

# UCI engine binary
uci: $(OBJS) $(UCIDIR)/uci.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(OBJS) $(UCIDIR)/uci.c -o $(BUILDDIR)/uci

# Search & eval test binary
test-search: $(OBJS) $(TESTDIR)/test_search.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(OBJS) $(TESTDIR)/test_search.c -o $(BUILDDIR)/test_search

clean:
	rm -rf $(BUILDDIR)
