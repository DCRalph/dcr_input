#pragma once

// Buttons, rotary encoders and joysticks as debounced, per-frame state.
// Add each with Controls::add() in setup(), call Controls::update() once per loop.
#include "dcr_input/Button.h"
#include "dcr_input/Controls.h"
#include "dcr_input/Encoder.h"
#include "dcr_input/Joystick.h"
#include "dcr_input/JoystickSources.h"
