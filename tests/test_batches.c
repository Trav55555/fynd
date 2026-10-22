/* Shared-credit proofs, A=1024/B=64. Use an external deadlock guard; a
 * timeout is NOT behavioral sensitivity evidence. Lock order is Pool->Proof.
 * Ownership hooks never wait; WALK/FILE enter outside Mu. Stop waits release Mu.
 */
#include "cu.h"
#include <assert.h>
static void batch_change(void *opaque);
static void batch_wait(void *opaque);
static void batch_publish(void *opaque);
static void batch_walk(void *opaque);
static size_t batch_fwrite(const void *bytes, size_t size, size_t count,
                           FILE *stream);
#define FYND_TASK_BYTES 1024U
#define FYND_BATCH_BYTES 64U
#define FYND_BATCH_CHANGE(pool) batch_change(pool)
#define FYND_BATCH_WAIT(task) batch_wait(task)
#define FYND_TASK_PUBLISH(task) batch_publish(task)
#define FYND_PENDING(pool) batch_change(pool)
#define FYND_WALK_BEGIN(run) batch_walk(run)
#define fwrite batch_fwrite
/* Rename the included public prototype AND body for separately linked dcheck. */
#define fynd_main batch_fixture_main
#include "../src/fynd.c"
#undef fynd_main
#undef fwrite
#ifndef __cerb__
#include <unistd.h>

enum { BATCH_OFF, BATCH_RESERVE, BATCH_WAKE, BATCH_ABORT, BATCH_FAIL };
static int scene;
static Pool *active_pool;
static CuMutex *proof_mutex;
static CuCond *proof_changed;
static int saturated;
static int head_reserve_claimed;
static int head_publications;
static int b_parked;
static int release_b;
static int a_published;
static int c_waits;
static int c_claimed;
static int io_held;
static int pinned_checks;
static int io_checks;
static Batch *held_shared;
static int abort_parked;
static int abort_exits;
static size_t abort_held;
static size_t abort_cached;
static Batch *failed_io;
static char abort_head[128];
static char abort_private[129];
static char head_out[145];
static char head_err[137];
static char future_out[31U * 64U + 17U];
static char wake_head[128];
static char wake_future[45U * 64U];
static char wake_other[128];
static const char reuse_out[4] = {'r', 0, 's', '\n'};
static const char reuse_err[3] = {'e', 0, '!'};

/* No private FILL descriptor/byte reads, even while Mu is held. */
static void mark_batch(Pool *pool, Batch *batch, int state,
                       unsigned char *seen)
{
    size_t i;
    for (i = 0; i < pool->nbatches; ++i)
        if (batch == &pool->batches[i])
            break;
    assert(i < pool->nbatches && !seen[i]);
    seen[i] = 1;
    assert(batch->state == state);
    if (state == B_QUEUED || state == B_IO)
        assert(batch->used <= pool->batch_bytes);
}

/* Enumerate every owner, not merely the sum of state counters: catches lost,
 * duplicate, stolen reservations and IO credits recycled at dequeue. */
static void conservation(Pool *pool)
{
    unsigned char seen[512];
    Batch *batch;
    Batch *last;
    Task *task;
    size_t i;
    int j;
    assert(pool->count >= 1 && pool->count <= 32);
    assert(pool->batch_bytes == 64U);
    assert(pool->nbatches == (size_t)pool->count * (size_t)16);
    assert(pool->nbatches <= 512U);
    assert(pool->nbatches * pool->batch_bytes ==
           (size_t)pool->count * (size_t)FYND_TASK_BYTES);
    for (i = 0; i < sizeof seen; ++i)
        seen[i] = 0;
    for (batch = pool->shared_free; batch != NULL; batch = batch->next) {
        mark_batch(pool, batch, B_FREE, seen);
        assert(batch->reserved_for == NULL);
    }
    for (j = 0; j < 32; ++j) {
        task = pool->slots[j];
        assert(task == &pool->tasks[j]);
        if (j >= pool->count) {
            assert(task->reserved == NULL && task->fill == NULL);
            assert(task->qfirst == NULL && task->qlast == NULL);
            continue;
        }
        assert(task->reserved == &pool->batches[j]);
        assert(task->reserved->reserved_for == task);
        if (task->reserved->state == B_FREE)
            mark_batch(pool, task->reserved, B_FREE, seen);
        if (task->fill != NULL) {
            mark_batch(pool, task->fill, B_FILL, seen);
            assert(task->fill->reserved_for == NULL ||
                   task->fill->reserved_for == task);
        }
        last = NULL;
        for (batch = task->qfirst; batch != NULL; batch = batch->next) {
            mark_batch(pool, batch, B_QUEUED, seen);
            assert(batch->reserved_for == NULL || batch->reserved_for == task);
            last = batch;
        }
        assert(last == task->qlast);
    }
    if (pool->io != NULL) {
        mark_batch(pool, pool->io, B_IO, seen);
        assert(pool->io->next == NULL && pool->first != NULL);
        assert(pool->io->reserved_for == NULL ||
               pool->io->reserved_for == pool->first);
    }
    for (i = 0; i < pool->nbatches; ++i) {
        assert(seen[i] && pool->batches[i].bytes != NULL);
        assert(pool->batches[i].bytes ==
               pool->batches[(i / 16U) * 16U].bytes + (i % 16U) * 64U);
    }
}

