#include "effect.hpp"
#include "3rd-party/JPEGDEC/JPEGDEC.h"

// cover art thumbnails are 200x200, we draw it 1/8 => 25 x 25 at offset 3/3
#define OFFSET_X 3
#define OFFSET_Y 3

static JPEGDEC decoder;

static int JPEGDraw(JPEGDRAW *pDraw) {
    Display * display = (Display*) pDraw->pUser;
    uint16_t i;
    uint16_t j;
    uint16_t * pixel = pDraw->pPixels;
    for (i=0;i<pDraw->iHeight;i++) {
        uint16_t y = pDraw->y + i + OFFSET_Y;
        if (y >= Display::HEIGHT) {
            continue;
        }
        for (j=0;j<pDraw->iWidth; j++) {
            uint16_t x = pDraw->x + j + OFFSET_X;
            if (x >= Display::WIDTH) {
                continue;
            }
            uint16_t rgb565 = *pixel++;
            uint8_t red_value =   (rgb565 & 0xF800) >> 8;
            uint8_t green_value = (rgb565 &  0x7E0) >> 3;
            uint8_t blue_value =  (rgb565 &   0x1F) << 3;
            display->set_pixel(x, y, red_value, green_value, blue_value);
        }
    }
    return 1;
}

void CoverArt::update(int16_t *buffer16, size_t sample_count) {
    if (this->render){
        this->render = false;
        if (this->cover_data != NULL){
            decoder.openRAM((uint8_t *) this->cover_data, this->cover_len, JPEGDraw);
            decoder.setUserPointer(&display);
            decoder.decode(0, 0, JPEG_SCALE_EIGHTH);
            decoder.close();
        } else {
            display.clear();
        }
    }
}

void CoverArt::init(uint32_t sample_frequency) {
}

void CoverArt::set_cover(const uint8_t * data, uint32_t len){
    this->cover_data = data;
    this->cover_len = len;
    this->render = true;
};

void CoverArt::set_artist(const char * artist){

}
void CoverArt::set_album(const char * album){

}
void CoverArt::set_title(const char * title){

}
