#include "Encoder.h"

namespace Controls
{
  void Encoder::begin(uint32_t)
  {
    if (!config_.readCount && config_.pinA >= 0 && config_.pinB >= 0)
      counter_.begin(config_.pinA, config_.pinB, config_.decoding, config_.pull, config_.glitchFilterNs);
    origin_ = count_ = readRaw();
    detent_ = zero_ = 0;
    delta_ = 0;
  }

  void Encoder::update(uint32_t nowMs)
  {
    if (ready())
      feed(readRaw(), nowMs);
  }

  int64_t Encoder::readRaw()
  {
    return config_.readCount ? config_.readCount() : counter_.total();
  }

  // A detent counts only once the shaft is countsPerDetent - 1 counts past the
  // last one, so bounce or a knob resting on a boundary cannot flicker.
  void Encoder::feed(int64_t count, uint32_t nowMs)
  {
    count_ = count;
    const int64_t c = (count - origin_) * config_.direction;
    const int64_t cpd = config_.countsPerDetent;
    const int64_t before = detent_;

    if (cpd <= 1)
      detent_ = c;
    else
    {
      if (c - detent_ * cpd > 4 * cpd || detent_ * cpd - c > 4 * cpd)
        detent_ = c / cpd;
      while (c >= detent_ * cpd + (cpd - 1))
        detent_++;
      while (c <= detent_ * cpd - (cpd - 1))
        detent_--;
    }

    delta_ = static_cast<int32_t>(detent_ - before);
    if (delta_ != 0)
      noteActivity(nowMs);
  }
}
