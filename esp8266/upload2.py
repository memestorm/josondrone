"""Upload files to ESP8266 via raw REPL - single exec per file."""
import serial
import time
import os
import binascii

PORT = '/dev/tty.usbserial-110'
BAUD = 115200

def connect():
    ser = serial.Serial(PORT, BAUD, timeout=1)
    ser.dtr = False
    ser.rts = True
    time.sleep(0.1)
    ser.rts = False
    time.sleep(0.1)
    buf = b''
    for i in range(30):
        chunk = ser.read(1024)
        buf += chunk
        if b'>>> ' in buf:
            ser.write(b'\x03\x03\x01')
            time.sleep(0.5)
            resp = ser.read(4096)
            if b'raw REPL' in resp:
                print("Connected to raw REPL")
                return ser
        ser.write(b'\x03')
        time.sleep(0.2)
    raise Exception("Could not enter raw REPL")

def upload_file(ser, local_path, remote_name):
    with open(local_path, 'rb') as f:
        data = f.read()
    b64 = binascii.b2a_base64(data).decode().strip()
    print(f"Uploading {remote_name} ({len(data)} bytes)...")
    code = f"import binascii\nf=open('{remote_name}','wb')\nf.write(binascii.a2b_base64('{b64}'))\nf.close()\nprint('OK')\n"
    ser.write(code.encode())
    ser.write(b'\x04')  # Ctrl-D to execute
    time.sleep(3)
    resp = ser.read(8192)
    if b'OK' in resp:
        print(f"  {remote_name} uploaded OK")
    else:
        print(f"  Response: {resp}")

if __name__ == '__main__':
    ser = connect()
    script_dir = os.path.dirname(os.path.abspath(__file__))
    upload_file(ser, os.path.join(script_dir, 'ssd1306.py'), 'ssd1306.py')
    upload_file(ser, os.path.join(script_dir, 'main.py'), 'main.py')
    print("Rebooting board...")
    ser.write(b'\x02')  # Ctrl-B exit raw REPL
    time.sleep(0.3)
    ser.write(b'\x04')  # Ctrl-D soft reset
    time.sleep(1)
    resp = ser.read(4096)
    print("Board reset. Display should come alive!")
    ser.close()
