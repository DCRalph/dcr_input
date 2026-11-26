#pragma once

#include <stdint.h>

#include "Controls.h"

namespace Controls
{
  enum class Decoding : uint8_t
  {
    Full,   // every edge of A and B: 4 counts per quadrature cycle
    Half,   // both edges of A: 2 counts per cycle
    Single, // falling edge of A: 1 count per cycle
  };

  namespace detail
  {
    // The hardware counter is 16-bit and restarts from 0 at +-kPcntLimit.
    constexpr int16_t kPcntLimit = 30000;
    // The longest glitch the filter can take: 1023 APB cycles at 80 MHz.
    constexpr uint16_t kPcntMaxFilterNs = 12700;

    /**
     * Counts moved between two readings of the hardware counter, across a restart
     * at the limit. Holds while fewer than kPcntLimit / 2 counts pass between reads.
     */
    int32_t pcntDelta(int16_t before, int16_t after);

    // One PCNT unit decoding a quadrature encoder. Polled; it installs no interrupt.
    class PcntCounter
    {
    public:
      PcntCounter() = default;
      PcntCounter(const PcntCounter &) = delete;
      PcntCounter &operator=(const PcntCounter &) = delete;

      /** Claims a free unit. False if the chip has none left, or no PCNT at all. */
      bool begin(int pinA, int pinB, Decoding decoding, Pull pull, uint16_t glitchFilterNs);
      bool ready() const { return ready_; }
      int16_t read();

    private:
      bool ready_ = false;
      int unit_ = -1;          // IDF 4: the unit number
      void *handle_ = nullptr; // IDF 5: the pcnt_unit_handle_t
    };
  }
}
