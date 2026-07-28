#include "services/rs485_service.h"

#include <string.h>

#include "board/board_config.h"

Rs485Service::Rs485Service(HardwareSerial &serial)
    : serial_(serial), discovery_(serial), poller_(serial) {}

bool Rs485Service::begin(uint32_t timeoutMs) {
  if (taskHandle_ != nullptr) return true;

  commandQueue_ = xQueueCreate(kCommandQueueLength, sizeof(Command));
  eventQueue_ = xQueueCreate(kEventQueueLength, sizeof(Event));
  events_ = xEventGroupCreate();
  passRequestBuffer_ = xMessageBufferCreateStatic(
      kPassRequestCapacity, passRequestStorage_, &passRequestControl_);
  passResponseBuffer_ = xStreamBufferCreateStatic(
      kPassResponseCapacity, 1, passResponseStorage_, &passResponseControl_);
  if (commandQueue_ == nullptr || eventQueue_ == nullptr || events_ == nullptr ||
      passRequestBuffer_ == nullptr || passResponseBuffer_ == nullptr) {
    return false;
  }

  BaseType_t created =
      xTaskCreate(taskEntry, "Rs485Task", 8192, this, 4, &taskHandle_);
  if (created != pdPASS) {
    taskHandle_ = nullptr;
    return false;
  }

  EventBits_t bits = xEventGroupWaitBits(
      events_, kInitializedBit, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeoutMs));
  return (bits & kInitializedBit) != 0;
}

bool Rs485Service::enterPassThrough() {
  Command command;
  command.type = CommandType::EnterPass;
  return sendCommand(command);
}

bool Rs485Service::startDiscovery() {
  Command command;
  command.type = CommandType::StartDiscovery;
  return sendCommand(command);
}

bool Rs485Service::startPolling(const SensorProfile *profile) {
  if (profile == nullptr) return false;
  Command command;
  command.type = CommandType::StartPolling;
  command.profile = profile;
  return sendCommand(command);
}

bool Rs485Service::stopAndWait(uint32_t timeoutMs) {
  if (events_ == nullptr) return false;
  xEventGroupClearBits(events_, kStoppedBit);
  Command command;
  command.type = CommandType::Stop;
  if (!sendCommand(command)) return false;
  EventBits_t bits = xEventGroupWaitBits(
      events_, kStoppedBit, pdTRUE, pdTRUE, pdMS_TO_TICKS(timeoutMs));
  return (bits & kStoppedBit) != 0;
}

bool Rs485Service::enqueuePassFrame(const uint8_t *frame, size_t length) {
  if (passRequestBuffer_ == nullptr || frame == nullptr || length == 0 ||
      length > kMaxPassFrameLength) {
    return false;
  }
  if (xMessageBufferSpaceAvailable(passRequestBuffer_) < length + sizeof(size_t) ||
      xMessageBufferSend(passRequestBuffer_, frame, length, 0) != length) {
    portENTER_CRITICAL(&statusMux_);
    status_.passRequestDrops++;
    portEXIT_CRITICAL(&statusMux_);
    return false;
  }
  return true;
}

size_t Rs485Service::readPassResponse(uint8_t *buffer, size_t capacity) {
  if (passResponseBuffer_ == nullptr || buffer == nullptr || capacity == 0) {
    return 0;
  }
  return xStreamBufferReceive(passResponseBuffer_, buffer, capacity, 0);
}

size_t Rs485Service::passResponseAvailable() const {
  return passResponseBuffer_ == nullptr
             ? 0
             : xStreamBufferBytesAvailable(passResponseBuffer_);
}

bool Rs485Service::takeEvent(Event &event) {
  return eventQueue_ != nullptr &&
         xQueueReceive(eventQueue_, &event, 0) == pdPASS;
}

void Rs485Service::getStatus(Status &status) const {
  portENTER_CRITICAL(&statusMux_);
  status = status_;
  portEXIT_CRITICAL(&statusMux_);
}

void Rs485Service::taskEntry(void *context) {
  static_cast<Rs485Service *>(context)->taskLoop();
}

