#include "control-icons.hpp"

#include "assets.hpp"

#include "engine/engine.hpp"

ControlIcons::ControlIcons()
{
    sprites = blit::SpriteSheet::load(asset_buttons);
}

ControlIcons::~ControlIcons()
{
    delete sprites;
}

void ControlIcons::render(Icon icon, blit::Point pos, blit::Pen colour, int size)
{
    int spriteIndex = static_cast<int>(icon);

    blit::Rect spriteRect;
    if(size <= 8)
        spriteRect = blit::Rect(spriteIndex, 0, 1, 1);
    else
        spriteRect = blit::Rect((spriteIndex % 8) * 2, (spriteIndex / 8) * 2 + 1, 2, 2);

    if(size >= 16)
        spriteRect.y += 4;

    int avgCol = (colour.r + colour.g + colour.b) / 3;

    blit::screen.sprites = sprites;
    sprites->palette[1] = colour;
    sprites->palette[2] = avgCol > 127 ? blit::Pen(colour.r / 2, colour.g / 2, colour.b / 2, colour.a) : blit::Pen(colour.r * 2, colour.g * 2, colour.b * 2, colour.a); // shadow

    blit::screen.sprite(spriteRect, pos);
}