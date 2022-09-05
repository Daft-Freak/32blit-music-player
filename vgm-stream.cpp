#include <cinttypes>
#include <cstring>

#include "vgm-stream.hpp"

#include "audio/audio.hpp"
#include "engine/engine.hpp"
#include "engine/file.hpp"

#ifdef PROFILER
#include "engine/profiler.hpp"
extern blit::ProfilerProbe *profilerRefillProbe;
extern blit::ProfilerProbe *profilerReadProbe;
extern blit::ProfilerProbe *profilerDecProbe;
#endif

#include "player/playera.hpp"
#include "player/vgmplayer.hpp"

// loader
struct BlitLoader
{
    std::string fileName;
    blit::File file;
    uint32_t offset;
};

static UINT8 BlitLoader_dopen(void *context);
static UINT32 BlitLoader_dread(void *context, UINT8 *buffer, UINT32 numBytes);
static UINT8 BlitLoader_dseek(void *context, UINT32 offset, UINT8 whence);
static UINT8 BlitLoader_dclose(void *context);
static INT32 BlitLoader_dtell(void *context);
static UINT32 BlitLoader_dlength(void *context);
static UINT8 BlitLoader_deof(void *context);
static void BlitLoader_dfree(void *context);

const DATA_LOADER_CALLBACKS fileLoader = {
    0x54494C42,
    "32Blit Loader",
    BlitLoader_dopen,
    BlitLoader_dread,
    BlitLoader_dseek,
    BlitLoader_dclose,
    BlitLoader_dtell,
    BlitLoader_dlength,
    BlitLoader_deof,
    BlitLoader_dfree,
};

static DATA_LOADER *BlitLoader_Init(const char *fileName)
{
    auto dLoader = (DATA_LOADER *)calloc(1, sizeof(DATA_LOADER));
    if(dLoader == NULL) return NULL;

    auto bLoader = new BlitLoader;
    bLoader->fileName = fileName;

    DataLoader_Setup(dLoader, &fileLoader, bLoader);

    return dLoader;
}

static UINT8 BlitLoader_dopen(void *context)
{
    auto loader = (BlitLoader *)context;

    if(!loader->file.open(loader->fileName))
        return 1;

    loader->offset = 0;

    return 0;
}

static UINT32 BlitLoader_dread(void *context, UINT8 *buffer, UINT32 numBytes)
{
    auto loader = (BlitLoader *)context;

    auto read = loader->file.read(loader->offset, numBytes, (char *)buffer);
    if(read <= 0)
        return 0;

    loader->offset += read;

    return read;
}

static UINT8 BlitLoader_dseek(void *context, UINT32 offset, UINT8 whence)
{
    auto loader = (BlitLoader *)context;

    if(whence == SEEK_SET)
    {
        loader->offset = offset;
        return 0;
    }

    blit::debugf("dseek %i\n", whence);
    return 1;
}

static UINT8 BlitLoader_dclose(void *context)
{
    auto loader = (BlitLoader *)context;
    loader->file.close();
    return 0;
}

static INT32 BlitLoader_dtell(void *context)
{
    blit::debugf("dtell\n");
    return 0;
}

static UINT32 BlitLoader_dlength(void *context)
{
    auto loader = (BlitLoader *)context;
    return loader->file.get_length();
}

static UINT8 BlitLoader_deof(void *context)
{
    auto loader = (BlitLoader *)context;
    return loader->offset == loader->file.get_length();
}

static void BlitLoader_dfree(void *context)
{
    auto loader = (BlitLoader *)context;
    delete loader;
}

VGMStream::VGMStream()
{
    player = new PlayerA();

    player->RegisterPlayerEngine(new VGMPlayer);
    // TODO: others?

    player->SetOutputSettings(22050, 2, 16, audioBufSize / 2);

    auto cfg = player->GetConfiguration();
    cfg.masterVol = 0x10000;
    cfg.loopCount = 0;
    cfg.fadeSmpls = 0;
    cfg.endSilenceSmpls = 0;
    cfg.pbSpeed = 1.0;
    player->SetConfiguration(cfg);
}

VGMStream::~VGMStream()
{
    if(loader)
    {
        player->Stop();
        player->UnloadFile();

        DataLoader_Deinit(loader);
    }

    player->UnregisterAllPlayers();
    delete player;
}

