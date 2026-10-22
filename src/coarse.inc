/* Bounded FIFO sibling-subtree windows. Workers never offer work.
 * Pending includes RUNNING, DONE/unread and the head during delivery. Claim
 * scans the task FIFO for its earliest QUEUED task; seeds/borrowed rules live
 * until drain or abort+join, and actor arenas are released before DONE.
 *
 * Payload is exactly N*FYND_TASK_BYTES, split into stable typed Batch credits.
 * Each slot keeps one exclusive reservation; all other credits are shared, so
 * a task may borrow more than its former per-task allowance. A head can always
 * fill/publish its reserve or have it delivered, even with a full future pool.
 * Mu owns scheduling, queues and FREE/FILL/QUEUED/IO transfers. Only the FILL
 * producer touches descriptors/bytes, outside Mu. Publication is immutable;
 * dequeue lends a credit to FILE I/O, and only completed I/O returns it.
 * Cancellation pins every outstanding credit until all workers have joined.
 * Listings/seeds/ignore/scratch/stacks remain outside the payload bound.
 */
#ifndef FYND_TASK_BYTES
#define FYND_TASK_BYTES 1048576U
#endif
#ifndef FYND_BATCH_BYTES
#define FYND_BATCH_BYTES 262144U
#endif
/* Ownership probes run under Mu and must never wait for another producer.
 * FILL descriptors/bytes are private and must not be inspected by probes. */
#ifndef FYND_BATCH_CHANGE
#define FYND_BATCH_CHANGE(pool) ((void)0)
#endif
#ifndef FYND_BATCH_WAIT
#define FYND_BATCH_WAIT(task) ((void)0)
#endif
#ifndef FYND_TASK_BEGIN
#define FYND_TASK_BEGIN(task) ((void)0)
#endif
#ifndef FYND_TASK_PUBLISH
#define FYND_TASK_PUBLISH(task) ((void)0)
#endif
#ifndef FYND_PENDING
#define FYND_PENDING(pool) ((void)0)
#endif
#ifndef FYND_POOL_PREPARE
#define FYND_POOL_PREPARE(pool) ((void)0)
#endif

enum { TASK_FREE, TASK_QUEUED, TASK_RUNNING, TASK_DONE };
enum { B_FREE, B_FILL, B_QUEUED, B_IO };
typedef struct Batch Batch;
struct Batch {
    Batch *next;
    Task *reserved_for; /* fixed slot owner, NULL for shared credits */
    char *bytes;
    size_t used;
    int channel;
    int flush;
    int state;
};

struct Task {
    Pool *pool;
    CuArena seed;
    const char *path;
    int depth;
    const IgnoreFrame *parent;
    Task *next;
    Batch *reserved;
    Batch *fill;
    Batch *qfirst;
    Batch *qlast;
    int write_stopped; /* producer-private; never touch canceled queued bytes */
    int state;
    int status;
    int terminal;
};

typedef struct SubtreeWorker {
    Pool *pool;
    CuThread *thread;
} SubtreeWorker;

struct Pool {
    CuArena arena;
    const Query *query;
    CuMutex *mutex;
    CuCond *work;
    CuCond *publication;
    CuCond *space;
    Task tasks[32];
    Task *slots[32];
    SubtreeWorker workers[32];
    Task *first;
    Task *last;
    Batch *batches; /* CU_PUSH typed controls, never moved */
    Batch *shared_free;
    Batch *io;
    size_t nbatches;
    size_t batch_bytes;
    int count;
    int pending;
    int started;
    int joined;
    int ready;
    int stop;
};

static void subtree_work(Pool *, Task *, int *status, int *terminal);

static int task_stopped(Task *task)
{
    Pool *pool;
    int stopped;
    pool = task->pool;
    cu_mutex_lock(pool->mutex);
    stopped = pool->stop;
    cu_mutex_unlock(pool->mutex);
    return stopped;
}

/* Called with Mu held: no ACK or replacement credit is needed to enqueue,
 * including the final partial/exactly-full tail before setting DONE. */
static void task_enqueue_locked(Task *task)
{
    Batch *batch;
    batch = task->fill;
    task->fill = NULL;
    batch->state = B_QUEUED;
    batch->next = NULL;
    if (task->qlast == NULL)
        task->qfirst = batch;
    else
        task->qlast->next = batch;
    task->qlast = batch;
    FYND_BATCH_CHANGE(task->pool);
    FYND_TASK_PUBLISH(task);
    cu_cond_broadcast(task->pool->publication);
}

