#pragma once

#include <functional>

#include "Controls.h"

namespace Controls
{
  /**
   * A debounced push button that counts clicks. clicks() follows ClickButton: 1, 2,
   * 3 for a finished sequence, -n when the nth press is held. Every reading holds
   * for the whole frame, so any number of readers see the same click.
   */
  class Button : public Device
  {
  public:
    using Pull = Controls::Pull;

    struct Config
    {
      int pin = -1;            // digital pin; -1 for an analog or callback button
      bool activeHigh = false; // pressed reads HIGH
      Pull pull = Pull::Up;    // internal resistor on `pin`

      // Resistor ladder: pressed while analogRead(analogPin) is in [analogMin, analogMax].
      int analogPin = -1;
      int analogMin = 0;
      int analogMax = 0;

      std::function<bool()> read; // any other source, e.g. an I/O expander; true = pressed

      uint16_t debounceMs = 20;   // a new level must hold this long
      uint16_t multiClickMs = 250; // a release this long ends the sequence
      uint16_t longMs = 800;      // a press this long reports -n
      uint16_t repeatMs = 0;      // while still held after that, repeat() this often (0 = off)
      uint8_t maxClicks = 0;      // the release that reaches this count reports at once (0 = no cap)
    };

    /** A button its owner feeds, e.g. one on a Joystick. */
    Button() = default;
    explicit Button(Config config) : config_(std::move(config)) {}
    explicit Button(int pin, bool activeHigh = false, Pull pull = Pull::Up);

    void begin(uint32_t nowMs) override;
    void update(uint32_t nowMs) override;

    /** One raw sample. update() calls it; call it yourself for a source of your own. */
    void feed(bool rawPressed, uint32_t nowMs);
    /** Drops any sequence in progress and rests at `pressedNow`. A held press then never counts. */
    void reset(bool pressedNow, uint32_t nowMs);

    /** The sequence that ended this frame: n clicks, -n if the nth was held, else 0. */
    int clicks() const { return clicks_; }
    bool longPressed() const { return clicks_ < 0; }
    bool pressed() const { return pressed_; }
    bool justPressed() const { return justPressed_; }
    bool justReleased() const { return justReleased_; }
    /** True on the long press, then every repeatMs while it stays held. */
    bool repeat() const { return repeat_; }
    uint32_t heldMs() const { return pressed_ ? now_ - edgeMs_ : 0; }

    /** Timings can change at runtime; pins take effect at the next begin(). */
    Config &config() { return config_; }

  private:
    bool readRaw() const;
    void finish(int clicks);

    Config config_;

    bool raw_ = false;
    uint32_t rawSinceMs_ = 0;
    bool pressed_ = false;
    uint32_t edgeMs_ = 0; // last debounced edge, timed from the raw change
    uint32_t now_ = 0;
    uint32_t lastRepeatMs_ = 0;
    int16_t count_ = 0;
    bool longFired_ = false;

    int16_t clicks_ = 0;
    bool justPressed_ = false;
    bool justReleased_ = false;
    bool repeat_ = false;
  };
}
