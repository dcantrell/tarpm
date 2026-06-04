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
#include <rpm/header.h>

#include "tarpm.h"

static void
usage(void)
{
    printf(_("RPM extraction and creation utility\n"));
    printf(_("Usage: %s [OPTIONS] [.rpm file] [directory]\n"), COMMAND_NAME);
    printf(_("Options:\n"));
    printf(_("    -t, --list                        List RPM payload contents\n"));
    printf(_("    -x, --extract                     Extract RPM file\n"));
    printf(_("    -v, --verbose                     Verbose progress output\n"));
    printf(_("    -f FILENAME, --filename=FILENAME  Use FILENAME as input or output\n"));
    printf(_("    -O DIRNAME, --output=DIRNAME      Use DIRNAME as output directory\n"));
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
    bool t_flag = false;
    bool x_flag = false;
    bool c_flag = false;
    bool v_flag = false;
    bool havefilename = false;
    char *tmp = NULL;
    char *payload_file = NULL;
    char *filename = NULL;
    char *cwd = NULL;
    char *candidate_path = NULL;
    char *output_dir = NULL;
    int flags = R_OK;
    int mode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
    int rpmfd = 0;
    struct json_object *lead = NULL;
    struct json_object *signature = NULL;
    struct json_object *header = NULL;
    Header h;
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

                filename = realpath(optarg, NULL);
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
        if (havefilename && !access(argv[optind + 1], flags)) {
            filename = realpath(argv[optind + 1], NULL);
        }
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
    if (t_flag) {
        /* XXX: can't list yet */
        printf(_("XXX: unable to list RPMs right now\n"));

        if (v_flag) {
            return EXIT_SUCCESS;
        }
    } else if (x_flag) {
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

        /* extract the RPM lead -- the first header (unused) */
        lead = read_lead_from_rpm(rpmfd);

        if (lead == NULL) {
            err(EXIT_FAILURE, "read_lead_from_rpm");
        }

        /* extract the RPM signature -- the second header (sort of used) */
        signature = read_signature_from_rpm(rpmfd);

        if (signature == NULL) {
            err(EXIT_FAILURE, "read_signature_from_rpm");
        }

        /* extract the RPM header -- the third header (used) */
        header = read_header_from_rpm(rpmfd);

        if (header == NULL) {
            err(EXIT_FAILURE, "read_header_from_rpm");
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

            output_dir = abspath(candidate_path);
            assert(output_dir != NULL);

            free(candidate_path);
            free(tmp);
        }

        /* create the output directory */
        if (mkdirp(output_dir, mode) == -1) {
            return EXIT_FAILURE;
        }

        /* write out the header metadata */
        if (write_json_file(lead, output_dir, OUTPUT_LEAD) != 0) {
            warn("write_json_file");
        }

        if (write_json_file(signature, output_dir, OUTPUT_SIGNATURE) != 0) {
            warn("write_json_file");
        }

        if (write_json_file(header, output_dir, OUTPUT_HEADER) != 0) {
            warn("write_json_file");
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

        if (unpack_archive(payload_file, tmp, true, v_flag) != 0) {
            err(EXIT_FAILURE, "unpack_archive");
        }

        if (unlink(payload_file) == -1) {
            err(EXIT_FAILURE, "unlink");
        }

        free_json(header);
        free_json(signature);
        free_json(lead);
        free(payload_file);
        free(tmp);
        free(output_dir);
        headerFree(h);
    } else if (c_flag) {
        /* XXX: can't create yet */
        printf(_("XXX: unable to create RPMs right now\n"));

        if (v_flag) {
            return EXIT_SUCCESS;
        }
    }

    /* Cleanup and exit */
    free(filename);
    free(cwd);

    return EXIT_SUCCESS;
}
