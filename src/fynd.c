#include "fynd.h"
#include "cu.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

/* Directory depth at which a walk stops with a recursion safety error, and
 * the largest --max-depth. The walk recurses once per level. */
#define FYND_DEPTH_LIMIT 256

typedef struct Query {
    CuStr pattern;
    CuVec extensions;
    CuVec roots;
    unsigned int kinds;
    int fold;
    int hidden;
    int delimiter;
    int depth;
    int no_ignore;
    int threads;
    int sort;
} Query;

typedef struct Sched Sched;
typedef struct SchedWorker SchedWorker;
typedef struct SchedHold SchedHold;
typedef struct SchedTask SchedTask;
enum { EMIT_OUT = 0, EMIT_ERR = 1 };

typedef struct Run {
    CuArena *arena;
    const Query *query;
    FILE *out;
    FILE *err;
    int status;
    int stop;
    Sched *sched; /* non-NULL while a -j N walk is active */
    SchedWorker *worker; /* this thread's worker, with sched */
    SchedHold *hold; /* owner of the frame children borrow, or NULL */
    SchedTask *task; /* with --sort under sched: where output is deferred */
} Run;

static void sched_lock_out(Run *, int);
static void sched_defer_failure(Run *, const char *, const char *, int);

static int emit(Run *r, int channel, const char *bytes, size_t len, int flush)
{
    FILE *stream;
    int ok;
    stream = channel == EMIT_OUT ? r->out : r->err;
    ok = len == 0 || fwrite(bytes, 1, len, stream) == len;
    if (flush && fflush(stream) == EOF)
        ok = 0;
    if (ferror(stream))
        ok = 0;
    if (!ok) {
        r->status = 1;
        r->stop = 1;
    }
    return ok;
}

static int diagnostic(Run *r, const char *message, const char *path)
{
    static const char hex[] = "0123456789abcdef";
    const unsigned char *p;
    char escaped[4];
    int ok;
    /* Workers write each diagnostic whole. */
    sched_lock_out(r, 1);
    ok = emit(r, EMIT_ERR, "fynd: ", 6, 0) &&
         emit(r, EMIT_ERR, message, strlen(message), 0);
    if (path != NULL) {
        if (!emit(r, EMIT_ERR, " '", 2, 0))
            ok = 0;
        for (p = (const unsigned char *)path; *p != 0; ++p) {
            if (*p >= 32 && *p <= 126 && *p != '\\' && *p != '\'') {
                if (!emit(r, EMIT_ERR, (const char *)p, 1, 0))
                    ok = 0;
            } else {
                escaped[0] = '\\';
                escaped[1] = 'x';
                escaped[2] = hex[(unsigned int)*p / 16U];
                escaped[3] = hex[(unsigned int)*p % 16U];
                if (!emit(r, EMIT_ERR, escaped, sizeof escaped, 0))
                    ok = 0;
            }
            if (!ok)
                break;
        }
        if (!emit(r, EMIT_ERR, "'", 1, 0))
            ok = 0;
    }
    if (!emit(r, EMIT_ERR, "\n", 1, 1))
        ok = 0;
    sched_lock_out(r, 0);
    return ok;
}

static void failure(Run *r, const char *message, const char *path, int terminal)
{
    r->status = 1;
    if (terminal)
        r->stop = 1;
    /* Ordered tasks report in walk order, when their output is written. */
    if (r->task != NULL)
        sched_defer_failure(r, message, path, terminal);
    else
        (void)diagnostic(r, message, path);
}

static unsigned char folded(unsigned char c, int fold)
{
    if (fold && c >= 'A' && c <= 'Z')
        return (unsigned char)(c + ('a' - 'A'));
    return c;
}

static int contains(CuStr name, CuStr pattern, int fold)
{
    size_t i;
    size_t j;
    if (pattern.len > name.len)
        return 0;
    for (i = 0; i <= name.len - pattern.len; ++i) {
        for (j = 0; j < pattern.len; ++j)
            if (folded((unsigned char)name.ptr[i + j], fold) !=
                folded((unsigned char)pattern.ptr[j], fold))
                break;
        if (j == pattern.len)
            return 1;
    }
    return 0;
}

