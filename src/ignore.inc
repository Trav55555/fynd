/* Private byte parser/matcher adapted from ../cgrep/src/ignore.inc.
 * Provenance: cgrep's glob_component, glob_path and parse_ignore; no stack,
 * walker or dependency code is imported. Malformed classes do not match.
 * All retained allocations belong to the caller's current arena scope. */
#ifndef FYND_IGNORE_INCLUDED
#define FYND_IGNORE_INCLUDED

#ifndef IGNORE_WORK_LIMIT
#define IGNORE_WORK_LIMIT 1000000UL
#endif
#define IGNORE_DEPTH_LIMIT 256U
#ifndef IGNORE_SOURCE_LIMIT
#define IGNORE_SOURCE_LIMIT 16777216UL
#endif

/* Invariant (ignore_parse): a glob containing '/' is always anchored, so an
 * unanchored glob is one component and can match only the final component. */
typedef struct IgnoreRule {
    const char *glob;
    size_t len;
    int negate;
    int dir_only;
    int anchored;
} IgnoreRule;
typedef struct IgnoreFile {
    IgnoreRule *rules;
    size_t count;
} IgnoreFile;
typedef struct IgnoreError {
    int number;
    const char *path;
} IgnoreError;
typedef struct IgnoreWork {
    unsigned long left;
    int failed;
} IgnoreWork;

static int ignore_tick(IgnoreWork *w)
{
    if (w->left == 0) {
        w->failed = 1;
        return 0;
    }
    --w->left;
    return 1;
}

static int ignore_component(const char *g, size_t gl, const char *s,
                            size_t sl, IgnoreWork *w)
{
    size_t gi, si, star_g, star_s, k;
    int negate, hit, have_star;
    unsigned char c, lo, hi;
    gi = si = star_g = star_s = 0;
    have_star = 0;
    while (si < sl) {
        if (!ignore_tick(w)) return 0;
        if (gi < gl && g[gi] == '*') {
            while (gi < gl && g[gi] == '*') {
                if (!ignore_tick(w)) return 0;
                ++gi;
            }
            have_star = 1;
            star_g = gi;
            star_s = si;
            continue;
        }
        if (gi < gl) {
            c = (unsigned char)s[si];
            if (g[gi] == '?') { ++gi; ++si; continue; }
            if (g[gi] == '[') {
                k = gi + 1;
                negate = k < gl && (g[k] == '!' || g[k] == '^');
                if (negate) ++k;
                hit = 0;
                if (k < gl && g[k] == ']') { hit = c == ']'; ++k; }
                while (k < gl && g[k] != ']') {
                    if (!ignore_tick(w)) return 0;
                    lo = (unsigned char)g[k]; hi = lo;
                    if (k + 2 < gl && g[k + 1] == '-' && g[k + 2] != ']') {
                        k += 2; hi = (unsigned char)g[k];
                    }
                    if (lo > hi) return 0;
                    if (c >= lo && c <= hi) hit = 1;
                    ++k;
                }
                if (k < gl && hit != negate) { gi = k + 1; ++si; continue; }
            } else {
                if (g[gi] == '\\' && gi + 1 < gl) ++gi;
                if ((unsigned char)g[gi] == c) { ++gi; ++si; continue; }
            }
        }
        if (!have_star) return 0;
        gi = star_g; si = ++star_s;
    }
    while (gi < gl && g[gi] == '*') {
        if (!ignore_tick(w)) return 0;
        ++gi;
    }
    return gi == gl;
}

static size_t ignore_component_len(const char *p, size_t n, IgnoreWork *w)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        if (!ignore_tick(w)) return 0;
        if (p[i] == '/') break;
    }
    return i;
}

