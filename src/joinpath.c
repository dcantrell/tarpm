/*
 * Given a list of path components as separate strings, join them in to a
 * correct (but unverified) Unix path.  Extra slashes are removed.  Spaces
 * and other special characters are not escaped.  This function allocates
 * memory for the returned value.  The caller must free this memory when
 * done.
 *
 * Usage: path = join(a, b, c, ..., NULL);
 * (where a, b, and c are char *)
 */

/* {{{ Apache License version 2.0
 */
/*
 * Copyright 2018 David Cantrell <david.l.cantrell@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/* }}} */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#ifdef _HAVE_OPENAT2
#include <sys/syscall.h>
#include <linux/openat2.h>
#endif

#include "tarpm.h"

char *joinpath(const char *path, ...)
{
    va_list ap;
    int i = 0;
    bool needsep = false;
    char *element = NULL;
    char *tail = NULL;
    char *built = NULL;
    char *tmp = NULL;
    char *near = NULL;
    char *far = NULL;
    str_list_t *list = NULL;
    str_entry_t *entry = NULL;
    size_t s = 0;

    if (path == NULL) {
        return NULL;
    }

    /* initial buffer length */
    s = strlen(path) + 2;

    /* collect the strings we were given */
    va_start(ap, path);

    while ((element = va_arg(ap, char *)) != NULL) {
        list = list_add(list, element);

        /*
         * add 2 here to account for possible leading and
         * trailing slash later
         */
        s += strlen(element) + 2;
    }

    va_end(ap);

    /* Allocate a large buffer to use for building the path. */
    tail = built = xalloc(s);

    /* Make sure the full path starts with a slash. */
    if (*path == '/') {
        /* this for loop trims multiple leading slashes down to just one */
        while (*(path + 1) == '/') path++;
    } else {
        /* ensure joined path begins with a slash */
        built[0] = '/';
        tail++;
    }

    /* begin our joined path */
    tail = stpncpy(tail, path, s - (tail - built));

    /* the remaining elements come in this way */
    if (list != NULL) {
        TAILQ_FOREACH(entry, list, items) {
            /* do not disrupt the main pointer (need to free later) */
            element = entry->str;

            /* for the trailing NUL */
            needsep = false;

            /*
             * This loop trims multiple leading slashes down to just
             * one.  If 'i' is 1, it will preserve 1 leading slash.
             * Actually, set this to the number of slashes to preserve.
             */
            if (*element == '/' && *tail != '/') {
                i = 1;
            } else {
                i = 0;
            }

            while (*(element + i) == '/') {
                element++;
            }

            /* make sure we have at least one slash in case there are none */
            if (*element != '/' && *tail != '/') {
                needsep = true;
            }

            /* perform the concatenations */
            if (needsep) {
                tail = stpncpy(tail, "/", s - (tail - built));
            }

            tail = stpncpy(tail, element, s - (tail - built));
        }
    }

    /* it's possible there are repeating slashes, eliminate them */
    near = built;
    far = built;

    while ((*near = *far)) {
        if (*near == '/') {
            while (*far == '/') {
                far++;
            }

            near++;
        } else {
            near++;
            far++;
        }
    }

    /* shrink memory allocation */
    tmp = xrealloc(built, strlen(built) + 1);
    built = tmp;

    /* clean up */
    list_free(list, free);

    return built;
}

/*
 * Returns true if path names something below base without leaving
 * it.  One leading slash is allowed and the path is still read as
 * relative to base.  Every component has to be non-empty and may not
 * be "." or "..".  No component that exists under base may be a
 * symlink, except the last one when allow_last_link is true.  Once a
 * component is missing there is nothing left to follow, so we only
 * check the names of the rest.
 */
bool path_beneath(const char *base, const char *path, const bool allow_last_link)
{
    size_t len = 0;
    bool last = false;
    bool exists = true;
    const char *start = NULL;
    const char *end = NULL;
    char *prefix = NULL;
    char *sub = NULL;
    struct stat sb;

    if (base == NULL || path == NULL) {
        return false;
    }

    if (path[0] == '/') {
        path++;
    }

    if (path[0] == '\0') {
        return false;
    }

    start = path;

    while (!last) {
        end = strchr(start, '/');

        if (end == NULL) {
            end = start + strlen(start);
            last = true;
        }

        len = end - start;

        if (len == 0 || (len == 1 && start[0] == '.') || (len == 2 && !strncmp(start, "..", 2))) {
            return false;
        }

        if (exists) {
            prefix = xalloc(end - path + 1);
            memcpy(prefix, path, end - path);
            prefix[end - path] = '\0';
            sub = joinpath(base, prefix, NULL);
            free(prefix);

            if (lstat(sub, &sb) == -1) {
                exists = false;
            } else if (S_ISLNK(sb.st_mode) && (!last || !allow_last_link)) {
                free(sub);
                return false;
            }

            free(sub);
        }

        start = end + 1;
    }

    return true;
}

