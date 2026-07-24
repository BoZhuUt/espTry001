#include "log_export_server.h"

#include <SD_MMC.h>
#include <esp_system.h>
#include <stdarg.h>
#include <string.h>

namespace {

// 解析 "PN53607_S000012.csv" 或 "PN53607_S000012_v2.csv" 中的 PN 与 session。
// 返回 true 表示成功解析；失败时 pn/session 不修改。
bool parseBaseName(const char *name, uint32_t *pn, uint32_t *session) {
  // 跳过 /logs/ 前缀
  const char *base = name;
  const char *slash = strrchr(name, '/');
  if (slash != nullptr) base = slash + 1;

  if (strncmp(base, "PN", 2) != 0) return false;
  const char *p = base + 2;
  if (*p < '0' || *p > '9') return false;

  uint32_t productNum = 0;
  while (*p >= '0' && *p <= '9') {
    productNum = productNum * 10 + (*p - '0');
    p++;
  }
  if (*p != '_') return false;
  p++;
  if (*p != 'S') return false;
  p++;
  if (*p < '0' || *p > '9') return false;

  uint32_t sid = 0;
  while (*p >= '0' && *p <= '9') {
    sid = sid * 10 + (*p - '0');
    p++;
  }
  // 后面必须是 .csv（分卷是 _vN.csv，结尾仍是 .csv）
  if (strcasecmp(p, ".csv") != 0) return false;

  *pn = productNum;
  *session = sid;
  return true;
}

}  // namespace

void LogExportServer::begin(SdStorage &sd, SensorProfileStore &store,
                            DataLogger &logger, const char *firmwareVersion) {
  cancel();
  sd_ = &sd;
  store_ = &store;
  logger_ = &logger;
  firmwareVersion_ = firmwareVersion != nullptr ? firmwareVersion : "unknown";

  if (sd_->state() != SdStorage::State::Ready) {
    fail("SD not ready");
    return;
  }
  state_ = State::WaitingCommand;
}

void LogExportServer::cancel() {
  resetSession();
  state_ = State::Idle;
}

void LogExportServer::resetSession() {
  if (listDir_) listDir_.close();
  if (statFileHandle_) statFileHandle_.close();
  if (transferFileHandle_) transferFileHandle_.close();
  listDir_ = File();
  statFileHandle_ = File();
  transferFileHandle_ = File();
  cmdLen_ = 0;
  cmdLine_[0] = '\0';
  listRemaining_ = 0;
  statFile_[0] = '\0';
  statCrc_ = 0xFFFFFFFF;
  statSize_ = 0;
  statDone_ = false;
  transferFile_[0] = '\0';
  transferOffset_ = 0;
  transferTotal_ = 0;
  transferRequested_ = 0;
  transferBlockLen_ = 0;
  transferBlockCrc_ = 0xFFFFFFFF;
  deleteFile_[0] = '\0';
  confirmToken_[0] = '\0';
  confirmIssuedMs_ = 0;
}

const char *LogExportServer::statusText() const {
  switch (state_) {
    case State::Idle:              return "Idle";
    case State::WaitingCommand:    return "Ready (ESPLOG1)";
    case State::Listing:           return "Listing";
    case State::StatRunning:       return "STAT running";
    case State::GetRespond:        return "Sending block";
    case State::WaitingAck:        return "Waiting ACK";
    case State::DeleteConfirming:  return "Waiting confirm";
    case State::Closing:           return "Closing";
    case State::Failed:            return "Failed";
  }
  return "Unknown";
}

void LogExportServer::fail(const char *message) {
  resetSession();
  snprintf(errorText_, sizeof(errorText_), "%.95s",
           message != nullptr ? message : "Unknown");
  state_ = State::Failed;
}

bool LogExportServer::writeAll(const uint8_t *data, size_t length) {
  if (usb_ == nullptr || data == nullptr) return false;

  size_t written = 0;
  uint32_t lastProgressMs = millis();
  while (written < length) {
    size_t chunk = length - written;
    if (chunk > kUsbWriteChunkSize) chunk = kUsbWriteChunkSize;

    // write() returns the number of bytes actually accepted. USB CDC is
    // allowed to return a short count; retry the remainder instead of silently
    // dropping it and entering WaitingAck with an incomplete DATA block.
    size_t accepted = usb_->write(data + written, chunk);
    if (accepted > 0) {
      written += accepted;
      lastProgressMs = millis();
      delay(0);  // Let the USB task drain its TX buffer between chunks.
      continue;
    }

    if ((uint32_t)(millis() - lastProgressMs) >= kUsbWriteTimeoutMs) {
      return false;
    }
    delay(1);
  }
  return true;
}

