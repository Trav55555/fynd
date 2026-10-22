#include "cu.h"
#include <assert.h>
#include <sys/stat.h>
#include <errno.h>
static unsigned long test_ignore_budget = 1000000UL;
static unsigned long test_ignore_source_budget = 16777216UL;
static int test_probe_errno;
static int test_ignore_stat(const char *path, struct stat *st)
{
    if (test_probe_errno != 0) { errno = test_probe_errno; return -1; }
    return stat(path, st);
}
#define IGNORE_STAT test_ignore_stat
#define IGNORE_WORK_LIMIT test_ignore_budget
#define IGNORE_SOURCE_LIMIT test_ignore_source_budget
/* Rename the included declaration as well as the definition. */
#define fynd_main ignore_test_fynd_main
#include "../src/fynd.c"
#include "../src/ignore.inc"
#undef fynd_main

static void match_case(const char *pattern, const char *path, int expected)
{
    IgnoreWork w;
    w.left = 1000000UL; w.failed = 0;
    assert(ignore_glob(pattern, strlen(pattern), path, strlen(path), 1, &w) == expected);
    assert(!w.failed);
}

/* Pre-filter rule traversal: differential oracle, not a second filter. */
static int unfiltered_verdict(const IgnoreFile *f, const char *p, size_t n,
                              int is_dir, IgnoreWork *w)
{
    const IgnoreRule *r;
    size_t i, j;
    int hit;
    for (i = f->count; i != 0;) {
        if (!ignore_tick(w)) return 0;
        r = &f->rules[--i];
        if (r->dir_only && !is_dir) continue;
        hit = ignore_glob(r->glob, r->len, p, n, 1, w);
        for (j = 0; !hit && !r->anchored && j < n && !w->failed; ++j) {
            if (!ignore_tick(w)) return 0;
            if (p[j] == '/') hit = ignore_glob(r->glob, r->len,
                                               p + j + 1, n - j - 1, 1, w);
        }
        if (w->failed) return 0;
        if (hit) return r->negate ? -1 : 1;
    }
    return 0;
}

static void verdict_case(const IgnoreFile *f, const char *p, size_t n,
                         int is_dir, int expected)
{
    IgnoreWork w, old;
    int got, reference;
    w.left = old.left = 1000000UL; w.failed = old.failed = 0;
    reference = unfiltered_verdict(f, p, n, is_dir, &old);
    got = ignore_file_verdict(f, p, n, is_dir, &w);
    assert(!old.failed && !w.failed && got == reference);
    assert(got == expected);
}

/* Independent literal oracle: decode components, then compare exact bytes at
 * eligible suffix boundaries. No production glob or filter helper is used. */
static int literal_verdict(const IgnoreRule *r, const char *p, size_t n, int dir)
{
    unsigned char decoded[32];
    size_t i, used, at;
    used = 0;
    for (i = 0; i < r->len; ++i) {
        if (r->glob[i] == '\\' && i + 1 < r->len && r->glob[i + 1] != '/') ++i;
        assert(used < sizeof decoded);
        decoded[used++] = (unsigned char)r->glob[i];
    }
    if (r->dir_only && !dir) return 0;
    for (at = 0; at <= n; ++at) {
        if ((at == 0 || p[at - 1] == '/') && n - at == used &&
            memcmp(decoded, p + at, used) == 0) return r->negate ? -1 : 1;
        if (r->anchored) break;
    }
    return 0;
}

/* Mirror ignore_parse: hand-built rules must honor the slash-anchoring invariant. */
static int parser_anchored(const char *glob, int anchored)
{
    return anchored || strchr(glob, '/') != NULL;
}

