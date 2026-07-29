# 传感器发现与参数显示工作流程

本文记录当前固件已经实现的运行流程，便于以后维护和增加新的传感器 PN。

## 1. 相关文件

| 文件 | 职责 |
|---|---|
| `src/main.cpp` | 对象装配、触摸/ADC、事件消费和顶层应用编排 |
| `src/app/app_controller.h/.cpp` | AppMode 切换及 RS485、Storage、USB 的有序交接 |
| `src/services/rs485_service.h/.cpp` | 唯一管理 Serial1；通过 Rs485Task 执行 PASS、发现和轮询 |
| `src/services/storage_service.h/.cpp` | 通过 StorageTask 执行 CSV 和 ESPLOG1 文件操作 |
| `src/usb/usb_service.h/.cpp` | 路由本地 Modbus、PASS、ESPCFG1 和 ESPLOG1 |
| `src/sensor_discovery.h/.cpp` | 使用地址 0 发现真实地址，并读取 PN 和设备名称 |
| `src/sensor_profile.h/.cpp` | PN 参数表、非阻塞 Modbus 轮询、数据类型解析 |
| `config/sensor_profiles.json` | 人工维护的 PN 与显示参数配置（唯一配置入口） |
| `generate_sensor_profiles.py` | 编译前校验 JSON、合并连续地址并生成 C++ 表 |
| `src/sensor_profiles_generated.inc` | 自动生成的固件内置表，禁止手工修改 |
| `src/sensor_profile_store.h/.cpp` | 启动时加载、校验 LittleFS JSON，并管理内置回退 |
| `data/sensor_profiles.json` | 前置脚本同步生成的 LittleFS 镜像输入文件 |
| `SENSOR_PROFILE_HOT_UPDATE_PLAN.md` | JSON 热更新协议、校验与回退设计 |

## 2. 总体流程

```text
上电（默认 PASS 模式）
        |
        | 点击 Dashboard 的 Mode 区域
        v
进入 MASTER 模式
        |
        | AppController 停止旧所有者并向 Rs485Task 投递 StartDiscovery
        v
Rs485Task 独占 Serial1
        |
        | 地址 0，FC03，读取资料地址 41001
        v
发现真实从站地址（例如 15）
        |
        | 使用真实地址读取 41001~41014
        v
解析状态、PN、Device Name
        |
        | findSensorProfile(PN)
        v
匹配当前 LittleFS 或内置参数配置
        |
        | Rs485Task 内部推进 SensorProfilePoller::update()
        v
分块、非阻塞读取参数
        |
        | ScanCompleted 不可变事件快照
        v
loopTask 更新 UI 状态并投递给 StorageTask 写入 CSV
```

发现和参数轮询仍是短周期非阻塞状态机，但只由 `Rs485Task` 推进。
`loopTask` 不访问 `Serial1` 或轮询器中间数组，只消费发现事件、完整扫描快照和
固定诊断状态，因此串口超时不会阻塞触摸、ADC 或屏幕刷新。

### 2.1 启动配置选择

```text
启动 SensorProfileStore
        |
        | 挂载 LittleFS（失败时不自动格式化）
        v
读取 /sensor_profiles.json
        |
        | 解析并执行与生成器一致的校验
        v
有效 ────────────── 无效/缺失/挂载失败
  |                         |
  v                         v
使用 LittleFS Profile      使用固件内置 Profile
```

系统信息页显示当前来源、`config_version`，回退时还会显示简短原因。有效的 LittleFS 文件是完整覆盖配置，不会与内置 PN 列表混合；这样可以避免误以为某个已从文件删除的 PN 仍被支持。

## 3. 地址发现阶段

### 3.1 发现真实地址

设备支持厂商自定义行为：对从站地址 `0` 的 FC03 请求进行响应。请求读取：

- 从站地址：`0`
- 功能码：`03`
- PDU 起始地址：`41000`
- 数量：`1`

资料地址从 1 开始，所以资料中的 `41001` 在 Modbus PDU 中是 `41000`。
响应帧的第一个字节不是 0，而是传感器自己的真实地址。

注意：标准 Modbus 中地址 0 通常是无响应广播地址，这是本项目传感器的自定义发现协议。工装上一次只能连接一个会响应地址 0 的设备，否则会发生总线冲突。

### 3.2 读取身份区

取得真实地址后，读取资料地址 `41001~41014`：

| 资料地址 | 内容 |
|---:|---|
| 41001 | `runStatus` |
| 41002 | `commStatus` |
| 41003 | `calibStatus` |
| 41004 | `configStatus` |
| 41005~41006 | `productNum`，32 位，CDAB |
| 41007~41014 | `deviceName`，16 字节，每个寄存器低字节字符在前 |