void LogExportServer::sendLine(const char *line) {
  if (usb_ == nullptr) return;
  if (!writeAll((const uint8_t *)line, strlen(line)) ||
      !writeAll((const uint8_t *)"\n", 1)) {
    fail("USB write timeout");
    return;
  }
  usb_->flush();
}

void LogExportServer::sendLineFmt(const char *fmt, ...) {
  char buffer[kCommandBuffer];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  sendLine(buffer);
}

bool LogExportServer::readCommand(Stream &usb) {
  while (usb.available() && cmdLen_ < kCommandBuffer - 1) {
    int value = usb.read();
    if (value < 0) break;
    if (value == '\r') continue;
    if (value == '\n') {
      cmdLine_[cmdLen_] = '\0';
      return true;
    }
    cmdLine_[cmdLen_++] = (char)value;
  }
  // 缓冲超长时直接截断并丢弃，避免攻击者用超长行打满 RAM。
  if (cmdLen_ >= kCommandBuffer - 1) {
    cmdLen_ = 0;
  }
  return false;
}

bool LogExportServer::isSafeLogPath(const char *path) {
  if (path == nullptr || path[0] != '/') return false;
  if (strncmp(path, kLogsPrefix, kLogsPrefixLen) != 0) return false;
  // 禁止 ".." 防越权
  if (strstr(path, "..") != nullptr) return false;
  // 必须以 .csv 结尾
  size_t len = strlen(path);
  if (len < 5) return false;
  if (strcasecmp(path + len - 4, ".csv") != 0) return false;
  return true;
}

bool LogExportServer::parseFileNameMeta(const char *name, uint32_t *pn,
                                        uint32_t *session) {
  return parseBaseName(name, pn, session);
}

uint32_t LogExportServer::crc32Update(uint32_t crc, const uint8_t *data,
                                      size_t len) {
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 1) ? ((crc >> 1) ^ 0xEDB88320) : (crc >> 1);
    }
  }
  return crc;
}

uint32_t LogExportServer::crc32Final(uint32_t crc) {
  return crc ^ 0xFFFFFFFF;
}

void LogExportServer::update(Stream &usb) {
  if (state_ == State::Idle || state_ == State::Failed ||
      state_ == State::Closing) {
    return;
  }
  usb_ = &usb;

  // 主等待态：解析一行命令
  if (state_ == State::WaitingCommand) {
    if (!readCommand(usb)) return;
    handleCommand(cmdLine_);
    cmdLen_ = 0;
    cmdLine_[0] = '\0';
    return;
  }

  // 长事务推进：LIST/STAT/GET 等多帧状态
  switch (state_) {
    case State::Listing:
      continueListing();
      break;
    case State::StatRunning:
      continueStat();
      break;
    case State::GetRespond:
      continueGet();
      break;
    case State::WaitingAck:
      // 等待 PC 一行确认
      if (readCommand(usb)) {
        handleAck(cmdLine_);
        cmdLen_ = 0;
        cmdLine_[0] = '\0';
      }
      break;
    case State::DeleteConfirming:
      // 30 秒未确认自动失败，回到 WaitingCommand
      if ((uint32_t)(millis() - confirmIssuedMs_) >= kConfirmTtlMs) {
        sendLine("ERR Confirm timeout");
        deleteFile_[0] = '\0';
        confirmToken_[0] = '\0';
        state_ = State::WaitingCommand;
      } else if (readCommand(usb)) {
        // cmdLine_ 形如 "CONFIRM <token>"，需要提取 token 部分再比较。
        // 直接传整行会让 strcmp("CONFIRM ABCD1234", "ABCD1234") 永远失败。
        if (strncmp(cmdLine_, "CONFIRM ", 8) == 0) {
          handleConfirm(cmdLine_ + 8);
        } else {
          sendLine("ERR Expected CONFIRM");
          deleteFile_[0] = '\0';
          confirmToken_[0] = '\0';
          state_ = State::WaitingCommand;
        }
        cmdLen_ = 0;
        cmdLine_[0] = '\0';
      }
      break;
    default:
      break;
  }
}

