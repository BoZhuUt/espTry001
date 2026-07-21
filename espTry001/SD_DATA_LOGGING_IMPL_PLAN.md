# SD 数据记录详细实施计划（阶段 A + B）

基于 `SD_DATA_AND_SENSOR_FIRMWARE_PLAN.md` 的总体决策，本文细化阶段 A 和 B 的代码结构、接口设计、数据流和集成步骤。

> 创建：2026-07-21
> 状态：阶段 A/B 首版已实现并通过真实 SD 卡与传感器测试；长稳与异常测试待完成

## 当前实现与验证结果（2026-07-21）

本节记录已经落地的实现；若后面的早期接口草案与本节不一致，以源代码和本节为准。

### 已完成

- 新增 `SdStorage`，使用 SD_MMC 4-bit：CLK=16、CMD=15、D0=17、D1=18、D2=13、D3=14。
- 启动时挂载 SD，创建 `/logs`，并在 System Info 显示 `Ready/Missing/Error` 和剩余容量。
- 新增 `DataLogger`，使用 4096 字节固定环形缓冲和 512 字节单行缓冲，不使用动态增长的 `String`。
- CSV 每 5 秒或缓冲超过一半时批量写入；切换模式和进入 `CONFIG UPLOAD` 时立即 flush 并关闭文件。
- CSV 行构造带严格边界检查；单行过长或写入失败时停止记录，不覆盖旧数据，也不产生拼接的半行。
- Session ID 保存在 NVS，每次开始记录递增；文件名为 `/logs/PN<PN>_S<session>.csv`，32 MB 后自动分卷。
- 文件头记录 `config_version`、`session_id`、Git 固件版本及 `time_source=uptime_ms`。
- `SensorProfilePoller` 已提供 `scanSequence()` 与 `takeScanCompleted()`；仅在一整轮 JSON Profile 参数读取完成后生成一条日志。
- MASTER 发现地址、读取 PN 并匹配 JSON Profile 后自动开始记录；日志名称来自 Profile，字段顺序及小数位来自 JSON。
- 切换 MASTER/PASS、重新建立发现流程或进入配置上传前调用 `DataLogger::stop()`，避免跨传感器混写。
- SD 不可用时 `DataLogger::start()` 安全返回，不影响 PASS、MASTER、触摸、显示和 JSON 配置功能。

### 实机验证

- 模拟模式使用 PN 53607 的 16 个字段，每秒写一行，连续 17 行的 `sample_seq` 无缺失，`uptime_ms` 每行增加 1000 ms。
- 模拟参数相邻值增加 0.1，每行 16 个字段完整，无截断、错列或半行。
- 创建了连续递增的 `S000001`、`S000002` 会话文件，证明 NVS Session 管理有效。
- 进入 `CONFIG UPLOAD` 后最后一条缓存记录仍保存在 CSV，证明停止时强制 flush/close 生效。
- 已关闭模拟开关（`SD_SIMULATE_LOG=0`），使用真实传感器完成 MASTER 自动记录测试，用户确认工作正常。
- PlatformIO release 构建通过：RAM 14.9%，Flash 14.7%。

### 尚待验收

- 连续写入至少 10 MB 以及 2/24 小时长稳测试。
- 未插卡启动、写满、运行中意外拔卡的完整异常测试。
- SENSOR INFO/Dashboard 的 `REC` 标记和安全拔卡专用页面。
- USB 日志导出、USB 校时/RTC，以及后续传感器 YMODEM 固件升级。

---

## 0. 前置约束（来自规划文档，不再改动）

- SD_MMC 4-bit：CLK=16, CMD=15, D0=17, D1=18, D2=13, D3=14
- 无 Card Detect，不支持运行中热插拔
- 无 RTC，第一版使用 `session_id + uptime_ms` 作为时间
- LittleFS 继续存储 `/sensor_profiles.json`，SD 卡只存日志
- 所有 SD 操作必须非阻塞，不允许长时间卡住 `loop()`
- SD 卡未插入/损坏时，PASS、MASTER、触摸、显示和 JSON Profile 仍能正常工作

---

## 1. 新增文件

