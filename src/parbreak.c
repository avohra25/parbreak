/* parbreak.c - Knuth-Plass total-fit and greedy first-fit line breaking. */
#include "parbreak.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define NO_POS SIZE_MAX

pb_item pb_box(double width, const char *text, size_t len) {
    pb_item it = {PB_BOX, width, 0, 0, 0, 0, text, len};
    return it;
}

pb_item pb_glue(double width, double stretch, double shrink) {
    pb_item it = {PB_GLUE, width, stretch, shrink, 0, 0, NULL, 0};
    return it;
}

pb_item pb_penalty(double width, double penalty, int flagged) {
    pb_item it = {PB_PENALTY, width, 0, 0, penalty, flagged ? 1 : 0, NULL, 0};
    return it;
}

void pb_params_default(pb_params *p) {
    p->tolerance = 2.0;
    p->line_penalty = 10.0;
    p->flagged_demerits = 3000.0;
    p->fitness_demerits = 3000.0;
    p->emergency = 1;
}

const char *pb_strerror(int status) {
    switch (status) {
    case PB_OK: return "ok";
    case PB_ERR_ARG: return "invalid argument (paragraph must end in a forced break)";
    case PB_ERR_NOMEM: return "out of memory";
    case PB_ERR_INFEASIBLE: return "no feasible line breaking within tolerance";
    default: return "unknown error";
    }
}

int pb_fitness(double r) {
    if (r < -0.5) return 0;
    if (r <= 0.5) return 1;
    if (r <= 1.0) return 2;
    return 3;
}

static int is_forced(const pb_item *it) {
    return it->kind == PB_PENALTY && it->penalty <= -PB_INFINITY;
}

int pb_is_breakpoint(const pb_item *items, size_t n, size_t i) {
    if (i >= n) return 0;
    const pb_item *it = &items[i];
    if (it->kind == PB_PENALTY) return it->penalty < PB_INFINITY;
    if (it->kind == PB_GLUE) return i > 0 && items[i - 1].kind == PB_BOX;
    return 0;
}

size_t pb_line_start(const pb_item *items, size_t n, size_t brk) {
    if (brk == NO_POS) return 0;
    size_t i = brk + 1;
    while (i < n && items[i].kind != PB_BOX && !is_forced(&items[i])) i++;
    return i;
}

static double ratio_of(double L, double Y, double Z, double width) {
    if (L < width) {
        if (Y >= PB_INFINITY) return 0.0; /* infinitely stretchable (e.g. \parfillskip) */
        if (Y > 0) return (width - L) / Y;
        return PB_INFINITY;
    }
    if (L > width) {
        if (Z > 0) return (width - L) / Z;
        return -PB_INFINITY;
    }
    return 0.0;
}

double pb_ratio(const pb_item *items, size_t start, size_t end, double width) {
    double L = 0, Y = 0, Z = 0;
    for (size_t i = start; i < end; i++) {
        if (items[i].kind == PB_BOX) {
            L += items[i].width;
        } else if (items[i].kind == PB_GLUE) {
            L += items[i].width;
            Y += items[i].stretch;
            Z += items[i].shrink;
        }
    }
    if (items[end].kind == PB_PENALTY) L += items[end].width;
    return ratio_of(L, Y, Z, width);
}

/* ---------------------------------------------------------------------- */

typedef struct {
    size_t pos;    /* break item index, NO_POS for paragraph start */
    size_t start;  /* first item of the following line */
    size_t line;   /* number of lines ended so far */
    int fit;
    int bad;       /* line ending here is outside tolerance (emergency) */
    double total;
    double ratio;
    size_t prev;   /* node index, NO_POS for start */
} node;

typedef struct {
    size_t key;
    int fit;
    int bad;
    double total;
    double ratio;
    size_t from;
} cand;

typedef struct {
    void *data;
    size_t len, cap, elem;
} vec;

static int vec_push(vec *v, const void *x) {
    if (v->len == v->cap) {
        size_t nc = v->cap ? v->cap * 2 : 16;
        void *nd = realloc(v->data, nc * v->elem);
        if (!nd) return 0;
        v->data = nd;
        v->cap = nc;
    }
    memcpy((char *)v->data + v->len * v->elem, x, v->elem);
    v->len++;
    return 1;
}

static int check_args(const pb_item *items, size_t n, const double *widths, size_t nwidths,
                      pb_breaks *out) {
    if (!items || n == 0 || !widths || nwidths == 0 || !out) return 0;
    if (!is_forced(&items[n - 1])) return 0;
    for (size_t i = 0; i < nwidths; i++)
        if (!(widths[i] >= 0) || isinf(widths[i])) return 0;
    return 1;
}