void LogExportServer::handleCommand(const char *line) {
  if (line[0] == '\0') return;

  if (strcmp(line, "PING") == 0) {
    sendLineFmt("PONG %s %s %s", kMagic,
                store_ != nullptr ? store_->configVersion() : "unknown",
                firmwareVersion_);
    return;
  }

  if (strcmp(line, "LIST") == 0) {
    startListing();
    return;
  }

  if (strcmp(line, "CLOSE") == 0) {
    sendLine("BYE");
    state_ = State::Closing;
    return;
  }

  if (strncmp(line, "STAT ", 5) == 0) {
    startStat(line + 5);
    return;
  }

  if (strncmp(line, "GET ", 4) == 0) {
    // GET <path> <offset> <length>
    char path[kMaxPathLen];
    uint32_t offset = 0;
    uint32_t length = 0;
    if (sscanf(line + 4, "%79s %lu %lu", path,
               (unsigned long *)&offset, (unsigned long *)&length) != 3) {
      sendLine("ERR Bad GET args");
      return;
    }
    if (length == 0 || length > kBlockSize) {
      sendLine("ERR Bad length");
      return;
    }
    startGet(path, offset, length);
    return;
  }

  if (strncmp(line, "DELETE ", 7) == 0) {
    startDelete(line + 7);
    return;
  }

  if (strcmp(line, "ACK") == 0 || strcmp(line, "RESEND") == 0 ||
      strcmp(line, "CANCEL") == 0) {
    // 在 WaitingCommand 收到 ACK/RESEND/CANCEL 视为协议错，忽略
    return;
  }

  sendLine("ERR Unknown command");
}

// ---------------------------------------------------------------------------
// LIST 实现：先扫一遍目录计数并缓存 count，再逐条发送
// ---------------------------------------------------------------------------

void LogExportServer::startListing() {
  if (!sd_->ensureDir(kLogsDir)) {
    sendLine("ERR No /logs");
    return;
  }
  listDir_ = SD_MMC.open(kLogsDir);
  if (!listDir_ || !listDir_.isDirectory()) {
    if (listDir_) listDir_.close();
    sendLine("ERR Open /logs failed");
    return;
  }

  // 扫一遍计数
  uint32_t count = 0;
  File entry = listDir_.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      const char *name = entry.name();
      if (name != nullptr) {
        size_t len = strlen(name);
        if (len > 4 && strcasecmp(name + len - 4, ".csv") == 0) {
          count++;
        }
      }
    }
    entry.close();
    entry = listDir_.openNextFile();
  }
  // rewind 需要重新打开目录，因为 openNextFile 迭代器没有公开 rewind
  listDir_.close();
  listDir_ = SD_MMC.open(kLogsDir);
  if (!listDir_) {
    sendLine("ERR Rewind failed");
    return;
  }

  listRemaining_ = count;
  sendLineFmt("COUNT %lu", (unsigned long)count);
  if (count == 0) {
    sendLine("END");
    listDir_.close();
    state_ = State::WaitingCommand;
  } else {
    state_ = State::Listing;
  }
}

void LogExportServer::continueListing() {
  // 每次调用最多发送一条，避免 USB 缓冲溢出
  File entry = listDir_.openNextFile();
  if (!entry) {
    // 目录耗尽，收尾
    if (listRemaining_ > 0) {
      // 计数与实际不符，仍发 END 保证 PC 解析闭环
      listRemaining_ = 0;
    }
    sendLine("END");
    listDir_.close();
    state_ = State::WaitingCommand;
    return;
  }

  if (entry.isDirectory()) {
    entry.close();
    return;  // 跳过子目录，continueListing 下次继续
  }

  const char *name = entry.name();
  if (name == nullptr) {
    entry.close();
    return;
  }
  // entry.name() 返回的是完整路径或仅文件名取决于实现；
  // ESP32 SD_MMC 通常返回文件名（不含目录前缀），这里补回 /logs/
  size_t len = strlen(name);
  if (len < 5 || strcasecmp(name + len - 4, ".csv") != 0) {
    entry.close();
    return;
  }

  char path[kMaxPathLen];
  snprintf(path, sizeof(path), "%s/%s", kLogsDir, name);

  uint32_t size = entry.size();
  uint32_t pn = 0, session = 0;
  parseFileNameMeta(path, &pn, &session);

  sendLineFmt("%s\t%lu\t%lu\t%lu", path, (unsigned long)size,
              (unsigned long)session, (unsigned long)pn);
  entry.close();

  if (listRemaining_ > 0) listRemaining_--;
  if (listRemaining_ == 0) {
    sendLine("END");
    listDir_.close();
    state_ = State::WaitingCommand;
  }
}

