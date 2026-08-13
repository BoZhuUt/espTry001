# 校准模式实现规划

## 1. 目标

在现有 ESP32-S3 RS485 Utility 的 2.8 英寸机型基础上增加本机触摸屏校准模式。固件按照传感器 PN 解析手机 APP 使用的 JSON 校准定义，在不阻塞 UI 和 FreeRTOS 任务的前提下，通过 Modbus RTU 顺序执行校准命令。第一阶段以“所有写响应校验成功，并完成 JSON 指定的最后等待时间”作为校准成功判据。

第一阶段以 PN 57769（HW-201T）为落地对象，同时保留可扩展的数据结构，使其他传感器可以通过增加 JSON 配置获得校准能力，而不需要为每个 PN 编写独立流程代码。

本规划只覆盖校准功能。`Config`、`Span4_20mA`、`RestoreFactory` 暂不纳入第一阶段，尤其恢复出厂应作为独立的高风险功能设计。

## 2. 已确认的协议约定

以下约定已经确认，可直接作为实现依据：

- JSON 中的寄存器地址采用 1-based 表示，发送 Modbus 请求时减 1。
- `RegType: "ushort"` 使用 Modbus FC06 写单个保持寄存器。
- `RegType: "float"` 使用 Modbus FC16 写两个连续保持寄存器。
- Float32 使用 CDAB 字序，与现有传感器 Float32 读取规则一致。
- 校准成功判据是：每条写命令均收到并通过 Modbus 响应校验，且最后一条命令的 `Waiting_mS` 已结束。
- `calibStatus` 不参与第一阶段成功判定；退出校准后重新发现设备时仍会读取并刷新该诊断值。
- `Waiting_mS` 是命令完成后的最短等待时间，必须由非阻塞状态机实现，不在 UI 或 RS485 任务中直接长时间 `delay()`。

## 3. 57769.json 现状分析

当前文件实际位于工程目录上一级：

```text
D:\pyxis\software\github\espTry001\57769.json
```

当前 PlatformIO 工程根目录为：

```text
D:\pyxis\software\github\espTry001\espTry001
```

正式纳入构建时，建议将 APP 原始配置放入工程内部：

```text
config/calibration_profiles/57769.json
```

### 3.1 零点校准

JSON 路径：

```text
Readings[0].Calibrations[0]
```

执行流程：

1. 向 1-based 地址 43006 写入 UInt16 值 21，使用 FC06。
2. 写响应校验成功后等待至少 1000 ms。
3. 等待至少 1000 ms。
4. 等待结束后报告校准成功。

### 3.2 斜率校准

JSON 路径：

```text
Readings[0].Calibrations[1]
```

执行流程：

1. 用户输入标准液浓度，范围 1～1000 ppm，JSON 默认值为 100 ppm。
2. 将该 Float32 值以 CDAB 字序写入 1-based 地址 44002，使用 FC16 写两个寄存器。
3. 写响应校验成功后等待至少 100 ms。
4. 向 1-based 地址 43006 写入 UInt16 值 22，使用 FC06。
5. 写响应校验成功后等待至少 1000 ms。
6. 等待至少 1000 ms。
7. 等待结束后报告校准成功。
8. 可选读回 48002 的 Float32 slope 并展示给用户，但该值不作为第一阶段的成功判据。

### 3.3 calibStatus 的来源及第一阶段处理

`57769.json` 本身没有出现以下内容：

- `calibStatus` 字段；
- 41003 寄存器定义；
- 校准状态码与含义的映射；
- 校准状态查询间隔或总超时时间。

`calibStatus` 来自固件现有的身份区读取协议，而不是 APP JSON：

- `SensorDiscovery::kIdentityRegister` 为 0-based 地址 41000，即文档中的 1-based 地址 41001；
- 身份区连续读取 14 个寄存器；
- `responseRegister(2)` 被解析为 `result_.calibStatus`；
- 因此 `calibStatus` 对应 1-based 地址 41003。

由于 APP JSON 没有给出状态码含义，第一阶段不在校准执行过程中轮询 41003，也不依据 `calibStatus` 宣称成功或失败。退出校准后，固件按照既定方案重新运行发现流程；此时现有 `SensorDiscovery` 会自然刷新该值，可将其保留为诊断信息。

## 4. 第一阶段成功与失败语义

对于 57769 这类 WaitOnly 配置，APP JSON 只定义写命令和命令后的等待时间，没有定义校准结果寄存器或状态码映射，因此采用以下明确语义：

