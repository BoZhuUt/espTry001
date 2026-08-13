#include "calibration_executor.h"

#include <ModbusMaster.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

namespace {

void copyText(char *destination, size_t capacity, const char *source) {
  if (capacity == 0) return;
  snprintf(destination, capacity, "%s", source != nullptr ? source : "");
}

bool reached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

}  // namespace

CalibrationExecutor::CalibrationExecutor(HardwareSerial &serial)
    : serial_(serial) {}

bool CalibrationExecutor::begin(uint8_t slaveAddress,
                                const CalibrationProfile &profile,
                                uint8_t procedureIndex,
                                const float *inputValues,
                                uint8_t inputCount) {
  if (slaveAddress == 0 || slaveAddress > 247 ||
      procedureIndex >= profile.procedureCount) {
    return false;
  }
  const CalibrationProcedure &procedure = profile.procedures[procedureIndex];
  if (procedure.commandCount == 0 ||
      procedure.firstCommand + procedure.commandCount > profile.commandCount ||
      inputCount != procedure.inputCount ||
      inputCount > kMaxProcedureInputs ||
      (inputCount > 0 && inputValues == nullptr)) {
    return false;
  }

  for (uint8_t index = 0; index < inputCount; ++index) {
    const CalibrationInput &input =
        profile.inputs[procedure.firstInput + index];
    float value = inputValues[index];
    if (!isfinite(value)) return false;
    bool below = input.minimumInclusive ? value < input.minimum
                                        : value <= input.minimum;
    bool above = input.maximumInclusive ? value > input.maximum
                                        : value >= input.maximum;
    if (below || above) return false;
    inputValues_[index] = value;
  }

  profile_ = &profile;
  procedure_ = &procedure;
  slaveAddress_ = slaveAddress;
  inputCount_ = inputCount;
  commandIndex_ = 0;
  message_[0] = '\0';
  eventPending_ = false;
  while (serial_.available()) serial_.read();
  state_ = State::SendingWrite;
  return true;
}

void CalibrationExecutor::cancel() {
  if (active()) finish(State::Cancelled, "Calibration cancelled");
  profile_ = nullptr;
  procedure_ = nullptr;
  responseLength_ = 0;
  expectedLength_ = 0;
}

bool CalibrationExecutor::active() const {
  return state_ == State::SendingWrite ||
         state_ == State::WaitingWriteResponse ||
         state_ == State::WaitingAfterWrite ||
         state_ == State::WaitingResultPoll ||
         state_ == State::WaitingResultResponse;
}

bool CalibrationExecutor::takeEvent(Event &event) {
  if (!eventPending_) return false;
  event = pendingEvent_;
  eventPending_ = false;
  return true;
}

void CalibrationExecutor::update() {
  if (!active() || profile_ == nullptr || procedure_ == nullptr) return;
  uint32_t now = millis();
  switch (state_) {
    case State::SendingWrite:
      sendWrite();
      break;
    case State::WaitingWriteResponse:
    case State::WaitingResultResponse:
      receiveResponse();
      break;
    case State::WaitingAfterWrite:
      if (reached(now, deadlineMs_)) advanceAfterWait();
      break;
    case State::WaitingResultPoll:
      if (reached(now, deadlineMs_)) sendResultRead();
      break;
    default:
      break;
  }
}

bool CalibrationExecutor::prepareCommandValue(
    const CalibrationCommand &command, float &value) {
  if (command.valueSource == CalibrationValueSource::Fixed) {
    value = command.fixedValue;
  } else {
    if (command.inputIndex < procedure_->firstInput ||
        command.inputIndex >= procedure_->firstInput + procedure_->inputCount) {
      return false;
    }
    uint8_t localIndex = command.inputIndex - procedure_->firstInput;
    if (localIndex >= inputCount_) return false;
    const CalibrationInput &input = profile_->inputs[command.inputIndex];
    value = inputValues_[localIndex] * input.scaleNumerator /
            input.scaleDenominator;
  }
  if (!isfinite(value)) return false;
  if (command.type == CalibrationValueType::UInt16) {
    float rounded = roundf(value);
    return value >= 0.0f && value <= 65535.0f &&
           fabsf(value - rounded) < 0.0001f;
  }
  return true;
}