static int named(Task *task, const char *name)
{
    return task->path != NULL && strcmp(basename_view(task->path).ptr, name) == 0;
}

static void wait_flag(int *flag)
{
    cu_mutex_lock(proof_mutex);
    while (!*flag)
        cu_cond_wait(proof_changed, proof_mutex);
    cu_mutex_unlock(proof_mutex);
}
#endif

static void batch_change(void *opaque)
{
#ifndef __cerb__
    Pool *pool;
    Task *task;
    pool = (Pool *)opaque;
    conservation(pool);
    if (scene == BATCH_OFF)
        return;
    cu_mutex_lock(proof_mutex);
    if (scene == BATCH_RESERVE && saturated && pool->first != NULL &&
        named(pool->first, "a") &&
        pool->first->fill == pool->first->reserved)
        head_reserve_claimed = 1;
    if (scene == BATCH_WAKE) {
        task = pool->slots[2];
        if (task->fill != NULL && task->fill->reserved_for == NULL) {
            assert(task->fill == held_shared);
            c_claimed = 1;
            cu_cond_broadcast(proof_changed);
        }
    }
    cu_mutex_unlock(proof_mutex);
#else
    (void)opaque;
#endif
}

static void batch_wait(void *opaque)
{
#ifndef __cerb__
    Task *task;
    Batch *batch;
    size_t queued;
    task = (Task *)opaque;
    conservation(task->pool);
    cu_mutex_lock(proof_mutex);
    if (scene == BATCH_RESERVE && named(task, "b") && !saturated) {
        assert(task->pool->shared_free == NULL && task->fill == NULL);
        queued = 0;
        for (batch = task->qfirst; batch != NULL; batch = batch->next) {
            assert(batch->used == 64U);
            ++queued;
        }
        assert(queued == 31U && queued * 64U > FYND_TASK_BYTES);
        assert(task->pool->first != task);
        assert(task->pool->first->reserved->state == B_FREE);
        saturated = 1;
        cu_cond_broadcast(proof_changed);
    }
    if (scene == BATCH_WAKE && named(task, "c")) {
        assert(task->pool->shared_free == NULL && task->fill == NULL);
        assert(task->qfirst == task->reserved);
        ++c_waits;
        cu_cond_broadcast(proof_changed);
    }
    cu_mutex_unlock(proof_mutex);
#else
    (void)opaque;
#endif
}

static void batch_publish(void *opaque)
{
#ifndef __cerb__
    static const int channels[9] = {0, 0, 0, 1, 1, 1, 0, 1, 0};
    static const size_t lengths[9] = {64, 64, 17, 64, 64, 9, 0, 0, 64};
    static const int flushes[9] = {0, 0, 0, 0, 0, 1, 1, 1, 0};
    Task *task;
    Batch *batch;
    task = (Task *)opaque;
    batch = task->qlast;
    assert(batch != NULL && batch->state == B_QUEUED);
    cu_mutex_lock(proof_mutex);
    if (scene == BATCH_RESERVE && named(task, "a")) {
        assert(head_publications < 9);
        assert(batch->channel == channels[head_publications]);
        assert(batch->used == lengths[head_publications]);
        assert(batch->flush == flushes[head_publications]);
        ++head_publications;
    }
    if (scene == BATCH_WAKE && named(task, "a") &&
        batch->reserved_for == NULL) {
        assert(batch->used == 64U && task->qfirst == task->reserved);
        a_published = 1;
        cu_cond_broadcast(proof_changed);
    }
    cu_mutex_unlock(proof_mutex);
#else
    (void)opaque;
#endif
}

