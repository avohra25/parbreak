/* layout.c - turning text into boxes/glue/penalties and rendering lines. */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "parbreak.h"

/* ---- UTF-8 width ------------------------------------------------------- */

static int in_ranges(uint32_t c, const uint32_t (*r)[2], size_t n) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (c < r[mid][0]) hi = mid;
        else if (c > r[mid][1]) lo = mid + 1;
        else return 1;
    }
    return 0;
}

static const uint32_t zero_width[][2] = {
    {0x0300, 0x036F}, {0x0483, 0x0489}, {0x0591, 0x05BD}, {0x0610, 0x061A},
    {0x064B, 0x065F}, {0x0E31, 0x0E31}, {0x0E34, 0x0E3A}, {0x0E47, 0x0E4E},
    {0x1AB0, 0x1AFF}, {0x1DC0, 0x1DFF}, {0x200B, 0x200F}, {0x202A, 0x202E},
    {0x2060, 0x2064}, {0x20D0, 0x20FF}, {0xFE00, 0xFE0F}, {0xFE20, 0xFE2F},
    {0xFEFF, 0xFEFF}, {0xE0100, 0xE01EF},
};

static const uint32_t wide[][2] = {
    {0x1100, 0x115F}, {0x2E80, 0x303E}, {0x3041, 0x33FF}, {0x3400, 0x4DBF},
    {0x4E00, 0x9FFF}, {0xA000, 0xA4CF}, {0xAC00, 0xD7A3}, {0xF900, 0xFAFF},
    {0xFE30, 0xFE4F}, {0xFF00, 0xFF60}, {0xFFE0, 0xFFE6}, {0x1F300, 0x1F64F},
    {0x1F900, 0x1F9FF}, {0x20000, 0x2FFFD}, {0x30000, 0x3FFFD},
};

/* Decode one code point; returns bytes consumed (>= 1). Invalid -> 0xFFFD. */
static size_t utf8_decode(const unsigned char *s, size_t len, uint32_t *cp) {
    unsigned char c = s[0];
    size_t need;
    uint32_t v, min;
    if (c < 0x80) { *cp = c; return 1; }
    if ((c & 0xE0) == 0xC0) { need = 1; v = c & 0x1F; min = 0x80; }
    else if ((c & 0xF0) == 0xE0) { need = 2; v = c & 0x0F; min = 0x800; }
    else if ((c & 0xF8) == 0xF0) { need = 3; v = c & 0x07; min = 0x10000; }
    else { *cp = 0xFFFD; return 1; }
    if (need >= len) { *cp = 0xFFFD; return 1; }
    for (size_t i = 1; i <= need; i++) {
        if ((s[i] & 0xC0) != 0x80) { *cp = 0xFFFD; return 1; }
        v = (v << 6) | (s[i] & 0x3F);
    }
    if (v < min || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) { *cp = 0xFFFD; return 1; }
    *cp = v;
    return need + 1;
}

size_t pb_utf8_width(const char *s, size_t len) {
    size_t w = 0, i = 0;
    const unsigned char *u = (const unsigned char *)s;
    while (i < len) {
        uint32_t cp;
        i += utf8_decode(u + i, len - i, &cp);
        if (in_ranges(cp, zero_width, sizeof zero_width / sizeof zero_width[0])) continue;
        w += in_ranges(cp, wide, sizeof wide / sizeof wide[0]) ? 2 : 1;
    }
    return w;
}

/* ---- building ---------------------------------------------------------- */

void pb_layout_opts_default(pb_layout_opts *o) {
    o->align = PB_ALIGN_JUSTIFY;
    o->indent = 0;
    o->space_stretch = 1;
    o->ragged_stretch = 3;
    o->hyphen_penalty = 50;
    o->hyphenator = NULL;
    o->auto_hyphen_penalty = 50;
}

typedef struct {
    pb_item *items;
    size_t n, cap;
} itemvec;

static int push(itemvec *v, pb_item it) {
    if (v->n == v->cap) {
        size_t nc = v->cap ? v->cap * 2 : 32;
        pb_item *ni = realloc(v->items, nc * sizeof *ni);
        if (!ni) return 0;
        v->items = ni;
        v->cap = nc;
    }
    v->items[v->n++] = it;
    return 1;
}

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static int push_space(itemvec *v, const pb_layout_opts *o) {
    if (o->align == PB_ALIGN_JUSTIFY) return push(v, pb_glue(1, o->space_stretch, 0));
    /* Knuth & Plass's ragged-right construction: a break here keeps the
     * stretchable glue on the line; no break cancels it out. */
    return push(v, pb_glue(0, o->ragged_stretch, 0)) && push(v, pb_penalty(0, 0, 0)) &&
           push(v, pb_glue(1, -o->ragged_stretch, 0));
}

