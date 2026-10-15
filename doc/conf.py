# Configuration file for the Sphinx documentation builder.
#
# This file only contains a selection of the most common options. For a full
# list see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

# -- Path setup --------------------------------------------------------------

# If extensions (or modules to document with autodoc) are in another directory,
# add these directories to sys.path here. If the directory is relative to the
# documentation root, use os.path.abspath to make it absolute, like shown here.
#
# import os
# import sys
# sys.path.insert(0, os.path.abspath('.'))
import os
import subprocess

# Where doxygen writes.  meson has its own copy of this under the
# meson build directory and passes it in with -D, so this only comes
# in to play when sphinx runs on its own.  Either way the output goes
# to a build directory rather than back in to the source tree.
doc_dir = os.path.abspath(os.path.dirname(__file__))
doxygen_dir = os.path.join(doc_dir, "_build", "doxygen")

# Read the Docs has no meson, so do there what doc/meson.build does
# here: fill in the paths Doxyfile.in leaves open and run doxygen so
# breathe has XML to read.
if os.environ.get("READTHEDOCS", None) == "True":
    os.makedirs(doxygen_dir, exist_ok=True)

    f = open(os.path.join(doc_dir, "Doxyfile.in"))
    doxyfile = f.read()
    f.close()

    doxyfile = doxyfile.replace(
        "@INPUT_DIR@", os.path.join(os.path.dirname(doc_dir), "include")
    )
    doxyfile = doxyfile.replace("@OUTPUT_DIR@", doxygen_dir)

    generated = os.path.join(doxygen_dir, "Doxyfile")
    f = open(generated, "w")
    f.write(doxyfile)
    f.close()

    subprocess.call(["doxygen", generated])


# -- Project information -----------------------------------------------------

project = "tarpm"
copyright = "2026, Dave Cantrell"
author = "Dave Cantrell"


# -- General configuration ---------------------------------------------------

# Add any Sphinx extension module names here, as strings. They can be
# extensions coming with Sphinx (named 'sphinx.ext.*') or your custom
# ones.
extensions = ["breathe"]

# Where doxygen leaves its XML, which is OUTPUT_DIRECTORY plus
# XML_OUTPUT from the Doxyfile.  Without these breathe has nothing to
# read and every breathe directive fails.  A meson build overrides
# this with -D so breathe reads the copy under the build directory.
breathe_projects = {"tarpm": os.path.join(doxygen_dir, "xml")}
breathe_default_project = "tarpm"

# Add any paths that contain templates here, relative to this directory.
templates_path = ["_templates"]

# List of patterns, relative to source directory, that match files and
# directories to ignore when looking for source files.
# This pattern also affects html_static_path and html_extra_path.
exclude_patterns = ["_build", "Thumbs.db", ".DS_Store"]


# -- Options for HTML output -------------------------------------------------

# The theme to use for HTML and HTML Help pages.  See the documentation for
# a list of builtin themes.
#
html_theme = "sphinx_rtd_theme"

# Add any paths that contain custom static files (such as style sheets) here,
# relative to this directory. They are copied after the builtin static files,
# so a file named "default.css" will overwrite the builtin "default.css".
# We have none, and naming a directory that is not there is a warning.
html_static_path = []