bool VGMStream::load(std::string filename)
{
    if(channel != -1)
        blit::channels[channel].off();

    if(loader)
    {
        player->Stop();
        player->UnloadFile();

        DataLoader_Deinit(loader);
    }

    loader = BlitLoader_Init(filename.c_str());

    DataLoader_SetPreloadBytes(loader, 1024);

    if(DataLoader_Load(loader)) {
        blit::debugf("failed to load DataLoader\n");
        DataLoader_Deinit(loader);
        return false;
    }

    if(player->LoadFile(loader))
    {
        blit::debugf("failed to load file\n");
        return false;
    }

    player->Start();

    currentSample = nullptr;
    bufferedSamples = 0;

    auto plrEngine = player->GetPlayer();
    auto durationSamples = plrEngine->Tick2Sample(plrEngine->GetTotalTicks());

    durationMs = (durationSamples * 1000) / 22050;

    // tags
    tags = MusicTags();

    auto playerTags = plrEngine->GetTags();
    while(*playerTags)
    {
        std::string_view key(playerTags[0]);
        if(key == "TITLE")
            tags.title = playerTags[1];
        else if(key == "ARTIST")
            tags.artist = playerTags[1];
        else if(key == "GAME")
            tags.album = playerTags[1];

        playerTags += 2;
    }

    return true;
}

void VGMStream::play(int channel)
{
    this->channel = channel;

    if(!currentSample)
    {
        decode(0);
        decode(1);

        curAudioBuf = 0;
        currentSample = audioBuf[0];
        endSample = currentSample + dataSize[0];
        blit::channels[channel].wave_buf_pos = 0;
    }

    blit::channels[channel].waveforms = blit::Waveform::WAVE;
    blit::channels[channel].user_data  = this;
    blit::channels[channel].wave_buffer_callback = &VGMStream::staticCallback;

    blit::channels[channel].trigger_sustain();
}

void VGMStream::pause()
{
    blit::channels[channel].off();
}

bool VGMStream::getPlaying() const
{
    return channel != -1 && blit::channels[channel].adsr_phase == blit::ADSRPhase::SUSTAIN;
}

void VGMStream::update()
{
    if(!currentSample)
        return;

    // refill audio buffers
    for(int i = 0; i < 2; i++)
    {
        if(!dataSize[i])
            decode(i);
    }
}

int VGMStream::getCurrentSample() const
{
    if(channel == -1)
        return 0;

    return bufferedSamples + blit::channels[channel].wave_buf_pos;
}

int VGMStream::getDurationMs() const
{
    return durationMs;
}

const MusicTags &VGMStream::getTags() const
{
    return tags;
}

bool VGMStream::getFileSupported() const
{
    return true;
}

void VGMStream::decode(int bufIndex)
{
#ifdef PROFILER
    blit::ScopedProfilerProbe scopedProbe(profilerRefillProbe);
#endif

    int samples = 0;

    // stereo -> mono
    while(samples < audioBufSize)
    {
        int16_t tmpBuf[audioBufSize];
        const int maxSize = audioBufSize;
        const int size =  std::min(maxSize, (maxSize - samples) * 2);

        player->Render(size * 2, tmpBuf);

        for(int i = 0; i < size; i += 2, samples++)
            audioBuf[bufIndex][samples] = (tmpBuf[i] + tmpBuf[i + 1]) / 2;
    }

    if(!samples)
    {
        dataSize[bufIndex] = -1;
        return;
    }

    // pad samples, should only happen at the end of the file
    while(samples % 64)
        audioBuf[bufIndex][samples++] = 0;

    dataSize[bufIndex] = samples;
}

void VGMStream::staticCallback(blit::AudioChannel &channel)
{
    reinterpret_cast<VGMStream *>(channel.user_data)->callback(channel);
}

void VGMStream::callback(blit::AudioChannel &channel)
{
    if(!currentSample)
    {
        channel.off();
        return;
    }

    // there was no buffer last time
    if(currentSample == endSample)
    {
        if(dataSize[curAudioBuf])
        {
            // recover from underrun
            endSample = audioBuf[curAudioBuf] + dataSize[curAudioBuf];
        }
        else
        {
            memset(channel.wave_buffer, 0, 64 * sizeof(int16_t));
            return;
        }
    }

    auto out = channel.wave_buffer;

    int i = 0;
    for(; i < 64; i++)
        *(out++) = *(currentSample++);

    // swap buffers
    if(currentSample == endSample)
    {
        dataSize[curAudioBuf] = 0;
        curAudioBuf = ++curAudioBuf % 2;

        if(dataSize[curAudioBuf] == -1) // EOF
            currentSample = endSample = nullptr;
        else
        {
            currentSample = audioBuf[curAudioBuf];
            endSample = currentSample + dataSize[curAudioBuf];

            // if(currentSample == endSample)
            // no more samples available - underrun
        }
    }

    bufferedSamples += 64;
}