/*
 * Return path past one leading slash if every component is a plain
 * name, NULL if one is empty, "." or "..".
 */
static const char *
plain_relative(const char *path)
{
    size_t len = 0;
    const char *start = NULL;
    const char *end = NULL;

    if (path[0] == '/') {
        path++;
    }

    start = path;

    while (true) {
        end = strchr(start, '/');

        if (end == NULL) {
            end = start + strlen(start);
        }

        len = end - start;

        if (len == 0 || (len == 1 && start[0] == '.') || (len == 2 && !strncmp(start, "..", 2))) {
            return NULL;
        }

        if (*end == '\0') {
            break;
        }

        start = end + 1;
    }

    return path;
}

/*
 * Open path below base for reading one component at a time with
 * O_NOFOLLOW, so no symlink in it is followed.  This is the fallback
 * for open_beneath() where openat2() is missing.  Returns the
 * descriptor or -1 with errno set.  A plain name check that fails
 * sets EINVAL and a symlink sets ELOOP.
 */
int
open_beneath_walk(const char *base, const char *path)
{
    int fd = -1;
    int next = -1;
    int saved = 0;
    char *copy = NULL;
    char *name = NULL;
    char *slash = NULL;
    const char *relative = NULL;
    struct stat sb;

    if (base == NULL || path == NULL || (relative = plain_relative(path)) == NULL) {
        errno = EINVAL;
        return -1;
    }

    if ((fd = open(base, O_PATH | O_DIRECTORY | O_CLOEXEC)) == -1) {
        return -1;
    }

    copy = xstrdup(relative);
    name = copy;

    /* every directory on the way, O_PATH so search permission is enough */
    while ((slash = strchr(name, '/')) != NULL) {
        *slash = '\0';
        next = openat(fd, name, O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        saved = errno;

        /* O_PATH opens a symlink itself, so O_DIRECTORY gives ENOTDIR */
        if (next == -1 && saved == ENOTDIR && fstatat(fd, name, &sb, AT_SYMLINK_NOFOLLOW) == 0 && S_ISLNK(sb.st_mode)) {
            saved = ELOOP;
        }

        close(fd);
        errno = saved;

        if (next == -1) {
            free(copy);
            return -1;
        }

        fd = next;
        name = slash + 1;
    }

    next = openat(fd, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    saved = errno;
    close(fd);
    free(copy);
    errno = saved;

    return next;
}

/*
 * Open path below base for reading without following a symlink
 * anywhere in it.  The kernel checks the whole path with openat2()
 * and RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS, so a component swapped
 * for a symlink after path_beneath() looked at it is not followed.
 * Without openat2() we walk the components ourselves.  Returns the
 * descriptor or -1 with errno set.  ELOOP, EXDEV and EINVAL mean the
 * path leaves base or goes through a symlink.
 */
int
open_beneath(const char *base, const char *path)
{
#ifdef _HAVE_OPENAT2
    int fd = -1;
    int dirfd = -1;
    int saved = 0;
    const char *relative = NULL;
    struct open_how how;

    if (base == NULL || path == NULL || (relative = plain_relative(path)) == NULL) {
        errno = EINVAL;
        return -1;
    }

    if ((dirfd = open(base, O_PATH | O_DIRECTORY | O_CLOEXEC)) == -1) {
        return -1;
    }

    memset(&how, 0, sizeof(how));
    how.flags = O_RDONLY | O_NOFOLLOW | O_CLOEXEC;
    how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS;
    fd = (int) syscall(SYS_openat2, dirfd, relative, &how, sizeof(how));
    saved = errno;
    close(dirfd);
    errno = saved;

    /* an older kernel or a seccomp filter may not let us have it */
    if (fd != -1 || (errno != ENOSYS && errno != EPERM)) {
        return fd;
    }
#endif

    return open_beneath_walk(base, path);
}
