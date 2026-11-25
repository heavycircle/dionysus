#!/usr/bin/env bash
# Check commit subjects against CONTRIBUTING.md#commit-messages, so release
# notes can be generated from history (cliff.toml). Merge commits are skipped.
#
#   scripts/lintcommit.sh origin/main..HEAD
#   scripts/lintcommit.sh --subject "fix(sdk): a pull request's title"
#
# A squash merge lands the pull request's title as the subject, so
# .github/workflows/pr-title.yml checks it the second way.
set -euo pipefail

usage='usage: scripts/lintcommit.sh <revision-range> | --subject <subject>'

types='build|chore|ci|docs|feat|fix|perf|refactor|revert|style|test'
conventional="^($types)(\([a-z0-9_./-]+\))?!?: [^ A-Z].*[^.]\$"
release='^release: v[0-9]+\.[0-9]+\.[0-9]+$'

if [[ ${1-} == --subject ]]; then
    (($# == 2)) || { echo "$usage" >&2; exit 2; }
    log="subject $2"
else
    range=${1:?$usage}
    # Read first: a failed git log inside the loop's process substitution would
    # be ignored, and a range git can't resolve would pass with nothing checked.
    log=$(git log --no-merges --format='%h %s' "$range") || {
        echo "lintcommit: git log failed for '$range'" >&2
        exit 2
    }
fi

failed=0
while read -r sha subject; do
    [[ -n $sha ]] || continue # an empty range is one empty line
    problem=
    if ((${#subject} > 72)); then
        problem="longer than 72 characters"
    elif ! [[ $subject =~ $conventional || $subject =~ $release ]]; then
        problem="expected 'type(scope): lowercase subject' with type one of ${types//|/, } and no trailing period"
    fi
    if [[ -n $problem ]]; then
        echo "$sha \"$subject\": $problem"
        failed=1
    fi
done <<<"$log"

if ((failed)); then
    echo
    echo "Reword with 'git rebase -i' (see CONTRIBUTING.md#commit-messages)."
    exit 1
fi
