#pragma once

struct StereoFrame { float left, right; };

inline StereoFrame scaleVolume(StereoFrame in, float volume)
{
    return {in.left * volume, in.right * volume};
}
