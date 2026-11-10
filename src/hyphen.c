/* hyphen.c - Frank Liang's pattern-based hyphenation (as used by TeX). */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "parbreak.h"

extern const char *const pb_hyph_en_us_lines[];
extern const size_t pb_hyph_en_us_count;

/* A trie over the alphabet '.', 'a'..'z' stored as first-child /
 * next-sibling lists. Each node may carry the digit vector of the pattern
 * (or the hyphen positions of the exception) that ends there. */
typedef struct {
    char c;
    int child;
    int sibling;
    int values;     /* offset into vals, -1 if none */
} tnode;

typedef struct {
    tnode *nodes;
    size_t n, cap;
    unsigned char *vals;
    size_t nv, vcap;
    size_t entries;
} trie;

struct pb_hyphenator {
    trie pat;
    trie exc;
    int left, right;
};

static int trie_init(trie *t) {
    memset(t, 0, sizeof *t);
    t->nodes = malloc(64 * sizeof *t->nodes);
    if (!t->nodes) return 0;
    t->cap = 64;
    t->nodes[0].c = 0;
    t->nodes[0].child = t->nodes[0].sibling = t->nodes[0].values = -1;
    t->n = 1;
    return 1;
}

static void trie_free(trie *t) {
    free(t->nodes);
    free(t->vals);
    memset(t, 0, sizeof *t);
}

static int trie_find(const trie *t, int node, char c) {
    for (int k = t->nodes[node].child; k >= 0; k = t->nodes[k].sibling)
        if (t->nodes[k].c == c) return k;
    return -1;
}

/* Insert key with `nvals` values; later insertions of the same key replace. */
static int trie_insert(trie *t, const char *key, size_t klen, const unsigned char *v, size_t nvals) {
    int node = 0;
    for (size_t i = 0; i < klen; i++) {
        int k = trie_find(t, node, key[i]);
        if (k < 0) {
            if (t->n == t->cap) {
                size_t nc = t->cap * 2;
                tnode *nn = realloc(t->nodes, nc * sizeof *nn);
                if (!nn) return PB_ERR_NOMEM;
                t->nodes = nn;
                t->cap = nc;
            }
            k = (int)t->n++;
            t->nodes[k].c = key[i];
            t->nodes[k].child = -1;
            t->nodes[k].values = -1;
            t->nodes[k].sibling = t->nodes[node].child;
            t->nodes[node].child = k;
        }
        node = k;
    }
    if (t->nv + nvals > t->vcap) {
        size_t nc = t->vcap ? t->vcap : 256;
        while (t->nv + nvals > nc) nc *= 2;
        unsigned char *nv = realloc(t->vals, nc);
        if (!nv) return PB_ERR_NOMEM;
        t->vals = nv;
        t->vcap = nc;
    }
    if (t->nodes[node].values < 0) t->entries++;
    t->nodes[node].values = (int)t->nv;
    memcpy(t->vals + t->nv, v, nvals);
    t->nv += nvals;
    return PB_OK;
}

pb_hyphenator *pb_hyph_new(void) {
    pb_hyphenator *h = calloc(1, sizeof *h);
    if (!h) return NULL;
    if (!trie_init(&h->pat) || !trie_init(&h->exc)) {
        pb_hyph_free(h);
        return NULL;
    }
    h->left = 2;
    h->right = 3;
    return h;
}

void pb_hyph_free(pb_hyphenator *h) {
    if (!h) return;
    trie_free(&h->pat);
    trie_free(&h->exc);
    free(h);
}

void pb_hyph_set_mins(pb_hyphenator *h, int left, int right) {
    if (!h) return;
    h->left = left < 1 ? 1 : left;
    h->right = right < 1 ? 1 : right;
}

size_t pb_hyph_pattern_count(const pb_hyphenator *h) { return h ? h->pat.entries : 0; }

int pb_hyph_add_pattern(pb_hyphenator *h, const char *pat, size_t len) {
    if (!h || !pat || len == 0 || len > 64) return PB_ERR_ARG;
    char key[65];
    unsigned char vals[66];
    size_t k = 0;
    memset(vals, 0, sizeof vals);
    for (size_t i = 0; i < len; i++) {
        char c = pat[i];
        if (c >= '0' && c <= '9') {
            if (i + 1 < len && pat[i + 1] >= '0' && pat[i + 1] <= '9') return PB_ERR_ARG;
            vals[k] = (unsigned char)(c - '0');
        } else if (c == '.' || (c >= 'a' && c <= 'z')) {
            /* '.' marks a word edge and may only appear at either end */
            if (c == '.' && k != 0 && i != len - 1) return PB_ERR_ARG;
            key[k++] = c;
        } else {
            return PB_ERR_ARG;
        }
    }
    if (k == 0) return PB_ERR_ARG;
    return trie_insert(&h->pat, key, k, vals, k + 1);
}

