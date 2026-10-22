/* Shared-stack subtree scheduler for -j N (N > 1).
 *
 * N threads take part: the coordinator is worker 0 and starts the other N-1
 * lazily, on the first subdirectory push, so a root without subdirectories
 * starts none. Every worker runs visit() on one directory task at a time;
 * a descending child becomes a new task instead of recursion. Tasks are a
 * LIFO stack under Mu, which also owns pending, generation, stop and status.
 *
 * Records are buffered whole per worker (SCHED_BUFFER) and written to the
 * caller's stream under Out, so bytes of different records never interleave.
 * Diagnostics are written whole under Out as well.
 *
 * A directory that loads rules keeps its ignore frame in a hold: a small
 * arena counted by the visit that made it, by every task that borrows it and
 * by each child hold whose frame chains to it. A hold is freed when its
 * count reaches zero, so frames live only as long as their subtree's work.
 * Directories without rules reuse a per-worker spare hold and keep nothing.
 *
 * A root ends when pending is zero (no task queued or running) and every
 * started worker has flushed its buffer and released its arena: the root
 * barrier. No hold is live then; the root's seed frames, which live in
 * worker 0's arena, are released with it. Root groups therefore stay in
 * argument order. Terminal failures set stop: queued tasks are dropped
 * unrun and running ones poll stop like the serial walker.
 */
#ifndef SCHED_BUFFER
#define SCHED_BUFFER 65536U
#endif
#ifndef FYND_TASK_ALLOC
#define FYND_TASK_ALLOC(size) malloc(size)
#endif
/* Observes the live hold count under Mu after each new hold. */
#ifndef FYND_HOLDS_LIVE
#define FYND_HOLDS_LIVE(count) ((void)0)
#endif

struct SchedHold {
    SchedHold *parent;
    SchedHold *dead; /* chains holds released together, outside Mu */
    CuArena arena;
    size_t refs;
};

typedef struct SchedTask SchedTask;
struct SchedTask {
    SchedTask *next;
    SchedHold *hold; /* counted owner of rules, or NULL */
    const IgnoreFrame *rules;
    int depth;
    char path[1];
};

struct SchedWorker {
    Sched *sched;
    CuThread *thread;
    CuArena arena;
    SchedHold *spare; /* reused while directories load no rules */
    char *buffer;
    size_t used;
    unsigned seen;
};

struct Sched {
    const Query *query;
    FILE *out;
    FILE *err;
    CuMutex *mu;
    CuMutex *out_mu;
    CuCond *wake;
    SchedTask *stack;
    size_t pending;
    size_t live_holds;
    unsigned generation;
    int acked;
    int waiting;
    int count;
    int launched;
    int started;
    int stop;
    int status;
    int quit;
    int output_reported;
    int settle_failed;
    CuArena arena;
    SchedWorker workers[32];
};

static int sched_stopped(Sched *s)
{
    int stopped;
    cu_mutex_lock(s->mu);
    stopped = s->stop;
    cu_mutex_unlock(s->mu);
    return stopped;
}

static void sched_lock_out(Run *r, int lock)
{
    if (r->worker == NULL)
        return;
    if (lock)
        cu_mutex_lock(r->sched->out_mu);
    else
        cu_mutex_unlock(r->sched->out_mu);
}

static void sched_fail(Sched *s, int terminal)
{
    cu_mutex_lock(s->mu);
    s->status = 1;
    if (terminal)
        s->stop = 1;
    cu_mutex_unlock(s->mu);
}

/* With Mu held: drop one reference and return the holds that reached zero,
 * for sched_free after Mu is released. */
static SchedHold *sched_drop_locked(Sched *s, SchedHold *h)
{
    SchedHold *dead;
    dead = NULL;
    while (h != NULL && --h->refs == 0) {
        h->dead = dead;
        dead = h;
        --s->live_holds;
        h = h->parent;
    }
    return dead;
}

/* Without Mu: no task or hold refers to a dead hold any more. */
static void sched_free(SchedHold *dead)
{
    SchedHold *next;
    while (dead != NULL) {
        next = dead->dead;
        cu_arena_release(&dead->arena);
        free(dead);
        dead = next;
    }
}

