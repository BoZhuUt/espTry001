#include "services/time_sync_server.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

TimeSyncServer::TimeSyncServer(TimeService &timeService,
                               WifiTimeService &wifiTimeService)
    : timeService_(timeService), wifiTimeService_(wifiTimeService) {}

void TimeSyncServer::begin() {
  commandLength_ = 0;
  command_[0] = '\0';
  errorText_[0] = '\0';
  state_ = State::WaitingCommand;
}

void TimeSyncServer::update(Stream &usb) {
  if (state_ == State::Idle || state_ == State::Closing) return;

  while (usb.available()) {
    const char value = static_cast<char>(usb.read());
    if (value == '\r') continue;
    if (value == '\n') {
      if (commandLength_ > 0) {
        command_[commandLength_] = '\0';
        handleCommand(usb, command_);
        commandLength_ = 0;
        command_[0] = '\0';
      }
      continue;
    }

    if (commandLength_ >= kMaxCommandLength) {
      commandLength_ = 0;
      command_[0] = '\0';
      fail(usb, "COMMAND TOO LONG");
      continue;
    }
    command_[commandLength_++] = value;
  }
}

void TimeSyncServer::cancel() {
  commandLength_ = 0;
  command_[0] = '\0';
  errorText_[0] = '\0';
  state_ = State::Idle;
}

const char *TimeSyncServer::statusText() const {
  switch (state_) {
    case State::Idle:           return "IDLE";
    case State::WaitingCommand: return "READY";
    case State::Success:        return "COMMAND OK";
    case State::Failed:         return "RETRY READY";
    case State::Closing:        return "CLOSING";
  }
  return "UNKNOWN";
}

void TimeSyncServer::handleCommand(Stream &usb, const char *command) {
  if (strcmp(command, "PING") == 0) {
    char response[64];
    snprintf(response, sizeof(response), "PONG %s %s",
             kMagic, timeService_.statusText());
    sendLine(usb, response);
    state_ = State::WaitingCommand;
    errorText_[0] = '\0';
    return;
  }

  if (strcmp(command, "GET") == 0) {
    if (!timeService_.valid()) {
      char response[64];
      snprintf(response, sizeof(response), "ERR %s",
               timeService_.statusText());
      fail(usb, response + 4);
      return;
    }
    char localTime[32];
    char response[96];
    timeService_.formatIso8601(localTime, sizeof(localTime));
    snprintf(response, sizeof(response), "TIME %lld %s",
             static_cast<long long>(timeService_.utcNow()), localTime);
    sendLine(usb, response);
    state_ = State::WaitingCommand;
    errorText_[0] = '\0';
    return;
  }

  if (strncmp(command, "SET ", 4) == 0) {
    errno = 0;
    char *end = nullptr;
    const char *number = command + 4;
    long long utcSeconds = strtoll(number, &end, 10);
    if (errno != 0 || end == number || *end != '\0') {
      fail(usb, "BAD UTC SECONDS");
      return;
    }
    if (!timeService_.setUtcEpoch(static_cast<int64_t>(utcSeconds))) {
      fail(usb, timeService_.statusText());
      return;
    }

    char localTime[32];
    char response[96];
    timeService_.formatIso8601(localTime, sizeof(localTime));
    snprintf(response, sizeof(response), "OK %lld %s",
             static_cast<long long>(timeService_.utcNow()), localTime);
    sendLine(usb, response);
    state_ = State::Success;
    errorText_[0] = '\0';
    return;
  }

  if (strcmp(command, "WIFI STATUS") == 0) {
    char maskedSsid[16];
    char response[128];
    wifiTimeService_.maskedSsid(maskedSsid, sizeof(maskedSsid));
    if (wifiTimeService_.configured()) {
      const uint32_t retrySeconds =
          wifiTimeService_.retrySecondsRemaining();
      if (retrySeconds > 0) {
        snprintf(response, sizeof(response),
                 "WIFI CONFIGURED %s %s %lus", maskedSsid,
                 wifiTimeService_.statusText(),
                 static_cast<unsigned long>(retrySeconds));
      } else {
        snprintf(response, sizeof(response), "WIFI CONFIGURED %s %s",
                 maskedSsid, wifiTimeService_.statusText());
      }
    } else {
      snprintf(response, sizeof(response), "WIFI NOT_CONFIGURED %s",
               wifiTimeService_.statusText());
    }
    sendLine(usb, response);
    state_ = State::WaitingCommand;
    errorText_[0] = '\0';
    return;
  }

  if (strcmp(command, "WIFI GET") == 0) {
    char ssid[WifiTimeService::kMaxSsidLength + 1];
    char password[WifiTimeService::kMaxPasswordLength + 1];
    if (!wifiTimeService_.copyCredentials(ssid, sizeof(ssid), password,
                                          sizeof(password))) {
      fail(usb, "NO WIFI CONFIG");
      return;
    }
    char encodedSsid[2 * WifiTimeService::kMaxSsidLength + 1];
    char encodedPassword[2 * WifiTimeService::kMaxPasswordLength + 1];
    if (!encodeHexToken(ssid, encodedSsid, sizeof(encodedSsid), false) ||
        !encodeHexToken(password, encodedPassword, sizeof(encodedPassword),
                        true)) {
      fail(usb, "WIFI ENCODING FAILED");
      return;
    }
    char response[kMaxCommandLength + 1];
    snprintf(response, sizeof(response), "WIFI CREDENTIALS %s %s",
             encodedSsid, encodedPassword);
    sendLine(usb, response);
    state_ = State::WaitingCommand;
    errorText_[0] = '\0';
    return;
  }

  if (strncmp(command, "WIFI SET ", 9) == 0) {
    char encoded[2 * WifiTimeService::kMaxSsidLength + 1];
    char encodedPassword[2 * WifiTimeService::kMaxPasswordLength + 1];
    const char *arguments = command + 9;
    const char *separator = strchr(arguments, ' ');
    if (separator == nullptr || strchr(separator + 1, ' ') != nullptr) {
      fail(usb, "BAD WIFI SET");
      return;
    }
    const size_t ssidTokenLength = static_cast<size_t>(separator - arguments);
    const size_t passwordTokenLength = strlen(separator + 1);
    if (ssidTokenLength == 0 || ssidTokenLength >= sizeof(encoded) ||
        passwordTokenLength == 0 ||
        passwordTokenLength >= sizeof(encodedPassword)) {
      fail(usb, "BAD WIFI LENGTH");
      return;
    }
    memcpy(encoded, arguments, ssidTokenLength);
    encoded[ssidTokenLength] = '\0';
    snprintf(encodedPassword, sizeof(encodedPassword), "%s", separator + 1);

    char ssid[WifiTimeService::kMaxSsidLength + 1];
    char password[WifiTimeService::kMaxPasswordLength + 1];
    if (!decodeHexToken(encoded, ssid, sizeof(ssid), false) ||
        !decodeHexToken(encodedPassword, password, sizeof(password), true)) {
      fail(usb, "BAD WIFI ENCODING");
      return;
    }
    if (!wifiTimeService_.saveCredentials(ssid, password)) {
      fail(usb, "WIFI STORE FAILED");
      return;
    }
    sendLine(usb, "OK WIFI STORED");
    state_ = State::Success;
    errorText_[0] = '\0';
    return;
  }

  if (strcmp(command, "WIFI CLEAR") == 0) {
    if (!wifiTimeService_.clearCredentials()) {
      fail(usb, "WIFI CLEAR FAILED");
      return;
    }
    sendLine(usb, "OK WIFI CLEARED");
    state_ = State::Success;
    errorText_[0] = '\0';
    return;
  }

  if (strcmp(command, "NTP START") == 0) {
    if (!wifiTimeService_.startManualSync()) {
      fail(usb, "NO WIFI CONFIG");
      return;
    }
    sendLine(usb, "OK NTP STARTED");
    state_ = State::Success;
    errorText_[0] = '\0';
    return;
  }

  if (strcmp(command, "CLOSE") == 0) {
    sendLine(usb, "BYE");
    state_ = State::Closing;
    return;
  }

  fail(usb, "UNKNOWN COMMAND");
}

