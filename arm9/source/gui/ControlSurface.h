#pragma once

// A small tiled surface shared by the player buttons and options panel.
// Coordinates are 8-pixel cells on the 32x24 controls screen.
namespace ControlSurface
{
enum Color { Clear, Header, Panel, Button, Selected, Divider };
void Initialize();
void Fill(int x, int y, int width, int height, Color color);
} // namespace ControlSurface
