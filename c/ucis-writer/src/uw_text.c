/* uw_text.c - XML escaping and text sanitisation.
 * SPDX-License-Identifier: Apache-2.0 */

#include "uw_text.h"

#include <string.h>

enum {
    UW_CH_PLAIN  = 0,
    UW_CH_AMP    = 1,   /* &  */
    UW_CH_LT     = 2,   /* <  */
    UW_CH_GT     = 3,   /* >  */
    UW_CH_QUOT   = 4,   /* "  */
    UW_CH_APOS   = 5,   /* '  */
    UW_CH_NUMREF = 6,   /* tab/LF/CR: legal, but must be a char ref inside an
                         * attribute or the parser normalises it to a space */
    UW_CH_BAD    = 7    /* C0 control that XML 1.0 cannot represent at all */
};

/* Classification of the 128 ASCII bytes. Bytes >= 0x80 are handled by the
 * UTF-8 path and are not in this table. */
static const unsigned char uw_class_ascii[128] = {
    /* 0x00 */ 7, 7, 7, 7, 7, 7, 7, 7, 7, 6, 6, 7, 7, 6, 7, 7,
    /* 0x10 */ 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    /* 0x20 */ 0, 0, 4, 0, 0, 0, 1, 5, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x30 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 3, 0,
    /* 0x40 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x50 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x60 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x70 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

#if UCIS_WRITER_UTF8_CHECK
/* Length of the valid UTF-8 sequence starting at s[0], or 0 if the bytes there
 * are not one. Rejects overlong forms, surrogates (D800-DFFF, which are not
 * legal XML characters), and anything above U+10FFFF. */
static size_t uw_utf8_len(const unsigned char* s, size_t avail)
{
    unsigned char c0 = s[0];
    unsigned long cp;
    size_t        need;
    size_t        i;

    if (c0 < 0xC2u) {
        return 0;                       /* continuation byte, or overlong 2-byte */
    } else if (c0 < 0xE0u) {
        need = 2; cp = (unsigned long)(c0 & 0x1Fu);
    } else if (c0 < 0xF0u) {
        need = 3; cp = (unsigned long)(c0 & 0x0Fu);
    } else if (c0 < 0xF5u) {
        need = 4; cp = (unsigned long)(c0 & 0x07u);
    } else {
        return 0;
    }
    if (avail < need) {
        return 0;
    }
    for (i = 1; i < need; ++i) {
        if ((s[i] & 0xC0u) != 0x80u) {
            return 0;
        }
        cp = (cp << 6) | (unsigned long)(s[i] & 0x3Fu);
    }
    if (need == 3 && cp < 0x800ul) {
        return 0;                       /* overlong */
    }
    if (need == 4 && cp < 0x10000ul) {
        return 0;                       /* overlong */
    }
    if (cp >= 0xD800ul && cp <= 0xDFFFul) {
        return 0;                       /* surrogate: not an XML character */
    }
    if (cp > 0x10FFFFul || cp == 0xFFFEul || cp == 0xFFFFul) {
        return 0;
    }
    return need;
}
#endif /* UCIS_WRITER_UTF8_CHECK */

UW_INTERNAL int uw_text_escape(uw_buf_t* b, const char* s, size_t n,
                               unsigned long* nwarn)
{
    size_t i = 0;
    size_t run = 0;   /* start of the current copy-verbatim span */

    if (s == NULL || b->status != UCIS_WRITER_OK) {
        return b->status;
    }

    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        unsigned      cls;
        const char*   rep;
        size_t        replen;

        if (c >= 0x80u) {
#if UCIS_WRITER_UTF8_CHECK
            size_t seq = uw_utf8_len((const unsigned char*)s + i, n - i);
            if (seq != 0) {
                i += seq;               /* valid: stays in the verbatim span */
                continue;
            }
            cls = UW_CH_BAD;
#else
            i++;
            continue;
#endif
        } else {
            cls = uw_class_ascii[c];
            if (cls == UW_CH_PLAIN) {
                i++;
                continue;
            }
        }

        /* Emit everything up to here verbatim, then the replacement. */
        if (i > run) {
            if (uw_buf_write(b, s + run, i - run) != UCIS_WRITER_OK) {
                return b->status;
            }
        }
        switch (cls) {
            case UW_CH_AMP:  rep = "&amp;";  replen = 5; break;
            case UW_CH_LT:   rep = "&lt;";   replen = 4; break;
            case UW_CH_GT:   rep = "&gt;";   replen = 4; break;
            case UW_CH_QUOT: rep = "&quot;"; replen = 6; break;
            case UW_CH_APOS: rep = "&apos;"; replen = 6; break;
            case UW_CH_NUMREF:
                rep = (c == 0x09u) ? "&#9;" : (c == 0x0Au) ? "&#10;" : "&#13;";
                replen = strlen(rep);
                break;
            default:
                /* Not representable. U+FFFD would be friendlier but widens the
                 * byte, and these strings are keys that consumers match on;
                 * a fixed-width '?' keeps offsets predictable. */
                rep = "?";
                replen = 1;
                if (nwarn) {
                    (*nwarn)++;
                }
                break;
        }
        if (uw_buf_write(b, rep, replen) != UCIS_WRITER_OK) {
            return b->status;
        }
        i++;
        run = i;
    }
    if (i > run) {
        return uw_buf_write(b, s + run, i - run);
    }
    return b->status;
}

