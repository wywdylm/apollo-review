// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace decision {
struct KeeperPositionControl {
    bool correcting_x{false};
    bool correcting_y{false};
    void reset() { correcting_x = correcting_y = false; }
    static double axis(double error, bool& correcting) {
        if (!std::isfinite(error)) { correcting = false; return 0.0; }
        // Hysteresis: activate outside 8cm and finish within 4cm.
        if (std::abs(error) <= 0.04) correcting = false;
        else if (std::abs(error) > 0.08) correcting = true;
        if (!correcting) return 0.0;
        // A bounded command above the observed near-target gait dead zone.
        return std::copysign(std::clamp(2.0 * std::abs(error), 0.25, 0.30), error);
    }
    std::array<double, 2> velocity(const std::array<double, 2>& error) {
        std::array<double, 2> v{axis(error[0], correcting_x), axis(error[1], correcting_y)};
        const double speed = std::hypot(v[0], v[1]);
        if (speed > 0.35) { v[0] *= 0.35/speed; v[1] *= 0.35/speed; }
        return v;
    }
};
} // namespace decision
