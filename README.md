# Minolta F-2400 Linux 扫描驱动

这是一份可独立构建的用户态 C11 驱动工程，支持 Minolta Dimage Scan Dual / F-2400
（INQUIRY：`Minolta #2882 1.10`）。通过 Linux SG_IO 和系统的 AM53C974 驱动访问扫描仪。

采集、初始化、自动曝光、校准、曲线、像素重建、PNG/TIFF读写、旋转、校验和扫描调度全部在 C 中实现。
Python 仅用于 Tk 桌面界面、显示缩放、文件选择和界面任务管理；没有 NumPy、原厂 DLL、Wine 或模拟器运行依赖。

- [构建、运行与安装](BUILD.md)
- [与原厂驱动的差异及目前未实现部分](ORIGINAL_GAPS.md)
- [代码结构和 C API](docs/ARCHITECTURE.md)
- [验证范围与证据](docs/VALIDATION.md)

## 快速开始

依赖：C 编译器、CMake、pkg-config、libpng、libtiff、json-c、OpenSSL Crypto。
构建默认测试还需 zlib。只有界面需要 Python 3、Tkinter、Pillow。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./start-frontend.sh
```

不使用界面也能完成扫描(本来计划的自动化扫描，但是由于该款扫描仪是手动进片遂放弃)：

```sh
./build/f2400 status
mkdir -p scans
# 先取下片夹，初始化完成后再放回胶片。
./build/f2400 initialize --output scans/calibration-001 --holder-removed
./build/f2400 scan --output scans/positive-001 \
  --calibration scans/calibration-001/profile.json --film color-positive --ratio 4
```

输出目录必须是新的，父目录需已存在。工具不会覆盖旧扫描目录。
CLI默认保存PNG、16位TIFF、原始BIN及审计记录。界面默认仅预览；临时文件在C图像载入内存后清理，仍可旋转和单独另存。

设备通过sysfs类型、厂商、型号和固件自动发现，打开后核验设备号、加锁并重新执行INQUIRY。

## 源码布局

```text
include/f2400/    公共 C API
src/             采集、协议、算法、图像、审计与任务调度
ui/              Tk界面和C接口适配
tests/          C测试、原厂对照数据和界面测试
udev/            可选桌面访问权限规则
docs/           架构和验证说明
```

必要的数据保留在`tests/fixtures/`，重构验证摘要在`docs/validation/`。
本目录及其测试数据可单独复制到其他位置构建。
不附带原厂可执行文件。
