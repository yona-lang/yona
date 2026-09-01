# Local ARM64 testing with QEMU

Use `scripts/test-arm64-qemu.sh` to build and run the Linux ARM64 test binary
inside a pinned Fedora ARM64 container. It is the project-local reproduction
path for ARM-only failures; GitHub Actions is a verification target, not the
only way to execute ARM tests.

## Fedora host dependencies

Install the host container engine and QEMU user-mode binfmt support:

```bash
sudo dnf install podman qemu-user-static qemu-user-binfmt
sudo systemctl restart systemd-binfmt
```

Verify emulation before building Yona:

```bash
podman run --rm --arch arm64 registry.fedoraproject.org/fedora:44 uname -m
```

The command must print `aarch64`. The container installs its own LLVM, CMake,
Ninja, PCRE2, CLI11, doctest, and libxml2 dependencies; no host cross compiler
is required.

## Run a focused regression

```bash
scripts/test-arm64-qemu.sh -tc="*raw channel natives consume references*"
```

Set `YONA_CONTAINER_ENGINE` to use Docker-compatible tooling, or
`YONA_ARM64_QEMU_IMAGE` to select a locally maintained image tag. QEMU is the
standard local reproducer; fixes affecting atomics, scheduling, or memory
ordering must additionally be confirmed on owned native ARM64 hardware when it
becomes available.
