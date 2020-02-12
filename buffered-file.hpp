#pragma once
#include <cstdint>
#include <string>

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
    int32_t file = -1;
    uint32_t offset = 0;

    static const int bufferSize = 1024 * 4;
    uint8_t buffer[bufferSize];
    int32_t bufferFilled = 0;
};