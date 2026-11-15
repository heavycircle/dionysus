#!/usr/bin/env bash
# Check commit subjects against CONTRIBUTING.md#commit-messages, so release
# notes can be generated from history (cliff.toml). Merge commits are skipped.
#
#   scripts/lintcommit.sh origin/main..HEAD
set -euo pipefail

range=${1:?usage: scripts/lintcommit.sh <revision-range>}

types='build|chore|ci|docs|feat|fix|perf|refactor|revert|test'
conventional="^($types)(\([a-z0-9_./-]+\))?!?: [^ A-Z].*[^.]\$"
release='^release: v[0-9]+\.[0-9]+\.[0-9]+$'

failed=0
while read -r sha subject; do
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
done < <(git log --no-merges --format='%h %s' "$range")

if ((failed)); then
    echo
    echo "Reword with 'git rebase -i' (see CONTRIBUTING.md#commit-messages)."
    exit 1
fi
