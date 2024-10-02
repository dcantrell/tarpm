/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: Apache-2.0
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
#include <rpm/header.h>

#include "tarpm.h"

static void
usage(void)
{
    printf(_("Binary RPM extraction and creation utility\n"));
    printf(_("Usage: %s [OPTIONS] [binary .rpm file]\n"), COMMAND_NAME);
    printf(_("Options:\n"));
    printf(_("    -x, --extract                     Extract binary RPM file\n"));
    printf(_("    -v, --verbose                     Verbose progress output\n"));
    printf(_("    -f FILENAME, --filename=FILENAME  Use FILENAME as input or output\n"));
    printf(_("    -V, --version                     Display version information\n"));
    printf(_("    -?, --help                        Display this screen\n"));
    printf(_("See the %s(1) man page for more information.\n"), COMMAND_NAME);

    return;
}

int
main(int argc, char **argv)
{
    int c = 0;
    int idx = 0;
    bool extract = false;
    bool create = false;
    bool verbose = false;
    bool havefilename = false;
    char *tmp = NULL;
    char *payload_file = NULL;
    char *filename = NULL;
    char *cwd = NULL;
    char *output_dir = NULL;
    int flags = R_OK;
    int mode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
    int rpmfd = 0;
    Header h;
    char *opt = NULL;
    char *short_opts = "xcvf:V\?";
    struct option long_opts[] = {
        { "extract", no_argument, 0, 'x' },
        { "create", no_argument, 0, 'c' },
        { "verbose", no_argument, 0, 'v' },
        { "filename", required_argument, 0, 'f' },
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

    /* Parse command line options */
    while (1) {
        c = getopt_long(argc, argv, short_opts, long_opts, &idx);

        if (c == -1) {
            break;
        }

        switch (c) {
            case 'x':
                if (create) {
                    errx(EXIT_FAILURE, _("*** -x and -c specified together; unsupported"));
                }

                extract = true;
                flags = R_OK;
                break;
            case 'c':
                if (extract) {
                    errx(EXIT_FAILURE, _("*** -x and -c specified together; unsupported"));
                }

                create = true;
                flags = W_OK;
                break;
            case 'v':
                verbose = true;
                break;
            case 'f':
                if (filename) {
                    errx(EXIT_FAILURE, _("*** -f already specified; only allowed once"));
                }

                filename = realpath(optarg, NULL);
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
     *     tar xvf FILENAME.tar
     *     tar cvf FILENAME.tar
     */
    if ((optind + 1) != argc) {
        /* process common short syntax options that may exist */
        opt = argv[optind];

        while (opt && *opt != '\0') {
            if (*opt == 'c') {
                create = true;
            } else if (*opt == 'x') {
                extract = true;
            } else if (*opt == 'v') {
                verbose = true;
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
        if (havefilename && !access(argv[optind + 1], flags)) {
            filename = realpath(argv[optind + 1], NULL);
        }
    }

    /* Make sure we have minimal options specified */
    if (!extract && !create) {
        errx(EXIT_FAILURE, _("*** must specify at least -x or -c"));
    }

    if (filename == NULL) {
        errx(EXIT_FAILURE, _("*** missing filename (-f) argument"));
    }

    /* figure out where we actually are */
    cwd = getcwd(NULL, 0);

    if (cwd == NULL) {
        err(EXIT_FAILURE, "getcwd");
    }

    /* Initialize librpm */
    if (init_librpm() != RPMRC_OK) {
        errx(EXIT_FAILURE, _("*** unable to read RPM configuration"));
    }

    /* Main operations begin here */
    if (extract) {
        /* validate the specified file is an RPM */
        h = get_rpm_header(filename);

        if (h == NULL) {
            errx(EXIT_FAILURE, _("*** %s is not a valid RPM"), filename);
        }

        /* open the RPM file (this handle will be passed around) */
        rpmfd = open(filename, O_RDONLY);

        if (rpmfd == -1) {
            err(EXIT_FAILURE, "open");
        }

        /* make a unique output directory name if we need to */
        if (output_dir == NULL) {
            /*
             * XXX: this should build a path and use abspath() from rpminspect, but that can come later
             */
            tmp = get_nevra(h);
            assert(tmp != NULL);

            xasprintf(&output_dir, "%s/%s", cwd, tmp);
            assert(output_dir != NULL);

            free(tmp);
        }

        /* create the output directory */
        if (mkdirp(output_dir, mode) == -1) {
            return EXIT_FAILURE;
        }

        /* extract the RPM lead -- the first header (unused) */
        if (extract_lead(rpmfd, output_dir) == -1) {
            err(EXIT_FAILURE, "extract_lead");
        }

        /* extract the RPM signature -- the second header (sort of used) */
        if (extract_signature(rpmfd, output_dir) == -1) {
            err(EXIT_FAILURE, "extract_signature");
        }






//        /* add header data to JSON structure */
//        if (extract_header(h, output_dir) == -1) {
//            err(EXIT_FAILURE, "extract_header");
//        }

        /* close the RPM after reading headers */
        if (close(rpmfd) == -1) {
            warn("close");
        }

        /* extract the RPM payload as an archive we can read in libarchive */
        if (chdir(output_dir) == -1) {
            err(EXIT_FAILURE, "chdir");
        }

        payload_file = extract_rpm_payload(filename);

        if (payload_file == NULL) {
            errx(EXIT_FAILURE, "extract_rpm_payload");
        }

        if (chdir(cwd) == -1) {
            err(EXIT_FAILURE, "chdir");
        }

        /* unpack the RPM payload */
        xasprintf(&tmp, "%s/%s", output_dir, PAYLOAD_SUBDIR);
        assert(tmp != NULL);

        if (mkdirp(tmp, mode) == -1) {
            return EXIT_FAILURE;
        }

        if (unpack_archive(payload_file, tmp, true, verbose) != 0) {
            err(EXIT_FAILURE, "unpack_archive");
        }

        if (unlink(payload_file) == -1) {
            err(EXIT_FAILURE, "unlink");
        }

        free(payload_file);
        free(tmp);
        free(output_dir);
        headerFree(h);
    } else if (create) {
        /* XXX: can't create yet */
        printf(_("XXX: unable to create RPMs right now\n"));

        if (verbose) {
            return EXIT_SUCCESS;
        }
    }

    /* Cleanup and exit */
    free(filename);
    free(cwd);

    return EXIT_SUCCESS;
}
