# 传感器自动发现与配置热更新规划

> 项目：ESP32-S3 RS485 检测工装  
> 初始规划：2026-07-17  
> 最近更新：2026-07-18  
> 状态：地址发现、身份读取和内置 Profile 已完成；JSON/LittleFS 热更新尚未实施

当前运行细节和维护方法见 `SENSOR_RUNTIME_WORKFLOW.md`。本文重点保留热更新的目标、数据模型和后续实施步骤。

## 0. 当前实施进度

已经完成：

- 地址 0 自定义发现，响应帧地址字节即设备真实地址；
- 使用真实地址读取 `41001~41014` 身份区；
- PN 按 CDAB 解析；
- `deviceName` 从 `41007` 开始读取 8 个寄存器，每个寄存器低字节字符在前；
- 按 PN 查找固件内置 `SensorProfile`；
- PN `55995` 的 8 个显示参数；
- 连续寄存器合并读取；
- 已建立 `sensor_profiles.json`，并由 Python 在编译前校验、合并读取块及生成固件默认表；
- 非阻塞地址发现和参数轮询，通信超时不会卡住触摸；
- 详情页每页显示 4 个参数，点击底部翻页。

尚未完成：

- 通用 JSON schema 的完整数据类型和字序支持；
- LittleFS 配置加载、校验、备份和默认回退；
- USB CONFIG 模式与上传协议；
- PC 端上传工具。

## 1. 背景和目标

当前工程已经具备两路 4-20mA 采样、屏幕和触摸、USB CDC 与 RS485 透传、USB 本地 Modbus 从站 247，以及触摸切换 PASS/MASTER 模式。MASTER 模式已经能够自动发现地址、读取身份并按 PN 选择固件内置配置；下一步是把内置配置扩展为可通过 USB 更新的运行时配置。

目标：

1. 进入 MASTER 时通过地址 0 发现传感器实际地址；
2. 使用实际地址读取固定身份区并取得唯一 PN；
3. 根据 PN 匹配 Sensor Profile；
4. 根据 Profile 自动读取、解析和显示业务寄存器；
5. 通过 USB 串口上传 JSON 支持列表，无需重新编译；
6. 配置损坏时自动回退到固件内置默认配置。

## 2. 地址 0 的协议约束

标准 Modbus RTU 中地址 0 通常是只写不回包的广播地址。当前传感器会响应地址 0，属于厂商自定义发现协议。

- 工装发现时必须保证一次只连接一个会响应地址 0 的传感器；
- 多个设备同时响应会造成总线冲突和 CRC 错误；
- 地址 0 只在进入 MASTER 或手动重新扫描时使用；
- 发现成功后，正常通信全部使用实际从站地址。

## 3. 固定身份区

寄存器地址以 1 开始：

| 1-based 地址 | 类型 | 字段 |
|---:|---|---|
| 41001 | UInt16 | `runStatus` |
| 41002 | UInt16 | `commStatus` |
| 41003 | UInt16 | `calibStatus` |
| 41004 | UInt16 | `configStatus` |
| 41005～41006 | UInt32 | `productNum`（唯一 PN） |
| 41007～41014 | Char[16] | `deviceName` |

协议 PDU 地址需要减 1：

```cpp
constexpr uint16_t IDENTITY_START_ADDRESS = 41000;
constexpr uint16_t IDENTITY_REG_COUNT = 14;
```

运行时结构建议：

```cpp
struct SensorIdentity {
    uint8_t slaveAddress;
    uint16_t runStatus;
    uint16_t commStatus;
    uint16_t calibStatus;
    uint16_t configStatus;
    uint32_t productNum;
    char deviceName[17];
};
```

已确认：地址 0 使用 FC03 读取 41001；响应帧第一个字节为实际地址；PN 使用 CDAB；设备名称每个寄存器低字节字符在前。

## 4. 两阶段发现流程

### 4.1 发现实际地址

当前实现发送：

```text
Slave ID：0
Function：03
PDU Start：41000
Quantity：1
```

已经确认响应地址字段为设备实际地址：

```text
请求：00 03 ...
响应：0F 03 02 ...
      ↑ 实际地址 15
```

发现阶段已在 `SensorDiscovery` 中实现为非阻塞自定义 RTU 事务，原因是 `ModbusMaster` 不适合处理“请求地址 0、响应地址非 0”：

1. 组装请求及 CRC16；
2. 通过 `Serial1` 发送；
3. 按字节数和 RTU 帧间隔接收完整响应；
4. 校验 CRC；
5. 接受响应地址 1～247；
6. 保存实际地址；
7. 超时建议 300～500ms，最多重试 3 次。

### 4.2 读取完整身份区

发现地址后发送：

```text
Slave ID：发现到的实际地址
Function：03
PDU Start：41000
Quantity：14
```

解析四个状态寄存器、`productNum` 和 `deviceName`。当前保留两阶段方式：地址 0 只读取一个寄存器，取得真实地址后再读取完整身份区，便于定位通信错误。

