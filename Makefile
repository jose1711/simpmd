.PHONY: all bin check clean install-data

all: bin

bin:
	@$(MAKE) -C src

clean:
	@$(MAKE) -C src clean
	@$(MAKE) -C tests clean

# Tests of the modules that can run without a display (see tests/)
check:
	@$(MAKE) -C tests check


# Copies ROMs, font, logos, games.lst and tapes into one flat directory (PMD_SHARE, with a trailing slash)
install-data:
	@test -n "$(PMD_SHARE)" || { echo "Usage: make install-data PMD_SHARE=<directory>/"; exit 1; }
	mkdir -p "$(PMD_SHARE)"
	find data -type f ! -path 'data/data/*' ! -name COPYING ! -name games.lst -exec cp -n {} "$(PMD_SHARE)" \;
	cp -f data/games.lst "$(PMD_SHARE)"
