# Waveshare 2.8 寸 RTC 与日志时间戳实施计划

更新日期：2026-08-03

状态：RTC-A~RTC-D 软件实现完成，RTC-C 实机验收通过；RTC-F 软件完成，待实机验收。

## 1. 目标

在 Waveshare ESP32-S3-Touch-LCD-2.8 V2 上启用板载 PCF85063ATL RTC，并配合
ML1220 可充电纽扣电池实现：

- 主板完全断电后继续保持日期和时间；
- Dashboard 显示当前北京时间；
- System Info 显示完整日期、时间和 RTC 状态；
- 每条 CSV 数据记录带有明确时区的 ISO 8601 时间戳；
- 保留 `uptime_ms`，用于分析重启、采样周期和时间跳变；
- 有已配置 Wi-Fi 时，开机后在后台通过 NTP 校准 RTC；
- RTC 缺失、未校时或失效时，现有 PASS、MASTER、PRECHECK 和日志功能仍能工作。

本计划不实现 RTC 闹钟、定时器、CLKOUT 或 GPIO9 中断唤醒。

## 2. 已确认的硬件

### 2.1 RTC

| 项目 | 参数 |
|---|---|
| 芯片 | PCF85063ATL |
| I2C 地址 | `0x51` |
| SDA | GPIO11 |
| SCL | GPIO10 |
| INT | GPIO9，第一阶段不用 |
| 晶振 | 32.768 kHz |
| RTC 电池接口 | J7，SH1.0-2P |
| J7 Pin 1 | RTC VDD / 电池正极 |
| J7 Pin 2 | GND / 电池负极 |

RTC 使用 GPIO10/11，与触摸使用的 GPIO1/3 是两条独立 I2C 总线。GPIO10/11
同时连接板载 QMI8658；当前产品不启用 IMU，但 RTC 驱动仍需遵循共享总线规则。

### 2.2 ML1220

微雪将 J7 定义为可充电 RTC 电池接口。原理图中 3.3V 通过 B5819WS 肖特基二极管
连接到 RTC VDD 和 J7 正极，因此 ML1220 的可充电体系符合使用方向。

需要注意：

- RTC 电池路径没有独立的专用充电管理芯片，主要依赖二极管和电池自身特性；
- 安装前必须实测 J7 Pin 1 对 GND 的空载电压，并核对所购 ML1220 的充电规格；
- 必须确认电池转接线极性，不能只依赖红黑线颜色；
- 优先购买带原厂焊片或点焊引线、SH1.0 插头的 ML1220；
- 不直接长时间焊接裸电池；
- 不再连接 CR1220 等不可充电纽扣电池。

PCF85063A 在 3.3V、I2C 空闲时典型电流约 0.22 µA；ML1220 标称容量约
17 mAh。理论保持时间很长，但实际时间会受到电池自放电、二极管反向漏电、
板级漏电和温度影响，最终以实机断电测试为准。

## 3. 时间模型

采用以下统一约定：

- RTC 内部保存 UTC；
- 固件内部使用 Unix UTC 时间；
- Dashboard 和 System Info 显示北京时间 `UTC+08:00`；
- CSV 使用带时区的 ISO 8601 北京时间；
- `uptime_ms` 始终保留；
- PC 校时时发送 UTC，不把电脑本地时区直接写入 RTC。

示例：

```text
RTC / 内部：2026-07-31T06:26:35Z
屏幕 / CSV：2026-07-31T14:26:35+08:00
```

PCF85063 只有两位年份。固件固定将 RTC 年份解释为 `2000–2099`，不采用微雪
例程中的 `1970` 年偏移。

## 4. 软件结构

### 4.1 板级配置

在板级配置中增加 RTC 能力和引脚：

```cpp
kHasRtc
kRtcSda
kRtcScl
kRtcInterrupt
kRtcI2cAddress
```

- `waveshare28`：启用 RTC，GPIO11/10/9；
- `esp32s3`（原 1.47 寸）：`kHasRtc=false`；
- 两个 PlatformIO 环境继续保持可编译。

