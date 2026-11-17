# 测试数据来源

- `curves.json`：原厂DS_Dual.dll在隔离CPU执行中生成的116组4096项LUT的SHA-256。
  原厂二进制SHA-256为`a830e06d0dbea8e1ca546e71416dcfda5f632df62f9419b2be7a52fd6d505cdf`；
  函数为正片10009456、黑白负片10009aa2、彩色负片1000985d。
- `meters.json`：19组原厂AE黄金结果；测试在C中按既定合成输入重建，不附带胶片照片。
  `ae-`、`cn-`和`pos-`文件为对应测试所用的暗场/平场，不能互相替代。
- `normalization.json`：原厂1000b17a的7组曝光规范化结果。
- `calibration-*.bin`：192次全部GOOD采样中的64次暗场和两遍64次白场累加值，以及原厂已比对输出。
  `calibration.json`为对应schema1档案，供加载和完整虚拟任务测试使用。
- `images.json`：确定性合成原始数据经旧版已验证处理实现所得的样本哈希；5种模式包含PNG抖动前后不同精度缓存。
- `cold-block38.bin.gz`、`cold-block39.bin.gz`：两次真实初始化命令/参数/回复，各2105条。
  其中CHECK CONDITION仍以原始失败状态保存；不因回放成功而称所有历史传输都成功。
  数据来自传感器初始化/白场，不是用户胶片图像。
- `errors.tsv`：4076组扩展错误优先级/模式/状态页组合的原厂CPU结果。

所有二进制数值格式由各C测试读取处明确指定。夹具中不含原厂DLL、Windows环境或模拟器。