static int ignore_glob(const char *g, size_t gl, const char *p, size_t pl,
                       unsigned depth, IgnoreWork *w)
{
    size_t gc, pc, at;
    for (;;) {
        if (depth > IGNORE_DEPTH_LIMIT) { w->failed = 1; return 0; }
        if (!ignore_tick(w)) return 0;
        if (gl == 0) return pl == 0;
        gc = ignore_component_len(g, gl, w);
        if (w->failed) return 0;
        if (gc == 2 && g[0] == '*' && g[1] == '*') {
            if (gc == gl) return 1;
            if (ignore_glob(g + 3, gl - 3, p, pl, depth + 1, w)) return 1;
            for (at = 0; at < pl && !w->failed; ++at) {
                if (!ignore_tick(w)) return 0;
                if (p[at] == '/' && ignore_glob(g + 3, gl - 3,
                        p + at + 1, pl - at - 1, depth + 1, w)) return 1;
            }
            return 0;
        }
        if (pl == 0) return 0;
        pc = ignore_component_len(p, pl, w);
        if (w->failed || !ignore_component(g, gc, p, pc, w)) return 0;
        if (gc == gl) return pc == pl;
        if (pc == pl) return 0;
        g += gc + 1; gl -= gc + 1;
        p += pc + 1; pl -= pc + 1;
        ++depth;
    }
}

static int ignore_file_verdict(const IgnoreFile *f, const char *rel,
                               size_t len, int is_dir, IgnoreWork *w)
{
    const IgnoreRule *r;
    const char *base;
    size_t i, base_len;
    unsigned char first;
    int matched;
    if (f->count == 0) return 0;
    /* Unanchored rules see only the final component; find it once, lazily. */
    base = NULL; base_len = 0;
    for (i = f->count; i != 0;) {
        if (!ignore_tick(w)) return 0;
        r = &f->rules[--i];
        if (r->dir_only && !is_dir) continue;
        if (!r->anchored && base == NULL) {
            base = rel + len;
            while (base != rel && base[-1] != '/') {
                if (!ignore_tick(w)) return 0;
                --base;
            }
            base_len = len - (size_t)(base - rel);
        }
        /* Only a necessary literal byte: metacharacters (even malformed
         * classes) and empty spans must use the unchanged matcher. The glob
         * splits on '/' BEFORE unescaping, so "\\/" starts with literal '\\'. */
        if (r->len != 0 && r->glob[0] != '*' &&
            r->glob[0] != '?' && r->glob[0] != '[') {
            first = (unsigned char)r->glob[0];
            if (first == '\\' && r->len > 1 && r->glob[1] != '/')
                first = (unsigned char)r->glob[1];
            if (r->anchored) {
                if (len == 0 || first != (unsigned char)rel[0]) continue;
            } else if (base_len == 0 || first != (unsigned char)base[0]) {
                continue;
            }
        }
        if (r->anchored)
            matched = ignore_glob(r->glob, r->len, rel, len, 1, w);
        else
            matched = ignore_glob(r->glob, r->len, base, base_len, 1, w);
        if (w->failed) return 0;
        if (matched) return r->negate ? -1 : 1;
    }
    return 0;
}

