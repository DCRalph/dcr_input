// An analog thumbstick and a Nunchuk side by side. Any JoystickSource works the same.
#include <Wire.h>
#include <dcr_input.h>

Controls::AnalogJoystick thumbSource(/*x*/ 1, /*y*/ 2, /*press*/ 3);
Controls::Joystick thumb(thumbSource);

Controls::Nunchuk nunchukSource;
Controls::Joystick nunchuk(nunchukSource);

const char *name(Controls::Joystick::Direction d)
{
  switch (d)
  {
  case Controls::Joystick::Direction::Up:
    return "up";
  case Controls::Joystick::Direction::Down:
    return "down";
  case Controls::Joystick::Direction::Left:
    return "left";
  case Controls::Joystick::Direction::Right:
    return "right";
  default:
    return "centre";
  }
}

void setup()
{
  Serial.begin(115200);
  Wire.begin(/*sda*/ 8, /*scl*/ 9);
  Controls::add(thumb); // samples the resting position, so leave the sticks alone
  Controls::add(nunchuk);
}

void loop()
{
  Controls::update();

  if (thumb.directionChanged())
    Serial.printf("thumb %s\n", name(thumb.direction()));
  if (thumb.button().clicks() == 2)
    Serial.println("thumb double click");

  if (nunchuk.connected() && (nunchuk.x() != 0 || nunchuk.y() != 0))
    Serial.printf("nunchuk %d, %d\n", nunchuk.x(), nunchuk.y());
  if (nunchuk.button(1).justPressed())
    Serial.println("C");

  delay(10);
}
