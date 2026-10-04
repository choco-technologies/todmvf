#define DMOD_ENABLE_REGISTRATION ON
#include "dmod.h"
#include "libtodmvf.h"
#include <errno.h>
#include <string.h>

/*
 * The conversion - what dmview's tools/ttf2dmvf.py did, on stb_truetype
 * (third_party/stb): each glyph is rendered LIBTODMVF_SUPERSAMPLE times
 * larger, its ink is snapped out to whole target pixels, and every target
 * pixel gets the average of the SUPERSAMPLE x SUPERSAMPLE pixels it covers,
 * in 4 bits. The glyphs are collected in memory and written as one file
 * (dmview's docs/font-format.md).
 */

/* stb_truetype: only what this file uses (static - unused parts drop out:
 * the SDF code, the only user of pow, fmod, cos and acos), memory through
 * dmod, math without libm */
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_malloc(x, u)  ((void)(u), Dmod_Malloc(x))
#define STBTT_free(x, u)    ((void)(u), Dmod_Free(x))
#define STBTT_assert(x)     ((void)0)
#define STBTT_ifloor(x)     ifloor_(x)
#define STBTT_iceil(x)      iceil_(x)
#define STBTT_sqrt(x)       sqrt_((float)(x))
#define STBTT_fabs(x)       __builtin_fabs(x)
#define STBTT_pow(x, y)     __builtin_pow(x, y)
#define STBTT_fmod(x, y)    __builtin_fmod(x, y)
#define STBTT_cos(x)        __builtin_cos(x)
#define STBTT_acos(x)       __builtin_acos(x)
#define STBTT_strlen(x)     strlen(x)
#define STBTT_memcpy        memcpy
#define STBTT_memset        memset

static inline int ifloor_(double x)
{
    int i = (int)x;
    return (x < (double)i) ? i - 1 : i;
}

static inline int iceil_(double x)
{
    int i = (int)x;
    return (x > (double)i) ? i + 1 : i;
}

/* Square root for flattening curves: Newton's method from an estimate of the
 * exponent - float precision, no libm (and no FPU needed) */
static inline float sqrt_(float x)
{
    if (!(x > 0.0f))
        return 0.0f;
    union { float f; uint32_t u; } v = { x };
    v.u = (v.u >> 1) + 0x1FBD1DF5u;
    float r = v.f;
    for (int i = 0; i < 4; i++)
        r = 0.5f * (r + x / r);
    return r;
}

#include "stb_truetype.h"

#define HEADER          ((uint32_t)sizeof(dmvf_header_t))
#define GLYPH           ((uint32_t)sizeof(dmvf_glyph_t))
#define MAX_CODEPOINT   0xFFFFu
#define Q               ((int)LIBTODMVF_SUPERSAMPLE)

static void wr16(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t* p, uint32_t v) { wr16(p, v); wr16(p + 2, v >> 16); }

static inline int floor_div(int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }
static inline int ceil_div(int a, int b)  { return -floor_div(-a, b); }

/* ---- Characters ---- */

/* A number - 0x... hexadecimal, else decimal - up to MAX_CODEPOINT + 1 */
static bool parse_number(const char** p, uint32_t* value)
{
    const char* s = *p;
    uint32_t v = 0, base = 10;
    bool any = false;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
    {
        base = 16;
        s += 2;
    }
    for (;; s++)
    {
        uint32_t d;
        if (*s >= '0' && *s <= '9')
            d = (uint32_t)(*s - '0');
        else if (base == 16 && *s >= 'a' && *s <= 'f')
            d = (uint32_t)(*s - 'a' + 10);
        else if (base == 16 && *s >= 'A' && *s <= 'F')
            d = (uint32_t)(*s - 'A' + 10);
        else
            break;
        v = v * base + d;
        if (v > MAX_CODEPOINT + 1U)
            v = MAX_CODEPOINT + 1U;
        any = true;
    }
    *p = s;
    *value = v;
    return any;
}