### 4.2 RtcDriver

新增板级 RTC 驱动，职责为：

- 使用 `Wire1` 初始化 GPIO11/10；
- 检查地址 `0x51` 是否应答；
- 读取和写入 PCF85063 控制、日期及时间寄存器；
- BCD 编解码；
- 使用 24 小时制；
- 解释年份为 `2000–2099`；
- 检查 CTRL1 的 STOP 位；
- 检查 Seconds 寄存器的 OS（Oscillator Stop）标志；
- 校验秒、分、时、日、月、年份和闰年范围；
- 写入后立即读回验证；
- 输出明确错误状态，不在底层自动写入编译时间。

建议错误状态：

```text
NotPresent
NotSet
OscillatorStopped
InvalidData
IoError
Ready
```

初始化时沿用板载晶振与微雪例程对应的负载电容设置。第一阶段保持 CLKOUT、
闹钟和定时器关闭。

### 4.3 TimeService

新增 `TimeService`，避免 UI 和 StorageTask 直接访问 I2C：

1. 开机读取 RTC；
2. RTC 有效时建立“UTC 秒 + 64 位单调时钟”的时间锚点；
3. 使用 `esp_timer_get_time()` 在内存中推进时间；
4. 约每 60 秒重新读取一次 RTC；
5. 提供 UTC、北京时间和 ISO 8601 格式化接口；
6. 对外发布固定大小的状态快照；
7. 时间回拨时避免生成倒退的数据时间戳；
8. RTC 临时读取失败时，可以短期使用已有单调锚点继续计时并标记状态降级。

每条传感器记录不直接读取 RTC。时间戳必须在 `ScanCompleted` 事件处理时生成，
随 `SensorSnapshot` 一起进入 StorageTask，确保时间对应采样完成时刻，而不是
SD 卡实际写入时刻。

## 5. 时间显示

### 5.1 Dashboard

在所有运行模式的 Dashboard 顶部右侧显示：

```text
14:26:35
```

- 每秒只刷新时间所在的小区域；
- 不重绘整个 Dashboard；
- RTC 无效时显示 `--:--:--`；
- 不影响 PASS、MASTER、PRECHECK 的现有动态区域。

### 5.2 System Info

增加：

```text
RTC: OK
2026-07-31 14:26:35
Timezone: UTC+08:00
```

错误时显示具体状态，例如：

```text
RTC: NOT SET
RTC: OSC STOP
RTC: I2C ERROR
```

## 6. USB 校时

### 6.1 设备端

新增独占的 TIME SYNC 模式，建议协议标识为 `ESPTIME1`。

最小命令集：

```text
PING
GET
SET <unix_utc_seconds>
CLOSE
```

`SET` 的处理顺序：

1. 检查 Unix 时间范围；
2. 转换为 UTC 日期时间；
3. 写入 RTC；
4. 清除 OS 标志；
5. 立即读回；
6. 误差不超过 1 秒才返回成功；
7. 更新 TimeService 锚点。

进入 TIME SYNC 前：

- 停止并确认关闭当前 CSV；
- 停止 RS485 发现或轮询；
- 清理 USB 和串口残留字节；
- 校时后返回 PASS；
- 后续 MASTER 记录应创建新日志会话，避免一个 CSV 内发生时间跳变。

### 6.2 Tools Menu

将重复的 BACK 卡片替换为 TIME SYNC：

```text
CONFIG UPLOAD
LOG EXPORT
TIME SYNC
```

点击页面标题仍可返回 System Info。

### 6.3 PC 工具

优先把“同步设备时间”集成到现有 PC 工具体系，不长期增加第四个独立 EXE。
开发阶段可以先提供 Python 协议测试脚本，协议稳定后再加入 GUI。

PC 工具行为：

- 读取电脑当前 UTC；
- 与设备握手；
- 显示写入前后的设备时间；
- 执行 SET；
- 读取并显示校时误差；
- 明确报告 RTC 未连接、OS 标志、范围错误和读回失败。

## 7. CSV 兼容方案

