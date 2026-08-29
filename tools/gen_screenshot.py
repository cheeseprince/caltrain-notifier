#!/usr/bin/env python3
"""Render the board to a PNG for the README, from real data.

    g++ -std=c++17 -Isrc -Ithird_party tools/board_dump.cpp \
        src/board_model.cpp src/timetable.cpp src/route.cpp src/siri_parse.cpp \
        -o /tmp/board_dump
    TZ=America/Los_Angeles /tmp/board_dump "San Francisco" "San Jose Diridon" \
        test/fixtures/stopmonitoring_70012.json | python3 tools/gen_screenshot.py

WHAT THIS IS, AND WHAT IT IS NOT. The numbers come from tools/board_dump.cpp,
which links the SAME board_model/timetable/route/siri_parse the firmware does
and feeds it the committed 511 capture. So the trains, times, countdowns and
urgency colours on the image are the ones the device would show, not a
designer's idea of them.

The GEOMETRY is copied from src/render.cpp — every constant below is quoted from
it — and the COLOURS are its RGB565 literals converted to RGB. What is only an
approximation is the typeface: TFT_eSPI draws bitmap GLCD fonts that are not
distributable as a TTF, so DejaVu Sans is substituted at the same pixel heights.
Glyph shapes and letter spacing therefore differ slightly from the panel. Treat
this as an accurate diagram of a real board, not a photograph of one.

If render.cpp's layout changes, this file has to be changed with it. There is no
mechanism keeping them in step, which is the honest cost of rendering a device
screen on a host.
"""
import json
import os
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "docs", "images", "board-sf-to-diridon.png")
OUT_SPLASH = os.path.join(ROOT, "docs", "images", "splash.png")
OUT_LEGEND = os.path.join(ROOT, "docs", "images", "urgency-legend.png")
RENDER_CPP = os.path.join(ROOT, "src", "render.cpp")

# --- Geometry, quoted from src/render.cpp ------------------------------------
SCREEN_W, SCREEN_H = 480, 320
BORDER = 8
INNER_X, INNER_Y = BORDER, BORDER
INNER_W, INNER_H = SCREEN_W - 2 * BORDER, SCREEN_H - 2 * BORDER   # 464 x 304
HEADER_H = 52
BOARD_ROWS = 3
ROWS_Y = INNER_Y + HEADER_H                                        # 60
ROW_H = (INNER_H - HEADER_H) // BOARD_ROWS                         # 84
COL_MIN_R = INNER_X + 108
COL_INFO = INNER_X + 156
COL_RIGHT = INNER_X + INNER_W - 8

