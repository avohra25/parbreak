/* parbreak - command-line paragraph formatter. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parbreak.h"

static const char *USAGE =
    "usage: parbreak [options] [file ...]\n"
    "\n"
    "Reformat paragraphs (separated by blank lines) from files or stdin.\n"
    "\n"
    "  -w, --width N         line width in columns (default 72)\n"
    "  -a, --align MODE      justify | left | right | center (default justify)\n"
    "  -i, --indent N        indent the first line of each paragraph by N columns\n"
    "  -H, --hyphenate       hyphenate with the built-in US English patterns\n"
    "  -p, --patterns FILE   hyphenate with a Liang pattern file (implies -H)\n"
    "  -g, --greedy          greedy first-fit breaking instead of Knuth-Plass\n"
    "  -t, --tolerance R     largest adjustment ratio allowed (default 2)\n"
    "  -c, --compare         show first-fit and Knuth-Plass side by side\n"
    "  -s, --stats           print line count, demerits and ratios to stderr\n"
    "  -x, --hyphens         just print each input word with its hyphenation points\n"
    "  -h, --help            show this help\n";

typedef struct {
    double width;
    pb_layout_opts layout;
    pb_params params;
    int greedy, compare, stats, hyphens_only;
    pb_hyphenator *hyph;
} options;

static char *read_all(FILE *f, size_t *len) {
    size_t cap = 4096, n = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    size_t r;
    while ((r = fread(buf + n, 1, cap - n, f)) > 0) {
        n += r;
        if (n == cap) {
            char *nb = realloc(buf, cap *= 2);
            if (!nb) {
                free(buf);
                return NULL;
            }
            buf = nb;
        }
    }
    if (ferror(f)) {
        free(buf);
        return NULL;
    }
    *len = n;
    return buf;
}

static int parse_num(const char *s, double *out) {
    char *end;
    errno = 0;
    double v = strtod(s, &end);
    if (errno || end == s || *end) return 0;
    *out = v;
    return 1;
}

static void print_stats(const pb_breaks *b, const char *label) {
    fprintf(stderr, "%s%zu lines, demerits %.0f, %zu bad lines, ratios:", label, b->lines,
            b->demerits, b->bad_lines);
    for (size_t i = 0; i < b->lines; i++) fprintf(stderr, " %.2f", b->ratios[i]);
    fputc('\n', stderr);
}

static char *layout(const options *o, const char *text, size_t len, int greedy) {
    pb_paragraph p;
    if (pb_paragraph_build(text, len, &o->layout, &p) != PB_OK) return NULL;
    pb_breaks b;
    int st = greedy ? pb_first_fit(p.items, p.n, &o->width, 1, &o->params, &b)
                    : pb_knuth_plass(p.items, p.n, &o->width, 1, &o->params, &b);
    char *out = NULL;
    if (st == PB_OK) {
        out = pb_render(&p, &b, &o->width, 1);
        if (o->stats) print_stats(&b, greedy ? "first-fit:   " : "knuth-plass: ");
    } else {
        fprintf(stderr, "parbreak: %s\n", pb_strerror(st));
    }
    pb_breaks_free(&b);
    pb_paragraph_free(&p);
    return out;
}

/* Print two renderings next to each other. */
static void side_by_side(const char *a, const char *b, int width) {
    printf("%-*s | %s\n", width, "first-fit", "knuth-plass");
    for (int i = 0; i < width; i++) putchar('-');
    printf("-+-");
    for (int i = 0; i < width; i++) putchar('-');
    putchar('\n');
    while (*a || *b) {
        const char *na = *a ? strchr(a, '\n') : a, *nb = *b ? strchr(b, '\n') : b;
        int la = (int)(na - a), lb = (int)(nb - b);
        int cols = (int)pb_utf8_width(a, (size_t)la);
        printf("%.*s%*s | %.*s\n", la, a, width > cols ? width - cols : 0, "", lb, b);
        a = *na ? na + 1 : na;
        b = *nb ? nb + 1 : nb;
    }
}

static int emit_paragraph(const options *o, const char *text, size_t len, int *first) {
    if (o->hyphens_only) {
        size_t k = 0;
        while (k < len) {
            while (k < len && strchr(" \t\r\n", text[k])) k++;
            size_t ws = k;
            while (k < len && !strchr(" \t\r\n", text[k])) k++;
            if (k == ws) break;
            unsigned char *pts = malloc(k - ws + 1);
            if (!pts) return 0;
            pb_hyphenate(o->hyph, text + ws, k - ws, pts);
            for (size_t c = ws; c < k; c++) {
                if (pts[c - ws]) putchar('-');
                putchar(text[c]);
            }
            putchar('\n');
            free(pts);
        }
        return 1;
    }
    if (!*first) putchar('\n');
    *first = 0;
    if (o->compare) {
        char *a = layout(o, text, len, 1), *b = layout(o, text, len, 0);
        int ok = a && b;
        if (ok) side_by_side(a, b, (int)o->width);
        free(a);
        free(b);
        return ok;
    }
    char *out = layout(o, text, len, o->greedy);
    if (!out) return 0;
    fputs(out, stdout);
    free(out);
    return 1;
}