static void filter_cases(void)
{
    static const struct {
        const char *g, *p;
        int anchored, dir_only, is_dir, expected;
    } finite[] = {
        {"a", "b/a", 0, 0, 0, 1}, {"a", "b/a", 1, 0, 0, 0},
        {"a", "ba", 0, 0, 0, 0}, {"a", "a/b", 0, 0, 0, 0},
        {"a", "b/a", 0, 1, 0, 0}, {"a", "b/a", 0, 1, 1, 1},
        {"", "", 1, 0, 0, 1}, {"", "a/", 0, 0, 0, 1},
        {"", "a/", 1, 0, 0, 0}, {"a", "", 0, 0, 0, 0},
        {"/", "/", 1, 0, 0, 1}, {"/", "a//", 1, 0, 0, 0},
        {"\\/x", "\\/x", 1, 0, 0, 1}, {"\\/x", "/x", 1, 0, 0, 0},
        {"\\/x", "a/\\/x", 1, 0, 0, 0},
        {"\\/", "\\/", 1, 0, 0, 1}, {"\\", "a/\\", 0, 0, 0, 1},
        {"\\*", "a/*", 0, 0, 0, 1}, {"\\?", "?", 1, 0, 0, 1},
        {"\\[", "a/[", 0, 0, 0, 1}, {"\\\\*", "a/\\x", 0, 0, 0, 1},
        {"*a", "b/a", 0, 0, 0, 1}, {"?", "a", 1, 0, 0, 1},
        {"[a]", "a", 1, 0, 0, 1}, {"[abc", "a", 0, 0, 0, 0},
        {"[z-a]", "z", 0, 0, 0, 0}, {"[]a]", "]", 1, 0, 0, 1},
        {"a[", "a[", 0, 0, 0, 0}, {"[", "[", 0, 0, 0, 0},
        {"**/a", "a", 1, 0, 0, 1}, {"**/a", "b/c/a", 1, 0, 0, 1},
        {"\200", "a/\200", 0, 0, 0, 1}, {"\377", "\200", 0, 0, 0, 0},
        {"\\\377", "a/\377", 0, 0, 0, 1}, {"\377*", "\377a", 1, 0, 0, 1}
    };
    static const char *const literals[] = {
        "", "a", "ab", "/", "//", "a/", "/a", "a/b", "\\", "\\/",
        "\\/a", "a\\/b", "\\\\", "\\*", "\\?", "\\[", "\\!", "\\#",
        "\\x", "a\\", "\\\\/", "a//b", "\200", "\377", "\\\200", "\\\377"
    };
    static const unsigned char bytes[] = {'a', '/', '\\', '*', '?', '[', 128, 255};
    static const char *const globs[] = {
        "", "a", "z*", "a?", "a[", "*", "?", "[", "[a]", "[!a]", "[z-a]",
        "[]a]", "[abc", "**", "**/a", "a/**", "*/a", "?/a", "a//b",
        "\\/a", "\\*", "\\?", "\\[", "\\", "\\\\*", "\200*", "\\\377"
    };
    IgnoreRule rules[3];
    IgnoreFile f;
    IgnoreWork w;
    CuArena a;
    char p[2], g[1], high_path[3];
    char boundary[] = {'\\','/','x','\n','!','\\','/','x'};
    char exact[] = {'a','\n','!','a','\n','z','\n','!','q','\n','/','a','/','\n'};
    size_t i, j, n, code, count, k, value;
    int flags, dir, expected, number;
    f.rules = rules; f.count = 1;
    for (i = 0; i < sizeof finite / sizeof finite[0]; ++i) {
        rules[0].glob = finite[i].g; rules[0].len = strlen(finite[i].g);
        rules[0].anchored = finite[i].anchored;
        assert(rules[0].anchored == parser_anchored(finite[i].g, finite[i].anchored));
        rules[0].dir_only = finite[i].dir_only;
        for (flags = 0; flags < 2; ++flags) {
            rules[0].negate = flags;
            expected = finite[i].expected ? (flags ? -1 : 1) : 0;
            verdict_case(&f, finite[i].p, strlen(finite[i].p), finite[i].is_dir, expected);
        }
    }
    /* Exact, nonterminated path spans, including separators and high bytes. */
    for (n = 0, count = 1; n <= sizeof p; ++n, count *= sizeof bytes) {
        for (code = 0; code < count; ++code) {
            value = code;
            for (k = 0; k < n; ++k) {
                p[k] = (char)bytes[value % sizeof bytes]; value /= sizeof bytes;
            }
            for (flags = 0; flags < 8; ++flags) {
                rules[0].negate = flags & 2;
                rules[0].dir_only = flags & 4;
                for (dir = 0; dir < 2; ++dir) {
                    for (i = 0; i < sizeof literals / sizeof literals[0]; ++i) {
                        rules[0].glob = literals[i]; rules[0].len = strlen(literals[i]);
                        rules[0].anchored = parser_anchored(literals[i], flags & 1);
                        expected = literal_verdict(&rules[0], p, n, dir);
                        verdict_case(&f, p, n, dir, expected);
                    }
                    for (j = 0; j < sizeof globs / sizeof globs[0]; ++j) {
                        rules[0].glob = globs[j]; rules[0].len = strlen(globs[j]);
                        rules[0].anchored = parser_anchored(globs[j], flags & 1);
                        w.left = 1000000UL; w.failed = 0;
                        expected = unfiltered_verdict(&f, p, n, dir, &w);
                        assert(!w.failed);
                        verdict_case(&f, p, n, dir, expected);
                    }
                }
            }
        }
    }
    /* All non-ASCII byte indices, not just a signed-char boundary sample. */
    rules[0].glob = g; rules[0].len = 1; rules[0].dir_only = 0;
    rules[0].negate = 0;
    high_path[0] = 'a'; high_path[1] = '/';
    for (i = 128; i < 256; ++i) {
        g[0] = high_path[2] = (char)(unsigned char)i;
        rules[0].anchored = 0; verdict_case(&f, high_path, sizeof high_path, 0, 1);
        rules[0].anchored = 1; verdict_case(&f, high_path, sizeof high_path, 0, 0);
        verdict_case(&f, high_path + 2, 1, 0, 1);
    }
    g[0] = high_path[2] = '\\';
    verdict_case(&f, high_path + 2, 1, 0, 1);
    /* A shared lazy set must survive intervening unknown and directory rules;
     * rejection of the final negative must not override an earlier match. */
    rules[0].glob = "a"; rules[0].len = 1; rules[0].anchored = 0;
    rules[1] = rules[0]; rules[1].glob = "?"; rules[1].dir_only = 1;
    rules[2] = rules[0]; rules[2].glob = "z"; rules[2].negate = 1;
    f.count = 3;
    verdict_case(&f, "b/a", 3, 0, 1);
    verdict_case(&f, "b/a", 3, 1, 1);
    rules[2].glob = "a";
    verdict_case(&f, "b/a", 3, 0, -1);
    rules[0].glob = ""; rules[0].len = 0;
    rules[1] = rules[2]; rules[1].glob = "z"; f.count = 2;
    verdict_case(&f, "", 0, 0, 1);
    cu_arena_init(&a);
    assert(ignore_parse(&a, cu_str_n(boundary, sizeof boundary), &f, &number));
    verdict_case(&f, "\\/x", 3, 0, -1);
    verdict_case(&f, "/x", 2, 0, 0);
    verdict_case(&f, "a/\\/x", 5, 0, 0);
    assert(ignore_parse(&a, cu_str_n(exact, sizeof exact), &f, &number));
    verdict_case(&f, "a", 1, 0, -1);
    verdict_case(&f, "a", 1, 1, 1);
    verdict_case(&f, "b/a", 3, 1, -1);
    verdict_case(&f, "z", 1, 0, 1);
    verdict_case(&f, "q", 1, 0, -1);
    cu_arena_release(&a);
}