static CuStr basename_view(const char *path)
{
    const char *last;
    const char *p;
    last = path;
    for (p = path; *p != 0; ++p)
        if (*p == '/' && p[1] != 0)
            last = p + 1;
    return cu_str_n(last, strlen(last));
}

static int selected(const Query *q, CuStr name, CuDirKind kind)
{
    size_t i;
    size_t dot;
    CuStr ext;
    const CuStr *wanted;
    if (q->kinds != 0 && (q->kinds & (1U << (unsigned int)kind)) == 0)
        return 0;
    if (!contains(name, q->pattern, q->fold))
        return 0;
    if (q->extensions.len == 0)
        return 1;
    dot = 0;
    for (i = 0; i < name.len; ++i)
        if (name.ptr[i] == '.')
            dot = i;
    if (dot == 0 || dot + 1 == name.len)
        return 0;
    ext = cu_str_n(name.ptr + dot + 1, name.len - dot - 1);
    for (i = 0; i < q->extensions.len; ++i) {
        wanted = CU_VEC_AT(&q->extensions, CuStr, i);
        if (ext.len == wanted->len && contains(ext, *wanted, q->fold))
            return 1;
    }
    return 0;
}

static int joined_size(size_t a, size_t b, int slash, size_t *size)
{
    size_t extra;
    extra = slash ? 2U : 1U;
    if (b > (size_t)-1 - extra || a > (size_t)-1 - b - extra)
        return 0;
    *size = a + b + extra;
    return 1;
}

static char *join_path(CuArena *a, const char *parent, CuStr name)
{
    size_t n;
    size_t total;
    int slash;
    char *path;
    n = strlen(parent);
    slash = n != 0 && parent[n - 1] != '/';
    if (!joined_size(n, name.len, slash, &total))
        return NULL;
    path = cu_arena_push_bytes(a, total);
    if (path == NULL)
        return NULL;
    memcpy(path, parent, n);
    if (slash)
        path[n++] = '/';
    memcpy(path + n, name.ptr, name.len);
    path[n + name.len] = 0;
    return path;
}

#include "listing.inc"
#include "ignore.inc"

static int sched_stopped(Sched *);
static int sched_push(Run *, const char *, int, const IgnoreFrame *);
static int sched_record(SchedWorker *, const char *, int);
static int sched_defer_record(Run *, const char *);
static void sched_output_failure(Run *);
static int sched_enter(Run *, const IgnoreFrame *, const CuDir *, const char *,
                       unsigned, const IgnoreFrame **, SchedHold **);
static void sched_drop(Sched *, SchedHold *);
static int sched_keep(Run *, SchedHold *);

static void ignore_failure(Run *r, const IgnoreError *error)
{
    failure(r, listing_resource(error->number) ? "ignore resource failure" :
            "cannot load ignore rules", error->path, listing_resource(error->number));
}

/* Poll only on directory entry and every 64 loop entries, not per leaf/output. */
static int walk_stopped(Run *r)
{
    if (!r->stop && r->sched != NULL && sched_stopped(r->sched))
        r->stop = 1;
    return r->stop;
}

static void visit(Run *r, const char *path, CuStr name, CuDirKind kind, int depth,
                  const IgnoreFrame *parent);

/* Hidden children need -H, or an ignore rule that re-includes them. */
static int skips_hidden(const Run *r, CuStr name)
{
    return !r->query->hidden && name.ptr[0] == '.';
}

/* Select, then descend into or queue, one listed child of path. */
static void visit_child(Run *r, const char *path, int depth, const IgnoreFrame *rules,
                        CuStr name, CuDirKind kind)
{
    CuTemp child;
    IgnoreError error;
    char *joined;
    int verdict;
    int hidden;
    hidden = skips_hidden(r, name);
    if (hidden && r->query->no_ignore)
        return;
    child = cu_temp_begin(r->arena);
    joined = join_path(r->arena, path, name);
    verdict = 0;
    if (joined == NULL)
        failure(r, "allocation failure", path, 1);
    else if (!r->query->no_ignore &&
             !ignore_verdict(r->arena, rules, joined, kind == CU_DIR_DIR, &verdict, &error))
        ignore_failure(r, &error);
    else if (verdict != 1 && (!hidden || verdict == -1)) {
        /* Cutoff and safety-limit directories are visited in place. */
        if (r->sched != NULL && kind == CU_DIR_DIR &&
            depth + 1 != r->query->depth && depth + 1 < FYND_DEPTH_LIMIT)
            (void)sched_push(r, joined, depth + 1, rules);
        else
            visit(r, joined, name, kind, depth + 1, rules);
    }
    cu_temp_end(child);
}

