#include "calibration_profile_store.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <math.h>
#include <string.h>

namespace {

constexpr uint8_t kSupportedSchemaVersion = 1;
constexpr uint32_t kMaxWaitAfterMs = 10UL * 60UL * 1000UL;
constexpr uint32_t kMaxResultTimeoutMs = 10UL * 60UL * 1000UL;

void copyText(char *destination, size_t capacity, const char *source) {
  if (capacity == 0) return;
  snprintf(destination, capacity, "%s", source != nullptr ? source : "");
}

bool readRequiredText(JsonVariantConst value, char *destination,
                      size_t capacity) {
  const char *text = value.as<const char *>();
  if (text == nullptr || text[0] == '\0' || strlen(text) >= capacity) {
    return false;
  }
  copyText(destination, capacity, text);
  return true;
}

bool readOptionalText(JsonVariantConst value, char *destination,
                      size_t capacity) {
  if (value.isNull()) {
    destination[0] = '\0';
    return true;
  }
  const char *text = value.as<const char *>();
  if (text == nullptr || strlen(text) >= capacity) return false;
  copyText(destination, capacity, text);
  return true;
}

bool readNumber(JsonVariantConst value, float &result) {
  if (!value.is<float>()) return false;
  result = value.as<float>();
  return isfinite(result);
}

bool parseValueType(const char *text, CalibrationValueType &type) {
  if (text != nullptr && strcmp(text, "uint16") == 0) {
    type = CalibrationValueType::UInt16;
    return true;
  }
  if (text != nullptr && strcmp(text, "float32") == 0) {
    type = CalibrationValueType::Float32CDAB;
    return true;
  }
  if (text != nullptr && strcmp(text, "float32_cdab") == 0) {
    type = CalibrationValueType::Float32CDAB;
    return true;
  }
  return false;
}

bool parseOutcome(const char *text, CalibrationOutcome &outcome) {
  if (text != nullptr && strcmp(text, "success") == 0) {
    outcome = CalibrationOutcome::Success;
  } else if (text != nullptr && strcmp(text, "failure") == 0) {
    outcome = CalibrationOutcome::Failure;
  } else if (text != nullptr && strcmp(text, "pending") == 0) {
    outcome = CalibrationOutcome::Pending;
  } else if (text != nullptr && strcmp(text, "info") == 0) {
    outcome = CalibrationOutcome::Info;
  } else {
    return false;
  }
  return true;
}

bool parseProcedureCategory(const char *text,
                            CalibrationProcedureCategory &category) {
  if (text != nullptr && strcmp(text, "sensor_calibration") == 0) {
    category = CalibrationProcedureCategory::SensorCalibration;
    return true;
  }
  if (text != nullptr && strcmp(text, "output_span") == 0) {
    category = CalibrationProcedureCategory::OutputSpan;
    return true;
  }
  return false;
}

bool validAddress(uint32_t address, uint8_t registerCount) {
  return address >= 1 && address <= 65536 &&
         address + registerCount - 1 <= 65536;
}

}  // namespace

void CalibrationProfileStore::clear() {
  // Do not assign CalibrationProfile{} here. The compiler materializes that
  // several-kilobyte aggregate on loopTask's stack and trips its stack canary
  // when a discovered PN loads a calibration profile.
  memset(&profile_, 0, sizeof(profile_));
  active_ = false;
}

