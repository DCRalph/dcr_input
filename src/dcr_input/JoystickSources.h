#pragma once

#include <Wire.h>

#include "Joystick.h"

namespace Controls
{
  /**
   * A potentiometer stick (KY-023, PS2-style thumbsticks) on two ADC pins, plus an
   * optional press switch. Use ADC1 pins on ESP32: ADC2 stops reading with WiFi on.
   */
  class AnalogJoystick : public JoystickSource
  {
  public:
    AnalogJoystick(int xPin, int yPin, int buttonPin = -1, bool buttonActiveHigh = false);
    bool begin() override;
    bool read(JoystickReading &out, uint32_t nowMs) override;

  private:
    int xPin_, yPin_, buttonPin_;
    bool buttonActiveHigh_;
  };

  /**
   * Switch sticks: 4- and 5-way navigation switches, arcade sticks and D-pads,
   * one pin per direction. Each direction reads full travel; diagonals combine.
   */
  class DigitalJoystick : public JoystickSource
  {
  public:
    /** -1 for any switch that is not there. `center` is the press, reported as button(0). */
    DigitalJoystick(int up, int down, int left, int right, int center = -1,
                    bool activeHigh = false, uint16_t debounceMs = 10);
    bool begin() override;
    bool read(JoystickReading &out, uint32_t nowMs) override;

  private:
    struct Switch
    {
      int pin = -1;
      bool raw = false, stable = false;
      uint32_t sinceMs = 0;
    };
    bool sample(Switch &s, uint32_t nowMs);

    Switch up_, down_, left_, right_;
    int center_;
    bool activeHigh_;
    uint16_t debounceMs_;
  };

  /**
   * Wii Nunchuk and its clones, on I2C at 0x52. Z is button(0), C is button(1).
   * Call Wire.begin() with your pins first.
   */
  class Nunchuk : public JoystickSource
  {
  public:
    explicit Nunchuk(TwoWire &wire = Wire);
    bool begin() override;
    bool read(JoystickReading &out, uint32_t nowMs) override;

    /** Decodes one 6-byte report. False for a bus that answered with nothing real. */
    static bool parse(const uint8_t data[6], JoystickReading &out);

  private:
    bool requestReport();
    TwoWire &wire_;
  };

  /**
   * SparkFun Qwiic Joystick (COM-15168), on I2C at 0x20 unless its address was
   * changed. 10-bit axes. Call Wire.begin() with your pins first.
   */
  class QwiicJoystick : public JoystickSource
  {
  public:
    explicit QwiicJoystick(TwoWire &wire = Wire, uint8_t address = 0x20);
    bool begin() override;
    bool read(JoystickReading &out, uint32_t nowMs) override;

  private:
    bool readRegister(uint8_t reg, uint8_t &value);
    TwoWire &wire_;
    uint8_t address_;
  };
}
