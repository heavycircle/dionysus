# Everything needed to build dino, build projects for every target, and run
# the results: zig, cmake, ninja, qemu-user for the Linux targets and wine for
# the Windows targets.
#
#   docker build -t dionysus .
#   docker run --rm -v "$PWD:/work" dionysus dino build --all
#   docker run --rm -v "$PWD:/work" dionysus dino run arm64
#   docker run --rm -v "$PWD:/repo:ro" dionysus /repo/tests/examples.sh /repo/examples

FROM debian:trixie-slim AS base

ARG ZIG_VERSION=0.17.1
ARG ZIG_SHA256=3581863e3cbcfd0fce0efc682ce7e9e93da96668a977791d356a26d2a7e9b17a

# i386 multiarch is what lets wine run 32-bit (win32) executables.
RUN dpkg --add-architecture i386 \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates curl xz-utils file \
        cmake ninja-build make \
        qemu-user \
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
COPY build.zig build.zig.zon ./
COPY cli cli
COPY sdk sdk
# Zig defaults to -mcpu=native; layers are cached and shared across machines
# (CI runners, pulled images), so build for the baseline CPU instead.
RUN zig build test && zig build -Drelease -Dcpu=baseline --prefix /usr/local ${DINO_VERSION:+-Dversion-string=$DINO_VERSION}

FROM base
COPY --from=build /usr/local/bin/dino /usr/local/bin/dino

RUN useradd --create-home dino
USER dino
ENV WINEDEBUG=-all
# Creating the wine prefix takes several seconds; do it once at image build time.
RUN wineboot --init >/dev/null 2>&1; wineserver --wait || true

WORKDIR /work
CMD ["dino", "doctor"]
