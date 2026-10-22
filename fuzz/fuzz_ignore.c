#include "cu.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#define FYND_IGNORE_PURE_ONLY
#include "../src/ignore.inc"

/* Independent dynamic-programming oracle for a component-only sublanguage.
 * No recursive production matcher is reused by the oracle. */
static int naive_component(const char *g, size_t gn, const char *p, size_t pn)
{
    unsigned char row[257], next[257];
    size_t i, j;
    row[0] = 1;
    for (j = 1; j <= pn; ++j) row[j] = 0;
    for (i = 0; i < gn; ++i) {
        next[0] = g[i] == '*' ? row[0] : 0;
        for (j = 1; j <= pn; ++j) {
            if (g[i] == '*') next[j] = (unsigned char)(row[j] || next[j - 1]);
            else next[j] = (unsigned char)(row[j - 1] && (g[i] == '?' || g[i] == p[j - 1]));
        }
        memcpy(row, next, pn + 1);
    }
    return row[pn] != 0;
}

/* Independent of rejection logic: original rule/suffix traversal using the
 * unchanged glob engine. Work totals need not agree with the filtered path. */
static int unfiltered_file(const IgnoreFile *f, const char *p, size_t n,
                           int dir, IgnoreWork *w)
{
    const IgnoreRule *r;
    size_t i, at;
    int hit;
    for (i = f->count; i != 0;) {
        if (!ignore_tick(w)) return 0;
        r = &f->rules[--i];
        if (r->dir_only && !dir) continue;
        hit = ignore_glob(r->glob, r->len, p, n, 1, w);
        for (at = 0; !hit && !r->anchored && at < n && !w->failed; ++at) {
            if (!ignore_tick(w)) return 0;
            if (p[at] == '/') hit = ignore_glob(r->glob, r->len,
                                               p + at + 1, n - at - 1, 1, w);
        }
        if (w->failed) return 0;
        if (hit) return r->negate ? -1 : 1;
    }
    return 0;
}

static void differential_file(const IgnoreFile *f, const char *p, size_t n, int dir)
{
    IgnoreWork before, after;
    int expected, got;
    before.left = after.left = 1000000UL; before.failed = after.failed = 0;
    expected = unfiltered_file(f, p, n, dir, &before);
    got = ignore_file_verdict(f, p, n, dir, &after);
    if (!before.failed && !after.failed) assert(got == expected);
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size);
int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
    CuArena a;
    IgnoreFile f;
    IgnoreRule r;
    IgnoreWork w;
    char pattern[256], path[256];
    static const char alphabet[] = "ab*?";
    size_t gn, pn, i;
    int number, expected, got;
    if (size == 0 || size > 512) return 0;
    gn = (size - 1) / 2; pn = size - 1 - gn;
    for (i = 0; i < gn; ++i) pattern[i] = alphabet[data[i + 1] % 4];
    for (i = 0; i < pn; ++i) path[i] = (char)('a' + data[gn + 1 + i] % 2);
    expected = naive_component(pattern, gn, path, pn);
    w.left = 1000000UL; w.failed = 0;
    got = ignore_glob(pattern, gn, path, pn, 1, &w);
    assert(!w.failed && got == expected);
    /* Extend the independent DP sensor through the filtered verdict boundary,
     * including empty exact-length spans and both signs/directory eligibility. */
    r.glob = pattern; r.len = gn; r.anchored = data[0] & 1;
    r.negate = data[0] & 2; r.dir_only = data[0] & 4;
    f.rules = &r; f.count = 1;
    if (r.dir_only && !(data[0] & 8)) expected = 0;
    if (r.negate) expected = -expected;
    w.left = 1000000UL; w.failed = 0;
    got = ignore_file_verdict(&f, path, pn, data[0] & 8, &w);
    assert(!w.failed && got == expected);
    /* Raw nonterminated spans exercise slash-before-escape boundaries,
     * malformed classes, high bytes and NUL bytes without parser normalization. */
    r.glob = (const char *)data + 1; r.len = gn;
    /* Mirror ignore_parse: a glob containing '/' is anchored. */
    r.anchored = (data[0] & 1) || memchr(r.glob, '/', gn) != NULL;
    differential_file(&f, (const char *)data + gn + 1, pn, data[0] & 8);
    cu_arena_init(&a);
    if (ignore_parse(&a, cu_str_n((const char *)data, size), &f, &number)) {
        assert(memchr(data, 0, size) == NULL);
        for (i = 0; i < f.count; ++i) {
            assert(f.rules[i].len != 0);
            assert(f.rules[i].glob >= (const char *)data);
            assert(f.rules[i].glob + f.rules[i].len <= (const char *)data + size);
        }
        w.left = 1000000UL; w.failed = 0;
        got = ignore_file_verdict(&f, path, pn, data[0] & 1, &w);
        assert(got >= -1 && got <= 1);
        differential_file(&f, (const char *)data + gn + 1, pn, data[0] & 1);
        /* A basename is also the final suffix of an unanchored rule. */
        if (!w.failed && f.count == 1 && !f.rules[0].anchored &&
            !f.rules[0].dir_only) {
            char prefixed[258];
            prefixed[0] = 'x'; prefixed[1] = '/';
            memcpy(prefixed + 2, path, pn);
            w.left = 1000000UL; w.failed = 0;
            expected = ignore_file_verdict(&f, prefixed, pn + 2, 0, &w);
            if (got != 0 && !w.failed) assert(expected == got);
        }
    } else assert(number == EINVAL || number == ENOMEM);
    cu_arena_release(&a);
    return 0;
}
