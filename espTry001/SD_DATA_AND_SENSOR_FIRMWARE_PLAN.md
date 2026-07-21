# SD 数据记录与传感器固件升级规划

本文规划 ESP32-S3 工装后续接入 SD 卡后的两项能力：

1. 在 MASTER 模式记录传感器参数和两路 4-20mA 数据；
2. 从 SD 卡读取匹配当前 PN 的 STM32 固件，并通过 RS485 使用 YMODEM 协议升级传感器。

实施顺序已经确定：**先完成 SD 数据记录，最后实现传感器升级**。升级协议调试风险较高，不能阻塞基础记录功能交付。

## 1. 已确定的原则

- SD 卡未插入、损坏或写满时，PASS、MASTER、触摸、显示和 JSON Profile 仍能工作；
- LittleFS 继续存储 `/sensor_profiles.json`，不把启动必需配置迁移到可拔出的 SD 卡；
- SD 卡只存日志、升级固件、升级清单和升级结果；
- SD 读写、YMODEM 和 RS485 升级都使用非阻塞状态机，不允许长时间卡住 `loop()`；
- 日志按一次完整 Profile 扫描生成一条记录，不能混合两轮参数；
- 传感器升级期间 RS485 必须独占，停止发现、参数轮询和 USB PASS 转发；
- 固件包必须匹配发现到的 PN，并在发送前校验文件大小和 CRC32；
- 第一版不做复杂文件浏览器，根据 PN 自动选择清单中的固件，降低小屏 UI 复杂度；
- YMODEM 升级放在最后阶段，数据记录验收通过后才开始。

## 2. 当前硬件资源

当前工程已占用：

| 功能 | GPIO |
|---|---|
| LCD SPI SCK/MOSI/CS/DC | 38 / 39 / 21 / 45 |
| LCD RST/背光 | 40 / 46 |
| 触摸 I2C SDA/SCL | 42 / 41 |
| 触摸 RST/INT | 47 / 48 |
| RS485 RX/TX | 43 / 44 |
| 4-20mA ADC CH1/CH2 | 1 / 2 |

SD 接口已经确认使用 SD_MMC 4-bit，映射如下：

| SD 信号 | GPIO |
|---|---:|
| D2 | 13 |
| D3 | 14 |
| CMD | 15 |
| CLK | 16 |
| D0 | 17 |
| D1 | 18 |

当前 Arduino-ESP32 `SD_MMC` 库支持 ESP32-S3 自定义六线映射，初始化顺序为：

```cpp
SD_MMC.setPins(16, 15, 17, 18, 13, 14);
//             CLK CMD D0 D1 D2 D3
SD_MMC.begin("/sdcard", false, false);
//                         ^ 4-bit 模式（mode1bit=false）
```

注意事项：

- 这组 GPIO 与当前 LCD、触摸、RS485 和两路 ADC 不冲突；
- 硬件已确认：CMD、D0～D3 具备上拉电阻；
- SD 卡和模块使用 3.3V 电平，并检查供电峰值和去耦；
- GPIO19 已确认没有连接，硬件没有 Card Detect；
- 第一版只在启动时初始化和挂载 SD 卡，不实现运行中插卡自动识别；
- 拔卡前必须停止记录，完成 `flush()`、关闭文件并显示 `Safe to remove SD`；更稳妥的操作是关机后拔卡；
- 运行中意外拔卡只能通过后续文件 I/O 失败被动发现，设备应停止记录并提示错误，不能继续写入或自动重启；
- 4-bit 高速模式不稳定时，冒烟测试阶段降低 SDMMC 时钟，不通过反复重试掩盖硬件问题。

## 3. 目标代码结构

建议新增：

```text
src/sd_storage.h/.cpp
src/data_logger.h/.cpp
src/firmware_package.h/.cpp
src/ymodem_sender.h/.cpp
src/sensor_firmware_updater.h/.cpp
```

职责划分：

| 模块 | 职责 |
|---|---|
| `SdStorage` | SD 初始化、卡状态、目录、容量、文件操作和错误信息 |
| `DataLogger` | 采集完整扫描快照、缓存 CSV、定时刷新和文件分卷 |
| `FirmwarePackage` | 读取 manifest、按 PN 匹配固件、大小和 CRC32 校验 |
| `YmodemSender` | 与具体业务无关的非阻塞 YMODEM/YMODEM-1K 发送状态机 |
| `SensorFirmwareUpdater` | 进入 Bootloader、独占 RS485、驱动 YMODEM、恢复正常通信 |

`main.cpp` 只负责模式切换、触摸事件、状态机调用和页面刷新，不能把 SD/YMODEM 细节继续堆在主文件中。