void Rs485Service::taskLoop() {
  pinMode(BoardConfig::kRs485Rx, INPUT_PULLUP);
  serial_.begin(BoardConfig::kRs485Baudrate, SERIAL_8E1,
                BoardConfig::kRs485Rx, BoardConfig::kRs485Tx);
  xEventGroupSetBits(events_, kInitializedBit | kStoppedBit);
  publishStatus(true);

  for (;;) {
#if RS485_TX_TEST_MODE
    // Oscilloscope channel test: continuous 0x55 at 9600 8E1. HardwareSerial
    // write blocks when its TX buffer is full, preserving the original test.
    serial_.write(0x55);
    continue;
#endif

    Command command;
    bool handledCommand =
        xQueueReceive(commandQueue_, &command, pdMS_TO_TICKS(2)) == pdPASS;
    if (handledCommand) handleCommand(command);

    switch (mode_) {
      case Mode::PassThrough:
        updatePassThrough();
        break;
      case Mode::Discovery:
        updateDiscovery();
        break;
      case Mode::Polling:
        updatePolling();
        break;
      case Mode::Idle:
      case Mode::Fault:
        break;
    }
    publishStatus(handledCommand);
  }
}

void Rs485Service::handleCommand(const Command &command) {
  switch (command.type) {
    case CommandType::EnterPass:
      discovery_.cancel();
      poller_.cancel();
      drainSerial();
      activeProfile_ = nullptr;
      memset(values_, 0, sizeof(values_));
      memset(valid_, 0, sizeof(valid_));
      resetPassBuffers();
      mode_ = Mode::PassThrough;
      break;

    case CommandType::StartDiscovery:
      discovery_.cancel();
      poller_.cancel();
      drainSerial();
      activeProfile_ = nullptr;
      memset(values_, 0, sizeof(values_));
      memset(valid_, 0, sizeof(valid_));
      resetPassBuffers();
      lastModbusError_ = 0xFF;
      blockUpdateSequence_ = 0;
      sensorFoundEmitted_ = false;
      discovery_.start();
      mode_ = Mode::Discovery;
      break;

    case CommandType::StartPolling:
      // Preserve the successful discovery result/state while ownership moves
      // to the poller; the UI uses it as the sensor-details availability flag.
      poller_.cancel();
      drainSerial();
      activeProfile_ = command.profile;
      memset(values_, 0, sizeof(values_));
      memset(valid_, 0, sizeof(valid_));
      lastModbusError_ = 0xFF;
      blockUpdateSequence_ = 0;
      poller_.begin(discovery_.result().slaveAddress, *activeProfile_,
                    values_, valid_);
      mode_ = Mode::Polling;
      break;

    case CommandType::Stop: {
      discovery_.cancel();
      poller_.cancel();
      drainSerial();
      activeProfile_ = nullptr;
      resetPassBuffers();
      mode_ = Mode::Idle;
      Event event;
      event.type = EventType::Stopped;
      emitEvent(event);
      xEventGroupSetBits(events_, kStoppedBit);
      break;
    }
  }
}

bool Rs485Service::sendCommand(const Command &command) {
  if (commandQueue_ == nullptr ||
      xQueueSend(commandQueue_, &command, 0) != pdPASS) {
    return false;
  }
  UBaseType_t depth = uxQueueMessagesWaiting(commandQueue_);
  portENTER_CRITICAL(&statusMux_);
  if (depth > status_.commandQueuePeak) {
    status_.commandQueuePeak =
        depth > UINT8_MAX ? UINT8_MAX : static_cast<uint8_t>(depth);
  }
  portEXIT_CRITICAL(&statusMux_);
  return true;
}

bool Rs485Service::emitEvent(const Event &event) {
  if (eventQueue_ == nullptr ||
      xQueueSend(eventQueue_, &event, 0) != pdPASS) {
    portENTER_CRITICAL(&statusMux_);
    status_.droppedEvents++;
    portEXIT_CRITICAL(&statusMux_);
    return false;
  }
  UBaseType_t depth = uxQueueMessagesWaiting(eventQueue_);
  portENTER_CRITICAL(&statusMux_);
  if (depth > status_.eventQueuePeak) {
    status_.eventQueuePeak =
        depth > UINT8_MAX ? UINT8_MAX : static_cast<uint8_t>(depth);
  }
  portEXIT_CRITICAL(&statusMux_);
  return true;
}