/* Exact-length text, never strlen(text). Error outputs may change. */
static int ignore_parse(CuArena *a, CuStr text, IgnoreFile *out, int *number)
{
    CuVec rules;
    IgnoreFile f;
    IgnoreRule r;
    size_t at, end, slashes, k;
    if (text.len > IGNORE_SOURCE_LIMIT) { *number = E2BIG; return 0; }
    if (memchr(text.ptr, 0, text.len) != NULL) { *number = EINVAL; return 0; }
    CU_VEC_INIT(&rules, a, IgnoreRule);
    at = 0;
    while (at < text.len) {
        end = at;
        while (end < text.len && text.ptr[end] != '\n') ++end;
        r.glob = text.ptr + at; r.len = end - at;
        at = end + (end < text.len ? 1U : 0U);
        if (r.len != 0 && r.glob[r.len - 1] == '\r') --r.len;
        while (r.len != 0 && r.glob[r.len - 1] == ' ') {
            slashes = 0; k = r.len - 1;
            while (k != 0 && r.glob[k - 1] == '\\') { ++slashes; --k; }
            if (slashes % 2 != 0) break;
            --r.len;
        }
        if (r.len == 0 || r.glob[0] == '#') continue;
        r.negate = r.glob[0] == '!';
        if (r.negate) { ++r.glob; --r.len; }
        else if (r.len >= 2 && r.glob[0] == '\\' &&
                 (r.glob[1] == '!' || r.glob[1] == '#')) { ++r.glob; --r.len; }
        r.anchored = r.len != 0 && r.glob[0] == '/';
        if (r.anchored) { ++r.glob; --r.len; }
        r.dir_only = r.len != 0 && r.glob[r.len - 1] == '/';
        if (r.dir_only) --r.len;
        if (r.len == 0) continue;
        if (memchr(r.glob, '/', r.len) != NULL) r.anchored = 1;
        CU_VEC_PUSH(&rules, &r);
        if (cu_vec_failed(&rules)) { *number = ENOMEM; return 0; }
    }
    f.count = cu_vec_len(&rules); f.rules = CU_VEC_DATA(&rules, IgnoreRule);
    *out = f;
    return 1;
}

#ifndef FYND_IGNORE_PURE_ONLY
#include <unistd.h>
#include <fcntl.h>
#ifndef IGNORE_STAT
#define IGNORE_STAT lstat
#endif
#ifndef IGNORE_DIR_INFO
#define IGNORE_DIR_INFO cu_dir_info
#endif

enum { IGNORE_PLAIN, IGNORE_GIT, IGNORE_EXCLUDE, IGNORE_CLASSES };
#define IGNORE_REPOSITORY_BIT (1U << IGNORE_CLASSES)
static const char *const ignore_names[] = { ".ignore", ".gitignore", ".git/info/exclude" };
typedef struct IgnoreFrame IgnoreFrame;
struct IgnoreFrame {
    const IgnoreFrame *parent;
    const IgnoreFrame *origin;
    const char *display_root;
    const char *match_root;
    const char *directory;
    IgnoreFile files[IGNORE_CLASSES];
    IgnoreFile global;
    const char *global_path;
    int global_loaded;
    int repository;
};

static int ignore_error(IgnoreError *error, int number, const char *path)
{
    error->number = number;
    error->path = path;
    return 0;
}

static char *ignore_copy(CuArena *a, const char *p, size_t n)
{
    char *s;
    if (n == (size_t)-1) return NULL;
    s = cu_arena_push_bytes(a, n + 1);
    if (s != NULL) { memcpy(s, p, n); s[n] = 0; }
    return s;
}

/* Lexical absolute normalization; not a symlink resolver or sandbox. */
static char *ignore_absolute(CuArena *a, const char *path, IgnoreError *error)
{
    char *cwd, *joined, *result;
    size_t cap, n, i, start, used;
    joined = NULL;
    if (path[0] != '/') {
        cap = 128;
        for (;;) {
            cwd = cu_arena_push_bytes(a, cap);
            if (cwd == NULL) { ignore_error(error, ENOMEM, path); return NULL; }
            if (getcwd(cwd, cap) != NULL) break;
            if (errno != ERANGE) { ignore_error(error, errno, path); return NULL; }
            if (cap > (size_t)-1 / 2) { ignore_error(error, E2BIG, path); return NULL; }
            cap *= 2;
        }
        joined = join_path(a, cwd, cu_str_cstr(path));
        if (joined == NULL) { ignore_error(error, ENOMEM, path); return NULL; }
        path = joined;
    }
    n = strlen(path);
    result = ignore_copy(a, path, n);
    if (result == NULL) { ignore_error(error, ENOMEM, path); return NULL; }
    used = 1; i = 0;
    while (i < n) {
        while (i < n && path[i] == '/') ++i;
        start = i;
        while (i < n && path[i] != '/') ++i;
        if (i == start || (i - start == 1 && path[start] == '.')) continue;
        if (i - start == 2 && path[start] == '.' && path[start + 1] == '.') {
            while (used > 1 && result[used - 1] != '/') --used;
            if (used > 1) --used;
        } else {
            if (used > 1) result[used++] = '/';
            memcpy(result + used, path + start, i - start);
            used += i - start;
        }
    }
    result[used] = 0;
    return result;
}

