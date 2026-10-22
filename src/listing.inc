/* Actor-owned cursor. A returned name is borrowed until next or release.
 * The arena and CuDir stay at their initialized addresses throughout use. */
typedef struct ListingJob {
    CuArena arena;
    CuDir dir;
    CuTemp entry_scope;
    const char *path;
    int active;
    int open;
    int error;
    int operation;
    int state; /* 0 new, 1 reading, 2 end/released, -1 failed */
} ListingJob;

static int listing_resource(int error)
{
    return error == ENOMEM || error == E2BIG || error == EMFILE || error == ENFILE;
}

#ifndef FYND_LISTING_COLLECT
#define FYND_LISTING_COLLECT(job) ((void)0)
#endif

static void listing_init(ListingJob *job, const char *path)
{
    cu_arena_init(&job->arena);
    /* One name per scope needs a small reusable block, not a directory slab.
     * Keep the allocator's sanitizer/debug default from initialization. */
    job->arena.block_size = 4096;
    job->dir.handle = NULL;
    job->dir.arena = &job->arena;
    job->dir.state = 0;
    job->dir.error = 0;
    job->path = path;
    job->active = 0;
    job->open = 0;
    job->error = 0;
    job->operation = 0;
    job->state = 0;
}

static int listing_open(ListingJob *job)
{
    int opened;
    if (job->state != 0)
        return job->state == 1;
    job->operation = 1;
    /* Retain the pre-open fault/overlap seam, with every owner initialized. */
    FYND_LISTING_COLLECT(job);
    if (cu_arena_failed(&job->arena))
        job->error = ENOMEM;
    if (job->error != 0) {
        job->state = -1;
        return 0;
    }
    opened = cu_dir_open(&job->dir, &job->arena, job->path);
    if (!opened || cu_arena_failed(&job->arena)) {
        job->error = cu_arena_failed(&job->arena) ? ENOMEM : cu_dir_errno(&job->dir);
        if (job->error == 0)
            job->error = EIO;
        job->state = -1;
        cu_dir_close(&job->dir);
        return 0;
    }
    /* Keep a block below every entry scope so normal rewind reuses it in
     * place instead of moving a block through the cache on every name. */
    if (cu_arena_push_bytes(&job->arena, 1) == NULL) {
        job->error = ENOMEM;
        job->state = -1;
        cu_dir_close(&job->dir);
        return 0;
    }
    job->open = 1;
    job->state = 1;
    return 1;
}

static int listing_next(ListingJob *job, CuDirEntry *entry)
{
    int next;
    /* End the OLD name scope, not the scope that will own the new name. */
    if (job->active) {
        cu_temp_end(job->entry_scope);
        job->active = 0;
    }
    if (job->state != 1)
        return job->state == -1 ? -1 : 0;
    job->entry_scope = cu_temp_begin(&job->arena);
    job->active = 1;
    job->operation = 2;
    next = cu_dir_next(&job->dir, entry);
    if (cu_arena_failed(&job->arena))
        job->error = ENOMEM;
    else if (next < 0) {
        job->error = cu_dir_errno(&job->dir);
        if (job->error == 0)
            job->error = EIO;
    }
    if (next == 1 && job->error == 0)
        return 1;
    cu_temp_end(job->entry_scope);
    job->active = 0;
    cu_dir_close(&job->dir);
    job->open = 0;
    job->state = job->error != 0 ? -1 : 2;
    return job->error != 0 ? -1 : 0;
}

static void listing_release(ListingJob *job)
{
    if (job->active) {
        cu_temp_end(job->entry_scope);
        job->active = 0;
    }
    if (job->open) {
        cu_dir_close(&job->dir);
        job->open = 0;
    }
    cu_arena_release(&job->arena);
    job->state = job->error != 0 ? -1 : 2;
}