static void batch_walk(void *opaque)
{
#ifndef __cerb__
    Task *task;
    task = ((Run *)opaque)->task;
    if (scene == BATCH_ABORT || scene == BATCH_FAIL) {
        if (named(task, "a")) {
            assert(task_write(task, EMIT_ERR, abort_head, sizeof abort_head, 1));
        } else {
            assert(named(task, "b"));
            /* Reserved + shared QUEUED credits and a shared private FILL. Park
             * outside publication/ownership hooks until real cancellation. */
            assert(task_write(task, EMIT_OUT, abort_private,
                              sizeof abort_private, 0));
            cu_mutex_lock(proof_mutex);
            abort_parked = 1;
            cu_cond_broadcast(proof_changed);
            cu_mutex_unlock(proof_mutex);
            cu_mutex_lock(task->pool->mutex);
            while (!task->pool->stop)
                cu_cond_wait(task->pool->work, task->pool->mutex);
            /* Still inside a live worker: abort must not reclaim the arena,
             * seed, queues or even the failed IO loan before joining us. */
            assert(task->pool->arena.held == abort_held);
            assert(task->pool->arena.cached == abort_cached);
            assert(task->seed.held != 0 && named(task, "b"));
            conservation(task->pool);
            if (scene == BATCH_FAIL) {
                assert(failed_io != NULL && task->pool->io == failed_io);
                assert(failed_io->state == B_IO);
            }
            cu_mutex_lock(proof_mutex);
            ++abort_exits;
            cu_mutex_unlock(proof_mutex);
            cu_mutex_unlock(task->pool->mutex);
        }
    } else if (scene == BATCH_RESERVE) {
        if (named(task, "a")) {
            wait_flag(&saturated);
            assert(task_write(task, EMIT_OUT, head_out, 13, 0));
            assert(task_write(task, EMIT_OUT, head_out + 13, 132, 0));
            /* Empty channel changes publish old bytes, not a flush event. */
            assert(task_write(task, EMIT_ERR, "", 0, 0));
            assert(task_write(task, EMIT_ERR, head_err, 70, 0));
            assert(task_write(task, EMIT_ERR, head_err + 70, 67, 1));
            assert(task_write(task, EMIT_OUT, "", 0, 1));
            assert(task_write(task, EMIT_ERR, "", 0, 1));
            assert(task_write(task, EMIT_OUT, head_out, 64, 0));
        } else {
            assert(named(task, "b"));
            assert(task_write(task, EMIT_OUT, future_out, sizeof future_out, 0));
        }
    } else if (scene == BATCH_WAKE) {
        if (named(task, "b")) {
            /* Reserve + 44 shared credits, leaving exactly one for A. Park
             * outside Mu without another claim, so C is the sole space waiter. */
            assert(task_write(task, EMIT_OUT, wake_future, sizeof wake_future, 1));
            cu_mutex_lock(proof_mutex);
            b_parked = 1;
            cu_cond_broadcast(proof_changed);
            while (!release_b)
                cu_cond_wait(proof_changed, proof_mutex);
            cu_mutex_unlock(proof_mutex);
        } else if (named(task, "a")) {
            wait_flag(&b_parked);
            assert(task_write(task, EMIT_OUT, wake_head, sizeof wake_head, 0));
        } else if (named(task, "c")) {
            wait_flag(&a_published);
            assert(task_write(task, EMIT_OUT, wake_other, sizeof wake_other, 0));
        } else {
            assert(named(task, "reuse"));
            assert(task_write(task, EMIT_OUT, reuse_out, 1, 0));
            assert(task_write(task, EMIT_OUT, reuse_out + 1, 3, 0));
            assert(task_write(task, EMIT_ERR, reuse_err, 1, 0));
            assert(task_write(task, EMIT_ERR, reuse_err + 1, 2, 1));
        }
    }
#else
    (void)opaque;
#endif
}

