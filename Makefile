BINS = sparql-query
REQUIRES = glib-2.0 libcurl
gitrev := $(shell git-rev-parse HEAD)

# PROFILE = -pg
CFLAGS = -std=gnu99 -Wall -DGIT_REV=\"$(gitrev)\" $(PROFILE) -g -O2 `pkg-config --cflags $(REQUIRES)`
LDFLAGS = $(PROFILE) `pkg-config --libs $(REQUIRES)` -lreadline -lncurses

all: $(BINS)

install:
	mkdir -p $(DESTDIR)/usr/local/bin/
	install $(BINS) $(DESTDIR)/usr/local/bin/

clean:
	rm -f *.o $(BINS)

sparql-query: sparql-query.o
	$(CC) $(LDFLAGS) -o $@ $^
