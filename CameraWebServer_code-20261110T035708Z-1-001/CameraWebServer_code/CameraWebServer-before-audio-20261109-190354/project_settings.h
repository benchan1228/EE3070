#pragma once

// GPIO 48 controls a suitable driver/MOSFET, never a 3 W LED directly.
static const int RED_LED_PIN = 48;
static const int RED_LED_ACTIVE_LEVEL = HIGH;
static const bool AI_ALARM_ENABLED = true;
static const unsigned long AI_RESULT_WAIT_MS = 60000;
static const unsigned long AI_POLL_INTERVAL_MS = 3000;
// Match the server UPLOAD_TOKEN if enabled; otherwise leave empty.
static const char DEVICE_UPLOAD_TOKEN[] = "";

// PIR wake -> capture -> bounded upload -> bounded AI result/alarm -> deep sleep.
static const bool LOW_POWER_MODE = true;

// Sensor and output pins
static const int PIR_PIN = 1;
static const int LIGHT_SENSOR_PIN = 2;
static const int WHITE_LED_PIN = 21;
static const int BUZZER_PIN = 47;
static const int BUZZER_ACTIVE_LEVEL = HIGH;
static const int TRIGGER_BUTTON_PIN = -1;
static const unsigned long BUZZER_DURATION_MS = 3000;

static const int PIR_ACTIVE_LEVEL = HIGH;
static const int WHITE_LED_ACTIVE_LEVEL = HIGH;

// Calibrate using readings taken in bright and dark conditions.
static const bool DARK_WHEN_READING_LOW = false;
static const int DARK_THRESHOLD = 1500;

static const unsigned long PIR_WARMUP_MS = 30000;
static const unsigned long TRIGGER_COOLDOWN_MS = 10000;
static const unsigned long PHOTO_INTERVAL_MS = 500;
static const unsigned long LIGHT_SETTLE_MS = 700;

// Cloud upload timing
static const unsigned long UPLOAD_RETRY_MS = 2000;
static const unsigned long UPLOAD_GIVE_UP_MS = 60000;
static const unsigned long UPLOAD_CONNECT_TIMEOUT_MS = 5000;
static const unsigned long UPLOAD_RESPONSE_TIMEOUT_MS = 15000;

static const int BURST_COUNT = 5;

// Smaller JPEGs than quality 12; resolution is configured elsewhere.
static const int PHOTO_JPEG_QUALITY = 16;

// Public cloud upload endpoint.
// Keep this variable name because your existing upload code uses it.
static const char LOCAL_DASHBOARD_UPLOAD_URL[] =
    "https://dislodge-rewrap-screen.ngrok-free.dev/api/device/upload";

static const char DEVICE_ID[] = "esp32-camera-1";
