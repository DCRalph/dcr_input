// A detented encoder, decoded on a PCNT unit, and two buttons, printed as they happen.
#include <dcr_input.h>

constexpr int kEncoderA = 5, kEncoderB = 6, kEncoderPush = 7, kBack = 8;

Controls::Encoder knob(kEncoderA, kEncoderB);
Controls::Button push(kEncoderPush);
Controls::Button back([] {
  Controls::Button::Config c;
  c.pin = kBack;
  c.maxClicks = 1; // nothing waits for a double click
  return c;
}());

void setup()
{
  Serial.begin(115200);
  Controls::add(knob);
  Controls::add(push);
  Controls::add(back);
}

void loop()
{
  Controls::update();

  if (knob.delta() != 0)
    Serial.printf("turned %d, at %lld\n", (int)knob.delta(), (long long)knob.position());
  if (push.clicks() > 0)
    Serial.printf("%d click(s)\n", push.clicks());
  if (push.longPressed())
    Serial.printf("long press on click %d\n", -push.clicks());
  if (back.clicks() == 1)
    knob.reset();

  delay(10);
}
