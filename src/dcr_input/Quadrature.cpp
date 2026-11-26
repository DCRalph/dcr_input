#include "Quadrature.h"

#if defined(ARDUINO)
#include <Arduino.h>
#endif

#if defined(ESP_PLATFORM)
#include <driver/gpio.h>
#include <esp_idf_version.h>
#include <soc/soc_caps.h>
#if SOC_PCNT_SUPPORTED
#define DCR_INPUT_PCNT 1
#if ESP_IDF_VERSION_MAJOR >= 5
#include <driver/pulse_cnt.h>
#else
#include <driver/pcnt.h>
#endif
#endif
#endif

#ifndef DRAM_ATTR
#define DRAM_ATTR
#endif

namespace Controls
{
  namespace detail
  {
    namespace
    {
      // Indexed by prev << 2 | curr, A as the high bit of each. A leading B
      // (00 10 11 01) counts down, as the PCNT channels below do.
      DRAM_ATTR const int8_t kSteps[16] = {0, 1, -1, 0, -1, 0, 0, 1, 1, 0, 0, -1, 0, -1, 1, 0};
    }

    int8_t quadratureStep(uint8_t prev, uint8_t curr) { return kSteps[((prev & 3) << 2) | (curr & 3)]; }

    int64_t scaleCounts(int64_t fullCounts, Decoding decoding)
    {
      const int64_t d = decoding == Decoding::Full ? 1 : decoding == Decoding::Half ? 2 : 4;
      return fullCounts >= 0 ? fullCounts / d : -((-fullCounts + d - 1) / d);
    }

    int32_t pcntDelta(int16_t before, int16_t after)
    {
      int32_t d = static_cast<int32_t>(after) - before;
      if (d < -kPcntLimit / 2)
        d += kPcntLimit;
      else if (d > kPcntLimit / 2)
        d -= kPcntLimit;
      return d;
    }

    bool QuadratureCounter::begin(int pinA, int pinB, Decoding decoding, Pull pull, uint16_t glitchFilterNs)
    {
      if (ready())
        return true;
      decoding_ = decoding;
      if (beginPcnt(pinA, pinB, decoding, pull, glitchFilterNs))
        backend_ = Backend::Pcnt;
      else if (beginInterrupts(pinA, pinB, pull))
        backend_ = Backend::Interrupts;
      return ready();
    }

    int64_t QuadratureCounter::total()
    {
      if (backend_ == Backend::Pcnt)
      {
        const int16_t now = readPcnt();
        total_ += pcntDelta(pcntLast_, now);
        pcntLast_ = now;
        return total_;
      }
      if (backend_ == Backend::Interrupts)
      {
        // Unsigned subtraction follows the 32-bit count through any wrap.
        const int32_t now = isr_.count;
        total_ += static_cast<int32_t>(static_cast<uint32_t>(now) - static_cast<uint32_t>(isrLast_));
        isrLast_ = now;
        return scaleCounts(total_, decoding_);
      }
      return 0;
    }

#if defined(ARDUINO)
    namespace
    {
      uint8_t readAB(uint8_t pinA, uint8_t pinB)
      {
        return static_cast<uint8_t>((digitalRead(pinA) == HIGH ? 2 : 0) | (digitalRead(pinB) == HIGH ? 1 : 0));
      }

      // Bounce only steps back and forth here, and a skipped state counts 0.
      void IRAM_ATTR onEdge(void *arg)
      {
        auto *s = static_cast<QuadratureCounter::IsrState *>(arg);
        const uint8_t ab = static_cast<uint8_t>((digitalRead(s->pinA) == HIGH ? 2 : 0) |
                                                (digitalRead(s->pinB) == HIGH ? 1 : 0));
        s->count = s->count + kSteps[(s->ab << 2) | ab];
        s->ab = ab;
      }
    }

