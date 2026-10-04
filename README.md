# todmvf

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![CI](https://github.com/choco-technologies/todmvf/actions/workflows/ci.yml/badge.svg)](https://github.com/choco-technologies/todmvf/actions/workflows/ci.yml)

Renders TrueType / OpenType fonts into dmview's font files (`.dmvf` - see
dmview's `docs/font-format.md`).

## Description

dmview draws text with fonts rendered beforehand for one pixel size: each
glyph an antialiased bitmap of 4-bit coverage, so the device needs no
rasterizer. todmvf makes them - in place of dmview's `tools/ttf2dmvf.py`,
with the same output, as a dmf application that runs anywhere dmod does:

- **at build time**, on the build host (dmod converts the fonts of a
  module's assets with it, like the views with todmv and the images with
  todmvi);
- **on a device** with dmod-os.

Each glyph is rendered 8 times larger and averaged down to the pixel grid,
so the coverage follows the font's outlines - no grid fitting at small
sizes. Rendering is done by [stb_truetype](third_party/stb).

Two modules, like todmv:

- **libtodmvf** - the conversion, for programs (`libtodmvf_convert_file()`,
  `libtodmvf_convert()`);
- **todmvf** - the command-line tool.

## Usage

```bash
dmod_loader todmvf.dmf --args Roboto-Regular.ttf 16 -o sans-16.dmvf
dmod_loader todmvf.dmf --args -c 0x20-0x7E Roboto-Bold.ttf 24 -o title.dmvf   # ASCII only - smaller
```

```
todmvf [options] FONT SIZE
  SIZE             pixel size (em), 4 ... 127
  -o OUTPUT        the .dmvf file (default: FONT without its extension, -SIZE.dmvf)
  -c RANGES        codepoints, e.g. 0x20-0x7E,0x104 (default: 0x20-0x7E,0xA0-0x17F)
  -q               print nothing but errors
```

```
sans-16.dmvf: 319 glyphs, 21255 bytes
```

The default characters are printable ASCII, Latin-1 and Latin Extended-A
(Polish, Czech, German, ...). Characters the font does not have are left
out (libdmview draws them as `?`) and counted.

### Compared with tools/ttf2dmvf.py

The same algorithm and the same file layout; the rasterizer differs
(stb_truetype instead of FreeType through Pillow, which hints the 8x
rendering). For Roboto 16 and 24 px: equal line metrics, 98 % of the
pixels within 1 of 15 levels of coverage, some glyphs one pixel wider or
taller at an edge - indistinguishable on screen.

## Documentation

- [docs/api-reference.md](docs/api-reference.md) - libtodmvf

## Building

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

Pass `-DDMOD_DIR=/path/to/local/dmod` to build against a local dmod checkout
instead of fetching `develop` from GitHub.

## Testing

The tests render Roboto Regular (tests/fixtures, Apache 2.0) and check the
files as libdmview does:

```bash
cd build
ctest --output-on-failure
```

## License

MIT - see [LICENSE](LICENSE); stb_truetype: public domain or MIT
([third_party/stb](third_party/stb)); the test font Roboto: Apache 2.0
([tests/fixtures/LICENSE-Roboto.txt](tests/fixtures/LICENSE-Roboto.txt)).