/* "0x20-0x7E,0xA0-0x17F" into a bit per codepoint (MAX_CODEPOINT + 1 bits) */
static int parse_chars(const char* spec, uint8_t* bits)
{
    const char* p = spec;
    memset(bits, 0, (MAX_CODEPOINT + 1U) / 8U);
    while (*p != '\0')
    {
        uint32_t first, last;
        if (!parse_number(&p, &first))
            return -EINVAL;
        last = first;
        if (*p == '-')
        {
            p++;
            if (!parse_number(&p, &last))
                return -EINVAL;
        }
        if (first > last || last > MAX_CODEPOINT || (*p != ',' && *p != '\0'))
            return -EINVAL;
        for (uint32_t c = first; c <= last; c++)
            bits[c / 8U] |= (uint8_t)(1U << (c % 8U));
        if (*p == ',')
            p++;
    }
    return 0;
}

/* ---- Output ---- */

typedef struct
{
    uint8_t*    data;
    uint32_t    size;
    uint32_t    capacity;
} buffer_t;

static bool reserve(buffer_t* b, uint32_t more)
{
    if (b->size + more <= b->capacity)
        return true;
    uint32_t capacity = (b->capacity < 4096U) ? 4096U : b->capacity;
    while (capacity < b->size + more)
        capacity *= 2U;
    uint8_t* data = Dmod_Malloc(capacity);
    if (data == NULL)
        return false;
    if (b->data != NULL)
    {
        memcpy(data, b->data, b->size);
        Dmod_Free(b->data);
    }
    b->data = data;
    b->capacity = capacity;
    return true;
}

/* "<output>.<pid>-<id>.tmp" (allocated), unique for every running conversion */
static char* temp_path(const char* output, const void* unique)
{
    size_t size = strlen(output) + 32U;
    char* path = Dmod_Malloc(size);
    if (path != NULL)
        Dmod_SnPrintf(path, size, "%s.%x-%x.tmp", output, (unsigned)Dmod_GetCurrentPid(), (unsigned)(uintptr_t)unique);
    return path;
}

static int write_file(const char* output, const buffer_t* parts, size_t count)
{
    char* temp = temp_path(output, &parts);
    if (temp == NULL)
        return -ENOMEM;
    void* f = Dmod_FileOpen(temp, "wb");
    if (f == NULL)
    {
        Dmod_Free(temp);
        return -EIO;
    }
    bool ok = true;
    for (size_t i = 0; i < count && ok; i++)
        ok = parts[i].size == 0 || Dmod_FileWrite(parts[i].data, 1, parts[i].size, f) == parts[i].size;
    Dmod_FileClose(f);

    /* The old output first: not every file system's rename replaces a file */
    if (ok && Dmod_FileAvailable(output))
        (void)Dmod_FileRemove(output);
    ok = ok && Dmod_Rename(temp, output) == 0;
    if (!ok)
        (void)Dmod_FileRemove(temp);
    Dmod_Free(temp);
    return ok ? 0 : -EIO;
}

/* ---- Glyphs ---- */

typedef struct
{
    stbtt_fontinfo  font;
    float           big;                /* Scale of the supersampled rendering */
    uint8_t*        render;             /* Its bitmap */
    uint32_t        render_size;
    buffer_t        glyphs;             /* dmvf_glyph_t records */
    buffer_t        bitmaps;
} converter_t;

static bool is_space(uint32_t c) { return c == 0x20u || c == 0xA0u; }

/* Render codepoint `c` into the tables; false and *status when it fails,
 * *missing when the font has no glyph for it */