/* This is the actual emit->fwrite call, not a dequeue-only sensor. Poke C's
 * credit predicate while holding A's shared IO buffer, and wait for C to retry
 * outside Mu. C must re-wait, not acquire/rewrite this still borrowed credit. */
static size_t batch_fwrite(const void *bytes, size_t size, size_t count,
                           FILE *stream)
{
#ifndef __cerb__
    Pool *pool;
    Batch *batch;
    char snapshot[64];
    size_t result;
    int hold;
    int before;
    pool = active_pool;
    if (pool == NULL)
        return fwrite(bytes, size, count, stream);
    cu_mutex_lock(pool->mutex);
    batch = pool->io;
    assert(batch != NULL && batch->state == B_IO && batch->bytes == bytes);
    assert(size == 1 && count == batch->used && count <= sizeof snapshot);
    conservation(pool);
    memcpy(snapshot, bytes, count);
    hold = scene == BATCH_WAKE && named(pool->first, "a") &&
           batch->reserved_for == NULL;
    before = 0;
    if (hold) {
        assert(pool->shared_free == NULL);
        cu_mutex_lock(proof_mutex);
        assert(c_waits != 0 && !c_claimed && b_parked && !release_b);
        before = c_waits;
        held_shared = batch;
        io_held = 1;
        cu_mutex_unlock(proof_mutex);
        cu_cond_broadcast(pool->space);
    }
    cu_mutex_unlock(pool->mutex);
    if (hold) {
        cu_mutex_lock(proof_mutex);
        while (c_waits == before && !c_claimed)
            cu_cond_wait(proof_changed, proof_mutex);
        assert(!c_claimed && io_held);
        cu_mutex_unlock(proof_mutex);
    }
    /* A deterministic short write at the actual FILE boundary. Using ERR
     * avoids a secondary OUT-failure diagnostic borrowing the same sensor. */
    if (scene == BATCH_FAIL && batch->reserved_for == NULL) {
        assert(failed_io == NULL && count == 64U);
        failed_io = batch;
        result = 0;
    } else
        result = fwrite(bytes, size, count, stream);
    cu_mutex_lock(pool->mutex);
    assert(pool->io == batch && batch->state == B_IO);
    assert(batch->bytes == bytes && batch->used == count);
    assert(memcmp(bytes, snapshot, count) == 0);
    conservation(pool);
    if (hold) {
        assert(pool->shared_free == NULL && pool->slots[2]->fill == NULL);
        cu_mutex_lock(proof_mutex);
        assert(!c_claimed);
        io_held = 0;
        ++pinned_checks;
        cu_mutex_unlock(proof_mutex);
    }
    ++io_checks;
    cu_mutex_unlock(pool->mutex);
    return result;
#else
    return fwrite(bytes, size, count, stream);
#endif
}

#ifndef __cerb__
static FILE *batch_file(void)
{
#ifdef __wasi__
    static unsigned int serial;
    char path[80];
    FILE *file;
    assert(snprintf(path, sizeof path, "/tmp/fynd-batch-stream-%u", serial++) > 0);
    file = fopen(path, "w+");
    if (file != NULL)
        assert(unlink(path) == 0);
    return file;
#else
    return tmpfile();
#endif
}

static void check_stream(FILE *file, const char *expected, size_t length)
{
    char bytes[4096];
    size_t n;
    assert(fflush(file) == 0 && fseek(file, 0, SEEK_SET) == 0);
    n = fread(bytes, 1, sizeof bytes, file);
    assert(n == length && !ferror(file) && feof(file));
    assert(memcmp(bytes, expected, length) == 0);
}

static void finish_checked(Pool *pool)
{
    int i;
    pool_abort(pool);
    if (pool->ready) {
        assert(pool->joined == pool->started);
        conservation(pool); /* All owners still exist immediately before free. */
    }
    pool_finish(pool);
    assert(pool->joined == pool->started);
    assert(pool->arena.held == 0 && pool->arena.cached == 0);
    for (i = 0; i < 32; ++i)
        assert(pool->slots[i]->seed.held == 0 && pool->slots[i]->seed.cached == 0);
    active_pool = NULL;
}

