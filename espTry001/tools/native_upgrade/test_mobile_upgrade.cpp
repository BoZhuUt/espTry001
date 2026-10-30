#include "services/mobile_upgrade.h"
#include <SD_MMC.h>
#include <cassert>
#include <cstdio>
uint32_t testTime = 0;
std::map<std::string, std::shared_ptr<std::vector<uint8_t>>> testFiles;
TestSD SD_MMC;
int32_t (*USBMSC::read)(uint32_t, uint32_t, void *, uint32_t) = nullptr;
int32_t (*USBMSC::write)(uint32_t, uint32_t, uint8_t *, uint32_t) = nullptr;
bool (*USBMSC::stop)(uint8_t, bool, bool) = nullptr;

using Phase = MobileUpgrade::Phase;
uint16_t crc(const uint8_t *p, size_t n) {
  uint16_t c = 0;
  while (n--) { c ^= uint16_t(*p++) << 8; for (int i = 0; i < 8; ++i) c = (c & 0x8000) ? (c << 1) ^ 0x1021 : c << 1; }
  return c;
}
void put32(uint8_t *p, uint32_t n) { for (unsigned i = 0; i < 4; ++i) p[i] = n >> (8 * i); }
void fat(std::vector<uint8_t> &disk, unsigned c, unsigned next) {
  unsigned off = 512 + c + c / 2;
  if (c & 1) { disk[off] = (disk[off] & 15) | (next << 4); disk[off + 1] = next >> 4; }
  else { disk[off] = next; disk[off + 1] = (disk[off + 1] & 0xF0) | (next >> 8); }
}
std::vector<uint8_t> makeDisk(MobileUpgrade &m, const std::vector<uint8_t> &image) {
  assert(m.exposeDisk());
  std::vector<uint8_t> disk(512 * 1024);
  assert(USBMSC::read(0, 0, disk.data(), disk.size()) == int(disk.size()));
  auto *entry = disk.data() + 7 * 512 + 32;
  memcpy(entry, "APP     BIN", 11); entry[11] = 0x20; entry[26] = 2;
  put32(entry + 28, image.size());
  unsigned blocks = (image.size() + 511) / 512;
  for (unsigned i = 0; i < blocks; ++i) {
    unsigned c = 2 + i * 3;
    memcpy(disk.data() + (11 + c - 2) * 512, image.data() + i * 512, std::min(size_t(512), image.size() - i * 512));
    fat(disk, c, i + 1 == blocks ? 0xFFF : c + 3);
  }
  memcpy(disk.data() + 4 * 512, disk.data() + 512, 3 * 512);
  return disk;
}
void writeDisk(std::vector<uint8_t> &disk) {
  assert(USBMSC::write(0, 0, disk.data(), disk.size()) == int(disk.size()));
  assert(USBMSC::write(1024, 0, disk.data(), 1) == -1);
}
void receive(MobileUpgrade &m, Rs485Service &r, const char *text) {
  for (; *text; ++text) r.rx.push_back(*text);
  m.update(r);
}
void receiveByte(MobileUpgrade &m, Rs485Service &r, uint8_t c) { r.rx.push_back(c); m.update(r); }
std::vector<uint8_t> transmitted(MobileUpgrade &m, Rs485Service &r, unsigned size) {
  for (unsigned i = 0; r.tx.size() < size && i < 100; ++i) { ++testTime; m.update(r); }
  assert(r.tx.size() == size);
  auto out = r.tx; r.tx.clear(); return out;
}
void handshake(MobileUpgrade &m, Rs485Service &r) {
  assert(m.start(r));
  receive(m, r, "DO Probe Startup\r\nBoot to App in 5 sec later\r\nPress abc to interrupt ......\r\n");
  assert(r.tx.empty()); testTime += 80; m.update(r);
  auto abc = transmitted(m, r, 3); assert(std::string(abc.begin(), abc.end()) == "abc");
  receive(m, r, "  Download Image To the Porbe Internal Flash ----------- 1\r\n\n");
  assert(r.tx.empty()); testTime += 80; m.update(r);
  assert(transmitted(m, r, 1)[0] == '1');
  receiveByte(m, r, 'C');
  auto header = transmitted(m, r, 133);
  assert(header[0] == 1 && header[1] == 0 && header[2] == 255);
  assert(!strcmp(reinterpret_cast<char *>(header.data() + 3), "APP.BIN"));
  uint16_t check = crc(header.data() + 3, 128);
  assert(header[131] == (check >> 8) && header[132] == (check & 255));
  // ACK and C may arrive together, and must both survive the state transition.
  r.rx.push_back(6); r.rx.push_back('C'); m.update(r);
  assert(m.phase() == Phase::DataAck);
}
int main() {
  MobileUpgrade m; Rs485Service r;
  std::vector<uint8_t> image(MobileUpgrade::kMaxImage);
  for (unsigned i = 8; i < image.size(); ++i) image[i] = i * 37;
  put32(image.data(), 0x20010000); put32(image.data() + 4, 0x08005009);
  auto disk = makeDisk(m, image); writeDisk(disk);
  assert(!m.hideDisk()); // Prevent reading half-written host files.
  assert(USBMSC::stop(0, false, true)); assert(m.hideDisk());
  assert(m.importFiles()); assert(m.fileCount == 1);
  assert(*testFiles.at("/firmware/APP.BIN") == image);
  assert(m.loadFile(0)); handshake(m, r);
  unsigned sent = 0, sequence = 1;
  while (sent < image.size()) {
    auto packet = transmitted(m, r, 1029);
    assert(packet[0] == 2 && packet[1] == uint8_t(sequence) && packet[2] == uint8_t(~sequence));
    unsigned n = std::min(size_t(1024), image.size() - sent);
    assert(!memcmp(packet.data() + 3, image.data() + sent, n));
    uint16_t check = crc(packet.data() + 3, 1024);
    assert(packet[1027] == (check >> 8) && packet[1028] == (check & 255));
    if (sequence == 1) { receiveByte(m, r, 0x15); assert(transmitted(m, r, 1029) == packet); }
    receiveByte(m, r, 6); sent += n; ++sequence;
  }
  assert(m.progress() == 100); assert(transmitted(m, r, 1)[0] == 4);
  receiveByte(m, r, 0x15); assert(transmitted(m, r, 1)[0] == 4); // Two-EOT peers.
  r.rx.push_back(6); r.rx.push_back('C'); m.update(r);
  auto end = transmitted(m, r, 133); assert(end[0] == 1 && end[1] == 0 && end[3] == 0);
  receiveByte(m, r, 6); assert(m.phase() == Phase::Confirm);
  receive(m, r, "\n\n\r Programming Completed Successfully!\n\r"); assert(m.phase() == Phase::Success);

  assert(m.loadFile(0)); handshake(m, r); transmitted(m, r, 1029);
  testTime += 15001; m.update(r); assert(m.phase() == Phase::Failed); assert(r.tx.empty());
  assert(m.start(r)); testTime += 60001; m.update(r); assert(m.phase() == Phase::Failed);
  assert(m.start(r)); receiveByte(m, r, 0x18); assert(m.phase() == Phase::Failed);

  disk = makeDisk(m, image); fat(disk, 2, 2); writeDisk(disk);
  USBMSC::stop(0, false, true); assert(m.hideDisk()); assert(!m.importFiles());
  assert(*testFiles.at("/firmware/APP.BIN") == image); // Never replace known firmware.
  testFiles.clear(); image.resize(1500); disk = makeDisk(m, image); writeDisk(disk);
  USBMSC::stop(0, false, true); assert(m.hideDisk()); assert(m.importFiles());
  assert(m.loadFile(0)); handshake(m, r); transmitted(m, r, 1029); receiveByte(m, r, 6);
  auto tail = transmitted(m, r, 1029);
  assert(!memcmp(tail.data() + 3, image.data() + 1024, 476));
  for (unsigned i = 476; i < 1024; ++i) assert(tail[i + 3] == 0x1A);
  receiveByte(m, r, 6); assert(transmitted(m, r, 1)[0] == 4);
  r.rx.push_back(6); r.rx.push_back('C'); m.update(r); // Supplied ST Boot: one EOT.
  transmitted(m, r, 133); receiveByte(m, r, 6);
  receive(m, r, "Programming Completed Successfully!"); assert(m.phase() == Phase::Success);
  assert(m.start(r));
  m.cancel(r); assert(!m.active());
  assert(r.tx.size() == 2 && r.tx[0] == 0x18 && r.tx[1] == 0x18);
  assert(m.exposeDisk()); m.discardDisk(); assert(!m.diskVisible());
  uint8_t byte; assert(USBMSC::read(0, 0, &byte, 1) == -1);
  puts("PASS: fragmented FAT12 import, eject gate, 108 KiB YMODEM, short writes, CRC/NAK, EOT, success confirmation, lost ACK, timeout, CAN, cyclic FAT, partial last block");
}
