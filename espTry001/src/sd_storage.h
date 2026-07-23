#pragma once

#include <Arduino.h>
#include <SD_MMC.h>

// SD_MMC 存储管理。
// 只负责初始化、卡状态和文件操作薄封装，不关心业务逻辑。
// SD 卡未插入或损坏时，其他功能（PASS、MASTER、触摸、Profile）不受影响。
class SdStorage {
public:
  enum class State : uint8_t {
    NotInitialized,  // 未初始化或初始化失败
    Ready,           // 正常可用
    Error            // 运行中出现 I/O 错误（如拔卡）
  };

  // 初始化 SD_MMC 4-bit 模式。只应在 setup() 中调用一次。
  bool begin();

  State state() const { return state_; }
  const char *stateText() const;
  const char *lastError() const { return lastError_; }

  // 卡信息（state_ == Ready 时有效）
  uint64_t totalBytes() const;
  uint64_t usedBytes() const;
  uint64_t freeBytes() const;
  const char *cardType() const;

  // 目录操作
  bool ensureDir(const char *path);

  // 文件操作
  File open(const char *path, const char *mode);
  bool exists(const char *path);

  // 标记运行时错误（写入失败或拔卡后由调用方调用）
  void setError(const char *message);

private:
  State state_ = State::NotInitialized;
  char lastError_[64] = {0};
};
