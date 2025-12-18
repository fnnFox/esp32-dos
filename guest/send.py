import serial
import time
import sys

# Настройки
PORT = '/dev/ttyUSB0'
BAUD = 115200
FILE = 'guest.o'

try:
    ser = serial.Serial(PORT, BAUD, timeout=0.1)
    print(f"Waiting for ESP32 on {PORT}...")

    # Пауза перед стартом
    time.sleep(0.1)
    
    # Читаем файл
    with open(FILE, "rb") as f:
        data = f.read()

    print(f"Sending {len(data)} bytes...")

    for byte in data:
        ser.write(bytes([byte])) 
        time.sleep(0.002) 

    print("Done!")
    
    # print("Done. Listening for result...")
    # 
    # while True:
    #     line = ser.readline().decode('utf-8', errors='ignore')
    #     if line:
    #         print(f"[ESP]: {line.strip()}")

except KeyboardInterrupt:
    print("\nExiting...")
except Exception as e:
    print(f"Error: {e}")