/* Follow source symlinks only to regular files. O_NONBLOCK plus fstat also
 * protects the stat/open replacement window from FIFO blocking. */
static int ignore_read(CuArena *a, const char *path, IgnoreFile *out,
                       IgnoreError *error)
{
    struct stat st;
    FILE *stream;
    int fd, saved, number;
    CuBuilder b;
    CuStr text;
    char bytes[1024];
    size_t n, total;
    IgnoreFile f;
    f.rules = NULL; f.count = 0;
    if (IGNORE_STAT(path, &st) != 0) {
        saved = errno;
        if (saved == ENOENT || saved == ENOTDIR) { *out = f; return 1; }
        return ignore_error(error, saved, path);
    }
    /* Marker probes are nonfollowing; policy sources still follow symlinks
     * to regular files, including the existing missing-target behavior. */
    if (S_ISLNK(st.st_mode) && stat(path, &st) != 0) {
        saved = errno;
        if (saved == ENOENT || saved == ENOTDIR) { *out = f; return 1; }
        return ignore_error(error, saved, path);
    }
    if (!S_ISREG(st.st_mode)) return ignore_error(error, EINVAL, path);
    fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        saved = errno;
        if (saved == ENOENT || saved == ENOTDIR) { *out = f; return 1; }
        return ignore_error(error, saved, path);
    }
    if (fstat(fd, &st) != 0) {
        saved = errno;
        (void)close(fd);
        return ignore_error(error, saved, path);
    }
    if (!S_ISREG(st.st_mode)) {
        (void)close(fd);
        return ignore_error(error, EINVAL, path);
    }
    stream = fdopen(fd, "rb");
    if (stream == NULL) { saved = errno; (void)close(fd); return ignore_error(error, saved, path); }
    cu_sb_init(&b, a);
    total = 0; saved = 0;
    errno = 0;
    while ((n = fread(bytes, 1, sizeof bytes, stream)) != 0) {
        if (n > IGNORE_SOURCE_LIMIT - total) { saved = E2BIG; break; }
        if (ferror(stream)) { saved = errno != 0 ? errno : EIO; break; }
        total += n;
        cu_sb_str(&b, cu_str_n(bytes, n));
        if (b.failed) { saved = ENOMEM; break; }
        errno = 0;
    }
    if (saved == 0 && ferror(stream)) saved = errno != 0 ? errno : EIO;
    (void)fclose(stream);
    if (saved != 0) return ignore_error(error, saved, path);
    if (!cu_sb_finish(&b, &text)) return ignore_error(error, ENOMEM, path);
    if (!ignore_parse(a, text, &f, &number)) return ignore_error(error, number, path);
    *out = f;
    return 1;
}

static int ignore_frame(CuArena *a, const IgnoreFrame *parent,
                        const char *directory, unsigned listed,
                        const IgnoreFrame **out, IgnoreError *error)
{
    IgnoreFrame *f;
    char *path;
    int k, has_repository;
    const IgnoreFrame *ancestor;
    f = CU_PUSH(a, IgnoreFrame, 1);
    if (f == NULL) return ignore_error(error, ENOMEM, directory);
    f->parent = parent; f->origin = parent->origin;
    f->display_root = parent->display_root; f->match_root = parent->match_root;
    f->global = parent->global;
    f->global_path = parent->global_path;
    f->global_loaded = parent->global_loaded;
    f->directory = directory;
    f->repository = (listed & IGNORE_REPOSITORY_BIT) != 0;
    has_repository = f->repository;
    for (ancestor = parent; ancestor != NULL; ancestor = ancestor->parent)
        if (ancestor->repository) { has_repository = 1; break; }
    if (has_repository && !f->global_loaded) {
        if (f->global_path != NULL && !ignore_read(a, f->global_path, &f->global, error)) return 0;
        f->global_loaded = 1;
    }
    for (k = 0; k < IGNORE_CLASSES; ++k) {
        f->files[k].rules = NULL; f->files[k].count = 0;
        if (k != IGNORE_PLAIN && !has_repository) continue;
        if (k == IGNORE_EXCLUDE ? f->repository : (listed & (1U << k)) != 0) {
            path = join_path(a, directory, cu_str_cstr(ignore_names[k]));
            if (path == NULL) return ignore_error(error, ENOMEM, directory);
            if (!ignore_read(a, path, &f->files[k], error)) return 0;
        }
    }
    *out = f;
    return 1;
}