bool CalibrationProfileStore::loadForProduct(uint32_t productNum,
                                             const char *indexPath) {
  clear();
  if (productNum == 0 || indexPath == nullptr || indexPath[0] == '\0') {
    return fail("Calibration lookup invalid");
  }

  // The hot updater commits the index last. If power is lost in the short
  // interval after the old index was renamed, restore its backup before the
  // first post-boot lookup.
  if (strcmp(indexPath, "/calibration/index.json") == 0 &&
      !LittleFS.exists(indexPath) &&
      LittleFS.exists("/calibration/index.backup")) {
    LittleFS.rename("/calibration/index.backup", indexPath);
  }

  File file = LittleFS.open(indexPath, "r");
  if (!file) return fail("Calibration index missing");
  if (file.size() == 0 || file.size() > kMaxIndexFileSize) {
    file.close();
    return fail("Calibration index size invalid");
  }

  JsonDocument document;
  DeserializationError jsonError = deserializeJson(document, file);
  file.close();
  if (jsonError) return fail(jsonError.c_str());
  if (!document["schema_version"].is<uint8_t>() ||
      document["schema_version"].as<uint8_t>() != kSupportedSchemaVersion) {
    return fail("Calibration index schema unsupported");
  }

  JsonArrayConst profiles = document["profiles"].as<JsonArrayConst>();
  if (profiles.isNull()) return fail("Calibration index profiles invalid");

  char selectedPath[80] = "";
  uint8_t matches = 0;
  for (JsonObjectConst candidate : profiles) {
    if (!candidate["product_num"].is<uint32_t>() ||
        candidate["product_num"].as<uint32_t>() != productNum) {
      continue;
    }
    // Version/match selection is intentionally rejected until identity version
    // comparison is implemented. Never guess between calibration variants.
    if (!candidate["match"].isNull() ||
        candidate["requires_version_read"].as<bool>()) {
      return fail("Calibration version match unsupported");
    }
    const char *path = candidate["file"].as<const char *>();
    if (path == nullptr || path[0] != '/' || strlen(path) >= sizeof(selectedPath)) {
      return fail("Calibration profile path invalid");
    }
    copyText(selectedPath, sizeof(selectedPath), path);
    matches++;
  }

  if (matches == 0) return fail("Calibration profile not found");
  if (matches != 1) return fail("Calibration profile ambiguous");
  return loadProfileFile(selectedPath, productNum);
}

bool CalibrationProfileStore::validateBundle(const char *indexPath) {
  clear();
  if (indexPath == nullptr || indexPath[0] == '\0') {
    return fail("Calibration index path invalid");
  }
  File file = LittleFS.open(indexPath, "r");
  if (!file) return fail("Calibration index missing");
  if (file.size() == 0 || file.size() > kMaxIndexFileSize) {
    file.close();
    return fail("Calibration index size invalid");
  }

  JsonDocument document;
  DeserializationError jsonError = deserializeJson(document, file);
  file.close();
  if (jsonError) return fail(jsonError.c_str());
  if (!document["schema_version"].is<uint8_t>() ||
      document["schema_version"].as<uint8_t>() != kSupportedSchemaVersion) {
    return fail("Calibration index schema unsupported");
  }
  JsonArrayConst profiles = document["profiles"].as<JsonArrayConst>();
  if (profiles.isNull() || profiles.size() == 0 ||
      profiles.size() > kMaxProfiles) {
    return fail("Calibration index profiles invalid");
  }

  uint32_t productNumbers[kMaxProfiles] = {0};
  uint8_t profileCount = 0;
  for (JsonObjectConst candidate : profiles) {
    if (!candidate["product_num"].is<uint32_t>()) {
      return fail("Calibration index PN invalid");
    }
    uint32_t productNum = candidate["product_num"].as<uint32_t>();
    if (productNum == 0) return fail("Calibration index PN invalid");
    for (uint8_t previous = 0; previous < profileCount; ++previous) {
      if (productNumbers[previous] == productNum) {
        return fail("Calibration index duplicate PN");
      }
    }
    productNumbers[profileCount++] = productNum;
    if (!candidate["match"].isNull() ||
        candidate["requires_version_read"].as<bool>()) {
      return fail("Calibration version match unsupported");
    }
    const char *path = candidate["file"].as<const char *>();
    if (path == nullptr || strncmp(path, "/calibration/", 13) != 0 ||
        strchr(path + 13, '/') != nullptr || strlen(path) >= 80) {
      return fail("Calibration profile path invalid");
    }
    if (!loadProfileFile(path, productNum)) return false;
  }

  clear();
  lastError_[0] = '\0';
  return true;
}

