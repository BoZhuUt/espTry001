#include "services/storage_service.h"

#include <string.h>

#include "debug_log.h"

StorageService::StorageService(SdStorage &sd, DataLogger &logger,
                               LogExportServer &logExportServer,
                               SensorProfileStore &profileStore, Stream &usb)
    : sd_(sd),
      logger_(logger),
      logExportServer_(logExportServer),
      profileStore_(profileStore),
      usb_(usb) {}

void StorageService::setMetadata(const char *configVersion,
                                 const char *firmwareVersion) {
  if (taskHandle_ != nullptr) return;
  snprintf(configVersion_, sizeof(configVersion_), "%s",
           configVersion != nullptr ? configVersion : "unknown");
  snprintf(firmwareVersion_, sizeof(firmwareVersion_), "%s",
           firmwareVersion != nullptr ? firmwareVersion : "unknown");
  logger_.setMetadata(configVersion_, firmwareVersion_);
}

bool StorageService::begin(uint32_t timeoutMs) {
  if (taskHandle_ != nullptr) return true;

  commandQueue_ = xQueueCreate(kCommandQueueLength, sizeof(Command));
  snapshotQueue_ = xQueueCreate(kSnapshotQueueLength, sizeof(SensorSnapshot));
  events_ = xEventGroupCreate();
  if (commandQueue_ == nullptr || snapshotQueue_ == nullptr ||
      events_ == nullptr) {
    noteCommandFailure();
    return false;
  }

  BaseType_t created =
      xTaskCreate(taskEntry, "StorageTask", 8192, this, 2, &taskHandle_);
  if (created != pdPASS) {
    taskHandle_ = nullptr;
    noteCommandFailure();
    return false;
  }

  EventBits_t bits = xEventGroupWaitBits(
      events_, kInitializedBit, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeoutMs));
  return (bits & kInitializedBit) != 0;
}

bool StorageService::startLogging(uint8_t slaveAddress, uint32_t productNum,
                                  const char *profileName,
                                  const SensorProfile *profile) {
  if (profile == nullptr) return false;
  Command command;
  command.type = CommandType::StartLogging;
  command.slaveAddress = slaveAddress;
  command.productNum = productNum;
  command.profile = profile;
  snprintf(command.profileName, sizeof(command.profileName), "%s",
           profileName != nullptr ? profileName : "?");
  return sendCommand(command);
}

bool StorageService::enqueueSnapshot(uint32_t scanSequence,
                                     uint32_t uptimeMs,
                                     const char *timestamp,
                                     const float *values, const bool *valid,
                                     uint8_t fieldCount, float current1Ma,
                                     float current2Ma) {
  if (snapshotQueue_ == nullptr || values == nullptr || valid == nullptr) {
    return false;
  }

  SensorSnapshot snapshot;
  snapshot.scanSequence = scanSequence;
  snapshot.uptimeMs = uptimeMs;
  snapshot.current1Ma = current1Ma;
  snapshot.current2Ma = current2Ma;
  snapshot.fieldCount =
      fieldCount <= kMaxSensorFields ? fieldCount : kMaxSensorFields;
  memcpy(snapshot.values, values,
         snapshot.fieldCount * sizeof(snapshot.values[0]));
  memcpy(snapshot.valid, valid,
         snapshot.fieldCount * sizeof(snapshot.valid[0]));
  snprintf(snapshot.timestamp, sizeof(snapshot.timestamp), "%s",
           timestamp != nullptr ? timestamp : "");

  if (xQueueSend(snapshotQueue_, &snapshot, 0) != pdPASS) {
    portENTER_CRITICAL(&statusMux_);
    status_.droppedSnapshots++;
    portEXIT_CRITICAL(&statusMux_);
    return false;
  }

  UBaseType_t depth = uxQueueMessagesWaiting(snapshotQueue_);
  portENTER_CRITICAL(&statusMux_);
  if (depth > status_.snapshotQueuePeak) {
    status_.snapshotQueuePeak =
        depth > UINT8_MAX ? UINT8_MAX : static_cast<uint8_t>(depth);
  }
  portEXIT_CRITICAL(&statusMux_);
  return true;
}

bool StorageService::stopLoggingAndWait(uint32_t timeoutMs) {
  if (events_ == nullptr) return false;
  xEventGroupClearBits(events_, kStoppedBit);
  Command command;
  command.type = CommandType::StopLogging;
  if (!sendCommand(command)) return false;
  EventBits_t bits = xEventGroupWaitBits(
      events_, kStoppedBit, pdTRUE, pdTRUE, pdMS_TO_TICKS(timeoutMs));
  return (bits & kStoppedBit) != 0;
}

bool StorageService::beginLogExport(const char *firmwareVersion) {
  Command command;
  command.type = CommandType::BeginLogExport;
  snprintf(command.firmwareVersion, sizeof(command.firmwareVersion), "%s",
           firmwareVersion != nullptr ? firmwareVersion : firmwareVersion_);
  return sendCommand(command);
}

