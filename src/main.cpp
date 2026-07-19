#include "FS.h"
#include "SPIFFS.h"
#include "oled/SSD1306.h"
#include "heatshrink_decoder.h"
#include "buzzer/PassiveBuzzer.h"
#include "music/BadAppleMelody.h"
#include "audio/HaruhikageMelodyPlayer.h"
#include "audio/PdmAudioPlayer.h"
#include "audio/LedcMelodyPlayer.h"

#ifndef BUZZER_PIN
#define BUZZER_PIN 25
#endif

#ifndef BUZZER_SELF_TEST
#define BUZZER_SELF_TEST 0
#endif

#ifndef AUDIO_ONLY_MODE
#define AUDIO_ONLY_MODE 0
#endif

#ifndef AUDIO_OUTPUT_PDM
#define AUDIO_OUTPUT_PDM 0
#endif

#ifndef AUDIO_VIDEO_SYNC
#define AUDIO_VIDEO_SYNC 1
#endif

#ifndef BUZZER_VOLUME
#define BUZZER_VOLUME 70
#endif

#define AUDIO_TRACK_BAD_APPLE 0
#define AUDIO_TRACK_HARUHIKAGE 1

#ifndef AUDIO_TRACK
#define AUDIO_TRACK AUDIO_TRACK_BAD_APPLE
#endif

#if AUDIO_TRACK != AUDIO_TRACK_BAD_APPLE && AUDIO_TRACK != AUDIO_TRACK_HARUHIKAGE
#error AUDIO_TRACK must be 0 (Bad Apple) or 1 (Haruhikage)
#endif

// Hints:
// * Adjust the display pins below
// * After uploading to ESP32, also do "ESP32 Sketch Data Upload" from Arduino

SSD1306 display(0x3c, SDA, SCL); // For Heltec
// SSD1306 display (0x3c, 5, 4);

// LEDC channel 0 is reserved for the active-low passive buzzer.
PassiveBuzzer buzzer(BUZZER_PIN, 0, true);
BadAppleMelody badAppleMelody(&buzzer);
PdmAudioPlayer pdmAudio(BUZZER_PIN);
LedcMelodyPlayer ledcAudio(badAppleMelody);
HaruhikageMelodyPlayer haruhikageAudio(buzzer);
uint32_t videoFrameIndex = 0;

void runBuzzerSelfTest()
{
  struct TestNote
  {
    uint16_t frequencyHz;
    uint16_t durationMs;
  };

  constexpr TestNote notes[] = {
      {523, 180}, // C5
      {659, 180}, // E5
      {784, 260}, // G5
  };

  Serial.println("Buzzer self-test: start");
  for (const TestNote &note : notes)
  {
    if (!buzzer.playTone(note.frequencyHz, BUZZER_VOLUME))
    {
      Serial.println("Buzzer self-test: failed to play tone");
      buzzer.stop();
      return;
    }

    delay(note.durationMs);
    buzzer.stop();
    delay(80);
  }
  Serial.println("Buzzer self-test: done");
}

#if HEATSHRINK_DYNAMIC_ALLOC
#error HEATSHRINK_DYNAMIC_ALLOC must be false for static allocation test suite.
#endif

static heatshrink_decoder hsd;

// global storage for putPixels
int16_t curr_x = 0;
int16_t curr_y = 0;

// global storage for decodeRLE
int32_t runlength = -1;
int32_t c_to_dup = -1;

