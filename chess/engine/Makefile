CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11 -DNO_BOOK
SRCDIR = src
TESTDIR = test
UCIDIR = uci
BUILDDIR = build

SRCS = $(SRCDIR)/board.c $(SRCDIR)/movegen.c $(SRCDIR)/zobrist.c \
       $(SRCDIR)/eval.c $(SRCDIR)/tt.c $(SRCDIR)/search.c $(SRCDIR)/engine.c
OBJS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRCS))

ABLATION_FEATURES = TEMPO PAWNS PASSED ROOK_FILES MOBILITY SHIELD

.PHONY: all clean perft uci test-search test-integration bench eval-fen ablation

all: perft uci test-search test-integration

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

# Integration test binary
test-integration: $(OBJS) $(TESTDIR)/test_integration.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(OBJS) $(TESTDIR)/test_integration.c -o $(BUILDDIR)/test_integration

# Desktop benchmark binary
bench: $(OBJS) $(TESTDIR)/bench.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(OBJS) $(TESTDIR)/bench.c -o $(BUILDDIR)/bench

# Static eval utility (FEN -> centipawn score)
eval-fen: $(OBJS) $(TESTDIR)/eval_fen.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(OBJS) $(TESTDIR)/eval_fen.c -o $(BUILDDIR)/eval_fen

# Ablation: build full + all NO_* variants
ablation: uci
	@cp $(BUILDDIR)/uci $(BUILDDIR)/uci_full
	@for feat in $(ABLATION_FEATURES); do \
		echo "Building uci_no_$$(echo $$feat | tr 'A-Z' 'a-z')..."; \
		$(CC) $(CFLAGS) -DNO_$$feat \
			$(patsubst $(SRCDIR)/%.c,$(SRCDIR)/%.c,$(SRCS)) \
			$(UCIDIR)/uci.c -o $(BUILDDIR)/uci_no_$$(echo $$feat | tr 'A-Z' 'a-z'); \
	done

clean:
	rm -rf $(BUILDDIR)
