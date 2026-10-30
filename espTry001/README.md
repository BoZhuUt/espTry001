# ESP32-S3 RS485 Utility

基于微雪 ESP32-S3-1.47TOUCH 的传感器配置、采集与日志工具。

## 功能

- PASS 模式下提供 USB ↔ RS485 Modbus RTU 双向透传；
- MASTER 模式支持地址 0 普通发现、厂商 `0x67` 特殊发现及 PN 识别；
- PRECHECK 模式固定读取从站 15 的 46037–46040，并显示
  `s470Raw`、`t365Raw` 两个 CDAB 浮点值；
- 根据 `sensor_profiles.json` 分块轮询最多 16 个参数，支持 UInt16 和 Float32 CDAB；
- 采集两路 4–20 mA 输入，并通过本地 Modbus 从站地址 247 提供数据；
- 将每轮完整传感器快照记录为带 CRC 可校验的 CSV；
- 通过 ESPCFG1 上传 JSON 配置，通过 ESPLOG1 列出、下载和删除日志；
- 触摸界面提供 Dashboard、Sensor Info、System Info、配置上传及日志导出页面。

## 运行架构

传感器离线升级新增了虚拟 U 盘导入与 SD 固件选择入口，使用方法和实机验收项见
[传感器离线升级](MOBILE_SENSOR_UPGRADE.md)。原有 CDC / UPGRADE PASS 保留。

固件使用单一 `AppMode` 管理协议所有权：

- `loopTask`：触摸、ADC、UI、应用编排和物理 USB CDC；
- `Rs485Task`：唯一管理 `Serial1`，执行 PASS、发现和 Profile 轮询；
- `StorageTask`：唯一执行 SD、CSV 和 ESPLOG1 文件操作；
- `UsbService`：根据模式路由本地 Modbus、PASS、ESPCFG1 和 ESPLOG1。

任务间只传递固定大小命令、事件和不可变扫描快照，不共享轮询中的可变数据。

## 硬件配置

| 模块 | 引脚 | 参数 |
|---|---|---|
| LCD (ST7789) | DC=45, CS=21, SCK=38, MOSI=39, RST=40, BL=46 | 172×320, SPI |
| RS-485 | RX=43, TX=44 | 9600, 8E1，自动收发芯片 |
| 4–20 mA ADC | CH1=IO1, CH2=IO2 | ADC_11db，eFuse 校准 |
| 触摸 (AXS5106L) | SDA=42, SCL=41, RST=47, INT=48 | I2C, 0x63 |

## 构建与烧录

```powershell
pio run
pio run -t upload
pio run -t uploadfs
pio device monitor
```

2.8 英寸机型首次启用或更新校准 Profile 时，必须同时执行 `upload` 和
`uploadfs`。应用固件与 LittleFS 是两个独立分区；只烧录
`firmware.factory.bin` 不包含 `data/calibration`。每次 PlatformIO 构建会先
运行校准发布门禁，只把 `approved` Profile 同步到 LittleFS 镜像输入目录。

`generate_sensor_profiles.py` 会在构建前校验
`config/sensor_profiles.json`，生成固件内置 Profile，并同步 LittleFS
镜像输入文件。`get_git_version.py` 根据 Git 提交数生成 `V1.0.n`。

## PC 工具

`tools/Build_And_Package_All_Tools.bat` 会重新构建三个 EXE，并将它们与当前
JSON 配置打包。独立程序：

- `tools/SensorProfileManager.exe`：编辑配置；
- `tools/SensorProfileUpdater.exe`：上传配置；
- `tools/SensorDataExporter.exe`：下载和删除日志。

上传工具也可通过 `tools/Start_Profile_Updater.bat` 启动。

## 本地 Modbus 与稳定性诊断

USB 本地从站地址为 247，支持 FC03。原有地址保持兼容：

| 资料地址 | 内容 |
|---:|---|
| 40001–40002 | CH1 电流 Float32 CDAB |
| 40003–40004 | CH2 电流 Float32 CDAB |
| 40005 | MASTER 标志，PASS=0，MASTER=1 |
| 40006–40007 | CH1、CH2 ADC 原始值 |

阶段 7 从 40008 起增加只读诊断寄存器。32 位整数均为高字在前：

| 资料地址 | 内容 |
|---:|---|
| 40008 | `AppMode` |
| 40009–40010 | `Rs485Task` 模式、最近 Modbus 错误 |
| 40011–40014 | RS485 命令/事件队列当前深度及峰值 |
| 40015–40022 | RS485 事件丢失、PASS 请求/响应丢失、任务栈水位（各 UInt32） |
| 40023–40026 | SD 状态、记录标志、快照队列深度及峰值 |
| 40027–40032 | 快照丢失、命令失败、StorageTask 栈水位（各 UInt32） |
| 40033–40036 | 当前空闲堆、历史最低空闲堆（各 UInt32） |
| 40037–40040 | 日志记录数、最近成功 flush 的 `uptime_ms`（各 UInt32） |
| 40041–40042 | 当前 `uptime_ms`（UInt32） |
| 40043 | ESPLOG1 状态 |

