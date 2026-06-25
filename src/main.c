/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <getopt.h>
#include <locale.h>
#include <errno.h>
#include <err.h>
#include <assert.h>
#include <arpa/inet.h>
#include <rpm/header.h>
#include <rpm/rpmbase64.h>

#include "tarpm.h"

/* Static variables */
static bool t_flag = false;
static bool x_flag = false;
static bool c_flag = false;
static bool v_flag = false;

static void
usage(void)
{
    printf(_("RPM extraction and creation utility\n"));
    printf(_("Usage: %s [OPTIONS] [.rpm file] [directory]\n"), COMMAND_NAME);
    printf(_("Options:\n"));
    printf(_("    -t, --list                        List RPM payload contents\n"));
    printf(_("    -c, --create                      Create an RPM file\n"));
    printf(_("    -x, --extract                     Extract RPM file\n"));
    printf(_("    -v, --verbose                     Verbose progress output\n"));
    printf(_("    -f FILENAME, --filename=FILENAME  Use FILENAME as input or output\n"));
    printf(_("    -O DIRNAME, --output=DIRNAME      Use DIRNAME as output directory\n"));
    printf(_("    -V, --version                     Display version information\n"));
    printf(_("    -?, --help                        Display this screen\n"));
    printf(_("See the %s(1) man page for more information.\n"), COMMAND_NAME);

    return;
}

/* Handler for -x mode (extract) */
static void
extract_rpm(const char *filename, const char *cwd, const char *output_dir)
{
    int mode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
    int rpmfd = -1;
    char *candidate_path = NULL;
    char *tmp = NULL;
    char *payload_file = NULL;
    char *dest_dir = NULL;
    Header h;
    struct json_object *lead = NULL;
    struct json_object *signature = NULL;
    struct json_object *header = NULL;

    assert(cwd != NULL);
    assert(filename != NULL);

    /* validate the specified file is an RPM */
    h = get_header(filename);

    if (h == NULL) {
        errx(EXIT_FAILURE, _("*** %s is not a valid RPM"), filename);
    }

    /* open the RPM file (this handle will be passed around) */
    rpmfd = open(filename, O_RDONLY);

    if (rpmfd == -1) {
        err(EXIT_FAILURE, "open");
    }

    /* extract the RPM lead -- the first header (unused) */
    lead = read_lead(rpmfd);

    if (lead == NULL) {
        err(EXIT_FAILURE, "read_lead");
    }

    /* extract the RPM signature -- the second header (sort of used) */
    signature = read_signature(rpmfd);

    if (signature == NULL) {
        err(EXIT_FAILURE, "read_signature");
    }

    /* extract the RPM header -- the third header (used) */
    header = read_header(rpmfd);

    if (header == NULL) {
        err(EXIT_FAILURE, "read_header");
    }

    /* close the RPM after reading headers */
    if (close(rpmfd) == -1) {
        warn("close");
    }

    /* make a unique output directory name if we need to */
    if (output_dir == NULL) {
        tmp = get_nevra(h);
        assert(tmp != NULL);

        xasprintf(&candidate_path, "%s/%s", cwd, tmp);
        assert(candidate_path != NULL);

        dest_dir = abspath(candidate_path);

        free(candidate_path);
        free(tmp);
    } else {
        dest_dir = strdup(output_dir);
    }

    assert(dest_dir != NULL);

    /* create the output directory */
    if (mkdirp(dest_dir, mode) == -1) {
        err(EXIT_FAILURE, "mkdirp");
    }

    /* write out the header metadata */
    if (write_json_file(lead, dest_dir, OUTPUT_LEAD) != 0) {
        warn("write_json_file");
    }

    if (write_json_file(signature, dest_dir, OUTPUT_SIGNATURE) != 0) {
        warn("write_json_file");
    }

    if (write_json_file(header, dest_dir, OUTPUT_HEADER) != 0) {
        warn("write_json_file");
    }

    /* extract the RPM payload as an archive we can read in libarchive */
    if (chdir(dest_dir) == -1) {
        err(EXIT_FAILURE, "chdir");
    }

    payload_file = extract_payload(filename);

    if (payload_file == NULL) {
        errx(EXIT_FAILURE, "extract_payload");
    }

    if (chdir(cwd) == -1) {
        err(EXIT_FAILURE, "chdir");
    }

    /* unpack the RPM payload */
    xasprintf(&tmp, "%s/%s", dest_dir, PAYLOAD_SUBDIR);
    assert(tmp != NULL);

    if (mkdirp(tmp, mode) == -1) {
        err(EXIT_FAILURE, "mkdir");
    }

    if (unpack_archive(payload_file, tmp, false, v_flag) != 0) {
        err(EXIT_FAILURE, "unpack_archive");
    }

    if (unlink(payload_file) == -1) {
        err(EXIT_FAILURE, "unlink");
    }

    json_object_put(header);
    json_object_put(signature);
    json_object_put(lead);
    free(payload_file);
    free(tmp);
    free(dest_dir);
    headerFree(h);

    return;
}

