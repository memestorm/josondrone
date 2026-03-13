from machine import I2S, Pin
import struct
import math
import gc

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

PI2 = 6.28318
SR = 22050
VOL = 2200
N = 401

def make_buf(harmonics):
    b = bytearray(N * 4)
    for i in range(N):
        t = i / SR
        v = 0
        for freq, amp in harmonics:
            v += amp * math.sin(PI2 * freq * t)
        s = int(v * VOL / 2.5)
        s = max(-32767, min(32767, s))
        struct.pack_into('<hh', b, i * 4, s, s)
    return b

print("Gen...")
specs = [
    [(55,1.0),(110,0.5),(165,0.3),(220,0.15),(880,0.03),(1100,0.02)],
    [(55,1.0),(110,0.55),(165,0.25),(220,0.2),(880,0.02),(1320,0.03)],
    [(55,1.0),(110,0.6),(165,0.2),(220,0.25),(990,0.03),(1100,0.01)],
    [(55,1.0),(110,0.65),(165,0.15),(220,0.3),(660,0.03),(1320,0.02)],
    [(55,1.0),(110,0.6),(165,0.2),(220,0.25),(770,0.02),(1100,0.03)],
    [(55,1.0),(110,0.55),(165,0.25),(220,0.2),(880,0.03),(990,0.02)],
    [(55,1.0),(110,0.5),(165,0.3),(220,0.15),(1100,0.02),(1320,0.02)],
    [(55,1.0),(110,0.45),(165,0.35),(220,0.1),(660,0.02),(880,0.03)],
]
bufs = []
for s in specs:
    bufs.append(make_buf(s))
    gc.collect()

print("Playing...")
while True:
    for b in bufs:
        for _ in range(220):
            audio_out.write(b)
