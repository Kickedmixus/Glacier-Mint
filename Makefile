CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
PKGS = gtk+-3.0 epoxy json-glib-1.0 libcurl
SOURCES = src/main.c src/shader.c src/storage.c src/network.c src/actions.c src/browser.c src/ui.c src/wallpaper.c
WEBKIT_PKG := $(shell if pkg-config --silence-errors --exists webkit2gtk-4.1; then echo webkit2gtk-4.1; elif pkg-config --silence-errors --exists webkit2gtk-4.0; then echo webkit2gtk-4.0; fi)

ifneq ($(WEBKIT_PKG),)
CFLAGS += -DHAVE_WEBKIT
PKGS += $(WEBKIT_PKG)
endif

glacier-mint: $(SOURCES) src/app.h logo1600x.png
	$(CC) $(CFLAGS) -Isrc $(SOURCES) -o $@ $$(pkg-config --cflags --libs $(PKGS)) -lX11 -lXext -lGL -lm

.PHONY: clean run
clean:
	rm -f glacier-mint

run: glacier-mint
	./glacier-mint
