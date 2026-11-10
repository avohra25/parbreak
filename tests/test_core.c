#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "parbreak.h"
#include "test.h"

/* Build a paragraph of monospace words: each word is a box, words are
 * separated by glue(1, 1, 0.5), ending with \parfillskip + forced break. */
static size_t words_to_items(const char **words, size_t nw, pb_item *out) {
    size_t n = 0;
    for (size_t i = 0; i < nw; i++) {
        if (i) out[n++] = pb_glue(1, 1, 0.5);
        out[n++] = pb_box((double)strlen(words[i]), words[i], strlen(words[i]));
    }
    out[n++] = pb_glue(0, PB_INFINITY, 0);
    out[n++] = pb_penalty(0, -PB_INFINITY, 0);
    return n;
}

static void test_single_line(void) {
    const char *w[] = {"hello", "world"};
    pb_item items[8];
    size_t n = words_to_items(w, 2, items);
    double width = 20;
    pb_breaks b;
    CHECK(pb_knuth_plass(items, n, &width, 1, NULL, &b) == PB_OK);
    CHECK(b.lines == 1);
    CHECK(b.breaks[0] == n - 1);
    CHECK_NEAR(b.ratios[0], 0.0, 1e-12);
    CHECK(b.bad_lines == 0);
    pb_breaks_free(&b);
}

static void test_requires_forced_end(void) {
    pb_item items[2] = {pb_box(3, "abc", 3), pb_glue(1, 1, 0)};
    double width = 10;
    pb_breaks b;
    CHECK(pb_knuth_plass(items, 2, &width, 1, NULL, &b) == PB_ERR_ARG);
    CHECK(pb_first_fit(items, 2, &width, 1, NULL, &b) == PB_ERR_ARG);
    CHECK(pb_knuth_plass(NULL, 0, &width, 1, NULL, &b) == PB_ERR_ARG);
    CHECK(strcmp(pb_strerror(PB_ERR_INFEASIBLE), "no feasible line breaking within tolerance") == 0);
}

static void test_breakpoints_and_line_start(void) {
    pb_item items[] = {pb_box(2, "aa", 2), pb_glue(1, 1, 0), pb_glue(1, 1, 0),
                       pb_penalty(0, 50, 0), pb_box(2, "bb", 2), pb_penalty(0, PB_INFINITY, 0),
                       pb_glue(1, 1, 0), pb_glue(0, PB_INFINITY, 0), pb_penalty(0, -PB_INFINITY, 0)};
    size_t n = sizeof items / sizeof items[0];
    CHECK(!pb_is_breakpoint(items, n, 0));  /* box */
    CHECK(pb_is_breakpoint(items, n, 1));   /* glue after box */
    CHECK(!pb_is_breakpoint(items, n, 2));  /* glue after glue */
    CHECK(pb_is_breakpoint(items, n, 3));   /* finite penalty */
    CHECK(!pb_is_breakpoint(items, n, 5));  /* forbidden */
    CHECK(!pb_is_breakpoint(items, n, 6));  /* glue after penalty */
    CHECK(pb_is_breakpoint(items, n, 8));
    CHECK(pb_line_start(items, n, (size_t)-1) == 0);
    CHECK(pb_line_start(items, n, 1) == 4); /* skips glue, glue, penalty */
    CHECK(pb_line_start(items, n, 5) == 8); /* stops at the forced break */
}

static void test_ratio_and_fitness(void) {
    pb_item items[] = {pb_box(4, "aaaa", 4), pb_glue(2, 3, 1), pb_box(4, "bbbb", 4),
                       pb_penalty(1, 50, 1)};
    CHECK_NEAR(pb_ratio(items, 0, 3, 13), 2.0 / 3.0, 1e-12); /* L = 4+2+4 + penalty width 1 */
    CHECK_NEAR(pb_ratio(items, 0, 3, 11), 0.0, 1e-12);
    CHECK_NEAR(pb_ratio(items, 0, 3, 10), -1.0, 1e-12);
    CHECK(pb_ratio(items, 0, 3, 9) < -1.0);
    pb_item rigid[] = {pb_box(4, "aaaa", 4), pb_penalty(0, 0, 0)};
    CHECK(pb_ratio(rigid, 0, 1, 8) >= PB_INFINITY);
    CHECK(pb_ratio(rigid, 0, 1, 3) <= -PB_INFINITY);
    CHECK(pb_fitness(-0.9) == 0);
    CHECK(pb_fitness(-0.5) == 1);
    CHECK(pb_fitness(0.5) == 1);
    CHECK(pb_fitness(0.7) == 2);
    CHECK(pb_fitness(1.0) == 2);
    CHECK(pb_fitness(1.5) == 3);
}