static int ignore_map(CuArena *a, const IgnoreFrame *f, const char *path,
                      char **out, IgnoreError *error)
{
    size_t n;
    char *p;
    n = strlen(f->display_root);
    if (strncmp(path, f->display_root, n) != 0 ||
        (path[n] != 0 && n != 0 && f->display_root[n - 1] != '/' && path[n] != '/'))
        return ignore_error(error, EINVAL, path);
    path += n;
    while (*path == '/') ++path;
    p = join_path(a, f->match_root, cu_str_cstr(path));
    if (p == NULL) return ignore_error(error, ENOMEM, f->display_root);
    *out = p;
    return 1;
}

static int ignore_enter(CuArena *a, const IgnoreFrame *parent,
                        const char *directory, unsigned listed,
                        const IgnoreFrame **out, IgnoreError *error)
{
    char *mapped;
    if (!ignore_map(a, parent, directory, &mapped, error)) return 0;
    return ignore_frame(a, parent, mapped, listed, out, error);
}

/* Nonfollowing probe of a name in the open listing dir. Returns 1 with
 * *present set, or 0 after reporting an error at the name's full path. */
static int ignore_probe(CuArena *a, const CuDir *dir, const char *directory,
                        const char *name, int *present, IgnoreError *error)
{
    CuFileInfo info;
    char *path;
    int saved;
    switch (IGNORE_DIR_INFO(dir, name, CU_PATH_NOFOLLOW, &info)) {
    case CU_INFO_OK: *present = 1; return 1;
    case CU_INFO_MISSING: *present = 0; return 1;
    default: break;
    }
    saved = errno;
    if (saved == ENOTDIR) { *present = 0; return 1; }
    path = join_path(a, directory, cu_str_cstr(name));
    if (path == NULL) return ignore_error(error, ENOMEM, directory);
    return ignore_error(error, saved, path);
}

/* Acquire current policy before the cursor yields ANY child. dir is the open
 * listing of directory. Presence of a repository marker includes every kind,
 * even a dangling symbolic link. Only sources that exist are read. */
static int ignore_stream_enter(CuArena *a, const IgnoreFrame *parent,
                               const CuDir *dir, const char *directory,
                               const IgnoreFrame **out, IgnoreError *error)
{
    const IgnoreFrame *f, *ancestor;
    unsigned listed;
    int k, present, repository;
    listed = 0;
    if (!ignore_probe(a, dir, directory, ".git", &present, error)) return 0;
    if (present) listed |= IGNORE_REPOSITORY_BIT;
    repository = present;
    for (ancestor = parent; !repository && ancestor != NULL; ancestor = ancestor->parent)
        repository = ancestor->repository;
    /* ignore_frame reads .gitignore only inside a repository. */
    for (k = 0; k <= IGNORE_GIT; ++k) {
        if (k == IGNORE_GIT && !repository) continue;
        if (!ignore_probe(a, dir, directory, ignore_names[k], &present, error)) return 0;
        if (present) listed |= 1U << k;
    }
    if (!ignore_enter(a, parent, directory, listed, &f, error)) return 0;
    /* A frame with no rules and no repository marker cannot decide or scope
     * any verdict: reuse the parent. Its storage stays in the caller's scope. */
    for (k = 0; k < IGNORE_CLASSES; ++k)
        if (f->files[k].count != 0) break;
    if (k == IGNORE_CLASSES && !f->repository &&
        f->global_loaded == parent->global_loaded)
        f = parent;
    *out = f;
    return 1;
}

