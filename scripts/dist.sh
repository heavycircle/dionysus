#!/usr/bin/env bash
# Build the release archives of dino for every supported host platform, plus
# SHA256SUMS. Used by .github/workflows/release.yml; runs the same locally:
#
#   scripts/dist.sh 0.1.0 dist
#
# Archives are reproducible: the same commit, Zig version and SOURCE_DATE_EPOCH
# (default: the commit time) give byte-identical files, so anyone can rebuild a
# release and compare it against SHA256SUMS.
set -euo pipefail

version=${1:?usage: scripts/dist.sh VERSION [OUTDIR]}
mkdir -p "${2:-dist}"
out=$(cd "${2:-dist}" && pwd)
cd "$(dirname "$0")/.."

: "${SOURCE_DATE_EPOCH:=$(git log -1 --format=%ct)}"
export LC_ALL=C TZ=UTC0

# https://www.gnu.org/software/tar/manual/html_section/Reproducibility.html
tar_flags=(
    --format=gnu --sort=name
    --mtime="@$SOURCE_DATE_EPOCH"
    --numeric-owner --owner=0 --group=0
    --mode=go+u,go-w
)

# <asset suffix> <zig target>. macOS is not supported yet.
platforms=(
    "x86_64-linux x86_64-linux-musl"
    "aarch64-linux aarch64-linux-musl"
    "x86_64-windows x86_64-windows-gnu"
)

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

for platform in "${platforms[@]}"; do
    read -r name triple <<<"$platform"
    dir=dino-$version-$name
    echo "==> $dir"
    zig build -Drelease -Dtarget="$triple" -Dversion-string="$version" --prefix "$work/prefix-$name"
    mkdir "$work/$dir"
    cp "$work/prefix-$name"/bin/dino* LICENSE README.md "$work/$dir/"
    if [[ $triple == *windows* ]]; then
        # zip has no --mtime/--sort: normalize the files and feed it a sorted list.
        chmod -R u=rwX,go=rX "$work/$dir"
        find "$work/$dir" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
        (cd "$work" && find "$dir" | sort | zip -qX -@ "$out/$dir.zip")
    else
        # xz output depends on the thread count, so pin it.
        tar -C "$work" "${tar_flags[@]}" -cf - "$dir" | xz -9 -T1 >"$out/$dir.tar.xz"
    fi
done

(cd "$out" && sha256sum dino-"$version"-* >SHA256SUMS)
cat "$out/SHA256SUMS"
