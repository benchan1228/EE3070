# ESP32 AI alarm update

GPIO 47 drives the existing active buzzer. GPIO 48 controls a red 3 W LED through a suitable driver. A suspicious AI result for the current device and capture event starts the buzzer and flashes the red LED for three seconds. The LED changes state every 250 ms. Both outputs turn off before sleep.

## Hardware

Do not power the 3 W LED from GPIO 48. Connect GPIO 48 only to a documented 3.3 V-compatible control input on a driver module, or a suitable external logic-level MOSFET driver. Use a power supply and current regulation rated for the actual LED module, and a common ground with the ESP32. If the module only has positive/negative power terminals, it needs an external switching driver. Its required supply voltage cannot be determined from '3 W' alone. Provide appropriate cooling. Configure RED_LED_ACTIVE_LEVEL as LOW only if the driver is active-low.

## Install the server addition first

Upload the files inside `server/` into `/workspace/autodl-backup/dashboard/`. They add an endpoint without replacing main.py, store.py, the worker, dashboard HTML or AI data. The endpoint expects the existing SQLite jobs table with device, event, photo_index and state columns.

In a RunPod terminal:

```bash
cd /workspace/autodl-backup/dashboard
source .web-runpod/bin/activate
python install_alarm_api.py
```

If your web environment is named `.web-venv`, activate that instead. The installer makes a dated main.py backup. Stop the existing dashboard process with Ctrl+C in its terminal, then start it again using the same web environment and any existing UPLOAD_TOKEN / REVIEW_TOKEN values:

```bash
python main.py
```

Leave the YOLO/Qwen worker and tunnel running. Check the new endpoint in another terminal:

```bash
curl --fail-with-body 'http://127.0.0.1:8000/api/device/event-status?device_id=esp32-camera-1&event_id=endpoint-test&expected=5'
```

Expected response: `not_found`. If UPLOAD_TOKEN is enabled, add your existing Bearer authorization header. This check tests the endpoint, not the alarm hardware. An existing event can be queried using its exact device/event IDs, but only the board's current event can trigger its alarm.

## Firmware

The firmware folder contains the updated sketch. Keep your real Wi-Fi credentials and working upload URL. In project_settings.h, check:

```cpp
static const int RED_LED_PIN = 48;
static const int RED_LED_ACTIVE_LEVEL = HIGH;
static const bool AI_ALARM_ENABLED = true;
static const unsigned long AI_RESULT_WAIT_MS = 60000;
static const unsigned long AI_POLL_INTERVAL_MS = 3000;
static const char DEVICE_UPLOAD_TOKEN[] = "";
```

Set DEVICE_UPLOAD_TOKEN only if your server uses UPLOAD_TOKEN. The existing upload URL must end in `/api/device/upload`; the status URL is derived automatically. No GPIO or camera-pin changes are needed elsewhere. Upload the sketch using your existing ESP32-S3, 16 MB flash, OPI PSRAM and sufficient application partition settings.

## Demonstration

1. Start the dashboard, worker and tunnel; verify the configured URL is current.
2. Power the ESP32 and allow the PIR to warm up and become inactive.
3. Trigger a fresh capture by moving in front of the PIR.
4. Serial Monitor should show uploads, `AI WAIT`, and event status messages.
5. A current-event `suspicious` result should flash the red LED and sound the buzzer for three seconds, then sleep. Normal, manual-review or missing results do not sound the alarm.

Use an appropriate staged demonstration; AI judgments can be wrong. An old alert in the gallery will not activate a new event's alarm.

## Timing and limitations

The extra AI wait starts after uploads finish or their retry window expires. It lasts up to 60 seconds plus any request already in flight. Poll requests use a 3-second connection timeout, 5-second TLS handshake timeout and 3-second response timeout. This increases awake time and battery consumption. Results received after the board sleeps remain available on the dashboard but cannot wake the board or sound its alarm. Missing uploads remain pending rather than being reported as normal. A suspicious result from any received photo of the current event is sufficient to alarm.

Both low-power and always-awake modes support result checks. Always-awake mode pauses new captures during the result wait/alarm. Existing new-trigger priority during uploads remains unchanged. No changes to AI prompts or thresholds are included.

The current source uses setInsecure() for uploads. This update follows that existing transport for status polling; certificate verification is not implemented or claimed by this package. Complete certificate verification before relying on authenticated alarm responses on an untrusted network.

The server update and firmware need to be deployed together. Validation: ESP32 Arduino core 3.3.12 compilation passed; eight API tests passed covering event/device isolation, pending/normal/review/suspicious states, authentication and input validation. Local compilation and API tests do not verify physical wiring, the live server schema, or real inference latency.
