#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/message_buffer.h>
#include <freertos/queue.h>
#include <freertos/stream_buffer.h>
#include <freertos/task.h>

#include "sensor_discovery.h"
#include "sensor_profile.h"

class Rs485Service {
 public:
  enum class Mode : uint8_t {
    Idle,
    PassThrough,
    Discovery,
    Polling,
    Fault,
    FixedPolling,
    // Append new modes to preserve the numeric values exposed by diagnostics.
    RawPassThrough,
  };

  enum class UartFormat : uint8_t {
    Modbus8E1,
    Boot8N1,
  };

  enum class EventType : uint8_t {
    SensorFound,
    ScanCompleted,
    CommunicationError,
    Stopped,
  };

  struct Event {
    EventType type = EventType::Stopped;
    Mode sourceMode = Mode::Idle;
    SensorDiscovery::Result identity{};
    uint32_t scanSequence = 0;
    uint8_t fieldCount = 0;
    float values[kMaxSensorFields] = {0};
    bool valid[kMaxSensorFields] = {false};
    uint8_t error = 0xFF;
  };

  struct Status {
    Mode mode = Mode::Idle;
    SensorDiscovery::State discoveryState = SensorDiscovery::State::Idle;
    SensorDiscovery::Error discoveryError = SensorDiscovery::Error::None;
    SensorDiscovery::Result identity{};
    uint8_t discoveryAttempt = 0;
    uint8_t discoveryMaxAttempts = 0;
    uint8_t discoveryExceptionCode = 0;
    char discoveryErrorText[32] = "None";

    uint8_t fieldCount = 0;
    float values[kMaxSensorFields] = {0};
    bool valid[kMaxSensorFields] = {false};
    uint32_t scanSequence = 0;
    uint32_t blockUpdateSequence = 0;
    uint8_t lastModbusError = 0xFF;

    uint8_t commandQueueDepth = 0;
    uint8_t commandQueuePeak = 0;
    uint8_t eventQueueDepth = 0;
    uint8_t eventQueuePeak = 0;
    uint32_t droppedEvents = 0;
    uint32_t passRequestDrops = 0;
    uint32_t passResponseDrops = 0;
    uint32_t rawUsbToUartBytes = 0;
    uint32_t rawUartToUsbBytes = 0;
    uint32_t rawUsbToUartDrops = 0;
    uint32_t rawUartToUsbDrops = 0;
    uint16_t rawRequestPeak = 0;
    uint16_t rawResponsePeak = 0;
    UartFormat uartFormat = UartFormat::Modbus8E1;
    uint32_t uartReconfigureCount = 0;
    UBaseType_t taskStackHighWater = 0;
  };

  static constexpr UBaseType_t kCommandQueueLength = 8;
  static constexpr UBaseType_t kEventQueueLength = 8;
  static constexpr size_t kMaxPassFrameLength = 2048;     // 增大: 256->2048, 支持大JSON升级包
  static constexpr size_t kPassRequestCapacity = 16384;   // 增大: 4096->16KB, 升级大数据量缓冲
  static constexpr size_t kPassResponseCapacity = 8192;   // 增大: 4096->8KB, 升级响应缓冲
  // A YMODEM-1K packet occupies 1029 bytes on the wire. These independent
  // byte streams provide enough headroom for multiple USB scheduling bursts.
  static constexpr size_t kRawRequestCapacity = 4096;
  static constexpr size_t kRawResponseCapacity = 2048;
  static constexpr uint32_t kStopTimeoutMs = 2000;

  explicit Rs485Service(HardwareSerial &serial);

  bool begin(uint32_t timeoutMs = 3000);
  bool enterPassThrough();
  bool enterUpgradePassAndWait(uint32_t timeoutMs = kStopTimeoutMs);
  bool startDiscovery();
  bool startPolling(const SensorProfile *profile);
  bool startFixedPolling(uint8_t slaveAddress, const SensorProfile *profile);
  bool stopAndWait(uint32_t timeoutMs = kStopTimeoutMs);

  bool enqueuePassFrame(const uint8_t *frame, size_t length);
  size_t readPassResponse(uint8_t *buffer, size_t capacity);
  size_t passResponseAvailable() const;

  /**
   * 把 USB 收到的升级字节放入 USB->UART 原始流。
   *
   * 此接口不识别 Modbus 或 YMODEM 帧，只返回实际接收的字节数。调用者
   * 必须保留未接收部分并稍后重试，不能假定一次写入一定完整。
   */
  size_t enqueueRawBytes(const uint8_t *data, size_t length);
  size_t rawRequestSpace() const;