### 7.1 保留现有数据

继续保留：

```text
session_id
sample_seq
uptime_ms
address
pn
CH1_mA
CH2_mA
动态传感器字段
```

为了不改变所有现有列的位置，将新列追加到每行末尾：

```text
timestamp
```

示例：

```text
...,s365HighZero_DI,t365Zero_DI,timestamp
...,3200,15000,2026-07-31T14:26:35+08:00
```

### 7.2 文件头

建议写入：

```text
# time_source=rtc+uptime_ms
# timezone=UTC+08:00
# timestamp_format=ISO8601
```

原有固件版本、配置版本和 session 信息继续保留。

### 7.3 RTC 无效时

- `timestamp` 字段留空；
- `uptime_ms` 正常记录；
- CSV 创建、采样和下载均不失败；
- System Info 明确提示 RTC 状态；
- RTC 恢复有效后，新日志会话才开始写入绝对时间。

文件名暂时保持 `PNxxxxx_Sxxxxxx.csv`，不在本阶段加入日期，避免影响 ESPLOG1
文件名解析和现有上位机。

## 8. Wi-Fi/NTP 开机自动校时规划

### 8.1 目标与范围

- 仅在 Waveshare 2.8 寸 RTC 板型启用；
- 已配置 Wi-Fi 时，每次开机自动尝试一次 NTP 校时；
- 联网和等待 NTP 全部使用非阻塞状态机，不延迟触摸、USB、RS485 和 SD；
- NTP 成功后写入 PCF85063、读回校验并更新 `TimeService` 锚点；
- 成功、超时或失败后都关闭 Wi-Fi 射频，不长期保持联网；
- 无凭据、无网络、密码错误或 NTP 失败时继续使用现有 RTC。

第一版不做持续联网、Web 配网页、手机配网、NTP 定时轮询或网络日志上传。

### 8.2 Wi-Fi 凭据

凭据不写入源码、`platformio.ini`、传感器 JSON 或 Git。通过 USB 独占的
`ESPTIME1` 会话设置，保存在 NVS 独立命名空间中。

建议 NVS 字段：

```text
namespace: wifi_time
enabled:   bool
ssid:      string, 1~32 bytes
password:  string, 0~63 bytes
```

安全边界：

- 页面、USB 响应、调试日志和 CSV 永不显示密码；
- `WIFI STATUS` 只返回是否已配置及连接结果，最多显示脱敏 SSID；
- 只在物理 USB 连接且设备进入 TIME SYNC 模式时允许修改凭据；
- 默认 NVS 不等于加密存储。若未来开启 ESP32 Flash Encryption，再将凭据安全
  目标升级为静态加密。

### 8.3 ESPTIME1 扩展

保留现有 `PING / GET / SET / CLOSE`，新增：

```text
WIFI STATUS
WIFI SET <encoded_ssid> <encoded_password>
WIFI CLEAR
NTP START
```

- SSID 和密码使用无空格的可逆编码传输，避免空格和特殊字符破坏行协议；
- 设备解码后必须检查 SSID/密码长度，超限时不覆盖旧凭据；
- `WIFI SET` 成功只表示 NVS 保存成功，不虚报已连网；
- `NTP START` 用于不重启设备的当场测试；
- PC 端统一使用 `tools\sync_rtc.py` 完成 USB 校时、Wi-Fi 配网和 NTP 测试。

#### 8.3.1 使用前准备

1. 使用 USB 将电脑连接到设备，并确认设备对应的 COM 口；
2. 在设备上依次进入 `SYSTEM INFO > TOOLS MENU > TIME SYNC`；
3. 保持屏幕停留在 `TIME SYNC` 页面，再运行下面的命令；
4. 下列示例使用 `COM12`，实际使用时必须替换成设备当前的 COM 口。

