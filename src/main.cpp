#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <WiFi.h>
#include "time.h"

#define EPD_POWER_PIN 1

GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(GxEPD2_154_D67(7, 9, 2, 0));
U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;

// Переменные в RTC
RTC_DATA_ATTR bool initialSyncDone = false;
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR time_t lastSyncTime = 0; // Время последней синхронизации

const char* ssid     = "MedVed";
const char* password = "19411945";
const long  gmtOffset_sec = 60*60*3;
const int   daylightOffset_sec = 0;

void setup() {
    pinMode(EPD_POWER_PIN, OUTPUT);
    digitalWrite(EPD_POWER_PIN, HIGH); 
    delay(100); 

    unsigned long startMillis = millis();
    Serial.begin(115200);
    bootCount++;

    SPI.begin(4, -1, 6, 7); 
    display.init(115200);
    u8g2Fonts.begin(display);
    u8g2Fonts.setFont(u8g2_font_unifont_t_cyrillic);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
    u8g2Fonts.setFontMode(1);

    configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org");
    struct tm timeinfo;
    bool wifiUsed = false;

    if (!initialSyncDone) {
        WiFi.begin(ssid, password);
        while (WiFi.status() != WL_CONNECTED) { delay(500); }
        if (getLocalTime(&timeinfo)) {
            initialSyncDone = true;
            lastSyncTime = mktime(&timeinfo); // Сохраняем время синхронизации
        }
        wifiUsed = true;
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    }
    
    getLocalTime(&timeinfo);
    time_t now = mktime(&timeinfo);
    
    // Вычисляем время, прошедшее с момента синхронизации (в секундах)
    long secondsSinceSync = (lastSyncTime > 0) ? (now - lastSyncTime) : 0;
    long totalSeconds = secondsSinceSync;
    int days = totalSeconds / 86400;
    int hours = (totalSeconds % 86400) / 3600;
    int minutes = (totalSeconds % 3600) / 60;
    char timeStr[15];
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
    
    // 3. Отрисовка
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        u8g2Fonts.setCursor(10, 30);
        u8g2Fonts.print(wifiUsed ? "Статус: NTP" : "Статус: RTC");
        u8g2Fonts.setCursor(10, 55);
        u8g2Fonts.print("Время: "); u8g2Fonts.print(timeStr);
        u8g2Fonts.setCursor(10, 80);
        u8g2Fonts.print("syn: ");
        u8g2Fonts.print(days); u8g2Fonts.print("д ");
        u8g2Fonts.print(hours); u8g2Fonts.print("ч ");
        u8g2Fonts.print(minutes); u8g2Fonts.print("м");
        u8g2Fonts.setCursor(10, 105);
        u8g2Fonts.print("Проходов: "); u8g2Fonts.print(bootCount);
        u8g2Fonts.setCursor(10, 130);
        u8g2Fonts.print("Работа: "); u8g2Fonts.print(millis() - startMillis); u8g2Fonts.print(" мс");
    } while (display.nextPage());

    display.hibernate();
    pinMode(4, INPUT); pinMode(6, INPUT); pinMode(7, INPUT); 
    digitalWrite(EPD_POWER_PIN, LOW);

    esp_sleep_enable_timer_wakeup(60 * 1000000);
    esp_deep_sleep_start();
}

void loop() {}