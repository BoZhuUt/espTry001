#pragma once

#include <Arduino.h>
#include "sensor_profile.h"

// 启动时加载 LittleFS JSON，并把内容复制到固定容量的运行时结构中。
// JSON 加载失败时，find() 自动改用编译进固件的生成配置。
class SensorProfileStore {
public:
  static constexpr uint8_t kMaxProfiles = 50;
  static constexpr uint8_t kMaxBlocksPerProfile = kMaxSensorFields;
  static constexpr size_t kMaxProfileNameLength = 24;
  static constexpr size_t kMaxFieldKeyLength = 24;
  static constexpr size_t kMaxFieldLabelLength = 24;
  static constexpr size_t kMaxConfigVersionLength = 24;
  static constexpr size_t kMaxJsonFileSize = 256 * 1024;

  // 挂载 LittleFS 并加载指定文件。返回 true 表示运行时 JSON 已启用；
  // 返回 false 时仍可正常调用 find()，它会查询固件内置配置。
  bool begin(const char *path = "/sensor_profiles.json");

  // 校验并激活一个已经存在于 LittleFS 的候选文件。
  // 主要供配置上传器在原子替换前复用完整校验规则。
  bool activateFile(const char *path);

  // LittleFS 配置有效时只查询文件配置；否则查询固件内置配置。
  const SensorProfile *find(uint32_t productNum) const;

  bool usingLittleFs() const { return runtimeActive_; }
  const char *sourceText() const { return runtimeActive_ ? "LittleFS" : "Built-in"; }
  const char *configVersion() const { return configVersion_; }
  const char *lastError() const { return lastError_; }
  uint8_t profileCount() const { return runtimeProfileCount_; }

private:
  struct RuntimeProfile {
    SensorProfile profile{};
    char name[kMaxProfileNameLength] = {0};
    SensorPollBlock blocks[kMaxBlocksPerProfile]{};
    SensorField fields[kMaxSensorFields]{};
    char labels[kMaxSensorFields][kMaxFieldLabelLength]{};
  };

  bool loadRuntimeFile(const char *path);
  bool fail(const char *message);
  void useBuiltIn(const char *reason);

  RuntimeProfile runtimeProfiles_[kMaxProfiles]{};
  uint8_t runtimeProfileCount_ = 0;
  bool runtimeActive_ = false;
  char configVersion_[kMaxConfigVersionLength] = {0};
  char lastError_[96] = {0};
};
