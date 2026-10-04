#define DMOD_ENABLE_REGISTRATION ON
#include "dmod_test.h"
#include "libtodmvf.h"
#include <errno.h>
#include <string.h>

/*
 * libtodmvf: Roboto Regular (fixtures/, Apache 2.0) rendered into .dmvf
 * files, read back and checked as libdmview checks them (dmview's
 * docs/font-format.md).
 */

#ifndef LIBTODMVF_TEST_DIR
#define LIBTODMVF_TEST_DIR "."
#endif
#ifndef LIBTODMVF_FIXTURES_DIR
#define LIBTODMVF_FIXTURES_DIR "fixtures"
#endif
#define OUTPUT(name)    LIBTODMVF_TEST_DIR "/" name
#define ROBOTO          LIBTODMVF_FIXTURES_DIR "/Roboto-Regular.ttf"

static uint8_t  g_file[64 * 1024];
static uint32_t g_size;

static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t* p) { return (uint32_t)rd16(p) | ((uint32_t)rd16(p + 2) << 16); }

static bool load(const char* path)
{
    void* f = Dmod_FileOpen(path, "rb");
    if (f == NULL)
        return false;
    g_size = (uint32_t)Dmod_FileRead(g_file, 1, sizeof(g_file), f);
    Dmod_FileClose(f);
    return g_size >= sizeof(dmvf_header_t);
}

/* The file is valid as libdmview's fontfile.c checks it */
static bool valid(void)
{
    const uint8_t* h = g_file;
    if (memcmp(h, "DMVF", 4) != 0 || rd16(h + 4) != DMVF_VERSION_MAJOR || rd32(h + 8) != g_size)
        return false;
    uint32_t count = rd32(h + 20), glyphs = rd32(h + 24), bitmaps = rd32(h + 28);
    if (glyphs % 4U != 0 || glyphs < sizeof(dmvf_header_t) || glyphs + count * sizeof(dmvf_glyph_t) > bitmaps ||
        bitmaps > g_size)
        return false;
    for (uint32_t i = 0; i < count; i++)
    {
        const uint8_t* g = g_file + glyphs + i * sizeof(dmvf_glyph_t);
        uint32_t bytes = ((uint32_t)g[6] + 1U) / 2U * g[7];
        if (rd32(g) + bytes > g_size - bitmaps || g[11] != 0 || (i > 0 && rd16(g + 4) <= rd16(g + 4 - sizeof(dmvf_glyph_t))))
            return false;
    }
    return true;
}

static const uint8_t* glyph(uint32_t codepoint)
{
    uint32_t count = rd32(g_file + 20), glyphs = rd32(g_file + 24);
    for (uint32_t i = 0; i < count; i++)
    {
        const uint8_t* g = g_file + glyphs + i * sizeof(dmvf_glyph_t);
        if (rd16(g + 4) == codepoint)
            return g;
    }
    return NULL;
}

/* Coverage (0 ... 15) of pixel x, y of a glyph's bitmap */
static uint32_t coverage(const uint8_t* g, uint32_t x, uint32_t y)
{
    const uint8_t* bitmap = g_file + rd32(g_file + 28) + rd32(g) + y * ((g[6] + 1U) / 2U);
    return (bitmap[x / 2U] >> ((x & 1U) * 4U)) & 0x0Fu;
}

