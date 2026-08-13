#pragma once

#include <Arduino.h>

#include "calibration_profile_store.h"

// Non-blocking Modbus RTU calibration transaction engine. It owns the serial
// port only while Rs485Service is in Calibration mode.
class CalibrationExecutor {
 public:
  enum class State : uint8_t {
    Idle,
    SendingWrite,
    WaitingWriteResponse,
    WaitingAfterWrite,
    WaitingResultPoll,
    WaitingResultResponse,
    Completed,
    DeviceFailure,
    CommunicationFailure,
    ResultUnknown,
    Cancelled,
  };

  enum class EventType : uint8_t {
    None,
    CommandStarted,
    CommandAccepted,
    ResultPending,
    Finished,
  };

  struct Event {
    EventType type = EventType::None;
    State state = State::Idle;
    uint8_t commandIndex = 0;
    uint8_t commandCount = 0;
    uint16_t resultValue = 0;
    char message[96] = "";
  };

  static constexpr uint8_t kMaxProcedureInputs = 8;
  static constexpr uint32_t kResponseTimeoutMs = 750;

  explicit CalibrationExecutor(HardwareSerial &serial);

  bool begin(uint8_t slaveAddress, const CalibrationProfile &profile,
             uint8_t procedureIndex, const float *inputValues,
             uint8_t inputCount);
  void cancel();
  void update();
  bool takeEvent(Event &event);

  State state() const { return state_; }
  bool active() const;
  uint8_t commandIndex() const { return commandIndex_; }
  uint8_t commandCount() const {
    return procedure_ != nullptr ? procedure_->commandCount : 0;
  }
  const char *message() const { return message_; }

 private:
  static constexpr size_t kResponseCapacity = 32;

  bool prepareCommandValue(const CalibrationCommand &command, float &value);
  void sendWrite();
  void sendResultRead();
  void receiveResponse();
  void handleWriteResponse();
  void handleResultResponse();
  void advanceAfterWait();
  void finish(State state, const char *message);
  void emit(EventType type, uint16_t resultValue = 0,
            const char *message = nullptr);
  void resetResponse(size_t expectedLength);
  static uint16_t crc16(const uint8_t *data, size_t length);

  HardwareSerial &serial_;
  const CalibrationProfile *profile_ = nullptr;
  const CalibrationProcedure *procedure_ = nullptr;
  uint8_t slaveAddress_ = 0;
  uint8_t commandIndex_ = 0;
  float inputValues_[kMaxProcedureInputs] = {0};
  uint8_t inputCount_ = 0;
  State state_ = State::Idle;
  uint8_t response_[kResponseCapacity] = {0};
  size_t responseLength_ = 0;
  size_t expectedLength_ = 0;
  uint32_t requestStartedMs_ = 0;
  uint32_t deadlineMs_ = 0;
  uint32_t resultStartedMs_ = 0;
  Event pendingEvent_{};
  bool eventPending_ = false;
  char message_[96] = "";
};
