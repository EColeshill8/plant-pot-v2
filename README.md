# Plant Pot V2

Plant monitor firmware for an ESP32-C3 Super Mini. It reads a soil moisture
sensor, a DHT11, and a photoresistor, and shows the readings on a 128x64 I2C
OLED. The screen wakes when the light level crosses its threshold or GPIO3
goes HIGH, then turns off after 15 seconds.

## What you need

- ESP32-C3 Super Mini
- 128x64 SSD1306 I2C OLED (address `0x3C`)
- Capacitive soil moisture sensor
- DHT11 temperature/humidity sensor
- Photoresistor and a resistor for a voltage divider
- Optional touch input on GPIO3
- PlatformIO (the project is set up for VS Code)

## Wiring

Connect all grounds together. The sensors and OLED should use 3.3 V.

| Part | Signal | ESP32-C3 GPIO |
| --- | --- | --- |
| Soil moisture sensor | Analog output | 0 |
| Photoresistor divider | Analog output | 1 |
| DHT11 | Data | 4 |
| Touch input (optional) | Digital signal (HIGH to wake) | 3 |
| OLED | SDA | 7 |
| OLED | SCL | 6 |

Keep analog signals within the ESP32-C3's input range; use a divider if needed.
The soil moisture calibration and light threshold in `src/main.cpp` may need
adjusting for your sensors.
GPIO3 uses an internal pull-down, so it stays LOW when idle. The ESP32-C3 does
not have a dedicated capacitive-touch peripheral; a bare wire may not reliably
signal a touch unless the input goes HIGH.

## Build and upload

1. Open this folder in VS Code with the PlatformIO extension installed.
2. Connect the ESP32-C3 over USB.
3. Build the `esp32-c3-super-mini` environment, then upload it to the board.
   PlatformIO will install the libraries listed in `platformio.ini`.
4. Open the serial monitor at 115200 baud if you want to check startup messages.

The display starts off. A GPIO3 rising edge or light-level crossing turns it on
for about 15 seconds. Holding GPIO3 HIGH will not keep the display on. Sensor
readings and diagnostic output are available once per second over serial at
115200 baud, including the photo, touch, soil, DHT, and display timer values.
