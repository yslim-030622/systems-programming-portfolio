BINS = wfs mkfs
CC = gcc
CFLAGS = -Wall -Werror -pedantic -std=gnu18 -g
FUSE_PKG ?= fuse
FUSE_CFLAGS = `pkg-config $(FUSE_PKG) --cflags --libs`
.PHONY: all
all: $(BINS)
wfs:
	$(CC) $(CFLAGS) wfs.c $(FUSE_CFLAGS) -o wfs
mkfs:
	$(CC) $(CFLAGS) -o mkfs mkfs.c
.PHONY: clean
clean:
	rm -rf $(BINS)