/* Paragraphs are runs of lines separated by blank (or whitespace-only) lines. */
static int format_text(const options *o, const char *text, size_t len, int *first) {
    size_t i = 0, para = 0;
    int in_para = 0;
    while (i <= len) {
        size_t ls = i, le = i;
        while (le < len && text[le] != '\n') le++;
        int blank = 1;
        for (size_t k = ls; k < le; k++)
            if (text[k] != ' ' && text[k] != '\t' && text[k] != '\r') blank = 0;
        if (!blank && !in_para) {
            para = ls;
            in_para = 1;
        }
        if ((blank || le >= len) && in_para) {
            size_t end = blank ? ls : le;
            if (!emit_paragraph(o, text + para, end - para, first)) return 0;
            in_para = 0;
        }
        i = le + 1;
    }
    return 1;
}

int main(int argc, char **argv) {
    options o;
    memset(&o, 0, sizeof o);
    o.width = 72;
    pb_layout_opts_default(&o.layout);
    pb_params_default(&o.params);
    const char *patterns = NULL;
    int want_hyph = 0, nfiles = 0;
    char **files = calloc((size_t)argc, sizeof *files);
    if (!files) return 1;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *val = (i + 1 < argc) ? argv[i + 1] : NULL;
#define TAKES(s, l) ((!strcmp(a, s) || !strcmp(a, l)) && (val || (fprintf(stderr, "parbreak: %s needs a value\n", a), 0)))
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            fputs(USAGE, stdout);
            free(files);
            return 0;
        } else if (TAKES("-w", "--width")) {
            if (!parse_num(val, &o.width) || o.width < 1 || o.width > 10000) goto bad_value;
            i++;
        } else if (TAKES("-a", "--align")) {
            if (!strcmp(val, "justify")) o.layout.align = PB_ALIGN_JUSTIFY;
            else if (!strcmp(val, "left")) o.layout.align = PB_ALIGN_LEFT;
            else if (!strcmp(val, "right")) o.layout.align = PB_ALIGN_RIGHT;
            else if (!strcmp(val, "center")) o.layout.align = PB_ALIGN_CENTER;
            else goto bad_value;
            i++;
        } else if (TAKES("-i", "--indent")) {
            if (!parse_num(val, &o.layout.indent) || o.layout.indent < 0) goto bad_value;
            i++;
        } else if (TAKES("-t", "--tolerance")) {
            if (!parse_num(val, &o.params.tolerance) || o.params.tolerance < 0) goto bad_value;
            i++;
        } else if (TAKES("-p", "--patterns")) {
            patterns = val;
            want_hyph = 1;
            i++;
        } else if (!strcmp(a, "-H") || !strcmp(a, "--hyphenate")) {
            want_hyph = 1;
        } else if (!strcmp(a, "-g") || !strcmp(a, "--greedy")) {
            o.greedy = 1;
        } else if (!strcmp(a, "-c") || !strcmp(a, "--compare")) {
            o.compare = 1;
        } else if (!strcmp(a, "-s") || !strcmp(a, "--stats")) {
            o.stats = 1;
        } else if (!strcmp(a, "-x") || !strcmp(a, "--hyphens")) {
            o.hyphens_only = 1;
            want_hyph = 1;
        } else if (a[0] == '-' && a[1]) {
            if (strcmp(a, "-w") && strcmp(a, "--width") && strcmp(a, "-a") && strcmp(a, "--align") &&
                strcmp(a, "-i") && strcmp(a, "--indent") && strcmp(a, "-t") && strcmp(a, "--tolerance") &&
                strcmp(a, "-p") && strcmp(a, "--patterns"))
                fprintf(stderr, "parbreak: unknown option %s\n%s", a, USAGE);
            free(files);
            return 2;
        } else {
            files[nfiles++] = argv[i];
        }
        continue;
    bad_value:
        fprintf(stderr, "parbreak: bad value for %s: %s\n", a, val);
        free(files);
        return 2;
    }

    if (want_hyph) {
        if (patterns) {
            FILE *f = fopen(patterns, "rb");
            size_t len = 0, line = 0;
            char *data = f ? read_all(f, &len) : NULL;
            if (f) fclose(f);
            o.hyph = pb_hyph_new();
            if (!data || !o.hyph) {
                fprintf(stderr, "parbreak: cannot read %s\n", patterns);
                free(data);
                free(files);
                pb_hyph_free(o.hyph);
                return 1;
            }
            int st = pb_hyph_load(o.hyph, data, len, &line);
            free(data);
            if (st != PB_OK) {
                fprintf(stderr, "parbreak: %s:%zu: bad pattern\n", patterns, line);
                pb_hyph_free(o.hyph);
                free(files);
                return 1;
            }
        } else {
            o.hyph = pb_hyph_en_us();
        }
        o.layout.hyphenator = o.hyph;
    }

    int ok = 1, first = 1;
    for (int k = 0; k < (nfiles ? nfiles : 1) && ok; k++) {
        FILE *f = nfiles && strcmp(files[k], "-") ? fopen(files[k], "rb") : stdin;
        if (!f) {
            fprintf(stderr, "parbreak: cannot open %s: %s\n", files[k], strerror(errno));
            ok = 0;
            break;
        }
        size_t len = 0;
        char *text = read_all(f, &len);
        if (f != stdin) fclose(f);
        if (!text) {
            fprintf(stderr, "parbreak: read error\n");
            ok = 0;
            break;
        }
        ok = format_text(&o, text, len, &first);
        free(text);
    }
    pb_hyph_free(o.hyph);
    free(files);
    return ok ? 0 : 1;
}
