name = libframetime.so
src = $(wildcard *.c)

.PHONY: all clean

CFLAGS ?= -O3
CFLAGS += -Wall -Wextra

LDFLAGS += -Wl,-O1

all: $(src)
	$(CC) -o $(name) -shared $(CFLAGS) $(LDFLAGS) $(src) -ldl

clean:
	rm -f $(name) *.o