static int emit(pb_breaks *out, size_t *cap, size_t brk, double r) {
    if (out->lines == *cap) {
        size_t nc = *cap ? *cap * 2 : 16;
        size_t *nb = realloc(out->breaks, nc * sizeof *nb);
        if (!nb) return 0;
        out->breaks = nb;
        double *nr = realloc(out->ratios, nc * sizeof *nr);
        if (!nr) return 0;
        out->ratios = nr;
        *cap = nc;
    }
    out->breaks[out->lines] = brk;
    out->ratios[out->lines] = r;
    out->lines++;
    return 1;
}

static int kp_run(const pb_item *items, size_t n, const double *widths, size_t nwidths,
                  const pb_params *p, int emergency, pb_breaks *out) {
    int status = PB_OK;
    double *W = malloc((n + 1) * sizeof *W);
    double *Y = malloc((n + 1) * sizeof *Y);
    double *Z = malloc((n + 1) * sizeof *Z);
    vec nodes = {0, 0, 0, sizeof(node)};
    vec active = {0, 0, 0, sizeof(size_t)};
    vec kept = {0, 0, 0, sizeof(size_t)};
    vec cands = {0, 0, 0, sizeof(cand)};
    if (!W || !Y || !Z) { status = PB_ERR_NOMEM; goto done; }

    W[0] = Y[0] = Z[0] = 0;
    for (size_t i = 0; i < n; i++) {
        W[i + 1] = W[i];
        Y[i + 1] = Y[i];
        Z[i + 1] = Z[i];
        if (items[i].kind != PB_PENALTY) W[i + 1] += items[i].width;
        if (items[i].kind == PB_GLUE) {
            Y[i + 1] += items[i].stretch;
            Z[i + 1] += items[i].shrink;
        }
    }

    node start = {NO_POS, 0, 0, 1, 0, 0.0, 0.0, NO_POS};
    size_t zero = 0;
    if (!vec_push(&nodes, &start) || !vec_push(&active, &zero)) { status = PB_ERR_NOMEM; goto done; }

    for (size_t b = 0; b < n; b++) {
        if (!pb_is_breakpoint(items, n, b)) continue;
        const pb_item *it = &items[b];
        int forced = is_forced(it);
        double pen = it->kind == PB_PENALTY ? it->penalty : 0.0;
        double penw = it->kind == PB_PENALTY ? it->width : 0.0;
        int flagged = it->kind == PB_PENALTY && it->flagged;

        kept.len = 0;
        cands.len = 0;
        for (size_t ai = 0; ai < active.len; ai++) {
            size_t a = ((size_t *)active.data)[ai];
            node na = ((node *)nodes.data)[a];
            if (b < na.start || (b == na.start && !forced)) {
                if (!vec_push(&kept, &a)) { status = PB_ERR_NOMEM; goto done; }
                continue;
            }
            size_t wi = na.line < nwidths - 1 ? na.line : nwidths - 1;
            double width = widths[wi];
            double L = W[b] - W[na.start] + penw;
            double r = ratio_of(L, Y[b] - Y[na.start], Z[b] - Z[na.start], width);
            int deact = r < -1.0 || forced;
            int feasible = r >= -1.0 && r <= p->tolerance;
            if (feasible || emergency) {
                double bad;
                if (feasible) bad = 100.0 * fabs(r) * fabs(r) * fabs(r);
                else if (r < -1.0) bad = 1e9 + 1e7 * (L - width);
                else bad = 1e7 + 1e3 * (r < PB_INFINITY ? r : PB_INFINITY);
                double d = p->line_penalty + bad;
                d *= d;
                if (pen >= 0) d += pen * pen;
                else if (pen > -PB_INFINITY) d -= pen * pen;
                int fit = pb_fitness(r);
                if (flagged && na.pos != NO_POS && items[na.pos].kind == PB_PENALTY &&
                    items[na.pos].flagged)
                    d += p->flagged_demerits;
                if (abs(fit - na.fit) > 1) d += p->fitness_demerits;
                double total = na.total + d;
                size_t key = na.line + 1 < nwidths - 1 ? na.line + 1 : nwidths - 1;
                size_t ci;
                for (ci = 0; ci < cands.len; ci++) {
                    cand *c = &((cand *)cands.data)[ci];
                    if (c->key == key && c->fit == fit) {
                        if (total < c->total) {
                            c->total = total;
                            c->ratio = r;
                            c->from = a;
                            c->bad = !feasible;
                        }
                        break;
                    }
                }
                if (ci == cands.len) {
                    cand c = {key, fit, !feasible, total, r, a};
                    if (!vec_push(&cands, &c)) { status = PB_ERR_NOMEM; goto done; }
                }
            }
            if (!deact && !vec_push(&kept, &a)) { status = PB_ERR_NOMEM; goto done; }
        }

        vec tmp = active;
        active = kept;
        kept = tmp;
        for (size_t ci = 0; ci < cands.len; ci++) {
            cand c = ((cand *)cands.data)[ci];
            node from = ((node *)nodes.data)[c.from];
            node nn = {b, pb_line_start(items, n, b), from.line + 1, c.fit, c.bad,
                       c.total, c.ratio, c.from};
            size_t idx = nodes.len;
            if (!vec_push(&nodes, &nn) || !vec_push(&active, &idx)) {
                status = PB_ERR_NOMEM;
                goto done;
            }
        }
        if (active.len == 0) { status = PB_ERR_INFEASIBLE; goto done; }
    }

    /* The last item is a forced break, so every active node now ends there. */
    size_t best = NO_POS;
    for (size_t ai = 0; ai < active.len; ai++) {
        size_t a = ((size_t *)active.data)[ai];
        node *na = &((node *)nodes.data)[a];
        if (na->pos != n - 1) continue;
        if (best == NO_POS || na->total < ((node *)nodes.data)[best].total) best = a;
    }
    if (best == NO_POS) { status = PB_ERR_INFEASIBLE; goto done; }

    size_t count = 0;
    for (size_t k = best; ((node *)nodes.data)[k].pos != NO_POS; k = ((node *)nodes.data)[k].prev)
        count++;
    out->breaks = malloc(count * sizeof *out->breaks);
    out->ratios = malloc(count * sizeof *out->ratios);
    if (!out->breaks || !out->ratios) { status = PB_ERR_NOMEM; goto done; }
    out->lines = count;
    out->demerits = ((node *)nodes.data)[best].total;
    out->bad_lines = 0;
    size_t i = count;
    for (size_t k = best; ((node *)nodes.data)[k].pos != NO_POS; k = ((node *)nodes.data)[k].prev) {
        node *nk = &((node *)nodes.data)[k];
        i--;
        out->breaks[i] = nk->pos;
        out->ratios[i] = nk->ratio;
        out->bad_lines += nk->bad ? 1 : 0;
    }

done:
    if (status != PB_OK) pb_breaks_free(out);
    free(W);
    free(Y);
    free(Z);
    free(nodes.data);
    free(active.data);
    free(kept.data);
    free(cands.data);
    return status;
}

