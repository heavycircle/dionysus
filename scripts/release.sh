#!/usr/bin/env bash
# Cut a release, following CONTRIBUTING.md#releases:
#
#   on main:          vX.Y.0, create release-X.Y, bump main to X.(Y+1).0-dev
#   on release-X.Y:   vX.Y.Z, bump the branch to X.Y.(Z+1)-dev
#
# Everything stays local; the push command is printed at the end.
set -euo pipefail

die() {
    echo "release: $*" >&2
    exit 1
}

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

bump() {
    set_version "$1" -dev
    git commit -q -m "build: bump version to $1-dev"
}

set_version "$release" ""
# Until the tag exists, build.zig rightly rejects a non-dev version.
zig build test -Dversion-string="$release"
git commit -q -m "release: v$release"
git tag -a "v$release" -m "dino v$release"

zig build
reported=$(zig-out/bin/dino version)
[[ $reported == "dino $release" ]] ||
    die "built dino reports '$reported', expected 'dino $release'. Undo with: git tag -d v$release && git reset --hard HEAD~1"

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

echo
git log --oneline --graph --decorate -4 "${refs[@]}"
echo
echo "Tagged v$release. Review the commits above, then publish with:"
echo "    git push origin ${refs[*]} v$release"
