ARCH := $(shell getconf LONG_BIT)
name32 = libframetime32.so
name64 = libframetime64.so
src = $(wildcard *.c)
 
.PHONY: all clean
 
CFLAGS32 ?= -O3 -Wall -Wextra -m32
CFLAGS64 ?= -O3 -Wall -Wextra
 
LDFLAGS += -Wl,-O1
 
all: $(src)
    ifeq ($(ARCH),64)
        $(CC) -o $(name64) -shared $(CFLAGS64) $(LDFLAGS) $(src) -ldl -fPIC
        strip --strip-unneeded $(name64)
        $(CC) -o $(name32) -shared $(CFLAGS32) $(LDFLAGS) $(src) -ldl -fPIC
        strip --strip-unneeded $(name32)
    else
        $(CC) -o $(name32) -shared $(CFLAGS32) $(LDFLAGS) $(src) -ldl -fPIC
        strip --strip-unneeded $(name32)
    endif
 
clean:
        rm -f $(name32) $(name64) *.o
