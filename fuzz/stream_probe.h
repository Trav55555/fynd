#ifndef FYND_STREAM_PROBE_H
#define FYND_STREAM_PROBE_H
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "fynd.h"
#include "cu.h"

static int stream_probe_open(CuDir *dir, CuArena *arena, const char *path);
static int stream_probe_next(CuDir *dir, CuDirEntry *entry);
static void stream_probe_close(CuDir *dir);
int fynd_stream_probe(int argc, char **argv, FILE *out, FILE *err);
#define fynd_main fynd_stream_probe
#define cu_dir_open stream_probe_open
#define cu_dir_next stream_probe_next
#define cu_dir_close stream_probe_close
#include "../src/fynd.c"
#undef cu_dir_close
#undef cu_dir_next
#undef cu_dir_open
#undef fynd_main

enum { STREAM_PROBE_EOF, STREAM_PROBE_EIO, STREAM_PROBE_OOM,
       STREAM_PROBE_RELEASE };
typedef struct StreamProbeFixture {
    const unsigned char *data;
    size_t size;
    size_t count;
    size_t index;
    size_t fault_at;
    size_t calls;
    size_t opens;
    size_t closes;
    int mode;
    int live;
    int fault_fired;
    CuDir *owner;
    CuArena *arena;
} StreamProbeFixture;

/* Serial, process-local test seam only. No filesystem or subprocess calls;
 * this models cursor ownership, not filesystem ordering or race behavior. */
static StreamProbeFixture *stream_probe_fixture;

static unsigned int stream_probe_byte(size_t at)
{
    const StreamProbeFixture *fixture;
    fixture = stream_probe_fixture;
    return fixture->size == 0 ? 0U : fixture->data[at % fixture->size];
}

static int stream_probe_open(CuDir *dir, CuArena *arena, const char *path)
{
    StreamProbeFixture *fixture;
    fixture = stream_probe_fixture;
    assert(fixture != NULL && !fixture->live && fixture->opens == 0);
    assert(memcmp(path, "memory", 7) == 0);
    dir->handle = NULL;
    dir->arena = arena;
    dir->state = 1;
    dir->error = 0;
    fixture->owner = dir;
    fixture->arena = arena;
    fixture->live = 1;
    ++fixture->opens;
    return 1;
}

static int stream_probe_next(CuDir *dir, CuDirEntry *entry)
{
    StreamProbeFixture *fixture;
    unsigned char raw[128];
    const char *name;
    size_t at;
    size_t length;
    size_t j;
    size_t attempt;
    int inject;
    int saved_debug;
    fixture = stream_probe_fixture;
    assert(fixture != NULL && fixture->live && fixture->owner == dir);
    assert(dir->arena == fixture->arena && dir->state == 1);
    ++fixture->calls;
    if (fixture->index == fixture->count) {
        if (fixture->mode == STREAM_PROBE_EIO) {
            dir->state = -1;
            dir->error = EIO;
            return -1;
        }
        return 0;
    }
    assert(fixture->index < fixture->count);
    at = 3 + fixture->index * 5;
    length = 1 + (size_t)(stream_probe_byte(at) & 127U);
    for (j = 0; j < length; ++j) {
        raw[j] = (unsigned char)stream_probe_byte(at + 2 + j);
        if (raw[j] == 0)
            raw[j] = 1;
    }
    /* raw is a byte span, not a C string. The real copy must allocate and
     * append its own terminator in the actual cursor's current name scope. */
    inject = fixture->mode == STREAM_PROBE_OOM &&
             fixture->index == fixture->fault_at;
    saved_debug = dir->arena->debug;
    attempt = dir->arena->attempts;
    if (inject) {
        fixture->fault_fired = 1;
        dir->arena->debug = 1;
        cu_arena_fail_at(dir->arena, attempt);
    }
    name = cu_str_cstr_copy(dir->arena,
                            cu_str_n((const char *)raw, length));
    if (inject) {
        cu_arena_fail_at(dir->arena, (size_t)-1);
        dir->arena->debug = saved_debug;
        assert(name == NULL && cu_arena_failed(dir->arena));
        assert(dir->arena->attempts == attempt + 1);
    }
    if (name == NULL) {
        dir->state = -1;
        dir->error = 0; /* CuDir's allocation failure convention. */
        return -1;
    }
    entry->name = cu_str_n(name, length);
    entry->kind = (CuDirKind)(stream_probe_byte(at + 1) & 3U);
    ++fixture->index;
    return 1;
}

static void stream_probe_close(CuDir *dir)
{
    StreamProbeFixture *fixture;
    fixture = stream_probe_fixture;
    assert(fixture != NULL && dir->arena == fixture->arena);
    ++fixture->closes;
    fixture->live = 0;
    fixture->owner = NULL;
    dir->handle = NULL;
    dir->state = 0;
}

