#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "project_settings.h"
#include "project_sensors.h"
#include "project_sleep.h"

namespace {
SemaphoreHandle_t cameraLock = nullptr;
SemaphoreHandle_t buzzerLock = nullptr;
esp_timer_handle_t buzzerTimer = nullptr;
int64_t buzzerDeadline = 0;
void stopBuzzerTimer(void *) {
  if (buzzerLock && xSemaphoreTake(buzzerLock, portMAX_DELAY)) {
    if (esp_timer_get_time() >= buzzerDeadline)
      digitalWrite(BUZZER_PIN, !BUZZER_ACTIVE_LEVEL);
    xSemaphoreGive(buzzerLock);
  }
}
struct Photo { uint8_t *data = nullptr; size_t size = 0; };
Photo pending[BURST_COUNT];
String eventId;
bool armed = false, triggeredBefore = false;
bool buttonWasDown = false;
bool warmupReported = false;
int previousPirLevel = -1;
unsigned long bootTime, lastTrigger = 0, lastRetry = 0;
unsigned long uploadStarted = 0;
bool nightEvent = false;
const size_t MAX_PHOTO_BYTES = 1024 * 1024;

void light(bool on) {
  if (WHITE_LED_PIN >= 0)
    digitalWrite(WHITE_LED_PIN, on ? WHITE_LED_ACTIVE_LEVEL : !WHITE_LED_ACTIVE_LEVEL);
}
bool hasPending() {
  for (auto &photo : pending) if (photo.data) return true;
  return false;
}
void discardPending(const char *reason) {
  int dropped = 0;
  for (auto &photo : pending) {
    if (photo.data) { free(photo.data); photo = Photo(); ++dropped; }
  }
  if (dropped) Serial.printf("Discarded %d unsent photos: %s. Ready for next trigger.\n", dropped, reason);
}
void uploadPending() {
  lastRetry = millis();
  if (millis() - uploadStarted >= UPLOAD_GIVE_UP_MS) {
    discardPending("upload retry window expired");
    return;
  }
  if (!LOCAL_DASHBOARD_UPLOAD_URL[0] || WiFi.status() != WL_CONNECTED) {
    Serial.println("Photos waiting in RAM: check dashboard URL/Wi-Fi. Will give up after retry window.");
    return;
  }
  for (int i = 0; i < BURST_COUNT; ++i) {
    if (!pending[i].data) continue;
    if (millis() - uploadStarted >= UPLOAD_GIVE_UP_MS) {
      discardPending("upload retry window expired");
      break;
    }

    WiFiClientSecure client;
    client.setInsecure(); // Handles Cloudflare TLS handshake without local CA certificates

    HTTPClient http;
    http.setConnectTimeout(UPLOAD_CONNECT_TIMEOUT_MS);
    http.setTimeout(UPLOAD_RESPONSE_TIMEOUT_MS);
    if (!http.begin(client, LOCAL_DASHBOARD_UPLOAD_URL)) break;
    http.addHeader("Content-Type", "image/jpeg");
    http.addHeader("X-Device-ID", DEVICE_ID);
    http.addHeader("X-Event-ID", eventId);
    http.addHeader("X-Photo-Index", String(i + 1));
    http.addHeader("X-Night-Capture", nightEvent ? "true" : "false");
    
    int code = http.POST(pending[i].data, pending[i].size);
    Serial.printf("Photo %d/%d: dashboard HTTP %d\n", i + 1, BURST_COUNT, code);
    
    if (code != 200 && code != 201) {
      Serial.print("Response: ");
      Serial.println(http.getString());
    }

    http.end();
    if (code == 200 || code == 201) {
      free(pending[i].data);
      pending[i] = Photo();
      lastRetry = millis() - UPLOAD_RETRY_MS;
    } else {
      Serial.println("Upload not confirmed; retained for retry. Check dashboard URL/server/firewall.");
      lastRetry = millis();
    }
    // Recheck sensors between requests instead of blocking on all five uploads.
    break;
  }
}
void captureBurst() {
  if (!cameraLock || !xSemaphoreTake(cameraLock, pdMS_TO_TICKS(5000))) {
    Serial.println("Camera busy. Stop live streaming and trigger again.");
    return;
  }
  char id[32];
  snprintf(id, sizeof(id), "%08lx-%08lx-%08lx", (unsigned long)esp_random(),
           (unsigned long)esp_random(), millis());
  eventId = id;
  sensor_t *sensor = esp_camera_sensor_get();
  // Restore photo settings even if the live-view page changed the resolution.
  if (!psramFound() || !sensor || sensor->set_framesize(sensor, FRAMESIZE_FHD) != 0
      || sensor->set_quality(sensor, PHOTO_JPEG_QUALITY) != 0) {
    Serial.println("Cannot start Full HD burst: check PSRAM and OV5640 settings.");
    xSemaphoreGive(cameraLock);
    return;
  }
  int reading = LIGHT_SENSOR_PIN >= 0 ? analogRead(LIGHT_SENSOR_PIN) : -1;
  nightEvent = reading >= 0 && (DARK_WHEN_READING_LOW ? reading < DARK_THRESHOLD : reading > DARK_THRESHOLD);
  Serial.printf("Burst %s. Light reading: %d. Dark: %s\n", id, reading, nightEvent ? "yes" : "no");
  light(nightEvent);
  delay(LIGHT_SETTLE_MS);
  // Drain old buffered frames after illumination/exposure changes.
  for (int i = 0; i < 2; ++i) {
    camera_fb_t *old = esp_camera_fb_get();
    if (old) esp_camera_fb_return(old);
  }
  int captured = 0;
  for (int i = 0; i < BURST_COUNT; ++i) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb && fb->format == PIXFORMAT_JPEG && fb->len <= MAX_PHOTO_BYTES) {
      pending[i].data = (uint8_t *)heap_caps_malloc(fb->len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (pending[i].data) {
        memcpy(pending[i].data, fb->buf, fb->len);
        pending[i].size = fb->len;
        ++captured;
      }
    }
    if (fb) esp_camera_fb_return(fb);
    if (!pending[i].data) {
      Serial.printf("Photo %d failed (camera, non-JPEG, size or PSRAM).\n", i + 1);
      break;
    }
    if (i + 1 < BURST_COUNT) delay(PHOTO_INTERVAL_MS);
  }
  light(false);
  xSemaphoreGive(cameraLock);
  Serial.printf("Captured %d/%d photos; sending to local dashboard.\n", captured, BURST_COUNT);
  uploadStarted = millis();
  lastRetry = millis() - UPLOAD_RETRY_MS; // First attempt on next loop iteration.
}
}

