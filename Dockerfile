# Everything needed to build dino, build projects for every target, and run
# the results: zig, cmake, ninja, qemu-user for the Linux targets, wine for
# the Windows targets, and valgrind for `dino test --memcheck`.
#
#   docker build -t dionysus .
#   docker run --rm -v "$PWD:/work" dionysus dino build --all
#   docker run --rm -v "$PWD:/work" dionysus dino run arm64
#   docker run --rm -v "$PWD:/repo:ro" dionysus /repo/tests/examples.sh /repo/examples

# Pinned by digest, so a rebuild gets the same base; dependabot moves it.
FROM debian:trixie-slim@sha256:8aa5a7bf5a1955bddbe7d74fcdd0b91ab30e74362a8b73af672b0481b35b44a0 AS base

ARG ZIG_VERSION=0.17.1
ARG ZIG_SHA256=3581863e3cbcfd0fce0efc682ce7e9e93da96668a977791d356a26d2a7e9b17a

# i386 multiarch is what lets wine run 32-bit (win32) executables.
RUN dpkg --add-architecture i386 \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates curl xz-utils file \
        cmake ninja-build make \
        qemu-user valgrind \
        wine wine64 wine32:i386 \
    && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL "https://ziglang.org/download/${ZIG_VERSION}/zig-x86_64-linux-${ZIG_VERSION}.tar.xz" -o /tmp/zig.tar.xz \
    && echo "${ZIG_SHA256}  /tmp/zig.tar.xz" | sha256sum -c - \
    && mkdir -p /opt/zig \
    && tar -xJf /tmp/zig.tar.xz -C /opt/zig --strip-components=1 \
    && rm /tmp/zig.tar.xz
ENV PATH=/opt/zig:$PATH

FROM base AS build
# The build context has no .git, so dino reports build.zig.zon's version unless
# the release workflow passes the exact one (e.g. --build-arg DINO_VERSION=1.0.0).
ARG DINO_VERSION
WORKDIR /src
COPY build.zig build.zig.zon .clang-format .clang-tidy ./
COPY cli cli
COPY sdk sdk
# Zig defaults to -mcpu=native; layers are cached and shared across machines
# (CI runners, pulled images), so build for the baseline CPU instead.
RUN zig build test && zig build -Drelease -Dcpu=baseline --prefix /usr/local ${DINO_VERSION:+-Dversion-string=$DINO_VERSION}

FROM base
COPY --from=build /usr/local/bin/dino /usr/local/bin/dino
COPY scripts/docker-entrypoint.sh /usr/local/bin/dino-entrypoint

RUN useradd --create-home dino && install -d -o dino -g dino /work && groupadd --system dino-entrypoint

ENV WINEDEBUG=-all
# Wine maps Windows file names and arguments to Unix ones through the locale:
# in the default C locale a name such as café.txt can't be created and its é
# arrives mangled in argv. C.UTF-8 ships with Debian's base image.
ENV LANG=C.UTF-8
# A wine prefix takes ~40 s to create, so it's built once here, in /opt, as dino,
# and made writable by anyone. Every user's own ~/.wine (dino's below, others'
# from the entrypoint) is a few KB that links into it. The chmod has to share
# the layer, or Docker copies all 1.4 GB again. The build's wineserver directory
# and the prefix's pointer to it are dropped: each user's prefix names its own.
RUN install -d -o dino -g dino /opt/wine-prefix \
    && setpriv --reuid=dino --regid=dino --init-groups sh -c \
        'HOME=/home/dino WINEPREFIX=/opt/wine-prefix wineboot --init >/dev/null 2>&1; \
         WINEPREFIX=/opt/wine-prefix wineserver --wait' \
    && rm -rf /tmp/wine-* /opt/wine-prefix/wineserver \
    && chmod -R a+rwX /opt/wine-prefix \
    && setpriv --reuid=dino --regid=dino --init-groups \
        env HOME=/home/dino dino-entrypoint --own-wine-prefix

# zig builds the C library and runtime for each target the first time a build
# needs it, which in a fresh container is every time: about a minute of a
# first build. The published images (release.yml, nightly.yml) pass
# WARM_ZIG_CACHE=1 to build a project for every target here, so their cache
# already holds what a Release build links. It lives in /opt and is writable by
# anyone, for whichever user the entrypoint picks, as the wine prefix is.
# The entrypoint points zig at it unless the user chose a cache of their own.
ARG WARM_ZIG_CACHE=0
RUN install -d -o dino -g dino /opt/zig-cache \
    && if [ "$WARM_ZIG_CACHE" = 1 ]; then \
        setpriv --reuid=dino --regid=dino --init-groups sh -c \
            'export HOME=/home/dino ZIG_GLOBAL_CACHE_DIR=/opt/zig-cache && cd /tmp && dino init --no-configure warm >/dev/null \
             && dino build --all -C warm >/dev/null' \
        && rm -rf /tmp/warm /home/dino/.cache/dino; \
    fi \
    && chmod -R a+rwX /opt/zig-cache

# Starts as root only to pick the user: the owner of the working directory
# (see scripts/docker-entrypoint.sh), so files in a mounted project stay yours.
# The group marks a start without --user: --user 0 starts in group 0.
USER root:dino-entrypoint
WORKDIR /work
ENTRYPOINT ["dino-entrypoint"]
CMD ["dino", "doctor"]