bool CalibrationProfileStore::loadProfileFile(const char *path,
                                               uint32_t expectedProductNum) {
  File file = LittleFS.open(path, "r");
  if (!file) return fail("Calibration profile missing");
  if (file.size() == 0 || file.size() > kMaxProfileFileSize) {
    file.close();
    return fail("Calibration profile size invalid");
  }

  JsonDocument document;
  DeserializationError jsonError = deserializeJson(document, file);
  file.close();
  if (jsonError) return fail(jsonError.c_str());
  if (!document["schema_version"].is<uint8_t>() ||
      document["schema_version"].as<uint8_t>() != kSupportedSchemaVersion) {
    return fail("Calibration profile schema unsupported");
  }
  if (!document["product_num"].is<uint32_t>() ||
      document["product_num"].as<uint32_t>() != expectedProductNum) {
    return fail("Calibration profile PN mismatch");
  }
  const char *releaseStatus = document["release_status"].as<const char *>();
  if (releaseStatus == nullptr || strcmp(releaseStatus, "approved") != 0) {
    return fail("Calibration profile not approved");
  }
  if (document["requires_version_read"].as<bool>()) {
    return fail("Calibration version read required");
  }

  // Parse directly into the member buffer. A local CalibrationProfile would
  // exceed loopTask's remaining stack on ESP32-S3. active_ stays false until
  // every field has passed validation, and fail() clears partial content.
  CalibrationProfile &parsed = profile_;
  memset(&parsed, 0, sizeof(parsed));
  parsed.productNum = expectedProductNum;
  if (!readRequiredText(document["profile_id"], parsed.profileId,
                        sizeof(parsed.profileId)) ||
      !readRequiredText(document["variant"], parsed.variant,
                        sizeof(parsed.variant)) ||
      !readRequiredText(document["model_name"], parsed.modelName,
                        sizeof(parsed.modelName))) {
    return fail("Calibration profile identity invalid");
  }

  JsonArrayConst procedures = document["procedures"].as<JsonArrayConst>();
  if (procedures.isNull() || procedures.size() == 0 ||
      procedures.size() >
          sizeof(parsed.procedures) / sizeof(parsed.procedures[0])) {
    return fail("Calibration procedures count invalid");
  }

  for (JsonObjectConst procedureJson : procedures) {
    if (!procedureJson["when"].isNull()) {
      return fail("Calibration procedure condition unsupported");
    }
    CalibrationProcedure &procedure =
        parsed.procedures[parsed.procedureCount];
    const char *categoryText = procedureJson["category"].as<const char *>();
    // Profiles published before category support contain only sensor
    // calibration procedures. Keep those profiles usable after an
    // application-only firmware update; output_span still requires a new
    // LittleFS bundle.
    if (categoryText == nullptr) {
      procedure.category = CalibrationProcedureCategory::SensorCalibration;
    } else if (!parseProcedureCategory(categoryText, procedure.category)) {
      return fail("Calibration procedure category invalid");
    }
    if (!readRequiredText(procedureJson["id"], procedure.id,
                          sizeof(procedure.id)) ||
        !readRequiredText(procedureJson["reading_name"], procedure.readingName,
                          sizeof(procedure.readingName)) ||
        !readRequiredText(procedureJson["name"], procedure.name,
                          sizeof(procedure.name)) ||
        !readRequiredText(procedureJson["message"], procedure.message,
                          sizeof(procedure.message)) ||
        !readOptionalText(procedureJson["unit"], procedure.unit,
                          sizeof(procedure.unit))) {
      return fail("Calibration procedure text invalid");
    }

    procedure.firstInput = parsed.inputCount;
    JsonArrayConst inputs = procedureJson["inputs"].as<JsonArrayConst>();
    if (inputs.isNull() || parsed.inputCount + inputs.size() >
                               sizeof(parsed.inputs) / sizeof(parsed.inputs[0])) {
      return fail("Calibration inputs count invalid");
    }
    for (JsonObjectConst inputJson : inputs) {
      CalibrationInput &input = parsed.inputs[parsed.inputCount];
      const char *typeText = inputJson["type"].as<const char *>();
      if (!readRequiredText(inputJson["id"], input.id, sizeof(input.id)) ||
          !readRequiredText(inputJson["label"], input.label,
                            sizeof(input.label)) ||
          !readOptionalText(inputJson["unit"], input.unit,
                            sizeof(input.unit)) ||
          !parseValueType(typeText, input.type) ||
          !readNumber(inputJson["default"], input.defaultValue) ||
          !readNumber(inputJson["minimum"], input.minimum) ||
          !readNumber(inputJson["maximum"], input.maximum) ||
          input.minimum > input.maximum ||
          !inputJson["decimals"].is<uint8_t>() ||
          inputJson["decimals"].as<uint8_t>() > 6) {
        return fail("Calibration input invalid");
      }
      input.decimals = inputJson["decimals"].as<uint8_t>();
      input.minimumInclusive = inputJson["minimum_inclusive"] | true;
      input.maximumInclusive = inputJson["maximum_inclusive"] | true;
      JsonObjectConst scale = inputJson["write_scale"].as<JsonObjectConst>();
      if (scale.isNull() ||
          !readNumber(scale["numerator"], input.scaleNumerator) ||
          !readNumber(scale["denominator"], input.scaleDenominator) ||
          input.scaleDenominator == 0.0f) {
        return fail("Calibration input scale invalid");
      }
      bool defaultBelow = input.minimumInclusive
                              ? input.defaultValue < input.minimum
                              : input.defaultValue <= input.minimum;
      bool defaultAbove = input.maximumInclusive
                              ? input.defaultValue > input.maximum
                              : input.defaultValue >= input.maximum;
      if (defaultBelow || defaultAbove) {
        return fail("Calibration input default out of range");
      }
      parsed.inputCount++;
      procedure.inputCount++;
    }

    procedure.firstCommand = parsed.commandCount;
    JsonArrayConst steps = procedureJson["steps"].as<JsonArrayConst>();
    if (steps.isNull() || steps.size() == 0 ||
        parsed.commandCount + steps.size() >
            sizeof(parsed.commands) / sizeof(parsed.commands[0])) {
      return fail("Calibration steps count invalid");
    }
    for (JsonObjectConst stepJson : steps) {
      if (!stepJson["when"].isNull()) {
        return fail("Calibration step condition unsupported");
      }
      JsonArrayConst alternatives = stepJson["alternatives"].as<JsonArrayConst>();
      if (alternatives.isNull() || alternatives.size() != 1) {
        return fail("Calibration alternatives unsupported");
      }
      JsonObjectConst alternative = alternatives[0].as<JsonObjectConst>();
      if (alternative.isNull() || !alternative["when"].isNull()) {
        return fail("Calibration alternative condition unsupported");
      }

      CalibrationCommand &command = parsed.commands[parsed.commandCount];
      JsonObjectConst write = alternative["write"].as<JsonObjectConst>();
      const char *writeType = write["type"].as<const char *>();
      if (write.isNull() || !write["address"].is<uint32_t>() ||
          !parseValueType(writeType, command.type)) {
        return fail("Calibration write invalid");
      }
      command.address1Based = write["address"].as<uint32_t>();
      uint8_t writeRegisterCount =
          command.type == CalibrationValueType::UInt16 ? 1 : 2;
      if (!validAddress(command.address1Based, writeRegisterCount)) {
        return fail("Calibration write address invalid");
      }

      JsonObjectConst value = write["value"].as<JsonObjectConst>();
      const char *kind = value["kind"].as<const char *>();
      if (kind != nullptr && strcmp(kind, "fixed") == 0) {
        command.valueSource = CalibrationValueSource::Fixed;
        if (!readNumber(value["number"], command.fixedValue)) {
          return fail("Calibration fixed value invalid");
        }
      } else if (kind != nullptr && strcmp(kind, "input") == 0) {
        command.valueSource = CalibrationValueSource::Input;
        const char *inputId = value["input_id"].as<const char *>();
        bool found = false;
        for (uint8_t index = 0; index < procedure.inputCount; ++index) {
          uint8_t absoluteIndex = procedure.firstInput + index;
          if (inputId != nullptr &&
              strcmp(parsed.inputs[absoluteIndex].id, inputId) == 0) {
            command.inputIndex = absoluteIndex;
            found = true;
            break;
          }
        }
        if (!found) return fail("Calibration input reference invalid");
      } else {
        return fail("Calibration value kind invalid");
      }

      if (!alternative["wait_after_ms"].is<uint32_t>() ||
          alternative["wait_after_ms"].as<uint32_t>() > kMaxWaitAfterMs) {
        return fail("Calibration wait invalid");
      }
      command.waitAfterMs = alternative["wait_after_ms"].as<uint32_t>();

      JsonVariantConst resultJson = alternative["result"];
      if (!resultJson.isNull()) {
        JsonObjectConst result = resultJson.as<JsonObjectConst>();
        const char *resultType = result["type"].as<const char *>();
        if (result.isNull() || !result["address"].is<uint32_t>() ||
            !validAddress(result["address"].as<uint32_t>(), 1) ||
            resultType == nullptr || strcmp(resultType, "uint16") != 0 ||
            !result["register_count"].is<uint8_t>() ||
            result["register_count"].as<uint8_t>() != 1 ||
            !result["poll_interval_ms"].is<uint32_t>() ||
            result["poll_interval_ms"].as<uint32_t>() == 0 ||
            !result["timeout_ms"].is<uint32_t>() ||
            result["timeout_ms"].as<uint32_t>() == 0 ||
            result["timeout_ms"].as<uint32_t>() > kMaxResultTimeoutMs) {
          return fail("Calibration result definition invalid");
        }
        command.result.enabled = true;
        command.result.address1Based = result["address"].as<uint32_t>();
        command.result.pollIntervalMs = result["poll_interval_ms"].as<uint32_t>();
        command.result.timeoutMs = result["timeout_ms"].as<uint32_t>();
        if (!readOptionalText(result["communication_error_message"],
                              command.result.communicationErrorMessage,
                              sizeof(command.result.communicationErrorMessage))) {
          return fail("Calibration result error text invalid");
        }
        JsonArrayConst mappings = result["mappings"].as<JsonArrayConst>();
        if (mappings.isNull() || mappings.size() == 0 ||
            mappings.size() > sizeof(command.result.mappings) /
                                  sizeof(command.result.mappings[0])) {
          return fail("Calibration result mappings invalid");
        }
        for (JsonObjectConst mappingJson : mappings) {
          CalibrationResultMapping &mapping =
              command.result.mappings[command.result.mappingCount];
          if (!mappingJson["value"].is<uint16_t>() ||
              !parseOutcome(mappingJson["outcome"].as<const char *>(),
                            mapping.outcome) ||
              !readRequiredText(mappingJson["message"], mapping.message,
                                sizeof(mapping.message))) {
            return fail("Calibration result mapping invalid");
          }
          mapping.value = mappingJson["value"].as<uint16_t>();
          for (uint8_t previous = 0;
               previous < command.result.mappingCount; ++previous) {
            if (command.result.mappings[previous].value == mapping.value) {
              return fail("Calibration result mapping duplicate");
            }
          }
          command.result.mappingCount++;
        }
      }

      parsed.commandCount++;
      procedure.commandCount++;
    }
    parsed.procedureCount++;
  }

  active_ = true;
  lastError_[0] = '\0';
  return true;
}

bool CalibrationProfileStore::fail(const char *message) {
  clear();
  copyText(lastError_, sizeof(lastError_), message);
  return false;
}
