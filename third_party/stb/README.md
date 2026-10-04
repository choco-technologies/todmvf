# stb_truetype

[stb_truetype.h](https://github.com/nothings/stb) v1.26 by Sean Barrett -
public domain or MIT, as you prefer (the end of the file).

The unmodified file of commit `2c980bb59875b0d32144a71867fbdebb2f77cd20`.
libtodmvf (src/convert.c) includes it with `STBTT_STATIC` - the parts it
does not use (the SDF code) drop out - with its memory through dmod and its
math without libm (`STBTT_*` macros).
