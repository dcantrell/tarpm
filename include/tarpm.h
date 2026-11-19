/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

/**
 * @file
 * @brief The tarpm internal API.
 *
 * Every function tarpm shares between source files is declared here.
 * The groups below follow the file in src/ that implements them.
 */

#ifndef _TARPM_TARPM_H
#define _TARPM_TARPM_H

#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <rpm/header.h>
#include <json.h>

#include "constants.h"
#include "i18n.h"
#include "helpers.h"
#include "epochtime.h"
#include "types.h"

/** @name Path canonicalization (abspath.c) */
/** @{ */

/**
 * @brief Canonicalize a path with relative references.
 *
 * Does not rely on filesystem existence, so a path that is not there
 * still resolves.  A relative path is taken from the current
 * directory.
 *
 * @param path Path to canonicalize.
 * @return Newly allocated absolute path, or NULL on failure.  Caller
 *         must free.
 */
char *abspath(const char *path);

/**
 * @brief Return the directory part of a path.
 *
 * dirname(3) may modify what we hand it, so it gets a copy.
 *
 * @param path Path to take the directory part of.
 * @return Newly allocated directory name, or NULL if path is NULL.
 *         Caller must free.
 */
char *xdirname(const char *path);

/** @} */

/** @name librpm helpers (rpm.c) */
/** @{ */

/**
 * @brief Unpack the payload of an RPM in to dest with librpm.
 *
 * This is the reader for the stripped cpio archive libarchive cannot
 * read, which is every v6 format payload and a v4 format one with
 * Longfilesizes.  The entries go straight to the same libarchive disk
 * writer unpack_archive() uses, so nothing is written anywhere else.
 * An entry that fails is skipped the way unpack_archive() skips one.
 *
 * @param rpm Path to the package to read.
 * @param dest Directory to unpack in to, which must already exist.
 * @param verbose Print each entry as it lands on disk.
 * @return 0 if every entry went in, -1 otherwise.
 */
int unpack_rpm_payload(const char *rpm, const char *dest, const bool verbose);

/**
 * @brief Return an RPM header struct for the given package filename.
 *
 * @param pkg Path to the package.
 * @return The librpm Header, or NULL if the package cannot be read.
 */
Header get_header(const char *pkg);

/**
 * @brief Get the named RPM header tag as a string.
 *
 * @param h Header to read.
 * @param tag Tag to look up.
 * @return Newly allocated string, or NULL when the tag is not there.
 *         Caller must free.
 */
char *get_rpmtag_str(Header h, rpmTagVal tag);

/**
 * @brief Return RPMTAG_ARCH, or "src" for a source package.
 *
 * @param h Header to read.
 * @return The architecture name.  Do not free it.
 */
const char *get_header_arch(Header h);

/**
 * @brief Get the RPMTAG_NEVR extension tag.
 *
 * @param h Header to read.
 * @return Newly allocated NEVR string.  Caller must free.
 */
char *get_nevr(Header h);

/**
 * @brief Get the equivalent of the RPMTAG_NEVRA extension tag.
 *
 * We do not use RPMTAG_NEVRA directly because on a source package the
 * "A" part was written at rpmbuild time and names the architecture
 * rpmbuild ran on rather than "src".
 *
 * @param h Header to read.
 * @return Newly allocated NEVRA string.  Caller must free.
 */
char *get_nevra(Header h);

/** @} */

/** @name String helpers (strfuncs.c) */
/** @{ */

/**
 * @brief Append one or more strings to dest and return the result.
 *
 * Every item to append must be a char * and the list ends with NULL.
 * Memory is allocated or reallocated and the first argument is
 * modified.
 *
 * @param dest String to append to, which may be NULL to start one.
 * @param ... Strings to append, terminated by NULL.
 * @return The combined string.  Caller must free.
 */
char *strappend(char *dest, ...);

/**
 * @brief Split a string on a delimiter.
 *
 * Each substring goes in to a str_list_t as its own entry.
 *
 * @param s String to split.
 * @param delim Delimiter to split on.
 * @return The list of substrings.  Caller must free the list.
 */
str_list_t *strsplit(const char *s, const char *delim);

/**
 * @brief Escape a name the way ls -b prints one.
 *
 * Control characters become \\ooo escapes and backslashes are
 * doubled.  Bytes of 0x80 and up are kept so UTF-8 names stay
 * readable.
 *
 * @param s String to escape.
 * @return Newly allocated escaped copy.  Caller must free.
 */
char *escape_name(const char *s);

/**
 * @brief Give back s, or "" when s is NULL.
 *
 * This is for callers that compare strings and do not want to test
 * for NULL first.
 *
 * @param s String to pass through.
 * @return s, or the empty string.  Do not free it.
 */
const char *str_or_empty(const char *s);

/**
 * @brief Format a message and pass it to warnx() with escape_name()
 *        applied.
 *
 * @param fmt printf(3) style format string.
 * @param ... Arguments for the format string.
 */
