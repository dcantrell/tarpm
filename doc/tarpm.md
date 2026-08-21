% TARPM(1)
% Dave Cantrell
% October 2024

# NAME

tarpm - list, create, modify, and extract RPM packages without using rpmbuild(1)

# SYNOPSIS

**tarpm** [**-?**]
**tarpm** [**-t**] [**-v**] [**-f** **RPMFILENAME**]
**tarpm** [**-x**] [**-v**] [**-f** **RPMFILENAME**]
**tarpm** [**-c**] [**-v**] [**-f** **RPMFILENAME**] [**DIRECTORY**]

# DESCRIPTION

**tarpm** is a command line tool for working with RPM package files.
The interface is intended to mimic he command line program tar(1), but
with necessary options specific to RPM files.  When you extract an RPM
file, a subdirectory is created matching the NEVRA of the package.
Inside this directory, you will find a subdirectory called **payload**
which contains all of the files and directories in the package.  At
the same level of the **payload** subdirectory you will find JSON
files containing the RPM metadata.  These files may be edited and the
contents of the **payload** subdirectory may be altered before
creating a new RPM package.

Keep in mind the source RPM and corresponding spec file are not used
by **tarpm**.  You are working with individual packages only.  Care
must be taken when modifying the metadata or the payload contents as
rpm(8) may refuse to work with your newly created package.

Some of the JSON metadata is for informational purposes only and may
not be modified.  See below for more details.

# OPTIONS

**-?**, **-\-help**
:    Display command usage information.

**-V**, **-\-version**
:    Display version information.

**-t**, **-\-list**
:    List the contents of the RPM package without metadata (cannot be used with **-c** or **-x**).

**-x**, **-\-extract**
:    Extract the named RPM package on the command line (cannot be used with **-t** or **-c**).

**-c**, **-\-create**
:    Create the named RPM package on the command line from the named
:    DIRECTORY contents (cannot be used with **-t** or **-x**).  The DIRECTORY must
:    contain RPM package metadata in JSON format, like what you see when
:    you extract an RPM.

**-v**, **-\-verbose**
:    Verbose progress output.

**-f** FILENAME, **-\-filename**=FILENAME
:    The name of the input or ouput RPM file.

**-O** DIRNAME, **-\-output**=DIRNAME
:    The name of the output directory when extracting an RPM.

Similar to tar(1), you may run options together, such as **-xvf** or
**-cvf**.  Likewise, the leading hyphen on combined options like this
is optional (in order to make **tarpm** more syntax compatible with
tar(1)).

# METADATA

RPM metadata is extracted by **tarpm** and written to JSON files.
When you create a package with **tarpm**, these JSON files must be
present in the DIRECTORY serving as the source for the package.
Fields that are read-only are marked with a read-only string set to
true.  The JSON files are:

**lead.json**
:    The RPM lead.  Obsolete, only shown for informational purposes.
:    **tarpm** recreates the lead when it creates a package.  The RPM
:    lead is the first 96 bytes of any RPM file.

**signature.json**
:    The RPM signature.  Marked with a magic number followed by the
:    number of records and the size of the data area.  Information is
:    stored in key=value manner but value may be an array.  The contents
:    of the RPM signature is read-only as **tarpm** will recalculate all
:    of the values when creating an RPM.

**header.json**
:    The RPM header.  Contains the metadata that most users are
:    expecting to find.  Things from the spec file, changelog
:    information, dependency information, and so on.  It is also marked
:    with a magic number followed by the number of records and the size
:    of the data area.  Information is stored in key=value manner but
:    value may be an array.

The contents of **lead.json** and **signature.json** files cannot be
modified, but might be interesting to look at or use for verification
purposes.

The contents of the **header.json** file can be modified.

# HEADER.JSON

The **header.json** file is the main metadata file that can be mostly
modified.  Fields that must remain untouched are magic, reserved, and
the Headerimmutable tags.  The JSON file is broken up in to arrays for
easier editing via programming languages or automation tools.  The
objective with the JSON structure is to avoid needing to parse
information like you may have to do reading filenames or spec files.