static int task_publish(Task *task)
{
    Pool *pool;
    int ok;
    pool = task->pool;
    cu_mutex_lock(pool->mutex);
    ok = !pool->stop;
    if (ok)
        task_enqueue_locked(task);
    cu_mutex_unlock(pool->mutex);
    if (!ok)
        task->write_stopped = 1;
    return ok;
}

/* Acquire lazily on the next actual write/flush, never after a final enqueue.
 * A returned shared credit can serve ANY waiter, not just its last producer. */
static int task_claim(Task *task)
{
    Pool *pool;
    Batch *batch;
    pool = task->pool;
    cu_mutex_lock(pool->mutex);
    for (;;) {
        if (pool->stop) {
            cu_mutex_unlock(pool->mutex);
            task->write_stopped = 1;
            return 0;
        }
        batch = task->reserved;
        if (batch->state == B_FREE)
            break;
        batch = pool->shared_free;
        if (batch != NULL) {
            pool->shared_free = batch->next;
            break;
        }
        FYND_BATCH_WAIT(task);
        cu_cond_wait(pool->space, pool->mutex);
    }
    batch->next = NULL;
    batch->used = 0;
    batch->channel = EMIT_OUT;
    batch->flush = 0;
    batch->state = B_FILL;
    task->fill = batch;
    FYND_BATCH_CHANGE(pool);
    cu_mutex_unlock(pool->mutex);
    return 1;
}

static int task_write(Task *task, int channel, const char *bytes, size_t len,
                      int flush)
{
    Batch *batch;
    size_t take;
    if (task->write_stopped)
        return 0;
    batch = task->fill;
    if (batch != NULL && batch->used != 0 && batch->channel != channel) {
        if (!task_publish(task))
            return 0;
        batch = NULL;
    }
    if (len == 0 && !flush)
        return 1;
    while (len != 0) {
        if (batch != NULL && batch->used == task->pool->batch_bytes) {
            if (!task_publish(task))
                return 0;
            batch = NULL;
        }
        if (batch == NULL) {
            if (!task_claim(task))
                return 0;
            batch = task->fill;
        }
        batch->channel = channel;
        take = task->pool->batch_bytes - batch->used;
        if (take > len)
            take = len;
        memcpy(batch->bytes + batch->used, bytes, take);
        batch->used += take;
        bytes += take;
        len -= take;
    }
    if (flush) {
        if (batch == NULL) {
            if (!task_claim(task))
                return 0;
            batch = task->fill;
        }
        batch->channel = channel;
        batch->flush = 1;
        return task_publish(task);
    }
    return 1;
}

static void batch_return_locked(Pool *pool, Batch *batch)
{
    pool->io = NULL;
    batch->used = 0;
    batch->channel = EMIT_OUT;
    batch->flush = 0;
    batch->state = B_FREE;
    batch->next = NULL;
    if (batch->reserved_for == NULL) {
        batch->next = pool->shared_free;
        pool->shared_free = batch;
    }
    FYND_BATCH_CHANGE(pool);
    cu_cond_broadcast(pool->space);
}

static void subtree_worker(void *context)
{
    SubtreeWorker *worker;
    Pool *pool;
    Task *task;
    int status;
    int terminal;
    worker = (SubtreeWorker *)context;
    pool = worker->pool;
    cu_mutex_lock(pool->mutex);
    for (;;) {
        task = NULL;
        if (pool->stop)
            break;
        if (pool->ready)
            for (task = pool->first; task != NULL; task = task->next)
                if (task->state == TASK_QUEUED)
                    break;
        if (task == NULL) {
            cu_cond_wait(pool->work, pool->mutex);
            continue;
        }
        task->state = TASK_RUNNING;
        cu_mutex_unlock(pool->mutex);
        status = 0;
        terminal = 0;
        FYND_TASK_BEGIN(task);
        subtree_work(pool, task, &status, &terminal);
        cu_mutex_lock(pool->mutex);
        task->status = status;
        task->terminal = terminal;
        if (!pool->stop && task->fill != NULL)
            task_enqueue_locked(task);
        task->state = TASK_DONE;
        cu_cond_broadcast(pool->publication);
    }
    cu_mutex_unlock(pool->mutex);
}

/* Coordinator-only lifecycle. Track joins separately from starts so stop at
 * any walker scope is safe, and finish also handles every partial startup. */