int pb_knuth_plass(const pb_item *items, size_t n, const double *widths, size_t nwidths,
                   const pb_params *p, pb_breaks *out) {
    if (out) memset(out, 0, sizeof *out);
    if (!check_args(items, n, widths, nwidths, out)) return PB_ERR_ARG;
    pb_params def;
    if (!p) {
        pb_params_default(&def);
        p = &def;
    }
    int st = kp_run(items, n, widths, nwidths, p, 0, out);
    if (st == PB_ERR_INFEASIBLE && p->emergency) st = kp_run(items, n, widths, nwidths, p, 1, out);
    return st;
}

int pb_first_fit(const pb_item *items, size_t n, const double *widths, size_t nwidths,
                 const pb_params *p, pb_breaks *out) {
    if (out) memset(out, 0, sizeof *out);
    if (!check_args(items, n, widths, nwidths, out)) return PB_ERR_ARG;
    pb_params def;
    if (!p) {
        pb_params_default(&def);
        p = &def;
    }
    size_t cap = 0, s = 0, line = 0, last_ok = NO_POS;
    double last_r = 0;
    for (size_t b = 0; b < n; b++) {
        if (!pb_is_breakpoint(items, n, b)) continue;
        int forced = is_forced(&items[b]);
        if (b < s || (b == s && !forced)) continue;
        double w = widths[line < nwidths - 1 ? line : nwidths - 1];
        double r = pb_ratio(items, s, b, w);
        size_t brk;
        double br;
        if (r < -1.0 && last_ok != NO_POS) {
            brk = last_ok;
            br = last_r;
        } else if (r < -1.0 || forced) {
            brk = b;
            br = r;
        } else {
            last_ok = b;
            last_r = r;
            continue;
        }
        if (!emit(out, &cap, brk, br)) {
            pb_breaks_free(out);
            return PB_ERR_NOMEM;
        }
        if (br < -1.0 || br > p->tolerance) out->bad_lines++;
        s = pb_line_start(items, n, brk);
        line++;
        last_ok = NO_POS;
        b = brk; /* rescan from just after the break */
    }
    return PB_OK;
}

void pb_breaks_free(pb_breaks *b) {
    if (!b) return;
    free(b->breaks);
    free(b->ratios);
    memset(b, 0, sizeof *b);
}
