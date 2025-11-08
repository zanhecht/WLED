#include "wled.h"

#ifdef WLED_ENABLE_GIF

#include "GifDecoder.h"


/*
 * Functions to render images from filesystem to segments, used by the "Image" effect
 */

File file;
char lastFilename[34] = "/";
GifDecoder<320,320,12,true> decoder;
bool gifDecodeFailed = false;
unsigned long lastFrameDisplayTime = 0, currentFrameDelay = 0;

bool fileSeekCallback(unsigned long position) {
  return file.seek(position);
}

unsigned long filePositionCallback(void) {
  return file.position();
}

int fileReadCallback(void) {
  return file.read();
}

int fileReadBlockCallback(void * buffer, int numberOfBytes) {
  return file.read((uint8_t*)buffer, numberOfBytes);
}

int fileSizeCallback(void) {
  return file.size();
}

bool openGif(const char *filename) {
  file = WLED_FS.open(filename, "r");

  if (!file) return false;
  return true;
}

Segment* activeSeg;
uint16_t gifWidth, gifHeight;
int lastCoordinate; // last coordinate (x+y) that was set, used to reduce redundant pixel writes
uint16_t perPixelX, perPixelY; // scaling factors when upscaling

void screenClearCallback(void) {
  activeSeg->fill(0);
}

void updateScreenCallback(void) {}

// note: GifDecoder drawing is done top right to bottom left, line by line

// callback to draw a pixel at (x,y) without scaling: used if GIF size matches segment size (faster)
void drawPixelCallbackNoScale(int16_t x, int16_t y, uint8_t red, uint8_t green, uint8_t blue) {
  activeSeg->setPixelColor(y * activeSeg->width() + x, red, green, blue);
}

void drawPixelCallback1D(int16_t x, int16_t y, uint8_t red, uint8_t green, uint8_t blue) {
  // 1D strip: load pixel-by-pixel left to right, top to bottom (0/0 = top-left in gifs)
  int totalImgPix = (int)gifWidth * gifHeight;
  int start =  ((int)y * gifWidth + (int)x) * activeSeg->vWidth() / totalImgPix; // simple nearest-neighbor scaling
  if (start == lastCoordinate) return; // skip setting same coordinate again
  lastCoordinate = start;
  for (int i = 0; i < perPixelX; i++) {
    activeSeg->setPixelColor(start + i, red, green, blue);
  }
}

void drawPixelCallback2D(int16_t x, int16_t y, uint8_t red, uint8_t green, uint8_t blue) {
  // simple nearest-neighbor scaling
  int outY = (int)y * activeSeg->vHeight() / gifHeight;
  int outX = (int)x * activeSeg->vWidth()  / gifWidth;
  if (outX + outY == lastCoordinate) return; // skip setting same coordinate again
  lastCoordinate = outX + outY; // since input is a "scanline" this is sufficient to identify a "unique" coordinate
  // set multiple pixels if upscaling
  for (int i = 0; i < perPixelX; i++) {
    for (int j = 0; j < perPixelY; j++) {
      activeSeg->setPixelColorXY(outX + i, outY + j, red, green, blue);
    }
  }
}

#define IMAGE_ERROR_NONE 0
#define IMAGE_ERROR_NO_NAME 1
#define IMAGE_ERROR_SEG_LIMIT 2
#define IMAGE_ERROR_UNSUPPORTED_FORMAT 3
#define IMAGE_ERROR_FILE_MISSING 4
#define IMAGE_ERROR_DECODER_ALLOC 5
#define IMAGE_ERROR_GIF_DECODE 6
#define IMAGE_ERROR_FRAME_DECODE 7
#define IMAGE_ERROR_WAITING 254
#define IMAGE_ERROR_PREV 255

