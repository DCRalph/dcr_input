#pragma once

#include "Button.h"
#include "Controls.h"

namespace Controls
{
  struct JoystickReading
  {
    int x = 0, y = 0;    // raw units, see JoystickSource::rawMin/rawMax
    uint8_t buttons = 0; // bit n = button(n) pressed
  };

  // Where a Joystick gets its samples. JoystickSources.h has analog, digital,
  // Nunchuk and Qwiic ones; subclass it for anything else.
  class JoystickSource
  {
  public:
    virtual ~JoystickSource() = default;
    /** Sets the hardware up. False if it did not answer; the Joystick retries each second. */
    virtual bool begin() = 0;
    /** One sample. False if the read failed. */
    virtual bool read(JoystickReading &out, uint32_t nowMs) = 0;

    int rawMin = 0;
    int rawMax = 4095;
    int rawCenter = -1; // -1 samples the resting position in begin() and recenter()
  };

  /**
   * A two-axis stick as -100..100 per axis, right and up positive, with a radial
   * deadzone. direction() gives one menu direction with hysteresis. Up to two
   * buttons (stick press, or Z and C on a Nunchuk) come through button().
   */
  class Joystick : public Device
  {
  public:
    static constexpr uint8_t kMaxButtons = 2;

    enum class Direction : uint8_t
    {
      None,
      Up,
      Down,
      Left,
      Right,
    };

    struct Config
    {
      uint8_t deadzone = 12; // percent of travel around the centre that reads 0
      uint8_t edge = 8;      // percent at the end of travel that already reads 100
      bool invertX = false;
      bool invertY = false;
      bool swapXY = false;
      uint8_t engage = 50;  // direction() engages at this percent
      uint8_t release = 35; // and lets go below this
      Button::Config button; // timings for the buttons; pins are ignored
    };

    explicit Joystick(JoystickSource &source) : Joystick(source, Config()) {}
    Joystick(JoystickSource &source, Config config);

    void begin(uint32_t nowMs) override;
    void update(uint32_t nowMs) override;

    /** One raw sample. update() calls it with the source's reading. */
    void feed(const JoystickReading &reading, uint32_t nowMs);

    int x() const { return x_; }
    int y() const { return y_; }
    Direction direction() const { return direction_; }
    bool directionChanged() const { return directionChanged_; }
    /** False while the source fails, e.g. an unplugged Nunchuk. Axes then read 0. */
    bool connected() const { return connected_; }
    Button &button(uint8_t index = 0) { return buttons_[index < kMaxButtons ? index : 0]; }
    const JoystickReading &raw() const { return raw_; }

    /** Samples the resting position again. The stick must be let go. */
    void recenter();

  private:
    int along(Direction d) const;
    void disconnect(uint32_t nowMs);

    JoystickSource &source_;
    Config config_;
    Button buttons_[kMaxButtons];

    int centerX_ = 0, centerY_ = 0;
    JoystickReading raw_;
    int x_ = 0, y_ = 0;
    Direction direction_ = Direction::None;
    bool directionChanged_ = false;
    bool connected_ = false;
    uint8_t failures_ = 0;
    uint32_t lastBeginMs_ = 0;
  };
}
