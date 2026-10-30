#pragma once

#include <Arduino.h>
#include <USBMSC.h>
#include <freertos/semphr.h>
#include "services/rs485_service.h"

// SD operations run only inside StorageTask; UART bytes use Rs485Service.
// The PC sees a separate FAT12 staging disk, never the mounted SD filesystem.
class MobileUpgrade {
 public:
  static constexpr size_t kMaxImage = 0x1B000;
  static constexpr unsigned kMaxFiles = 16;
  enum class Phase : uint8_t { Idle, WaitingBoot, Menu, Crc, HeaderAck,
    HeaderCrc, DataAck, EotAck, FinalCrc, FinalAck, Confirm, Success, Failed };
  MobileUpgrade();
  bool exposeDisk();
  bool hideDisk();
  void discardDisk();
  bool diskVisible() const { return visible_; }
  bool importFiles();                 // StorageTask only, after hideDisk
  bool scanFiles();                   // StorageTask only
  bool loadFile(unsigned index);      // StorageTask only
  bool start(Rs485Service &rs485);
  void update(Rs485Service &rs485);
  void cancel(Rs485Service &rs485);
  bool active() const { return phase_ != Phase::Idle && phase_ != Phase::Success && phase_ != Phase::Failed; }
  Phase phase() const { return phase_; }
  unsigned progress() const { return imageSize_ ? (100 * sent_ / imageSize_) : 0; }
  unsigned fileCount = 0;
  char files[kMaxFiles][64] = {};
  char message[96] = "Select firmware or USB import";
 private:
  static MobileUpgrade *owner_;
  static constexpr size_t kDiskSize = 512 * 1024;
  static int32_t readDisk(uint32_t lba, uint32_t offset, void *buf, uint32_t size);
  static int32_t writeDisk(uint32_t lba, uint32_t offset, uint8_t *buf, uint32_t size);
  static bool startStop(uint8_t, bool start, bool eject);
  void packet(unsigned length, uint8_t sequence);
  void header(bool empty);
  void data();
  void send(Rs485Service &rs485, Phase phase);
  void fail(const char *reason);
  USBMSC msc_;
  SemaphoreHandle_t diskMutex_ = nullptr;
  uint8_t *disk_ = nullptr;
  uint8_t *image_ = nullptr;
  bool visible_ = false;
  bool registered_ = false;
  bool ejected_ = false;
  uint32_t imageSize_ = 0, sent_ = 0, blockBytes_ = 0;
  uint8_t sequence_ = 1;
  uint8_t tx_[1029] = {};
  size_t txLength_ = 0, txOffset_ = 0;
  uint32_t deadline_ = 0;
  unsigned retries_ = 0;
  Phase phase_ = Phase::Idle;
  char line_[128] = {};
  unsigned lineLength_ = 0;
  uint32_t lastRxMs_ = 0;
  bool bootSeen_ = false, menuSeen_ = false;
};