- 每条 FC06/FC16 请求均收到合法响应，且响应回显与请求一致；
- 每条命令完成后等待对应的 `Waiting_mS`；
- 最后一条命令等待结束后，UI 显示“命令执行完成”；
- CRC 错误、Modbus 异常响应、响应回显不符或响应超时均判为执行失败；
- 若写请求已经发出但没有收到响应，显示“结果未知”，因为设备可能已经执行了写操作；
- 不根据未知含义的 `calibStatus` 值推断结果。

UI 文案优先使用“Calibration command completed / 校准命令执行完成”，避免在缺少设备侧结果定义时作出过强保证。未来如果获得状态码文档，可以将 41003 轮询作为增强功能加入，而不改变 JSON 命令模型。

## 4.1 原始设备库审计基线

已对以下目录执行只读扫描：

```text
D:\pyxis\software\github\espTry001\devices\devices
```

截至 2026-08-13 的统计结果：

| 项目 | 数量/结果 |
|---|---:|
| JSON 文件 | 251 |
| JSON 语法有效 | 251 |
| 含 `Readings[].Calibrations[]` | 199 |
| 不含校准项目 | 52 |
| 校准项目总数 | 756 |
| 校准命令总数 | 1387 |
| 可编辑命令 | 494 |
| `float` 校准命令 | 520 |
| `ushort` 校准命令 | 867 |
| 带非零结果寄存器的命令 | 736 |
| 带结果寄存器的设备文件 | 190 |
| 原始 JSON 总大小 | 约 5.04 MiB |

审计结论：

- 所有校准写命令的 `RegType` 只有 `float` 和 `ushort`，当前 FC16/CDAB 与 FC06 设计可以覆盖；
- 单个 Reading 最多 5 个校准项目，单个校准项目最多 4 条命令；
- 单文件最多 9 个 Reading、11 个校准项目、20 条命令；
- 473 条 Float 和 21 条 UInt16 写命令需要用户输入；
- 17 个校准项目包含多个可编辑命令，单个校准项目最多需要输入 3 个值；
- 输入范围包含负数、小数和很大的上限，因此数字键盘必须支持可选负号、小数点和足够的有效位；
- 5 条命令缺少 `Waiting_mS`，规范化时需使用明确默认值并在上位机中给出警告；
- 40 条命令包含软件或硬件版本条件，不能简单丢弃；
- 存在同 PN 多份文件以及一个文件名与 JSON PN 不一致的情况，必须在导入阶段解决冲突。

### 4.2 两种完成模式

全库审计表明，校准流程必须支持两种完成模式：

1. **WaitOnly**：`ResultRegAddress` 为 0 或缺省。所有写响应成功并完成最后等待后，报告“校准命令执行完成”。57769 属于此类。
2. **MappedResult**：相关命令提供非零 `ResultRegAddress`、`ResultRegType` 和 `Results[]`。完成等待后使用 FC03 轮询结果寄存器，将 UInt16 原始值与 `Results[].RegValue` 匹配，并按结构化 outcome 处理成功、失败或继续等待。

结果寄存器不能写死为 41003。库中常见地址包括 44002、41003、1031、48031、41002、48050 等。当前 736 个有效结果定义的 `ResultRegType` 均为 `ushort`，`Results[]` 结构统一为：

```json
{
  "RegValue": 769,
  "ResultMessage": "Calibration succeed."
}
```

对于 MappedResult：

- 匹配到 `RegValue` 时原样显示对应消息；
- 不通过英文关键词猜测成功或失败；
- 规范化配置应显式标注每个结果为 success/failure/info，标注由上位机导入规则生成并允许人工审核；
- 原始消息为 in process、in progress 或 idle 时，转换器建议标为 pending，执行器继续非阻塞轮询；
- 未匹配到任何值时显示原始寄存器值和“未知设备结果”，不能按成功处理；
- 结果读取通信失败与设备返回失败结果必须区分。

### 4.3 PN 与变体冲突

发现 12 组重复 PN，包括 `_ST500`/`_ST588`、普通版/`_New` 等。PN 56377 同时存在 `56377.json` 和 `56377_New.json`，且根级版本范围为空，不能仅凭 PN 自动选择。

另有 `50316.json` 内部 PN 为 50352，与文件名不一致。该文件已决定从校准配置导入范围中排除；原始文件不修改。

运行时索引键建议为：

```text
PN + 可选变体标识 + 可选硬件/软件/参数版本范围
```