static bool add_glyph(converter_t* cv, uint32_t c, bool* missing, int* status)
{
    *missing = false;
    int g = stbtt_FindGlyphIndex(&cv->font, (int)c);
    if (g == 0)
    {
        *missing = true;
        return true;
    }

    int advance_units, lsb;
    stbtt_GetGlyphHMetrics(&cv->font, g, &advance_units, &lsb);
    int advance = ifloor_((double)advance_units * cv->big / Q + 0.5);      /* rounded */

    /* The supersampled bitmap and its ink, relative to the pen at the baseline */
    int x0, y0, x1, y1, ink_x0 = 0, ink_y0 = 0, ink_x1 = 0, ink_y1 = 0;
    stbtt_GetGlyphBitmapBox(&cv->font, g, cv->big, cv->big, &x0, &y0, &x1, &y1);
    int bw = x1 - x0, bh = y1 - y0;
    bool ink = false;
    if (bw > 0 && bh > 0)
    {
        uint32_t need = (uint32_t)bw * (uint32_t)bh;
        if (need > cv->render_size)
        {
            if (cv->render != NULL)
                Dmod_Free(cv->render);
            cv->render_size = 0;
            if ((cv->render = Dmod_Malloc(need)) == NULL)
            {
                *status = -ENOMEM;
                return false;
            }
            cv->render_size = need;
        }
        stbtt_MakeGlyphBitmap(&cv->font, cv->render, bw, bh, bw, cv->big, cv->big, g);
        for (int y = 0; y < bh; y++)
            for (int x = 0; x < bw; x++)
            {
                if (cv->render[y * bw + x] == 0)
                    continue;
                if (!ink)
                {
                    ink_x0 = ink_x1 = x;
                    ink_y0 = ink_y1 = y;
                    ink = true;
                }
                if (x < ink_x0) ink_x0 = x;
                if (x > ink_x1) ink_x1 = x;
                if (y < ink_y0) ink_y0 = y;
                if (y > ink_y1) ink_y1 = y;
            }
    }
    if (!ink && !is_space(c))
    {
        *missing = true;                /* Blank - not really in the font */
        return true;
    }

    /* Snapped out to whole target pixels */
    int left = 0, upper = 0, width = 0, height = 0;
    if (ink)
    {
        left = floor_div(x0 + ink_x0, Q);
        upper = floor_div(y0 + ink_y0, Q);
        width = ceil_div(x0 + ink_x1 + 1, Q) - left;
        height = ceil_div(y0 + ink_y1 + 1, Q) - upper;
    }
    int top = -upper;
    if (width > 255 || height > 255 || advance < 0 || advance > 255 || left < -128 || left > 127 || top < -128 ||
        top > 127)
    {
        *status = -EINVAL;              /* Too large for the format */
        return false;
    }

    uint32_t row_bytes = ((uint32_t)width + 1U) / 2U;
    if (!reserve(&cv->glyphs, GLYPH) || !reserve(&cv->bitmaps, row_bytes * (uint32_t)height))
    {
        *status = -ENOMEM;
        return false;
    }
    uint8_t* rec = cv->glyphs.data + cv->glyphs.size;
    wr32(rec, cv->bitmaps.size);
    wr16(rec + 4, c);
    rec[6] = (uint8_t)width;
    rec[7] = (uint8_t)height;
    rec[8] = (uint8_t)(int8_t)left;
    rec[9] = (uint8_t)(int8_t)top;
    rec[10] = (uint8_t)advance;
    rec[11] = 0;
    cv->glyphs.size += GLYPH;

    /* Each target pixel: the average of the Q x Q pixels it covers */
    uint8_t* out = cv->bitmaps.data + cv->bitmaps.size;
    memset(out, 0, row_bytes * (uint32_t)height);
    for (int ty = 0; ty < height; ty++)
        for (int tx = 0; tx < width; tx++)
        {
            uint32_t sum = 0;
            for (int sy = 0; sy < Q; sy++)
            {
                int y = (upper + ty) * Q + sy - y0;
                if (y < 0 || y >= bh)
                    continue;
                for (int sx = 0; sx < Q; sx++)
                {
                    int x = (left + tx) * Q + sx - x0;
                    if (x >= 0 && x < bw)
                        sum += cv->render[y * bw + x];
                }
            }
            uint32_t average = (sum + (uint32_t)(Q * Q) / 2U) / (uint32_t)(Q * Q);
            uint32_t v = (average * 15U + 127U) / 255U;
            out[(uint32_t)ty * row_bytes + (uint32_t)tx / 2U] |= (uint8_t)(v << (((uint32_t)tx & 1U) * 4U));
        }
    cv->bitmaps.size += row_bytes * (uint32_t)height;
    return true;
}

