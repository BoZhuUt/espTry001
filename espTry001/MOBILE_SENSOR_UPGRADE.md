# 传感器离线升级

当前适配 ST588 BootLoader，来源：`D:\pyxis\software\gitee\ST_588\BootLoader\BootLoader`。
原有 CDC、普通 PASS、UPGRADE PASS 和设备自身的 FIRMWARE DOWNLOAD 均保留。
本功能不修改传感器 BootLoader。

## 操作

1. 插好 SD 卡，进入 System Info → Tools Menu → SENSOR UPGRADE。
2. 点击 USB IMPORT，再通过设备原生 USB 口连接电脑。电脑显示 512 KiB 的
   `PYXIS_FW` 临时 U 盘。
3. 将传感器 **APP 的 BIN 文件**放到虚拟盘根目录。文件名采用英文、数字、
   下划线或短横线，主文件名不超过 8 个字符，例如 `ST588V12.BIN`。
   不放 HEX、整片 Flash 镜像或 BootLoader 镜像，不重新格式化虚拟盘。
4. 在电脑上弹出该盘，再点击设备上的 EJECT PC, THEN SAVE。
   文件经过校验、SD 写入和读回比对后，保存到 SD 的 `/firmware` 目录。
   同名文件不会被覆盖，需要换名后再次导入。每次导入的临时盘都是空盘。
   CANCEL IMPORT 可放弃本次尚未保存的导入；直接断电也不会保存临时文件。
5. 设备可脱离电脑运行。点击文件名区域循环选择固件，再点 START UPGRADE。
   当前升级协议固定为 ST588 / 9600 8N1 / YMODEM-1K。
6. 只连接一个目标传感器，给传感器上电。设备最多等待 60 秒，识别启动提示后，
   在 5 秒 Boot 窗口内发送 `abc`，进入菜单发送 `1`，再发送选定固件。
7. 等待 Upgrade successful；此提示来自 BootLoader 的编程成功回报。
   顶部标题返回 System Info 并恢复普通 PASS。取消或失败后重新上电再重试。

保存后的固件也可通过读卡器放入 SD 的 `/firmware` 根目录，页面最多列出 16 个 BIN。
应用地址固定为 `0x08005000`，文件最大 `0x1B000`（108 KiB），加载时检查
栈指针和复位入口。这些检查不能代替产品型号/版本确认，用户仍需选对 ST588 固件。

## 资源与协议

- USB 为 CDC + MSC 复合设备，MSC 平时无介质，仅导入时出现临时盘；首次更新后
  Windows 可能重新分配 COM 口。临时盘优先使用 PSRAM，主要面向现有 2.8 寸机型。
  内存不足会拒绝创建导入盘，原有功能可继续使用。
- 电脑访问独立 FAT12 内存盘，不能直接修改设备正在挂载的 SD 文件系统。
  支持碎片化文件；损坏、循环或超界的 FAT 链会被拒绝。
- StorageTask 独占扫描、导入、读回和固件加载。进入页面前先停止日志和 RS485；
  固件加载到内存后，再通过现有 Rs485Service 原始通道发送。
- 离线模式不读取电脑 CDC 的升级数据，避免两个发送源争用 RS485。
- 每包 1024 字节，包序号与 CRC16 校验完整，末包使用 `0x1A` 填充。
  发送端保留分段写入偏移，只在 ACK 后推进文件偏移，NAK 最多重发 5 次。
  适配所提供 Boot 的单 EOT ACK，也支持先 NAK 再 ACK 的双 EOT 对端。
- 所提供 Boot 不接受重复数据包序号；应答超时停止而不盲目重发。
  `100%` 仅表示所有数据收到 ACK，仍需完成结束握手和编程成功确认。
- 启动和菜单提示后等待 80 ms 串口静默再发送，避免半双工 RS485 冲突。

## 验证

```powershell
pio run -e waveshare28
python tools/test_mobile_upgrade_native.py
python tools/test_cdc_native.py
```

本地模拟直接编译生产升级模块，覆盖最大 108 KiB 固件、37 字节短写、CRC、
ACK/NAK、单/双 EOT、成功回报、应答丢失、等待超时、CAN 取消、FAT 碎片、
循环链、末包填充与弹出保护。CDC 回归覆盖原有 1029 字节顺序、背压和断连。

实机待验收：Windows 盘符识别/弹出/再次导入；保存后断电重启仍能选固件；
真实 ST588 上电 5 秒窗口；最大固件升级后版本读回；升级中断电后重新上电重试；
返回普通 PASS 与原有 UPGRADE PASS；升级页长时间运行的触摸、堆和任务栈稳定性。
