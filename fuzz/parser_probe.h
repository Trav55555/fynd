#ifndef FYND_PARSER_PROBE_H
#define FYND_PARSER_PROBE_H
#include "pure_probe.h"

static void check_parser(int argc, char **argv)
{
    CuArena arena;
    Query q;
    char *extensions[512];
    char *roots[512];
    const char *pattern;
    const char *s;
    const char *v;
    const CuStr *view;
    size_t ne;
    size_t nr;
    size_t k;
    unsigned int kinds;
    int fold;
    int hidden;
    int no_ignore;
    int threads;
    int sort;
    int delimiter;
    int depth;
    int i;
    int valid;
    int result;
    int d;
    ne = 0; nr = 0; kinds = 0;
    fold = -1; hidden = 0; delimiter = '\n'; depth = -1;
    no_ignore = 0; threads = 0; /* automatic */
    sort = 0;
    pattern = "";
    valid = 1;
    i = 1;
    while (i < argc) {
        s = argv[i];
        if (strcmp(s, "--") == 0) { ++i; break; }
        if (s[0] != '-' || s[1] == 0) break;
        ++i;
        if (!strcmp(s, "-i") || !strcmp(s, "--ignore-case")) fold = 1;
        else if (!strcmp(s, "-s") || !strcmp(s, "--case-sensitive")) fold = 0;
        else if (!strcmp(s, "-H") || !strcmp(s, "--hidden")) hidden = 1;
        else if (!strcmp(s, "--no-ignore")) no_ignore = 1;
        else if (!strcmp(s, "--sort")) sort = 1;
        else if (!strcmp(s, "-0") || !strcmp(s, "--print0")) delimiter = 0;
        else {
            if ((!strncmp(s, "-j", 2) || !strncmp(s, "-d", 2)) && s[2]) v = s + 2;
            else {
                if (i == argc) { valid = 0; break; }
                v = argv[i++];
            }
            if (!strcmp(s, "--threads") || !strncmp(s, "-j", 2)) {
                d = 0;
                if (!*v) valid = 0;
                while (*v && valid) {
                    if (*v < '0' || *v > '9') valid = 0;
                    else { d = d * 10 + (*v - '0'); if (d > 32) valid = 0; }
                    ++v;
                }
                if (d < 1) valid = 0;
                threads = d;
            } else if (!strcmp(s, "-t") || !strcmp(s, "--type")) {
                if (!strcmp(v, "f")) kinds |= 1U << CU_DIR_FILE;
                else if (!strcmp(v, "d")) kinds |= 1U << CU_DIR_DIR;
                else if (!strcmp(v, "l")) kinds |= 1U << CU_DIR_LINK;
                else if (!strcmp(v, "o")) kinds |= 1U << CU_DIR_OTHER;
                else valid = 0;
            } else if (!strcmp(s, "-e") || !strcmp(s, "--extension")) {
                if (!*v || strpbrk(v, "./") != NULL) valid = 0;
                else extensions[ne++] = argv[i - 1];
            } else if (!strcmp(s, "--max-depth") || !strncmp(s, "-d", 2)) {
                d = 0;
                if (!*v) valid = 0;
                while (*v && valid) {
                    if (*v < '0' || *v > '9') valid = 0;
                    else { d = d * 10 + (*v - '0'); if (d > 256) valid = 0; }
                    ++v;
                }
                depth = d;
            } else valid = 0;
            if (!valid) break;
        }
    }
    if (valid) {
        if (i < argc) pattern = argv[i++];
        /* Smart case: any ASCII capital in the pattern means exact. */
        if (fold < 0) {
            fold = 1;
            for (v = pattern; *v; ++v)
                if (*v >= 'A' && *v <= 'Z') fold = 0;
        }
        if (i == argc) roots[nr++] = ".";
        while (i < argc) {
            if (!*argv[i]) valid = 0;
            roots[nr++] = argv[i++];
        }
    }
    cu_arena_init(&arena);
    result = parse(&q, &arena, argc, argv);
    if (result != 1) {
        assert(result == (valid ? 0 : 2));
        if (valid) {
            assert(q.fold == fold && q.hidden == hidden);
            assert(q.no_ignore == no_ignore && q.threads == threads && q.sort == sort);
            assert(q.delimiter == delimiter && q.depth == depth && q.kinds == kinds);
            assert(q.pattern.len == strlen(pattern));
            assert(memcmp(q.pattern.ptr, pattern, q.pattern.len) == 0);
            assert(q.extensions.len == ne && q.roots.len == nr);
            for (k = 0; k < ne + nr; ++k) {
                view = k < ne ? CU_VEC_AT(&q.extensions, CuStr, k) :
                    CU_VEC_AT(&q.roots, CuStr, k - ne);
                s = k < ne ? extensions[k] : roots[k - ne];
                assert(view->len == strlen(s));
                assert(memcmp(view->ptr, s, view->len) == 0);
            }
        }
    } else assert(cu_arena_failed(&arena));
    cu_arena_release(&arena);
}
#endif
