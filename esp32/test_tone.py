from machine import I2S, Pin
import struct
import math

print("Setting up I2S...")
print("BCK=GPIO26, DIN=GPIO25, WS/LRCK=GPIO33")

try:
    audio_out = I2S(
        0,
        sck=Pin(26),
        ws=Pin(33),
        sd=Pin(25),
        mode=I2S.TX,
        bits=16,
        format=I2S.STEREO,
        rate=22050,
        ibuf=4096,
    )
    print("I2S init OK")
except Exception as e:
    print("I2S error:", e)
    raise

print("Generating 440Hz tone...")
buf = bytearray(4000)
for i in range(1000):
    v = int(math.sin(2 * 3.14159 * 440 * i / 22050) * 24000)
    struct.pack_into("<hh", buf, i * 4, v, v)

print("Playing continuous tone - listen now!")
count = 0
while True:
    audio_out.write(buf)
    count += 1
    if count % 100 == 0:
        print("Still playing... block", count)
