# Original icon for Poe2 | MapStat: dark-fantasy gold ring, blood-red waypoint, hourglass.
from PIL import Image, ImageDraw, ImageFilter
import math, sys
S = 1024
img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
c = S // 2
# outer dark disc with subtle radial shading
for r in range(470, 0, -6):
    t = r / 470
    col = (int(18 + 22 * (1 - t)), int(14 + 12 * (1 - t)), int(20 + 18 * (1 - t)), 255)
    d.ellipse([c - r, c - r, c + r, c + r], fill=col)
# gold ornate ring
gold, gold_dark = (214, 168, 84, 255), (120, 86, 36, 255)
d.ellipse([c - 470, c - 470, c + 470, c + 470], outline=gold_dark, width=46)
d.ellipse([c - 455, c - 455, c + 455, c + 455], outline=gold, width=22)
d.ellipse([c - 395, c - 395, c + 395, c + 395], outline=gold_dark, width=10)
# runic ticks on the ring
for i in range(24):
    a = i * math.pi / 12
    r1, r2 = 405, (440 if i % 2 == 0 else 425)
    d.line([c + r1 * math.cos(a), c + r1 * math.sin(a), c + r2 * math.cos(a), c + r2 * math.sin(a)], fill=gold, width=10)
# blood-red waypoint (map pin) with glow
glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
g = ImageDraw.Draw(glow)
g.ellipse([c - 230, c - 300, c + 230, c + 160], fill=(200, 20, 30, 150))
glow = glow.filter(ImageFilter.GaussianBlur(60))
img.alpha_composite(glow)
d = ImageDraw.Draw(img)
red, red_dark = (196, 26, 36, 255), (110, 10, 18, 255)
pin_top = c - 290
d.ellipse([c - 190, pin_top, c + 190, pin_top + 380], fill=red_dark)
d.polygon([(c - 168, pin_top + 250), (c + 168, pin_top + 250), (c, c + 330)], fill=red_dark)
d.ellipse([c - 172, pin_top + 14, c + 172, pin_top + 358], fill=red)
d.polygon([(c - 150, pin_top + 250), (c + 150, pin_top + 250), (c, c + 300)], fill=red)
# hourglass inside the pin (gold)
hx, hy, hw, hh = c, pin_top + 186, 92, 120
d.rectangle([hx - hw - 14, hy - hh - 18, hx + hw + 14, hy - hh], fill=gold)
d.rectangle([hx - hw - 14, hy + hh, hx + hw + 14, hy + hh + 18], fill=gold)
d.polygon([(hx - hw, hy - hh), (hx + hw, hy - hh), (hx + 10, hy), (hx + hw, hy + hh), (hx - hw, hy + hh), (hx - 10, hy)],
          fill=(30, 10, 14, 255), outline=gold, width=10)
d.polygon([(hx - 58, hy - hh + 26), (hx + 58, hy - hh + 26), (hx, hy - 6)], fill=gold)   # sand top
d.polygon([(hx - 70, hy + hh - 4), (hx + 70, hy + hh - 4), (hx, hy + 52)], fill=gold)     # sand bottom
out = sys.argv[1]
sizes = [(256, 256), (128, 128), (64, 64), (48, 48), (32, 32), (24, 24), (16, 16)]
img.resize((256, 256), Image.LANCZOS).save(out, sizes=sizes)
img.resize((256, 256), Image.LANCZOS).save(out.replace(".ico", "_preview.png"))
