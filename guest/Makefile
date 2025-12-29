CC = xtensa-esp32-elf-gcc
OBJDUMP = xtensa-esp32-elf-objdump
OBJCOPY = xtensa-esp32-elf-objcopy

CFLAGS = -c \
		 -mlongcalls \
		 -I./include

TARGET = guest

all: $(TARGET).mod

$(TARGET).mod: $(TARGET).c
	$(CC) $(CFLAGS) -o $@ $<

dump: $(TARGET).mod
	$(OBJDUMP) -d -t -r $<

clean:
	rm -f *.mod

.PHONY: all dump clean
