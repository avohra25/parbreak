#include <stdlib.h>
#include <string.h>

#include "parbreak.h"
#include "test.h"

static pb_hyphenator *en;

/* Render word with '-' at each hyphenation point. */
static void show(const pb_hyphenator *h, const char *w, char *out) {
    unsigned char pts[256];
    size_t n = strlen(w), k = 0;
    pb_hyphenate(h, w, n, pts);
    for (size_t i = 0; i < n; i++) {
        if (pts[i]) out[k++] = '-';
        out[k++] = w[i];
    }
    out[k] = '\0';
}

static int hy_is(const pb_hyphenator *h, const char *w, const char *expect) {
    char buf[512];
    show(h, w, buf);
    if (strcmp(buf, expect) != 0) fprintf(stderr, "  %s -> %s (expected %s)\n", w, buf, expect);
    return strcmp(buf, expect) == 0;
}

static void test_liang_thesis_style_patterns(void) {
    /* Hand-made patterns: odd values allow, even values inhibit, max wins. */
    pb_hyphenator *h = pb_hyph_new();
    CHECK(h != NULL);
    pb_hyph_set_mins(h, 1, 1);
    CHECK(pb_hyph_add_pattern(h, "a1b", 3) == PB_OK);
    CHECK(pb_hyph_pattern_count(h) == 1);
    CHECK(hy_is(h, "abab", "a-ba-b"));
    CHECK(pb_hyph_add_pattern(h, "2b.", 3) == PB_OK); /* not before a final b */
    CHECK(hy_is(h, "abab", "a-bab"));
    CHECK(pb_hyph_add_pattern(h, ".a3", 3) == PB_OK); /* 3 beats the 1 after the first a */
    CHECK(hy_is(h, "abab", "a-bab"));
    CHECK(pb_hyph_add_pattern(h, "a4b", 3) == PB_OK); /* replaces a1b */
    CHECK(pb_hyph_pattern_count(h) == 3);
    CHECK(hy_is(h, "abab", "abab"));
    pb_hyph_free(h);
}

static void test_pattern_syntax_errors(void) {
    pb_hyphenator *h = pb_hyph_new();
    CHECK(pb_hyph_add_pattern(h, "", 0) == PB_ERR_ARG);
    CHECK(pb_hyph_add_pattern(h, "12a", 3) == PB_ERR_ARG);  /* two digits in a row */
    CHECK(pb_hyph_add_pattern(h, "a.b", 3) == PB_ERR_ARG);  /* dot in the middle */
    CHECK(pb_hyph_add_pattern(h, "aB1", 3) == PB_ERR_ARG);  /* uppercase */
    CHECK(pb_hyph_add_pattern(h, "4", 1) == PB_ERR_ARG);    /* no letters */
    CHECK(pb_hyph_add_exception(h, "-ab", 3) == PB_ERR_ARG);
    CHECK(pb_hyph_add_exception(h, "ab-", 3) == PB_ERR_ARG);
    CHECK(pb_hyph_add_exception(h, "a--b", 4) == PB_ERR_ARG);
    size_t bad = 0;
    const char *file = "% comment\n@patterns\n.ach4 hy3ph\n\n@exceptions\nta-ble\n@patterns\nx!y\n";
    CHECK(pb_hyph_load(h, file, strlen(file), &bad) == PB_ERR_ARG);
    CHECK(bad == 8);
    CHECK(pb_hyph_pattern_count(h) == 2);
    pb_hyph_free(h);
}

static void test_exceptions_override_patterns(void) {
    pb_hyphenator *h = pb_hyph_new();
    const char *file = "@patterns\n1b 1c 1d\n@exceptions\nAB-CDE\n";
    CHECK(pb_hyph_load(h, file, strlen(file), NULL) == PB_OK);
    pb_hyph_set_mins(h, 1, 1);
    CHECK(hy_is(h, "abcde", "ab-cde"));
    CHECK(hy_is(h, "Abcde", "Ab-cde"));  /* case-insensitive lookup, case kept */
    CHECK(hy_is(h, "abcd", "a-b-c-d"));  /* not an exception: patterns apply */
    pb_hyph_free(h);
}

static void test_tex_us_english(void) {
    CHECK(pb_hyph_pattern_count(en) == 4938);
    CHECK(hy_is(en, "hyphenation", "hy-phen-ation"));
    CHECK(hy_is(en, "algorithm", "al-go-rithm"));
    CHECK(hy_is(en, "typesetting", "type-set-ting"));
    CHECK(hy_is(en, "beautiful", "beau-ti-ful"));
    CHECK(hy_is(en, "programming", "pro-gram-ming"));
    CHECK(hy_is(en, "table", "ta-ble"));          /* exception */
    CHECK(hy_is(en, "associate", "as-so-ciate")); /* exception */
    CHECK(hy_is(en, "present", "present"));       /* exception: no hyphens */
    CHECK(hy_is(en, "democrat", "de-mo-c-rat"));  /* the patterns' documented known bug */
    CHECK(hy_is(en, "computer", "com-puter"));    /* right minimum 3 blocks com-put-er */
    CHECK(hy_is(en, "Paragraphs", "Para-graphs"));
    CHECK(hy_is(en, "\"forest,\"", "\"for-est,\""));
    CHECK(hy_is(en, "king's", "king's"));         /* inner punctuation: left alone */
    CHECK(hy_is(en, "caf\xc3\xa9s", "caf\xc3\xa9s"));
    CHECK(hy_is(en, "cat", "cat"));
}

