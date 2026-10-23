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
 *
 * With --sort the walk is ordered. A task still does one directory's whole
 * serial work, but keeps its records and failures in its own result, with an
 * event where each queued child's output belongs. The coordinator is the
 * writer: it drains results depth first, so stdout and stderr equal the
 * serial walk's. Helpers take queued tasks in walk order, at most SCHED_AHEAD
 * of them run but not yet reached by the writer; the writer runs a task it
 * needs that no helper has started. A failure takes effect only when the
 * writer reaches it, so helpers never stop the walk.
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
/* Ordered tasks a helper may run ahead of the writer. */
#ifndef SCHED_AHEAD
#define SCHED_AHEAD 1024U
#endif
#define SCHED_NO_SLOT ((size_t)-1)
enum { SCHED_QUEUED, SCHED_RUNNING, SCHED_DONE };

/* One point in an ordered task's output: a child directory's output, or a
 * failure as failure() was given it. */
typedef struct SchedEvent {
    size_t at; /* deferred record bytes that come before it */
    SchedTask *child; /* NULL for a failure */
    const char *message;
    const char *path; /* in the result, or NULL */
    int number;
    int terminal;
} SchedEvent;

struct SchedHold {
    SchedHold *parent;
    SchedHold *dead; /* chains holds released together, outside Mu */
    CuArena arena;
    size_t refs;
};

