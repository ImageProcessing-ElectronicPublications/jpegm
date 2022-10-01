CFLAGS+=-std=c99 -pedantic -Wall -Wextra -march=native -O3 -D_XOPEN_SOURCE -D_GNU_SOURCE -g
LDFLAGS+=-rdynamic
LDLIBS+=-lm
BINS= jpegmenc jpegmdec
LIBJPG= libjpegm.a
BINDIR?=$(DESTDIR)$(PREFIX)/usr/bin

CFLAGS+=$(EXTRA_CFLAGS)
LDFLAGS+=$(EXTRA_LDFLAGS)
LDLIBS+=$(EXTRA_LDLIBS)

AR=ar
INSTALL=install
RM=rm

OBJLIB= src/common.o src/io.o src/huffman.o src/coeffs.o src/imgproc.o src/frame.o
OBJENC= src/encoder.o
OBJDEC= src/decoder.o

.PHONY: all clean distclean install

all: $(BINS)

clean:
	$(RM) -- $(BINS) $(LIBJPG) $(OBJLIB) $(OBJENC) $(OBJDEC)

distclean: clean
	$(RM) -- *.gcda

$(LIBJPG): $(OBJLIB)
	$(AR) crs $@ $^

jpegmenc: $(OBJENC) $(LIBJPG)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@

jpegmdec: $(OBJDEC) $(LIBJPG)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@

install: all
	$(INSTALL) -d $(BINDIR)
	$(INSTALL) -m 755 $(BINS) $(BINDIR)
