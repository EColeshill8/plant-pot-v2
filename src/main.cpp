#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

// Pin mapping
const uint8_t SOIL_MOISTURE_PIN = 0;          // GPIO0 / ADC1_CH0
const uint8_t DHT_PIN = 4;                    // GPIO4
const uint8_t PHOTO_RESISTOR_PIN = 1;         // GPIO1 / ADC1_CH1
const uint8_t TOUCH_PIN = 3;                  // GPIO3, active-high digital touch input
const uint8_t OLED_SDA_PIN = 7;               // GPIO7, connected to the OLED SDA wire
const uint8_t OLED_SCL_PIN = 6;               // GPIO6, connected to the OLED SCL wire

// Thresholds and timings
const uint16_t PHOTO_RISING_THRESHOLD = 3500;     // Below this = dark, above this = bright enough to wake display
const uint32_t SCREEN_ON_TIME_MS = 15000;          // Keep OLED visible for 15 seconds after trigger
const uint32_t SENSOR_READ_INTERVAL_MS = 1000;    // DHT11 readings and serial diagnostics every second

// Soil moisture calibration.
// The raw ADC value from a capacitive probe usually sits between a dry and wet reading.
// If your sensor reacts in the opposite direction, swap the min/max or invert the final percent.
const float SOIL_MOISTURE_MIN_RAW = 1500.0f;
const float SOIL_MOISTURE_MAX_RAW = 3500.0f;

// EMA filter settings for smoothing ADC and DHT readings.
// Alpha is between 0 and 1. Small alpha = smoother but slower to respond.
// For sensor readings, 0.12 to 0.25 is usually a good starting point.
const float EMA_ALPHA = 0.18f;

// OLED configuration
const uint8_t OLED_WIDTH = 128;
const uint8_t OLED_HEIGHT = 64;
const uint8_t OLED_ADDRESS = 0x3C;
const int OLED_RESET_PIN = -1; // -1 means sharing Arduino reset pin

// Sensor objects
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET_PIN);
DHT dht(DHT_PIN, DHT11);

// Runtime state
bool displayWasOn = false;
unsigned long screenOnUntilMs = 0;
unsigned long lastSensorReadMs = 0;
int lastPhotoValue = 0;
int lastSoilRawValue = 0;
float lastRawTemperature = NAN;
float lastRawHumidity = NAN;
float soilMoistureValue = 0.0f;
float soilMoisturePercent = 0.0f;
float temperatureValue = 0.0f;
float humidityValue = 0.0f;

// Simple exponential moving average filter.
// This is lighter and easier to tune than introducing a full DSP/filter library.
float emaFilter(float previousValue, float newRawValue, float alpha)
{
  if (alpha <= 0.0f)
  {
    return newRawValue;
  }

  if (alpha >= 1.0f)
  {
    return newRawValue;
  }

  return ((alpha * newRawValue) + ((1.0f - alpha) * previousValue));
}

// Helper functions
void updateDisplayPower(bool shouldBeOn)
{
  if (shouldBeOn)
  {
    display.ssd1306_command(SSD1306_DISPLAYON);
  }
  else
  {
    display.ssd1306_command(SSD1306_DISPLAYOFF);
  }
}

void readSensors()
{
  // Soil moisture is an analog reading, so smooth it with EMA before converting to a percent.
  lastSoilRawValue = analogRead(SOIL_MOISTURE_PIN);
  const float rawSoilValue = lastSoilRawValue;
  if (soilMoistureValue == 0.0f)
  {
    soilMoistureValue = rawSoilValue;
  }
  else
  {
    soilMoistureValue = emaFilter(soilMoistureValue, rawSoilValue, EMA_ALPHA);
  }

  // Convert the analog raw reading to a 0..100 scale.
  // If your sensor is inverted, flip the mapping or use 100 - soilMoisturePercent.
  soilMoisturePercent = constrain(
    ((soilMoistureValue - SOIL_MOISTURE_MIN_RAW) / (SOIL_MOISTURE_MAX_RAW - SOIL_MOISTURE_MIN_RAW)) * 100.0f,
    0.0f,
    100.0f
  );

  // DHT11 can be noisy or drop reads, so smooth the temperature and humidity values too.
  lastRawTemperature = dht.readTemperature();
  lastRawHumidity = dht.readHumidity();

  if (isnan(lastRawTemperature))
  {
    temperatureValue = -999.0f;
  }
  else
  {
    if (temperatureValue == 0.0f)
    {
      temperatureValue = lastRawTemperature;
    }
    else
    {
      temperatureValue = emaFilter(temperatureValue, lastRawTemperature, EMA_ALPHA);
    }
  }

  if (isnan(lastRawHumidity))
  {
    humidityValue = -999.0f;
  }
  else
  {
    if (humidityValue == 0.0f)
    {
      humidityValue = lastRawHumidity;
    }
    else
    {
      humidityValue = emaFilter(humidityValue, lastRawHumidity, EMA_ALPHA);
    }
  }
}

