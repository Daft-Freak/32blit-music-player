#pragma once

#include <string>

#include "audio/audio.hpp"
#include "engine/file.hpp"

#include "music-stream.hpp"
#include "music-tags.hpp"

class MP3Stream final : public MusicStream
{
public:
    MP3Stream();
    ~MP3Stream();

    bool load(std::string filename, bool doDurationCalc = false);

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
    int calcDuration();

    void read(int32_t len);

    static void staticCallback(blit::AudioChannel &channel);
    void callback(blit::AudioChannel &channel);

    // file io
    blit::File file;
    uint32_t fileOffset = 0;

    static const int fileBufferSize = 1024 * 4;
    uint8_t fileBuffer[fileBufferSize];
    int32_t fileBufferFilled = 0;

    int channel = -1;

    // decoding
    void *mp3dec = nullptr;
    bool needConvert = false;

    static const int audioBufSize = 1152 * 4;
    int16_t audioBuf[2][audioBufSize];
    int16_t *currentSample = nullptr, *endSample = nullptr;
    int dataSize[2]{};
    int curAudioBuf = 0;

    int bufferedSamples = 0;
    int durationMs = 0;

    MusicTags tags;

    bool supported = true;
};