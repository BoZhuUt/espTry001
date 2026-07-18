#pragma once

#include <Arduino.h>

// 传感器发现状态机。
// update() 是非阻塞函数，开始发现后必须在 loop() 中持续调用。
class SensorDiscovery {
public:
  static constexpr uint16_t kDiscoveryRegister = 41000; // 41001, 1-based
  static constexpr uint16_t kDiscoveryQuantity = 1;
  static constexpr uint16_t kIdentityRegister = 41000; // 41001, 1-based
  static constexpr uint16_t kIdentityRegisterCount = 14; // 41001~41014
  static constexpr bool kProductLowWordFirst = true; // CDAB
  static constexpr bool kNameHighByteFirst = false; // 每个寄存器低字节字符在前
  // 单次请求最多等待 500 ms；失败后间隔 2 s 再试，最多发送 5 次。
  static constexpr uint32_t kDefaultTimeoutMs = 500;
  static constexpr uint32_t kDefaultRetryIntervalMs = 2000;
  static constexpr uint8_t kDefaultMaxAttempts = 5;

  enum class State : uint8_t {
    Idle,
    DiscoveringAddress,
    ReadingIdentity,
    Success,
    Failed
  };

  enum class Error : uint8_t {
    None,
    Timeout,
    CrcMismatch,
    InvalidAddress,
    InvalidFunction,
    InvalidLength,
    ExceptionResponse,
    BufferOverflow
  };

  struct Result {
    uint8_t slaveAddress = 0;
    uint16_t runStatus = 0;
    uint16_t commStatus = 0;
    uint16_t calibStatus = 0;
    uint16_t configStatus = 0;
    uint32_t productNum = 0;
    char deviceName[17] = {0}; // 16字节名称 + 结尾NUL
  };

  // serial 通常传入连接 RS485 的 Serial1。
  explicit SensorDiscovery(HardwareSerial &serial);

  // 开始完整流程：先向地址 0 查询真实地址，再读取身份区。
  // maxAttempts 是每个阶段的总发送次数（包含第一次）；任意一次成功后
  // 立即停止该阶段重试。retryIntervalMs 是失败到下一次发送的间隔。
  void start(uint8_t maxAttempts = kDefaultMaxAttempts,
             uint32_t timeoutMs = kDefaultTimeoutMs,
             uint32_t retryIntervalMs = kDefaultRetryIntervalMs);
  // 推进接收、校验及超时状态；每次调用都会快速返回。
  void update();
  // 退出发现流程并清空当前接收状态。
  void cancel();

  State state() const { return state_; }
  Error error() const { return error_; }
  const Result &result() const { return result_; }
  uint8_t attempt() const { return attempt_; }
  uint8_t maxAttempts() const { return maxAttempts_; }
  uint8_t exceptionCode() const { return exceptionCode_; }
  const char *errorText() const;

private:
  static constexpr size_t kResponseBufferSize = 64;

  HardwareSerial &serial_;
  State state_ = State::Idle;
  Error error_ = Error::None;
  Result result_{};
  uint8_t response_[kResponseBufferSize] = {0};
  size_t responseLength_ = 0;
  size_t expectedLength_ = 0;
  uint8_t attempt_ = 0;
  uint8_t maxAttempts_ = kDefaultMaxAttempts;
  uint8_t exceptionCode_ = 0;
  uint32_t timeoutMs_ = kDefaultTimeoutMs;
  uint32_t retryIntervalMs_ = kDefaultRetryIntervalMs;
  unsigned long requestStartTime_ = 0;
  unsigned long retryAtTime_ = 0;
  bool retryWaiting_ = false;

  void sendAddressRequest();  // 地址 0、FC03、读取资料地址 41001
  void sendIdentityRequest(); // 使用真实地址读取资料地址 41001~41014
  // 组装并发送一帧 FC03 请求，同时切换到指定等待状态。
  void sendReadRequest(uint8_t slaveAddress, uint16_t startAddress,
                       uint16_t quantity, State waitingState);
  // 完整帧收到后统一检查 CRC、功能码，再分派给当前阶段。
  void handleResponse();
  // 从响应帧的从站地址字节取得设备真实地址。
  void handleAddressResponse();
  // 解析状态、PN（CDAB）和 16 字节设备名称。
  void handleIdentityResponse();
  // 当前阶段失败时安排延时重试，达到次数上限后进入 Failed。
  void retryOrFail(Error error);
  // 从 FC03 数据区读取第 index 个寄存器。
  uint16_t responseRegister(uint16_t index) const;
  // 标准 Modbus RTU CRC16；在线路上低字节先发送。
  static uint16_t crc16(const uint8_t *data, size_t length);
};
