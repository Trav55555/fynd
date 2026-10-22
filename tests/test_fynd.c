#include "cu.h"
static int test_dir_next(CuDir *dir, CuDirEntry *entry);
#define cu_dir_next test_dir_next
/* Rename both the included prototype and body when dcheck also links src/fynd.c. */
#define fynd_main test_fynd_main
/* These sensors use process-global hooks and serial diagnostic order. The default
 * thread count is pinned to one; tests/test_sched.c covers threaded walks. */
#define FYND_ONLINE_CPUS() 1L
#include "../src/fynd.c"
#undef cu_dir_next
#include <assert.h>
#ifndef __cerb__
#include <unistd.h>
#include <fcntl.h>
#endif

static int listing_countdown = -1;
static char listing_first[256];

#ifndef FYND_TEST_DIR_NEXT
#define FYND_TEST_DIR_NEXT(dir, entry, result) 0
#endif

static int test_dir_next(CuDir *dir, CuDirEntry *entry)
{
    int next;
    int capture;
    if (FYND_TEST_DIR_NEXT(dir, entry, &next))
        return next;
    if (listing_countdown == 0) {
        dir->error = EIO;
        dir->state = -1;
        return -1;
    }
    capture = listing_countdown > 0;
    if (capture)
        --listing_countdown;
    next = cu_dir_next(dir, entry);
    if (capture && next == 1 && listing_first[0] == 0) {
        assert(entry->name.len < sizeof listing_first);
        memcpy(listing_first, entry->name.ptr, entry->name.len);
        listing_first[entry->name.len] = 0;
    }
    return next;
}

#ifndef __cerb__
static FILE *test_tmpfile(void)
{
#ifdef __wasi__
    static unsigned int serial;
    char path[80];
    FILE *file;
    assert(snprintf(path, sizeof path, "/tmp/fynd-test-stream-%u", serial++) > 0);
    file = fopen(path, "w+");
    if (file != NULL)
        assert(unlink(path) == 0);
    return file;
#else
    return tmpfile();
#endif
}

#define tmpfile test_tmpfile

static void expect_bytes(int argc, char **argv, int status,
                         const char *expected, size_t length)
{
    FILE *out;
    FILE *err;
    char buffer[4096];
    size_t n;
    out = tmpfile();
    err = tmpfile();
    assert(out != NULL && err != NULL);
    assert(fynd_main(argc, argv, out, err) == status);
    assert(fseek(out, 0, SEEK_SET) == 0);
    n = fread(buffer, 1, sizeof buffer, out);
    assert(!ferror(out) && n == length);
    assert(memcmp(buffer, expected, n) == 0);
    assert(fclose(out) == 0);
    assert(fclose(err) == 0);
}

static void expect_call(int argc, char **argv, int status, const char *expected)
{
    expect_bytes(argc, argv, status, expected, strlen(expected));
}

/* Independent token multiset: one occurrence is consumed per complete path.
 * Expected records are NUL terminated even when actual paths use raw LF.
 * Backtracking handles LF inside names without treating it as a boundary. */
static int record_match(const char *bytes, size_t length, const char **paths,
                        size_t *sizes, unsigned char *used, size_t count,
                        size_t left, int delimiter)
{
    size_t i, n;
    if (left == 0)
        return length == 0;
    for (i = 0; i < count; ++i) {
        n = sizes[i];
        if (!used[i] && n < length &&
            memcmp(bytes, paths[i], n) == 0 &&
            (unsigned char)bytes[n] == (unsigned char)delimiter) {
            used[i] = 1;
            if (record_match(bytes + n + 1, length - n - 1, paths, sizes,
                             used, count, left - 1, delimiter))
                return 1;
            used[i] = 0;
        }
    }
    return 0;
}

