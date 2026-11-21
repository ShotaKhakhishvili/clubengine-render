#pragma once

#include "SceneDesc.h"

#include <cmath>

namespace CER
{

// Maps 0..1 to 0..1 (Back and Elastic overshoot on purpose)
inline double Ease(EEase ease, double t)
{
    constexpr double pi = 3.14159265358979323846;
    constexpr double c1 = 1.70158;
    constexpr double c2 = c1 * 1.525;
    constexpr double c3 = c1 + 1.0;

    switch (ease)
    {
    case EEase::Linear:     return t;
    case EEase::Step:       return t < 1.0 ? 0.0 : 1.0;
    case EEase::InQuad:     return t * t;
    case EEase::OutQuad:    return 1.0 - (1.0 - t) * (1.0 - t);
    case EEase::InOutQuad:  return t < 0.5 ? 2.0 * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 2) / 2.0;
    case EEase::InCubic:    return t * t * t;
    case EEase::OutCubic:   return 1.0 - std::pow(1.0 - t, 3);
    case EEase::InOutCubic: return t < 0.5 ? 4.0 * t * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 3) / 2.0;
    case EEase::InSine:     return 1.0 - std::cos(t * pi / 2.0);
    case EEase::OutSine:    return std::sin(t * pi / 2.0);
    case EEase::InOutSine:  return -(std::cos(pi * t) - 1.0) / 2.0;
    case EEase::InBack:     return c3 * t * t * t - c1 * t * t;
    case EEase::OutBack:    return 1.0 + c3 * std::pow(t - 1.0, 3) + c1 * std::pow(t - 1.0, 2);
    case EEase::InOutBack:
        return t < 0.5
            ? (std::pow(2.0 * t, 2) * ((c2 + 1.0) * 2.0 * t - c2)) / 2.0
            : (std::pow(2.0 * t - 2.0, 2) * ((c2 + 1.0) * (t * 2.0 - 2.0) + c2) + 2.0) / 2.0;
    case EEase::OutBounce:
    {
        constexpr double n1 = 7.5625, d1 = 2.75;
        if (t < 1.0 / d1) return n1 * t * t;
        if (t < 2.0 / d1) { t -= 1.5 / d1;  return n1 * t * t + 0.75; }
        if (t < 2.5 / d1) { t -= 2.25 / d1; return n1 * t * t + 0.9375; }
        t -= 2.625 / d1;
        return n1 * t * t + 0.984375;
    }
    case EEase::OutElastic:
        if (t <= 0.0 || t >= 1.0) return t;
        return std::pow(2.0, -10.0 * t) * std::sin((t * 10.0 - 0.75) * (2.0 * pi / 3.0)) + 1.0;
    }
    return t;
}

}
