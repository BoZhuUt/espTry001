#pragma once

#include <Arduino.h>

enum class CalibrationValueType : uint8_t {
  UInt16,
  Float32CDAB,
};

enum class CalibrationValueSource : uint8_t {
  Fixed,
  Input,
};

enum class CalibrationOutcome : uint8_t {
  Success,
  Failure,
  Pending,
  Info,
};

struct CalibrationInput {
  char id[24] = "";
  char label[40] = "";
  char unit[16] = "";
  CalibrationValueType type = CalibrationValueType::UInt16;
  float defaultValue = 0.0f;
  float minimum = 0.0f;
  float maximum = 0.0f;
  bool minimumInclusive = true;
  bool maximumInclusive = true;
  uint8_t decimals = 0;
  float scaleNumerator = 1.0f;
  float scaleDenominator = 1.0f;
};

struct CalibrationResultMapping {
  uint16_t value = 0;
  CalibrationOutcome outcome = CalibrationOutcome::Info;
  char message[96] = "";
};

struct CalibrationResult {
  bool enabled = false;
  uint32_t address1Based = 0;
  uint32_t pollIntervalMs = 0;
  uint32_t timeoutMs = 0;
  char communicationErrorMessage[96] = "";
  CalibrationResultMapping mappings[8];
  uint8_t mappingCount = 0;
};

struct CalibrationCommand {
  uint32_t address1Based = 0;
  CalibrationValueType type = CalibrationValueType::UInt16;
  CalibrationValueSource valueSource = CalibrationValueSource::Fixed;
  float fixedValue = 0.0f;
  uint8_t inputIndex = 0;
  uint32_t waitAfterMs = 0;
  CalibrationResult result;
};

struct CalibrationProcedure {
  char id[40] = "";
  char readingName[40] = "";
  char name[48] = "";
  char message[128] = "";
  char unit[16] = "";
  uint8_t firstInput = 0;
  uint8_t inputCount = 0;
  uint8_t firstCommand = 0;
  uint8_t commandCount = 0;
};

struct CalibrationProfile {
  char profileId[40] = "";
  uint32_t productNum = 0;
  char variant[24] = "";
  char modelName[40] = "";
  CalibrationProcedure procedures[16];
  CalibrationInput inputs[48];
  CalibrationCommand commands[32];
  uint8_t procedureCount = 0;
  uint8_t inputCount = 0;
  uint8_t commandCount = 0;
};

// Loads only the approved calibration profile for the currently discovered PN.
// LittleFS must already be mounted by SensorProfileStore during setup().
class CalibrationProfileStore {
 public:
  static constexpr size_t kMaxIndexFileSize = 16 * 1024;
  static constexpr size_t kMaxProfileFileSize = 64 * 1024;

  bool loadForProduct(uint32_t productNum,
                      const char *indexPath = "/calibration/index.json");
  void clear();

  const CalibrationProfile *profile() const {
    return active_ ? &profile_ : nullptr;
  }
  const char *lastError() const { return lastError_; }

 private:
  bool loadProfileFile(const char *path, uint32_t expectedProductNum);
  bool fail(const char *message);

  CalibrationProfile profile_{};
  bool active_ = false;
  char lastError_[96] = "Not loaded";
};
