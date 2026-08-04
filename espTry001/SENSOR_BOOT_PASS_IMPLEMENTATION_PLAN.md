# 传感器 Boot YMODEM-1K 升级透传实施与验收计划

更新日期：2026-08-04

状态：方案确认，尚未开始编码

## 1. 目标

在 ESP32-S3 设备中增加独立的 `UPGRADE PASS` 模式，使现有 Windows
传感器升级上位机无需修改，即可通过 ESP32-S3 原生 USB CDC 和板载 RS485
接口完成传感器 Bootloader 升级。

已确认的传感器 Boot 参数：

| 项目 | 参数 |
|---|---|
| 升级协议 | YMODEM-1K |
| Boot 波特率 | 9600 |
| 数据位 | 8 |
| 奇偶校验 | None |
| 停止位 | 1 |
| 串口格式 | `9600,8N1` |

传感器正常应用和 Modbus RTU 继续使用：

```text
9600,8E1
```

本阶段只实现“已有 PC 上位机经 ESP32 透明访问传感器 Bootloader”。从 SD 卡读取
固件并由 ESP32 主动发送 YMODEM 的离线升级功能仍按照
`SD_DATA_AND_SENSOR_FIRMWARE_PLAN.md` 独立实施。

## 2. 为什么不能直接复用现有 PASS

当前 PASS 是 Modbus RTU 网关，不是任意二进制流桥：

- USB 侧按 5 ms 静默间隔组装 RTU 帧；
- USB RTU 帧最大 256 字节；
- RS485 请求按完整消息写入 `MessageBuffer`；
- 地址 247 会被 ESP32 本机 Modbus 从站拦截；
- RS485 返回缓冲按短 Modbus 响应设计。

YMODEM-1K 数据包长度为 1029 字节：

```text
STX(1) + SEQ(1) + ~SEQ(1) + DATA(1024) + CRC16(2)
```

因此只把 UART 校验位改成 None 不够。继续使用现有 PASS 会造成分包、截断、地址
误判或缓冲区溢出。`UPGRADE PASS` 必须使用独立的原始字节流路径。

## 3. 总体模式设计

新增应用模式：

```cpp
AppMode::UpgradePass
```

新增 RS485 Task 模式：

```cpp
Rs485Service::Mode::RawPassThrough
```

模式关系：

```text
普通 PASS
  UART 固定 9600,8E1
  Modbus RTU 组帧
  地址 247 由 ESP32 处理
          |
          | 用户确认进入
          v
UPGRADE PASS
  UART 跟随 USB CDC Line Coding
  允许 9600,8E1 / 9600,8N1
  原始字节双向转发
  不解析 Modbus 或 YMODEM
          |
          | 用户退出或故障恢复
          v
普通 PASS
  清空残留字节
  恢复 9600,8E1
```

## 4. 上位机串口参数跟随

已有升级上位机可能先执行以下动作：

1. 用 `9600,8E1` 向传感器应用发送进入 Bootloader 命令；
2. 关闭并重新打开 COM，或通过 `SetCommState` 改成 `9600,8N1`；
3. 等待 Bootloader 周期发送字符 `C`；
4. 开始 YMODEM-1K。

ESP32-S3 Arduino USB CDC 提供 `ARDUINO_USB_CDC_LINE_CODING_EVENT`，事件包含：

```text
bit_rate
data_bits
parity
stop_bits
```

仅在 `UPGRADE PASS` 中应用该事件：

| CDC 参数 | RS485 UART 行为 |
|---|---|
| `9600,8E1` | 重新初始化为 `SERIAL_8E1` |
| `9600,8N1` | 重新初始化为 `SERIAL_8N1` |
| 其他波特率 | 拒绝转发并显示 `UNSUPPORTED BAUD` |
| 非 8 数据位 | 拒绝并显示 `UNSUPPORTED DATA BITS` |
| 其他校验/停止位 | 拒绝并显示 `UNSUPPORTED FORMAT` |

普通 PASS、MASTER、PRECHECK、ESPCFG1、ESPLOG1 和 ESPTIME1 不跟随 CDC Line
Coding，继续保持现有行为。

