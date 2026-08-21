#include "services/wifi_time_service.h"

#include <Preferences.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <time.h>

#include "board/board_config.h"

volatile bool WifiTimeService::sntpCompleted_ = false;

namespace {

constexpr uint32_t kRetryDelaysMs[] = {5000, 10000, 30000, 60000};

}  // namespace

WifiTimeService::WifiTimeService(TimeService &timeService)
    : timeService_(timeService) {}

void WifiTimeService::begin() {
  if (!BoardConfig::kHasRtc) {
    state_ = State::Unsupported;
    return;
  }
  if (!loadCredentials()) {
    state_ = State::NoCredentials;
    return;
  }
  retryDelayIndex_ = 0;
  startConnection();
}

void WifiTimeService::update(bool rtcWriteAllowed) {
  const uint32_t nowMs = millis();
  switch (state_) {
    case State::Connecting:
      if (WiFi.status() == WL_CONNECTED) {
        sntpCompleted_ = false;
        sntp_set_time_sync_notification_cb(onSntpCompleted);
        // Use globally reachable servers first and keep Tencent as a mainland
        // fallback. The RTC always stores UTC, so deployment country does not
        // affect the value written to the chip.
        configTime(0, 0, "pool.ntp.org", "time.cloudflare.com",
                   "ntp.tencent.com");
        state_ = State::WaitingNtp;
        stateStartedMs_ = nowMs;
      } else if ((uint32_t)(nowMs - stateStartedMs_) >= kWifiTimeoutMs) {
        stopNetwork();
        scheduleRetry(false);
      }
      break;

    case State::WaitingNtp:
      if (WiFi.status() != WL_CONNECTED) {
        stopNetwork();
        scheduleRetry(false);
        break;
      }
      // Poll the SDK status as a fallback in case the asynchronous callback
      // occurs while USB/other tasks briefly delay the application loop.
      if (sntpCompleted_ ||
          sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
        time_t nowUtc = time(nullptr);
        stopNetwork();
        if (nowUtc >= 946684800LL && nowUtc < 4102444800LL) {
          retryDelayIndex_ = 0;
          state_ = State::PendingRtcWrite;
        } else {
          scheduleRetry(true);
        }
      } else if ((uint32_t)(nowMs - stateStartedMs_) >= kNtpTimeoutMs) {
        stopNetwork();
        scheduleRetry(true);
      }
      break;

    case State::WaitingRetry:
      if (static_cast<int32_t>(nowMs - retryAtMs_) >= 0) {
        startConnection();
      }
      break;

    case State::PendingRtcWrite:
      if (rtcWriteAllowed) applyNtpToRtc();
      break;

    case State::Unsupported:
    case State::NoCredentials:
    case State::Idle:
    case State::Synced:
    case State::WifiTimeout:
    case State::NtpTimeout:
    case State::RtcWriteFailed:
      break;
  }
}

bool WifiTimeService::saveCredentials(const char *ssid,
                                      const char *password) {
  if (!BoardConfig::kHasRtc || ssid == nullptr || password == nullptr) {
    return false;
  }
  const size_t ssidLength = strlen(ssid);
  const size_t passwordLength = strlen(password);
  if (ssidLength == 0 || ssidLength > kMaxSsidLength ||
      passwordLength > kMaxPasswordLength) {
    return false;
  }

  // A provisioning command supersedes any boot-time attempt using old keys.
  stopNetwork();

  Preferences preferences;
  if (!preferences.begin(kNvsNamespace, false)) return false;
  // Disable first so a power loss between key writes cannot leave a mixed
  // old/new credential pair marked usable.
  bool stored = preferences.putBool(kNvsEnabledKey, false) == 1;
  if (stored) stored = preferences.putString(kNvsSsidKey, ssid) == ssidLength;
  if (stored) {
    preferences.putString(kNvsPasswordKey, password);
    stored = preferences.getString(kNvsPasswordKey, "") == password;
  }
  if (stored) stored = preferences.putBool(kNvsEnabledKey, true) == 1;
  preferences.end();
  if (!stored) return false;

  snprintf(ssid_, sizeof(ssid_), "%s", ssid);
  snprintf(password_, sizeof(password_), "%s", password);
  credentialsLoaded_ = true;
  retryDelayIndex_ = 0;
  state_ = State::Idle;
  return true;
}

bool WifiTimeService::clearCredentials() {
  stopNetwork();
  Preferences preferences;
  if (!preferences.begin(kNvsNamespace, false)) return false;
  bool cleared = preferences.clear();
  preferences.end();
  if (!cleared) return false;

  memset(ssid_, 0, sizeof(ssid_));
  memset(password_, 0, sizeof(password_));
  credentialsLoaded_ = false;
  state_ = BoardConfig::kHasRtc ? State::NoCredentials
                                : State::Unsupported;
  return true;
}

bool WifiTimeService::startManualSync() {
  if (!BoardConfig::kHasRtc || !credentialsLoaded_) return false;
  retryDelayIndex_ = 0;
  return startConnection();
}

