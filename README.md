# parbreak

Optimal paragraph line breaking in plain C11: the Knuth-Plass total-fit
algorithm that TeX uses, plus a greedy first-fit breaker for comparison.
No dependencies beyond libc and libm.

> Work in progress: breaking engine, text layout and hyphenation so far.

## The model

A paragraph is a list of items (Knuth & Plass, *Breaking Paragraphs into
Lines*, 1981):

- **box**: fixed-width material such as a word or syllable
- **glue**: breakable space with a natural width, stretch and shrink
- **penalty**: an optional break with a cost; `-PB_INFINITY` forces a break,
  `+PB_INFINITY` forbids one, and *flagged* penalties mark hyphenation points

`pb_knuth_plass` picks the breaks that minimise total demerits over the whole
paragraph (badness `100|r|^3` from each line's adjustment ratio `r`, line
penalty, break penalties, fitness-class and consecutive-hyphen demerits), with
per-line widths. If nothing fits within tolerance, an emergency pass accepts
over- or underfull lines instead of failing.

## Formatting text

```c
#include "parbreak.h"

pb_layout_opts o;
pb_layout_opts_default(&o);   /* justified, no indent */
char *out = NULL;
if (pb_format(text, strlen(text), 60, &o, NULL, 0, &out) == PB_OK) {
    fputs(out, stdout);
    free(out);
}
```

`pb_paragraph_build` turns text into items (UTF-8 aware widths, with East
Asian wide characters as two columns and combining marks as zero), allowing
breaks after explicit hyphens. Justified lines are padded to the exact
measure by spreading extra spaces by largest remainder, alternating sides
between lines. Ragged-right, ragged-left and centered text use Knuth and
Plass's ragged construction, so the right edge is evened out over the whole
paragraph instead of line by line.

## Hyphenation

`pb_hyph_en_us()` loads the TeX US English patterns (Liang's patterns as
extended by Gerard Kuiken, 4938 patterns plus exceptions) that are bundled in
`data/hyph-en-us.txt` and compiled into the library by
`tools/embed_patterns.py`. Pass it as `o.hyphenator` and words get flagged
penalties at their hyphenation points:

```c
pb_hyphenator *en = pb_hyph_en_us();
unsigned char pts[32];
pb_hyphenate(en, "hyphenation", 11, pts);   /* hy-phen-ation */
```

You can also build your own hyphenator from patterns (`pb_hyph_add_pattern`,
`pb_hyph_add_exception`, or `pb_hyph_load` for a whole file) and change the
left/right minimums. The C implementation is checked against an independent
Python one (`tools/liang_ref.py`) on 3000+ words.

## Build and test

```sh
make test       # build and run the test suite
make sanitize   # same, under AddressSanitizer + UBSan
```

The tests include 6000 random paragraphs whose optimal demerits are checked
against an exhaustive brute-force search.

## License

MIT
