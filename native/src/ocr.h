#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct OcrBox {
    std::wstring text;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

struct OcrPass {
    bool ok = false;
    std::wstring error;
    std::vector<OcrBox> lines;
    std::vector<OcrBox> words;
};

OcrPass recognize_bgra(const std::uint8_t* pixels, int width, int height, int stride);
