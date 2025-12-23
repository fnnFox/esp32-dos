CC = xtensa-esp32-elf-gcc
OBJDUMP = xtensa-esp32-elf-objdump
READELF = xtensa-esp32-elf-readelf

CFLAGS = -c \
		 -fPIC \
		 -mlongcalls \
		 -fno-builtin \
		 -fno-jump-tables \
		 -mtext-section-literals \
		 -I./include

LDFLAGS = -shared \
		  -nostdlib

TARGET = guest

all: $(TARGET).so

$(TARGET).o: $(TARGET).c
	$(CC) $(CFLAGS) -o $@ $<

$(TARGET).so: $(TARGET).o
	$(CC) $(LDFLAGS) -o $@ $<

dump: $(TARGET).so
	$(READELF) -h -s -r $<

clean:
	rm -f *.o *.so

.PHONY: all dump clean
