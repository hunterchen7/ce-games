CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11
SRCDIR = src
TESTDIR = test
BUILDDIR = build

SRCS = $(SRCDIR)/board.c $(SRCDIR)/movegen.c $(SRCDIR)/zobrist.c
OBJS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRCS))

.PHONY: all clean perft

all: perft

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Perft test binary
perft: $(OBJS) $(TESTDIR)/perft.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(OBJS) $(TESTDIR)/perft.c -o $(BUILDDIR)/perft

clean:
	rm -rf $(BUILDDIR)