/* Helper for create_rpm() that writes header data to the RPM */
static void
write_header(FILE *rpm, struct rpmhdr *hdr, struct rpmhdrinfo *hdrinfo, bool is_signature, struct json_object *data)
{
    uint32_t i = 0;
    uint32_t n = 0;
    uint32_t nentries = 0;
    uint32_t nbytes = 0;
    uint32_t hlen = 0;
    uint32_t padlen = 0;
    uint8_t padding[8] = {0};
    struct json_object *tags = NULL;
    struct json_object *entry = NULL;
    struct json_object *trailer_obj = NULL;
    struct json_object *value_obj = NULL;
    const char *trailer_value = NULL;
    uint8_t *trailer_data = NULL;
    size_t trailer_size = 0;
    int r = 0;
    bool has_trailer = false;

    assert(rpm != NULL);
    assert(hdr != NULL);
    assert(hdrinfo != NULL);

    if (fwrite(hdr, sizeof(*hdr), 1, rpm) != 1) {
        warn("fwrite");
    }

    /* write the signature index entries */
    nentries = ntohl(hdr->nentries);

    if (fwrite(hdrinfo->estart, sizeof(struct rpmhdrentry), nentries, rpm) != nentries) {
        warn("fwrite");
    }

    /* write the signature data */
    nbytes = ntohl(hdr->nbytes);
    n = nbytes;

    /*
     * in HEADER_SIGNATURES or HEADER_IMMUTABLE, actual data is 16
     * bytes less, because the trailer is counted in nbytes but
     * not written in the main data section
     */
    for (i = 0; i < nentries; i++) {
        if (ntohl(hdrinfo->estart[i].tag) == HEADER_SIGNATURES || ntohl(hdrinfo->estart[i].tag) == HEADER_IMMUTABLE) {
            /* trailer size */
            n -= 16;
            has_trailer = true;
            break;
        }
    }

    if (fwrite(hdrinfo->datastart, 1, n, rpm) != n) {
        warn("fwrite");
    }

    /* write the trailer if present (for both signature and header sections) */
    if (has_trailer && data != NULL) {
        if (json_object_object_get_ex(data, "tags", &tags) == 1) {
            for (i = 0; i < json_object_array_length(tags); i++) {
                entry = json_object_array_get_idx(tags, i);

                if (json_object_object_get_ex(entry, "trailer", &trailer_obj) == 1) {
                    if (json_object_object_get_ex(entry, "value", &value_obj) == 1) {
                        trailer_value = json_object_get_string(value_obj);
                        r = rpmBase64Decode(trailer_value, (void **) &trailer_data, &trailer_size);

                        if (r == 0 && trailer_size == 16) {
                            if (fwrite(trailer_data, 1, trailer_size, rpm) != trailer_size) {
                                warn("fwrite");
                            }
                        }

                        free(trailer_data);
                    }

                    break;
                }
            }
        }
    }

    /* write padding after signature data to align to 8-byte boundary */
    if (is_signature) {
        /* padding is based on index entries + full nbytes (including trailer) */
        hlen = nentries * sizeof(struct rpmhdrentry) + nbytes;
        padlen = (8 - (hlen % 8)) % 8;

        if (padlen > 0) {
            if (fwrite(padding, 1, padlen, rpm) != padlen) {
                warn("fwrite");
            }
        }
    }

    return;
}

