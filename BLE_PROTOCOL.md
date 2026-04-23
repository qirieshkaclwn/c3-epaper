# BLE Протокол взаимодействия между ESP32 и Android телефоном

## Общее описание

Устройство ESP32 (с дисплеем e-paper) работает как **BLE сервер**, который ожидает подключения от Android телефона. Цель: получить актуальное время и отобразить его на дисплее.

---

## 📋 Параметры BLE сервиса

### Константы подключения
```
Service UUID:        4fafc201-1fb5-459e-8fcc-c5c9c331914b
Characteristic UUID: beb5483e-36e1-4688-b7f5-ea07361b26a8
Device Name:         esp32-clock
Wait Timeout:        15 секунд (15000 мс)
```

### Свойства характеристики
- ✅ **READ** - можно читать значение
- ✅ **WRITE** - можно записывать значение

---

## 🔄 Процесс взаимодействия

### Фаза 1: Инициализация (сторона ESP32)

1. **Запуск BLE сервера**
   ```cpp
   NimBLEDevice::init("esp32-clock");
   NimBLEServer* server = NimBLEDevice::createServer();
   ```

2. **Создание сервиса**
   ```cpp
   NimBLEService* service = server->createService(SERVICE_UUID);
   ```

3. **Создание характеристики**
   - UUID: `beb5483e-36e1-4688-b7f5-ea07361b26a8`
   - Начальное значение: `"clock"` (8 байт ASCII)
   - Свойства: READ | WRITE

4. **Запуск рекламы**
   ```cpp
   NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
   advertising->addServiceUUID(SERVICE_UUID);
   advertising->start();
   ```

5. **Ожидание подключения** (15 секунд)
   - При подключении: `gBleConnected = true`
   - При отключении: `gBleConnected = false`

### Фаза 2: Подключение (сторона телефона)

1. **Поиск устройства**
   - Имя: `esp32-clock`
   - Ищем сервис с UUID: `4fafc201-1fb5-459e-8fcc-c5c9c331914b`

2. **Подключение к сервису**

3. **Обнаружение характеристики**
   - UUID: `beb5483e-36e1-4688-b7f5-ea07361b26a8`

### Фаза 3: Передача времени (сторона телефона)

**Отправляем текущее время в характеристику:**

#### Формат данных: 8 байт (Big-Endian)

```
Представление: Unix время в миллисекундах (uint64_t)
Порядок байт:  от старшего к младшему (Big-Endian)
Размер:        ровно 8 байт

Пример:
Текущее время: 1734902400000 мс (2024-12-23 00:00:00 UTC)
В шестнадцатеричной: 0x000193566DC000

Байты (в порядке передачи):
[0] = 0x00
[1] = 0x01
[2] = 0x93
[3] = 0x56
[4] = 0x6D
[5] = 0xC0
[6] = 0x00
[7] = 0x00
```

#### Алгоритм преобразования времени:

```
1. Получить текущее время в миллисекундах:
   unixMillis = System.currentTimeMillis()

2. Представить как массив 8 байт (Big-Endian):
   byte[] timeBytes = new byte[8];
   timeBytes[0] = (byte) ((unixMillis >> 56) & 0xFF);
   timeBytes[1] = (byte) ((unixMillis >> 48) & 0xFF);
   timeBytes[2] = (byte) ((unixMillis >> 40) & 0xFF);
   timeBytes[3] = (byte) ((unixMillis >> 32) & 0xFF);
   timeBytes[4] = (byte) ((unixMillis >> 24) & 0xFF);
   timeBytes[5] = (byte) ((unixMillis >> 16) & 0xFF);
   timeBytes[6] = (byte) ((unixMillis >> 8) & 0xFF);
   timeBytes[7] = (byte) (unixMillis & 0xFF);

3. Отправить в характеристику:
   characteristic.writeValue(timeBytes)
```

### Фаза 4: Обработка (сторона ESP32)

Функция `onWrite()` вызывается автоматически:

