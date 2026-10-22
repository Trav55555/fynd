/* Private coarse-subtree tests. Run under an external timeout: a broken
 * scheduler may deadlock the predicate barriers; elapsed time is not evidence.
 * Lock order is Pool -> Proof. No hook condition-waits under Pool; no Proof owner
 * acquires Pool. BEGIN (outside Pool) is the only deliberately held producer.
 */
#include "cu.h"
static void parallel_begin(void *opaque);
static void parallel_publish(void *opaque);
static void parallel_pending(void *opaque);
static void parallel_walk(void *opaque);
static void parallel_collect(void *opaque);
static void parallel_window(void *opaque);
static void parallel_prepare(void *opaque);
static void parallel_temp_end(CuTemp temp);
static int parallel_open(CuDir *dir, CuArena *arena, const char *path);
static int parallel_next(CuDir *dir, CuDirEntry *entry, int *result);
#define FYND_TEST_DIR_NEXT(dir, entry, result) parallel_next(dir, entry, result)
#define FYND_TASK_BYTES 64U
#define FYND_BATCH_BYTES 64U
#define FYND_TASK_BEGIN(task) parallel_begin(task)
#define FYND_TASK_PUBLISH(task) parallel_publish(task)
#define FYND_PENDING(pool) parallel_pending(pool)
#define FYND_WALK_BEGIN(run) parallel_walk(run)
#define FYND_LISTING_COLLECT(job) parallel_collect(job)
#define FYND_WINDOW_READY(run) parallel_window(run)
#define FYND_POOL_PREPARE(pool) parallel_prepare(pool)
#define cu_temp_end parallel_temp_end
#define cu_dir_open parallel_open
int serial_fixture_main(void);
#define main serial_fixture_main
#include "test_fynd.c"
#undef main
#undef cu_dir_open
#undef cu_temp_end

#ifndef __cerb__
enum { PROOF_OFF, PROOF_OVERLAP, PROOF_HEAD, PROOF_RUNNING,
       PROOF_FULL, PROOF_NESTED, PROOF_PACKETS,
       PROOF_ROOT_EIO, PROOF_WORKER_EIO };
static int proof_mode;
static int fault_kind;
static const char *fault_path;
static CuMutex *proof_mutex;
static CuCond *proof_changed;
static int collectors;
static int worker_entries;
static int head_waiting;
static int fifo_head_a;
static int future_full;
static int borrowed_checked;
static int empty_flushes;
static int peak_pending;
static int done_tokens;
static int pool_prepare_enabled;
static size_t pool_fail_attempt;
static Pool *nested_pool;
static CuArena *nested_arena;
static int nested_unwinds;
static int window_checks;
/* Cursor identities come only from successful opens of these exact paths.
 * Proof owns all late-fault state; configuration changes only between joins. */
static const char *late_root_path;
static const char *late_a_path;
static CuDir *late_root_dir;
static CuDir *late_a_dir;
static CuArena *late_arena;
static Pool *late_pool;
static Run *late_run;
static Task *late_a_task;
static Task *late_b_task;
static int late_armed;
static int late_fault_seen;
static int late_waiters;
static int late_unwinds;
static int late_first_reads;
static char late_first[256];
#ifdef __linux__
static const char *nested_trigger;
#endif

static int ends_with(const char *path, const char *suffix)
{
    size_t n;
    size_t m;
    n = strlen(path);
    m = strlen(suffix);
    return n >= m && strcmp(path + n - m, suffix) == 0;
}
#endif

static void parallel_pending(void *opaque)
{
    Pool *pool;
    int i;
    int occupied;
    int done;
    pool = (Pool *)opaque;
    occupied = 0;
    done = 0;
    for (i = 0; i < 32; ++i) {
        assert(pool->slots[i] == &pool->tasks[i]);
        if (pool->slots[i]->state != TASK_FREE) {
            assert(i < pool->count);
            ++occupied;
            if (pool->slots[i]->state == TASK_DONE)
                ++done;
        }
    }
    assert(occupied == pool->pending);
    assert(occupied >= 0 && occupied <= pool->count);
#ifndef __cerb__
    if (occupied > peak_pending)
        peak_pending = occupied;
    if (done != 0)
        done_tokens = 1;
#else
    (void)done;
#endif
}

static void parallel_begin(void *opaque)
{
#ifndef __cerb__
    Task *task;
    Pool *pool;
    size_t root_len;
    int head;
    task = (Task *)opaque;
    pool = task->pool;
    if (proof_mode == PROOF_ROOT_EIO) {
        /* Fail before parking if this is not a real two-worker dispatch.
         * The coordinator never waits for worker arrival: it faults its next
         * read with both pending tokens still held, even if one is QUEUED. */
        cu_mutex_lock(pool->mutex);
        assert(pool->started == 2 && pool->count == 2 && pool->ready);
        assert(!pool->stop && task->state == TASK_RUNNING);
        assert(task->parent != NULL);
        cu_mutex_unlock(pool->mutex);
        cu_mutex_lock(proof_mutex);
        ++late_waiters;
        cu_cond_broadcast(proof_changed);
        while (!late_fault_seen)
            cu_cond_wait(proof_changed, proof_mutex);
        /* Re-read actual borrowed rule bytes AFTER the root read failed. */
        assert(strcmp(task->parent->display_root, late_root_path) == 0);
        assert(task->parent->origin != NULL);
        assert(strcmp(task->parent->origin->display_root, late_root_path) == 0);
        assert(task->parent->files[IGNORE_PLAIN].count == 1);
        assert(task->parent->files[IGNORE_PLAIN].rules[0].negate);
        assert(task->parent->files[IGNORE_PLAIN].rules[0].len == 2);
        assert(memcmp(task->parent->files[IGNORE_PLAIN].rules[0].glob,
                      "**", 2) == 0);
        ++borrowed_checked;
        cu_mutex_unlock(proof_mutex);
        return;
    }
    if (proof_mode != PROOF_HEAD && proof_mode != PROOF_RUNNING &&
        proof_mode != PROOF_FULL && proof_mode != PROOF_NESTED)
        return;
    /* Public enumeration is unordered; the held producer is the actual FIFO
     * head, not whichever fixture name happened to be created first. */
    cu_mutex_lock(pool->mutex);
    head = pool->first == task;
    cu_mutex_unlock(pool->mutex);
    cu_mutex_lock(proof_mutex);
    if (head) {
        fifo_head_a = ends_with(task->path, "/a");
        head_waiting = 1;
        cu_cond_broadcast(proof_changed);
        if (proof_mode == PROOF_HEAD)
            while (!future_full)
                cu_cond_wait(proof_changed, proof_mutex);
    } else {
        while (!head_waiting)
            cu_cond_wait(proof_changed, proof_mutex);
    }
    cu_mutex_unlock(proof_mutex);
    if (head && (proof_mode == PROOF_RUNNING || proof_mode == PROOF_FULL ||
                 proof_mode == PROOF_NESTED)) {
        cu_mutex_lock(pool->mutex);
        while (!pool->stop)
            cu_cond_wait(pool->work, pool->mutex);
        cu_mutex_unlock(pool->mutex);
        /* The coordinator must still own the borrowed ignore scope at join. */
        assert(task->parent != NULL);
        assert(strcmp(task->parent->display_root, fault_path) == 0);
        assert(task->parent->origin != NULL);
        assert(task->parent->directory[0] == '/');
        if (proof_mode == PROOF_NESTED) {
            root_len = strlen(task->parent->origin->match_root);
            assert(strncmp(task->parent->directory,
                           task->parent->origin->match_root, root_len) == 0);
            assert(task->parent->directory[root_len] == 0 ||
                   (task->parent->directory[root_len] == '/' &&
                    task->parent->directory[root_len + 1] == 0));
            assert(strcmp(task->parent->origin->display_root, fault_path) == 0);
            assert(task->parent->files[IGNORE_PLAIN].count == 1);
            assert(task->parent->files[IGNORE_PLAIN].rules[0].negate);
            assert(task->parent->files[IGNORE_PLAIN].rules[0].len == 2);
            assert(memcmp(task->parent->files[IGNORE_PLAIN].rules[0].glob,
                          "**", 2) == 0);
        }
        cu_mutex_lock(proof_mutex);
        ++borrowed_checked;
        cu_mutex_unlock(proof_mutex);
    }
#else
    (void)opaque;
#endif
}

