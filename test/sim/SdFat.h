#pragma once
#include "Arduino.h"
#include "SPI.h"
#include <string>

// SdFat のうち、スケッチが使う部分だけをホストのファイルシステムで模したもの
struct SdCard {
  uint32_t sectorCount();
};

class FsFile {
public:
  FsFile() {}
  bool open(const char *path, int oflag = O_RDONLY);
  bool openNext(FsFile *dir, int oflag = O_RDONLY);
  bool isOpen() const { return open_; }
  bool isDir() const { return isdir_; }
  bool close();
  int read(void *buf, size_t n);
  size_t write(const void *buf, size_t n);
  bool seekSet(uint64_t pos);
  uint64_t fileSize();
  size_t getName(char *name, size_t size);

private:
  bool open_ = false, isdir_ = false;
  int fd_ = -1;
  int flags_ = 0;
  uint32_t gen_ = 0;
  std::string name_;
  void *dirp_ = nullptr;
};

class SdFat {
public:
  bool begin(uint8_t csPin = 10);
  void end();
  SdCard *card();
  bool exists(const char *path);
  bool remove(const char *path);
  bool rename(const char *oldPath, const char *newPath);
};
