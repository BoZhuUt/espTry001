#pragma once

#include <Arduino.h>
#include <ModbusMaster.h>

enum class SensorValueType : uint8_t {
  UInt16,
  // 两个 16 位寄存器的低字在前，即寄存器字顺序 CDAB。
  Float32CDAB
};

// 一次 FC03 连续读取任务。地址采用设备资料中的 1-based 写法。
struct SensorPollBlock {
  uint32_t address1Based;
  uint16_t registerCount;
};

// 一个需要显示的参数，以及它在某个读取块中的位置和解析方式。
struct SensorField {
  const char *label;
  uint8_t blockIndex;
  uint8_t registerOffset;
  SensorValueType type;
  uint8_t decimals;
};

// 某个 PN 的完整显示配置。
struct SensorProfile {
  uint32_t productNum;
  const char *name;
  const SensorPollBlock *blocks;
  uint8_t blockCount;
  const SensorField *fields;
  uint8_t fieldCount;
};

// 每页显示 4 个字段，最多 4 页。
constexpr uint8_t kMaxSensorFields = 16;

// 根据 PN 查找编译进固件的回退配置；不支持时返回 nullptr。
const SensorProfile *findSensorProfile(uint32_t productNum);
// 返回生成内置表时读取到的 JSON config_version。
const char *builtInProfileConfigVersion();

// 参数轮询状态机。它直接操作 HardwareSerial，不使用 ModbusMaster 的
// 同步等待接口，因此设备不响应时也不会卡住触摸和界面。
class SensorProfilePoller {
public:
  explicit SensorProfilePoller(HardwareSerial &serial);

  // 绑定已经发现的从站和 PN 配置，并从第一个读取块开始轮询。
  // values/valid 数组由调用方持有，长度至少为 kMaxSensorFields。
  void begin(uint8_t slaveAddress, const SensorProfile &profile,
             float *values, bool *valid);
  // 停止轮询。切回 PASS 模式前必须调用。
  void cancel();

  // 非阻塞推进函数，必须在 loop() 中反复调用。
  // 每完成一个读取块（成功、异常或超时）返回 true，并更新 lastError。
  bool update(uint8_t &lastError);

private:
  // 16 个连续 Float 最多返回 32 个寄存器，即 69 字节 RTU 响应。
  static constexpr size_t kResponseBufferSize = 128;
  static constexpr uint32_t kResponseTimeoutMs = 500;
  static constexpr uint32_t kInterBlockDelayMs = 20;
  static constexpr uint32_t kScanIntervalMs = 1000;

  void sendCurrentBlock();                        // 发送当前块的 FC03 请求
  void finishCurrentBlock(uint8_t error);         // 标记有效性并安排下一块
  void decodeCurrentBlock();                      // 按字段类型解析当前响应
  uint16_t responseRegister(uint8_t offset) const; // 读取响应数据区寄存器
  static uint16_t crc16(const uint8_t *data, size_t length); // Modbus CRC16

  HardwareSerial &serial_;
  const SensorProfile *profile_ = nullptr;
  float *values_ = nullptr;
  bool *valid_ = nullptr;
  uint8_t slaveAddress_ = 0;
  uint8_t blockIndex_ = 0;
  uint8_t response_[kResponseBufferSize] = {0};
  size_t responseLength_ = 0;
  size_t expectedLength_ = 0;
  uint32_t requestStartMs_ = 0;
  uint32_t nextRequestMs_ = 0;
  bool waiting_ = false;
};