如果同一 PN 的多个候选配置无法通过已读取的设备版本唯一匹配，设备端不得任意选择，应显示“Calibration profile ambiguous”。上位机可以在导出固件包时让用户明确选定该 PN 的默认变体。当前已经确认的例外规则是：PN 56377 固定采用 `56377_New.json`，忽略 `56377.json`。

### 4.4 与当前 Master Profile 的交集

当前 Master 配置有 7 个 PN。设备库对应情况：

| Master PN | JSON 情况 | 校准情况 |
|---:|---|---:|
| 55995 | `55995.json` | 4 项、7 命令 |
| 56377 | 固定采用 `56377_New.json` | 4 项、7 命令 |
| 56600 | 无对应 JSON，明确排除 | 不支持 |
| 53001 | `53001.json` | 5 项、12 命令 |
| 53607 | `53607.json` | 8 项、15 命令 |
| 53251 | `53251.json` | 3 项、5 命令 |
| 50661 | `50661.json` | 2 项、3 命令 |

第一轮实作建议选择：

- PN 57769：验证 WaitOnly、Float 输入、FC06/FC16；
- PN 50661：验证最简单的 MappedResult；
- PN 53607：验证较复杂的多 Reading、多校准项目和分页；
- 其余无冲突 Master PN 在框架稳定后批量导入；
- PN 56377 固定导入 `_New` 版本；
- PN 56600 不纳入校准功能，不显示校准入口；
- `50316.json` 不纳入导入、生成或固件配置。

## 5. JSON 支持范围

规范化格式、字段约束和导入规则以同级文档 `CALIBRATION_PROFILE_SCHEMA.md` 为准。本节保留总体范围，实施时不得让转换器、上位机和固件各自维护不同的隐式 Schema。

### 5.1 第一阶段支持的节点

第一阶段解析以下字段：

- 根节点：`PN`、`ModelName`；
- `Readings[]`：`Name`、`Unit`、`RegAddress`、`RegType`、`Decimal`；
- `Calibrations[]`：`Name`、`Message`；
- `Cmds[]`：
  - `RegAddress`；
  - `RegType`；
  - `RegLen`，允许缺省并根据类型推导；
  - `Editable`；
  - `SetValue`；
  - `SetValueMin`；
  - `SetValueMax`；
  - `Waiting_mS`；
  - `Unit`；
  - `ResultRegAddress`；
  - `ResultRegType`；
  - `Results`。

未知字段可以忽略，但已支持字段如果类型错误、范围无效或命令不安全，应拒绝整份配置并报告明确错误，不能静默使用部分流程。

### 5.2 固定容量约束

运行时继续沿用当前项目的固定容量原则，但不把 199 个校准 Profile 全部展开到 RAM。ArduinoJson 仅用于解析当前匹配的一个设备文件，解析完成后释放。

根据实测库并预留余量，活动 Profile 建议上限：

- 最多 12 个 Reading；
- 最多 16 个 Calibration；
- 每个 Calibration 最多 4 条命令；
- 每个活动 Profile 最多 32 条命令；
- 每条结果命令最多 8 个 `RegValue/ResultMessage` 映射；
- 名称、消息和单位使用固定长度 UTF-8 字符数组；
- 单个精简校准文件设置明确的最大字节数。

全库只在 Flash 中保留小型索引；发现 PN 后按需打开一个精简文件并装载一个活动 Profile。这样支持的 PN 数量主要受 LittleFS 容量约束，而不是静态 RAM 数组数量约束。

最终上限应结合编译后的静态 RAM、任务栈和 LittleFS 空间测量后确定。

### 5.3 JSON 校验规则

构建期和运行时必须执行一致的校验：

- PN 必须为有效且唯一的 UInt32；
- `RegAddress` 必须在 1～65536；
- `ushort` 的寄存器长度必须为 1；
- `float` 的寄存器长度必须为 2，且地址不能越界；
- `Editable: true` 时必须提供合法的最小值和最大值；
- 默认值必须位于允许范围内；
- `Waiting_mS` 必须设置合理上限，防止错误配置导致永久等待；
- 第一阶段只允许 FC06/FC16 所对应的 `ushort` 和 `float`；
- 不允许空命令校准项；
- 不允许将只读测量区误作为校准写入目标，必要时增加允许写入地址范围白名单。

## 6. 配置文件组织与生成

建议新增：

```text
config/calibration_profiles/*.json
data/calibration/index.json
data/calibration/<profile-id>.json
src/calibration_index_generated.inc
generate_calibration_profiles.py
```

构建期生成器负责：

