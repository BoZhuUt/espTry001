#include "sd_storage.h"
#include "board/board_config.h"
#include "debug_log.h"

const char *SdStorage::stateText() const {
  switch (state_) {
    case State::NotInitialized: return "Missing";
    case State::Ready:          return "Ready";
    case State::Error:          return "Error";
  }
  return "Unknown";
}

bool SdStorage::begin() {
  if (state_ == State::Ready) return true;

  int8_t data1 = BoardConfig::kSdOneBit ? -1 : BoardConfig::kSdD1;
  int8_t data2 = BoardConfig::kSdOneBit ? -1 : BoardConfig::kSdD2;
  int8_t data3 = BoardConfig::kSdOneBit ? -1 : BoardConfig::kSdD3;
  if (!SD_MMC.setPins(BoardConfig::kSdClk, BoardConfig::kSdCmd,
                      BoardConfig::kSdD0, data1, data2, data3)) {
    snprintf(lastError_, sizeof(lastError_), "SD_MMC setPins failed");
    state_ = State::Error;
    DEBUG_LOG_PRINTLN("[SD] SD_MMC pin mapping failed");
    return false;
  }

  // On the 2.8 board D3 is not part of the 1-bit bus, but the board circuit
  // requires it to be held high while the card is initialized.
  if (BoardConfig::kSdD3MustBeHeldHigh) {
    pinMode(BoardConfig::kSdD3, OUTPUT);
    digitalWrite(BoardConfig::kSdD3, HIGH);
    delay(10);
  }

  // Never format a user's card automatically when mounting fails.
  if (!SD_MMC.begin("/sdcard", BoardConfig::kSdOneBit, false)) {
    snprintf(lastError_, sizeof(lastError_), "SD_MMC begin failed");
    state_ = State::NotInitialized;
    DEBUG_LOG_PRINTLN("[SD] SD_MMC begin failed, no card?");
    return false;
  }

  uint64_t total = SD_MMC.cardSize();
  if (total == 0) {
    snprintf(lastError_, sizeof(lastError_), "Card size = 0");
    state_ = State::NotInitialized;
    DEBUG_LOG_PRINTLN("[SD] Card size reported as 0");
    SD_MMC.end();
    return false;
  }

  state_ = State::Ready;
  lastError_[0] = '\0';

  if (!ensureDir("/logs")) {
    state_ = State::Error;
    return false;
  }

  DEBUG_LOG_PRINTF("[SD] %s, %.1f GB, free %.1f MB\n",
                   cardType(),
                   totalBytes() / 1073741824.0,
                   freeBytes() / 1048576.0);

  return true;
}

uint64_t SdStorage::totalBytes() const {
  if (state_ != State::Ready) return 0;
  return SD_MMC.totalBytes();
}

uint64_t SdStorage::usedBytes() const {
  if (state_ != State::Ready) return 0;
  return SD_MMC.usedBytes();
}

uint64_t SdStorage::freeBytes() const {
  if (state_ != State::Ready) return 0;
  uint64_t total = SD_MMC.totalBytes();
  uint64_t used = SD_MMC.usedBytes();
  return used <= total ? total - used : 0;
}

const char *SdStorage::cardType() const {
  if (state_ != State::Ready) return "N/A";
  switch (SD_MMC.cardType()) {
    case CARD_MMC:    return "MMC";
    case CARD_SD:     return "SDSC";
    case CARD_SDHC:   return "SDHC";
    default:          return "Unknown";
  }
}

bool SdStorage::ensureDir(const char *path) {
  if (state_ != State::Ready) return false;
  if (SD_MMC.exists(path)) return true;
  if (SD_MMC.mkdir(path)) {
    DEBUG_LOG_PRINTF("[SD] Created directory: %s\n", path);
    return true;
  }
  snprintf(lastError_, sizeof(lastError_), "mkdir %s failed", path);
  DEBUG_LOG_PRINTF("[SD] mkdir %s failed\n", path);
  return false;
}

File SdStorage::open(const char *path, const char *mode) {
  if (state_ != State::Ready) return File();
  return SD_MMC.open(path, mode);
}

bool SdStorage::exists(const char *path) {
  if (state_ != State::Ready) return false;
  return SD_MMC.exists(path);
}

void SdStorage::setError(const char *message) {
  snprintf(lastError_, sizeof(lastError_), "%.63s", message);
  state_ = State::Error;
  DEBUG_LOG_PRINTF("[SD] Error: %s\n", message);
}
