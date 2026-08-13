# 校准 Profile 规范化 Schema v1

## 1. 用途

本 Schema 是手机 APP 原始设备 JSON 与固件校准执行器之间的稳定接口，由以下组件共同使用：

- `CalibrationProfileManager`：批量导入、编辑、冲突处理和人工审核；
- `generate_calibration_profiles.py`：构建期转换、严格校验和 LittleFS 文件生成；
- `CalibrationProfileStore`：发现传感器后按 PN/变体加载一个活动 Profile；
- `CalibrationExecutor`：按照规范化步骤执行 FC06、FC16 和结果读取；
- Arduino_GFX UI：只读取流程、输入和结果信息，不解析手机 APP 原始结构。

原始设备库保持只读。转换器输出规范化文件，不覆盖原始 APP JSON。

## 2. 文件组织

```text
config/calibration_import_rules.json       # 排除、首选变体和人工审核规则
config/calibration_profiles/*.json         # 上位机导出的规范化源文件
data/calibration/index.json                 # LittleFS 小型索引
data/calibration/<profile-id>.json          # 每个活动 Profile 一份精简文件
src/calibration_index_generated.inc         # 首批回退索引
```

索引只包含匹配信息和文件路径。固件发现 PN 后只解析一个精简文件，禁止一次性把全库载入 ArduinoJson 或固定 RAM 数组。

## 3. 地址与数据约定

- 所有 JSON `address` 均使用设备资料中的 1-based 地址。
- 组装 Modbus PDU 时统一且只执行一次 `address - 1`。
- `uint16` 使用 FC06，单寄存器，大端寄存器值。
- `float32_cdab` 使用 FC16，两个寄存器，低字寄存器在前，即 CDAB。
- 第一版结果寄存器仅支持 FC03 读取 `uint16`。
- 所有时间均为无符号毫秒。
- 所有显示文本均为 UTF-8，固件保存到定长缓冲区并校验编码后字节数。

## 4. 索引 Schema

示例：

```json
{
  "schema_version": 1,
  "config_version": "2026.08.13.1",
  "profiles": [
    {
      "profile_id": "57769.default",
      "product_num": 57769,
      "variant": "default",
      "priority": 0,
      "file": "/calibration/57769.default.json",
      "match": null
    },
    {
      "profile_id": "56377.new",
      "product_num": 56377,
      "variant": "new",
      "priority": 100,
      "file": "/calibration/56377.new.json",
      "match": null
    }
  ]
}
```

字段规则：

- `schema_version`：必须为 1；
- `config_version`：非空版本字符串；
- `profile_id`：ASCII 稳定标识，只允许字母、数字、点、下划线和短横线；
- `product_num`：1～UInt32 最大值；
- `variant`：人工可读的变体名；
- `priority`：同 PN 多候选时使用，数值越大优先级越高；
- `file`：LittleFS 绝对路径；
- `match`：可选设备版本条件。

同一 PN 如果有多个无条件候选且最高优先级相同，索引校验失败。PN 56377 的导入规则固定只输出 `56377_New.json` 对应的 `56377.new`，不在设备端保留二义性。

## 5. Profile Schema

### 5.1 完整示例