static void sched_drop(Sched *s, SchedHold *h)
{
    SchedHold *dead;
    cu_mutex_lock(s->mu);
    dead = sched_drop_locked(s, h);
    cu_mutex_unlock(s->mu);
    sched_free(dead);
}

/* Enter a directory's ignore policy in the worker's spare hold. Returns 0
 * after reporting a failure. With new rules, *own is a counted hold that the
 * caller must drop; otherwise *out is parent and the spare is rolled back. */
static int sched_enter(Run *r, const IgnoreFrame *parent, const char *path,
                       const IgnoreFrame **out, SchedHold **own)
{
    Sched *s;
    SchedHold *h;
    CuTemp scope;
    IgnoreError error;
    s = r->sched;
    *own = NULL;
    h = r->worker->spare;
    if (h == NULL) {
        h = (SchedHold *)FYND_TASK_ALLOC(sizeof *h);
        if (h == NULL) {
            sched_fail(s, 1);
            failure(r, "allocation failure", path, 1);
            return 0;
        }
        cu_arena_init(&h->arena);
        h->arena.block_size = 1024;
        r->worker->spare = h;
    }
    scope = cu_temp_begin(&h->arena);
    if (!ignore_stream_enter(&h->arena, parent, path, out, &error)) {
        ignore_failure(r, &error); /* error.path may point into this scope */
        cu_temp_end(scope);
        return 0;
    }
    if (*out == parent) {
        cu_temp_end(scope);
        return 1;
    }
    /* The open scope is discarded when the hold's arena is released. */
    r->worker->spare = NULL;
    h->parent = r->hold;
    h->refs = 1;
    cu_mutex_lock(s->mu);
    if (h->parent != NULL)
        ++h->parent->refs;
    ++s->live_holds;
    FYND_HOLDS_LIVE(s->live_holds);
    cu_mutex_unlock(s->mu);
    *own = h;
    return 1;
}

/* Report one "output failure" however many workers observe it. */
static void sched_output_failure(Run *r)
{
    Sched *s;
    int first;
    s = r->sched;
    cu_mutex_lock(s->mu);
    first = !s->output_reported;
    s->output_reported = 1;
    s->status = 1;
    s->stop = 1;
    cu_mutex_unlock(s->mu);
    r->status = 1;
    r->stop = 1;
    if (first)
        (void)diagnostic(r, "output failure", NULL);
}

static int sched_flush(SchedWorker *w)
{
    Sched *s;
    int ok;
    s = w->sched;
    if (w->used == 0)
        return 1;
    cu_mutex_lock(s->out_mu);
    ok = fwrite(w->buffer, 1, w->used, s->out) == w->used && !ferror(s->out);
    cu_mutex_unlock(s->out_mu);
    w->used = 0;
    return ok;
}

/* Append one whole record; a record longer than the buffer is written alone. */
static int sched_record(SchedWorker *w, const char *path, int delimiter)
{
    Sched *s;
    size_t n;
    int ok;
    s = w->sched;
    n = strlen(path);
    if (n >= SCHED_BUFFER - w->used && !sched_flush(w))
        return 0;
    if (n >= SCHED_BUFFER) {
        cu_mutex_lock(s->out_mu);
        ok = fwrite(path, 1, n, s->out) == n && fputc(delimiter, s->out) != EOF &&
             !ferror(s->out);
        cu_mutex_unlock(s->out_mu);
        return ok;
    }
    memcpy(w->buffer + w->used, path, n);
    w->buffer[w->used + n] = (char)delimiter;
    w->used += n + 1;
    return 1;
}

static void sched_worker(void *context);

/* Called by the coordinator before any other worker exists. */
static int sched_launch(Run *r)
{
    Sched *s;
    int i;
    s = r->sched;
    s->launched = 1;
    for (i = 1; i < s->count; ++i) {
        /* Only the coordinator changes generation, and it is here. */
        s->workers[i].seen = s->generation;
        if (!cu_thread_start(&s->workers[i].thread, &s->arena, 0, sched_worker,
                             &s->workers[i])) {
            sched_fail(s, 1);
            failure(r, "cannot start listing workers", NULL, 1);
            return 0;
        }
        cu_mutex_lock(s->mu);
        ++s->started;
        cu_mutex_unlock(s->mu);
    }
    return 1;
}

