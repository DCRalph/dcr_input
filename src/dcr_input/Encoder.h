#pragma once

#include <functional>

#include "Controls.h"
#include "Pcnt.h"

namespace Controls
{
  /**
   * A rotary encoder in detents. Given pins, it decodes quadrature on one of the
   * chip's PCNT units in hardware; given `readCount`, it uses any other counter.
   */
  class Encoder : public Device
  {
  public:
    struct Config
    {
      int pinA = -1;
      int pinB = -1;
      Decoding decoding = Decoding::Full;
      Pull pull = Pull::Up;                                   // on A and B
      uint16_t glitchFilterNs = detail::kPcntMaxFilterNs;     // shorter pulses are bounce; 0 = off
      uint8_t countsPerDetent = 4;                            // 4 for Full decoding of a typical detented knob
      int8_t direction = 1;                                   // -1 flips clockwise

      std::function<int64_t()> readCount; // instead of pins: a raw count from elsewhere
    };

    explicit Encoder(Config config) : config_(std::move(config)) {}
    Encoder(int pinA, int pinB, uint8_t countsPerDetent = 4)
    {
      config_.pinA = pinA;
      config_.pinB = pinB;
      config_.countsPerDetent = countsPerDetent;
    }
    explicit Encoder(std::function<int64_t()> readCount, uint8_t countsPerDetent = 4)
    {
      config_.readCount = std::move(readCount);
      config_.countsPerDetent = countsPerDetent;
    }

    void begin(uint32_t nowMs) override;
    void update(uint32_t nowMs) override;

    /** One raw count. update() calls it with the hardware's running count. */
    void feed(int64_t count, uint32_t nowMs);

    /** False if pins were given but no PCNT unit was free (S3: 4, ESP32: 8). */
    bool ready() const { return config_.readCount || pcnt_.ready(); }

    /** Detents turned this frame, positive clockwise. Turns on frames nobody reads are gone. */
    int32_t delta() const { return delta_; }
    /** Detents since begin() or reset(). */
    int64_t position() const { return detent_ - zero_; }
    /** position() reads 0 from here. */
    void reset() { zero_ = detent_; }
    /** Raw counts since begin(), before direction and detents. */
    int64_t rawCount() const { return count_ - origin_; }

    /** countsPerDetent and direction apply at once; pins, decoding, pull and filter at the first begin(). */
    Config &config() { return config_; }

  private:
    int64_t readRaw();

    Config config_;
    detail::PcntCounter pcnt_;
    int16_t pcntLast_ = 0;
    int64_t pcntTotal_ = 0;

    int64_t origin_ = 0;
    int64_t count_ = 0;
    int64_t detent_ = 0;
    int64_t zero_ = 0;
    int32_t delta_ = 0;
  };
}