static void stream_probe_finish(ListingJob *job, int status, int error,
                                size_t calls)
{
    CuDirEntry unused;
    size_t i;
    assert(job->error == error && job->active == 0 && job->open == 0);
    assert(listing_resource(job->error) == (error == ENOMEM));
    assert(stream_probe_fixture->calls == calls);
    assert(stream_probe_fixture->opens == 1);
    assert(stream_probe_fixture->closes == 1 && !stream_probe_fixture->live);
    for (i = 0; i < 3; ++i)
        assert(listing_next(job, &unused) == status);
    assert(!listing_open(job));
    assert(stream_probe_fixture->calls == calls);
    assert(stream_probe_fixture->opens == 1);
    listing_release(job);
    assert(job->arena.held == 0 && job->arena.cached == 0);
    assert(job->arena.head == NULL && job->arena.cache == NULL);
    assert(job->active == 0 && job->open == 0 && job->error == error);
    assert(listing_next(job, &unused) == status);
    listing_release(job);
    assert(job->arena.held == 0 && job->arena.cached == 0);
    assert(stream_probe_fixture->calls == calls);
    assert(stream_probe_fixture->closes == 1 && !stream_probe_fixture->live);
    stream_probe_fixture = NULL;
}

static void check_stream_bytes(const unsigned char *data, size_t size)
{
    unsigned char expected[32][128];
    size_t lengths[32];
    int kinds[32];
    StreamProbeFixture fixture;
    ListingJob job; /* Never copied or moved after initialization. */
    CuDirEntry entry;
    size_t count;
    size_t wanted;
    size_t fault_at;
    size_t i;
    size_t j;
    size_t at;
    int mode;
    int error;
    int status;
    int next;
    assert(size <= 512);
    count = size == 0 ? 0U : (size_t)data[0] % 33U;
    mode = size < 2 ? STREAM_PROBE_EOF : (int)(data[1] % 4U);
    if (count == 0 && mode == STREAM_PROBE_OOM)
        mode = STREAM_PROBE_EOF;
    fault_at = count == 0 ? 0U : (size_t)data[2 % size] % count;
    wanted = mode == STREAM_PROBE_OOM ? fault_at : count;
    if (mode == STREAM_PROBE_RELEASE)
        wanted = (size_t)data[2 % size] % (count + 1);
    error = mode == STREAM_PROBE_EIO ? EIO :
            (mode == STREAM_PROBE_OOM ? ENOMEM : 0);
    status = error == 0 ? 0 : -1;
    /* Independent oracle, fully decoded BEFORE any production source call.
     * Never obtain expected bytes, kinds or counts from a returned view. */
    for (i = 0; i < count; ++i) {
        at = 3 + 5 * i;
        lengths[i] = (size_t)data[at % size] % 128U + 1;
        kinds[i] = (int)(data[(at + 1) % size] % 4U);
        for (j = 0; j < lengths[i]; ++j) {
            expected[i][j] = data[(at + j + 2) % size];
            if (expected[i][j] == 0)
                expected[i][j] = 1;
        }
    }
    fixture.data = data;
    fixture.size = size;
    fixture.count = count;
    fixture.index = 0;
    fixture.fault_at = fault_at;
    fixture.calls = 0;
    fixture.opens = 0;
    fixture.closes = 0;
    fixture.mode = mode;
    fixture.live = 0;
    fixture.fault_fired = 0;
    fixture.owner = NULL;
    fixture.arena = NULL;
    assert(stream_probe_fixture == NULL);
    stream_probe_fixture = &fixture;
    listing_init(&job, "memory");
    assert(job.dir.arena == &job.arena && job.arena.block_size == 4096);
    assert(job.error == 0 && job.active == 0 && job.open == 0);
    if (!listing_open(&job)) {
        /* Even genuine exhaustion of the initial anchor must clean up. */
        assert(cu_arena_failed(&job.arena));
        stream_probe_finish(&job, -1, ENOMEM, 0);
        return;
    }
    assert(job.dir.arena == &job.arena && job.open && job.state == 1);
    assert(listing_open(&job) && fixture.opens == 1);
    /* Reopening an already-open cursor is idempotent, not a second handle. */
    assert(listing_open(&job));
    assert(fixture.opens == 1 && fixture.live);
    for (i = 0; i < wanted; ++i) {
        next = listing_next(&job, &entry);
        if (next != 1) {
            /* Real allocator exhaustion is not a fabricated directory errno. */
            assert(next == -1 && cu_arena_failed(&job.arena));
            assert(!fixture.fault_fired);
            stream_probe_finish(&job, -1, ENOMEM, i + 1);
            return;
        }
        assert(entry.name.ptr != NULL && entry.name.len == lengths[i]);
        assert(memcmp(entry.name.ptr, expected[i], lengths[i]) == 0);
        assert(entry.name.ptr[lengths[i]] == 0);
        assert((int)entry.kind == kinds[i]);
        assert(job.active && job.error == 0 && job.dir.arena == &job.arena);
        assert(job.arena.held <= 8192U);
        assert(job.arena.cached <= 8192U - job.arena.held);
        /* No read or comparison of this borrowed view after the next call. */
    }
    if (mode == STREAM_PROBE_RELEASE) {
        listing_release(&job); /* Also tests release with a name scope active. */
        stream_probe_finish(&job, status, error, wanted);
    } else {
        assert(listing_next(&job, &entry) == status);
        assert(fixture.index == wanted);
        assert(fixture.fault_fired == (mode == STREAM_PROBE_OOM));
        if (mode == STREAM_PROBE_OOM)
            assert(cu_arena_failed(&job.arena));
        stream_probe_finish(&job, status, error, wanted + 1);
    }
}
#endif