void listDir(fs::FS &fs, const char *dirname, uint8_t levels)
{
  Serial.printf("Listing directory: %s\n", dirname);

  File root = fs.open(dirname);
  if (!root)
  {
    Serial.println("Failed to open directory");
    return;
  }
  if (!root.isDirectory())
  {
    Serial.println("Not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file)
  {
    if (file.isDirectory())
    {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if (levels)
      {
        listDir(fs, file.name(), levels - 1);
      }
    }
    else
    {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}

uint32_t lastRefresh = 0;

void putPixels(uint8_t c, int32_t len)
{
  uint8_t b = 0;
  while (len--)
  {
    b = 128;
    for (int i = 0; i < 8; i++)
    {
      if (c & b)
      {
        display.setColor(WHITE);
      }
      else
      {
        display.setColor(BLACK);
      }
      b >>= 1;
      display.setPixel(curr_x, curr_y);
      curr_x++;
      if (curr_x >= 128)
      {
        curr_x = 0;
        curr_y++;
        if (curr_y >= 64)
        {
          curr_y = 0;
#if AUDIO_VIDEO_SYNC && AUDIO_TRACK == AUDIO_TRACK_BAD_APPLE
#if AUDIO_OUTPUT_PDM
          if (videoFrameIndex == BadAppleMelody::kAudioStartFrame)
          {
            Serial.println("PDM audio: playing Bad Apple");
            if (!pdmAudio.begin(BUZZER_VOLUME))
            {
              Serial.println("PDM audio initialization failed");
            }
          }
          ++videoFrameIndex;
#else
          badAppleMelody.updateFrame(videoFrameIndex++);
#endif
#else
          ++videoFrameIndex;
#endif
          display.display();
          // display.clear();
          //  30 fps target rate
          if (digitalRead(0))
            while ((millis() - lastRefresh) < 33)
              ;
          lastRefresh = millis();
        }
      }
    }
  }
}

void decodeRLE(uint8_t c)
{
  if (c_to_dup == -1)
  {
    if ((c == 0x55) || (c == 0xaa))
    {
      c_to_dup = c;
    }
    else
    {
      putPixels(c, 1);
    }
  }
  else
  {
    if (runlength == -1)
    {
      if (c == 0)
      {
        putPixels(c_to_dup & 0xff, 1);
        c_to_dup = -1;
      }
      else if ((c & 0x80) == 0)
      {
        if (c_to_dup == 0x55)
        {
          putPixels(0, c);
        }
        else
        {
          putPixels(255, c);
        }
        c_to_dup = -1;
      }
      else
      {
        runlength = c & 0x7f;
      }
    }
    else
    {
      runlength = runlength | (c << 7);
      if (c_to_dup == 0x55)
      {
        putPixels(0, runlength);
      }
      else
      {
        putPixels(255, runlength);
      }
      c_to_dup = -1;
      runlength = -1;
    }
  }
}

#define RLEBUFSIZE 4096
#define READBUFSIZE 2048
void readFile(fs::FS &fs, const char *path)
{
  static uint8_t rle_buf[RLEBUFSIZE];
  size_t rle_bufhead = 0;
  size_t rle_size = 0;

  size_t filelen = 0;
  size_t filesize;
  static uint8_t compbuf[READBUFSIZE];

  Serial.printf("Reading file: %s\n", path);
  File file = fs.open(path);
  if (!file || file.isDirectory())
  {
    Serial.println("Failed to open file for reading");
    display.drawStringMaxWidth(0, 10, 128, "File open error. Upload video.hs using ESP32 Sketch Upload.");
    display.display();
    return;
  }
  filelen = file.size();
  filesize = filelen;
  Serial.printf("File size: %d\n", filelen);

  // init display, putPixels and decodeRLE
  display.clear();
  display.display();
  curr_x = 0;
  curr_y = 0;
  runlength = -1;
  c_to_dup = -1;
  lastRefresh = millis();
  videoFrameIndex = 0;
#if AUDIO_VIDEO_SYNC && AUDIO_TRACK == AUDIO_TRACK_BAD_APPLE && !AUDIO_OUTPUT_PDM
  badAppleMelody.begin(BUZZER_VOLUME);
#endif

  // init decoder
  heatshrink_decoder_reset(&hsd);
  size_t count = 0;
  uint32_t sunk = 0;
  size_t toRead;
  size_t toSink = 0;
  uint32_t sinkHead = 0;

  // Go through file...
  while (filelen)
  {
    if (toSink == 0)
    {
      toRead = filelen;
      if (toRead > READBUFSIZE)
        toRead = READBUFSIZE;
      file.read(compbuf, toRead);
      filelen -= toRead;
      toSink = toRead;
      sinkHead = 0;
    }

    // uncompress buffer
    HSD_sink_res sres;
    sres = heatshrink_decoder_sink(&hsd, &compbuf[sinkHead], toSink, &count);
    // Serial.print("^^ sinked ");
    // Serial.println(count);
    toSink -= count;
    sinkHead = count;
    sunk += count;
    if (sunk == filesize)
    {
      heatshrink_decoder_finish(&hsd);
    }

    HSD_poll_res pres;
    do
    {
      rle_size = 0;
      pres = heatshrink_decoder_poll(&hsd, rle_buf, RLEBUFSIZE, &rle_size);
      // Serial.print("^^ polled ");
      // Serial.println(rle_size);
      if (pres < 0)
      {
        Serial.print("POLL ERR! ");
        Serial.println(pres);
#if AUDIO_VIDEO_SYNC && AUDIO_TRACK == AUDIO_TRACK_BAD_APPLE && AUDIO_OUTPUT_PDM
        pdmAudio.end();
#elif AUDIO_VIDEO_SYNC && AUDIO_TRACK == AUDIO_TRACK_BAD_APPLE
        badAppleMelody.stop();
#endif
        return;
      }

      rle_bufhead = 0;
      while (rle_size)
      {
        rle_size--;
        if (rle_bufhead >= RLEBUFSIZE)
        {
          Serial.println("RLE_SIZE ERR!");
#if AUDIO_VIDEO_SYNC && AUDIO_TRACK == AUDIO_TRACK_BAD_APPLE && AUDIO_OUTPUT_PDM
          pdmAudio.end();
#elif AUDIO_VIDEO_SYNC && AUDIO_TRACK == AUDIO_TRACK_BAD_APPLE
          badAppleMelody.stop();
#endif
          return;
        }
        decodeRLE(rle_buf[rle_bufhead++]);
      }
    } while (pres == HSDR_POLL_MORE);
  }
  file.close();
#if AUDIO_VIDEO_SYNC && AUDIO_TRACK == AUDIO_TRACK_BAD_APPLE && AUDIO_OUTPUT_PDM
  pdmAudio.end();
#elif AUDIO_VIDEO_SYNC && AUDIO_TRACK == AUDIO_TRACK_BAD_APPLE
  badAppleMelody.stop();
#endif
  Serial.println("Done.");
}

void setup()
{
  Serial.begin(115200);
#if AUDIO_OUTPUT_PDM && AUDIO_TRACK == AUDIO_TRACK_BAD_APPLE
  const bool buzzerReady = true;
#else
  const bool buzzerReady = buzzer.begin();
  if (!buzzerReady)
  {
    Serial.println("Buzzer initialization failed");
  }
#endif

#if BUZZER_SELF_TEST && !AUDIO_OUTPUT_PDM
  if (buzzerReady)
  {
    runBuzzerSelfTest();
  }
#endif

#if AUDIO_ONLY_MODE
  if (buzzerReady)
  {
#if AUDIO_TRACK == AUDIO_TRACK_HARUHIKAGE
    Serial.println("Audio-only mode: playing Haruhikage melody");
    if (!haruhikageAudio.begin(BUZZER_VOLUME))
    {
      Serial.println("Haruhikage melody task initialization failed");
    }
#elif AUDIO_OUTPUT_PDM
    Serial.println("Audio-only PDM mode: playing Bad Apple with 4-voice DDS");
    if (!pdmAudio.begin(BUZZER_VOLUME))
    {
      Serial.println("PDM audio initialization failed");
    }
#else
    Serial.println("Audio-only mode: playing Bad Apple melody");
    if (!ledcAudio.begin(BUZZER_VOLUME))
    {
      Serial.println("LEDC melody task initialization failed");
    }
#endif
  }
  return;
#endif

#if !AUDIO_VIDEO_SYNC || AUDIO_TRACK == AUDIO_TRACK_HARUHIKAGE
  if (buzzerReady)
  {
#if AUDIO_TRACK == AUDIO_TRACK_HARUHIKAGE
    Serial.println("Independent LEDC audio: playing Haruhikage without video sync");
    if (!haruhikageAudio.begin(BUZZER_VOLUME))
    {
      Serial.println("Haruhikage melody task initialization failed");
    }
#elif AUDIO_OUTPUT_PDM
    Serial.println("Independent PDM audio: playing without video sync");
    if (!pdmAudio.begin(BUZZER_VOLUME))
    {
      Serial.println("PDM audio initialization failed");
    }
#else
    Serial.println("Independent LEDC audio: playing without video sync");
    if (!ledcAudio.begin(BUZZER_VOLUME))
    {
      Serial.println("LEDC melody task initialization failed");
    }
#endif
  }
#endif

  // Reset for some displays
  pinMode(16, OUTPUT);
  digitalWrite(16, LOW);
  delay(50);
  digitalWrite(16, HIGH);
  display.init();
  display.flipScreenVertically();
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);
  display.setColor(WHITE);
  display.drawString(0, 0, "Mounting SPIFFS...     ");
  display.display();
  if (!SPIFFS.begin())
  {
    Serial.println("SPIFFS mount failed");
    display.drawStringMaxWidth(0, 10, 128, "SPIFFS mount failed. Upload video.hs using ESP32 Sketch Upload.");
    display.display();
    return;
  }

  pinMode(0, INPUT_PULLUP);
  Serial.print("totalBytes(): ");
  Serial.println(SPIFFS.totalBytes());
  Serial.print("usedBytes(): ");
  Serial.println(SPIFFS.usedBytes());
  listDir(SPIFFS, "/", 0);
  readFile(SPIFFS, "/video.hs");
}

void loop()
{
  delay(10);
}