bool StorageService::cancelLogExportAndWait(uint32_t timeoutMs) {
  if (events_ == nullptr) return false;
  xEventGroupClearBits(events_, kExportCancelledBit);
  Command command;
  command.type = CommandType::CancelLogExport;
  if (!sendCommand(command)) return false;
  EventBits_t bits = xEventGroupWaitBits(
      events_, kExportCancelledBit, pdTRUE, pdTRUE, pdMS_TO_TICKS(timeoutMs));
  return (bits & kExportCancelledBit) != 0;
}

void StorageService::getStatus(Status &out) const {
  portENTER_CRITICAL(&statusMux_);
  out = status_;
  portEXIT_CRITICAL(&statusMux_);
}

bool StorageService::isRecording() const {
  Status status;
  getStatus(status);
  return status.recording;
}

bool StorageService::exportShouldExit() const {
  Status status;
  getStatus(status);
  return status.exportShouldExit;
}

void StorageService::taskEntry(void *context) {
  static_cast<StorageService *>(context)->taskLoop();
}

void StorageService::taskLoop() {
  sd_.begin();
  publishStatus(true);
  xEventGroupSetBits(events_, kInitializedBit | kStoppedBit);

  for (;;) {
    Command command;
    bool handledCommand =
        xQueueReceive(commandQueue_, &command, pdMS_TO_TICKS(5)) == pdPASS;
    if (handledCommand) {
      handleCommand(command);
    }

    if (exportActive_) {
      logExportServer_.update(usb_);
    } else {
      SensorSnapshot snapshot;
      if (xQueueReceive(snapshotQueue_, &snapshot, 0) == pdPASS) {
        logger_.onScanComplete(
            snapshot.scanSequence, snapshot.uptimeMs, snapshot.timestamp,
            snapshot.values, snapshot.valid, snapshot.fieldCount,
            snapshot.current1Ma, snapshot.current2Ma);
      }
      logger_.update();
    }
    publishStatus(handledCommand);
  }
}

void StorageService::handleCommand(const Command &command) {
  switch (command.type) {
    case CommandType::StartLogging:
      if (exportActive_) {
        logExportServer_.cancel();
        exportActive_ = false;
      }
      xQueueReset(snapshotQueue_);
      logger_.setIdentity(command.slaveAddress, command.productNum,
                          command.profileName, command.profile);
      logger_.start();
      break;

    case CommandType::StopLogging:
      logger_.stop();
      xQueueReset(snapshotQueue_);
      xEventGroupSetBits(events_, kStoppedBit);
      break;

    case CommandType::BeginLogExport:
      logger_.stop();
      xQueueReset(snapshotQueue_);
      logExportServer_.begin(sd_, profileStore_, logger_,
                             command.firmwareVersion);
      exportActive_ = true;
      break;

    case CommandType::CancelLogExport:
      logExportServer_.cancel();
      exportActive_ = false;
      xEventGroupSetBits(events_, kExportCancelledBit);
      break;
  }
}

bool StorageService::sendCommand(const Command &command) {
  if (commandQueue_ == nullptr ||
      xQueueSend(commandQueue_, &command, 0) != pdPASS) {
    noteCommandFailure();
    return false;
  }
  return true;
}

void StorageService::publishStatus(bool force) {
  uint32_t now = millis();
  if (!force && (uint32_t)(now - lastStatusPublishMs_) < 100) return;
  lastStatusPublishMs_ = now;

  Status next;
  next.storageState = sd_.state();
  snprintf(next.storageStateText, sizeof(next.storageStateText), "%s",
           sd_.stateText());
  next.storageFreeBytes = sd_.freeBytes();
  snprintf(next.storageError, sizeof(next.storageError), "%s",
           sd_.lastError());

  next.recording = logger_.isRecording();
  next.logRecordCount = logger_.recordCount();
  next.lastSuccessfulFlushMs = logger_.lastSuccessfulFlushMs();
  snprintf(next.currentLogFile, sizeof(next.currentLogFile), "%s",
           logger_.currentFileName());
  snprintf(next.loggerError, sizeof(next.loggerError), "%s",
           logger_.lastError());

  next.exportState = logExportServer_.state();
  snprintf(next.exportStatus, sizeof(next.exportStatus), "%s",
           logExportServer_.statusText());
  snprintf(next.exportFile, sizeof(next.exportFile), "%s",
           logExportServer_.currentFile());
  next.exportOffset = logExportServer_.transferOffset();
  next.exportTotal = logExportServer_.transferTotal();
  snprintf(next.exportError, sizeof(next.exportError), "%s",
           logExportServer_.errorText());
  next.exportShouldExit = logExportServer_.shouldExit();

  UBaseType_t depth = uxQueueMessagesWaiting(snapshotQueue_);
  next.snapshotQueueDepth =
      depth > UINT8_MAX ? UINT8_MAX : static_cast<uint8_t>(depth);
  next.taskStackHighWater = uxTaskGetStackHighWaterMark(nullptr);

  portENTER_CRITICAL(&statusMux_);
  next.snapshotQueuePeak = status_.snapshotQueuePeak;
  next.droppedSnapshots = status_.droppedSnapshots;
  next.commandFailures = status_.commandFailures;
  status_ = next;
  portEXIT_CRITICAL(&statusMux_);
}

void StorageService::noteCommandFailure() {
  portENTER_CRITICAL(&statusMux_);
  status_.commandFailures++;
  portEXIT_CRITICAL(&statusMux_);
}
