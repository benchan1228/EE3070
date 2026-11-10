# Microphone and speaker test

Open `AudioHardwareTest.ino` in Arduino IDE. This standalone sketch replaces the
camera program on the board temporarily; upload your camera sketch again when
finished. It does not initialize the camera, connect to Wi-Fi, or deep sleep.

## Wiring (power off before changing wires)

| Module pin | Connection |
|---|---|
| INMP441 VDD | 3.3V |
| INMP441 GND | Common GND |
| INMP441 SCK | GPIO39 |
| INMP441 WS | GPIO40 |
| INMP441 SD | GPIO41 |
| INMP441 L/R | GND (left channel) |
| MAX98357 VIN | Regulated 5V supply |
| MAX98357 GND | Common GND with ESP32 and microphone |
| MAX98357 BCLK | GPIO14 |
| MAX98357 LRC | GPIO38 |
| MAX98357 DIN | GPIO42 |
| MAX98357 SD / MODE | 3.3V for this test |
| MAX98357 GAIN | Leave disconnected |
| Speaker two wires | Amplifier speaker + and - screw terminals |

The two modules have independent clock wires. The microphone SD pin carries
audio data; the amplifier SD pin controls shutdown/channel selection.
Do not connect either speaker terminal to ground. Do not feed 5V into the
microphone. The amplifier needs a power supply, not power from a GPIO.
The 3.3V SD connection is a control signal and does not power the amplifier.
Avoid using thin split Dupont leads as the shared supply for the amplifier,
high-power LEDs and ESP32. A splitter does not increase available supply current.

## Arduino settings

- Board: ESP32S3 Dev Module; ESP32 by Espressif Systems 3.x (checked with 3.3.12).
- With the board's native USB port: USB CDC On Boot = Enabled.
- Select the current COM port. Use upload speed 115200 if uploads are unreliable.
- Keep the board's existing 16MB Flash / OPI PSRAM settings. PSRAM is not required
  for the 96KB recording buffer.
- Serial Monitor: 115200 baud; any line-ending choice works.

## Test

1. Upload the sketch, open Serial Monitor and press RESET if needed.
2. A one-second tone should play. Keep it away from your ear. Send `T` to repeat it.
3. Send `R`. After `RECORDING` appears, speak near the microphone for three seconds.
4. The board prints the peak signal and plays the recorded sound through the speaker.
5. Repeat `R` as needed. Recording and playback are separate to avoid live feedback.

If tone works but voice does not, check microphone wiring and L/R=GND. A peak of
zero indicates no microphone signal. Small nonzero peaks may just be noise;
verify they rise when you speak. For quiet recordings increase MIC_GAIN gradually
from 4 to 8. If clipped samples are reported, reduce MIC_GAIN. Playback volume
starts at 1.0 (100%) and can be adjusted with PLAYBACK_VOLUME (0 to 1). Reduce it if
the speaker distorts. The tone amplitude is 6000 (previously 3000).

If there is no tone, check the amplifier 5V, common GND, SD=3.3V, speaker terminals
and the three I2S wires. If the board resets during sound, investigate power and
wiring before increasing volume. This sketch holds project GPIO21, 47 and 48 LOW
to keep the existing active-high light/alarm modules off.

Compilation can verify software compatibility; actual microphone quality,
speaker operation and power stability must be checked on your hardware.
