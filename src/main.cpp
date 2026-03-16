#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <WiFi.h>
#include <time.h>

// Параметры: CS=7, DC=9, RST=2, BUSY=0
GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(GxEPD2_154_D67(7, 9, 2, 0));
U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;

const char* ssid = "MedVed";
const char* password = "19411945";

void setup() {
  Serial.begin(115200);

  // Инициализация SPI и дисплея
  SPI.begin(4, -1, 6, 7); // SCK=6, MOSI=4, CS=7
  display.init(115200);
  u8g2Fonts.begin(display);
  u8g2Fonts.setFont(u8g2_font_unifont_t_cyrillic);
  u8g2Fonts.setForegroundColor(GxEPD_BLACK);
  u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
  u8g2Fonts.setFontMode(1);
  // Подключение к Wi-Fi
  WiFi.begin(ssid, password);
  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  // Получение времени
  configTime(10800, 0, "pool.ntp.org");
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char t1[10];
    strftime(t1, sizeof(t1), "%H:%M:%S", &timeinfo);

    display.setFullWindow();
    display.firstPage();
    do {
      display.fillScreen(GxEPD_WHITE);
      u8g2Fonts.setCursor(10, 40);
      u8g2Fonts.print("Время: ");
      u8g2Fonts.print(t1);
    } while (display.nextPage());
  }

  // Завершение работы
  Serial.println("Уходим в сон...");
  Serial.flush(); 
  
  WiFi.disconnect(true);
  esp_sleep_enable_timer_wakeup(60 * 1000000); // 60 секунд
  esp_deep_sleep_start();
}

void loop() {}