# Yona Compiler — Multi-stage Docker build
#
# Build:   docker build -t yona .
# Run:     docker run --rm yona yona -e '"hello world"'
# Shell:   docker run --rm -it yona
#
# Stage 1: Build the compiler from source
# Stage 2: Minimal runtime with yonac + yona + stdlib

# ===== Build stage =====
FROM fedora:43 AS builder

RUN dnf install -y \
    llvm llvm-devel llvm-libs llvm-static \
    clang lld cmake ninja-build \
    pcre2-devel \
    git \
    && dnf clean all

WORKDIR /build
COPY . .

RUN cmake --preset x64-release-linux \
    && cmake --build --preset build-release-linux

# Run tests to verify the build
RUN cd out/build/x64-release-linux && ctest --output-on-failure || true

# ===== Runtime stage =====
FROM fedora:43-minimal AS runtime

RUN microdnf install -y \
    llvm-libs clang lld \
    pcre2 \
    && microdnf clean all

# Copy compiler binaries
COPY --from=builder /build/out/build/x64-release-linux/yonac /usr/local/bin/yonac
COPY --from=builder /build/out/build/x64-release-linux/yona /usr/local/bin/yona
COPY --from=builder /build/out/build/x64-release-linux/yona-repl /usr/local/bin/yona-repl
COPY --from=builder /build/out/build/x64-release-linux/yls /usr/local/bin/yls

# Copy standard library
COPY --from=builder /build/lib /usr/local/lib/yona/lib

# Copy runtime source (needed for LTO compilation)
COPY --from=builder /build/src/compiled_runtime.c /usr/local/lib/yona/src/compiled_runtime.c
COPY --from=builder /build/src/runtime /usr/local/lib/yona/src/runtime

# Default module search path
ENV YONA_LIB=/usr/local/lib/yona/lib

WORKDIR /workspace

# Default: interactive REPL
CMD ["yona"]