void drawMoistureRing(float percent)
{
  const int centerX = 64;
  const int centerY = 34;      // Shift the circle down by 4 px from the previous layout.
  const int ringRadius = 18;
  const int holeRadius = 13;  // Extra inner room so the smaller center text stays inside the donut.
  const int startAngle = -90;
  const int sweep = (int)constrain(percent * 3.6f, 0.0f, 360.0f);

  // Draw the fully outlined donut first.
  display.drawCircle(centerX, centerY, ringRadius, SSD1306_WHITE);

  // Draw the filled arc using a dense pixel fill instead of sparse lines.
  // This avoids the small black gaps that were left by the earlier thin segment method.
  for (int y = centerY - ringRadius; y <= centerY + ringRadius; y++)
  {
    for (int x = centerX - ringRadius; x <= centerX + ringRadius; x++)
    {
      const int dx = x - centerX;
      const int dy = y - centerY;
      const float distance = sqrtf((float)(dx * dx) + (float)(dy * dy));

      if (distance < (float)ringRadius && distance > (float)holeRadius)
      {
        const float angle = atan2f((float)dy, (float)dx) * 57.2957795f;
        const float normalized = angle - startAngle;
        const float wrapped = normalized < 0.0f ? normalized + 360.0f : normalized;

        if (wrapped <= (float)sweep)
        {
          display.drawPixel(x, y, SSD1306_WHITE);
        }
      }
    }
  }

  // Make the center of the donut hollow and clean.
  display.fillCircle(centerX, centerY, holeRadius - 1, SSD1306_BLACK);

  // Center value.
  // Use a smaller font and move the text inward so it fits inside the ring instead of clipping it.
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  char valueText[8];
  snprintf(valueText, sizeof(valueText), "%d", (int)percent);

  const int digitCount = ((int)percent >= 100) ? 3 : 2;
  int16_t textX = centerX - (digitCount * 3) + 1;
  int16_t textY = centerY - 4;
  display.setCursor(textX, textY);
  display.print(valueText);
}

void drawScreen()
{
  display.clearDisplay();

  // Title at the top again.
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(38, 0);
  display.print("Plant Monitor");

  // Moisture ring in the upper area.
  drawMoistureRing(soilMoisturePercent);

  // Bottom-left temperature value.
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(4, 52);
  display.print(temperatureValue, 1);

  // Bottom-right humidity value.
  display.setCursor(90, 52);
  display.print(humidityValue, 1);

  display.display();
}

void setup()
{
  Serial.begin(115200);
  delay(100);

  // Initialize the OLED display.
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS))
  {
    Serial.println("OLED init failed");
    while (true)
    {
      delay(1000);
    }
  }

  // Set 12-bit ADC reads with maximum input attenuation.
  analogReadResolution(12);
  analogSetPinAttenuation(SOIL_MOISTURE_PIN, ADC_11db);
  analogSetPinAttenuation(PHOTO_RESISTOR_PIN, ADC_11db);

  // DHT11 sensor setup.
  dht.begin();
  pinMode(SOIL_MOISTURE_PIN, INPUT);
  pinMode(PHOTO_RESISTOR_PIN, INPUT);
  pinMode(TOUCH_PIN, INPUT_PULLDOWN);

  // Start with screen off.
  updateDisplayPower(false);
  displayWasOn = false;

  Serial.println("Plant monitor started.");
}

void loop()
{
  const unsigned long currentTime = millis();
  // Read the photoresistor and detect a rising edge across the threshold.
  const int currentPhotoValue = analogRead(PHOTO_RESISTOR_PIN);
  const bool photoRisingEdge = (currentPhotoValue >= PHOTO_RISING_THRESHOLD) && (lastPhotoValue < PHOTO_RISING_THRESHOLD);

  // GPIO3 is treated as an active-high touch input.
  const bool touchPressed = (digitalRead(TOUCH_PIN) == HIGH);
  static bool previousTouchPressed = false;
  const bool touchRisingEdge = touchPressed && !previousTouchPressed;

  // If the photo sensor sees a rising edge, start a new 15-second display timer.
  if (photoRisingEdge)
  {
    screenOnUntilMs = currentTime + SCREEN_ON_TIME_MS;
    Serial.printf("event=photo_wake value=%d threshold=%u\n", currentPhotoValue, PHOTO_RISING_THRESHOLD);
  }

  // A held or floating-high input should not keep extending the display timer.
  if (touchRisingEdge)
  {
    screenOnUntilMs = currentTime + SCREEN_ON_TIME_MS;
    Serial.println("event=touch_wake");
  }

  const bool screenShouldBeOn = (static_cast<int32_t>(screenOnUntilMs - currentTime) > 0);

  if (screenShouldBeOn != displayWasOn)
  {
    updateDisplayPower(screenShouldBeOn);
    displayWasOn = screenShouldBeOn;
  }

  if ((currentTime - lastSensorReadMs) >= SENSOR_READ_INTERVAL_MS)
  {
    readSensors();
    lastSensorReadMs = currentTime;

    Serial.printf(
      "ms=%lu photo=%d threshold=%u touch=%d touch_edge=%d screen=%d remaining_ms=%lu soil_raw=%d soil_avg=%.0f soil_pct=%.1f dht_raw_temp=%.1f dht_raw_humidity=%.1f temp=%.1f humidity=%.1f\n",
      currentTime,
      currentPhotoValue,
      PHOTO_RISING_THRESHOLD,
      touchPressed,
      touchRisingEdge,
      screenShouldBeOn,
      screenShouldBeOn ? static_cast<unsigned long>(screenOnUntilMs - currentTime) : 0UL,
      lastSoilRawValue,
      soilMoistureValue,
      soilMoisturePercent,
      lastRawTemperature,
      lastRawHumidity,
      temperatureValue,
      humidityValue
    );

    if (screenShouldBeOn)
    {
      drawScreen();
    }
  }

  // Keep the last photo reading for rising-edge detection.
  lastPhotoValue = currentPhotoValue;
  previousTouchPressed = touchPressed;

  delay(50);
}