static void parallel_publish(void *opaque)
{
#ifndef __cerb__
    Task *task;
    Batch *batch;
    task = (Task *)opaque;
    batch = task->qlast;
    assert(batch != NULL && batch->state == B_QUEUED);
    if (proof_mode == PROOF_PACKETS && batch->used == 0 && batch->flush) {
        cu_mutex_lock(proof_mutex);
        ++empty_flushes;
        cu_mutex_unlock(proof_mutex);
    }
    if ((proof_mode == PROOF_HEAD || proof_mode == PROOF_FULL ||
         proof_mode == PROOF_NESTED) &&
        task->pool->first != task && batch->used == FYND_TASK_BYTES) {
        cu_mutex_lock(proof_mutex);
        if (!future_full) {
            assert(task->qfirst == batch && task->state == TASK_RUNNING);
            assert(task->pool->first != task);
            assert(task->pool->first->next == task);
            assert(head_waiting);
            future_full = 1;
            cu_cond_broadcast(proof_changed);
        }
        cu_mutex_unlock(proof_mutex);
    }
#else
    (void)opaque;
#endif
}

static void parallel_walk(void *opaque)
{
#ifndef __cerb__
    Run *run;
    char full[64];
    run = (Run *)opaque;
    if (proof_mutex != NULL) {
        cu_mutex_lock(proof_mutex);
        ++worker_entries;
        cu_mutex_unlock(proof_mutex);
    }
    if (fault_kind == 2 && ends_with(run->task->path, "/a")) {
        /* Before the first scope/container exists: every push is an attempt,
         * so this really fails the worker traversal arena's next allocation. */
        assert(run->arena->held == 0);
        cu_arena_release(run->arena);
        cu_arena_init_with(run->arena, 64, 1);
        cu_arena_fail_at(run->arena, run->arena->attempts);
    }
    if (proof_mode == PROOF_PACKETS) {
        assert(task_write(run->task, EMIT_OUT, "out", 3, 0));
        assert(task_write(run->task, EMIT_ERR, "err", 3, 0));
        assert(task_write(run->task, EMIT_ERR, "", 0, 1));
        assert(task_write(run->task, EMIT_OUT, "", 0, 1));
        memset(full, 'Z', sizeof full);
        /* Exactly full is not published until another write or final DONE. */
        assert(task_write(run->task, EMIT_OUT, full, sizeof full, 0));
    }
#else
    (void)opaque;
#endif
}

static void parallel_collect(void *opaque)
{
#ifndef __cerb__
    ListingJob *job;
    job = (ListingJob *)opaque;
    if (proof_mode == PROOF_OVERLAP &&
        (ends_with(job->path, "/a") || ends_with(job->path, "/b"))) {
        /* A serial-start mutant must fail before entering the open barrier. */
        cu_mutex_lock(proof_mutex);
        assert(worker_entries > 0);
        cu_mutex_unlock(proof_mutex);
    }
    if (fault_kind == 1 && strcmp(job->path, fault_path) == 0) {
        job->error = EIO;
        job->operation = 1;
    }
#else
    (void)opaque;
#endif
}

static void parallel_window(void *opaque)
{
#ifndef __cerb__
    Run *run;
    Task *tail;
    if (proof_mode == PROOF_ROOT_EIO || proof_mode == PROOF_WORKER_EIO) {
        run = (Run *)opaque;
        /* Upfront scheduler assertions, not a timeout-as-a-kill claim.
         * No arrival barrier here: both real pending borrowers suffice. */
        assert(run->task == NULL && run->pool != NULL);
        assert(!run->query->no_ignore && run->arena == late_arena);
        cu_mutex_lock(run->pool->mutex);
        assert(run->pool->started == 2 && run->pool->joined == 0);
        assert(run->pool->count == 2 && run->pool->pending == 2);
        assert(!run->pool->stop && !run->stop);
        tail = run->pool->first->next;
        assert(tail != NULL && tail->next == NULL);
        assert(run->pool->first->parent != NULL);
        assert(tail->parent == run->pool->first->parent);
        cu_mutex_lock(proof_mutex);
        assert(late_pool == NULL && late_root_dir != NULL);
        late_pool = run->pool;
        late_run = run;
        fifo_head_a = strcmp(run->pool->first->path, late_a_path) == 0;
        late_a_task = fifo_head_a ? run->pool->first : tail;
        late_b_task = fifo_head_a ? tail : run->pool->first;
        assert(strcmp(late_a_task->path, late_a_path) == 0);
        assert(ends_with(late_b_task->path, "/b"));
        if (proof_mode == PROOF_ROOT_EIO) {
            assert((late_a_task->state == TASK_RUNNING ||
                    late_a_task->state == TASK_QUEUED) &&
                   (late_b_task->state == TASK_RUNNING ||
                    late_b_task->state == TASK_QUEUED));
            assert(late_a_task->qfirst == NULL && late_b_task->qfirst == NULL);
            late_armed = 1;
        }
        ++window_checks;
        cu_mutex_unlock(proof_mutex);
        cu_mutex_unlock(run->pool->mutex);
        return;
    }
    if (proof_mode != PROOF_NESTED)
        return;
    run = (Run *)opaque;
    assert(run->task == NULL && run->pool != NULL);
    assert(run->arena == nested_arena && run->arena->debug);
    assert(!run->query->no_ignore);
    assert(run->query->kinds == (1U << (unsigned int)CU_DIR_FILE));
    cu_mutex_lock(proof_mutex);
    while (!head_waiting || !future_full)
        cu_cond_wait(proof_changed, proof_mutex);
    cu_mutex_unlock(proof_mutex);
    nested_pool = run->pool;
    cu_mutex_lock(nested_pool->mutex);
    assert(!nested_pool->stop && nested_pool->pending == 2);
    assert(nested_pool->started == 2 && nested_pool->joined == 0);
    assert(ends_with(nested_pool->first->path, "/a") ||
           ends_with(nested_pool->first->path, "/b"));
    assert(nested_pool->first->state == TASK_RUNNING);
    tail = nested_pool->first->next;
    assert(ends_with(tail->path, "/a") || ends_with(tail->path, "/b"));
    assert(strcmp(nested_pool->first->path, tail->path) != 0);
    assert(tail->state == TASK_RUNNING && tail->qfirst != NULL);
    assert(tail->qfirst->state == B_QUEUED && tail->qfirst->used == 64U);
    cu_mutex_unlock(nested_pool->mutex);
    ++window_checks;
#ifdef __linux__
    /* Drive a real selected-leaf FILE failure at this exact borrowed scope,
     * independent of where readdir placed the ordinary trigger file. */
    assert(nested_trigger != NULL);
    assert(freopen("/dev/full", "w", run->out) == run->out);
    assert(setvbuf(run->out, NULL, _IONBF, 0) == 0);
    visit(run, nested_trigger, basename_view(nested_trigger), CU_DIR_FILE, 1, NULL);
    assert(run->stop && ferror(run->out));
#endif
#else
    (void)opaque;
#endif
}