DMOD_TEST_STEP(libtodmvf_renders_a_font)
{
    libtodmvf_result_t r;
    libtodmvf_options_t o = { 16, NULL };
    DMOD_TEST_EXPECT_EQ(libtodmvf_convert_file(ROBOTO, OUTPUT("sans-16.dmvf"), &o, &r), 0);
    DMOD_TEST_EXPECT_TRUE(load(OUTPUT("sans-16.dmvf")));
    DMOD_TEST_EXPECT_TRUE(valid());
    DMOD_TEST_EXPECT_EQ(r.size, g_size);
    DMOD_TEST_EXPECT_TRUE(r.glyphs > 300u && r.glyphs + r.missing == (0x7Eu - 0x20u + 1u) + (0x17Fu - 0xA0u + 1u));
    DMOD_TEST_EXPECT_EQ(rd32(g_file + 20), r.glyphs);
    DMOD_TEST_EXPECT_EQ(rd16(g_file + 12), 16u);
    DMOD_TEST_EXPECT_EQ(rd16(g_file + 14), (uint32_t)(r.ascent + r.descent));
    DMOD_TEST_EXPECT_TRUE(r.ascent >= 14 && r.ascent <= 16 && r.descent >= 3 && r.descent <= 5);   /* Roboto: 0.93 / 0.24 em */

    /* The space: no bitmap, an advance */
    const uint8_t* space = glyph(' ');
    DMOD_TEST_EXPECT_TRUE(space != NULL && space[6] == 0 && space[7] == 0 && space[10] >= 3);

    /* 'l': a full-height stem, solid in the middle, on the baseline */
    const uint8_t* l = glyph('l');
    DMOD_TEST_EXPECT_TRUE(l != NULL);
    if (l != NULL)
    {
        DMOD_TEST_EXPECT_TRUE(l[7] >= 11 && l[7] <= 13);          /* height */
        DMOD_TEST_EXPECT_EQ((int8_t)l[9], (int)l[7]);             /* top: it ends on the baseline */
        uint32_t best = 0;
        for (uint32_t x = 0; x < l[6]; x++)
            best = (coverage(l, x, l[7] / 2U) > best) ? coverage(l, x, l[7] / 2U) : best;
        DMOD_TEST_EXPECT_TRUE(best >= 12);
    }

    /* 'g' descends below the baseline; Polish letters are there */
    const uint8_t* g = glyph('g');
    DMOD_TEST_EXPECT_TRUE(g != NULL && (int)g[7] > (int8_t)g[9]);
    DMOD_TEST_EXPECT_TRUE(glyph(0x105) != NULL && glyph(0x17C) != NULL);     /* ą ż */
}

DMOD_TEST_STEP(libtodmvf_renders_the_characters_asked_for)
{
    libtodmvf_result_t r;
    libtodmvf_options_t o = { 24, "0x41-0x43,0x30,0xFFFF" };
    DMOD_TEST_EXPECT_EQ(libtodmvf_convert_file(ROBOTO, OUTPUT("abc.dmvf"), &o, &r), 0);
    DMOD_TEST_EXPECT_EQ(r.glyphs, 4u);                          /* 0 A B C */
    DMOD_TEST_EXPECT_EQ(r.missing, 1u);                         /* U+FFFF */
    DMOD_TEST_EXPECT_TRUE(load(OUTPUT("abc.dmvf")));
    DMOD_TEST_EXPECT_TRUE(valid());
    DMOD_TEST_EXPECT_TRUE(glyph('0') != NULL && glyph('C') != NULL && glyph('D') == NULL);

    o.chars = "65-66";                                          /* decimal */
    DMOD_TEST_EXPECT_EQ(libtodmvf_convert_file(ROBOTO, OUTPUT("abc.dmvf"), &o, &r), 0);
    DMOD_TEST_EXPECT_EQ(r.glyphs, 2u);
}

DMOD_TEST_STEP(libtodmvf_reports_what_it_cannot_do)
{
    libtodmvf_options_t o = { 16, "0x50-0x40" };
    DMOD_TEST_EXPECT_EQ(libtodmvf_convert_file(ROBOTO, OUTPUT("x.dmvf"), &o, NULL), -EINVAL);
    o.chars = "A-Z";
    DMOD_TEST_EXPECT_EQ(libtodmvf_convert_file(ROBOTO, OUTPUT("x.dmvf"), &o, NULL), -EINVAL);
    o.chars = "0x20-0x10000";
    DMOD_TEST_EXPECT_EQ(libtodmvf_convert_file(ROBOTO, OUTPUT("x.dmvf"), &o, NULL), -EINVAL);
    o.chars = NULL;
    o.size = 3;
    DMOD_TEST_EXPECT_EQ(libtodmvf_convert_file(ROBOTO, OUTPUT("x.dmvf"), &o, NULL), -EINVAL);
    o.size = 16;
    DMOD_TEST_EXPECT_EQ(libtodmvf_convert_file(OUTPUT("missing.ttf"), OUTPUT("x.dmvf"), &o, NULL), -ENOENT);
    static const char not_a_font[] = "This is not a font, just some text long enough.";
    DMOD_TEST_EXPECT_EQ(libtodmvf_convert(not_a_font, sizeof(not_a_font), OUTPUT("x.dmvf"), &o, NULL), -EBADMSG);
    DMOD_TEST_EXPECT_FALSE(Dmod_FileAvailable(OUTPUT("x.dmvf")));
}
