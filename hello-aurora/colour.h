#pragma once

struct Rgb { float r, g, b; };

inline Rgb hsvToRgb(float h, float s, float v)
{
    h = h - static_cast<int>(h);   // wrap to [0, 1)
    if (h < 0.f) h += 1.f;

    int   i = static_cast<int>(h * 6.f);
    float f = h * 6.f - i;
    float p = v * (1.f - s);
    float q = v * (1.f - f * s);
    float t = v * (1.f - (1.f - f) * s);

    switch (i % 6)
    {
        case 0: return {v, t, p};
        case 1: return {q, v, p};
        case 2: return {p, v, t};
        case 3: return {p, q, v};
        case 4: return {t, p, v};
        case 5: return {v, p, q};
    }
    return {0.f, 0.f, 0.f};
}