```text
src/sd_storage.h        SdStorage 类声明
src/sd_storage.cpp      SdStorage 类实现
src/data_logger.h       DataLogger 类声明
src/data_logger.cpp     DataLogger 类实现
```

不新增全局变量文件，模块自包含。

---

## 2. SdStorage 类设计（阶段 A）

### 2.1 职责

- SD_MMC 初始化与挂载
- 卡状态查询（容量、剩余空间）
- 目录创建
- 文件打开/写入/关闭/flush
- 错误信息查询

SdStorage 不关心业务逻辑，只提供可靠的文件操作抽象。

### 2.2 类接口

```cpp
#pragma once
#include <Arduino.h>
#include <SD_MMC.h>

class SdStorage {
public:
  enum class State : uint8_t {
    NotInitialized,  // 未初始化或初始化失败
    Ready,           // 正常可用
    Error            // 运行中出现 I/O 错误
  };

  // 初始化 SD_MMC 4-bit。只应在 setup() 中调用一次。
  // 成功时 state() 变为 Ready，失败时保持 NotInitialized。
  bool begin();

  // 重新尝试初始化。可用于启动时无卡、后续插入后的手动重试。
  bool retry();

  State state() const { return state_; }
  const char *stateText() const;
  const char *lastError() const { return lastError_; }

  // 卡信息
  uint64_t totalBytes() const;
  uint64_t usedBytes() const;
  uint64_t freeBytes() const;
  const char *cardType() const;

  // 目录操作
  bool ensureDir(const char *path);

  // 文件操作（薄封装，调用方负责打开/关闭生命周期）
  File open(const char *path, const char *mode);
  bool exists(const char *path);

  // 标记运行时错误（SD 写入失败或拔卡后调用）
  void setError(const char *message);

  // 清除错误状态，前提是 SD 已确认可用
  void clearError();

private:
  static constexpr char kStorageLabel[] = "/sdcard";
  State state_ = State::NotInitialized;
  char lastError_[64] = {0};
  bool initialized_ = false;
};
```

### 2.3 初始化流程

```text
setup()
  |
  v
SdStorage::begin()
  |
  |-- SD_MMC.setPins(16, 15, 17, 18, 13, 14)
  |-- SD_MMC.begin("/sdcard", false, false)   // mode1bit=false → 4-bit
  |
  |-- 成功 → state_ = Ready
  |   |-- 串口打印卡类型和容量
  |   |-- 创建 /logs 目录（如不存在）
  |
  |-- 失败 → state_ = NotInitialized
      |-- 记录 lastError_
      |-- 不影响其他功能运行
```

### 2.4 关键实现要点

1. **初始化只调用一次**：`begin()` 在 `setup()` 中调用，不在 `loop()` 中反复重试
2. **运行中拔卡检测**：通过后续 `File::write()` / `File::flush()` 返回值判断，不依赖 Card Detect
3. **错误传播**：`DataLogger` 调用写入失败后调用 `setError()`，SdStorage 进入 Error 状态
4. **4-bit 不稳定降级**：第一版冒烟测试中若 4-bit 频繁出错，临时降为 1-bit 调试，不通过重试掩盖硬件问题
5. **内存占用**：SdStorage 本身不含大缓冲区，仅 ~80 字节状态

### 2.5 冒烟测试验收项

| 项目 | 通过标准 |
|---|---|
| 识别卡类型和容量 | 串口输出 `SD: SDHC, 7.4 GB` 类似信息 |
| 创建并读写诊断文件 | `/diagnostic.txt` 写入、关闭、重新打开、比对内容一致 |
| 连续写入 10 MB | 无随机写入失败，写入期间触摸和 RS485 不卡顿 |
| 未插卡启动 | PASS、MASTER、触摸、显示和 JSON Profile 正常 |
| 运行中意外拔卡 | 触摸和 RS485 仍正常，屏幕提示 SD Error |
| System Info 显示 | `SD: Ready/Missing/Error`、容量信息 |

---

## 3. DataLogger 类设计（阶段 B）

### 3.1 职责

- 取得完整扫描快照（整轮 Profile 参数 + 两路 4-20mA）
- 格式化为 CSV 行并缓存
- 定时批量写入 SD 卡
- 文件创建、分卷和关闭
- 写入状态查询（供 UI 显示）

