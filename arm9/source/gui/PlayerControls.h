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
enum Button { None, Back, Repeat, Previous, PlayPause, Next, Options };
static const Rectangle Timeline = {16, 112, 224, 24};
static const Rectangle Buttons[] = {
    {0,0,0,0}, {8,8,32,32}, {8,140,40,48}, {56,140,40,48},
    {104,136,48,52}, {160,140,40,48}, {208,140,40,48}
};
inline Button Hit(int x, int y)
{
    for (int i = Back; i <= Options; ++i)
        if (Buttons[i].Contains(x, y)) return (Button)i;
    return None;
}
} // namespace PlayerControls
