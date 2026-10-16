# TinyUSB CDC 普通透传参数跟随实施计划

日期：2026-10-16

当前修订目标：上位机无需开启 DTR/RTS，无需改动原有程序或控制线设置。前版关于 DTR 必须开启的结论已撤销。

当前实机进度：2.8 寸修订固件已刷入。DTR off/on 九组合透传、屏幕下载入口、MASTER/PRECHECK、BOOT PASS、配置错误拒绝、日志查询和校时只读回归均通过。日志 GET/整文件 CRC 因目录为空未覆盖；其他未执行写入项仍见下表。设备已恢复普通 PASS、9600 8E1。

## 目标与边界

- 使用官方 Arduino-ESP32 内置 TinyUSB CDC。
- 普通 PASS 模式跟随上位机串口参数；首批验收为 9600、19200、115200 × 8N1、8E1、8O1。
- 发现、采集、校准继续使用设备固定参数；BOOT PASS 保持 9600 8N1。
- 没收到有效上位机参数时，PASS 默认 9600 8E1。不支持的参数拒绝应用，暂停 PASS 转发，避免以错误参数发送。
- UART 始终只由 Rs485Task 操作。参数切换会结束旧发送并丢弃旧会话缓存，上位机应在切换后重发未完成事务。
- 保留工作区已有的校准工具/配置改动。

## 执行顺序（前版历史；无 DTR 修订见后文）

1. [x] 检查 USB、RS485 所有权和模式切换，实现范围确认。
2. [x] 建立纯参数校验/映射逻辑及测试；处理 RTU 帧间隔。
3. [x] 切换 TinyUSB，接收 CDC 参数，串行提交至 Rs485Task，增加应用状态和转发门控。
4. [x] 检查普通 PASS、MASTER、BOOT PASS、配置上传、日志导出和校时的模式隔离。
5. [x] 编译两种板型，执行相关自动化检查，修复失败。
6. [x] 增加实机九组合双向透传、在线切参及重新打开串口的验收脚本。
7. [x] 检查串口设备，条件具备时烧录并执行实机验收；缺少硬件/接线明确标记待验证。
8. [x] 更新使用说明和本计划的实际结果，审查差异并交付。

## 验收标准

- 九组合下，USB→RS485 和 RS485→USB 的 Modbus 帧逐字节一致。
- 同一次打开串口内切参、关闭后重新打开，后续事务使用最新参数。
- 非 PASS 模式收到 CDC 参数不改变运行中的固定 UART 参数；回到 PASS 使用最新有效设置。
- 不支持的格式不会默默沿用旧参数继续转发；重新设置有效格式可恢复。
- 切参期间无跨任务 UART 调用，无旧帧按新参数发送。
- USB 配置上传、日志导出、校时和升级透传需实机回归，不能用编译通过代替。
- 不依赖 USB JTAG 自动复位；检查 1200bps/DTR/RTS 的下载和重启影响。

## 执行记录

- 初始检查：当前 ARDUINO_USB_MODE=1；RS485 固定 9600；PASS 为 8E1，BOOT PASS 为 8N1；USB 层通过 Stream 接口与应用协议解耦。
- 当前本机框架包版本 3.3.11，已有 USBCDC line coding 事件和 enableReboot 接口。
- 初始阶段未修改或烧录固件；后续执行结果如下。

### 实现与本机检查

- 已切换 TinyUSB，并移除开发板默认 USB_MODE=1 定义以消除冲突。
- CDC 回调只更新参数邮箱；loopTask 提交配置命令，等待 Rs485Task 发布匹配版本后恢复 PASS。
- UART 重配只在 Rs485Task 内执行；旧缓存清理后再确认版本。切参时允许正在发送的旧帧完成，丢弃未完成/排队的旧事务。
- 增加上位机参数校验、非法设置暂停/恢复、DTR 会话边界处理，以及实际 UART 参数诊断。
- 保留 USB 接收组帧 5ms 下限。USB 包间隔不能直接视为 UART 字符间隔，九种首批格式无需降低此值。
- PASS 页面显示实际波特率及校验位；不支持格式显示 Unsupported PC format。
- 已将 Python/C# 串口工具改为 DTR 开启、RTS 关闭，并重新生成三个受影响的 EXE。
- 本机测试：CDC 九种格式/非法参数测试通过；CalibrationExecutor 原生测试通过；相关 Python 脚本语法检查通过；三个 C# 工具编译通过。
- 实机已确认：微雪 2.8 寸为 COM24，独立 CH343 RS485 适配器为 COM6。切换 USB 模式后需重新扫描板端 COM 号。
- 首轮编译发现重复声明，已清理；2.8 寸固件最终编译和烧录均通过。
### 2.8 寸板实机普通 PASS 验收：通过

