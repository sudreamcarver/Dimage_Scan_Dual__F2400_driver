# 架构与接口

## 分层

```text
Tk UI / controller.py
  ├─ 子进程调用 build/f2400（采集、初始化、取消和任务报告）
  └─ ctypes调用 libf2400（校准档案验证、内存图像、旋转、另存）
                  │
          include/f2400/f2400.h
                  │
  pipeline.c ─ device / transport / capture / initialization
       └──── exposure / curves / calibration / image / audit
```

没有`.c`包含`.c`、重命名main或运行时加载Python处理脚本的机制。
所有C实现按独立编译单元构建为同一组对象，CLI、共享库和测试复用这组对象。

| 文件                          | 责任                                                    |
| ----------------------------- | ------------------------------------------------------- |
| `device.c` / `f2400-device.h` | sysfs身份筛选、节点号校验；不通过探测硬盘寻找扫描仪     |
| `transport.c`                 | SG_IO、sense与忙状态；保留失败状态/数据，失败READ不重试 |
| `capture.c`                   | 暗场、准备、逐行读数、灰阶/RGB、取消和收尾              |
| `initialization.c`            | 已验证的有界冷启动控制流                                |
| `exposure.c`                  | 正片、黑白负片、彩负测光、分类与规范化                  |
| `curves.c`                    | 默认原厂LUT，保留扩展精度和原厂取整语义                 |
| `calibration.c`               | 校准算法、档案读取、hash检查和校准采集审计              |
| `image.c`                     | 像素重建、校正、抖动、统计、无损90度旋转、PNG/TIFF      |
| `audit.c`                     | 独立解析wire日志、核对窗口/掩码/计数/暗场/原始字节      |
| `pipeline.c`                  | 同一设备锁内的完整任务、状态机、错误路径和报告          |
| `cli.c`                       | 参数解析；在线和离线入口                                |
| `util.c`                      | 文件、JSON、SHA256、错误文本、信号注册                  |

## C API

公共头文件为`include/f2400/f2400.h`。共享库不导出内部协议/测试符号。

- `f2400_scan`：完整测光、正式扫描、处理、导出和审计。
- `f2400_calibrate`：冷启动/校准刷新及建档。
- `f2400_control`：只读状态或收尾。
- `f2400_meter`、`f2400_lut`、`f2400_process`：可脱离设备的处理API。
- `f2400_normalize_color_exposure`：原厂预览曝光规范化。
- `f2400_profile_info`：在C中验证校准档案及平场哈希，输出JSON供UI显示。
- `f2400_image_load/rotate/save/free`：C拥有全深度样本和8位显示缓存。
- `f2400_error`：当前线程的最近错误说明，只在失败返回后读取。

采集接口同步执行，每个进程只运行一个硬件任务；该部分内部状态不是可重入接口。
CLI按任务启动独立进程，打开设备后在整个测光/正式采集或初始化/刷新期间持有flock。
不允许在一个进程中并行调用多个采集/初始化API。独立图像对象可由调用者分别管理。

`f2400_process`读取本机协议的BE16传感器原始序列，暗场使用7008个宿主uint16值，平场为7008个BYTE。
返回C所有的图像对象，必须调用`f2400_image_free`。`pixels`是交错的宿主uint16、16位容器样本；
`preview`为交错8位显示数据。原厂暗部抖动只作用于RGB非RAW的显示缓存，不改变16位TIFF数据。
`f2400_image_preview`返回的指针在旋转或释放对象后失效，UI须先复制显示缓存。

90度旋转同时重新排列显示缓存与16位样本，无插值。显示缩放仅在Tk/Pillow中进行。
即使源临时文件已被删除，C对象仍可保存完整样本。

## 数据与错误约束

- 输出目录必须全新；流程失败不标记完整成片。
- 完成采集不等于完成任务：必须经过C的独立审计，再原子更新`result.json`为complete。
- SIGINT/SIGTERM只设置停止标志；不从信号处理函数发设备命令。
- 普通采集的取消路径允许已知收尾；冷启动取消不假装设备已就绪。
- 校准刷新必须192次读取全部GOOD，独立重新累加并核对BIN后才生成档案。
- 初始化阶段精确setup-pending响应只允许已恢复原厂分支继续诊断，仍保留CHECK CONDITION日志。
- PNG8不写Gamma、sRGB或ICC标签；RAW数值不经过色彩曲线。TIFF16不添加ICC。
- 目前高深度导入针对无符号、连续平面、左上方向的RGB/灰阶8或16位TIFF；其他TIFF布局会明确拒绝。
