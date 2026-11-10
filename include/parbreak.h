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

/* ---------------------------------------------------------------------- */
/* Liang hyphenation                                                      */
/* ---------------------------------------------------------------------- */

typedef struct pb_hyphenator pb_hyphenator;

/* An empty hyphenator (left/right minimums 2 and 3, as in TeX). */
pb_hyphenator *pb_hyph_new(void);
/* A hyphenator loaded with the bundled TeX US English patterns. */
pb_hyphenator *pb_hyph_en_us(void);
void pb_hyph_free(pb_hyphenator *h);

/* Add one Liang pattern such as ".ach4" or "hy3ph" (letters a-z and '.',
 * digits 0-9 between them). Returns PB_OK or PB_ERR_ARG / PB_ERR_NOMEM. */
int pb_hyph_add_pattern(pb_hyphenator *h, const char *pat, size_t len);
/* Add a whole-word exception such as "ta-ble" (overrides the patterns). */
int pb_hyph_add_exception(pb_hyphenator *h, const char *word, size_t len);
/* Load a pattern file: whitespace-separated tokens, '%' comments, and the
 * section markers "@patterns" / "@exceptions". On error *bad_line (if not
 * NULL) receives the 1-based line number. */
int pb_hyph_load(pb_hyphenator *h, const char *text, size_t len, size_t *bad_line);
void pb_hyph_set_mins(pb_hyphenator *h, int left, int right);
size_t pb_hyph_pattern_count(const pb_hyphenator *h);

/* Find hyphenation points in one word. points must have room for len+1
 * entries; points[i] is set to 1 when a hyphen may go before byte i. Only
 * the run of ASCII letters in the middle of the word (after stripping
 * leading/trailing punctuation) is hyphenated; words with other characters
 * inside are left alone. Returns the number of points. */
size_t pb_hyphenate(const pb_hyphenator *h, const char *word, size_t len, unsigned char *points);

/* ---------------------------------------------------------------------- */
/* Monospace text layout                                                  */
/* ---------------------------------------------------------------------- */

/* Display width of UTF-8 text in terminal columns: combining marks and
 * zero-width characters count 0, East Asian wide/fullwidth characters
 * count 2, everything else 1. Invalid bytes count 1 each. */
size_t pb_utf8_width(const char *s, size_t len);

typedef enum {
    PB_ALIGN_JUSTIFY, /* flush left and right (last line ragged) */
    PB_ALIGN_LEFT,    /* ragged right */
    PB_ALIGN_RIGHT,   /* ragged left */
    PB_ALIGN_CENTER
} pb_align;

typedef struct {
    pb_align align;
    double indent;           /* first-line indent in columns (default 0) */
    double space_stretch;    /* justified mode: stretch per space (default 1) */
    double ragged_stretch;   /* ragged modes: stretch per break (default 3) */
    double hyphen_penalty;   /* penalty for breaking after an explicit '-' (default 50) */
    const pb_hyphenator *hyphenator; /* automatic hyphenation (default NULL = off) */
    double auto_hyphen_penalty;      /* penalty for an inserted hyphen (default 50) */
} pb_layout_opts;

void pb_layout_opts_default(pb_layout_opts *o);

/* A paragraph built from text. Owns a copy of the text that its items point into. */
typedef struct {
    pb_item *items;
    size_t n;
    char *text;
    pb_align align;
} pb_paragraph;

/* Split `text` on whitespace into words and build the item list for the
 * chosen alignment, ending with \parfillskip and a forced break. Words may
 * break after an explicit hyphen ("well-|known"). */
int pb_paragraph_build(const char *text, size_t len, const pb_layout_opts *o, pb_paragraph *out);
void pb_paragraph_free(pb_paragraph *p);

/* Render broken lines as text, one '\n'-terminated line each. Justified
 * lines are padded to exactly the line width by distributing extra spaces
 * in proportion to each space's stretch (largest remainder, alternating the
 * tie direction between lines). Returns a malloc'd string or NULL. */
char *pb_render(const pb_paragraph *p, const pb_breaks *b, const double *widths, size_t nwidths);

/* Convenience: build, break with Knuth-Plass (or first-fit if
 * `first_fit`), and render one paragraph at a fixed width. */
int pb_format(const char *text, size_t len, double width, const pb_layout_opts *o,
              const pb_params *params, int first_fit, char **out);

#ifdef __cplusplus
}
#endif
#endif