发现事务单次响应超时为 500 ms，失败后等待 2 秒再试，每个阶段最多发送 3 次。任意一次成功就立即停止该阶段重试；完整身份读取成功后状态变为 `Success`，不会继续发送发现请求。

### 3.3 特殊 0x67 发现回退

普通地址 0、FC03 发现连续尝试 3 次仍失败时，状态机自动改用厂商私有报文，并独立尝试最多 3 次：

```text
请求：00 67 00 00 00 00 00 00 00 00 00 00 00 00 21 E6
响应：AA 67 xx xx xx xx xx xx xx xx xx xx xx xx CRC_L CRC_H
```

特殊响应固定为 16 字节，`AA` 是设备真实地址。固件检查地址范围、功能码 `0x67`、固定长度和 CRC。特殊设备不保证具有完整身份区，因此发现成功后仅向真实地址发送 FC03，读取资料地址 `41005~41006`，按 CDAB 解析 PN。设备名称和四个状态字段保持为空/0；取得 PN 后仍然使用相同 JSON Profile 读取和显示业务参数。

```text
普通 00/03 发现 x3
  ├─ 成功 -> 读取 41001~41014 -> PN、名称、状态
  └─ 失败 -> 特殊 00/67 发现 x3
                ├─ 成功 -> 仅读取 41005~41006 -> PN
                └─ 失败 -> Discovery Failed
```

## 4. PN 配置结构

屏幕上的 `Device Name` 使用 PN 匹配到的 JSON Profile `name`，不使用身份区 `41007~41014` 返回的原始名称。这样普通发现设备和只支持 PN 的特殊 `0x67` 设备具有一致的显示规则；PN 未配置时显示 `Unsupported PN`。

当前 PN `55995` 的配置在 `config/sensor_profiles.json`。PlatformIO 编译前会自动运行 `generate_sensor_profiles.py`，生成 `src/sensor_profiles_generated.inc`，`sensor_profile.cpp` 不再写死具体 PN。

为了减少 Modbus 请求次数，连续参数被合并为读取块：

| 块编号 | 资料起始地址 | 数量 | 参数 |
|---:|---:|---:|---|
| 0 | 46001 | 2 | `sensorValue` |
| 1 | 46033 | 8 | 四个连续 float 原始值 |
| 2 | 45050 | 3 | 三个连续 UInt16 零点值 |

`SensorField` 中的 `blockIndex` 指定参数属于哪个块，`registerOffset` 是参数相对该块第一个寄存器的偏移量。

例如 `s470Raw` 地址是 46037，所属块从 46033 开始，因此偏移为：

```text
46037 - 46033 = 4
```

## 5. 非阻塞参数轮询

`Rs485Task` 中的 `SensorProfilePoller` 每次只处理一个读取块：

1. 将资料的 1-based 地址减 1，组装 FC03 请求并添加 CRC；
2. 发送后立即返回任务循环；
3. 后续短周期 `update()` 调用逐字节收取响应；
4. 收齐后检查地址、功能码、长度和 CRC；
5. 按字段类型解析数据，并更新 `values[]` 和 `valid[]`；
6. 20 ms 后读取下一块；
7. 一轮全部完成后等待 5000 ms，再开始下一轮；传感器自身约 4 秒更新一次，使用不同周期可避免两个时钟形成慢拍。

单个块 500 ms 没有响应时只把该块参数标为无效。整轮结束后生成固定大小的
`ScanCompleted` 快照，屏幕对无效字段显示 `--`，StorageTask 记录同一份完整快照。

## 6. 数据解析规则

### UInt16

Modbus 响应中每个寄存器按高字节在前组成一个 16 位无符号整数。

### Float32 CDAB

一个 float 占两个寄存器。当前传感器使用低字寄存器在前：

```text
第一个寄存器 = CD
第二个寄存器 = AB
组合后的 32 位数据 = ABCD
```

解析入口是 `decodeFloatCDAB()`。以后若某个 PN 使用 ABCD 或其他顺序，应新增数据类型，不要直接修改 CDAB 的公共实现，以免破坏 PN 55995。

## 7. 页面操作

- Dashboard 的 Mode 区域：切换 PASS / MASTER；
- MASTER 发现成功后，点击 Dashboard 底部提示进入传感器详情；
- 详情页一次显示 4 个参数，最多显示 4 页、共 16 个参数；
- 点击详情页底部循环切换下一页；
- 点击详情页顶部返回 Dashboard；
- 切回 PASS 时，AppController 等待 Rs485Task 停止确认，再清空会话并投递
  `EnterPass` 命令；CSV 也会先由 StorageTask flush/close。

## 8. 增加一个新 PN

