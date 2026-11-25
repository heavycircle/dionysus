#!/usr/bin/env bash
# Cut a release, following CONTRIBUTING.md#releases:
#
#   on main:          vX.Y.0, create release-X.Y, bump main to X.(Y+1).0-dev
#   on release-X.Y:   vX.Y.Z, bump the branch to X.Y.(Z+1)-dev
#
# The branch must match origin and have a green ci run (--no-ci-check skips
# that lookup, which needs gh and the network). Everything stays local; the
# push command is printed at the end. A failure part-way undoes the commits,
# the tag and the branch made so far.
set -euo pipefail

die() {
    echo "release: $*" >&2
    exit 1
}

ci_check=1
case ${1:-} in
"") ;;
--no-ci-check) ci_check=0 ;;
*) die "usage: scripts/release.sh [--no-ci-check]" ;;
esac

cd "$(git rev-parse --show-toplevel)"
zon=build.zig.zon
cmake=sdk/CMakeLists.txt

current=$(sed -n 's/^    \.version = "\(.*\)",$/\1/p' "$zon")
[[ $current =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)-dev$ ]] || die "$zon version '$current' is not X.Y.Z-dev"
major=${BASH_REMATCH[1]} minor=${BASH_REMATCH[2]} patch=${BASH_REMATCH[3]}
release=$major.$minor.$patch

branch=$(git symbolic-ref --short HEAD)
case $branch in
main) ((patch == 0)) || die "main must carry X.Y.0-dev, found $current" ;;
"release-$major.$minor") ((patch > 0)) || die "$branch must carry X.Y.Z-dev with Z > 0, found $current" ;;
*) die "releases are cut from main or release-$major.$minor, not '$branch'" ;;
esac
[[ -z $(git status --porcelain) ]] || die "working tree is not clean"
! git rev-parse -q --verify "refs/tags/v$release" >/dev/null || die "tag v$release already exists"
if [[ $branch == main ]]; then
    ! git rev-parse -q --verify "refs/heads/release-$major.$minor" >/dev/null ||
        die "branch release-$major.$minor already exists"
fi

# Tagging a stale or unpushed branch publishes a commit that isn't on origin's.
git fetch -q origin "$branch" || die "can't fetch $branch from origin"
start=$(git rev-parse HEAD)
[[ $start == "$(git rev-parse "origin/$branch")" ]] ||
    die "$branch is not at origin/$branch: pull, or push and wait for ci"
remote_refs=$(git ls-remote origin "refs/tags/v$release" "refs/heads/release-$major.$minor")
[[ $branch != main || $remote_refs != *refs/heads/* ]] || die "origin already has release-$major.$minor"
[[ $remote_refs != *refs/tags/* ]] || die "origin already has the tag v$release"

if ((ci_check)); then
    command -v gh >/dev/null || die "gh is needed to look up the ci run (or pass --no-ci-check)"
    ci=$(gh run list --workflow ci --branch "$branch" --event push --commit "$start" --limit 1 \
        --json status,conclusion --jq '.[0] | "\(.status) \(.conclusion)"') ||
        die "can't look up the ci run (or pass --no-ci-check)"
    [[ $ci == "completed success" ]] ||
        die "ci for ${start:0:9} on $branch is '${ci:-missing}', not a success (wait for it, or pass --no-ci-check)"
fi

# Nothing is pushed by this script, so until it finishes everything it made
# can be taken back. The tree was clean, so the reset loses nothing.
finished=0
undo() {
    ((finished)) && return
    echo "release: failed; undoing the release commits, tag and branch" >&2
    git switch -q -f "$branch"
    git reset -q --hard "$start"
    git tag -d "v$release" >/dev/null 2>&1 || true
    if [[ $branch == main ]]; then
        git branch -q -D "release-$major.$minor" 2>/dev/null || true
    fi
}
trap undo EXIT

# Usage: set_version X.Y.Z [-dev]
set_version() {
    # -i.bak works with both GNU and BSD sed.
    sed -i.bak "s/^    \.version = \".*\",\$/    .version = \"$1$2\",/" "$zon"
    sed -i.bak -e "s/^project(DinoSDK VERSION [0-9.]* /project(DinoSDK VERSION $1 /" \
        -e "s/^set(DINO_VERSION_PRERELEASE \".*\")\$/set(DINO_VERSION_PRERELEASE \"$2\")/" "$cmake"
    rm -f "$zon.bak" "$cmake.bak"
    grep -q "^    \.version = \"$1$2\",\$" "$zon" || die "failed to set version in $zon"
    grep -q "^project(DinoSDK VERSION $1 " "$cmake" || die "failed to set version in $cmake"
    grep -q "^set(DINO_VERSION_PRERELEASE \"$2\")\$" "$cmake" || die "failed to set prerelease in $cmake"
    git add "$zon" "$cmake"
}

# The docs pin the image to the series they ship with (0.9), a tag every patch
# release moves: main then never names an older patch than release-0.9 has, and
# a tag's docs name an image that exists once release.yml publishes it.
pin_docs() {
    local image='ghcr\.io/heavycircle/dionysus:' page
    # A read loop rather than mapfile: macOS ships bash 3.2.
    { git grep -lE "${image}[0-9]+\.[0-9]+" -- '*.md' || true; } | while IFS= read -r page; do
        sed -E -i.bak "s|($image)[0-9]+\.[0-9]+(\.[0-9]+)?|\1$1|g" "$page"
        rm -f "$page.bak"
        git add "$page"
    done
}

bump() {
    set_version "$1" -dev
    git commit -q -m "build: bump version to $1-dev"
}

set_version "$release" ""
pin_docs "$major.$minor"
# Until the tag exists, build.zig rightly rejects a non-dev version.
zig build test -Dversion-string="$release"
git commit -q -m "release: v$release"
git tag -a "v$release" -m "dino v$release"

zig build
reported=$(zig-out/bin/dino version)
[[ $reported == "dino $release" ]] ||
    die "built dino reports '$reported', expected 'dino $release'"

if [[ $branch == main ]]; then
    git switch -q -c "release-$major.$minor"
    bump "$major.$minor.1"
    git switch -q main
    bump "$major.$((minor + 1)).0"
    refs=(main "release-$major.$minor")
else
    bump "$major.$minor.$((patch + 1))"
    refs=("$branch")
fi
finished=1

echo
git log --oneline --graph --decorate -4 "${refs[@]}"
echo
echo "Tagged v$release. Review the commits above, then publish with:"
echo "    git push --atomic origin ${refs[*]} v$release"
echo "Pushing the tag runs .github/workflows/release.yml, which runs ci on the tag, tests the"
echo "image, and then publishes the image and the GitHub Release."
if [[ $branch == main ]]; then
    echo "Then create the backport label for the new branch:"
    echo "    gh label create 'ci:backport release-$major.$minor' --color FBCA04"
    if ((minor > 0)); then
        echo "and retire the previous one, since only the newest release branch is supported:"
        echo "    gh label delete 'ci:backport release-$major.$((minor - 1))' --yes"
    fi
fi
