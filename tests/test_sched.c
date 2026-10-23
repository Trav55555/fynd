/* Threaded-walk sensors for src/sched.inc. Run under an external timeout: a
 * broken barrier may deadlock, and elapsed time is not evidence.
 *
 * Every threaded result is compared with the serial walker (-j1) on the same
 * fixture: equal status, equal record multiset, equal diagnostic multiset.
 * With --sort, stdout and stderr must equal the serial bytes exactly.
 * A 64-byte record buffer forces constant flushing and the oversized-record
 * path, and a two-task lookahead keeps ordered helpers at their limit, so the
 * writer often runs tasks itself. Hooks are guarded by one mutex, so they
 * are safe on every worker.
 */
#include "cu.h"
#include <stdio.h>
#include <stdlib.h>
static void *sched_test_alloc(size_t size);
static size_t holds_peak; /* written under the scheduler's Mu */
#define FYND_HOLDS_LIVE(count) (holds_peak = (count) > holds_peak ? (count) : holds_peak)
static int sched_dir_open(CuDir *dir, CuArena *arena, const char *path);
static int sched_dir_next(CuDir *dir, CuDirEntry *entry);
static void sched_dir_close(CuDir *dir);
#define SCHED_BUFFER 64U
#define SCHED_AHEAD 2U
#define FYND_TASK_ALLOC(size) sched_test_alloc(size)
#ifndef __wasi__
/* Native runs pick the CPU count; WASI keeps the production rule (one). */
static long online_cpus = 4;
#define FYND_ONLINE_CPUS() online_cpus
#endif
#define cu_dir_open sched_dir_open
#define cu_dir_next sched_dir_next
#define cu_dir_close sched_dir_close
#define fynd_main sched_test_fynd_main
#include "../src/fynd.c"
#undef fynd_main
#undef cu_dir_open
#undef cu_dir_next
#undef cu_dir_close
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>

#ifndef __cerb__
static CuMutex *hook_mutex;
static long alloc_calls;
static long alloc_fail_at = -1;
static struct { CuDir *dir; int entries; } eio_dirs[64];

static void *sched_test_alloc(size_t size)
{
    int fail;
    cu_mutex_lock(hook_mutex);
    fail = alloc_calls++ == alloc_fail_at;
    cu_mutex_unlock(hook_mutex);
    return fail ? NULL : malloc(size);
}

/* A directory named "eio" yields one entry, then a late EIO. */
static int sched_dir_open(CuDir *dir, CuArena *arena, const char *path)
{
    const char *name;
    int ok;
    size_t i;
    ok = cu_dir_open(dir, arena, path);
    name = strrchr(path, '/');
    if (ok && name != NULL && strcmp(name, "/eio") == 0) {
        cu_mutex_lock(hook_mutex);
        for (i = 0; i < sizeof eio_dirs / sizeof eio_dirs[0]; ++i)
            if (eio_dirs[i].dir == NULL) {
                eio_dirs[i].dir = dir;
                eio_dirs[i].entries = 0;
                break;
            }
        assert(i < sizeof eio_dirs / sizeof eio_dirs[0]);
        cu_mutex_unlock(hook_mutex);
    }
    return ok;
}

static int sched_dir_next(CuDir *dir, CuDirEntry *entry)
{
    size_t i;
    int found, entries, next;
    cu_mutex_lock(hook_mutex);
    found = 0;
    entries = 0;
    for (i = 0; i < sizeof eio_dirs / sizeof eio_dirs[0]; ++i)
        if (eio_dirs[i].dir == dir) {
            found = 1;
            entries = eio_dirs[i].entries;
            break;
        }
    cu_mutex_unlock(hook_mutex);
    if (found && entries != 0) {
        dir->error = EIO;
        dir->state = -1;
        return -1;
    }
    next = cu_dir_next(dir, entry);
    if (found && next == 1) {
        cu_mutex_lock(hook_mutex);
        ++eio_dirs[i].entries;
        cu_mutex_unlock(hook_mutex);
    }
    return next;
}

static void sched_dir_close(CuDir *dir)
{
    size_t i;
    cu_mutex_lock(hook_mutex);
    for (i = 0; i < sizeof eio_dirs / sizeof eio_dirs[0]; ++i)
        if (eio_dirs[i].dir == dir)
            eio_dirs[i].dir = NULL;
    cu_mutex_unlock(hook_mutex);
    cu_dir_close(dir);
}