static void stream_record_group(FILE *file, const char *expected, size_t length,
                                int delimiter)
{
    char bytes[4096];
    const char *paths[64];
    const char *end;
    size_t sizes[64];
    unsigned char used[64];
    size_t at, count;
    assert(length < sizeof bytes);
    assert(fread(bytes, 1, length, file) == length && !ferror(file));
    at = count = 0;
    while (at < length) {
        assert(count < sizeof paths / sizeof paths[0]);
        end = memchr(expected + at, 0, length - at);
        assert(end != NULL);
        paths[count] = expected + at;
        sizes[count] = (size_t)(end - paths[count]);
        used[count] = 0;
        at += sizes[count++] + 1;
    }
    assert(record_match(bytes, length, paths, sizes, used, count, count,
                        delimiter));
}

static void stream_record_groups(FILE *file, const char *expected, size_t length,
                                 int delimiter, size_t groups)
{
    size_t i;
    assert(fflush(file) == 0 && fseek(file, 0, SEEK_SET) == 0);
    for (i = 0; i < groups; ++i)
        stream_record_group(file, expected, length, delimiter);
    assert(fgetc(file) == EOF && !ferror(file));
}

static void expect_record_groups(int argc, char **argv, int status,
                                 const char *expected, size_t length,
                                 int delimiter, size_t groups)
{
    FILE *out, *err;
    out = tmpfile(); err = tmpfile();
    assert(out != NULL && err != NULL);
    assert(fynd_main(argc, argv, out, err) == status);
    stream_record_groups(out, expected, length, delimiter, groups);
    assert(fclose(out) == 0 && fclose(err) == 0);
}

/* Only fixtures whose known path names contain no LF use this adapter. */
static void expect_line_set(int argc, char **argv, int status,
                            const char *expected)
{
    char records[4096];
    size_t i, n;
    n = strlen(expected);
    assert(n < sizeof records);
    for (i = 0; i < n; ++i)
        records[i] = expected[i] == '\n' ? 0 : expected[i];
    expect_record_groups(argc, argv, status, records, n, '\n', 1);
}
#endif

