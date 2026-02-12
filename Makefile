# ----------------------------
# Top-level Makefile for ce-games
# Builds all games or individual targets
# ----------------------------

GAMES = pong sudoku

.PHONY: all clean $(GAMES)

all: $(GAMES)

$(GAMES):
	$(MAKE) -C $@

clean:
	@for game in $(GAMES); do \
		$(MAKE) -C $$game clean; \
	done
