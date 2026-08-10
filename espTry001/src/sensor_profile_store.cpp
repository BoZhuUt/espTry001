#include "sensor_profile_store.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <esp_partition.h>

namespace {

constexpr uint8_t kSupportedSchemaVersion = 1;
constexpr size_t kPartitionProbeSize = 256;

void copyText(char *destination, size_t capacity, const char *source) {
  if (capacity == 0) return;
  snprintf(destination, capacity, "%s", source != nullptr ? source : "");
}

bool littleFsPartitionIsBlank() {
  const esp_partition_t *partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
  if (partition == nullptr) return false;

  uint8_t buffer[kPartitionProbeSize];
  for (size_t offset = 0; offset < partition->size;
       offset += sizeof(buffer)) {
    size_t length = std::min<size_t>(sizeof(buffer), partition->size - offset);
    if (esp_partition_read(partition, offset, buffer, length) != ESP_OK) {
      return false;
    }
    for (size_t index = 0; index < length; ++index) {
      if (buffer[index] != 0xFF) return false;
    }
  }
  return true;
}

} // namespace

bool SensorProfileStore::begin(const char *path) {
  useBuiltIn("LittleFS not loaded");

  // Never format a partition that contains data. A brand-new board uses an
  // erased (all 0xFF) partition, which is safe to format exactly once.
  if (!LittleFS.begin(false)) {
    if (!littleFsPartitionIsBlank() || !LittleFS.begin(true)) {
      return fail("LittleFS mount failed");
    }
  }
  if (!activateFile(path)) {
    // loadRuntimeFile() 已记录具体原因；保持 Built-in 回退模式。
    return false;
  }
  return true;
}

bool SensorProfileStore::activateFile(const char *path) {
  if (!loadRuntimeFile(path)) return false;
  runtimeActive_ = true;
  lastError_[0] = '\0';
  return true;
}

const SensorProfile *SensorProfileStore::find(uint32_t productNum) const {
  if (!runtimeActive_) return findSensorProfile(productNum);

  for (uint8_t i = 0; i < runtimeProfileCount_; i++) {
    if (runtimeProfiles_[i].profile.productNum == productNum) {
      return &runtimeProfiles_[i].profile;
    }
  }
  return nullptr;
}