#ifndef __wasi__
typedef struct Result {
    int status;
    char *out;
    size_t out_len;
    char *err;
    size_t err_len;
} Result;

static char *slurp(FILE *file, size_t *length)
{
    char *bytes;
    long size;
    assert(fflush(file) == 0 || ferror(file));
    assert(fseek(file, 0, SEEK_END) == 0);
    size = ftell(file);
    assert(size >= 0);
    bytes = (char *)malloc((size_t)size + 1);
    assert(bytes != NULL);
    rewind(file);
    *length = fread(bytes, 1, (size_t)size, file);
    assert(*length == (size_t)size);
    bytes[*length] = 0;
    return bytes;
}

/* Run with caller-supplied streams; NULL means a fresh temporary file. */
static Result call_with(char **argv, FILE *out, FILE *err)
{
    Result result;
    int argc;
    int own_out, own_err;
    for (argc = 0; argv[argc] != NULL; ++argc)
        ;
    own_out = out == NULL;
    own_err = err == NULL;
    if (own_out)
        out = tmpfile();
    if (own_err)
        err = tmpfile();
    assert(out != NULL && err != NULL);
    result.status = sched_test_fynd_main(argc, argv, out, err);
    result.out = NULL;
    result.err = NULL;
    result.out_len = 0;
    result.err_len = 0;
    /* Caller-supplied streams stay open for the caller to inspect. */
    if (own_out) {
        result.out = slurp(out, &result.out_len);
        assert(fclose(out) == 0);
    }
    if (own_err) {
        result.err = slurp(err, &result.err_len);
        assert(fclose(err) == 0);
    }
    return result;
}

static Result call(char **argv)
{
    return call_with(argv, NULL, NULL);
}

static void release(Result *result)
{
    free(result->out);
    free(result->err);
}

typedef struct Span {
    const char *ptr;
    size_t len;
} Span;

static int span_compare(const void *a, const void *b)
{
    const Span *x;
    const Span *y;
    size_t n;
    int c;
    x = (const Span *)a;
    y = (const Span *)b;
    n = x->len < y->len ? x->len : y->len;
    c = memcmp(x->ptr, y->ptr, n);
    if (c != 0)
        return c;
    return x->len < y->len ? -1 : x->len > y->len;
}

/* Split delimiter-terminated records; every byte must belong to a record. */
static Span *records(const char *bytes, size_t length, int delimiter, size_t *count)
{
    Span *spans;
    size_t i, start, n;
    assert(length == 0 || bytes[length - 1] == (char)delimiter);
    n = 0;
    for (i = 0; i < length; ++i)
        n += bytes[i] == (char)delimiter;
    spans = (Span *)malloc((n + 1) * sizeof *spans);
    assert(spans != NULL);
    n = 0;
    start = 0;
    for (i = 0; i < length; ++i)
        if (bytes[i] == (char)delimiter) {
            spans[n].ptr = bytes + start;
            spans[n].len = i - start;
            ++n;
            start = i + 1;
        }
    *count = n;
    return spans;
}

static void same_multiset(const char *a, size_t an, const char *b, size_t bn, int delimiter)
{
    Span *x, *y;
    size_t xn, yn, i;
    x = records(a, an, delimiter, &xn);
    y = records(b, bn, delimiter, &yn);
    assert(xn == yn);
    qsort(x, xn, sizeof *x, span_compare);
    qsort(y, yn, sizeof *y, span_compare);
    for (i = 0; i < xn; ++i)
        assert(span_compare(&x[i], &y[i]) == 0);
    free(x);
    free(y);
}

static void same_bytes(const char *a, size_t an, const char *b, size_t bn)
{
    assert(an == bn && (an == 0 || memcmp(a, b, an) == 0));
}

/* Every record of a also occurs in b (a may be any partial result). */
static void subset(const char *a, size_t an, const char *b, size_t bn)
{
    Span *x, *y;
    size_t xn, yn, i;
    x = records(a, an, '\n', &xn);
    y = records(b, bn, '\n', &yn);
    qsort(y, yn, sizeof *y, span_compare);
    for (i = 0; i < xn; ++i)
        assert(bsearch(&x[i], y, yn, sizeof *y, span_compare) != NULL);
    free(x);
    free(y);
}

static void make_dir(const char *path)
{
    assert(mkdir(path, 0700) == 0);
}

