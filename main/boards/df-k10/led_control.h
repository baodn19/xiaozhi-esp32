#ifndef LED_CONTROL_H
#define LED_CONTROL_H

#include "led/circular_strip.h"

class LedStripControl {
private:
    CircularStrip* led_strip_;
    int brightness_level_;  // Brightness level (0-8)

    int LevelToBrightness(int level) const;  // Convert level to brightness value
    StripColor RGBToColor(int red, int green, int blue);

public:
    explicit LedStripControl(CircularStrip* led_strip);
}; 

#endif // LED_STRIP_CONTROL_H
