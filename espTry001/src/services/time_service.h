#pragma once

#include <Arduino.h>

#include "board/rtc_driver.h"

class TimeService {
 public:
  // RTC 内部保持 UTC；界面和导出文件按中国标准时间 UTC+08:00 显示。
  static constexpr int32_t kDisplayUtcOffsetSeconds = 8 * 60 * 60;
  static constexpr uint32_t kRtcRefreshIntervalMs = 60000;

  enum class Status : uint8_t {
    Unsupported,
    NotPresent,
    NotSet,
    OscillatorStopped,
    InvalidData,
    IoError,
    Holdover,
    Ready,
  };

  enum class Source : uint8_t {
    None,
    Rtc,
    Usb,
    Ntp,
  };

  // 使用外部传入的 RTC 驱动，服务本身不拥有硬件驱动的生命周期。
  explicit TimeService(BoardRtc::Driver &driver);

  /**
   * 初始化 RTC，并用芯片时间建立“UTC + 单调时钟”锚点。
   *
   * RTC 不存在或时间无效不会阻塞启动，状态会留给 UI 显示。
   */
  void begin();

  /**
   * 周期维护入口，应在主循环中频繁调用。
   *
   * 函数内部每 60 秒才访问一次 RTC；两次访问之间使用 ESP32 单调
   * 计时器推进时间，因此不会持续占用 I2C 总线。
   */
  void update();

  /**
   * 写入 Unix UTC 秒数并重新建立时间锚点。
   *
   * 当前只接受 PCF85063 可表达的 2000~2099 年；用于后续 USB 校时协议。
   */
  bool setUtcEpoch(int64_t utcSeconds, Source source = Source::Usb);

  // 查询当前时间是否可用，以及 RTC 是否正处于临时 I2C 故障保持模式。
  bool supported() const { return status_ != Status::Unsupported; }
  bool valid() const { return valid_; }
  bool holdover() const { return status_ == Status::Holdover; }
  Status status() const { return status_; }
  const char *statusText() const;
  Source source() const { return source_; }
  const char *sourceText() const;
  /**
   * 返回当前 Unix UTC 秒数。
   *
   * 结果由最近一次 RTC 值加上 ESP32 单调时钟经过时间得到；
   * valid()==false 时返回 0，调用者应先检查 valid()。
   */
  int64_t utcNow() const;

  // 以下格式化函数只负责显示转换，不会改变 RTC 中保存的 UTC。
  void formatLocalTime(char *buffer, size_t capacity) const;
  void formatLocalDateTime(char *buffer, size_t capacity) const;
  void formatIso8601(char *buffer, size_t capacity) const;

 private:
  /**
   * 从 RTC 刷新 UTC 锚点。
   *
   * 已经拥有有效时间时，单次 I2C 故障进入 Holdover，继续依靠单调
   * 时钟走时；芯片时间倒退等数据错误则使时间失效，避免错误时间入库。
   */
  bool refreshFromRtc();

  // 同时记录 UTC 秒数和当前单调微秒数，形成可持续推进的时间锚点。
  void setAnchor(int64_t utcSeconds);

  // 在 PCF85063 日历结构与 Unix UTC 秒数之间双向转换。
  static int64_t epochFromDateTime(const BoardRtc::DateTime &value);
  static BoardRtc::DateTime dateTimeFromEpoch(int64_t epochSeconds);

  /**
   * Gregorian 公历日期与“距 1970-01-01 的天数”互相转换。
   *
   * 采用按 400 年周期分段的纯整数算法，不依赖设备时区或 C 运行库，
   * 因而在 ESP32 上结果固定、可逆，也能正确处理闰年。
   */
  static int64_t daysFromCivil(int year, unsigned month, unsigned day);
  static void civilFromDays(int64_t days, int &year, unsigned &month,
                            unsigned &day);

  // 把底层硬件状态映射为上层可展示、可决策的服务状态。
  static Status mapDriverStatus(BoardRtc::Status status);

  BoardRtc::Driver &driver_;
  Status status_ = Status::Unsupported;
  Source source_ = Source::None;
  bool valid_ = false;
  int64_t anchorUtcSeconds_ = 0;
  int64_t anchorMonotonicUs_ = 0;
  uint32_t lastRtcRefreshMs_ = 0;
};
