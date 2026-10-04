#ifndef LIBTODMVF_H
#define LIBTODMVF_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "dmod_types.h"
#include "libtodmvf_defs.h"
#include "dmview_format.h"

/**
 * libtodmvf - renders TrueType / OpenType fonts into dmview's font files
 * (.dmvf, dmview's docs/font-format.md): every glyph an antialiased bitmap
 * with 4 bits of coverage per pixel, for one pixel size - libdmview only
 * blends them.
 *
 * Each glyph is rendered LIBTODMVF_SUPERSAMPLE times larger and averaged
 * down to the pixel grid, so the coverage follows the font's outlines (no
 * grid fitting at small sizes). The font file is read into memory whole
 * while it is converted (stb_truetype).
 */

/** Pixel sizes (em) a font can be made for. */
#define LIBTODMVF_MIN_SIZE          4u
#define LIBTODMVF_MAX_SIZE          127u

/** Largest letter spacing, either way, in 1/100 pixel. */
#define LIBTODMVF_MAX_TRACKING      12700

/** Each glyph is rendered this many times larger, then averaged down. */
#define LIBTODMVF_SUPERSAMPLE       8u

/** Default characters: printable ASCII, Latin-1 and Latin Extended-A (Polish, Czech, German, ...). */
#define LIBTODMVF_DEFAULT_CHARS     "0x20-0x7E,0xA0-0x17F"

typedef struct
{
    uint8_t     size;           /**< Pixel size (em), LIBTODMVF_MIN_SIZE ... LIBTODMVF_MAX_SIZE */
    const char* chars;          /**< Codepoint ranges, e.g. "0x20-0x7E,0x104"; NULL: LIBTODMVF_DEFAULT_CHARS */
    int32_t     tracking;       /**< Letter spacing added to every advance, in 1/100 pixel (-240: -2.4 px, CSS letter-spacing); 0: none */
} libtodmvf_options_t;

typedef struct
{
    uint32_t    glyphs;         /**< Glyphs written */
    uint32_t    missing;        /**< Characters asked for that the font does not have */
    uint16_t    line_height;
    int16_t     ascent;
    int16_t     descent;
    uint32_t    size;           /**< Bytes of the file */
} libtodmvf_result_t;

/**
 * @brief Render the font file @p input (.ttf / .otf - TrueType outlines;
 *        the first font of a collection) into the .dmvf file @p output.
 * The output is written to a temporary file and renamed when complete: a
 * reader sees the old file or the new one, a failed conversion leaves the
 * old one.
 * @param result Receives what was written (may be NULL)
 * @return 0, -ENOENT (no input), -EBADMSG (not a font stb_truetype reads),
 *         -EINVAL (a size out of range, a wrong range in chars, a glyph too
 *         large for the format), -ENOMEM, -EIO
 */
dmod_libtodmvf_api(1.0, int, _convert_file, ( const char* input, const char* output, const libtodmvf_options_t* options, libtodmvf_result_t* result ));

/** @brief libtodmvf_convert_file() of a font in memory. */
dmod_libtodmvf_api(1.0, int, _convert, ( const void* font, size_t font_size, const char* output, const libtodmvf_options_t* options, libtodmvf_result_t* result ));

#endif /* LIBTODMVF_H */