```json
{
  "schema_version": 1,
  "profile_id": "57769.default",
  "product_num": 57769,
  "variant": "default",
  "model_name": "HW-201T",
  "source_file": "57769.json",
  "requires_version_read": false,
  "release_status": "approved",
  "procedures": [
    {
      "id": "sugar.zero",
      "reading_name": "Sugar",
      "name": "zero_title",
      "message": "zero_message",
      "unit": "ppm",
      "when": null,
      "inputs": [],
      "steps": [
        {
          "id": "trigger_zero",
          "alternatives": [
            {
              "when": null,
              "write": {
                "address": 43006,
                "type": "uint16",
                "value": { "kind": "fixed", "number": 21 }
              },
              "wait_after_ms": 1000,
              "result": null
            }
          ]
        }
      ]
    },
    {
      "id": "sugar.slope",
      "reading_name": "Sugar",
      "name": "slope_title",
      "message": "slope_message",
      "unit": "ppm",
      "when": null,
      "inputs": [
        {
          "id": "standard",
          "label": "Sugar standard",
          "type": "float32",
          "default": 100,
          "minimum": 1,
          "maximum": 1000,
          "minimum_inclusive": true,
          "maximum_inclusive": true,
          "decimals": 2,
          "unit": "ppm",
          "write_scale": { "numerator": 1, "denominator": 1 }
        }
      ],
      "steps": [
        {
          "id": "write_standard",
          "alternatives": [
            {
              "when": null,
              "write": {
                "address": 44002,
                "type": "float32_cdab",
                "value": { "kind": "input", "input_id": "standard" }
              },
              "wait_after_ms": 100,
              "result": null
            }
          ]
        },
        {
          "id": "trigger_slope",
          "alternatives": [
            {
              "when": null,
              "write": {
                "address": 43006,
                "type": "uint16",
                "value": { "kind": "fixed", "number": 22 }
              },
              "wait_after_ms": 1000,
              "result": null
            }
          ]
        }
      ]
    }
  ]
}
```

### 5.2 Profile 字段

- `schema_version`：必须为 1；
- `profile_id`：必须与索引一致；
- `product_num`：必须与索引一致；
- `variant`：变体名；
- `model_name`：显示名称；
- `source_file`：用于报告和追溯，不作为运行时匹配依据；
- `requires_version_read`：任一流程或步骤包含版本条件时为 true；
- `release_status`：`approved`、`draft` 或 `blocked`；只有 approved 可进入固件发布包；
- `procedures`：1～16 个校准流程。

## 6. 校准流程与输入

### 6.1 Procedure

每个 `procedure` 表示用户可以选择的一项完整校准：

- `id`：Profile 内唯一的稳定 ID；
- `reading_name`：来源 Reading 名称；
- `name`：APP 校准标题；
- `message`：操作提示；
- `unit`：默认显示单位；
- `when`：合并 Reading 与 Calibration 层的可选版本条件；条件为 false 时 UI 不显示该流程；
- `inputs`：0～3 个用户输入；
- `steps`：1～16 个有序执行步骤。

同名校准项目不能只靠 `name` 区分，ID 应包含 Reading 和序号或稳定 slug。

### 6.2 Input

```json
{
  "id": "orp_standard",
  "label": "ORP standard",
  "type": "float32",
  "default": 229,
  "minimum": -1500,
  "maximum": 1500,
  "minimum_inclusive": true,
  "maximum_inclusive": true,
  "decimals": 2,
  "unit": "mV",
  "write_scale": { "numerator": 1, "denominator": 1000 }
}
```

规则：

- `type` 仅允许 `uint16` 或 `float32`；
- `uint16` 的范围必须位于 0～65535，`decimals` 必须为 0；
- `float32` 允许负数和小数；
- `minimum <= default <= maximum`；
- `minimum_inclusive`、`maximum_inclusive` 表示边界能否取等号；
- 原始字符串边界 `>5`、`>=0.0`、`<10`、`<=0.2` 必须转换为数值加 inclusive 标志，固件不解析表达式；
- `decimals` 控制 UI 输入和显示，不改变 IEEE-754 编码；
- `write_scale` 将用户显示值转换为设备写入值；
- 原始 `Conversion: "value/100"` 变为 1/100；
- 原始 `Conversion: "value/1000"` 变为 1/1000；
- 没有 Conversion 时为 1/1；
- `DynamicUnit: "Unit"` 归一为 Reading 的 `Unit`，不在固件中保留表达式。

UI 依次收集所有 inputs，确认页同时显示全部输入。命令只通过 `input_id` 引用，UI 不需要知道寄存器地址。

## 7. 步骤与版本分支

### 7.1 Step

每个 Step 可以有可选 `when`，并且至少有一个 `alternative`。命令级 `VersionJudgment` 转为 Step 的 `when`；条件为 false 时跳过整个步骤。无地址/实现差异时只有一个 `when: null` 的 alternative；同一步骤存在地址或结果地址版本差异时，多个 alternative 互斥。

