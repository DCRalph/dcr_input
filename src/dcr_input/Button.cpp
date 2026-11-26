#include "Button.h"

#include <Arduino.h>

namespace Controls
{
  Button::Button(int pin, bool activeHigh, Pull pull)
  {
    config_.pin = pin;
    config_.activeHigh = activeHigh;
    config_.pull = pull;
  }

  void Button::begin(uint32_t nowMs)
  {
    if (config_.pin >= 0 && !config_.read)
    {
      const uint8_t mode = config_.pull == Pull::Up     ? INPUT_PULLUP
                           : config_.pull == Pull::Down ? INPUT_PULLDOWN
                                                        : INPUT;
      pinMode(config_.pin, mode);
    }
    reset(readRaw(), nowMs);
  }

  void Button::update(uint32_t nowMs) { feed(readRaw(), nowMs); }

  bool Button::readRaw() const
  {
    if (config_.read)
      return config_.read();
    if (config_.analogPin >= 0)
    {
      const int v = analogRead(config_.analogPin);
      return v >= config_.analogMin && v <= config_.analogMax;
    }
    if (config_.pin >= 0)
      return (digitalRead(config_.pin) == HIGH) == config_.activeHigh;
    return false;
  }

  void Button::reset(bool pressedNow, uint32_t nowMs)
  {
    raw_ = pressed_ = pressedNow;
    rawSinceMs_ = edgeMs_ = now_ = lastRepeatMs_ = nowMs;
    count_ = 0;
    longFired_ = false;
    clicks_ = 0;
    justPressed_ = justReleased_ = repeat_ = false;
  }

  void Button::finish(int clicks)
  {
    clicks_ = static_cast<int16_t>(clicks);
    count_ = 0;
  }

  // Unsigned differences throughout, so millis() rolling over is harmless.
  void Button::feed(bool rawPressed, uint32_t nowMs)
  {
    now_ = nowMs;
    clicks_ = 0;
    justPressed_ = justReleased_ = repeat_ = false;

    if (rawPressed != raw_)
    {
      raw_ = rawPressed;
      rawSinceMs_ = nowMs;
    }

    if (raw_ != pressed_ && nowMs - rawSinceMs_ >= config_.debounceMs)
    {
      pressed_ = raw_;
      edgeMs_ = rawSinceMs_;
      noteActivity(nowMs);
      if (pressed_)
      {
        justPressed_ = true;
        longFired_ = false;
        if (count_ < INT16_MAX)
          count_++;
      }
      else
      {
        justReleased_ = true;
        if (config_.maxClicks != 0 && count_ >= config_.maxClicks)
          finish(count_);
      }
    }

    if (pressed_ && count_ > 0 && !longFired_ && nowMs - edgeMs_ >= config_.longMs)
    {
      finish(-count_);
      longFired_ = true;
      repeat_ = true;
      lastRepeatMs_ = nowMs;
    }
    else if (pressed_ && longFired_ && config_.repeatMs != 0 &&
             nowMs - lastRepeatMs_ >= config_.repeatMs)
    {
      repeat_ = true;
      lastRepeatMs_ = nowMs;
    }

    if (!pressed_ && count_ > 0 && nowMs - edgeMs_ >= config_.multiClickMs)
      finish(count_);
  }
}
