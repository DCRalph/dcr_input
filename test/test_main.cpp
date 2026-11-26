// Host tests for dcr_input. Run test/run.sh.
#include <cmath>
#include <cstdio>

#include <Arduino.h>
#include <dcr_input.h>

namespace Fake
{
  uint32_t now = 0;
  int pinLevel[64] = {};
  int pinModeSet[64] = {};
  int adc[64] = {};
}
TwoWire Wire;

using namespace Controls;

static int failures = 0;
#define CHECK(cond)                                                \
  do                                                               \
  {                                                                \
    if (!(cond))                                                   \
    {                                                              \
      std::printf("%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
      failures++;                                                  \
    }                                                              \
  } while (0)
#define CHECK_EQ(a, b)                                                                 \
  do                                                                                   \
  {                                                                                    \
    const long long va = (a), vb = (b);                                                \
    if (va != vb)                                                                      \
    {                                                                                  \
      std::printf("%s:%d: %s == %lld, expected %lld\n", __FILE__, __LINE__, #a, va, vb); \
      failures++;                                                                      \
    }                                                                                  \
  } while (0)

// Feeds `pressed` every 5 ms for `ms`, returning the non-zero clicks seen (last one).
struct Driver
{
  Button &b;
  uint32_t t;
  int events = 0;
  int last = 0;

  void run(bool pressed, uint32_t ms)
  {
    for (uint32_t end = t + ms; t != end; t += 5)
    {
      b.feed(pressed, t);
      if (b.clicks() != 0)
      {
        events++;
        last = b.clicks();
      }
    }
  }
};

Button::Config timings()
{
  Button::Config c;
  c.debounceMs = 20;
  c.multiClickMs = 200;
  c.longMs = 500;
  return c;
}

void testBounceIgnored()
{
  Button b(timings());
  b.reset(false, 0);
  Driver d{b, 0};
  for (int i = 0; i < 10; i++)
  {
    d.run(true, 10);
    d.run(false, 10);
  }
  d.run(false, 400);
  CHECK_EQ(d.events, 0);
}

void testClickCounts()
{
  for (int n = 1; n <= 3; n++)
  {
    Button b(timings());
    b.reset(false, 0);
    Driver d{b, 0};
    for (int i = 0; i < n; i++)
    {
      d.run(true, 60);
      d.run(false, 100);
    }
    d.run(false, 300);
    CHECK_EQ(d.events, 1);
    CHECK_EQ(d.last, n);
  }
}

void testSingleClickLastsOneFrame()
{
  Button b(timings());
  b.reset(false, 0);
  Driver d{b, 0};
  d.run(true, 60);
  d.run(false, 300);
  CHECK_EQ(d.events, 1); // exactly one 5 ms frame carried it
  CHECK_EQ(b.clicks(), 0);
}

void testLongPress()
{
  Button b(timings());
  b.reset(false, 0);
  Driver d{b, 0};
  d.run(true, 60);
  d.run(false, 100);
  d.run(true, 1000); // second press held: -2
  CHECK_EQ(d.events, 1);
  CHECK_EQ(d.last, -2);
  d.run(false, 400); // the release after a long press reports nothing
  CHECK_EQ(d.events, 1);
}

void testLongPressTimedFromEdge()
{
  Button b(timings());
  b.reset(false, 0);
  b.feed(true, 0);
  b.feed(true, 499);
  CHECK_EQ(b.clicks(), 0);
  b.feed(true, 500);
  CHECK_EQ(b.clicks(), -1);
  CHECK(b.repeat());
}

void testMaxClicksReportsOnRelease()
{
  Button::Config c = timings();
  c.maxClicks = 1;
  Button b(c);
  b.reset(false, 0);
  b.feed(true, 0);
  b.feed(true, 30);
  b.feed(false, 60);
  b.feed(false, 85); // debounced release
  CHECK_EQ(b.clicks(), 1);
  CHECK(b.justReleased());
}

void testRepeat()
{
  Button::Config c = timings();
  c.repeatMs = 100;
  Button b(c);
  b.reset(false, 0);
  int repeats = 0;
  for (uint32_t t = 0; t <= 1000; t += 5)
  {
    b.feed(true, t);
    repeats += b.repeat();
  }
  CHECK_EQ(repeats, 6); // 500, 600, ... 1000
}

void testPressHeldAtBeginNeverCounts()
{
  Button b(timings());
  b.reset(true, 0);
  Driver d{b, 0};
  d.run(true, 2000);
  d.run(false, 400);
  CHECK_EQ(d.events, 0);
  CHECK(!b.pressed());
}

void testMillisRollover()
{
  Button b(timings());
  const uint32_t start = 0xFFFFFFFFu - 100;
  b.reset(false, start);
  Driver d{b, start};
  d.run(true, 60);
  d.run(false, 400);
  CHECK_EQ(d.events, 1);
  CHECK_EQ(d.last, 1);
}

void testPinButton()
{
  Fake::pinLevel[4] = HIGH; // released with a pull-up
  Button b(4);
  b.begin(0);
  CHECK_EQ(Fake::pinModeSet[4], INPUT_PULLUP);
  Fake::pinLevel[4] = LOW;
  b.update(0);
  b.update(25);
  CHECK(b.pressed());
  CHECK(b.justPressed());
}

void testAnalogLadder()
{
  Button::Config c;
  c.analogPin = 7;
  c.analogMin = 1000;
  c.analogMax = 1400;
  Button b(c);
  Fake::adc[7] = 4095;
  b.begin(0);
  Fake::adc[7] = 1200;
  b.update(0);
  b.update(25);
  CHECK(b.pressed());
}

void testEncoderDetents()
{
  int64_t count = 1000; // begin() takes whatever the counter already holds
  Encoder::Config c;
  c.readCount = [&] { return count; };
  Encoder e(c);
  e.begin(0);
  CHECK_EQ(e.position(), 0);

  for (int v : {1001, 1002, 1001, 1002, 1000})
  {
    count = v;
    e.update(0);
    CHECK_EQ(e.delta(), 0); // bounce within a detent never steps
  }
  count = 1003;
  e.update(0);
  CHECK_EQ(e.delta(), 1);
  count = 1002; // back one count at rest
  e.update(0);
  CHECK_EQ(e.delta(), 0);
  count = 1000 - 3; // from detent 1 past 0 to -1
  e.update(0);
  CHECK_EQ(e.delta(), -2);
  CHECK_EQ(e.position(), -1);
  count = 1000 + 40; // a big jump lands exactly
  e.update(0);
  CHECK_EQ(e.delta(), 11);
  CHECK_EQ(e.position(), 10);
  e.update(0);
  CHECK_EQ(e.delta(), 0); // a delta lasts one frame
  e.reset();
  CHECK_EQ(e.position(), 0);
}

void testEncoderDirectionAndSingleCount()
{
  int64_t count = 0;
  Encoder::Config c;
  c.readCount = [&] { return count; };
  c.countsPerDetent = 1;
  c.direction = -1;
  Encoder e(c);
  e.begin(0);
  count = 3;
  e.update(0);
  CHECK_EQ(e.delta(), -3);
  CHECK_EQ(e.position(), -3);
}

void testAnalogJoystickScaling()
{
  Fake::adc[1] = 2000; // resting off-centre, as real sticks do
  Fake::adc[2] = 2100;
  AnalogJoystick src(1, 2);
  Joystick j(src);
  j.begin(0);

  Fake::adc[1] = 2000 + 150; // inside the deadzone
  j.update(0);
  CHECK_EQ(j.x(), 0);
  Fake::adc[1] = 4095;
  j.update(0);
  CHECK_EQ(j.x(), 100);
  CHECK(j.direction() == Joystick::Direction::Right);
  Fake::adc[1] = 0;
  Fake::adc[2] = 0;
  j.update(0); // diagonal, capped to the unit circle
  CHECK_EQ(j.x(), -71);
  CHECK_EQ(j.y(), -71);
  Fake::adc[1] = 2000;
  Fake::adc[2] = 2100;
  j.update(0);
  CHECK_EQ(j.x(), 0);
  CHECK(j.direction() == Joystick::Direction::None);
  CHECK(j.directionChanged());
}

// A source the test sets directly, in -100..100.
struct ManualSource : JoystickSource
{
  JoystickReading next;
  ManualSource()
  {
    rawMin = -100;
    rawMax = 100;
    rawCenter = 0;
  }
  bool begin() override { return true; }
  bool read(JoystickReading &out, uint32_t) override
  {
    out = next;
    return true;
  }
};

void testDirectionHysteresis()
{
  ManualSource src;
  Joystick::Config c;
  c.deadzone = 0;
  c.edge = 0;
  Joystick j(src, c);
  j.begin(0);

  auto at = [&](int x, int y)
  {
    src.next.x = x;
    src.next.y = y;
    j.update(0);
    return j.direction();
  };
  CHECK(at(45, 0) == Joystick::Direction::None);
  CHECK(at(55, 0) == Joystick::Direction::Right);
  CHECK(at(40, 0) == Joystick::Direction::Right); // above release
  CHECK(at(30, 0) == Joystick::Direction::None);
  CHECK(at(60, 0) == Joystick::Direction::Right);
  CHECK(at(60, 66) == Joystick::Direction::Right); // Up leads, but not by the margin
  CHECK(at(60, 80) == Joystick::Direction::Up);
}

void testDigitalJoystick()
{
  for (int p : {10, 11, 12, 13, 14})
    Fake::pinLevel[p] = HIGH;
  DigitalJoystick src(/*up*/ 10, /*down*/ 11, /*left*/ 12, /*right*/ 13, /*center*/ 14);
  Joystick j(src);
  j.begin(0);
  CHECK_EQ(Fake::pinModeSet[10], INPUT_PULLUP);

  Fake::pinLevel[10] = LOW; // up
  j.update(0);
  CHECK_EQ(j.y(), 0); // still debouncing
  j.update(10);
  CHECK_EQ(j.y(), 100);
  CHECK(j.direction() == Joystick::Direction::Up);
  Fake::pinLevel[13] = LOW; // and right
  j.update(20);
  j.update(30);
  CHECK_EQ(j.x(), 71);
  CHECK_EQ(j.y(), 71);
}

void testNunchuk()
{
  Wire = TwoWire();
  Wire.address = 0x52;
  const uint8_t still[6] = {128, 128, 0x80, 0x80, 0x80, 0x03};
  Wire.nextRead.assign(still, still + 6);
  Nunchuk src;
  Joystick j(src);
  Fake::now = 0;
  j.begin(0);
  CHECK(j.connected());
  CHECK(Wire.written.size() >= 5);
  CHECK_EQ(Wire.written[0], 0xF0);
  CHECK_EQ(Wire.written[1], 0x55);

  const uint8_t pushed[6] = {225, 128, 0x80, 0x80, 0x80, 0x02}; // right, Z down
  Wire.nextRead.assign(pushed, pushed + 6);
  j.update(0);
  CHECK_EQ(j.x(), 100);
  j.update(30);
  CHECK(j.button(0).pressed());
  CHECK(!j.button(1).pressed());

  JoystickReading r;
  const uint8_t ff[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  CHECK(!Nunchuk::parse(ff, r));

  Wire.present = false; // unplugged: three misses, then centred and disconnected
  j.update(40);
  j.update(50);
  CHECK(j.connected());
  j.update(60);
  CHECK(!j.connected());
  CHECK_EQ(j.x(), 0);

  Wire.present = true;
  Wire.nextRead.assign(still, still + 6);
  j.update(500);
  CHECK(!j.connected()); // retries once a second
  j.update(1060);
  CHECK(j.connected());
}

void testQwiic()
{
  Wire = TwoWire();
  Wire.address = 0x20;
  Wire.registerMode = true;
  // Resting at 512 (0x80 0x00 left-aligned), button up.
  Wire.registers[3] = 0x80;
  Wire.registers[5] = 0x80;
  Wire.registers[7] = 1;
  QwiicJoystick src;
  Joystick j(src);
  j.begin(0);
  CHECK(j.connected());
  CHECK_EQ(j.raw().x, 512);

  Wire.registers[3] = 0xFF; // 1023
  Wire.registers[4] = 0xC0;
  Wire.registers[7] = 0; // pressed
  j.update(0);
  j.update(30);
  CHECK_EQ(j.raw().x, 1023);
  CHECK_EQ(j.x(), 100);
  CHECK(j.button().pressed());
}

void testActivity()
{
  Fake::now = 1000;
  Button b(timings());
  Controls::add(b); // pin -1, reads released
  CHECK_EQ(Controls::lastActivityMs(), 1000);
  Controls::update(2000);
  CHECK_EQ(Controls::lastActivityMs(), 1000);
  b.feed(true, 3000);
  b.feed(true, 3030);
  CHECK_EQ(Controls::lastActivityMs(), 3030);
  Controls::remove(b);
}

int main()
{
  testBounceIgnored();
  testClickCounts();
  testSingleClickLastsOneFrame();
  testLongPress();
  testLongPressTimedFromEdge();
  testMaxClicksReportsOnRelease();
  testRepeat();
  testPressHeldAtBeginNeverCounts();
  testMillisRollover();
  testPinButton();
  testAnalogLadder();
  testEncoderDetents();
  testEncoderDirectionAndSingleCount();
  testAnalogJoystickScaling();
  testDirectionHysteresis();
  testDigitalJoystick();
  testNunchuk();
  testQwiic();
  testActivity();
  if (failures == 0)
    std::printf("all tests passed\n");
  return failures == 0 ? 0 : 1;
}
