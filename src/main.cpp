#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <WiFi.h>
#include "time.h"
#include <NimBLEDevice.h>

#define EPD_POWER_PIN 1

// ===== НАСТРОЙКИ ADC ДЛЯ БАТАРЕИ =====
#define BAT_ADC_PIN     3
#define R1              300000.0
#define R2              100000.0
#define ADC_REF_VOLTAGE 1.038
#define ADC_RESOLUTION  4095.0

// ===== НАСТРОЙКИ BLE =====
#define BLE_SCAN_TIME     2         // секунды
#define BLE_SCAN_ACTIVE   false

GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(GxEPD2_154_D67(7, 9, 2, 0));
U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;

RTC_DATA_ATTR bool initialSyncDone = false;
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR time_t lastSyncTime = 0;

const char* ssid     = "MedVed";
const char* password = "19411945";
const long  gmtOffset_sec = 60*60*3;
const int   daylightOffset_sec = 0;

// ===== СЧЁТЧИК BLE-УСТРОЙСТВ =====
volatile uint16_t bleDeviceCount = 0;

// ===== КОЛЛБЕК ДЛЯ СКАНИРОВАНИЯ (NimBLE v2.x API) =====
class BLEScanCallback : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) override {
        bleDeviceCount++;
    }
};

// ===== ФУНКЦИЯ СКАНИРОВАНИЯ BLE =====
uint16_t scanBLEDevices() {
    bleDeviceCount = 0;
    
    NimBLEDevice::init("epaper-clock");
    NimBLEScan* pBLEScan = NimBLEDevice::getScan();
    
    BLEScanCallback* pCallback = new BLEScanCallback();
    pBLEScan->setAdvertisedDeviceCallbacks(pCallback, true);  // ← v2.x API
    pBLEScan->setActiveScan(BLE_SCAN_ACTIVE);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);
    
    BLEScanResults results = pBLEScan->start(BLE_SCAN_TIME, false);
    uint16_t count = bleDeviceCount;
    
    pBLEScan->stop();
    pBLEScan->clearResults();
    delete pCallback;
    
    NimBLEDevice::deinit();
    
    return count;
}

// ===== ФУНКЦИЯ ИЗМЕРЕНИЯ НАПРЯЖЕНИЯ БАТАРЕИ =====
float readBatteryVoltage() {
  // Холостое чтение для "прогрева" АЦП
  analogRead(BAT_ADC_PIN);
  delayMicroseconds(100);
  
  uint32_t sum_mv = 0;
  const int samples = 16;
  
  for(int i = 0; i < samples; i++) {
    // analogReadMilliVolts возвращает уже откалиброванное напряжение в милливольтах!
    sum_mv += analogReadMilliVolts(BAT_ADC_PIN);
    delayMicroseconds(50);
  }
  
  // Среднее значение в милливольтах
  float avg_mv = sum_mv / (float)samples;
  
  // Переводим в вольты
  float v_adc = avg_mv / 1000.0;
  
  // Умножаем на коэффициент делителя: (300k + 100k) / 100k = 4.0
  float v_bat = v_adc * ((R1 + R2) / R2); 
  
  // Применяем калибровочный коэффициент для компенсации погрешностей
  const float CALIBRATION_FACTOR = 1.0202; 
  v_bat = v_bat * CALIBRATION_FACTOR; 
  
  return v_bat;
}

void setup() {
    pinMode(EPD_POWER_PIN, OUTPUT);
    digitalWrite(EPD_POWER_PIN, HIGH); 
    delay(100); 

    unsigned long startMillis = millis();
    Serial.begin(115200);
    bootCount++;

    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);
    pinMode(BAT_ADC_PIN, INPUT);

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
            lastSyncTime = mktime(&timeinfo);
        }
        wifiUsed = true;
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    }
    
    getLocalTime(&timeinfo);
    time_t now = mktime(&timeinfo);
    
    long secondsSinceSync = (lastSyncTime > 0) ? (now - lastSyncTime) : 0;
    long totalSeconds = secondsSinceSync;
    int days = totalSeconds / 86400;
    int hours = (totalSeconds % 86400) / 3600;
    int minutes = (totalSeconds % 3600) / 60;
    char timeStr[15];
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
    
    float vbat = readBatteryVoltage();
    uint16_t bleCount = scanBLEDevices();
    
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
        u8g2Fonts.print("Батарея: ");
        u8g2Fonts.print(vbat, 2);
        u8g2Fonts.print(" V");
        
        u8g2Fonts.setCursor(10, 125);
        u8g2Fonts.print("BLE: ");
        u8g2Fonts.print(bleCount);
        u8g2Fonts.print(" устр.");
        
        u8g2Fonts.setCursor(10, 145);
        u8g2Fonts.print("Проходов: "); u8g2Fonts.print(bootCount);
        
        u8g2Fonts.setCursor(10, 165);
        u8g2Fonts.print("Работа: "); u8g2Fonts.print(millis() - startMillis); u8g2Fonts.print(" мс");
        
    } while (display.nextPage());

    display.hibernate();
    
    pinMode(4, INPUT); 
    pinMode(6, INPUT); 
    pinMode(7, INPUT); 
    digitalWrite(EPD_POWER_PIN, LOW);

    esp_sleep_enable_timer_wakeup(60 * 1000000);
    esp_deep_sleep_start();
}

void loop() {
}