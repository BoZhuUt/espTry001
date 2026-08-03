#pragma once

#include <Arduino.h>
#include "sd_storage.h"
#include "sensor_profile.h"

// 向 SD 卡记录传感器参数和 4-20mA 数据的 CSV 日志。
// 使用环形缓冲区批量写入，不在 loop() 中同步等待 SD 卡。
class DataLogger {
public:
  // 环形缓冲区大小：约 8 条 CSV 记录
  static constexpr size_t kRingBufferSize = 4096;
  // 最大单行 CSV 长度
  static constexpr size_t kLineBufferSize = 768;
  // 定期 flush 间隔
  static constexpr uint32_t kFullFlushIntervalMs = 5000;
  // 文件分卷大小
  static constexpr uint64_t kMaxFileSize = 32 * 1024 * 1024;  // 32 MB
  // 最大 Profile 名称长度
  static constexpr size_t kMaxProfileNameLen = 24;

  explicit DataLogger(SdStorage &sd);

  // Values written into the CSV comment header for traceability.
  void setMetadata(const char *configVersion, const char *firmwareVersion);

  // 设置当前传感器身份。MASTER 发现成功后调用。
  void setIdentity(uint8_t slaveAddress, uint32_t productNum,
                   const char *profileName, const SensorProfile *profile);

  // 开始记录。创建 CSV 文件并写入文件头。
  void start();

  // 停止记录。flush 缓冲并关闭文件。切回 PASS 模式前必须调用。
  void stop();

  /**
   * 记录一轮已经完成并冻结时间的传感器快照。
   *
   * uptimeMs 与 timestamp 来自扫描完成时刻，而不是 StorageTask 实际
   * 写卡时刻；timestamp 为空时仍写入其余数据，并在 CSV 末列留空。
   */
  void onScanComplete(uint32_t scanSequence, uint32_t uptimeMs,
                      const char *timestamp,
                      const float *values, const bool *valid,
                      uint8_t fieldCount,
                      float current1Ma, float current2Ma);

  // 非阻塞推进函数，在 loop() 中反复调用。
  void update();

  // 状态查询
  bool isRecording() const { return recording_; }
  uint32_t recordCount() const { return recordCount_; }
  uint32_t sessionId() const { return sessionId_; }
  uint32_t lastSuccessfulFlushMs() const {
    return lastSuccessfulFlushMs_;
  }
  const char *currentFileName() const { return fileName_; }
  const char *lastError() const { return lastError_; }

private:
  // NVS 键名
  static constexpr const char *kNvsNamespace = "datalog";
  static constexpr const char *kNvsSessionKey = "sid";

  SdStorage &sd_;

  // 会话
  uint32_t sessionId_ = 0;
  uint32_t scanSequence_ = 0;
  uint32_t lastLoggedSequence_ = 0;
  uint32_t sampleSeq_ = 0;
  uint32_t recordCount_ = 0;

  // 传感器身份
  uint8_t slaveAddress_ = 0;
  uint32_t productNum_ = 0;
  char profileName_[kMaxProfileNameLen + 1] = {0};
  char configVersion_[32] = {0};
  char firmwareVersion_[32] = {0};
  const SensorProfile *profile_ = nullptr;

  // 文件
  char fileName_[64] = {0};
  File file_;
  bool fileOpen_ = false;
  bool recording_ = false;
  bool headerWritten_ = false;
  uint32_t fileVolume_ = 0;  // 分卷编号

  // 环形缓冲区
  char ring_[kRingBufferSize] = {0};
  size_t ringHead_ = 0;   // 写入位置
  size_t ringTail_ = 0;   // 读取位置
  size_t ringUsed_ = 0;   // 已用字节数

  // 定时
  uint32_t lastFlushMs_ = 0;
  uint32_t lastSuccessfulFlushMs_ = 0;

  // 错误
  char lastError_[64] = {0};

  // 内部方法
  void loadSessionId();
  void incrementSessionId();
  bool createFile();
  bool writeFileHeader();
  /**
   * 把固定快照格式化为一行 CSV 并放入环形缓冲区。
   *
   * timestamp 始终位于最后一列，避免改变现有传感器字段的位置。
   */
  bool formatCsvLine(uint32_t uptimeMs, const char *timestamp,
                     const float *values, const bool *valid,
                     uint8_t fieldCount,
                     float current1Ma, float current2Ma);
  bool ringWrite(const char *data, size_t length);
  bool appendFormat(char *buffer, size_t capacity, size_t &length,
                    const char *format, ...);
  bool flushToFile();
  void closeFile();
};
