#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#define TFT_MOSI 10
#define TFT_SCLK 8
#define TFT_CS 5 /
#define TFT_RST 4
#define TFT_DC 3

#define BACKGROUND_COLOR ST77XX_BLACK
#define TEXT_COLOR ST77XX_WHITE
#define ALERT_COLOR ST77XX_RED

int currentBattery = 0;
int currentSpeed = 0;
int currentSafetyMargin = 0;
int currentDistance = 0;

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

class WiFiManager
{
public:
  void connect()
  {
    WiFi.begin("SSID", "PASS");

    while (WiFi.status() != WL_CONNECTED)
    {
      delay(1000);
    }

    routerIP = WiFi.gatewayIP();
  }

  static IPAddress getRouterIP()
  {
    return WiFi.gatewayIP();
  }

  bool isConnected()
  {
    return WiFi.status() == WL_CONNECTED;
  }

private:
  IPAddress routerIP;
};

class Display
{
public:
  void init()
  {
    tft.initR(INITR_MINI160x80);
    tft.setRotation(1);
    tft.fillScreen(BACKGROUND_COLOR);
    tft.setSPISpeed(20000000);
    drawLabels();
  }

  void updateBattery(int oldValue, int newValue)
  {
    if (!inAlertMode)
    {
      if (newValue > 80)
      {
        updateValue(oldValue, newValue, 0, 20, ST7735_GREEN, 3);
      }
      else if (newValue > 60)
      {
        updateValue(oldValue, newValue, 0, 20, ST7735_YELLOW, 3);
      }
      else
      {
        updateValue(oldValue, newValue, 0, 20, ST7735_RED, 3);
      }
    }
  }

  void updateSpeed(int oldValue, int newValue)
  {
    if (!inAlertMode)
    {
      updateValue(oldValue, newValue, 70, 20, TEXT_COLOR, 7);
    }
  }

  void updateDistance(int oldValue, int newValue)
  {
    if (!inAlertMode)
    {
      updateValue(oldValue, newValue, 0, 60, TEXT_COLOR, 3);
    }
  }

  void updateSafetyMargin(int oldValue, int newValue)
  {
    if (newValue < SAFETY_THRESHOLD)
    {
      if (!inAlertMode)
      {
        // Entering alert mode
        inAlertMode = true;
        showFullScreenAlert();
      }
      // Update only the percentage
      updateAlertPercentage(oldValue, newValue);
    }
    else if (inAlertMode)
    {
      // Exiting alert mode
      inAlertMode = false;
      tft.fillScreen(BACKGROUND_COLOR);
      drawLabels();
      updateBattery(0, currentBattery);
      updateSpeed(0, currentSpeed);
    }
  }
  void clearAndInitialize()
  {
    tft.fillScreen(BACKGROUND_COLOR);
    drawLabels();
    updateBattery(0, currentBattery);
    updateSpeed(0, currentSpeed);
    updateDistance(0, currentDistance);
  }

  void setCurrentValues(int battery, int speed, int distance)
  {
    currentBattery = battery;
    currentSpeed = speed;
    currentDistance = distance;
  }

private:
  static const int SAFETY_THRESHOLD = 15;
  int currentBattery = 0;
  int currentSpeed = 0;
  bool inAlertMode = false;

  void drawLabels()
  {

    tft.setTextColor(TEXT_COLOR);
    tft.setTextSize(1);
    tft.setCursor(5, 5);
    tft.print("BAT%");
    tft.setCursor(15, 45);
    tft.print("DIS");
    tft.setCursor(95, 5);
    tft.print("MPH");
    tft.drawFastVLine(50, 0, tft.height(), 0x7BEF);
  }

  void updateValue(int oldValue, int newValue, int x, int y, uint16_t color, int size = 5)
  {

    tft.setTextSize(size);

    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds("0", 0, 0, &x1, &y1, &w, &h);

    // Ensure two-digit display
    String oldStr = (oldValue < 10) ? "0" + String(oldValue) : String(min(oldValue, 99));
    String newStr = (newValue < 10) ? "0" + String(newValue) : String(min(newValue, 99));

    for (int i = 0; i < 2; i++)
    {
      if (oldStr[i] != newStr[i])
      {
        // Erase old digit
        tft.setTextColor(BACKGROUND_COLOR);
        tft.setCursor(x + i * w, y);
        tft.print(oldStr[i]);

        // Write new digit
        tft.setTextColor(color);
        tft.setCursor(x + i * w, y);
        tft.print(newStr[i]);
      }
    }
  }