static void make_file(const char *path, const char *bytes)
{
    int fd;
    size_t n;
    fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0600);
    assert(fd >= 0);
    n = strlen(bytes);
    assert(write(fd, bytes, n) == (long)n);
    assert(close(fd) == 0);
}

/* big: 8 x 6 directories of 10 files. mixed: ignore rules borrowed across
 * tasks, hidden names, raw newline names and a late-EIO directory. */
static void build_fixture(const char *root)
{
    char path[1024];
    int i, j, k;
    assert(snprintf(path, sizeof path, "%s/big", root) > 0);
    make_dir(path);
    for (i = 0; i < 8; ++i) {
        assert(snprintf(path, sizeof path, "%s/big/d%02d", root, i) > 0);
        make_dir(path);
        for (j = 0; j < 6; ++j) {
            assert(snprintf(path, sizeof path, "%s/big/d%02d/e%02d", root, i, j) > 0);
            make_dir(path);
            for (k = 0; k < 10; ++k) {
                assert(snprintf(path, sizeof path, "%s/big/d%02d/e%02d/f%02d.c",
                                root, i, j, k) > 0);
                make_file(path, "");
            }
        }
    }
    assert(snprintf(path, sizeof path, "%s/mixed", root) > 0);
    make_dir(path);
    assert(snprintf(path, sizeof path, "%s/mixed/.ignore", root) > 0);
    make_file(path, "*.skip\n");
    for (i = 0; i < 4; ++i) {
        assert(snprintf(path, sizeof path, "%s/mixed/s%d", root, i) > 0);
        make_dir(path);
        assert(snprintf(path, sizeof path, "%s/mixed/s%d/inner", root, i) > 0);
        make_dir(path);
        assert(snprintf(path, sizeof path, "%s/mixed/s%d/inner/a.skip", root, i) > 0);
        make_file(path, "");
        assert(snprintf(path, sizeof path, "%s/mixed/s%d/inner/keep.skip", root, i) > 0);
        make_file(path, "");
        assert(snprintf(path, sizeof path, "%s/mixed/s%d/inner/.hidden", root, i) > 0);
        make_file(path, "");
        assert(snprintf(path, sizeof path, "%s/mixed/s%d/inner/line\nname", root, i) > 0);
        make_file(path, "");
    }
    /* A nested rule re-includes keep.skip only under s1, read by other tasks. */
    assert(snprintf(path, sizeof path, "%s/mixed/s1/.ignore", root) > 0);
    make_file(path, "!keep.skip\n");
    assert(snprintf(path, sizeof path, "%s/mixed/s2/eio", root) > 0);
    make_dir(path);
    for (k = 0; k < 5; ++k) {
        assert(snprintf(path, sizeof path, "%s/mixed/s2/eio/g%d", root, k) > 0);
        make_file(path, "");
    }
    assert(snprintf(path, sizeof path, "%s/empty", root) > 0);
    make_dir(path);
    /* 200 sibling directories that each load rules, under a rule root. */
    assert(snprintf(path, sizeof path, "%s/holds", root) > 0);
    make_dir(path);
    assert(snprintf(path, sizeof path, "%s/holds/.ignore", root) > 0);
    make_file(path, "x\n");
    for (i = 0; i < 200; ++i) {
        assert(snprintf(path, sizeof path, "%s/holds/h%03d", root, i) > 0);
        make_dir(path);
        assert(snprintf(path, sizeof path, "%s/holds/h%03d/.ignore", root, i) > 0);
        make_file(path, "y\n");
        assert(snprintf(path, sizeof path, "%s/holds/h%03d/f", root, i) > 0);
        make_file(path, "");
    }
    assert(snprintf(path, sizeof path, "%s/chain", root) > 0);
    make_dir(path);
    for (i = 0; i < FYND_DEPTH_LIMIT + 2; ++i) {
        assert(strlen(path) + 3 < sizeof path);
        strcat(path, "/c");
        make_dir(path);
    }
}

static void remove_tree(const char *path)
{
    struct stat st;
    DIR *dir;
    struct dirent *entry;
    char child[1024];
    assert(lstat(path, &st) == 0);
    if (!S_ISDIR(st.st_mode)) {
        assert(unlink(path) == 0);
        return;
    }
    dir = opendir(path);
    assert(dir != NULL);
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        assert(snprintf(child, sizeof child, "%s/%s", path, entry->d_name) > 0);
        remove_tree(child);
    }
    assert(closedir(dir) == 0);
    assert(rmdir(path) == 0);
}

