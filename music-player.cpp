#include "music-player.hpp"

#include "assets.hpp"
#include "control-icons.hpp"
#include "file-browser.hpp"
#include "mp3-stream.hpp"
#include "vorbis-stream.hpp"

#ifdef PROFILER
#include "engine/profiler.hpp"

blit::Profiler profiler;
blit::ProfilerProbe *profilerUpdateProbe;
blit::ProfilerProbe *profilerRefillProbe;
blit::ProfilerProbe *profilerReadProbe;
blit::ProfilerProbe *profilerDecProbe;
#endif

MP3Stream mp3Stream;
VorbisStream vorbisStream;
MusicStream *musicStream;

const blit::Font tallFont(asset_tall_font);
duh::FileBrowser fileBrowser(tallFont);

std::string fileToLoad;
bool renderedLoadMessage = false;

void openMP3(std::string filename)
{
   // delay loading so that we can show the loading message
   renderedLoadMessage = false;
   fileToLoad = filename;
}

void init()
{
    blit::set_screen_mode(blit::ScreenMode::hires);

#ifdef PROFILER
    profiler.set_display_size(blit::screen.bounds.w, blit::screen.bounds.h);
    profiler.set_rows(5);
    profiler.set_alpha(200);
    profiler.display_history(true);

    profiler.setup_graph_element(blit::Profiler::dmCur, true, true, blit::Pen(0, 255, 0));
    profiler.setup_graph_element(blit::Profiler::dmAvg, true, true, blit::Pen(0, 255, 255));
    profiler.setup_graph_element(blit::Profiler::dmMax, true, true, blit::Pen(255, 0, 0));
    profiler.setup_graph_element(blit::Profiler::dmMin, true, true, blit::Pen(255, 255, 0));

    profilerUpdateProbe = profiler.add_probe("Update", 300);
    profilerRefillProbe = profiler.add_probe("Refill", 300);
    profilerReadProbe = profiler.add_probe("Read", 300);
    profilerDecProbe = profiler.add_probe("Decode", 300);
#endif

    fileBrowser.set_extensions({".mp3", ".ogg", ".oga"});
    fileBrowser.set_display_rect(blit::Rect(0, 0, blit::screen.bounds.w, blit::screen.bounds.h / 2 + 20));
    fileBrowser.set_on_file_open(openMP3);
    fileBrowser.init();

    auto launchPath = blit::get_launch_path();
    if(launchPath)
    {
        std::string pathStr(launchPath);
        auto pos = pathStr.find_last_of('/');
        if(pos != std::string::npos)
            fileBrowser.set_current_dir(pathStr.substr(0, pos));

        openMP3(launchPath);
    }
}

void formatTime(int timeMs, char *buf, int bufLen)
{
    snprintf(buf, bufLen, "%i:%02i", timeMs / 60000, (timeMs / 1000) % 60);
}

void render(uint32_t time_ms)
{
    blit::screen.alpha = 0xFF;
    blit::screen.pen = blit::Pen(20, 30, 40);
    blit::screen.clear();

#ifdef PROFILER
    profiler.display_probe_overlay(1);

    if(musicStream)
        return;
#endif

    if(!fileToLoad.empty())
    {
        blit::screen.pen = blit::Pen(0xFF, 0xFF, 0xFF);
        blit::screen.text("Please wait...", blit::minimal_font, blit::Point(blit::screen.bounds.w / 2, blit::screen.bounds.h / 2), true, blit::TextAlign::center_center);
        renderedLoadMessage = true;
        return;
    }

    fileBrowser.render();

    if(!musicStream)
        return;

    int sampleOffset = musicStream->getCurrentSample();
    int durationMs = musicStream->getDurationMs();

    //float time = sampleOffset / 22050.0f;
    int time = (static_cast<uint64_t>(sampleOffset) * 1000) / 22050;

    // format tag info
    auto &tags = musicStream->getTags();
    std::string info;

    if(!musicStream->getFileSupported())
        info += "WARNING: unsupported file!\n\n";

    if(!tags.artist.empty())
        info += tags.artist + "\n";

    if(!tags.title.empty())
        info += tags.title + "\n";

    if(!tags.album.empty())
        info += tags.album;

    // size for 5 lines of text
    blit::Rect infoRect(5, blit::screen.bounds.h / 2 + 25, blit::screen.bounds.w - 10, (tallFont.char_h + tallFont.spacing_y) * 5);
    int centerH = blit::screen.bounds.h - 10; // center of progress bar

    // progress
    float w = durationMs == 0 ? 0.0f : static_cast<float>(blit::screen.bounds.w - 10) / durationMs * time;

    blit::screen.pen = blit::Pen(0, 0, 0);
    blit::screen.rectangle(blit::Rect(5, centerH - 5, blit::screen.bounds.w - 10, 10));

    if(!musicStream->getFileSupported())
        blit::screen.pen = blit::Pen(255, 0, 0);
    else
        blit::screen.pen = blit::Pen(255, 255, 255);

    blit::screen.rectangle(blit::Rect(5, centerH - 5, w, 10));

    // time
    char buf[10];
    formatTime(time, buf, 10);
    blit::screen.text(buf, blit::minimal_font, blit::Point(5, centerH - 15));

    // duration
    formatTime(durationMs, buf, 10);
    blit::screen.text(buf, blit::minimal_font, blit::Rect(0, centerH - 15, blit::screen.bounds.w - 5, 10), true, blit::TextAlign::top_right);

    // track info
    blit::screen.text(info, tallFont, infoRect, true, blit::bottom_left);

    // play/pause
    std::string playPauseLabel = musicStream->getPlaying() ? "Pause" : "Play";
    auto labelLen = blit::screen.measure_text(playPauseLabel, tallFont).w;
    blit::screen.text(playPauseLabel, tallFont, infoRect, true, blit::top_right);
    duh::draw_control_icon(&blit::screen, duh::Icon::X, infoRect.tr() - blit::Point(labelLen + 12 + 2, 0));

    //
    //blit::screen.text(std::to_string(initTime) + " " + std::to_string(dataSize[0]), blit::minimal_font, blit::Point(0));
}

void update(uint32_t time_ms)
{
#ifdef PROFILER
    profiler.set_graph_time(profilerUpdateProbe->elapsed_metrics().uMaxElapsedUs);
    blit::ScopedProfilerProbe scopedProbe(profilerUpdateProbe);
#endif

    static uint32_t lastButtonState = 0;

    fileBrowser.update(time_ms);

    // load file
    if(!fileToLoad.empty() && renderedLoadMessage)
    {
        auto ext = fileToLoad.substr(fileToLoad.find_last_of('.'));
        std::for_each(ext.begin(), ext.end(), [](char & c) {c = tolower(c);});

        if(ext == ".mp3" && mp3Stream.load(fileToLoad, true))
            musicStream = &mp3Stream;
        else if((ext == ".ogg" || ext == ".oga") && vorbisStream.load(fileToLoad))
            musicStream = &vorbisStream;
        else
            musicStream = nullptr;

        if(musicStream)
            musicStream->play(0);

        fileToLoad = "";
    }

    if(musicStream)
        musicStream->update();

    // x released
    if(musicStream && (lastButtonState & blit::Button::X) && !(blit::buttons & blit::Button::X))
    {
        if(musicStream->getPlaying())
            musicStream->pause();
        else
            musicStream->play(0);
    }

    lastButtonState = blit::buttons;
}
