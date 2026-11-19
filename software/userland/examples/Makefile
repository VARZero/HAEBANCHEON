# Use CROSS_COMPILE=/absolute/buildroot/host/bin/riscv32-buildroot-linux-musl-
# on the host; leave it empty for native compilation on the board.
CC = $(CROSS_COMPILE)gcc
CFLAGS ?= -O2 -Wall -Wextra
OUTPUT ?= build
.PHONY: all install
all: $(OUTPUT)/hello
$(OUTPUT)/hello: hello.c
	mkdir -p $(OUTPUT)
	$(CC) $(CFLAGS) $< -o $@
install: all
	install -D -m 0755 $(OUTPUT)/hello $(DESTDIR)/data/apps/hello
	install -D -m 0755 hello.py $(DESTDIR)/data/apps/hello-python
