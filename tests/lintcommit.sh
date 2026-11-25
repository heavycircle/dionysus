#!/usr/bin/env bash
# scripts/lintcommit.sh accepts and rejects the subjects CONTRIBUTING.md says
# it does, checked one commit at a time in a scratch repository.
set -uo pipefail

repo=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1
git init -q .
git config user.name test
git config user.email test@example.invalid
git commit -q --allow-empty -m 'chore: start'
failed=0

# Exit status lintcommit gives a range holding one commit with this subject.
lint() {
    git commit -q --allow-empty -m "$1"
    "$repo/scripts/lintcommit.sh" HEAD~1..HEAD >/dev/null
}

expect() {
    local want=$1 subject=$2 code
    lint "$subject"
    code=$?
    if [[ $code != "$want" ]]; then
        echo "FAILED: \"$subject\": exit $code, expected $want"
        failed=1
    fi
}

for subject in 'feat: add a thing' 'fix(sdk): handle empty input' 'refactor(sdk)!: rename the api' \
    'style: format' 'docs(guide/docker.md): fix a link' 'release: v1.2.3' "test: $(printf 'x%.0s' {1..66})"; do
    expect 0 "$subject"
done
for subject in 'Feat: add' 'feat: Add a thing' 'feat: add a thing.' 'feature: add' 'feat add' \
    'feat(SDK): add' 'feat:  two spaces' 'release: 1.2.3' "test: $(printf 'x%.0s' {1..67})"; do
    expect 1 "$subject"
done

# Merges are skipped; a range git can't resolve is an error, not a pass.
git switch -q -c side && git commit -q --allow-empty -m 'fix: side' && git switch -q -
git merge -q --no-ff -m 'Merge branch side' side
"$repo/scripts/lintcommit.sh" HEAD~1..HEAD >/dev/null || { echo "FAILED: a merge was linted"; failed=1; }
"$repo/scripts/lintcommit.sh" nosuchref..HEAD >/dev/null 2>&1
code=$?
[[ $code == 2 ]] || { echo "FAILED: an unknown range: exit $code, expected 2"; failed=1; }

((failed)) || echo "ok      lintcommit"
exit $failed
