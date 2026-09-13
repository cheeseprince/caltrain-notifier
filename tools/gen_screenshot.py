#!/usr/bin/env python3
"""Render the sign's screens to PNGs — board, splash, urgency legend — from real data.

    g++ -std=c++17 -Isrc -Ithird_party tools/board_dump.cpp \
        src/board_model.cpp src/timetable.cpp src/route.cpp src/siri_parse.cpp \
        -o /tmp/board_dump
    TZ=America/Los_Angeles /tmp/board_dump "San Francisco" "San Jose Diridon" \
        test/fixtures/stopmonitoring_70012.json | python3 tools/gen_screenshot.py

The 2.8" QDtech ES3C28P layout renders as a preview only, into a directory of
your choosing rather than docs/images/:

    ... | python3 tools/gen_screenshot.py --board es3c28p --out /some/dir
    python3 tools/gen_screenshot.py --splash --board es3c28p --out /some/dir

WHAT THIS IS, AND WHAT IT IS NOT. The numbers come from tools/board_dump.cpp,
which links the SAME board_model/timetable/route/siri_parse the firmware does
and feeds it the committed 511 capture. So the trains, times, countdowns and
urgency colours on the image are the ones the device would show, not a
designer's idea of them.

PIXEL-ACCURATE WHEN IT CAN BE. When a PlatformIO build has fetched TFT_eSPI into
.pio/libdeps, text is drawn with TFT_eSPI's own glyphs: the width tables, the
font 2 bitmaps and the run-length-encoded fonts 4 and 6 are read straight out
of its Fonts/*.c sources and decoded the way TFT_eSPI::drawChar() decodes them,
and text is positioned with drawString()'s datum arithmetic. The image is then
the panel's pixels, not an impression of them. Without those files (a fresh
clone with no build yet) it falls back to DejaVu Sans at the same pixel
heights and says so. That fallback is 12-40% wider than the real fonts, so
tight layouts look more crowded in it than they are on the panel.

The GEOMETRY is copied from src/layout.h — every value in BOARDS below is quoted
from it — and the COLOURS are render.cpp's RGB565 literals converted to RGB. If
layout.h or render.cpp's layout changes, this file has to be changed with it.
There is no mechanism keeping them in step, which is the honest cost of
rendering a device screen on a host.
"""
import glob
import json
import os
import re
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_LEGEND = os.path.join(ROOT, "docs", "images", "urgency-legend.png")
RENDER_CPP = os.path.join(ROOT, "src", "render.cpp")
LAYOUT_H = os.path.join(ROOT, "src", "layout.h")
STATION_LABEL_H = os.path.join(ROOT, "src", "station_label.h")

BOARD_ROWS = 3

# --- Geometry, quoted from src/layout.h, one entry per board ------------------
# Fonts are TFT_eSPI font numbers: 2 (16 px), 4 (26 px), 6 (48 px, digits only).
BOARDS = {
    # Elecrow CrowPanel 3.5" — layout.h's default (#else) block.
    "crowpanel35": dict(
        SCREEN_W=480, SCREEN_H=320, BORDER=8, HEADER_H=52,
        CLOCK_FONT=4, CLOCK_DY=6, COL_RIGHT_INSET=8, ROUTE_CLOCK_GAP=24,
        ROUTE_FONT_BIG=4, ROUTE_BIG_DY=6, ROUTE_SMALL_DY=11,
        ROUTE_WRAP=False, ROUTE_LINE1_DY=0, ROUTE_LINE2_DY=0, NOTE_DY=34,
        SHORT_STATION_NAMES=False,
        COUNT_FONT=6, COUNT_R=108, COUNT_DY=40, MIN_LABEL_DX=6, MIN_LABEL_DY=46,
        COL_INFO_X=156, WHEN_DY=28, ROW_STATUS_FONT=4, INFO_DY=60,
        SPLASH_TITLE_Y=74, SPLASH_ATTR_Y=126, SPLASH_ATTR_DY=20, SPLASH_NOTE_Y=282,
        SPLASH_ARRAY="kSplashAttributionWide",
        OUT=os.path.join(ROOT, "docs", "images", "board-sf-to-diridon.png"),
        OUT_SPLASH=os.path.join(ROOT, "docs", "images", "splash.png"),
    ),
    # QDtech ES3C28P 2.8" — layout.h's BOARD_ES3C28P block. Preview only.
    "es3c28p": dict(
        SCREEN_W=320, SCREEN_H=240, BORDER=6, HEADER_H=52,
        CLOCK_FONT=2, CLOCK_DY=9, COL_RIGHT_INSET=6, ROUTE_CLOCK_GAP=16,
        ROUTE_FONT_BIG=2, ROUTE_BIG_DY=9, ROUTE_SMALL_DY=9,
        ROUTE_WRAP=True, ROUTE_LINE1_DY=2, ROUTE_LINE2_DY=18, NOTE_DY=34,
        SHORT_STATION_NAMES=True,
        COUNT_FONT=4, COUNT_R=46, COUNT_DY=17, MIN_LABEL_DX=4, MIN_LABEL_DY=13,
        COL_INFO_X=82, WHEN_DY=17, ROW_STATUS_FONT=2, INFO_DY=43,
        SPLASH_TITLE_Y=26, SPLASH_ATTR_Y=50, SPLASH_ATTR_DY=17, SPLASH_NOTE_Y=220,
        SPLASH_ARRAY="kSplashAttributionNarrow",
        OUT=None, OUT_SPLASH=None,
    ),
}

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
COL_FRAME_IDLE = rgb565(0x8410)
URGENCY = {0: COL_GREEN, 1: COL_YELLOW, 2: COL_RED}


