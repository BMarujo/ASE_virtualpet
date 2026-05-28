from pathlib import Path


ROOT = Path(__file__).resolve().parent
ASSET_DIR = ROOT / "main" / "assets"

BLACK = 0x0000
WHITE = 0xFFFF
OUTLINE = 0x18E3
FUR = 0xF5A5
FUR_DARK = 0xCC64
FUR_SLEEP = 0x8C51
PINK = 0xF9B6
RED = 0xF800
YELLOW = 0xFFE0
FOOD = 0xFD20
FOOD_DARK = 0xB420
BOWL = 0x7BEF
BOWL_DARK = 0x39E7
WATER = 0x04BF
WATER_DARK = 0x0279
Z_GRAY = 0xBDF7

CAT_W = 32
CAT_H = 24
BOWL_W = 40
BOWL_H = 18


def new_canvas(width, height, color=BLACK):
    return [[color for _ in range(width)] for _ in range(height)]


def set_px(canvas, x, y, color):
    if 0 <= y < len(canvas) and 0 <= x < len(canvas[0]):
        canvas[y][x] = color


def fill_rect(canvas, x, y, w, h, color):
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            set_px(canvas, xx, yy, color)


def fill_ellipse(canvas, cx, cy, rx, ry, color):
    for y in range(cy - ry, cy + ry + 1):
        for x in range(cx - rx, cx + rx + 1):
            if ((x - cx) * (x - cx) * ry * ry + (y - cy) * (y - cy) * rx * rx) <= rx * rx * ry * ry:
                set_px(canvas, x, y, color)


def draw_line(canvas, x0, y0, x1, y1, color):
    dx = abs(x1 - x0)
    dy = -abs(y1 - y0)
    sx = 1 if x0 < x1 else -1
    sy = 1 if y0 < y1 else -1
    err = dx + dy
    while True:
        set_px(canvas, x0, y0, color)
        if x0 == x1 and y0 == y1:
            break
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy


def draw_z(canvas, x, y):
    draw_line(canvas, x, y, x + 5, y, Z_GRAY)
    draw_line(canvas, x + 5, y, x, y + 5, Z_GRAY)
    draw_line(canvas, x, y + 5, x + 5, y + 5, Z_GRAY)


def draw_cat_face(canvas, expression):
    eye_y = 10
    if expression == "sleep":
        draw_line(canvas, 10, eye_y, 13, eye_y + 1, OUTLINE)
        draw_line(canvas, 20, eye_y + 1, 23, eye_y, OUTLINE)
    elif expression == "tired":
        draw_line(canvas, 10, eye_y, 13, eye_y, OUTLINE)
        draw_line(canvas, 20, eye_y, 23, eye_y, OUTLINE)
    elif expression == "thirsty":
        fill_rect(canvas, 10, eye_y - 1, 3, 3, WHITE)
        fill_rect(canvas, 20, eye_y - 1, 3, 3, WHITE)
        set_px(canvas, 11, eye_y, OUTLINE)
        set_px(canvas, 21, eye_y, OUTLINE)
        set_px(canvas, 24, 12, WATER)
        set_px(canvas, 24, 13, WATER_DARK)
    else:
        fill_rect(canvas, 10, eye_y - 1, 3, 3, WHITE)
        fill_rect(canvas, 20, eye_y - 1, 3, 3, WHITE)
        set_px(canvas, 11, eye_y, OUTLINE)
        set_px(canvas, 21, eye_y, OUTLINE)

    set_px(canvas, 16, 13, PINK)
    set_px(canvas, 15, 14, OUTLINE)
    set_px(canvas, 17, 14, OUTLINE)

    if expression == "happy":
        draw_line(canvas, 13, 15, 15, 17, OUTLINE)
        draw_line(canvas, 17, 17, 19, 15, OUTLINE)
        set_px(canvas, 16, 17, RED)
    elif expression == "hungry":
        draw_line(canvas, 13, 17, 16, 15, OUTLINE)
        draw_line(canvas, 16, 15, 19, 17, OUTLINE)
    elif expression == "tired":
        draw_line(canvas, 14, 17, 18, 17, OUTLINE)
    elif expression == "thirsty":
        draw_line(canvas, 14, 16, 18, 16, OUTLINE)
    elif expression == "sleep":
        set_px(canvas, 16, 16, OUTLINE)
        set_px(canvas, 17, 16, OUTLINE)
    else:
        draw_line(canvas, 14, 15, 16, 16, OUTLINE)
        draw_line(canvas, 16, 16, 18, 15, OUTLINE)

    for wx in (7, 8, 24, 25):
        draw_line(canvas, wx, 13, wx - 4 if wx < 16 else wx + 4, 11, OUTLINE)
        draw_line(canvas, wx, 15, wx - 4 if wx < 16 else wx + 4, 15, OUTLINE)