void TimeSyncServer::sendLine(Stream &usb, const char *line) {
  usb.print(line);
  usb.print('\n');
}

void TimeSyncServer::fail(Stream &usb, const char *message) {
  snprintf(errorText_, sizeof(errorText_), "%s", message);
  char response[80];
  snprintf(response, sizeof(response), "ERR %s", errorText_);
  sendLine(usb, response);
  state_ = State::Failed;
}

bool TimeSyncServer::decodeHexToken(const char *token, char *output,
                                    size_t capacity,
                                    bool allowDashAsEmpty) {
  if (token == nullptr || output == nullptr || capacity == 0) return false;
  if (allowDashAsEmpty && strcmp(token, "-") == 0) {
    output[0] = '\0';
    return true;
  }
  const size_t encodedLength = strlen(token);
  if (encodedLength == 0 || (encodedLength % 2) != 0 ||
      encodedLength / 2 >= capacity) {
    return false;
  }
  for (size_t index = 0; index < encodedLength; index += 2) {
    int8_t high = hexValue(token[index]);
    int8_t low = hexValue(token[index + 1]);
    if (high < 0 || low < 0) return false;
    output[index / 2] = static_cast<char>((high << 4) | low);
  }
  output[encodedLength / 2] = '\0';
  return true;
}

bool TimeSyncServer::encodeHexToken(const char *value, char *output,
                                    size_t capacity, bool dashWhenEmpty) {
  if (value == nullptr || output == nullptr || capacity == 0) return false;
  const size_t length = strlen(value);
  if (length == 0 && dashWhenEmpty) {
    if (capacity < 2) return false;
    output[0] = '-';
    output[1] = '\0';
    return true;
  }
  if (length == 0 || length * 2 + 1 > capacity) return false;
  static const char kHex[] = "0123456789abcdef";
  for (size_t index = 0; index < length; ++index) {
    const uint8_t byte = static_cast<uint8_t>(value[index]);
    output[index * 2] = kHex[byte >> 4];
    output[index * 2 + 1] = kHex[byte & 0x0F];
  }
  output[length * 2] = '\0';
  return true;
}

int8_t TimeSyncServer::hexValue(char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return -1;
}
