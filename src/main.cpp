#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "time.h"
#include <NimBLEDevice.h>
#include <cstring>
#include <string>

#define EPD_POWER_PIN 1

// ===== НАСТРОЙКИ ADC ДЛЯ БАТАРЕИ =====
#define BAT_ADC_PIN     3
#define R1              300000.0
#define R2              100000.0
#define ADC_REF_VOLTAGE 1.038
#define ADC_RESOLUTION  4095.0

// ===== НАСТРОЙКИ BLE (под Android-клиент) =====
static constexpr const char* SERVICE_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
static constexpr const char* CHARACTERISTIC_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26a8";
static constexpr uint32_t BLE_WAIT_FOR_PHONE_MS = 15000;
static constexpr uint64_t SLEEP_INTERVAL_US = 60ULL * 1000000ULL;

GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(GxEPD2_154_D67(7, 9, 2, 0));
U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;

RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR uint32_t cachedUnixTime = 0;
RTC_DATA_ATTR bool cachedHasUnixTime = false;

static volatile bool gBleConnected = false;
static volatile bool gTimeUpdated = false;

class ClockServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) override {
        (void)pServer;
        gBleConnected = true;
    }

    void onDisconnect(NimBLEServer* pServer) override {
        (void)pServer;
        gBleConnected = false;
    }
};

class ClockCharacteristicCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic) override {
        const std::string value = pCharacteristic->getValue();
        if (value.size() != 8) {
            return;
        }

        uint64_t unixMillis = 0;
        for (size_t i = 0; i < 8; ++i) {
            unixMillis = (unixMillis << 8) | static_cast<uint8_t>(value[i]);
        }

        cachedUnixTime = static_cast<uint32_t>(unixMillis / 1000ULL);
        cachedHasUnixTime = true;
        gTimeUpdated = true;
    }
};

static ClockServerCallbacks gServerCallbacks;
static ClockCharacteristicCallbacks gCharacteristicCallbacks;

static void formatCachedTime(char* out, size_t outSize) {
    if (outSize == 0) {
        return;
    }

    if (!cachedHasUnixTime) {
        std::strncpy(out, "--:--:--", outSize - 1);
        out[outSize - 1] = '\0';
        return;
    }

    time_t rawTime = static_cast<time_t>(cachedUnixTime);
    struct tm tmInfo;
    gmtime_r(&rawTime, &tmInfo);
    strftime(out, outSize, "%H:%M:%S", &tmInfo);
}

static bool waitForPhoneTime(bool& outWasConnected) {
    outWasConnected = false;
    gBleConnected = false;
    gTimeUpdated = false;

    NimBLEDevice::init("esp32-clock");
    NimBLEServer* server = NimBLEDevice::createServer();
    if (server == nullptr) {
        NimBLEDevice::deinit();
        return false;
    }
    server->setCallbacks(&gServerCallbacks);

    NimBLEService* service = server->createService(SERVICE_UUID);
    if (service == nullptr) {
        NimBLEDevice::deinit();
        return false;
    }

    NimBLECharacteristic* characteristic = service->createCharacteristic(
        CHARACTERISTIC_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
    if (characteristic == nullptr) {
        NimBLEDevice::deinit();
        return false;
    }
    characteristic->setCallbacks(&gCharacteristicCallbacks);
    characteristic->setValue("clock");
    service->start();

    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    advertising->addServiceUUID(SERVICE_UUID);
    advertising->start();

    const uint32_t waitStart = millis();
    while ((millis() - waitStart) < BLE_WAIT_FOR_PHONE_MS) {
        if (gBleConnected) {
            outWasConnected = true;
        }
        if (gTimeUpdated) {
            advertising->stop();
            NimBLEDevice::deinit();
            return true;
        }
        delay(20);
    }

    advertising->stop();
    NimBLEDevice::deinit();
    return false;
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

    float vbat = readBatteryVoltage();
    bool phoneConnected = false;
    const bool phoneDataUpdated = waitForPhoneTime(phoneConnected);

    char timeStr[16];
    formatCachedTime(timeStr, sizeof(timeStr));
    
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        
        u8g2Fonts.setCursor(10, 30);
        u8g2Fonts.print("Телефон: ");
        u8g2Fonts.print(phoneConnected ? "OK" : "OFF");
        
        u8g2Fonts.setCursor(10, 55);
        u8g2Fonts.print("Время: "); u8g2Fonts.print(timeStr);

        u8g2Fonts.setCursor(10, 80);
        u8g2Fonts.print("Обновлено: ");
        u8g2Fonts.print(phoneDataUpdated ? "да" : "нет");

        u8g2Fonts.setCursor(10, 105);
        u8g2Fonts.print("Батарея: ");
        u8g2Fonts.print(vbat, 2);
        u8g2Fonts.print(" V");
        
        u8g2Fonts.setCursor(10, 130);
        u8g2Fonts.print("Проходов: "); u8g2Fonts.print(bootCount);
        
        u8g2Fonts.setCursor(10, 155);
        u8g2Fonts.print("Работа: "); u8g2Fonts.print(millis() - startMillis); u8g2Fonts.print(" мс");
        
    } while (display.nextPage());

    display.hibernate();
    
    pinMode(4, INPUT); 
    pinMode(6, INPUT); 
    pinMode(7, INPUT); 
    digitalWrite(EPD_POWER_PIN, LOW);

    esp_sleep_enable_timer_wakeup(SLEEP_INTERVAL_US);
    esp_deep_sleep_start();
}

void loop() {
}
