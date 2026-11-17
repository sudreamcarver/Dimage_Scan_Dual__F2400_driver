# Minolta F-2400 Linux Scanner Driver

English | [简体中文](docs/README.zh-CN.md)

A standalone C11 userspace driver for the Minolta Dimage Scan Dual / F-2400
(INQUIRY identity: `Minolta #2882 1.10`). It communicates with the scanner through
Linux SG_IO and the system's AM53C974 driver.

Acquisition, initialization, automatic exposure, calibration, tone curves, pixel
reconstruction, PNG/TIFF I/O, rotation, verification, and scan orchestration are
implemented in C. Python is used only for the Tk desktop interface, display
scaling, file dialogs, and UI task management.

- [Build and run](#quick-start)
- [Factory-driver comparison and remaining gaps](ORIGINAL_GAPS.md) (Chinese)
- [Architecture and C API](docs/ARCHITECTURE.md) (Chinese)
- [Validation records](docs/validation/)

## Quick start

Core dependencies: a C compiler, CMake, pkg-config, libpng, libtiff, json-c, and
OpenSSL Crypto. The default test suite also requires zlib. Python 3, Tkinter, and
Pillow are needed only for the desktop interface.

Run these commands from the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./start-frontend.sh
```

Scanning is also available without the desktop interface. The scanner requires
manual film-holder feeding, so this does not provide unattended batch scanning.

```sh
./build/f2400 status
mkdir -p scans
# Remove the film holder before initialization; reinsert it before scanning.
./build/f2400 initialize --output scans/calibration-001 --holder-removed
./build/f2400 scan --output scans/positive-001 \
  --calibration scans/calibration-001/profile.json --film color-positive --ratio 4
```

Each output directory must be new, and its parent directory must already exist.
Existing scan directories are not overwritten.

By default, the CLI saves PNG, 16-bit TIFF, raw BIN files, and audit records. The
desktop interface defaults to preview-only mode: temporary files are removed
after the native C image is loaded into memory. Rotation and manual Save As remain
available afterward.

The scanner is discovered automatically through its sysfs device type, vendor,
model, and firmware. After opening it, the driver verifies the device number,
acquires a lock, and checks the identity again with INQUIRY.

## Source layout

```text
include/f2400/   Public C API
src/            Acquisition, protocol, algorithms, images, audits, and scan jobs
ui/             Tk desktop interface and C API bindings
tests/          C tests, factory reference fixtures, and UI tests
udev/           Optional local desktop access rule
docs/           Chinese documentation, architecture, and validation records
```

Required test data is included in [`tests/fixtures/`](tests/fixtures/), and the
migration validation summaries are in [`docs/validation/`](docs/validation/).
The repository and its test data can be copied elsewhere and built independently.
Original manufacturer executables are not included.