static void pure_tests(void)
{
    char name[3] = {'x', 'A', 'z'};
    char yes[2] = {'a', 'z'};
    char no[2] = {'z', 'A'};
    size_t size;
    int depth;
    Query q;
    CuArena arena;
    CuStr ext;
    char *joined;
    char high[2] = {(char)0xc1, 'Z'};
    char low[2] = {(char)0xe1, 'z'};
    char *args[4];
    const char *invalid[] = {"", "ff", "x"};
    size_t i;
    assert(!contains(cu_str_n(high, 2), cu_str_n(low, 2), 1));
    assert(contains(cu_str_n(name, 3), cu_str_n(yes, 2), 1));
    assert(!contains(cu_str_n(name, 3), cu_str_n(yes, 2), 0));
    assert(!contains(cu_str_n(name, 3), cu_str_n(no, 2), 1));
    assert(!contains(cu_str_n(yes, 2), cu_str_n(name, 3), 1));
    assert(contains(cu_str_n(name, 3), cu_str_n("", 0), 0));
    assert(joined_size(4, 7, 1, &size) && size == 13);
    assert(joined_size((size_t)-1 - 2, 0, 1, &size) && size == (size_t)-1);
    assert(!joined_size((size_t)-1 - 1, 0, 1, &size));
    assert(!joined_size(0, (size_t)-1, 0, &size));
    assert(joined_size((size_t)-1 - 1, 0, 0, &size) && size == (size_t)-1);
    assert(!joined_size((size_t)-1, 0, 0, &size));
    assert(parse_depth("000128", &depth) && depth == 128);
    assert(parse_depth("0", &depth) && depth == 0);
    assert(!parse_depth("129", &depth));
    assert(!parse_depth("-1", &depth));
    assert(!parse_depth("1x", &depth));
    assert(!parse_depth("", &depth));
    assert(parse_threads("0001", &depth) && depth == 1);
    assert(parse_threads("32", &depth) && depth == 32);
    assert(!parse_threads("0", &depth));
    assert(!parse_threads("33", &depth));
    assert(!parse_threads("+2", &depth));
    assert(!parse_threads("999999999999999999999", &depth));
    assert(!parse_threads("2x", &depth));
    assert(!parse_threads("", &depth));
    cu_arena_init_with(&arena, 64, 1);
    joined = join_path(&arena, "ab", cu_str_n("xyz", 3));
    assert(joined != NULL && strcmp(joined, "ab/xyz") == 0);
    joined = join_path(&arena, "/", cu_str_n("x", 1));
    assert(joined != NULL && strcmp(joined, "/x") == 0);
    assert(parse(&q, &arena, 1, NULL) == 0);
    ext = cu_str_n("json", 4);
    CU_VEC_PUSH(&q.extensions, &ext);
    assert(selected(&q, cu_str_n(".config.json", 12), CU_DIR_DIR));
    assert(!selected(&q, cu_str_n(".json", 5), CU_DIR_FILE));
    assert(!selected(&q, cu_str_n("a.", 2), CU_DIR_FILE));
    assert(!selected(&q, cu_str_n("a.jsonx", 7), CU_DIR_FILE));
    args[0] = "fynd"; args[1] = "-t";
    assert(parse(&q, &arena, 2, args) == 2);
    for (i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        args[2] = (char *)invalid[i];
        assert(parse(&q, &arena, 3, args) == 2);
    }
    args[1] = "-e";
    assert(parse(&q, &arena, 2, args) == 2);
    args[2] = ""; assert(parse(&q, &arena, 3, args) == 2);
    args[2] = "a.b"; assert(parse(&q, &arena, 3, args) == 2);
    args[2] = "a/b"; assert(parse(&q, &arena, 3, args) == 2);
    args[1] = "--"; args[2] = "-literal";
    assert(parse(&q, &arena, 3, args) == 0);
    assert(q.pattern.len == 8 && q.roots.len == 1);
    assert(q.threads == 0 && !q.no_ignore);
    args[1] = "-j";
    assert(parse(&q, &arena, 2, args) == 2);
    args[2] = "32";
    assert(parse(&q, &arena, 3, args) == 0 && q.threads == 32);
    args[1] = "--threads"; args[2] = "0";
    assert(parse(&q, &arena, 3, args) == 2);
    args[1] = "-j2"; args[2] = "--no-ignore";
    assert(parse(&q, &arena, 3, args) == 0 && q.threads == 2 && q.no_ignore);
    /* --sort is serial: an explicit -j above 1 conflicts in either order. */
    args[1] = "--sort"; args[2] = "x";
    assert(parse(&q, &arena, 3, args) == 0 && q.sort && q.threads == 0);
    args[1] = "--sort"; args[2] = "-j2";
    assert(parse(&q, &arena, 3, args) == 2);
    args[1] = "-j2"; args[2] = "--sort";
    assert(parse(&q, &arena, 3, args) == 2);
    args[1] = "-j1"; args[2] = "--sort";
    assert(parse(&q, &arena, 3, args) == 0 && q.sort && q.threads == 1);
    cu_arena_release(&arena);
}

#ifndef __cerb__
static void failures(char *root)
{
    CuArena arena;
    FILE *out;
    FILE *err;
    size_t i;
    size_t attempts;
    size_t base;
    int result;
    char *args[7];
    args[0] = "fynd";
    args[1] = "-e";
    args[2] = "txt";
    args[3] = "";
    args[4] = root;
    args[5] = NULL;
    for (i = 0; ; ++i) {
        cu_arena_init_with(&arena, 64, 1);
        base = cu_arena_scope(&arena);
        cu_arena_fail_at(&arena, i);
        out = tmpfile();
        err = tmpfile();
        assert(out != NULL && err != NULL);
        result = fynd_run(5, args, out, err, &arena);
        attempts = arena.attempts;
        assert(cu_arena_scope(&arena) == base);
        if (attempts > i)
            assert(result == 1 && cu_arena_failed(&arena));
        else
            assert(result == 0 && !cu_arena_failed(&arena));
        cu_arena_release(&arena);
        assert(arena.held == 0 && arena.cached == 0);
        assert(fclose(out) == 0);
        assert(fclose(err) == 0);
        expect_call(5, args, 0, "");
        if (attempts <= i)
            break;
        assert(i < 1000);
    }
    cu_arena_init_with(&arena, 64, 1);
    cu_arena_set_limit(&arena, 0);
    out = tmpfile();
    err = tmpfile();
    assert(out != NULL && err != NULL);
    assert(fynd_run(5, args, out, err, &arena) == 1);
    assert(cu_arena_failed(&arena));
    cu_arena_release(&arena);
    assert(fclose(out) == 0 && fclose(err) == 0);
}