/* Push a hyphen-free piece of a word, splitting it at Liang hyphenation
 * points into boxes joined by flagged penalties that render a '-'. */
static int push_piece(itemvec *v, const char *w, size_t len, const pb_layout_opts *o) {
    if (o->hyphenator && len >= 4 && len <= 256) {
        unsigned char points[257];
        if (pb_hyphenate(o->hyphenator, w, len, points) > 0) {
            size_t start = 0;
            for (size_t i = 1; i < len; i++) {
                if (!points[i]) continue;
                if (!push(v, pb_box((double)pb_utf8_width(w + start, i - start), w + start, i - start)))
                    return 0;
                pb_item hy = pb_penalty(1, o->auto_hyphen_penalty, 1);
                hy.text = "-";
                hy.len = 1;
                if (!push(v, hy)) return 0;
                start = i;
            }
            return push(v, pb_box((double)pb_utf8_width(w + start, len - start), w + start, len - start));
        }
    }
    return push(v, pb_box((double)pb_utf8_width(w, len), w, len));
}

/* Push one word, allowing a break after each interior explicit hyphen. */
static int push_word(itemvec *v, const char *w, size_t len, const pb_layout_opts *o) {
    size_t start = 0;
    for (size_t i = 0; i < len; i++) {
        if (w[i] == '-' && i > start && w[i - 1] != '-' && i + 1 < len && w[i + 1] != '-') {
            if (!push_piece(v, w + start, i + 1 - start, o)) return 0;
            if (!push(v, pb_penalty(0, o->hyphen_penalty, 1))) return 0;
            start = i + 1;
        }
    }
    return push_piece(v, w + start, len - start, o);
}

int pb_paragraph_build(const char *text, size_t len, const pb_layout_opts *o, pb_paragraph *out) {
    if (!out || (!text && len)) return PB_ERR_ARG;
    memset(out, 0, sizeof *out);
    pb_layout_opts def;
    if (!o) {
        pb_layout_opts_default(&def);
        o = &def;
    }
    out->text = malloc(len + 1);
    if (!out->text) return PB_ERR_NOMEM;
    if (len) memcpy(out->text, text, len);
    out->text[len] = '\0';
    out->align = o->align;

    itemvec v = {0, 0, 0};
    int ok = 1;
    if (o->indent > 0) ok = push(&v, pb_box(o->indent, NULL, 0));
    size_t i = 0, words = 0;
    while (ok && i < len) {
        while (i < len && is_space(out->text[i])) i++;
        if (i >= len) break;
        size_t s = i;
        while (i < len && !is_space(out->text[i])) i++;
        if (words++ && !push_space(&v, o)) ok = 0;
        if (ok) ok = push_word(&v, out->text + s, i - s, o);
    }
    ok = ok && push(&v, pb_glue(0, PB_INFINITY, 0)) && push(&v, pb_penalty(0, -PB_INFINITY, 0));
    if (!ok) {
        free(v.items);
        free(out->text);
        memset(out, 0, sizeof *out);
        return PB_ERR_NOMEM;
    }
    out->items = v.items;
    out->n = v.n;
    return PB_OK;
}

void pb_paragraph_free(pb_paragraph *p) {
    if (!p) return;
    free(p->items);
    free(p->text);
    memset(p, 0, sizeof *p);
}

/* ---- rendering --------------------------------------------------------- */

typedef struct {
    char *s;
    size_t len, cap;
} sbuf;

static int sb_put(sbuf *b, const char *s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap : 128;
        while (b->len + n + 1 > nc) nc *= 2;
        char *ns = realloc(b->s, nc);
        if (!ns) return 0;
        b->s = ns;
        b->cap = nc;
    }
    memcpy(b->s + b->len, s, n);
    b->len += n;
    b->s[b->len] = '\0';
    return 1;
}

static int sb_spaces(sbuf *b, long n) {
    for (long i = 0; i < n; i++)
        if (!sb_put(b, " ", 1)) return 0;
    return 1;
}

static long iround(double x) { return (long)floor(x + 0.5); }