/* Copy a child path out of the caller's temporary scope and queue it. */
static int sched_push(Run *r, const char *path, int depth, const IgnoreFrame *rules)
{
    Sched *s;
    SchedTask *task;
    size_t n;
    s = r->sched;
    if (!s->launched && !sched_launch(r))
        return 0;
    n = strlen(path);
    task = (SchedTask *)FYND_TASK_ALLOC(sizeof *task + n);
    if (task == NULL) {
        sched_fail(s, 1);
        failure(r, "allocation failure", path, 1);
        return 0;
    }
    memcpy(task->path, path, n + 1);
    task->depth = depth;
    task->rules = rules;
    task->hold = r->hold;
    cu_mutex_lock(s->mu);
    if (task->hold != NULL)
        ++task->hold->refs;
    task->next = s->stack;
    s->stack = task;
    ++s->pending;
    if (s->waiting != 0)
        cu_cond_broadcast(s->wake);
    cu_mutex_unlock(s->mu);
    return 1;
}

static void sched_run(SchedWorker *w, SchedTask *task)
{
    Sched *s;
    Run r;
    s = w->sched;
    r.arena = &w->arena;
    r.query = s->query;
    r.out = s->out;
    r.err = s->err;
    r.status = 0;
    r.stop = 0;
    r.sched = s;
    r.worker = w;
    r.hold = task->hold;
    visit(&r, task->path, basename_view(task->path), CU_DIR_DIR, task->depth, task->rules);
    if (r.status != 0 || r.stop)
        sched_fail(s, r.stop);
}

/* With Mu held: pop and run (or drop, after stop) one task. */
static void sched_take_locked(SchedWorker *w)
{
    Sched *s;
    SchedTask *task;
    SchedHold *dead;
    s = w->sched;
    task = s->stack;
    s->stack = task->next;
    if (!s->stop) {
        cu_mutex_unlock(s->mu);
        sched_run(w, task);
        cu_mutex_lock(s->mu);
    }
    dead = sched_drop_locked(s, task->hold);
    if (--s->pending == 0)
        cu_cond_broadcast(s->wake);
    if (dead != NULL) {
        cu_mutex_unlock(s->mu);
        sched_free(dead);
        cu_mutex_lock(s->mu);
    }
    free(task);
}

/* Barrier step, without Mu: nothing is queued or running, so no task borrows
 * this arena and no other record of this root is still being produced. */
static int sched_settle(SchedWorker *w)
{
    int ok;
    ok = sched_flush(w);
    cu_arena_release(&w->arena);
    cu_arena_init(&w->arena);
    return ok;
}

static void sched_worker(void *context)
{
    SchedWorker *w;
    Sched *s;
    int ok;
    w = (SchedWorker *)context;
    s = w->sched;
    cu_mutex_lock(s->mu);
    for (;;) {
        if (s->quit)
            break;
        if (s->stack != NULL) {
            sched_take_locked(w);
            continue;
        }
        if (w->seen != s->generation) {
            cu_mutex_unlock(s->mu);
            ok = sched_settle(w);
            cu_mutex_lock(s->mu);
            if (!ok)
                s->settle_failed = 1;
            w->seen = s->generation;
            ++s->acked;
            cu_cond_broadcast(s->wake);
            continue;
        }
        ++s->waiting;
        cu_cond_wait(s->wake, s->mu);
        --s->waiting;
    }
    cu_mutex_unlock(s->mu);
}