### 3.2 类接口

```cpp
#pragma once
#include <Arduino.h>
#include "sensor_profile.h"
#include "sensor_discovery.h"

class SdStorage;  // 前向声明

class DataLogger {
public:
  // 最大单行 CSV 长度估算：16 字段 × 16 字符 + 固定头 ≈ 400 字节
  static constexpr size_t kLineBufferSize = 512;
  // 环形缓冲大小：约 8 条记录
  static constexpr size_t kRingBufferSize = 4096;
  // 刷新间隔：累计约 4 KB 或 1 秒
  static constexpr uint32_t kFlushIntervalMs = 1000;
  // 每 5~10 秒 flush 一次
  static constexpr uint32_t kFullFlushIntervalMs = 5000;

  explicit DataLogger(SdStorage &sd);

  // 设置当前会话的识别信息。每次 MASTER 发现成功后调用。
  void setIdentity(const SensorDiscovery::Result &identity,
                   const SensorProfile *profile);

  // 每轮参数轮询完成后调用。传入当前 values/valid 数组和 4-20mA 值。
  // 只在扫描序号变化时创建一条记录。
  void onScanComplete(const float *values, const bool *valid,
                      uint8_t fieldCount,
                      float current1Ma, float current2Ma);

  // 非阻塞推进函数，必须在 loop() 中反复调用。
  // 负责缓存写入和定时 flush。
  void update();

  // 手动停止记录，flush 并关闭文件。切换 PASS 模式前必须调用。
  void stop();

  // 恢复记录（重新发现传感器后继续写同一文件）
  void resume();

  // 状态查询
  bool isRecording() const { return recording_; }
  uint32_t recordCount() const { return recordCount_; }
  uint32_t sessionId() const { return sessionId_; }
  const char *currentFileName() const { return fileName_; }
  const char *lastError() const { return lastError_; }
  bool sdReady() const;

private:
  // NVS 键名，保存递增的 session_id
  static constexpr const char *kNvsNamespace = "datalog";
  static constexpr const char *kNvsSessionKey = "session_id";

  // 文件分卷大小（字节）
  static constexpr uint64_t kMaxFileSize = 32 * 1024 * 1024;  // 32 MB

  SdStorage &sd_;

  // 会话信息
  uint32_t sessionId_ = 0;
  uint32_t scanSequence_ = 0;
  uint32_t recordCount_ = 0;

  // 当前传感器信息
  uint8_t slaveAddress_ = 0;
  uint32_t productNum_ = 0;
  char profileName_[SensorProfileStore::kMaxProfileNameLength] = {0};
  const SensorProfile *profile_ = nullptr;
  uint8_t fieldCount_ = 0;

  // 上一轮已记录的扫描序号，用于检测是否产生新的完整扫描
  uint32_t lastLoggedSequence_ = 0;

  // 文件
  char fileName_[64] = {0};
  File file_;
  bool fileOpen_ = false;
  bool recording_ = false;
  bool headerWritten_ = false;
  uint32_t fileIndex_ = 0;  // 分卷编号

  // 环形缓冲区
  char ringBuffer_[kRingBufferSize] = {0};
  size_t ringHead_ = 0;  // 写入位置
  size_t ringTail_ = 0;  // 读取位置
  size_t ringUsed_ = 0;  // 已用字节数

  // 定时
  uint32_t lastFlushMs_ = 0;
  uint32_t lastFullFlushMs_ = 0;

  // 错误
  char lastError_[64] = {0};

  // 内部方法
  void loadSessionId();
  void incrementSessionId();
  bool createFile();
  bool writeHeader();
  bool formatCsvLine(const float *values, const bool *valid,
                     float current1Ma, float current2Ma);
  void ringWrite(const char *data, size_t length);
  size_t ringRead(char *data, size_t maxLength);
  bool flushToFile();
  void closeFile();
  void setLastError(const char *msg);
};
```

### 3.3 数据流