def make_cat(expression):
    canvas = new_canvas(CAT_W, CAT_H)
    fur = FUR_SLEEP if expression == "sleep" else FUR

    fill_ellipse(canvas, 16, 13, 11, 8, OUTLINE)
    fill_ellipse(canvas, 16, 13, 10, 7, fur)

    for dy in range(5):
        for dx in range(dy + 1):
            set_px(canvas, 7 + dx, 6 - dy, OUTLINE)
            set_px(canvas, 24 - dx, 6 - dy, OUTLINE)
            if dx < dy:
                set_px(canvas, 8 + dx, 6 - dy, fur)
                set_px(canvas, 23 - dx, 6 - dy, fur)

    fill_ellipse(canvas, 16, 20, 10, 4, OUTLINE)
    fill_ellipse(canvas, 16, 20, 9, 3, fur)
    fill_rect(canvas, 8, 21, 5, 3, fur)
    fill_rect(canvas, 20, 21, 5, 3, fur)
    fill_rect(canvas, 8, 23, 5, 1, OUTLINE)
    fill_rect(canvas, 20, 23, 5, 1, OUTLINE)

    draw_line(canvas, 5, 16, 1, 15, OUTLINE)
    draw_line(canvas, 1, 15, 2, 11, OUTLINE)
    draw_line(canvas, 2, 11, 6, 12, OUTLINE)
    draw_line(canvas, 5, 15, 2, 14, fur)

    draw_cat_face(canvas, expression)
    if expression == "sleep":
        draw_z(canvas, 23, 1)
        draw_z(canvas, 27, 7)
    return canvas


def make_roll_cat():
    canvas = new_canvas(CAT_W, CAT_H)
    fill_ellipse(canvas, 16, 13, 10, 9, OUTLINE)
    fill_ellipse(canvas, 16, 13, 9, 8, FUR_DARK)
    fill_rect(canvas, 9, 5, 4, 4, OUTLINE)
    fill_rect(canvas, 20, 5, 4, 4, OUTLINE)
    fill_rect(canvas, 10, 6, 2, 2, FUR_DARK)
    fill_rect(canvas, 21, 6, 2, 2, FUR_DARK)
    fill_rect(canvas, 11, 12, 3, 2, WHITE)
    fill_rect(canvas, 19, 12, 3, 2, WHITE)
    set_px(canvas, 12, 13, OUTLINE)
    set_px(canvas, 20, 13, OUTLINE)
    set_px(canvas, 16, 15, PINK)
    draw_line(canvas, 12, 17, 20, 17, OUTLINE)
    draw_line(canvas, 6, 19, 3, 21, OUTLINE)
    draw_line(canvas, 26, 19, 29, 21, OUTLINE)
    return canvas


def flip_vertical(canvas):
    return [list(row) for row in reversed(canvas)]


def flip_horizontal(canvas):
    return [list(reversed(row)) for row in canvas]