static void test_forced_breaks_are_taken(void) {
    pb_item items[] = {pb_box(3, "one", 3), pb_penalty(0, -PB_INFINITY, 0),
                       pb_box(3, "two", 3), pb_glue(0, PB_INFINITY, 0),
                       pb_penalty(0, -PB_INFINITY, 0)};
    double width = 40;
    pb_breaks b;
    /* The first line has no stretch, so only emergency mode can set it. */
    pb_params p;
    pb_params_default(&p);
    p.emergency = 0;
    CHECK(pb_knuth_plass(items, 5, &width, 1, &p, &b) == PB_ERR_INFEASIBLE);
    CHECK(b.lines == 0 && b.breaks == NULL);
    p.emergency = 1;
    CHECK(pb_knuth_plass(items, 5, &width, 1, &p, &b) == PB_OK);
    CHECK(b.lines == 2 && b.breaks[0] == 1 && b.breaks[1] == 4);
    CHECK(b.bad_lines == 1);
    pb_breaks_free(&b);
    CHECK(pb_first_fit(items, 5, &width, 1, &p, &b) == PB_OK);
    CHECK(b.lines == 2 && b.breaks[0] == 1);
    pb_breaks_free(&b);
}

static void test_overfull_word_emergency(void) {
    const char *w[] = {"a", "incomprehensibilities", "b"};
    pb_item items[16];
    size_t n = words_to_items(w, 3, items);
    double width = 10;
    pb_breaks b;
    CHECK(pb_knuth_plass(items, n, &width, 1, NULL, &b) == PB_OK);
    CHECK(b.lines >= 2);
    CHECK(b.bad_lines >= 1);
    pb_breaks_free(&b);
    CHECK(pb_first_fit(items, n, &width, 1, NULL, &b) == PB_OK);
    CHECK(b.lines == 3);
    CHECK(b.bad_lines >= 1);
    pb_breaks_free(&b);
}

/* The classic case: greedy packs the first line tight and leaves a very
 * loose second line; total fit balances them. */
static void test_total_fit_beats_first_fit(void) {
    const char *w[] = {"aaa", "bb", "cc", "ddddd", "e", "ffff", "ggg", "hh", "iiiiii", "jj", "k", "lll"};
    pb_item items[64];
    size_t n = words_to_items(w, sizeof w / sizeof w[0], items);
    double width = 12;
    pb_breaks kp, ff;
    CHECK(pb_knuth_plass(items, n, &width, 1, NULL, &kp) == PB_OK);
    CHECK(pb_first_fit(items, n, &width, 1, NULL, &ff) == PB_OK);
    double skp = 0, sff = 0;
    for (size_t i = 0; i + 1 < kp.lines; i++) skp += kp.ratios[i] * kp.ratios[i];
    for (size_t i = 0; i + 1 < ff.lines; i++) sff += ff.ratios[i] * ff.ratios[i];
    CHECK(skp <= sff + 1e-9);
    for (size_t i = 0; i < kp.lines; i++) CHECK(kp.ratios[i] >= -1.0 && kp.ratios[i] <= 2.0);
    pb_breaks_free(&kp);
    pb_breaks_free(&ff);
}

static void test_variable_line_widths(void) {
    const char *w[] = {"xx", "xx", "xx", "xx", "xx", "xx", "xx", "xx"};
    pb_item items[32];
    size_t n = words_to_items(w, 8, items);
    double widths[] = {5, 11}; /* a narrow first line (like a drop cap) then wider */
    pb_breaks b;
    CHECK(pb_knuth_plass(items, n, widths, 2, NULL, &b) == PB_OK);
    CHECK(b.lines == 3);
    CHECK(b.breaks[0] == 3); /* "xx xx" exactly fills 5 */
    CHECK_NEAR(b.ratios[0], 0.0, 1e-12);
    pb_breaks_free(&b);
}

/* ---------------- brute force reference ------------------------------- */

typedef struct { uint64_t s; } rng;
static uint32_t next_u32(rng *r) {
    r->s ^= r->s << 13;
    r->s ^= r->s >> 7;
    r->s ^= r->s << 17;
    return (uint32_t)(r->s >> 11);
}
static int rint_(rng *r, int lo, int hi) { return lo + (int)(next_u32(r) % (uint32_t)(hi - lo + 1)); }