static void offer_named(Pool *pool, const char *name)
{
    CuDirEntry entry;
    entry.name = cu_str_cstr(name);
    entry.kind = CU_DIR_DIR;
    assert(pool_offer(pool, "/synthetic", &entry, 1, NULL) == 1);
}

static void wait_task_done(Pool *pool, Task *task)
{
    cu_mutex_lock(pool->mutex);
    while (task->state != TASK_DONE)
        cu_cond_wait(pool->publication, pool->mutex);
    conservation(pool);
    cu_mutex_unlock(pool->mutex);
}

static void shared_fixture(int mode)
{
    CuArena arena;
    Query query;
    Pool pool;
    Run run;
    Task *slot;
    Batch *reservation;
    char expected[4096];
    size_t n;
    int count;
    /* Previous fixtures have joined ALL workers before changing test globals. */
    scene = mode;
    saturated = 0;
    head_reserve_claimed = 0;
    head_publications = 0;
    b_parked = 0;
    release_b = 0;
    a_published = 0;
    c_waits = 0;
    c_claimed = 0;
    io_held = 0;
    pinned_checks = 0;
    io_checks = 0;
    held_shared = NULL;
    cu_arena_init(&arena);
    assert(parse(&query, &arena, 1, NULL) == 0);
    query.no_ignore = 1;
    query.depth = 1;
    query.pattern = cu_str_cstr("not-a-path-match");
    count = mode == BATCH_RESERVE ? 2 : 3;
    query.threads = count;
    run.arena = &arena;
    run.query = &query;
    run.out = batch_file();
    run.err = batch_file();
    assert(run.out != NULL && run.err != NULL);
    run.status = 0;
    run.stop = 0;
    run.pool = NULL;
    run.task = NULL;
    active_pool = &pool;
    assert(pool_start(&pool, count, &query));
    slot = pool.slots[0];
    reservation = slot->reserved;
    offer_named(&pool, "a");
    offer_named(&pool, "b");
    if (mode == BATCH_WAKE) {
        offer_named(&pool, "c");
        wait_flag(&c_waits);
        wait_task_done(&pool, slot); /* Exact full shared final tail, no drain. */
        cu_mutex_lock(pool.mutex);
        assert(pool.shared_free == NULL && pool.pending == 3);
        assert(slot->fill == NULL && slot->qfirst == reservation);
        assert(slot->qlast != reservation && slot->qlast->used == 64U);
        assert(slot->qlast->reserved_for == NULL);
        cu_mutex_unlock(pool.mutex);
    }
    pool_drain(&pool, &run);
    if (mode == BATCH_RESERVE) {
        assert(saturated && head_reserve_claimed && head_publications == 9);
        pool_drain(&pool, &run);
        memcpy(expected, head_out, sizeof head_out);
        memcpy(expected + sizeof head_out, head_out, 64);
        n = sizeof head_out + 64U;
        memcpy(expected + n, future_out, sizeof future_out);
        n += sizeof future_out;
        check_stream(run.out, expected, n);
        check_stream(run.err, head_err, sizeof head_err);
    } else {
        /* B remains parked. Only returning A's shared chunk can wake C, a
         * DIFFERENT producer, and C must finish before any B or C drain. */
        wait_task_done(&pool, pool.slots[2]);
        cu_mutex_lock(pool.mutex);
        assert(c_claimed && pinned_checks == 1 && held_shared != NULL);
        assert(pool.slots[2]->qlast == held_shared);
        assert(held_shared->state == B_QUEUED && pool.pending == 2);
        cu_mutex_unlock(pool.mutex);
        cu_mutex_lock(proof_mutex);
        assert(!release_b);
        release_b = 1;
        cu_cond_broadcast(proof_changed);
        cu_mutex_unlock(proof_mutex);
        pool_drain(&pool, &run);
        pool_drain(&pool, &run);
        cu_mutex_lock(pool.mutex);
        assert(pool.pending == 0 && slot->state == TASK_FREE);
        assert(slot->reserved == reservation && reservation->state == B_FREE);
        assert(slot->fill == NULL && slot->qfirst == NULL && slot->qlast == NULL);
        conservation(&pool);
        cu_mutex_unlock(pool.mutex);
        offer_named(&pool, "reuse");
        cu_mutex_lock(pool.mutex);
        assert(pool.first == slot && slot->reserved == reservation);
        cu_mutex_unlock(pool.mutex);
        pool_drain(&pool, &run);
        memcpy(expected, wake_head, sizeof wake_head);
        n = sizeof wake_head;
        memcpy(expected + n, wake_future, sizeof wake_future);
        n += sizeof wake_future;
        memcpy(expected + n, wake_other, sizeof wake_other);
        n += sizeof wake_other;
        memcpy(expected + n, reuse_out, sizeof reuse_out);
        n += sizeof reuse_out;
        check_stream(run.out, expected, n);
        check_stream(run.err, reuse_err, sizeof reuse_err);
    }
    assert(!run.stop && run.status == 0 && pool.pending == 0 && io_checks > 0);
    finish_checked(&pool);
    assert(fclose(run.out) == 0 && fclose(run.err) == 0);
    cu_arena_release(&arena);
}