## 4. 阶段 A：SD 硬件和文件系统冒烟测试

目标：先证明 SD 卡电气连接和文件操作稳定，不接入 MASTER 数据。

任务：

- [x] 确认使用 SD_MMC 4-bit，GPIO 为 CLK16、CMD15、D0～D3=17/18/13/14；
- [x] 确认 CMD、D0～D3 上拉电阻已安装；
- [x] 确认 GPIO19 未连接，不具备 Card Detect；
- [ ] 初始化 SD，识别卡类型、总容量和剩余容量；
- [ ] 创建 `/diagnostic.txt`，写入、关闭、重新打开并核对内容；
- [ ] 连续写入至少 10 MB，确认没有随机写入失败；
- [ ] System Info 显示 `SD: Ready/Missing/Error`、容量和最近错误；
- [ ] 未插卡启动和运行中意外拔卡不影响现有 PASS、MASTER、触摸和显示；
- [ ] 第一版明确不支持运行中重新插卡，重新插卡后需要重启设备；

验收：连续运行 2 小时无死机，拔卡或写入失败后触摸和 RS485 仍正常。

预计改动：`SdStorage` 100～180 行，`main.cpp`/UI 约 50～100 行。

## 5. 阶段 B：MASTER 数据记录

### 5.1 完整扫描事件

当前 `SensorProfilePoller::update()` 在完成一个寄存器块时返回，不代表整个 Profile 已完成。需要增加独立的整轮完成事件，例如：

```cpp
bool takeScanCompleted();
uint32_t scanSequence() const;
```

最后一个读取块成功或失败并完成后，递增扫描序号。`DataLogger` 只在序号变化时复制：

- 真实从站地址；
- PN 和 JSON `name`；
- CH1/CH2 mA；
- `profileValues[0..fieldCount-1]`；
- 每个字段的有效标志；
- 时间戳或启动时间。

### 5.2 CSV 格式

建议每次启动或更换 PN 创建新文件：

```text
/logs/PN53607_20260720_151001.csv
```

示例：

```csv
time,uptime_ms,address,pn,CH1_mA,CH2_mA,sensorValue,pH,Temp_C
2026-07-20 15:10:01,123456,10,53607,4.1,12.3,8.62,7.01,25.2
```

规则：

- 第一行写字段名，字段顺序与 JSON Profile 一致；
- 无效或超时字段留空，同时可增加 `valid_mask`；
- float 按 JSON `decimals` 输出；
- `uint16` 不输出小数；
- 文件头注释记录 `config_version`、设备名称和固件 Git 版本；
- Profile 改变、PN 改变或字段结构改变时必须新建文件。

### 5.3 时间来源

当前硬件没有独立 RTC，因此第一版采用“会话编号 + 相对时间”，真实日期是可选增强：

| 方案 | 特点 |
|---|---|
| `session_id + uptime_ms` | 第一版必选；不依赖时钟，掉电后仍可区分记录批次和相对顺序 |
| USB 上位机设置时间 | PC 在线时可发送 Unix 时间，断电后失效，但本次运行期间可生成真实时间 |
| 后续增加 DS3231 RTC | 当前不实施，硬件下个版本需要独立长期时间时再考虑 |

会话规则：

- NVS 保存单调递增的 `session_id`，每次开始新日志加 1；
- CSV 每行始终包含 `session_id`、`sample_seq` 和 `uptime_ms`；
- 未校时时 `time` 留空，文件名使用 `PN53607_S000123.csv`；
- PC 通过 USB 校时后，同时记录 ISO 时间，并在文件头写 `time_source=usb`；
- 日志分析不能依赖 FAT 文件修改时间，因为未校时时它可能不代表真实日期。

### 5.4 写入策略

- RAM 中使用固定长度环形缓冲，不使用不断增长的 `String`；
- 每累计约 4 KB 或达到 1 秒批量写入；
- 每 5～10 秒 `flush()`，模式切换和停止记录时立即关闭；
- 写入时间过长时不能阻塞 RS485 超时判断；必要时拆分为小块写入；
- 文件达到约 32～64 MB 后分卷；
- 写满、拔卡或 I/O 错误后停止记录并在屏幕提示，不自动重启设备。

### 5.5 UI

- System Info：SD 状态、容量、日志文件、写入错误；
- SENSOR INFO：显示 `REC`、记录条数和写入状态；
- 第一版可在 MASTER 识别成功后自动开始记录；
- 后续再增加点击开始/停止，避免第一版触摸逻辑过度复杂。

验收：使用包含 16 个 float 的 Profile 连续记录 24 小时，CSV 行结构一致、没有半行、触摸无明显卡顿、Modbus 轮询不因 SD 写入大量超时。

