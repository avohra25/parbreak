/*
 * parbreak - optimal paragraph line breaking (Knuth-Plass) in C11.
 *
 * A paragraph is a sequence of items in the box/glue/penalty model from
 * Knuth & Plass, "Breaking Paragraphs into Lines" (1981):
 *
 *   box      fixed-width material (a word, a syllable, an indent)
 *   glue     breakable, stretchable/shrinkable space
 *   penalty  a possible break point with an aesthetic cost; a penalty of
 *            -PB_INFINITY forces a break, +PB_INFINITY forbids one
 *
 * The last item of every paragraph must be a forced break.
 */
#ifndef PARBREAK_H
#define PARBREAK_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PB_INFINITY 10000.0

typedef enum { PB_BOX, PB_GLUE, PB_PENALTY } pb_kind;

typedef struct {
    pb_kind kind;
    double width;     /* natural width (all kinds; for penalties: width added if broken here) */
    double stretch;   /* glue only */
    double shrink;    /* glue only */
    double penalty;   /* penalty only */
    int flagged;      /* penalty only: e.g. a hyphenation point */
    const char *text; /* optional payload (boxes), not owned */
    size_t len;
} pb_item;

pb_item pb_box(double width, const char *text, size_t len);
pb_item pb_glue(double width, double stretch, double shrink);
pb_item pb_penalty(double width, double penalty, int flagged);

typedef struct {
    double tolerance;        /* largest acceptable adjustment ratio (default 2.0) */
    double line_penalty;     /* added to badness of every line (default 10) */
    double flagged_demerits; /* two flagged breaks in a row (default 3000) */
    double fitness_demerits; /* adjacent lines of incompatible tightness (default 3000) */
    int emergency;           /* if no feasible breaking exists, retry allowing
                                over/underfull lines instead of failing (default 1) */
} pb_params;

void pb_params_default(pb_params *p);

typedef struct {
    size_t *breaks;  /* item index where each line ends; breaks[lines-1] == n-1 */
    double *ratios;  /* adjustment ratio of each line */
    size_t lines;
    double demerits; /* total demerits (Knuth-Plass only; 0 for first-fit) */
    size_t bad_lines;/* lines outside [-1, tolerance] (only possible in emergency mode) */
} pb_breaks;

enum {
    PB_OK = 0,
    PB_ERR_ARG = -1,        /* bad arguments, or last item is not a forced break */
    PB_ERR_NOMEM = -2,
    PB_ERR_INFEASIBLE = -3  /* no breaking within tolerance and emergency disabled */
};

const char *pb_strerror(int status);

/* Fitness class of a line with adjustment ratio r:
 * 0 tight (r < -0.5), 1 decent (r <= 0.5), 2 loose (r <= 1), 3 very loose. */
int pb_fitness(double r);

/* Index of the first item of a line that follows a break at item `brk`
 * (discardable glue and non-forced penalties after a break are dropped).
 * Pass (size_t)-1 for the start of the paragraph. */
size_t pb_line_start(const pb_item *items, size_t n, size_t brk);

/* Adjustment ratio of the line containing items [start, end] when the
 * paragraph breaks at `end`, set to `width`. Returns +/-PB_INFINITY-ish
 * huge values when the line cannot stretch/shrink at all. */
double pb_ratio(const pb_item *items, size_t start, size_t end, double width);

/* Is item i a legal breakpoint? */
int pb_is_breakpoint(const pb_item *items, size_t n, size_t i);

/* Optimal (minimum total demerits) breaking. `widths` gives the width of
 * line 1, 2, ...; the last entry repeats for all later lines. */
int pb_knuth_plass(const pb_item *items, size_t n, const double *widths,
                   size_t nwidths, const pb_params *p, pb_breaks *out);

/* Greedy first-fit breaking: each line takes as much as fits. */
int pb_first_fit(const pb_item *items, size_t n, const double *widths,
                 size_t nwidths, const pb_params *p, pb_breaks *out);

void pb_breaks_free(pb_breaks *b);

#ifdef __cplusplus
}
#endif
#endif
