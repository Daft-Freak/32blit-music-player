#pragma once
#include <cstdint>
#include <string>

#include "engine/file.hpp"

class BufferedFile final
{
public:
    BufferedFile();
    ~BufferedFile();

    bool open(std::string filename);

    void read(int32_t len);

    int32_t getBufferFilled() const;
    const uint8_t *getBuffer() const;

private:
    blit::File file;
    uint32_t offset = 0;

    static const int bufferSize = 1024 * 4;
    uint8_t buffer[bufferSize];
    int32_t bufferFilled = 0;
};