bool SensorProfileStore::loadRuntimeFile(const char *path) {
  File file = LittleFS.open(path, "r");
  if (!file) return fail("Profile JSON missing");
  if (file.size() == 0 || file.size() > kMaxJsonFileSize) {
    file.close();
    return fail("Profile JSON size invalid");
  }

  // JsonDocument 只在启动解析阶段存在；解析完成后数据复制到固定数组。
  JsonDocument document;
  DeserializationError jsonError = deserializeJson(document, file);
  file.close();
  if (jsonError) return fail(jsonError.c_str());

  if (!document["schema_version"].is<uint8_t>() ||
      document["schema_version"].as<uint8_t>() != kSupportedSchemaVersion) {
    return fail("Unsupported schema_version");
  }

  const char *version = document["config_version"].as<const char *>();
  if (version == nullptr || version[0] == '\0' ||
      strlen(version) >= sizeof(configVersion_)) {
    return fail("config_version invalid");
  }

  JsonArray profiles = document["profiles"].as<JsonArray>();
  if (profiles.isNull() || profiles.size() == 0 ||
      profiles.size() > kMaxProfiles) {
    return fail("profiles count invalid");
  }

  runtimeProfileCount_ = 0;
  uint8_t profileIndex = 0;
  for (JsonObject profileJson : profiles) {
    if (!profileJson["product_num"].is<uint32_t>()) {
      return fail("product_num invalid");
    }
    uint32_t productNum = profileJson["product_num"].as<uint32_t>();
    if (productNum == 0) return fail("product_num cannot be zero");
    for (uint8_t previous = 0; previous < profileIndex; previous++) {
      if (runtimeProfiles_[previous].profile.productNum == productNum) {
        return fail("Duplicate product_num");
      }
    }

    const char *name = profileJson["name"].as<const char *>();
    if (name == nullptr || name[0] == '\0' ||
        strlen(name) >= kMaxProfileNameLength) {
      return fail("Profile name invalid");
    }

    JsonArray fieldsJson = profileJson["fields"].as<JsonArray>();
    if (fieldsJson.isNull() || fieldsJson.size() == 0 ||
        fieldsJson.size() > kMaxSensorFields) {
      return fail("fields count invalid");
    }

    RuntimeProfile &storage = runtimeProfiles_[profileIndex];
    storage = RuntimeProfile{};
    copyText(storage.name, sizeof(storage.name), name);
    storage.profile.productNum = productNum;
    storage.profile.name = storage.name;

    uint32_t addresses[kMaxSensorFields] = {0};
    uint8_t registerCounts[kMaxSensorFields] = {0};
    char keys[kMaxSensorFields][kMaxFieldKeyLength] = {{0}};
    uint8_t fieldIndex = 0;
    for (JsonObject fieldJson : fieldsJson) {
      const char *key = fieldJson["key"].as<const char *>();
      const char *label = fieldJson["label"].as<const char *>();
      if (key == nullptr || key[0] == '\0' ||
          strlen(key) >= kMaxFieldKeyLength) {
        return fail("Field key invalid");
      }
      if (label == nullptr || label[0] == '\0' ||
          strlen(label) >= kMaxFieldLabelLength) {
        return fail("Field label invalid");
      }
      for (uint8_t previous = 0; previous < fieldIndex; previous++) {
        if (strcmp(keys[previous], key) == 0) return fail("Duplicate field key");
      }
      copyText(keys[fieldIndex], sizeof(keys[fieldIndex]), key);

      if (!fieldJson["function"].is<uint8_t>() ||
          fieldJson["function"].as<uint8_t>() != 3) {
        return fail("Only function 03 supported");
      }
      if (!fieldJson["address_base"].is<uint8_t>() ||
          fieldJson["address_base"].as<uint8_t>() != 1) {
        return fail("address_base must be 1");
      }
      if (!fieldJson["address"].is<uint32_t>()) {
        return fail("Field address invalid");
      }
      uint32_t address = fieldJson["address"].as<uint32_t>();
      if (address == 0 || address > 65536) return fail("Field address out of range");

      const char *type = fieldJson["type"].as<const char *>();
      SensorValueType valueType;
      uint8_t registerCount;
      if (type != nullptr && strcmp(type, "uint16") == 0) {
        valueType = SensorValueType::UInt16;
        registerCount = 1;
      } else if (type != nullptr && strcmp(type, "float32") == 0) {
        const char *wordOrder = fieldJson["word_order"].as<const char *>();
        if (wordOrder == nullptr || strcmp(wordOrder, "CDAB") != 0) {
          return fail("float32 word_order must be CDAB");
        }
        valueType = SensorValueType::Float32CDAB;
        registerCount = 2;
      } else {
        return fail("Field type unsupported");
      }
      if (address + registerCount - 1 > 65536) {
        return fail("Field exceeds address range");
      }

      if (!fieldJson["decimals"].is<uint8_t>()) return fail("decimals invalid");
      uint8_t decimals = fieldJson["decimals"].as<uint8_t>();
      if (decimals > 3) return fail("decimals out of range");

      copyText(storage.labels[fieldIndex], sizeof(storage.labels[fieldIndex]), label);
      storage.fields[fieldIndex].label = storage.labels[fieldIndex];
      storage.fields[fieldIndex].type = valueType;
      storage.fields[fieldIndex].decimals = decimals;
      addresses[fieldIndex] = address;
      registerCounts[fieldIndex] = registerCount;
      fieldIndex++;
    }

    // 按地址排序索引，但不改变 fields[] 的显示顺序。
    uint8_t order[kMaxSensorFields] = {0};
    for (uint8_t i = 0; i < fieldIndex; i++) order[i] = i;
    for (uint8_t i = 0; i + 1 < fieldIndex; i++) {
      uint8_t minimum = i;
      for (uint8_t j = i + 1; j < fieldIndex; j++) {
        if (addresses[order[j]] < addresses[order[minimum]]) minimum = j;
      }
      uint8_t temporary = order[i];
      order[i] = order[minimum];
      order[minimum] = temporary;
    }

    uint32_t previousEnd = 0;
    uint8_t blockCount = 0;
    for (uint8_t sortedIndex = 0; sortedIndex < fieldIndex; sortedIndex++) {
      uint8_t originalIndex = order[sortedIndex];
      uint32_t start = addresses[originalIndex];
      uint32_t end = start + registerCounts[originalIndex];
      if (sortedIndex > 0 && start < previousEnd) return fail("Field addresses overlap");

      if (blockCount > 0 && start == previousEnd) {
        storage.blocks[blockCount - 1].registerCount += registerCounts[originalIndex];
      } else {
        storage.blocks[blockCount].address1Based = start;
        storage.blocks[blockCount].registerCount = registerCounts[originalIndex];
        blockCount++;
      }
      uint8_t currentBlock = blockCount - 1;
      storage.fields[originalIndex].blockIndex = currentBlock;
      storage.fields[originalIndex].registerOffset =
          start - storage.blocks[currentBlock].address1Based;
      previousEnd = end;
    }

    storage.profile.blocks = storage.blocks;
    storage.profile.blockCount = blockCount;
    storage.profile.fields = storage.fields;
    storage.profile.fieldCount = fieldIndex;
    runtimeProfileCount_++;
    profileIndex++;
  }

  copyText(configVersion_, sizeof(configVersion_), version);
  return true;
}

bool SensorProfileStore::fail(const char *message) {
  copyText(lastError_, sizeof(lastError_), message);
  runtimeActive_ = false;
  runtimeProfileCount_ = 0;
  copyText(configVersion_, sizeof(configVersion_), builtInProfileConfigVersion());
  return false;
}

void SensorProfileStore::useBuiltIn(const char *reason) {
  runtimeActive_ = false;
  runtimeProfileCount_ = 0;
  copyText(configVersion_, sizeof(configVersion_), builtInProfileConfigVersion());
  copyText(lastError_, sizeof(lastError_), reason);
}
