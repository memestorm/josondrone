"""Upload files to ESP8266 via raw REPL - line by line with paste mode."""
import serial
import time
import os

PORT = '/dev/tty.usbserial-110'
BAUD = 115200

def connect():
    ser = serial.Serial(PORT, BAUD, timeout=1)
    ser.dtr = False
    ser.rts = True
    time.sleep(0.1)
    ser.rts = False
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

def raw_exec(ser, code, timeout=5):
    """Execute code in raw REPL and wait for response."""
    ser.read(4096)  # flush
    ser.write(code.encode() + b'\x04')
    # Wait for OK...> or error
    buf = b''
    start = time.time()
    while time.time() - start < timeout:
        chunk = ser.read(1024)
        buf += chunk
        if b'\x04>' in buf or b'\x04' in buf:
            break
    return buf

def upload_file(ser, local_path, remote_name):
    with open(local_path, 'r') as f:
        content = f.read()
    print(f"Uploading {remote_name} ({len(content)} bytes)...")
    # Open file
    raw_exec(ser, f"_f=open('{remote_name}','w')")
    # Write content in small line-based chunks
    lines = content.split('\n')
    batch = ''
    for line in lines:
        batch += line + '\n'
        if len(batch) > 128:
            escaped = repr(batch)
            raw_exec(ser, f"_f.write({escaped})")
            batch = ''
    if batch:
        escaped = repr(batch)
        raw_exec(ser, f"_f.write({escaped})")
    raw_exec(ser, "_f.close()")
    # Verify
    resp = raw_exec(ser, f"import os; print(os.stat('{remote_name}'))")
    print(f"  {remote_name} done. stat: {resp}")

if __name__ == '__main__':
    ser = connect()
    d = os.path.dirname(os.path.abspath(__file__))
    upload_file(ser, os.path.join(d, 'ssd1306.py'), 'ssd1306.py')
    upload_file(ser, os.path.join(d, 'main.py'), 'main.py')
    print("Rebooting...")
    ser.write(b'\x02')
    time.sleep(0.3)
    ser.write(b'\x04')
    time.sleep(2)
    resp = ser.read(4096)
    print("Board reset. Display should come alive!")
    ser.close()
