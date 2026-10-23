/* Behavioral streaming sensors. These also compile against the pre-stream
 * source: only existing CuDir functions and public arena state are observed.
 * All invocations are serial (default or -j1); hooks are not thread-safe.
 */
#include <stdio.h>
#ifndef __cerb__
#include "cu.h"
#include <string.h>
#include <sys/stat.h>
static int stream_dir_open(CuDir *dir, CuArena *arena, const char *path);
static int stream_dir_next(CuDir *dir, CuDirEntry *entry);
static void stream_dir_close(CuDir *dir);
#define cu_dir_open stream_dir_open
#define cu_dir_next stream_dir_next
#define cu_dir_close stream_dir_close
/* Count probes for ignore sources; a listing that ends within a window
 * this small shows them instead, and a longer one is still probed. */
static size_t probe_calls;
static CuInfoResult stream_dir_info(const CuDir *dir, const char *name,
                                    CuPathFollow follow, CuFileInfo *out)
{
    ++probe_calls;
    return cu_dir_info(dir, name, follow, out);
}
#define IGNORE_DIR_INFO stream_dir_info
/* By path, the root's .git is looked up once while seeding its ancestors;
 * any further lookup is a probe. */
static const char *marker_dir;
static size_t marker_stats;
static int stream_lstat(const char *path, struct stat *st)
{
    size_t n;
    n = strlen(path);
    if (marker_dir != NULL && strstr(path, marker_dir) != NULL &&
        n >= 5 && strcmp(path + n - 5, "/.git") == 0)
        ++marker_stats;
    return lstat(path, st);
}
#define IGNORE_STAT stream_lstat
#define FYND_LISTING_AHEAD 4
/* Rename the included prototype too when a checker also links fynd.c. */
#define fynd_main stream_test_fynd_main
/* Its hooks are process-global and not thread-safe. The default
 * thread count is pinned to one; tests/test_sched.c covers threaded walks. */
#define FYND_ONLINE_CPUS() 1L
#include "../src/fynd.c"
#undef fynd_main
#undef cu_dir_open
#undef cu_dir_next
#undef cu_dir_close
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>

enum { STREAM_NORMAL, STREAM_VISIBLE, STREAM_EIO, STREAM_NAME_OOM,
       STREAM_OPEN_ERROR, STREAM_MEMORY,
       STREAM_ANCHOR_OOM, STREAM_EMFILE };
static int hook_mode;
static FILE *watch_out;
static int observed_before_eof;
static int eof_seen;
static size_t successful_entries;
static size_t actual_files;
static size_t open_attempts;
static size_t opened;
static size_t closed;
static size_t live_dirs;
static size_t peak_live_dirs;
static size_t next_calls;
static int anchor_failed;
static size_t failed_open_closes;
static size_t peak_name_bytes;
static int fault_fired;
static char first_name[256];
static const char *missing_path;

static void sample_names(const CuDir *dir)
{
    size_t bytes;
    /* The pinned cu.h exposes held/cached directly, not CuArenaStats. */
    if (hook_mode != STREAM_MEMORY)
        return;
    bytes = dir->arena->held + dir->arena->cached;
    if (bytes > peak_name_bytes)
        peak_name_bytes = bytes;
}

static int stream_dir_open(CuDir *dir, CuArena *arena, const char *path)
{
    int ok;
    ++open_attempts;
    if (hook_mode == STREAM_EMFILE && open_attempts == 64) {
        fault_fired = 1;
        /* Match the real failed constructor without exhausting host FDs. */
        dir->handle = NULL;
        dir->arena = arena;
        dir->state = -1;
        dir->error = EMFILE;
        ok = 0;
    } else if (hook_mode == STREAM_OPEN_ERROR && !fault_fired) {
        fault_fired = 1;
        /* Exercise a real failed constructor, not a permission assumption. */
        ok = cu_dir_open(dir, arena, missing_path);
    } else
        ok = cu_dir_open(dir, arena, path);
    if (dir->handle != NULL) {
        ++opened;
        ++live_dirs;
        if (live_dirs > peak_live_dirs)
            peak_live_dirs = live_dirs;
    }
    if (ok && hook_mode == STREAM_ANCHOR_OOM && !fault_fired) {
        /* Open allocated no names: the NEXT allocation is the one-byte
         * cursor anchor, not cu_dir_next's borrowed-name copy. */
        assert(arena->head == NULL && arena->cache == NULL);
        assert(arena->held == 0 && arena->cached == 0);
        assert(arena->attempts == 0 && !cu_arena_failed(arena));
        fault_fired = 1;
        cu_arena_fail_at(arena, 0);
    }
    return ok;
}

