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
#include <sys/stat.h>

#include "tarpm.h"

/* Static variables */
static bool t_flag = false;
static bool x_flag = false;
static bool c_flag = false;
static bool v_flag = false;

/*
 * Work out where one of the JSON metadata files lives.  A single
 * hyphen means stdout and is kept as it is.  A relative path is taken
 * from the current directory.  A path that ends with a slash or names
 * a directory we already have gets the usual filename added to it.
 * Caller must free the returned string.
 */
static char *
metadata_path(const char *path, const char *name)
{
    char *r = NULL;
    char *full = NULL;
    size_t len = 0;
    struct stat sb;

    if (path == NULL || name == NULL) {
        return NULL;
    }

    /* the JSON goes to stdout rather than a file */
    if (!strcmp(path, OUTPUT_STDOUT)) {
        r = strdup(path);

        if (r == NULL) {
            err(EXIT_FAILURE, "strdup");
        }

        return r;
    }

    full = abspath(path);

    if (full == NULL) {
        errx(EXIT_FAILURE, _("*** unable to canonicalize %s"), path);
    }

    len = strlen(path);

    if ((len > 0 && path[len - 1] == '/') || (stat(full, &sb) == 0 && S_ISDIR(sb.st_mode))) {
        r = joinpath(full, name, NULL);
        free(full);
    } else {
        r = full;
    }

    if (r == NULL) {
        errx(EXIT_FAILURE, _("*** unable to canonicalize %s"), path);
    }

    return r;
}

/*
 * Make the directory a JSON metadata file goes in if it is not there
 * already.  Nothing to make when the JSON goes to stdout.  Exits if
 * we cannot create it.
 */
static void
make_metadata_dir(const char *path)
{
    int mode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
    char *dir = NULL;

    if (path == NULL || !strcmp(path, OUTPUT_STDOUT)) {
        return;
    }

    dir = dir_name(path);

    if (mkdirp(dir, mode) == -1) {
        errx(EXIT_FAILURE, _("*** unable to create %s"), dir);
    }

    free(dir);

    return;
}

static void
usage(void)
{
    printf(_("RPM extraction and creation utility\n"));
    printf(_("Usage: %s [OPTIONS] [.rpm file] [directory]\n"), COMMAND_NAME);
    printf(_("Options:\n"));
    printf(_("    -t, --list                  List RPM payload contents\n"));
    printf(_("    -c, --create                Create an RPM file\n"));
    printf(_("    -x, --extract               Extract RPM file\n"));
    printf(_("    -v, --verbose               Verbose progress output\n"));
    printf(_("    -f FILE, --filename=FILE    Use FILE as input or output RPM file\n"));
    printf(_("    -O DIR, --output=DIR        Use DIR as output directory\n"));
    printf(_("    -L FILE, --lead=FILE        Use FILE for lead.json\n"));
    printf(_("    -S FILE, --signature=FILE   Use FILE for signature.json\n"));
    printf(_("    -H FILE, --header=FILE      Use FILE for header.json\n"));
    printf(_("    -P DIR, --payload=DIR       Use DIR for the payload tree\n"));
    printf(_("    -F NUM, --format=NUM        Write an RPM format NUM package (%d or %d)\n"), RPM_FORMAT_V4, RPM_FORMAT_V6);
    printf(_("    -m, --payload-mtime         Take file timestamps from the payload tree\n"));
    printf(_("    -u, --payload-user          Take file owners from the payload tree\n"));
    printf(_("    -g, --payload-group         Take file groups from the payload tree\n"));
    printf(_("    -l, --payload-linkto        Take symlink targets from the payload tree\n"));
    printf(_("    -a, --payload-all           Take all of the above from the payload tree\n"));
    printf(_("    -V, --version               Display version information\n"));
    printf(_("    -?, --help                  Display this screen\n"));
    printf(_("A FILE of \"%s\" with -L, -S, or -H writes the JSON to standard output.\n"), OUTPUT_STDOUT);
    printf(_("It may only be used when extracting an RPM.\n"));
    printf(_("The -m, -u, -g, -l, -a, and -F options may only be used when creating an RPM.\n"));
    printf(_("The default format is %d.  The format shapes the lead, the signature\n"), RPM_FORMAT_DEFAULT);
    printf(_("header, the payload digest tags in the main header, and the payload\n"));
    printf(_("compressor.  The signature header is generated, so -S is ignored when\n"));
    printf(_("creating an RPM.\n"));
    printf(_("See the %s(1) man page for more information.\n"), COMMAND_NAME);

    return;
}

