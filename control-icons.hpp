#pragma once

#include "engine/input.hpp"
#include "graphics/sprite.hpp"

class ControlIcons final
{
public:
    ControlIcons();
    ~ControlIcons();

    enum class Icon
    {
        A = 0,
        B,
        X,
        Y,
        Home,
        Menu,

        StickLR,
        StickUD,
        StickL,
        StickU,
        StickD,
        StickR,

        DPadL,
        DPadU,
        DPadD,
        DPadR
    };

    void render(Icon icon, blit::Point pos, int size = 12);
private:

    blit::SpriteSheet *sprites;
};