static int stream_dir_next(CuDir *dir, CuDirEntry *entry)
{
    int next;
    int saved_debug;
    ++next_calls;
    /* Observe BEFORE calling the real next, including the call returning EOF.
     * No assertion here: old collection must finish and prove correct bytes
     * and cleanup before the first test's final visibility assertion fails.
     */
    if (hook_mode == STREAM_VISIBLE && actual_files != 0 && !eof_seen &&
        watch_out != NULL && ftell(watch_out) > 0)
        observed_before_eof = 1;
    sample_names(dir);
    if (hook_mode == STREAM_EIO && successful_entries != 0) {
        fault_fired = 1;
        dir->error = EIO;
        dir->state = -1;
        return -1;
    }
    if (hook_mode == STREAM_NAME_OOM && !fault_fired) {
        fault_fired = 1;
        /* Make the real name copy allocate even if a path block has spare
         * capacity. Preserve the arena and every live scope; only temporarily
         * select its public per-push debug allocation policy. */
        saved_debug = dir->arena->debug;
        dir->arena->debug = 1;
        cu_arena_fail_at(dir->arena, dir->arena->attempts);
        next = cu_dir_next(dir, entry);
        cu_arena_fail_at(dir->arena, (size_t)-1);
        dir->arena->debug = saved_debug;
        assert(next == -1 && cu_dir_errno(dir) == 0);
        assert(cu_arena_failed(dir->arena));
        return next;
    }
    next = cu_dir_next(dir, entry);
    sample_names(dir);
    if (next == 1) {
        if (hook_mode == STREAM_EIO && successful_entries == 0) {
            /* Copy NOW: a later next may rewind the borrowed name arena. */
            assert(entry->name.len < sizeof first_name);
            memcpy(first_name, entry->name.ptr, entry->name.len);
            first_name[entry->name.len] = 0;
        }
        ++successful_entries;
        if (entry->kind == CU_DIR_FILE)
            ++actual_files;
    } else if (next == 0)
        eof_seen = 1;
    return next;
}

static void stream_dir_close(CuDir *dir)
{
    int had_handle;
    sample_names(dir);
    had_handle = dir->handle != NULL;
    if (hook_mode == STREAM_ANCHOR_OOM && had_handle)
        anchor_failed = cu_arena_failed(dir->arena) &&
                        dir->arena->attempts == 1 &&
                        dir->arena->held == 0 && dir->arena->cached == 0 &&
                        cu_dir_errno(dir) == 0;
    if (!had_handle && dir->state == -1 && dir->error != 0)
        ++failed_open_closes;
    cu_dir_close(dir);
    if (had_handle) {
        ++closed;
        --live_dirs;
    }
}

static void reset_hooks(int mode)
{
    assert(live_dirs == 0);
    hook_mode = mode;
    watch_out = NULL;
    observed_before_eof = 0;
    eof_seen = 0;
    successful_entries = 0;
    actual_files = 0;
    open_attempts = 0;
    opened = 0;
    closed = 0;
    peak_live_dirs = 0;
    next_calls = 0;
    anchor_failed = 0;
    probe_calls = 0;
    marker_stats = 0;
    failed_open_closes = 0;
    peak_name_bytes = 0;
    fault_fired = 0;
    first_name[0] = 0;
    missing_path = NULL;
}

static FILE *stream_tmpfile(void)
{
#ifdef __wasi__
    static unsigned int serial;
    char path[96];
    int fd;
    FILE *file;
    int n;
    n = snprintf(path, sizeof path, "/tmp/fynd-stream-output-%u", serial++);
    assert(n > 0 && (size_t)n < sizeof path);
    fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0)
        return NULL;
    file = fdopen(fd, "w+");
    if (file == NULL)
        assert(close(fd) == 0);
    assert(unlink(path) == 0);
    return file;
