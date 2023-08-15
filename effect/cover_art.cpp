#include "effect.hpp"
#include "3rd-party/JPEGDEC/JPEGDEC.h"
#include "btstack_util.h"

// cover art thumbnails are 200x200, main options:
#if 0
// - we draw it 1/4 => 50 x 50 at offset -9/-9
#define OFFSET_X (-9)
#define OFFSET_Y (-9)
#define SCALING JPEG_SCALE_QUARTER
#else
// - we draw it 1/8 => 25 x 25 at offset 3/3
#define OFFSET_X (3)
#define OFFSET_Y (3)
#define SCALING JPEG_SCALE_EIGHTH
#endif

static JPEGDEC decoder;

static int JPEGDraw(JPEGDRAW *pDraw) {
    Display * display = (Display*) pDraw->pUser;
    int16_t i;
    int16_t j;
    uint16_t * pixel = pDraw->pPixels;
    for (i=0;i<pDraw->iHeight;i++) {
        int16_t y = pDraw->y + i + OFFSET_Y;
        for (j=0;j<pDraw->iWidth; j++) {
            uint16_t rgb565 = *pixel++;
            int16_t x = pDraw->x + j + OFFSET_X;
            if ((y < 0) || (y >= Display::HEIGHT)) {
                continue;
            }
            if ((x < 0) || (x >= Display::WIDTH)) {
                continue;
            }
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
        display.clear();
        if (this->cover_data != NULL){
            this->render = false;
            decoder.openRAM((uint8_t *) this->cover_data, this->cover_len, JPEGDraw);
            decoder.setUserPointer(&display);
            decoder.decode(0, 0, SCALING);
            decoder.close();
        } else {
            uint16_t scroll_offset = this->cover_art_scroller_offset / 2;
            display.draw_string(Display::WIDTH - scroll_offset, 12, this->cover_art_info);
            this->cover_art_scroller_offset++;
            if (scroll_offset >= ((MAX_TEXT_LEN * 8 * 2) + Display::WIDTH)){
                this->render = false;
            }
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

void CoverArt::set_info(const char *info) {
    btstack_strcpy(this->cover_art_info, sizeof(this->cover_art_info), info);
    printf("Scroller: %s\n", this->cover_art_info);
    this->cover_data = NULL;
    this->cover_art_scroller_offset = 0;
    this->render = true;
}