UW_INTERNAL int uw_text_escape_cstr(uw_buf_t* b, const char* s,
                                    unsigned long* nwarn)
{
    if (s == NULL) {
        return b->status;
    }
    return uw_text_escape(b, s, strlen(s), nwarn);
}

UW_INTERNAL int uw_text_attr(uw_buf_t* b, const char* name, const char* value,
                             unsigned long* nwarn)
{
    if (value == NULL) {
        return b->status;
    }
    if (uw_buf_putc(b, ' ') != UCIS_WRITER_OK) { return b->status; }
    if (uw_buf_puts(b, name) != UCIS_WRITER_OK) { return b->status; }
    if (UW_LIT(b, "=\"") != UCIS_WRITER_OK) { return b->status; }
    if (uw_text_escape_cstr(b, value, nwarn) != UCIS_WRITER_OK) { return b->status; }
    return uw_buf_putc(b, '"');
}

UW_INTERNAL int uw_text_attr_n(uw_buf_t* b, const char* name, const char* value,
                               size_t len, unsigned long* nwarn)
{
    if (value == NULL) {
        return b->status;
    }
    if (uw_buf_putc(b, ' ') != UCIS_WRITER_OK) { return b->status; }
    if (uw_buf_puts(b, name) != UCIS_WRITER_OK) { return b->status; }
    if (UW_LIT(b, "=\"") != UCIS_WRITER_OK) { return b->status; }
    if (uw_text_escape(b, value, len, nwarn) != UCIS_WRITER_OK) { return b->status; }
    return uw_buf_putc(b, '"');
}

UW_INTERNAL int uw_text_attr_u64(uw_buf_t* b, const char* name, uint64_t value)
{
    if (uw_buf_putc(b, ' ') != UCIS_WRITER_OK) { return b->status; }
    if (uw_buf_puts(b, name) != UCIS_WRITER_OK) { return b->status; }
    if (UW_LIT(b, "=\"") != UCIS_WRITER_OK) { return b->status; }
    if (uw_buf_u64(b, value) != UCIS_WRITER_OK) { return b->status; }
    return uw_buf_putc(b, '"');
}

UW_INTERNAL int uw_text_attr_i64(uw_buf_t* b, const char* name, int64_t value)
{
    if (uw_buf_putc(b, ' ') != UCIS_WRITER_OK) { return b->status; }
    if (uw_buf_puts(b, name) != UCIS_WRITER_OK) { return b->status; }
    if (UW_LIT(b, "=\"") != UCIS_WRITER_OK) { return b->status; }
    if (uw_buf_i64(b, value) != UCIS_WRITER_OK) { return b->status; }
    return uw_buf_putc(b, '"');
}

UW_INTERNAL int uw_text_attr_double(uw_buf_t* b, const char* name, double value)
{
    if (uw_buf_putc(b, ' ') != UCIS_WRITER_OK) { return b->status; }
    if (uw_buf_puts(b, name) != UCIS_WRITER_OK) { return b->status; }
    if (UW_LIT(b, "=\"") != UCIS_WRITER_OK) { return b->status; }
    if (uw_buf_double(b, value) != UCIS_WRITER_OK) { return b->status; }
    return uw_buf_putc(b, '"');
}