#else
    return tmpfile();
#endif
}
#define tmpfile stream_tmpfile

typedef struct StreamCheck {
    CuArena arena;
    size_t base;
    FILE *out;
    FILE *err;
} StreamCheck;

static void check_begin(StreamCheck *check)
{
    cu_arena_init_with(&check->arena, 256, 1);
    check->base = cu_arena_scope(&check->arena);
    check->out = tmpfile();
    check->err = tmpfile();
    assert(check->out != NULL && check->err != NULL);
    assert(setvbuf(check->out, NULL, _IONBF, 0) == 0);
}

static int check_run(StreamCheck *check, int argc, char **args)
{
    int status;
    watch_out = check->out;
    /* Any accidental serial-mode startup primitive must fail. */
    cu_thread_fail_at(0);
    status = fynd_run(argc, args, check->out, check->err, &check->arena);
    cu_thread_fail_at((size_t)-1);
    watch_out = NULL;
    assert(live_dirs == 0 && opened == closed);
    assert(cu_arena_scope(&check->arena) == check->base);
    return status;
}

static void check_end(StreamCheck *check)
{
    assert(fclose(check->out) == 0);
    assert(fclose(check->err) == 0);
    cu_arena_release(&check->arena);
    assert(check->arena.held == 0 && check->arena.cached == 0);
}

/* Independent multiset oracle: consume one expected occurrence per raw NUL
 * record. Never split on newline, sort/unique, or consult walker internals.
 * Ordered mode additionally checks the sequential input-root groups.
 */
static void expect_paths(FILE *file, const char **expected, size_t count,
                         int ordered)
{
    unsigned int used[16];
    char record[1024];
    size_t i;
    size_t n;
    size_t records;
    size_t match;
    int byte;
    assert(count <= sizeof used / sizeof used[0]);
    for (i = 0; i < count; ++i)
        used[i] = 0;
    assert(fflush(file) == 0 && fseek(file, 0, SEEK_SET) == 0);
    n = 0;
    records = 0;
    while ((byte = fgetc(file)) != EOF) {
        if (byte != 0) {
            assert(n + 1 < sizeof record);
            record[n++] = (char)byte;
            continue;
        }
        record[n] = 0;
        assert(records < count);
        match = count;
        for (i = 0; i < count; ++i)
            if (!used[i] && strcmp(record, expected[i]) == 0) {
                match = i;
                break;
            }
        assert(match < count);
        if (ordered)
            assert(strcmp(record, expected[records]) == 0);
        used[match] = 1;
        ++records;
        n = 0;
    }
    assert(!ferror(file) && feof(file));
    assert(n == 0 && records == count);
    for (i = 0; i < count; ++i)
        assert(used[i] == 1);
}

static void expect_text(FILE *file, const char *expected)
{
    size_t i;
    assert(fflush(file) == 0 && fseek(file, 0, SEEK_SET) == 0);
    for (i = 0; expected[i] != 0; ++i)
        assert(fgetc(file) == (unsigned char)expected[i]);
    assert(fgetc(file) == EOF && !ferror(file) && feof(file));
}

static void expected_error(char *dest, size_t capacity, const char *message,
                           const char *path)
{
    static const char hex[] = "0123456789abcdef";
    const unsigned char *p;
    size_t n;
    int written;
    written = snprintf(dest, capacity, "fynd: %s '", message);
    assert(written > 0 && (size_t)written < capacity);
    n = (size_t)written;
    for (p = (const unsigned char *)path; *p != 0; ++p) {
        assert(n + 7 < capacity);
        if (*p >= 32 && *p <= 126 && *p != '\\' && *p != '\'')
            dest[n++] = (char)*p;
        else {
            dest[n++] = '\\';
            dest[n++] = 'x';
            dest[n++] = hex[(unsigned int)*p / 16U];
            dest[n++] = hex[(unsigned int)*p % 16U];
        }
    }
    dest[n++] = '\'';
    dest[n++] = '\n';
    dest[n] = 0;
}

