#pragma once

#include <Arduino.h>

namespace BoardRtc {

struct DateTime {
  // RTC 芯片中的时间统一按 UTC 保存；year 的有效范围是 2000~2099。
  uint16_t year = 2000;
  uint8_t month = 1;
  uint8_t day = 1;
  uint8_t weekday = 0;
  uint8_t hour = 0;
  uint8_t minute = 0;
  uint8_t second = 0;
};

enum class Status : uint8_t {
  Unsupported,
  NotPresent,
  NotSet,
  OscillatorStopped,
  InvalidData,
  IoError,
  Ready,
};

class Driver {
 public:
  /**
   * 初始化 2.8 寸板独立的 Wire1 总线并探测 PCF85063。
   *
   * 返回 true 仅代表芯片存在、振荡器正常且时间寄存器有效。
   * 没装 RTC 的板型会返回 false，并把状态设为 Unsupported。
   */
  bool begin();

  /**
   * 从 PCF85063 读取 UTC 时间。
   *
   * 成功时写入 out 并返回 true；I2C 异常、振荡停止或寄存器内容
   * 不合法时返回 false，具体原因可通过 status()/statusText() 查询。
   */
  bool readUtc(DateTime &out);

  /**
   * 将 UTC 时间写入 PCF85063，并立即回读确认。
   *
   * 只接受 2000-01-01 到 2099-12-31 范围内的合法时间。
   */
  bool setUtc(const DateTime &value);

  // 返回最近一次 RTC 操作的结构化状态和便于界面显示的短文本。
  Status status() const { return status_; }
  const char *statusText() const;

 private:
  static constexpr uint8_t kControl1Register = 0x00;
  static constexpr uint8_t kSecondsRegister = 0x04;
  static constexpr uint8_t kControl1Stop = 0x20;
  static constexpr uint8_t kControl1Normal = 0x01;
  static constexpr uint8_t kSecondsOscillatorStop = 0x80;

  // 仅通过 I2C 地址应答判断 RTC 是否在线，不修改任何寄存器。
  bool probe();

  // 从连续寄存器读取 length 字节；任何总线或长度异常均返回 false。
  bool readRegisters(uint8_t firstRegister, uint8_t *data, size_t length);

  // 从 firstRegister 开始连续写入 length 字节。
  bool writeRegisters(uint8_t firstRegister, const uint8_t *data,
                      size_t length);

  // 解码 RTC 使用的压缩 BCD，并用 mask 去掉寄存器中的状态位。
  static bool decodeBcd(uint8_t encoded, uint8_t mask, uint8_t &decoded);

  // 把 0~99 的十进制数转换成 RTC 使用的压缩 BCD。
  static uint8_t encodeBcd(uint8_t value);

  // 校验年月日、星期和时分秒，防止无效值进入芯片或上层服务。
  static bool isValidDateTime(const DateTime &value);

  // 返回指定月份的天数，包含 Gregorian 闰年规则。
  static uint8_t daysInMonth(uint16_t year, uint8_t month);

  Status status_ = Status::Unsupported;
  bool initialized_ = false;
};

}  // namespace BoardRtc
