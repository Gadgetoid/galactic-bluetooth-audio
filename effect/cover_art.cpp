#include "effect.hpp"

void CoverArt::update(int16_t *buffer16, size_t sample_count) {
    if (this->render){
        // TODO:
    }
}

void CoverArt::init(uint32_t sample_frequency) {
}

void CoverArt::set_cover(const uint8_t * data, uint32_t len){
    this->cover_data = data;
    this->cover_len = len;
    this->render = true;
};