struct SchedTask {
    SchedTask *next;
    SchedHold *hold; /* counted owner of rules, or NULL */
    const IgnoreFrame *rules;
    /* Ordered walks only. state and slot change under Mu; the result is
     * written by the task's runner, then read by the writer once DONE. */
    size_t slot; /* heap index while queued for helpers */
    int state;
    int helped; /* run by a helper: counts against SCHED_AHEAD */
    int failed; /* deferred output could not be kept */
    size_t cursor; /* first event the writer has not finished */
    SchedHold *own; /* the hold its children borrow, kept until they are queued */
    CuArena result;
    CuVec out; /* deferred record bytes */
    CuVec events; /* SchedEvent */
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
    int ordered; /* --sort */
    CuCond *done; /* ordered: the task the writer waits for finished */
    SchedTask *awaited; /* that task, or NULL */
    size_t ahead; /* ordered tasks run by helpers, not yet reached */
    size_t running; /* ordered tasks helpers are running */
    CuArena heap_arena; /* released at each root's end */
    CuVec heap; /* SchedTask *, queued ordered tasks in walk order */
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

/* Failures in ordered tasks wait for the writer; others stop the walk now. */
static void sched_fail_task(Run *r, int terminal)
{
    if (r->task == NULL)
        sched_fail(r->sched, terminal);
}

/* Keep one event in the running ordered task. One that cannot be kept fails
 * the task: the writer reports an allocation failure where its output ends. */
static int sched_defer(Run *r, const SchedEvent *event)
{
    SchedTask *t;
    t = r->task;
    if (!t->failed) {
        CU_VEC_PUSH(&t->events, event);
        t->failed = cu_vec_failed(&t->events);
    }
    if (!t->failed)
        return 1;
    r->status = 1;
    r->stop = 1;
    return 0;
}

static int sched_defer_record(Run *r, const char *path)
{
    SchedTask *t;
    size_t n;
    t = r->task;
    n = strlen(path);
    if (!t->failed && n < (size_t)-1 && cu_vec_reserve(&t->out, n + 1)) {
        memcpy(t->out.data + t->out.len, path, n);
        t->out.data[t->out.len + n] = (char)r->query->delimiter;
        t->out.len += n + 1;
        return 1;
    }
    t->failed = 1;
    r->status = 1;
    r->stop = 1;
    return 0;
}

/* message is a literal; path is copied into the result. */
static void sched_defer_failure(Run *r, const char *message, const char *path, int number,
                                int terminal)
{
    SchedEvent event;
    event.at = cu_vec_len(&r->task->out);
    event.child = NULL;
    event.message = message;
    event.path = NULL;
    event.number = number;
    event.terminal = terminal;
    if (path != NULL) {
        event.path = cu_str_cstr_copy(&r->task->result, cu_str_cstr(path));
        if (event.path == NULL)
            r->task->failed = 1;
    }
    (void)sched_defer(r, &event);
}

/* Depth-first walk order of two paths under one root, each directory's
 * names in byte order: a path's end, then '/', then any other byte. */
static int sched_walk_order(const char *a, const char *b)
{
    unsigned char x;
    unsigned char y;
    for (;; ++a, ++b) {
        x = (unsigned char)*a;
        y = (unsigned char)*b;
        if (x != y)
            break;
        if (x == 0)
            return 0;
    }
    if (x == 0 || y == 0)
        return x == 0 ? -1 : 1;
    if (x == '/' || y == '/')
        return x == '/' ? -1 : 1;
    return x < y ? -1 : 1;
}

/* Heap of queued ordered tasks, first in walk order at the top; each task
 * knows its slot so the writer can take one out. All with Mu held. */
static void sched_heap_set(Sched *s, size_t i, SchedTask *t)
{
    CU_VEC_DATA(&s->heap, SchedTask *)[i] = t;
    t->slot = i;
}

static void sched_heap_sift(Sched *s, size_t i)
{
    SchedTask **h;
    SchedTask *t;
    size_t n;
    size_t up;
    size_t c;
    h = CU_VEC_DATA(&s->heap, SchedTask *);
    n = cu_vec_len(&s->heap);
    t = h[i];
    while (i > 0) {
        up = (i - 1) / 2;
        if (sched_walk_order(h[up]->path, t->path) <= 0)
            break;
        sched_heap_set(s, i, h[up]);
        i = up;
    }
    for (;;) {
        c = 2 * i + 1;
        if (c >= n)
            break;
        if (c + 1 < n && sched_walk_order(h[c + 1]->path, h[c]->path) < 0)
            ++c;
        if (sched_walk_order(t->path, h[c]->path) <= 0)
            break;
        sched_heap_set(s, i, h[c]);
        i = c;
    }
    sched_heap_set(s, i, t);
}

/* A task the heap cannot hold is left to the writer. */
static void sched_heap_push(Sched *s, SchedTask *t)
{
    CU_VEC_PUSH(&s->heap, &t);
    if (!cu_vec_failed(&s->heap))
        sched_heap_sift(s, cu_vec_len(&s->heap) - 1);
}

static void sched_heap_remove(Sched *s, SchedTask *t)
{
    SchedTask *last;
    size_t i;
    i = t->slot;
    (void)CU_VEC_POP(&s->heap, &last);
    t->slot = SCHED_NO_SLOT;
    if (last != t) {
        sched_heap_set(s, i, last);
        sched_heap_sift(s, i);
    }
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
static int sched_enter(Run *r, const IgnoreFrame *parent, const CuDir *dir, const char *path,
                       unsigned listed, const IgnoreFrame **out, SchedHold **own)
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
            sched_fail_task(r, 1);
            failure(r, "allocation failure", path, 0, 1);
            return 0;
        }
        cu_arena_init(&h->arena);
        h->arena.block_size = 1024;
        r->worker->spare = h;
    }
    scope = cu_temp_begin(&h->arena);
    if (!ignore_stream_enter(&h->arena, parent, dir, path, listed, out, &error)) {
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
        (void)diagnostic(r, "output failure", NULL, 0);
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
            sched_fail_task(r, 1);
            failure(r, "cannot start listing workers", NULL, 0, 1);
            return 0;
        }
        cu_mutex_lock(s->mu);
        ++s->started;
        cu_mutex_unlock(s->mu);
    }
    return 1;
}

/* Copy a child path out of the caller's temporary scope and queue it. In an
 * ordered task the child's output belongs at this point of the caller's. */
static int sched_push(Run *r, const char *path, int depth, const IgnoreFrame *rules)
{
    Sched *s;
    SchedTask *task;
    SchedEvent event;
    size_t n;
    s = r->sched;
    if (!s->launched && !sched_launch(r))
        return 0;
    n = strlen(path);
    task = (SchedTask *)FYND_TASK_ALLOC(sizeof *task + n);
    if (task == NULL) {
        sched_fail_task(r, 1);
        failure(r, "allocation failure", path, 0, 1);
        return 0;
    }
    memcpy(task->path, path, n + 1);
    task->depth = depth;
    task->rules = rules;
    task->hold = r->hold;
    /* An ordered child is queued with its siblings when this task finishes. */
    if (r->task != NULL) {
        task->slot = SCHED_NO_SLOT;
        task->state = SCHED_QUEUED;
        task->helped = 0;
        task->failed = 0;
        task->cursor = 0;
        task->own = NULL;
        event.at = cu_vec_len(&r->task->out);
        event.child = task;
        event.message = NULL;
        event.path = NULL;
        event.number = 0;
        event.terminal = 0;
        if (!sched_defer(r, &event)) {
            free(task);
            return 0;
        }
        return 1;
    }
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
    r.task = NULL;
    if (s->ordered) {
        r.task = task;
        cu_arena_init(&task->result);
        task->result.block_size = 4096;
        CU_VEC_INIT(&task->out, &task->result, char);
        CU_VEC_INIT(&task->events, &task->result, SchedEvent);
    }
    visit(&r, task->path, basename_view(task->path), CU_DIR_DIR, task->depth, task->rules);
    if (!s->ordered && (r.status != 0 || r.stop))
        sched_fail(s, r.stop);
}

