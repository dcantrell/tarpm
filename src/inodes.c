/* Tail queue for hard link inodes. */

/*
 * Copyright Dave Cantrell <dcantrell@burdell.org>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <ftw.h>
#include <limits.h>
#include <err.h>

#include "tarpm.h"

/* Local data structures */
typedef struct _inode_entry_t {
    ino_t inode;
    char *path;
    TAILQ_ENTRY(_inode_entry_t) items;
} inode_entry_t;

typedef TAILQ_HEAD(inodelist_s, _inode_entry_t) inodelist_t;

static inodelist_t *inodes = NULL;

/* Local prototypes */
static void init_inodes(void);
static int add_inode(const char *, const struct stat *, int, struct FTW *);

/*
 * Initialize the inode tailq.
 * This is only called by add_inode() and it only does something if the data
 * structure is NULL.
 */
static void
init_inodes(void)
{
    if (inodes == NULL) {
        inodes = xcalloc(1, sizeof(*inodes));
        TAILQ_INIT(inodes);
        return;
    }

    return;
}

/*
 * Called by nftw() in add_all_inodes().
 * Add a new inode to the hash table but only if it doesn't already exist.
 * The way our inodes hash table works is that only one entry is allowed
 * for any given inode.  That means the first filename we come across for
 * an inode will be how we refer to it.  All subsequent filenames for that
 * inode will be considered hard links.
 */
static int
add_inode(const char *fpath, const struct stat *sb, __attribute__((unused)) int tflag, __attribute__((unused)) struct FTW *ftwbuf)
{
    inode_entry_t *newinode = NULL;

    if (!strcmp(fpath, ".") || !S_ISREG(sb->st_mode)) {
        return 0;
    }

    if (sb->st_ino == 0) {
        return -1;
    }

    init_inodes();

    if (lookup_inode(sb->st_ino) != NULL) {
        return 0;
    }

    newinode = xcalloc(1, sizeof(*newinode));
    newinode->inode = sb->st_ino;

    /* the +2 here is to advance past the "./" */
    newinode->path = strdup(fpath + 2);

    TAILQ_INSERT_TAIL(inodes, newinode, items);

    return 0;
}

/*
 * Look up the specified inode and return its filename, or NULL.
 * The search key is the inode and the returned value is a pointer to a
 * string containing the filename for that inode.
 */
char *
lookup_inode(const ino_t inode)
{
    inode_entry_t *entry = NULL;

    if (inode == 0 || inodes == NULL) {
        return NULL;
    }

    TAILQ_FOREACH(entry, inodes, items) {
        if (entry->inode == inode) {
            return entry->path;
        }
    }

    return NULL;
}

/*
 * Scan a tree and add all unique inodes.
 */
int
add_inodes(const char *path)
{
    char cwd[PATH_MAX + 1];

    if (path == NULL) {
        return -1;
    }

    memset(cwd, '\0', sizeof(cwd));

    if (getcwd(cwd, PATH_MAX) == NULL) {
        err(EXIT_FAILURE, "getcwd");
    }

    if (chdir(path) != 0) {
        warn("chdir");
        return -1;
    }

    if (nftw(".", add_inode, 25, FTW_MOUNT | FTW_PHYS) == -1) {
        warn("nftw");
        return -1;
    }

    if (chdir(cwd) != 0) {
        warn("chdir");
        return -1;
    }

    return 0;
}

/*
 * Clear the hash table and free memory.
 * This function is needed since inodes is static.
 */
void
free_inodes(void)
{
    inode_entry_t *entry = NULL;

    if (inodes == NULL) {
        return;
    }

    while (!TAILQ_EMPTY(inodes)) {
        entry = TAILQ_FIRST(inodes);
        TAILQ_REMOVE(inodes, entry, items);
        free(entry->path);
        entry->path = NULL;
        free(entry);
    }

    free(inodes);
    inodes = NULL;

    return;
}
