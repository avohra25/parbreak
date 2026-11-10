#!/usr/bin/env python3
"""Independent reference implementation of Liang hyphenation in Python.

Used to generate tests/data/hyphen-vectors.txt, which the C test suite
checks pb_hyphenate() against:

    python3 tools/liang_ref.py --vectors > tests/data/hyphen-vectors.txt

or hyphenate words given on stdin:

    echo "hyphenation algorithm" | python3 tools/liang_ref.py
"""
import pathlib
import random
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
LEFT, RIGHT = 2, 3


def load(path):
    pats, exc, mode = {}, {}, "p"
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.split("%")[0].strip()
        if not line:
            continue
        if line == "@patterns":
            mode = "p"
        elif line == "@exceptions":
            mode = "e"
        elif mode == "p":
            key = re.sub(r"\d", "", line)
            vals, k = [0] * (len(key) + 1), 0
            for c in line:
                if c.isdigit():
                    vals[k] = int(c)
                else:
                    k += 1
            pats[key] = vals
        else:
            exc[line.replace("-", "")] = line
    return pats, exc


def hyphenate(word, pats, exc):
    lw = word.lower()
    if len(lw) < LEFT + RIGHT:
        return word
    if lw in exc:
        marks = [False] * (len(lw) + 1)
        k = 0
        for c in exc[lw]:
            if c == "-":
                marks[k] = True
            else:
                k += 1
        v = [0] + [1 if m else 0 for m in marks]
    else:
        d = "." + lw + "."
        v = [0] * (len(d) + 1)
        for i in range(len(d)):
            for j in range(i + 1, len(d) + 1):
                p = pats.get(d[i:j])
                if p:
                    for k, x in enumerate(p):
                        v[i + k] = max(v[i + k], x)
    out = []
    for t, c in enumerate(word):
        if t >= LEFT and len(word) - t >= RIGHT and v[t + 1] % 2:
            out.append("-")
        out.append(c)
    return "".join(out)


def main():
    pats, exc = load(ROOT / "data" / "hyph-en-us.txt")
    if sys.argv[1:] == ["--vectors"]:
        rng = random.Random(2026)
        words = set()
        alphabet = "etaoinshrdlucmfwypvbgkqjxz"
        while len(words) < 3000:
            n = rng.randint(4, 15)
            words.add("".join(rng.choice(alphabet[: rng.randint(8, 26)]) for _ in range(n)))
        words |= {w.replace("-", "") for w in exc.values()}
        for w in sorted(words):
            print(hyphenate(w, pats, exc))
        return
    for w in sys.stdin.read().split():
        print(hyphenate(w, pats, exc))


if __name__ == "__main__":
    main()