void LogExportServer::finishListing() {
  // 占位，保留为以后批量刷新用
}

// ---------------------------------------------------------------------------
// STAT 实现：分块读整文件并累计 CRC32
// ---------------------------------------------------------------------------

void LogExportServer::startStat(const char *path) {
  if (!isSafeLogPath(path)) {
    sendLine("ERR Bad path");
    return;
  }
  if (!sd_->exists(path)) {
    sendLine("ERR Not found");
    return;
  }
  File f = sd_->open(path, FILE_READ);
  if (!f) {
    sendLine("ERR Open failed");
    return;
  }
  statFileHandle_ = f;
  snprintf(statFile_, sizeof(statFile_), "%s", path);
  statCrc_ = 0xFFFFFFFF;
  statSize_ = 0;
  statDone_ = false;
  state_ = State::StatRunning;
}

void LogExportServer::continueStat() {
  if (!statFileHandle_) {
    sendLine("ERR File closed");
    state_ = State::WaitingCommand;
    return;
  }
  // 每次最多读 kBlockSize 并累加 CRC32
  size_t n = statFileHandle_.read(block_, kBlockSize);
  if (n == 0) {
    // 读到 EOF
    uint32_t crc = crc32Final(statCrc_);
    sendLineFmt("OK %llu %08X", (unsigned long long)statSize_,
                (unsigned int)crc);
    statFileHandle_.close();
    state_ = State::WaitingCommand;
    return;
  }
  statCrc_ = crc32Update(statCrc_, block_, n);
  statSize_ += n;
}

// ---------------------------------------------------------------------------
// GET 实现：块级握手，支持断点续传
// ---------------------------------------------------------------------------

void LogExportServer::startGet(const char *path, uint32_t offset,
                               uint32_t length) {
  if (!isSafeLogPath(path)) {
    sendLine("ERR Bad path");
    return;
  }
  if (!sd_->exists(path)) {
    sendLine("ERR Not found");
    return;
  }
  File f = sd_->open(path, FILE_READ);
  if (!f) {
    sendLine("ERR Open failed");
    return;
  }
  uint32_t size = f.size();
  if (offset > size) {
    f.close();
    sendLine("ERR Offset > size");
    return;
  }
  if (offset == size) {
    f.close();
    sendLine("EOF");
    return;
  }

  // seek 到请求偏移
  if (!f.seek(offset)) {
    f.close();
    sendLine("ERR Seek failed");
    return;
  }

  transferFileHandle_ = f;
  snprintf(transferFile_, sizeof(transferFile_), "%s", path);
  transferOffset_ = offset;
  transferTotal_ = size;
  transferRequested_ = length;
  state_ = State::GetRespond;
  continueGet();
}

void LogExportServer::continueGet() {
  // 计算本块实际长度：min(请求长度, 剩余字节)
  uint32_t remaining = transferTotal_ - transferOffset_;
  size_t want = transferRequested_;
  if (want > remaining) want = remaining;
  if (want > kBlockSize) want = kBlockSize;

  size_t n = transferFileHandle_.read(block_, want);
  if (n == 0 && want > 0) {
    sendLine("ERR SD read failed");
    transferFileHandle_.close();
    state_ = State::WaitingCommand;
    return;
  }
  transferBlockLen_ = n;
  transferBlockCrc_ = crc32Final(crc32Update(0xFFFFFFFF, block_, n));

  // Send DATA header, binary payload and CRC trailer as one logical frame.
  // Every byte must be accepted before entering WaitingAck; otherwise the PC
  // waits for a 4096-byte block while the device waits for an ACK forever.
  char header[32];
  char trailer[32];
  int headerLength = snprintf(header, sizeof(header), "DATA %zu\r\n", n);
  int trailerLength = snprintf(trailer, sizeof(trailer), "CRC %08X\n",
                               (unsigned int)transferBlockCrc_);
  if (headerLength <= 0 || trailerLength <= 0 ||
      !writeAll((const uint8_t *)header, (size_t)headerLength) ||
      !writeAll(block_, n) ||
      !writeAll((const uint8_t *)trailer, (size_t)trailerLength)) {
    fail("USB write timeout");
    return;
  }
  usb_->flush();

  state_ = State::WaitingAck;
}

