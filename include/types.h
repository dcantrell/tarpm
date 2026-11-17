/*
 * Copyright The rpminspect Project Authors
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

/**
 * @file
 * @brief The structures tarpm reads an RPM in to.
 *
 * The first few mirror the binary layout of an RPM file.  The rest
 * are the working types we carry the metadata around in.
 */

#ifndef _TARPM_TYPES_H
#define _TARPM_TYPES_H

#include "queue.h"

/**
 * @brief The RPM lead, the first 96 bytes of every package.
 *
 * This is from lib/rpmlead.c in rpm's source.  The RPM "lead" is
 * legacy, but is part of every RPM.  The type and handling of this is
 * not part of the public librpm API, but we need it here to
 * reconstruct an RPM.
 */
struct rpmlead {
    unsigned char magic[4];  /**< The lead magic number */
    unsigned char major;     /**< 3 for the v4 format, 4 for the v6 format */
    unsigned char minor;     /**< The minor version, which is 0 */
    int16_t type;            /**< 0 for a binary package, 1 for a source one */
    int16_t archnum;         /**< Build architecture, 0 from rpm 6.0.0 on */
    char name[66];           /**< The package NEVR, NUL padded */
    int16_t osnum;           /**< Build OS, 0 from rpm 6.0.0 on */
    int16_t signature_type;  /*!< Signature header type (RPMSIG_HEADERSIG) */
    char reserved[16];       /*!< Pad to 96 bytes -- 8 byte aligned! */
};

/**
 * @brief The structure that begins a header.
 *
 * This and struct rpmhdrentry are in the "signature" and "header"
 * header.  This is the core structure that begins a new header.  The
 * way the format is defined is that an RPM can have multiple headers
 * like this.  Right now RPM files have two: one called a "signature"
 * and one called a "header".  The latter contains all of the metadata
 * most packagers are looking for.
 */
struct rpmhdr {
    uint32_t magic;        /**< must be "\216\255\350\001" */
    uint32_t reserved;     /**< must be "\0\0\0\0" */
    uint32_t nentries;     /**< number of index records */
    uint32_t nbytes;       /**< size of storage area for data */
};

/**
 * @brief Computed values for an rpmhdr.
 *
 * mkhdrinfo() works these out so the rest of the code can walk a
 * header without recomputing them.
 */
struct rpmhdrinfo {
    uint32_t hlen;                 /**< Length of the header */
    struct rpmhdrentry *estart;    /**< The first index entry */
    struct rpmhdrentry *entry;     /**< The entry we are looking at */
    uint8_t *datastart;            /**< Start of the data area */
    uint32_t padlen;               /**< Bytes of padding after the header */
    struct rpmhdr pad;             /**< The padding itself */
};

/** The size of the header intro to read from the file */
#define RPMHDRINTROSZ (sizeof(uint32_t) * 4)

/** @brief One index entry in a header, which is one tagged value. */
struct rpmhdrentry {
    uint32_t tag;          /**< the key */
    uint32_t type;         /**< the data type */
    int32_t offset;        /**< where to find the data in the storage area */
    uint32_t count;        /**< how many data items are stored in this key */
};

/** A union for data types used when extracting data from the header. */
union datatypes
{
    char c;         /**< RPM_CHAR_TYPE */
    uint8_t i8;     /**< RPM_INT8_TYPE */
    uint16_t i16;   /**< RPM_INT16_TYPE */
    uint32_t i32;   /**< RPM_INT32_TYPE */
    uint64_t i64;   /**< RPM_INT64_TYPE */
};

/** One entry in a str_list_t. */
typedef struct _str_entry_t {
    char *str;                       /**< The string this entry carries */
    TAILQ_ENTRY(_str_entry_t) items; /**< Link to the rest of the list */
} str_entry_t;

/** List of strings. */
typedef TAILQ_HEAD(str_entry_s, _str_entry_t) str_list_t;

/** One entry in a uint32_list_t. */
typedef struct _uint32_entry_t {
    uint32_t value;                     /**< The value this entry carries */
    TAILQ_ENTRY(_uint32_entry_t) items; /**< Link to the rest of the list */
} uint32_entry_t;

/** List of uint32_t values. */
typedef TAILQ_HEAD(uint32_entry_s, _uint32_entry_t) uint32_list_t;

/** One entry in a uint64_list_t. */
typedef struct _uint64_entry_t {
    uint64_t value;                     /**< The value this entry carries */
    TAILQ_ENTRY(_uint64_entry_t) items; /**< Link to the rest of the list */
} uint64_entry_t;

/** List of uint64_t values. */
typedef TAILQ_HEAD(uint64_entry_s, _uint64_entry_t) uint64_list_t;

/**
 * @brief The header lists that carry per-entry values for the
 *        payload.
 *
 * This is just a convenience grouping to hold all of the metadata
 * lists that will be used to construct libarchive entries.
 */