static void filter_work_cases(void)
{
    IgnoreRule rules[64];
    IgnoreFile f;
    IgnoreWork w;
    char p[256];
    size_t i;
    unsigned long bound;
    f.rules = rules; f.count = sizeof rules / sizeof rules[0];
    /* 128 "a/" components, then a 128-byte final component. */
    for (i = 0; i < sizeof p; ++i)
        p[i] = i < sizeof p / 2 && i % 2 != 0 ? '/' : 'a';
    for (i = 0; i < f.count; ++i) {
        rules[i].glob = "z*"; rules[i].len = 2; rules[i].anchored = 0;
        rules[i].negate = (int)(i % 2); rules[i].dir_only = 0;
    }
    /* Cheap-operation sensor: no glob/component retry per impossible rule,
     * and ONE charged final-component scan, not one per rule or full path. */
    bound = (unsigned long)(sizeof p / 2 + f.count);
    w.left = 1000UL; w.failed = 0;
    assert(ignore_file_verdict(&f, p, sizeof p, 0, &w) == 0 && !w.failed);
    assert(1000UL - w.left <= bound && 1000UL - w.left >= sizeof p / 2);
    w.left = (unsigned long)(sizeof p / 2); w.failed = 0;
    assert(ignore_file_verdict(&f, p, sizeof p, 0, &w) == 0 && w.failed);
    for (i = 0; i < f.count; ++i) rules[i].anchored = 1;
    w.left = 1000UL; w.failed = 0;
    assert(ignore_file_verdict(&f, p, sizeof p, 0, &w) == 0 && !w.failed);
    assert(1000UL - w.left <= (unsigned long)f.count);
    w.left = 0; w.failed = 0;
    assert(ignore_file_verdict(&f, p, sizeof p, 0, &w) == 0 && w.failed);
    /* An unknown start costs one rule, one final-component byte and the glob;
     * an ineligible directory rule never scans the final component. */
    f.count = 1; rules[0].anchored = 0; rules[0].glob = "*"; rules[0].len = 1;
    w.left = 8; w.failed = 0;
    assert(ignore_file_verdict(&f, "a", 1, 0, &w) == 1 && !w.failed && w.left == 0);
    rules[0].glob = "z"; rules[0].dir_only = 1;
    w.left = 1; w.failed = 0;
    assert(ignore_file_verdict(&f, p, sizeof p, 0, &w) == 0 && !w.failed && w.left == 0);
}