static char *fixture_begin(char *template)
{
#ifdef __wasi__
    (void)template;
    assert(mkdir("/tmp/fynd-stream-fixture", 0700) == 0);
    return "/tmp/fynd-stream-fixture";
#else
    char *root;
    root = mkdtemp(template);
    assert(root != NULL);
    return root;
#endif
}

static void fixture_path(char *dest, size_t capacity, const char *root,
                         const char *name)
{
    int n;
    n = snprintf(dest, capacity, "%s/%s", root, name);
    assert(n > 0 && (size_t)n < capacity);
}

static void make_file(const char *path)
{
    int fd;
    fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0600);
    assert(fd >= 0 && close(fd) == 0);
}

static void file_args(char **args, const char *root)
{
    args[0] = "fynd";
    args[1] = "-H";
    args[2] = "-t";
    args[3] = "f";
    args[4] = "-0";
    args[5] = "--no-ignore";
    args[6] = "";
    args[7] = (char *)root;
    args[8] = NULL;
}

/* MUST run first. Old collect-all source passes status, exact multiset and
 * cleanup, then fails ONLY observed_before_eof (native SIGABRT / shell 134).
 * -t f excludes the directory itself, so ftell cannot see a root record.
 */
static void first_path_before_eof(void)
{
    char template[] = "/tmp/fynd-stream-XXXXXX";
    char *root;
    char a[512];
    char b[512];
    char *args[9];
    const char *expected[2];
    StreamCheck check;
    root = fixture_begin(template);
    fixture_path(a, sizeof a, root, "a");
    fixture_path(b, sizeof b, root, "b\nraw");
    make_file(a);
    make_file(b);
    expected[0] = a;
    expected[1] = b;
    file_args(args, root);
    reset_hooks(STREAM_VISIBLE);
    check_begin(&check);
    assert(check_run(&check, 8, args) == 0);
    expect_paths(check.out, expected, 2, 0);
    expect_text(check.err, "");
    assert(actual_files == 2 && successful_entries == 2 && eof_seen);
    assert(open_attempts == 1 && opened == 1 && closed == 1);
    check_end(&check);
    assert(unlink(a) == 0);
    assert(unlink(b) == 0);
    assert(rmdir(root) == 0);
    assert(observed_before_eof);
}

/* Both creation permutations must retain the pre-next policy snapshot.
 * Neither this hook nor the repository test replaces production lstat. */
/* A listing that ends within FYND_LISTING_AHEAD names shows its ignore
 * sources, so none is probed; a longer one is probed. Either way policy is
 * in place before any child is selected, whatever the listing order. */
static void policy_from_listing(void)
{
    int permutation;
    int padded;
    for (padded = 0; padded < 2; ++padded) {
        for (permutation = 0; permutation < 2; ++permutation) {
            char template[] = "/tmp/fynd-stream-XXXXXX";
            char *root;
            char blocked[512];
            char kept[512];
            char rules[512];
            char pads[2][512];
            char *args[9];
            const char *expected[1];
            FILE *file;
            StreamCheck check;
            int status;
            int i;
            root = fixture_begin(template);
            fixture_path(blocked, sizeof blocked, root, "blocked-data");
            fixture_path(kept, sizeof kept, root, "kept-data");
            fixture_path(rules, sizeof rules, root, ".ignore");
            fixture_path(pads[0], sizeof pads[0], root, "pad-1");
            fixture_path(pads[1], sizeof pads[1], root, "pad-2");
            make_file(permutation ? kept : blocked);
            make_file(permutation ? blocked : kept);
            for (i = 0; padded && i < 2; ++i)
                make_file(pads[i]);
            file = fopen(rules, "w");
            assert(file != NULL && fputs("blocked-data\n", file) >= 0);
            assert(fclose(file) == 0);
            file_args(args, root);
            args[5] = "-j1";
            args[6] = "data";
            expected[0] = kept;
            reset_hooks(STREAM_NORMAL);
            marker_dir = strrchr(root, '/');
            check_begin(&check);
            status = check_run(&check, 8, args);
            for (i = 0; padded && i < 2; ++i)
                assert(unlink(pads[i]) == 0);
            assert(unlink(blocked) == 0 && unlink(kept) == 0);
            assert(unlink(rules) == 0 && rmdir(root) == 0);
            assert(status == 0);
            expect_paths(check.out, expected, 1, 0);
            expect_text(check.err, "");
            check_end(&check);
            marker_dir = NULL;
            if (padded)
                assert(probe_calls != 0);
            else
                assert(probe_calls == 0 && marker_stats == 1);
            assert(open_attempts == 1 && opened == 1 && closed == 1);
            assert(live_dirs == 0);
        }
    }
}

