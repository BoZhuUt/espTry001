#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "data_logger.h"
#include "log_export_server.h"
#include "sd_storage.h"
#include "sensor_profile_store.h"

// Fixed snapshot copied at the boundary between the application loop and the
// storage task. The queue never contains pointers to poller-owned value arrays.
struct SensorSnapshot {
  uint32_t scanSequence = 0;
  float current1Ma = 0.0f;
  float current2Ma = 0.0f;
  uint8_t fieldCount = 0;
  float values[kMaxSensorFields] = {0};
  bool valid[kMaxSensorFields] = {false};
};

// Sole execution owner of SdStorage, DataLogger and LogExportServer.
// The application sends fixed-size commands/snapshots and reads a copied
// diagnostic status; it never performs an SD filesystem operation directly.
class StorageService {
 public:
  static constexpr UBaseType_t kCommandQueueLength = 6;
  static constexpr UBaseType_t kSnapshotQueueLength = 4;
  static constexpr uint32_t kStopTimeoutMs = 3000;

  struct Status {
    SdStorage::State storageState = SdStorage::State::NotInitialized;
    char storageStateText[16] = "Missing";
    uint64_t storageFreeBytes = 0;
    char storageError[64] = "";

    bool recording = false;
    uint32_t logRecordCount = 0;
    char currentLogFile[64] = "";
    char loggerError[64] = "";

    LogExportServer::State exportState = LogExportServer::State::Idle;
    char exportStatus[64] = "Idle";
    char exportFile[LogExportServer::kMaxPathLen] = "";
    uint32_t exportOffset = 0;
    uint32_t exportTotal = 0;
    char exportError[96] = "";
    bool exportShouldExit = false;

    uint8_t snapshotQueueDepth = 0;
    uint8_t snapshotQueuePeak = 0;
    uint32_t droppedSnapshots = 0;
    uint32_t commandFailures = 0;
    UBaseType_t taskStackHighWater = 0;
  };

  StorageService(SdStorage &sd, DataLogger &logger,
                 LogExportServer &logExportServer,
                 SensorProfileStore &profileStore, Stream &usb);

  void setMetadata(const char *configVersion, const char *firmwareVersion);

  // Creates StorageTask and waits for its one-time SD mount attempt.
  bool begin(uint32_t timeoutMs = 5000);

  bool startLogging(uint8_t slaveAddress, uint32_t productNum,
                    const char *profileName, const SensorProfile *profile);
  bool enqueueSnapshot(uint32_t scanSequence, const float *values,
                       const bool *valid, uint8_t fieldCount,
                       float current1Ma, float current2Ma);

  // Stop is an acknowledged resource handoff: success means buffered CSV data
  // has been flushed and the active file handle has been closed.
  bool stopLoggingAndWait(uint32_t timeoutMs = kStopTimeoutMs);

  bool beginLogExport(const char *firmwareVersion);
  bool cancelLogExportAndWait(uint32_t timeoutMs = kStopTimeoutMs);

  void getStatus(Status &out) const;
  bool isRecording() const;
  bool exportShouldExit() const;

 private:
  enum class CommandType : uint8_t {
    StartLogging,
    StopLogging,
    BeginLogExport,
    CancelLogExport,
  };

  struct Command {
    CommandType type = CommandType::StopLogging;
    uint8_t slaveAddress = 0;
    uint32_t productNum = 0;
    char profileName[DataLogger::kMaxProfileNameLen + 1] = {0};
    const SensorProfile *profile = nullptr;
    char firmwareVersion[32] = {0};
  };

  static constexpr EventBits_t kInitializedBit = BIT0;
  static constexpr EventBits_t kStoppedBit = BIT1;
  static constexpr EventBits_t kExportCancelledBit = BIT2;

  static void taskEntry(void *context);
  void taskLoop();
  void handleCommand(const Command &command);
  bool sendCommand(const Command &command);
  void publishStatus(bool force = false);
  void noteCommandFailure();

  SdStorage &sd_;
  DataLogger &logger_;
  LogExportServer &logExportServer_;
  SensorProfileStore &profileStore_;
  Stream &usb_;

  QueueHandle_t commandQueue_ = nullptr;
  QueueHandle_t snapshotQueue_ = nullptr;
  EventGroupHandle_t events_ = nullptr;
  TaskHandle_t taskHandle_ = nullptr;
  bool exportActive_ = false;
  uint32_t lastStatusPublishMs_ = 0;

  char configVersion_[32] = "unknown";
  char firmwareVersion_[32] = "unknown";

  mutable portMUX_TYPE statusMux_ = portMUX_INITIALIZER_UNLOCKED;
  Status status_;
};
