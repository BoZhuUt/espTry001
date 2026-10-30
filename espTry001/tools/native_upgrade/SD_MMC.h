#pragma once
#include <map>
#include <vector>
#include <string>
#include <memory>
#include <cstring>
extern std::map<std::string, std::shared_ptr<std::vector<uint8_t>>> testFiles;
struct File {
  std::string path;
  bool valid = false, dir = false;
  size_t pos = 0;
  std::shared_ptr<std::vector<uint8_t>> bytes;
  std::vector<std::string> children;
  operator bool() const { return valid; }
  bool isDirectory() const { return dir; }
  const char *name() const { return path.c_str(); }
  size_t size() const { return bytes ? bytes->size() : 0; }
  size_t read(uint8_t *out, size_t n) { n = std::min(n, size() - pos); memcpy(out, bytes->data() + pos, n); pos += n; return n; }
  size_t write(const uint8_t *p, size_t n) { bytes->insert(bytes->end(), p, p + n); return n; }
  void flush() {} void close() {}
  File openNextFile() { if (pos >= children.size()) return {}; File f; f.path = children[pos++]; f.valid = true; f.bytes = testFiles[f.path]; return f; }
};
constexpr const char *FILE_WRITE = "w";
struct TestSD {
  bool exists(const char *p) { return std::string(p) == "/firmware" || testFiles.count(p); }
  bool mkdir(const char *) { return true; }
  bool remove(const char *p) { return testFiles.erase(p); }
  bool rename(const char *a, const char *b) { testFiles[b] = testFiles[a]; testFiles.erase(a); return true; }
  File open(const char *p, const char *mode = "r") {
    File f; f.path = p;
    if (f.path == "/firmware") { f.valid = f.dir = true; for (auto &it : testFiles) f.children.push_back(it.first); }
    else if (mode[0] == 'w' || testFiles.count(p)) {
      if (mode[0] == 'w') testFiles[p] = std::make_shared<std::vector<uint8_t>>();
      f.valid = true; f.bytes = testFiles[p];
    }
    return f;
  }
};
extern TestSD SD_MMC;