static void cursor_anchor_failure(void)
{
    char template[] = "/tmp/fynd-stream-XXXXXX";
    char *root;
    char directory[512];
    char child[512];
    char leaf[512];
    char later[512];
    char error[4096];
    char *args[11];
    StreamCheck check;
    int status;
    root = fixture_begin(template);
    fixture_path(directory, sizeof directory, root, "first\n'\\");
    fixture_path(child, sizeof child, directory, "a");
    fixture_path(leaf, sizeof leaf, root, "later-data");
    fixture_path(later, sizeof later, root, "later-dir");
    assert(mkdir(directory, 0700) == 0 && mkdir(later, 0700) == 0);
    make_file(child);
    make_file(leaf);
    file_args(args, directory);
    args[8] = leaf;
    args[9] = later;
    args[10] = NULL;
    reset_hooks(STREAM_ANCHOR_OOM);
    check_begin(&check);
    status = check_run(&check, 10, args);
    assert(unlink(child) == 0 && unlink(leaf) == 0);
    assert(rmdir(directory) == 0 && rmdir(later) == 0);
    assert(rmdir(root) == 0);
    assert(status == 1);
    expect_paths(check.out, NULL, 0, 0);
    expected_error(error, sizeof error, "allocation failure", directory);
    expect_text(check.err, error);
    check_end(&check);
    assert(fault_fired && anchor_failed && next_calls == 0);
    assert(successful_entries == 0 && actual_files == 0 && !eof_seen);
    assert(open_attempts == 1 && opened == 1 && closed == 1);
    assert(peak_live_dirs == 1 && live_dirs == 0);
}

static void dangling_repository_marker(void)
{
    int permutation;
    for (permutation = 0; permutation < 2; ++permutation) {
        char template[] = "/tmp/fynd-stream-XXXXXX";
        char *root;
        char marker[512];
        char blocked[512];
        char kept[512];
        char rules[512];
        char *args[9];
        const char *expected[1];
        FILE *file;
        StreamCheck check;
        int status;
        int link_status;
        int link_error;
        root = fixture_begin(template);
        fixture_path(marker, sizeof marker, root, ".git");
        link_status = symlink("missing-target", marker);
        if (link_status != 0) {
            link_error = errno;
            assert(rmdir(root) == 0);
#ifdef __wasi__
            if (link_error == ENOSYS) {
                puts("dangling .git fixture skipped: WASI symlink ENOSYS");
                return;
            }
#endif
            /* Native failure, or any other WASI error, is NOT a skip. */
            assert(link_error == 0 && link_status == 0);
            return;
        }
        fixture_path(blocked, sizeof blocked, root, "blocked-data");
        fixture_path(kept, sizeof kept, root, "kept-data");
        fixture_path(rules, sizeof rules, root, ".gitignore");
        make_file(permutation ? kept : blocked);
        make_file(permutation ? blocked : kept);
        file = fopen(rules, "w");
        assert(file != NULL && fputs("blocked-data\n", file) >= 0);
        assert(fclose(file) == 0);
        file_args(args, root);
        args[5] = "-j1";
        args[6] = "data";
        expected[0] = kept;
        reset_hooks(STREAM_NORMAL);
        check_begin(&check);
        status = check_run(&check, 8, args);
        assert(unlink(marker) == 0 && unlink(rules) == 0);
        assert(unlink(blocked) == 0 && unlink(kept) == 0);
        assert(rmdir(root) == 0);
        assert(status == 0);
        expect_paths(check.out, expected, 1, 0);
        expect_text(check.err, "");
        check_end(&check);
        assert(successful_entries == 4 && actual_files == 3 && eof_seen);
        assert(next_calls == 5 && open_attempts == 1);
        assert(opened == 1 && closed == 1 && live_dirs == 0);
    }
}