运行时规则：

1. 按步骤顺序执行；
2. 先计算 Step 的 `when`，false 时安全跳过该步骤；
3. Step 适用时计算所有 alternative 的 `when`；
4. 必须且只能匹配一个 alternative；
5. 匹配 0 个或多于 1 个均停止，并报告 Profile 条件错误；
6. 不允许通过数组顺序偷偷充当优先级。

### 7.2 结构化条件

不在固件中解析 `"softVersion>=64 && softVersion<103"` 这类字符串。上位机将其转换成结构化条件：

```json
{
  "all": [
    { "field": "software_version", "operator": "gte", "value": 64 },
    { "field": "software_version", "operator": "lt", "value": 103 }
  ]
}
```

支持字段：

- `hardware_version`；
- `software_version`；
- 后续需要时增加 `parameter_version`。

支持操作符：

- `eq`、`lt`、`lte`、`gt`、`gte`。

条件对象支持：

- `{ "all": [...] }`：所有谓词成立；
- `{ "not": <condition> }`：条件取反，用于表达版本覆盖项的默认分支。

例如原始地址默认 44004、硬件版本大于等于 20 时改为 44002，会生成互斥 alternatives：默认项 `not(hardware_version >= 20)`，覆盖项 `hardware_version >= 20`。

第一版转换器只接受全库已经观察到的简单数值比较和 `&&`。出现其他表达式时标记为不支持并要求人工处理，禁止执行任意表达式或脚本。

### 7.3 版本数据来源

当前固件完整身份发现只覆盖 41001～41014，尚未读取 APP JSON 中常见的：

- Hardware Version：41023，`ushort`，长度 8；
- Software Version：41031，`ushort`，长度 8；

全库在 Reading、Calibration、命令和地址覆盖等多个层级使用版本条件。第一版首批中的 53607 也包含 Calibration 级软件版本条件，因此分两步实施：

1. Schema、转换器和上位机完整保留结构化条件，但将条件 Profile 标记为 `requires_version_read`；
2. 固件增加版本读取与数值规范化后，才允许这些 Profile 进入可执行状态。

版本读取失败时不能选择默认命令分支，应显示“Device version unavailable”。

## 8. 写命令

### 8.1 固定值

```json
"write": {
  "address": 43006,
  "type": "uint16",
  "value": { "kind": "fixed", "number": 22 }
}
```

### 8.2 用户输入值

```json
"write": {
  "address": 44002,
  "type": "float32_cdab",
  "value": { "kind": "input", "input_id": "standard" }
}
```

规则：

- `address` 为 1～65536；
- `uint16` 地址占 1 个寄存器；
- `float32_cdab` 地址及下一地址必须有效；
- 固定 UInt16 值必须为整数；
- 输入引用必须指向当前 Procedure 中存在的 Input；
- 写入前再次检查输入范围和缩放后数值是否有限；
- `wait_after_ms` 缺省时规范化为 0，同时生成导入警告；
- 当前库缺少 Waiting 的 5 条命令需在上位机人工审核后才能发布。

## 9. 结果模式

### 9.1 WaitOnly

`result: null` 表示该步骤写响应成功并完成等待后继续下一步骤。最后一步也是 `result: null` 时，整个 Procedure 结果为 `command_completed`，UI 不宣称设备内部算法已经确认成功。

### 9.2 MappedResult

```json
"result": {
  "address": 41003,
  "type": "uint16",
  "register_count": 1,
  "poll_interval_ms": 500,
  "timeout_ms": 120000,
  "communication_error_message": "Communication failure, please retry.",
  "mappings": [
    { "value": 1, "outcome": "success", "message": "Calibration succeed." },
    { "value": 0, "outcome": "pending", "message": "Calibration Idle" },
    { "value": 3, "outcome": "failure", "message": "Invalid calibration solution value." }
  ]
}
```

允许 outcome：

- `success`：终态，Procedure 成功；
- `failure`：终态，设备明确失败；
- `pending`：非终态，继续轮询；
- `info`：非终态，仅展示；如果没有后续映射变化，最终由超时结束。

