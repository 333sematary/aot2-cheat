from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

OUT = Path(__file__).parent / "demo.gif"
W, H = 760, 488
FONT_DIR = Path("C:/Windows/Fonts")


def font(size, bold=False):
    name = "segoeuib.ttf" if bold else "segoeui.ttf"
    return ImageFont.truetype(str(FONT_DIR / name), size)


def ease(t):
    return 1 - (1 - t) ** 3


def rounded(draw, box, radius, fill, outline=None, width=1):
    draw.rounded_rectangle(box, radius, fill=fill, outline=outline, width=width)


def draw_frame(active, selected, phase):
    im = Image.new("RGB", (W, H), (10, 11, 13))
    d = ImageDraw.Draw(im)
    # Minimal frameless app header, matching the native launcher.
    d.text((W // 2, 24), ["Main", "Player Data", "World", "Tools", "Social"][active],
           font=font(15, True), fill=(224, 225, 228), anchor="mm")
    d.line((0, 53, W, 53), fill=(39, 41, 44), width=1)
    x0, top, gap, pw, ph = 42, 76, 12, 332, 296
    left = ["Main", "Current Character", "Supplies", "Combat Tools", "Social"][active]
    right = ["Resources", "Change Values", "Equipment", "Automation", "Progress"][active]
    groups = [
        (["Personal Script", "Max Money", "Max City Materials", "Max Wing of Freedom Badges", "All Materials", "All Gifts"],
         ["Max Wing of Freedom Badges", "All Materials", "All Gifts"]),
        (["Player Data", "Current Character", "Name", "Level / Rank (-1)", "Experience", "Stats", "Strength", "Dexterity"],
         ["Equipment", "Blades Reinforcement", "Scabbards / Canisters", "ODM Gear Reinforcement"]),
        (["Infinite Gas", "Infinite Blades", "Infinite Ammo", "Infinite Combat Items"],
         ["Supplies", "Infinite Gas", "Infinite Blades"]),
        (["Infinite Dash & Multi-Attacks", "Auto Hook Drive", "Auto Overall Score SS"],
         ["Automation", "Quick Friendship Gain", "Quick Camaraderie Points"]),
        (["Quick Friendship Gain", "Quick Camaraderie Points"],
         ["Progress", "Max Money", "All Gifts"]),
    ]
    columns = groups[active]
    for col, (title, rows) in enumerate(((left, columns[0]), (right, columns[1]))):
        x = x0 + col * (pw + gap)
        rounded(d, (x, top, x + pw, top + ph), 18, (16, 17, 19), (43, 45, 48), 1)
        d.text((x + 17, top + 13), title, font=font(14, True), fill=(222, 223, 226))
        d.line((x + 1, top + 38, x + pw - 1, top + 38), fill=(74, 76, 80), width=1)
        for idx, text in enumerate(rows):
            y = top + 50 + idx * 30
            if y > top + ph - 23:
                break
            if selected == col * 6 + idx:
                pulse = 0.5 + 0.5 * __import__("math").sin(phase * 2 * __import__("math").pi)
                alpha = int(7 + 8 * pulse)
                rounded(d, (x + 10, y, x + pw - 8, y + 26), 9, (24 + alpha, 26 + alpha, 29 + alpha))
            if active == 1 and idx in (1, 5):
                d.text((x + 17, y + 3), text, font=font(12, True), fill=(176, 178, 182))
            else:
                d.text((x + 17, y + 3), text, font=font(12, True), fill=(218, 219, 222))
                rounded(d, (x + pw - 31, y + 1, x + pw - 10, y + 22), 7, (37, 39, 42), (77, 79, 83))
    # Centered category bar with a restrained white active marker.
    nav_w, nav_h = 292, 58
    nx, ny = (W - nav_w) // 2, H - 76
    rounded(d, (nx, ny, nx + nav_w, ny + nav_h), 19, (17, 18, 20))
    for i in range(5):
        cx = nx + 32 + i * 57
        color = (232, 234, 237) if i == active else (123, 126, 131)
        # Draw crisp, centered line icons directly; this avoids missing-glyph squares.
        ink = color
        if i == 0:  # house
            d.line([(cx-10,ny+23),(cx,ny+13),(cx+10,ny+23)],fill=ink,width=2)
            d.line([(cx-7,ny+21),(cx-7,ny+33),(cx+7,ny+33),(cx+7,ny+21)],fill=ink,width=2)
            d.rectangle((cx-2,ny+27,cx+2,ny+33),outline=ink,width=1)
        elif i == 1:  # user
            d.ellipse((cx-4,ny+13,cx+4,ny+21),outline=ink,width=2)
            d.arc((cx-10,ny+20,cx+10,ny+38),185,355,fill=ink,width=2)
        elif i == 2:  # globe
            d.ellipse((cx-10,ny+14,cx+10,ny+34),outline=ink,width=2)
            d.arc((cx-5,ny+14,cx+5,ny+34),75,285,fill=ink,width=1)
            d.arc((cx-5,ny+14,cx+5,ny+34),255,105,fill=ink,width=1)
            d.line((cx-9,ny+24,cx+9,ny+24),fill=ink,width=1)
        elif i == 3:  # sliders
            for yy, xx in ((ny+17,cx-4),(ny+24,cx+4),(ny+31,cx-1)):
                d.line((cx-10,yy,cx+10,yy),fill=ink,width=2)
                d.ellipse((xx-2,yy-3,xx+2,yy+3),fill=(17,18,20),outline=ink,width=1)
        else:  # settings
            d.ellipse((cx-8,ny+16,cx+8,ny+32),outline=ink,width=2)
            d.ellipse((cx-2,ny+22,cx+2,ny+26),outline=ink,width=2)
            for dx,dy in ((0,-11),(0,11),(-11,0),(11,0),(-8,-8),(8,-8),(-8,8),(8,8)):
                d.line((cx+dx//2,ny+24+dy//2,cx+dx,ny+24+dy),fill=ink,width=2)
        if i == active:
            d.ellipse((cx - 3, ny + 47, cx + 3, ny + 53), fill=(245, 246, 248))
    # Small caption badge.
    rounded(d, (W - 146, 18, W - 22, 42), 12, (23, 25, 28))
    d.ellipse((W - 133, 27, W - 125, 35), fill=(232, 234, 237))
    d.text((W - 117, 22), "OFFLINE UI PREVIEW", font=font(9, True), fill=(171, 174, 179))
    return im


frames = []
# Smoothly cycle through categories; the tiny hover pulse is just illustrative.
for active in range(5):
    frames.extend(draw_frame(active, active % 2, i / 12) for i in range(12))
frames[0].save(OUT, save_all=True, append_images=frames[1:], duration=85, loop=0, optimize=True)
print(f"Created {OUT} ({OUT.stat().st_size:,} bytes)")
