APP = dpdk-tx
CC = g++
PKGCONF ?= pkg-config

### O3 has the best stability here
CFLAGS += -O2 $(shell $(PKGCONF) --cflags libdpdk) -DALLOW_EXPERIMENTAL_API -Wall -Wpedantic -std=c++20 -Werror -Wno-packed-not-aligned
LDFLAGS = $(shell $(PKGCONF) --libs libdpdk) -lm

all: $(APP)

$(APP): main.o
	$(CC) $(CFLAGS) main.o -o $(APP) $(LDFLAGS)

main.o: main.cpp main.hpp ./Makefile
	$(CC) -c $(CFLAGS) main.cpp -o main.o

clean:
	rm -f *.o $(APP)

clean_stats:
	rm -f ./*.csv ./*.png ./*.pdf ./*_logout_*

