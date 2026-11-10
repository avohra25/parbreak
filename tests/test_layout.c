#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "parbreak.h"
#include "test.h"

static const char *SAMPLE =
    "In olden times when wishing still helped one, there lived a king whose "
    "daughters were all beautiful; and the youngest was so beautiful that the "
    "sun itself, which has seen so much, was astonished whenever it shone in "
    "her face. Close by the king's castle lay a great dark forest, and under an "
    "old lime-tree in the forest was a well, and when the day was very warm, "
    "the king's child went out into the forest and sat down by the side of the "
    "cool fountain; and when she was bored she took a golden ball, and threw it "
    "up on high and caught it; and this ball was her favorite plaything.";

static void test_utf8_width(void) {
    CHECK(pb_utf8_width("hello", 5) == 5);
    CHECK(pb_utf8_width("h\xc3\xa9llo", 6) == 5);         /* é precomposed */
    CHECK(pb_utf8_width("e\xcc\x81", 3) == 1);             /* e + combining acute */
    CHECK(pb_utf8_width("\xe6\x97\xa5\xe6\x9c\xac", 6) == 4); /* 日本 */
    CHECK(pb_utf8_width("\xf0\x9f\x98\x80", 4) == 2);      /* emoji */
    CHECK(pb_utf8_width("a\xffz", 3) == 3);                /* invalid byte */
    CHECK(pb_utf8_width("\xe2\x80\x8b", 3) == 0);          /* zero width space */
    CHECK(pb_utf8_width("\xc0\xaf", 2) == 2);              /* overlong: two invalid bytes */
    CHECK(pb_utf8_width("\xe6\x97", 2) == 2);              /* truncated sequence */
}

/* Split rendered text into lines (without the newline). */
static size_t split_lines(char *s, char **lines, size_t max) {
    size_t n = 0;
    char *p = s;
    while (*p && n < max) {
        char *nl = strchr(p, '\n');
        if (!nl) break;
        *nl = '\0';
        lines[n++] = p;
        p = nl + 1;
    }
    return n;
}

/* Words of the output, de-hyphenating at line ends that end in a break
 * hyphen we inserted, must reproduce the input word sequence. */
static int same_words(const char *in, char **lines, size_t n) {
    char *joined = malloc(strlen(in) * 2 + 16);
    joined[0] = '\0';
    for (size_t i = 0; i < n; i++) {
        const char *l = lines[i];
        while (*l == ' ') l++;
        if (i && joined[0]) {
            size_t jl = strlen(joined);
            /* an explicit hyphen at the end of a line joins without a space */
            if (!(jl && joined[jl - 1] == '-')) strcat(joined, " ");
        }
        strcat(joined, l);
    }
    /* normalise runs of spaces */
    char *a = malloc(strlen(in) + 1), *b = malloc(strlen(joined) + 1);
    const char *srcs[2] = {in, joined};
    char *dsts[2] = {a, b};
    for (int k = 0; k < 2; k++) {
        const char *src = srcs[k];
        char *d = dsts[k];
        int sp = 1;
        for (; *src; src++) {
            int ws = *src == ' ' || *src == '\n' || *src == '\t';
            if (ws) {
                if (!sp) *d++ = ' ';
                sp = 1;
            } else {
                *d++ = *src;
                sp = 0;
            }
        }
        if (d > dsts[k] && d[-1] == ' ') d--;
        *d = '\0';
    }
    int eq = strcmp(a, b) == 0;
    if (!eq) fprintf(stderr, "words differ:\n  in : %s\n  out: %s\n", a, b);
    free(a);
    free(b);
    free(joined);
    return eq;
}

static void test_justified_lines_are_exact(void) {
    for (int w = 24; w <= 72; w += 6) {
        char *out = NULL;
        pb_layout_opts o;
        pb_layout_opts_default(&o);
        CHECK(pb_format(SAMPLE, strlen(SAMPLE), w, &o, NULL, 0, &out) == PB_OK);
        char *lines[128];
        size_t n = split_lines(out, lines, 128);
        CHECK(n >= 2);
        for (size_t i = 0; i + 1 < n; i++) {
            CHECK((int)pb_utf8_width(lines[i], strlen(lines[i])) == w);
            CHECK(lines[i][0] != ' ');
        }
        CHECK((int)pb_utf8_width(lines[n - 1], strlen(lines[n - 1])) <= w);
        CHECK(same_words(SAMPLE, lines, n));
        free(out);
    }
}