static void io_failures(void)
{
#ifdef __linux__
    FILE *out;
    FILE *err;
    char *args[4];
    char buffer[1024];
    args[0] = "fynd";
    args[1] = "--version";
    args[2] = NULL;
    out = fopen("/dev/full", "w");
    if (out != NULL) {
        err = tmpfile();
        assert(err != NULL);
        assert(setvbuf(out, buffer, _IOFBF, sizeof buffer) == 0);
        assert(fynd_main(2, args, out, err) == 1);
        (void)fclose(out);
        assert(fclose(err) == 0);
        out = fopen("/dev/full", "w");
        err = tmpfile();
        assert(out != NULL && err != NULL);
        assert(setvbuf(out, NULL, _IONBF, 0) == 0);
        assert(fynd_main(2, args, out, err) == 1);
        assert(ferror(out));
        (void)fclose(out);
        assert(fclose(err) == 0);
        out = fopen("/dev/full", "w"); err = tmpfile();
        assert(out != NULL && err != NULL);
        assert(setvbuf(out, NULL, _IONBF, 0) == 0);
        args[1] = ""; args[2] = "/dev/null";
        assert(fynd_main(3, args, out, err) == 1 && ferror(out));
        (void)fclose(out); assert(fclose(err) == 0);
        out = tmpfile(); err = fopen("/dev/full", "w");
        assert(out != NULL && err != NULL);
        assert(setvbuf(err, NULL, _IONBF, 0) == 0);
        args[2] = "/no-such-root";
        assert(fynd_main(3, args, out, err) == 1 && ferror(err));
        assert(fclose(out) == 0); (void)fclose(err);
    }
#endif
}

static void preexisting_errors(char *root)
{
    FILE *out;
    FILE *err;
    char *args[4];
    char byte;
    int i;
#ifdef __wasi__
    char sink[512];
    assert(snprintf(sink, sizeof sink, "%s/b", root) > 0);
#else
    const char *sink = "/dev/null";
#endif
    args[0] = "fynd"; args[1] = ""; args[2] = root; args[3] = NULL;
    for (i = 0; i < 4; ++i) {
        out = tmpfile(); err = tmpfile();
        assert(out != NULL && err != NULL);
        /* A read on a write-only stream sets a sticky error without pending output. */
        assert(freopen(sink, "w", out) == out);
        assert(fread(&byte, 1, 1, out) == 0 && ferror(out));
        if (i == 1) args[1] = "no-match";
        if (i == 2) args[1] = "--version";
        if (i == 3) args[1] = "--bad";
        listing_countdown = 0;
        assert(fynd_main(i >= 2 ? 2 : 3, args, out, err) == (i == 3 ? 2 : 1));
        assert(listing_countdown == 0 && ferror(out));
        assert(ftell(out) == 0);
        listing_countdown = -1;
        assert(fclose(out) == 0 && fclose(err) == 0);
    }
    out = tmpfile(); err = tmpfile();
    assert(out != NULL && err != NULL);
    assert(freopen(sink, "w", err) == err);
    assert(fread(&byte, 1, 1, err) == 0 && ferror(err));
    args[1] = "--bad";
    assert(fynd_main(2, args, out, err) == 1 && ferror(err));
    assert(fclose(out) == 0 && fclose(err) == 0);
}

/* --sort: exact byte-sorted preorder (A < a < ab < b), hidden only with -H,
 * and every allocation failure while reading a directory whole. */