typedef struct SortedEntry {
    CuStr name;
    CuDirKind kind;
} SortedEntry;

static int sorted_compare(const void *a, const void *b)
{
    const SortedEntry *x;
    const SortedEntry *y;
    size_t n;
    int c;
    x = (const SortedEntry *)a;
    y = (const SortedEntry *)b;
    n = x->name.len < y->name.len ? x->name.len : y->name.len;
    c = memcmp(x->name.ptr, y->name.ptr, n);
    if (c != 0)
        return c;
    return x->name.len < y->name.len ? -1 : x->name.len > y->name.len;
}

#ifndef FYND_LISTING_AHEAD
/* Names read before a directory's policy is entered. A listing that ends
 * within them shows which ignore sources it holds, so none is probed. */
#define FYND_LISTING_AHEAD 64
#endif

static void listing_failure(Run *r, const ListingJob *job, const char *path)
{
    failure(r, job->error == ENOMEM ? "allocation failure" : "cannot list directory",
            path, listing_resource(job->error));
}

/* Read up to limit names of job into entries, in the caller's directory
 * scope, noting in *listed the ignore sources among them. Hidden names that
 * no rule can re-include are not kept. Returns the last listing_next result, 1
 * when the limit or a stop comes first, or -2 after reporting an allocation
 * failure. */
static int read_ahead(Run *r, ListingJob *job, const char *path, size_t limit,
                      CuVec *entries, unsigned *listed)
{
    CuDirEntry entry;
    SortedEntry item;
    const char *copy;
    size_t n;
    int next;
    *listed = 0;
    for (n = 0; n < limit; ++n) {
        if (n % 64U == 0 && walk_stopped(r))
            return 1;
        next = listing_next(job, &entry);
        if (next != 1)
            return next;
        *listed |= ignore_listed_name(entry.name);
        if (r->query->no_ignore && skips_hidden(r, entry.name))
            continue;
        copy = cu_str_cstr_copy(r->arena, entry.name);
        if (copy == NULL) {
            failure(r, "allocation failure", path, 1);
            return -2;
        }
        item.name = cu_str_n(copy, entry.name.len);
        item.kind = entry.kind;
        CU_VEC_PUSH(entries, &item);
        if (cu_vec_failed(entries)) {
            failure(r, "allocation failure", path, 1);
            return -2;
        }
    }
    return 1;
}