# --- Colours: render.cpp's RGB565 literals -----------------------------------
def rgb565(v):
    return (((v >> 11) & 0x1F) * 255 // 31,
            ((v >> 5) & 0x3F) * 255 // 63,
            (v & 0x1F) * 255 // 31)

COL_BG      = rgb565(0x0000)
COL_TEXT    = rgb565(0xFFFF)
COL_DIM     = rgb565(0xC618)
COL_RULE    = rgb565(0x2965)
COL_GREEN   = rgb565(0x0640)
COL_YELLOW  = rgb565(0xFE60)
COL_RED     = rgb565(0xF800)
COL_LATE    = rgb565(0xFD20)
COL_SCHED   = rgb565(0x05FF)
URGENCY = {0: COL_GREEN, 1: COL_YELLOW, 2: COL_RED}

# TFT_eSPI font heights: font 2 = 16 px, font 4 = 26 px, font 6 = 48 px (digits).
FONT_DIR = "/usr/share/fonts/truetype/dejavu"
F_SMALL = ImageFont.truetype(os.path.join(FONT_DIR, "DejaVuSans.ttf"), 15)
F_MED   = ImageFont.truetype(os.path.join(FONT_DIR, "DejaVuSans.ttf"), 24)
F_BIG   = ImageFont.truetype(os.path.join(FONT_DIR, "DejaVuSans-Bold.ttf"), 46)


def text(d, xy, s, font, fill, datum="TL"):
    """Draw with TFT_eSPI's datum semantics: T/M/B x L/C/R."""
    if not s:
        return
    l, t, r, b = d.textbbox((0, 0), s, font=font)
    w, h = r - l, b - t
    x, y = xy
    if datum[1] == "R": x -= w
    elif datum[1] == "C": x -= w // 2
    if datum[0] == "M": y -= h // 2
    elif datum[0] == "B": y -= h
    d.text((x - l, y - t), s, font=font, fill=fill)


def render(model):
    img = Image.new("RGB", (SCREEN_W, SCREEN_H), COL_BG)
    d = ImageDraw.Draw(img)

    # Border takes its colour from the soonest train — the one the glance is about.
    frame = URGENCY[model["rows"][0]["urgency"]] if model["rows"] else rgb565(0x8410)
    for i in range(BORDER):
        d.rectangle([i, i, SCREEN_W - 1 - i, SCREEN_H - 1 - i], outline=frame)

    # The clock is laid out first because it is fixed-width and reserves the
    # right-hand space the route name then has to fit inside. The width used is
    # that of the WIDEST clock, "88:88", not the current one.
    text(d, (COL_RIGHT, INNER_Y + 6), model["clock"], F_MED, COL_TEXT, "TR")
    clock_w = d.textlength("88:88", font=F_MED)
    route_max_w = INNER_W - clock_w - 24

    # fontThatFits(): the route drops to the small font rather than colliding
    # with the clock. "San Francisco > San Jose Diridon" is long enough to
    # trigger exactly that, so a render without this step draws a header the
    # device would never show.
    header = f'{model["origin"]}  >  {model["destination"]}'
    fits = d.textlength(header, font=F_MED) <= route_max_w
    f = F_MED if fits else F_SMALL
    text(d, (INNER_X + 4, INNER_Y + (6 if fits else 11)), header, f, COL_TEXT, "TL")

    if not model["anyLive"]:
        text(d, (INNER_X + 4, INNER_Y + 34), "SCHEDULED TIMES - no live data",
             F_SMALL, COL_SCHED, "TL")

    for i in range(1, BOARD_ROWS):
        y = ROWS_Y + i * ROW_H
        d.line([(INNER_X + 4, y), (INNER_X + INNER_W - 4, y)], fill=COL_RULE)

    for i, r in enumerate(model["rows"][:BOARD_ROWS]):
        y = ROWS_Y + i * ROW_H
        text(d, (COL_MIN_R, y + ROW_H // 2 - 2), str(r["minutesAway"]),
             F_BIG, URGENCY[r["urgency"]], "MR")
        text(d, (COL_MIN_R + 6, y + ROW_H // 2 + 4), "min", F_SMALL, COL_DIM, "TL")
        text(d, (COL_INFO, y + 28), r["when"], F_MED, COL_TEXT, "ML")

        if not r["isLive"]:
            status, colour = "SCHED", COL_SCHED
        elif r["delaySec"] >= 60:
            status, colour = f'+{r["delaySec"] // 60} late', COL_LATE
        elif r["delaySec"] <= -60:
            status, colour = f'{-r["delaySec"] // 60} early', COL_DIM
        else:
            status, colour = "on time", COL_DIM
        text(d, (COL_RIGHT, y + 28), status, F_MED, colour, "MR")
        text(d, (COL_INFO, y + 60), f'#{r["number"]} {r["route"]}', F_SMALL, COL_DIM, "ML")

    return img


SPLASH_TITLE_Y = 74
SPLASH_ATTR_Y = 126
SPLASH_ATTR_DY = 20
SPLASH_NOTE_Y = 282
COL_FRAME_IDLE = rgb565(0x8410)


def splash_lines():
    """Read the attribution strings out of render.cpp rather than restating them.

    Two copies of a legal notice drift, and the copy in the picture is the one
    people would quote. This parses the actual array the firmware draws.
    """
    src = open(RENDER_CPP, encoding="utf-8").read()
    start = src.index("kSplashAttribution[] = {")
    body = src[start:src.index("};", start)]
    out = []
    for line in body.splitlines()[1:]:
        line = line.strip()
        if line.startswith('"'):
            out.append(line[1:line.rindex('"')].replace('\\"', '"'))
    return out


def render_splash(detail="connecting to WiFi..."):
    img = Image.new("RGB", (SCREEN_W, SCREEN_H), COL_BG)
    d = ImageDraw.Draw(img)
    for i in range(BORDER):
        d.rectangle([i, i, SCREEN_W - 1 - i, SCREEN_H - 1 - i], outline=COL_FRAME_IDLE)
    text(d, (SCREEN_W // 2, SPLASH_TITLE_Y), "Caltrain Notifier", F_MED, COL_TEXT, "MC")
    for i, line in enumerate(splash_lines()):
        if not line:
            continue
        text(d, (SCREEN_W // 2, SPLASH_ATTR_Y + i * SPLASH_ATTR_DY), line,
             F_SMALL, COL_DIM, "MC")
        w = d.textlength(line, font=F_SMALL)
        if w > INNER_W - 8:
            print(f"  WARNING: line overruns the panel by {w - (INNER_W - 8):.0f}px: {line}",
                  file=sys.stderr)
    text(d, (SCREEN_W // 2, SPLASH_NOTE_Y), detail, F_SMALL, COL_DIM, "MC")
    return img


def parse_colour(name):
    """Read an RGB565 constant out of render.cpp by name.

    The legend has to show the colours the firmware actually draws. Typing the
    hex into the README by hand creates a second source of truth that nobody
    notices has drifted, because a slightly wrong green still looks like green.
    """
    src = open(RENDER_CPP, encoding="utf-8").read()
    i = src.index(f"constexpr uint16_t {name}")
    val = src[i:src.index(";", i)].split("=")[1].strip()
    return rgb565(int(val, 16))


def render_legend():
    """A swatch per urgency band, in the border colours themselves."""
    # The number in each swatch is a representative countdown, drawn in the
    # band's own colour and at the board's countdown size -- so the legend reads
    # as three miniature rows of the real display rather than as abstract chips.
    rows = [
        ("22", "more than 15 min", "COL_GREEN",  "plenty of time"),
        ("12", "10 to 15 min",     "COL_YELLOW", "start moving"),
        ("4",  "under 10 min",     "COL_RED",    "go now"),
    ]
    pad, sw_w, row_h = 16, 116, 64
    w, h = 430, pad * 2 + row_h * len(rows)
    img = Image.new("RGB", (w, h), COL_BG)
    d = ImageDraw.Draw(img)
    for i, (mins, band, cname, note) in enumerate(rows):
        c = parse_colour(cname)
        y = pad + i * row_h
        # A thick frame, drawn the way the panel draws it: the border IS the
        # signal, so the swatch is a border rather than a filled block.
        for k in range(BORDER):
            d.rectangle([pad + k, y + k, pad + sw_w - 1 - k, y + row_h - 12 - k], outline=c)
        text(d, (pad + sw_w // 2 - 16, y + (row_h - 12) // 2), mins, F_BIG, c, "MC")
        text(d, (pad + sw_w // 2 + 20, y + (row_h - 12) // 2 + 6), "min", F_SMALL, COL_DIM, "ML")
        text(d, (pad + sw_w + 24, y + (row_h - 12) // 2 - 10), band, F_MED, COL_TEXT, "ML")
        text(d, (pad + sw_w + 24, y + (row_h - 12) // 2 + 14), note, F_SMALL, COL_DIM, "ML")
    return img


def main():
    if "--legend" in sys.argv:
        os.makedirs(os.path.dirname(OUT_LEGEND), exist_ok=True)
        img = render_legend()
        img.save(OUT_LEGEND)
        img.resize((img.width * 2, img.height * 2), Image.NEAREST).save(
            OUT_LEGEND.replace(".png", "@2x.png"))
        print(f"wrote {OUT_LEGEND} and its @2x")
        return 0
    if "--splash" in sys.argv:
        os.makedirs(os.path.dirname(OUT_SPLASH), exist_ok=True)
        img = render_splash()
        img.save(OUT_SPLASH)
        img.resize((SCREEN_W * 2, SCREEN_H * 2), Image.NEAREST).save(
            OUT_SPLASH.replace(".png", "@2x.png"))
        print(f"wrote {OUT_SPLASH} and its @2x")
        return 0
    model = json.load(sys.stdin)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    img = render(model)
    img.save(OUT)
    # 2x for legibility on a high-density display, nearest-neighbour so the
    # pixel grid of the panel stays visible rather than being smoothed away.
    img.resize((SCREEN_W * 2, SCREEN_H * 2), Image.NEAREST).save(
        OUT.replace(".png", "@2x.png"))
    print(f"wrote {OUT} and its @2x")
    return 0


if __name__ == "__main__":
    sys.exit(main())
