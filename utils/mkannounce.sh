#!/bin/sh
#
# Create a text file suitable for a GitHub release announcement or
# blog post.  Groups git log summaries by category.  Entries with no
# category marking go under a catch all heading at the end.
#
# Copyright Dave Cantrell
# SPDX-License-Identifier: GPL-3.0-or-later
#

PATH=/bin:/usr/bin
TMPDIR="$(mktemp -d)"
CWD="$(pwd)"

# where the categorized entries collect and what we head the rest with
CATEGORY_DIR="${TMPDIR}/categories"
UNCATEGORIZED="${TMPDIR}/uncategorized"
UNCATEGORIZED_DESC="Uncategorized"

trap 'rm -rf "${TMPDIR}"' EXIT

mkdir -p "${CATEGORY_DIR}"

# need the correct tags
if [ "$1" = "--stable" ]; then
    LATEST_TAG="$(git tag -l | sort -V | tail -n 1)"

    # One tag means this is the first release, so there is no earlier
    # tag to start from.
    if [ "$(git tag -l | wc -l)" -gt 1 ]; then
        PREV_TAG="$(git tag -l | sort -V | tail -n 2 | head -n 1)"
    else
        PREV_TAG=
    fi
else
    PREV_TAG="$(git tag -l | sort -V | tail -n 1)"
    LATEST_TAG="HEAD"
fi

# Without an earlier tag there is nothing to start from, so take the
# whole log.  That is what a project looks like before it has tagged
# anything and what the first release looks like once it has.
get_log() {
    if [ -n "${PREV_TAG}" ]; then
        git log --reverse --pretty=tformat:%s "${PREV_TAG}".."${LATEST_TAG}" 2>/dev/null
    elif [ -n "${LATEST_TAG}" ]; then
        git log --reverse --pretty=tformat:%s "${LATEST_TAG}" 2>/dev/null
    else
        git log --reverse --pretty=tformat:%s 2>/dev/null
    fi
}

# gather log entries since the latest tag in reverse order.  An entry
# starting with a category marking goes under that category and the
# rest collect under the catch all.
get_log | while read -r logline ; do
    category=

    case "${logline}" in
        \[*\]*)
            category="$(echo "${logline}" | cut -d ']' -f 1 | cut -d '[' -f 2)"
            ;;
    esac

    if [ -z "${category}" ]; then
        echo "* ${logline}" >> "${UNCATEGORIZED}"
    else
        echo "*$(echo "${logline}" | cut -d ']' -f 2- | xargs -0)" >> "${CATEGORY_DIR}"/"${category}"
    fi
done

# now create a draft announcement grouped by category
for category in "${CATEGORY_DIR}"/* ; do
    # nothing was collected, so the glob is still the literal pattern
    [ -f "${category}" ] || continue

    if [ -f "${CWD}"/CONTRIBUTING.md ]; then
        desc="$(grep "\[$(basename "${category}")\]" "${CWD}"/CONTRIBUTING.md | cut -d '|' -f 3 | awk '{$1=$1};1')"
    fi

    if [ -z "${desc}" ]; then
        echo "$(basename "${category}"):"
    else
        echo "${desc}:"
    fi

    cat "${category}"
    echo
done

# the entries carrying no category marking go last
if [ -f "${UNCATEGORIZED}" ]; then
    echo "${UNCATEGORIZED_DESC}:"
    cat "${UNCATEGORIZED}"
    echo
fi
