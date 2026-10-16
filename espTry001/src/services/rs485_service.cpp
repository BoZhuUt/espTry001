#include "services/rs485_service.h"

#include <string.h>

#include "board/board_config.h"

Rs485Service::Rs485Service(HardwareSerial &serial)
    : serial_(serial), discovery_(serial), poller_(serial), calibration_(serial) {}

bool Rs485Service::begin(uint32_t timeoutMs) {
  if (taskHandle_ != nullptr) return true;

  commandQueue_ = xQueueCreate(kCommandQueueLength, sizeof(Command));
  eventQueue_ = xQueueCreate(kEventQueueLength, sizeof(Event));
  events_ = xEventGroupCreate();
  passRequestBuffer_ = xMessageBufferCreateStatic(
      kPassRequestCapacity, passRequestStorage_, &passRequestControl_);
  passResponseBuffer_ = xStreamBufferCreateStatic(
      kPassResponseCapacity, 1, passResponseStorage_, &passResponseControl_);
  rawRequestBuffer_ = xStreamBufferCreateStatic(
      kRawRequestCapacity, 1, rawRequestStorage_, &rawRequestControl_);
  rawResponseBuffer_ = xStreamBufferCreateStatic(
      kRawResponseCapacity, 1, rawResponseStorage_, &rawResponseControl_);
  if (commandQueue_ == nullptr || eventQueue_ == nullptr || events_ == nullptr ||
      passRequestBuffer_ == nullptr || passResponseBuffer_ == nullptr ||
      rawRequestBuffer_ == nullptr || rawResponseBuffer_ == nullptr) {
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

bool Rs485Service::enterUpgradePassAndWait(uint32_t timeoutMs) {
  if (events_ == nullptr) return false;
  xEventGroupClearBits(events_, kUpgradePassReadyBit);
  Command command;
  command.type = CommandType::EnterUpgradePass;
  if (!sendCommand(command)) return false;
  EventBits_t bits = xEventGroupWaitBits(
      events_, kUpgradePassReadyBit, pdTRUE, pdTRUE,
      pdMS_TO_TICKS(timeoutMs));
  return (bits & kUpgradePassReadyBit) != 0;
}

bool Rs485Service::setPassLineCoding(const CdcLineCoding &coding,
                                    uint32_t revision) {
  Command command;
  command.type = CommandType::SetPassLineCoding;
  command.coding = coding;
  command.codingRevision = revision;
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

bool Rs485Service::startFixedPolling(uint8_t slaveAddress,
                                     const SensorProfile *profile) {
  if (slaveAddress == 0 || slaveAddress > 247 || profile == nullptr) {
    return false;
  }
  Command command;
  command.type = CommandType::StartFixedPolling;
  command.profile = profile;
  command.slaveAddress = slaveAddress;
  return sendCommand(command);
}

bool Rs485Service::startCalibration(uint8_t slaveAddress,
                                    const CalibrationProfile *profile,
                                    uint8_t procedureIndex,
                                    const float *inputValues,
                                    uint8_t inputCount) {
  if (slaveAddress == 0 || slaveAddress > 247 || profile == nullptr ||
      procedureIndex >= profile->procedureCount ||
      inputCount > CalibrationExecutor::kMaxProcedureInputs ||
      (inputCount > 0 && inputValues == nullptr)) {
    return false;
  }
  Command command;
  command.type = CommandType::StartCalibration;
  command.calibrationProfile = profile;
  command.slaveAddress = slaveAddress;
  command.procedureIndex = procedureIndex;
  command.inputCount = inputCount;
  if (inputCount > 0) {
    memcpy(command.inputValues, inputValues, inputCount * sizeof(float));
  }
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

size_t Rs485Service::enqueueRawBytes(const uint8_t *data, size_t length) {
  if (rawRequestBuffer_ == nullptr || data == nullptr || length == 0) return 0;

  size_t accepted = xStreamBufferSend(rawRequestBuffer_, data, length, 0);
  size_t depth = xStreamBufferBytesAvailable(rawRequestBuffer_);
  portENTER_CRITICAL(&statusMux_);
  status_.rawUsbToUartBytes += accepted;
  status_.rawUsbToUartDrops += length - accepted;
  if (depth > status_.rawRequestPeak) {
    status_.rawRequestPeak = static_cast<uint16_t>(
        min(depth, static_cast<size_t>(UINT16_MAX)));
  }
  portEXIT_CRITICAL(&statusMux_);
  return accepted;
}

size_t Rs485Service::rawRequestSpace() const {
  return rawRequestBuffer_ == nullptr
             ? 0
             : xStreamBufferSpacesAvailable(rawRequestBuffer_);
}

size_t Rs485Service::readRawResponse(uint8_t *buffer, size_t capacity) {
  if (rawResponseBuffer_ == nullptr || buffer == nullptr || capacity == 0) {
    return 0;
  }
  size_t received =
      xStreamBufferReceive(rawResponseBuffer_, buffer, capacity, 0);
  if (received > 0) {
    portENTER_CRITICAL(&statusMux_);
    status_.rawUartToUsbBytes += received;
    portEXIT_CRITICAL(&statusMux_);
  }
  return received;
}

size_t Rs485Service::rawResponseAvailable() const {
  return rawResponseBuffer_ == nullptr
             ? 0
             : xStreamBufferBytesAvailable(rawResponseBuffer_);
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
  configureUart(UartFormat::Modbus8E1);
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
      case Mode::RawPassThrough:
        updateRawPassThrough();
        break;
      case Mode::Discovery:
        updateDiscovery();
        break;
      case Mode::Polling:
      case Mode::FixedPolling:
        updatePolling();
        break;
      case Mode::Calibration:
        updateCalibration();
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
    case CommandType::SetPassLineCoding:
      passCoding_ = command.coding;
      passCodingRevision_ = command.codingRevision;
      if (mode_ == Mode::PassThrough) {
        // UsbService waits for this revision before touching either buffer.
        resetPassBuffers();
        if (passCoding_.supported() && !configureUart(UartFormat::HostCdc)) {
          mode_ = Mode::Fault;
        }
        drainSerial();
      }
      break;

    case CommandType::EnterPass:
      discovery_.cancel();
      poller_.cancel();
      calibration_.cancel();
      drainSerial();
      activeProfile_ = nullptr;
      pollingSlaveAddress_ = 0;
      memset(values_, 0, sizeof(values_));
      memset(valid_, 0, sizeof(valid_));
      resetPassBuffers();
      resetRawBuffers();
      mode_ = (!passCoding_.supported() || configureUart(UartFormat::HostCdc))
                  ? Mode::PassThrough
                  : Mode::Fault;
      break;

    case CommandType::EnterUpgradePass:
      discovery_.cancel();
      poller_.cancel();
      calibration_.cancel();
      drainSerial();
      activeProfile_ = nullptr;
      pollingSlaveAddress_ = 0;
      memset(values_, 0, sizeof(values_));
      memset(valid_, 0, sizeof(valid_));
      resetPassBuffers();
      resetRawBuffers();
      mode_ = configureUart(UartFormat::Boot8N1)
                  ? Mode::RawPassThrough
                  : Mode::Fault;
      if (mode_ == Mode::RawPassThrough) {
        xEventGroupSetBits(events_, kUpgradePassReadyBit);
      }
      break;

    case CommandType::StartDiscovery:
      discovery_.cancel();
      poller_.cancel();
      calibration_.cancel();
      drainSerial();
      activeProfile_ = nullptr;
      pollingSlaveAddress_ = 0;
      memset(values_, 0, sizeof(values_));
      memset(valid_, 0, sizeof(valid_));
      resetPassBuffers();
      resetRawBuffers();
      lastModbusError_ = 0xFF;
      blockUpdateSequence_ = 0;
      sensorFoundEmitted_ = false;
      if (configureUart(UartFormat::Modbus8E1)) {
        discovery_.start();
        mode_ = Mode::Discovery;
      } else {
        mode_ = Mode::Fault;
      }
      break;

    case CommandType::StartPolling:
      // Preserve the successful discovery result/state while ownership moves
      // to the poller; the UI uses it as the sensor-details availability flag.
      poller_.cancel();
      calibration_.cancel();
      drainSerial();
      activeProfile_ = command.profile;
      pollingSlaveAddress_ = discovery_.result().slaveAddress;
      memset(values_, 0, sizeof(values_));
      memset(valid_, 0, sizeof(valid_));
      lastModbusError_ = 0xFF;
      blockUpdateSequence_ = 0;
      if (configureUart(UartFormat::Modbus8E1)) {
        poller_.begin(pollingSlaveAddress_, *activeProfile_, values_, valid_);
        mode_ = Mode::Polling;
      } else {
        mode_ = Mode::Fault;
      }
      break;

    case CommandType::StartFixedPolling:
      discovery_.cancel();
      poller_.cancel();
      calibration_.cancel();
      drainSerial();
      resetPassBuffers();
      resetRawBuffers();
      activeProfile_ = command.profile;
      pollingSlaveAddress_ = command.slaveAddress;
      memset(values_, 0, sizeof(values_));
      memset(valid_, 0, sizeof(valid_));
      lastModbusError_ = 0xFF;
      blockUpdateSequence_ = 0;
      if (configureUart(UartFormat::Modbus8E1)) {
        poller_.begin(pollingSlaveAddress_, *activeProfile_, values_, valid_);
        mode_ = Mode::FixedPolling;
      } else {
        mode_ = Mode::Fault;
      }
      break;

    case CommandType::StartCalibration: {
      discovery_.cancel();
      poller_.cancel();
      calibration_.cancel();
      drainSerial();
      resetPassBuffers();
      resetRawBuffers();
      activeProfile_ = nullptr;
      pollingSlaveAddress_ = command.slaveAddress;
      memset(values_, 0, sizeof(values_));
      memset(valid_, 0, sizeof(valid_));
      lastModbusError_ = 0xFF;
      const CalibrationProfile *profile = command.calibrationProfile;
      if (configureUart(UartFormat::Modbus8E1) &&
          calibration_.begin(command.slaveAddress, *profile,
                             command.procedureIndex, command.inputValues,
                             command.inputCount)) {
        mode_ = Mode::Calibration;
      } else {
        mode_ = Mode::Fault;
      }
      break;
    }

    case CommandType::Stop: {
      discovery_.cancel();
      poller_.cancel();
      calibration_.cancel();
      drainSerial();
      activeProfile_ = nullptr;
      pollingSlaveAddress_ = 0;
      resetPassBuffers();
      resetRawBuffers();
      mode_ = configureUart(UartFormat::Modbus8E1)
                  ? Mode::Idle
                  : Mode::Fault;
      Event event;
      event.type = EventType::Stopped;
      emitEvent(event);
      xEventGroupSetBits(events_, kStoppedBit);
      break;
    }
  }
}

bool Rs485Service::configureUart(UartFormat format) {
  const uint32_t baud = format == UartFormat::HostCdc
                            ? passCoding_.baud : BoardConfig::kRs485Baudrate;
  const uint8_t parity = format == UartFormat::HostCdc
                            ? passCoding_.parity
                            : (format == UartFormat::Boot8N1 ? 0 : 2);
  if (format == UartFormat::HostCdc && !passCoding_.supported()) return false;
  if (uartStarted_ && uartFormat_ == format &&
      uartBaud_ == baud && uartParity_ == parity) return true;

  if (uartStarted_) {
    serial_.flush();
    drainSerial();
    serial_.end();
    delay(2);
  }

  const uint32_t serialConfig = parity == 0 ? SERIAL_8N1
                               : parity == 1 ? SERIAL_8O1 : SERIAL_8E1;
  // HardwareSerial also defaults to a small RX ring. The RS485 peer can send
  // a complete 1029-byte YMODEM packet continuously, so reserve enough space
  // before begin(); setRxBufferSize() is rejected while UART is running.
  serial_.setRxBufferSize(kRawResponseCapacity);
  serial_.begin(baud, serialConfig,
                BoardConfig::kRs485Rx, BoardConfig::kRs485Tx);
  if (!serial_) {
    uartStarted_ = false;
    return false;
  }
  uartStarted_ = true;
  uartBaud_ = baud;
  uartParity_ = parity;
  uartFormat_ = format;
  uartReconfigureCount_++;
  drainSerial();
  return true;
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
  if (!passCoding_.supported()) {
    drainSerial();
    return;
  }
  uint8_t frame[kMaxPassFrameLength];
  size_t length = xMessageBufferReceive(
      passRequestBuffer_, frame, sizeof(frame), 0);
  if (length > 0) {
    serial_.write(frame, length);
    serial_.flush();
  }

  uint8_t response[256];  // 增大: 64->256, 高流量时减少读取循环次数
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

void Rs485Service::updateRawPassThrough() {
  // Keep a chunk until HardwareSerial accepts every byte. This matters when a
  // 1029-byte YMODEM packet arrives from USB much faster than UART can send it.
  if (rawUartTxOffset_ >= rawUartTxLength_) {
    rawUartTxLength_ = xStreamBufferReceive(
        rawRequestBuffer_, rawUartTx_, sizeof(rawUartTx_), 0);
    rawUartTxOffset_ = 0;
  }
  if (rawUartTxOffset_ < rawUartTxLength_) {
    int writable = serial_.availableForWrite();
    if (writable > 0) {
      size_t remaining = rawUartTxLength_ - rawUartTxOffset_;
      size_t requested = min(remaining, static_cast<size_t>(writable));
      size_t written = serial_.write(rawUartTx_ + rawUartTxOffset_, requested);
      rawUartTxOffset_ += written;
    }
  }

  // Read only what the response stream can accept. The USB task drains this
  // independently and preserves its own short-write offset.
  uint8_t response[kRawUartChunkSize];
  size_t space = xStreamBufferSpacesAvailable(rawResponseBuffer_);
  size_t available = serial_.available();
  size_t requested = min(sizeof(response), min(space, available));
  size_t responseLength = 0;
  while (responseLength < requested && serial_.available()) {
    response[responseLength++] = static_cast<uint8_t>(serial_.read());
  }
  if (responseLength > 0) {
    size_t accepted =
        xStreamBufferSend(rawResponseBuffer_, response, responseLength, 0);
    size_t depth = xStreamBufferBytesAvailable(rawResponseBuffer_);
    portENTER_CRITICAL(&statusMux_);
    status_.rawUartToUsbDrops += responseLength - accepted;
    if (depth > status_.rawResponsePeak) {
      status_.rawResponsePeak = static_cast<uint16_t>(
          min(depth, static_cast<size_t>(UINT16_MAX)));
    }
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
    event.sourceMode = Mode::Discovery;
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
      event.sourceMode = mode_;
      event.error = lastModbusError_;
      emitEvent(event);
    }
  }

  if (poller_.takeScanCompleted()) {
    Event event;
    event.type = EventType::ScanCompleted;
    event.sourceMode = mode_;
    if (mode_ == Mode::FixedPolling) {
      event.identity = SensorDiscovery::Result{};
      event.identity.slaveAddress = pollingSlaveAddress_;
    } else {
      event.identity = discovery_.result();
    }
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

void Rs485Service::updateCalibration() {
  calibration_.update();
  CalibrationExecutor::Event calibrationEvent;
  if (!calibration_.takeEvent(calibrationEvent)) return;

  Event event;
  event.type = calibrationEvent.type == CalibrationExecutor::EventType::Finished
                   ? EventType::CalibrationFinished
                   : EventType::CalibrationProgress;
  event.sourceMode = Mode::Calibration;
  event.calibrationState = calibrationEvent.state;
  event.calibrationCommandIndex = calibrationEvent.commandIndex;
  event.calibrationCommandCount = calibrationEvent.commandCount;
  event.calibrationResultValue = calibrationEvent.resultValue;
  snprintf(event.calibrationMessage, sizeof(event.calibrationMessage), "%s",
           calibrationEvent.message);
  emitEvent(event);
}

void Rs485Service::drainSerial() {
  while (serial_.available()) serial_.read();
}

void Rs485Service::resetPassBuffers() {
  if (passRequestBuffer_ != nullptr) xMessageBufferReset(passRequestBuffer_);
  if (passResponseBuffer_ != nullptr) xStreamBufferReset(passResponseBuffer_);
}

void Rs485Service::resetRawBuffers() {
  if (rawRequestBuffer_ != nullptr) xStreamBufferReset(rawRequestBuffer_);
  if (rawResponseBuffer_ != nullptr) xStreamBufferReset(rawResponseBuffer_);
  rawUartTxLength_ = 0;
  rawUartTxOffset_ = 0;
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
  next.calibrationState = calibration_.state();
  next.calibrationCommandIndex = calibration_.commandIndex();
  next.calibrationCommandCount = calibration_.commandCount();
  snprintf(next.calibrationMessage, sizeof(next.calibrationMessage), "%s",
           calibration_.message());

  portENTER_CRITICAL(&statusMux_);
  next.commandQueuePeak = status_.commandQueuePeak;
  next.eventQueuePeak = status_.eventQueuePeak;
  next.droppedEvents = status_.droppedEvents;
  next.passRequestDrops = status_.passRequestDrops;
  next.passResponseDrops = status_.passResponseDrops;
  next.rawUsbToUartBytes = status_.rawUsbToUartBytes;
  next.rawUartToUsbBytes = status_.rawUartToUsbBytes;
  next.rawUsbToUartDrops = status_.rawUsbToUartDrops;
  next.rawUartToUsbDrops = status_.rawUartToUsbDrops;
  next.rawRequestPeak = status_.rawRequestPeak;
  next.rawResponsePeak = status_.rawResponsePeak;
  next.uartFormat = uartFormat_;
  next.uartReconfigureCount = uartReconfigureCount_;
  next.uartBaud = uartBaud_;
  next.uartParity = uartParity_;
  next.passCodingRevision = passCodingRevision_;
  next.passCodingSupported = passCoding_.supported();
  status_ = next;
  portEXIT_CRITICAL(&statusMux_);
}