/* Handler for -c mode (create) */
static void
create_rpm(const char *filename, const char *cwd, const char *input_dir, const int flags)
{
    FILE *rpm = NULL;
    struct stat sb;
    struct json_object *signature = NULL;
    struct json_object *header = NULL;
    struct rpmlead *rawlead = NULL;
    struct rpmhdr *sig = NULL;
    struct rpmhdrinfo *siginfo = NULL;
    struct rpmhdr *hdr = NULL;
    struct rpmhdrinfo *hdrinfo = NULL;

    assert(filename != NULL);
    assert(cwd != NULL);
    assert(input_dir != NULL);

    /* make sure the input directory exists */
    if (access(input_dir, flags)) {
        errx(EXIT_FAILURE, _("*** %s does not exist"), input_dir);
    }

    /* change to the input directory */
    if (chdir(input_dir) == -1) {
        err(EXIT_FAILURE, "chdir");
    }

    /* make sure we have the payload subdirectory */
    if (lstat(PAYLOAD_SUBDIR, &sb) == -1) {
        err(EXIT_FAILURE, "lstat");
    }

    if (!S_ISDIR(sb.st_mode)) {
        errx(EXIT_FAILURE, _("*** %s is not a directory"), PAYLOAD_SUBDIR);
    }

    /* read in signature.json and header.json */
    signature = read_json_file(OUTPUT_SIGNATURE);

    if (signature == NULL) {
        errx(EXIT_FAILURE, _("*** missing signature data"));
    }

    header = read_json_file(OUTPUT_HEADER);

    if (header == NULL) {
        errx(EXIT_FAILURE, _("*** missing header data"));
    }

    /* create the lead from header metadata */
    rawlead = create_lead(header);

    if (rawlead == NULL) {
        errx(EXIT_FAILURE, _("*** unable to construct RPM lead"));
    }

    /* create the signature */
    if (create_header(signature, &sig, &siginfo) == -1) {
        errx(EXIT_FAILURE, _("*** unable to construct RPM signature"));
    }

    /* create the header (the main header) */
    if (create_header(header, &hdr, &hdrinfo) == -1) {
        errx(EXIT_FAILURE, _("*** unable to construct RPM header"));
    }


/*

TODO:

* make sure the input directory exists, error if not
* check for the JSON metadata files (signature and header), error if not
* check for the payload subdirectory, error if not
* read in signature.json to object
* read in header.json to object
* create the lead using data from the header

* create the signature using data from signature.json
* create the header using data from header.json

* open a file and get a handle for the target filename
* write the lead to the output file
* write the signature to the output file
* write the header to the output file
- create the payload writer (use librpm) and yeet each payload file in to the output file
* close the output file

*/

    /* create an RPM for writing */
    rpm = fopen(filename, "wb");

    if (rpm == NULL) {
        err(EXIT_FAILURE, "fopen");
    }

    /* write the lead to the RPM */
    if (fwrite(rawlead, sizeof(*rawlead), 1, rpm) != 1) {
        warn("fwrite");
    }

    /* write the signature to the RPM */
    write_header(rpm, sig, siginfo, true, signature);

    /* write the header to the RPM */
    write_header(rpm, hdr, hdrinfo, false, header);


/* XXX */


    /* close the RPM */
    if (fclose(rpm) != 0) {
        warn("fclose");
    }

    /* back to the starting point and clean up */
    if (chdir(cwd) == -1) {
        err(EXIT_FAILURE, "chdir");
    }

    json_object_put(header);
    json_object_put(signature);
    free(sig);
    free(siginfo->estart);
    free(siginfo->datastart);
    free(siginfo);
    free(hdr);
    free(hdrinfo->estart);
    free(hdrinfo->datastart);
    free(hdrinfo);
    free(rawlead);

    return;
}

