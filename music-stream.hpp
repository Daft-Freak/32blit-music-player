#pragma once
#include <string>

#include "music-tags.hpp"

class MusicStream
{
public:
    virtual ~MusicStream(){}

    //virtual bool load(std::string filename) = 0;

    virtual void play(int channel) = 0;
    virtual void pause() = 0;

    virtual bool getPlaying() const = 0;

    virtual void update() = 0;

    virtual int getCurrentSample() const = 0;
    virtual int getDurationMs() const = 0;

    virtual const MusicTags &getTags() const = 0;

    virtual bool getFileSupported() const = 0;
};