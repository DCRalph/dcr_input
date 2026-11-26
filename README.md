# dcr_input

Buttons, rotary encoders and joysticks for ESP32. Each one is debounced and
sampled once per loop, and its state holds for that whole frame.

## Install

```ini
lib_deps =
  https://github.com/DCRalph/dcr_input.git
  madhephaestus/ESP32Encoder   ; only for Controls::Encoder on the PCNT unit
```

There are no other dependencies. Everything lives in the `Controls` namespace,
so `Controls::Button` does not clash with dcr_display's `Button` widget.

## Quick start

```cpp
#include <ESP32Encoder.h>
#include <dcr_input.h>

ESP32Encoder pcnt;
Controls::Encoder knob([] { return pcnt.getCount(); });
Controls::Button push(ENCODER_PUSH_PIN);        // active low, internal pull-up

void setup() {
  pcnt.attachFullQuad(ENCODER_A, ENCODER_B);
  Controls::add(knob);                         // sets pins up, takes the resting state
  Controls::add(push);
}

void loop() {
  Controls::update();                          // once per loop, before anything reads
  if (knob.delta()) menu.move(knob.delta());
  if (push.clicks() == 2) openSettings();
  if (push.clicks() == -1) powerOff();          // one press, held
}
```

## One frame at a time

`Controls::update()` samples every device. Until the next call, every reading
stays the same, so a click is seen by every reader in that frame and then it is
gone. Nothing builds up: turns made on a screen that does not read the encoder
are dropped, not applied when the next screen starts reading.

`Controls::lastActivityMs()` is the last press, release, detent or stick
movement, for auto-off and screen dimming.

## Button

`clicks()` reports a finished sequence for one frame:

| Value | Meaning |
| --- | --- |
| `1`, `2`, `3`... | That many clicks. Reported once the button has been released for `multiClickMs` |
| `-1`, `-2`... | That many presses, with the last one held for `longMs`. Reported while it is still held |
| `0` | Nothing finished this frame |

This is ClickButton's convention, so code that reads `.clicks` keeps working.
Alongside it are `pressed()`, `justPressed()`, `justReleased()`, `heldMs()`
and `repeat()`. `repeat()` fires on the long press and then every `repeatMs`
while the button stays held.

```cpp
Controls::Button::Config c;
c.pin = 7;
c.activeHigh = true;        // pressed reads HIGH
c.pull = Controls::Button::Pull::None;
c.multiClickMs = 200;
c.longMs = 500;
c.maxClicks = 3;            // a triple reports on release instead of 200 ms later
Controls::Button knobPush(c);
```

A button can also be read from:

- **A resistor ladder:** set `analogPin`, `analogMin` and `analogMax`. It is
  pressed while the ADC reading is in that range.
- **Anything else**, such as an I/O expander or a touch pad: set
  `read = [] { return expander.read(3); }`.

A button that is held when it is added never counts, so the press that powered
the device on does not show up as a click.

## Encoder

`Controls::Encoder` turns a raw quadrature count into detents.

- `delta()` is the detents turned this frame.
- `position()` is the count since `begin()` or `reset()`.

A detent counts only once the shaft is `countsPerDetent - 1` counts past the last
one. A knob resting on a boundary, or contact bounce, can't make it step back and
forth. Counting is left to hardware: ESP32Encoder's `attachFullQuad` on the PCNT
unit, with `setFilter(1023)` against bounce.

## Joystick

`Controls::Joystick` reads any `JoystickSource`.

- `x()` and `y()` run -100..100, with right and up positive. A radial deadzone is
  rescaled so movement starts at 0 just past it, and diagonals are capped to the
  unit circle.
- `direction()` gives one menu direction with hysteresis. It engages at 50% and
  lets go below 35%, and near 45 degrees it doesn't flip between two directions.
- `button(0)` and `button(1)` are full `Button`s.
- `connected()` is false while an I2C stick is unplugged. Its axes then read 0,
  and it retries once a second.

| Source | Hardware | Buttons |
| --- | --- | --- |
| `AnalogJoystick(x, y, press)` | Potentiometer sticks: KY-023, PS2-style thumbsticks | press |
| `DigitalJoystick(up, down, left, right, center)` | 4- and 5-way navigation switches, arcade sticks, D-pads | center |
| `Nunchuk(Wire)` | Wii Nunchuk and clones, I2C 0x52 | Z, C |
| `QwiicJoystick(Wire, 0x20)` | SparkFun Qwiic Joystick, 10-bit | press |

```cpp
Controls::AnalogJoystick source(JOY_X, JOY_Y, JOY_PRESS);
Controls::Joystick::Config c;
c.deadzone = 10;              // percent
c.invertY = true;             // match how the stick is mounted
Controls::Joystick stick(source, c);
```

On analog sticks, use ADC1 pins, because ADC2 doesn't read while WiFi is on.
The resting position is sampled when the joystick is added, so leave the stick
alone at boot. Call `recenter()` to sample it again.

For I2C sticks, call `Wire.begin(sda, scl)` before `Controls::add()`.

For other hardware, subclass `JoystickSource`. Implement `begin()` and `read()`,
and set `rawMin`, `rawMax` and, if it's known, `rawCenter`.

## With dcr_display

dcr_display's input sources take callbacks, so you connect them with one lambda
per input:

```cpp
EncoderInput encoderInput([] {
  EncoderInput::Config c;
  c.readDelta = [] { return knob.delta(); };
  c.readClicks = [] { return push.clicks(); };
  return c;
}());

JoystickInput joystickInput([] {
  JoystickInput::Config c;
  c.readX = [] { return stick.x(); };
  c.readY = [] { return -stick.y(); };  // dcr_display counts down as positive
  c.center = 0;
  c.deadzone = 50;
  c.readClicks = [] { return stick.button().clicks(); };
  return c;
}());
```

Call `Controls::update()` before `display.render()` in the loop.

## From ClickButton

| ClickButton | dcr_input |
| --- | --- |
| `ClickButton b(pin, HIGH)` | `Controls::Button b(pin, true, Controls::Button::Pull::None)`, then `Controls::add(b)` |
| `b.Update()` in the loop | One `Controls::update()` for every device |
| `b.clicks` | `b.clicks()` |
| `b.depressed` | `b.pressed()` |
| `b.debounceTime` and the other timings | `Config` fields, or `b.config()` at runtime |

ClickButton's internal pull-up did nothing on ESP32, because it relied on the
AVR `digitalWrite(HIGH)` trick. `Pull::Up`, the default, sets `INPUT_PULLUP`.

## Tests

```sh
test/run.sh
```

This builds the library on the host against a fake Arduino and I2C bus in
`test/shim`, and runs it under the address and undefined-behaviour sanitizers.