static void sort_cases(char *root)
{
    static const char *const names[] = {"b", "ab", "A", ".h"};
    char dir[1024], path[1024], expected[4096];
    char *args[7];
    CuArena arena;
    FILE *out, *err;
    size_t i, attempts;
    int fd, result;
    assert(snprintf(dir, sizeof dir, "%s/sorted", root) > 0);
    assert(mkdir(dir, 0700) == 0);
    assert(snprintf(path, sizeof path, "%s/a", dir) > 0);
    assert(mkdir(path, 0700) == 0);
    assert(snprintf(path, sizeof path, "%s/a/z", dir) > 0);
    fd = open(path, O_CREAT | O_WRONLY, 0600);
    assert(fd >= 0 && close(fd) == 0);
    for (i = 0; i < sizeof names / sizeof names[0]; ++i) {
        assert(snprintf(path, sizeof path, "%s/%s", dir, names[i]) > 0);
        fd = open(path, O_CREAT | O_WRONLY, 0600);
        assert(fd >= 0 && close(fd) == 0);
    }
    args[0] = "fynd"; args[1] = "--sort"; args[2] = ""; args[3] = dir; args[4] = NULL;
    assert(snprintf(expected, sizeof expected, "%s\n%s/A\n%s/a\n%s/a/z\n%s/ab\n%s/b\n",
                    dir, dir, dir, dir, dir, dir) > 0);
    expect_call(4, args, 0, expected);
    args[1] = "-H"; args[2] = "--sort"; args[3] = ""; args[4] = dir; args[5] = NULL;
    assert(snprintf(expected, sizeof expected,
                    "%s\n%s/.h\n%s/A\n%s/a\n%s/a/z\n%s/ab\n%s/b\n",
                    dir, dir, dir, dir, dir, dir, dir) > 0);
    expect_call(5, args, 0, expected);
    args[1] = "--sort"; args[2] = ""; args[3] = dir; args[4] = NULL;
    for (i = 0; ; ++i) {
        cu_arena_init_with(&arena, 64, 1);
        cu_arena_fail_at(&arena, i);
        out = tmpfile();
        err = tmpfile();
        assert(out != NULL && err != NULL);
        result = fynd_run(4, args, out, err, &arena);
        attempts = arena.attempts;
        if (attempts > i)
            assert(result == 1 && cu_arena_failed(&arena));
        else
            assert(result == 0 && !cu_arena_failed(&arena));
        cu_arena_release(&arena);
        assert(fclose(out) == 0 && fclose(err) == 0);
        if (attempts <= i)
            break;
        assert(i < 1000);
    }
    for (i = 0; i < sizeof names / sizeof names[0]; ++i) {
        assert(snprintf(path, sizeof path, "%s/%s", dir, names[i]) > 0);
        assert(unlink(path) == 0);
    }
    assert(snprintf(path, sizeof path, "%s/a/z", dir) > 0);
    assert(unlink(path) == 0);
    assert(snprintf(path, sizeof path, "%s/a", dir) > 0);
    assert(rmdir(path) == 0);
    assert(rmdir(dir) == 0);
}