static void pool_abort(Pool *pool)
{
    Task *task;
    if (pool->mutex != NULL) {
        cu_mutex_lock(pool->mutex);
        pool->stop = 1;
        for (task = pool->first; task != NULL; task = task->next)
            if (task->state == TASK_QUEUED) {
                task->state = TASK_DONE;
                task->status = 1;
                task->terminal = 1;
            }
        if (pool->work != NULL)
            cu_cond_broadcast(pool->work);
        if (pool->publication != NULL)
            cu_cond_broadcast(pool->publication);
        if (pool->space != NULL)
            cu_cond_broadcast(pool->space);
        cu_mutex_unlock(pool->mutex);
    }
    while (pool->joined < pool->started) {
        cu_thread_join(pool->workers[pool->joined].thread);
        ++pool->joined;
    }
}

static void pool_finish(Pool *pool)
{
    int i;
    pool_abort(pool);
    for (i = 0; i < 32; ++i)
        cu_arena_release(&pool->slots[i]->seed);
    /* Reverse primitive acquisition order, after every successful start joins. */
    if (pool->space != NULL)
        cu_cond_destroy(pool->space);
    if (pool->publication != NULL)
        cu_cond_destroy(pool->publication);
    if (pool->work != NULL)
        cu_cond_destroy(pool->work);
    if (pool->mutex != NULL)
        cu_mutex_destroy(pool->mutex);
    cu_arena_release(&pool->arena);
}

static int pool_start(Pool *pool, int count, const Query *query)
{
    Task *task;
    Batch *batch;
    char *slab;
    size_t per_slab;
    size_t j;
    size_t k;
    int i;
    cu_arena_init(&pool->arena);
    pool->query = query;
    pool->mutex = NULL;
    pool->work = NULL;
    pool->publication = NULL;
    pool->space = NULL;
    pool->first = NULL;
    pool->last = NULL;
    pool->batches = NULL;
    pool->shared_free = NULL;
    pool->io = NULL;
    pool->nbatches = 0;
    pool->batch_bytes = (size_t)FYND_BATCH_BYTES;
    pool->count = count;
    pool->pending = 0;
    pool->started = 0;
    pool->joined = 0;
    pool->ready = 0;
    pool->stop = 0;
    for (i = 0; i < 32; ++i) {
        task = &pool->tasks[i];
        pool->slots[i] = task;
        task->pool = pool;
        cu_arena_init(&task->seed);
        task->path = NULL;
        task->depth = 0;
        task->parent = NULL;
        task->next = NULL;
        task->reserved = NULL;
        task->fill = NULL;
        task->qfirst = NULL;
        task->qlast = NULL;
        task->write_stopped = 0;
        task->state = TASK_FREE;
        task->status = 0;
        task->terminal = 0;
        pool->workers[i].pool = pool;
        pool->workers[i].thread = NULL;
    }
    if (count < 1 || count > 32 || (size_t)FYND_TASK_BYTES == 0 ||
        pool->batch_bytes == 0)
        return 0;
    if (pool->batch_bytes > (size_t)FYND_TASK_BYTES)
        pool->batch_bytes = (size_t)FYND_TASK_BYTES;
    if ((size_t)FYND_TASK_BYTES % pool->batch_bytes != 0 ||
        (size_t)FYND_TASK_BYTES > (size_t)-1 / (size_t)count)
        return 0;
    per_slab = (size_t)FYND_TASK_BYTES / pool->batch_bytes;
    if (per_slab > (size_t)512 / (size_t)count)
        return 0;
    pool->nbatches = (size_t)count * per_slab;
    FYND_POOL_PREPARE(pool);
    pool->batches = CU_PUSH(&pool->arena, Batch, pool->nbatches);
    if (pool->batches == NULL)
        return 0;
    /* Initialize every control before any later allocation can fail. */
    for (k = 0; k < pool->nbatches; ++k) {
        batch = &pool->batches[k];
        batch->next = NULL;
        batch->reserved_for = NULL;
        batch->bytes = NULL;
        batch->used = 0;
        batch->channel = EMIT_OUT;
        batch->flush = 0;
        batch->state = B_FREE;
        if (k < (size_t)count) {
            batch->reserved_for = pool->slots[k];
            pool->slots[k]->reserved = batch;
        } else {
            batch->next = pool->shared_free;
            pool->shared_free = batch;
        }
    }
    for (i = 0; i < count; ++i) {
        slab = cu_arena_push_bytes(&pool->arena, (size_t)FYND_TASK_BYTES);
        if (slab == NULL)
            return 0;
        for (j = 0; j < per_slab; ++j)
            pool->batches[(size_t)i * per_slab + j].bytes =
                slab + j * pool->batch_bytes;
    }
    if (!cu_mutex_init(&pool->mutex, &pool->arena) ||
        !cu_cond_init(&pool->work, &pool->arena) ||
        !cu_cond_init(&pool->publication, &pool->arena) ||
        !cu_cond_init(&pool->space, &pool->arena))
        return 0;
    for (i = 0; i < count; ++i) {
        if (!cu_thread_start(&pool->workers[i].thread, &pool->arena, 0,
                             subtree_worker, &pool->workers[i]))
            return 0;
        ++pool->started;
    }
    cu_mutex_lock(pool->mutex);
    pool->ready = 1;
    FYND_BATCH_CHANGE(pool);
    cu_cond_broadcast(pool->work);
    cu_mutex_unlock(pool->mutex);
    return 1;
}

