# Mac（clang）とボード（gcc・Cortex-A9）の両方で焼く。整数しか使わないので浮動小数の焼き方は結果に効かない。
CC ?= cc
UNAME_M := $(shell uname -m)
ifeq ($(UNAME_M),armv7l)
CFLAGS ?= -O3 -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard
else
CFLAGS ?= -O3 -mcpu=native
endif
CFLAGS += -std=c11 -Wall -Wextra

all: sa sa_fast

sa: sa.c
	$(CC) $(CFLAGS) -o $@ $< -lpthread

sa_fast: sa_fast.c sa_fast_grp.h xs_fused.h
	$(CC) $(CFLAGS) -o $@ $< -lpthread

clean:
	rm -f sa sa_fast
