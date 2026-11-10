# parbreak

Optimal paragraph line breaking in plain C11: the Knuth-Plass total-fit
algorithm that TeX uses, plus a greedy first-fit breaker for comparison.
No dependencies beyond libc and libm.

> Work in progress: this first commit contains the core breaking engine.

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

## Build and test

```sh
make test       # build and run the test suite
make sanitize   # same, under AddressSanitizer + UBSan
```

The tests include 6000 random paragraphs whose optimal demerits are checked
against an exhaustive brute-force search.

## License

MIT
