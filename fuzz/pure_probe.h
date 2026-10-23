#ifndef FYND_PURE_PROBE_H
#define FYND_PURE_PROBE_H
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "fynd.h"
int fynd_pure_probe(int argc, char **argv, FILE *out, FILE *err);
#define fynd_main fynd_pure_probe
#include "../src/fynd.c"
#undef fynd_main

static void check_match(const unsigned char *h, size_t hn,
                        const unsigned char *p, size_t pn)
{
    unsigned char *a;
    unsigned char *b;
    size_t i;
    size_t j;
    int mode;
    int expected;
    int equal;
    unsigned int x;
    unsigned int y;
    CuStr hay;
    CuStr pat;
    a = hn == 0 ? NULL : (unsigned char *)malloc(hn);
    b = pn == 0 ? NULL : (unsigned char *)malloc(pn);
    if ((hn != 0 && a == NULL) || (pn != 0 && b == NULL)) {
        free(a);
        free(b);
        return;
    }
    if (hn != 0) memcpy(a, h, hn);
    if (pn != 0) memcpy(b, p, pn);
    hay = cu_str_n(hn == 0 ? "" : (const char *)a, hn);
    pat = cu_str_n(pn == 0 ? "" : (const char *)b, pn);
    for (mode = 0; mode < 2; ++mode) {
        expected = pn == 0;
        for (i = 0; i < hn && !expected; ++i) {
            equal = 1;
            for (j = 0; j < pn; ++j) {
                if (j >= hn - i) { equal = 0; break; }
                x = h[i + j];
                y = p[j];
                if (mode) {
                    if (x >= 65 && x <= 90) x += 32;
                    if (y >= 65 && y <= 90) y += 32;
                }
                if (x != y) { equal = 0; break; }
            }
            if (equal) expected = 1;
        }
        assert(contains(hay, pat, mode) == expected);
    }
    free(b);
    free(a);
}

static void check_size(size_t a, size_t b, int slash)
{
    size_t room;
    size_t got;
    int expected;
    room = (size_t)-1;
    expected = 0;
    if (a <= room) {
        room -= a;
        if (b <= room) {
            room -= b;
            expected = room >= (slash ? 2U : 1U);
        }
    }
    got = 37;
    assert(joined_size(a, b, slash, &got) == expected);
    if (expected)
        assert(got == (size_t)-1 - (room - (slash ? 2U : 1U)));
    else
        assert(got == 37);
}

static void check_depth(const char *text)
{
    const unsigned char *p;
    unsigned long n;
    int valid;
    int got;
    n = 0;
    valid = *text != 0;
    for (p = (const unsigned char *)text; *p != 0; ++p) {
        if (*p < 48 || *p > 57) valid = 0;
        if (valid) {
            n = n * 10 + (unsigned long)(*p - 48);
            if (n > 256) valid = 0;
        }
    }
    got = -7;
    assert(parse_depth(text, &got) == valid);
    if (valid) assert(got == (int)n);
    else assert(got == -7);
}
#endif