static void equivalence(const char *root)
{
    static const char *const jobs[] = {"-j2", "-j3", "-j8", "-j32", NULL};
    static const char *const flags[][3] = {
        {NULL}, {"-0", NULL}, {"-H", NULL}, {"--no-ignore", NULL},
        {"-e", "c", NULL}, {"--max-depth", "2", NULL}
    };
    char *argv[8];
    Result serial, threaded;
    size_t f, j, a, k;
    char tree[512];
    int delimiter;
    assert(snprintf(tree, sizeof tree, "%s", root) > 0);
    for (f = 0; f < sizeof flags / sizeof flags[0]; ++f) {
        delimiter = flags[f][0] != NULL && strcmp(flags[f][0], "-0") == 0 ? 0 : '\n';
        for (j = 0; j <= sizeof jobs / sizeof jobs[0] - 1; ++j) {
            a = 0;
            argv[a++] = "fynd";
            argv[a++] = "-j1";
            for (k = 0; flags[f][k] != NULL; ++k)
                argv[a++] = (char *)flags[f][k];
            argv[a++] = "";
            argv[a++] = tree;
            argv[a] = NULL;
            serial = call(argv);
            /* Unfiltered, the reference itself sees the late EIO. */
            if (f == 0)
                assert(serial.status == 1 && serial.err_len != 0);
            if (jobs[j] != NULL)
                argv[1] = (char *)jobs[j];
            else
                memmove(argv + 1, argv + 2, (a - 1) * sizeof *argv); /* default -j */
            threaded = call(argv);
            assert(threaded.status == serial.status);
            same_multiset(threaded.out, threaded.out_len, serial.out, serial.out_len,
                          delimiter);
            same_multiset(threaded.err, threaded.err_len, serial.err, serial.err_len, '\n');
            release(&serial);
            release(&threaded);
        }
    }
}

/* Roots stay contiguous groups in argument order, including duplicates. */
static void root_groups(const char *root)
{
    char a[512], b[512];
    char *argv[7];
    Result whole, first, second;
    Span *spans;
    size_t n, i, group;
    assert(snprintf(a, sizeof a, "%s/big/d00", root) > 0);
    assert(snprintf(b, sizeof b, "%s/big/d01", root) > 0);
    argv[0] = "fynd"; argv[1] = "-j8"; argv[2] = ""; argv[3] = a;
    argv[4] = b; argv[5] = a; argv[6] = NULL;
    whole = call(argv);
    assert(whole.status == 0 && whole.err_len == 0);
    argv[4] = NULL;
    first = call(argv);
    argv[3] = b;
    second = call(argv);
    spans = records(whole.out, whole.out_len, '\n', &n);
    /* Each root: itself, 6 directories and 60 files. */
    assert(n == 3 * 67);
    group = 0;
    for (i = 0; i < n; ++i) {
        if (i == 67 || i == 134)
            ++group;
        assert(spans[i].len >= strlen(a) &&
               memcmp(spans[i].ptr, group == 1 ? b : a, strlen(a)) == 0);
    }
    same_multiset(spans[0].ptr, (size_t)(spans[67].ptr - spans[0].ptr), first.out,
                  first.out_len, '\n');
    same_multiset(spans[67].ptr, (size_t)(spans[134].ptr - spans[67].ptr), second.out,
                  second.out_len, '\n');
    free(spans);
    release(&whole);
    release(&first);
    release(&second);
}

/* Threads first start on a later root: earlier roots barrier with none. */
static void late_launch(const char *root)
{
    char file[512], empty[512], tree[512];
    char *argv[9];
    Result serial, threaded;
    assert(snprintf(file, sizeof file, "%s/big/d00/e00/f00.c", root) > 0);
    assert(snprintf(empty, sizeof empty, "%s/empty", root) > 0);
    assert(snprintf(tree, sizeof tree, "%s/big", root) > 0);
    argv[0] = "fynd"; argv[1] = "-j1"; argv[2] = ""; argv[3] = file; argv[4] = empty;
    argv[5] = tree; argv[6] = empty; argv[7] = tree; argv[8] = NULL;
    serial = call(argv);
    argv[1] = "-j8";
    threaded = call(argv);
    assert(serial.status == 0 && threaded.status == 0 && threaded.err_len == 0);
    /* Leading single-record roots keep their exact positions. */
    assert(threaded.out_len == serial.out_len &&
           memcmp(threaded.out, serial.out, strlen(file) + strlen(empty) + 2) == 0);
    same_multiset(threaded.out, threaded.out_len, serial.out, serial.out_len, '\n');
    release(&serial);
    release(&threaded);
}

