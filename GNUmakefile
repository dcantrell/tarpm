# Commands used during build (override in the calling environment for
# non-standard executable names or non-standard paths to said
# commands).
MESON    ?= meson
NINJA    ?= ninja
REALPATH ?= realpath
GREP     ?= grep
CUT      ?= cut
PYTHON   ?= python3

# Where to build
MESON_BUILD_DIR = build
topdir := $(shell $(REALPATH) $(dir $(lastword $(MAKEFILE_LIST))))

# Project information (may be an easier way to get this from meson)
PROJECT_NAME = $(shell $(GREP) ^project $(topdir)/meson.build | $(CUT) -d "'" -f 2)
PROJECT_VERSION = $(shell $(GREP) version $(topdir)/meson.build | $(GREP) -E ',$$' | $(CUT) -d "'" -f 2)

# Take additional command line argument as a positional parameter for
# the Makefile target
TARGET_ARG = `arg="$(filter-out $@,$(MAKECMDGOALS))" && echo $${arg:-${1}}`

# regexp of email addresses of primary authors on the project
PRIMARY_AUTHORS = dcantrell@burdell.org

# full path to release tarball and detached signature
# (this comes from a 'make srpm')
RELEASED_TARBALL = $(PROJECT_NAME)-$(PROJECT_VERSION).tar.gz
RELEASED_TARBALL_ASC = $(RELEASED_TARBALL).asc

all: setup
	$(NINJA) -C $(MESON_BUILD_DIR) -v

setup:
	$(MESON) setup $(MESON_BUILD_DIR) $(MESON_OPTIONS)

debug: setup-debug
	$(NINJA) -C $(MESON_BUILD_DIR) -v

setup-debug:
	$(MESON) setup $(MESON_BUILD_DIR) --werror --buildtype=debug $(MESON_OPTIONS)

update-pot: setup
	find src -type f -name "*.c" > po/POTFILES.new
	find include -type f -name "*.h" >> po/POTFILES.new
	sort -u po/POTFILES.new > po/POTFILES
	rm -f po/POTFILES.new
	$(NINJA) -C $(MESON_BUILD_DIR) $(PROJECT_NAME)-pot

# To keep intermediate files and results files for each test case, set
# KEEP=y (or to any value) in the calling environment when you run
# 'make check'.  For example: make check KEEP=y
check: setup
	@test_name="$(call TARGET_ARG,)" ; \
	if [ -z "$${test_name}" ]; then \
		env $(MESON) test -C $(MESON_BUILD_DIR) -v ; \
	else \
		test_script="test_$${test_name}.py" ; \
		if [ ! -f "$(topdir)/test/integration/$${test_script}" ]; then \
			echo "*** test/$${test_script} does not exist." >&2 ; \
			exit 1 ; \
		fi ; \
		env TARPM=$(topdir)/build/src/tarpm \
		$(PYTHON) -Bm unittest discover -v $(topdir)/test/integration/ $${test_script} ; \
	fi

flake8:
	$(PYTHON) -m flake8

black:
	$(PYTHON) -m black --check --diff $(topdir)/test/

clean:
	-rm -rf $(MESON_BUILD_DIR)

authors:
	echo "Primary Authors" > AUTHORS.md
	echo "===============" >> AUTHORS.md
	echo >> AUTHORS.md
	git log --pretty="%an <%ae>" | sort -u | grep -E "$(PRIMARY_AUTHORS)" | sed -e 's|^|- |g' | sed G >> AUTHORS.md
	git log --pretty="%aN <%aE>" | sort -u | grep -vE "$(PRIMARY_AUTHORS)" | sed -e 's|^|- |g' | sed G >> AUTHORS.contrib
	if [ -s AUTHORS.contrib ]; then \
		echo >> AUTHORS.md ; \
		echo "Contributors" >> AUTHORS.md ; \
		echo "============" >> AUTHORS.md ; \
		cat AUTHORS.contrib >> AUTHORS.md ; \
	fi
	rm -f AUTHORS.contrib
	head -n $$(($$(wc -l < AUTHORS.md) - 1)) AUTHORS.md > AUTHORS.md.new
	mv AUTHORS.md.new AUTHORS.md

help:
	@echo "$(PROJECT_NAME) helper Makefile"
	@echo "The source tree uses meson(1) for building and testing, but this Makefile"
	@echo "is intended as a simple helper for the common steps."
	@echo
	@echo "    all               Default target, setup tree to build and build"
	@echo "    debug             Setup tree for debug build and build"
	@echo "    setup             Run '$(MESON) setup $(MESON_BUILD_DIR)'"
	@echo "    setup-debug       The counterpart to 'setup'; called by 'debug'"
	@echo "    check             Run '$(MESON) test -C $(MESON_BUILD_DIR) -v'"
	@echo "    update-pot        Update po/POTFILES and po/$(PROJECT_NAME).pot"
	@echo "    clean             Run 'rm -rf $(MESON_BUILD_DIR)'"
	@echo "    authors           Generate a new AUTHORS.md file"
	@echo
	@echo "To build:"
	@echo "    make"
	@echo
	@echo "To perform syntax and style checks:"
	@echo "    make flake8       Run Python flake8 checks on all Python files"
	@echo "    make black        Run Python black checks on all Python files"
	@echo
	@echo "To run the test suite:"
	@echo "    make check"
	@echo
	@echo "To run a single test script (e.g., test_options.py):"
	@echo "    make check options"

# Quiet errors about target arguments not being targets
%:
	@true