static void parallel_prepare(void *opaque)
{
#ifndef __cerb__
    Pool *pool;
    if (!pool_prepare_enabled)
        return;
    pool = (Pool *)opaque;
    assert(pool->arena.held == 0 && pool->arena.cached == 0);
    assert(pool->started == 0 && pool->joined == 0);
    cu_arena_init_with(&pool->arena, 64, 1);
    cu_arena_fail_at(&pool->arena, pool_fail_attempt);
#else
    (void)opaque;
#endif
}

static void parallel_temp_end(CuTemp temp)
{
#ifndef __cerb__
    Pool *pool;
    if ((proof_mode == PROOF_ROOT_EIO || proof_mode == PROOF_WORKER_EIO) &&
        temp.arena == late_arena) {
        cu_mutex_lock(proof_mutex);
        pool = late_pool;
        cu_mutex_unlock(proof_mutex);
        if (pool != NULL) {
            cu_mutex_lock(pool->mutex);
            cu_mutex_lock(proof_mutex);
            /* Ordinary EIO must DRAIN, not cancel, before either the root's
             * directory CuTemp or its owning root CuTemp can reclaim rules. */
            assert(late_fault_seen && !pool->stop && pool->pending == 0);
            assert(pool->started == 2 && pool->joined == 0);
            assert(late_run->status == 1 && !late_run->stop);
            assert(late_a_task->state == TASK_FREE && !late_a_task->terminal);
            assert(late_b_task->state == TASK_FREE && !late_b_task->terminal);
            assert(late_a_task->path == NULL && late_a_task->parent == NULL);
            assert(late_b_task->path == NULL && late_b_task->parent == NULL);
            assert(late_b_task->status == 0);
            assert(late_a_task->status == (proof_mode == PROOF_WORKER_EIO));
            if (proof_mode == PROOF_ROOT_EIO)
                assert(late_waiters == 2 && borrowed_checked == 2);
            else
                assert(late_first_reads == 1 && late_first[0] != 0);
            ++late_unwinds;
            if (late_unwinds == 2)
                late_pool = NULL; /* The later explicit root is independent. */
            cu_mutex_unlock(proof_mutex);
            cu_mutex_unlock(pool->mutex);
        }
    }
    if (proof_mode == PROOF_NESTED && temp.arena == nested_arena &&
        nested_pool != NULL) {
        /* The per-entry scope ended before WINDOW_READY. Both remaining
         * borrowing scopes (directory and root) must see joined producers,
         * not merely fynd_run's final pool_finish. Pool is still live here. */
        assert(nested_pool->stop);
        assert(nested_pool->joined == nested_pool->started);
        assert(nested_pool->started == 2 && borrowed_checked == 1);
        ++nested_unwinds;
    }
#endif
    cu_temp_end(temp);
}

static int parallel_open(CuDir *dir, CuArena *arena, const char *path)
{
    int ok;
    ok = cu_dir_open(dir, arena, path);
#ifndef __cerb__
    if (ok && (proof_mode == PROOF_ROOT_EIO || proof_mode == PROOF_WORKER_EIO)) {
        cu_mutex_lock(proof_mutex);
        if (strcmp(path, late_root_path) == 0) {
            assert(late_root_dir == NULL);
            late_root_dir = dir;
        }
        if (strcmp(path, late_a_path) == 0) {
            assert(late_a_dir == NULL);
            late_a_dir = dir;
        }
        cu_mutex_unlock(proof_mutex);
    }
    if (proof_mode == PROOF_OVERLAP &&
        (ends_with(path, "/a") || ends_with(path, "/b"))) {
        /* Both real directory streams must be open concurrently. A callback
         * merely being queued or a second idle worker is not sufficient. */
        assert(ok);
        cu_mutex_lock(proof_mutex);
        ++collectors;
        cu_cond_broadcast(proof_changed);
        while (collectors < 2)
            cu_cond_wait(proof_changed, proof_mutex);
        cu_mutex_unlock(proof_mutex);
    }
#endif
    return ok;
}

/* Defined after test_fynd.c's #undef: this calls the REAL cursor, never the
 * global countdown shim. Parallel tests leave listing_countdown at -1. */
static int parallel_next(CuDir *dir, CuDirEntry *entry, int *result)
{
#ifndef __cerb__
    Pool *pool;
    if (proof_mode != PROOF_ROOT_EIO && proof_mode != PROOF_WORKER_EIO)
        return 0;
    cu_mutex_lock(proof_mutex);
    if (proof_mode == PROOF_ROOT_EIO && dir == late_root_dir && late_armed) {
        pool = late_pool;
        cu_mutex_unlock(proof_mutex);
        assert(pool != NULL);
        cu_mutex_lock(pool->mutex);
        assert(!pool->stop && pool->pending == 2);
        cu_mutex_lock(proof_mutex);
        assert(!late_fault_seen);
        late_fault_seen = 1;
        late_root_dir = NULL;
        dir->error = EIO;
        dir->state = -1;
        *result = -1;
        cu_cond_broadcast(proof_changed);
        cu_mutex_unlock(proof_mutex);
        cu_mutex_unlock(pool->mutex);
        return 1;
    }
    if (proof_mode == PROOF_WORKER_EIO && dir == late_a_dir) {
        if (late_first_reads == 0) {
            *result = cu_dir_next(dir, entry);
            assert(*result == 1 && entry->kind == CU_DIR_FILE);
            assert(entry->name.len > 0 && entry->name.len < sizeof late_first);
            memcpy(late_first, entry->name.ptr, entry->name.len);
            late_first[entry->name.len] = 0;
            ++late_first_reads;
        } else {
            assert(late_first_reads == 1 && !late_fault_seen);
            late_fault_seen = 1;
            late_a_dir = NULL; /* No stale identity if the worker reuses its stack. */
            dir->error = EIO;
            dir->state = -1;
            *result = -1;
        }
        cu_mutex_unlock(proof_mutex);
        return 1;
    }
    cu_mutex_unlock(proof_mutex);
#else
    (void)dir;
    (void)entry;
    (void)result;
#endif
    return 0;
}