static void test_ragged_modes(void) {
    pb_align aligns[] = {PB_ALIGN_LEFT, PB_ALIGN_RIGHT, PB_ALIGN_CENTER};
    for (int k = 0; k < 3; k++) {
        pb_layout_opts o;
        pb_layout_opts_default(&o);
        o.align = aligns[k];
        char *out = NULL;
        CHECK(pb_format(SAMPLE, strlen(SAMPLE), 40, &o, NULL, 0, &out) == PB_OK);
        char *lines[128];
        size_t n = split_lines(out, lines, 128);
        for (size_t i = 0; i < n; i++) {
            size_t len = strlen(lines[i]);
            size_t w = pb_utf8_width(lines[i], len);
            CHECK(w <= 40);
            CHECK(len == 0 || lines[i][len - 1] != ' ');
            CHECK(strstr(lines[i], "  ") == NULL || aligns[k] != PB_ALIGN_LEFT);
            if (aligns[k] == PB_ALIGN_RIGHT) CHECK(w == 40);
            if (aligns[k] == PB_ALIGN_LEFT) CHECK(lines[i][0] != ' ');
            if (aligns[k] == PB_ALIGN_CENTER) {
                size_t lead = strspn(lines[i], " ");
                CHECK(lead == (40 - (w - lead)) / 2);
            }
        }
        CHECK(same_words(SAMPLE, lines, n));
        free(out);
    }
}

/* Total fit produces a more even right edge than greedy first fit. */
static void test_ragged_total_fit_is_more_even(void) {
    pb_layout_opts o;
    pb_layout_opts_default(&o);
    o.align = PB_ALIGN_LEFT;
    for (int w = 30; w <= 50; w += 5) {
        char *kp = NULL, *ff = NULL;
        CHECK(pb_format(SAMPLE, strlen(SAMPLE), w, &o, NULL, 0, &kp) == PB_OK);
        CHECK(pb_format(SAMPLE, strlen(SAMPLE), w, &o, NULL, 1, &ff) == PB_OK);
        char *lk[128], *lf[128];
        size_t nk = split_lines(kp, lk, 128), nf = split_lines(ff, lf, 128);
        double sk = 0, sf = 0;
        for (size_t i = 0; i + 1 < nk; i++) {
            double slack = w - (double)strlen(lk[i]);
            sk += slack * slack * slack;
        }
        for (size_t i = 0; i + 1 < nf; i++) {
            double slack = w - (double)strlen(lf[i]);
            sf += slack * slack * slack;
        }
        CHECK(sk <= sf);
        free(kp);
        free(ff);
    }
}

static void test_explicit_hyphen_break(void) {
    const char *t = "a well-known name";
    pb_paragraph p;
    CHECK(pb_paragraph_build(t, strlen(t), NULL, &p) == PB_OK);
    /* a, glue, "well-", penalty(flagged), "known", glue, "name", fill, forced */
    CHECK(p.n == 9);
    CHECK(p.items[2].kind == PB_BOX && p.items[2].len == 5 && memcmp(p.items[2].text, "well-", 5) == 0);
    CHECK(p.items[3].kind == PB_PENALTY && p.items[3].flagged && p.items[3].width == 0);
    pb_paragraph_free(&p);

    char *out = NULL;
    CHECK(pb_format(t, strlen(t), 7, NULL, NULL, 0, &out) == PB_OK);
    CHECK(strcmp(out, "a well-\nknown\nname\n") == 0);
    free(out);
    /* leading, trailing and doubled hyphens are not break points */
    CHECK(pb_paragraph_build("-x- a--b", 8, NULL, &p) == PB_OK);
    for (size_t i = 0; i < p.n; i++) CHECK(!(p.items[i].kind == PB_PENALTY && p.items[i].flagged));
    pb_paragraph_free(&p);
}