/* Both abort paths retain every owner until finish. No private descriptor or
 * payload is inspected until abort has joined ALL producers. */
static void abort_fixture(int mode)
{
    CuArena arena;
    Query query;
    Pool pool;
    Run run;
    Task *a;
    Task *b;
    Batch *fill;
    Batch *shared;
    Batch *queued;
    scene = mode;
    abort_parked = 0;
    abort_exits = 0;
    failed_io = NULL;
    io_checks = 0;
    cu_arena_init(&arena);
    assert(parse(&query, &arena, 1, NULL) == 0);
    query.no_ignore = 1;
    query.depth = 1;
    query.pattern = cu_str_cstr("not-a-path-match");
    query.threads = 2;
    run.arena = &arena;
    run.query = &query;
    run.out = batch_file();
    run.err = batch_file();
    assert(run.out != NULL && run.err != NULL);
    run.status = 0;
    run.stop = 0;
    run.pool = NULL;
    run.task = NULL;
    active_pool = &pool;
    assert(pool_start(&pool, 2, &query));
    abort_held = pool.arena.held;
    abort_cached = pool.arena.cached;
    assert(abort_held >= pool.nbatches * pool.batch_bytes);
    offer_named(&pool, "a");
    offer_named(&pool, "b");
    wait_flag(&abort_parked);
    a = pool.slots[0];
    b = pool.slots[1];
    wait_task_done(&pool, a);
    cu_mutex_lock(pool.mutex);
    conservation(&pool);
    assert(pool.started == 2 && pool.joined == 0 && pool.pending == 2);
    assert(a->fill == NULL && a->qfirst == a->reserved);
    shared = a->qlast;
    assert(shared != a->reserved && shared->reserved_for == NULL);
    assert(shared->state == B_QUEUED && shared->used == 64U);
    assert(memcmp(shared->bytes, abort_head + 64, 64) == 0);
    assert(b->state == TASK_RUNNING && b->qfirst == b->reserved);
    assert(b->reserved->state == B_QUEUED);
    queued = b->qlast;
    assert(queued != b->reserved && queued == b->reserved->next);
    assert(queued->reserved_for == NULL && queued->state == B_QUEUED);
    assert(queued->used == 64U && queued->next == NULL);
    assert(memcmp(queued->bytes, abort_private + 64, 64) == 0);
    fill = b->fill; /* Pointer/state only: private used/channel/bytes stay private. */
    assert(fill != NULL && fill->reserved_for == NULL && fill->state == B_FILL);
    cu_mutex_unlock(pool.mutex);
    if (mode == BATCH_FAIL) {
        pool_drain(&pool, &run);
        assert(run.stop && run.status == 1 && io_checks == 2);
        assert(failed_io == shared && pool.io == shared);
    } else {
        pool_abort(&pool);
        assert(!run.stop && run.status == 0 && pool.io == NULL);
    }
    assert(pool.stop && pool.joined == pool.started && abort_exits == 1);
    assert(pool.arena.held == abort_held && pool.arena.cached == abort_cached);
    assert(pool.pending == 2 && a->state == TASK_DONE && b->state == TASK_DONE);
    conservation(&pool);
    assert(b->fill == fill && fill->state == B_FILL && fill->used == 1U);
    assert(fill->channel == EMIT_OUT && !fill->flush);
    assert(memcmp(fill->bytes, abort_private + 128, 1) == 0);
    assert(b->qfirst == b->reserved && b->qlast == queued);
    assert(b->reserved->next == queued && queued->next == NULL);
    assert(queued->state == B_QUEUED && queued->used == 64U);
    assert(queued->channel == EMIT_OUT && !queued->flush);
    assert(memcmp(queued->bytes, abort_private + 64, 64) == 0);
    assert(b->reserved->used == 64U);
    assert(memcmp(b->reserved->bytes, abort_private, 64) == 0);
    assert(shared->used == 64U && shared->channel == EMIT_ERR && shared->flush);
    assert(memcmp(shared->bytes, abort_head + 64, 64) == 0);
    if (mode == BATCH_FAIL) {
        assert(shared->state == B_IO && a->qfirst == NULL && a->qlast == NULL);
        assert(a->reserved->state == B_FREE);
        check_stream(run.err, abort_head, 64);
    } else {
        assert(shared->state == B_QUEUED && a->qfirst == a->reserved);
        assert(a->qlast == shared && a->reserved->next == shared);
        check_stream(run.err, "", 0);
    }
    check_stream(run.out, "", 0);
    pool_abort(&pool); /* Idempotence must not return the failed IO loan. */
    assert(pool.io == (mode == BATCH_FAIL ? shared : NULL));
    conservation(&pool);
    finish_checked(&pool);
    assert(fclose(run.out) == 0 && fclose(run.err) == 0);
    cu_arena_release(&arena);
}
#endif