static void directory_resource_lifetime(void)
{
    int inject;
    for (inject = 0; inject < 2; ++inject) {
        char template[] = "/tmp/fynd-stream-XXXXXX";
        char *root;
        char directory[512];
        char path[1024];
        char failed[1024];
        char leaf[512];
        char later[512];
        char error[8192];
        char *args[11];
        StreamCheck check;
        size_t n;
        int i;
        int status;
        root = fixture_begin(template);
        fixture_path(directory, sizeof directory, root, "chain");
        assert(mkdir(directory, 0700) == 0);
        n = strlen(directory);
        memcpy(path, directory, n + 1);
        failed[0] = 0;
        for (i = 0; i < FYND_DEPTH_LIMIT; ++i) {
            assert(n + 3 <= sizeof path);
            memcpy(path + n, "/d", 3);
            n += 2;
            assert(mkdir(path, 0700) == 0);
            /* Root depth 0 is open #1; depth 63 is open #64. */
            if (i == 62)
                memcpy(failed, path, n + 1);
        }
        fixture_path(leaf, sizeof leaf, root, "later-data");
        fixture_path(later, sizeof later, root, "later-dir");
        make_file(leaf);
        assert(mkdir(later, 0700) == 0);
        file_args(args, directory);
        if (inject) {
            args[8] = leaf;
            args[9] = later;
            args[10] = NULL;
        }
        expected_error(error, sizeof error,
                       inject ? "cannot open directory" : "recursion safety limit",
                       inject ? failed : path);
        reset_hooks(inject ? STREAM_EMFILE : STREAM_NORMAL);
        check_begin(&check);
        /* Default serial recursion: no explicit depth cutoff, no host limit
         * changes, and no attempt to exhaust actual process descriptors. */
        status = check_run(&check, inject ? 10 : 8, args);
        for (i = 0; i < FYND_DEPTH_LIMIT; ++i) {
            assert(rmdir(path) == 0);
            n -= 2;
            path[n] = 0;
        }
        assert(rmdir(directory) == 0 && rmdir(later) == 0);
        assert(unlink(leaf) == 0 && rmdir(root) == 0);
        assert(status == 1);
        expect_paths(check.out, NULL, 0, 0);
        expect_text(check.err, error);
        check_end(&check);
        assert(actual_files == 0 && live_dirs == 0 && opened == closed);
        assert(peak_live_dirs <= FYND_DEPTH_LIMIT);
        if (inject) {
            assert(fault_fired && open_attempts == 64);
            assert(opened == 63 && closed == 63 && peak_live_dirs == 63);
            assert(failed_open_closes == 1 && !eof_seen);
            assert(next_calls == 63 && successful_entries == 63);
        } else {
            /* visit checks the depth limit BEFORE open; every shallower
             * depth stays live at once in default serial recursion. */
            assert(open_attempts == FYND_DEPTH_LIMIT && opened == FYND_DEPTH_LIMIT &&
                   closed == FYND_DEPTH_LIMIT);
            assert(peak_live_dirs == FYND_DEPTH_LIMIT && failed_open_closes == 0);
            assert(next_calls == 2 * FYND_DEPTH_LIMIT && successful_entries == FYND_DEPTH_LIMIT &&
                   eof_seen);
        }
    }
}