UART 的 `end()`、`begin()`、串口排空和状态发布只能在 `Rs485Task` 中执行。USB
事件回调只保存一个固定大小的待应用配置，不能直接操作 `Serial1`。

## 5. 原始字节桥设计

UPGRADE PASS 不识别 YMODEM 包边界：

```text
USB CDC
  -> USB-to-RS485 StreamBuffer
  -> Rs485Task
  -> Serial1

Serial1
  -> RS485-to-USB StreamBuffer
  -> UsbService
  -> USB CDC
```

建议容量：

| 缓冲 | 建议容量 | 原因 |
|---|---:|---|
| USB → RS485 | 4096 字节 | 可容纳至少 3 个 YMODEM-1K 包的瞬时数据 |
| RS485 → USB | 2048 字节 | Boot 返回内容较少，同时保留异常输出余量 |
| 单次搬运 | 64～256 字节 | 快速返回，不长期占用 loopTask/Rs485Task |

写入规则：

- USB 侧只读取 StreamBuffer 当前可接收的字节数；
- 不允许先从 USB 读取后再因缓冲满而静默丢弃；
- UART 写入允许短写，未写完部分必须留待下一轮；
- USB 写入允许短写，未写完部分必须保留偏移；
- 不使用 5 ms RTU 静默间隔；
- 不检查首字节是否为 247；
- 不计算或修改 YMODEM CRC16；
- YMODEM 的 `SOH/STX/ACK/NAK/CAN/C/EOT` 全部原样透传。

增加诊断计数：

```text
raw_usb_to_rs485_bytes
raw_rs485_to_usb_bytes
raw_usb_to_rs485_drops
raw_rs485_to_usb_drops
raw_tx_buffer_peak
raw_rx_buffer_peak
uart_reconfigure_count
last_uart_config_error
```

任何 drop 计数增加都视为升级链路失败，不能只作为普通告警继续显示成功。

## 6. 进入与退出顺序

### 6.1 进入 UPGRADE PASS

1. 仅允许从普通 PASS 请求进入；
2. 停止传感器发现、轮询和 PRECHECK；
3. 确认日志已经停止并关闭 CSV；
4. 清空 USB RTU 半帧、本机 Modbus半帧及现有 PASS 缓冲；
5. 向 `Rs485Task` 发送 `EnterRawPass` 命令；
6. `Rs485Task` 排空 UART 并建立原始流缓冲；
7. 默认先使用 `9600,8E1`，兼容上位机发送进入 Boot 命令；
8. 收到 CDC Line Coding 后按白名单切换；
9. `Rs485Task` 发布已生效配置后，UI 才显示 `READY`；
10. 开始原始双向转发。

### 6.2 退出 UPGRADE PASS

1. 停止接收新的 USB 原始数据；
2. 给已接收的 UART TX 数据有限时间发送完成；
3. 清空两个原始 StreamBuffer 和 USB 残留数据；
4. `Rs485Task` 执行 `Serial1.end()`；
5. 恢复 `9600,SERIAL_8E1`；
6. 读空 UART 中 Bootloader 的迟到字节；
7. 清零原始传输临时状态，但保留本次统计供 UI 查看；
8. `Rs485Task` 确认恢复完成；
9. 返回普通 PASS。

不得在未确认 UART 恢复完成前进入 MASTER 或 PRECHECK。

## 7. UI 规划

在普通 PASS 页面点击 `485 Config: 9600,8E1` 区域，进入确认页：

```text
UPGRADE BRIDGE

PC updater controls UART:
9600,8E1 -> 9600,8N1

Raw YMODEM-1K bridge
Do not disconnect power

[ ENTER UPGRADE PASS ]
```

工作页面：

```text
UPGRADE PASS

UART: 9600,8N1
State: TRANSFERRING

PC -> SENSOR: 123456 bytes
SENSOR -> PC: 234 bytes
Drops: 0 / 0

[ EXIT AND RESTORE 8E1 ]
```

状态至少包括：

```text
PREPARING
WAITING PC
9600,8E1
9600,8N1
TRANSFERRING
UNSUPPORTED FORMAT
BUFFER ERROR
RESTORING 8E1
READY TO EXIT
```

进入确认页可以返回；开始原始传输后，页面必须明确提示不要断电或切换模式。