- 固件已烧录并校验写入哈希。Windows 端口由 COM24 变为 COM8；RS485 适配器 COM6。
- 最终 2.8 寸固件 RAM 210324/327680 字节（64.2%），应用 Flash 1307803/6553600 字节（20.0%）。
- `python tools/test_cdc_passthrough.py COM8 COM6 --report reports/cdc_acceptance.json`：退出码 0，报告 passed=true。
- 九种组合全部通过；在线正向/逆向遍历各一次，每组 5 次双向逐字节比对。
- 每种组合关闭并重新打开两次，每次 5 次双向比对；相同配置重开的设置版本必须改变。
- 38400bps、1200bps、7 数据位、2 停止位均暂停下行转发；改回有效格式后双向恢复。
- DTR/RTS 下载复位序列不引起重启，运行时间连续性检查通过。
- 共 190 轮双向事务（380 个方向的数据比对），另有 4 个不支持格式拒绝场景；41 条验收记录。
- 首次烧录日志遇到 GBK 进度条输出异常，已仅终止该次下载进程树，启用 PYTHONUTF8/PYTHONIOENCODING 后重烧成功。未执行 LittleFS 上传。
- MASTER、PRECHECK、BOOT PASS 和专用 USB 协议的实机回归正在继续；需要在触摸屏进入相应模式。
### 两板型构建与固定参数模式

- `pio run -e waveshare28`：成功；2.8 寸固件已烧录到实机。SHA256：E6E30E88E28F1FB7C6C76AC0CE645700B237D76966A2585ABE4DEC1FB4CFECB9。
- `pio run -e esp32s3`：成功；1.47 寸 RAM 210520/327680（64.2%），Flash 1285261/3342336（38.5%）。1.47 寸未进行实机烧录。
- MASTER 实机：九种常用组合及 38400/1200bps 共 11 项通过，下行始终 9600 8E1，UART 重配次数不增加。报告 reports/cdc_master_isolation.json。
- PRECHECK 实机：同样 11 项通过，下行始终 9600 8E1，UART 重配次数不增加。报告 reports/cdc_precheck_isolation.json。
### BOOT PASS 大包回归：通过

- 命令：`python tools/test_boot_pass.py COM8 COM6 --count 5 --host-baud 115200 --host-parity O`。
- USB 主机设为 115200 8O1，独立 RS485 适配器保持 9600 8N1；升级通道不跟随 USB 主机设置。
- 递增、全零、全 FF、随机四类 YMODEM-1K（1029 字节）数据，每类双向重复 5 次，40/40 次逐字节比对通过。
- 每方向 20580 字节。日志：reports/cdc_boot_pass.txt。屏幕 DROP 计数已由用户核对为 0/0。
- 当前剩余专用协议回归：ESPCFG1 错误上传拒绝及大包接收、ESPLOG1 查询/下载、ESPTIME1 只读握手。
- 校准动作不会对接线中的设备发起实际校准写入；已执行 CalibrationExecutor 本机行为测试，固定 UART 配置路径经代码审查。
### 专用 USB 协议回归与收尾

- ESPCFG1：6/6 错误场景通过，包括 118630 字节大包的 CRC 错误拒绝。已安装配置未替换。日志 reports/cdc_espcfg1_negative.txt。
- ESPLOG1：PING/LIST/CLOSE 通过，配置版本 2026.08.27.1，固件 V1.0.44。目录文件数为 0，不能执行真实 GET 和整文件 CRC；此项待有日志后补测。报告 reports/cdc_esplog1.json。
- ESPLOG1 首次 PING 超时，后续探测收到 ESPCFG1 的 NAK:Bad magic。用户重新确认进入 LOG EXPORT 后握手与查询正常；首次尝试未记为通过。
- ESPTIME1：PING/GET/CLOSE 通过，RTC 返回有效时间。测试没有写入 RTC 或更改 Wi-Fi 凭据。报告 reports/cdc_esptime1.json。
- 保持 COM8 连接，在 TIME SYNC 中修改为 19200 8O1，CLOSE 后无需重开串口，PASS 实际 UART 即为 19200 8O1；模式与设置恢复检查通过。
- 2.8 寸固件已经运行在板端 COM8；适配器 COM6。普通透传首批九组合验收完成。
- Git 差异已审查；UART 配置仍由 Rs485Task 独占，既有 Modbus 地址保持兼容，诊断寄存器只追加到 40051。
- 保留原有校准工具与配置改动。构建脚本根据已有 config/calibration_profiles/53577.default.json 同步生成 data/calibration/53577.default.json 并更新索引，未执行 LittleFS 上传。

