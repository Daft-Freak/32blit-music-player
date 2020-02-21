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

void ControlIcons::render(Icon icon, blit::Point pos, int size)
{
    int spriteIndex = static_cast<int>(icon);

    blit::Rect spriteRect;
    if(size <= 8)
        spriteRect = blit::Rect(spriteIndex, 0, 1, 1);
    else
        spriteRect = blit::Rect((spriteIndex % 8) * 2, (spriteIndex / 8) * 2 + 1, 2, 2);

    if(size >= 16)
        spriteRect.y += 4;

    blit::screen.sprites = sprites;
    blit::screen.sprite(spriteRect, pos);
}