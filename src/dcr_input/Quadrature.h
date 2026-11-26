#pragma once

#include <stdint.h>

#include "Controls.h"

namespace Controls
{
  enum class Decoding : uint8_t
  {
    Full,   // every edge of A and B: 4 counts per quadrature cycle
    Half,   // both edges of A: 2 counts per cycle
    Single, // one edge of A: 1 count per cycle
  };

  namespace detail
  {
    // The PCNT counter is 16-bit and restarts from 0 at +-kPcntLimit.
    constexpr int16_t kPcntLimit = 30000;
    // The longest glitch the PCNT filter can take: 1023 APB cycles at 80 MHz.
    constexpr uint16_t kPcntMaxFilterNs = 12700;

    /**
     * Counts moved between two readings of the PCNT counter, across a restart at
     * the limit. Holds while fewer than kPcntLimit / 2 counts pass between reads.
     */
    int32_t pcntDelta(int16_t before, int16_t after);

    /** Counts for one move from AB state `prev` to `curr` (A is bit 1): +-1, or 0 for none or a skip. */
    int8_t quadratureStep(uint8_t prev, uint8_t curr);

    /** Full-decoding counts as `decoding` would count them, rounded down. */
    int64_t scaleCounts(int64_t fullCounts, Decoding decoding);

    // A quadrature counter on a PCNT unit where one is free, else on pin-change
    // interrupts (ESP8266, ESP32-C3, or every unit taken). Either way, A leading B
    // counts down. PCNT is polled and filters glitches in hardware.
    class QuadratureCounter
    {
    public:
      QuadratureCounter() = default;
      QuadratureCounter(const QuadratureCounter &) = delete;
      QuadratureCounter &operator=(const QuadratureCounter &) = delete;

      bool begin(int pinA, int pinB, Decoding decoding, Pull pull, uint16_t glitchFilterNs);
      bool ready() const { return backend_ != Backend::None; }
      bool usesPcnt() const { return backend_ == Backend::Pcnt; }
      /** Counts since begin(). */
      int64_t total();

      // Written by the interrupt handler.
      struct IsrState
      {
        uint8_t pinA = 0, pinB = 0;
        volatile uint8_t ab = 0;
        volatile int32_t count = 0;
      };

    private:
      enum class Backend : uint8_t
      {
        None,
        Pcnt,
        Interrupts,
      };

      bool beginPcnt(int pinA, int pinB, Decoding decoding, Pull pull, uint16_t glitchFilterNs);
      bool beginInterrupts(int pinA, int pinB, Pull pull);
      int16_t readPcnt();

      Backend backend_ = Backend::None;
      Decoding decoding_ = Decoding::Full;
      int64_t total_ = 0;

      int unit_ = -1;          // PCNT on IDF 4: the unit number
      void *handle_ = nullptr; // PCNT on IDF 5: the pcnt_unit_handle_t
      int16_t pcntLast_ = 0;

      IsrState isr_;
      int32_t isrLast_ = 0;
    };
  }
}
