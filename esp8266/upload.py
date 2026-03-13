"""Upload files to ESP8266 MicroPython via raw REPL with DTR/RTS reset trick."""
import serial
import time
import sys
import os

PORT = '/dev/tty.usbserial-110'
BAUD = 115200

def connect():
    ser = serial.Serial(PORT, BAUD, timeout=1)
    # Reset board via DTR/RTS
    ser.dtr = False
    ser.rts = True
    time.sleep(0.1)
    ser.rts = False
    time.sleep(0.1)
    # Wait for prompt
    buf = b''
    for i in range(30):
        chunk = ser.read(1024)
        buf += chunk
        if b'>>> ' in buf:
            ser.write(b'\x03\x03\x01')  # Ctrl-C x2, then Ctrl-A for raw REPL
            time.sleep(0.5)
            resp = ser.read(4096)
            if b'raw REPL' in resp:
                print("Connected to raw REPL")
                return ser
        ser.write(b'\x03')
        time.sleep(0.2)
    raise Exception("Could not enter raw REPL")

def exec_raw(ser, code):
    ser.write(code.encode() + b'\x04')
    time.sleep(0.3)
    resp = ser.read(8192)
    return resp

def upload_file(ser, local_path, remote_name):
    with open(local_path, 'r') as f:
        content = f.read()
    print(f"Uploading {remote_name} ({len(content)} bytes)...")
    # Write in chunks to avoid buffer overflow
    exec_raw(ser, f"f=open('{remote_name}','w')")
    chunk_size = 256
    for i in range(0, len(content), chunk_size):
        chunk = content[i:i+chunk_size]
        escaped = chunk.replace('\\', '\\\\').replace("'", "\\'").replace('\n', '\\n').replace('\r', '')
        exec_raw(ser, f"f.write('{escaped}')")
    exec_raw(ser, "f.close()")
    print(f"  {remote_name} uploaded OK")

if __name__ == '__main__':
    ser = connect()
    script_dir = os.path.dirname(os.path.abspath(__file__))
    upload_file(ser, os.path.join(script_dir, 'ssd1306.py'), 'ssd1306.py')
    upload_file(ser, os.path.join(script_dir, 'main.py'), 'main.py')
    print("Rebooting board...")
    # Exit raw REPL and reset
    ser.write(b'\x02')  # Ctrl-B to exit raw REPL
    time.sleep(0.3)
    ser.write(b'\x04')  # Ctrl-D to soft reset
    time.sleep(1)
    resp = ser.read(4096)
    print("Board reset. main.py should be running now!")
    ser.close()
