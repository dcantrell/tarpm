# Commands used during build (override in the calling environment for
# non-standard executable names or non-standard paths to said
# commands).
MESON    ?= meson
NINJA    ?= ninja
REALPATH ?= realpath
GREP     ?= grep
CUT      ?= cut

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
PRIMARY_AUTHORS = dcantrell@redhat.com

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
	$(NINJA) -C $(MESON_BUILD_DIR) tarpm-pot

check:
	@echo "*** No test suite right now."

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

# Quiet errors about target arguments not being targets
%:
	@true