static int convert(const uint8_t* font, size_t font_size, const char* output, const libtodmvf_options_t* o,
                   libtodmvf_result_t* result)
{
    if (o->size < LIBTODMVF_MIN_SIZE || o->size > LIBTODMVF_MAX_SIZE)
        return -EINVAL;
    uint8_t* chars = Dmod_Malloc((MAX_CODEPOINT + 1U) / 8U);
    converter_t* cv = Dmod_Malloc(sizeof(*cv));
    int status = (chars != NULL && cv != NULL) ? 0 : -ENOMEM;
    if (status == 0)
    {
        memset(cv, 0, sizeof(*cv));
        status = parse_chars((o->chars != NULL) ? o->chars : LIBTODMVF_DEFAULT_CHARS, chars);
    }
    int offset = (status == 0) ? stbtt_GetFontOffsetForIndex(font, 0) : -1;
    if (status == 0 && (font_size < 12U || offset < 0 || !stbtt_InitFont(&cv->font, font, offset)))
        status = -EBADMSG;

    uint32_t missing = 0;
    int ascent = 0, descent = 0;
    if (status == 0)
    {
        float scale = stbtt_ScaleForMappingEmToPixels(&cv->font, (float)o->size);
        int ascent_units, descent_units, gap;
        stbtt_GetFontVMetrics(&cv->font, &ascent_units, &descent_units, &gap);
        ascent = iceil_((double)ascent_units * scale);
        descent = iceil_(-(double)descent_units * scale);
        cv->big = scale * (float)Q;
        for (uint32_t c = 0; c <= MAX_CODEPOINT && status == 0; c++)
        {
            bool is_missing = false;
            if ((chars[c / 8U] & (1U << (c % 8U))) != 0 && add_glyph(cv, c, &is_missing, &status))
                missing += is_missing ? 1U : 0U;
        }
    }

    if (status == 0)
    {
        uint32_t count = cv->glyphs.size / GLYPH, glyphs_at = HEADER, bitmaps_at = HEADER + cv->glyphs.size;
        uint32_t size = bitmaps_at + cv->bitmaps.size;
        uint8_t h[sizeof(dmvf_header_t)];
        memcpy(h, "DMVF", 4);
        wr16(h + 4, DMVF_VERSION_MAJOR);
        wr16(h + 6, DMVF_VERSION_MINOR);
        wr32(h + 8, size);
        wr16(h + 12, o->size);
        wr16(h + 14, (uint32_t)(ascent + descent));
        wr16(h + 16, (uint32_t)(uint16_t)(int16_t)ascent);
        wr16(h + 18, (uint32_t)(uint16_t)(int16_t)descent);
        wr32(h + 20, count);
        wr32(h + 24, glyphs_at);
        wr32(h + 28, bitmaps_at);
        buffer_t parts[3] = { { h, HEADER, HEADER }, cv->glyphs, cv->bitmaps };
        status = write_file(output, parts, 3);
        if (status == 0 && result != NULL)
        {
            result->glyphs = count;
            result->missing = missing;
            result->line_height = (uint16_t)(ascent + descent);
            result->ascent = (int16_t)ascent;
            result->descent = (int16_t)descent;
            result->size = size;
        }
    }

    if (cv != NULL)
    {
        if (cv->render != NULL)
            Dmod_Free(cv->render);
        if (cv->glyphs.data != NULL)
            Dmod_Free(cv->glyphs.data);
        if (cv->bitmaps.data != NULL)
            Dmod_Free(cv->bitmaps.data);
        Dmod_Free(cv);
    }
    if (chars != NULL)
        Dmod_Free(chars);
    return status;
}

/* ---- API ---- */

dmod_libtodmvf_api_declaration(1.0, int, _convert, ( const void* font, size_t font_size, const char* output, const libtodmvf_options_t* options, libtodmvf_result_t* result ))
{
    if (font == NULL || output == NULL || options == NULL)
        return -EINVAL;
    if (result != NULL)
        memset(result, 0, sizeof(*result));
    return convert(font, font_size, output, options, result);
}

dmod_libtodmvf_api_declaration(1.0, int, _convert_file, ( const char* input, const char* output, const libtodmvf_options_t* options, libtodmvf_result_t* result ))
{
    size_t size = 0;
    if (input == NULL || output == NULL || options == NULL)
        return -EINVAL;
    if (result != NULL)
        memset(result, 0, sizeof(*result));
    void* f = Dmod_FileOpen(input, "rb");
    if (f == NULL)
        return -ENOENT;
    uint8_t* data = NULL;
    int status = 0;
    if (!Dmod_FileSizeToSizeT(Dmod_FileSize(f), &size) || size == 0 || size > 0x7FFFFFFFu)
        status = -EBADMSG;
    else if ((data = Dmod_Malloc(size)) == NULL)
        status = -ENOMEM;
    else if (Dmod_FileRead(data, 1, size, f) != size)
        status = -EIO;
    Dmod_FileClose(f);
    if (status == 0)
        status = convert(data, size, output, options, result);
    if (data != NULL)
        Dmod_Free(data);
    return status;
}

int dmod_init(const Dmod_Config_t* Config)
{
    (void)Config;
    return 0;
}

int dmod_deinit(void)
{
    return 0;
}