static void visit(Run *r, const char *path, CuStr name, CuDirKind kind, int depth,
                  const IgnoreFrame *parent)
{
    CuTemp directory;
    ListingJob job;
    const IgnoreFrame *rules;
    IgnoreError error;
    CuDirEntry entry;
    CuVec ahead;
    const SortedEntry *at;
    const CuDir *dir;
    SchedHold *inherited;
    SchedHold *own;
    unsigned tick;
    unsigned listed;
    size_t i;
    int next;
    int ok;
    int seeded;
    if (kind == CU_DIR_DIR && walk_stopped(r))
        return;
    if (selected(r->query, name, kind)) {
        if (r->task != NULL)
            ok = sched_defer_record(r, path);
        else if (r->worker != NULL)
            ok = sched_record(r->worker, path, r->query->delimiter);
        else
            ok = fputs(path, r->out) >= 0 &&
                 fputc(r->query->delimiter, r->out) != EOF && !ferror(r->out);
        if (!ok) {
            /* A deferred record that cannot be kept has stopped its task. */
            if (r->task != NULL)
                return;
            if (r->sched != NULL)
                sched_output_failure(r);
            else
                failure(r, "output failure", NULL, 1);
            return;
        }
    }
    if (kind != CU_DIR_DIR || depth == r->query->depth)
        return;
    if (depth == FYND_DEPTH_LIMIT) {
        failure(r, "recursion safety limit", path, 0);
        return;
    }
    directory = cu_temp_begin(r->arena);
    rules = parent;
    inherited = r->hold;
    own = NULL;
    seeded = 0;
    listing_init(&job, path);
    /* Open failures keep precedence over policy failures, and policy
     * failures over listing failures. No child is selected until all
     * current-directory policy has been acquired. */
    if (!listing_open(&job)) {
        failure(r, job.error == ENOMEM ? "allocation failure" :
                (job.operation == 1 ? "cannot open directory" : "cannot list directory"),
                path, listing_resource(job.error));
        goto done;
    }
    /* --sort reads the whole directory before visiting it. */
    CU_VEC_INIT(&ahead, r->arena, SortedEntry);
    next = read_ahead(r, &job, path, r->query->sort ? (size_t)-1 :
                      r->query->no_ignore ? 0 : FYND_LISTING_AHEAD, &ahead, &listed);
    if (next == -2 || walk_stopped(r))
        goto done;
    if (!r->query->no_ignore) {
        if (parent == NULL) {
            if (!ignore_seed(r->arena, path, &rules, &error)) {
                ignore_failure(r, &error);
                goto done;
            }
            seeded = 1;
        }
        /* An unfinished listing is probed: through the cursor while it is
         * open, by path after a listing failure has closed it. */
        if (next != 0)
            listed = IGNORE_UNLISTED;
        dir = next == 1 ? &job.dir : NULL;
        /* Scheduled frames live in reference-counted holds, freed when the
         * last task borrowing them finishes; serial frames use this scope. */
        if (r->sched != NULL) {
            if (!sched_enter(r, rules, dir, path, listed, &rules, &own))
                goto done;
            if (own != NULL)
                r->hold = own;
        } else if (!ignore_stream_enter(r->arena, rules, dir, path, listed, &rules, &error)) {
            ignore_failure(r, &error);
            goto done;
        }
    }
    if (r->query->sort) {
        /* The cursor closes before any child is visited. */
        listing_release(&job);
        if (next < 0)
            listing_failure(r, &job, path);
        if (r->stop)
            goto done;
        qsort(CU_VEC_DATA(&ahead, SortedEntry), cu_vec_len(&ahead), sizeof(SortedEntry),
              sorted_compare);
    }
    tick = 0;
    for (i = 0; i < cu_vec_len(&ahead) && !r->stop; ++i) {
        if (tick == 0 && walk_stopped(r))
            goto done;
        tick = (tick + 1U) % 64U;
        at = CU_VEC_AT(&ahead, SortedEntry, i);
        visit_child(r, path, depth, rules, at->name, at->kind);
    }
    while (next == 1 && !r->stop) {
        if (tick == 0 && walk_stopped(r))
            break;
        next = listing_next(&job, &entry);
        if (next != 1)
            break;
        tick = (tick + 1U) % 64U;
        visit_child(r, path, depth, rules, entry.name, entry.kind);
        /* Only now may next invalidate entry.name. */
    }
    if (next < 0 && !r->query->sort && !r->stop)
        listing_failure(r, &job, path);
done:
    listing_release(&job);
    if (own != NULL && !sched_keep(r, own))
        sched_drop(r->sched, own);
    r->hold = inherited;
    /* A scheduled root's seed frames stay until the root barrier. */
    if (r->sched == NULL || !seeded)
        cu_temp_end(directory);
}

#include "sched.inc"

static int parse_depth(const char *s, int *out)
{
    int value;
    unsigned int digit;
    if (*s == 0)
        return 0;
    value = 0;
    while (*s != 0) {
        if (*s < '0' || *s > '9')
            return 0;
        digit = (unsigned int)(*s - '0');
        if (value > FYND_DEPTH_LIMIT / 10 || value * 10 + (int)digit > FYND_DEPTH_LIMIT)
            return 0;
        value = value * 10 + (int)digit;
        ++s;
    }
    *out = value;
    return 1;
}

static int parse_threads(const char *s, int *out)
{
    int value;
    if (!parse_depth(s, &value) || value < 1 || value > 32)
        return 0;
    *out = value;
    return 1;
}

