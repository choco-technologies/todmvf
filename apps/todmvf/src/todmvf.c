#include "dmod.h"
#include "libtodmvf.h"
#include <errno.h>
#include <string.h>

/**
 * @brief todmvf - render a TrueType / OpenType font into a dmview font
 *        file (.dmvf) for one pixel size. See libtodmvf.
 */

#define PATH_MAX_LEN    256

static void print_usage(const char* name)
{
    Dmod_Printf("Usage: %s [options] FONT SIZE\n", name);
    Dmod_Printf("  SIZE             pixel size (em), %u ... %u\n", (unsigned)LIBTODMVF_MIN_SIZE, (unsigned)LIBTODMVF_MAX_SIZE);
    Dmod_Printf("  -o OUTPUT        the .dmvf file (default: FONT without its extension, -SIZE.dmvf)\n");
    Dmod_Printf("  -c RANGES        codepoints, e.g. 0x20-0x7E,0x104 (default: %s)\n", LIBTODMVF_DEFAULT_CHARS);
    Dmod_Printf("  -t PIXELS        letter spacing added to every advance, e.g. -2.4 (CSS letter-spacing)\n");
    Dmod_Printf("  -q               print nothing but errors\n");
}

/* Roboto-Regular.ttf, 16 -> Roboto-Regular-16.dmvf */
static void default_output(const char* input, unsigned size, char* output, size_t space)
{
    const char* dot = strrchr(input, '.');
    const char* slash = strrchr(input, '/');
    size_t len = (dot != NULL && (slash == NULL || dot > slash)) ? (size_t)(dot - input) : strlen(input);
    if (len + 16U > space)
        len = space - 16U;
    memcpy(output, input, len);
    Dmod_SnPrintf(output + len, space - len, "-%u.dmvf", size);
}

static bool parse_size(const char* s, uint8_t* size)
{
    uint32_t v = 0;
    if (*s == '\0')
        return false;
    for (; *s != '\0'; s++)
    {
        if (*s < '0' || *s > '9' || v > 1000U)
            return false;
        v = v * 10U + (uint32_t)(*s - '0');
    }
    if (v < LIBTODMVF_MIN_SIZE || v > LIBTODMVF_MAX_SIZE)
        return false;
    *size = (uint8_t)v;
    return true;
}

/* "-2.4" -> -240: pixels with up to two decimals, in 1/100 pixel */
static bool parse_tracking(const char* s, int32_t* tracking)
{
    bool minus = *s == '-';
    int32_t whole = 0, fraction = 0, scale = 10;
    if (*s == '-' || *s == '+')
        s++;
    if (*s < '0' || *s > '9')
        return false;
    for (; *s >= '0' && *s <= '9'; s++)
    {
        if (whole > LIBTODMVF_MAX_TRACKING / 100)
            return false;
        whole = whole * 10 + (*s - '0');
    }
    if (*s == '.')
    {
        for (s++; *s >= '0' && *s <= '9'; s++, scale /= 10)
            fraction += (*s - '0') * scale;     /* Digits past the second add 0 */
    }
    int32_t v = whole * 100 + fraction;
    if (*s != '\0' || v > LIBTODMVF_MAX_TRACKING)
        return false;
    *tracking = minus ? -v : v;
    return true;
}

static const char* error_text(int ret)
{
    switch (ret)
    {
        case -ENOENT:  return "cannot read the font";
        case -EBADMSG: return "not a TrueType / OpenType font";
        case -EINVAL:  return "invalid characters, or a glyph too large for a .dmvf";
        case -ENOMEM:  return "out of memory";
        default:       return "cannot write the output";
    }
}

int main(int argc, char* argv[])
{
    const char* input = NULL;
    const char* size_arg = NULL;
    const char* output = NULL;
    char default_path[PATH_MAX_LEN];
    bool quiet = false;
    libtodmvf_options_t options;
    memset(&options, 0, sizeof(options));

    for (int i = 1; i < argc; i++)
    {
        const char* a = argv[i];
        bool value = i + 1 < argc;
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0)
        {
            print_usage(argv[0]);
            return 0;
        }
        else if (strcmp(a, "-o") == 0 && value)
            output = argv[++i];
        else if (strcmp(a, "-c") == 0 && value)
            options.chars = argv[++i];
        else if (strcmp(a, "-t") == 0 && value)
        {
            if (!parse_tracking(argv[++i], &options.tracking))
            {
                Dmod_Printf("todmvf: the letter spacing must be a number of pixels, -127 ... 127, e.g. -2.4\n");
                return 1;
            }
        }
        else if (strcmp(a, "-q") == 0)
            quiet = true;
        else if (input == NULL && a[0] != '-')
            input = a;
        else if (size_arg == NULL && a[0] != '-')
            size_arg = a;
        else
        {
            print_usage(argv[0]);
            return 1;
        }
    }
    if (input == NULL || size_arg == NULL)
    {
        print_usage(argv[0]);
        return 1;
    }
    if (!parse_size(size_arg, &options.size))
    {
        Dmod_Printf("todmvf: the size must be %u ... %u\n", (unsigned)LIBTODMVF_MIN_SIZE, (unsigned)LIBTODMVF_MAX_SIZE);
        return 1;
    }
    if (output == NULL)
    {
        default_output(input, options.size, default_path, sizeof(default_path));
        output = default_path;
    }

    libtodmvf_result_t r;
    int ret = libtodmvf_convert_file(input, output, &options, &r);
    if (ret != 0)
    {
        Dmod_Printf("todmvf: %s: %s\n", input, error_text(ret));
        return 1;
    }
    if (!quiet)
    {
        Dmod_Printf("%s: %u glyphs, %u bytes", output, (unsigned)r.glyphs, (unsigned)r.size);
        if (r.missing != 0)
            Dmod_Printf(", %u not in the font", (unsigned)r.missing);
        Dmod_Printf("\n");
    }
    return 0;
}