1. 枚举 `config/calibration_profiles/*.json`；
2. 按受支持的 APP JSON 子集逐项校验；
3. 检查 PN 重复、容量上限和写入地址；
4. 生成仅含 PN、变体、版本条件和文件名的小型内置索引；
5. 为每个设备生成只包含校准所需字段的 LittleFS 精简文件；
6. 保证构建期和设备运行时使用同一套字段约束。

不生成包含全部 251 份 APP JSON 的单个大文件。原始库约 5.04 MiB，整体 ArduinoJson 反序列化不适合本项目；按设备拆分还能让单个 PN 热更新而无需重传整库。

原始 APP JSON 仍作为人工维护入口；生成文件不得手工修改。

### 6.1 校准 JSON 上位机

校准 JSON 数量较多时，应提供与现有 `SensorProfileManager.exe` 工作方式相似的可视化上位机。现有工具使用 C# WinForms 和系统自带 JSON 能力，不依赖额外运行库，适合继续沿用。

建议新建独立的 `CalibrationProfileManager.exe`，而不是把大量校准表格直接塞进现有 Master Profile 编辑页。两种 JSON 的用途不同：

- `SensorProfileManager` 编辑周期读取字段；
- `CalibrationProfileManager` 编辑 Reading、Calibration 和有顺序的写命令；
- 两个工具可以复用语言切换、文件选择、校验、PDF 报告、打包脚本和界面风格；
- 后续如果需要统一入口，可再增加一个启动器，不必让单个窗口承担两套复杂模型。

上位机建议采用“一个目录管理多个 PN JSON”的方式，并提供：

- 打开单个 APP JSON；
- 导入多个文件或整个目录；
- 左侧按 PN、ModelName 显示传感器列表；
- 编辑 Reading 的名称、单位和读取地址；
- 编辑校准项目的名称、提示文本及顺序；
- 用可上下移动的表格编辑 `Cmds[]`，命令顺序必须清晰可见；
- 根据 `RegType` 自动限制 RegLen，并显示实际 FC06/FC16；
- 编辑默认值、最小值、最大值、单位和等待时间；
- 复制相似 PN、另存为新 PN、删除 Profile；
- 全目录校验、重复 PN 检查、地址范围检查和容量预算；
- 输出每个 PN 的 APP JSON；
- 一键生成固件使用的小型索引和逐设备精简 JSON；
- 生成便于审核的 PDF 报告；
- 显示“固件支持/暂不支持”的 JSON 节点，防止误以为 `RestoreFactory` 等功能已经可用。

导入工具需要支持项目级排除/覆盖清单，且每次批量导入都应用同一规则，避免人工重复处理。第一版清单至少包含：

```json
{
  "exclude_files": ["50316.json"],
  "exclude_pns": [56600],
  "preferred_files": {
    "56377": "56377_New.json"
  }
}
```

清单只控制生成结果，不删除或改写原始设备库。上位机报告应列出每个被排除或被替代的文件及原因。

保存 APP JSON 时必须保留工具尚未支持的根节点和字段，例如 `Info`、`Config`、`Diagnosis`、`Span4_20mA` 和 `RestoreFactory`。推荐在内存中保留原始 JSON 对象，只替换工具负责编辑的节点，避免一次保存意外删除手机 APP 仍需要的数据。

推荐工作流：

```text
多个手机 APP JSON
  → CalibrationProfileManager 导入/编辑/校验
  → 保存各 PN 原始 JSON
  → 生成 config/calibration_profiles/*.json
  → 构建脚本生成索引和逐设备 LittleFS 文件
  → 校准配置上传器更新设备
```

第一阶段可以先完成编辑、校验和构建期生成。运行时热更新时再扩展上传协议，更新 `/calibration/index.json` 和 `/calibration/<profile-id>.json`，避免覆盖现有 `/sensor_profiles.json`。上传协议应明确配置类型、目标 Profile ID 和完整性校验，不能让设备根据 JSON 内容猜测文件类型。

## 7. 固件模块设计

### 7.1 CalibrationProfileStore

新增独立的 `CalibrationProfileStore`，不扩充现有只读采集用 `SensorProfileStore`。

职责：

- 启动时加载小型 `/calibration/index.json`，或使用内置索引回退；
- 按 PN、变体和版本条件解析唯一候选文件；
- 只将当前设备的精简 JSON 解析并复制到固定数组；
- LittleFS 文件无效时，仅对编译进固件的首批验证 Profile 提供回退；
- 提供配置来源、版本和最后错误信息；
- 不持有可悬空的 ArduinoJson 字符串指针。