static int ignore_seed(CuArena *a, const char *root,
                       const IgnoreFrame **out, IgnoreError *error)
{
    IgnoreFrame *origin;
    const IgnoreFrame *parent;
    char *absolute, *directory, *probe, *global;
    const char *env;
    size_t i, n, nearest;
    unsigned listed;
    int k, saved;
    struct stat st;
#if defined(__wasi__) || defined(__cerb__)
    /* These capability-limited targets do not provide POSIX realpath. */
    absolute = ignore_absolute(a, root, error);
#else
    /* Resolve before collapsing '..': a symlink changes its physical parent.
     * realpath owns this temporary allocation; retained paths belong to a. */
    probe = realpath(root, NULL);
    if (probe == NULL) return ignore_error(error, errno, root);
    absolute = ignore_absolute(a, probe, error);
    free(probe);
    if (absolute == NULL) error->path = root;
#endif
    if (absolute == NULL) return 0;
    origin = CU_PUSH(a, IgnoreFrame, 1);
    if (origin == NULL) return ignore_error(error, ENOMEM, root);
    origin->parent = NULL; origin->origin = origin;
    origin->match_root = absolute;
    origin->display_root = ignore_copy(a, root, strlen(root));
    if (origin->display_root == NULL) return ignore_error(error, ENOMEM, root);
    origin->directory = absolute; origin->repository = 0;
    origin->global.rules = NULL; origin->global.count = 0;
    origin->global_path = NULL; origin->global_loaded = 0;
    for (k = 0; k < IGNORE_CLASSES; ++k) {
        origin->files[k].rules = NULL; origin->files[k].count = 0;
    }
    env = getenv("XDG_CONFIG_HOME");
    global = NULL;
    if (env != NULL && *env != 0) global = join_path(a, env, cu_str_cstr("git/ignore"));
    else {
        env = getenv("HOME");
        if (env != NULL && *env != 0) global = join_path(a, env, cu_str_cstr(".config/git/ignore"));
    }
    if (env != NULL && *env != 0) {
        if (global == NULL) return ignore_error(error, ENOMEM, root);
        origin->global_path = global;
    }
    parent = origin;
    n = strlen(absolute);
    /* Discover the nearest proper-ancestor repository before reading any
     * Git source: shadowed outer excludes must not cause spurious errors. */
    nearest = 0;
    for (i = 1; i < n; ++i) {
        if (i != 1 && absolute[i] != '/') continue;
        directory = ignore_copy(a, absolute, i);
        if (directory == NULL) return ignore_error(error, ENOMEM, root);
        probe = join_path(a, directory, cu_str_cstr(".git"));
        if (probe == NULL) return ignore_error(error, ENOMEM, directory);
        if (IGNORE_STAT(probe, &st) == 0) nearest = i;
        else {
            saved = errno;
#if defined(__wasi__) && defined(ENOTCAPABLE)
            if (saved == ENOTCAPABLE) continue;
#endif
            if (saved != ENOENT && saved != ENOTDIR) return ignore_error(error, saved, probe);
        }
    }
    /* A root marker suppresses outer Git sources, but is not published as
     * root rules/context until current policy is entered. */
    probe = join_path(a, absolute, cu_str_cstr(".git"));
    if (probe == NULL) return ignore_error(error, ENOMEM, root);
    if (IGNORE_STAT(probe, &st) == 0) nearest = n;
    else {
        saved = errno;
        if (saved != ENOENT && saved != ENOTDIR) return ignore_error(error, saved, probe);
    }
    /* Proper ancestors only; '/' has none. */
    for (i = 1; i < n; ++i) {
        if (i != 1 && absolute[i] != '/') continue;
        directory = ignore_copy(a, absolute, i);
        if (directory == NULL) return ignore_error(error, ENOMEM, root);
        listed = i == nearest ? IGNORE_REPOSITORY_BIT : 0;
        for (k = 0; k <= IGNORE_GIT; ++k) {
            if (k == IGNORE_GIT && (nearest == 0 || i < nearest)) continue;
            probe = join_path(a, directory, cu_str_cstr(ignore_names[k]));
            if (probe == NULL) return ignore_error(error, ENOMEM, directory);
            if (IGNORE_STAT(probe, &st) == 0) listed |= 1U << k;
            else {
                saved = errno;
#ifdef __wasi__
                /* Preopened capabilities can deny proper ancestors. Only this
                 * target treats ENOTCAPABLE as an ancestor boundary. */
#ifdef ENOTCAPABLE
                if (saved == ENOTCAPABLE) continue;
#endif
#endif
                if (saved != ENOENT && saved != ENOTDIR) return ignore_error(error, saved, probe);
            }
        }
        if (!ignore_frame(a, parent, directory, listed, &parent, error)) return 0;
    }
    *out = parent;
    return 1;
}