def make_bowl(percent):
    canvas = new_canvas(BOWL_W, BOWL_H)
    fill_rect(canvas, 9, 2, 22, 2, BOWL)
    fill_rect(canvas, 6, 4, 28, 2, BOWL)

    for y in range(6, 15):
        left = 4 + (y - 6) // 2
        right = BOWL_W - left - 1
        for x in range(left, right + 1):
            color = BOWL_DARK
            if x == left or x == right or y == 14:
                color = BOWL
            canvas[y][x] = color

    fill_rows = round((percent / 100.0) * 7)
    for y in range(13, 13 - fill_rows, -1):
        left = 7 + max(0, (y - 9) // 2)
        right = BOWL_W - left - 1
        for x in range(left, right + 1):
            canvas[y][x] = FOOD if (x + y) % 3 else FOOD_DARK

    fill_rect(canvas, 11, 3, 18, 1, WHITE)
    return canvas


def c_array(name, canvas):
    flat = [value for row in canvas for value in row]
    lines = [f"const uint16_t {name}[{len(canvas[0])} * {len(canvas)}] = {{"]
    for i in range(0, len(flat), 12):
        chunk = ", ".join(f"0x{value:04X}" for value in flat[i:i + 12])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("")
    return "\n".join(lines)


def write_assets():
    ASSET_DIR.mkdir(parents=True, exist_ok=True)

    cat_idle = make_cat("idle")
    cat_happy = make_cat("happy")
    cat_hungry = make_cat("hungry")
    cat_thirsty = make_cat("thirsty")
    cat_tired = make_cat("tired")
    cat_sleep = make_cat("sleep")
    roll = make_roll_cat()

    cat_assets = {
        "cat_idle": cat_idle,
        "cat_happy": cat_happy,
        "cat_hungry": cat_hungry,
        "cat_thirsty": cat_thirsty,
        "cat_tired": cat_tired,
        "cat_sleep": cat_sleep,
        "cat_flip_0": cat_happy,
        "cat_flip_1": roll,
        "cat_flip_2": flip_vertical(cat_happy),
        "cat_flip_3": flip_horizontal(roll),
    }
    bowl_assets = {
        "food_bowl_0": make_bowl(0),
        "food_bowl_25": make_bowl(25),
        "food_bowl_50": make_bowl(50),
        "food_bowl_75": make_bowl(75),
        "food_bowl_100": make_bowl(100),
    }

    header = f"""#pragma once
#include <stdint.h>

#define CAT_SPRITE_WIDTH {CAT_W}
#define CAT_SPRITE_HEIGHT {CAT_H}
#define PET_WIDTH CAT_SPRITE_WIDTH
#define PET_HEIGHT CAT_SPRITE_HEIGHT

#define BOWL_SPRITE_WIDTH {BOWL_W}
#define BOWL_SPRITE_HEIGHT {BOWL_H}
#define CAT_FLIP_FRAME_COUNT 4
#define BOWL_FRAME_COUNT 5

extern const uint16_t cat_idle[CAT_SPRITE_WIDTH * CAT_SPRITE_HEIGHT];
extern const uint16_t cat_happy[CAT_SPRITE_WIDTH * CAT_SPRITE_HEIGHT];
extern const uint16_t cat_hungry[CAT_SPRITE_WIDTH * CAT_SPRITE_HEIGHT];
extern const uint16_t cat_thirsty[CAT_SPRITE_WIDTH * CAT_SPRITE_HEIGHT];
extern const uint16_t cat_tired[CAT_SPRITE_WIDTH * CAT_SPRITE_HEIGHT];
extern const uint16_t cat_sleep[CAT_SPRITE_WIDTH * CAT_SPRITE_HEIGHT];

extern const uint16_t cat_flip_0[CAT_SPRITE_WIDTH * CAT_SPRITE_HEIGHT];
extern const uint16_t cat_flip_1[CAT_SPRITE_WIDTH * CAT_SPRITE_HEIGHT];
extern const uint16_t cat_flip_2[CAT_SPRITE_WIDTH * CAT_SPRITE_HEIGHT];
extern const uint16_t cat_flip_3[CAT_SPRITE_WIDTH * CAT_SPRITE_HEIGHT];
extern const uint16_t *const cat_flip_frames[CAT_FLIP_FRAME_COUNT];

extern const uint16_t food_bowl_0[BOWL_SPRITE_WIDTH * BOWL_SPRITE_HEIGHT];
extern const uint16_t food_bowl_25[BOWL_SPRITE_WIDTH * BOWL_SPRITE_HEIGHT];
extern const uint16_t food_bowl_50[BOWL_SPRITE_WIDTH * BOWL_SPRITE_HEIGHT];
extern const uint16_t food_bowl_75[BOWL_SPRITE_WIDTH * BOWL_SPRITE_HEIGHT];
extern const uint16_t food_bowl_100[BOWL_SPRITE_WIDTH * BOWL_SPRITE_HEIGHT];
extern const uint16_t *const food_bowl_frames[BOWL_FRAME_COUNT];
"""

    source_lines = ['#include "pet_assets.h"', ""]
    for name, canvas in cat_assets.items():
        source_lines.append(c_array(name, canvas))
    source_lines.append("const uint16_t *const cat_flip_frames[CAT_FLIP_FRAME_COUNT] = {")
    source_lines.append("    cat_flip_0, cat_flip_1, cat_flip_2, cat_flip_3,")
    source_lines.append("};")
    source_lines.append("")
    for name, canvas in bowl_assets.items():
        source_lines.append(c_array(name, canvas))
    source_lines.append("const uint16_t *const food_bowl_frames[BOWL_FRAME_COUNT] = {")
    source_lines.append("    food_bowl_0, food_bowl_25, food_bowl_50, food_bowl_75, food_bowl_100,")
    source_lines.append("};")
    source_lines.append("")

    (ASSET_DIR / "pet_assets.h").write_text(header)
    (ASSET_DIR / "pet_assets.c").write_text("\n".join(source_lines))


if __name__ == "__main__":
    write_assets()
    print(f"Generated assets in {ASSET_DIR}")