static size_t gen_paragraph(rng *r, pb_item *it) {
    size_t n = 0;
    int words = rint_(r, 1, 9);
    for (int w = 0; w < words; w++) {
        if (w) {
            if (rint_(r, 0, 9) == 0) {
                it[n++] = pb_penalty(0, -PB_INFINITY, 0); /* hard line break */
            } else {
                double gw = rint_(r, 1, 2);
                it[n++] = pb_glue(gw, rint_(r, 0, 4) / 2.0, rint_(r, 0, (int)gw) / 2.0);
                if (rint_(r, 0, 5) == 0) it[n++] = pb_penalty(0, rint_(r, -100, 100), 0);
            }
        }
        int parts = rint_(r, 1, 3); /* syllables joined by flagged hyphen penalties */
        for (int s = 0; s < parts; s++) {
            if (s) it[n++] = pb_penalty(1, rint_(r, 0, 1) ? 50 : 100, 1);
            it[n++] = pb_box(rint_(r, 1, 5), NULL, 0);
        }
    }
    it[n++] = pb_glue(0, PB_INFINITY, 0);
    it[n++] = pb_penalty(0, -PB_INFINITY, 0);
    return n;
}

/* Independent evaluation of a set of breaks: returns -1 if infeasible. */
static double ref_line_ratio(const pb_item *it, size_t s, size_t b, double width) {
    double L = 0, Y = 0, Z = 0;
    for (size_t i = s; i < b; i++) {
        if (it[i].kind != PB_PENALTY) L += it[i].width;
        if (it[i].kind == PB_GLUE) { Y += it[i].stretch; Z += it[i].shrink; }
    }
    if (it[b].kind == PB_PENALTY) L += it[b].width;
    if (L < width) return Y >= PB_INFINITY ? 0 : (Y > 0 ? (width - L) / Y : 1e30);
    if (L > width) return Z > 0 ? (width - L) / Z : -1e30;
    return 0;
}

static int ref_is_bp(const pb_item *it, size_t i) {
    if (it[i].kind == PB_PENALTY) return it[i].penalty < PB_INFINITY;
    return it[i].kind == PB_GLUE && i > 0 && it[i - 1].kind == PB_BOX;
}

/* Returns 1 and the minimum total demerits in *best if any feasible breaking
 * exists (demerits can be negative because of negative penalties). */
static int brute_force(const pb_item *it, size_t n, const double *widths, size_t nw,
                       const pb_params *p, double *best) {
    size_t bps[64], nb = 0;
    for (size_t i = 0; i + 1 < n; i++) if (ref_is_bp(it, i)) bps[nb++] = i;
    int found = 0;
    for (uint32_t mask = 0; mask < (1u << nb); mask++) {
        size_t chosen[65], nc = 0;
        int ok = 1;
        for (size_t k = 0; k < nb; k++) {
            int forced = it[bps[k]].kind == PB_PENALTY && it[bps[k]].penalty <= -PB_INFINITY;
            int on = (mask >> k) & 1;
            if (forced && !on) { ok = 0; break; }
            if (on) chosen[nc++] = bps[k];
        }
        if (!ok) continue;
        chosen[nc++] = n - 1;
        double total = 0;
        size_t start = 0, prevpos = (size_t)-1;
        int prevfit = 1;
        for (size_t l = 0; l < nc && ok; l++) {
            size_t b = chosen[l];
            int forced = it[b].kind == PB_PENALTY && it[b].penalty <= -PB_INFINITY;
            if (b < start || (b == start && !forced)) { ok = 0; break; }
            double width = widths[l < nw ? l : nw - 1];
            /* the start node must not have been deactivated by an earlier
             * overfull breakpoint */
            for (size_t m = start; m < b; m++) {
                if (!ref_is_bp(it, m) || (m == start)) continue;
                if (ref_line_ratio(it, start, m, width) < -1) { ok = 0; break; }
            }
            if (!ok) break;
            double r = ref_line_ratio(it, start, b, width);
            if (r < -1 || r > p->tolerance) { ok = 0; break; }
            double bad = 100 * fabs(r) * fabs(r) * fabs(r);
            double d = (p->line_penalty + bad) * (p->line_penalty + bad);
            double pen = it[b].kind == PB_PENALTY ? it[b].penalty : 0;
            if (pen >= 0) d += pen * pen;
            else if (pen > -PB_INFINITY) d -= pen * pen;
            int fit = r < -0.5 ? 0 : r <= 0.5 ? 1 : r <= 1 ? 2 : 3;
            if (it[b].kind == PB_PENALTY && it[b].flagged && prevpos != (size_t)-1 &&
                it[prevpos].kind == PB_PENALTY && it[prevpos].flagged)
                d += p->flagged_demerits;
            if (abs(fit - prevfit) > 1) d += p->fitness_demerits;
            total += d;
            prevfit = fit;
            prevpos = b;
            start = b + 1;
            while (start < n && it[start].kind != PB_BOX &&
                   !(it[start].kind == PB_PENALTY && it[start].penalty <= -PB_INFINITY))
                start++;
        }
        if (ok && (!found || total < *best)) {
            *best = total;
            found = 1;
        }
    }
    return found;
}