#ifndef __cerb__
static void stream_bytes(FILE *file, const char *expected, size_t length)
{
    char bytes[4096];
    size_t n;
    assert(length < sizeof bytes);
    assert(fflush(file) == 0);
    assert(fseek(file, 0, SEEK_SET) == 0);
    n = fread(bytes, 1, sizeof bytes, file);
    assert(!ferror(file) && n == length);
    assert(memcmp(bytes, expected, n) == 0);
    assert(feof(file));
}

static void query_init(Query *query, CuArena *arena)
{
    assert(parse(query, arena, 1, NULL) == 0);
    query->no_ignore = 1;
    query->threads = 2;
}

static void run_init(Run *run, Query *query, CuArena *arena)
{
    run->arena = arena;
    run->query = query;
    run->out = tmpfile();
    run->err = tmpfile();
    assert(run->out != NULL && run->err != NULL);
    run->status = 0;
    run->stop = 0;
    run->pool = NULL;
    run->task = NULL;
}

static void run_close(Run *run)
{
    assert(fclose(run->out) == 0);
    assert(fclose(run->err) == 0);
}

static int offer(Pool *pool, const char *root, const char *name,
                 const IgnoreFrame *parent)
{
    CuDirEntry entry;
    entry.name = cu_str_cstr(name);
    entry.kind = CU_DIR_DIR;
    return pool_offer(pool, root, &entry, 1, parent);
}

static void reset_proof(int mode)
{
    /* Called only after every previous worker has joined. */
    proof_mode = mode;
    collectors = 0;
    worker_entries = 0;
    nested_pool = NULL;
    nested_arena = NULL;
    nested_unwinds = 0;
    window_checks = 0;
    head_waiting = 0;
    fifo_head_a = 0;
    future_full = 0;
    borrowed_checked = 0;
    empty_flushes = 0;
    cu_mutex_lock(proof_mutex);
    late_root_path = NULL;
    late_a_path = NULL;
    late_root_dir = NULL;
    late_a_dir = NULL;
    late_arena = NULL;
    late_pool = NULL;
    late_run = NULL;
    late_a_task = NULL;
    late_b_task = NULL;
    late_armed = 0;
    late_fault_seen = 0;
    late_waiters = 0;
    late_unwinds = 0;
    late_first_reads = 0;
    late_first[0] = 0;
    cu_mutex_unlock(proof_mutex);
}

static void wait_proof(int *predicate)
{
    cu_mutex_lock(proof_mutex);
    while (!*predicate)
        cu_cond_wait(proof_changed, proof_mutex);
    cu_mutex_unlock(proof_mutex);
}

static void wait_done(Pool *pool)
{
    Task *task;
    int all_done;
    cu_mutex_lock(pool->mutex);
    do {
        all_done = 1;
        for (task = pool->first; task != NULL; task = task->next)
            if (task->state != TASK_DONE)
                all_done = 0;
        if (!all_done)
            cu_cond_wait(pool->publication, pool->mutex);
    } while (!all_done);
    cu_mutex_unlock(pool->mutex);
}

/* Test final publication without any consumer ACK, including an exact B tail.
 * Synthetic paths are intentional: depth==1 never opens these directories. */
static void final_tails(const char *root)
{
    CuArena arena;
    Query query;
    Pool pool;
    Run run;
    Task *a;
    Task *b;
    char name[64];
    char expected[256];
    size_t n;
    cu_arena_init(&arena);
    query_init(&query, &arena);
    query.depth = 1;
    run_init(&run, &query, &arena);
    assert(pool_start(&pool, 2, &query));
    assert(strlen(root) < 62U);
    n = 62U - strlen(root);
    assert(n > 0 && n < sizeof name);
    memset(name, 'z', n);
    name[n] = 0;
    assert(offer(&pool, root, "a", NULL) == 1);
    assert(offer(&pool, root, name, NULL) == 1);
    wait_done(&pool); /* No drain: final enqueue must not wait for ACK. */
    cu_mutex_lock(pool.mutex);
    a = pool.first;
    b = a->next;
    assert(a->state == TASK_DONE && a->qfirst != NULL);
    assert(b->state == TASK_DONE && b->qfirst != NULL);
    assert(a->qfirst == a->qlast && a->qfirst->state == B_QUEUED);
    assert(b->qfirst == b->qlast && b->qfirst->state == B_QUEUED);
    assert(a->qfirst->used != 0 && a->qfirst->used < 64U);
    assert(b->qfirst->used == 64U && pool.pending == 2);
    parallel_pending(&pool);
    cu_mutex_unlock(pool.mutex);
    assert(done_tokens);
    assert(offer(&pool, root, "third", NULL) == 0);
    assert(snprintf(expected, sizeof expected, "%s/a\n%s/%s\n",
                    root, root, name) > 0);
    pool_drain(&pool, &run);
    pool_drain(&pool, &run);
    assert(pool.pending == 0 && !run.stop && run.status == 0);
    stream_bytes(run.out, expected, strlen(expected));
    stream_bytes(run.err, "", 0);
    pool_finish(&pool);
    assert(pool.joined == pool.started);
    run_close(&run);
    cu_arena_release(&arena);
}

static void packet_test(const char *root)
{
    CuArena arena;
    Query query;
    Pool pool;
    Run run;
    char expected[67];
    cu_arena_init(&arena);
    query_init(&query, &arena);
    query.pattern = cu_str_cstr("no-match");
    query.depth = 1;
    run_init(&run, &query, &arena);
    reset_proof(PROOF_PACKETS);
    assert(pool_start(&pool, 2, &query));
    assert(offer(&pool, root, "a", NULL) == 1);
    pool_drain(&pool, &run);
    pool_finish(&pool);
    assert(!run.stop && run.status == 0 && empty_flushes == 1);
    memcpy(expected, "out", 3);
    memset(expected + 3, 'Z', 64);
    stream_bytes(run.out, expected, sizeof expected);
    stream_bytes(run.err, "err", 3);
    run_close(&run);
    cu_arena_release(&arena);
    reset_proof(PROOF_OFF);
}

/* One long raw record crosses batches; compare every byte, including LF
 * inside its name. Public single-file raw/NUL encoding remains exact too. */
