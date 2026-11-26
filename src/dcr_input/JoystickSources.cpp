#include "JoystickSources.h"

#include <Arduino.h>
#include <initializer_list>

namespace Controls
{
  AnalogJoystick::AnalogJoystick(int xPin, int yPin, int buttonPin, bool buttonActiveHigh)
      : xPin_(xPin), yPin_(yPin), buttonPin_(buttonPin), buttonActiveHigh_(buttonActiveHigh)
  {
  }

  bool AnalogJoystick::begin()
  {
    if (buttonPin_ >= 0)
      pinMode(buttonPin_, detail::inputMode(buttonActiveHigh_ ? Pull::Down : Pull::Up));
    return true;
  }

  // Two reads per axis halve the ESP32 ADC's noise for about 20 us more.
  bool AnalogJoystick::read(JoystickReading &out, uint32_t)
  {
    out.x = (analogRead(xPin_) + analogRead(xPin_)) / 2;
    out.y = (analogRead(yPin_) + analogRead(yPin_)) / 2;
    out.buttons = buttonPin_ >= 0 && (digitalRead(buttonPin_) == HIGH) == buttonActiveHigh_;
    return true;
  }

  DigitalJoystick::DigitalJoystick(int up, int down, int left, int right, int center,
                                   bool activeHigh, uint16_t debounceMs)
      : center_(center), activeHigh_(activeHigh), debounceMs_(debounceMs)
  {
    up_.pin = up;
    down_.pin = down;
    left_.pin = left;
    right_.pin = right;
    rawMin = -1;
    rawMax = 1;
    rawCenter = 0;
  }

  bool DigitalJoystick::begin()
  {
    const uint8_t mode = detail::inputMode(activeHigh_ ? Pull::Down : Pull::Up);
    for (int pin : {up_.pin, down_.pin, left_.pin, right_.pin, center_})
      if (pin >= 0)
        pinMode(pin, mode);
    return true;
  }

  bool DigitalJoystick::sample(Switch &s, uint32_t nowMs)
  {
    if (s.pin < 0)
      return false;
    const bool raw = (digitalRead(s.pin) == HIGH) == activeHigh_;
    if (raw != s.raw)
    {
      s.raw = raw;
      s.sinceMs = nowMs;
    }
    if (s.raw != s.stable && nowMs - s.sinceMs >= debounceMs_)
      s.stable = s.raw;
    return s.stable;
  }

  // The centre press is left raw: the Joystick's Button debounces it.
  bool DigitalJoystick::read(JoystickReading &out, uint32_t nowMs)
  {
    out.x = sample(right_, nowMs) - sample(left_, nowMs);
    out.y = sample(up_, nowMs) - sample(down_, nowMs);
    out.buttons = center_ >= 0 && (digitalRead(center_) == HIGH) == activeHigh_;
    return true;
  }

  namespace
  {
    constexpr uint8_t kNunchukAddress = 0x52;
  }

  // Genuine sticks span about 30..225 around 128; clones reach 0..255 and clamp.
  Nunchuk::Nunchuk(TwoWire &wire) : wire_(wire)
  {
    rawMin = 30;
    rawMax = 225;
  }

  // 0xF0 0x55 then 0xFB 0x00 starts genuine and clone Nunchuks unencrypted.
  bool Nunchuk::begin()
  {
    wire_.beginTransmission(kNunchukAddress);
    wire_.write(0xF0);
    wire_.write(0x55);
    if (wire_.endTransmission() != 0)
      return false;
    delay(1);
    wire_.beginTransmission(kNunchukAddress);
    wire_.write(0xFB);
    wire_.write(0x00);
    if (wire_.endTransmission() != 0)
      return false;
    delay(1);
    return requestReport();
  }

  bool Nunchuk::requestReport()
  {
    wire_.beginTransmission(kNunchukAddress);
    wire_.write(0x00);
    return wire_.endTransmission() == 0;
  }

  // Each read collects the report requested by the previous one and asks for the
  // next, so a frame never waits for the Nunchuk to convert.
  bool Nunchuk::read(JoystickReading &out, uint32_t)
  {
    uint8_t data[6];
    if (wire_.requestFrom(kNunchukAddress, static_cast<uint8_t>(6)) != 6)
      return false;
    for (uint8_t &b : data)
      b = static_cast<uint8_t>(wire_.read());
    const bool requested = requestReport();
    return parse(data, out) && requested;
  }

  bool Nunchuk::parse(const uint8_t data[6], JoystickReading &out)
  {
    bool allFF = true, allZero = true;
    for (uint8_t i = 0; i < 6; i++)
    {
      allFF = allFF && data[i] == 0xFF;
      allZero = allZero && data[i] == 0x00;
    }
    if (allFF || allZero)
      return false;

    out.x = data[0];
    out.y = data[1];
    out.buttons = 0;
    if (!(data[5] & 0x01))
      out.buttons |= 0x01; // Z, active low
    if (!(data[5] & 0x02))
      out.buttons |= 0x02; // C, active low
    return true;
  }

  namespace
  {
    constexpr uint8_t kQwiicXMsb = 0x03; // X LSB, Y MSB, Y LSB, button follow
    constexpr uint8_t kQwiicButton = 0x07;
  }

  QwiicJoystick::QwiicJoystick(TwoWire &wire, uint8_t address) : wire_(wire), address_(address)
  {
    rawMin = 0;
    rawMax = 1023;
  }

  bool QwiicJoystick::begin()
  {
    wire_.beginTransmission(address_);
    return wire_.endTransmission() == 0;
  }

  // One register per transaction, as SparkFun's own library reads it.
  bool QwiicJoystick::readRegister(uint8_t reg, uint8_t &value)
  {
    wire_.beginTransmission(address_);
    wire_.write(reg);
    if (wire_.endTransmission() != 0)
      return false;
    if (wire_.requestFrom(address_, static_cast<uint8_t>(1)) != 1)
      return false;
    value = static_cast<uint8_t>(wire_.read());
    return true;
  }

  // Axes are 10 bits left-aligned in MSB:LSB. The button register reads 0 while pressed.
  bool QwiicJoystick::read(JoystickReading &out, uint32_t)
  {
    uint8_t r[5];
    for (uint8_t i = 0; i < 5; i++)
      if (!readRegister(kQwiicXMsb + i, r[i]))
        return false;
    out.x = ((r[0] << 8) | r[1]) >> 6;
    out.y = ((r[2] << 8) | r[3]) >> 6;
    out.buttons = r[kQwiicButton - kQwiicXMsb] == 0;
    return true;
  }
}
