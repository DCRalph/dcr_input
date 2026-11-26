#include "Controls.h"

#include <Arduino.h>
#include <algorithm>
#include <vector>

namespace
{
  std::vector<Controls::Device *> devices;
  uint32_t lastActivity = 0;
}

namespace Controls
{
  void add(Device &device)
  {
    const uint32_t now = millis();
    if (std::find(devices.begin(), devices.end(), &device) == devices.end())
      devices.push_back(&device);
    device.begin(now);
    lastActivity = now;
  }

  void remove(Device &device)
  {
    devices.erase(std::remove(devices.begin(), devices.end(), &device), devices.end());
  }

  void update() { update(millis()); }

  void update(uint32_t nowMs)
  {
    for (Device *device : devices)
      device->update(nowMs);
  }

  uint32_t lastActivityMs() { return lastActivity; }

  void noteActivity(uint32_t nowMs) { lastActivity = nowMs; }

  uint8_t detail::inputMode(Pull pull)
  {
    if (pull == Pull::Up)
      return INPUT_PULLUP;
#ifdef INPUT_PULLDOWN
    if (pull == Pull::Down)
      return INPUT_PULLDOWN;
#endif
    return INPUT;
  }
}