static void success_cases(char *root)
{
    char path[1024];
    char expected[4096];
    char *args[18];
    int fd;
    int i;
    size_t n;
    args[0] = "fynd";
    assert(snprintf(path, sizeof path, "%s/.hide", root) > 0);
    assert(mkdir(path, 0700) == 0);
    assert(snprintf(path, sizeof path, "%s/.hide/UP.TXT", root) > 0);
    fd = open(path, O_CREAT | O_WRONLY, 0600);
    assert(fd >= 0 && close(fd) == 0);
    args[1] = "--ignore-case"; args[2] = "-i";
    args[3] = "--hidden"; args[4] = "-H";
    args[5] = "--extension"; args[6] = "none";
    args[7] = "-e"; args[8] = "txt";
    args[9] = "--type"; args[10] = "d";
    args[11] = "-t"; args[12] = "f";
    args[13] = "up"; args[14] = root;
    assert(snprintf(expected, sizeof expected, "%s/.hide/UP.TXT\n", root) > 0);
    expect_call(15, args, 0, expected);
    args[1] = ""; args[2] = path;
    expect_call(3, args, 0, expected);
    args[1] = "--max-depth"; args[2] = "1";
    args[3] = "--max-depth"; args[4] = "0";
    args[5] = ""; args[6] = root;
    assert(snprintf(expected, sizeof expected, "%s\n", root) > 0);
    expect_call(7, args, 0, expected);
    args[1] = "-t"; args[2] = "d"; args[3] = ""; args[4] = root;
    assert(snprintf(expected, sizeof expected, "%s\n%s/a\n", root, root) > 0);
    expect_call(5, args, 0, expected);
    args[1] = "--print0"; args[2] = "--max-depth"; args[3] = "0";
    args[4] = "--"; args[5] = ""; args[6] = root; args[7] = root;
    n = strlen(root);
    memcpy(expected, root, n + 1);
    memcpy(expected + n + 1, root, n + 1);
    expect_bytes(8, args, 0, expected, 2 * (n + 1));
    args[1] = ""; args[2] = root;
    assert(snprintf(expected, sizeof expected, "%s\n%s/a\n%s/a/z\n%s/b\n",
                    root, root, root, root) > 0);
    expect_line_set(3, args, 0, expected);
    assert(snprintf(path, sizeof path, "%s/.hide", root) > 0);
    args[2] = path;
    assert(snprintf(expected, sizeof expected, "%s/.hide\n%s/.hide/UP.TXT\n", root, root) > 0);
    expect_call(3, args, 0, expected);
    assert(snprintf(path, sizeof path, "%s/.hide/UP.TXT", root) > 0);
    assert(unlink(path) == 0);
    assert(snprintf(path, sizeof path, "%s/.hide", root) > 0);
    assert(rmdir(path) == 0);
    assert(snprintf(path, sizeof path, "%s/link", root) > 0);
    assert(symlink("missing", path) == 0);
    args[1] = "-t"; args[2] = "l"; args[3] = ""; args[4] = root;
    assert(snprintf(expected, sizeof expected, "%s/link\n", root) > 0);
    expect_call(5, args, 0, expected);
    assert(snprintf(path, sizeof path, "%s/link///", root) > 0);
    args[1] = ""; args[2] = path;
    expect_call(3, args, 0, expected);
    assert(snprintf(path, sizeof path, "%s/link", root) > 0);
    assert(unlink(path) == 0);
/* WASI preview1 has symlinks, but no FIFO creation API. */
#ifndef __wasi__
    assert(mkfifo(path, 0600) == 0);
    args[1] = "-t"; args[2] = "o"; args[3] = ""; args[4] = root;
    expect_call(5, args, 0, expected);
    args[1] = ""; args[2] = path;
    expect_call(3, args, 0, expected);
    assert(unlink(path) == 0);
#endif
    assert(snprintf(path, sizeof path, "%s/deep", root) > 0);
    assert(mkdir(path, 0700) == 0);
    n = strlen(path);
    for (i = 0; i < 128; ++i) {
        memcpy(path + n, "/d", 3); n += 2;
        assert(mkdir(path, 0700) == 0);
    }
    args[1] = "--max-depth"; args[2] = "128";
    args[3] = "no-match";
    path[n - 256] = 0; args[4] = path;
    expect_call(5, args, 0, "");
    args[1] = "no-match"; args[2] = path;
    expect_call(3, args, 1, "");
    path[n - 256] = '/';
    for (i = 0; i < 128; ++i) {
        assert(rmdir(path) == 0); n -= 2; path[n] = 0;
    }
    assert(rmdir(path) == 0);
}
#endif