static void pure_cases(void)
{
    CuArena a;
    IgnoreFile f, saved;
    IgnoreWork w;
    int number;
    char exact[] = {'#','x','\n','a','\r','\n','!','a','\n','b','/','\n','\\','#'};
    char nul[] = {'a',0,'b'};
    char deep[514];
    char exact_glob[3] = {'a', '*', '?'};
    char exact_path[2] = {'a', 'b'};
    size_t i;
    cu_arena_init(&a);
    w.left = 1000000UL; w.failed = 0;
    assert(ignore_glob(exact_glob, sizeof exact_glob, exact_path,
                       sizeof exact_path, 1, &w) && !w.failed);
    match_case("a*?",  "abcd", 1);
    match_case("*a*b", "aaac", 0);
    match_case("a/**/b", "a/b", 1);
    match_case("a/**/b", "a/c/d/b", 1);
    match_case("a/**", "a", 0);
    match_case("a/**", "a/b", 1);
    match_case("[!a-c]", "z", 1);
    match_case("[^a-c]", "b", 0);
    match_case("[]a]", "]", 1);
    match_case("[z-a]", "z", 0);
    match_case("[abc", "a", 0);
    match_case("\\*", "*", 1);
    match_case("x?", "x/", 0);
    assert(ignore_parse(&a, cu_str_n(exact, sizeof exact), &f, &number));
    assert(f.count == 4 && f.rules[1].negate && f.rules[2].dir_only);
    w.left = 1000000UL; w.failed = 0;
    assert(ignore_file_verdict(&f, "a", 1, 0, &w) == -1);
    assert(ignore_file_verdict(&f, "b", 1, 0, &w) == 0);
    assert(ignore_file_verdict(&f, "b", 1, 1, &w) == 1);
    assert(ignore_file_verdict(&f, "#", 1, 0, &w) == 1);
    saved = f;
    assert(!ignore_parse(&a, cu_str_n(nul, sizeof nul), &f, &number));
    assert(number == EINVAL && f.rules == saved.rules && f.count == saved.count);
    assert(!ignore_parse(&a, cu_str_n(exact, IGNORE_SOURCE_LIMIT + 1UL), &f, &number));
    assert(number == E2BIG && f.rules == saved.rules);
    assert(ignore_parse(&a, cu_str_cstr("/a\nb c  \n\\!x\n\\#x\n!\n/\n"), &f, &number));
    assert(f.count == 4 && f.rules[0].anchored && f.rules[1].len == 3);
    w.left = 0; w.failed = 0;
    assert(!ignore_glob("a", 1, "a", 1, 1, &w) && w.failed);
    w.left = 1; w.failed = 0;
    assert(ignore_glob("", 0, "", 0, 1, &w) && !w.failed && w.left == 0);
    w.left = 1; w.failed = 0;
    assert(ignore_tick(&w) && !w.failed && w.left == 0);
    assert(!ignore_tick(&w) && w.failed);
    for (i = 0; i < 257; ++i) { deep[i * 2] = 'a'; deep[i * 2 + 1] = '/'; }
    deep[513] = 0;
    w.left = 1000000UL; w.failed = 0;
    assert(!ignore_glob(deep, 513, deep, 513, 1, &w) && w.failed);
    w.left = 1000000UL; w.failed = 0;
    assert(ignore_glob(deep, 511, deep, 511, 1, &w) && !w.failed);
    cu_arena_release(&a);
}