camera_fb_t *projectCameraGet() {
  if (!cameraLock || !xSemaphoreTake(cameraLock, pdMS_TO_TICKS(10000))) return nullptr;
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) xSemaphoreGive(cameraLock);
  return fb;
}
void projectCameraReturn(camera_fb_t *fb) {
  if (!fb) return;
  esp_camera_fb_return(fb);
  xSemaphoreGive(cameraLock);
}
void setupProjectSensors() {
  cameraLock = xSemaphoreCreateMutex();
  if (BUZZER_PIN >= 0) {
    digitalWrite(BUZZER_PIN, !BUZZER_ACTIVE_LEVEL);
    pinMode(BUZZER_PIN, OUTPUT);
    buzzerLock = xSemaphoreCreateMutex();
    esp_timer_create_args_t timerConfig = {};
    timerConfig.callback = stopBuzzerTimer;
    timerConfig.name = "buzzer-off";
    if (esp_timer_create(&timerConfig, &buzzerTimer) != ESP_OK)
      Serial.println("Buzzer timer unavailable; alarm disabled.");
  }
  bootTime = millis();
  if (PIR_PIN >= 0) pinMode(PIR_PIN, INPUT);
  if (LIGHT_SENSOR_PIN >= 0) { pinMode(LIGHT_SENSOR_PIN, INPUT); analogReadResolution(12); }
  if (WHITE_LED_PIN >= 0) { digitalWrite(WHITE_LED_PIN, !WHITE_LED_ACTIVE_LEVEL); pinMode(WHITE_LED_PIN, OUTPUT); }
  if (TRIGGER_BUTTON_PIN >= 0) pinMode(TRIGGER_BUTTON_PIN, INPUT_PULLUP);
  if (LOW_POWER_MODE) {
    Serial.println("PIR wake capture ready. Dashboard manual controls disabled in low-power mode.");
    return;
  }
  Serial.println("Local sensor capture ready. Serial B = test burst; L = light reading.");
  Serial.printf("AUTOMATIC mode: PIR GPIO %d; warmup %lu seconds. No capture button is required.\n",
                PIR_PIN, PIR_WARMUP_MS / 1000);
  Serial.println("PIR arms after warmup and a LOW/inactive reading. Sensor GPIO -1 means disabled.");
}
bool setProjectBuzzer(bool on) {
  if (BUZZER_PIN < 0 || !buzzerLock || !buzzerTimer) return false;
  if (!xSemaphoreTake(buzzerLock, pdMS_TO_TICKS(100))) return false;
  esp_timer_stop(buzzerTimer);
  buzzerDeadline = 0;
  digitalWrite(BUZZER_PIN, !BUZZER_ACTIVE_LEVEL);
  bool success = true;
  if (on) {
    // An independent timer switches it off even while camera/upload code is busy.
    unsigned long duration = constrain(BUZZER_DURATION_MS, 100UL, 10000UL);
    buzzerDeadline = esp_timer_get_time() + (uint64_t)duration * 1000;
    success = esp_timer_start_once(buzzerTimer, (uint64_t)duration * 1000) == ESP_OK;
    if (success) digitalWrite(BUZZER_PIN, BUZZER_ACTIVE_LEVEL);
  }
  xSemaphoreGive(buzzerLock);
  return success;
}
void captureProjectWakeBurst() {
  captureBurst();
}
void pollProjectSensors() {
  if (LOW_POWER_MODE) {
    if (hasPending() && millis() - uploadStarted >= UPLOAD_GIVE_UP_MS)
      discardPending("upload retry window expired; sleeping");
    if (hasPending() && millis() - lastRetry >= UPLOAD_RETRY_MS) uploadPending();
    if (!hasPending()) sleepProjectUntilMotion();
    return;
  }
  bool manual = false;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'b' || c == 'B') manual = true;
    if (c == 'l' || c == 'L') Serial.printf("Light AO: %d\n", LIGHT_SENSOR_PIN >= 0 ? analogRead(LIGHT_SENSOR_PIN) : -1);
  }
  bool down = TRIGGER_BUTTON_PIN >= 0 && digitalRead(TRIGGER_BUTTON_PIN) == LOW;
  if (down && !buttonWasDown) {
    delay(30);
    if (digitalRead(TRIGGER_BUTTON_PIN) == LOW) manual = true;
  }
  buttonWasDown = down;
  bool motion = PIR_PIN >= 0 && digitalRead(PIR_PIN) == PIR_ACTIVE_LEVEL;
  if (PIR_PIN >= 0 && int(motion) != previousPirLevel) {
    Serial.println(motion ? "PIR: ACTIVE (motion)" : "PIR: INACTIVE (clear)");
    previousPirLevel = int(motion);
  }
  bool warmed = millis() - bootTime >= PIR_WARMUP_MS;
  if (warmed && !warmupReported) {
    warmupReported = true;
    Serial.println("PIR warmup complete. Waiting for inactive signal before arming.");
  }
  bool cooled = !triggeredBefore || millis() - lastTrigger >= TRIGGER_COOLDOWN_MS;
  if (PIR_PIN >= 0 && warmed && !motion && cooled && !armed) {
    armed = true;
    Serial.println("AUTO READY: move in front of the PIR to capture 5 photos.");
  }
  if (cooled && (manual || (warmed && armed && motion))) {
    Serial.println(manual ? "MANUAL TEST: starting burst." : "AUTO TRIGGER: PIR motion -> light check -> 5 photos -> dashboard.");
    discardPending("new trigger takes priority");
    armed = false;
    triggeredBefore = true;
    captureBurst();
    lastTrigger = millis();
    return;
  }
  if (hasPending() && millis() - uploadStarted >= UPLOAD_GIVE_UP_MS)
    discardPending("upload retry window expired");
  if (hasPending() && millis() - lastRetry >= UPLOAD_RETRY_MS) uploadPending();
}