static void raw_records(const char *directory, const char *name)
{
    CuArena arena;
    Query query;
    Pool pool;
    Run run;
    char expected[1024];
    char leaf[512];
    char *args[4];
    cu_arena_init(&arena);
    query_init(&query, &arena);
    query.depth = 1;
    run_init(&run, &query, &arena);
    assert(pool_start(&pool, 2, &query));
    assert(offer(&pool, directory, name, NULL) == 1);
    pool_drain(&pool, &run);
    pool_finish(&pool);
    assert(!run.stop && run.status == 0);
    assert(snprintf(expected, sizeof expected, "%s/%s\n", directory, name) > 0);
    assert(strlen(expected) > FYND_BATCH_BYTES);
    stream_bytes(run.out, expected, strlen(expected));
    stream_bytes(run.err, "", 0);
    run_close(&run);
    cu_arena_release(&arena);
    assert(snprintf(leaf, sizeof leaf, "%s/%s/leaf", directory, name) > 0);
    args[0] = "fynd"; args[1] = ""; args[2] = leaf;
    assert(snprintf(expected, sizeof expected, "%s\n", leaf) > 0);
    expect_call(3, args, 0, expected);
    args[1] = "-0"; args[2] = ""; args[3] = leaf;
    expect_bytes(4, args, 0, leaf, strlen(leaf) + 1);
}

static void seed_failure(const char *root)
{
    CuArena arena;
    Query query;
    Pool pool;
    Run run;
    Task *slot;
    size_t attempt;
    char expected[256];
    cu_arena_init(&arena);
    query_init(&query, &arena);
    query.depth = 1;
    run_init(&run, &query, &arena);
    assert(pool_start(&pool, 2, &query));
    slot = pool.slots[0];
    assert(slot->seed.held == 0);
    attempt = slot->seed.attempts;
    cu_arena_fail_at(&slot->seed, attempt);
    assert(offer(&pool, root, "a", NULL) == -1);
    assert(slot->seed.attempts == attempt + 1);
    assert(slot->seed.held == 0 && slot->state == TASK_FREE);
    assert(pool.pending == 0 && pool.first == NULL && pool.last == NULL);
    cu_arena_fail_at(&slot->seed, (size_t)-1);
    assert(offer(&pool, root, "a", NULL) == 1);
    pool_drain(&pool, &run);
    assert(pool.pending == 0 && !run.stop && run.status == 0);
    assert(snprintf(expected, sizeof expected, "%s/a\n", root) > 0);
    stream_bytes(run.out, expected, strlen(expected));
    pool_finish(&pool);
    assert(slot->seed.held == 0 && slot->seed.cached == 0);
    run_close(&run);
    cu_arena_release(&arena);
}

static void pool_allocation_failures(void)
{
    CuArena arena;
    Query query;
    Pool pool;
    size_t attempt;
    size_t allocations;
    int result;
    int partial;
    int i;
    cu_arena_init(&arena);
    query_init(&query, &arena);
    pool_prepare_enabled = 1;
    partial = 0;
    for (attempt = 0; ; ++attempt) {
        pool_fail_attempt = attempt;
        result = pool_start(&pool, 2, &query);
        allocations = pool.arena.attempts;
        if (allocations > attempt) {
            assert(!result && cu_arena_failed(&pool.arena));
            if (pool.started != 0)
                partial = 1;
        } else
            assert(result && !cu_arena_failed(&pool.arena));
        /* No fault is suppressed: finish must handle every partial constructor,
         * including failure after one real worker has already started. */
        pool_finish(&pool);
        assert(pool.joined == pool.started);
        assert(pool.arena.held == 0 && pool.arena.cached == 0);
        for (i = 0; i < 32; ++i)
            assert(pool.slots[i]->seed.held == 0 && pool.slots[i]->seed.cached == 0);
        if (allocations <= attempt)
            break;
        assert(attempt < 1000);
    }
    pool_prepare_enabled = 0;
    /* Typed controls, two slabs, mutex, three conditions, two thread records. */
    assert(attempt >= 8 && partial);
    cu_arena_release(&arena);
}

static void cancellation(const char *root, int mode)
{
    CuArena arena;
    CuTemp parent_scope;
    Query query;
    Pool pool;
    const IgnoreFrame *parent;
    IgnoreError error;
    Task *task;
    cu_arena_init(&arena);
    query_init(&query, &arena);
    parent_scope = cu_temp_begin(&arena);
    assert(ignore_seed(&arena, root, &parent, &error));
    assert(ignore_enter(&arena, parent, root, 0, &parent, &error));
    reset_proof(mode);
    fault_path = root;
    assert(pool_start(&pool, 2, &query));
    if (mode == PROOF_OFF) {
        cu_mutex_lock(pool.mutex);
        pool.ready = 0;
        cu_mutex_unlock(pool.mutex);
    }
    assert(offer(&pool, root, "a", parent) == 1);
    if (mode != PROOF_RUNNING)
        assert(offer(&pool, root, "b", parent) == 1);
    if (mode == PROOF_RUNNING)
        wait_proof(&head_waiting);
    if (mode == PROOF_FULL) {
        wait_proof(&future_full);
        cu_mutex_lock(pool.mutex);
        task = pool.first->next;
        assert(task->state == TASK_RUNNING && task->qfirst != NULL);
        assert(task->qfirst->state == B_QUEUED && task->qfirst->used == 64U);
        cu_mutex_unlock(pool.mutex);
    }
    pool_abort(&pool);
    assert(pool.joined == pool.started && pool.started == 2);
    pool_abort(&pool); /* Joining twice would be a CuThread contract failure. */
    assert(pool.joined == pool.started);
    if (mode == PROOF_OFF) {
        for (task = pool.first; task != NULL; task = task->next)
            assert(task->state == TASK_DONE && task->terminal && task->status);
    } else
        assert(borrowed_checked == 1);
    /* No borrowed frame can be reclaimed until abort has joined producers. */
    cu_temp_end(parent_scope);
    pool_finish(&pool);
    assert(pool.arena.held == 0 && pool.arena.cached == 0);
    cu_arena_release(&arena);
    reset_proof(PROOF_OFF);
    fault_path = NULL;
}

/* Independent diagnostic oracle: escape raw bytes, not source diagnostic(). */
static void error_bytes(char *dest, size_t cap, const char *message,
                        const char *path)
{
    static const char hex[] = "0123456789abcdef";
    const unsigned char *p;
    size_t n;
    int written;
    written = snprintf(dest, cap, "fynd: %s '", message);
    assert(written > 0 && (size_t)written < cap);
    n = (size_t)written;
    for (p = (const unsigned char *)path; *p != 0; ++p) {
        assert(n + 7 < cap);
        if (*p >= 32 && *p <= 126 && *p != '\\' && *p != '\'')
            dest[n++] = (char)*p;
        else {
            dest[n++] = '\\'; dest[n++] = 'x';
            dest[n++] = hex[(unsigned int)*p / 16U];
            dest[n++] = hex[(unsigned int)*p % 16U];
        }
    }
    dest[n++] = '\''; dest[n++] = '\n'; dest[n] = 0;
}