The arrays in **header.json** containing metadata:

**tags**
:    Information seen in spec files such as Name, Version, and Release.
:    You will also find things automatically added by rpmbuild if the
:    package was generated with rpmbuild.  The tags array is a series
:    of key=value dicts.  Each array entry contains a dict and each
:    dict contains tag, type, and value or file.  The tag corresponds
:    to an RPMTAG_ value from /usr/include/rpm/rpmtag.h.  The value is
:    the actual information.  The type corresponds to the types defined
:    in /usr/include/rpm/rpmtag.h.  If the type is an array, the value
:    will be an array of that type.
:
:    If an entry in the tags array contains a file key rather than value,
:    then that key corresponds to a text file in the extracted RPM
:    directory.  Some tags are written to text files for easier editing.
:    The Description, all scriptlets, and the Spec (for source RPMs only)
:    are all written to text files rather than placed directly in the
:    JSON file.

**dependencies**
:    RPM dependency information is written to the **dependencies** dict.
:    There is one dict entry (key) for each type of dependency which is
:    any of provides, requires, conflicts, obsoletes, recommends,
:    suggests, supplements, and enhances.  The value for each key is an
:    array of dicts where each array entry is a dict consisting of at
:    least name and optionally comparison, version, and sense_flags.
:    The structure looks like this:
:
:    "dependencies": {
:        "DEP_TYPE": [
:            {
:                "name": "VALUE",
:                "comparison": "VALUE",
:                "version": "VALUE",
:                "sense_flags": [
:                    "FLAG", ...
:                ]
:            }, ...
:    }
:
:    All dependencies must have at least a name.  This can be given
:    explicitly in a spec file and correspond to a file path or a package
:    name.  Or it can be autogenerated by rpmbuild.  A dependency may
:    also have a comparison which can be =, <, >, <=, or >=.  If it has
:    a comparison operator it also needs a version string.  The
:    sense_flags are dependency flags found in the header that do not
:    strictly map to a comparison operator.  **tarpm** converts these to
:    string representation and places them in the sense_flags array.  You
:    can find a list of these in /usr/include/rpm/rpmds.h as the
:    RPMSENSE_ macros.  Most of these are autogenerated by rpmbuild.
:
:    You may change these dependency values in the JSON metadata, but keep
:    in mind that **tarpm** does not validation or checking other than to
:    make sure the metadata types are correct.  It will put whatever you
:    tell it to put in the header.  If you wish to manually create an RPM,
:    consider using the /usr/lib/rpm/find-requires and
:    /usr/lib/rpm/find-provides tools to generate this information for the
:    software you are packaging.  That will align with what an RPM database
:    is expecting.

**changelog**
:    The changelog information from the spec file is stored as an array in
:    the **header.json** file.  It is an array of dicts and each dict
:    corresponds to one changelog block from the spec file.  Each dict
:    contains a timestamp key, a name key, and a text key.  The timestamp
:    value is the date you would see on the changelog line, such as
:    "Tue Jul 21 2026".  Internally these are stored as time_t values in
:    the header, but tarpm converts them to the human readable format.
:    The name value is a string containing everything after the date on the
:    changelog line.  Usually this is the packager's name, email address, a
:    hyphen, and the version-release value of that update (but without the
:    dist tag), but this is just a common convention.  The text value is an
:    array of strings containing everything for that changelog entry.  The
:    actual value in the header is one long string, but tarpm splits that
:    on newlines and presents it as an array in the JSON file for easier
:    editing.  The entire array is joined together when constructing an RPM.

# PAYLOAD

The **payload** subdirectory contains the RPM payload contents.  You
may modify this subdirectory by adding or removing files, changing
permissions, timestamps, and other things to modify the contents of
the package.  **tarpm** will update the file metadata in the header
based on the contents of this subdirectory, so it is not necessary to
manually update the **files** array in **header.json**

# SEE ALSO

**rpmbuild**(1), **tar**(1), **rpm2cpio**(8)