    bool QuadratureCounter::beginInterrupts(int pinA, int pinB, Pull pull)
    {
#ifdef NOT_AN_INTERRUPT
      if (digitalPinToInterrupt(pinA) == NOT_AN_INTERRUPT || digitalPinToInterrupt(pinB) == NOT_AN_INTERRUPT)
        return false;
#endif
      pinMode(pinA, inputMode(pull));
      pinMode(pinB, inputMode(pull));
      isr_.pinA = static_cast<uint8_t>(pinA);
      isr_.pinB = static_cast<uint8_t>(pinB);
      isr_.ab = readAB(isr_.pinA, isr_.pinB);
      isr_.count = 0;
      isrLast_ = 0;
      attachInterruptArg(digitalPinToInterrupt(pinA), onEdge, &isr_, CHANGE);
      attachInterruptArg(digitalPinToInterrupt(pinB), onEdge, &isr_, CHANGE);
      return true;
    }
#else
    bool QuadratureCounter::beginInterrupts(int, int, Pull) { return false; }
#endif

#if DCR_INPUT_PCNT
    namespace
    {
      void setPull(int pin, Pull pull)
      {
        gpio_set_pull_mode(static_cast<gpio_num_t>(pin), pull == Pull::Up     ? GPIO_PULLUP_ONLY
                                                         : pull == Pull::Down ? GPIO_PULLDOWN_ONLY
                                                                              : GPIO_FLOATING);
      }
    }