static void worker_outcomes(const char *root, const char *b_output, size_t length)
{
    CuArena arena;
    Query query;
    Pool pool;
    Run run;
    char path[256];
    char diagnostic_bytes[1024];
    int kind;
    for (kind = 1; kind <= 2; ++kind) {
        cu_arena_init(&arena);
        query_init(&query, &arena);
        query.kinds = 1U << (unsigned int)CU_DIR_FILE;
        query.delimiter = 0;
        run_init(&run, &query, &arena);
        assert(snprintf(path, sizeof path, "%s/a", root) > 0);
        fault_path = path;
        fault_kind = kind;
        assert(pool_start(&pool, 2, &query));
        assert(offer(&pool, root, "a", NULL) == 1);
        assert(offer(&pool, root, "b", NULL) == 1);
        pool_drain(&pool, &run);
        assert(run.status == 1 && run.stop == (kind == 2));
        if (kind == 1) {
            pool_drain(&pool, &run);
            assert(pool.pending == 0 && !run.stop);
            stream_record_groups(run.out, b_output, length, 0, 1);
        } else {
            assert(pool.joined == pool.started);
            stream_bytes(run.out, "", 0);
        }
        error_bytes(diagnostic_bytes, sizeof diagnostic_bytes,
                    kind == 1 ? "cannot open directory" : "allocation failure", path);
        stream_bytes(run.err, diagnostic_bytes, strlen(diagnostic_bytes));
        pool_finish(&pool);
        run_close(&run);
        cu_arena_release(&arena);
        fault_kind = 0;
        fault_path = NULL;
    }
}

static void delivery_failures(const char *root)
{
    CuArena arena;
    Query query;
    Pool pool;
    Run run;
    char path[256];
    char byte;
    cu_arena_init(&arena);
    query_init(&query, &arena);
    query.kinds = 1U << (unsigned int)CU_DIR_FILE;
    run_init(&run, &query, &arena);
    assert(snprintf(path, sizeof path, "%s/b/f00", root) > 0);
    assert(freopen(path, "w", run.err) == run.err);
    assert(fread(&byte, 1, 1, run.err) == 0 && ferror(run.err));
    fault_kind = 1;
    assert(snprintf(path, sizeof path, "%s/a", root) > 0);
    fault_path = path;
    assert(pool_start(&pool, 2, &query));
    assert(offer(&pool, root, "a", NULL) == 1);
    assert(offer(&pool, root, "b", NULL) == 1);
    pool_drain(&pool, &run);
    assert(run.status == 1 && run.stop && ferror(run.err));
    assert(pool.joined == pool.started);
    stream_bytes(run.out, "", 0);
    pool_finish(&pool);
    run_close(&run);
    fault_kind = 0;
    fault_path = NULL;
#ifdef __linux__
    run_init(&run, &query, &arena);
    assert(fclose(run.out) == 0);
    run.out = fopen("/dev/full", "w");
    assert(run.out != NULL && setvbuf(run.out, NULL, _IONBF, 0) == 0);
    assert(pool_start(&pool, 2, &query));
    assert(offer(&pool, root, "a", NULL) == 1);
    assert(offer(&pool, root, "b", NULL) == 1);
    pool_drain(&pool, &run);
    assert(run.status == 1 && run.stop && ferror(run.out));
    assert(pool.joined == pool.started);
    stream_bytes(run.err, "fynd: output failure\n", 21);
    pool_finish(&pool);
    (void)fclose(run.out);
    assert(fclose(run.err) == 0);
#endif
    cu_arena_release(&arena);
}

static void make_file(const char *path)
{
    int fd;
    fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0600);
    assert(fd >= 0 && close(fd) == 0);
}

#ifdef __linux__
static void nested_cancellation(const char *root)
{
    CuArena arena;
    FILE *out;
    FILE *err;
    FILE *ignore;
    char trigger[512];
    char ignore_path[512];
    char *args[6];
    size_t base;
    assert(snprintf(trigger, sizeof trigger, "%s/0trigger", root) > 0);
    make_file(trigger);
    assert(snprintf(ignore_path, sizeof ignore_path, "%s/.ignore", root) > 0);
    ignore = fopen(ignore_path, "w");
    assert(ignore != NULL);
    /* Make the borrowed root frame contain live rule bytes, and override
     * ancestor rules without disabling ignore discovery or evaluation. */
    assert(fputs("!**\n", ignore) >= 0 && fclose(ignore) == 0);
    out = tmpfile();
    err = tmpfile();
    assert(out != NULL && err != NULL);
    assert(setvbuf(out, NULL, _IONBF, 0) == 0);
    cu_arena_init_with(&arena, 64, 1);
    base = cu_arena_scope(&arena);
    args[0] = "fynd"; args[1] = "-j2"; args[2] = "-t";
    args[3] = "f"; args[4] = ""; args[5] = (char *)root;
    reset_proof(PROOF_NESTED);
    nested_arena = &arena;
    fault_path = root;
    nested_trigger = trigger;
    /* WINDOW_READY holds the actual FIFO head and fills its future mailbox,
     * then visits the trigger with a failing FILE before any consumer ACK. */
    assert(fynd_run(6, args, out, err, &arena) == 1);
    nested_pool = NULL;
    nested_arena = NULL;
    nested_trigger = NULL;
    assert(window_checks == 1 && nested_unwinds == 2);
    assert(head_waiting && future_full && borrowed_checked == 1);
    assert(cu_arena_scope(&arena) == base && ferror(out));
    stream_bytes(err, "fynd: output failure\nfynd: output flush failure\n", 48);
    (void)fclose(out);
    assert(fclose(err) == 0);
    cu_arena_release(&arena);
    assert(arena.held == 0 && arena.cached == 0);
    assert(unlink(trigger) == 0 && unlink(ignore_path) == 0);
    reset_proof(PROOF_OFF);
    fault_path = NULL;
}
#endif

static size_t manifest_path(char *dest, size_t cap, size_t at, const char *path)
{
    size_t n;
    n = strlen(path) + 1;
    assert(at <= cap && n <= cap - at);
    memcpy(dest + at, path, n);
    return at + n;
}

/* Run only after all existing core fixtures, inside their already-owned root
 * and after the existing unsupported-thread skip. No new temp-dir API needed. */