即使应用进入 `Fault`，地址 247 的诊断读取仍可用，但不会向 RS485 转发非本地请求。

阶段 7 长时间监控示例：

```powershell
# 每分钟采样，持续2小时
python tools/monitor_stability.py COM21 --interval 60 --duration 2

# 每分钟采样，持续24小时并指定CSV
python tools/monitor_stability.py COM21 --interval 60 --duration 24 --output stability_24h.csv
```

脚本需要 `pyserial`，只读取本地诊断寄存器，不切换模式或修改设备状态。CSV
中的 `alerts` 会标记设备重启、队列/桥接丢失及任务栈低水位。输出文件已存在时
会自动追加，并读取最后一条作为续跑基线，不会覆盖已有监控数据。

## 相关文档

- `SENSOR_RUNTIME_WORKFLOW.md`：发现、轮询、配置与运行流程；
- `SD_DATA_LOGGING_IMPL_PLAN.md`：CSV、Session、分卷和导出；
- `FREERTOS_REFACTOR_PLAN.md`：任务所有权、阶段进度和稳定性验收；
- `STAGE7_STABILITY_CHECKLIST.md`：2/24 小时、重复切换和异常场景验收表；
- `tools/SENSOR_PROFILE_MANAGER_README.md`：PC 工具使用说明。

## 依赖

- GFX Library for Arduino 1.4.7
- ModbusMaster 2.0.1
- ArduinoJson 7.4.3

## TinyUSB CDC 串口参数跟随

以下描述适用于本次修订固件；刷入与实测状态见文末验收记录。

普通 PASS 模式自动跟随电脑 COM 口设置，首版支持 9600/19200/115200
与 8N1/8E1/8O1 的九种组合。屏幕显示下行实际参数；其他格式会显示
`Unsupported PC format` 并暂停下行转发，恢复有效设置后可继续。
发现、采集、校准固定为 9600 8E1；BOOT PASS 固定为 9600 8N1。
USB 配置上传、文件导出和校时仍使用原有协议。

上位机无需开启 DTR 或 RTS，也无需为本固件修改原有程序或控制线设置。
设备端发送按 USB 端点就绪状态判断，现有工具保持 DTR/RTS 关闭即可。
固件禁用 DTR/RTS 和 1200bps 自动重启；
刷入此修订版后，可在 TOOLS → FIRMWARE DOWNLOAD 从屏幕进入下载模式。
旧版或无法使用屏幕时，需按住 BOOT 再按 RESET，松开
BOOT 后选择下载端口。切换固件后 Windows 可能重新分配 COM 号。
本次 2.8 寸板验证：应用端口 COM8，ROM 下载端口 COM24。烧录后若仍停在
下载端口，可断电再上电；使用 esptool 时，`--after watchdog-reset` 已验证可恢复运行。

切换参数时，正在发送的旧帧会先结束，未完成事务和旧队列会丢弃；请在
修改设置后重新发起事务。USB 包不是 UART 字符，组帧保留 5ms 下限。

从站 247 的 FC03 新增只读诊断（旧寄存器保持兼容）：

| 资料地址 | 内容 |
|---:|---|
| 40044–40045 | 实际 UART 波特率 UInt32，高字在前 |
| 40046 | 实际 UART 校验编码：0=N、1=O、2=E |
| 40047 | 最近上位机格式是否支持：1/0 |
| 40048–40049 | RS485 任务已接收的上位机设置版本 UInt32 |
| 40050–40051 | UART 重配次数 UInt32 |

本机检查：`python tools/test_cdc_native.py`。
实机普通 PASS 验收（两个串口分别连接板端和独立 RS485 适配器）：

```powershell
python tools/test_cdc_passthrough.py COMxx COM6 --dtr off --report reports/cdc_dtr_off.json
python tools/test_cdc_passthrough.py COMxx COM6 --dtr on --report reports/cdc_dtr_on.json
```

脚本核对实际 UART 参数，并逐字节校验双向传输、在线切参、相同设置重开，
以及不支持格式的拒绝/恢复。执行步骤与实际结果见
[TINYUSB_CDC_PLAN.md](TINYUSB_CDC_PLAN.md)。

2026-10-16 实机记录：2.8 寸修订固件已刷入，DTR off/on 九组合完整透传验收通过；
屏幕下载入口、固定模式、BOOT PASS 大包、配置错误拒绝、日志查询与校时只读回归通过。
日志目录为空，GET/整文件 CRC 未覆盖；1.47 寸仅完成编译。详细边界见上述计划。
