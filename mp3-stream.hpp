#pragma once

#include <string>

#define MINIMP3_ONLY_MP3
#include "minimp3.h"

#include "engine/file.hpp"

#include "music-stream.hpp"
#include "music-tags.hpp"

class MP3Stream final : public MusicStream
{
public:
    MP3Stream();

    bool load(std::string filename);

    MusicTags parseTags(std::string filename);

    void play(int channel);
    void pause();

    bool getPlaying() const;

    void update();

    int getCurrentSample() const;
    int getDurationMs() const;

    const MusicTags &getTags() const;

    bool getFileSupported() const;

private:
    void decode(int bufIndex);
    int calcDuration(std::string filename);

    void read(int32_t len);

    static void staticCallback(void *arg);
    void callback();

    // file io
    blit::File file;
    uint32_t fileOffset = 0;

    static const int fileBufferSize = 1024 * 4;
    uint8_t fileBuffer[fileBufferSize];
    int32_t fileBufferFilled = 0;

    int channel = -1;

    // decoding
    mp3dec_t mp3dec;
    bool needConvert = false;

    static const int audioBufSize = MINIMP3_MAX_SAMPLES_PER_FRAME * 2;
    int16_t audioBuf[2][audioBufSize];
    int16_t *currentSample = nullptr, *endSample = nullptr;
    int dataSize[2]{};
    int curAudioBuf = 0;

    int bufferedSamples = 0;
    int durationMs = 0;

    MusicTags tags;

    bool supported = true;
};