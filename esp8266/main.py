import machine
import ssd1306
import time
import math

# I2C setup - ESP32 default pins: SCL=GPIO22, SDA=GPIO21
i2c = machine.I2C(0, scl=machine.Pin(22), sda=machine.Pin(21))
oled = ssd1306.SSD1306_I2C(128, 64, i2c)

# Ball state
bx, by = 20.0, 30.0
bvx, bvy = 1.5, 1.0
br = 3

# Stars
import os
stars = []
for _ in range(15):
    sx = (int.from_bytes(os.urandom(1), 'big') % 128)
    sy = (int.from_bytes(os.urandom(1), 'big') % 48) + 16
    stars.append([sx, sy, int.from_bytes(os.urandom(1), 'big') % 3])

frame = 0
msg = "  Hello from MicroPython!  ESP8266 + SSD1306  "
scroll_x = 128

while True:
    oled.fill(0)

    # Twinkling stars
    for s in stars:
        s[2] = (s[2] + 1) % 6
        if s[2] < 4:
            oled.pixel(s[0], s[1], 1)

    # Bouncing ball
    bx += bvx
    by += bvy
    if bx <= br or bx >= 127 - br:
        bvx = -bvx
    if by <= 16 + br or by >= 63 - br:
        bvy = -bvy
    # Draw ball as filled circle
    for dy in range(-br, br + 1):
        for dx in range(-br, br + 1):
            if dx * dx + dy * dy <= br * br:
                px = int(bx) + dx
                py = int(by) + dy
                if 0 <= px < 128 and 0 <= py < 64:
                    oled.pixel(px, py, 1)

    # Sine wave across bottom area
    for x in range(0, 128, 2):
        y = int(50 + 8 * math.sin((x + frame * 3) * 0.05))
        if 0 <= y < 64:
            oled.pixel(x, y, 1)

    # Scrolling banner in yellow zone (top 16 rows)
    oled.text(msg, int(scroll_x), 4, 1)
    scroll_x -= 1.5
    if scroll_x < -(len(msg) * 8):
        scroll_x = 128

    # Thin separator line between yellow and blue zones
    oled.hline(0, 15, 128, 1)

    oled.show()
    frame += 1
    time.sleep_ms(30)
