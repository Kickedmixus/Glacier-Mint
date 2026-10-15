CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
PKGS = gtk+-3.0 epoxy json-glib-1.0 libcurl
WEBKIT_PKG := $(shell if pkg-config --silence-errors --exists webkit2gtk-4.1; then echo webkit2gtk-4.1; elif pkg-config --silence-errors --exists webkit2gtk-4.0; then echo webkit2gtk-4.0; fi)

ifneq ($(WEBKIT_PKG),)
CFLAGS += -DHAVE_WEBKIT
PKGS += $(WEBKIT_PKG)
endif

glacier-mint: src/main.c
	$(CC) $(CFLAGS) -Isrc $< -o $@ $$(pkg-config --cflags --libs $(PKGS)) -lX11 -lXext -lGL -lm

.PHONY: clean run
clean:
	rm -f glacier-mint

run: glacier-mint
	./glacier-mint
