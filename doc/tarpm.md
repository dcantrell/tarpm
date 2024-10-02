% TARPM(1)
% David Cantrell
% October 2024

# NAME

tarpm - create, modify, and extract binary RPM packages without using rpmbuild(1)

# SYNOPSIS

**tarpm** [**-?**]
**tarpm** [**-x**] [**-v**] [**-f** **RPMFILENAME**]
**tarpm** [**-c**] [**-v**] [**-f** **RPMFILENAME**] [**DIRECTORY**]

# DESCRIPTION

**tarpm** is a command line tool for working with built RPM packages.
The interface is intended to work like tar(1).  When you extract an
RPM package, a subdirectory is created matching the NEVRA of the
package.  Inside this directory, you will find a subdirectory called
**payload** which contains all of the files and directories in the
package.  At the same level of the **payload** subdirectory you will
find JSON files containing the RPM metadata.  These files may be
edited and the contents of the **payload** subdirectory may be altered
before creating a new RPM package.

Keep in mind the source RPM and corresponding spec file are not used
by **tarpm**.  You are working with individual packages only.  Care
must be taken when modifying the metadata or payload as rpm(8) may
refuse to work with your newly generated package.

Some of the JSON metadata is for informational purposes only and may
not be modified.

# OPTIONS

**-?**, **-\-help**
:    Display command usage information.

**-V**, **-\-version**
:    Display version information.

**-x**, **-\-extract**
:    Extract the named RPM file on the command line (cannot be used with **-c**).

**-c**, **-\-create**
:    Create the named RPM files on the command line from the named
:    DIRECTORY contents (cannot be used with **-x**).  The DIRECTORY must
:    contain RPM package metadata in JSON format, like what you see when
:    you extract an RPM.

**-v**, **-\-verbose**
:    Verbose progress output.

**-f**, **-\-filename**
:    The name of the input or ouput RPM file.

Similar to tar(1), you may run options together, such as **-xvf** or
**-cvf**.  Likewise, the leading hyphen on combined options like this
is optional (in order to make **tarpm** more syntax compatible with
tar(1)).

# METADATA

RPM metadata is extracted by **tarpm** and written to JSON files.
When you create a package with **tarpm**, these JSON files must be
present in the DIRECTORY serving as the source for the package.  The
JSON files are:

**lead.json**
:    The RPM lead.  Obsolete, only shown for informational purposes.
:    **tarpm** recreates the lead when it creates a package.

**signature.json**
:    The RPM signature.  Marked with a magic number followed by the
:    number of records and the size of the data area.  Information is
:    stored in key=value manner but value may be an array.

**header.json**
:    The RPM header.  Contains the metadata that most users are
:    expecting to find.  Things from the spec file, changelog
:    information, dependency information, and so on.  It is also marked
:    with a magic number followed by the number of records and the size
:    of the data area.  Informatikon is stored in key=value manner but
:    value may be an array.

The contents of **lead.json** cannot be modified, but it might be
interesting to look at.  The **signature.json** file can be modified,
but care must be taken.  Most users will want to modify the
**header.json** metadata.  But do be careful modifying any of the
metadata files.

# SEE ALSO

**rpmbuild**(1), **tar**(1), **rpm2cpio**(8)