## 8. 安全边界

- UPGRADE PASS 不自动发送进入 Bootloader 命令；
- UPGRADE PASS 不读取、修改或保存 PC 固件文件；
- UPGRADE PASS 不宣称升级成功，只显示链路状态和字节计数；
- 升级成功与否仍由现有上位机和传感器 Bootloader 判断；
- 模式中禁止 MASTER、PRECHECK、配置上传、日志导出和校时；
- 不因 USB 暂时断开自动退出，兼容上位机通过关闭/重开 COM 改变校验位；
- 设备重新上电默认回普通 PASS 和 `9600,8E1`；
- 不把上一次保存的 `8N1` 当作开机默认值；
- 不在 USB 事件回调、UI 或 loopTask 中直接重配 RS485 UART；
- 不允许丢字节计数非零时显示正常状态；
- 第一版不自动重试整次固件升级。

## 9. 分阶段实施

### 阶段 BP-A：UART 动态切换

实现：

- 增加串口配置结构和白名单校验；
- 增加 `EnterRawPass / ApplyLineCoding / ExitRawPass` 命令；
- 由 `Rs485Task` 独占执行 UART 重初始化；
- 增加配置生效确认和错误状态；
- 暂不转发 YMODEM 数据。

通过条件：能够稳定完成 `8E1 -> 8N1 -> 8E1`，普通 Modbus 回归正常。

### 阶段 BP-B：原始双向字节桥

实现：

- 增加两个 StreamBuffer；
- UsbService 增加完全绕开 RTU 解析的路径；
- 支持 USB/UART 短写和剩余偏移；
- 增加字节、峰值和丢失计数。

通过条件：任意二进制数据逐字节一致，1029 字节包不截断。

### 阶段 BP-C：CDC Line Coding 跟随

实现：

- 注册 `ARDUINO_USB_CDC_LINE_CODING_EVENT`；
- 仅在 UPGRADE PASS 转换并应用参数；
- 支持上位机关闭并重新打开 COM；
- 屏幕显示实际生效格式。

通过条件：已有上位机切换校验位时，UART 自动同步且不重启 ESP32。

### 阶段 BP-D：UI 与模式编排

实现：

- 增加确认页和工作页；
- AppController 统一停止日志和 RS485 现有工作；
- 锁定冲突模式；
- 退出后确认恢复普通 PASS。

通过条件：任何入口和退出路径都不会留下 `8N1` 或未关闭日志。

### 阶段 BP-E：YMODEM-1K 与真实传感器联调

实现：

- 使用现有升级上位机；
- 先连接模拟/开发板接收端；
- 再连接真实传感器；
- 记录成功升级抓包和超时参数。

通过条件：同一固件通过 ESP32 桥接升级的结果与原 USB-RS485 转换器一致。

## 10. 逐步验收步骤

### 验收 0：建立直连基线

目的：确认现有上位机、固件文件和传感器 Bootloader 本身正常。

操作：

1. 使用原有 USB-RS485 转换器直接连接传感器；
2. 打开现有升级上位机；
3. 记录初始串口参数；
4. 执行一次完整升级；
5. 保存上位机日志；
6. 记录是否先使用 8E1、何时切换为 8N1；
7. 记录首个 `C` 的等待时间、总耗时和最终版本；
8. 如条件允许，保存一份串口抓包。

通过标准：

- 原转换器下同一固件可重复成功升级；
- 明确上位机切换串口格式的实际顺序；
- 明确 EOT 后传感器的重启和版本确认方式。

### 验收 1：UART 格式切换

目的：单独验证 ESP32 UART 配置，不传输固件。

操作：

1. 烧录 BP-A 固件；
2. 上电确认普通 PASS 显示 `9600,8E1`；
3. 使用示波器或逻辑分析仪发送固定字节 `0x55`；
4. 确认 8E1 波形；
5. 进入 UPGRADE PASS；
6. 从 PC 把 COM 设置为 `9600,8N1`；
7. 再发送 `0x55` 并确认无校验位波形；
8. 退出 UPGRADE PASS；
9. 再次确认 UART 回到 `9600,8E1`；
10. 切换 MASTER，确认可以发现并读取传感器。