#ifndef __cerb__
static void put_source(const char *path, const char *text, size_t n)
{
    FILE *f;
    f = fopen(path, "wb"); assert(f != NULL);
    assert(fwrite(text, 1, n, f) == n);
    assert(fclose(f) == 0);
}

static void filesystem_cases(void)
{
#ifdef __wasi__
    char base[] = "/tmp/fynd-ignore-fixture";
#else
    char base[] = "/tmp/fynd-ignore-XXXXXX";
#endif
    CuArena a;
    const IgnoreFrame *seed, *root, *child, *nested, *sentinel;
    IgnoreError e;
    IgnoreFile f;
    CuTemp t;
    char *sub, *p, *home, *oldhome, *oldxdg, *longroot;
    int v, k;
    size_t i, held;
#ifdef __wasi__
    assert(mkdir(base, 0700) == 0);
#else
    assert(mkdtemp(base) != NULL);
#endif
    cu_arena_init(&a);
    oldhome = getenv("HOME"); oldxdg = getenv("XDG_CONFIG_HOME");
    if (oldhome != NULL) oldhome = ignore_copy(&a, oldhome, strlen(oldhome));
    if (oldxdg != NULL) oldxdg = ignore_copy(&a, oldxdg, strlen(oldxdg));
    assert(setenv("HOME", base, 1) == 0 && setenv("XDG_CONFIG_HOME", base, 1) == 0);
    home = join_path(&a, base, cu_str_cstr("git")); assert(home != NULL);
    assert(mkdir(home, 0700) == 0);
    p = join_path(&a, home, cu_str_cstr("ignore")); assert(p != NULL);
    put_source(p, "/global\n", 8);
    p = join_path(&a, base, cu_str_cstr(".ignore")); assert(p != NULL);
    put_source(p, "rank\nancestor\n", 14);
    sub = join_path(&a, base, cu_str_cstr("sub")); assert(sub != NULL);
    assert(mkdir(sub, 0700) == 0);
    p = join_path(&a, sub, cu_str_cstr(".git")); assert(p != NULL);
    assert(mkdir(p, 0700) == 0);
    p = join_path(&a, p, cu_str_cstr("info")); assert(p != NULL);
    assert(mkdir(p, 0700) == 0);
    p = join_path(&a, p, cu_str_cstr("exclude")); assert(p != NULL);
    put_source(p, "exclude\n", 8);
    p = join_path(&a, sub, cu_str_cstr(".gitignore")); assert(p != NULL);
    put_source(p, "!rank\ngit\n", 10);
    p = join_path(&a, sub, cu_str_cstr(".ignore")); assert(p != NULL);
    put_source(p, "rootonly\n!last\nlast\n", 19);
    assert(ignore_seed(&a, sub, &seed, &e) && seed != NULL);
    p = join_path(&a, sub, cu_str_cstr("rootonly")); assert(p != NULL);
    assert(ignore_verdict(&a, seed, p, 0, &v, &e) && v == 0);
    p = join_path(&a, sub, cu_str_cstr("global")); assert(p != NULL);
    assert(ignore_verdict(&a, seed, p, 0, &v, &e) && v == 0);
    assert(ignore_enter(&a, seed, sub, (1U << IGNORE_PLAIN) |
        (1U << IGNORE_GIT) | IGNORE_REPOSITORY_BIT, &root, &e));
    {
        const char *names[] = {"rootonly", "rank", "ancestor", "global", "exclude", "git", "last"};
        for (k = 0; k < 7; ++k) {
            p = join_path(&a, sub, cu_str_cstr(names[k])); assert(p != NULL);
            assert(ignore_verdict(&a, root, p, 0, &v, &e) && v == 1);
        }
    }
    {
        const IgnoreFrame *hs, *hf;
        p = join_path(&a, base, cu_str_cstr(".config")); assert(p != NULL);
        assert(mkdir(p, 0700) == 0);
        p = join_path(&a, p, cu_str_cstr("git")); assert(p != NULL);
        assert(mkdir(p, 0700) == 0);
        p = join_path(&a, p, cu_str_cstr("ignore")); assert(p != NULL);
        put_source(p, "/homeonly\n", 10);
        assert(setenv("XDG_CONFIG_HOME", "", 1) == 0);
        assert(ignore_seed(&a, sub, &hs, &e));
        assert(ignore_enter(&a, hs, sub, IGNORE_REPOSITORY_BIT, &hf, &e));
        p = join_path(&a, sub, cu_str_cstr("homeonly")); assert(p != NULL);
        assert(ignore_verdict(&a, hf, p, 0, &v, &e) && v == 1);
        assert(ignore_verdict(&a, root, p, 0, &v, &e) && v == 0);
        assert(setenv("XDG_CONFIG_HOME", base, 1) == 0);
    }
#ifndef __wasi__
    {
        const IgnoreFrame *ps, *pf;
        char *actual, *top, *walk, *link, *spelling;
        actual = join_path(&a, base, cu_str_cstr("actual")); assert(actual != NULL);
        assert(mkdir(actual, 0700) == 0);
        top = join_path(&a, actual, cu_str_cstr("top")); assert(top != NULL);
        assert(mkdir(top, 0700) == 0);
        p = join_path(&a, top, cu_str_cstr("child")); assert(p != NULL);
        assert(mkdir(p, 0700) == 0);
        walk = join_path(&a, base, cu_str_cstr("walk")); assert(walk != NULL);
        assert(mkdir(walk, 0700) == 0);
        link = join_path(&a, walk, cu_str_cstr("link")); assert(link != NULL);
        assert(symlink(p, link) == 0);
        p = join_path(&a, actual, cu_str_cstr(".ignore")); assert(p != NULL);
        put_source(p, "/top/physical-ancestor\n", 23);
        p = join_path(&a, top, cu_str_cstr(".ignore")); assert(p != NULL);
        put_source(p, "/hidden\n", 8);
        p = join_path(&a, walk, cu_str_cstr(".ignore")); assert(p != NULL);
        put_source(p, "/lexical-only\n", 14);
        spelling = join_path(&a, link, cu_str_cstr("..")); assert(spelling != NULL);
        assert(ignore_seed(&a, spelling, &ps, &e));
        assert(strcmp(ps->match_root, top) == 0);
        assert(strcmp(ps->display_root, spelling) == 0);
        p = join_path(&a, spelling, cu_str_cstr("physical-ancestor")); assert(p != NULL);
        assert(ignore_verdict(&a, ps, p, 0, &v, &e) && v == 1);
        assert(ignore_enter(&a, ps, spelling, 1U, &pf, &e));
        p = join_path(&a, spelling, cu_str_cstr("hidden")); assert(p != NULL);
        assert(ignore_verdict(&a, pf, p, 0, &v, &e) && v == 1);
        p = join_path(&a, spelling, cu_str_cstr("lexical-only")); assert(p != NULL);
        assert(ignore_verdict(&a, pf, p, 0, &v, &e) && v == 0);
        p = join_path(&a, spelling, cu_str_cstr("missing-root")); assert(p != NULL);
        sentinel = pf;
        assert(!ignore_seed(&a, p, &sentinel, &e));
        assert(e.number == ENOENT && e.path == p && sentinel == pf);
    }
#endif
    p = join_path(&a, sub, cu_str_cstr("homeonly")); assert(p != NULL);
    held = a.held;
    for (i = 0; i < 100; ++i) assert(ignore_verdict(&a, root, p, 0, &v, &e));
    assert(a.held == held);
    t = cu_temp_begin(&a);
    p = join_path(&a, sub, cu_str_cstr("nested")); assert(p != NULL);
    assert(mkdir(p, 0700) == 0);
    p = join_path(&a, p, cu_str_cstr(".ignore")); assert(p != NULL);
    put_source(p, "!ancestor\n", 10);
    p = join_path(&a, sub, cu_str_cstr("nested")); assert(p != NULL);
    assert(ignore_enter(&a, root, p, 1, &child, &e));
    p = join_path(&a, p, cu_str_cstr(".git")); assert(p != NULL);
    put_source(p, "gitdir: deliberately not resolved\n", 33);
    p = join_path(&a, sub, cu_str_cstr("nested")); assert(p != NULL);
    assert(ignore_enter(&a, root, p, 1U | IGNORE_REPOSITORY_BIT, &nested, &e));
    p = join_path(&a, p, cu_str_cstr("git")); assert(p != NULL);
    assert(ignore_verdict(&a, child, p, 0, &v, &e) && v == 1);
    assert(ignore_verdict(&a, nested, p, 0, &v, &e) && v == 0);
    p = join_path(&a, sub, cu_str_cstr("nested/ancestor")); assert(p != NULL);
    assert(ignore_verdict(&a, nested, p, 0, &v, &e) && v == -1);
    p = join_path(&a, sub, cu_str_cstr("nested/rank")); assert(p != NULL);
    assert(ignore_verdict(&a, nested, p, 0, &v, &e) && v == 1);
    cu_temp_end(t);
    test_ignore_budget = 0; v = 42;
    p = join_path(&a, sub, cu_str_cstr("rank")); assert(p != NULL);
    assert(!ignore_verdict(&a, root, p, 0, &v, &e) && e.number == E2BIG && v == 42);
    assert(strcmp(e.path, p) == 0); test_ignore_budget = 1000000UL;
    sentinel = root;
    assert(!ignore_enter(&a, root, "/not-the-root", 0, &sentinel, &e) && sentinel == root);
    p = join_path(&a, sub, cu_str_cstr(".ignore")); assert(p != NULL);
    put_source(p, "a\0b", 3);
    assert(!ignore_enter(&a, seed, sub, 1, &sentinel, &e) && e.number == EINVAL && sentinel == root);
    assert(strcmp(e.path, p) == 0);
    assert(unlink(p) == 0);
    assert(mkdir(p, 0700) == 0);
    assert(!ignore_read(&a, p, &f, &e) && e.number == EINVAL);
    assert(rmdir(p) == 0);
#ifndef __wasi__
    assert(mkfifo(p, 0600) == 0);
    assert(!ignore_read(&a, p, &f, &e) && e.number == EINVAL);
    assert(unlink(p) == 0);
    assert(symlink(".gitignore", p) == 0);
    assert(ignore_read(&a, p, &f, &e) && f.count == 2);
    assert(unlink(p) == 0);
#endif
    assert(ignore_read(&a, p, &f, &e) && f.count == 0);
    test_ignore_source_budget = 3;
    put_source(p, "abc", 3);
    assert(ignore_read(&a, p, &f, &e) && f.count == 1);
    put_source(p, "abcd", 4);
    sentinel = root;
    assert(!ignore_enter(&a, seed, sub, 1, &sentinel, &e));
    assert(e.number == E2BIG && sentinel == root && strcmp(e.path, p) == 0);
    test_ignore_source_budget = 16777216UL;
    assert(unlink(p) == 0);
    test_probe_errno = EIO;
    assert(!ignore_read(&a, p, &f, &e) && e.number == EIO);
    sentinel = root;
    assert(!ignore_seed(&a, sub, &sentinel, &e) && e.number == EIO && sentinel == root);
    assert(e.path != NULL && strlen(e.path) != 0);
    test_probe_errno = 0;
    put_source(p, "x\n", 2);
    {
        CuArena faults;
        const IgnoreFrame *s, *r;
        size_t fail, scope;
        int ok;
        for (fail = 0; fail < 100; ++fail) {
            cu_arena_init_with(&faults, 128, 1);
            assert(ignore_seed(&faults, sub, &s, &e));
            cu_arena_fail_at(&faults, faults.attempts + fail);
            r = root;
            ok = ignore_enter(&faults, s, sub, 1U | IGNORE_REPOSITORY_BIT, &r, &e);
            if (!ok) assert(e.number == ENOMEM && r == root);
            cu_arena_release(&faults);
            if (ok) break;
        }
        assert(fail < 100);
        cu_arena_init_with(&faults, 128, 1);
        assert(ignore_seed(&faults, sub, &s, &e));
        cu_arena_fail_at(&faults, faults.attempts);
        scope = faults.scope;
        v = 42;
        assert(!ignore_verdict(&faults, s, p, 0, &v, &e));
        assert(e.number == ENOMEM && v == 42 && faults.scope == scope);
        cu_arena_release(&faults);
    }
    assert(unlink(p) == 0);
    p = ignore_absolute(&a, "/a/../b//./c", &e);
    assert(p != NULL && strcmp(p, "/b/c") == 0);
    p = ignore_absolute(&a, ".", &e);
    assert(p != NULL && p[0] == '/');
    longroot = cu_arena_push_bytes(&a, 6002); assert(longroot != NULL);
    longroot[0] = '/';
    for (i = 1; i < 6001; ++i) longroot[i] = 'x';
    longroot[6001] = 0;
    /* Synthetic mapping only: this is not a seedable filesystem directory. */
    {
        IgnoreFrame fake;
        char *mapped;
        fake = *root; fake.display_root = longroot; fake.match_root = longroot;
        assert(ignore_map(&a, &fake, longroot, &mapped, &e));
        assert(strlen(mapped) == 6002);
    }
    if (oldhome != NULL) assert(setenv("HOME", oldhome, 1) == 0);
    else assert(unsetenv("HOME") == 0);
    if (oldxdg != NULL) assert(setenv("XDG_CONFIG_HOME", oldxdg, 1) == 0);
    else assert(unsetenv("XDG_CONFIG_HOME") == 0);
    cu_arena_release(&a);
    /* Fixture files intentionally remain in the unique test temporary dir. */
}

static void allocation_cases(void)
{
    CuArena a;
    const IgnoreFrame *f, *out;
    IgnoreFrame marker;
    IgnoreError e;
    size_t fail;
    int ok;
    for (fail = 0; fail < 200; ++fail) {
        cu_arena_init_with(&a, 128, 1);
        cu_arena_fail_at(&a, fail);
        out = &marker;
        ok = ignore_seed(&a, "/tmp", &out, &e);
        if (!ok) assert(e.number == ENOMEM && out == &marker);
        else {
            f = out;
            assert(f != NULL);
            cu_arena_release(&a);
            break;
        }
        cu_arena_release(&a);
    }
    assert(fail < 200);
}
#endif

int main(void)
{
    pure_cases();
    filter_cases();
    filter_work_cases();
#ifndef __cerb__
    filesystem_cases();
    allocation_cases();
#endif
    return 0;
}