static void test_matches_brute_force(void) {
    rng r = {0x9E3779B97F4A7C15ull};
    pb_params p;
    pb_params_default(&p);
    p.emergency = 0;
    int compared = 0, infeasible = 0;
    for (int iter = 0; iter < 6000; iter++) {
        pb_item it[64];
        size_t n = gen_paragraph(&r, it);
        size_t nbp = 0;
        for (size_t i = 0; i + 1 < n; i++) nbp += ref_is_bp(it, i);
        if (nbp > 16) continue;
        double widths[3];
        size_t nw = (size_t)rint_(&r, 1, 3);
        for (size_t k = 0; k < nw; k++) widths[k] = rint_(&r, 6, 16);
        p.tolerance = rint_(&r, 0, 1) ? 1.0 : 3.0;
        double ref = 0;
        int found = brute_force(it, n, widths, nw, &p, &ref);
        pb_breaks b;
        int st = pb_knuth_plass(it, n, widths, nw, &p, &b);
        if (getenv("PB_DEBUG") && (!found != (st != PB_OK) || (st == PB_OK && fabs(b.demerits - ref) > 1e-6 * (1 + fabs(ref))))) {
            fprintf(stderr, "iter %d ref=%g st=%d dem=%g tol=%g widths=", iter, ref, st, st == PB_OK ? b.demerits : -1, p.tolerance);
            for (size_t k = 0; k < nw; k++) fprintf(stderr, "%g ", widths[k]);
            fprintf(stderr, "\n");
            for (size_t i = 0; i < n; i++) fprintf(stderr, "  %zu: %d w=%g y=%g z=%g p=%g f=%d\n", i, it[i].kind, it[i].width, it[i].stretch, it[i].shrink, it[i].penalty, it[i].flagged);
            if (st == PB_OK) { for (size_t k = 0; k < b.lines; k++) fprintf(stderr, " brk %zu r=%g\n", b.breaks[k], b.ratios[k]); }
            exit(2);
        }
        if (!found) {
            CHECK(st == PB_ERR_INFEASIBLE);
            infeasible++;
        } else {
            CHECK(st == PB_OK);
            if (st == PB_OK) {
                CHECK(fabs(b.demerits - ref) <= 1e-6 * (1 + fabs(ref)));
                CHECK(b.breaks[b.lines - 1] == n - 1);
                CHECK(b.bad_lines == 0);
            }
            compared++;
        }
        pb_breaks_free(&b);
    }
    CHECK(compared > 1500);
    CHECK(infeasible > 100);
}

/* With emergency mode on, every paragraph gets a breaking and every break
 * index is a legal breakpoint, in increasing order. */
static void test_emergency_always_succeeds(void) {
    rng r = {12345};
    for (int iter = 0; iter < 2000; iter++) {
        pb_item it[64];
        size_t n = gen_paragraph(&r, it);
        double width = rint_(&r, 1, 12);
        pb_breaks kp, ff;
        CHECK(pb_knuth_plass(it, n, &width, 1, NULL, &kp) == PB_OK);
        CHECK(pb_first_fit(it, n, &width, 1, NULL, &ff) == PB_OK);
        pb_breaks *bs[2] = {&kp, &ff};
        for (int k = 0; k < 2; k++) {
            CHECK(bs[k]->lines >= 1);
            CHECK(bs[k]->breaks[bs[k]->lines - 1] == n - 1);
            for (size_t i = 0; i < bs[k]->lines; i++) {
                CHECK(pb_is_breakpoint(it, n, bs[k]->breaks[i]));
                if (i) CHECK(bs[k]->breaks[i] > bs[k]->breaks[i - 1]);
            }
            /* every forced break is used */
            for (size_t i = 0; i < n; i++) {
                if (it[i].kind == PB_PENALTY && it[i].penalty <= -PB_INFINITY) {
                    int found = 0;
                    for (size_t j = 0; j < bs[k]->lines; j++) found |= bs[k]->breaks[j] == i;
                    CHECK(found);
                }
            }
        }
        pb_breaks_free(&kp);
        pb_breaks_free(&ff);
    }
}

void run_core_tests(void) {
    RUN(test_single_line);
    RUN(test_requires_forced_end);
    RUN(test_breakpoints_and_line_start);
    RUN(test_ratio_and_fitness);
    RUN(test_forced_breaks_are_taken);
    RUN(test_overfull_word_emergency);
    RUN(test_total_fit_beats_first_fit);
    RUN(test_variable_line_widths);
    RUN(test_matches_brute_force);
    RUN(test_emergency_always_succeeds);
}
