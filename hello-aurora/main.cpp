/** hello-aurora
 *
 *  Knob colour control and audio passthrough.
 *  Each knob drives the hue of its paired LED via HSV.
 *  MIX knob also controls audio volume.
 *  FREEZE and REVERSE button LEDs light white when pressed.
 *  Bottom LEDs driven by first 3 knobs for physical position mapping.
 */
#include "aurora.h"
#include "colour.h"
#include "audio.h"

using namespace daisy;
using namespace aurora;

Hardware hw;

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    hw.ProcessAllControls();
    float volume = hw.GetKnobValue(KNOB_MIX);
    for (size_t i = 0; i < size; i++)
    {
        StereoFrame frame = scaleVolume({in[0][i], in[1][i]}, volume);
        out[0][i] = frame.left;
        out[1][i] = frame.right;
    }
}

int main(void)
{
    hw.Init();
    hw.StartAudio(AudioCallback);

    const Leds knobLeds[KNOB_LAST] = {
        LED_1,           // KNOB_TIME
        LED_2,           // KNOB_REFLECT
        LED_3,           // KNOB_MIX
        LED_4,           // KNOB_ATMOSPHERE
        LED_5,           // KNOB_BLUR
        LED_6,           // KNOB_WARP
    };

    const Leds  botLeds[3]  = { LED_BOT_1, LED_BOT_2, LED_BOT_3 };
    const int   botKnobs[3] = { KNOB_TIME, KNOB_REFLECT, KNOB_MIX };

    while (1)
    {
        hw.ClearLeds();

        for (int i = 0; i < KNOB_LAST; i++)
        {
            Rgb c = hsvToRgb(hw.GetKnobValue(i), 1.f, 1.f);
            hw.SetLed(knobLeds[i], c.r, c.g, c.b);
        }

        for (int i = 0; i < 3; i++)
        {
            Rgb c = hsvToRgb(hw.GetKnobValue(botKnobs[i]), 1.f, 1.f);
            hw.SetLed(botLeds[i], c.r, c.g, c.b);
        }

        if (hw.GetButton(SW_FREEZE).Pressed())
            hw.SetLed(LED_FREEZE, 1.f, 1.f, 1.f);

        if (hw.GetButton(SW_REVERSE).Pressed())
            hw.SetLed(LED_REVERSE, 1.f, 1.f, 1.f);

        hw.WriteLeds();
    }
}