项目使用 PlatformIO 虚拟环境中的 Python。在项目根目录打开 PowerShell 后执行：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tools\sync_rtc.py COM12 [操作参数]
```

脚本会禁用 DTR/RTS，打开串口时不会主动复位 ESP32-S3。一次脚本执行结束后，设备会
自动退出 TIME SYNC 并返回 PASS 页面；要运行下一条命令，需要重新进入 TIME SYNC。

#### 8.3.2 使用电脑时间校准 RTC

不带操作参数时，脚本读取电脑当前 UTC 时间、写入设备 RTC、立即读回，并检查误差
是否不超过 1 秒：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tools\sync_rtc.py COM12
```

看到 `RESULT: PASS` 表示 USB 校时完成。该操作不会修改电脑时间。

#### 8.3.3 首次配置 Wi-Fi 并立即执行 NTP 校时

推荐只在命令行中填写 SSID，不填写密码：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tools\sync_rtc.py COM12 --ssid "WiFi名称"
```

出现 `WiFi password (empty = open):` 后输入密码并按 Enter。输入期间 PowerShell 不会
显示密码；开放网络直接按 Enter。脚本会依次执行以下操作：

1. 将 SSID 和密码保存到设备 NVS；
2. 连接 Wi-Fi；
3. 获取 NTP 时间并写入 RTC；
4. 读回 RTC 进行验证；
5. 成功后显示 `RESULT: PASS`。

也支持 `--password "WiFi密码"`，但不推荐日常使用，因为明文密码可能保留在
PowerShell 历史记录中。设备输出、屏幕和 CSV 均不会显示密码。

凭据保存成功后会跨重启保留。以后正常开机时设备会自动尝试一次 NTP 校时，不需要
再次运行配网脚本；NTP 成功或失败后都会关闭 Wi-Fi。

#### 8.3.4 使用已保存的网络手动重试 NTP

无需重新输入 SSID 或密码：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tools\sync_rtc.py COM12 --ntp-start
```

适合网络恢复后当场重试。看到 `SYNCED`、`After NTP: TIME ...` 和 `RESULT: PASS` 表示
NTP、RTC 写入及读回全部成功。

#### 8.3.5 查询配网和 NTP 状态

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tools\sync_rtc.py COM12 --wifi-status
```

返回内容只包含是否已配置、脱敏 SSID 和当前/最近一次状态，不包含密码。

#### 8.3.6 清除已保存的 Wi-Fi 凭据

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tools\sync_rtc.py COM12 --clear-wifi
```

看到 `OK WIFI CLEARED` 和 `RESULT: PASS` 表示清除成功。之后设备开机不再联网，仍会
使用 RTC 中已有的时间。

#### 8.3.7 常见结果说明

- `OSC STOP`：RTC 振荡停止标志尚未被有效校时清除，可执行 USB 或 NTP 校时；
- `WIFI TIMEOUT`：未找到热点、信号差或密码错误；
- `NTP TIMEOUT` / `SERVER TIMEOUT`：Wi-Fi 已连接，但 NTP UDP 123 请求未成功；
- 手机热点成功而公司网络超时：优先检查公司内网防火墙、出口策略或内部 NTP 地址；
- `Handshake timeout`：确认 COM 口正确，并确认设备停留在 TIME SYNC 页面；
- 任何失败都不会清除已有 RTC 时间，也不会阻塞 PASS、MASTER、PRECHECK、RS485 或
  SD 日志功能。

### 8.4 开机状态机

```text
Disabled / NoCredentials
          |
          v
ConnectingWiFi --10 s--> WiFiTimeout
          |
          v
WaitingNtp ------10 s--> NtpTimeout
          |
          v
NtpReady --> PendingRtcWrite --> Synced --> WiFiOff
```

顺序：

1. `TimeService.begin()` 先读 RTC，屏幕立即可用旧时间；
2. `WifiTimeService.begin()` 从 NVS 读取凭据，无凭据时立即结束；
3. 后台连接 Wi-Fi，建议上限 10 秒；
4. 连接成功后启动 SNTP，使用 UTC，首次 DNS/SNTP 等待上限 30 秒；
5. 仅接受 PCF85063 可表达的 2000~2099 年 Unix UTC 时间；
6. NTP 时间有效后立即断开 Wi-Fi，系统时钟继续在本地走时；
7. 当前没有活动 CSV 时，才写入 RTC 并更新 `TimeService`；
8. 如果已在记录，保留 `PendingRtcWrite`，等日志停止后再校时；
9. 写入后读回，与当前 NTP 时间差不超过 1 秒才标记 `Synced`。