# --- TFT_eSPI's own fonts ------------------------------------------------------
def _active_lines(text, defined=()):
    """The lines of a C snippet that survive #ifdef / #ifndef / #else / #endif.

    TFT_eSPI's Font16.c puts an `#ifdef TFT_ESPI_GRAVE_IS_DEGREE` block INSIDE
    its width table. Reading both branches yields 104 entries instead of 96 and
    shifts every glyph from 'h' on — which silently corrupted this script's
    measurements once. This firmware does not define that symbol, so the #else
    branch is the one actually compiled.
    """
    stack = []
    for line in text.splitlines():
        s = line.strip()
        if s.startswith(("#ifdef", "#ifndef")):
            stack.append((s.split()[1] in defined) == s.startswith("#ifdef"))
        elif s.startswith("#else"):
            stack[-1] = not stack[-1]
        elif s.startswith("#endif"):
            stack.pop()
        elif s.startswith("#"):
            raise ValueError(f"unhandled preprocessor line in TFT_eSPI font data: {s}")
        elif all(stack):
            yield line


def _c_body(src, name):
    """Text between the braces of the C array definition `name[...] = { ... }`.

    TFT_eSPI's font files put a comment between the "=" and the "{" (and the
    brace on the next line), so the definition is found first and the body
    starts at the next brace after it.
    """
    m = re.search(rf"\b{re.escape(name)}\s*\[[^\]]*\]\s*=", src)
    if not m:
        raise ValueError(f"array {name} not found")
    start = src.index("{", m.end()) + 1
    return src[start:src.index("}", start)]


def _c_numbers(src, name):
    """The integer literals (decimal or hex) of a C array, preprocessor-aware."""
    body = "\n".join(_active_lines(_c_body(src, name)))
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    body = re.sub(r"//[^\n]*", "", body)
    return [int(tok, 0) for tok in re.findall(r"0[xX][0-9a-fA-F]+|\d+", body)]


