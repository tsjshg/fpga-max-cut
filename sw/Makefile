# Mac（clang）とボード（gcc・Cortex-A9）の両方で焼く。整数しか使わないので浮動小数の焼き方は結果に効かない。
CC ?= cc
UNAME_M := $(shell uname -m)
ifeq ($(UNAME_M),armv7l)
CFLAGS ?= -O3 -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard
else
CFLAGS ?= -O3 -mcpu=native
endif
CFLAGS += -std=c11 -Wall -Wextra

sa: sa.c
	$(CC) $(CFLAGS) -o $@ $< -lpthread

clean:
	rm -f sa