char *pb_render(const pb_paragraph *p, const pb_breaks *br, const double *widths, size_t nwidths) {
    if (!p || !br || !widths || nwidths == 0) return NULL;
    sbuf out = {0, 0, 0};
    long *extra = NULL;
    double *frac = NULL;
    if (!sb_put(&out, "", 0)) return NULL;
    size_t prev = (size_t)-1;
    for (size_t line = 0; line < br->lines; line++) {
        size_t b = br->breaks[line];
        size_t s = pb_line_start(p->items, p->n, prev);
        double width = widths[line < nwidths - 1 ? line : nwidths - 1];
        const pb_item *brk = &p->items[b];
        int hyphen = brk->kind == PB_PENALTY && brk->width > 0 && brk->penalty > -PB_INFINITY;

        /* natural width and stretch of the line */
        double L = 0, Y = 0;
        size_t nglue = 0;
        int infinite = 0;
        for (size_t i = s; i < b && i < p->n; i++) {
            const pb_item *it = &p->items[i];
            if (it->kind == PB_PENALTY) continue;
            L += it->width;
            if (it->kind == PB_GLUE) {
                if (it->stretch >= PB_INFINITY) infinite = 1;
                else Y += it->stretch;
                nglue++;
            }
        }
        if (hyphen) L += brk->width;
        long lnat = iround(L);
        long lwidth = iround(width);

        free(extra);
        free(frac);
        extra = calloc(nglue + 1, sizeof *extra);
        frac = calloc(nglue + 1, sizeof *frac);
        if (!extra || !frac) goto fail;
        int justify = p->align == PB_ALIGN_JUSTIFY && !infinite && Y > 0 && lnat < lwidth;
        if (justify) {
            long slack = lwidth - lnat, given = 0;
            size_t g = 0;
            for (size_t i = s; i < b; i++) {
                const pb_item *it = &p->items[i];
                if (it->kind != PB_GLUE) continue;
                double share = slack * it->stretch / Y;
                extra[g] = (long)floor(share);
                frac[g] = share - extra[g];
                given += extra[g];
                g++;
            }
            /* hand out the remaining columns by largest remainder; ties go
             * left on even lines and right on odd lines to avoid rivers */
            while (given < slack && nglue > 0) {
                size_t best = 0;
                if (line & 1) {
                    best = nglue - 1;
                    for (size_t k = nglue; k-- > 0;)
                        if (frac[k] > frac[best] + 1e-12) best = k;
                } else {
                    for (size_t k = 1; k < nglue; k++)
                        if (frac[k] > frac[best] + 1e-12) best = k;
                }
                extra[best]++;
                frac[best] = -1.0;
                given++;
            }
        }

        sbuf ln = {0, 0, 0};
        if (!sb_put(&ln, "", 0)) goto fail;
        size_t g = 0;
        for (size_t i = s; i < b; i++) {
            const pb_item *it = &p->items[i];
            if (it->kind == PB_BOX) {
                if (it->text) {
                    if (!sb_put(&ln, it->text, it->len)) { free(ln.s); goto fail; }
                } else if (!sb_spaces(&ln, iround(it->width))) { free(ln.s); goto fail; }
            } else if (it->kind == PB_GLUE) {
                long n = iround(it->width) + (justify ? extra[g] : 0);
                if (it->stretch < PB_INFINITY && !sb_spaces(&ln, n)) { free(ln.s); goto fail; }
                g++;
            }
        }
        if (hyphen && !sb_put(&ln, "-", 1)) { free(ln.s); goto fail; }
        /* trailing spaces never reach the output */
        while (ln.len > 0 && ln.s[ln.len - 1] == ' ') ln.s[--ln.len] = '\0';
        long used = (long)pb_utf8_width(ln.s, ln.len);
        long pad = 0;
        if (p->align == PB_ALIGN_RIGHT && used < lwidth) pad = lwidth - used;
        if (p->align == PB_ALIGN_CENTER && used < lwidth) pad = (lwidth - used) / 2;
        int ok = sb_spaces(&out, pad) && sb_put(&out, ln.s, ln.len) && sb_put(&out, "\n", 1);
        free(ln.s);
        if (!ok) goto fail;
        prev = b;
    }
    free(extra);
    free(frac);
    return out.s;
fail:
    free(extra);
    free(frac);
    free(out.s);
    return NULL;
}

int pb_format(const char *text, size_t len, double width, const pb_layout_opts *o,
              const pb_params *params, int first_fit, char **out) {
    if (!out) return PB_ERR_ARG;
    *out = NULL;
    pb_paragraph p;
    int st = pb_paragraph_build(text, len, o, &p);
    if (st != PB_OK) return st;
    pb_breaks b;
    st = first_fit ? pb_first_fit(p.items, p.n, &width, 1, params, &b)
                   : pb_knuth_plass(p.items, p.n, &width, 1, params, &b);
    if (st == PB_OK) {
        *out = pb_render(&p, &b, &width, 1);
        if (!*out) st = PB_ERR_NOMEM;
    }
    pb_breaks_free(&b);
    pb_paragraph_free(&p);
    return st;
}
