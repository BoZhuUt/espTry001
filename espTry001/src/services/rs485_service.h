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
    UBaseType_t taskStackHighWater = 0;
  };

  static constexpr UBaseType_t kCommandQueueLength = 8;
  static constexpr UBaseType_t kEventQueueLength = 8;
  static constexpr size_t kMaxPassFrameLength = 256;
  static constexpr size_t kPassRequestCapacity = 1024;
  static constexpr size_t kPassResponseCapacity = 1024;
  static constexpr uint32_t kStopTimeoutMs = 2000;

  explicit Rs485Service(HardwareSerial &serial);

  bool begin(uint32_t timeoutMs = 3000);
  bool enterPassThrough();
  bool startDiscovery();
  bool startPolling(const SensorProfile *profile);
  bool startFixedPolling(uint8_t slaveAddress, const SensorProfile *profile);
  bool stopAndWait(uint32_t timeoutMs = kStopTimeoutMs);

  bool enqueuePassFrame(const uint8_t *frame, size_t length);
  size_t readPassResponse(uint8_t *buffer, size_t capacity);
  size_t passResponseAvailable() const;

  bool takeEvent(Event &event);
  void getStatus(Status &status) const;

 private:
  enum class CommandType : uint8_t {
    EnterPass,
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

  static void taskEntry(void *context);
  void taskLoop();
  void handleCommand(const Command &command);
  bool sendCommand(const Command &command);
  bool emitEvent(const Event &event);
  void updatePassThrough();
  void updateDiscovery();
  void updatePolling();
  void drainSerial();
  void resetPassBuffers();
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

  Mode mode_ = Mode::Idle;
  const SensorProfile *activeProfile_ = nullptr;
  uint8_t pollingSlaveAddress_ = 0;
  float values_[kMaxSensorFields] = {0};
  bool valid_[kMaxSensorFields] = {false};
  uint8_t lastModbusError_ = 0xFF;
  uint32_t blockUpdateSequence_ = 0;
  bool sensorFoundEmitted_ = false;
  uint32_t lastStatusPublishMs_ = 0;

  mutable portMUX_TYPE statusMux_ = portMUX_INITIALIZER_UNLOCKED;
  Status status_;
};
