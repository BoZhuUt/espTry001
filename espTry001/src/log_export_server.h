#pragma once

#include <Arduino.h>
#include "sd_storage.h"
#include "sensor_profile_store.h"
#include "data_logger.h"

// ESPLOG1 协议：USB CDC 上的 SD 日志导出服务。
//
// 与 ESPCFG1 同源的应用层块级握手设计，避免 ESP32-S3 原生 USB CDC
// 小接收缓冲区导致的静默丢字节。文本命令便于抓包，二进制数据块
// 保证 CSV 内容零损失。
//
// 命令集（行结束 \\n，路径以 /logs/ 开头）：
//   PING                              -> PONG ESPLOG1 <cfg> <fw>\n
//   LIST                              -> COUNT <n>\n  <n 行>\n  END\n
//   STAT <path>                       -> OK <size> <crc32_hex>\n | ERR <reason>\n
//   GET <path> <offset> <length>      -> DATA <actual>\r\n <bytes> CRC <hex>\n
//                                      （PC 回 ACK/RESEND/CANCEL，EOF 主动结束）
//   DELETE <path>                     -> CONFIRM <token>\n | ERR <reason>\n
//   CONFIRM <token>                   -> OK Deleted\n | ERR Not Confirmed\n
//   CLOSE                             -> BYE\n
//
// 非阻塞推进：update() 单次最多处理一条命令或一个数据块，
// 不允许长时间卡住 loop()。
class LogExportServer {
 public:
  enum class State : uint8_t {
    Idle,             // 未启动
    WaitingCommand,   // 主等待态，解析一行命令
    Listing,          // 枚举 /logs/*.csv，分批发送
    StatRunning,      // 流式计算 CRC32
    GetRespond,       // 准备 DATA 头并发送一块
    WaitingAck,       // 等待 PC 的 ACK/RESEND/CANCEL
    DeleteConfirming, // 已发 CONFIRM token，等待 CONFIRM 回执
    Closing,          // 已发 BYE，等待主程序退出 LOG EXPORT 模式
    Failed            // 不可恢复错误（SD 不可用等）
  };

  // ESPLOG1 协议常量
  static constexpr const char *kMagic = "ESPLOG1";
  static constexpr const char *kLogsDir = "/logs";
  static constexpr const char *kLogsPrefix = "/logs/";
  static constexpr size_t kLogsPrefixLen = 6;
  // 单块二进制大小，4KB = USB FS 包整数倍，匹配 ESPCFG1 经验值
  static constexpr size_t kBlockSize = 4096;
  // 命令行缓冲
  static constexpr size_t kCommandBuffer = 160;
  // 最大文件名长度（含 /logs/ 前缀）
  static constexpr size_t kMaxPathLen = 80;
  // DELETE 二次确认 token 有效期
  static constexpr uint32_t kConfirmTtlMs = 30000;

  // 进入 LOG EXPORT 模式调用。DataLogger 必须已经 stop()。
  void begin(SdStorage &sd, SensorProfileStore &store,
             DataLogger &logger, const char *firmwareVersion);
  // 主推进函数，在 loop() 中持续调用。
  void update(Stream &usb);
  // 强制取消，清理文件句柄和内部状态。
  void cancel();

  State state() const { return state_; }
  const char *statusText() const;
  const char *currentFile() const { return transferFile_; }
  uint32_t transferOffset() const { return transferOffset_; }
  uint32_t transferTotal() const { return transferTotal_; }
  const char *errorText() const { return errorText_; }
  // 收到 CLOSE 后返回 true，主程序据此退出 LOG EXPORT 模式回 Dashboard。
  bool shouldExit() const { return state_ == State::Closing; }

 private:
  SdStorage *sd_ = nullptr;
  SensorProfileStore *store_ = nullptr;
  DataLogger *logger_ = nullptr;
  const char *firmwareVersion_ = "unknown";

  Stream *usb_ = nullptr;
  State state_ = State::Idle;

  // 命令行累积缓冲
  char cmdLine_[kCommandBuffer] = {0};
  size_t cmdLen_ = 0;

  // 通用数据缓冲（STAT 与 GET 共用）
  uint8_t block_[kBlockSize] = {0};

  // LIST 状态
  File listDir_;               // 打开的 /logs 目录
  uint32_t listRemaining_ = 0; // 尚未发送的条目数（0 表示未知或扫描中）

  // STAT 状态
  char statFile_[kMaxPathLen] = {0};
  File statFileHandle_;
  uint32_t statCrc_ = 0xFFFFFFFF;
  uint64_t statSize_ = 0;
  bool statDone_ = false;

  // GET 状态
  char transferFile_[kMaxPathLen] = {0};
  File transferFileHandle_;
  uint32_t transferOffset_ = 0;
  uint32_t transferTotal_ = 0;
  uint32_t transferRequested_ = 0;  // 当前块 PC 请求的长度
  size_t transferBlockLen_ = 0;     // 当前块实际长度
  uint32_t transferBlockCrc_ = 0xFFFFFFFF;

  // DELETE 状态
  char deleteFile_[kMaxPathLen] = {0};
  char confirmToken_[9] = {0};       // 8 hex + '\0'
  uint32_t confirmIssuedMs_ = 0;

  char errorText_[96] = {0};

  // 内部方法
  void resetSession();
  void fail(const char *message);
  void sendLine(const char *line);
  void sendLineFmt(const char *fmt, ...);
  bool readCommand(Stream &usb);
  void handleCommand(const char *line);
  static bool isSafeLogPath(const char *path);
  static bool parseFileNameMeta(const char *name, uint32_t *pn,
                                uint32_t *session);
  void startListing();
  void continueListing();
  void finishListing();
  void startStat(const char *path);
  void continueStat();
  void startGet(const char *path, uint32_t offset, uint32_t length);
  void continueGet();
  void handleAck(const char *line);
  void startDelete(const char *path);
  void handleConfirm(const char *token);
  void generateToken();
  static uint32_t crc32Update(uint32_t crc, const uint8_t *data, size_t len);
  static uint32_t crc32Final(uint32_t crc);
};