## 明确未覆盖的实机项目

| 项目 | 当前状态 |
|---|---|
| 日志 GET 与整文件 CRC | 设备目录为空，待有日志后补测 |
| 成功安装新配置 | 未执行，避免替换现有配置；大包接收及错误拒绝已测 |
| RTC SET / Wi-Fi 配置写入 | 未执行；校时协议只读握手与正常退出已测 |
| 对真实传感器执行校准写入 | 未执行；本机 CalibrationExecutor 测试通过 |
| 1.47 寸板实机 | 两板型均已编译；当前接线的 2.8 寸板完成上述实测 |

## 复测命令

```powershell
python tools/test_cdc_native.py
python tools/test_cdc_passthrough.py COM8 COM6 --report reports/cdc_acceptance.json
# 以下两项分别需要在屏幕进入 MASTER / PRECHECK
python tools/test_cdc_fixed_mode.py COM8 --mode master --report reports/cdc_master_isolation.json
python tools/test_cdc_fixed_mode.py COM8 --mode precheck --report reports/cdc_precheck_isolation.json
# 需要在屏幕进入 BOOT PASS
python tools/test_boot_pass.py COM8 COM6 --count 5 --host-baud 115200 --host-parity O
# 需要在屏幕进入 CONFIG UPLOAD；不替换配置
python tools/test_espcfg1.py COM8 config/sensor_profiles.json --suite negative
# 有日志后，在 LOG EXPORT 中复测文件下载
python tools/test_esplog1.py COM8 --download reports/log_download
```

使用注意（修订固件）：电脑无需开启 DTR/RTS，项目内工具已恢复原有关闭设置。固件禁用串口控制线自动复位；修订版刷入后从 TOOLS → FIRMWARE DOWNLOAD 进入下载模式。板上旧版仍需 BOOT/RESET 入口。

## 修订：上位机无需开启 DTR（2026-10-16）

用户澄清验收目标：不修改任何上位机。前版要求 DTR 的交付不满足该目标，以下修订优先于前版 DTR 使用说明。

1. [x] 确认限制来自 Arduino USBCDC 的 connected/DTR 发送门控，底层 TinyUSB 支持无 DTR 发送。
2. [x] 增加项目内 UsbCdcStream，接收与事件仍走官方 CDC，发送按 USB 端点就绪状态判断；不改 SDK 全局文件。
3. [x] 发送按 FIFO 可用空间分片，防止 DTR 关闭时覆盖积压数据；超时/拔线返回真实短写长度。
4. [x] 应用协议与本地 Modbus 回复统一使用该 Stream；DTR/RTS 不再中断转发或改变 UART 设置。
5. [x] 将现有 C#/Python 工具恢复 DTR/RTS 关闭，新增 DTR off/on 验收选项。
6. [x] 本机验证背压、短写、拔线和无 DTR 大包。
7. [x] 两板型修订固件构建通过（最终增量核对见下）。
8. [x] 在 2.8 寸板烧录修订固件，分别执行 DTR off/on 九组合实测，并验证屏幕下载入口。
9. [x] 更新使用说明，移除上位机必须开启 DTR 的要求；未刷入、未实测的内容明确保留为待验收。

- 修订本机测试通过：真实 UsbCdcStream 对 1029 字节数据按 FIFO 空间发送；覆盖满队列超时、短写重试的字节连续性、断开时返回部分长度以及 RX 代理；不调用 DTR connected 门控。
- 三个 C# 工具已恢复 DtrEnable=false 并重新编译，与原有上位机控制线行为一致。
- DTR/RTS 都关闭时，同样参数的关闭/重开可能没有可观察的 CDC 控制事件。重开验收以实际数据比对为准，不再强制要求设置版本变化。
### 装壳维护补充

