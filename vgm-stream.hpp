#pragma once

#include <string>

#include "audio/audio.hpp"
#include "engine/file.hpp"

#include "music-stream.hpp"
#include "music-tags.hpp"

class VGMStream final : public MusicStream
{
public:
    VGMStream();
    ~VGMStream();

    bool load(std::string filename);

    void play(int channel) override;
    void pause() override;

    bool getPlaying() const override;

    void update() override;

    int getCurrentSample() const override;
    int getDurationMs() const override;

    const MusicTags &getTags() const override;

    bool getFileSupported() const override;

private:
    void decode(int bufIndex);

    static void staticCallback(blit::AudioChannel &channel);
    void callback(blit::AudioChannel &channel);

    int channel = -1;

    //stb_vorbis *vorbis;
    unsigned int channels, sampleRate;
    bool needConvert = false;

    static const int audioBufSize = 1024 * 4;
    int16_t audioBuf[2][audioBufSize];
    int16_t *currentSample = nullptr, *endSample = nullptr;
    int dataSize[2]{};
    int curAudioBuf = 0;

    int bufferedSamples = 0;
    int durationMs = 0;

    MusicTags tags;

    bool supported = true;
};