/* A finished subtree frees its frames before the root ends: with 4 threads
 * at most one hold per running visit plus the root's own is ever live. */
static void hold_release(const char *root)
{
    char tree[512];
    char *argv[5];
    Result serial, threaded;
    assert(snprintf(tree, sizeof tree, "%s/holds", root) > 0);
    argv[0] = "fynd"; argv[1] = "-j1"; argv[2] = ""; argv[3] = tree; argv[4] = NULL;
    serial = call(argv);
    argv[1] = "-j4";
    holds_peak = 0;
    threaded = call(argv);
    assert(threaded.status == 0 && serial.status == 0 && threaded.err_len == 0);
    same_multiset(threaded.out, threaded.out_len, serial.out, serial.out_len, '\n');
    assert(holds_peak >= 2 && holds_peak <= 5);
    release(&serial);
    release(&threaded);
}

static void depth_limits(const char *root)
{
    char tree[512];
    char *argv[7];
    Result result;
    size_t n;
    Span *spans;
    assert(snprintf(tree, sizeof tree, "%s/chain", root) > 0);
    argv[0] = "fynd"; argv[1] = "-j4"; argv[2] = ""; argv[3] = tree; argv[4] = NULL;
    result = call(argv);
    assert(result.status == 1);
    assert(result.err_len > 36 &&
           memcmp(result.err, "fynd: recursion safety limit '", 30) == 0 &&
           memchr(result.err, '\n', result.err_len) == result.err + result.err_len - 1);
    spans = records(result.out, result.out_len, '\n', &n);
    assert(n == FYND_DEPTH_LIMIT + 1);
    free(spans);
    release(&result);
    argv[2] = "--max-depth"; argv[3] = "256"; argv[4] = ""; argv[5] = tree; argv[6] = NULL;
    result = call(argv);
    assert(result.status == 0 && result.err_len == 0);
    spans = records(result.out, result.out_len, '\n', &n);
    assert(n == FYND_DEPTH_LIMIT + 1);
    free(spans);
    release(&result);
}

static void output_failures(const char *root)
{
    static const char expected[] = "fynd: output failure\nfynd: output flush failure\n";
    char tree[512];
    char *argv[5];
    Result result;
    FILE *out, *err;
    assert(snprintf(tree, sizeof tree, "%s/big", root) > 0);
    argv[0] = "fynd"; argv[1] = "-j8"; argv[2] = ""; argv[3] = tree; argv[4] = NULL;
    /* Many workers fail while writing; exactly one report. */
    out = fopen("/dev/full", "w");
    if (out != NULL) {
        err = tmpfile();
        assert(err != NULL);
        result = call_with(argv, out, err);
        assert(result.status == 1);
        result.err = slurp(err, &result.err_len);
        assert(result.err_len == sizeof expected - 1 &&
               memcmp(result.err, expected, sizeof expected - 1) == 0);
        assert(fclose(err) == 0);
        free(result.err);
    }
    /* A record still buffered at the root barrier fails there. */
    assert(snprintf(tree, sizeof tree, "%s/empty", root) > 0);
    out = fopen("/dev/full", "w");
    if (out != NULL) {
        assert(setvbuf(out, NULL, _IONBF, 0) == 0);
        err = tmpfile();
        assert(err != NULL);
        result = call_with(argv, out, err);
        assert(result.status == 1);
        result.err = slurp(err, &result.err_len);
        assert(result.err_len == sizeof expected - 1 &&
               memcmp(result.err, expected, sizeof expected - 1) == 0);
        assert(fclose(err) == 0);
        free(result.err);
    }
    /* A diagnostic write failure is terminal and observed on err. */
    assert(snprintf(tree, sizeof tree, "%s/mixed", root) > 0);
    err = fopen("/dev/full", "w");
    if (err != NULL) {
        assert(setvbuf(err, NULL, _IONBF, 0) == 0);
        out = tmpfile();
        assert(out != NULL);
        result = call_with(argv, out, err);
        assert(result.status == 1 && ferror(err));
        result.out = slurp(out, &result.out_len);
        free(result.out);
        assert(fclose(out) == 0);
    }
}