static int sched_start(Sched *s, int count, const Query *query, FILE *out, FILE *err)
{
    int i;
    cu_arena_init(&s->arena);
    s->query = query;
    s->out = out;
    s->err = err;
    s->mu = NULL;
    s->out_mu = NULL;
    s->wake = NULL;
    s->stack = NULL;
    s->pending = 0;
    s->live_holds = 0;
    s->generation = 0;
    s->acked = 0;
    s->waiting = 0;
    s->count = count;
    s->launched = 0;
    s->started = 0;
    s->stop = 0;
    s->status = 0;
    s->quit = 0;
    s->output_reported = 0;
    s->settle_failed = 0;
    for (i = 0; i < 32; ++i) {
        s->workers[i].sched = s;
        s->workers[i].thread = NULL;
        s->workers[i].spare = NULL;
        s->workers[i].buffer = NULL;
        s->workers[i].used = 0;
        s->workers[i].seen = 0;
        cu_arena_init(&s->workers[i].arena);
    }
    if (count < 2 || count > 32)
        return 0;
    for (i = 0; i < count; ++i) {
        s->workers[i].buffer = cu_arena_push_bytes(&s->arena, SCHED_BUFFER);
        if (s->workers[i].buffer == NULL)
            return 0;
    }
    return cu_mutex_init(&s->mu, &s->arena) && cu_mutex_init(&s->out_mu, &s->arena) &&
           cu_cond_init(&s->wake, &s->arena);
}

/* Coordinator: walk one directory root as worker 0, help drain the stack,
 * then hold the root barrier until every started worker has settled. */
static void sched_root(Sched *s, Run *r, const char *path)
{
    SchedWorker *w;
    CuArena *caller;
    int ok;
    int report;
    w = &s->workers[0];
    /* Frames this walk keeps must live in an arena the barrier releases. */
    caller = r->arena;
    r->arena = &w->arena;
    r->sched = s;
    r->worker = w;
    r->hold = NULL;
    visit(r, path, basename_view(path), CU_DIR_DIR, 0, NULL);
    r->arena = caller;
    if (r->stop)
        sched_fail(s, 1);
    cu_mutex_lock(s->mu);
    while (s->pending != 0) {
        if (s->stack != NULL) {
            sched_take_locked(w);
            continue;
        }
        ++s->waiting;
        cu_cond_wait(s->wake, s->mu);
        --s->waiting;
    }
    /* Every visit and task has dropped its references by now. */
    if (s->live_holds != 0)
        abort();
    ++s->generation;
    s->acked = 0;
    cu_cond_broadcast(s->wake);
    cu_mutex_unlock(s->mu);
    ok = sched_settle(w);
    cu_mutex_lock(s->mu);
    while (s->acked != s->started)
        cu_cond_wait(s->wake, s->mu);
    if (!ok)
        s->settle_failed = 1;
    report = s->settle_failed && !s->output_reported;
    if (s->settle_failed) {
        s->output_reported = 1;
        s->status = 1;
        s->stop = 1;
    }
    if (s->status != 0)
        r->status = 1;
    if (s->stop)
        r->stop = 1;
    cu_mutex_unlock(s->mu);
    r->worker = NULL;
    if (report)
        failure(r, "output failure", NULL, 1);
}

static void sched_finish(Sched *s)
{
    int i;
    SchedTask *task;
    if (s->mu != NULL) {
        cu_mutex_lock(s->mu);
        s->quit = 1;
        if (s->wake != NULL)
            cu_cond_broadcast(s->wake);
        cu_mutex_unlock(s->mu);
    }
    for (i = 1; i <= s->started; ++i)
        cu_thread_join(s->workers[i].thread);
    while (s->stack != NULL) {
        task = s->stack;
        s->stack = task->next;
        sched_free(sched_drop_locked(s, task->hold));
        free(task);
    }
    for (i = 0; i < 32; ++i) {
        if (s->workers[i].spare != NULL) {
            cu_arena_release(&s->workers[i].spare->arena);
            free(s->workers[i].spare);
        }
        cu_arena_release(&s->workers[i].arena);
    }
    if (s->wake != NULL)
        cu_cond_destroy(s->wake);
    if (s->out_mu != NULL)
        cu_mutex_destroy(s->out_mu);
    if (s->mu != NULL)
        cu_mutex_destroy(s->mu);
    cu_arena_release(&s->arena);
}