void Rs485Service::updatePassThrough() {
  uint8_t frame[kMaxPassFrameLength];
  size_t length = xMessageBufferReceive(
      passRequestBuffer_, frame, sizeof(frame), 0);
  if (length > 0) {
    serial_.write(frame, length);
    serial_.flush();
  }

  uint8_t response[64];
  size_t responseLength = 0;
  while (serial_.available() && responseLength < sizeof(response)) {
    response[responseLength++] = static_cast<uint8_t>(serial_.read());
  }
  if (responseLength > 0 &&
      (xStreamBufferSpacesAvailable(passResponseBuffer_) < responseLength ||
       xStreamBufferSend(passResponseBuffer_, response, responseLength, 0) !=
           responseLength)) {
    portENTER_CRITICAL(&statusMux_);
    status_.passResponseDrops++;
    portEXIT_CRITICAL(&statusMux_);
  }
}

void Rs485Service::updateDiscovery() {
  discovery_.update();
  if (discovery_.state() == SensorDiscovery::State::Success &&
      !sensorFoundEmitted_) {
    sensorFoundEmitted_ = true;
    Event event;
    event.type = EventType::SensorFound;
    event.identity = discovery_.result();
    emitEvent(event);
  }
}

void Rs485Service::updatePolling() {
  if (activeProfile_ == nullptr) return;
  if (poller_.update(lastModbusError_)) {
    blockUpdateSequence_++;
    if (lastModbusError_ != ModbusMaster::ku8MBSuccess) {
      Event event;
      event.type = EventType::CommunicationError;
      event.error = lastModbusError_;
      emitEvent(event);
    }
  }

  if (poller_.takeScanCompleted()) {
    Event event;
    event.type = EventType::ScanCompleted;
    event.identity = discovery_.result();
    event.scanSequence = poller_.scanSequence();
    event.fieldCount =
        min(activeProfile_->fieldCount, static_cast<uint8_t>(kMaxSensorFields));
    memcpy(event.values, values_,
           event.fieldCount * sizeof(event.values[0]));
    memcpy(event.valid, valid_,
           event.fieldCount * sizeof(event.valid[0]));
    event.error = lastModbusError_;
    emitEvent(event);
  }
}

void Rs485Service::drainSerial() {
  while (serial_.available()) serial_.read();
}

void Rs485Service::resetPassBuffers() {
  if (passRequestBuffer_ != nullptr) xMessageBufferReset(passRequestBuffer_);
  if (passResponseBuffer_ != nullptr) xStreamBufferReset(passResponseBuffer_);
}

void Rs485Service::publishStatus(bool force) {
  uint32_t now = millis();
  if (!force && (uint32_t)(now - lastStatusPublishMs_) < 50) return;
  lastStatusPublishMs_ = now;

  Status next;
  next.mode = mode_;
  next.discoveryState = discovery_.state();
  next.discoveryError = discovery_.error();
  next.identity = discovery_.result();
  next.discoveryAttempt = discovery_.attempt();
  next.discoveryMaxAttempts = discovery_.maxAttempts();
  next.discoveryExceptionCode = discovery_.exceptionCode();
  snprintf(next.discoveryErrorText, sizeof(next.discoveryErrorText), "%s",
           discovery_.errorText());
  next.fieldCount =
      activeProfile_ != nullptr ? activeProfile_->fieldCount : 0;
  memcpy(next.values, values_, sizeof(next.values));
  memcpy(next.valid, valid_, sizeof(next.valid));
  next.scanSequence = poller_.scanSequence();
  next.blockUpdateSequence = blockUpdateSequence_;
  next.lastModbusError = lastModbusError_;
  next.commandQueueDepth =
      static_cast<uint8_t>(uxQueueMessagesWaiting(commandQueue_));
  next.eventQueueDepth =
      static_cast<uint8_t>(uxQueueMessagesWaiting(eventQueue_));
  next.taskStackHighWater = uxTaskGetStackHighWaterMark(nullptr);

  portENTER_CRITICAL(&statusMux_);
  next.commandQueuePeak = status_.commandQueuePeak;
  next.eventQueuePeak = status_.eventQueuePeak;
  next.droppedEvents = status_.droppedEvents;
  next.passRequestDrops = status_.passRequestDrops;
  next.passResponseDrops = status_.passResponseDrops;
  status_ = next;
  portEXIT_CRITICAL(&statusMux_);
}
