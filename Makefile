#CC=clang
#LD=ld.lld
CFLAGS=-Wall -Wunused -Os -ffunction-sections -fdata-sections -Wl,-gc-sections -Wl,-Map,link.map
#CFLAGS += -fuse-ld=`which $(LD)`

chip8: main.c
	$(CC) $(CFLAGS) main.c -o chip8
#	$(CC) -c $(CFLAGS) main.c -o main.o
#	$(LD) -gc-sections -Map=link.map -lc main.o -o chip8
clean:
	rm -rf chip8
