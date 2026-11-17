/*
 * Copyright 2012 David Cantrell <dcantrell@burdell.org>
 *                Chris Lumens <chris@bangmoney.org>
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

/**
 * @file
 * @brief Payload unpacking with libarchive.
 *
 * libarchive is quick but only reads newc, so a package it cannot
 * read goes to the librpm reader in rpm.c instead.  Both share the
 * disk writer here.
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <archive.h>
#include <archive_entry.h>

#include "tarpm.h"

/* Local prototypes */
static const char *read_error(struct archive *);
static int copy_data(struct archive *, struct archive *, const char *, bool *);
static int extract_entry(struct archive *, struct archive *, struct archive_entry *, bool *);

/*
 * Say why reading the payload failed.  libarchive does not always set
 * an error string, so we fall back to our own.
 */
static const char *
read_error(struct archive *ar)
{
    const char *msg = NULL;

    msg = archive_error_string(ar);

    if (msg == NULL) {
        msg = _("the payload is corrupt or cut short");
    }

    return msg;
}

/*
 * From:
 * https://github.com/libarchive/libarchive/wiki/Examples
 * Reads data from the input archive stream and writes it to the output
 * archive stream.  path names the entry in our messages.  readfailed
 * is set when reading failed, so the caller knows we said why.
 */
static int
copy_data(struct archive *ar, struct archive *aw, const char *path, bool *readfailed)
{
    int r;
    const void *buf = NULL;
    size_t s;
    off_t o;

    while ((r = archive_read_data_block(ar, &buf, &s, &o)) != ARCHIVE_EOF) {
        if (r != ARCHIVE_OK) {
            /* a payload cut short or corrupt lands here */
            warnx_escaped(_("*** unable to read %s from the payload: %s"), str_or_empty(path), read_error(ar));
            *readfailed = true;
            return r;
        }

        r = archive_write_data_block(aw, buf, s, o);

        if (r != ARCHIVE_OK) {
            warnx_escaped("archive_write_data_block: %s", archive_error_string(aw));
            return r;
        }
    }

    return ARCHIVE_OK;
}

/*
 * Drop the setuid and setgid bits and limit device nodes to 0600.  The
 * full modes stay in header.json.
 */
void
restrict_mode(struct archive_entry *entry)
{
    mode_t mode = 0;

    if (entry == NULL) {
        return;
    }

    mode = archive_entry_mode(entry);

    if (S_ISCHR(mode) || S_ISBLK(mode)) {
        mode = (mode & ~(mode_t) 07777) | (mode & 0600);
    } else {
        mode &= ~(mode_t) (S_ISUID | S_ISGID);
    }

    archive_entry_set_mode(entry, mode);
    return;
}

/*
 * Make the libarchive writer both payload readers unpack with.  It
 * restores the times and modes and refuses a path that leaves the
 * current directory or goes through a symlink.
 */
struct archive *
new_disk_writer(void)
{
    int flags = 0;
    struct archive *output = NULL;

    /* attributes to restore */
    flags = ARCHIVE_EXTRACT_TIME;
    flags |= ARCHIVE_EXTRACT_PERM;
    flags |= ARCHIVE_EXTRACT_ACL;
    flags |= ARCHIVE_EXTRACT_FFLAGS;
    flags |= ARCHIVE_EXTRACT_UNLINK;
    flags |= ARCHIVE_EXTRACT_SECURE_NODOTDOT;
    flags |= ARCHIVE_EXTRACT_SECURE_SYMLINKS;
    flags |= ARCHIVE_EXTRACT_SECURE_NOABSOLUTEPATHS;

    output = archive_write_disk_new();
    archive_write_disk_set_options(output, flags);
    archive_write_disk_set_standard_lookup(output);

    return output;
}

/*
 * Print the path of an entry for -v the way tar(1) does.  We take off
 * one leading "./" or "/" so a dotfile keeps its dot.
 */
void
print_entry_path(struct archive_entry *entry)
{
    const char *p = NULL;

    p = archive_entry_pathname(entry);

    if (p == NULL) {
        return;
    }

    if (!strncmp(p, "./", 2)) {
        p += 2;
    } else if (*p == '/') {
        p++;
    }

    /* verbose_detail() escapes the name for us */
    verbose_detail("./%s", p);

    return;
}

/*
 * In a newc payload the last entry of a hard link set carries the
 * data and the earlier ones are empty.  libarchive links the new name
 * to an earlier one and then opens that name to write the data in, so
 * a set rpmbuild wrote read only comes out empty with "Can't create".
 * bsdtar does the same, and no write disk option changes it.  Open the
 * earlier name and add the owner write bit for the write.  We hold on
 * to the descriptor so reprotect_hardlink() can put the mode back
 * without resolving the path a second time.  Returns the descriptor,
 * or -1 when there is nothing to do.
 */