static void test_indent_and_empty(void) {
    pb_layout_opts o;
    pb_layout_opts_default(&o);
    o.indent = 4;
    o.align = PB_ALIGN_LEFT;
    char *out = NULL;
    CHECK(pb_format("one two three", 13, 12, &o, NULL, 0, &out) == PB_OK);
    CHECK(strncmp(out, "    one two\n", 12) == 0);
    free(out);
    CHECK(pb_format("", 0, 10, NULL, NULL, 0, &out) == PB_OK);
    CHECK(strcmp(out, "\n") == 0);
    free(out);
    CHECK(pb_format("   \n\t ", 6, 10, NULL, NULL, 0, &out) == PB_OK);
    CHECK(strcmp(out, "\n") == 0);
    free(out);
}

static void test_known_justified_output(void) {
    const char *t = "the quick brown fox jumps over the lazy dog";
    char *out = NULL;
    CHECK(pb_format(t, strlen(t), 16, NULL, NULL, 0, &out) == PB_OK);
    /* even lines put leftover spaces on the left, odd lines on the right */
    CHECK(strcmp(out, "the  quick brown\nfox  jumps  over\nthe lazy dog\n") == 0);
    if (strcmp(out, "the  quick brown\nfox  jumps  over\nthe lazy dog\n") != 0) fprintf(stderr, "%s", out);
    free(out);
}

static void test_utf8_text_layout(void) {
    const char *t = "caf\xc3\xa9 na\xc3\xafve r\xc3\xa9sum\xc3\xa9 \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e d\xc3\xa9j\xc3\xa0 vu";
    char *out = NULL;
    CHECK(pb_format(t, strlen(t), 12, NULL, NULL, 0, &out) == PB_OK);
    char *lines[16];
    size_t n = split_lines(out, lines, 16);
    CHECK(n == 4);
    CHECK(strcmp(lines[2], "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e  d\xc3\xa9j\xc3\xa0") == 0);
    /* every multi-word line but the last is justified to exactly 12 columns */
    for (size_t i = 0; i + 1 < n; i++)
        if (strchr(lines[i], ' ')) CHECK(pb_utf8_width(lines[i], strlen(lines[i])) == 12);
    free(out);
}

/* Random texts in every alignment: words survive and no line is too wide
 * unless a single word is wider than the measure. */
static void test_random_texts(void) {
    uint64_t s = 0xC0FFEE;
    for (int iter = 0; iter < 400; iter++) {
        char text[600];
        size_t len = 0;
        int nwords = 1 + (int)(s % 60);
        size_t longest = 0;
        for (int w = 0; w < nwords; w++) {
            s ^= s << 13; s ^= s >> 7; s ^= s << 17;
            int wl = 1 + (int)(s % 11);
            if ((size_t)wl > longest) longest = (size_t)wl;
            for (int c = 0; c < wl; c++) text[len++] = (char)('a' + (s >> (c * 3)) % 26);
            text[len++] = (s & 64) ? '\n' : ' ';
        }
        text[len] = '\0';
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        int width = 8 + (int)(s % 40);
        pb_layout_opts o;
        pb_layout_opts_default(&o);
        o.align = (pb_align)(s % 4);
        char *out = NULL;
        CHECK(pb_format(text, len, width, &o, NULL, (int)((s >> 8) & 1), &out) == PB_OK);
        char *lines[400];
        size_t n = split_lines(out, lines, 400);
        for (size_t i = 0; i < n; i++) {
            size_t w = strlen(lines[i]);
            CHECK(w <= (size_t)width || longest > (size_t)width);
            if (o.align == PB_ALIGN_JUSTIFY && i + 1 < n && longest <= (size_t)width) {
                /* a justified line is exact unless it holds a single word */
                if (strchr(lines[i], ' ')) CHECK(w == (size_t)width);
            }
        }
        CHECK(same_words(text, lines, n));
        free(out);
    }
}

void run_layout_tests(void) {
    RUN(test_utf8_width);
    RUN(test_justified_lines_are_exact);
    RUN(test_ragged_modes);
    RUN(test_ragged_total_fit_is_more_even);
    RUN(test_explicit_hyphen_break);
    RUN(test_indent_and_empty);
    RUN(test_known_justified_output);
    RUN(test_utf8_text_layout);
    RUN(test_random_texts);
}