class TftFonts:
    """TFT_eSPI's built-in fonts 2, 4 and 6, decoded from its Fonts/*.c sources."""

    FILES = {2: ("Font16", "f16"), 4: ("Font32rle", "f32"), 6: ("Font64rle", "f64")}

    def __init__(self, fonts_dir):
        self.widths, self.height = {}, {}
        self._src, self._names, self._glyphs = {}, {}, {}
        for font, (base, tag) in self.FILES.items():
            header = open(os.path.join(fonts_dir, base + ".h"), encoding="utf-8").read()
            src = open(os.path.join(fonts_dir, base + ".c"), encoding="utf-8").read()
            self.height[font] = int(re.search(rf"#define\s+chr_hgt_{tag}\s+(\d+)", header).group(1))
            self.widths[font] = _c_numbers(src, f"widtbl_{tag}")
            table = "\n".join(_active_lines(_c_body(src, f"chrtbl_{tag}")))
            self._names[font] = re.findall(rf"\bchr_{tag}_[0-9A-Fa-f]+\b", table)
            if len(self.widths[font]) != 96 or len(self._names[font]) != 96:
                raise ValueError(f"{base}.c: expected 96 widths and 96 glyphs, got "
                                 f"{len(self.widths[font])} and {len(self._names[font])}")
            self._src[font] = src

    def glyph(self, font, code):
        """(x, y) pixel offsets of character `code` (32..127), lit in the text colour."""
        key = (font, code)
        if key not in self._glyphs:
            index = code - 32
            width, height = self.widths[font][index], self.height[font]
            data = _c_numbers(self._src[font], self._names[font][index])
            self._glyphs[key] = (self._decode_bitmap if font == 2 else self._decode_rle)(
                data, width, height)
        return self._glyphs[key]

    @staticmethod
    def _decode_bitmap(data, width, height):
        # Font 2: (width + 6) / 8 bytes per row, most significant bit first,
        # exactly as TFT_eSPI::drawChar() walks it.
        per_row = (width + 6) // 8
        if len(data) < per_row * height:
            raise ValueError("font 2 glyph shorter than its rows")
        pixels = []
        for row in range(height):
            for k in range(per_row):
                byte = data[row * per_row + k]
                for bit in range(8):
                    if byte & (0x80 >> bit):
                        pixels.append((k * 8 + bit, row))
        return pixels

    @staticmethod
    def _decode_rle(data, width, height):
        # Fonts 4 and 6: each byte is a run of (byte & 0x7F) + 1 pixels, lit when
        # bit 7 is set, filling the glyph left to right, top to bottom. Like
        # TFT_eSPI::drawChar(), stop once width * height pixels are covered: a few
        # glyphs this firmware never draws (font 6 is digits only) carry runs that
        # overshoot, and the panel simply ignores the excess.
        total, pc, pixels = width * height, 0, []
        for byte in data:
            if pc >= total:
                break
            run = (byte & 0x7F) + 1
            if byte & 0x80:
                pixels.extend(((p % width, p // width) for p in range(pc, min(pc + run, total))))
            pc += run
        if pc < total:
            raise ValueError(f"RLE glyph data ends after {pc} of {total} pixels")
        return pixels


def _load_tft_fonts():
    # Every environment pins the same TFT_eSPI (platformio.ini [base] lib_deps),
    # so whichever environment's copy is found first is the right one.
    hits = sorted(glob.glob(os.path.join(ROOT, ".pio", "libdeps", "*", "TFT_eSPI", "Fonts")))
    return TftFonts(hits[0]) if hits else None


TFT = _load_tft_fonts()

# DejaVu Sans stand-ins at TFT_eSPI's pixel heights, used only when TFT is None.
FONT_DIR = "/usr/share/fonts/truetype/dejavu"
PIL_FONT = {
    2: ImageFont.truetype(os.path.join(FONT_DIR, "DejaVuSans.ttf"), 15),
    4: ImageFont.truetype(os.path.join(FONT_DIR, "DejaVuSans.ttf"), 24),
    6: ImageFont.truetype(os.path.join(FONT_DIR, "DejaVuSans-Bold.ttf"), 46),
}


def text_width(s, font):
    """Width of `s` in TFT_eSPI font `font`, as TFT_eSPI::textWidth() computes it."""
    if TFT:
        # Characters outside 32..127 count as a space, as textWidth() does.
        return sum(TFT.widths[font][ord(c) - 32 if 32 <= ord(c) < 128 else 0] for c in s)
    return PIL_FONT[font].getlength(s)


def text(d, xy, s, font, fill, datum="TL"):
    """Draw `s` in TFT_eSPI font `font` with drawString()'s datum semantics: T/M/B x L/C/R."""
    if not s:
        return
    x, y = xy
    if TFT:
        # drawString(): width is the sum of the glyph widths, height the font's
        # height, and the offsets use integer division.
        cwidth, cheight = text_width(s, font), TFT.height[font]
        if datum[1] == "C": x -= cwidth // 2
        elif datum[1] == "R": x -= cwidth
        if datum[0] == "M": y -= cheight // 2
        elif datum[0] == "B": y -= cheight
        points = []
        for c in s:
            code = ord(c) if 32 <= ord(c) < 128 else 32
            points.extend((x + gx, y + gy) for gx, gy in TFT.glyph(font, code))
            x += TFT.widths[font][code - 32]
        d.point(points, fill=fill)
        return
    f = PIL_FONT[font]
    l, t, r, b = d.textbbox((0, 0), s, font=f)
    w, h = r - l, b - t
    if datum[1] == "R": x -= w
    elif datum[1] == "C": x -= w // 2
    if datum[0] == "M": y -= h // 2
    elif datum[0] == "B": y -= h
    d.text((x - l, y - t), s, font=f, fill=fill)


def station_labels():
    """The short station names from src/station_label.h, as {full: short}."""
    src = open(STATION_LABEL_H, encoding="utf-8").read()
    return dict(re.findall(r'\{"([^"]+)",\s*"([^"]+)"\}', src))


def frame(d, b, colour):
    for i in range(b["BORDER"]):
        d.rectangle([i, i, b["SCREEN_W"] - 1 - i, b["SCREEN_H"] - 1 - i], outline=colour)


def render(model, b):
    W, H, BORDER = b["SCREEN_W"], b["SCREEN_H"], b["BORDER"]
    INNER_X = INNER_Y = BORDER
    INNER_W, INNER_H = W - 2 * BORDER, H - 2 * BORDER
    ROWS_Y = INNER_Y + b["HEADER_H"]
    ROW_H = (INNER_H - b["HEADER_H"]) // BOARD_ROWS
    COL_MIN_R = INNER_X + b["COUNT_R"]
    COL_INFO = INNER_X + b["COL_INFO_X"]
    COL_RIGHT = INNER_X + INNER_W - b["COL_RIGHT_INSET"]

    img = Image.new("RGB", (W, H), COL_BG)
    d = ImageDraw.Draw(img)

    # Border takes its colour from the soonest train — the one the glance is about.
    frame(d, b, URGENCY[model["rows"][0]["urgency"]] if model["rows"] else COL_FRAME_IDLE)

    # The clock is laid out first because it is fixed-width and reserves the
    # right-hand space the route name then has to fit inside. The width used is
    # that of the WIDEST clock, "88:88", not the current one.
    text(d, (COL_RIGHT, INNER_Y + b["CLOCK_DY"]), model["clock"], b["CLOCK_FONT"], COL_TEXT, "TR")
    route_max_w = INNER_W - text_width("88:88", b["CLOCK_FONT"]) - b["ROUTE_CLOCK_GAP"]

    # fontThatFits(), then the wrap: the same decisions render.cpp's board() makes.
    labels = station_labels() if b["SHORT_STATION_NAMES"] else {}
    origin = labels.get(model["origin"], model["origin"])
    dest = labels.get(model["destination"], model["destination"])
    header = f"{origin}  >  {dest}"
    big = b["ROUTE_FONT_BIG"]
    f = big if text_width(header, big) <= route_max_w else 2
    if b["ROUTE_WRAP"] and text_width(header, f) > route_max_w:
        text(d, (INNER_X + 4, INNER_Y + b["ROUTE_LINE1_DY"]), origin, 2, COL_TEXT, "TL")
        text(d, (INNER_X + 4, INNER_Y + b["ROUTE_LINE2_DY"]), f"> {dest}", 2, COL_TEXT, "TL")
    else:
        dy = b["ROUTE_BIG_DY"] if f == 4 else b["ROUTE_SMALL_DY"]
        text(d, (INNER_X + 4, INNER_Y + dy), header, f, COL_TEXT, "TL")

    if not model["anyLive"]:
        text(d, (INNER_X + 4, INNER_Y + b["NOTE_DY"]), "SCHEDULED TIMES - no live data",
             2, COL_SCHED, "TL")

    for i in range(1, BOARD_ROWS):
        y = ROWS_Y + i * ROW_H
        d.line([(INNER_X + 4, y), (INNER_X + INNER_W - 4, y)], fill=COL_RULE)

    for i, r in enumerate(model["rows"][:BOARD_ROWS]):
        y = ROWS_Y + i * ROW_H
        text(d, (COL_MIN_R, y + b["COUNT_DY"]), str(r["minutesAway"]),
             b["COUNT_FONT"], URGENCY[r["urgency"]], "MR")
        text(d, (COL_MIN_R + b["MIN_LABEL_DX"], y + b["MIN_LABEL_DY"]), "min", 2, COL_DIM, "TL")
        text(d, (COL_INFO, y + b["WHEN_DY"]), r["when"], 4, COL_TEXT, "ML")

        if not r["isLive"]:
            status, colour = "SCHED", COL_SCHED
        elif r["delaySec"] >= 60:
            status, colour = f'+{r["delaySec"] // 60} late', COL_LATE
        elif r["delaySec"] <= -60:
            status, colour = f'{-r["delaySec"] // 60} early', COL_DIM
        else:
            status, colour = "on time", COL_DIM
        text(d, (COL_RIGHT, y + b["WHEN_DY"]), status, b["ROW_STATUS_FONT"], colour, "MR")
        text(d, (COL_INFO, y + b["INFO_DY"]), f'#{r["number"]} {r["route"]}', 2, COL_DIM, "ML")

    return img


def splash_lines(array_name="kSplashAttributionWide"):
    """Read the attribution strings out of src/layout.h rather than restating them.

    Two copies of a legal notice drift, and the copy in the picture is the one
    people would quote. This parses the actual array the firmware draws.
    """
    src = open(LAYOUT_H, encoding="utf-8").read()
    start = src.index(f"{array_name}[] = {{")
    body = src[start:src.index("};", start)]
    out = []
    for line in body.splitlines()[1:]:
        line = line.strip()
        if line.startswith('"'):
            out.append(line[1:line.rindex('"')].replace('\\"', '"'))
    return out


def render_splash(b, detail="connecting to WiFi..."):
    W, H = b["SCREEN_W"], b["SCREEN_H"]
    inner_w = W - 2 * b["BORDER"]
    img = Image.new("RGB", (W, H), COL_BG)
    d = ImageDraw.Draw(img)
    frame(d, b, COL_FRAME_IDLE)
    text(d, (W // 2, b["SPLASH_TITLE_Y"]), "Caltrain Notifier", 4, COL_TEXT, "MC")
    for i, line in enumerate(splash_lines(b["SPLASH_ARRAY"])):
        if not line:
            continue
        text(d, (W // 2, b["SPLASH_ATTR_Y"] + i * b["SPLASH_ATTR_DY"]), line, 2, COL_DIM, "MC")
        w = text_width(line, 2)
        if w > inner_w - 8:
            print(f"  WARNING: line overruns the panel by {w - (inner_w - 8):.0f}px: {line}",
                  file=sys.stderr)
    text(d, (W // 2, b["SPLASH_NOTE_Y"]), detail, 2, COL_DIM, "MC")
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
    # The bands are the shipped DEFAULTS (kUrgencyDefaults in src/urgency.h,
    # 10 and 16). They are adjustable per device in the setup portal, so this
    # legend documents what a fresh sign does, not a rule fixed in firmware.
    border = BOARDS["crowpanel35"]["BORDER"]
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
        for k in range(border):
            d.rectangle([pad + k, y + k, pad + sw_w - 1 - k, y + row_h - 12 - k], outline=c)
        text(d, (pad + sw_w // 2 - 16, y + (row_h - 12) // 2), mins, 6, c, "MC")
        text(d, (pad + sw_w // 2 + 20, y + (row_h - 12) // 2 + 6), "min", 2, COL_DIM, "ML")
        text(d, (pad + sw_w + 24, y + (row_h - 12) // 2 - 10), band, 4, COL_TEXT, "ML")
        text(d, (pad + sw_w + 24, y + (row_h - 12) // 2 + 14), note, 2, COL_DIM, "ML")
    return img


def arg_value(name):
    """The value after `name` on the command line, or None if the flag is absent."""
    if name not in sys.argv:
        return None
    i = sys.argv.index(name)
    if i + 1 >= len(sys.argv):
        sys.exit(f"{name} needs a value")
    return sys.argv[i + 1]


def write(img, path):
    # 2x for legibility on a high-density display, nearest-neighbour so the
    # pixel grid of the panel stays visible rather than being smoothed away.
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.save(path)
    img.resize((img.width * 2, img.height * 2), Image.NEAREST).save(path.replace(".png", "@2x.png"))
    print(f"wrote {path} and its @2x")


def main():
    if TFT is None:
        print("  NOTE: TFT_eSPI fonts not found under .pio/libdeps (run a pio build first); "
              "drawing with DejaVu Sans, which is wider than the panel's fonts", file=sys.stderr)

    if "--legend" in sys.argv:
        write(render_legend(), OUT_LEGEND)
        return 0

    board_name = arg_value("--board") or "crowpanel35"
    if board_name not in BOARDS:
        sys.exit(f"unknown --board {board_name}; choose from {', '.join(BOARDS)}")
    b = BOARDS[board_name]
    out_dir = arg_value("--out")
    if b["OUT"] is None and out_dir is None:
        sys.exit(f"--board {board_name} renders previews only; pass --out DIR")

    if "--splash" in sys.argv:
        path = os.path.join(out_dir, f"splash-{board_name}.png") if out_dir else b["OUT_SPLASH"]
        write(render_splash(b), path)
        return 0

    model = json.load(sys.stdin)
    path = os.path.join(out_dir, f"board-{board_name}.png") if out_dir else b["OUT"]
    write(render(model, b), path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