```text
SensorProfilePoller 一轮完成
        |
        v
main.cpp 检测到 scanSequence 变化
        |
        v
DataLogger::onScanComplete(values, valid, fieldCount, current1Ma, current2Ma)
        |
        |-- 检查 lastLoggedSequence_ 是否变化
        |-- 变化 → formatCsvLine() → ringWrite()
        |-- 未变化 → 跳过（避免重复记录）
        |
        v
DataLogger::update()    [loop() 中每帧调用]
        |
        |-- ringUsed_ >= kRingBufferSize / 2  →  flushToFile()
        |-- 距上次 flush >= kFullFlushIntervalMs  →  flushToFile()
        |-- 检查文件大小是否需要分卷
        |
        v
flushToFile()
        |
        |-- ringRead() 取出数据
        |-- File::write()
        |-- 写入失败 → sd_.setError()、停止记录
```

### 3.4 CSV 格式详细设计

#### 文件名

未校时格式：
```text
/logs/PN53607_S000012.csv
```

校时后格式（未来扩展）：
```text
/logs/PN53607_20260720_151001.csv
```

#### 文件头

```csv
# config_version=2026.07.20.1
# session_id=12
# firmware=V1.0.45
# time_source=uptime_ms
session_id,sample_seq,uptime_ms,address,pn,CH1_mA,CH2_mA,sensorValue,s365LowRaw,s365HighRaw,s470Raw,t365Raw,s365LowZero_DI,s365HighZero_DI,t365Zero_DI
```

- 前四行以 `#` 开头为注释行
- `session_id`：NVS 单调递增，设备生命周期内不重复
- `sample_seq`：本次会话内的采样序号，从 1 开始
- `uptime_ms`：`millis()` 值
- `address`：从站真实地址
- `pn`：产品编号
- `CH1_mA` / `CH2_mA`：两路 4-20mA 采样值
- 后续字段与 JSON Profile 中 `fields` 顺序一致

#### 数据行

```csv
12,1,54321,15,53607,4.1,12.3,8.620,1234.56,2345.67,3456.78,25.20,1000,2000,3000
```

规则：
- 无效字段留空（两个连续逗号 `,,`）
- `float32` 按 Profile 的 `decimals` 输出小数位
- `uint16` 不输出小数
- PN 变化、Profile 字段结构变化时必须新建文件

### 3.5 Session ID 管理

- 使用 NVS 存储 `session_id`，键名 `datalog/session_id`
- 每次创建新日志文件时递增
- NVS 读写只在创建文件时发生，不阻塞 `loop()`
- NVS 空间极小（一个 uint32），不影响 LittleFS 配置分区

### 3.6 写入策略

| 场景 | 行为 |
|---|---|
| 缓冲 >= 2 KB | 立即 flush 到文件 |
| 距上次 flush >= 5 秒 | 执行 flush |
| `stop()` 被调用 | 立即 flush 并关闭文件 |
| 切回 PASS 模式 | 调用 `stop()` |
| 写入失败 | 停止记录、标记 Error、不自动重启 |
| 文件 >= 32 MB | 关闭当前文件，创建分卷文件 `_v2.csv` |

### 3.7 与 SensorProfilePoller 的集成

当前 `SensorProfilePoller` 在一轮完成后将 `blockIndex_` 重置为 0 并设置 `nextRequestMs_`，但没有暴露"一轮完成"的事件。需要增加：

```cpp
// sensor_profile.h 中 SensorProfilePoller 新增：

// 返回当前扫描序号。每完成一轮递增。
uint32_t scanSequence() const { return scanSequence_; }

// 尝试取走"一轮完成"标志。如果新一轮刚完成返回 true，
// 调用方应立即读取 values/valid 数组。
bool takeScanCompleted();
```

实现方式：
- `scanSequence_` 在 `finishCurrentBlock()` 中 `blockIndex_` 回绕时递增
- `takeScanCompleted()` 比较调用方保存的旧序号与当前序号

`main.cpp` 的 `loop()` 中：

```cpp
if (activeProfile != nullptr && profilePoller.update(modbusResult)) {
  // 原有的 UI 刷新逻辑...
  
  // 新增：检测完整扫描完成
  if (profilePoller.takeScanCompleted() && dataLogger.isRecording()) {
    dataLogger.onScanComplete(
        profileValues, profileValueValid, activeProfile->fieldCount,
        current1Ma, current2Ma);
  }
}
```

