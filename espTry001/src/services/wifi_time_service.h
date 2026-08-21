#pragma once

#include <Arduino.h>

#include "services/time_service.h"

/**
 * 非阻塞 Wi-Fi/NTP 校时服务。
 *
 * 服务使用非阻塞状态机：主循环只轮询连接和 SNTP 结果，
 * 不等待网络。凭据保存在 NVS，不进入源码、JSON 或日志。
 */
class WifiTimeService {
 public:
  static constexpr size_t kMaxSsidLength = 32;
  static constexpr size_t kMaxPasswordLength = 63;
  static constexpr uint32_t kWifiTimeoutMs = 10000;
  // First DNS lookup plus the initial SNTP retry can exceed 10 seconds on
  // managed or congested networks; keep the main loop non-blocking while
  // allowing a realistic first-boot window.
  static constexpr uint32_t kNtpTimeoutMs = 30000;
  static constexpr uint8_t kRetryDelayCount = 4;

  enum class State : uint8_t {
    Unsupported,
    NoCredentials,
    Idle,
    Connecting,
    WaitingNtp,
    WaitingRetry,
    PendingRtcWrite,
    Synced,
    WifiTimeout,
    NtpTimeout,
    RtcWriteFailed,
  };

  explicit WifiTimeService(TimeService &timeService);

  /**
   * 读取 NVS 凭据并发起自动校时。
   *
   * 无 RTC 板型或无凭据时立即返回，不启动 Wi-Fi。
   */
  void begin();

  /**
   * 推进 Wi-Fi/NTP 状态机。
   *
   * rtcWriteAllowed=false 时可以完成 NTP 取时并关闭 Wi-Fi，
   * 但会延迟写 RTC，避免活动 CSV 内出现时间跳变。
   */
  void update(bool rtcWriteAllowed);

  /**
   * 保存一组 Wi-Fi 凭据到 NVS。
   *
   * 只有 SSID 1~32 字节、密码 0~63 字节时才会覆盖旧值；
   * 成功仅代表存储成功，不代表网络已连接。
   */
  bool saveCredentials(const char *ssid, const char *password);

  // 清除 NVS 凭据并立即停止当前 Wi-Fi/NTP 尝试。
  bool clearCredentials();

  /**
   * 使用已保存凭据手动开始一次新的 NTP 尝试。
   *
   * 用于 ESPTIME1 当场测试；返回 false 表示板型不支持或无凭据。
   */
  bool startManualSync();

  bool configured() const { return credentialsLoaded_; }
  State state() const { return state_; }
  const char *statusText() const;
  uint32_t retrySecondsRemaining() const;

  // 返回脱敏 SSID：仅保留首尾字符，永不返回密码。
  void maskedSsid(char *buffer, size_t capacity) const;

  // 只供用户主动进入 TIME SYNC 页面及其独占 USB 协议显示完整凭据。
  bool copyCredentials(char *ssid, size_t ssidCapacity,
                       char *password, size_t passwordCapacity) const;

 private:
  static constexpr const char *kNvsNamespace = "wifi_time";
  static constexpr const char *kNvsEnabledKey = "enabled";
  static constexpr const char *kNvsSsidKey = "ssid";
  static constexpr const char *kNvsPasswordKey = "password";

  // SNTP 网络任务回调只置位标志，不在回调中访问 RTC/I2C。
  static void onSntpCompleted(struct timeval *timeValue);

  // 从 NVS 读取并严格校验凭据长度。
  bool loadCredentials();

  // 重置状态并启动 STA 连接，函数本身不等待 DHCP。
  bool startConnection();

  // 网络失败后安排下一次非阻塞尝试，间隔按 5/10/30/60 秒退避。
  void scheduleRetry(bool ntpFailure);

  // 停止 SNTP 和 STA 并关闭射频，保留 NVS 凭据。
  void stopNetwork();

  // 将系统 UTC 写入 RTC，只在记录安全条件满足时调用。
  void applyNtpToRtc();

  static volatile bool sntpCompleted_;

  TimeService &timeService_;
  State state_ = State::Unsupported;
  bool credentialsLoaded_ = false;
  uint32_t stateStartedMs_ = 0;
  uint32_t retryAtMs_ = 0;
  uint8_t retryDelayIndex_ = 0;
  bool retryAfterNtpFailure_ = false;
  char ssid_[kMaxSsidLength + 1] = "";
  char password_[kMaxPasswordLength + 1] = "";
};