建议核心结构：

```cpp
enum class CalibrationValueType : uint8_t {
  UInt16,
  Float32CDAB,
};

enum class CalibrationCompletionMode : uint8_t {
  WaitOnly,
  MappedResult,
};

enum class CalibrationResultOutcome : uint8_t {
  Success,
  Failure,
  Pending,
  Info,
};

struct CalibrationResultMapping {
  uint16_t registerValue;
  const char *message;
  // Success / Failure / Pending / Info；设备端不分析英文消息关键词。
  CalibrationResultOutcome outcome;
};

struct CalibrationCommand {
  uint32_t address1Based;
  CalibrationValueType type;
  bool editable;
  float defaultValue;
  float minimumValue;
  float maximumValue;
  uint32_t waitingMs;
  uint32_t resultAddress1Based;
  CalibrationCompletionMode completionMode;
  const CalibrationResultMapping *results;
  uint8_t resultCount;
};

struct CalibrationProcedure {
  const char *name;
  const char *message;
  const char *unit;
  const CalibrationCommand *commands;
  uint8_t commandCount;
};

struct CalibrationProfile {
  uint32_t productNum;
  const char *modelName;
  const CalibrationProcedure *procedures;
  uint8_t procedureCount;
};
```

实际运行时结构需要拥有字符串存储，不能直接引用 JsonDocument 内存。

### 7.2 CalibrationExecutor

新增非阻塞执行器，由 `Rs485Task` 单独推进。建议状态：

```text
Idle
Preparing
SendingWrite
WaitingWriteResponse
WaitingCommandDelay
SendingResultRead
WaitingResultResponse
Success
Failed
Cancelled
```

职责：

- 记录从站地址、PN、校准项和用户输入；
- 生成 FC06、FC16，以及 MappedResult 所需的 FC03 RTU 请求；
- 校验从站地址、功能码、响应长度、回显内容和 CRC；
- 解析 Modbus 异常响应；
- 按 `Waiting_mS` 非阻塞推进；
- WaitOnly 在所有写响应成功并完成最后等待后发布完成事件；
- MappedResult 在完成等待后轮询配置指定的结果寄存器，pending 继续等待，success/failure 结束；
- 支持安全取消和总超时；
- 保存最后错误、当前命令序号、命令总数和剩余等待时间。

写请求超时后不应自动无条件重发触发命令。虽然写寄存器通常具有幂等性，但校准触发值可能引起重复操作。第一阶段建议将“响应超时”直接报告为结果未知，由用户重新读取状态或明确选择重试。

### 7.3 公共 Modbus RTU 能力

现有 `SensorDiscovery` 和 `SensorProfilePoller` 分别实现了 CRC、收帧与校验。实现校准前，应评估抽取小型公共组件：

- CRC16；
- RTU 帧接收器；
- FC03 请求/响应；
- FC06 请求/响应；
- FC16 请求/响应；
- 异常响应解析。

目标是避免第三套略有不同的 CRC 和超时逻辑，但不能为了复用而大规模改写已经稳定的发现和轮询流程。可以先抽取无状态的组帧/校验函数，再逐步复用。

## 8. Rs485Service 集成

在枚举尾部追加，保持现有诊断寄存器中的数值兼容：

```cpp
Rs485Service::Mode::Calibration
AppMode::Calibration
```

新增命令建议包括：

- `StartCalibration`；
- `CancelCalibration`。

新增事件建议包括：

- `CalibrationStarted`；
- `CalibrationProgress`；
- `CalibrationResultRead`；
- `CalibrationCompleted`；
- `CalibrationFailed`；
- `CalibrationCancelled`。

命令和事件必须复制必要数据，不允许跨任务保存 UI 临时对象或 JsonDocument 指针。

## 9. 应用模式切换

### 9.1 进入校准模式

建议只允许在以下条件全部满足时进入：

- 当前处于 Master 模式；
- 已成功发现真实从站地址和 PN；
- 当前 PN 存在有效校准 Profile；
- SD 日志能够正常停止并完成 flush/close；
- RS485 周期轮询能够停止并获得任务确认；
- 校准命令队列有空间。

切换顺序：

1. 保留已发现的从站地址、PN 和设备名称；
2. 停止并关闭当前日志会话；
3. 停止 Profile 周期轮询；
4. 清理残留串口接收字节；
5. 将应用模式切换为 `Calibration`；
6. 打开校准项目列表页；
7. 用户确认后才真正发送写命令。

### 9.2 退出校准模式

无论成功、失败还是取消，退出时建议：