void LogExportServer::handleAck(const char *line) {
  if (strcmp(line, "ACK") == 0) {
    transferOffset_ += transferBlockLen_;
    transferBlockLen_ = 0;
    if (transferOffset_ >= transferTotal_) {
      // 最后一块 ACK 后发 OK Done 作为传输完成的显式确认。
      // 不能省略：PC 必须读这一行才能进入下一个命令，
      // 否则该行会残留在串口缓冲污染下一次通信。
      // 也不能发 EOF：EOF 在协议中是"PC 主动请求越界 offset 时的响应"，
      // 用在这里会让 PC 的 GetBlock 把它当作"块返回"，导致数据流错位。
      sendLine("OK Done");
      transferFileHandle_.close();
      transferFile_[0] = '\0';
      state_ = State::WaitingCommand;
      return;
    }
    // 继续下一块
    state_ = State::GetRespond;
    continueGet();
    return;
  }

  if (strcmp(line, "RESEND") == 0) {
    // 重发当前块。文件指针在 continueGet 的 read 后已前进，必须
    // seek 回 transferOffset_（ACK 后才更新，仍指向当前块起始），
    // 否则会读到下一个块造成数据错乱。
    if (transferFileHandle_) {
      transferFileHandle_.seek(transferOffset_);
    }
    state_ = State::GetRespond;
    continueGet();
    return;
  }

  if (strcmp(line, "CANCEL") == 0) {
    if (transferFileHandle_) transferFileHandle_.close();
    transferFile_[0] = '\0';
    state_ = State::WaitingCommand;
    return;
  }

  // 未知响应：当作协议错，取消本次 GET
  if (transferFileHandle_) transferFileHandle_.close();
  transferFile_[0] = '\0';
  state_ = State::WaitingCommand;
}

// ---------------------------------------------------------------------------
// DELETE 实现：两步确认，禁止删除当前活日志
// ---------------------------------------------------------------------------

void LogExportServer::startDelete(const char *path) {
  if (!isSafeLogPath(path)) {
    sendLine("ERR Bad path");
    return;
  }
  if (!sd_->exists(path)) {
    sendLine("ERR Not found");
    return;
  }
  // 禁止删除正在写入的活日志
  if (logger_ != nullptr && logger_->isRecording()) {
    const char *active = logger_->currentFileName();
    if (active != nullptr && strcmp(active, path) == 0) {
      sendLine("ERR File in use");
      return;
    }
  }
  snprintf(deleteFile_, sizeof(deleteFile_), "%s", path);
  generateToken();
  confirmIssuedMs_ = millis();
  state_ = State::DeleteConfirming;
  sendLineFmt("CONFIRM %s", confirmToken_);
}

void LogExportServer::handleConfirm(const char *token) {
  if (deleteFile_[0] == '\0' || confirmToken_[0] == '\0') {
    sendLine("ERR No pending delete");
    state_ = State::WaitingCommand;
    return;
  }
  if ((uint32_t)(millis() - confirmIssuedMs_) >= kConfirmTtlMs) {
    sendLine("ERR Confirm timeout");
    deleteFile_[0] = '\0';
    confirmToken_[0] = '\0';
    state_ = State::WaitingCommand;
    return;
  }
  if (strcmp(token, confirmToken_) != 0) {
    sendLine("ERR Not confirmed");
    deleteFile_[0] = '\0';
    confirmToken_[0] = '\0';
    state_ = State::WaitingCommand;
    return;
  }

  // 二次确认前再查一次活日志，避免极端竞态
  if (logger_ != nullptr && logger_->isRecording()) {
    const char *active = logger_->currentFileName();
    if (active != nullptr && strcmp(active, deleteFile_) == 0) {
      sendLine("ERR File in use");
      deleteFile_[0] = '\0';
      confirmToken_[0] = '\0';
      state_ = State::WaitingCommand;
      return;
    }
  }

  if (SD_MMC.remove(deleteFile_)) {
    sendLine("OK Deleted");
  } else {
    sendLine("ERR Delete failed");
  }
  deleteFile_[0] = '\0';
  confirmToken_[0] = '\0';
  state_ = State::WaitingCommand;
}

void LogExportServer::generateToken() {
  // 用 esp_random 产生 8 位 hex token
  uint32_t v = esp_random();
  snprintf(confirmToken_, sizeof(confirmToken_), "%08X", (unsigned int)v);
}
