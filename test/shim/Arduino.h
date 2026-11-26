#pragma once
// Just enough Arduino for the host tests. Pins and the clock are plain globals.
#include <stdint.h>
#include <stdlib.h>

#define HIGH 1
#define LOW 0
#define INPUT 0x01
#define INPUT_PULLUP 0x05
#define INPUT_PULLDOWN 0x09

namespace Fake
{
  extern uint32_t now;
  extern int pinLevel[64];
  extern int pinModeSet[64];
  extern int adc[64];
}

inline uint32_t millis() { return Fake::now; }
inline void delay(uint32_t ms) { Fake::now += ms; }
inline void pinMode(uint8_t pin, uint8_t mode) { Fake::pinModeSet[pin] = mode; }
inline int digitalRead(uint8_t pin) { return Fake::pinLevel[pin]; }
inline int analogRead(uint8_t pin) { return Fake::adc[pin]; }
