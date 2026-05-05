#pragma once
#include <string>
#include <cstdint>

struct PoliticalEntity {
    std::string id;
    std::string name;
    uint8_t     color_r = 200;
    uint8_t     color_g = 80;
    uint8_t     color_b = 80;
};