```cpp
void onWrite(NimBLECharacteristic* pCharacteristic) override {
    const std::string value = pCharacteristic->getValue();
    
    // Проверяем размер: должно быть ровно 8 байт
    if (value.size() != 8) {
        return;  // Игнорируем неверные данные
    }

    // Парсим Big-Endian uint64_t
    uint64_t unixMillis = 0;
    for (size_t i = 0; i < 8; ++i) {
        unixMillis = (unixMillis << 8) | static_cast<uint8_t>(value[i]);
    }

    // Конвертируем в Unix-секунды (uint32_t)
    cachedUnixTime = static_cast<uint32_t>(unixMillis / 1000ULL);
    
    // Отмечаем, что время получено
    cachedHasUnixTime = true;
    gTimeUpdated = true;  // Сигнал выхода из ожидания
}
```

### Фаза 5: Завершение (сторона ESP32)

После получения времени:

1. ✅ Останавливаем рекламу
2. ✅ Выключаем BLE (`NimBLEDevice::deinit()`)
3. ✅ Отображаем данные на экране:
   - Статус подключения
   - Полученное время
   - Статус обновления
   - Напряжение батареи
   - Количество включений (bootCount)
   - Время работы
4. ✅ Переводим устройство в глубокий сон (60 секунд)

---

## 📱 Реализация Android приложения

### Требуемые разрешения (AndroidManifest.xml)

```xml
<uses-permission android:name="android.permission.BLUETOOTH" />
<uses-permission android:name="android.permission.BLUETOOTH_ADMIN" />
<uses-permission android:name="android.permission.BLUETOOTH_SCAN" />
<uses-permission android:name="android.permission.BLUETOOTH_CONNECT" />
<uses-feature android:name="android.hardware.bluetooth_le" android:required="true" />
```

### Пошаговая реализация

#### 1. Поиск устройства

```kotlin
// Инициализируем BLE сканер
val bluetoothManager = context.getSystemService(BluetoothManager::class.java)
val bluetoothAdapter = bluetoothManager?.adapter
val scanner = bluetoothAdapter?.bluetoothLeScanner

// Ищем устройство по имени "esp32-clock"
scanner?.startScan(object : ScanCallback() {
    override fun onScanResult(callbackType: Int, result: ScanResult) {
        if (result.device.name == "esp32-clock") {
            // Найдено! Подключаемся
            connectToDevice(result.device)
        }
    }
})
```

#### 2. Подключение к устройству

```kotlin
fun connectToDevice(device: BluetoothDevice) {
    gatt = device.connectGatt(context, false, object : BluetoothGattCallback() {
        override fun onConnectionStateChange(
            gatt: BluetoothGatt?,
            status: Int,
            newState: Int
        ) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                // Подключились! Ищем сервисы
                gatt?.discoverServices()
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt?, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                // Получили список сервисов
                handleServicesDiscovered(gatt)
            }
        }
    })
}
```

#### 3. Поиск нужной характеристики

```kotlin
fun handleServicesDiscovered(gatt: BluetoothGatt?) {
    val serviceUUID = UUID.fromString("4fafc201-1fb5-459e-8fcc-c5c9c331914b")
    val characteristicUUID = UUID.fromString("beb5483e-36e1-4688-b7f5-ea07361b26a8")

    val service = gatt?.getService(serviceUUID)
    val characteristic = service?.getCharacteristic(characteristicUUID)

    if (characteristic != null) {
        sendTimeToDevice(gatt, characteristic)
    }
}
```

#### 4. Отправка времени