  void showFullScreenAlert()
  {
    tft.fillScreen(ALERT_COLOR);
    tft.setTextColor(TEXT_COLOR);
    tft.setTextSize(2);
    tft.setCursor(10, 20);
    tft.print("SAFETY MARGIN");
  }

  void updateAlertPercentage(int oldValue, int newValue)
  {
    tft.setTextSize(5);

    // Ensure two-digit display
    String oldStr = (oldValue < 10) ? "0" + String(oldValue) : String(oldValue);
    String newStr = (newValue < 10) ? "0" + String(newValue) : String(newValue);

    // Erase old value
    tft.setTextColor(ALERT_COLOR);
    tft.setCursor(30, 40);
    tft.print(oldStr);
    tft.print("%");

    // Write new value
    tft.setTextColor(TEXT_COLOR);
    tft.setCursor(30, 40);
    tft.print(newStr);
    tft.print("%");
  }
};

class APIManager
{
public:
  APIManager(Display &display, int &currentBattery, int &currentSpeed, int &currentSafetyMargin, int &currentDistance)
      : display(display), currentBattery(currentBattery), currentSpeed(currentSpeed), currentSafetyMargin(currentSafetyMargin), currentDistance(currentDistance) {}

  void fetchValues()
  {

    String url = "http://" + WiFiManager::getRouterIP().toString() + ":8080/api/values?attrs=0&filter=%28vsp%7Cvba%7Cvsmg%7Cvdi%29";

    HTTPClient http;
    http.begin(url);

    int httpResponseCode = http.GET();

    if (httpResponseCode > 0)
    {
      String &&payload = http.getString();
      parseAndUpdateValues(std::move(payload));
    }
    else
    {
    }

    http.end();
  }

private:
  Display &display;
  int &currentBattery;
  int &currentSpeed;
  int &currentSafetyMargin;
  int &currentDistance;
  void parseAndUpdateValues(String payload)
  {
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, payload);

    if (error)
    {
      return;
    }

    if (doc.containsKey("vba") && doc["vba"].containsKey("v"))
    {
      int newBattery = doc["vba"]["v"].as<int>();
      if (newBattery != currentBattery)
      {
        display.updateBattery(currentBattery, newBattery);
        currentBattery = newBattery;
      }
    }

    if (doc.containsKey("vsp") && doc["vsp"].containsKey("v"))
    {
      int newSpeed = doc["vsp"]["v"].as<int>();
      if (newSpeed != currentSpeed)
      {
        newSpeed = round(newSpeed * 0.621371);
        display.updateSpeed(currentSpeed, newSpeed);
        currentSpeed = newSpeed;
      }
    }

    if (doc.containsKey("vsmg") && doc["vsmg"].containsKey("v"))
    {
      int newSafetyMargin = doc["vsmg"]["v"].as<int>();
      if (newSafetyMargin != currentSafetyMargin)
      {
        display.updateSafetyMargin(currentSafetyMargin, newSafetyMargin);
        currentSafetyMargin = newSafetyMargin;
      }
    }

    if (doc.containsKey("vdi") && doc["vdi"].containsKey("v"))
    {
      int newDistance = doc["vdi"]["v"].as<int>();
      if (newDistance != currentDistance)
      {
        newDistance = round(newDistance * 0.621371);
        display.updateDistance(currentDistance, newDistance);
        currentDistance = newDistance;
      }
    }

    display.setCurrentValues(currentBattery, currentSpeed, currentDistance);
  }
};

Display display;
WiFiManager wifiManager;
APIManager apiManager(display, currentBattery, currentSpeed, currentSafetyMargin, currentDistance);

void setup()
{
  SPI.begin(8, 9, 10); // SCLK, MISO, MOSI

  display.init();

  // Display connecting message
  tft.fillScreen(BACKGROUND_COLOR);
  tft.setTextColor(TEXT_COLOR);
  tft.setTextSize(2);
  tft.setCursor(10, 30);
  tft.print("Connecting.");

  wifiManager.connect();

  while (!wifiManager.isConnected())
  {
    delay(500);
  }

  tft.fillScreen(BACKGROUND_COLOR);
  tft.setCursor(10, 40);
  tft.print("Connected :)");
  delay(500); 
  display.clearAndInitialize();

  display.updateBattery(22, 0);
  display.updateSpeed(22, 0);
  display.updateDistance(22, 0);
}

void loop()
{
  if (wifiManager.isConnected())
  {
    apiManager.fetchValues();
  }
  else
  {
    wifiManager.connect();
  }
  delay(200);
}