int
main(int argc, char **argv)
{
    int r = EXIT_SUCCESS;
    int c = 0;
    int idx = 0;
    bool havefilename = false;
    bool haveformat = false;
    char *filename = NULL;
    char *cwd = NULL;
    char *input_dir = NULL;
    char *output_dir = NULL;
    int flags = R_OK;
    char *opt = NULL;
    struct json_paths paths;
    char *short_opts = "txcvf:O:L:S:H:P:F:muglaV?";
    struct option long_opts[] = {
        { "list", no_argument, 0, 't' },
        { "extract", no_argument, 0, 'x' },
        { "create", no_argument, 0, 'c' },
        { "verbose", no_argument, 0, 'v' },
        { "filename", required_argument, 0, 'f' },
        { "output", required_argument, 0, 'O' },
        { "lead", required_argument, 0, 'L' },
        { "signature", required_argument, 0, 'S' },
        { "header", required_argument, 0, 'H' },
        { "payload", required_argument, 0, 'P' },
        { "format", required_argument, 0, 'F' },
        { "payload-mtime", no_argument, 0, 'm' },
        { "payload-user", no_argument, 0, 'u' },
        { "payload-group", no_argument, 0, 'g' },
        { "payload-linkto", no_argument, 0, 'l' },
        { "payload-all", no_argument, 0, 'a' },
        { "version", no_argument, 0, 'V' },
        { "help", no_argument, 0, '?' },
        { 0, 0, 0, 0 }
    };

    /* the metadata and the payload land in the working directory by default */
    paths.lead = NULL;
    paths.signature = NULL;
    paths.header = NULL;
    paths.payload = NULL;

    /* Allow users to do "tarpm ... 2>&1 | tee" */
    setlinebuf(stdout);

    /* Set up the i18n environment */
    setlocale(LC_ALL, "");
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

                if ((t_flag || x_flag) && access(optarg, R_OK) == 0) {
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
            case 'L':
                if (paths.lead) {
                    errx(EXIT_FAILURE, _("*** -L already specified; only allowed once"));
                }

                paths.lead = metadata_path(optarg, OUTPUT_LEAD);
                break;
            case 'S':
                if (paths.signature) {
                    errx(EXIT_FAILURE, _("*** -S already specified; only allowed once"));
                }

                paths.signature = metadata_path(optarg, OUTPUT_SIGNATURE);
                break;
            case 'H':
                if (paths.header) {
                    errx(EXIT_FAILURE, _("*** -H already specified; only allowed once"));
                }

                paths.header = metadata_path(optarg, OUTPUT_HEADER);
                break;
            case 'P':
                if (paths.payload) {
                    errx(EXIT_FAILURE, _("*** -P already specified; only allowed once"));
                }

                paths.payload = abspath(optarg);

                if (paths.payload == NULL) {
                    errx(EXIT_FAILURE, _("*** unable to canonicalize %s"), optarg);
                }

                break;
            case 'F':
                if (haveformat) {
                    errx(EXIT_FAILURE, _("*** -F already specified; only allowed once"));
                }

                if (!strcmp(optarg, "4")) {
                    rpmformat = RPM_FORMAT_V4;
                } else if (!strcmp(optarg, "6")) {
#ifndef _HAS_RPMFORMAT_TAGS
                    /* librpm has to know the v6 tag names */
                    errx(EXIT_FAILURE, _("*** -F %d needs rpm 6.0.0 or newer"), RPM_FORMAT_V6);
#endif
                    rpmformat = RPM_FORMAT_V6;
                } else {
                    errx(EXIT_FAILURE, _("*** -F must be %d or %d"), RPM_FORMAT_V4, RPM_FORMAT_V6);
                }

                haveformat = true;
                break;
            case 'm':
                payload_overrides |= PAYLOAD_OVERRIDE_MTIME;
                break;
            case 'u':
                payload_overrides |= PAYLOAD_OVERRIDE_USER;
                break;
            case 'g':
                payload_overrides |= PAYLOAD_OVERRIDE_GROUP;
                break;
            case 'l':
                payload_overrides |= PAYLOAD_OVERRIDE_LINKTO;
                break;
            case 'a':
                payload_overrides |= PAYLOAD_OVERRIDE_ALL;
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
            } else if (*opt == 'm') {
                payload_overrides |= PAYLOAD_OVERRIDE_MTIME;
            } else if (*opt == 'u') {
                payload_overrides |= PAYLOAD_OVERRIDE_USER;
            } else if (*opt == 'g') {
                payload_overrides |= PAYLOAD_OVERRIDE_GROUP;
            } else if (*opt == 'l') {
                payload_overrides |= PAYLOAD_OVERRIDE_LINKTO;
            } else if (*opt == 'a') {
                payload_overrides |= PAYLOAD_OVERRIDE_ALL;
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

    /* the payload metadata options only mean something when we create an RPM */
    if (!c_flag && payload_overrides != PAYLOAD_OVERRIDE_NONE) {
        errx(EXIT_FAILURE, _("*** -m, -u, -g, -l, and -a may only be used when creating an RPM"));
    }

    /* the format option only means something when we create an RPM */
    if (!c_flag && haveformat) {
        errx(EXIT_FAILURE, _("*** -F may only be used when creating an RPM"));
    }

    /* Creating an RPM reads the JSON metadata from files, not stdin */
    if (c_flag && ((paths.lead != NULL && !strcmp(paths.lead, OUTPUT_STDOUT)) || (paths.signature != NULL && !strcmp(paths.signature, OUTPUT_STDOUT)) || (paths.header != NULL && !strcmp(paths.header, OUTPUT_STDOUT)))) {
        errx(EXIT_FAILURE, _("*** \"%s\" may not be used when creating an RPM"), OUTPUT_STDOUT);
    }

    /* we generate the signature header, so -S has nothing to say here */
    if (c_flag && paths.signature != NULL) {
        warnx(_("*** -S is ignored when creating an RPM"));
    }

    /* Make the directories the JSON metadata files are written to */
    if (x_flag) {
        make_metadata_dir(paths.lead);
        make_metadata_dir(paths.signature);
        make_metadata_dir(paths.header);
    }

    /* Reset librpm */
    reset_librpm();

    /* Main operations begin here */
    if (t_flag) {
        list_rpm(filename);
    } else if (x_flag) {
        if (extract_rpm(filename, cwd, output_dir, &paths, v_flag)) {
            r = EXIT_FAILURE;
        }
    } else if (c_flag) {
        if (input_dir == NULL) {
            warnx(_("*** missing input directory, unable to create RPM"));
        } else {
            if (create_rpm(filename, cwd, input_dir, &paths)) {
                r = EXIT_FAILURE;
            }
        }
    }

    /* Cleanup and exit */
    close_magic();
    free(filename);
    free(cwd);
    free(output_dir);
    free(input_dir);
    free(paths.lead);
    free(paths.signature);
    free(paths.header);
    free(paths.payload);

    return r;
}