预计改动：300～500 行；包含 UI 和异常恢复约 1～3 天。

## 6. 阶段 C：日志管理与导出

### 6.1 第一版：直接读取 SD 卡

第一版采用最简单、最容易验证的方法：

1. 在屏幕上停止记录；
2. `DataLogger` 刷新缓存并关闭 CSV；
3. 屏幕显示 `Safe to remove SD`；
4. 操作员拔卡，在电脑上直接用 Excel、LibreOffice 或 Python 打开 CSV；
5. 如果硬件没有 Card Detect，建议关机后再拔卡。

不能在文件仍打开时拔卡，否则最后一批缓存、FAT 目录或整个文件都可能损坏。

### 6.2 正式版：USB LOG EXPORT

推荐在现有 USB CDC 上增加独占的 `LOG EXPORT` 模式，而不是一开始实现 USB Mass Storage：

```text
System Info -> LOG EXPORT
  -> 停止 MASTER 日志
  -> flush 并关闭 SD 文件
  -> 禁止 PASS、CONFIG UPLOAD 和其他 USB 协议
  -> PC 上位机列出日志
  -> 下载所选 CSV
  -> 校验长度和 CRC32
  -> 退出后重新挂接正常工作流
```

原因：

- 当前工程已经有稳定的 USB CDC 分块握手机制，可复用设计；
- ESP32 仍管理 FAT 文件系统，只向 PC 发送指定文件，不会出现 Windows 与 ESP32 同时挂载造成的文件系统损坏；
- 可以支持断点续传、CRC32、批量下载和明确的错误信息；
- 不要求同事安装读卡器或拆开工装。

建议新增：

```text
src/log_export_server.h/.cpp
tools/SensorDataExporter.cs
```

PC 上位机功能：

- 选择 COM 口；
- 设备日志列表：文件名、PN、大小、记录数和会话编号；
- 选择保存目录并批量下载；
- 下载完成后校验字节数和 CRC32；
- 一键打开所在文件夹；
- 删除设备日志必须二次确认，第一版可以完全不提供远程删除；
- 中文、英文、西班牙文界面可复用配置管理器的语言设计。

设备端协议建议使用新的魔术字 `ESPLOG1`，不能与 `ESPCFG1` 混用。至少包含：

```text
LIST                  获取日志目录
GET path offset       从指定偏移下载，支持断点续传
STAT path             获取大小和CRC32
CLOSE                 安全退出导出模式
```

文件内容按固定块发送，每块由 PC 确认后再发下一块，避免 USB CDC 缓冲区溢出。

### 6.3 暂不首选 USB Mass Storage

ESP32-S3 可以把 SD 卡暴露为 USB MSC，但 PC 和 ESP32 不能同时安全地修改同一个 FAT 文件系统。实现时必须停止记录、卸载 SD，再把块设备交给 PC，并处理 Windows 缓存和“安全弹出”。它适合作为后续可选功能，不作为第一版导出方案。

### 6.4 日志管理任务

- [ ] 文件命名、分卷和最大保留空间策略；
- [ ] SD 剩余空间低于阈值时提前报警；
- [ ] 确定是否自动删除最旧日志，默认建议不自动删除；
- [ ] 第一版完成“停止记录并安全拔卡”的 UI；
- [ ] 第二版实现 USB `ESPLOG1` 导出和三语言上位机；
- [ ] 文档说明没有 Card Detect 时必须停止记录或关机后再拔卡。

完成阶段 C 后，SD 数据记录功能可以单独发布，不等待传感器升级。

## 7. 阶段 D：固件包格式和离线校验

建议目录：

```text
/firmware/
  manifest.json
  PN53607/
    V1.2.3.bin
```

Manifest 示例：

```json
{
  "schema_version": 1,
  "packages": [
    {
      "product_num": 53607,
      "version": "1.2.3",
      "protocol": "ymodem-1k",
      "file": "PN53607/V1.2.3.bin",
      "size": 262144,
      "crc32": "12AB34CD",
      "baudrate": 115200,
      "serial": "8N1"
    }
  ]
}
```

任务：

- [ ] 读取和校验 manifest；
- [ ] 按当前 PN 精确匹配，不允许使用通配 PN；
- [ ] 检查文件存在、长度、CRC32；
- [ ] 明确同一 PN 多版本时的选择规则；
- [ ] 默认禁止降级，除非 manifest 明确允许；
- [ ] 固件校验失败时绝不进入 Bootloader；
- [ ] 后续量产版本考虑增加数字签名，CRC32 只能检错，不能防止错误或恶意固件。

这一阶段只管理和校验文件，不向传感器发送升级数据，便于独立测试。

## 8. 阶段 E：YMODEM 发送器单元测试

