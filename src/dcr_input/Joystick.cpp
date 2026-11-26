#include "Joystick.h"

#include <Arduino.h>
#include <math.h>
#include <utility>

namespace Controls
{
  namespace
  {
    constexpr uint8_t kFailuresBeforeDisconnect = 3;
    constexpr uint32_t kRetryMs = 1000;
    constexpr uint8_t kCenterSamples = 8;
    constexpr uint8_t kStaleSamples = 2; // reads right after begin() can predate it
    constexpr int kSwitchMargin = 15;    // a new direction must lead by this much to take over

    // -1..1, from the centre to the rail on that side. Sticks are rarely symmetric.
    float normalise(int raw, int center, int rawMin, int rawMax)
    {
      const int d = raw - center;
      const int span = d >= 0 ? rawMax - center : center - rawMin;
      if (span <= 0)
        return 0;
      const float v = static_cast<float>(d) / span;
      return v > 1 ? 1 : (v < -1 ? -1 : v);
    }
  }

  Joystick::Joystick(JoystickSource &source, Config config)
      : source_(source), config_(std::move(config))
  {
    for (Button &b : buttons_)
    {
      b.config() = config_.button;
      b.config().pin = -1;
      b.config().analogPin = -1;
      b.config().read = nullptr;
    }
  }

  void Joystick::begin(uint32_t nowMs)
  {
    lastBeginMs_ = nowMs;
    failures_ = 0;
    connected_ = source_.begin();
    raw_ = JoystickReading();
    if (connected_)
      recenter();
    for (uint8_t i = 0; i < kMaxButtons; i++)
      buttons_[i].reset(connected_ && (raw_.buttons & (1u << i)), nowMs);
    x_ = y_ = 0;
    direction_ = Direction::None;
    directionChanged_ = false;
  }

  void Joystick::recenter()
  {
    if (source_.rawCenter >= 0)
    {
      centerX_ = centerY_ = source_.rawCenter;
      return;
    }

    long sumX = 0, sumY = 0;
    int n = 0;
    for (uint8_t i = 0; i < kCenterSamples + kStaleSamples; i++)
    {
      JoystickReading r;
      if (!source_.read(r, millis()))
        continue;
      raw_ = r;
      if (i >= kStaleSamples)
      {
        sumX += r.x;
        sumY += r.y;
        n++;
      }
      delay(1);
    }
    if (n > 0)
    {
      centerX_ = static_cast<int>(sumX / n);
      centerY_ = static_cast<int>(sumY / n);
    }
    else
      centerX_ = centerY_ = (source_.rawMin + source_.rawMax) / 2;
  }

  void Joystick::update(uint32_t nowMs)
  {
    if (!connected_ && nowMs - lastBeginMs_ >= kRetryMs)
    {
      lastBeginMs_ = nowMs;
      if (source_.begin())
      {
        connected_ = true;
        failures_ = 0;
        recenter();
      }
    }

    JoystickReading r;
    if (connected_ && source_.read(r, nowMs))
    {
      failures_ = 0;
      feed(r, nowMs);
      return;
    }

    // A single failed I2C read keeps the last sample; a run of them is an unplug.
    if (connected_ && ++failures_ < kFailuresBeforeDisconnect)
    {
      feed(raw_, nowMs);
      return;
    }
    if (connected_)
    {
      connected_ = false;
      lastBeginMs_ = nowMs;
    }
    JoystickReading rest;
    rest.x = centerX_;
    rest.y = centerY_;
    feed(rest, nowMs);
  }

  int Joystick::along(Direction d) const
  {
    switch (d)
    {
    case Direction::Right:
      return x_;
    case Direction::Left:
      return -x_;
    case Direction::Up:
      return y_;
    case Direction::Down:
      return -y_;
    default:
      return 0;
    }
  }

  void Joystick::feed(const JoystickReading &reading, uint32_t nowMs)
  {
    raw_ = reading;
    float nx = normalise(reading.x, centerX_, source_.rawMin, source_.rawMax);
    float ny = normalise(reading.y, centerY_, source_.rawMin, source_.rawMax);
    if (config_.swapXY)
      std::swap(nx, ny);
    if (config_.invertX)
      nx = -nx;
    if (config_.invertY)
      ny = -ny;

    // Radial deadzone, rescaled so output starts at 0 just past it and reaches
    // 100 `edge` percent before the rail. Diagonals are capped to the unit circle.
    const float m = sqrtf(nx * nx + ny * ny);
    const float dz = config_.deadzone / 100.0f;
    const float full = 1.0f - config_.edge / 100.0f;
    if (m <= dz)
      nx = ny = 0;
    else
    {
      float scaled = full > dz ? (m - dz) / (full - dz) : 1.0f;
      if (scaled > 1)
        scaled = 1;
      nx *= scaled / m;
      ny *= scaled / m;
    }
    x_ = static_cast<int>(lroundf(nx * 100));
    y_ = static_cast<int>(lroundf(ny * 100));

    Direction next = direction_;
    if (next != Direction::None && along(next) < config_.release)
      next = Direction::None;
    const int ax = abs(x_), ay = abs(y_);
    Direction candidate = Direction::None;
    if ((ax > ay ? ax : ay) >= config_.engage)
      candidate = ax >= ay ? (x_ > 0 ? Direction::Right : Direction::Left)
                           : (y_ > 0 ? Direction::Up : Direction::Down);
    if (candidate != Direction::None && candidate != next &&
        (next == Direction::None || along(candidate) > along(next) + kSwitchMargin))
      next = candidate;
    directionChanged_ = next != direction_;
    direction_ = next;

    if (x_ != 0 || y_ != 0 || directionChanged_)
      noteActivity(nowMs);
    for (uint8_t i = 0; i < kMaxButtons; i++)
      buttons_[i].feed(reading.buttons & (1u << i), nowMs);
  }
}