const char *WifiTimeService::statusText() const {
  switch (state_) {
    case State::Unsupported:     return "UNSUPPORTED";
    case State::NoCredentials:   return "NO WIFI CONFIG";
    case State::Idle:            return "READY";
    case State::Connecting:      return "CONNECTING";
    case State::WaitingNtp:      return "WAITING NTP";
    case State::WaitingRetry:
      return retryAfterNtpFailure_ ? "RETRY NTP" : "RETRY WIFI";
    case State::PendingRtcWrite: return "PENDING RTC";
    case State::Synced:          return "SYNCED";
    case State::WifiTimeout:     return "WIFI TIMEOUT";
    case State::NtpTimeout:      return "NTP TIMEOUT";
    case State::RtcWriteFailed:  return "RTC WRITE FAIL";
  }
  return "UNKNOWN";
}

uint32_t WifiTimeService::retrySecondsRemaining() const {
  if (state_ != State::WaitingRetry) return 0;
  const int32_t remainingMs = static_cast<int32_t>(retryAtMs_ - millis());
  if (remainingMs <= 0) return 0;
  return (static_cast<uint32_t>(remainingMs) + 999) / 1000;
}

void WifiTimeService::maskedSsid(char *buffer, size_t capacity) const {
  if (buffer == nullptr || capacity == 0) return;
  if (!credentialsLoaded_) {
    buffer[0] = '\0';
    return;
  }
  const size_t length = strlen(ssid_);
  if (length <= 2) {
    snprintf(buffer, capacity, "**");
    return;
  }
  snprintf(buffer, capacity, "%c***%c", ssid_[0], ssid_[length - 1]);
}

bool WifiTimeService::copyCredentials(char *ssid, size_t ssidCapacity,
                                      char *password,
                                      size_t passwordCapacity) const {
  if (ssid == nullptr || ssidCapacity == 0 || password == nullptr ||
      passwordCapacity == 0) {
    return false;
  }
  if (!credentialsLoaded_) {
    ssid[0] = '\0';
    password[0] = '\0';
    return false;
  }
  snprintf(ssid, ssidCapacity, "%s", ssid_);
  snprintf(password, passwordCapacity, "%s", password_);
  return true;
}

void WifiTimeService::onSntpCompleted(struct timeval *timeValue) {
  (void)timeValue;
  sntpCompleted_ = true;
}

bool WifiTimeService::loadCredentials() {
  credentialsLoaded_ = false;
  Preferences preferences;
  // Open read-write on first boot so Preferences can create the namespace.
  // A read-only open of a missing namespace emits an ESP-IDF error log on the
  // shared USB CDC stream, which can corrupt the first ESPTIME1 response.
  if (!preferences.begin(kNvsNamespace, false)) return false;
  const bool enabled = preferences.getBool(kNvsEnabledKey, false);
  String ssid = preferences.getString(kNvsSsidKey, "");
  String password = preferences.getString(kNvsPasswordKey, "");
  preferences.end();

  if (!enabled || ssid.length() == 0 ||
      ssid.length() > kMaxSsidLength ||
      password.length() > kMaxPasswordLength) {
    return false;
  }
  snprintf(ssid_, sizeof(ssid_), "%s", ssid.c_str());
  snprintf(password_, sizeof(password_), "%s", password.c_str());
  credentialsLoaded_ = true;
  return true;
}

bool WifiTimeService::startConnection() {
  stopNetwork();
  sntpCompleted_ = false;
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  if (!WiFi.mode(WIFI_STA)) {
    scheduleRetry(false);
    return true;
  }
  WiFi.begin(ssid_, password_);
  state_ = State::Connecting;
  stateStartedMs_ = millis();
  return true;
}

void WifiTimeService::scheduleRetry(bool ntpFailure) {
  if (!credentialsLoaded_) {
    state_ = State::NoCredentials;
    return;
  }
  const uint8_t index = retryDelayIndex_ < kRetryDelayCount
                            ? retryDelayIndex_
                            : kRetryDelayCount - 1;
  retryAtMs_ = millis() + kRetryDelaysMs[index];
  if (retryDelayIndex_ + 1 < kRetryDelayCount) retryDelayIndex_++;
  retryAfterNtpFailure_ = ntpFailure;
  state_ = State::WaitingRetry;
}

void WifiTimeService::stopNetwork() {
  if (esp_sntp_enabled()) esp_sntp_stop();
  sntp_set_time_sync_notification_cb(nullptr);
  // Arduino-ESP32 logs ESP_ERR_WIFI_NOT_INIT to the shared USB CDC stream if
  // disconnect() is called after the STA driver has already been stopped.
  // That diagnostic can be mistaken for an ESPTIME1 response, so only call
  // disconnect while a Wi-Fi mode is actually active. wifioff=true already
  // ends the STA driver; a second WiFi.mode(WIFI_OFF) call is unnecessary.
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(true, false);
  }
}

void WifiTimeService::applyNtpToRtc() {
  const time_t nowUtc = time(nullptr);
  if (nowUtc < 946684800LL || nowUtc >= 4102444800LL ||
      !timeService_.setUtcEpoch(static_cast<int64_t>(nowUtc),
                                TimeService::Source::Ntp)) {
    state_ = State::RtcWriteFailed;
    return;
  }
  state_ = State::Synced;
}