    // Both drivers get the same channels. Channel A counts A's edges, reversed
    // while B is high; channel B counts B's edges, reversed while A is low.
#if ESP_IDF_VERSION_MAJOR >= 5
    bool QuadratureCounter::beginPcnt(int pinA, int pinB, Decoding decoding, Pull pull, uint16_t glitchFilterNs)
    {
      pcnt_unit_config_t unitConfig = {};
      unitConfig.low_limit = -kPcntLimit;
      unitConfig.high_limit = kPcntLimit;
      pcnt_unit_handle_t unit = nullptr;
      if (pcnt_new_unit(&unitConfig, &unit) != ESP_OK)
        return false;

      if (glitchFilterNs != 0)
      {
        pcnt_glitch_filter_config_t filter = {};
        filter.max_glitch_ns = glitchFilterNs < kPcntMaxFilterNs ? glitchFilterNs : kPcntMaxFilterNs;
        pcnt_unit_set_glitch_filter(unit, &filter);
      }

      pcnt_chan_config_t a = {};
      a.edge_gpio_num = pinA;
      a.level_gpio_num = pinB;
      pcnt_channel_handle_t chanA = nullptr;
      if (pcnt_new_channel(unit, &a, &chanA) != ESP_OK)
      {
        pcnt_del_unit(unit);
        return false;
      }
      pcnt_channel_set_edge_action(chanA,
                                   decoding == Decoding::Single ? PCNT_CHANNEL_EDGE_ACTION_HOLD
                                                                : PCNT_CHANNEL_EDGE_ACTION_DECREASE,
                                   PCNT_CHANNEL_EDGE_ACTION_INCREASE);
      pcnt_channel_set_level_action(chanA, PCNT_CHANNEL_LEVEL_ACTION_INVERSE, PCNT_CHANNEL_LEVEL_ACTION_KEEP);

      if (decoding == Decoding::Full)
      {
        pcnt_chan_config_t b = {};
        b.edge_gpio_num = pinB;
        b.level_gpio_num = pinA;
        pcnt_channel_handle_t chanB = nullptr;
        if (pcnt_new_channel(unit, &b, &chanB) != ESP_OK)
        {
          pcnt_del_channel(chanA);
          pcnt_del_unit(unit);
          return false;
        }
        pcnt_channel_set_edge_action(chanB, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
        pcnt_channel_set_level_action(chanB, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
      }

      // Watch points at the limits make the counter restart from 0 there.
      pcnt_unit_add_watch_point(unit, kPcntLimit);
      pcnt_unit_add_watch_point(unit, -kPcntLimit);
      setPull(pinA, pull);
      setPull(pinB, pull);

      pcnt_unit_enable(unit);
      pcnt_unit_clear_count(unit);
      pcnt_unit_start(unit);
      handle_ = unit;
      return true;
    }

    int16_t QuadratureCounter::readPcnt()
    {
      int value = 0;
      pcnt_unit_get_count(static_cast<pcnt_unit_handle_t>(handle_), &value);
      return static_cast<int16_t>(value);
    }
#else
    namespace
    {
      uint32_t usedUnits = 0; // bit n: this library holds unit n
    }

    bool QuadratureCounter::beginPcnt(int pinA, int pinB, Decoding decoding, Pull pull, uint16_t glitchFilterNs)
    {
      int unit = -1;
      for (int u = 0; u < SOC_PCNT_UNITS_PER_GROUP; u++)
        if (!(usedUnits & (1u << u)))
        {
          unit = u;
          break;
        }
      if (unit < 0)
        return false;
      const pcnt_unit_t u = static_cast<pcnt_unit_t>(unit);

      pcnt_config_t c = {};
      c.unit = u;
      c.counter_h_lim = kPcntLimit;
      c.counter_l_lim = -kPcntLimit;

      c.channel = PCNT_CHANNEL_0;
      c.pulse_gpio_num = pinA;
      c.ctrl_gpio_num = pinB;
      c.pos_mode = decoding == Decoding::Single ? PCNT_COUNT_DIS : PCNT_COUNT_DEC;
      c.neg_mode = PCNT_COUNT_INC;
      c.lctrl_mode = PCNT_MODE_KEEP;
      c.hctrl_mode = PCNT_MODE_REVERSE;
      if (pcnt_unit_config(&c) != ESP_OK)
        return false;

      c.channel = PCNT_CHANNEL_1;
      if (decoding == Decoding::Full)
      {
        c.pulse_gpio_num = pinB;
        c.ctrl_gpio_num = pinA;
        c.pos_mode = PCNT_COUNT_DEC;
        c.neg_mode = PCNT_COUNT_INC;
        c.lctrl_mode = PCNT_MODE_REVERSE;
        c.hctrl_mode = PCNT_MODE_KEEP;
      }
      else
      {
        c.pulse_gpio_num = PCNT_PIN_NOT_USED;
        c.ctrl_gpio_num = PCNT_PIN_NOT_USED;
        c.pos_mode = c.neg_mode = PCNT_COUNT_DIS;
        c.lctrl_mode = c.hctrl_mode = PCNT_MODE_KEEP;
      }
      if (pcnt_unit_config(&c) != ESP_OK)
        return false;

      // pcnt_unit_config() turns pull-ups on; set what was asked for.
      setPull(pinA, pull);
      setPull(pinB, pull);

      const uint32_t cycles = static_cast<uint32_t>(glitchFilterNs) * 80 / 1000; // APB is 80 MHz
      if (cycles > 0)
      {
        pcnt_set_filter_value(u, cycles > 1023 ? 1023 : static_cast<uint16_t>(cycles));
        pcnt_filter_enable(u);
      }
      else
        pcnt_filter_disable(u);

      // The limit events make the counter restart from 0 there. No interrupt is installed.
      pcnt_event_enable(u, PCNT_EVT_H_LIM);
      pcnt_event_enable(u, PCNT_EVT_L_LIM);
      pcnt_counter_pause(u);
      pcnt_counter_clear(u);
      pcnt_counter_resume(u);

      usedUnits |= 1u << unit;
      unit_ = unit;
      return true;
    }

    int16_t QuadratureCounter::readPcnt()
    {
      int16_t value = 0;
      pcnt_get_counter_value(static_cast<pcnt_unit_t>(unit_), &value);
      return value;
    }
#endif
#else
    bool QuadratureCounter::beginPcnt(int, int, Decoding, Pull, uint16_t)
    {
      unit_ = -1;
      handle_ = nullptr;
      return false;
    }
    int16_t QuadratureCounter::readPcnt() { return 0; }
#endif
  }
}