static void late_errors(void)
{
    char template[] = "/tmp/fynd-stream-XXXXXX";
    char *root;
    char directory[512];
    char a[512];
    char b[512];
    char leaf[512];
    char missing[512];
    char error[4096];
    char *args[10];
    const char *expected[2];
    StreamCheck check;
    root = fixture_begin(template);
    fixture_path(directory, sizeof directory, root, "dir\n'\\");
    assert(mkdir(directory, 0700) == 0);
    fixture_path(a, sizeof a, directory, "a\nraw");
    fixture_path(b, sizeof b, directory, "b");
    fixture_path(leaf, sizeof leaf, root, "later\nleaf");
    fixture_path(missing, sizeof missing, root, "missing");
    make_file(a);
    make_file(b);
    make_file(leaf);
    file_args(args, directory);
    args[8] = leaf;
    args[9] = NULL;
    reset_hooks(STREAM_EIO);
    check_begin(&check);
    assert(check_run(&check, 9, args) == 1);
    assert(fault_fired && successful_entries == 1 && actual_files == 1);
    /* Select from the independent fixture manifest, not a borrowed entry. */
    if (strcmp(first_name, "a\nraw") == 0)
        expected[0] = a;
    else {
        assert(strcmp(first_name, "b") == 0);
        expected[0] = b;
    }
    expected[1] = leaf;
    expect_paths(check.out, expected, 2, 1);
    expected_error(error, sizeof error, "cannot list directory", directory);
    expect_text(check.err, error);
    check_end(&check);

    reset_hooks(STREAM_NAME_OOM);
    check_begin(&check);
    assert(check_run(&check, 9, args) == 1);
    assert(fault_fired && successful_entries == 0 && open_attempts == 1);
    expect_paths(check.out, NULL, 0, 0);
    expected_error(error, sizeof error, "allocation failure", directory);
    expect_text(check.err, error);
    check_end(&check);

    /* --sort reads a directory before visiting it: a late EIO keeps the
     * entries already read, and both faults diagnose as when streaming. */
    {
        char *sorted[11];
        memcpy(sorted, args, 6 * sizeof *sorted);
        sorted[6] = "--sort";
        sorted[7] = args[6];
        sorted[8] = args[7];
        sorted[9] = leaf;
        sorted[10] = NULL;
        reset_hooks(STREAM_EIO);
        check_begin(&check);
        assert(check_run(&check, 10, sorted) == 1);
        assert(fault_fired && successful_entries == 1 && actual_files == 1);
        expected[0] = strcmp(first_name, "a\nraw") == 0 ? a : b;
        expected[1] = leaf;
        expect_paths(check.out, expected, 2, 1);
        expected_error(error, sizeof error, "cannot list directory", directory);
        expect_text(check.err, error);
        check_end(&check);
        reset_hooks(STREAM_NAME_OOM);
        check_begin(&check);
        assert(check_run(&check, 10, sorted) == 1);
        assert(fault_fired && successful_entries == 0 && open_attempts == 1);
        expect_paths(check.out, NULL, 0, 0);
        expected_error(error, sizeof error, "allocation failure", directory);
        expect_text(check.err, error);
        check_end(&check);
    }

    reset_hooks(STREAM_OPEN_ERROR);
    missing_path = missing;
    check_begin(&check);
    assert(check_run(&check, 9, args) == 1);
    assert(fault_fired && opened == 0 && failed_open_closes != 0);
    assert(open_attempts == 1 && successful_entries == 0);
    expected[0] = leaf;
    expect_paths(check.out, expected, 1, 1);
    expected_error(error, sizeof error, "cannot open directory", directory);
    expect_text(check.err, error);
    check_end(&check);
    missing_path = NULL;
    assert(unlink(a) == 0);
    assert(unlink(b) == 0);
    assert(unlink(leaf) == 0);
    assert(rmdir(directory) == 0 && rmdir(root) == 0);
}

static void root_groups(void)
{
    char template[] = "/tmp/fynd-stream-XXXXXX";
    char *root;
    char a[512];
    char b[512];
    char *args[12];
    const char *expected[3];
    StreamCheck check;
    int explicit_j1;
    root = fixture_begin(template);
    fixture_path(a, sizeof a, root, "a\nraw");
    fixture_path(b, sizeof b, root, "b");
    make_file(a);
    make_file(b);
    expected[0] = b;
    expected[1] = a;
    expected[2] = b;
    for (explicit_j1 = 0; explicit_j1 < 2; ++explicit_j1) {
        file_args(args, b);
        if (explicit_j1) {
            args[6] = "-j1";
            args[7] = "";
            args[8] = b;
            args[9] = a;
            args[10] = b;
            args[11] = NULL;
        } else {
            args[8] = a;
            args[9] = b;
            args[10] = NULL;
        }
        reset_hooks(STREAM_NORMAL);
        check_begin(&check);
        assert(check_run(&check, explicit_j1 ? 11 : 10, args) == 0);
        expect_paths(check.out, expected, 3, 1);
        expect_text(check.err, "");
        assert(open_attempts == 0);
        check_end(&check);
    }
    assert(unlink(a) == 0 && unlink(b) == 0 && rmdir(root) == 0);
}

