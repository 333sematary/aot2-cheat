"""Render a source-faithful UI preview from the launcher layout constants."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = Path(__file__).parent / "demo.gif"
W, H = 640, 440
SCALE = 2
BG, PANEL, BORDER = (10, 11, 13), (16, 17, 19), (42, 44, 47)
TEXT, MUTED, WHITE = (220, 221, 223), (135, 137, 141), (245, 246, 248)
FONT_DIR = Path("C:/Windows/Fonts")
ICON_FILES = ["house.png", "user-round.png", "globe.png", "sliders-horizontal.png", "settings.png"]
CATEGORIES = ["Main", "Player Data", "World", "Tools", "Social"]


def font(size, bold=False):
    return ImageFont.truetype(str(FONT_DIR / ("segoeuib.ttf" if bold else "segoeui.ttf")), size * SCALE)


def box(coords):
    return tuple(round(v * SCALE) for v in coords)


def rounded(d, coords, radius, fill, outline=None, width=1):
    d.rounded_rectangle(box(coords), radius * SCALE, fill=fill, outline=outline, width=width * SCALE)


def text(d, x, y, value, size=14, color=TEXT, bold=False, anchor=None):
    d.text((round(x * SCALE), round(y * SCALE)), value, font=font(size, bold), fill=color, anchor=anchor)


def checkbox(d, x, y, checked=False):
    rounded(d, (x, y, x + 21, y + 21), 8, WHITE if checked else (35, 36, 39), WHITE if checked else (75, 77, 81))
    if checked:
        # A dark tick keeps the enabled white control legible in the promo preview.
        d.line([((x + 5) * SCALE, (y + 10) * SCALE), ((x + 9) * SCALE, (y + 14) * SCALE),
                ((x + 16) * SCALE, (y + 7) * SCALE)], fill=(31, 33, 36), width=2 * SCALE, joint="curve")


def panel(d, rect, title, rows, checked_names=()):
    x, y, r, b = rect
    rounded(d, rect, 18, PANEL, BORDER)
    text(d, x + 15, y + 12, title, 14, TEXT, True)
    d.line([((x + 1) * SCALE, (y + 38) * SCALE), ((r - 1) * SCALE, (y + 38) * SCALE)], fill=WHITE, width=SCALE)
    for i, row in enumerate(rows):
        row_y = y + 42 + i * 27
        if row_y + 24 > b - 8:
            break
        text(d, x + 15, row_y + 2, row, 12, TEXT, True)
        checkbox(d, r - 15 - 21, row_y + 2, row in checked_names)


def draw_scene(active, script_enabled):
    im = Image.new("RGB", (W * SCALE, H * SCALE), BG)
    d = ImageDraw.Draw(im)
    title = CATEGORIES[active]
    text(d, W // 2, 17, title, 15, TEXT, True, anchor="mt")
    # Frameless caption controls and separator match src/main.cpp.
    d.line([(0, 53 * SCALE), (W * SCALE, 53 * SCALE)], fill=BORDER, width=SCALE)
    d.line([(W - 58 * SCALE, 26 * SCALE), (W - 48 * SCALE, 26 * SCALE)], fill=MUTED, width=2 * SCALE)
    d.line([((W - 34) * SCALE, 21 * SCALE), ((W - 22) * SCALE, 33 * SCALE)], fill=MUTED, width=2 * SCALE)
    d.line([((W - 34) * SCALE, 33 * SCALE), ((W - 22) * SCALE, 21 * SCALE)], fill=MUTED, width=2 * SCALE)

    content_x, top, content_w, panel_h, gap = 20, 64, 600, 280, 12
    panel_w = (content_w - gap) // 2
    left = (content_x, top, content_x + panel_w, top + panel_h)
    right = (content_x + panel_w + gap, top, content_x + content_w, top + panel_h)
    if active == 0 and not script_enabled:
        panel(d, (content_x, top, content_x + content_w, top + panel_h), "Main", ["Personal Script"], {"Personal Script"} if script_enabled else ())
    else:
        pages = {
            0: ("Main", "Resources",
                ["Personal Script", "Max Money", "Max City Materials"],
                ["Max Wing of Freedom Badges", "All Materials", "All Gifts"]),
            1: ("Current Character", "Change Values",
                ["Player Data", "Titan Timer", "Partners Skills No Cooldown", "Max Reinforce New Equipment (Buy)", "Max Awakening Ability Points (+140)", "Showdown Equipment Always Available", "Infinite Showdown Equipment (on rescue missions)"],
                ["Battle Alert Signal Infinite Duration", "Spawn Dire Elimination", "Auto End/Win Battle"]),
            2: ("World", "Supplies", ["Infinite Gas", "Infinite Blades"], ["Infinite Ammo", "Infinite Combat Items"]),
            3: ("Combat Tools", "Automation", ["Infinite Dash & Multi-Attacks", "Auto Hook Drive"], ["Auto Overall Score SS"]),
            4: ("Social", "Progress", ["Quick Friendship Gain"], ["Quick Camaraderie Points"]),
        }
        left_title, right_title, left_rows, right_rows = pages[active]
        panel(d, left, left_title, left_rows, {"Personal Script"} if active == 0 else ())
        panel(d, right, right_title, right_rows)

    visible_categories = 5 if script_enabled else 1
    nav_w, nav_h = 12 + 56 * visible_categories, 58
    nav_x, nav_y = (W - nav_w) // 2, H - nav_h - 30
    rounded(d, (nav_x, nav_y, nav_x + nav_w, nav_y + nav_h), 19, (17, 18, 20))
    icon_ids = range(visible_categories)
    for index in icon_ids:
        center_x = nav_x + 34 + index * 56
        selected = index == active
        icon = Image.open(ROOT / "assets" / "icons" / ICON_FILES[index]).convert("RGBA").resize((24 * SCALE, 24 * SCALE), Image.Resampling.LANCZOS)
        if not selected:
            alpha = icon.getchannel("A").point(lambda value: int(value * 0.58))
            icon.putalpha(alpha)
        im.paste(icon, (round((center_x - 12) * SCALE), round((nav_y + 17) * SCALE)), icon)
        if selected:
            d.ellipse(box((center_x - 3, nav_y + 49, center_x + 3, nav_y + 55)), fill=WHITE)

    return im.resize((W, H), Image.Resampling.LANCZOS)


# The first scene mirrors the actual startup view: only Personal Script and Home.
# Subsequent scenes show the enabled categories from the shipped C++ interface.
scenes = [(0, False), (0, True), (1, True), (2, True), (3, True), (4, True)]
frames = []
for scene in scenes:
    frame = draw_scene(*scene)
    for _ in range(5):
        frames.append(frame.copy())
frames[0].save(OUT, save_all=True, append_images=frames[1:], duration=180, loop=0, optimize=True)
print(f"Created {OUT} ({OUT.stat().st_size:,} bytes, {len(frames)} frames)")