## 5. MASTER 状态机（当前实现）

```text
PASS
  ↓ 点击 Mode
DISCOVERING_ADDRESS
  ├─ 超时/CRC错 → 最多重试 3 次 → FAILED
  └─ 成功 → READING_IDENTITY
                  ├─ 失败 → FAILED
                  └─ 成功 → LOOKUP_PROFILE
                                  ├─ 未匹配 → UNSUPPORTED
                                  └─ 匹配 → NON_BLOCKING_POLL
                                                   ↓
                                             RENDER_FIELDS
```

地址和身份状态位于 `SensorDiscovery`；业务参数轮询状态位于 `SensorProfilePoller`。两个状态机的 `update()` 都由 `loop()` 持续调用，不允许同步等待从站响应。

规则：

- 每次进入 MASTER 重新发现；
- 切回 PASS 时停止主站事务并清空 RS485 缓冲区；
- 更换传感器后重新进入 MASTER 或点击“重新扫描”；
- 未知 PN 显示身份信息，但不盲目轮询业务寄存器。

## 6. 屏幕状态

```text
Scanning sensor...
```

```text
Addr: 15
PN: 12345678
```

匹配成功后显示 Profile 名称和动态字段。未知型号显示：

```text
Unsupported Sensor
Addr: 15
PN: 12345678
```

错误需要区分 `No response`、`Discovery timeout`、`Identity timeout` 和 `CRC error`。

## 7. Sensor Profile 数据模型

固定身份区属于固件通用协议，不在每个 Profile 中重复。Profile 只描述 PN 对应的业务数据。

当前固件内置 Profile 已由 JSON 生成，并支持：

- 类型：`uint16`、`float32 CDAB`；
- 每个 PN 最多 16 个显示字段，每页 4 个、最多 4 页；
- 按连续地址手动定义读取块；
- 标签、小数位和显示顺序。

JSON 热更新目标扩展为：

- 类型：`uint16`、`int16`、`uint32`、`int32`、`float32`、`ascii`；
- 字序：`ABCD`、`CDAB`、`BADC`、`DCBA`；
- 功能码：03、04；
- 字段属性：`label`、`address`、`address_base`、`scale`、`offset`、`decimals`、`unit`。

建议限制调整为：最多 32 个 Profile，每个 Profile 最多 16 个显示字段，JSON 最大尺寸在实现 LittleFS 加载时根据内存测试确定。

## 8. JSON 草案

```json
{
  "schema_version": 1,
  "config_version": "2026.07.17.1",
  "profiles": [
    {
      "product_num": 55995,
      "name": "ST-731SS-T",
      "poll_interval_ms": 1000,
      "fields": [
        {
          "key": "sensorValue",
          "label": "sensorValue",
          "function": 3,
          "address": 46001,
          "address_base": 1,
          "type": "float32",
          "word_order": "CDAB",
          "scale": 1.0,
          "offset": 0.0,
          "decimals": 3,
          "unit": ""
        },
        {
          "key": "t365Raw",
          "label": "T365 Raw",
          "function": 3,
          "address": 46039,
          "address_base": 1,
          "type": "float32",
          "word_order": "CDAB",
          "scale": 1.0,
          "offset": 0.0,
          "decimals": 2,
          "unit": ""
        }
      ]
    }
  ]
}
```

`product_num` 是唯一键。JSON 保留手册中的 1-based 地址，加载时统一转换为 PDU 0-based 地址。

## 9. 连续寄存器合并

加载 Profile 后，把功能码相同且地址连续的字段合并成一次请求。例如：

```text
s470Raw：46037～46038
t365Raw：46039～46040
```

应合并为功能码 03、起始地址 46037（手册 1-based）、数量 4。响应后根据字段偏移分别解析两个 Float CDAB，减少总线事务。

## 10. 配置存储

采用“固件默认配置 + LittleFS 运行时覆盖”：

```text
/profiles.json       当前有效配置
/profiles.backup     上一个有效配置
/profiles.uploading  上传临时文件
```

启动时：

1. 加载 `/profiles.json`；
2. 校验 JSON、schema、字段类型和地址范围；
3. 成功后转换为固定大小的 C++ 结构体数组；
4. 失败时使用固件内置默认 Profile；
5. 系统信息页显示配置版本和来源。

## 11. USB 串口热更新

正常 USB CDC 承载 Modbus RTU，不能直接混入 JSON。需要通过系统信息页长按等方式进入独立 CONFIG 模式：

```text
NORMAL
  ↓ 长按
CONFIG MODE
  - 停止 MASTER
  - 停止 RS485 透传
  - USB 只接收配置上传协议
```

上传协议建议：

```text
Magic："ESPCFG1"
Length：JSON 字节数（UInt32）
CRC32：JSON 内容校验
Payload：JSON 内容
```

ESP32 返回 `ACK` 或 `NAK`。更新必须原子化：

1. 写入 `/profiles.uploading`；
2. 检查长度和 CRC32；
3. 解析并验证 JSON；
4. 检查重复 PN、非法地址、数量和未知类型；
5. 备份旧配置；
6. 临时文件替换正式文件；
7. 第一版上传成功后自动重启。

