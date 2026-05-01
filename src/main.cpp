#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <LittleFS.h>
#include <WiFi.h>

namespace {

constexpr char kDeviceName[] = "WiFiRangeLogger";
constexpr char kLogPath[] = "/wifi_range.csv";
constexpr uint32_t kScanIntervalMs = 5000;
constexpr uint32_t kStatusIntervalMs = 10000;

// Nordic UART Service UUIDs. Many BLE terminal apps already understand these.
constexpr char kUartServiceUuid[] = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr char kUartRxUuid[] = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"; // phone -> ESP32
constexpr char kUartTxUuid[] = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"; // ESP32 -> phone

struct GpsFix {
  bool valid = false;
  double lat = 0.0;
  double lon = 0.0;
  double alt = NAN;
  double accuracy = NAN;
  double speed = NAN;
  uint32_t updatedMs = 0;
};

GpsFix gps;
BLECharacteristic *txCharacteristic = nullptr;
bool bleConnected = false;
uint32_t lastScanMs = 0;
uint32_t lastStatusMs = 0;
uint32_t logRows = 0;

String csvEscape(const String &value) {
  String escaped = value;
  escaped.replace("\"", "\"\"");
  return "\"" + escaped + "\"";
}

String encTypeToString(wifi_auth_mode_t type) {
  switch (type) {
    case WIFI_AUTH_OPEN:
      return "OPEN";
    case WIFI_AUTH_WEP:
      return "WEP";
    case WIFI_AUTH_WPA_PSK:
      return "WPA";
    case WIFI_AUTH_WPA2_PSK:
      return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:
      return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE:
      return "WPA2-ENT";
    case WIFI_AUTH_WPA3_PSK:
      return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:
      return "WPA2/WPA3";
    default:
      return "UNKNOWN";
  }
}

bool extractJsonNumber(const String &json, const char *key, double &out) {
  const String needle = String("\"") + key + "\"";
  int keyIndex = json.indexOf(needle);
  if (keyIndex < 0) {
    return false;
  }

  int colonIndex = json.indexOf(':', keyIndex + needle.length());
  if (colonIndex < 0) {
    return false;
  }

  int valueStart = colonIndex + 1;
  while (valueStart < json.length() && isspace(static_cast<unsigned char>(json[valueStart]))) {
    valueStart++;
  }

  int valueEnd = valueStart;
  while (valueEnd < json.length()) {
    const char c = json[valueEnd];
    if (!(isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E')) {
      break;
    }
    valueEnd++;
  }

  if (valueEnd == valueStart) {
    return false;
  }

  out = json.substring(valueStart, valueEnd).toDouble();
  return true;
}

bool parseGpsJson(const String &payload) {
  double lat = 0.0;
  double lon = 0.0;

  if (!extractJsonNumber(payload, "lat", lat) || !extractJsonNumber(payload, "lon", lon)) {
    return false;
  }

  gps.lat = lat;
  gps.lon = lon;
  gps.valid = true;
  gps.updatedMs = millis();
  extractJsonNumber(payload, "alt", gps.alt);
  extractJsonNumber(payload, "accuracy", gps.accuracy);
  extractJsonNumber(payload, "speed", gps.speed);
  return true;
}

void notifyPhone(const String &message) {
  Serial.println(message);
  if (txCharacteristic != nullptr && bleConnected) {
    txCharacteristic->setValue(message.c_str());
    txCharacteristic->notify();
  }
}

void ensureLogHeader() {
  if (LittleFS.exists(kLogPath)) {
    File existing = LittleFS.open(kLogPath, "r");
    if (existing && existing.size() > 0) {
      existing.close();
      return;
    }
    existing.close();
  }

  File file = LittleFS.open(kLogPath, "w");
  if (!file) {
    Serial.println("Failed to create log file");
    return;
  }

  file.println("uptime_ms,scan_id,ssid,bssid,rssi_dbm,channel,encryption,lat,lon,alt_m,accuracy_m,speed_mps,gps_age_ms");
  file.close();
}

void appendScanRows() {
  static uint32_t scanId = 0;
  scanId++;

  notifyPhone("Scanning Wi-Fi...");
  const int networkCount = WiFi.scanNetworks(false, true);
  if (networkCount < 0) {
    notifyPhone("Wi-Fi scan failed");
    return;
  }

  File file = LittleFS.open(kLogPath, "a");
  if (!file) {
    notifyPhone("Failed to open log file");
    WiFi.scanDelete();
    return;
  }

  const uint32_t now = millis();
  const uint32_t gpsAge = gps.valid ? now - gps.updatedMs : 0;

  for (int i = 0; i < networkCount; i++) {
    file.print(now);
    file.print(',');
    file.print(scanId);
    file.print(',');
    file.print(csvEscape(WiFi.SSID(i)));
    file.print(',');
    file.print(WiFi.BSSIDstr(i));
    file.print(',');
    file.print(WiFi.RSSI(i));
    file.print(',');
    file.print(WiFi.channel(i));
    file.print(',');
    file.print(encTypeToString(WiFi.encryptionType(i)));
    file.print(',');

    if (gps.valid) {
      file.print(gps.lat, 7);
      file.print(',');
      file.print(gps.lon, 7);
      file.print(',');
      if (!isnan(gps.alt)) {
        file.print(gps.alt, 2);
      }
      file.print(',');
      if (!isnan(gps.accuracy)) {
        file.print(gps.accuracy, 2);
      }
      file.print(',');
      if (!isnan(gps.speed)) {
        file.print(gps.speed, 2);
      }
      file.print(',');
      file.print(gpsAge);
    } else {
      file.print(",,,,,");
    }
    file.println();
    logRows++;
  }

  file.close();
  WiFi.scanDelete();
  notifyPhone("Logged " + String(networkCount) + " networks; total rows " + String(logRows));
}

void dumpLogToSerial() {
  File file = LittleFS.open(kLogPath, "r");
  if (!file) {
    Serial.println("No log file found");
    return;
  }

  Serial.println("----- BEGIN WIFI RANGE CSV -----");
  while (file.available()) {
    Serial.write(file.read());
  }
  Serial.println("----- END WIFI RANGE CSV -----");
  file.close();
}

void eraseLog() {
  LittleFS.remove(kLogPath);
  ensureLogHeader();
  logRows = 0;
  notifyPhone("Log erased");
}

void handleCommand(String payload) {
  payload.trim();
  if (payload.length() == 0) {
    return;
  }

  if (payload.equalsIgnoreCase("DUMP")) {
    dumpLogToSerial();
    notifyPhone("Log dumped to USB serial");
    return;
  }

  if (payload.equalsIgnoreCase("ERASE")) {
    eraseLog();
    return;
  }

  if (parseGpsJson(payload)) {
    notifyPhone("GPS fix " + String(gps.lat, 6) + "," + String(gps.lon, 6));
    return;
  }

  notifyPhone("Unknown input. Send GPS JSON, DUMP, or ERASE.");
}

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *) override {
    bleConnected = true;
    Serial.println("BLE phone connected");
  }