int main(void)
{
#ifndef __cerb__
#ifdef __wasi__
    char template[] = "/tmp/fynd-c-fixture";
#else
    char template[] = "/tmp/fynd-c-XXXXXX";
#endif
    char *root;
    char path[512];
    char expected[2048];
    char *args[9];
    int fd;
#endif
    pure_tests();
#ifndef __cerb__
#ifdef __wasi__
    root = template;
    assert(mkdir(root, 0700) == 0);
#else
    root = mkdtemp(template);
    assert(root != NULL);
#endif
    assert(snprintf(path, sizeof path, "%s/b", root) > 0);
    fd = open(path, O_CREAT | O_WRONLY, 0600);
    assert(fd >= 0 && close(fd) == 0);
    assert(snprintf(path, sizeof path, "%s/a", root) > 0);
    assert(mkdir(path, 0700) == 0);
    assert(snprintf(path, sizeof path, "%s/a/z", root) > 0);
    fd = open(path, O_CREAT | O_WRONLY, 0600);
    assert(fd >= 0 && close(fd) == 0);
    args[0] = "fynd"; args[1] = ""; args[2] = root; args[3] = NULL;
    assert(snprintf(expected, sizeof expected, "%s\n%s/a\n%s/a/z\n%s/b\n",
                    root, root, root, root) > 0);
    expect_line_set(3, args, 0, expected);
    success_cases(root);
    sort_cases(root);
    preexisting_errors(root);
    failures(root);
    /* Retain the first entry, then continue to an independent leaf root.
     * If that entry is a directory, the global hook also fails its read. */
    assert(snprintf(path, sizeof path, "%s/b", root) > 0);
    args[3] = path;
    listing_countdown = 1;
    listing_first[0] = 0;
    {
        FILE *out, *err;
        char bytes[2048];
        char errors[2048];
        size_t n;
        out = tmpfile(); err = tmpfile();
        assert(out != NULL && err != NULL);
        assert(fynd_main(4, args, out, err) == 1);
        assert(listing_countdown == 0);
        assert(strcmp(listing_first, "a") == 0 || strcmp(listing_first, "b") == 0);
        assert(snprintf(expected, sizeof expected, "%s\n%s/%s\n%s/b\n",
                        root, root, listing_first, root) > 0);
        rewind(out);
        n = fread(bytes, 1, sizeof bytes, out);
        assert(!ferror(out) && n == strlen(expected));
        assert(memcmp(bytes, expected, n) == 0);
        if (strcmp(listing_first, "a") == 0)
            assert(snprintf(errors, sizeof errors,
                "fynd: cannot list directory '%s/a'\nfynd: cannot list directory '%s'\n",
                root, root) > 0);
        else
            assert(snprintf(errors, sizeof errors,
                "fynd: cannot list directory '%s'\n", root) > 0);
        rewind(err);
        n = fread(bytes, 1, sizeof bytes, err);
        assert(!ferror(err) && n == strlen(errors));
        assert(memcmp(bytes, errors, n) == 0);
        assert(fclose(out) == 0 && fclose(err) == 0);
    }
    listing_countdown = -1;
    args[3] = NULL;
    args[1] = "--max-depth"; args[2] = "129";
    expect_call(3, args, 2, "");
    args[1] = "-it"; expect_call(2, args, 2, "");
    args[1] = "--help"; args[2] = root; expect_call(3, args, 2, "");
    args[1] = ""; args[2] = ""; expect_call(3, args, 2, "");
    args[1] = ""; args[2] = "/no-such-fynd-root/\n";
    expect_call(3, args, 1, "");
    io_failures();
    {
        FILE *out;
        FILE *err;
        char text[1024];
        size_t n;
        out = tmpfile(); err = tmpfile();
        assert(out != NULL && err != NULL);
        args[1] = "--help";
        assert(fynd_main(2, args, out, err) == 0);
        rewind(out);
        n = fread(text, 1, sizeof text - 1, out);
        assert(!ferror(out)); text[n] = 0;
        assert(strstr(text, "Usage: fynd") != NULL);
        assert(strstr(text, "--max-depth") != NULL);
        assert(fclose(out) == 0 && fclose(err) == 0);
        args[1] = "--version";
        expect_call(2, args, 0, "fynd 0.1.0\n");
    }
    assert(snprintf(path, sizeof path, "%s/a/z", root) > 0);
    assert(unlink(path) == 0);
    assert(snprintf(path, sizeof path, "%s/a", root) > 0);
    assert(rmdir(path) == 0);
    assert(snprintf(path, sizeof path, "%s/b", root) > 0);
    assert(unlink(path) == 0);
    assert(rmdir(root) == 0);
#else
    puts("POSIX filesystem/FILE fixtures unavailable under __cerb__; pure tests ran");
#endif
#ifdef __wasi__
    puts("WASI fixtures ran: FIFO and Linux /dev/full unavailable; sticky errors use a regular file");
#endif
    puts("C tests passed");
    return 0;
}
