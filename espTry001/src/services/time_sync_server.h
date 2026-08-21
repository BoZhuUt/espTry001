#pragma once

#include <Arduino.h>

#include "services/time_service.h"
#include "services/wifi_time_service.h"

/**
 * ESPTIME1 USB CDC 文本协议服务。
 *
 * 服务只在 AppMode::TimeSync 下取得 USB 独占权。它采用逐行、非阻塞解析，
 * 避免等待电脑命令时卡住触摸和界面刷新。支持的命令为：
 *   PING
 *   GET
 *   SET <unix_utc_seconds>
 *   WIFI STATUS
 *   WIFI GET
 *   WIFI SET <ssid_hex> <password_hex_or_dash>
 *   WIFI CLEAR
 *   NTP START
 *   CLOSE
 */
class TimeSyncServer {
 public:
  static constexpr const char *kMagic = "ESPTIME1";
  static constexpr size_t kMaxCommandLength = 223;

  enum class State : uint8_t {
    Idle,
    WaitingCommand,
    Success,
    Failed,
    Closing,
  };

  TimeSyncServer(TimeService &timeService, WifiTimeService &wifiTimeService);

  /**
   * 开始一个新的校时会话。
   *
   * 清除上一会话的半行命令、错误和关闭标记，但不修改 RTC。
   */
  void begin();

  /**
   * 消费当前可用的 USB 字节并处理完整命令行。
   *
   * 每次调用只处理已经到达的数据，不等待换行，因此可安全放在 loop()。
   */
  void update(Stream &usb);

  // 立即结束会话并丢弃尚未完成的命令。
  void cancel();

  // CLOSE 已经回复 BYE 后返回 true，主程序据此安全切回 PASS。
  bool closeRequested() const { return state_ == State::Closing; }

  State state() const { return state_; }
  const char *statusText() const;
  const char *errorText() const { return errorText_; }

 private:
  /**
   * 解析一条去掉 CR/LF 的完整命令并写回单行响应。
   *
   * 协议错误只更新 Failed 状态，不结束会话，电脑可以直接再次发送命令。
   */
  void handleCommand(Stream &usb, const char *command);

  // 发送一行响应并补齐换行；所有协议响应都从此处输出。
  void sendLine(Stream &usb, const char *line);

  // 将失败原因保存为固定长度文本，同时保持会话可重试。
  void fail(Stream &usb, const char *message);

  /**
   * 将偶数长度十六进制 token 解码为原始字节串。
   *
   * 输出始终以 NUL 结尾；"-" 仅用于表示空密码。
   */
  static bool decodeHexToken(const char *token, char *output,
                             size_t capacity, bool allowDashAsEmpty);

  // 将 UTF-8 字节编码为无空格十六进制 token；空密码编码为 "-"。
  static bool encodeHexToken(const char *value, char *output,
                             size_t capacity, bool dashWhenEmpty);

  // 转换单个十六进制字符，非法输入返回 -1。
  static int8_t hexValue(char value);

  TimeService &timeService_;
  WifiTimeService &wifiTimeService_;
  State state_ = State::Idle;
  char command_[kMaxCommandLength + 1] = "";
  size_t commandLength_ = 0;
  char errorText_[64] = "";
};