static void late_eio_fixtures(const char *owner)
{
    static const char *a_names[] = { "first'\n\\\200", "second\nleaf" };
    static const char *b_names[] = { "other'\n\\", "tail" };
    char root[512];
    char a[512];
    char b[512];
    char later[512];
    char path[1024];
    char policy[1024];
    char a_output[2048];
    char b_output[2048];
    char first_output[1024];
    char diagnostic_bytes[4096];
    char *args[8];
    CuArena arena;
    FILE *out;
    FILE *err;
    FILE *ignore;
    size_t a_length;
    size_t b_length;
    size_t first_length;
    size_t base;
    int mode;
    int i;
    assert(snprintf(root, sizeof root, "%s/late'\n\\\200", owner) > 0);
    assert(mkdir(root, 0700) == 0);
    assert(snprintf(a, sizeof a, "%s/a", root) > 0 && mkdir(a, 0700) == 0);
    assert(snprintf(b, sizeof b, "%s/b", root) > 0 && mkdir(b, 0700) == 0);
    assert(snprintf(later, sizeof later, "%s/late-explicit'\n\\\200", owner) > 0);
    make_file(later);
    assert(snprintf(policy, sizeof policy, "%s/.ignore", root) > 0);
    ignore = fopen(policy, "w");
    assert(ignore != NULL && fputs("!**\n", ignore) >= 0 && fclose(ignore) == 0);
    a_length = b_length = 0;
    for (i = 0; i < 2; ++i) {
        assert(snprintf(path, sizeof path, "%s/%s", a, a_names[i]) > 0);
        make_file(path);
        a_length = manifest_path(a_output, sizeof a_output, a_length, path);
        assert(snprintf(path, sizeof path, "%s/%s", b, b_names[i]) > 0);
        make_file(path);
        b_length = manifest_path(b_output, sizeof b_output, b_length, path);
    }
    args[0] = "fynd"; args[1] = "-j2"; args[2] = "-0";
    args[3] = "-t"; args[4] = "f"; args[5] = "";
    args[6] = root; args[7] = later;
    for (mode = PROOF_ROOT_EIO; mode <= PROOF_WORKER_EIO; ++mode) {
        cu_arena_init_with(&arena, 64, 1);
        base = cu_arena_scope(&arena);
        out = tmpfile(); err = tmpfile();
        assert(out != NULL && err != NULL);
        reset_proof(mode);
        cu_mutex_lock(proof_mutex);
        late_root_path = root;
        late_a_path = a;
        late_arena = &arena;
        cu_mutex_unlock(proof_mutex);
        assert(listing_countdown == -1);
        assert(fynd_run(8, args, out, err, &arena) == 1);
        assert(cu_arena_scope(&arena) == base);
        cu_mutex_lock(proof_mutex);
        assert(window_checks == 1 && late_unwinds == 2 && late_pool == NULL);
        assert(late_fault_seen);
        if (mode == PROOF_ROOT_EIO) {
            assert(late_armed && late_waiters == 2 && borrowed_checked == 2);
            first_length = a_length;
            memcpy(first_output, a_output, first_length);
        } else {
            assert(late_first_reads == 1);
            assert(strcmp(late_first, a_names[0]) == 0 ||
                   strcmp(late_first, a_names[1]) == 0);
            assert(snprintf(path, sizeof path, "%s/%s", a, late_first) > 0);
            first_length = manifest_path(first_output, sizeof first_output, 0, path);
        }
        cu_mutex_unlock(proof_mutex);
        assert(fflush(out) == 0 && fseek(out, 0, SEEK_SET) == 0);
        /* Exact root grouping AND intact whole-task groups; raw names with
         * LF/high bytes are compared as NUL multisets, never split on LF. */
        if (fifo_head_a) {
            stream_record_group(out, first_output, first_length, 0);
            stream_record_group(out, b_output, b_length, 0);
        } else {
            stream_record_group(out, b_output, b_length, 0);
            stream_record_group(out, first_output, first_length, 0);
        }
        stream_record_group(out, later, strlen(later) + 1, 0);
        assert(fgetc(out) == EOF && !ferror(out));
        error_bytes(diagnostic_bytes, sizeof diagnostic_bytes,
                    "cannot list directory", mode == PROOF_ROOT_EIO ? root : a);
        stream_bytes(err, diagnostic_bytes, strlen(diagnostic_bytes));
        assert(fclose(out) == 0 && fclose(err) == 0);
        reset_proof(PROOF_OFF);
        cu_arena_release(&arena);
        assert(arena.held == 0 && arena.cached == 0);
    }
    for (i = 0; i < 2; ++i) {
        assert(snprintf(path, sizeof path, "%s/%s", a, a_names[i]) > 0);
        assert(unlink(path) == 0);
        assert(snprintf(path, sizeof path, "%s/%s", b, b_names[i]) > 0);
        assert(unlink(path) == 0);
    }
    assert(unlink(policy) == 0 && unlink(later) == 0);
    assert(rmdir(a) == 0 && rmdir(b) == 0 && rmdir(root) == 0);
}