static void resource_failures(const char *root)
{
    static const char workers[] = "fynd: cannot start listing workers\n";
    char tree[512];
    char *argv[5];
    Result reference, result;
    long at;
    assert(snprintf(tree, sizeof tree, "%s/big", root) > 0);
    argv[0] = "fynd"; argv[1] = "-j1"; argv[2] = ""; argv[3] = tree; argv[4] = NULL;
    reference = call(argv);
    assert(reference.status == 0);
    argv[1] = "-j4";
    /* Task allocation failure at several points: terminal, one report,
     * partial output only, every queued task dropped without a hang. */
    for (at = 0; at < 40; at += 7) {
        cu_mutex_lock(hook_mutex);
        alloc_calls = 0;
        alloc_fail_at = at;
        cu_mutex_unlock(hook_mutex);
        result = call(argv);
        assert(result.status == 1);
        assert(memcmp(result.err, "fynd: allocation failure '", 26) == 0 &&
               memchr(result.err, '\n', result.err_len) == result.err + result.err_len - 1);
        subset(result.out, result.out_len, reference.out, reference.out_len);
        release(&result);
    }
    cu_mutex_lock(hook_mutex);
    alloc_fail_at = -1;
    cu_mutex_unlock(hook_mutex);
    /* Primitive initialization fails before traversal: no output. */
    cu_thread_fail_at(0);
    result = call(argv);
    cu_thread_fail_at((size_t)-1);
    assert(result.status == 1 && result.out_len == 0);
    assert(result.err_len == sizeof workers - 1 &&
           memcmp(result.err, workers, sizeof workers - 1) == 0);
    release(&result);
    /* Without -j the default uses the online CPU count (4 here): threaded. */
    argv[1] = "";
    argv[2] = tree;
    argv[3] = NULL;
    cu_thread_fail_at(0);
    result = call(argv);
    cu_thread_fail_at((size_t)-1);
    assert(result.status == 1 && result.out_len == 0);
    assert(result.err_len == sizeof workers - 1 &&
           memcmp(result.err, workers, sizeof workers - 1) == 0);
    release(&result);
    argv[1] = "-j4";
    argv[2] = "";
    argv[3] = tree;
    /* A lazy thread start fails mid-walk (first, second or third thread, so
     * earlier ones are already waiting): one report, no serial fallback. */
    for (at = 3; at <= 5; ++at) {
        cu_thread_fail_at((size_t)at);
        result = call(argv);
        cu_thread_fail_at((size_t)-1);
        assert(result.status == 1);
        assert(result.err_len == sizeof workers - 1 &&
               memcmp(result.err, workers, sizeof workers - 1) == 0);
        subset(result.out, result.out_len, reference.out, reference.out_len);
        release(&result);
    }
    release(&reference);
}

/* --sort with threads writes exactly the serial bytes on both streams: late
 * EIO, the depth safety limit and borrowed rule frames included, across
 * repeated runs and repeated roots. */
static void ordered_equivalence(const char *root)
{
    static const char *const jobs[] = {"-j2", "-j3", "-j8", "-j32", NULL};
    static const char *const flags[][3] = {
        {NULL}, {"-0", NULL}, {"-H", NULL}, {"--no-ignore", NULL},
        {"-e", "c", NULL}, {"--max-depth", "2", NULL}
    };
    char *argv[12];
    Result serial, threaded;
    size_t f, j, a, k, roots;
    int repeat;
    char tree[512], big[512];
    assert(snprintf(tree, sizeof tree, "%s", root) > 0);
    assert(snprintf(big, sizeof big, "%s/big", root) > 0);
    for (f = 0; f < sizeof flags / sizeof flags[0]; ++f) {
        for (roots = 1; roots <= 2; ++roots) {
            a = 0;
            argv[a++] = "fynd";
            argv[a++] = "-j1";
            argv[a++] = "--sort";
            for (k = 0; flags[f][k] != NULL; ++k)
                argv[a++] = (char *)flags[f][k];
            argv[a++] = "";
            argv[a++] = tree;
            if (roots == 2) {
                argv[a++] = big;
                argv[a++] = tree;
            }
            argv[a] = NULL;
            serial = call(argv);
            if (f == 0)
                assert(serial.status == 1 && serial.err_len != 0);
            for (j = 0; j < sizeof jobs / sizeof jobs[0]; ++j) {
                for (repeat = 0; repeat < 3; ++repeat) {
                    if (jobs[j] != NULL)
                        argv[1] = (char *)jobs[j];
                    else
                        argv[1] = "--sort"; /* default -j, --sort twice */
                    threaded = call(argv);
                    assert(threaded.status == serial.status);
                    same_bytes(threaded.out, threaded.out_len, serial.out, serial.out_len);
                    same_bytes(threaded.err, threaded.err_len, serial.err, serial.err_len);
                    release(&threaded);
                }
            }
            release(&serial);
        }
    }
}