int main(void)
{
#ifndef __cerb__
    CuArena proof;
    CuArena arena;
    Query query;
    Pool pool;
    size_t i;
    int ok;
    /* Production arithmetic must fit 32-bit size_t; geometry checks precede
     * multiplication in pool_start. The private max geometry is also 512. */
    assert((size_t)1048576 <= (size_t)-1 / (size_t)32);
    assert((size_t)32 * ((size_t)1048576 / (size_t)65536) == 512U);
    cu_arena_init(&proof);
    cu_arena_init(&arena);
    assert(parse(&query, &arena, 1, NULL) == 0);
    active_pool = &pool;
    errno = 0;
    ok = pool_start(&pool, 2, &query);
#ifdef __wasi__
    if (!ok)
        assert(errno == ENOSYS);
#else
    assert(ok);
#endif
    finish_checked(&pool);
    if (ok) {
        assert(cu_mutex_init(&proof_mutex, &proof));
        assert(cu_cond_init(&proof_changed, &proof));
        active_pool = &pool;
        assert(!pool_start(&pool, 0, &query));
        finish_checked(&pool);
        active_pool = &pool;
        assert(!pool_start(&pool, 33, &query));
        finish_checked(&pool);
        active_pool = &pool;
        assert(pool_start(&pool, 32, &query));
        assert(pool.nbatches == 512U);
        finish_checked(&pool);
        for (i = 0; i < sizeof head_out; ++i)
            head_out[i] = (char)(i % 127U);
        for (i = 0; i < sizeof head_err; ++i)
            head_err[i] = (char)(126U - i % 127U);
        for (i = 0; i < sizeof future_out; ++i)
            future_out[i] = (char)(i % 113U);
        memset(wake_head, 'H', sizeof wake_head);
        for (i = 0; i < sizeof wake_future; ++i)
            wake_future[i] = (char)(i % 101U);
        memset(wake_other, 'C', sizeof wake_other);
        shared_fixture(BATCH_RESERVE);
        shared_fixture(BATCH_WAKE);
        for (i = 0; i < sizeof abort_head; ++i)
            abort_head[i] = (char)(i % 97U);
        for (i = 0; i < sizeof abort_private; ++i)
            abort_private[i] = (char)(96U - i % 97U);
        abort_fixture(BATCH_ABORT);
        abort_fixture(BATCH_FAIL);
    }
    cu_arena_release(&arena);
    if (ok) {
        cu_cond_destroy(proof_changed);
        cu_mutex_destroy(proof_mutex);
    }
    cu_arena_release(&proof);
    puts(ok ? "shared batch fixtures completed" : "shared batches: ENOSYS backend");
#else
    puts("shared batch scheduling barriers unavailable under __cerb__");
#endif
    return 0;
}