全库 736 个结果命令均恰好有一个包含 succeed/success/complete 的成功消息。导入工具可以据此生成 success 建议；“in process”“in progress”和“idle”建议为 pending；其余建议为 failure。但是：

- 自动分类必须在上位机中显示审核状态；
- 未审核的结果 Profile 不允许发布到固件包；
- 固件只读取明确的 `outcome`，不分析英文文本；
- 每个 MappedResult 必须恰好有一个或多个明确 success，至少一个终态；
- `value` 在同一映射表中必须唯一；
- 未匹配值连续出现时显示原始值，达到策略阈值后结束为 `unknown_device_result`；
- FC03 通信失败和设备 failure 不能合并。

原始 `Results[]` 中 `RegValue: null` 表示通信失败时使用的兜底文案，不是可匹配的寄存器值。转换器将其移出 mappings，保存为可选 `communication_error_message`。同一个结果定义最多允许一个 null 项。

默认轮询间隔为 500 ms，总超时为 120 s。上位机允许按设备调整。库中存在“过程约 90 秒”的提示，因此总超时不能只取原始 `Waiting_mS`。

### 9.3 ResultRegLen=3 特例

`50315.json` 和 `50319.json` 的 8 条结果定义同时声明：

- `ResultRegType: "ushort"`；
- `ResultRegLen: 3`；
- Results 值却是 0、1、2 的单值映射；
- 结果地址与写入 Float 地址相同。

该含义无法从 JSON 自证。第一版将这两个 Profile 标记为 `unsupported_result_layout`，不生成可执行校准配置，直到协议得到确认。不得擅自读取三个寄存器后选择其中某一个。

## 10. 运行时容量

活动 Profile 固定容量建议：

- 12 个 Reading 来源分组；
- 16 个 Procedure；
- 每个 Procedure 3 个 Input；
- 每个 Procedure 16 个 Step；
- 每个 Step 4 个 Alternative；
- 每个 Profile 32 个实际写命令；
- 每个 Result 8 个 Mapping；
- 单次只加载一个 Profile。

生成器必须输出精确的容量使用报告。超限 Profile 不做静默截断，上位机应要求拆分、提高经测量后的容量或排除。

## 11. 导入规则

项目级规则文件第一版：

```json
{
  "schema_version": 1,
  "source_directory": "D:/pyxis/software/github/espTry001/devices/devices",
  "exclude_files": [
    { "file": "50316.json", "reason": "Filename PN differs from JSON PN; excluded by project decision" }
  ],
  "exclude_pns": [
    { "product_num": 56600, "reason": "No calibration JSON; excluded by project decision" }
  ],
  "preferred_files": [
    { "product_num": 56377, "file": "56377_New.json", "reason": "Use new version by project decision" }
  ]
}
```

转换器不得删除或修改源目录文件。每次输出报告至少列出：导入、排除、替代、冲突、警告、条件 Profile、未审核结果和不支持布局。

## 12. 校验严重级别

- `error`：不能生成，例如 JSON 类型错误、地址越界、无匹配命令分支；
- `unsupported`：原始结构有效但固件 v1 不支持，例如未确认的 ResultRegLen=3；
- `review_required`：可以转换但必须人工确认，例如结果 outcome 自动分类；
- `warning`：使用安全默认值，例如缺少 Waiting_mS；
- `info`：正常转换说明，例如 DynamicUnit 已继承 Reading Unit。

只有没有 error/unsupported/review_required 的 Profile 才能进入发布包。

## 13. Schema v1 完成条件

- 57769 可无损表达为 WaitOnly；
- 50661 可表达结果读取和映射；
- 53607 可表达多 Reading、多流程、多个输入和分页；
- `value/100`、`value/1000` 和 DynamicUnit 可结构化转换；
- 现有版本判断字符串可转换为结构化条件；
- 结果 pending 能驱动非阻塞轮询；
- 重复 PN、首选文件和排除规则可重复执行；
- 超限与不支持结构不会被静默忽略；
- 固件不解析原始 APP 表达式、不分析英文结果文本，也不一次加载全库。
