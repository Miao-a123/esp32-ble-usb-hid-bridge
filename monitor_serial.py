#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ESP32-S3 Serial Monitor Script
- Does not trigger DTR/RTS to avoid FTDI-caused ESP32-S3 resets
- Real-time serial output display with timestamps
"""

import serial
import sys
import time
from datetime import datetime

PORT = "COM3"
BAUD = 115200
TIMEOUT_SEC = 60  # Monitor duration (seconds)

def main():
    print(f"[{datetime.now().strftime('%H:%M:%S')}] Opening {PORT} @ {BAUD} baud...")
    try:
        ser = serial.Serial(
            port=PORT,
            baudrate=BAUD,
            timeout=0.1,
            dsrdtr=False,
            rtscts=False,
        )
        ser.dtr = False
        ser.rts = False
    except Exception as e:
        print(f"Failed to open serial port: {e}")
        sys.exit(1)

    print(f"[{datetime.now().strftime('%H:%M:%S')}] Monitoring started (duration {TIMEOUT_SEC} seconds)...")
    print("=" * 60)

    start = time.time()
    try:
        while time.time() - start < TIMEOUT_SEC:
            data = ser.read(256)
            if data:
                try:
                    text = data.decode('utf-8', errors='replace')
                    timestamp = datetime.now().strftime('%H:%M:%S')
                    for line in text.split('\n'):
                        if line.strip():
                            print(f"[{timestamp}] {line}")
                except:
                    pass
    except KeyboardInterrupt:
        print("\nUser interrupted")
    finally:
        ser.close()
        print("=" * 60)
        print(f"[{datetime.now().strftime('%H:%M:%S')}] Monitoring ended")

if __name__ == "__main__":
    main()