1. 停止校准状态机并清理残留响应；
2. 返回 Master Discovery；
3. 重新读取身份区，刷新 `calibStatus`、PN 和设备名称；
4. 重新查找采集 Profile；
5. 恢复周期轮询；
6. 根据现有策略开始新的日志会话。

不建议直接复用校准前的轮询状态，因为传感器可能在校准期间重启、改变状态或短暂离线。

## 10. UI 设计

建议从 `Sensor Info` 页面进入校准，只有当前 PN 存在校准定义时才显示入口。

页面流程：

```text
Sensor Info
  → Calibration List
  → Calibration Instructions
  → Value Input 1..N（逐个处理 Editable 命令）
  → Final Confirmation
  → Running / command progress
  → Success 或 Failed
```

第一阶段需要新增：

- `UiPage::CalibrationList`；
- `UiPage::CalibrationInput`；
- `UiPage::CalibrationConfirm`；
- `UiPage::CalibrationProgress`；
- 对应的语义化 `UiAction`；
- `CalibrationState` 固定大小 UI 快照。

交互要求：

- 零点校准必须显示 JSON 中的提示信息并二次确认；
- 斜率校准输入必须限制在 1～1000 ppm；
- 使用屏幕数字键盘，输入尚未确认前不发送任何 Modbus 写入；
- 执行页面展示当前命令、总命令数、剩余等待时间、已用时间和错误详情；
- 执行期间忽略重复启动触摸；
- “返回”与“取消校准”语义分开；
- 如果写命令已发送，取消只能停止后续命令和等待流程，不能宣称设备端操作已撤销。

### 10.1 数值键盘实现

不引入 LVGL，也不需要使用外部 GUI 布局工具。现有项目已经使用 Arduino_GFX 绘制矩形、文字和按钮，并由 `ui_input.cpp` 将触摸坐标转换为语义动作；校准键盘继续使用同一套架构即可。

建议实现一个仅用于数值的轻量键盘，而不是完整字母键盘：

- 顶部显示校准名称、当前输入、单位和允许范围；
- 主区使用 `0～9`、小数点、退格键；
- 底部使用取消和确认按钮；
- 只有配置允许负数时才显示正负号；
- `ushort` 禁用小数点和负号；
- Float 根据配置允许小数，并限制有效字符和长度；
- 一个校准项目有多个 Editable 命令时逐项输入，并显示“参数 1/3”等进度；
- 每次按键后即时解析并检查范围；
- 只有输入合法时确认按钮才可用；
- 点击确认只进入最终确认页，不立即写寄存器。

项目同时支持 172×320 和 240×320 竖屏。键盘应按 `BoardConfig::kDisplayWidth` 计算按钮宽度，使用三列数字布局，以较窄的 172 像素屏幕作为最低适配基准。按钮命中区域与绘制区域使用同一组布局常量，避免显示与触摸坐标不一致。

如果某个校准项目只有少量固定标准值，未来可从 JSON 扩展为预设值按钮；但当前 APP JSON 给出连续的 1～1000 ppm 范围，数字键盘比 `+1/-1` 步进更通用，也适用于后续其他传感器。

## 11. 完成判定策略

推荐流程：

1. 用户确认校准项目和输入值；
2. 按 JSON 顺序执行全部写命令；
3. 每条命令都严格校验写响应；
4. 每条响应成功后进入对应的非阻塞等待；
5. WaitOnly 在最后等待结束后报告“校准命令执行完成”；
6. MappedResult 在最后等待结束后轮询指定 UInt16 结果寄存器并匹配 outcome；
7. 任一明确的 Modbus 错误立即停止后续命令并报告失败；
8. 写请求发出后响应超时则报告“结果未知”；
9. 结果值未匹配时显示原始值并报告“未知设备结果”；
10. 退出页面后重新发现设备，刷新身份区和正常轮询数据。

`calibStatus` 仍随重新发现被读取，但第一阶段不解释它的数值，也不参与完成判定。

## 12. 错误模型

至少区分：

- 不支持该 PN；
- JSON 无效；
- 输入超出范围；
- RS485 命令队列满；
- 写响应超时；
- 写响应 CRC 错误；
- 写响应回显不匹配；
- Modbus 异常响应及异常码；
- 命令等待过程异常；
- 整体执行超时；
- 用户取消；
- 结果未知。

“通信失败”“设备报告校准失败”“结果未知”必须在 UI 和事件中保持不同含义。

## 13. 测试计划

### 13.1 JSON 生成器测试

