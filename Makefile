BINS = sparql-query
REQUIRES = glib-2.0 libcurl libxml-2.0
gitrev := $(shell git-rev-parse --short HEAD)

# PROFILE = -pg
CFLAGS = -std=gnu99 -Wall -DGIT_REV=\"$(gitrev)\" $(PROFILE) -g -O2 `pkg-config --cflags $(REQUIRES)`
LDFLAGS = $(PROFILE) `pkg-config --libs $(REQUIRES)` -lreadline -lncurses

all: $(BINS)

install:
	mkdir -p $(DESTDIR)/usr/local/bin/
	install $(BINS) $(DESTDIR)/usr/local/bin/

clean:
	rm -f *.o $(BINS)

sparql-query: sparql-query.o result-parse.o
	$(CC) $(LDFLAGS) -o $@ $^