void CalibrationExecutor::sendWrite() {
  const CalibrationCommand &command =
      profile_->commands[procedure_->firstCommand + commandIndex_];
  float value = 0.0f;
  if (!prepareCommandValue(command, value)) {
    finish(State::CommunicationFailure, "Calibration write value invalid");
    return;
  }

  while (serial_.available()) serial_.read();
  uint16_t address = static_cast<uint16_t>(command.address1Based - 1);
  uint8_t request[13] = {0};
  size_t requestLength = 0;
  request[0] = slaveAddress_;
  request[2] = static_cast<uint8_t>(address >> 8);
  request[3] = static_cast<uint8_t>(address);
  if (command.type == CalibrationValueType::UInt16) {
    uint16_t integerValue = static_cast<uint16_t>(roundf(value));
    request[1] = 0x06;
    request[4] = static_cast<uint8_t>(integerValue >> 8);
    request[5] = static_cast<uint8_t>(integerValue);
    requestLength = 8;
    uint16_t crc = crc16(request, 6);
    request[6] = static_cast<uint8_t>(crc);
    request[7] = static_cast<uint8_t>(crc >> 8);
  } else {
    uint32_t raw = 0;
    memcpy(&raw, &value, sizeof(raw));
    uint16_t lowWord = static_cast<uint16_t>(raw);
    uint16_t highWord = static_cast<uint16_t>(raw >> 16);
    request[1] = 0x10;
    request[4] = 0;
    request[5] = 2;
    request[6] = 4;
    // CDAB: low 16-bit word is sent in the first Modbus register.
    request[7] = static_cast<uint8_t>(lowWord >> 8);
    request[8] = static_cast<uint8_t>(lowWord);
    request[9] = static_cast<uint8_t>(highWord >> 8);
    request[10] = static_cast<uint8_t>(highWord);
    requestLength = 13;
    uint16_t crc = crc16(request, 11);
    request[11] = static_cast<uint8_t>(crc);
    request[12] = static_cast<uint8_t>(crc >> 8);
  }

  resetResponse(8);
  requestStartedMs_ = millis();
  state_ = State::WaitingWriteResponse;
  emit(EventType::CommandStarted);
  serial_.write(request, requestLength);
  serial_.flush();
}

void CalibrationExecutor::sendResultRead() {
  const CalibrationCommand &command =
      profile_->commands[procedure_->firstCommand + commandIndex_];
  if (!command.result.enabled) {
    finish(State::CommunicationFailure, "Calibration result missing");
    return;
  }
  if (reached(millis(), resultStartedMs_ + command.result.timeoutMs)) {
    finish(State::CommunicationFailure, "Calibration result timed out");
    return;
  }

  while (serial_.available()) serial_.read();
  uint16_t address = static_cast<uint16_t>(command.result.address1Based - 1);
  uint8_t request[8] = {
      slaveAddress_, 0x03, static_cast<uint8_t>(address >> 8),
      static_cast<uint8_t>(address), 0, 1, 0, 0};
  uint16_t crc = crc16(request, 6);
  request[6] = static_cast<uint8_t>(crc);
  request[7] = static_cast<uint8_t>(crc >> 8);
  resetResponse(7);
  requestStartedMs_ = millis();
  state_ = State::WaitingResultResponse;
  serial_.write(request, sizeof(request));
  serial_.flush();
}

void CalibrationExecutor::receiveResponse() {
  while (serial_.available()) {
    if (responseLength_ >= sizeof(response_)) {
      finish(State::CommunicationFailure, "Calibration response too long");
      return;
    }
    response_[responseLength_++] = static_cast<uint8_t>(serial_.read());
    if (responseLength_ >= 2 && (response_[1] & 0x80) != 0) {
      expectedLength_ = 5;
    }
    if (responseLength_ >= expectedLength_) {
      if (state_ == State::WaitingWriteResponse) {
        handleWriteResponse();
      } else {
        handleResultResponse();
      }
      return;
    }
  }

  if (static_cast<uint32_t>(millis() - requestStartedMs_) >=
      kResponseTimeoutMs) {
    if (state_ == State::WaitingWriteResponse) {
      finish(State::ResultUnknown,
             "Write sent but response timed out; result unknown");
    } else {
      const CalibrationCommand &command =
          profile_->commands[procedure_->firstCommand + commandIndex_];
      if (reached(millis(), resultStartedMs_ + command.result.timeoutMs)) {
        finish(State::CommunicationFailure, "Calibration result timed out");
      } else {
        deadlineMs_ = millis() + command.result.pollIntervalMs;
        state_ = State::WaitingResultPoll;
      }
    }
  }
}