- 用户表示设备已装壳，BOOT/RESET 不便操作；普通断电上电不等于进入 ROM 下载模式。
- 已检查前版应用与 USB 栈：没有下载命令或已启用的 DFU 接口，USBCDC.enableReboot(false) 也禁用了常规控制线/1200bps 自动下载。
- 新固件增加 TOOLS → FIRMWARE DOWNLOAD 触摸入口。进入前先等待 SD 日志关闭、停止 RS485，再调用框架 usb_persist_restart(RESTART_BOOTLOADER)，不依赖上位机修改或控制线状态。
- 入口本机测试通过：只在 Tools 对应区域生效，不与 CONFIG/LOG/TIME 或 Dashboard 操作混淆；实际触摸进入 ROM 尚待刷入后验证。
- tools/try_usb_boot_window.py 是仅用于本次维护的尽力尝试：配合完全断电捕获初始化前的 USB 自动下载窗口，不写固件，可能捕获失败，不能把普通上电描述为可靠下载方式。
### 修订版最终构建与板端状态（2026-10-16，刷入前记录）

- 最终增量构建两项 SUCCESS：waveshare28 与 esp32s3。两份 ELF 均确认包含 UsbCdcStream::write 和 UiManager::drawFirmwareDownloadPage。
- 2.8 寸：RAM 210340/327680；Flash 1308479/6553600。firmware.bin SHA256：DFBAC259585215AE1CC8198E4C4F07472827C42785F3BB944E3F686370EB2EE1。
- 1.47 寸：RAM 210544/327680；Flash 1286001/3342336。firmware.bin SHA256：B0121F0D57F84184C38CD74B78FF7B7D4BFB1A450F3C27B48711A8417518F286。
- 本机三组测试通过：九种格式及无效设置；无 DTR 发送、1029 字节顺序、背压、短写重试、拔线；屏幕下载动作区域。相关 Python 脚本语法检查通过。
- 用户已确认断电并重新接回 USB。确认到达时监听已超时，未记录到断电过程；随后扫描仅看到应用 COM8 和适配器 COM6，没有确认 ROM 下载端口。不能据此断言已经实测并排除了启动窗口。
- 本次没有写入板端 Flash，板上仍为此前要求 DTR 的旧固件。修订版无 DTR 实机验收、专用协议回归和屏幕进入 ROM 验证均未完成；此前 DTR 开启状态的报告不能作为修订版验收结果。
- 下一步需要一次可用的 ROM 下载入口。旧固件没有屏幕下载入口；普通上电不会安装修订版。进入下载模式后仅刷写 2.8 寸固件，不上传 LittleFS，再执行 DTR off/on 验收。
## 修订版烧录与实机验收（2026-10-16）

- 用户拆壳并通过 BOOT/RESET 进入 ROM；COM24 核对 MAC 28:84:85:3b:88:54，与原 2.8 寸实机一致。
- 已将 SHA256 DFBAC259585215AE1CC8198E4C4F07472827C42785F3BB944E3F686370EB2EE1 的 waveshare28/firmware.bin 写入 0x10000，esptool 返回 Hash of data verified。仅更新应用分区，未写 LittleFS 或配置。
- hard-reset 后仍停在 ROM；通过 esptool --after watchdog-reset chip-id 正常退出到应用 COM8，不需要再次触碰按键。
- 首次无 DTR 测试本机诊断正常，但 RS485 收不到数据；用户确认拆壳时拔掉 A/B 并重新接好。接好后测试恢复。该次失败不计为通过。
- DTR 开启快速探测通过，完整 DTR off/on 验收进行中；屏幕下载入口和模式/专用协议回归待验证。
### 普通 PASS：DTR off/on 完整验收通过

- 两组分别执行默认每项 5 次：18 组在线设置（九种顺序和逆序）、18 组关闭重开（每种两次）、4 种不支持格式拒绝与恢复、四种 DTR/RTS 状态传输、下载复位序列不重启及运行时间连续性。
- 两组均 passed=true：reports/cdc_dtr_off.json 和 reports/cdc_dtr_on.json。每组 210 轮双向事务（420 个方向的逐字节比对），共 420 轮双向事务。
- DTR off 测试打开端口前即设 DTR=false、RTS=false；不是打开后才关闭。普通上位机无需添加 DTR 操作。
- 固件已实际运行于 2.8 寸 COM8；CH343 独立适配器 COM6。后续待验收项：屏幕进入 ROM，DTR 关闭的固定模式、BOOT PASS 及专用协议回归。
### 屏幕进入 ROM 与恢复运行：通过