开机默认在 PASS，通常会在用户进入 MASTER 前完成。但代码不依赖这个时序假设，
必须以 `StorageService::isRecording()` 作为写 RTC 的安全条件。

### 8.5 时间源与 UI

`TimeService` 增加最近一次成功校时来源：

```text
RTC
USB
NTP
```

System Info 或 TIME SYNC 页显示简短状态：

```text
NTP: DISABLED
NTP: NO WIFI CONFIG
NTP: CONNECTING
NTP: SYNCED
NTP: WIFI TIMEOUT
NTP: SERVER TIMEOUT
```

不在 Dashboard 持续显示 Wi-Fi 图标：Wi-Fi 只是开机短时校时手段，不代表设备的
常驻连接能力。

### 8.6 失败和时间跳变策略

- Wi-Fi/NTP 失败只是诊断状态，不进入 `AppMode::Fault`；
- 开机自动尝试一次，失败后不在后台无限重连；
- 手动 `NTP START` 可再试，不必重启；
- NTP 比 RTC 早或晚都允许校准，但不能在活动 CSV 中生效；
- NTP 失败时不清空已有 RTC 锚点；
- RTC 原本无效且 NTP 失败时，CSV `timestamp` 继续留空；
- 成功写 RTC 后，后续新日志会话才使用新绝对时间。

### 8.7 验收矩阵

| 场景 | 预期结果 |
|---|---|
| 无 Wi-Fi 凭据开机 | 立即使用 RTC，显示 `NO WIFI CONFIG` |
| 正确凭据且有网 | 开机后自动 NTP 校时，误差不超过 1 秒 |
| 密码错误 | 超时退出并关 Wi-Fi，RTC/触摸/RS485/USB/SD 正常 |
| AP 存在但无互联网 | NTP 超时并关 Wi-Fi，保留旧 RTC |
| RTC 无效但 NTP 成功 | RTC 恢复有效，屏幕和新 CSV 有时间 |
| 正在 MASTER 记录时 NTP 到达 | 当前 CSV 不跳时，停止记录后再写 RTC |
| 完全断电重启 | NVS 凭据保留，再次自动尝试 NTP |
| `WIFI CLEAR` 后重启 | 不联网，仅使用 RTC |
| NTP 成功后导出日志 | ESPLOG1 CRC32 通过，时间戳与屏幕一致 |

## 9. 实施阶段

### RTC-A：硬件与底层驱动

- [ ] 测量 J7 Pin 1 极性和空载电压；
- [ ] 安装 ML1220 并确认无异常发热；
- [ ] 在 GPIO11/10 上检测到 `0x51`；
- [x] 增加双板 RTC 配置；
- [x] 完成 RtcDriver；
- [x] 检查 STOP、OS、BCD 和日期范围；
- [x] 两个 PlatformIO 环境编译通过。

验收：能够读取 RTC，并可靠区分 Ready、NotSet、OscillatorStopped 和 IoError。

### RTC-B：TimeService 与屏幕

- [x] 建立 UTC 与单调时钟锚点；
- [x] 60 秒周期校准；
- [x] Dashboard 每秒刷新 `HH:MM:SS`；
- [x] System Info 显示完整日期、时区和状态；
- [x] RTC 故障不阻塞触摸、RS485、USB 和 SD。

验收：连续运行时显示正常，没有明显闪烁，不增加 UI 卡顿。

### RTC-C：USB 校时

- [x] 增加 TIME SYNC AppMode；
- [x] 增加 `ESPTIME1` 协议；
- [x] Tools Menu 增加 TIME SYNC；
- [x] 提供 Python 测试脚本；
- [x] 写入后读回误差不超过 1 秒；
- [x] 失败后可以原地重试；
- [x] 校时完成后安全返回 PASS。