int
main(int argc, char **argv)
{
    int c = 0;
    int idx = 0;
    bool havefilename = false;
    char *filename = NULL;
    char *cwd = NULL;
    char *input_dir = NULL;
    char *output_dir = NULL;
    int flags = R_OK;
    char *opt = NULL;
    char *short_opts = "txcvf:O:V\?";
    struct option long_opts[] = {
        { "list", no_argument, 0, 't' },
        { "extract", no_argument, 0, 'x' },
        { "create", no_argument, 0, 'c' },
        { "verbose", no_argument, 0, 'v' },
        { "filename", required_argument, 0, 'f' },
        { "output", required_argument, 0, 'O' },
        { "version", no_argument, 0, 'V' },
        { "help", no_argument, 0, '?' },
        { 0, 0, 0, 0 }
    };

    /* Allow users to do "tarpm ... 2>&1 | tee" */
    setlinebuf(stdout);

    /* Set up the i18n environment */
    setlocale(LC_ALL, "");
    bindtextdomain("tarpm", "/usr/share/locale/");
    textdomain("tarpm");

    /* figure out where we actually are */
    cwd = getcwd(NULL, 0);

    if (cwd == NULL) {
        err(EXIT_FAILURE, "getcwd");
    }

    /* Parse command line options */
    while (1) {
        c = getopt_long(argc, argv, short_opts, long_opts, &idx);

        if (c == -1) {
            break;
        }

        switch (c) {
            case 't':
                if (c_flag || x_flag) {
                    errx(EXIT_FAILURE, _("*** only one of -t, -x, or -c may be specified"));
                }

                t_flag = true;
                flags = R_OK;
                break;
            case 'x':
                if (t_flag || c_flag) {
                    errx(EXIT_FAILURE, _("*** only one of -t, -x, or -c may be specified"));
                }

                x_flag = true;
                flags = R_OK;
                break;
            case 'c':
                if (t_flag || x_flag) {
                    errx(EXIT_FAILURE, _("*** only one of -t, -x, or -c may be specified"));
                }

                c_flag = true;
                flags = W_OK;
                break;
            case 'v':
                v_flag = true;
                break;
            case 'f':
                if (filename) {
                    errx(EXIT_FAILURE, _("*** -f already specified; only allowed once"));
                }

                if (!access(optarg, flags)) {
                    filename = realpath(optarg, NULL);
                } else {
                    if (optarg[0] == '/') {
                        filename = strdup(optarg);
                    } else {
                        filename = joinpath(cwd, optarg, NULL);
                    }
                }

                assert(filename != NULL);
                break;
            case 'O':
                if (output_dir) {
                    errx(EXIT_FAILURE, _("*** -O already specified; only allowed once"));
                }

                output_dir = abspath(optarg);
                break;
            case 'V':
                printf(_("%s version %s\n"), COMMAND_NAME, PACKAGE_VERSION);
                exit(EXIT_SUCCESS);
            case '?':
                usage();
                exit(EXIT_SUCCESS);
            default:
                errx(EXIT_FAILURE, _("*** ?? getopt returned character code 0%o ??"), c);
        }
    }

    /*
     * Handle the common short form syntax for tar(1) options, such as:
     *     tar tvf FILENAME.tar
     *     tar xvf FILENAME.tar
     *     tar cvf FILENAME.tar
     */
    if ((optind + 1) != argc) {
        /* process common short syntax options that may exist */
        opt = argv[optind];

        while (opt && *opt != '\0') {
            if (*opt == 't') {
                t_flag = true;
            } else if (*opt == 'c') {
                c_flag = true;
            } else if (*opt == 'x') {
                x_flag = true;
            } else if (*opt == 'v') {
                v_flag = true;
            } else if (*opt == 'f') {
                /* the filename must come after 'f' */
                if (filename) {
                    errx(EXIT_FAILURE, _("*** -f already specified; only allowed once"));
                }

                havefilename = true;
                break;
            }

            opt++;
        }

        /* pick up the 'f' filename if we don't have one */
        if (havefilename) {
            if ((t_flag || x_flag) && !access(argv[optind + 1], flags)) {
                /* for -t and -x, the filename specified needs to exist */
                filename = realpath(argv[optind + 1], NULL);
            } else {
                /* the other mode is -c which will create the named file */
                flags |= W_OK;

                if (argv[optind + 1][0] == '/') {
                    filename = strdup(argv[optind + 1]);
                } else {
                    filename = joinpath(cwd, argv[optind + 1], NULL);
                }
            }

            assert(filename != NULL);
            optind += 2;
        }
    }

    /* Pick up the input directory for -c */
    if (c_flag && optind < argc && !access(argv[optind], flags)) {
        input_dir = realpath(argv[optind], NULL);
        assert(input_dir != NULL);
    }

    /* Ensure we only have one of -t, -x, or -c */
    if ((t_flag + x_flag + c_flag) >= 2) {
        errx(EXIT_FAILURE, _("*** only one of -t, -x, or -c may be specified"));
    }

    /* Make sure we have minimal options specified */
    if (!t_flag && !x_flag && !c_flag) {
        errx(EXIT_FAILURE, _("*** must specify at least -t, -x, or -c"));
    }

    if (filename == NULL) {
        errx(EXIT_FAILURE, _("*** missing filename (-f) argument"));
    }

    /* Reset librpm */
    reset_librpm();

    /* Main operations begin here */
    if (t_flag) {
        /* XXX: can't list yet */
        printf(_("XXX: unable to list RPMs right now\n"));
    } else if (x_flag) {
        extract_rpm(filename, cwd, output_dir);
    } else if (c_flag) {
        create_rpm(filename, cwd, input_dir, flags);
    }

    /* Cleanup and exit */
    free(filename);
    free(cwd);
    free(output_dir);
    free(input_dir);

    return EXIT_SUCCESS;
}