- 用户在 TOOLS 点击 FIRMWARE DOWNLOAD，屏幕显示 USB DOWNLOAD；PC 从应用 COM8 切换为 ROM COM24。
- esptool --before no-reset chip-id 确认 ROM 通信及同一 MAC，未再次写入 Flash。
- --after watchdog-reset 后恢复应用 COM8，无 DTR 诊断确认普通 PASS [0,1]，UART 9600 8E1。
- 此次完整流程没有再按 BOOT/RESET。后续屏幕可用时，可从屏幕进入下载模式；本次硬件型号已验证该维护入口。
### 固定模式：无 DTR 隔离检查通过

- MASTER：九种首批组合加 38400/1200bps，共 11 项；诊断响应正常，UART 始终 9600 8E1、重配次数不增加。报告 reports/cdc_dtr_off_master.json。
- PRECHECK：同样 11 项全部通过；固定 UART 配置不受 CDC 参数影响。报告 reports/cdc_dtr_off_precheck.json。
- 两项均使用打开前 DTR=false、RTS=false；不依赖 PC 工具额外控制线设置。
### BOOT PASS：无 DTR 大包传输通过

- 命令：python tools/test_boot_pass.py COM8 COM6 --count 5 --host-baud 115200 --host-parity O。该工具保持 DTR/RTS 关闭。
- PC USB 115200 8O1，独立 RS485 端固定 9600 8N1；递增、全零、全 FF、随机四种 1029 字节 YMODEM-1K 数据，双向各 5 次。
- 40/40 次逐字节比对通过，每方向 20580 字节。报告 reports/cdc_dtr_off_boot_pass.txt。用户已核对屏幕 DROP 为 0/0。
### ESPCFG1：无 DTR 负向上传回归通过

- 使用原有 tools/test_espcfg1.py（DTR/RTS 关闭），六种错误场景全部返回预期 NAK：错误 magic、零长度、超长、CRC32 错误、JSON 不完整、不支持 schema。
- CRC 错误场景完整发送现有 sensor_profiles.json 大包后被拒绝，未成功安装或替换配置。报告 reports/cdc_dtr_off_espcfg1_negative.txt。
### ESPLOG1、ESPTIME1：无 DTR 只读回归通过

- ESPLOG1：原有协议 PING/LIST/CLOSE 通过，配置版本 2026.08.27.1、固件 V1.0.44；目录 0 个文件。未执行 GET/整文件 CRC，不将查询通过等同于下载通过。报告 reports/cdc_dtr_off_esplog1.json。
- ESPTIME1：PING 返回 PONG ESPTIME1 OK，GET 返回有效 RTC，CLOSE 返回 BYE。保持同一次连接在 TIME SYNC 内切为 19200 8O1，退出后诊断证实 PASS UART 为 19200 8O1。报告 reports/cdc_dtr_off_esptime1.json。
- 两者均在打开端口前保持 DTR/RTS 关闭；没有修改 RTC、Wi-Fi 凭据或现有配置。
- 最终已重新设置并确认普通 PASS [0,1]、UART 9600 8E1、supported=true，然后关闭 PC 测试串口。

### 交付结论

- 首批普通透传自动跟随验收完成：9600/19200/115200 × 8N1/8E1/8O1，DTR off/on 均通过；无需修改原有上位机控制线设置。
- 2.8 寸板已安装本次修订固件；两板型均编译通过，1.47 寸仍只有构建验证。
- 装壳后可从 TOOLS → FIRMWARE DOWNLOAD 进入下载，已实测；烧录后 esptool --after watchdog-reset 可恢复应用。
- 未覆盖项保持透明：空目录下的日志 GET/整文件 CRC、成功配置安装、RTC SET/Wi-Fi 写入、真实传感器校准写入，以及 1.47 寸实机。上述写入操作未为了本次回归而执行。
- 原有校准工具及配置改动保留。本次仅写入应用分区，没有执行 LittleFS 上传。