```kotlin
fun sendTimeToDevice(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
    // Получаем текущее время в миллисекундах
    val unixMillis = System.currentTimeMillis()

    // Конвертируем в 8 байт (Big-Endian)
    val timeBytes = ByteArray(8)
    timeBytes[0] = ((unixMillis shr 56) and 0xFF).toByte()
    timeBytes[1] = ((unixMillis shr 48) and 0xFF).toByte()
    timeBytes[2] = ((unixMillis shr 40) and 0xFF).toByte()
    timeBytes[3] = ((unixMillis shr 32) and 0xFF).toByte()
    timeBytes[4] = ((unixMillis shr 24) and 0xFF).toByte()
    timeBytes[5] = ((unixMillis shr 16) and 0xFF).toByte()
    timeBytes[6] = ((unixMillis shr 8) and 0xFF).toByte()
    timeBytes[7] = (unixMillis and 0xFF).toByte()

    // Записываем в характеристику
    characteristic.value = timeBytes
    gatt.writeCharacteristic(characteristic)

    // После успешной записи: отключаемся
    gatt.disconnect()
}
```

#### 5. Обработка результата

```kotlin
override fun onCharacteristicWrite(
    gatt: BluetoothGatt?,
    characteristic: BluetoothGattCharacteristic?,
    status: Int
) {
    if (status == BluetoothGatt.GATT_SUCCESS) {
        Log.d("BLE", "✅ Время успешно отправлено!")
        gatt?.disconnect()
    } else {
        Log.e("BLE", "❌ Ошибка при отправке времени")
    }
}
```

---

## 🔍 Отладка и тестирование

### Проверка на сторону ESP32

Добавьте в `onWrite()` для отладки:

```cpp
Serial.printf("📝 Получены данные (%d байт):\n", value.size());
for (size_t i = 0; i < value.size(); i++) {
    Serial.printf("  [%d] = 0x%02X\n", i, static_cast<uint8_t>(value[i]));
}
Serial.printf("📅 Unix время: %lu секунд\n", cachedUnixTime);
```

### Проверка на сторона Android

```kotlin
Log.d("BLE", "Отправляемое время (мс): $unixMillis")
Log.d("BLE", "Байты: ${timeBytes.joinToString(" ") { "%02X".format(it) }}")
```

---

## ⚠️ Важные моменты

1. **Размер данных**: Должно быть **ровно 8 байт**, иначе будут проигнорированы
2. **Формат**: **Big-Endian** (от старшего байта к младшему)
3. **Тип данных**: **uint64_t** миллисекунды (не секунды!)
4. **Таймаут**: ESP32 ждёт подключения **15 секунд**
5. **После получения**: Устройство автоматически отключается и засыпает на **60 секунд**
6. **Повторное подключение**: Возможно только после пробуждения устройства

---

## 📊 Пример полного цикла

```
Телефон                          ESP32
  |                                |
  |--- BLE сканирование ---------->|  (реклама: "esp32-clock")
  |                                |
  |<--- Ответ о наличии сервиса ---|
  |                                |
  |--- Подключение --------|------->|
  |                         |--------|  гBleConnected = true
  |<------- OK ------------|<--------|
  |                                |
  |--- Discover services -------->|
  |                                |
  |<--- Service list -------------|
  |                                |
  |--- Write time (8 bytes) ----->|
  |   [0x00, 0x01, 0x93, 0x56,    |
  |    0x6D, 0xC0, 0x00, 0x00]    |
  |                                |
  |                        |-------|  gTimeUpdated = true
  |                        |-------|  Отображение на экране
  |                        |-------|  Отключение BLE
  |<------ Disconnect ------------|
  |                        |-------|  esp_deep_sleep_start()
```

---

## 📝 Контрольный список для реализации

- [ ] Добавить разрешения в AndroidManifest.xml
- [ ] Запросить runtime permissions (API 31+)
- [ ] Инициализировать BluetoothManager и BluetoothAdapter
- [ ] Реализовать BLE сканер для поиска "esp32-clock"
- [ ] Реализовать подключение к устройству (connectGatt)
- [ ] Реализовать обнаружение сервисов (discoverServices)
- [ ] Найти нужную характеристику (UUID: beb5483e-36e1-4688-b7f5-ea07361b26a8)
- [ ] Конвертировать текущее время в массив 8 байт (Big-Endian)
- [ ] Отправить данные (writeCharacteristic)
- [ ] Отключиться после отправки (disconnect)
- [ ] Добавить обработку ошибок и таймауты
- [ ] Добавить логирование для отладки