/* The coordinator alone allocates seeds. A failed/full offer consumes neither
 * a token nor an entry in the caller's monotonic refill cursor. */
static int pool_offer(Pool *pool, const char *directory, const CuDirEntry *entry,
                      int child_depth, const IgnoreFrame *parent)
{
    Task *task;
    char *path;
    int i;
    cu_mutex_lock(pool->mutex);
    if (pool->stop || pool->pending == pool->count) {
        cu_mutex_unlock(pool->mutex);
        return 0;
    }
    task = NULL;
    for (i = 0; i < pool->count; ++i)
        if (pool->slots[i]->state == TASK_FREE) {
            task = pool->slots[i];
            break;
        }
    if (task == NULL)
        abort();
    path = join_path(&task->seed, directory, entry->name);
    if (path == NULL) {
        cu_arena_release(&task->seed);
        cu_mutex_unlock(pool->mutex);
        return -1;
    }
    task->path = path;
    task->depth = child_depth;
    task->parent = parent;
    task->next = NULL;
    task->fill = NULL;
    task->qfirst = NULL;
    task->qlast = NULL;
    task->write_stopped = 0;
    task->status = 0;
    task->terminal = 0;
    task->state = TASK_QUEUED;
    if (pool->last == NULL)
        pool->first = task;
    else
        pool->last->next = task;
    pool->last = task;
    ++pool->pending;
    FYND_PENDING(pool);
    cu_cond_broadcast(pool->work);
    cu_mutex_unlock(pool->mutex);
    return 1;
}

/* Consume exactly the FIFO head, retaining its token throughout delivery.
 * failure() is deliberately outside mutex, with coordinator.task==NULL: its
 * diagnostics cannot recurse into or overwrite the borrowed IO batch. */
static void pool_drain(Pool *pool, Run *coordinator)
{
    Task *task;
    Batch *batch;
    const char *bytes;
    size_t len;
    int channel;
    int flush;
    int status;
    int terminal;
    cu_mutex_lock(pool->mutex);
    task = pool->first;
    if (task == NULL || pool->stop) {
        cu_mutex_unlock(pool->mutex);
        return;
    }
    for (;;) {
        while (task->qfirst == NULL && task->state != TASK_DONE && !pool->stop)
            cu_cond_wait(pool->publication, pool->mutex);
        if (pool->stop) {
            cu_mutex_unlock(pool->mutex);
            return;
        }
        if (task->qfirst != NULL) {
            batch = task->qfirst;
            task->qfirst = batch->next;
            if (task->qfirst == NULL)
                task->qlast = NULL;
            batch->next = NULL;
            batch->state = B_IO;
            pool->io = batch;
            bytes = batch->bytes;
            len = batch->used;
            channel = batch->channel;
            flush = batch->flush;
            FYND_BATCH_CHANGE(pool);
            cu_mutex_unlock(pool->mutex);
            if (!emit(coordinator, channel, bytes, len, flush)) {
                if (channel == EMIT_OUT)
                    failure(coordinator, "output failure", NULL, 1);
                else {
                    coordinator->status = 1;
                    coordinator->stop = 1;
                }
                pool_abort(pool);
                return;
            }
            cu_mutex_lock(pool->mutex);
            batch_return_locked(pool, batch);
        }
        if (task->state == TASK_DONE && task->qfirst == NULL)
            break;
    }
    status = task->status;
    terminal = task->terminal;
    pool->first = task->next;
    if (pool->first == NULL)
        pool->last = NULL;
    task->next = NULL;
    task->path = NULL;
    task->parent = NULL;
    task->state = TASK_FREE;
    --pool->pending;
    FYND_PENDING(pool);
    cu_mutex_unlock(pool->mutex);
    cu_arena_release(&task->seed);
    if (status != 0)
        coordinator->status = 1;
    if (terminal) {
        coordinator->status = 1;
        coordinator->stop = 1;
        pool_abort(pool);
    }
}
