#include "Pcnt.h"

#if defined(ESP_PLATFORM)
#include <driver/gpio.h>
#include <esp_idf_version.h>
#include <soc/soc_caps.h>
#if SOC_PCNT_SUPPORTED
#if ESP_IDF_VERSION_MAJOR >= 5
#include <driver/pulse_cnt.h>
#else
#include <driver/pcnt.h>
#endif
#endif
#endif

namespace Controls
{
  namespace detail
  {
    int32_t pcntDelta(int16_t before, int16_t after)
    {
      int32_t d = static_cast<int32_t>(after) - before;
      if (d < -kPcntLimit / 2)
        d += kPcntLimit;
      else if (d > kPcntLimit / 2)
        d -= kPcntLimit;
      return d;
    }

#if defined(ESP_PLATFORM) && SOC_PCNT_SUPPORTED
    namespace
    {
      void setPull(int pin, Pull pull)
      {
        gpio_set_pull_mode(static_cast<gpio_num_t>(pin), pull == Pull::Up     ? GPIO_PULLUP_ONLY
                                                         : pull == Pull::Down ? GPIO_PULLDOWN_ONLY
                                                                              : GPIO_FLOATING);
      }
    }

    // Both drivers get the same channels, so a knob turns the same way on either.
    // Channel A counts A's edges, reversed while B is high; channel B counts B's
    // edges, reversed while A is low. Each step of A-leads-B then counts -1.
#if ESP_IDF_VERSION_MAJOR >= 5
    bool PcntCounter::begin(int pinA, int pinB, Decoding decoding, Pull pull, uint16_t glitchFilterNs)
    {
      if (ready_)
        return true;

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
      ready_ = true;
      return true;
    }

    int16_t PcntCounter::read()
    {
      int value = 0;
      if (ready_)
        pcnt_unit_get_count(static_cast<pcnt_unit_handle_t>(handle_), &value);
      return static_cast<int16_t>(value);
    }
#else
    namespace
    {
      uint32_t usedUnits = 0; // bit n: this library holds unit n
    }

    bool PcntCounter::begin(int pinA, int pinB, Decoding decoding, Pull pull, uint16_t glitchFilterNs)
    {
      if (ready_)
        return true;

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
      ready_ = true;
      return true;
    }

    int16_t PcntCounter::read()
    {
      int16_t value = 0;
      if (ready_)
        pcnt_get_counter_value(static_cast<pcnt_unit_t>(unit_), &value);
      return value;
    }
#endif
#else
    // No PCNT (host builds, ESP32-C3): pins cannot be counted; use Config::readCount.
    bool PcntCounter::begin(int, int, Decoding, Pull, uint16_t)
    {
      unit_ = -1;
      handle_ = nullptr;
      return false;
    }
    int16_t PcntCounter::read() { return 0; }
#endif
  }
}
