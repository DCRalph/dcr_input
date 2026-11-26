#pragma once
// A scripted I2C bus: one device at `address` with a register file, and a
// queue of bytes for requestFrom. Records what was written.
#include <stdint.h>
#include <vector>

class TwoWire
{
public:
  uint8_t address = 0;
  bool present = true;
  std::vector<uint8_t> written;   // every byte written, across transactions
  std::vector<uint8_t> nextRead;  // bytes the next requestFrom returns
  uint8_t registers[16] = {};     // used when registerMode is on
  bool registerMode = false;

  void beginTransmission(uint8_t addr) { target_ = addr; }
  void write(uint8_t b)
  {
    written.push_back(b);
    reg_ = b;
  }
  uint8_t endTransmission() { return present && target_ == address ? 0 : 2; }
  uint8_t requestFrom(uint8_t addr, uint8_t n)
  {
    buffer_.clear();
    if (!present || addr != address)
      return 0;
    if (registerMode)
      buffer_.push_back(registers[reg_ & 15]);
    else
      buffer_ = nextRead;
    pos_ = 0;
    return buffer_.size() >= n ? n : static_cast<uint8_t>(buffer_.size());
  }
  int read() { return pos_ < buffer_.size() ? buffer_[pos_++] : -1; }

private:
  uint8_t target_ = 0, reg_ = 0;
  std::vector<uint8_t> buffer_;
  size_t pos_ = 0;
};

extern TwoWire Wire;