/* Output and resource failures stop an ordered walk where the serial walk
 * would see them: exact reports, and stdout a prefix of the full walk. */
static void ordered_failures(const char *root)
{
    static const char expected[] = "fynd: output failure\nfynd: output flush failure\n";
    static const char workers[] = "fynd: cannot start listing workers\n";
    char tree[512];
    char *argv[6];
    Result reference, result;
    FILE *out, *err;
    long at;
    assert(snprintf(tree, sizeof tree, "%s/big", root) > 0);
    argv[0] = "fynd"; argv[1] = "-j1"; argv[2] = "--sort"; argv[3] = "";
    argv[4] = tree; argv[5] = NULL;
    reference = call(argv);
    assert(reference.status == 0 && reference.err_len == 0);
    argv[1] = "-j8";
    out = fopen("/dev/full", "w");
    if (out != NULL) {
        err = tmpfile();
        assert(err != NULL);
        result = call_with(argv, out, err);
        assert(result.status == 1);
        result.err = slurp(err, &result.err_len);
        same_bytes(result.err, result.err_len, expected, sizeof expected - 1);
        assert(fclose(err) == 0);
        free(result.err);
    }
    argv[1] = "-j4";
    for (at = 0; at < 60; at += 3) {
        cu_mutex_lock(hook_mutex);
        alloc_calls = 0;
        alloc_fail_at = at;
        cu_mutex_unlock(hook_mutex);
        result = call(argv);
        assert(result.status == 1);
        assert(memcmp(result.err, "fynd: allocation failure '", 26) == 0 &&
               memchr(result.err, '\n', result.err_len) == result.err + result.err_len - 1);
        assert(result.out_len < reference.out_len &&
               memcmp(result.out, reference.out, result.out_len) == 0);
        release(&result);
    }
    cu_mutex_lock(hook_mutex);
    alloc_fail_at = -1;
    cu_mutex_unlock(hook_mutex);
    for (at = 3; at <= 5; ++at) {
        cu_thread_fail_at((size_t)at);
        result = call(argv);
        cu_thread_fail_at((size_t)-1);
        assert(result.status == 1);
        same_bytes(result.err, result.err_len, workers, sizeof workers - 1);
        assert(result.out_len < reference.out_len &&
               memcmp(result.out, reference.out, result.out_len) == 0);
        release(&result);
    }
    release(&reference);
}

static void default_thread_count(void)
{
    online_cpus = 0;
    assert(default_threads() == 1);
    online_cpus = 64;
    assert(default_threads() == 32);
    online_cpus = 6;
    assert(default_threads() == 6);
    online_cpus = 4;
}
#endif /* __wasi__ */
#endif /* __cerb__ */

int main(void)
{
#if defined(__wasi__) && !defined(__cerb__)
    Sched sched;
    Query query;
    int started;
    memset(&query, 0, sizeof query);
    errno = 0;
    started = sched_start(&sched, 2, &query, stdout, stderr);
    /* A working WASI thread backend must enable the fixtures below. */
    assert(!started && errno == ENOSYS);
    sched_finish(&sched);
    assert(default_threads() == 1);
    puts("scheduler fixtures skipped: actual ENOSYS thread backend");
#elif !defined(__cerb__)
    CuArena arena;
    char template[] = "/tmp/fynd-sched-XXXXXX";
    char *root;
    cu_arena_init(&arena);
    assert(cu_mutex_init(&hook_mutex, &arena));
    root = mkdtemp(template);
    assert(root != NULL);
    build_fixture(root);
    default_thread_count();
    equivalence(root);
    root_groups(root);
    late_launch(root);
    hold_release(root);
    depth_limits(root);
    output_failures(root);
    resource_failures(root);
    ordered_equivalence(root);
    ordered_failures(root);
    remove_tree(root);
    puts("scheduler tests passed");
    cu_mutex_destroy(hook_mutex);
    cu_arena_release(&arena);
#else
    puts("native threads unavailable under __cerb__; scheduler suite skipped");
#endif
    return 0;
}