// renders an image (.gif only; .bmp and .fseq to be added soon) from FS to a segment
byte renderImageToSegment(Segment &seg) {
  if (!seg.name) return IMAGE_ERROR_NO_NAME;
  // disable during effect transition, causes flickering, multiple allocations and depending on image, part of old FX remaining
  //if (seg.mode != seg.currentMode()) return IMAGE_ERROR_WAITING;
  if (activeSeg && activeSeg != &seg) return IMAGE_ERROR_SEG_LIMIT; // only one segment at a time
  activeSeg = &seg;

  if (strncmp(lastFilename +1, seg.name, 32) != 0) { // segment name changed, load new image
    strncpy(lastFilename +1, seg.name, 32);
    gifDecodeFailed = false;
    if (strcmp(lastFilename + strlen(lastFilename) - 4, ".gif") != 0) {
      gifDecodeFailed = true;
      return IMAGE_ERROR_UNSUPPORTED_FORMAT;
    }
    if (file) file.close();
    openGif(lastFilename);
    if (!file) { gifDecodeFailed = true; return IMAGE_ERROR_FILE_MISSING; }
    lastCoordinate = -1;
    decoder.setScreenClearCallback(screenClearCallback);
    decoder.setUpdateScreenCallback(updateScreenCallback);
    decoder.setDrawPixelCallback(drawPixelCallbackNoScale);
    decoder.setFileSeekCallback(fileSeekCallback);
    decoder.setFilePositionCallback(filePositionCallback);
    decoder.setFileReadCallback(fileReadCallback);
    decoder.setFileReadBlockCallback(fileReadBlockCallback);
    decoder.setFileSizeCallback(fileSizeCallback);
    decoder.alloc();
    DEBUG_PRINTLN(F("Starting decoding"));
    if(decoder.startDecoding() < 0) { gifDecodeFailed = true; return IMAGE_ERROR_GIF_DECODE; }
    DEBUG_PRINTLN(F("Decoding started"));
    // after startDecoding, we can get GIF size, update static variables and callbacks if needed
    decoder.getSize(&gifWidth, &gifHeight);
    if (activeSeg->height() == 1) {
      int totalImgPix = (int)gifWidth * gifHeight;
      if (totalImgPix - activeSeg->vWidth() == 1) totalImgPix--; // handle off-by-one: skip last pixel instead of first (gifs constructed from 1D input padds last pixel if length is odd)
      perPixelX   = (activeSeg->vWidth() + totalImgPix-1) / totalImgPix;
      if (totalImgPix != activeSeg->vWidth()) {
        decoder.setDrawPixelCallback(drawPixelCallback1D); // use 1D callback with scaling
      }
    } else {
      perPixelX   = (activeSeg->vWidth()  + gifWidth -1) / gifWidth;
      perPixelY   = (activeSeg->vHeight() + gifHeight-1) / gifHeight;
      if (activeSeg->vWidth() != gifWidth || activeSeg->vHeight() != gifHeight) {
        decoder.setDrawPixelCallback(drawPixelCallback2D); // use 2D callback with scaling
      }
    }
  }

  if (gifDecodeFailed) return IMAGE_ERROR_PREV;
  if (!file) { gifDecodeFailed = true; return IMAGE_ERROR_FILE_MISSING; }
  //if (!decoder) { gifDecodeFailed = true; return IMAGE_ERROR_DECODER_ALLOC; }

  // speed 0 = half speed, 128 = normal, 255 = full FX FPS
  // TODO: 0 = 4x slow, 64 = 2x slow, 128 = normal, 192 = 2x fast, 255 = 4x fast
  uint32_t wait = currentFrameDelay * 2 - seg.speed * currentFrameDelay / 128;

  // TODO consider handling this on FX level with a different frametime, but that would cause slow gifs to speed up during transitions
  if (millis() - lastFrameDisplayTime < wait) return IMAGE_ERROR_WAITING;

  int result = decoder.decodeFrame(false);
  if (result < 0) { gifDecodeFailed = true; return IMAGE_ERROR_FRAME_DECODE; }

  currentFrameDelay = decoder.getFrameDelay_ms();
  unsigned long tooSlowBy = (millis() - lastFrameDisplayTime) - wait; // if last frame was longer than intended, compensate
  currentFrameDelay = tooSlowBy > currentFrameDelay ? 0 : currentFrameDelay - tooSlowBy;
  lastFrameDisplayTime = millis();

  return IMAGE_ERROR_NONE;
}

void endImagePlayback(Segment *seg) {
  DEBUG_PRINTLN(F("Image playback end called"));
  if (!activeSeg || activeSeg != seg) return;
  if (file) file.close();
  decoder.dealloc();
  gifDecodeFailed = false;
  activeSeg = nullptr;
  lastFilename[1] = '\0';
  DEBUG_PRINTLN(F("Image playback ended"));
}

#endif