int pb_hyph_add_exception(pb_hyphenator *h, const char *word, size_t len) {
    if (!h || !word || len == 0 || len > 128) return PB_ERR_ARG;
    char key[129];
    unsigned char pos[130];
    size_t k = 0;
    memset(pos, 0, sizeof pos);
    for (size_t i = 0; i < len; i++) {
        char c = (char)tolower((unsigned char)word[i]);
        if (c == '-') {
            if (k == 0 || i + 1 == len || word[i + 1] == '-') return PB_ERR_ARG;
            pos[k] = 1;
        } else if (c >= 'a' && c <= 'z') {
            key[k++] = c;
        } else {
            return PB_ERR_ARG;
        }
    }
    return trie_insert(&h->exc, key, k, pos, k + 1);
}

int pb_hyph_load(pb_hyphenator *h, const char *text, size_t len, size_t *bad_line) {
    if (!h || (!text && len)) return PB_ERR_ARG;
    int exceptions = 0;
    size_t line = 1, i = 0;
    while (i < len) {
        char c = text[i];
        if (c == '\n') { line++; i++; continue; }
        if (c == ' ' || c == '\t' || c == '\r') { i++; continue; }
        if (c == '%') {
            while (i < len && text[i] != '\n') i++;
            continue;
        }
        size_t s = i;
        while (i < len && text[i] != ' ' && text[i] != '\t' && text[i] != '\r' && text[i] != '\n' &&
               text[i] != '%')
            i++;
        size_t tl = i - s;
        int st;
        if (tl == 9 && memcmp(text + s, "@patterns", 9) == 0) { exceptions = 0; continue; }
        if (tl == 11 && memcmp(text + s, "@exceptions", 11) == 0) { exceptions = 1; continue; }
        st = exceptions ? pb_hyph_add_exception(h, text + s, tl) : pb_hyph_add_pattern(h, text + s, tl);
        if (st != PB_OK) {
            if (bad_line) *bad_line = line;
            return st;
        }
    }
    return PB_OK;
}

pb_hyphenator *pb_hyph_en_us(void) {
    pb_hyphenator *h = pb_hyph_new();
    if (!h) return NULL;
    int exceptions = 0;
    for (size_t i = 0; i < pb_hyph_en_us_count; i++) {
        const char *l = pb_hyph_en_us_lines[i];
        size_t len = strlen(l);
        int st;
        if (strcmp(l, "@patterns") == 0) { exceptions = 0; continue; }
        if (strcmp(l, "@exceptions") == 0) { exceptions = 1; continue; }
        st = exceptions ? pb_hyph_add_exception(h, l, len) : pb_hyph_add_pattern(h, l, len);
        if (st != PB_OK) {
            pb_hyph_free(h);
            return NULL;
        }
    }
    return h;
}

static int is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

size_t pb_hyphenate(const pb_hyphenator *h, const char *word, size_t len, unsigned char *points) {
    if (!points) return 0;
    memset(points, 0, len + 1);
    if (!h || !word) return 0;
    /* strip leading and trailing ASCII punctuation such as quotes, commas */
    size_t s = 0, e = len;
    while (s < e && ispunct((unsigned char)word[s])) s++;
    while (e > s && ispunct((unsigned char)word[e - 1])) e--;
    size_t n = e - s;
    if (n < (size_t)(h->left + h->right) || n > 120) return 0;
    for (size_t i = s; i < e; i++)
        if (!is_alpha(word[i])) return 0;

    char w[124];
    unsigned char v[125];
    memset(v, 0, sizeof v);
    for (size_t i = 0; i < n; i++) w[i] = (char)tolower((unsigned char)word[s + i]);

    /* exceptions first */
    int node = 0;
    for (size_t i = 0; i < n && node >= 0; i++) node = trie_find(&h->exc, node, w[i]);
    if (node >= 0 && h->exc.nodes[node].values >= 0) {
        const unsigned char *pos = h->exc.vals + h->exc.nodes[node].values;
        for (size_t i = 1; i < n; i++) v[i + 1] = pos[i] ? 1 : 0;
    } else {
        /* dotted word ".word." ; v[p] is the value between dotted[p-1] and dotted[p] */
        char d[126];
        d[0] = '.';
        memcpy(d + 1, w, n);
        d[n + 1] = '.';
        size_t m = n + 2;
        for (size_t i = 0; i < m; i++) {
            int nd = 0;
            for (size_t j = i; j < m; j++) {
                nd = trie_find(&h->pat, nd, d[j]);
                if (nd < 0) break;
                int off = h->pat.nodes[nd].values;
                if (off >= 0) {
                    const unsigned char *pv = h->pat.vals + off;
                    for (size_t k = 0; k <= j - i + 1; k++)
                        if (pv[k] > v[i + k]) v[i + k] = pv[k];
                }
            }
        }
    }
    size_t count = 0;
    for (size_t t = (size_t)h->left; t + (size_t)h->right <= n; t++) {
        if (v[t + 1] & 1) {
            points[s + t] = 1;
            count++;
        }
    }
    return count;
}