PC 端第一版使用 Python 工具：

```powershell
python upload_profiles.py COM21 sensors.json
```

后续可升级为专用上位机或 WebSerial 页面。

## 12. 热更新能力边界

无需编译即可更新：

- PN 列表；
- 业务寄存器和功能码 03/04；
- 数据类型、字序、比例和偏移；
- 标签、单位、小数位数和显示顺序；
- 轮询周期。

仍需更新固件：

- 尚未支持的新功能码；
- 非 Modbus 协议；
- 特殊校验或私有解码算法；
- 超出现有 UI/运行时结构能力的功能。

## 13. 与当前功能兼容

必须保留：

- PASS 模式 USB↔RS485 透传；
- USB 本地 Modbus 从站 247；
- 本地 40001～40004：两路电流 Float CDAB；
- 本地 40005：当前模式；
- 本地 40006～40007：ADC 原始值；
- 触摸切换 PASS/MASTER；
- 两路模拟量校准采样和屏幕显示。

CONFIG 模式与正常 Modbus 模式必须互斥，避免 JSON 被转发到 RS485。

## 14. 分阶段实施与验收

### 阶段 A：确认发现协议（已完成）

- [x] 捕获地址 0 请求和真实响应帧；
- [x] 确认响应地址字段；
- [x] 确认 PN 字序为 CDAB；
- [x] 确认一次只连接一个设备；
- [x] 确认单次超时 500 ms、重试间隔 2 秒、每阶段最多尝试 5 次。

验收：能够稳定发现实际地址，连续测试无误识别。

### 阶段 B：固定身份区与状态机（已完成）

- [x] 增加身份结果结构；
- [x] 实现地址 0 非阻塞自定义 RTU 事务；
- [x] 用实际地址读取 41001～41014；
- [x] 解析地址、四个状态、PN 和设备名称；
- [x] 增加未知 PN 和通信错误状态显示。

验收：不依赖业务寄存器即可识别传感器并显示身份。

### 阶段 C：通用 Profile 引擎（基础版本已完成）

- [x] 将 PN 55995 的 8 个参数改为内置 Profile；
- [x] 支持 `uint16` 和 `float32 CDAB`；
- [ ] 支持其余基础数据类型和字序；
- [x] 支持字段标签、显示顺序和 decimals；
- [ ] 支持 scale/offset/unit；
- [x] 合并连续寄存器；
- [x] 非阻塞轮询，超时不影响触摸；
- [x] 动态渲染字段并支持两页切换。
- [x] 使用 JSON 作为内置 Profile 的唯一配置源；
- [x] 增加 Python 编译前校验和 C++ 表生成器；

验收：现有传感器功能不变，但 `main.cpp` 不再写死业务寄存器。

### 阶段 D：LittleFS JSON

- [ ] 定稿 schema v1；
- [ ] 增加 JSON 解析和严格校验；
- [ ] 转换为固定大小的运行时结构；
- [ ] 增加固件默认配置回退；
- [ ] 显示配置版本和来源。

验收：替换 LittleFS 配置即可增加或修改 Profile。

### 阶段 E：USB 上传

- [ ] 增加触摸 CONFIG 模式；
- [ ] 实现长度和 CRC32；
- [ ] 实现临时文件与原子替换；
- [ ] 实现 ACK/NAK；
- [ ] 编写 `upload_profiles.py`；
- [ ] 成功后自动重启。

验收：不重新编译，仅通过 COM 口更新支持列表。

### 阶段 F：可靠性测试

- [ ] 上传中途断电；
- [ ] JSON/CRC 错误；
- [ ] 重复 PN 或非法地址；
- [ ] 未知类型和未知 PN；
- [ ] 地址 0 无响应；
- [ ] 多设备响应导致 CRC 错误；
- [ ] PASS/MASTER/CONFIG 反复切换；
- [ ] 配置损坏后的默认回退。

## 15. 已有验证样本

- 真实地址：15；
- 真实 PN：55995；
- 设备名称：`ST-731SS-T`；
- PN 字序：CDAB；
- 业务参数：5 个 Float32 CDAB、3 个 UInt16；
- 地址和业务寄存器均按资料 1-based 地址配置。

## 16. 推荐的下一步

下一步进入阶段 C 后半段和阶段 D：

1. 在现有 schema v1 上增加其余数据类型、字序、scale、offset 和 unit；
2. 选择并引入 ESP32 运行时 JSON 解析库；
3. 启用 LittleFS，先实现“生成的固件默认配置 + 文件覆盖 + 校验失败回退”；
4. 让运行时解析器采用与 Python 生成器相同的校验规则；
5. 使用预置文件验证成功后，再实现 USB CONFIG 上传模式。

暂时不要直接做串口上传。先让同一套 Profile 引擎能够同时加载“固件内置 PN 55995”和 LittleFS 中的等价 JSON，确认两者显示结果完全一致。
