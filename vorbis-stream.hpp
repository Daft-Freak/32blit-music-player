#pragma once

#include <string>

#include "buffered-file.hpp"

struct stb_vorbis;

class VorbisStream final
{
public:
    struct Tags
    {
        std::string album, artist, title, track;
        //...
    };

    VorbisStream();
    ~VorbisStream();

    bool load(std::string filename);

    //Tags parseTags(std::string filename);

    void play(int channel);
    void pause();

    bool getPlaying() const;

    void update();

    int getCurrentSample() const;
    int getDurationMs() const;

    const Tags &getTags() const;

    bool getFileSupported() const;

private:
    void decode(int bufIndex);
    uint64_t calcDuration(std::string filename);

    static void staticCallback(void *arg);
    void callback();

    BufferedFile audioFile;

    int channel = -1;

    stb_vorbis *vorbis;
    unsigned int channels, sampleRate;
    bool needConvert = false;
    float **lastOutput = nullptr;
    int lastOutputSamples = 0;

    static const int audioBufSize = 1024 * 4;
    int16_t audioBuf[2][audioBufSize];
    int16_t *currentSample = nullptr, *endSample = nullptr;
    int dataSize[2]{};
    int curAudioBuf = 0;

    int bufferedSamples = 0;
    int durationMs = 0;

    Tags tags;

    bool supported = true;
};