### 3.8 与 main.cpp 的集成点

| main.cpp 位置 | 新增调用 |
|---|---|
| 全局变量 | `SdStorage sdStorage;` `DataLogger dataLogger(sdStorage);` |
| `setup()` | `sdStorage.begin();`（在 `profileStore.begin()` 之后） |
| MASTER 发现成功 | `dataLogger.setIdentity(...)` 并 `dataLogger.resume()` |
| `loop()` 轮询完成 | `dataLogger.onScanComplete(...)` |
| `loop()` 主循环 | `dataLogger.update()` |
| 切回 PASS | `dataLogger.stop()` |
| System Info 页 | 显示 SD 状态和记录信息 |
| SensorInfo 页 | 显示 `REC` 标记和记录条数 |

---

## 4. 分步实施顺序

### 步骤 1：SdStorage 基础（阶段 A 前半段）

1. 新建 `sd_storage.h/.cpp`
2. 实现 `begin()`、状态查询、卡信息
3. 在 `setup()` 中调用 `sdStorage.begin()`
4. System Info 页增加 SD 状态一行
5. 串口输出 SD 初始化结果
6. 编译验证，烧录，检查无卡启动是否正常

### 步骤 2：SdStorage 文件操作验证（阶段 A 后半段）

1. 实现 `ensureDir()`、`open()`、`exists()`
2. `setup()` 中创建诊断文件 `/diagnostic.txt`
3. 编译烧录，验证 SD 卡上文件内容
4. 拔卡测试、意外拔卡后设备行为验证

### 步骤 3：DataLogger 基础框架（阶段 B 前半段）

1. 新建 `data_logger.h/.cpp`
2. 实现 session ID 管理（NVS）
3. 实现文件创建和 CSV 头写入
4. 实现 `setIdentity()`
5. 在 MASTER 发现成功后调用，验证文件创建

### 步骤 4：DataLogger 数据记录（阶段 B 后半段）

1. 增加 `SensorProfilePoller::scanSequence()` 和 `takeScanCompleted()`
2. 实现 CSV 行格式化和环形缓冲
3. 实现 `onScanComplete()` 和 `update()`
4. 集成到 `main.cpp` 的 `loop()`
5. 实现 `stop()` 和切回 PASS 的清理

### 步骤 5：UI 增强

1. System Info：SD 状态、容量、日志文件、记录条数
2. SensorInfo：`REC` 标记
3. Dashboard：MASTER 模式下简要显示记录状态

### 步骤 6：异常处理和边界测试

1. SD 写入失败时停止记录并提示
2. 拔卡后设备不崩溃
3. 重新上电后 session_id 递增
4. 不同 PN 切换时新建文件
5. 长时间运行无内存泄漏（环形缓冲边界）

---

## 5. RAM 与 Flash 预算

| 项目 | RAM 占用 |
|---|---|
| SdStorage 状态 | ~80 B |
| DataLogger 环形缓冲 | 4096 B |
| DataLogger 其他成员 | ~300 B |
| SD_MMC 驱动内部缓冲 | ~4096 B（由 IDF 管理） |
| **合计新增** | **~8.5 KB** |

Flash 增加约 8~12 KB（SD_MMC 库 + 新代码）。

ESP32-S3 DevKitC-1 有 512 KB SRAM 和 8 MB Flash，余量充足。

---

## 6. platformio.ini 变更

无需新增 `lib_deps`。Arduino-ESP32 自带 `SD_MMC` 库和 `NVS`。

可能需要确认 `board_build.partitions` 是否需要调整。当前使用默认分区表，LittleFS 和应用固件空间应足够。

---

## 7. 不在阶段 A+B 范围内的事项

以下明确不在本次实施范围内：

- USB LOG EXPORT（阶段 C）
- 固件包管理（阶段 D）
- YMODEM 发送器（阶段 E）
- 传感器固件升级（阶段 F）
- 运行中拔卡/插卡自动检测
- USB 校时或 RTC 时间
- 自动删除旧日志
- 文件浏览器或日志选择 UI
- SD 卡文件系统修复工具