- 正确解析 57769.json；
- 拒绝缺少 PN、Calibrations 或 Cmds；
- 拒绝重复 PN；
- 拒绝未知 RegType；
- 拒绝越界寄存器地址；
- 拒绝 float 的错误 RegLen；
- 拒绝默认值超出范围；
- 拒绝命令数量和文本长度超限；
- 验证生成结果稳定且可重复。

### 13.2 Modbus 帧测试

- FC06 写 43006=21 的完整请求帧；
- FC06 写 43006=22 的完整请求帧；
- FC16 将 100.0f 以 CDAB 写入 44002；
- 正确处理 CRC 错误、错误地址、错误功能码和异常响应；
- 验证 1-based 到 0-based 的转换只发生一次。

### 13.3 状态机测试

- 零点校准成功；
- 斜率校准成功；
- 第一条写入失败时不发送触发命令；
- 最后一条写入超时；
- 中间命令等待与最后命令等待正确结束；
- 写请求已发出但响应超时，结果标记为未知；
- 用户在写入前取消；
- 用户在触发命令后取消；
- 校准完成后重新发现和恢复轮询。

### 13.4 回归测试

- PASS、MASTER、PRECHECK、UPGRADE PASS 模式切换保持正常；
- CONFIG、LOG EXPORT、TIME SYNC 的 USB 所有权不受影响；
- 校准过程中不写 CSV；
- 退出校准后产生新的日志会话，不把校准前后数据混入同一传感器会话；
- 本地 Modbus 诊断仍可报告模式和错误；
- 任务栈高水位、固定数组和队列深度满足稳定性要求。

## 14. 分阶段实施

### 阶段 A：配置与静态模型

1. 将 57769.json 纳入工程配置目录。
2. 编写校准 JSON 校验/生成脚本。
3. 定义固定容量 Profile 数据结构。
4. 生成小型索引和逐设备 LittleFS 精简文件。
5. 实现 `CalibrationProfileStore`。
6. 新建 `CalibrationProfileManager`，先支持批量导入、核心校准节点编辑、全目录校验和保存。

验收：固件能按 PN 57769 加载两个校准项目，并能完整打印或展示解析后的命令，但不发送写请求。

### 阶段 B：Modbus 校准执行器

1. 实现 FC06、FC16/CDAB 请求及严格的写响应校验。
2. 实现非阻塞命令等待和响应校验。
3. 实现 MappedResult 的 FC03 非阻塞轮询、精确值映射及结果总超时。
4. 增加 Rs485Service 命令、事件和状态快照。
5. 使用模拟从站验证所有错误路径。

验收：不依赖 UI 即可执行 57769 的 WaitOnly 和 50661 的 MappedResult 流程，并正确区分完成、设备失败、通信失败、未知结果与写后响应超时。

### 阶段 C：应用模式与 UI

1. 增加 `AppMode::Calibration`。
2. 实现进入/退出的资源所有权切换。
3. 增加校准列表、说明、输入、确认和进度页面。
4. 显示明确的成功、失败、超时和结果未知状态。
5. 完成退出后的重新发现、轮询和日志恢复。

验收：用户可完全通过触摸屏完成 PN 57769 的零点及斜率校准。

### 阶段 D：实机与稳定性验证

1. 连接真实 PN 57769 设备验证。
2. 验证校准期间断电、拔线、异常响应和重复触摸。
3. 检查栈、堆、队列、日志和模式诊断。
4. 完善上位机 PDF 报告、构建打包和校准配置上传。
5. 更新 README 和传感器运行流程文档。

验收：正常及主要异常场景均不会卡住 RS485、UI 或其他 FreeRTOS 任务，且不会错误宣称校准成功。

## 15. 第一版完成定义

满足以下条件后，校准模式第一版才算完成：

- 57769 APP JSON 是校准流程的人工维护来源；
- 构建期和运行时均有严格 JSON 校验；
- PN 57769 自动出现零点和斜率校准入口；
- UInt16 使用 FC06，Float32 使用 FC16/CDAB；
- 所有等待和通信均为非阻塞状态机；
- 所有写响应成功且最后等待结束时报告“校准命令执行完成”；
- 带结果映射的配置能够读取任意指定结果寄存器并显示精确映射消息；
- 写响应超时能够明确显示“结果未知”；
- 校准前停止日志和轮询，结束后重新发现并恢复；
- UI 使用现有 Arduino_GFX 实现数值键盘，无需引入 LVGL；
- 校准 UI 仅支持 2.8 英寸 `waveshare28`；1.47 英寸机型不加载校准配置、不显示入口，也不纳入校准验收；
- UI 能显示进度、执行完成、失败、超时及结果未知；
- `CalibrationProfileManager` 能管理多个 PN JSON，校验并生成固件配置；
- 完成模拟从站测试、实机测试和原有模式回归测试。