  void onDisconnect(BLEServer *server) override {
    bleConnected = false;
    Serial.println("BLE phone disconnected");
    server->getAdvertising()->start();
  }
};

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    const std::string value = characteristic->getValue();
    handleCommand(String(value.c_str()));
  }
};

void setupBle() {
  BLEDevice::init(kDeviceName);
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  BLEService *service = server->createService(kUartServiceUuid);

  txCharacteristic = service->createCharacteristic(
      kUartTxUuid,
      BLECharacteristic::PROPERTY_NOTIFY);
  txCharacteristic->addDescriptor(new BLE2902());

  BLECharacteristic *rxCharacteristic = service->createCharacteristic(
      kUartRxUuid,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rxCharacteristic->setCallbacks(new RxCallbacks());

  service->start();
  server->getAdvertising()->addServiceUUID(kUartServiceUuid);
  server->getAdvertising()->start();
}

void printStatus() {
  Serial.print("Rows: ");
  Serial.print(logRows);
  Serial.print(" | BLE: ");
  Serial.print(bleConnected ? "connected" : "waiting");
  Serial.print(" | GPS: ");
  if (gps.valid) {
    Serial.print(gps.lat, 6);
    Serial.print(',');
    Serial.print(gps.lon, 6);
    Serial.print(" age ");
    Serial.print(millis() - gps.updatedMs);
    Serial.println(" ms");
  } else {
    Serial.println("none");
  }
}

} // namespace

void setup() {
  Serial.begin(115200);
  delay(300);

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
  }
  ensureLogHeader();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(100);

  setupBle();

  Serial.println();
  Serial.println("WiFiRangeLogger ready");
  Serial.println("Send GPS JSON over BLE: {\"lat\":-23.55,\"lon\":-46.63,\"accuracy\":8}");
  Serial.println("Send DUMP over BLE or USB serial monitor command line to inspect CSV.");
}

void loop() {
  const uint32_t now = millis();

  if (Serial.available()) {
    handleCommand(Serial.readStringUntil('\n'));
  }

  if (now - lastScanMs >= kScanIntervalMs) {
    lastScanMs = now;
    appendScanRows();
  }

  if (now - lastStatusMs >= kStatusIntervalMs) {
    lastStatusMs = now;
    printStatus();
  }
}