void CalibrationExecutor::handleWriteResponse() {
  const CalibrationCommand &command =
      profile_->commands[procedure_->firstCommand + commandIndex_];
  uint16_t receivedCrc = response_[responseLength_ - 2] |
                         (static_cast<uint16_t>(response_[responseLength_ - 1]) << 8);
  if (crc16(response_, responseLength_ - 2) != receivedCrc ||
      response_[0] != slaveAddress_) {
    finish(State::CommunicationFailure, "Calibration write response invalid");
    return;
  }
  if ((response_[1] & 0x80) != 0) {
    finish(State::DeviceFailure, "Calibration write exception response");
    return;
  }
  uint8_t expectedFunction =
      command.type == CalibrationValueType::UInt16 ? 0x06 : 0x10;
  uint16_t expectedAddress = static_cast<uint16_t>(command.address1Based - 1);
  if (responseLength_ != 8 || response_[1] != expectedFunction ||
      response_[2] != static_cast<uint8_t>(expectedAddress >> 8) ||
      response_[3] != static_cast<uint8_t>(expectedAddress)) {
    finish(State::CommunicationFailure, "Calibration write echo mismatch");
    return;
  }
  if (expectedFunction == 0x06) {
    float value = 0.0f;
    prepareCommandValue(command, value);
    uint16_t expectedValue = static_cast<uint16_t>(roundf(value));
    if (response_[4] != static_cast<uint8_t>(expectedValue >> 8) ||
        response_[5] != static_cast<uint8_t>(expectedValue)) {
      finish(State::CommunicationFailure, "Calibration write value mismatch");
      return;
    }
  } else if (response_[4] != 0 || response_[5] != 2) {
    finish(State::CommunicationFailure, "Calibration write count mismatch");
    return;
  }

  emit(EventType::CommandAccepted);
  deadlineMs_ = millis() + command.waitAfterMs;
  state_ = State::WaitingAfterWrite;
}

void CalibrationExecutor::advanceAfterWait() {
  const CalibrationCommand &command =
      profile_->commands[procedure_->firstCommand + commandIndex_];
  if (command.result.enabled) {
    resultStartedMs_ = millis();
    deadlineMs_ = millis();
    state_ = State::WaitingResultPoll;
    return;
  }
  commandIndex_++;
  if (commandIndex_ >= procedure_->commandCount) {
    finish(State::Completed, "Calibration command completed");
  } else {
    state_ = State::SendingWrite;
  }
}

void CalibrationExecutor::handleResultResponse() {
  const CalibrationCommand &command =
      profile_->commands[procedure_->firstCommand + commandIndex_];
  uint16_t receivedCrc = response_[responseLength_ - 2] |
                         (static_cast<uint16_t>(response_[responseLength_ - 1]) << 8);
  if (crc16(response_, responseLength_ - 2) != receivedCrc ||
      response_[0] != slaveAddress_) {
    finish(State::CommunicationFailure, "Calibration result response invalid");
    return;
  }
  if (responseLength_ == 5 && (response_[1] & 0x80) != 0) {
    finish(State::DeviceFailure, "Calibration result exception response");
    return;
  }
  if (responseLength_ != 7 || response_[1] != 0x03 || response_[2] != 2) {
    finish(State::CommunicationFailure, "Calibration result response invalid");
    return;
  }

  uint16_t resultValue =
      (static_cast<uint16_t>(response_[3]) << 8) | response_[4];
  const CalibrationResultMapping *matched = nullptr;
  for (uint8_t index = 0; index < command.result.mappingCount; ++index) {
    if (command.result.mappings[index].value == resultValue) {
      matched = &command.result.mappings[index];
      break;
    }
  }
  if (matched == nullptr) {
    finish(State::ResultUnknown, "Unknown calibration result value");
    return;
  }
  if (matched->outcome == CalibrationOutcome::Pending) {
    emit(EventType::ResultPending, resultValue,
         matched->message);
    if (reached(millis(), resultStartedMs_ + command.result.timeoutMs)) {
      finish(State::CommunicationFailure, "Calibration result timed out");
    } else {
      deadlineMs_ = millis() + command.result.pollIntervalMs;
      state_ = State::WaitingResultPoll;
    }
    return;
  }
  if (matched->outcome == CalibrationOutcome::Failure) {
    finish(State::DeviceFailure, matched->message);
    return;
  }
  if (matched->outcome == CalibrationOutcome::Info) {
    finish(State::Completed, matched->message);
    return;
  }

  commandIndex_++;
  if (commandIndex_ >= procedure_->commandCount) {
    finish(State::Completed, matched->message);
  } else {
    state_ = State::SendingWrite;
  }
}

void CalibrationExecutor::finish(State state, const char *message) {
  state_ = state;
  copyText(message_, sizeof(message_), message);
  emit(EventType::Finished, 0, message_);
}

void CalibrationExecutor::emit(EventType type, uint16_t resultValue,
                               const char *message) {
  // Rs485Task consumes one executor event per update. Preserve terminal events
  // over progress events if the caller has not drained the previous one yet.
  if (eventPending_ && pendingEvent_.type == EventType::Finished) return;
  pendingEvent_ = Event{};
  pendingEvent_.type = type;
  pendingEvent_.state = state_;
  pendingEvent_.commandIndex = commandIndex_;
  pendingEvent_.commandCount = commandCount();
  pendingEvent_.resultValue = resultValue;
  copyText(pendingEvent_.message, sizeof(pendingEvent_.message), message);
  eventPending_ = true;
}

void CalibrationExecutor::resetResponse(size_t expectedLength) {
  responseLength_ = 0;
  expectedLength_ = expectedLength;
}

uint16_t CalibrationExecutor::crc16(const uint8_t *data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t index = 0; index < length; ++index) {
    crc ^= data[index];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 1) != 0 ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
  }
  return crc;
}