static void quiet_name_bound(void)
{
    char template[] = "/tmp/fynd-stream-XXXXXX";
    char *root;
    char name[128];
    char path[512];
    char *args[10];
    StreamCheck check;
    size_t peak;
    int i;
    int n;
    root = fixture_begin(template);
    for (i = 0; i < 5000; ++i) {
        n = snprintf(name, sizeof name,
                     "f%04d-abcdefghijklmnopqrstuvwxyz-abcdefghijklmnopqrstuvwxyz", i);
        assert(n > 0 && (size_t)n < sizeof name);
        fixture_path(path, sizeof path, root, name);
        make_file(path);
    }
    file_args(args, root);
    args[6] = "-j1";
    args[7] = "not-present-anywhere";
    args[8] = root;
    args[9] = NULL;
    reset_hooks(STREAM_MEMORY);
    check_begin(&check);
    assert(check_run(&check, 9, args) == 0);
    expect_paths(check.out, NULL, 0, 0);
    expect_text(check.err, "");
    assert(actual_files == 5000 && successful_entries == 5000 && eof_seen);
    assert(open_attempts == 1 && opened == 1);
    peak = peak_name_bytes;
    check_end(&check);
    /* No names or arena pointers were retained by the measuring hook. */
    for (i = 0; i < 5000; ++i) {
        n = snprintf(name, sizeof name,
                     "f%04d-abcdefghijklmnopqrstuvwxyz-abcdefghijklmnopqrstuvwxyz", i);
        assert(n > 0 && (size_t)n < sizeof name);
        fixture_path(path, sizeof path, root, name);
        assert(unlink(path) == 0);
    }
    assert(rmdir(root) == 0);
    assert(peak > 0 && peak <= 65536UL);
}

static void late_ignore_policy(void)
{
    char template[] = "/tmp/fynd-stream-XXXXXX";
    char *root;
    char blocked[512];
    char kept[512];
    char rules[512];
    char *args[9];
    const char *expected[1];
    FILE *file;
    StreamCheck check;
    root = fixture_begin(template);
    fixture_path(blocked, sizeof blocked, root, "blocked*literal");
    fixture_path(kept, sizeof kept, root, "kept\n*literal");
    fixture_path(rules, sizeof rules, root, ".ignore");
    make_file(blocked);
    make_file(kept);
    /* Create policy after both children. No filesystem iteration order is
     * assumed; policy must already apply at the first descendant selection.
     */
    make_file(rules);
    file = fopen(rules, "w");
    assert(file != NULL);
    assert(fputs("blocked*\n.ignore\n", file) >= 0);
    assert(fclose(file) == 0);
    args[0] = "fynd";
    args[1] = "-H";
    args[2] = "-t";
    args[3] = "f";
    args[4] = "-0";
    args[5] = "-j1";
    args[6] = "*literal";
    args[7] = root;
    args[8] = NULL;
    expected[0] = kept;
    reset_hooks(STREAM_NORMAL);
    check_begin(&check);
    assert(check_run(&check, 8, args) == 0);
    expect_paths(check.out, expected, 1, 0);
    expect_text(check.err, "");
    check_end(&check);
    /* With an empty predicate the late-created rules file itself is also
     * selectable, but its own policy must prune it before any emission. */
    args[6] = "";
    reset_hooks(STREAM_NORMAL);
    check_begin(&check);
    assert(check_run(&check, 8, args) == 0);
    expect_paths(check.out, expected, 1, 0);
    expect_text(check.err, "");
    check_end(&check);
    assert(unlink(blocked) == 0);
    assert(unlink(kept) == 0);
    assert(unlink(rules) == 0 && rmdir(root) == 0);
}
#endif

int main(void)
{
#ifndef __cerb__
    first_path_before_eof();
    policy_from_listing();
    cursor_anchor_failure();
    dangling_repository_marker();
    directory_resource_lifetime();
    late_errors();
    root_groups();
    quiet_name_bound();
    late_ignore_policy();
    puts("streaming filesystem tests passed");
#else
    puts("streaming filesystem/FILE fixtures skipped under __cerb__");
#endif
    return 0;
}
