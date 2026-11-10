# Event audio and dashboard talk

## What changed

On PIR wake, the ESP32-S3 records four seconds from the INMP441 while taking its
five OV5640 photos. It uploads the photos first, then a mono 16 kHz, 16-bit WAV
file associated with the same device/event ID. Four seconds is about 128 KB.
The existing retry window remains bounded; failed uploads cannot keep it awake
forever. There is no SD-card storage in this implementation.

After uploads, the board checks the AI alarm and listens for dashboard speech.
With LOW_POWER_MODE=true it stays available for 90 seconds after uploads, then
returns to PIR sleep. With LOW_POWER_MODE=false the board stays online for testing.
An HTTP request cannot wake it from deep sleep.

Dashboard speech is one-way, sent in one-second chunks with a short network delay.
It is not a full-duplex phone call. Event recordings provide recorded sound from
the camera location. Audio does not go to YOLO or Qwen; their existing image
analysis and alarm decisions are unchanged.

## Wiring (same as the successful standalone test)

| Module pin | ESP32 / supply |
|---|---|
| INMP441 VDD | 3.3V |
| INMP441 GND and L/R | GND |
| INMP441 SCK | GPIO39 |
| INMP441 WS | GPIO40 |
| INMP441 SD | GPIO41 |
| MAX98357 VIN | Regulated 5V |
| MAX98357 GND | Common GND |
| MAX98357 BCLK | GPIO14 |
| MAX98357 LRC | GPIO38 |
| MAX98357 DIN | GPIO42 |
| MAX98357 SD/MODE | 3.3V for this wiring |
| MAX98357 GAIN | Unconnected |
| Speaker two wires | MAX98357 speaker + and - terminals |

The amplifier's SD pin is an enable/channel control; the microphone's SD pin is
data output. Never connect a speaker terminal to GND. The amplifier uses its 5V
supply, not a GPIO power output. All modules share ground. Keep the original PIR,
light sensor, white LED driver, buzzer and red LED driver connections unchanged.

With SD/MODE tied to 3.3V the amplifier remains powered during ESP32 sleep. I2S
clocks stop, but this is not full amplifier power-off. A future GPIO-controlled
SD pin or supply switch is required for lower total standby current.

## Settings you must check before uploading

1. Open CameraWebServer_Samplecode.ino from this folder.
2. Use **ESP32 by Espressif Systems 3.3.12**, ESP32S3 Dev Module, OPI PSRAM and
   16MB Flash. Select a partition with at least 3MB application space. Enable
   USB CDC On Boot if using the native USB port for Serial Monitor.
3. Copy wifi_settings.example.h to wifi_settings.h and set your actual 2.4GHz Wi-Fi
   name/password. The existing example uses the previous test network.
4. In project_settings.h keep LOCAL_DASHBOARD_UPLOAD_URL set to:
   `https://dislodge-rewrap-screen.ngrok-free.dev/api/device/upload`
5. In the dashboard administrator page, assign esp32-camera-1 to the intended
   ordinary user and save/generate its camera token. Paste that token into
   DEVICE_UPLOAD_TOKEN in project_settings.h. The camera was deliberately left
   unassigned, and the local token remains empty. Audio and talking require a
   registered camera with a valid token and a user owner.
6. Upload the sketch. This has not been flashed automatically.

Never commit a populated token or private wifi_settings.h to a public repository.
Camera token rotation also changes authentication for photo uploads and AI alarm
polling. Existing historical reports are not reassigned automatically.

Optional settings in project_settings.h:

```cpp
static const unsigned int EVENT_AUDIO_SECONDS = 4; // 3, 4 or 5
static const int PROJECT_MIC_GAIN = 4;
static const float PROJECT_SPEAKER_VOLUME = 1.0f;  // 100%; reduce if distorted
static const bool TALK_ENABLED = true;
static const unsigned long TALK_WINDOW_MS = 90000;
```

## Demonstration

1. Start the dashboard, AI worker and ngrok using your existing three terminals.
2. Sign in to the HTTPS dashboard as the ordinary user assigned to the camera.
3. Power/reset the board. In low-power mode allow its initial 30-second PIR
   warmup, then move in front of the PIR after it arms.
4. Serial Monitor at 115200 should show simultaneous recording/burst capture,
   photo HTTP 201 responses and `Event audio upload HTTP 201`.
5. Scroll to **Camera audio → Event recordings**. Play the recording; it shows
   its received time, camera ID, burst ID and duration.
6. When the camera shows **Ready for speaker audio**, click **Turn on microphone**
   and allow the browser microphone permission. Speak, then click **Stop talking**.
   Start with the speaker away from your ear.
7. Check **Speaker playback confirmed**. This is the device's playback acknowledgement;
   actual acoustic output still depends on speaker wiring and power.

Use a browser such as Chrome on HTTPS (localhost also works for development).
The microphone stops on tab hiding, logout, disconnect, or the two-minute browser
limit. In low-power mode the board's 90-second window can end sooner. Sleeping or
busy devices are not offered as ready. The existing APK is not rebuilt here;
its WebView microphone permission support has not been verified.

## Server and privacy

- Added files: audio_api.py, audio.js and audio-worklet.js. main.py, user_auth.py
  and index.html load the new routes and controls.
- Event audio is private to the recorded event's owner and stored in ai-data/audio.
  Seven-day retention is enforced during audio API activity; at most 2000 event
  recordings are accepted. Stop the server and files may remain on disk until
  the next cleanup request. Photos keep their existing retention rules.
- Speech chunks are private to a logged-in owner and the registered camera.
  They expire after eight seconds, are delivered at most once, and are cancelled
  by Stop, logout, camera reassignment, session expiry or the camera's sleep notice.
  At most three fresh chunks are queued per session. Unsent speech is not replayed
  on a later wake. The server only briefly holds it in its SQLite queue; it is
  not a permanent voice-message archive.
- HTTPS now uses the core's built-in CA bundle and hostname verification. It
  requires a valid clock: the sketch tries NTP for up to ten seconds. It does not
  fall back to setInsecure(). If uploads fail on a school network, verify that
  Wi-Fi has internet access and allows NTP, or try a phone hotspot.

## Validation and limitations

The complete firmware compiled with core 3.3.12: 1,212,159 bytes of application
space, 59,180 bytes of static RAM. Hardware recording during camera DMA and live
speaker playback still need your board test. Backend tests cover private audio,
WAV validation, duplicate uploads, queue limits, expiry, logout, reassignment and
sleep cancellation. Browser tests use a simulated microphone/device and exercise
capture, upload, playback, stop, background handling and mobile layout.

Backups were taken before editing. The new audio consumes additional power during
recording and the listening window. Upload failures and failed AI analysis remain
separate conditions; audio itself does not prove suspicious activity.