static void parallel_fixtures(void)
{
#ifdef __wasi__
    char template[] = "/tmp/fynd-coarse-parallel";
#else
    char template[] = "/tmp/fynd-coarse-XXXXXX";
#endif
    char *root;
    char path[512];
    char raw_name[105];
    char raw_path[512];
    char a_path[512];
    char b_path[512];
    char expected[4096];
    char partial[4096];
    char b_output[2048];
    char diagnostic_bytes[2048];
    char *args[8];
    CuArena arena;
    CuArena proof;
    Query query;
    Pool pool;
    FILE *out;
    FILE *err;
    size_t n;
    size_t a_at;
    size_t b_at;
    size_t leaf_at;
    size_t b_length;
    size_t partial_length;
    size_t attempt;
    int i;
    int supported;
    cu_arena_init(&arena);
    query_init(&query, &arena);
    errno = 0;
    supported = pool_start(&pool, 2, &query);
#ifdef __wasi__
    if (!supported)
        assert(errno == ENOSYS); /* No EAGAIN/ENOMEM regression may skip. */
#else
    assert(supported);
#endif
    pool_finish(&pool);
    cu_arena_release(&arena);
    if (!supported) {
        args[0] = "fynd"; args[1] = "-j2"; args[2] = "";
        expect_bytes(3, args, 1, "", 0);
        puts("parallel fixtures skipped: actual ENOSYS thread backend");
        return;
    }
#ifdef __wasi__
    root = template;
    assert(mkdir(root, 0700) == 0);
#else
    root = mkdtemp(template);
    assert(root != NULL);
#endif
    assert(snprintf(a_path, sizeof a_path, "%s/a", root) > 0);
    assert(mkdir(a_path, 0700) == 0);
    assert(snprintf(b_path, sizeof b_path, "%s/b", root) > 0);
    assert(mkdir(b_path, 0700) == 0);
    memset(raw_name, 'q', sizeof raw_name - 1);
    raw_name[sizeof raw_name - 1] = 0;
    raw_name[5] = '\''; raw_name[6] = '\\'; raw_name[7] = '\n';
    raw_name[8] = (char)0x80;
    assert(snprintf(raw_path, sizeof raw_path, "%s/a/%s", root, raw_name) > 0);
    assert(strlen(raw_path) > FYND_TASK_BYTES);
    assert(mkdir(raw_path, 0700) == 0);
    /* Build the NUL manifest from known fixture paths, never by splitting LF. */
    n = manifest_path(expected, sizeof expected, 0, root);
    a_at = n;
    n = manifest_path(expected, sizeof expected, n, a_path);
    n = manifest_path(expected, sizeof expected, n, raw_path);
    leaf_at = n;
    assert(snprintf(path, sizeof path, "%s/leaf", raw_path) > 0);
    make_file(path);
    n = manifest_path(expected, sizeof expected, n, path);
    b_at = n;
    n = manifest_path(expected, sizeof expected, n, b_path);
    b_length = 0;
    for (i = 0; i < 8; ++i) {
        assert(snprintf(path, sizeof path, "%s/b/f%02d", root, i) > 0);
        make_file(path);
        n = manifest_path(expected, sizeof expected, n, path);
        b_length = manifest_path(b_output, sizeof b_output, b_length, path);
    }
    assert(b_length > 64U);
    args[0] = "fynd"; args[1] = "--no-ignore"; args[2] = "-0";
    args[3] = "-j2"; args[4] = ""; args[5] = root;
    cu_arena_init(&proof);
    assert(cu_mutex_init(&proof_mutex, &proof));
    assert(cu_cond_init(&proof_changed, &proof));
    reset_proof(PROOF_OVERLAP);
    expect_record_groups(6, args, 0, expected, n, 0, 1);
    assert(collectors == 2);
    reset_proof(PROOF_HEAD);
    /* The actual FIFO head cannot produce until its future mailbox is full.
     * Each task's set is unordered, but the two whole-task groups stay FIFO. */
    out = tmpfile(); err = tmpfile();
    assert(out != NULL && err != NULL);
    assert(fynd_main(6, args, out, err) == 0);
    assert(fflush(out) == 0 && fseek(out, 0, SEEK_SET) == 0);
    stream_record_group(out, expected, a_at, 0);
    if (fifo_head_a) {
        stream_record_group(out, expected + a_at, b_at - a_at, 0);
        stream_record_group(out, expected + b_at, n - b_at, 0);
    } else {
        stream_record_group(out, expected + b_at, n - b_at, 0);
        stream_record_group(out, expected + a_at, b_at - a_at, 0);
    }
    assert(fgetc(out) == EOF && !ferror(out));
    stream_bytes(err, "", 0);
    assert(fclose(out) == 0 && fclose(err) == 0);
    assert(head_waiting && future_full && peak_pending == 2);
    reset_proof(PROOF_OFF);
    args[6] = root;
    expect_record_groups(7, args, 0, expected, n, 0, 2);
    /* Raw LF records use the same known tokens, not newline splitting. */
    args[2] = "-j2"; args[3] = ""; args[4] = root; args[5] = root;
    expect_record_groups(6, args, 0, expected, n, '\n', 2);
    args[2] = "-0"; args[3] = "-j2"; args[4] = "";
    args[5] = a_path; args[6] = b_path; args[7] = a_path;
    out = tmpfile(); err = tmpfile();
    assert(out != NULL && err != NULL);
    assert(fynd_main(8, args, out, err) == 0);
    assert(fflush(out) == 0 && fseek(out, 0, SEEK_SET) == 0);
    stream_record_group(out, expected + a_at, b_at - a_at, 0);
    stream_record_group(out, expected + b_at, n - b_at, 0);
    stream_record_group(out, expected + a_at, b_at - a_at, 0);
    assert(fgetc(out) == EOF && !ferror(out));
    stream_bytes(err, "", 0);
    assert(fclose(out) == 0 && fclose(err) == 0);
    args[5] = root;
    raw_records(a_path, raw_name);

    /* Escaped diagnostic crosses several batches and ordinary EIO continues. */
    fault_kind = 1; fault_path = raw_path;
    memcpy(partial, expected, leaf_at);
    memcpy(partial + leaf_at, expected + b_at, n - b_at);
    partial_length = leaf_at + n - b_at;
    expect_record_groups(6, args, 1, partial, partial_length, 0, 1);
    out = tmpfile(); err = tmpfile();
    assert(out != NULL && err != NULL);
    assert(fynd_main(6, args, out, err) == 1);
    stream_record_groups(out, partial, partial_length, 0, 1);
    error_bytes(diagnostic_bytes, sizeof diagnostic_bytes,
                "cannot open directory", raw_path);
    assert(strlen(diagnostic_bytes) > FYND_TASK_BYTES);
    stream_bytes(err, diagnostic_bytes, strlen(diagnostic_bytes));
    assert(fclose(out) == 0 && fclose(err) == 0);
    fault_kind = 0; fault_path = NULL;

    final_tails(root);
    packet_test(root);
    seed_failure(root);
    cancellation(root, PROOF_OFF);
    cancellation(root, PROOF_RUNNING);
    cancellation(root, PROOF_FULL);
    worker_outcomes(root, b_output, b_length);
    delivery_failures(root);
#ifdef __linux__
    nested_cancellation(root);
#endif
    pool_allocation_failures();
    /* Exactly one mutex + three conditions + two worker starts. */
    for (attempt = 0; attempt < 6; ++attempt) {
        cu_thread_fail_at(attempt);
        expect_bytes(6, args, 1, "", 0);
        cu_thread_fail_at((size_t)-1);
        expect_record_groups(6, args, 0, expected, n, 0, 1);
    }
    cu_thread_fail_at(6); /* No seventh startup primitive should be attempted. */
    expect_record_groups(6, args, 0, expected, n, 0, 1);
    cu_thread_fail_at((size_t)-1);
    /* Both default serial and explicit -j1 must touch no primitives. */
    args[3] = ""; args[4] = root;
    cu_thread_fail_at(0);
    expect_record_groups(5, args, 0, expected, n, 0, 1);
    args[3] = "-j1"; args[4] = "";
    expect_record_groups(6, args, 0, expected, n, 0, 1);
    cu_thread_fail_at((size_t)-1);
    late_eio_fixtures(root);
    cu_cond_destroy(proof_changed);
    cu_mutex_destroy(proof_mutex);
    cu_arena_release(&proof);
    proof_changed = NULL; proof_mutex = NULL;
    assert(snprintf(path, sizeof path, "%s/leaf", raw_path) > 0);
    assert(unlink(path) == 0 && rmdir(raw_path) == 0);
    for (i = 0; i < 8; ++i) {
        assert(snprintf(path, sizeof path, "%s/b/f%02d", root, i) > 0);
        assert(unlink(path) == 0);
    }
    assert(snprintf(path, sizeof path, "%s/a", root) > 0);
    assert(rmdir(path) == 0);
    assert(snprintf(path, sizeof path, "%s/b", root) > 0);
    assert(rmdir(path) == 0 && rmdir(root) == 0);
}
#endif

int main(void)
{
    int status;
    status = serial_fixture_main();
#ifndef __cerb__
    parallel_fixtures();
#else
    puts("native scheduling barriers unavailable under __cerb__; serial suite retained");
#endif
    return status;
}
