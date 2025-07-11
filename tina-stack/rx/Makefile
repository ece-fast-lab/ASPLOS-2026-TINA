APP = dpdk-rx
CC = g++
PKGCONF ?= pkg-config

# Check if DEBUG flag is set
ifdef DEBUG
OPTFLAG = -g
else
OPTFLAG = -O1
endif

CFLAGS += $(OPTFLAG) $(shell $(PKGCONF) --cflags libdpdk)  -Wall -g

LDFLAGS = $(shell $(PKGCONF) --libs libdpdk) -lcrypto 
SOURCE_FILES = main.cpp main.h ./apps/*

all: dpdk-rx 

#### Unprofiled one should has processing time of 210ns w/o TX
#### Profiled one should has processing time of 180ns w/o TX
profiled_main:  check_optflag ./Makefile $(SOURCE_FILES)
	@echo "\033[1;33m\033[1m[START Profiling] Building with Profile Generation\033[0m"
	$(CC) $(CFLAGS) -fprofile-generate main.cpp -o $(APP) $(LDFLAGS)
	@echo "\033[1;33m\033[1m[START Profiling] Test Run Starts\033[0m"
	@bash -c 'sudo timeout 3s ./dpdk-rx -l 1-3 -a 0000:18:00.1 -- -y 1024 -l 100 -i 100 -a 0 -b 1 || [ $$? -eq 124 ]'
	@echo "\033[1;33m\033[1m[START Profiling] Tunning Compilation\033[0m"
	$(CC) $(CFLAGS) -fprofile-use main.cpp -o $(APP) $(LDFLAGS)
	@echo "\033[1;33m\033[1m[START Profiling] Validation Run Starts\033[0m"
	@bash -c 'sudo timeout 3s ./dpdk-rx -l 1-3 -a 0000:18:00.1 -- -y 1024 -l 100 -i 100 -a 0 -b 1 || [ $$? -eq 124 ]'
	@echo "\033[1;33m\033[1m[END Profiling] Building with Profile Generation\033[0m"

$(APP):  check_optflag ./Makefile $(SOURCE_FILES)
	$(CC) $(CFLAGS) main.cpp -o $(APP) $(LDFLAGS)

	
check_optflag:
	@if [ "$(OPTFLAG)" != "-O1" ]; then \
		echo "\033[1;31m\033[1m[WARNING] OPTFLAG is not set to -O1\033[0m"; \
	fi

objdump: $(APP)
	objdump -d $(APP) > $(APP).asm
	objdump -h $(APP)

clean:
	rm -f *.o $(APP)
	rm -f *.gcda *.gcno
