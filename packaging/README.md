# Packaging

Builds the release binary, packages it with
[nfpm](https://github.com/goreleaser/nfpm) and writes the bare executable.

## Usage

```bash
./packaging/package.sh                  # deb, rpm and archlinux for the host
./packaging/package.sh deb rpm          # selected formats
./packaging/package.sh --help
```

Artifacts go to `build/`:

| Artifact | Name |
| :- | :- |
| deb | `riipl_<version>_<arch>.deb` |
| rpm | `riipl-<version>-1.<arch>.rpm` |
| archlinux | `riipl-<version>-1-<arch>.pkg.tar.zst` |
| bare executable | `riipl_<version>_linux_<arch>_<libc>` |

## Other architectures

`--arch` takes one architecture or a list, and each one builds in
`build/packaging/<arch>`:

```bash
./packaging/package.sh --arch amd64,arm64
```

A foreign architecture takes its toolchain from
`packaging/toolchains/<arch>.cmake`. When that file is missing, the script
generates one from the architecture's GNU triple and installs the cross
compiler. The sysroot needs a Qt 6.8 development install for the target
architecture; write your own toolchain file to point at one elsewhere.

## Requirements

- CMake 3.21+, Qt 6.8 development packages (Widgets, Network, Svg,
  LinguistTools), git.
- nfpm: from `$NFPM`, else `PATH`, else downloaded to
  `${XDG_CACHE_HOME:-$HOME/.cache}/riipl-nfpm`. The pinned release is
  `NFPM_VERSION`.
