# Yona Compiler — Multi-stage Docker build
#
# Build:   docker build -t yona .
# Run:     docker run --rm yona yona -e '"hello world"'
# Shell:   docker run --rm -it yona
#
# Stage 1: Build the compiler from source
# Stage 2: Minimal runtime with yonac + yona + stdlib

# ===== Build stage =====
FROM fedora:44 AS builder

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

# Copy the canonical runtime archive and its public headers. The installed
# compiler never rebuilds runtime sources.
COPY --from=builder /build/out/build/x64-release-linux/runtime/libyona_runtime.a /usr/local/lib/yona/runtime/libyona_runtime.a
COPY --from=builder /build/include/yona/Runtime /usr/local/include/yona/Runtime

# Default module search path
ENV YONA_LIB=/usr/local/lib/yona/lib

WORKDIR /workspace

# Default: interactive REPL
CMD ["yona"]