static int parse(Query *q, CuArena *arena, int argc, char **argv)
{
    int i;
    const char *s;
    const char *value;
    CuStr view;
    unsigned int kind;
    q->pattern = cu_str_n("", 0);
    CU_VEC_INIT(&q->extensions, arena, CuStr);
    CU_VEC_INIT(&q->roots, arena, CuStr);
    q->kinds = 0;
    q->fold = 0;
    q->hidden = 0;
    q->delimiter = '\n';
    q->depth = -1;
    q->threads = 0; /* automatic */
    q->sort = 0;
    q->no_ignore = 0;
    i = 1;
    while (i < argc) {
        s = argv[i];
        if (strcmp(s, "--") == 0) {
            ++i;
            break;
        }
        if (s[0] != '-' || s[1] == 0)
            break;
        ++i;
        if (strcmp(s, "-i") == 0 || strcmp(s, "--ignore-case") == 0)
            q->fold = 1;
        else if (strcmp(s, "-H") == 0 || strcmp(s, "--hidden") == 0)
            q->hidden = 1;
        else if (strcmp(s, "--no-ignore") == 0)
            q->no_ignore = 1;
        else if (strcmp(s, "--sort") == 0)
            q->sort = 1;
        else if (strcmp(s, "-j") == 0 || strcmp(s, "--threads") == 0) {
            if (i == argc || !parse_threads(argv[i++], &q->threads))
                return 2;
        } else if (s[0] == '-' && s[1] == 'j') {
            if (!parse_threads(s + 2, &q->threads))
                return 2;
        } else if (strcmp(s, "-0") == 0 || strcmp(s, "--print0") == 0)
            q->delimiter = 0;
        else if (strcmp(s, "-t") == 0 || strcmp(s, "--type") == 0) {
            if (i == argc)
                return 2;
            value = argv[i++];
            if (strlen(value) != 1)
                return 2;
            switch (*value) {
            case 'f': kind = CU_DIR_FILE; break;
            case 'd': kind = CU_DIR_DIR; break;
            case 'l': kind = CU_DIR_LINK; break;
            case 'o': kind = CU_DIR_OTHER; break;
            default: return 2;
            }
            q->kinds |= 1U << kind;
        } else if (strcmp(s, "-e") == 0 || strcmp(s, "--extension") == 0) {
            if (i == argc)
                return 2;
            value = argv[i++];
            if (*value == 0 || strchr(value, '.') != NULL || strchr(value, '/') != NULL)
                return 2;
            view = cu_str_n(value, strlen(value));
            CU_VEC_PUSH(&q->extensions, &view);
            if (cu_vec_failed(&q->extensions))
                return 1;
        } else if (strcmp(s, "--max-depth") == 0 || strcmp(s, "-d") == 0) {
            if (i == argc || !parse_depth(argv[i++], &q->depth))
                return 2;
        } else if (s[0] == '-' && s[1] == 'd') {
            if (!parse_depth(s + 2, &q->depth))
                return 2;
        } else
            return 2;
    }
    if (i < argc) {
        q->pattern = cu_str_n(argv[i], strlen(argv[i]));
        ++i;
    }
    if (i == argc) {
        view = cu_str_n(".", 1);
        CU_VEC_PUSH(&q->roots, &view);
    } else {
        for (; i < argc; ++i) {
            if (argv[i][0] == 0)
                return 2;
            view = cu_str_n(argv[i], strlen(argv[i]));
            CU_VEC_PUSH(&q->roots, &view);
            if (cu_vec_failed(&q->roots))
                return 1;
        }
    }
    return cu_vec_failed(&q->roots) ? 1 : 0;
}

/* WASI's thread backend reports ENOSYS: automatic means one thread there,
 * while an explicit -j N above 1 still fails without serial fallback. */
#ifndef FYND_ONLINE_CPUS
#if defined(_SC_NPROCESSORS_ONLN) && !defined(__wasi__)
#define FYND_ONLINE_CPUS() sysconf(_SC_NPROCESSORS_ONLN)
#else
#define FYND_ONLINE_CPUS() 1L
#endif
#endif

/* Default -j: online CPUs, clamped to 1 through 32. */
static int default_threads(void)
{
    long online;
    online = FYND_ONLINE_CPUS();
    if (online < 1)
        return 1;
    return online > 32 ? 32 : (int)online;
}