## 16. 当前实施进度（2026-08-13）

已完成：

- 全量只读审计 251 份原始 JSON；
- Schema v1 与项目导入规则；
- `tools/import_calibration_profiles.py` 批量转换/审计器；
- `tools/build_calibration_bundle.py` 规范校验和 approved-only 发布闸门；
- 57769、50661、53607 三份 pilot 规范化 Profile；
- 57769 approved LittleFS 索引和逐设备文件；
- 7 个自包含单元测试；
- `CalibrationProfileManager` WinForms MVP：多 Profile 浏览、步骤树、结果 outcome 审核、`release_status` 切换、校验与保存；
- 上位机已接入现有一键构建和工具打包流程；
- 固件侧 `CalibrationProfileStore` 第一版：按发现 PN 从 LittleFS 索引只加载当前设备 Profile，并使用固定容量结构严格校验 57769 所需字段。
- 非阻塞 `CalibrationExecutor`：FC06、FC16/CDAB、严格写回显校验、命令后等待、MappedResult FC03 轮询，以及完成/设备失败/通信失败/结果未知语义；
- `Rs485Service::Calibration` 独占模式和进度/结束事件；
- Arduino_GFX 校准 MVP：Sensor Info 入口、流程选择、数字键盘、执行确认、进度与结果页；退出后停止校准并重新发现设备。
- 原生 C++ 模拟从站测试直接编译生产 `CalibrationExecutor`，已覆盖 57769 WaitOnly、100.0 Float32 CDAB、FC06/FC16/FC03 精确帧、输入边界、CRC/回显/异常响应、写超时结果未知、MappedResult pending/success/failure/unknown/总超时。

全库 dry-run 当前结果：

- 178 个无冲突校准 Profile 可生成；
- 9 个无需结果审核的 Profile 自动为 approved；
- 167 个 Profile 因结果 outcome 待审核为 draft；
- 50315、50319 因 `ResultRegLen=3` 含义不明被 blocked；
- 10 组重复 PN 等待项目级首选文件规则；
- 54148、59409 的硬件版本地址覆盖已转换为互斥 alternatives，不再 blocked；
- `RegValue:null` 已规范为通信错误文案；
- `>5`、`>=0.0`、`<=0.2` 等字符串边界已规范为数值及 inclusive 标志。

当前环境的 Windows 窗口枚举接口返回“系统找不到指定的路径”，因此 `CalibrationProfileManager` 已完成编译和源码布局检查，但仍需在本机人工打开一次确认视觉布局；检查时不得批准或保存 Profile。

下一步补充 Modbus 模拟从站自动测试，并在 PN 57769 实机上验证 LittleFS 加载、零点 FC06 与斜率 FC16/CDAB。版本条件和多候选 alternatives 在版本选择逻辑完成前仍必须明确拒绝，不允许静默猜测。

### PN 50661 实机验证记录（2026-08-13）

- 修复发现阶段加载大型 `CalibrationProfile` 临时对象导致的 `loopTask` 栈保护 PANIC；Profile 现直接在常驻存储中解析。
- 修复固件烧录后连续执行 3 次 MASTER 发现流程，均成功且未再次重启。
- PN 50661 斜率校准实机执行成功。
- 校准完成后自动返回 MASTER 界面并重新发现探头，符合既定退出与恢复流程。

### 4–20 mA 输出量程扩展

- 校准入口命名为 `CAL & OUTPUT`，同一页面区分 `[SENSOR]` 与 `[OUTPUT]` 操作。
- 顶层 `Span4_20mA` 转换为 `category: output_span` 流程，复用现有数字键盘、确认页和非阻塞 Modbus 执行器。
- PN 50661 的 4 mA 映射固定为 0 ppb 且无命令，不提供可操作项；20 mA 映射允许输入 1～300 ppb。
- PN 50661 输出设置依次写 1016=输入值、等待 1000 ms、写 1044=2、等待 1000 ms；原 JSON 无结果寄存器，写响应与等待均完成后显示 `Output range saved`。
- 退出后沿用返回 MASTER 并重新发现探头的安全恢复流程。
- 2026-08-13 实机验证：PN 50661 斜率校准与新增 4–20 mA 输出量程设置均成功。