/* An ordered task keeps the hold its children borrow until it queues them.
 * Returns 0 when the caller must drop own now. */
static int sched_keep(Run *r, SchedHold *own)
{
    if (r->task == NULL || r->task->own != NULL)
        return 0;
    r->task->own = own;
    return 1;
}

/* With Mu held: an ordered task has run. Its children are queued, each
 * borrowing its hold, before the task's own holds are dropped; its result
 * waits for the writer. */
static void sched_finished_locked(Sched *s, SchedTask *t)
{
    const SchedEvent *e;
    SchedHold *dead;
    SchedHold *more;
    size_t i;
    int queued;
    queued = 0;
    for (i = 0; i < cu_vec_len(&t->events); ++i) {
        e = CU_VEC_AT(&t->events, SchedEvent, i);
        if (e->child == NULL)
            continue;
        if (e->child->hold != NULL)
            ++e->child->hold->refs;
        sched_heap_push(s, e->child);
        queued = 1;
    }
    t->state = SCHED_DONE;
    if (t->helped)
        --s->running;
    if (queued && s->waiting != 0)
        cu_cond_broadcast(s->wake);
    if (s->awaited == t || (s->awaited != NULL && s->stop && s->running == 0))
        cu_cond_broadcast(s->done);
    dead = sched_drop_locked(s, t->own);
    more = sched_drop_locked(s, t->hold);
    t->own = NULL;
    t->hold = NULL;
    if (dead != NULL || more != NULL) {
        cu_mutex_unlock(s->mu);
        sched_free(dead);
        sched_free(more);
        cu_mutex_lock(s->mu);
    }
}

