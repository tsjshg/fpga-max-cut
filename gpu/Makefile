# Mac の GPU（Metal）版。シェーダは実行時にソースからコンパイルする（Xcode の metal コマンドが無くても動く）
CC ?= cc
CFLAGS ?= -O2
CFLAGS += -Wall -Wextra -fobjc-arc

sa_gpu: sa_gpu.m sa_gpu_metal.h
	$(CC) $(CFLAGS) -o $@ sa_gpu.m -framework Metal -framework Foundation

sa_gpu_metal.h: sa_gpu.metal
	xxd -i sa_gpu.metal > $@

clean:
	rm -f sa_gpu sa_gpu_metal.h