通过标准：

- 三次显示和实际波形完全对应；
- ESP32 没有重启；
- 退出后 Modbus CRC、发现和轮询正常；
- UART 重配置错误计数为 0。

### 验收 2：短原始数据双向透传

目的：确认不再经过 Modbus RTU 解析。

测试数据必须包含：

```text
00 01 02 43 06 15 18 F7 FF
```

其中 `0x43/0x06/0x15/0x18` 分别覆盖 YMODEM 的 `C/ACK/NAK/CAN`，`0xF7`
覆盖本机 Modbus 地址 247。

操作：

1. PC 经 USB 向 ESP32 发送测试数组；
2. RS485 端使用接收器保存全部字节；
3. 逐字节比较；
4. 从 RS485 端反向发送相同数组；
5. PC 保存并逐字节比较；
6. 连续重复 100 次。

通过标准：

- 两个方向长度和内容完全一致；
- `0xF7` 不被 ESP32 截获；
- 零字节不会截断数据；
- 两个 drop 计数均为 0。

### 验收 3：单个 YMODEM-1K 尺寸边界

目的：验证现有 256 字节限制已经完全绕开。

在 2.8 寸设备进入 `BOOT PASS`、COM7 为 ESP32、COM5 为独立 USB-RS485
转换器时，可先运行自动双向测试：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tools\test_boot_pass.py COM7 COM5 --count 5
```

脚本每次都以一次 `write()` 发送完整 1029 字节包，覆盖递增、全零、全
`0xFF` 和固定随机数据，并逐字节检查两个方向。

操作：

1. 构造一个完整 1029 字节 STX 包；
2. DATA 区使用递增值、全 `0x00`、全 `0xFF` 和随机数四组内容；
3. 每组各发送 100 包；
4. 接收端保存每一包；
5. 对比长度、序号、反码、DATA 和 CRC16；
6. 同时观察缓冲峰值和丢失计数。

通过标准：

- 400 个包全部为 1029 字节；
- 内容与发送端完全一致；
- 不发生 256 字节截断或 5 ms 分帧；
- 丢失计数为 0；
- UI、触摸和 USB 保持响应。

### 验收 4：连续流和背压

目的：验证 PC 瞬时写入速度高于 9600 UART 时不会静默丢包。

操作：

1. PC 一次写入一个完整 1029 字节包；
2. 不人为拆成小块；
3. 接收端延迟返回 ACK，分别测试 0、100、500 和 1000 ms；
4. 每种延迟连续测试 100 包；
5. 观察 USB 写入是否正确等待，以及 ESP32 缓冲峰值；
6. 制造一次缓冲区无法继续接收的测试条件。

通过标准：

- 正常条件下不丢包；
- PC 写入能够受到合理背压；
- 人为溢出时明确进入 `BUFFER ERROR`；
- 不允许溢出后继续显示正常升级状态。

### 验收 5：CDC Line Coding 切换

目的：验证现有上位机无需修改。

操作：

1. 进入 UPGRADE PASS；
2. PC 以 `9600,8E1` 打开 COM；
3. 确认屏幕和 UART 都显示/使用 8E1；
4. PC 不关闭 COM，直接改成 `9600,8N1`；
5. 确认 UART 自动变为 8N1；
6. 再测试关闭 COM 后以 8N1 重新打开；
7. 测试不支持的 `115200,8N1`；
8. 恢复 `9600,8N1`；
9. 退出模式并确认恢复 8E1。

通过标准：

- 支持的两种格式正确生效；
- 关闭/重开 COM 不会让设备退出或重启；
- 不支持的格式不转发数据并明确提示；
- 普通 PASS 不受 Line Coding 事件影响。

### 验收 6：PC YMODEM 模拟接收端

目的：在连接真实传感器前验证完整协议流量。

操作：

1. RS485 侧连接 STM32 开发板或已验证的 YMODEM 接收器；
2. PC 侧使用现有升级上位机；
3. 发送小文件、恰好 1024 字节文件、1025 字节文件和真实固件大小文件；
4. 分别覆盖 Block 0、单包、跨包、最后包填充和 EOT；
5. 接收端保存实际文件；
6. 对比原文件长度、CRC32 和 SHA-256；
7. 每类至少重复 5 次。

通过标准：

- 所有接收文件与原文件逐字节一致；
- YMODEM 重试可以正常穿过桥接；
- ESP32 drop 计数始终为 0；
- 上位机无需任何代码修改。

### 验收 7：真实传感器正常升级

操作：

1. 记录传感器当前 PN 和固件版本；
2. 使用原转换器再次确认该固件包可升级；
3. 改由 ESP32 连接传感器；
4. 进入 UPGRADE PASS；
5. 运行现有上位机；
6. 确认先发送进入 Boot 命令；
7. 确认屏幕从 8E1 自动切到 8N1；
8. 完成 YMODEM-1K；
9. 等待传感器重启；
10. 退出 UPGRADE PASS；
11. 切换 MASTER 并重新发现；
12. 读取并记录升级后的固件版本；
13. 检查两个 drop 计数。

通过标准：

- 上位机报告成功；
- 传感器能够正常重启；
- 新版本与目标一致；
- MASTER 发现、参数读取和日志记录正常；
- drop 计数为 0；
- ESP32 全程没有重启或失去触摸响应。

### 验收 8：异常与恢复

每项使用可恢复的测试传感器，且必须先确认 Bootloader 恢复方法。

场景：

1. 等待 `C` 时 PC 超时退出；
2. Block 0 后断开 USB；
3. 数据包中途关闭上位机；
4. 上位机关闭 COM 后重新打开；
5. 人工制造一个 NAK；
6. 人工制造一次 CRC16 错误包；
7. 传感器不响应 EOT；
8. UPGRADE PASS 中尝试点击 MASTER；
9. UPGRADE PASS 中让 ESP32 重新上电。

通过标准：

- ESP32 不死机、不看门狗复位；
- 页面显示可区分的失败原因；
- USB 重连后原始桥仍可继续或由用户安全退出；
- 退出后 UART 必定恢复 9600,8E1；
- 普通 PASS 和 MASTER 可再次工作；
- ESP32 重启后默认是普通 PASS，不保留 8N1。

### 验收 9：现有功能回归

完成真实升级后逐项测试：

- [ ] 普通 PASS 的 9600,8E1 Modbus 透传；
- [ ] 本机地址 247 响应；
- [ ] MASTER 普通发现和特殊发现；
- [ ] MASTER 参数轮询；
- [ ] PRECHECK 地址 15 固定读取；
- [ ] 4-20mA 两通道显示；
- [ ] SD 日志创建、写入和停止；
- [ ] 新日期时间日志文件名；
- [ ] ESPLOG1 列表、STAT、下载和 CRC32；
- [ ] ESPCFG1 配置上传；
- [ ] ESPTIME1 USB/网络校时；
- [ ] 触摸页面切换；
- [ ] 一小时高水位监控。

最终通过标准：UPGRADE PASS 的加入不能改变任何现有模式的串口格式、协议所有权、
日志完整性或稳定性指标。

## 11. 开始编码前仍需记录的信息

- 现有上位机进入 Boot 前是否明确使用 9600,8E1；
- 上位机通过 `SetCommState` 原地切换，还是关闭/重开 COM；
- 进入 Boot 命令的完整报文；
- Bootloader 首个 `C` 的典型和最大等待时间；
- YMODEM Block 0 文件名和长度格式；
- EOT 握手的实际顺序；
- 升级完成后的自动重启时长；
- 固件版本的 Modbus 查询地址；
- 中断升级后重新进入 Bootloader 的恢复方法；
- 一份使用原 USB-RS485 转换器成功升级的完整日志或抓包。

## 12. 提交拆分建议

建议每个阶段单独提交：

```text
BP-A  feat: 增加RS485 UART动态格式切换
BP-B  feat: 增加升级原始字节透传
BP-C  feat: 跟随USB CDC串口参数
BP-D  feat: 增加UPGRADE PASS界面与模式编排
BP-E  test: 完成YMODEM-1K升级桥实机验证
```

每次提交前至少编译 `waveshare28` 和 `esp32s3` 两个环境；只有 BP-E 实机验收完成后，
才将 UPGRADE PASS 标记为可用于正式传感器升级。