1. 只编辑 `config/sensor_profiles.json`，在 `profiles` 数组增加一个对象；
2. 填写唯一 `product_num`、设备 `name` 和最多 16 个 `fields`；
3. 字段地址填写资料中的 1-based 地址，当前功能码只支持 03；
4. 当前字段类型支持 `uint16` 和 `float32`，float 字序必须为 `CDAB`；
5. 正常执行 PlatformIO Build，生成器会校验并自动合并连续寄存器；
6. 不要编辑 `src/sensor_profiles_generated.inc`，它会在下次构建时被覆盖；
7. 用 Modbus Poll 对照至少一个 UInt16 和一个 float；
8. 测试无设备、CRC 错误和非法寄存器时，触摸仍然能够工作。

也可以在命令行单独验证和生成：

```powershell
python generate_sensor_profiles.py
```

配置错误会直接显示 PN、字段位置和错误原因，并让构建失败，防止错误表被烧入设备。

## 9. 上传 LittleFS 配置

第一次使用或修改 JSON 后执行：

```powershell
pio run -t uploadfs
```

前置脚本会先把 `config/sensor_profiles.json` 规范化同步到 `data/sensor_profiles.json`，再由 PlatformIO 生成并上传 LittleFS 镜像。这里只更新文件系统，不重新编译业务代码；设备重新启动后加载新配置。

烧录全新设备时，需要分别上传固件和文件系统。没有上传文件系统也能正常启动，但系统信息页会显示 `Profile: Built-in`。

## 10. USB CONFIG 上传

### 10.1 进入与退出

1. 点击 Dashboard 顶部进入 `SYSTEM INFO`；
2. 点击底部 `CONFIG UPLOAD`；
3. 屏幕显示 `Waiting for ESPCFG1` 后运行 PC 工具；
4. 成功后设备自动重启；点击 CONFIG 页面顶部可以取消并重启。

CONFIG 模式会强制停止 MASTER、RS485 透传和 USB 本地 Modbus，USB CDC 只接收配置上传协议，避免 JSON 被误转发。

### 10.2 PC 上传命令

推荐使用 Windows 图形上位机，双击：

```text
tools/Start_Profile_Updater.bat
```

界面操作：

1. `Refresh` 扫描 COM 口；
2. 选择 ESP32 的 COM 口；
3. `Browse` 选择 JSON，默认指向工程的 `config/sensor_profiles.json`；
4. 检查界面显示的版本、PN 数量和 PN 列表；
5. 先让设备进入 `CONFIG UPLOAD`，再点击 `Upload Profile`；
6. 观察进度条、日志和最终 ACK/NAK。

该上位机使用 Windows 自带 PowerShell + WinForms，不依赖 Python。命令行方式仍然保留：

```powershell
python tools/upload_sensor_profiles.py COM21 config/sensor_profiles.json
```

脚本需要 `pyserial`。如果当前 Python 环境没有安装：

```powershell
python -m pip install pyserial
```

协议格式：

```text
Magic   7 bytes   "ESPCFG1"
Length  4 bytes   JSON 长度，UInt32 little-endian
CRC32   4 bytes   标准 CRC32，UInt32 little-endian
Payload N bytes   UTF-8 JSON 原始内容
```

为防止 ESP32-S3 USB CDC 接收缓冲区溢出，协议带有块级握手：设备接受 Header 后返回 `R`；PC 每发送 64 字节后等待设备返回 `P`，收到后才继续下一块。所有块完成后才返回最终 `ACK` 或 `NAK`。

ESP32 流式写入 `/sensor_profiles.uploading`，收完后检查 CRC32 和全部 Profile 规则。成功时备份旧配置、原子替换、返回 `ACK` 并重启；失败时删除临时文件、返回 `NAK:原因`，正式配置不变。

## 11. 维护时不要踩的坑

- 不要从 `loopTask`、UI 或 USB 服务直接访问 `Serial1`；PASS、发现和轮询必须
  通过 `Rs485Service` 的固定命令/事件接口。
- 不要同时修改 JSON 和自动生成的 `.inc`；JSON 是唯一人工维护入口。
- 配置表写资料地址（1-based），真正发包时统一减 1。
- PN 55995 的 float 是 CDAB，设备名则是每个寄存器低字节字符在前，这两种字节规则不要混淆。
- 地址 0 发现完成后，所有正常参数读取必须使用发现到的真实地址。
- 一次总线上不要连接多个会响应地址 0 的传感器。
- 不要把 `LittleFS.begin(false)` 改成自动格式化；现场配置损坏时应该回退并保留文件，便于排查。
- 运行上传脚本前必须先让屏幕进入 CONFIG UPLOAD；普通 PASS 模式会把协议数据当作 Modbus/透传数据。
