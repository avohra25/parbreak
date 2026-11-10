# parbreak

Optimal paragraph line breaking in plain C11: the Knuth-Plass total-fit
algorithm that TeX uses, Liang hyphenation with the TeX US English patterns,
UTF-8 aware monospace layout (justified, ragged right, ragged left, centered),
and a `parbreak` command-line formatter. No dependencies beyond libc and libm.

```
$ parbreak -w 30 -a left --compare examples/frog-king.txt
first-fit                      | knuth-plass
-------------------------------+-------------------------------
In olden times when wishing    | In olden times when wishing
still helped one, there lived  | still helped one, there
a king whose daughters were    | lived a king whose daughters
all beautiful; and the         | were all beautiful; and the
youngest was so beautiful that | youngest was so beautiful
the sun itself, which has seen | that the sun itself, which has
so much, was astonished        | seen so much, was astonished
whenever it shone in her face. | whenever it shone in her face.
```

Greedy breaking fills each line as far as it can and lives with whatever is
left over; total fit looks at the whole paragraph and evens out the ragged
edge (or the word spacing, when justifying).

## How it works

A paragraph is a list of items, as in Knuth & Plass, *Breaking Paragraphs
into Lines* (1981):

- **box**: fixed-width material such as a word or syllable
- **glue**: breakable space with a natural width, stretch and shrink
- **penalty**: an optional break with a cost; `-PB_INFINITY` forces a break,
  `+PB_INFINITY` forbids one, and *flagged* penalties mark hyphenation points

For each candidate line the adjustment ratio `r` says how far its glue must
stretch (`r > 0`) or shrink (`r < 0`). A line's badness is `100|r|^3`, and its
demerits are `(line_penalty + badness)^2`, plus the penalty at the break, plus
extra demerits for two hyphenated lines in a row and for adjacent lines in
incompatible fitness classes (tight, decent, loose, very loose).
`pb_knuth_plass` finds the breaks that minimise total demerits with the
classic active-node dynamic program: one best node per (line class, fitness
class) at each breakpoint, and nodes are dropped once a line from them would
be overfull. Line widths can vary per line (`widths[]`, the last entry
repeats). If no breaking fits within `tolerance`, an emergency pass accepts
over- or underfull lines rather than failing (and reports how many).

`pb_first_fit` is the greedy alternative, handy for comparison.

Text layout (`pb_paragraph_build`) turns words into boxes, measuring them in
terminal columns (East Asian wide characters count 2, combining marks 0).
Justified text uses stretchable spaces; the ragged modes use Knuth and Plass's
ragged-right construction (`glue(0, 3, 0) penalty(0) glue(1, -3, 0)`), so the
optimiser balances line lengths. `pb_render` pads justified lines to exactly
the measure, handing out leftover columns by largest remainder and
alternating sides between lines to avoid rivers of white space.

Hyphenation is Frank Liang's algorithm: patterns such as `hy3ph` or `.ach4`
live in a trie, every substring of `.word.` is matched, the largest digit
wins at each gap, and odd digits allow a hyphen. Exceptions override the
patterns, and the left/right minimums default to TeX's 2 and 3.

## CLI

```
usage: parbreak [options] [file ...]

  -w, --width N         line width in columns (default 72)
  -a, --align MODE      justify | left | right | center (default justify)
  -i, --indent N        indent the first line of each paragraph by N columns
  -H, --hyphenate       hyphenate with the built-in US English patterns
  -p, --patterns FILE   hyphenate with a Liang pattern file (implies -H)
  -g, --greedy          greedy first-fit breaking instead of Knuth-Plass
  -t, --tolerance R     largest adjustment ratio allowed (default 2)
  -c, --compare         show first-fit and Knuth-Plass side by side
  -s, --stats           print line count, demerits and ratios to stderr
  -x, --hyphens         just print each input word with its hyphenation points
```

Paragraphs are separated by blank lines. Some examples:

```
$ echo "the quick brown fox jumps over the lazy dog" | parbreak -w 16
the  quick brown
fox  jumps  over
the lazy dog

$ echo "hyphenation typesetting algorithm" | parbreak -x
hy-phen-ation
type-set-ting
al-go-rithm

$ parbreak -w 30 -H examples/frog-king.txt
In   olden  times  when  wish-
ing still  helped  one,  there
lived  a  king whose daughters
were all  beautiful;  and  the
youngest was so beautiful that
the sun itself, which has seen
so  much, was astonished when-
ever it shone in her face.
...
```

## Library

```c
#include "parbreak.h"

pb_hyphenator *en = pb_hyph_en_us();
pb_layout_opts o;
pb_layout_opts_default(&o);          /* justified, no indent */
o.hyphenator = en;

char *out = NULL;
if (pb_format(text, strlen(text), 60, &o, NULL, 0, &out) == PB_OK) {
    fputs(out, stdout);
    free(out);
}
pb_hyph_free(en);
```

For full control, build your own item list (`pb_box`, `pb_glue`,
`pb_penalty`) and call `pb_knuth_plass` with per-line widths and your own
`pb_params` (tolerance, line penalty, flagged and fitness demerits). The
whole API is in [`include/parbreak.h`](include/parbreak.h).

## Build and test

```sh
make            # build/libparbreak.a and build/parbreak
make test       # unit tests + CLI tests
make sanitize   # the same under AddressSanitizer and UBSan
```

The test suite includes:

- 6000 random paragraphs (variable line widths, hyphen penalties, negative
  penalties, forced breaks) whose optimal demerits are checked against an
  exhaustive brute-force search, including the infeasible cases
- 2000 random paragraphs in emergency mode, checking every result is a valid
  breaking that honours every forced break
- 3000+ words hyphenated by the C code and by an independent Python
  implementation (`tools/liang_ref.py`), plus known TeX results such as
  `hy-phen-ation` and the exception list
- layout properties over random texts: justified lines are exactly the
  measure, words survive in order, ragged lines never exceed the width
- end-to-end CLI tests (`tests/cli_test.sh`)

The suite builds and passes with gcc and clang, with and without sanitizers.

## Layout

```
include/parbreak.h     public API
src/parbreak.c         Knuth-Plass and first-fit breaking
src/layout.c           UTF-8 widths, text to items, rendering
src/hyphen.c           Liang hyphenation (trie, patterns, exceptions)
src/hyph_en_us.c       generated: bundled patterns (tools/embed_patterns.py)
cli/main.c             the parbreak command
data/hyph-en-us.txt    TeX US English patterns (see notice in the file)
tests/                 unit tests, reference vectors, CLI tests
```

## License

MIT for the code (see `LICENSE`). The hyphenation patterns in
`data/hyph-en-us.txt` (and the generated `src/hyph_en_us.c`) are Copyright (C)
1990, 2004, 2005 Gerard D.C. Kuiken, based on Liang and Knuth's Plain TeX
patterns, and are distributed under their own permissive notice, which is
kept in the file.
