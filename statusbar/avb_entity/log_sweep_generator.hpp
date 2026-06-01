// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT
#ifndef STATUSBAR_AVB_ENTITY_LOG_SWEEP_GENERATOR_HPP
#define STATUSBAR_AVB_ENTITY_LOG_SWEEP_GENERATOR_HPP

#include <cmath>
#include <cstdint>
#include <numbers>

namespace statusbar::avb_entity {

/// Repeating logarithmic sine sweep (chirp): the instantaneous frequency moves
/// f_start -> f_end across `duration_s`, then restarts. Logarithmic = constant
/// octaves/second, so the sweep sounds even across the range. RT-safe: `next()`
/// is a phase accumulator + one sinf, no allocation.
///
/// f(t) = f_start * (f_end/f_start)^(t/duration),   t in [0, duration)
/// phase advances by 2*pi*f/Fs each sample; phase resets to 0 at each restart so
/// every cycle begins cleanly at the low frequency (no boundary discontinuity).
class LogSweepGenerator
{
  public:
    void configure(double f_start_hz, double f_end_hz, double duration_s, double sample_rate_hz, float amplitude) noexcept
    {
        f_start_ = (f_start_hz > 0.0) ? f_start_hz : 1.0;
        double const f_end = (f_end_hz > 0.0) ? f_end_hz : 1.0;
        ratio_ = f_end / f_start_;
        sample_rate_ = (sample_rate_hz > 0.0) ? sample_rate_hz : 1.0;
        double const dur = (duration_s > 0.0) ? duration_s : 1.0;
        period_samples_ = dur * sample_rate_;
        amplitude_ = amplitude;
        reset();
    }

    void reset() noexcept
    {
        idx_ = 0;
        phase_ = 0.0;
    }

    /// The instantaneous frequency (Hz) of the sample `next()` will return next.
    [[nodiscard]] auto current_frequency_hz() const noexcept -> double
    {
        double const t_frac = period_frac();
        return f_start_ * std::pow(ratio_, t_frac);
    }

    /// Produce one sample and advance. Output = amplitude * sin(phase).
    [[nodiscard]] auto next() noexcept -> float
    {
        auto const out = static_cast<float>(amplitude_ * std::sin(phase_));
        double const f = current_frequency_hz();
        phase_ += (2.0 * std::numbers::pi * f) / sample_rate_;
        if (phase_ >= 2.0 * std::numbers::pi) {
            phase_ -= 2.0 * std::numbers::pi;
        }
        ++idx_;
        if (period_samples_ >= 1.0 && static_cast<double>(idx_) >= period_samples_) {
            // Restart the sweep cleanly at the low frequency.
            idx_ = 0;
            phase_ = 0.0;
        }
        return out;
    }

  private:
    [[nodiscard]] auto period_frac() const noexcept -> double
    {
        if (period_samples_ < 1.0) {
            return 0.0;
        }
        return static_cast<double>(idx_) / period_samples_;
    }

    double f_start_{20.0};
    double ratio_{50.0};  // f_end/f_start (1000/20)
    double sample_rate_{96000.0};
    double period_samples_{480000.0};  // 5 s @ 96 kHz
    double amplitude_{0.5};
    double phase_{0.0};
    uint64_t idx_{0};
};

}  // namespace statusbar::avb_entity

#endif  // STATUSBAR_AVB_ENTITY_LOG_SWEEP_GENERATOR_HPP
