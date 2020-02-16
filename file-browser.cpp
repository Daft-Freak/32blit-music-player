#include "file-browser.hpp"

#include "engine/engine.hpp"
#include "engine/input.hpp"

FileBrowser::FileBrowser(const blit::Font &font) : font(font)
{
    // too early
    //files = blit::list_files("");
}

void FileBrowser::init()
{
    updateList();

    // default
    if(displayRect.w == 0 && displayRect.h == 0)
        displayRect = blit::Rect(blit::Point(0, 0), blit::screen.bounds);
}

void FileBrowser::render()
{
    int itemHeight = font.char_h + 2;

    int y = 0;
    int i = 0;

    // scrolling
    int totalHeight = files.size() * itemHeight;
    int selectedY = selectedFile * itemHeight;
    int yOff = displayRect.h / 2 - selectedY;

    if(yOff < -(totalHeight - displayRect.h))
        yOff = -(totalHeight - displayRect.h);

    yOff = std::min(0, yOff);

    for(auto &f : files)
    {
        if(y + yOff > displayRect.h)
            break;

        if(y + yOff + itemHeight < 0)
        {
            i++;
            y += itemHeight;
            continue;
        }

        // background
        if(i == selectedFile)
            blit::screen.pen = blit::Pen(0xF7, 0xF7, 0xF7);
        else if(i % 2)
            blit::screen.pen = blit::Pen(0x22, 0x22, 0x22);
        else
            blit::screen.pen = blit::Pen(0x11, 0x11, 0x11);

        auto str = f.name + ((f.flags & blit::FileFlags::directory) ? "/" : "");

        blit::Rect r(displayRect.x, displayRect.y + yOff + y, displayRect.w, itemHeight);
        blit::Rect clipped = r.intersection(displayRect);

        blit::screen.rectangle(clipped);

        if(i == selectedFile)
            blit::screen.pen = blit::Pen(0x11, 0x11, 0x11);
        else
            blit::screen.pen = blit::Pen(0xF7, 0xF7, 0xF7);

        r.h += font.spacing_y; // account for vertical spacing in alignment

        r.x += itemPadding;
        r.w -= itemPadding * 2;

        blit::screen.text(str, font, r, true, blit::TextAlign::center_left, clipped);

        y += itemHeight;
        i++;
    }
}

void FileBrowser::update(uint32_t time)
{
    unsigned int upDown = blit::Button::DPAD_UP | blit::Button::DPAD_DOWN;

    if((blit::buttons & upDown) != (lastButtonState & upDown))
        repeatStartTime = time;

    // repeat delay
    if((time - repeatStartTime) % 200 == 0)
    {
        if(blit::buttons & blit::Button::DPAD_UP)
            selectedFile--;
        else if(blit::buttons & blit::Button::DPAD_DOWN)
            selectedFile++;

        if(selectedFile == -1)
            selectedFile = files.size() - 1;
        else if(selectedFile >= static_cast<int>(files.size()))
            selectedFile = 0;
    }

    // A released
    if((lastButtonState & blit::Button::A) && !(blit::buttons & blit::Button::A))
    {
        if(files[selectedFile].flags & blit::FileFlags::directory)
        {
            if(!curDir.empty())
                curDir += "/";

            curDir += files[selectedFile].name;
            updateList();
        }
        else if(onFileOpen)
            onFileOpen(curDir + (curDir.empty() ? "" : "/") + files[selectedFile].name);
    }
    // B released
    else if((lastButtonState & blit::Button::B) && !(blit::buttons & blit::Button::B))
    {
        if(!curDir.empty())
        {
            // go up
            auto pos = curDir.find_last_of('/');
            if(pos == std::string::npos)
                curDir = "";
            else
                curDir = curDir.substr(0, pos);

            updateList();
        }
    }

    lastButtonState = blit::buttons;
}

void FileBrowser::setDisplayRect(blit::Rect r)
{
    displayRect = r;
}

void FileBrowser::setExtensions(std::set<std::string> exts)
{
    fileExts = exts;
}

void FileBrowser::setOnFileOpen(void (*func)(std::string))
{
    onFileOpen = func;
}

void FileBrowser::updateList()
{
    files = blit::list_files(curDir);
    selectedFile = 0;

    std::sort(files.begin(), files.end(), [](blit::FileInfo &a, blit::FileInfo & b){return a.name < b.name;});

    if(fileExts.empty())
        return;

    // filter by extensions
    files.erase(std::remove_if(files.begin(), files.end(), [this](const blit::FileInfo &f)
    {
        if(!(f.flags & blit::FileFlags::directory))
        {
            std::string ext;
            auto dotPos = f.name.find_last_of('.');
            if(dotPos != std::string::npos)
                ext = f.name.substr(dotPos);

            // convert to lower case
            std::for_each(ext.begin(), ext.end(), [](char & c) {c = tolower(c);});

            if(fileExts.find(ext) == fileExts.end())
                return true;
        }

        return false;
    }), files.end());
}
