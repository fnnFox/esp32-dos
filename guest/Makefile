CC = xtensa-esp32-elf-gcc
OBJDUMP = xtensa-esp32-elf-objdump
OBJCOPY = xtensa-esp32-elf-objcopy

CFLAGS = -c \
		 -O2 \
		 -mlongcalls \
		 -fno-builtin \
		 -nostdlib \
		 -ffunction-sections \
		 -fdata-sections \
		 -mtext-section-literals \
		 -I./include

TARGET = guest

all: $(TARGET).o

$(TARGET).o: $(TARGET).c
	$(CC) $(CFLAGS) -o $@ $<

dump: $(TARGET).o
	$(OBJDUMP) -d -t -r $<

clean:
	rm -f *.o

.PHONY: all dump clean
