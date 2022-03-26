# Nekomonogatari

Monorepo for catalysts

Included projects:

- **CatSyn** - Catalyzed video processing framework
- **Metalloporphyrin** - VapourSynth compatibility layer for CatSyn
- **VapStd** - Standalone VapourSynth core filters

## Build

**You can only build for Reference use.
Distributing built binary to anyone else (unless you both in the same company)
goes against the license and is strictly forbidden.**

Clang, CMake, and Ninja are required to be present in PATH.
These requirements can be obtained through Visual Studio Installer.
MinGW and Cygwin are not supported.

Before you start, all submodules should be checked out recursively.

```
git submodules update --init --recursive
```

Use the CMake Presets to build and install.

```
cmake --preset Release-Clang
cmake --build --preset Release-Clang
cmake --install build-release --prefix install-release
```