验收：电脑一键校时，重启后时间保持。

### RTC-D：CSV 时间戳

- [x] 在扫描完成时生成时间戳；
- [x] 扩展 `SensorSnapshot`；
- [x] CSV 末尾追加 `timestamp`；
- [x] 增加时间源、时区和格式元数据；
- [x] RTC 无效时 timestamp 留空；
- [ ] ESPLOG1 列表、STAT、分块下载和 CRC32 不受影响。

验收：下载的 CSV 中时间连续、格式明确，并与屏幕时间一致。

### RTC-E：电池保持与稳定性

- [ ] 校时后完全断开 USB 和主电池 12 小时；
- [ ] 完全断电 24 小时；
- [ ] 恢复供电后对比电脑时间；
- [ ] 拔掉 ML1220 后重复断电，确认能识别时间失效；
- [ ] 测试 ML1220 反复充放电后的 RTC 恢复；
- [ ] MASTER 记录、PRECHECK、PASS、配置上传和日志下载回归；
- [ ] 运行长时间稳定性监控。

初步漂移目标：24 小时误差不超过 5 秒。如果实际漂移长期稳定且超出目标，再评估
使用 PCF85063 offset 校准寄存器，不在第一版预先校准。

### RTC-F：Wi-Fi/NTP 开机自动校时

- [x] 实现 NVS Wi-Fi 凭据存储与清除；
- [x] 扩展 ESPTIME1 配网和手动 NTP 命令；
- [x] 扩展 Python 工具，密码不回显；
- [x] 实现非阻塞 `WifiTimeService` 状态机；
- [x] 限制 Wi-Fi 连接和 NTP 等待超时；
- [x] NTP 成功后写 RTC、读回并更新 `TimeService`；
- [x] 日志记录期间禁止 RTC 时间跳变；
- [x] 成功或失败后关闭 Wi-Fi 射频；
- [x] System Info/TIME SYNC 显示脱敏状态；
- [ ] 无凭据、错密码、无互联网和有网四种实机测试；
- [ ] MASTER、PRECHECK、PASS、ESPCFG1、ESPLOG1 回归；
- [ ] NTP 后 CSV 时间戳与屏幕时间一致。

实机记录（2026-08-03）：手机热点 `tony` 下 Wi-Fi 连接、SNTP、RTC 写入和
ESPTIME1 回读完整通过，返回 `SYNCED`，时间为
`2026-08-03T09:43:55+08:00`。`pyxiswireless` 可连接并获取 IP，但 NTP 连续超时；
同网络 Windows `w32tm` 三次请求中一次超时，初步归为现场网络 NTP 丢包/策略差异。

验收：配网一次后，有网环境开机自动校准；无网环境不阻塞主功能，不造成
日志时间倒退，不在任何输出中泄露 Wi-Fi 密码。

## 10. 关键边界

- RTC 失败不能让设备进入 Fault；
- 时间无效不能阻止日志记录；
- 不使用编译时间静默覆盖 RTC；
- 不在每条记录时访问 I2C；
- 不允许校时造成同一日志文件内的时间倒退；
- 不移除 `uptime_ms`；
- 不改变现有日志文件命名；
- 不让 RTC 驱动与触摸共用同一个 `Wire` 实例；
- Wi-Fi/NTP 只能是可选校时源，不能成为主功能的启动依赖；
- 不引入 RTC 闹钟、低功耗唤醒或长期常驻 Wi-Fi。

## 11. 资料

- 微雪主板原理图：
  `C:\Users\15189\Downloads\ESP32-S3-Touch-LCD-2.8.pdf`
- 微雪本地示例：
  `C:\Users\15189\Downloads\ESP32-S3-LCD-2.8-Demo`
- 微雪产品文档：
  <https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-2.8>
- NXP PCF85063A 数据手册：
  <https://www.nxp.com/docs/en/data-sheet/PCF85063A.pdf>
- Panasonic ML1220 资料：
  <https://api.pim.na.industrial.panasonic.com/file_stream/main/fileversion/3564>
