#include "mobile_upgrade.h"
#include <SD_MMC.h>
#include <esp_heap_caps.h>
#include <ctype.h>
#include <string.h>

MobileUpgrade *MobileUpgrade::owner_ = nullptr;
namespace {
uint16_t le16(const uint8_t *p) { return p[0] | (p[1] << 8); }
uint32_t le32(const uint8_t *p) { return le16(p) | (uint32_t(le16(p + 2)) << 16); }
void put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
uint16_t crc16(const uint8_t *p, unsigned n) {
  uint16_t crc = 0;
  while (n--) {
    crc ^= uint16_t(*p++) << 8;
    for (unsigned i = 0; i < 8; ++i) crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
  }
  return crc;
}
}
MobileUpgrade::MobileUpgrade() {
  owner_ = this;
  // Register before Arduino starts the composite USB device (CDC on boot).
  msc_.vendorID("PYXIS"); msc_.productID("FW IMPORT"); msc_.productRevision("1.0");
  msc_.onRead(readDisk); msc_.onWrite(writeDisk); msc_.onStartStop(startStop);
  registered_ = msc_.begin(kDiskSize / 512, 512);
  msc_.mediaPresent(false);
}
bool MobileUpgrade::exposeDisk() {
  if (active() || !registered_) return false;
  if (!diskMutex_) diskMutex_ = xSemaphoreCreateMutex();
  if (!disk_) disk_ = static_cast<uint8_t *>(heap_caps_malloc(kDiskSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!disk_) disk_ = static_cast<uint8_t *>(malloc(kDiskSize));
  if (!disk_ || !diskMutex_) { snprintf(message, sizeof(message), "Not enough memory for USB disk"); return false; }
  xSemaphoreTake(diskMutex_, portMAX_DELAY);
  memset(disk_, 0, kDiskSize);
  // FAT12: 1 reserved sector, two 3-sector FATs, 64 root entries.
  uint8_t *b = disk_; b[0] = 0xEB; b[1] = 0x3C; b[2] = 0x90;
  memcpy(b + 3, "MSDOS5.0", 8); put16(b + 11, 512); b[13] = 1;
  put16(b + 14, 1); b[16] = 2; put16(b + 17, 64); put16(b + 19, 1024);
  b[21] = 0xF8; put16(b + 22, 3); put16(b + 24, 1); put16(b + 26, 1);
  b[36] = 0x80; b[38] = 0x29; memcpy(b + 43, "PYXIS_FW   ", 11);
  memcpy(b + 54, "FAT12   ", 8); b[510] = 0x55; b[511] = 0xAA;
  for (unsigned f : {1u, 4u}) { disk_[f * 512] = 0xF8; disk_[f * 512 + 1] = 0xFF; disk_[f * 512 + 2] = 0xFF; }
  memcpy(disk_ + 7 * 512, "PYXIS_FW   ", 11); disk_[7 * 512 + 11] = 8;
  ejected_ = false; visible_ = true;
  xSemaphoreGive(diskMutex_);
  msc_.mediaPresent(true);
  snprintf(message, sizeof(message), "Copy BIN to USB root; eject on PC"); return true;
}
bool MobileUpgrade::hideDisk() {
  if (!diskMutex_) return false;
  xSemaphoreTake(diskMutex_, portMAX_DELAY);
  if (visible_ && !ejected_) { xSemaphoreGive(diskMutex_); snprintf(message, sizeof(message), "Eject PYXIS_FW on computer first"); return false; }
  visible_ = false;
  xSemaphoreGive(diskMutex_); msc_.mediaPresent(false); return true;
}
void MobileUpgrade::discardDisk() {
  if (!diskMutex_) return;
  xSemaphoreTake(diskMutex_, portMAX_DELAY);
  visible_ = false;
  xSemaphoreGive(diskMutex_);
  msc_.mediaPresent(false);
  snprintf(message, sizeof(message), "Import cancelled; SD files unchanged");
}
int32_t MobileUpgrade::readDisk(uint32_t lba, uint32_t off, void *buf, uint32_t n) {
  auto *s = owner_; if (!s || !s->diskMutex_) return -1;
  xSemaphoreTake(s->diskMutex_, portMAX_DELAY);
  uint64_t pos = uint64_t(lba) * 512 + off;
  bool ok = s->visible_ && !s->ejected_ && pos + n <= kDiskSize;
  if (ok) memcpy(buf, s->disk_ + pos, n);
  xSemaphoreGive(s->diskMutex_); return ok ? int32_t(n) : -1;
}
int32_t MobileUpgrade::writeDisk(uint32_t lba, uint32_t off, uint8_t *buf, uint32_t n) {
  auto *s = owner_; if (!s || !s->diskMutex_) return -1;
  xSemaphoreTake(s->diskMutex_, portMAX_DELAY);
  uint64_t pos = uint64_t(lba) * 512 + off;
  bool ok = s->visible_ && !s->ejected_ && pos + n <= kDiskSize;
  if (ok) memcpy(s->disk_ + pos, buf, n);
  xSemaphoreGive(s->diskMutex_); return ok ? int32_t(n) : -1;
}
bool MobileUpgrade::startStop(uint8_t, bool start, bool eject) {
  auto *s = owner_; if (!s || !s->diskMutex_) return true;
  xSemaphoreTake(s->diskMutex_, portMAX_DELAY);
  if (eject && !start) s->ejected_ = true;
  xSemaphoreGive(s->diskMutex_); return true;
}
bool MobileUpgrade::importFiles() {
  if (visible_ || !disk_) return false;
  // Accept only our own layout, including fragmented FAT12 cluster chains.
  if (le16(disk_ + 11) != 512 || disk_[13] != 1 || le16(disk_ + 14) != 1 ||
      disk_[16] != 2 || le16(disk_ + 17) != 64 || le16(disk_ + 19) != 1024 || le16(disk_ + 22) != 3) {
    snprintf(message, sizeof(message), "USB reformatted; import rejected"); return false;
  }
  if (!SD_MMC.exists("/firmware") && !SD_MMC.mkdir("/firmware")) { snprintf(message, sizeof(message), "SD card unavailable"); return false; }
  if (!image_) image_ = static_cast<uint8_t *>(heap_caps_malloc(kMaxImage, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!image_) image_ = static_cast<uint8_t *>(malloc(kMaxImage));
  if (!image_) { snprintf(message, sizeof(message), "Insufficient firmware memory"); return false; }
  imageSize_ = 0; unsigned imported = 0;
  for (unsigned i = 0; i < 64; ++i) {
    const uint8_t *e = disk_ + 7 * 512 + i * 32;
    if (!e[0]) break;
    if (e[0] == 0xE5 || (e[11] & 0x18) || (e[11] & 0x0F) == 0x0F || memcmp(e + 8, "BIN", 3)) continue;
    uint32_t size = le32(e + 28); uint16_t cluster = le16(e + 26);
    if (size < 8 || size > kMaxImage) { snprintf(message, sizeof(message), "BIN invalid size (max 108 KiB)"); return false; }
    uint8_t visited[128] = {}; uint32_t copied = 0;
    while (copied < size) {
      if (cluster < 2 || cluster >= 1015 || (visited[cluster / 8] & (1 << (cluster % 8)))) {
        snprintf(message, sizeof(message), "Invalid FAT chain; recopy BIN"); return false;
      }
      visited[cluster / 8] |= 1 << (cluster % 8);
      uint32_t n = min(uint32_t(512), size - copied);
      memcpy(image_ + copied, disk_ + (11 + cluster - 2) * 512, n); copied += n;
      unsigned f = cluster + cluster / 2; uint16_t pair = le16(disk_ + 512 + f);
      cluster = (cluster & 1) ? pair >> 4 : pair & 0xFFF;
    }
    if (cluster < 0xFF8) { snprintf(message, sizeof(message), "Invalid FAT end marker"); return false; }
    uint32_t sp = le32(image_), pc = le32(image_ + 4);
    if ((sp & 0x2FFE0000) != 0x20000000 || !(pc & 1) || (pc & ~1u) < 0x08005000 || (pc & ~1u) >= 0x08005000 + size) {
      snprintf(message, sizeof(message), "Need ST588 APP BIN at 0x08005000"); return false;
    }
    char name[13] = {}; unsigned len = 0;
    for (unsigned j = 0; j < 8 && e[j] != ' '; ++j) {
      if (!isalnum(e[j]) && e[j] != '_' && e[j] != '-') { snprintf(message, sizeof(message), "Use ASCII 8.3 firmware names"); return false; }
      name[len++] = e[j];
    }
    if (!len) return false;
    memcpy(name + len, ".BIN", 5);
    char path[80]; snprintf(path, sizeof(path), "/firmware/%s", name);
    if (SD_MMC.exists(path)) { snprintf(message, sizeof(message), "Name exists; rename BIN on PC"); return false; }
    SD_MMC.remove("/firmware/import.tmp");
    File out = SD_MMC.open("/firmware/import.tmp", FILE_WRITE);
    bool ok = out && out.write(image_, size) == size; out.flush(); out.close();
    if (ok) {
      // Verify the saved bytes before making the file selectable for upgrading.
      File check = SD_MMC.open("/firmware/import.tmp");
      uint8_t chunk[256];
      ok = check && check.size() == size;
      for (uint32_t offset = 0; ok && offset < size; offset += sizeof(chunk)) {
        size_t count = min(uint32_t(sizeof(chunk)), size - offset);
        ok = check.read(chunk, count) == count && !memcmp(chunk, image_ + offset, count);
      }
      check.close();
    }
    if (!ok || !SD_MMC.rename("/firmware/import.tmp", path)) {
      SD_MMC.remove("/firmware/import.tmp"); snprintf(message, sizeof(message), "SD save failed"); return false;
    }
    ++imported;
  }
  scanFiles(); snprintf(message, sizeof(message), "%u firmware(s) saved", imported); return true;
}
bool MobileUpgrade::scanFiles() {
  fileCount = 0; imageSize_ = 0; phase_ = Phase::Idle;
  if (!SD_MMC.exists("/firmware") && !SD_MMC.mkdir("/firmware")) { snprintf(message, sizeof(message), "SD card unavailable"); return false; }
  File dir = SD_MMC.open("/firmware"); if (!dir || !dir.isDirectory()) return false;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    const char *name = strrchr(f.name(), '/'); name = name ? name + 1 : f.name();
    String upper(name); upper.toUpperCase();
    if (!f.isDirectory() && strlen(name) < sizeof(files[0]) && upper.endsWith(".BIN") && fileCount < kMaxFiles) snprintf(files[fileCount++], sizeof(files[0]), "%s", name);
    f.close();
  }
  dir.close(); snprintf(message, sizeof(message), "ST588 / 9600 8N1 / YMODEM-1K"); return true;
}
bool MobileUpgrade::loadFile(unsigned index) {
  imageSize_ = 0; if (index >= fileCount) return false;
  if (!image_) image_ = static_cast<uint8_t *>(heap_caps_malloc(kMaxImage, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!image_) image_ = static_cast<uint8_t *>(malloc(kMaxImage));
  if (!image_) { snprintf(message, sizeof(message), "Insufficient firmware memory"); return false; }
  char path[96]; snprintf(path, sizeof(path), "/firmware/%s", files[index]);
  File f = SD_MMC.open(path); uint32_t size = f ? f.size() : 0;
  bool ok = size >= 8 && size <= kMaxImage && f.read(image_, size) == size; f.close();
  if (ok) {
    uint32_t sp = le32(image_), pc = le32(image_ + 4);
    ok = (sp & 0x2FFE0000) == 0x20000000 && (pc & 1) && (pc & ~1u) >= 0x08005000 && (pc & ~1u) < 0x08005000 + size;
  }
  if (!ok) { snprintf(message, sizeof(message), "Invalid ST588 APP BIN (max 108 KiB)"); return false; }
  imageSize_ = size; return true;
}
void MobileUpgrade::packet(unsigned length, uint8_t seq) {
  tx_[0] = length == 1024 ? 2 : 1; tx_[1] = seq; tx_[2] = ~seq;
  uint16_t crc = crc16(tx_ + 3, length); tx_[length + 3] = crc >> 8; tx_[length + 4] = crc;
  txLength_ = length + 5;
}
void MobileUpgrade::header(bool empty) {
  memset(tx_ + 3, 0, 128);
  if (!empty) { memcpy(tx_ + 3, "APP.BIN", 8); snprintf(reinterpret_cast<char *>(tx_ + 11), 120, "%lu ", (unsigned long)imageSize_); }
  packet(128, 0);
}
void MobileUpgrade::data() {
  blockBytes_ = min(uint32_t(1024), imageSize_ - sent_);
  memset(tx_ + 3, 0x1A, 1024); memcpy(tx_ + 3, image_ + sent_, blockBytes_); packet(1024, sequence_);
}
void MobileUpgrade::send(Rs485Service &, Phase p) {
  phase_ = p; txOffset_ = 0; deadline_ = millis() + 15000;
}
void MobileUpgrade::fail(const char *reason) { phase_ = Phase::Failed; snprintf(message, sizeof(message), "%s", reason); }
bool MobileUpgrade::start(Rs485Service &r) {
  if (!imageSize_ || visible_ || !r.enterUpgradePassAndWait()) { fail("Firmware/UART not ready"); return false; }
  sent_ = 0; sequence_ = 1; retries_ = 0; txLength_ = txOffset_ = 0;
  lineLength_ = 0; line_[0] = 0; phase_ = Phase::WaitingBoot; deadline_ = millis() + 60000;
  bootSeen_ = menuSeen_ = false; lastRxMs_ = millis();
  snprintf(message, sizeof(message), "Connect ONE sensor and power it on"); return true;
}
void MobileUpgrade::cancel(Rs485Service &r) {
  if (active()) { const uint8_t ca[] = {0x18, 0x18}; r.enqueueRawBytes(ca, sizeof(ca)); }
  phase_ = Phase::Idle; txLength_ = txOffset_ = 0;
}
void MobileUpgrade::update(Rs485Service &r) {
  if (!active()) return;
  if (txOffset_ < txLength_) {
    txOffset_ += r.enqueueRawBytes(tx_ + txOffset_, txLength_ - txOffset_);
    if (txOffset_ < txLength_) { if (int32_t(millis() - deadline_) >= 0) fail("UART transmit stalled"); return; }
  }
  uint8_t b;
  while (r.readRawResponse(&b, 1)) {
    lastRxMs_ = millis();
    if (b == 0x18) { fail("Sensor aborted upgrade"); return; }
    if (b >= 32 && b < 127) { if (lineLength_ >= sizeof(line_) - 1) lineLength_ = 0; line_[lineLength_++] = b; line_[lineLength_] = 0; }
    if (phase_ == Phase::WaitingBoot && strstr(line_, "Press abc")) bootSeen_ = true;
    if (phase_ == Phase::Menu && strstr(line_, "Internal Flash")) menuSeen_ = true;
    if ((phase_ == Phase::Crc || phase_ == Phase::HeaderCrc || phase_ == Phase::FinalCrc) && b == 'C') {
      retries_ = 0;
      if (phase_ == Phase::Crc) { header(false); send(r, Phase::HeaderAck); }
      else if (phase_ == Phase::HeaderCrc) { data(); send(r, Phase::DataAck); }
      else { header(true); send(r, Phase::FinalAck); }
      break;
    }
    if (b == 6) {
      retries_ = 0;
      if (phase_ == Phase::HeaderAck) { phase_ = Phase::HeaderCrc; txLength_ = txOffset_ = 0; }
      else if (phase_ == Phase::DataAck) {
        sent_ += blockBytes_; ++sequence_;
        if (sent_ < imageSize_) { data(); send(r, Phase::DataAck); }
        else { tx_[0] = 4; txLength_ = 1; send(r, Phase::EotAck); }
        break;
      } else if (phase_ == Phase::EotAck) { phase_ = Phase::FinalCrc; txLength_ = txOffset_ = 0; }
      else if (phase_ == Phase::FinalAck) { phase_ = Phase::Confirm; txLength_ = txOffset_ = 0; lineLength_ = 0; line_[0] = 0; deadline_ = millis() + 10000; }
    } else if (b == 0x15 && txLength_ && phase_ != Phase::Confirm) {
      if (++retries_ > 5) { fail("Too many rejected packets"); return; }
      send(r, phase_); break;
    }
    if (phase_ == Phase::Confirm && strstr(line_, "Programming Completed Successfully!")) {
      phase_ = Phase::Success; snprintf(message, sizeof(message), "Upgrade successful; sensor restarting"); return;
    }
  }
  // RS485 is half duplex: wait until the entire printed prompt has finished.
  if ((uint32_t)(millis() - lastRxMs_) >= 80) {
    if (phase_ == Phase::WaitingBoot && bootSeen_) {
      memcpy(tx_, "abc", 3); txLength_ = 3; send(r, Phase::Menu); lineLength_ = 0; line_[0] = 0;
    } else if (phase_ == Phase::Menu && menuSeen_) {
      tx_[0] = '1'; txLength_ = 1; send(r, Phase::Crc); lineLength_ = 0; line_[0] = 0;
    }
  }
  if (int32_t(millis() - deadline_) >= 0) {
    // This receiver rejects duplicate data sequence numbers. A lost ACK must
    // terminate instead of blindly resending and risking wrong advancement.
    fail(phase_ == Phase::WaitingBoot ? "No boot detected; repower sensor" : "Boot timeout; repower and retry");
  } else if (phase_ != Phase::WaitingBoot) snprintf(message, sizeof(message), "ST588 upgrading: %u%%", progress());
}
