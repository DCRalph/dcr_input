#pragma once

#include <stdint.h>

namespace Controls
{
  /** Internal resistor on an input pin. */
  enum class Pull : uint8_t
  {
    None,
    Up,
    Down,
  };

  // Anything Controls::update() drives. Not copyable: the registry keeps its address.
  class Device
  {
  public:
    Device() = default;
    Device(const Device &) = delete;
    Device &operator=(const Device &) = delete;
    virtual ~Device() = default;

    /** Sets the hardware up and takes its resting state, without events. */
    virtual void begin(uint32_t nowMs) = 0;
    /** Samples the hardware. This frame's state holds until the next update. */
    virtual void update(uint32_t nowMs) = 0;
  };

  /** Calls `device.begin()` and has update() drive it from now on. Call from setup(). */
  void add(Device &device);
  void remove(Device &device);

  /** Samples every added device. Call once per loop, before anything reads input. */
  void update();
  /** update() on your own clock, for tests and replays. */
  void update(uint32_t nowMs);

  /** millis() of the last press, release, detent or stick movement. add() counts as one. */
  uint32_t lastActivityMs();
  /** Devices call this when a person touches them. */
  void noteActivity(uint32_t nowMs);

  namespace detail
  {
    /** The pinMode for an input with this pull. Pull::Down is INPUT where the core has none (ESP8266). */
    uint8_t inputMode(Pull pull);
  }
}
