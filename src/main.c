/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <locale.h>
#include <libintl.h>
#include <err.h>

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
    char *short_opts = "txcvf:O:V?";
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

                if ((t_flag || x_flag) && !access(optarg, R_OK)) {
                    filename = realpath(optarg, NULL);
                } else {
                    if (optarg[0] == '/') {
                        filename = strdup(optarg);
                    } else {
                        filename = joinpath(cwd, optarg, NULL);
                    }
                }

                if (filename == NULL) {
                    errx(EXIT_FAILURE, _("*** unable to canonicalize %s"), optarg);
                }

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
        havefilename = false;

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
        if (havefilename && optind < argc) {
            if ((t_flag || x_flag) && !access(argv[optind + 1], flags)) {
                /* for -t and -x, the filename specified needs to exist */
                filename = realpath(argv[optind + 1], NULL);

                if (filename == NULL) {
                    err(EXIT_FAILURE, "realpath");
                }
            } else if (optind + 1 < argc) {
                /* the other mode is -c which will create the named file */
                if (argv[optind + 1][0] == '/') {
                    filename = strdup(argv[optind + 1]);

                    if (filename == NULL) {
                        err(EXIT_FAILURE, "strdup");
                    }
                } else {
                    filename = joinpath(cwd, argv[optind + 1], NULL);

                    if (filename == NULL) {
                        err(EXIT_FAILURE, "joinpath");
                    }
                }
            }

            if (filename == NULL) {
                errx(EXIT_FAILURE, _("*** missing filename for '-f' option"));
            }

            optind += 2;
        }
    }

    /* Pick up the input directory for -c */
    if (c_flag && optind < argc && !access(argv[optind], R_OK|X_OK)) {
        input_dir = realpath(argv[optind], NULL);

        if (input_dir == NULL) {
            err(EXIT_FAILURE, "realpath");
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

    /* Reset librpm */
    reset_librpm();

    /* Main operations begin here */
    if (t_flag) {
        list_rpm(filename);
    } else if (x_flag) {
        extract_rpm(filename, cwd, output_dir, v_flag);
    } else if (c_flag) {
        if (input_dir == NULL) {
            warnx(_("*** missing input directory, unable to create RPM"));
        } else {
            create_rpm(filename, cwd, input_dir);
        }
    }

    /* Cleanup and exit */
    free(filename);
    free(cwd);
    free(output_dir);
    free(input_dir);

    return EXIT_SUCCESS;
}
