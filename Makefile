BINS = sparql-query
TESTS = scan-test
LINKS = sparql-update
REQUIRES = glib-2.0 libcurl libxml-2.0
gitrev := $(shell git rev-parse --short HEAD)

# PROFILE = -pg
CFLAGS = -std=gnu99 -Wall -DGIT_REV=\"$(gitrev)\" $(PROFILE) -g -O2 `pkg-config --cflags $(REQUIRES)`
LDFLAGS = $(PROFILE) `pkg-config --libs $(REQUIRES)` -lreadline -lncurses

all: $(BINS) $(LINKS)

sparql-update:
	ln -s sparql-query sparql-update

install:
	mkdir -p $(DESTDIR)/usr/local/bin/
	install $(BINS) $(DESTDIR)/usr/local/bin/
	ln -s -f $(DESTDIR)/usr/local/bin/sparql-query $(DESTDIR)/usr/local/bin/sparql-update

clean:
	rm -f *.o $(BINS) $(LINKS) $(TESTS)

scan-test: scan-test.o scan-sparql.o
	$(CC) $(LDFLAGS) -o $@ $^

sparql-query: sparql-query.o result-parse.o scan-sparql.o
	$(CC) $(LDFLAGS) -o $@ $^
