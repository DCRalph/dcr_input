#pragma once

#include <functional>

#include "Controls.h"

namespace Controls
{
  /**
   * A rotary encoder in detents. Counting the pulses is left to hardware, e.g.
   * ESP32Encoder on the PCNT unit, and read through `readCount`.
   */
  class Encoder : public Device
  {
  public:
    struct Config
    {
      std::function<int64_t()> readCount; // raw quadrature count
      uint8_t countsPerDetent = 4;        // 4 for full-quad decoding of a typical detented knob
      int8_t direction = 1;               // -1 flips clockwise
    };

    explicit Encoder(Config config) : config_(std::move(config)) {}
    explicit Encoder(std::function<int64_t()> readCount, uint8_t countsPerDetent = 4)
    {
      config_.readCount = std::move(readCount);
      config_.countsPerDetent = countsPerDetent;
    }

    void begin(uint32_t nowMs) override;
    void update(uint32_t nowMs) override;

    /** One raw count. update() calls it with readCount(). */
    void feed(int64_t count, uint32_t nowMs);

    /** Detents turned this frame, positive clockwise. Turns on frames nobody reads are gone. */
    int32_t delta() const { return delta_; }
    /** Detents since begin() or reset(). */
    int64_t position() const { return detent_ - zero_; }
    /** position() reads 0 from here. */
    void reset() { zero_ = detent_; }
    int64_t rawCount() const { return count_; }

  private:
    Config config_;
    int64_t origin_ = 0;
    int64_t count_ = 0;
    int64_t detent_ = 0;
    int64_t zero_ = 0;
    int32_t delta_ = 0;
  };
}
