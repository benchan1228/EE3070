/*
  Standalone ESP32-S3 microphone + speaker test (Arduino-ESP32 3.x).
  INMP441: VDD=3.3V, GND=GND, SCK=39, WS=40, SD=41, L/R=GND.
  MAX98357: VIN=5V, GND=GND, BCLK=14, LRC=38, DIN=42,
            SD/MODE=3.3V for this test, GAIN unconnected.
  Speaker connects ONLY between amplifier speaker + and - terminals.
  All power supplies share GND. Never power the amplifier from a GPIO.
  Serial Monitor: 115200 baud. T=tone, R=record 3 seconds and play back.
  No camera, network, cloud, or SD card is used by this test.
*/
#include <Arduino.h>
#include <ESP_I2S.h>
#include <math.h>

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "Select ESP32S3 Dev Module for this sketch."
#endif

static constexpr int MIC_BCLK = 39;
static constexpr int MIC_WS = 40;
static constexpr int MIC_DATA = 41;
static constexpr int AMP_BCLK = 14;
static constexpr int AMP_WS = 38;
static constexpr int AMP_DATA = 42;
static constexpr uint32_t SAMPLE_RATE = 16000;
static constexpr size_t RECORD_SAMPLES = SAMPLE_RATE * 3;
static constexpr int MIC_GAIN = 4; // Increase if quiet; reduce if clipping.
static constexpr float PLAYBACK_VOLUME = 1.0f; // Range 0..1; reduce if distorted.

I2SClass microphone(I2S_NUM_0);
I2SClass speaker(I2S_NUM_1);
int16_t *recording = nullptr;

void help() {
  Serial.println("\nT: speaker tone | R: record 3 seconds then play back | H: help");
}

bool startSpeaker() {
  speaker.setPins(AMP_BCLK, AMP_WS, AMP_DATA, -1);
  if (!speaker.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT,
                     I2S_SLOT_MODE_STEREO)) {
    Serial.printf("Speaker I2S initialization failed: %d\n", speaker.lastError());
    return false;
  }
  return true;
}

bool writeSpeaker(const int16_t *samples, size_t frames) {
  const size_t bytes = frames * 2 * sizeof(int16_t); // L + R
  if (speaker.write(samples, bytes) != bytes) {
    Serial.println("Speaker I2S write failed.");
    return false;
  }
  return true;
}

void stopSpeaker() {
  // Queue silence and let DMA finish the final audio before stopping clocks.
  int16_t silence[256 * 2] = {};
  for (int i = 0; i < 8; ++i) {
    if (!writeSpeaker(silence, 256)) break;
  }
  delay(120);
  speaker.end();
}

void testTone() {
  if (!startSpeaker()) return;
  Serial.println("Playing a 440 Hz tone for one second...");
  int16_t stereo[256 * 2];
  for (size_t offset = 0; offset < SAMPLE_RATE; offset += 256) {
    const size_t count = min(size_t(256), size_t(SAMPLE_RATE) - offset);
    for (size_t i = 0; i < count; ++i) {
      // Fade the tone edges to reduce clicks.
      const size_t n = offset + i;
      const float fade = min(1.0f, min(float(n) / 320.0f,
                                     float(SAMPLE_RATE - 1 - n) / 320.0f));
      const int16_t value = int16_t(6000.0f * fade *
          sinf(2.0f * PI * 440.0f * float(n) / SAMPLE_RATE));
      stereo[2 * i] = value;
      stereo[2 * i + 1] = value;
    }
    if (!writeSpeaker(stereo, count)) break;
  }
  stopSpeaker();
  Serial.println("Tone test finished.");
}

bool readMic(int32_t *buffer, size_t samples) {
  const size_t bytes = samples * sizeof(int32_t);
  size_t received = 0;
  // A bounded read avoids hanging forever if I2S fails.
  const esp_err_t result = i2s_channel_read(microphone.rxChan(), buffer,
                                          bytes, &received, 1000);
  if (result != ESP_OK || received != bytes) {
    Serial.printf("Microphone read failed: error=%d, bytes=%u\n",
                  int(result), unsigned(received));
    return false;
  }
  return true;
}

void recordAndPlay() {
  if (!recording) {
    Serial.println("Recording buffer unavailable; press T for speaker-only test.");
    return;
  }
  microphone.setPins(MIC_BCLK, MIC_WS, -1, MIC_DATA);
  if (!microphone.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_32BIT,
                        I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT)) {
    Serial.printf("Microphone initialization failed: %d\n", microphone.lastError());
    return;
  }
  int32_t raw[256];
  // Discard startup audio while the microphone settles (~256 ms).
  for (int i = 0; i < 16; ++i) {
    if (!readMic(raw, 256)) { microphone.end(); return; }
  }
  Serial.println("RECORDING: speak near the microphone for 3 seconds now.");
  int peak = 0;
  size_t clipped = 0;
  for (size_t offset = 0; offset < RECORD_SAMPLES; offset += 256) {
    const size_t count = min(size_t(256), RECORD_SAMPLES - offset);
    if (!readMic(raw, count)) { microphone.end(); return; }
    for (size_t i = 0; i < count; ++i) {
      // INMP441 supplies signed 24-bit audio aligned in a 32-bit slot.
      int32_t sample = (raw[i] / 65536) * MIC_GAIN;
      if (sample > 32767 || sample < -32768) ++clipped;
      sample = constrain(sample, -32768, 32767);
      recording[offset + i] = int16_t(sample);
      peak = max(peak, int(abs(sample)));
    }
  }
  microphone.end();
  Serial.printf("Recorded. Peak=%d / 32768; clipped samples=%u\n", peak, unsigned(clipped));
  if (peak == 0) Serial.println("No mic signal: check 3.3V, GND, SD=41 and L/R=GND.");
  if (clipped) Serial.println("Reduce MIC_GAIN or speak farther from the microphone.");
  delay(300);
  if (!startSpeaker()) return;
  Serial.println("PLAYBACK: you should hear your recording.");
  int16_t stereo[256 * 2];
  for (size_t offset = 0; offset < RECORD_SAMPLES; offset += 256) {
    const size_t count = min(size_t(256), RECORD_SAMPLES - offset);
    for (size_t i = 0; i < count; ++i) {
      const int16_t value = int16_t(recording[offset + i] * PLAYBACK_VOLUME);
      stereo[2 * i] = value;
      stereo[2 * i + 1] = value;
    }
    if (!writeSpeaker(stereo, count)) break;
  }
  stopSpeaker();
  Serial.println("Playback finished.");
}

void setup() {
  // Keep existing project alarm/light outputs off during this standalone test.
  for (int pin : {21, 47, 48}) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
  Serial.begin(115200);
  const unsigned long started = millis();
  while (!Serial && millis() - started < 4000) delay(10);
  delay(300);
  recording = static_cast<int16_t *>(malloc(RECORD_SAMPLES * sizeof(int16_t)));
  Serial.println("\nESP32-S3 INMP441 + MAX98357 audio test");
  if (!recording) Serial.println("Not enough RAM for recording buffer.");
  testTone();
  help();
}

void loop() {
  if (Serial.available()) {
    const char command = Serial.read();
    if (command == 't' || command == 'T') { testTone(); help(); }
    else if (command == 'r' || command == 'R') { recordAndPlay(); help(); }
    else if (command == 'h' || command == 'H') help();
  }
  delay(10);
}
