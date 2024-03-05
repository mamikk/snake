
all: snake.com floppy.img

leveldata.h: mklevels.py
	./mklevels.py > $@

snake.com: snake.c leveldata.h com.ld
	gcc -Wall -W -Wextra -std=gnu99 -fno-pie -static -Os -nostdlib -fno-asynchronous-unwind-tables -ffreestanding -m16 -march=i386 -Wl,--build-id=none,--nmagic,--script=com.ld snake.c -o snake.com

floppy.img: snake.com
	./mkfloppy.sh $@ $^

clean:
	rm -f snake.com floppy.img leveldata.h

.PHONY: all clean