  /**
   * 读取 UART->USB 原始流中的升级字节。
   *
   * 返回值可能小于 capacity；UsbService 会保留 USB 短写偏移，确保每个
   * 字节只读取一次并最终按原顺序发送到电脑。
   */
  size_t readRawResponse(uint8_t *buffer, size_t capacity);
  size_t rawResponseAvailable() const;

  bool takeEvent(Event &event);
  void getStatus(Status &status) const;

 private:
  enum class CommandType : uint8_t {
    EnterPass,
    EnterUpgradePass,
    StartDiscovery,
    StartPolling,
    StartFixedPolling,
    Stop,
  };

  struct Command {
    CommandType type = CommandType::Stop;
    const SensorProfile *profile = nullptr;
    uint8_t slaveAddress = 0;
  };

  static constexpr EventBits_t kInitializedBit = BIT0;
  static constexpr EventBits_t kStoppedBit = BIT1;
  static constexpr EventBits_t kUpgradePassReadyBit = BIT2;

  static void taskEntry(void *context);
  void taskLoop();
  void handleCommand(const Command &command);
  bool sendCommand(const Command &command);
  bool emitEvent(const Event &event);
  void updatePassThrough();
  /**
   * 在 Rs485Task 内推进原始双向字节流。
   *
   * USB->UART 方向保留 HardwareSerial 短写的剩余偏移；UART->USB 方向
   * 只在 StreamBuffer 有空间时读取串口，从而不主动丢弃 YMODEM 数据。
   */
  void updateRawPassThrough();
  void updateDiscovery();
  void updatePolling();
  /**
   * 在 Rs485Task 内重新初始化唯一的 HardwareSerial。
   *
   * 其他任务只能发送模式命令，不能直接调用 Serial1.end()/begin()；这样
   * 可以保证切换校验位时不存在并发轮询、发现或透传写操作。
   */
  bool configureUart(UartFormat format);
  void drainSerial();
  void resetPassBuffers();
  void resetRawBuffers();
  void publishStatus(bool force = false);

  HardwareSerial &serial_;
  SensorDiscovery discovery_;
  SensorProfilePoller poller_;

  QueueHandle_t commandQueue_ = nullptr;
  QueueHandle_t eventQueue_ = nullptr;
  EventGroupHandle_t events_ = nullptr;
  TaskHandle_t taskHandle_ = nullptr;

  StaticMessageBuffer_t passRequestControl_{};
  uint8_t passRequestStorage_[kPassRequestCapacity + 1] = {0};
  MessageBufferHandle_t passRequestBuffer_ = nullptr;

  StaticStreamBuffer_t passResponseControl_{};
  uint8_t passResponseStorage_[kPassResponseCapacity + 1] = {0};
  StreamBufferHandle_t passResponseBuffer_ = nullptr;

  StaticStreamBuffer_t rawRequestControl_{};
  uint8_t rawRequestStorage_[kRawRequestCapacity + 1] = {0};
  StreamBufferHandle_t rawRequestBuffer_ = nullptr;

  StaticStreamBuffer_t rawResponseControl_{};
  uint8_t rawResponseStorage_[kRawResponseCapacity + 1] = {0};
  StreamBufferHandle_t rawResponseBuffer_ = nullptr;

  static constexpr size_t kRawUartChunkSize = 256;
  uint8_t rawUartTx_[kRawUartChunkSize] = {0};
  size_t rawUartTxLength_ = 0;
  size_t rawUartTxOffset_ = 0;

  Mode mode_ = Mode::Idle;
  const SensorProfile *activeProfile_ = nullptr;
  uint8_t pollingSlaveAddress_ = 0;
  float values_[kMaxSensorFields] = {0};
  bool valid_[kMaxSensorFields] = {false};
  uint8_t lastModbusError_ = 0xFF;
  uint32_t blockUpdateSequence_ = 0;
  bool sensorFoundEmitted_ = false;
  bool uartStarted_ = false;
  UartFormat uartFormat_ = UartFormat::Modbus8E1;
  uint32_t uartReconfigureCount_ = 0;
  uint32_t lastStatusPublishMs_ = 0;

  mutable portMUX_TYPE statusMux_ = portMUX_INITIALIZER_UNLOCKED;
  Status status_;
};