/* 3000+ words checked against the independent Python implementation in
 * tools/liang_ref.py (tests/data/hyphen-vectors.txt). */
static void test_reference_vectors(void) {
    FILE *f = fopen("tests/data/hyphen-vectors.txt", "r");
    CHECK(f != NULL);
    if (!f) return;
    char line[256], word[256];
    int n = 0, bad = 0;
    while (fgets(line, sizeof line, f)) {
        size_t len = strcspn(line, "\r\n");
        line[len] = '\0';
        size_t k = 0;
        for (size_t i = 0; i < len; i++)
            if (line[i] != '-') word[k++] = line[i];
        word[k] = '\0';
        if (!hy_is(en, word, line)) bad++;
        n++;
    }
    fclose(f);
    CHECK(n > 3000);
    CHECK(bad == 0);
}

static void test_hyphenated_layout(void) {
    const char *t = "Typesetting algorithms distribute hyphenation opportunities "
                    "throughout paragraphs, producing beautifully balanced justification.";
    pb_layout_opts o;
    pb_layout_opts_default(&o);
    o.hyphenator = en;
    char *with = NULL, *without = NULL;
    CHECK(pb_format(t, strlen(t), 20, &o, NULL, 0, &with) == PB_OK);
    o.hyphenator = NULL;
    CHECK(pb_format(t, strlen(t), 20, &o, NULL, 0, &without) == PB_OK);
    /* with hyphenation every line can be set exactly to 20 columns */
    int hyphens = 0;
    size_t lines = 0;
    for (char *p = with; *p;) {
        char *nl = strchr(p, '\n');
        size_t len = (size_t)(nl - p);
        if (nl[1] != '\0') CHECK(len == 20);
        if (len && p[len - 1] == '-') hyphens++;
        lines++;
        p = nl + 1;
    }
    CHECK(hyphens >= 1);
    CHECK(lines >= 5);
    /* un-hyphenating the output gives back the words */
    char joined[512] = "";
    for (char *p = with; *p;) {
        char *nl = strchr(p, '\n');
        size_t len = (size_t)(nl - p);
        char seg[128];
        memcpy(seg, p, len);
        seg[len] = '\0';
        int cont = len && seg[len - 1] == '-' && nl[1] != '\0';
        if (cont) seg[len - 1] = '\0';
        strcat(joined, seg);
        if (!cont) strcat(joined, " ");
        p = nl + 1;
    }
    char norm[512];
    size_t k = 0;
    for (size_t i = 0; joined[i]; i++)
        if (!(joined[i] == ' ' && (k == 0 || norm[k - 1] == ' '))) norm[k++] = joined[i];
    while (k && norm[k - 1] == ' ') k--;
    norm[k] = '\0';
    CHECK(strcmp(norm, t) == 0);
    free(with);
    free(without);
}

static int hyphen_stacks(const char *out) {
    int prev = 0, stacks = 0;
    for (const char *q = out; *q;) {
        const char *nl = strchr(q, '\n');
        int h = nl > q && nl[-1] == '-';
        stacks += h && prev;
        prev = h;
        q = nl + 1;
    }
    return stacks;
}

/* Raising the consecutive-hyphen demerits never produces more stacked
 * hyphens, and removes some in this hyphen-hungry text. */
static void test_flagged_demerits_reduce_hyphen_stacks(void) {
    const char *t = "international organizations communicate electronically "
                    "notwithstanding considerable administrative complications "
                    "regarding interoperability standardization";
    pb_layout_opts o;
    pb_layout_opts_default(&o);
    o.hyphenator = en;
    pb_params lo, hi;
    pb_params_default(&lo);
    pb_params_default(&hi);
    lo.flagged_demerits = 0;
    hi.flagged_demerits = 1e7;
    lo.tolerance = hi.tolerance = 10;
    int total_lo = 0, total_hi = 0;
    for (int w = 14; w <= 40; w++) {
        char *a = NULL, *b = NULL;
        CHECK(pb_format(t, strlen(t), w, &o, &lo, 0, &a) == PB_OK);
        CHECK(pb_format(t, strlen(t), w, &o, &hi, 0, &b) == PB_OK);
        int sa = hyphen_stacks(a), sb = hyphen_stacks(b);
        CHECK(sb <= sa);
        total_lo += sa;
        total_hi += sb;
        free(a);
        free(b);
    }
    CHECK(total_hi < total_lo);
}

void run_hyphen_tests(void) {
    en = pb_hyph_en_us();
    CHECK(en != NULL);
    RUN(test_liang_thesis_style_patterns);
    RUN(test_pattern_syntax_errors);
    RUN(test_exceptions_override_patterns);
    RUN(test_tex_us_english);
    RUN(test_reference_vectors);
    RUN(test_hyphenated_layout);
    RUN(test_flagged_demerits_reduce_hyphen_stacks);
    pb_hyph_free(en);
}