static int
unprotect_hardlink(struct archive_entry *entry, mode_t *mode)
{
    int fd = -1;
    const char *target = NULL;
    struct stat sb;

    *mode = 0;

    if (archive_entry_size(entry) <= 0) {
        return -1;
    }

    target = archive_entry_hardlink(entry);

    if (target == NULL) {
        return -1;
    }

    /* O_NOFOLLOW so a symlink dropped in the tree is not what we chmod */
    fd = open(target, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);

    if (fd == -1) {
        return -1;
    }

    if (fstat(fd, &sb) != 0 || !S_ISREG(sb.st_mode) || (sb.st_mode & S_IWUSR)) {
        close(fd);
        return -1;
    }

    if (fchmod(fd, sb.st_mode | S_IWUSR) != 0) {
        close(fd);
        return -1;
    }

    *mode = sb.st_mode;
    return fd;
}

/* Put back the mode unprotect_hardlink() took off and close its descriptor */
static void
reprotect_hardlink(const int fd, const mode_t mode)
{
    if (fd == -1) {
        return;
    }

    if (fchmod(fd, mode) != 0) {
        warn("fchmod");
    }

    if (close(fd) == -1) {
        warn("close");
    }

    return;
}

/*
 * Extract a single entry from an archive and write it to disk.
 * readfailed is set when the entry data could not be read.
 */
static int
extract_entry(struct archive *input, struct archive *output, struct archive_entry *entry, bool *readfailed)
{
    int r = 0;
    int ret = 0;
    int linkfd = -1;
    mode_t linkmode = 0;

    linkfd = unprotect_hardlink(entry, &linkmode);
    r = archive_write_header(output, entry);

    if (r != ARCHIVE_OK) {
        warnx_escaped("archive_write_header: %s", archive_error_string(output));
        ret = -1;
    } else if (archive_entry_size(entry) > 0) {
        if (copy_data(input, output, archive_entry_pathname(entry), readfailed) != ARCHIVE_OK) {
            ret = -1;
        }
    }

    r = archive_write_finish_entry(output);

    if (r < ARCHIVE_WARN) {
        warnx_escaped("archive_write_finish_entry: %s", archive_error_string(output));
        ret = -1;
    } else if (r != ARCHIVE_OK) {
        warnx_escaped("archive_write_finish_entry: %s", archive_error_string(output));
    }

    reprotect_hardlink(linkfd, linkmode);

    return ret;
}

/*
 * Unpack an archive file to a destination directory.  The directory must
 * exist before calling this function.  Returns 0 if every entry went
 * in and -1 if one failed after the others were written.  Returns
 * UNPACK_UNREADABLE when libarchive could not read a single entry,
 * which is the one case a second try with another reader can fix.
 */
int
unpack_archive(const char *archive, const char *dest, const bool verbose)
{
    int r = 0;
    int ret = 0;
    size_t nread = 0;
    bool readfailed = false;
    char cwd[PATH_MAX + 1];
    struct archive *input = NULL;
    struct archive *output = NULL;
    struct archive_entry *entry = NULL;

    if (archive == NULL || dest == NULL) {
        return -1;
    }

    memset(cwd, '\0', sizeof(cwd));

    /* archive reader */
    input = archive_read_new();
    archive_read_support_filter_all(input);
    archive_read_support_format_all(input);
    r = archive_read_open_filename(input, archive, BUFSIZ);

    if (r != ARCHIVE_OK) {
        archive_read_free(input);
        return UNPACK_UNREADABLE;
    }

    /* change to dest */
    if (getcwd(cwd, PATH_MAX) == NULL) {
        warn("getcwd");
        archive_read_free(input);
        return -1;
    }

    if (chdir(dest) != 0) {
        warn("chdir");
        archive_read_free(input);
        return -1;
    }

    /* handler to write archive members to disk */
    output = new_disk_writer();

    /* extract each archive member */
    while ((r = archive_read_next_header(input, &entry)) != ARCHIVE_EOF) {
        if (r == ARCHIVE_FATAL) {
            /*
             * We cannot recover here, so just take what we could
             * extract.  With no entry read the caller tries librpm,
             * which reports its own failure.  After a failed data
             * read copy_data() has already said why.
             */
            if (nread > 0 && !readfailed) {
                warnx_escaped(_("*** unable to read the next entry in the payload: %s"), read_error(input));
            }

            ret = -1;
            break;
        }

        if (r != ARCHIVE_OK) {
            warnx_escaped("archive_read_next_header: %s", archive_error_string(input));

            /* the entry is not usable, so skip it and try the next one */
            if (r < ARCHIVE_WARN || r == ARCHIVE_RETRY) {
                ret = -1;
                continue;
            }
        }

        nread++;

        if (verbose) {
            print_entry_path(entry);
        }

        restrict_mode(entry);

        if (extract_entry(input, output, entry, &readfailed)) {
            ret = -1;
        }
    }

    archive_read_free(input);

    /* closing sets the directory modes and times libarchive held back */
    r = archive_write_close(output);

    if (r < ARCHIVE_WARN) {
        warnx_escaped("archive_write_close: %s", archive_error_string(output));
        ret = -1;
    } else if (r != ARCHIVE_OK) {
        warnx_escaped("archive_write_close: %s", archive_error_string(output));
    }

    archive_write_free(output);

    /* change back to original directory */
    if (chdir(cwd) != 0) {
        warn("chdir");
        return -1;
    }

    /* nothing came out, so nothing was written for a second try to repeat */
    if (ret != 0 && nread == 0) {
        return UNPACK_UNREADABLE;
    }

    return ret;
}