当前假设传感器使用 STM32 常见的 YMODEM/YMODEM-1K IAP。ESP32 是发送端，STM32 Bootloader 是接收端。

建议状态机：

```text
Idle
  -> WaitReceiverC
  -> SendBlock0
  -> WaitBlock0Ack
  -> SendDataBlock
  -> WaitDataAck
  -> SendEot
  -> FinishHandshake
  -> SendEmptyBlock0
  -> Success / Failed
```

实现要求：

- 支持 `SOH` 128 字节 Block 0；
- 优先支持 `STX` 1024 字节数据块；
- 包序号及反码；
- CRC16-XMODEM，高字节先发送；
- 最后一包使用 `0x1A` 填充；
- 处理 `C`、`ACK`、`NAK`、`CAN` 和 `EOT`；
- 超时、分包重试和总失败上限；
- 从 `File` 流式读取，每次只占用约 1 KB RAM；
- 暂时不接真实传感器，先用 PC YMODEM 接收器或 STM32 开发板验证完整文件；
- 接收端保存的文件必须与 SD 原始文件逐字节一致。

不同 STM32 IAP 对 EOT 结束握手可能存在差异，不能只凭标准文档假设。必须取得现有升级工具的成功抓包。

预计改动：YMODEM 核心 400～650 行，测试辅助代码另计。

## 9. 阶段 F：真实传感器升级

开始前必须确认：

- [ ] 进入 Bootloader 的 Modbus 寄存器或特殊报文；
- [ ] Bootloader 波特率、数据位、校验位和停止位；
- [ ] 是否周期发送 `C`（0x43）；
- [ ] 支持 YMODEM-1K 还是仅支持 128 字节块；
- [ ] EOT 握手顺序；
- [ ] `.bin` 文件格式、最大长度和应用起始地址；
- [ ] 升级成功后的重启和版本查询方法；
- [ ] 中途掉电后的 Bootloader 恢复能力；
- [ ] 一份现有 PC 工具成功升级的完整抓包。

建议显式管理 RS485 所有权：

```cpp
enum class Rs485Owner {
  PassThrough,
  Discovery,
  ParameterPolling,
  FirmwareUpdate
};
```

升级开始时：

1. 校验当前 PN 与固件包；
2. 停止 `SensorProfilePoller` 和 `SensorDiscovery`；
3. 禁止 USB PASS；
4. 清空串口缓冲区；
5. 发送进入 Bootloader 命令；
6. 按要求重新初始化 `Serial1`；
7. 启动 `YmodemSender`；
8. 屏幕显示文件名、版本、百分比、包号和重试；
9. 完成后恢复 `9600 8E1`；
10. 重新发现 PN，并读取版本寄存器确认结果。

失败处理：

- 不得自动重复整次升级，必须让操作员确认；
- 保留升级日志到 `/upgrade/`；
- 区分进入 Bootloader 失败、等待 `C` 超时、包 NAK、CRC 错、EOT 失败和重启失败；
- 不得在升级过程中切换 PASS/MASTER；
- 屏幕必须提示不能断电。

预计改动：RS485/升级编排和 UI 约 300～500 行；在 YMODEM 核心完成后，真实设备联调预计 2～4 天，协议差异可能继续增加时间。

## 10. 总工作量估计

| 阶段 | 预计代码量 | 预计时间 | 风险 |
|---|---:|---:|---|
| A：SD 冒烟测试 | 150～280 行 | 0.5～1 天 | 低，主要是硬件 |
| B/C：数据记录和管理 | 300～600 行 | 1～3 天 | 中，关注写入阻塞和拔卡 |
| D：固件包管理 | 200～350 行 | 1～2 天 | 低～中 |
| E：YMODEM 发送器 | 400～650 行 | 2～3 天 | 中 |
| F：真实传感器升级 | 300～500 行 | 2～4 天以上 | 高，取决于 Bootloader 差异 |

建议把阶段 A～C 作为第一个独立里程碑并单独提交、测试、发布。阶段 D～F 建立在已经稳定的 SD 层之上，升级调试失败时不会影响数据记录版本。

## 11. 下一步

开始代码前只做两件事：

1. 开始实现 `SdStorage`，按已确认的 4-bit 引脚完成初始化、容量查询和诊断文件测试；
2. 第一版按“停止记录并关闭文件后拔卡，重新插卡需要重启”验收；
3. 后续再决定 USB LOG EXPORT 是独立上位机，还是合并进现有工具。

SD_MMC 4-bit 映射、上拉电阻、无 Card Detect 和无 RTC 时间策略均已确定。下一步从 `SdStorage` 冒烟测试开始，不提前编写 YMODEM 升级代码。
