#pragma once

namespace PlayerControls
{
struct Rectangle
{
    int x, y, width, height;
    bool Contains(int px, int py) const
    {
        return px >= x && px < x + width && py >= y && py < y + height;
    }
};
// Keep rendering and hit targets together. The centre play/pause button
// and timeline retain the original player geometry.
static const Rectangle Repeat = {8, 144, 88, 32};
static const Rectangle Options = {160, 144, 88, 32};
} // namespace PlayerControls