struct hdr_file_lists {
    struct json_object *basenames;   /**< Basenames */
    struct json_object *dirnames;    /**< Dirnames */
    struct json_object *dirindexes;  /**< Dirindexes */
    struct json_object *filesizes;   /**< Filesizes or Longfilesizes */
    struct json_object *filemodes;   /**< Filemodes */
    struct json_object *filerdevs;   /**< Filerdevs */
    struct json_object *filemtimes;  /**< Filemtimes */
    struct json_object *filelinktos; /**< Filelinktos */
    struct json_object *fileinodes;  /**< Fileinodes */
    struct json_object *fileflags;   /**< Fileflags */
};

/**
 * @brief The hardlink groups we find in the header file lists.
 *
 * A group is one inode, how many file list entries use it, and the
 * payload path of the last entry we saw.  That last one holds the
 * file data.  All three arrays use the same index and hold 'count'
 * entries.
 */
struct hardlink_groups {
    char **paths;        /**< Payload path of the last entry per group */
    uint32_t *inodes;    /**< The inode number of each group */
    uint32_t *nlinks;    /**< How many file list entries use each group */
    size_t count;        /**< How many groups there are */
    size_t *group;       /**< per file, the group index or SIZE_MAX */
};

/**
 * @brief What we need to compress the payload with zstd or xz on our
 *        own.
 *
 * We write the cpio stream to the pipe and a child process compresses
 * it in to the payload file.  xz picks xz over zstd.
 */
struct zstd_payload {
    bool used;       /**< True when we are compressing this way */
    bool xz;         /**< Use xz rather than zstd */
    int level;       /**< The compression level */
    int pipefd[2];   /**< The pipe we write the cpio stream to */
    pid_t pid;       /**< The child doing the compression */
    int status;      /**< The wait status of the child */
};

/**
 * @brief The header values one payload entry needs.
 *
 * This struct is to help get that info over to libarchive.  The
 * entries in this struct match entries from RPM header tags for the
 * file metadata.  RPM divides up the information in a unique way.  We
 * update the data in our structure from the actual filesystem if it
 * differs from what's in the RPM header metadata.
 */
struct file_params {
    /** from RPMTAG_DIRNAMES */
    const char *dirname;

    /** from RPMTAG_BASENAMES */
    const char *basename;

    /** from RPMTAG_FILESIZES */
    uint64_t size;

    /** from RPMTAG_FILEMODES */
    uint16_t mode;

    /** from RPMTAG_FILEUIDS */
    uint32_t uid;

    /** from RPMTAG_FILEGIDS */
    uint32_t gid;

    /** from RPMTAG_FILERDEVS */
    uint16_t rdev;

    /** from RPMTAG_FILEMTIMES */
    uint32_t mtime;

    /** from RPMTAG_FILELINKTOS */
    const char *linkto;

    /** from RPMTAG_FILEINODES */
    uint32_t inode;

    /** from RPMTAG_FILENLINKS */
    uint32_t nlink;

    /** path to the first occurrence of the hardlink in our payload subdir */
    const char *hardlink;

    /** position in the header file list, which a stripped cpio entry carries */
    size_t index;
};

/**
 * @brief Where the JSON metadata files and the payload tree are read
 *        from or written to.
 *
 * A NULL member means the usual name in the working directory.  The
 * paths here are absolute.
 */
struct json_paths {
    char *lead;       /**< Where lead.json goes */
    char *signature;  /**< Where signature.json goes */
    char *header;     /**< Where header.json goes */
    char *payload;    /**< Where the payload tree goes */
};

/** Dependency type definitions */
struct dep_type {
    const char *key;     /**< The dependencies key, such as "requires" */
    rpmTagVal name;      /**< The name tag for this type */
    rpmTagVal flags;     /**< The flags tag for this type */
    rpmTagVal version;   /**< The version tag for this type */

    /** how the depends dictionary names this type */
    char abbrev;
};

/**
 * @brief The three tag arrays we read for one dependency type.
 *
 * The names and the versions are strings and the flags are numbers.
 * Each array carries its own count because the header does not have
 * to give us all three.
 */
struct dep_arrays {
    char **names;        /**< The dependency names */
    uint32_t nnames;     /**< How many names there are */
    uint32_t *flags;     /**< The dependency flag words */
    uint32_t nflags;     /**< How many flag words there are */
    char **versions;     /**< The dependency versions */
    uint32_t nversions;  /**< How many versions there are */
};

/** A path ending and the class and MIME type rpm gives every file that has it. */
struct class_extension {
    const char *extension;  /**< The path ending, such as ".h" */
    const char *name;       /**< The class rpm records for it */
    const char *mime;       /**< The MIME type rpm records for it */
};

/** Free function for the data a list entry carries. */
typedef void (*list_entry_data_free_func)(void *);

#endif /* _TARPM_TYPES_H */
