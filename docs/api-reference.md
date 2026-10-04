# libtodmvf API Reference

`#include "libtodmvf.h"` - it includes dmview's `dmview_format.h` for the
`.dmvf` structures.

## Options

`libtodmvf_options_t`:

| Field | Meaning |
|-------|---------|
| `size` | Pixel size (em) the font is made for, `LIBTODMVF_MIN_SIZE` (4) ... `LIBTODMVF_MAX_SIZE` (127) |
| `chars` | Codepoint ranges - `0x20-0x7E,0x104`, hexadecimal (`0x...`) or decimal, up to `0xFFFF`; NULL: `LIBTODMVF_DEFAULT_CHARS` (`0x20-0x7E,0xA0-0x17F`) |
| `tracking` | Letter spacing added to every glyph's advance, in 1/100 pixel (`-240`: -2.4 px, like CSS `letter-spacing`), up to `LIBTODMVF_MAX_TRACKING` (127 px) either way; the advance is rounded after it is added, and never goes below 0 |

## Result

`libtodmvf_result_t`:

| Field | Meaning |
|-------|---------|
| `glyphs` | Glyphs written |
| `missing` | Characters asked for that the font does not have (or that are blank, other than the spaces U+0020, U+00A0) |
| `line_height`, `ascent`, `descent` | As in the file's header: the font's ascent and descent at the size, rounded up |
| `size` | Bytes of the file |

## Rendering

For each character: its glyph is rendered `LIBTODMVF_SUPERSAMPLE` (8)
times larger, the ink is snapped out to whole target pixels, and each
target pixel gets the average of the 8 x 8 pixels it covers, as 4 bits
(0 ... 15). The advance is the font's, rounded. Fonts with TrueType or CFF
outlines (`.ttf`, `.otf`) are read; of a collection (`.ttc`), the first
font.

## Functions

### `libtodmvf_convert_file`

```c
int libtodmvf_convert_file(const char* input, const char* output, const libtodmvf_options_t* options, libtodmvf_result_t* result);
```

Render the font file `input` into the `.dmvf` file `output`. The font is
read into memory whole while it is converted. The output is written to
`<output>.<pid>-<id>.tmp` and renamed when complete: a reader sees the old
file or the new one, a failed conversion leaves the old one. `result` may
be NULL.

Returns 0, `-ENOENT` (no input), `-EBADMSG` (not a font), `-EINVAL` (a
size or a letter spacing out of range, wrong ranges in `chars`, a glyph too large for the
format - wider or taller than 255 pixels), `-ENOMEM`, `-EIO`.

### `libtodmvf_convert`

```c
int libtodmvf_convert(const void* font, size_t font_size, const char* output, const libtodmvf_options_t* options, libtodmvf_result_t* result);
```

The same for a font in memory.

## todmvf

```
todmvf [options] FONT SIZE
  SIZE             pixel size (em), 4 ... 127
  -o OUTPUT        the .dmvf file (default: FONT without its extension, -SIZE.dmvf)
  -c RANGES        codepoints, e.g. 0x20-0x7E,0x104 (default: 0x20-0x7E,0xA0-0x17F)
  -t PIXELS        letter spacing added to every advance, e.g. -2.4 (CSS letter-spacing)
  -q               print nothing but errors
```

Exit code 0 on success, 1 on any error (printed).