/* With Mu held: a helper runs the first queued ordered task in walk order. */
static void sched_take_ordered_locked(SchedWorker *w)
{
    Sched *s;
    SchedTask *t;
    s = w->sched;
    t = CU_VEC_DATA(&s->heap, SchedTask *)[0];
    sched_heap_remove(s, t);
    t->state = SCHED_RUNNING;
    t->helped = 1;
    ++s->ahead;
    ++s->running;
    cu_mutex_unlock(s->mu);
    sched_run(w, t);
    cu_mutex_lock(s->mu);
    sched_finished_locked(s, t);
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
        if (s->ordered) {
            if (!s->stop && cu_vec_len(&s->heap) != 0 && s->ahead < SCHED_AHEAD) {
                sched_take_ordered_locked(w);
                continue;
            }
        } else if (s->stack != NULL) {
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
    s->ordered = query->sort;
    s->done = NULL;
    s->awaited = NULL;
    s->ahead = 0;
    s->running = 0;
    cu_arena_init(&s->heap_arena);
    CU_VEC_INIT(&s->heap, &s->heap_arena, SchedTask *);
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
           cu_cond_init(&s->wake, &s->arena) && cu_cond_init(&s->done, &s->arena);
}

/* Writer, without Mu: wait for t's result, running t itself when no helper
 * has started it. */
static void sched_ready(Sched *s, SchedTask *t)
{
    cu_mutex_lock(s->mu);
    if (t->state == SCHED_QUEUED) {
        if (t->slot != SCHED_NO_SLOT)
            sched_heap_remove(s, t);
        t->state = SCHED_RUNNING;
        cu_mutex_unlock(s->mu);
        sched_run(&s->workers[0], t);
        cu_mutex_lock(s->mu);
        sched_finished_locked(s, t);
    }
    while (t->state != SCHED_DONE) {
        s->awaited = t;
        cu_cond_wait(s->done, s->mu);
        s->awaited = NULL;
    }
    if (t->helped) {
        t->helped = 0;
        if (s->ahead-- == SCHED_AHEAD)
            cu_cond_broadcast(s->wake);
    }
    cu_mutex_unlock(s->mu);
}

/* Write t's deferred records from *at up to end. */
static int sched_write(Run *r, const SchedTask *t, size_t *at, size_t end)
{
    size_t n;
    n = end - *at;
    if (n != 0 && (fwrite(t->out.data + *at, 1, n, r->out) != n || ferror(r->out))) {
        failure(r, "output failure", NULL, 0, 1);
        return 0;
    }
    *at = end;
    return 1;
}

/* Writer: t's output and its children's, depth first, freeing each task as
 * it finishes. Returns 0 once the walk stops; t and the tasks it has not
 * finished are then left to sched_discard. */
static int sched_drain(Sched *s, Run *r, SchedTask *t)
{
    const SchedEvent *e;
    size_t at;
    sched_ready(s, t);
    at = 0;
    for (; t->cursor < cu_vec_len(&t->events); ++t->cursor) {
        e = CU_VEC_AT(&t->events, SchedEvent, t->cursor);
        if (!sched_write(r, t, &at, e->at))
            return 0;
        if (e->child != NULL) {
            if (!sched_drain(s, r, e->child))
                return 0;
        } else {
            failure(r, e->message, e->path, e->number, e->terminal);
            if (r->stop)
                return 0;
        }
    }
    if (!sched_write(r, t, &at, cu_vec_len(&t->out)))
        return 0;
    if (t->failed) {
        failure(r, "allocation failure", NULL, 0, 1);
        return 0;
    }
    cu_arena_release(&t->result);
    free(t);
    return 1;
}

/* After a stop, with nothing running or queued for helpers: free what the
 * writer did not finish, and the holds of tasks that never ran. */
static void sched_discard(Sched *s, SchedTask *t)
{
    const SchedEvent *e;
    size_t i;
    if (t->state == SCHED_DONE) {
        for (i = t->cursor; i < cu_vec_len(&t->events); ++i) {
            e = CU_VEC_AT(&t->events, SchedEvent, i);
            if (e->child != NULL)
                sched_discard(s, e->child);
        }
        cu_arena_release(&t->result);
    } else {
        sched_drop(s, t->hold);
    }
    free(t);
}

/* Ordered coordinator: write one root's walk, then stop the helpers and free
 * whatever a stop left. The root task runs here, as worker 0. */
static void sched_root_ordered(Sched *s, Run *r, const char *path)
{
    SchedTask *root;
    size_t n;
    n = strlen(path);
    root = (SchedTask *)FYND_TASK_ALLOC(sizeof *root + n);
    if (root == NULL) {
        failure(r, "allocation failure", path, 0, 1);
        return;
    }
    memcpy(root->path, path, n + 1);
    root->depth = 0;
    root->rules = NULL;
    root->hold = NULL;
    root->slot = SCHED_NO_SLOT;
    root->state = SCHED_QUEUED;
    root->helped = 0;
    root->failed = 0;
    root->cursor = 0;
    root->own = NULL;
    if (sched_drain(s, r, root))
        return;
    /* Stopped helpers start nothing more; running tasks still queue theirs. */
    sched_fail(s, 1);
    cu_mutex_lock(s->mu);
    while (s->running != 0) {
        s->awaited = root;
        cu_cond_wait(s->done, s->mu);
        s->awaited = NULL;
    }
    while (cu_vec_len(&s->heap) != 0)
        sched_heap_remove(s, CU_VEC_DATA(&s->heap, SchedTask *)[0]);
    s->ahead = 0;
    cu_mutex_unlock(s->mu);
    sched_discard(s, root);
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
    if (s->ordered)
        sched_root_ordered(s, r, path);
    else
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
    if (s->ordered) {
        cu_arena_release(&s->heap_arena);
        cu_arena_init(&s->heap_arena);
        CU_VEC_INIT(&s->heap, &s->heap_arena, SchedTask *);
    }
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
        failure(r, "output failure", NULL, 0, 1);
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
    if (s->done != NULL)
        cu_cond_destroy(s->done);
    if (s->wake != NULL)
        cu_cond_destroy(s->wake);
    if (s->out_mu != NULL)
        cu_mutex_destroy(s->out_mu);
    if (s->mu != NULL)
        cu_mutex_destroy(s->mu);
    cu_arena_release(&s->heap_arena);
    cu_arena_release(&s->arena);
}