static const char *ignore_relative(const char *path, const char *directory)
{
    size_t n;
    n = strlen(directory);
    if (strncmp(path, directory, n) != 0) return NULL;
    if (n != 0 && directory[n - 1] != '/' && path[n] != '/') return NULL;
    path += n;
    if (*path == '/') ++path;
    return path;
}

static int ignore_verdict(CuArena *a, const IgnoreFrame *frame,
                          const char *path, int is_dir, int *verdict,
                          IgnoreError *error)
{
    CuTemp scratch;
    const IgnoreFrame *f, *repo;
    const char *rel;
    char *mapped;
    IgnoreWork work;
    int values[IGNORE_CLASSES], k, v, ok;
    scratch = cu_temp_begin(a);
    ok = ignore_map(a, frame, path, &mapped, error);
    if (!ok) {
        /* map errors refer to caller paths or retained origin, not scratch. */
        cu_temp_end(scratch);
        return 0;
    }
    work.left = IGNORE_WORK_LIMIT; work.failed = 0;
    repo = NULL;
    for (f = frame; f != NULL; f = f->parent)
        if (f->repository) { repo = f; break; }
    for (k = 0; k < IGNORE_CLASSES; ++k) values[k] = 0;
    for (f = frame; f != NULL; f = f->parent) {
        /* Most frames load no rules; empty files never decide or charge work. */
        rel = f->files[IGNORE_PLAIN].count == 0 && f->files[IGNORE_GIT].count == 0 &&
              f->files[IGNORE_EXCLUDE].count == 0 ? NULL :
              ignore_relative(mapped, f->directory);
        if (rel != NULL) {
            for (k = 0; k < IGNORE_CLASSES; ++k) {
                if (values[k] == 0 && (k == IGNORE_PLAIN || repo != NULL))
                    values[k] = ignore_file_verdict(&f->files[k], rel, strlen(rel), is_dir, &work);
            }
        }
        if (f == repo) repo = NULL;
        if (work.failed) break;
    }
    v = 0;
    for (k = 0; k < IGNORE_CLASSES; ++k) if (values[k] != 0) { v = values[k]; break; }
    if (v == 0 && !work.failed) {
        for (f = frame; f != NULL; f = f->parent) if (f->repository) break;
        if (f != NULL) {
            rel = ignore_relative(mapped, f->directory);
            if (rel != NULL) v = ignore_file_verdict(&frame->global, rel, strlen(rel), is_dir, &work);
        }
    }
    cu_temp_end(scratch);
    if (work.failed) return ignore_error(error, E2BIG, path);
    *verdict = v;
    return 1;
}
#endif
#endif