static int fynd_run(int argc, char **argv, FILE *out, FILE *err, CuArena *arena)
{
    Query q;
    Run r;
    Sched sched;
    int sched_live;
    int threads;
    CuTemp root_scope;
    const CuStr *root;
    char *path;
    size_t n;
    size_t total;
    size_t i;
    struct stat st;
    CuDirKind kind;
    int parsed;
    r.arena = arena;
    r.query = &q;
    r.out = out;
    r.err = err;
    r.status = 0;
    r.stop = 0;
    r.sched = NULL;
    r.worker = NULL;
    r.hold = NULL;
    r.task = NULL;
    sched_live = 0;
    if (argc == 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0 ||
        strcmp(argv[1], "-V") == 0 || strcmp(argv[1], "--version") == 0)) {
        if (ferror(out)) {
            failure(&r, "output failure", NULL, 1);
            return r.status;
        }
        if (strcmp(argv[1], "-V") == 0 || strcmp(argv[1], "--version") == 0)
            parsed = fputs("fynd 0.1.0\n", out);
        else if ((parsed = fputs("Usage: fynd [OPTIONS] [--] [PATTERN [ROOT ...]]\n"
                "  -i, --ignore-case   ASCII case folding\n"
                "  -t, --type KIND     f, d, l, or o (union)\n"
                "  -e, --extension EXT final suffix (union)\n"
                "  -H, --hidden        include discovered hidden names\n"
                "  -0, --print0        NUL-delimited paths\n"
                "  -d, --max-depth N   selection depth 0 through 256\n", out)) >= 0)
            /* Two literals: C90 guarantees only 509 characters per string. */
            parsed = fputs("  --no-ignore         bypass ignore rules\n"
                "  --sort              byte-sorted depth-first order\n"
                "  -j N, --threads N   threads 1 through 32 (default: CPUs)\n"
                "  -h, --help          show help alone\n"
                "  -V, --version       show version alone\n", out);
        if (parsed < 0 || ferror(out))
            failure(&r, "output failure", NULL, 1);
    } else {
        parsed = parse(&q, arena, argc, argv);
        if (parsed != 0) {
            r.status = parsed;
            if (!diagnostic(&r, parsed == 2 ? "invalid invocation" : "allocation failure", NULL))
                r.status = 1;
        } else {
            /* Validate first: a pre-existing output error does not make usage valid. */
            if (ferror(out))
                failure(&r, "output failure", NULL, 1);
            threads = q.threads != 0 ? q.threads : default_threads();
            if (!r.stop && threads > 1) {
                sched_live = 1;
                if (!sched_start(&sched, threads, &q, out, err))
                    failure(&r, "cannot start listing workers", NULL, 1);
                else
                    r.sched = &sched;
            }
            for (i = 0; i < q.roots.len && !r.stop; ++i) {
                root = CU_VEC_AT(&q.roots, CuStr, i);
                root_scope = cu_temp_begin(arena);
                n = root->len;
                while (n > 1 && root->ptr[n - 1] == '/')
                    --n;
                path = joined_size(n, 0, 0, &total) ?
                    cu_arena_push_bytes(arena, total) : NULL;
                if (path == NULL)
                    failure(&r, "allocation failure", NULL, 1);
                else {
                    memcpy(path, root->ptr, n);
                    path[n] = 0;
                    if (lstat(path, &st) != 0)
                        failure(&r, "cannot stat root", path,
                            errno == ENOMEM || errno == EMFILE || errno == ENFILE);
                    else {
                        if (S_ISREG(st.st_mode)) kind = CU_DIR_FILE;
                        else if (S_ISDIR(st.st_mode)) kind = CU_DIR_DIR;
                        else if (S_ISLNK(st.st_mode)) kind = CU_DIR_LINK;
                        else kind = CU_DIR_OTHER;
                        if (r.sched != NULL && kind == CU_DIR_DIR)
                            sched_root(r.sched, &r, path);
                        else
                            visit(&r, path, basename_view(path), kind, 0, NULL);
                    }
                }
                cu_temp_end(root_scope);
            }
        }
    }
    if (sched_live)
        sched_finish(&sched);
    if (fflush(out) == EOF || (r.status != 2 && ferror(out)))
        failure(&r, "output flush failure", NULL, 1);
    return r.status;
}

int fynd_main(int argc, char **argv, FILE *out, FILE *err)
{
    CuArena arena;
    int status;
    cu_arena_init(&arena);
    status = fynd_run(argc, argv, out, err, &arena);
    cu_arena_release(&arena);
    return status;
}