void warnx_escaped(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/**
 * @brief The warn() version of warnx_escaped().
 *
 * errno is kept so warn() can report it.
 *
 * @param fmt printf(3) style format string.
 * @param ... Arguments for the format string.
 */
void warn_escaped(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/** True when -v was given on the command line. */
extern bool verbose;

/**
 * Where the -v status lines go.  This is stdout, or stderr when a
 * piece of JSON is going to stdout so the JSON stays clean for a
 * pipe.
 */
extern FILE *verbose_out;

/**
 * @brief Print the line for a stage of the work with -v.
 *
 * Stages start with VERBOSE_STAGE_PREFIX and the details of each one
 * sit under it.
 *
 * @param fmt printf(3) style format string.
 * @param ... Arguments for the format string.
 */
void verbose_stage(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/**
 * @brief Print a detail line under the current stage with -v.
 *
 * @param fmt printf(3) style format string.
 * @param ... Arguments for the format string.
 */
void verbose_detail(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/**
 * @brief Write a byte count to buf for people to read.
 *
 * Under 1 KiB it is a count of bytes, above that one decimal place of
 * KiB, MiB or GiB.
 *
 * @param bytes Number of bytes to describe.
 * @param buf Buffer to write in to.
 * @param len Size of buf, for which HUMAN_SIZE_LEN is enough.
 */
void human_size(const uint64_t bytes, char *buf, const size_t len);

/** @} */

/** @name Linked lists (listfuncs.c) */
/** @{ */

/**
 * @brief Free a str_list_t and each entry->str.
 *
 * @param list List to free, which may be NULL.
 * @param free_func Function to free each entry->str, or NULL to leave
 *        the strings alone.
 */
void list_free(str_list_t *list, list_entry_data_free_func free_func);

/**
 * @brief Append a string to a str_list_t and return the list.
 *
 * A NULL string is not added and the caller gets the same list back.
 * A NULL list starts a new list with the string in it.  Caller
 * handles all memory management.
 *
 * @param list List to append to, or NULL to start one.
 * @param s String to append.
 * @return The list.
 */
str_list_t *list_add(str_list_t *list, const char *s);

/**
 * @brief Join all members of a str_list_t in to a single string.
 *
 * Combine all the members of a str_list_t in to a newly allocated
 * string.  The delimiter goes between each member, or pass NULL to
 * run them together.  Caller must free the string.
 *
 * @param list str_list_t containing members to join
 * @param delimiter Optional delimiter string to put between each list
 *        member (NULL to disable)
 * @return Newly allocated string of concatenated list members.  Caller
 *         must free.
 */
char *list_to_string(const str_list_t *list, const char *delimiter);

/**
 * @brief Free a uint32_list_t and each entry.
 *
 * @param list List to free.  Safe to call with NULL.
 */
void uint32_list_free(uint32_list_t *list);

/**
 * @brief Append a value to a uint32_list_t and return the list.
 *
 * A NULL list starts a new list with the value in it.  Caller handles
 * all memory management.
 *
 * @param list List to append to, or NULL to start one.
 * @param value Value to append.
 * @return The list.
 */
uint32_list_t *uint32_list_add(uint32_list_t *list, const uint32_t value);

/**
 * @brief Free a uint64_list_t and each entry.
 *
 * @param list List to free.  Safe to call with NULL.
 */
void uint64_list_free(uint64_list_t *list);

/**
 * @brief Append a value to a uint64_list_t and return the list.
 *
 * A NULL list starts a new list with the value in it.  Caller handles
 * all memory management.
 *
 * @param list List to append to, or NULL to start one.
 * @param value Value to append.
 * @return The list.
 */
uint64_list_t *uint64_list_add(uint64_list_t *list, const uint64_t value);

/**
 * @brief Return the number of entries in a str_list_t.
 *
 * @param list List to count, which may be NULL.
 * @return The number of entries.  A NULL list has none.
 */
uint32_t str_list_len(const str_list_t *list);

/**
 * @brief Return the number of entries in a uint32_list_t.
 *
 * @param list List to count, which may be NULL.
 * @return The number of entries.  A NULL list has none.
 */
uint32_t uint32_list_len(const uint32_list_t *list);

/**
 * @brief Return the first entry of a str_list_t.
 *
 * This and the five that follow walk one or more lists in lockstep.
 * Each one is NULL safe so callers can treat a missing list as a
 * missing value.
 *
 * @param list List to walk, which may be NULL.
 * @return The first entry, or NULL.
 */
str_entry_t *first_str(str_list_t *list);

/**
 * @brief Return the first entry of a uint32_list_t.
 *
 * @param list List to walk, which may be NULL.
 * @return The first entry, or NULL.
 */
uint32_entry_t *first_uint32(uint32_list_t *list);

/**
 * @brief Return the entry after this one in a str_list_t.
 *
 * @param entry Entry to step past, which may be NULL.
 * @return The next entry, or NULL at the end.
 */
str_entry_t *next_str(str_entry_t *entry);

/**
 * @brief Return the entry after this one in a uint32_list_t.
 *
 * @param entry Entry to step past, which may be NULL.
 * @return The next entry, or NULL at the end.
 */
uint32_entry_t *next_uint32(uint32_entry_t *entry);

/**
 * @brief Return the first entry of a uint64_list_t.
 *
 * @param list List to walk, which may be NULL.
 * @return The first entry, or NULL.
 */
uint64_entry_t *first_uint64(uint64_list_t *list);

/**
 * @brief Return the entry after this one in a uint64_list_t.
 *
 * @param entry Entry to step past, which may be NULL.
 * @return The next entry, or NULL at the end.
 */
uint64_entry_t *next_uint64(uint64_entry_t *entry);

/** @} */

/** @name Allocation that cannot fail (xalloc.c) */
/** @{ */

/**
 * @brief calloc(3) that exits when memory runs out.
 *
 * @param n Number of items.
 * @param s Size of each item.
 * @return Zero initialized memory.  Caller must free.
 */
void *xcalloc(size_t n, size_t s);

/**
 * @brief Allocate one zero initialized object.
 *
 * @param s Number of bytes.
 * @return Zero initialized memory.  Caller must free.
 */
void *xalloc(size_t s);

/**
 * @brief realloc(3) that exits when memory runs out.
 *
 * Note that xrealloc(NULL, ...) works the way you would want.
 *
 * @param p Memory to resize, which may be NULL.
 * @param s New size in bytes.
 * @return The resized memory.  Caller must free.
 */
void *xrealloc(void *p, size_t s);

/**
 * @brief strdup(3) that exits when memory runs out.
 *
 * @param s String to copy.
 * @return The copy.  Caller must free.
 */
char *xstrdup(const char *s);

/** @} */

/** @name Making directories and output files (mkdirp.c) */
/** @{ */

/**
 * @brief Create a directory and every parent it needs.
 *
 * A path that is already a directory is left alone.
 *
 * @param path Directory to create.
 * @param mode Mode for the directories we create.
 * @return 0 on success, -1 on failure.
 */
int mkdirp(const char *path, mode_t mode);

/**
 * @brief Create the directory needed to write out the named file.
 *
 * @param path File we are about to write.
 * @return 0 on success, -1 on failure.
 */
int mkdirp_for_file(const char *path);

/**
 * @brief Open a path for writing without following a symlink.
 *
 * This works like fopen() with mode "w", but refuses to follow a
 * symlink at path.
 *
 * @param path File to open.
 * @return The stream, or NULL on failure.
 */
FILE *fopen_output(const char *path);

/** @} */

/** @name Payload unpacking with libarchive (unpack.c) */
/** @{ */

struct archive;
struct archive_entry;

/**
 * @brief Unpack an archive file to a destination directory.
 *
 * The directory must exist before calling this.
 *
 * @param archive Path to the archive to read.
 * @param dest Directory to unpack in to.
 * @param verbose Print each entry as it lands on disk.
 * @return 0 if every entry went in, -1 if one failed after others
 *         were written, or UNPACK_UNREADABLE when libarchive could
 *         not read a single entry, which is the one case a second try
 *         with another reader can fix.
 */
int unpack_archive(const char *archive, const char *dest, const bool verbose);

/**
 * @brief Drop the setuid and setgid bits and limit device nodes to
 *        0600.
 *
 * The full modes stay in header.json.
 *
 * @param entry Archive entry to restrict.
 */
void restrict_mode(struct archive_entry *entry);

/**
 * @brief Make the libarchive writer both payload readers unpack with.
 *
 * It restores the times and modes and refuses a path that leaves the
 * current directory or goes through a symlink.
 *
 * @return The writer.  Caller must free it with libarchive.
 */
struct archive *new_disk_writer(void);

/**
 * @brief Print the path of an entry for -v the way tar(1) does.
 *
 * We take off one leading "./" or "/" so a dotfile keeps its dot.
 *
 * @param entry Archive entry to print.
 */
void print_entry_path(struct archive_entry *entry);

/** @} */

/** @name The RPM lead (lead.c) */
/** @{ */

/**
 * @brief Read the RPM lead and convert it to JSON data.
 *
 * @param fd Descriptor open on the package, positioned at the lead.
 * @return Newly allocated json_object, or NULL on error.  Caller must
 *         free.
 */
struct json_object *read_lead(const int fd);

/**
 * @brief Build a new RPM lead from header metadata.
 *
 * @param header The header.json data to read the name and type from.
 * @return Newly allocated lead, or NULL on failure.  Caller must
 *         free.
 */
struct rpmlead *create_lead(struct json_object *header);

/** @} */

/** @name RPM format handling (format.c) */
/** @{ */

/**
 * The RPM format we write the package for, which is RPM_FORMAT_V4 or
 * RPM_FORMAT_V6.  We set this from the command line before we create
 * an RPM.
 */
extern int rpmformat;

/**
 * @brief Put the tags array in to the shape the format calls for.
 *
 * A package we extracted carries the tags the format it was built as
 * owns, and leaving those behind would describe the package we write
 * wrongly, so they come out here.  The size tags move over to the
 * spelling the format uses rather than being thrown away, since the
 * numbers are the same either way.  Doing this twice over the same
 * array changes nothing the second time round.
 *
 * @param tags The tags array to filter.
 * @param format RPM format we are writing.
 */
void filter_format_tags(struct json_object *tags, const int format);

/**
 * @brief Put the main header in to the shape the format calls for.
 *
 * The tags the other format owns come out and the ones this format
 * needs go in with placeholder values.
 *
 * @param header The header.json data.
 * @param format RPM format we are writing.
 * @return 0 on success, non-zero on failure.
 */
int apply_rpmformat(struct json_object *header, const int format);

/**
 * @brief Return a mask of the payload digest tags header.json carries
 *        a value for.
 *
 * There is one bit per payload_digests[] entry.  We have to run
 * before apply_rpmformat() adds placeholders.  Without keep_digests
 * the mask is empty so every digest gets computed.
 *
 * @param header The header.json data.
 * @return The mask of kept digests.
 */
uint32_t kept_payload_digests(struct json_object *header);

/**
 * @brief Work out the payload digests and sizes and write them in to
 *        the main header.
 *
 * The payload has to exist by the time we are called.  A digest with
 * its bit set in kept stays as header.json has it.  We read the
 * payload once as it is and decompress it once, whatever the number
 * of digests.
 *
 * @param header The header.json data to update.
 * @param payloadfd Descriptor open on the payload.
 * @param format RPM format we are writing.
 * @param kept Mask from kept_payload_digests().
 * @param uncompressed Where to store the uncompressed size, or NULL.
 * @return 0 on success, non-zero on failure.
 */
int update_payload_tags(struct json_object *header, const int payloadfd, const int format, const uint32_t kept, uint64_t *uncompressed);

/** @} */

/** @name Header defaults (defaults.c) */
/** @{ */

/**
 * @brief Fill in the main header tags the tree did not name.
 *
 * Name, Version and Release have no value we can work out, so a
 * header without them is an error.  Everything else here is additive,
 * which is what lets an extracted tree go back out unchanged.
 *
 * @param header The header.json data to fill in.
 * @param format RPM format we are writing.
 * @return 0 on success, non-zero on failure.
 */
int apply_header_defaults(struct json_object *header, const int format);

/** @} */

/** @name The signature header (signature.c) */
/** @{ */

/**
 * @brief Read the RPM signature header and convert it to JSON data.
 *
 * @param fd Descriptor open on the package, positioned at the
 *        signature header.
 * @return Newly allocated json_object, or NULL on error.  Caller must
 *         free.
 */
struct json_object *read_signature(const int fd);

/**
 * @brief Build the signature header rpm would write for the format.
 *
 * We make this ourselves rather than read signature.json because
 * every tag in it is recalculated anyway.
 *
 * @param format RPM format we are writing.
 * @return Newly allocated json_object, or NULL on error.  Caller must
 *         free.
 */
struct json_object *make_signature(const int format);

/** @} */

/** @name Binary header handling (header.c) */
/** @{ */

/**
 * @brief Validate an RPM header.
 *
 * @param hdr Header to check.
 * @return True if the header is valid, false if hdr is NULL or the
 *         header does not hold together.
 */
bool valid_header(struct rpmhdr *hdr);

/**
 * @brief Read the main header and convert it to JSON data.
 *
 * @param fd Descriptor open on the package, positioned at the main
 *        header.
 * @param dest_dir Directory the tag value files go in.
 * @param raw_tags Set to true when the file list or changelog tags do
 *        not agree and stay in the tags array as they are.
 * @return Newly allocated json_object, or NULL on error.  Caller must
 *         free.
 */
struct json_object *read_header(const int fd, const char *dest_dir, bool *raw_tags);

/**
 * @brief Create a new header to write to an RPM file later.
 *
 * A header starts with the magic number, the record count and the
 * size of the storage area.  The storage area is a run of header
 * index structs followed by the data those records point at.  This
 * mostly sets the header up and the caller adds records to it after.
 *
 * @param data The JSON data to build the header from.
 * @param hdr Where to store the allocated header.  Caller must free.
 * @param hdrinfo Where to store the allocated computed values.
 *        Caller must free.
 * @param payload_dir The payload tree.
 * @param tagfile_dir Directory holding the tag value files.
 * @param is_signature True when building the signature header.
 * @return 0 on success, non-zero otherwise.
 */
int create_header(const struct json_object *data, struct rpmhdr **hdr, struct rpmhdrinfo **hdrinfo, const char *payload_dir, const char *tagfile_dir, bool is_signature);

/**
 * @brief Run the checks create_header() runs without building the
 *        header.
 *
 * @param data The JSON data to check.
 * @param payload_dir The payload tree.
 * @param tagfile_dir Directory holding the tag value files.
 * @param is_signature True when checking signature header data.
 * @return 0 if create_header() would accept the data, -1 otherwise.
 */
int check_header(const struct json_object *data, const char *payload_dir, const char *tagfile_dir, bool is_signature);

/**
 * @brief Return the number of bytes create_header() would make the
 *        header, not counting the padding that follows it.
 *
 * That is the header start, the index entries and the data area with
 * the trailer.  We add it up without laying the header out.
 *
 * @param data The JSON data to measure.
 * @param payload_dir The payload tree.
 * @param tagfile_dir Directory holding the tag value files.
 * @param is_signature True when measuring signature header data.
 * @return The size in bytes, or 0 on failure.
 */
size_t header_size(const struct json_object *data, const char *payload_dir, const char *tagfile_dir, bool is_signature);

/**
 * @brief Report whether the header carries a region trailer.
 *
 * @param nentries Number of index entries.
 * @param estart The first index entry.
 * @return True if there is a trailer in the header.
 */
bool has_trailer(const uint32_t nentries, const struct rpmhdrentry *estart);

/**
 * @brief Find the region tag and decode its trailer.
 *
 * Looks for the region tag of this kind of header in the header tags
 * and if found, base64 decodes it.
 *
 * @param data The JSON data to read.
 * @param trailer_data Where to store the decoded trailer.  Caller
 *        must free.
 * @param trailer_size Where to store the size of the trailer.
 * @param is_signature True when reading signature header data.
 * @return 0 on success, non-zero on error.
 */
int get_trailer_data(const struct json_object *data, uint8_t **trailer_data, size_t *trailer_size, bool is_signature);

/**
 * @brief Work the region trailer offset out again for the entries we
 *        really have.
 *
 * The region trailer records the size of the index entries it covers
 * as a negative offset.  The trailer we read back from the JSON still
 * describes the header the package came with, so we work the offset
 * out again.  That leaves out read-only tags like the signatures on a
 * signed package and takes in the tags a format change added.
 * Everything that reads the trailer has to go through here or the
 * digests will not match what we write.
 *
 * @param trailer_data The decoded trailer to fix.
 * @param trailer_size Size of the trailer.
 * @param nentries Number of index entries we are writing.
 */
void fix_trailer_offset(uint8_t *trailer_data, const size_t trailer_size, const uint32_t nentries);

/** @} */

/** @name The changelog (changelog.c) */
/** @{ */

/**
 * @brief Build the changelog array from raw header data.
 *
 * @param hdr The header to read.
 * @param hdrinfo Computed values for the header.
 * @return A JSON array of changelog entries, or NULL when the package
 *         has no changelog.
 */
struct json_object *generate_changelog(const struct rpmhdr *hdr, const struct rpmhdrinfo *hdrinfo);

/**
 * @brief Reconstruct the three changelog tag entries from the
 *        changelog array.
 *
 * @param tags The tags array to add the three tag entries to.
 * @param changelog The changelog array to read.
 * @return 0 on success, or -1 if an entry carries a date that cannot
 *         be read, which would otherwise land in the package as
 *         1 Jan 1970.
 */
int add_changelog_tags(struct json_object *tags, struct json_object *changelog);

/**
 * @brief Report whether a tag is one of the changelog tags.
 *
 * These are kept out of normal tag processing.
 *
 * @param tag Tag to test.
 * @return True if the tag is a changelog tag.
 */
bool is_changelog_tag(rpmTagVal tag);

/** @} */

/** @name Dependencies (deps.c) */
/** @{ */

/**
 * @brief Build all dependency arrays from raw header data.
 *
 * @param hdr The header to read.
 * @param hdrinfo Computed values for the header.
 * @return A JSON object with one array per dependency type, or NULL
 *         when the package has no dependencies.
 */
struct json_object *generate_dependencies(const struct rpmhdr *hdr, const struct rpmhdrinfo *hdrinfo);

/**
 * @brief Reconstruct all dependency tag entries from the dependencies
 *        object.
 *
 * @param tags The tags array to add the tag entries to.
 * @param dependencies The dependencies object to read.
 */
void add_dependency_tags(struct json_object *tags, struct json_object *dependencies);

/**
 * @brief Make sure the dependencies object has the shape we read it
 *        with.
 *
 * Each dependency type holds an array of objects.  The name,
 * comparison and version are strings and the sense flags an array of
 * strings.
 *
 * @param dependencies The dependencies object to check.
 * @return 0 on success, -1 if anything has the wrong type.
 */
int check_dependencies(struct json_object *dependencies);

/**
 * @brief Report whether a tag is one of the dependency tags.
 *
 * These are kept out of normal tag processing.
 *
 * @param tag Tag to test.
 * @return True if the tag is a dependency tag.
 */
bool is_dependency_tag(rpmTagVal tag);

/**
 * @brief Return the dependency type key for a depends dictionary
 *        abbreviation.
 *
 * @param abbrev The abbreviation to look up.
 * @return The type key, or NULL if it is not a type tarpm knows
 *         about.
 */
const char *dependency_type_key(const char abbrev);

/**
 * @brief Return the depends dictionary abbreviation for a dependency
 *        type key.
 *
 * @param key The type key to look up.
 * @return The abbreviation, or a NUL byte if it is not a type tarpm
 *         knows about.
 */
char dependency_type_abbrev(const char *key);

/**
 * @brief Build a lookup for lookup_dependency().
 *
 * Each dependency type maps the name and version of its entries to
 * the indexes that carry them, so a file with many provides does not
 * search the whole array for each one.
 *
 * @param dependencies The dependencies object to index.
 * @return The lookup, or NULL when there are no dependencies.  Caller
 *         must release it with json_object_put().
 */
struct json_object *index_dependencies(struct json_object *dependencies);

/**
 * @brief Find the entry in dependencies[key] matching the given entry
 *        and return its index.
 *
 * The name, version and flag word are everything the header records
 * about a dependency, so entries that agree on all three are the same
 * dependency.
 *
 * @param dependencies The dependencies object to search.
 * @param lookup The index from index_dependencies() on the same
 *        dependencies.
 * @param key The dependency type to search.
 * @param entry The dependency to look for.
 * @return The index, or -1 if there is no match.
 */
int lookup_dependency(struct json_object *dependencies, struct json_object *lookup, const char *key, struct json_object *entry);

/**
 * @brief Return how many dependencies of every kind the header data
 *        names.
 *
 * @param header The header.json data.
 * @return The total number of dependencies.
 */
size_t dependency_count(struct json_object *header);

/** @} */

/** @name The file list (files.c) */
/** @{ */

/**
 * The PAYLOAD_OVERRIDE_* bits for the file metadata we take from the
 * payload tree instead of from header.json.  We set this from the
 * command line before we create an RPM.
 */
extern uint32_t payload_overrides;

/**
 * Use the digests header.json and signature.json carry instead of
 * recomputing them.  This covers the file digests, the payload digest
 * tags, and the digests in the signature header.  We set this from
 * the command line before we create an RPM.  A digest missing from
 * the JSON still gets a computed one.
 *
 * @warning This is dangerous and really for testing and debugging
 *          only.
 */
extern bool keep_digests;

/**
 * Carry the package signatures signature.json holds in to the RPM we
 * create.  These are the Dsa, Rsa, Openpgp, Pgp, Gpg, and Pgp5 tags.
 * We set this from the command line before we create an RPM.  The
 * signatures are copied as-is and not checked.
 *
 * @warning This is dangerous and really for testing and debugging
 *          only.
 */
extern bool keep_signatures;

/**
 * @brief Generate a "files" array from the Dirnames, Basenames and
 *        Dirindexes tags.
 *
 * Each entry describes one file the package owns, gathered from the
 * parallel file list arrays in the header.
 *
 * @param hdr The header to read.
 * @param hdrinfo Computed values for the header.
 * @param dependencies The dependencies object, for the per-file
 *        provides.
 * @return The files array, or NULL if the required tags are not
 *         there.
 */
struct json_object *generate_files(const struct rpmhdr *hdr, const struct rpmhdrinfo *hdrinfo, struct json_object *dependencies);

/**
 * @brief A directory open_payload_tree() opened and the mode it had.
 */
struct payload_dir_mode {
    char *path;
    mode_t mode;
    struct payload_dir_mode *next;
};

/**
 * @brief Give ourselves access to the payload tree.
 *
 * A package can own a directory with a mode like 0644.  Only root can
 * look at the files below one of those, so we add the owner search
 * and read bits to the directories the file list names.  The mode in
 * the file list is what goes in to the package, not the one on disk,
 * so nothing we write changes.  Does nothing when we are root.
 *
 * @param payload_dir The payload tree.
 * @param files The files array, which says which modes are recorded.
 * @return The directories we opened, deepest last.  Hand it to
 *         close_payload_tree() when done with the payload tree.
 */
struct payload_dir_mode *open_payload_tree(const char *payload_dir, struct json_object *files);

/**
 * @brief Put back the modes open_payload_tree() changed.
 *
 * @param opened What open_payload_tree() gave back.  Freed here.
 */
void close_payload_tree(struct payload_dir_mode *opened);

/**
 * @brief Add entries to the "files" array for files added to the
 *        payload tree.
 *
 * Done when creating an RPM so a file put in the payload lands in the
 * file list of the new package.
 *
 * @param tags The tags array.
 * @param files The files array to add to.
 * @param payload_dir The payload tree.
 * @param format RPM format we are writing, which decides whether a
 *        new file gets a class or a MIME type.
 */
void add_payload_files(struct json_object *tags, struct json_object *files, const char *payload_dir, const int format);

/**
 * @brief Rebuild the file list tags from the "files" array and add
 *        them to the tags array.
 *
 * @param tags The tags array to add to.
 * @param files The files array to read.
 * @param payload_dir The payload tree.
 * @param dependencies The dependencies object, for the per-file
 *        provides.
 * @param format RPM format we are writing, which determines whether a
 *        file missing a class or a MIME type gets one assigned from
 *        the payload tree.
 * @return 0 on success, -1 when a file in the payload tree could not
 *         be read.
 */
int add_file_list_tags(struct json_object *tags, struct json_object *files, const char *payload_dir, struct json_object *dependencies, const int format);

/**
 * @brief Move the file and verity signatures to the "files" array.
 *
 * They go with the files they sign so they are visible there.  We
 * work the signature length out again when we create a package, so it
 * goes away.  The verity algorithm cannot be determined, so it moves
 * to the header tags.
 *
 * An older rpmsign(1) put the file signatures in the main header of a
 * v4 format package instead.  Those go to the "files" array too.
 * rpm(8) rejects a package with them in both headers, so we leave the
 * main header ones alone if the signature header had any.
 *
 * @param signature The signature.json data.
 * @param header The header.json data.
 */
void move_file_signatures(struct json_object *signature, struct json_object *header);

/**
 * @brief Create a signature header tag from a value and type.
 *
 * @param tag Tag number.
 * @param type Tag type.
 * @param value The value to carry.
 * @return The new entry as a json_object.
 */
struct json_object *new_signature_entry(const rpmTagVal tag, const rpmTagType type, struct json_object *value);

/**
 * @brief Add the file and verity signatures in the "files" array to
 *        the signature header.
 *
 * Each list holds one value per file in the file list, with an empty
 * string for a file with none, so we keep the same files
 * add_file_list_tags() does.  A list with nothing in it stays out.
 *
 * @param signature The signature header data to add to.
 * @param header The header.json data to read.
 * @param payload_dir The payload tree.
 * @param format RPM format we are writing.
 */
void add_file_signature_tags(struct json_object *signature, struct json_object *header, const char *payload_dir, const int format);

/**
 * @brief Report whether a tag is one of the file list tags.
 *
 * @param tag Tag to test.
 * @return True if the tag is a file list tag.
 */
bool is_file_list_tag(rpmTagVal tag);

/** @} */

/** @name File classes and MIME types (class.c) */
/** @{ */

/**
 * @brief Tell whether rpm keeps the type string libmagic handed back.
 *
 * rpm only records the ones that hold a token it knows and leaves the
 * rest with no class.
 *
 * @param name The string libmagic returned.
 * @return True when rpm would keep it.
 */
bool wanted_class(const char *name);

/**
 * @brief Find the entry in the table of path endings rpm answers
 *        itself.
 *
 * @param path Path to test.
 * @return The table entry, or NULL when the path does not end with
 *         one of them.
 */
const struct class_extension *skipped_extension(const char *path);

/**
 * @brief Determine the class string for a file in the payload.
 *
 * @param path Path relative to the payload root tree.
 * @param file_path Full path to the file in the payload tree.
 * @param sb Stat buffer for the file.
 * @return Newly allocated class string, or NULL when the file has no
 *         class.  Caller must free.
 */
char *file_class(const char *path, const char *file_path, const struct stat *sb);

/**
 * @brief Determine the MIME type rpm would give a file in a v6 format
 *        package.
 *
 * rpmfcClassify() in build/rpmfc.cc in the rpm source asks libmagic
 * about every file whatever its type, except that a regular file or
 * symlink with a path ending it knows about gets the type from its
 * table.  A file that is not there has no MIME type and one libmagic
 * cannot read is application/octet-stream.
 *
 * @param path Path relative to the payload root tree.
 * @param file_path Full path to the file in the payload tree.
 * @param sb Stat buffer for the file.
 * @return Newly allocated MIME type string, or NULL.  Caller must
 *         free.
 */
char *file_mime(const char *path, const char *file_path, const struct stat *sb);

/**
 * @brief Close the libmagic handles if we opened any.
 */
void close_magic(void);

/** @} */

/** @name Path joining and containment (joinpath.c) */
/** @{ */

/**
 * @brief Join path elements in to one path.
 *
 * A single slash goes between each element.  The list ends with NULL.
 *
 * @param path First path element.
 * @param ... The rest of the elements, terminated by NULL.
 * @return Newly allocated path, or NULL if path is NULL.  Caller must
 *         free.
 */
char *joinpath(const char *path, ...);

/**
 * @brief Report whether a path names something below base without
 *        leaving it.
 *
 * One leading slash is allowed and the path is still read as relative
 * to base.  Every component has to be non-empty and may not be "." or
 * "..".  No component that exists under base may be a symlink, except
 * the last one when allow_last_link is true.  Once a component is
 * missing there is nothing left to follow, so we only check the names
 * of the rest.
 *
 * @param base Directory the path has to stay below.
 * @param path Path to check.
 * @param allow_last_link Allow the last component to be a symlink.
 * @return True when the path stays below base.
 */
bool path_beneath(const char *base, const char *path, const bool allow_last_link);

/**
 * @brief path_beneath() for a caller checking many paths under one
 *        base.
 *
 * seen keys the leading components we found are not symlinks, so each
 * directory gets one lstat() however many files are below it.  The
 * last component is always looked at.
 *
 * @param base Directory the path has to stay below.
 * @param path Path to check.
 * @param allow_last_link Allow the last component to be a symlink.
 * @param seen Cache of components already checked, which may be NULL.
 * @return True when the path stays below base.
 */
bool path_beneath_seen(const char *base, const char *path, const bool allow_last_link, struct json_object *seen);

/**
 * @brief Open a path one component at a time with O_NOFOLLOW.
 *
 * This is the fallback for open_beneath_at() where openat2() is
 * missing.  basefd stays open.
 *
 * @param basefd Descriptor for the directory the path sits below.
 * @param path Path to open, relative to basefd.
 * @return The descriptor, or -1 with errno set.  A plain name check
 *         that fails sets EINVAL and a symlink sets ELOOP.
 */
int open_beneath_walk_at(const int basefd, const char *path);

/**
 * @brief Open a path below a directory without following a symlink
 *        anywhere in it.
 *
 * The kernel checks the whole path with openat2() and
 * RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS, so a component swapped for a
 * symlink after path_beneath() looked at it is not followed.  Without
 * openat2() we walk the components ourselves.  basefd stays open, so
 * a caller opening many files below one directory opens that
 * directory once.
 *
 * @param basefd Descriptor for the directory the path sits below.
 * @param path Path to open, relative to basefd.
 * @return The descriptor, or -1 with errno set.  ELOOP, EXDEV and
 *         EINVAL mean the path leaves the directory or goes through a
 *         symlink.
 */
int open_beneath_at(const int basefd, const char *path);

/**
 * @brief Like open_beneath_at() but also open a regular file we own
 *        that has no owner read bit.
 *
 * Files such as /etc/shadow and setuid programs extract that way for
 * a user who is not root.  The owner read bit is added long enough to
 * open the file and the mode is put back right away.
 *
 * @param basefd Descriptor for the directory the path sits below.
 * @param path Path to open, relative to basefd.
 * @return The descriptor, or -1 with errno set.
 */
int open_beneath_readable_at(const int basefd, const char *path);

/**
 * @brief Open a directory for use as the basefd of open_beneath_at()
 *        and open_beneath_walk_at().
 *
 * @param base Directory to open.
 * @return The descriptor, or -1 with errno set.  The caller has to
 *         close it.
 */
int open_beneath_base(const char *base);

/** @} */

/** @name JSON reading and writing (json.c) */
/** @{ */

/**
 * @brief Turn a struct rpmhdrentry in to a json_object.
 *
 * @param hdrentry The index entry to convert.
 * @param signature True when the entry is a signature header tag.
 * @return The new json_object.
 */
struct json_object *create_json_entry(const struct rpmhdrentry *hdrentry, const bool signature);

/**
 * @brief Generate a signature or header JSON structure for output.
 *
 * @param hdr The header to describe.
 * @return The new json_object.
 */
struct json_object *generate_json(const struct rpmhdr *hdr);

/**
 * @brief Generate the JSON array of tag entries for a header.
 *
 * @param hdr The header to read.
 * @param hdrinfo Computed values for the header.
 * @param trailer The region trailer, or NULL.
 * @param dest_dir Directory the tag value files go in.
 * @param signature True when reading the signature header.
 * @return The array of entries.
 */
struct json_object *generate_json_entries(const struct rpmhdr *hdr, const struct rpmhdrinfo *hdrinfo, struct rpmhdrentry *trailer, const char *dest_dir, const bool signature);

/**
 * @brief Add the header tags is_member picks to a tags array.
 *
 * We use this when the dedicated array for a group of tags cannot be
 * built, so the values still reach header.json.
 *
 * @param kvals The tags array to add to.
 * @param hdr The header to read.
 * @param hdrinfo Computed values for the header.
 * @param dest_dir Directory the tag value files go in.
 * @param is_member Predicate picking the tags to keep.
 * @return True if the header has any of those tags.
 */
bool keep_raw_entries(struct json_object *kvals, const struct rpmhdr *hdr, const struct rpmhdrinfo *hdrinfo, const char *dest_dir, bool (*is_member)(rpmTagVal));

/**
 * @brief Sort a tags array by tag number.
 *
 * qsort() is not stable, so we break a tie on the position in the
 * array, which is the header order.  The copies of a tag that is
 * there twice then go back in the order they came out, and a round
 * trip keeps the value rpm finds.
 *
 * @param kvals The tags array to sort.
 * @param signature Read the names as signature tags, since some share
 *        a name with a header tag that has another number.
 */
void sort_tags_stable(struct json_object *kvals, const bool signature);

/**
 * @brief Return the length of the array a key holds.
 *
 * @param obj Object to look in.
 * @param key Key naming the array.
 * @return The length, or 0 if the key does not hold an array.
 */
size_t key_array_length(struct json_object *obj, const char *key);

/**
 * @brief Print with -v how much the header data holds.
 *
 * The dependencies object is counted across every kind of dependency.
 *
 * @param header The header.json data.
 */
void verbose_counts(struct json_object *header);

/**
 * @brief Read a JSON file.
 *
 * @param input_file Path to the file.
 * @return The parsed json_object, or NULL on error.  Caller must
 *         free.
 */
struct json_object *read_json_file(const char *input_file);

/**
 * @brief Write JSON data to the file named by path.
 *
 * A path of a single hyphen sends the JSON to stdout, which the
 * caller keeps.
 *
 * @param data The JSON to write.
 * @param path Where to write it.
 * @return 0 on success, -1 on error.
 */
int write_json_file(struct json_object *data, const char *path);

/** @} */

/** @name Tag names, types and values (tags.c) */
/** @{ */

/**
 * @brief Convert a tag type to its symbolic type name.
 *
 * @param type The type to name.
 * @return The name.  Do not free it.
 */
const char *strtagtype(rpmTagType type);

/**
 * @brief Convert a file digest algorithm number to its name.
 *
 * An algorithm with no known name comes back as the number itself in
 * string form.
 *
 * @param algo The algorithm number.
 * @return Newly allocated name.  Caller must free.
 */
char *strdigestalgo(uint32_t algo);

/**
 * @brief Convert a file digest algorithm name back to its number.
 *
 * A name that is not known is read as a number, which is how
 * strdigestalgo() writes out an algorithm it has no name for.
 *
 * @param name The algorithm name.
 * @return The number, or zero if the name is neither known nor a
 *         number.
 */
uint32_t digest_algo(const char *name);

/**
 * @brief Convert a build time to an ISO 8601 timestamp in UTC.
 *
 * This is how header.json records it.  A value that will not convert
 * comes back as the number itself in string form.
 *
 * @param buildtime The build time.
 * @return Newly allocated timestamp.  Caller must free.
 */
char *strbuildtime(uint32_t buildtime);

/**
 * @brief Convert an ISO 8601 timestamp back to a build time.
 *
 * A string that is not a timestamp is read as a number, which is how
 * strbuildtime() writes out a value it could not convert.
 *
 * @param timestamp The timestamp to read.
 * @return The build time, or zero if the string is neither.
 */
uint32_t buildtime_value(const char *timestamp);

/**
 * @brief Convert a string type name to its rpmTagType value.
 *
 * @param tag The tags array entry to read the type from.
 * @return The type.
 */
rpmTagType tag_type(struct json_object *tag);

/**
 * @brief Convert a signature tag value to its symbolic name.
 *
 * We take a uint32_t here and not an rpmSigTag since we may get
 * private values that are not in the rpmSigTag enum.
 *
 * @param tag The tag number.
 * @return The name.  Do not free it.
 */
const char *sig_tag_name(uint32_t tag);

/**
 * @brief Return the name we write in a tags entry for a main header
 *        tag.
 *
 * A tag the librpm we are linked to has no name for, such as
 * Sourcenevr before rpm 6.0.0, gets the "#XXXX" form so
 * get_tag_number() reads it back.
 *
 * @param tag The tag number.
 * @return The name, good until the next call.  Do not free it.
 */
const char *header_tag_name(const rpmTagVal tag);

/**
 * @brief Report whether a tag is a signature header tag rpmsign
 *        writes.
 *
 * Only rpmsign can make these, so we leave them out of a signature
 * header we create and let rpmsign put them back.
 *
 * @param tag The tag number.
 * @return True when rpmsign writes the tag.
 */
bool is_rpmsign_tag(uint32_t tag);

/**
 * @brief Report whether a tag is one of the file or verity signature
 *        tags.
 *
 * rpmsign(1) makes these too, but they sign the files rather than the
 * package, so they still hold once we rebuild it.  The "files" array
 * in header.json carries them.
 *
 * @param tag The tag number.
 * @return True when the tag is a file or verity signature tag.
 */
bool is_file_signature_tag(uint32_t tag);

/**
 * @brief Return the tag number for the name a tags entry carries.
 *
 * @param entry The tags array entry.
 * @param signature True to read the name as a signature tag.
 * @return The tag number.
 */
rpmTagVal get_tag_number(struct json_object *entry, bool signature);

/**
 * @brief Take every copy of the n tags in drop out of the tags array.
 *
 * We slide the kept entries down in one pass and cut the tail off
 * once, so the cost does not grow with how many copies go.
 *
 * @param tags The tags array.
 * @param drop The tags to take out.
 * @param n How many tags drop holds.
 */
void drop_tags(struct json_object *tags, const rpmTagVal *drop, const size_t n);

/**
 * @brief Take every copy of one tag out of the tags array.
 *
 * @param tags The tags array.
 * @param tag The tag to take out.
 */
void drop_tag(struct json_object *tags, const rpmTagVal tag);

/**
 * @brief Map a tag name spelling to its number for tags renamed in
 *        major RPM versions.
 *
 * @param tag The tag name.
 * @return The tag number, or RPMTAG_NOT_FOUND for an unknown name.
 */
rpmTagVal renamed_tag_number(const char *tag);

/**
 * @brief Report whether a tags array carries an entry for a tag.
 *
 * Unlike get_tag_value() this does not care whether the entry holds
 * its value inline or in a file alongside header.json.
 *
 * @param tags The tags array.
 * @param tag The tag to look for.
 * @param signature True to read the names as signature tags.
 * @return True when the tag is there.
 */
bool has_tag(const struct json_object *tags, const rpmTagVal tag, const bool signature);

/**
 * @brief Find the entry for a tag and return its value as a string.
 *
 * @param tags The tags array.
 * @param tag The tag to look for.
 * @param signature True to read the names as signature tags.
 * @return The value, or NULL if there is no match.  Do not free it.
 */
const char *get_tag_value(const struct json_object *tags, const rpmTagVal tag, const bool signature);

/**
 * @brief Find the entry for a tag and set its value.
 *
 * @param tags The tags array.
 * @param tag The tag to set.
 * @param signature True to read the names as signature tags.
 * @param new_value The value to store.
 * @return 0 on success, -1 on failure.
 */
int set_tag_value(struct json_object *tags, const rpmTagVal tag, const bool signature, const char *new_value);

/** @} */

/** @name Reading the binary header (read.c) */
/** @{ */

/**
 * @brief Compute the values needed to iterate over a header.
 *
 * @param hdr The header to describe.
 * @param signature True when the header is the signature header.
 * @return Newly allocated computed values.  Caller must free.
 */
struct rpmhdrinfo *mkhdrinfo(const struct rpmhdr *hdr, const bool signature);

/**
 * @brief Read the intro part of a signature or main header.
 *
 * These are structurally the same but hold different data.
 *
 * @param fd Descriptor open on the package, positioned at the header.
 * @return Newly allocated header, or NULL on error.  Caller must
 *         free.
 */
struct rpmhdr *read_header_signature(const int fd);

/**
 * @brief Read the entries block of a header in to a buffer for random
 *        access.
 *
 * @param fd Descriptor open on the package.
 * @param hdr The header being read.
 * @param hlen Length of the header.
 * @return Newly allocated buffer with the data in it, or NULL on
 *         error.  Caller must free.
 */
uint32_t *read_header_entries(const int fd, const struct rpmhdr *hdr, const uint32_t hlen);

/**
 * @brief Read and return the region trailer if there is one.
 *
 * @param hdr The header being read.
 * @param estart The first index entry.
 * @param datastart The start of the data area.
 * @return Newly allocated trailer, or NULL.  Caller must free.
 */
struct rpmhdrentry *read_header_trailer(const struct rpmhdr *hdr, const struct rpmhdrentry *estart, const uint8_t *datastart);

/**
 * @brief Check that the data of every index entry sits inside the
 *        data area.
 *
 * Each string has to end in a NUL before the end of the data area so
 * the readers can strlen() it.  Nothing reads the data of a type we
 * do not know, so for one of those we only check the offset.
 *
 * @param hdr The header being read.
 * @param estart The first index entry.
 * @param datastart The start of the data area.
 * @return True if every entry is in bounds, false otherwise.
 */
bool valid_header_entries(const struct rpmhdr *hdr, const struct rpmhdrentry *estart, const uint8_t *datastart);

/** @} */

/** @name librpm state (reset.c) */
/** @{ */

/**
 * @brief Drop the librpm macro and rc state we picked up.
 *
 * librpm reads the system macro files the first time we use it.  We
 * only need its tag tables, so let the rest go.  Calling this more
 * than once does nothing after the first time.
 */
void reset_librpm(void);

/** @} */

/** @name Header index entry values (entry.c) */
/** @{ */

/**
 * @brief Report whether a tag value should go to a text file.
 *
 * @param tag Tag to test.
 * @return True when the value goes to its own file.
 */
bool is_file_tag(rpmTagVal tag);

/**
 * @brief Get the name of the file to write a tag value to.
 *
 * @param tag The tag the file is named after.
 * @param ending The file ending to use, such as "txt".
 * @return Newly allocated file name.  Caller must free.
 */
char *get_tag_filename(rpmTagVal tag, const char *ending);

/**
 * @brief Replace an i18n string value with an object keyed by locale.
 *
 * An i18n string tag holds one string per locale in the order of
 * Headeri18ntable, and may stop short of the end of the table.  This
 * replaces the value add_entry_value() wrote for the first one.
 * Without a locale for every string, or with a locale named twice, we
 * warn and leave it alone since the object would lose a string.
 *
 * @param arrayentry The tags array entry to update.
 * @param tag The tag being read.
 * @param data The raw tag data.
 * @param count How many strings the tag holds.
 * @param i18ntable The Headeri18ntable value.
 * @param dest_dir Directory the tag value files go in.
 * @return 0 on success, -1 for a locale we cannot use as a file name.
 */
int add_i18n_value(struct json_object *arrayentry, rpmTagVal tag, uint8_t *data, uint32_t count, struct json_object *i18ntable, const char *dest_dir);

/**
 * @brief Read the value of one header index entry in to a tags array
 *        entry.
 *
 * The value lands under a "value" key, or under a "file" key naming
 * the text file we wrote it to for a tag that works that way.
 *
 * @param arrayentry The tags array entry to fill in.
 * @param tag The tag being read.
 * @param buffer The header data area.
 * @param offset Where the value sits in the data area.
 * @param datatype The type of the value.
 * @param count How many values the entry holds.
 * @param dest_dir Directory the tag value files go in.
 * @param signature True when reading a signature header tag.
 * @return 0 on success, -1 on failure.
 */
int add_entry_value(struct json_object *arrayentry, rpmTagVal tag, uint8_t *buffer, uint32_t offset, rpmTagType datatype, uint32_t count, const char *dest_dir, const bool signature);

/** @} */

/** @name Reading bytes (xread.c) */
/** @{ */

/**
 * @brief Wrapper for read(2) that reads the full count.
 *
 * Passing in a count of 0 is valid and returns true without calling
 * read(2).  We know this.
 *
 * @param fd Descriptor to read from.
 * @param buf Buffer to read in to.
 * @param count How many bytes to read.
 * @return True when the full count was read, false otherwise.
 */
bool xread(int fd, void *buf, size_t count);

/** @} */

/** @name Command line modes (extract.c, create.c, list.c) */
/** @{ */

/**
 * @brief Handler for -x mode (extract).
 *
 * @param filename The package to extract.
 * @param cwd The directory we were run from.
 * @param output_dir Where the tree goes, or NULL for the NEVRA name.
 * @param paths Where each JSON file and the payload tree go.
 * @param verbose Print each stage of the work.
 * @param skip_payload Write the metadata and skip the payload.
 * @return 0 on success, non-zero on failure.
 */
int extract_rpm(const char *filename, const char *cwd, const char *output_dir, const struct json_paths *paths, const bool verbose, const bool skip_payload);

/**
 * @brief Handler for -c mode (create).
 *
 * @param filename The package to write.
 * @param cwd The directory we were run from.
 * @param input_dir The tree to build the package from.
 * @param paths Where each JSON file and the payload tree are read
 *        from.
 * @return 0 on success, non-zero on failure.
 */
int create_rpm(const char *filename, const char *cwd, const char *input_dir, const struct json_paths *paths);

/**
 * @brief Handler for -t mode (list).
 *
 * Opens the package and lists the payload contents.
 *
 * @param rpm The package to list.
 * @return 0 on success, -1 if the package or its payload cannot be
 *         read.
 */
int list_rpm(const char *rpm);

/** @} */

/** @name Digests (digest.c) */
/** @{ */

/**
 * @brief Compute a signature header digest.
 *
 * This covers the header, and the payload as well for the Md5 the
 * signature header records.  rpm only feeds the payload in to Md5,
 * every other signature digest covers the header alone.
 *
 * This is not a general purpose digest function.  It only feeds those
 * two parts of an RPM in to the algorithm.
 *
 * @param type The digest algorithm.
 * @param hdr The header to digest.
 * @param hdrinfo Computed values for the header.
 * @param data The header JSON data.
 * @param fd Descriptor open on the payload.
 * @return Newly allocated digest, or NULL on failure.  Caller must
 *         free.
 */
unsigned char *mksigdigest(const int type, const struct rpmhdr *hdr, const struct rpmhdrinfo *hdrinfo, const struct json_object *data, const int fd);

/**
 * @brief Digest the payload as it lands in the package.
 *
 * We read it once and compute a digest for each of the n types, so
 * out needs room for n strings.
 *
 * @param types The digest algorithms to use.
 * @param n How many types there are.
 * @param payloadfd Descriptor open on the payload.
 * @param out Where to store the n digest strings.  Caller must free
 *        them.
 * @param size Where to store the size the payload takes up, or NULL.
 * @return 0 on success, -1 on failure.
 */
int payload_digest_list(const int *types, const size_t n, const int payloadfd, char **out, uint64_t *size);

/**
 * @brief Digest the payload with the compression taken back off.
 *
 * This is what the ALT tags in the main header record.  We decompress
 * it once and compute a digest for each of the n types, so out needs
 * room for n strings.  With n of 0 we only measure it.
 *
 * @param types The digest algorithms to use.
 * @param n How many types there are.
 * @param payloadfd Descriptor open on the payload.
 * @param out Where to store the n digest strings.  Caller must free
 *        them.
 * @param size Where to store the uncompressed size, or NULL.
 * @return 0 on success, -1 on failure.
 */
int archive_digest_list(const int *types, const size_t n, const int payloadfd, char **out, uint64_t *size);

/**
 * @brief Give back a digest of all zeroes in the hex form the header
 *        records.
 *
 * rpm sizes its header with one of these in nullDigest() so the two
 * sizing passes agree, and we do the same.
 *
 * @param type The digest algorithm.
 * @return Newly allocated string, or NULL for a type we do not know.
 *         Caller must free.
 */
char *nul_digest(const int type);

/** @} */

/** @name File mode strings (strmode.c) */
/** @{ */

/**
 * @brief Write a mode out the way ls(1) prints it.
 *
 * @param mode The mode to describe.
 * @param p Buffer to write in to, which needs room for 12 bytes.
 */
void strmode(mode_t mode, char *p);

/** @} */

/** @name Reading a whole file (readfile.c) */
/** @{ */

/**
 * @brief Read the contents of a file in to a single buffer.
 *
 * @param path File to read.
 * @param len Where to store the number of bytes read.
 * @return Newly allocated buffer, or NULL on failure.  Caller must
 *         free.
 */
void *read_file_bytes(const char *path, off_t *len);

/** @} */

#endif /* _TARPM_TARPM_H */
