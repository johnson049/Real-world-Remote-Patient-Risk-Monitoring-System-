#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_task_wdt.h"
#include <FS.h>
#include <SPIFFS.h>
#include <Preferences.h>

// Declare this type before Arduino generates function prototypes.
struct SamplingConfig { int intervalSeconds; bool autoMode; };
// ============================================================
// TASK 4 - INTELLIGENT REMOTE BED ELEVATION CONTROL
// ============================================================
// ---------------------------
// TASK 3 medication hardware
// ---------------------------
#define MEDICATION_LED_PIN       23
#define MEDICATION_BUZZER_PIN    18
#define MEDICATION_MIN_DOSAGE     0
#define MEDICATION_MAX_DOSAGE   100
#define MEDICATION_NORMAL_MAX    50
#define MEDICATION_WARNING_MAX   80
#define MEDICATION_CRITICAL_MIN  81
#define MEDICATION_RATE_LIMIT    10
#define MEDICATION_RATE_INTERVAL 1000
#define MEDICATION_ALARM_INTERVAL 150
// ---------------------------
// TASK 4 bed hardware
// ---------------------------
#define BED_SERVO_PIN            13
#define BED_MIN_ANGLE             0
#define BED_MAX_ANGLE            90
#define BED_SLEEP_ANGLE          10
#define BED_BREATHING_ANGLE      45
#define BED_EMERGENCY_ANGLE      90
#define BED_STEP_DEGREES          1
#define BED_STEP_INTERVAL_MS     40
// ---------------------------
// Existing Task 2 hardware
// ---------------------------
#define BODY_TEMP_SENSOR  14
#define ECG_SENSOR        34
#define MOTION_DETECTOR   12
#define ROOM_TEMP_SENSOR  27
#define OXYGEN_SENSOR     35
#define AQI_SENSOR        39
#define BUZZER_PIN_1       4
#define BUZZER_PIN_2      16
#define BUZZER_PIN_3      17
#define BUZZER_PIN_4       5
#define SW_TEMP           32
#define SW_HR             33
#define SW_SPO2           25
#define SW_MOTION         26
// ---------------------------
// OLED
// ---------------------------
#define SCREEN_WIDTH       128
#define SCREEN_HEIGHT       64
#define OLED_RESET          -1
#define OLED_ADDRESS       0x3C
// ---------------------------
// WiFi / Adafruit IO
// ---------------------------
#define WIFI_SSID         "Wokwi-GUEST"
#define WIFI_PASS         ""
#define IO_USERNAME       "jssg1234"
#define IO_KEY            "aio_KEY"
#define AIO_SERVER        "io.adafruit.com"
#define AIO_SERVERPORT    1883
// ============================================================
// FEEDS
// ============================================================
// Medical
#define MEDICAL_BODY_TEMP_FEED \
  IO_USERNAME "/feeds/medical-body-temperature"
#define MEDICAL_HR_FEED \
  IO_USERNAME "/feeds/medical-heart-rate"
#define MEDICAL_SPO2_FEED \
  IO_USERNAME "/feeds/medical-spo2"
#define MEDICAL_ALERT_FEED \
  IO_USERNAME "/feeds/medical-alert"
// Facility
#define FACILITY_ROOM_TEMP_FEED \
  IO_USERNAME "/feeds/facility-room-temperature"
#define FACILITY_OXYGEN_FEED \
  IO_USERNAME "/feeds/facility-oxygen-level"
#define FACILITY_AQI_FEED \
  IO_USERNAME "/feeds/facility-aqi"
#define FACILITY_ALERT_FEED \
  IO_USERNAME "/feeds/facility-alert"
// Medication
#define MEDICATION_CURRENT_FEED \
  IO_USERNAME "/feeds/medication-current"
#define MEDICATION_ALERT_FEED \
  IO_USERNAME "/feeds/medication-alert"
// Task 4
#define BED_CONTROL_FEED \
  IO_USERNAME "/feeds/bed-control"
#define SAMPLING_RATE_FEED \
  IO_USERNAME "/feeds/sampling-rate"
#define SAMPLING_MODE_FEED \
  IO_USERNAME "/feeds/sampling-mode"
// Task 6: historical offline snapshots (create this feed in Adafruit IO)
#define OFFLINE_LOG_FEED \
  IO_USERNAME "/feeds/offline-log"
// ============================================================
// MQTT
// ============================================================
WiFiClient client;
Adafruit_MQTT_Client mqtt(
  &client,
  AIO_SERVER,
  AIO_SERVERPORT,
  IO_USERNAME,
  IO_KEY
);
// Publishers
Adafruit_MQTT_Publish medicalBodyTempFeed(
  &mqtt, MEDICAL_BODY_TEMP_FEED
);
Adafruit_MQTT_Publish medicalHRFeed(
  &mqtt, MEDICAL_HR_FEED
);
Adafruit_MQTT_Publish medicalSpO2Feed(
  &mqtt, MEDICAL_SPO2_FEED
);
Adafruit_MQTT_Publish medicalAlertFeed(
  &mqtt, MEDICAL_ALERT_FEED
);
Adafruit_MQTT_Publish facilityRoomTempFeed(
  &mqtt, FACILITY_ROOM_TEMP_FEED
);
Adafruit_MQTT_Publish facilityOxygenFeed(
  &mqtt, FACILITY_OXYGEN_FEED
);
Adafruit_MQTT_Publish facilityAQIFeed(
  &mqtt, FACILITY_AQI_FEED
);
Adafruit_MQTT_Publish facilityAlertFeed(
  &mqtt, FACILITY_ALERT_FEED
);
Adafruit_MQTT_Publish medicationAlertFeed(
  &mqtt, MEDICATION_ALERT_FEED
);
// Task 4: one combined input feed
// Task 6: historical snapshots are uploaded as CSV text to this feed.
Adafruit_MQTT_Publish offlineLogFeed(
  &mqtt, OFFLINE_LOG_FEED
);
Adafruit_MQTT_Subscribe bedControlFeed(
  &mqtt, BED_CONTROL_FEED
);
Adafruit_MQTT_Subscribe samplingRateFeed(
  &mqtt, SAMPLING_RATE_FEED
);
Adafruit_MQTT_Subscribe samplingModeFeed(
  &mqtt, SAMPLING_MODE_FEED
);
// Task 3 subscriptions
Adafruit_MQTT_Subscribe medicationCurrentControlFeed(
  &mqtt, MEDICATION_CURRENT_FEED
);
Adafruit_MQTT_Subscribe medicationAlertControlFeed(
  &mqtt, MEDICATION_ALERT_FEED
);
// ============================================================
// SENSOR / OLED / SERVO
// ============================================================
OneWire bodyTempWire(BODY_TEMP_SENSOR);
DallasTemperature bodyTempSensor(&bodyTempWire);
OneWire roomTempWire(ROOM_TEMP_SENSOR);
DallasTemperature roomTempSensor(&roomTempWire);
Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  OLED_RESET
);
Servo bedServo;
// ============================================================
// DATA STRUCTURES
// ============================================================
struct MedicalData {
  float bodyTemperature;
  int heartRate;
  int spo2;
};
struct FacilityData {
  float roomTemperature;
  float oxygenLevel;
  int aqi;
};
struct MedicationData {
  int requestedDosage;
  int currentDosage;
  char status[16];
  char alert[64];
  bool confirmationRequired;
  bool criticalConfirmed;
};
enum BedMode {
  BED_MANUAL,
  BED_SLEEP,
  BED_BREATHING,
  BED_EMERGENCY,
  BED_AUTO
};
struct BedData {
  int requestedAngle;
  int targetAngle;
  int currentAngle;
  BedMode mode;
  bool moving;
};
struct SystemData {
  MedicalData medical;
  FacilityData facility;
  MedicationData medication;
  BedData bed;
  bool motion;
  int ecg;
  char medicalAlert[64];
  char facilityAlert[64];
};
SystemData systemData;
// ============================================================
// ============================================================
// TASK 6 - OFFLINE DETECTION AND PERSISTENT BUFFER
// ============================================================
enum ConnectivityState {
  STATE_ONLINE,
  STATE_DEGRADED,
  STATE_OFFLINE
};
volatile ConnectivityState connectivityState = STATE_OFFLINE;
volatile bool mqttPublishHealthy = true;
static const char *OFFLINE_FILE = "/offline.csv";
static const char *OFFLINE_TEMP_FILE = "/offline.tmp";
static const char *OFFLINE_BACKUP_FILE = "/offline.bak";
static const size_t OFFLINE_MAX_BYTES = 30000; // Keep storage bounded.
static const uint32_t TELEMETRY_INTERVAL_MS = 30000; // Leave capacity for backlog synchronization.
static const uint32_t MAX_RETRY_DELAY_MS = 30000;
bool offlineStorageReady = false;
volatile bool offlineBacklogPending = false;
uint32_t wifiRetryDelayMs = 1000;
uint32_t mqttRetryDelayMs = 1000;
uint32_t nextWifiAttemptMs = 0;
uint32_t nextMqttAttemptMs = 0;
// Temporary Task 6 test switch: Serial Monitor commands mqtt_off / mqtt_on.
bool mqttTestDisconnect = false;
uint32_t lastOfflineSyncMs = 0;
// Network alerts share the existing medication buzzer (GPIO 18).
// medicationTask remains the only task that writes to that physical pin;
// a critical medication alarm has priority over the network beep pattern.
volatile bool networkBuzzerOn = false;
int networkPatternCode = -1; // 0=offline (3 beeps), 1=degraded (2), 2=online (long beep)
uint32_t networkPatternStartMs = 0;
const char *getConnectivityName(ConnectivityState state) {
  switch (state) {
    case STATE_ONLINE: return "ONLINE";
    case STATE_DEGRADED: return "DEGRADED";
    case STATE_OFFLINE: return "OFFLINE";
    default: return "UNKNOWN";
  }
}
void startNetworkBuzzerPattern(ConnectivityState state) {
  if (state == STATE_OFFLINE) networkPatternCode = 0;
  else if (state == STATE_DEGRADED) networkPatternCode = 1;
  else networkPatternCode = 2;
  networkPatternStartMs = millis();
  networkBuzzerOn = false;
}
void serviceNetworkBuzzer() {
  if (networkPatternCode < 0) {
    networkBuzzerOn = false;
    return;
  }
  uint32_t elapsed = millis() - networkPatternStartMs;
  bool on = false;
  uint32_t duration = 0;
  if (networkPatternCode == 0) {        // Wi-Fi offline: 3 short beeps
    duration = 600;
    on = (elapsed < duration) && ((elapsed % 200) < 100);
  } else if (networkPatternCode == 1) { // MQTT degraded: 2 short beeps
    duration = 400;
    on = (elapsed < duration) && ((elapsed % 200) < 100);
  } else {                              // Recovered: one longer beep
    duration = 400;
    on = elapsed < duration;
  }
  networkBuzzerOn = on;
  if (elapsed >= duration) {
    networkBuzzerOn = false;
    networkPatternCode = -1;
  }
}
void updateConnectivityState() {
  ConnectivityState newState;
  if (WiFi.status() != WL_CONNECTED) {
    newState = STATE_OFFLINE;
  } else if (!mqtt.connected() || !mqttPublishHealthy) {
    newState = STATE_DEGRADED;
  } else {
    newState = STATE_ONLINE;
  }
  if (newState != connectivityState) {
    ConnectivityState previous = connectivityState;
    connectivityState = newState;
    Serial.print("TASK 6 | Connectivity: ");
    Serial.print(getConnectivityName(previous));
    Serial.print(" -> ");
    Serial.println(getConnectivityName(newState));
    startNetworkBuzzerPattern(newState);
  }
}
bool initializeOfflineStorage() {
  // Format only on first initialization. Never auto-format a previously initialized filesystem.
  Preferences prefs;
  bool prefsReady = prefs.begin("task6store", false);
  bool previouslyInitialized = prefsReady && prefs.getBool("spiffs_init", false);
  bool mounted = SPIFFS.begin(false);
  if (!mounted && !previouslyInitialized) {
    Serial.println("TASK 6 | First-time SPIFFS format attempt");
    mounted = SPIFFS.begin(true);
  }
  if (!mounted) {
    Serial.println("TASK 6 | SPIFFS mount failed; refusing to format existing storage");
    if (prefsReady) prefs.end();
    return false;
  }

  // Task 6 diagnostic: verify SPIFFS can write and read.
  File testFile = SPIFFS.open("/task6_test.txt", FILE_WRITE);
  if (!testFile) {
    Serial.println("TASK 6 | ERROR: SPIFFS write test failed");
    if (prefsReady) prefs.end();
    return false;
  }
  testFile.println("SPIFFS_TEST_OK");
  testFile.close();

  File verifyFile = SPIFFS.open("/task6_test.txt", FILE_READ);
  if (!verifyFile) {
    Serial.println("TASK 6 | ERROR: SPIFFS read test failed");
    if (prefsReady) prefs.end();
    return false;
  }
  String testResult = verifyFile.readStringUntil('\n');
  verifyFile.close();
  testResult.trim();
  Serial.printf("TASK 6 | SPIFFS test read: [%s], length=%u\n",
                testResult.c_str(), (unsigned)testResult.length());
  if (testResult != "SPIFFS_TEST_OK") {
    Serial.println("TASK 6 | ERROR: SPIFFS verification failed");
    SPIFFS.remove("/task6_test.txt");
    if (prefsReady) prefs.end();
    return false;
  }
  SPIFFS.remove("/task6_test.txt");
  Serial.println("TASK 6 | SPIFFS write/read test PASSED");
  if (prefsReady) {
    prefs.putBool("spiffs_init", true);
    prefs.end();
  }
  if (!SPIFFS.exists(OFFLINE_FILE) && SPIFFS.exists(OFFLINE_BACKUP_FILE)) {
    if (!SPIFFS.rename(OFFLINE_BACKUP_FILE, OFFLINE_FILE)) {
      Serial.println("TASK 6 | Could not recover backup offline log");
      return false;
    }
  }
  if (!SPIFFS.exists(OFFLINE_FILE)) {
    File created = SPIFFS.open(OFFLINE_FILE, FILE_WRITE);
    if (!created) {
      Serial.println("TASK 6 | Could not create offline log");
      return false;
    }
    created.close();
  }
  File queued = SPIFFS.open(OFFLINE_FILE, FILE_READ);
  size_t queuedBytes = queued ? queued.size() : 0;
  if (queued) queued.close();
  offlineBacklogPending = queuedBytes > 0;
  Serial.printf("TASK 6 | SPIFFS ready; queued bytes: %u\n", (unsigned)queuedBytes);
  return true;
}
bool appendOfflineSnapshot(const SystemData &data) {
  if (!offlineStorageReady) {
    Serial.println("TASK 6 | WARNING: no persistent storage; snapshot not saved");
    return false;
  }
  File current = SPIFFS.open(OFFLINE_FILE, FILE_READ);
  size_t currentSize = current ? current.size() : 0;
  if (current) current.close();
  // Never silently delete older records when storage is full.
  if (currentSize >= OFFLINE_MAX_BYTES) {
    Serial.println("TASK 6 | OFFLINE LOG FULL; oldest records preserved");
    return false;
  }
  File file = SPIFFS.open(OFFLINE_FILE, FILE_APPEND);
  if (!file) {
    Serial.println("TASK 6 | ERROR opening offline log for append");
    return false;
  }
  // CSV schema: uptime_ms,temp_C,heart_rate,spo2,room_C,oxygen,aqi,motion,medical_alert,facility_alert
  // millis() is uptime, not a real calendar timestamp; no RTC/NTP timestamp is assumed.
  file.printf("%lu,%.2f,%d,%d,%.2f,%.2f,%d,%d,%s,%s\n",
              (unsigned long)millis(),
              data.medical.bodyTemperature,
              data.medical.heartRate,
              data.medical.spo2,
              data.facility.roomTemperature,
              data.facility.oxygenLevel,
              data.facility.aqi,
              data.motion ? 1 : 0,
              data.medicalAlert,
              data.facilityAlert);
  bool ok = !file.getWriteError();
  file.close();
  if (ok) { offlineBacklogPending = true; Serial.println("TASK 6 | Sensor snapshot stored locally"); }
  else Serial.println("TASK 6 | ERROR writing offline snapshot");
  return ok;
}
bool syncOneOfflineSnapshot() {
  if (!offlineStorageReady || !mqtt.connected()) return false;
  File source = SPIFFS.open(OFFLINE_FILE, FILE_READ);
  if (!source) return false;
  if (source.size() == 0) {
    source.close();
    return false;
  }
  String record = source.readStringUntil('\n');
  record.trim();
  if (record.length() == 0) {
    source.close();
    return false;
  }
  // Publish first. Do not remove the record if the cloud publish fails.
  if (!offlineLogFeed.publish(record.c_str())) {
    source.close();
    Serial.println("TASK 6 | Backlog publish failed; record retained");
    mqttPublishHealthy = false;
    mqtt.disconnect();
    nextMqttAttemptMs = millis() + mqttRetryDelayMs;
    mqttRetryDelayMs = min(mqttRetryDelayMs * 2, MAX_RETRY_DELAY_MS);
    return false;
  }
  // Build a replacement containing every record except the one just uploaded.
  File temp = SPIFFS.open(OFFLINE_TEMP_FILE, FILE_WRITE);
  if (!temp) {
    source.close();
    Serial.println("TASK 6 | Could not create sync temp file; source retained");
    return false;
  }
  while (source.available()) {
    String remaining = source.readStringUntil('\n');
    if (remaining.length() > 0) temp.println(remaining);
  }
  source.close();
  bool tempOk = !temp.getWriteError();
  temp.close();
  if (!tempOk) {
    SPIFFS.remove(OFFLINE_TEMP_FILE);
    Serial.println("TASK 6 | Temp write failed; source log retained");
    return false;
  }
  // Preserve the original until the replacement is in place. If power is lost,
  // initializeOfflineStorage() can restore the backup on the next boot.
  SPIFFS.remove(OFFLINE_BACKUP_FILE);
  if (!SPIFFS.rename(OFFLINE_FILE, OFFLINE_BACKUP_FILE)) {
    SPIFFS.remove(OFFLINE_TEMP_FILE);
    Serial.println("TASK 6 | Could not back up source log; source retained");
    return false;
  }
  if (!SPIFFS.rename(OFFLINE_TEMP_FILE, OFFLINE_FILE)) {
    SPIFFS.rename(OFFLINE_BACKUP_FILE, OFFLINE_FILE);
    Serial.println("TASK 6 | Replacement failed; attempted to restore source log");
    return false;
  }
  SPIFFS.remove(OFFLINE_BACKUP_FILE);
  File remainingFile = SPIFFS.open(OFFLINE_FILE, FILE_READ);
  offlineBacklogPending = remainingFile && remainingFile.size() > 0;
  if (remainingFile) remainingFile.close();
  Serial.println("TASK 6 | One queued snapshot synchronized");
  return true;
}
// TASK 2 THRESHOLDS
// ============================================================
#define BODY_TEMP_HIGH  38.0
#define HEART_RATE_LOW  60
#define HEART_RATE_HIGH 300
#define SPO2_LOW        85
#define ROOM_TEMP_LOW   18.0
#define ROOM_TEMP_HIGH  30.0
#define OXYGEN_LOW      19.5
#define OXYGEN_HIGH     23.5
#define AQI_HIGH        100
// ============================================================
// RTOS OBJECTS
// ============================================================
QueueHandle_t medicalQueue;
QueueHandle_t facilityQueue;
QueueHandle_t environmentQueue;
QueueHandle_t ecgQueue;
// TASK 5 adaptive sampling configuration
#define DEFAULT_SAMPLING_SECONDS 10
#define STABLE_SAMPLING_SECONDS 30
#define FAST_SAMPLING_SECONDS 5
#define MAX_SAMPLING_SECONDS 60
QueueHandle_t medicalSamplingQueue;
QueueHandle_t facilitySamplingQueue;
SamplingConfig currentSamplingConfig = { DEFAULT_SAMPLING_SECONDS, false };
volatile bool patientAbnormalForSampling = false;
SemaphoreHandle_t dataMutex;
SemaphoreHandle_t displayMutex;
SemaphoreHandle_t displaySemaphore;
SemaphoreHandle_t alertSemaphore;
// ============================================================
// WATCHDOG
// ============================================================
#define TASK_COUNT 10
#define WATCHDOG_CHECK_INTERVAL_MS 1000
#define TASK_TIMEOUT_MS 4000
enum TaskID {
  TASK_MEDICAL_SENSOR = 0,
  TASK_FACILITY_SENSOR,
  TASK_ECG,
  TASK_ENVIRONMENT,
  TASK_DATA_MANAGER,
  TASK_DISPLAY,
  TASK_ALERT,
  TASK_MQTT,
  TASK_MEDICATION,
  TASK_BED
};
volatile uint32_t taskHeartbeat[TASK_COUNT] = {0};
// ============================================================
// MEDICATION STATE
// ============================================================
volatile int medicationRequestedDosage = 0;
volatile int medicationCurrentDosage = 0;
volatile bool medicationCriticalConfirmed = false;
volatile bool medicationConfirmationRequired = false;
volatile uint32_t medicationLastIncrease = 0;
volatile uint32_t medicationLastAlarm = 0;
volatile bool medicationAlarmState = false;
// ============================================================
// BED STATE
// ============================================================
volatile int bedRequestedAngle = BED_SLEEP_ANGLE;
volatile int bedTargetAngle = BED_SLEEP_ANGLE;
volatile int bedCurrentAngle = BED_SLEEP_ANGLE;
volatile BedMode bedMode = BED_MANUAL;
volatile bool bedMoving = false;
volatile uint32_t bedLastStep = 0;
// ============================================================
// DISPLAY
// ============================================================
volatile int displayMode = 0;
// 0 = Medical
// 1 = Facility
// 2 = Medication
// 3 = Bed
// ============================================================
// UTILITY
// ============================================================
float readOxygenLevel() {
  int rawValue = analogRead(OXYGEN_SENSOR);
  return 18.0 + ((float)rawValue / 4095.0) * 7.0;
}
int readAQI() {
  return map(analogRead(AQI_SENSOR), 0, 4095, 0, 500);
}
const char* getBedModeName() {
  switch (bedMode) {
    case BED_MANUAL:    return "MANUAL";
    case BED_SLEEP:     return "SLEEP";
    case BED_BREATHING: return "BREATHING";
    case BED_EMERGENCY: return "EMERGENCY";
    case BED_AUTO:      return "AUTO";
    default:            return "UNKNOWN";
  }
}
// ============================================================
// MEDICATION STATUS
// ============================================================
void updateMedicationStatus() {
  if (medicationCurrentDosage <= MEDICATION_NORMAL_MAX) {
    strcpy(systemData.medication.status, "NORMAL");
  }
  else if (medicationCurrentDosage <= MEDICATION_WARNING_MAX) {
    strcpy(systemData.medication.status, "WARNING");
  }
  else {
    strcpy(systemData.medication.status, "CRITICAL");
  }
  systemData.medication.requestedDosage =
    medicationRequestedDosage;
  systemData.medication.currentDosage =
    medicationCurrentDosage;
  systemData.medication.confirmationRequired =
    medicationConfirmationRequired;
  systemData.medication.criticalConfirmed =
    medicationCriticalConfirmed;
}
// ============================================================
// MEDICATION CONTROL
// ============================================================
void requestMedicationDosage(int requested) {
  requested = constrain(
    requested,
    MEDICATION_MIN_DOSAGE,
    MEDICATION_MAX_DOSAGE
  );
  medicationRequestedDosage = requested;
  if (
    requested >= MEDICATION_CRITICAL_MIN &&
    !medicationCriticalConfirmed
  ) {
    medicationConfirmationRequired = true;
    if (
      xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50))
      == pdTRUE
    ) {
      strcpy(
        systemData.medication.alert,
        "CRITICAL: CONFIRM REQUIRED"
      );
      updateMedicationStatus();
      xSemaphoreGive(dataMutex);
    }
    return;
  }
  medicationConfirmationRequired = false;
  if (requested < medicationCurrentDosage) {
    medicationCurrentDosage = requested;
  }
  else if (requested > medicationCurrentDosage) {
    uint32_t now = millis();
    if (
      now - medicationLastIncrease >=
      MEDICATION_RATE_INTERVAL
    ) {
      int increase =
        requested - medicationCurrentDosage;
      if (increase > MEDICATION_RATE_LIMIT) {
        increase = MEDICATION_RATE_LIMIT;
      }
      medicationCurrentDosage += increase;
      medicationLastIncrease = now;
    }
  }
  if (
    xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50))
    == pdTRUE
  ) {
    if (
      medicationCurrentDosage >=
      MEDICATION_CRITICAL_MIN
    ) {
      strcpy(
        systemData.medication.alert,
        "CRITICAL DOSAGE ACTIVE"
      );
    }
    else if (
      medicationCurrentDosage >
      MEDICATION_NORMAL_MAX
    ) {
      strcpy(
        systemData.medication.alert,
        "WARNING DOSAGE"
      );
    }
    else {
      strcpy(
        systemData.medication.alert,
        "Dosage normal"
      );
    }
    updateMedicationStatus();
    xSemaphoreGive(dataMutex);
  }
  Serial.print("Medication request: ");
  Serial.print(requested);
  Serial.print(" mg/hr -> current: ");
  Serial.print(medicationCurrentDosage);
  Serial.println(" mg/hr");
}
// ============================================================
// TASK 4 - BED TARGET LOGIC
// ============================================================
void updateBedTarget() {
  if (bedMode == BED_MANUAL) {
    bedTargetAngle = bedRequestedAngle;
  }
  else if (bedMode == BED_SLEEP) {
    bedTargetAngle = BED_SLEEP_ANGLE;
  }
  else if (bedMode == BED_BREATHING) {
    bedTargetAngle = BED_BREATHING_ANGLE;
  }
  else if (bedMode == BED_EMERGENCY) {
    bedTargetAngle = BED_EMERGENCY_ANGLE;
  }
  else if (bedMode == BED_AUTO) {
    // Critical condition has priority.
    if (
      (
        systemData.medical.heartRate > HEART_RATE_HIGH ||
        systemData.medical.heartRate < HEART_RATE_LOW
      )
      &&
      systemData.medical.heartRate != 0
    ) {
      bedTargetAngle = BED_EMERGENCY_ANGLE;
    }
    else if (
      systemData.medical.bodyTemperature >
      BODY_TEMP_HIGH
    ) {
      bedTargetAngle = BED_EMERGENCY_ANGLE;
    }
    // Low SpO2 -> breathing support.
    else if (
      systemData.medical.spo2 > 0 &&
      systemData.medical.spo2 < SPO2_LOW
    ) {
      bedTargetAngle = BED_BREATHING_ANGLE;
    }
    // Normal condition -> sleeping position.
    else {
      bedTargetAngle = BED_SLEEP_ANGLE;
    }
  }
  bedTargetAngle = constrain(
    bedTargetAngle,
    BED_MIN_ANGLE,
    BED_MAX_ANGLE
  );
  if (
    xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10))
    == pdTRUE
  ) {
    systemData.bed.requestedAngle =
      bedRequestedAngle;
    systemData.bed.targetAngle =
      bedTargetAngle;
    systemData.bed.currentAngle =
      bedCurrentAngle;
    systemData.bed.mode =
      bedMode;
    systemData.bed.moving =
      bedMoving;
    xSemaphoreGive(dataMutex);
  }
}
// ============================================================
// TASK 2 + TASK 5 - MEDICAL SENSOR TASK
// ============================================================
int getAdaptiveInterval(const MedicalData &data, bool autoMode, int manualInterval) {
  manualInterval = constrain(manualInterval, 5, MAX_SAMPLING_SECONDS);
  if (!autoMode) return manualInterval;
  bool abnormal = false;
  if (!digitalRead(SW_TEMP) && data.bodyTemperature >= BODY_TEMP_HIGH) abnormal = true;
  if (!digitalRead(SW_HR) && (data.heartRate < HEART_RATE_LOW || data.heartRate > HEART_RATE_HIGH)) abnormal = true;
  if (!digitalRead(SW_SPO2) && data.spo2 < SPO2_LOW) abnormal = true;
  patientAbnormalForSampling = abnormal;
  return abnormal ? FAST_SAMPLING_SECONDS : STABLE_SAMPLING_SECONDS;
}
// Wait in short slices to keep watchdog heartbeats alive and receive dashboard updates promptly.
bool waitForSamplingConfig(QueueHandle_t queue, SamplingConfig &config, int taskId, int intervalSeconds) {
  uint32_t start = millis();
  uint32_t waitMs = (uint32_t)constrain(intervalSeconds, 5, MAX_SAMPLING_SECONDS) * 1000UL;
  while ((uint32_t)(millis() - start) < waitMs) {
    taskHeartbeat[taskId] = millis();
    uint32_t remaining = waitMs - (uint32_t)(millis() - start);
    TickType_t slice = pdMS_TO_TICKS(remaining > 250 ? 250 : remaining);
    if (slice < 1) slice = 1;
    SamplingConfig updated;
    if (xQueueReceive(queue, &updated, slice) == pdTRUE) { config = updated; return true; }
  }
  return false;
}
void medicalSensorTask(void *param) {
  MedicalData data = {};
  SamplingConfig config = currentSamplingConfig;
  while (1) {
    taskHeartbeat[TASK_MEDICAL_SENSOR] = millis();
    xQueuePeek(medicalSamplingQueue, &config, 0);
    data.bodyTemperature = 0.0; data.heartRate = 0; data.spo2 = 0;
    if (!digitalRead(SW_TEMP)) {
      bodyTempSensor.requestTemperatures();
      data.bodyTemperature = bodyTempSensor.getTempCByIndex(0);
    }
    if (!digitalRead(SW_HR)) data.heartRate = random(45, 351);
    if (!digitalRead(SW_SPO2)) data.spo2 = random(60, 101);
    xQueueOverwrite(medicalQueue, &data);
    int intervalSeconds = getAdaptiveInterval(data, config.autoMode, config.intervalSeconds);
    Serial.printf("TASK 5 | Medical interval: %d sec | Mode: %s\n", intervalSeconds, config.autoMode ? "AUTO" : "MANUAL");
    waitForSamplingConfig(medicalSamplingQueue, config, TASK_MEDICAL_SENSOR, intervalSeconds);
  }
}
// ============================================================
// TASK 2 + TASK 5 - FACILITY SENSOR TASK
// ============================================================
void facilitySensorTask(void *param) {
  FacilityData data = {};
  SamplingConfig config = currentSamplingConfig;
  while (1) {
    taskHeartbeat[TASK_FACILITY_SENSOR] = millis();
    xQueuePeek(facilitySamplingQueue, &config, 0);
    roomTempSensor.requestTemperatures();
    data.roomTemperature = roomTempSensor.getTempCByIndex(0);
    data.oxygenLevel = readOxygenLevel();
    data.aqi = readAQI();
    xQueueOverwrite(facilityQueue, &data);
    int intervalSeconds = config.autoMode
      ? (patientAbnormalForSampling ? FAST_SAMPLING_SECONDS : STABLE_SAMPLING_SECONDS)
      : constrain(config.intervalSeconds, 5, MAX_SAMPLING_SECONDS);
    Serial.printf("TASK 5 | Facility interval: %d sec | Mode: %s\n", intervalSeconds, config.autoMode ? "AUTO" : "MANUAL");
    waitForSamplingConfig(facilitySamplingQueue, config, TASK_FACILITY_SENSOR, intervalSeconds);
  }
}
// ============================================================
// TASK 2 - ECG TASK
// ============================================================
void ecgTask(void *param) {
  int ecgValue;
  while (1) {
    taskHeartbeat[TASK_ECG] = millis();
    ecgValue = analogRead(ECG_SENSOR);
    xQueueOverwrite(ecgQueue, &ecgValue);
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
 // TASK 2 - ENVIRONMENT / MOTION SENSOR TASK
void environmentTask(void *param) {
  bool motionDetected = false;
  while (1) {
    taskHeartbeat[TASK_ENVIRONMENT] = millis();
    motionDetected = digitalRead(MOTION_DETECTOR) == HIGH;
    // Send motion status to the data manager task
    if (environmentQueue != nullptr) {
      xQueueOverwrite(environmentQueue, &motionDetected);
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
// ============================================================
// TASK 3 - MEDICATION TASK
// ============================================================
void medicationTask(void *param) {
  pinMode(MEDICATION_LED_PIN, OUTPUT);
  pinMode(MEDICATION_BUZZER_PIN, OUTPUT);
  digitalWrite(MEDICATION_LED_PIN, LOW);
  digitalWrite(MEDICATION_BUZZER_PIN, LOW);
  while (1) {
    taskHeartbeat[TASK_MEDICATION] = millis();
    if (
      medicationRequestedDosage >
      medicationCurrentDosage &&
      !medicationConfirmationRequired
    ) {
      requestMedicationDosage(
        medicationRequestedDosage
      );
    }
    if (
      medicationCurrentDosage >=
      MEDICATION_CRITICAL_MIN
    ) {
      uint32_t now = millis();
      if (
        now - medicationLastAlarm >=
        MEDICATION_ALARM_INTERVAL
      ) {
        medicationLastAlarm = now;
        medicationAlarmState =
          !medicationAlarmState;
        digitalWrite(
          MEDICATION_LED_PIN,
          medicationAlarmState
        );
        digitalWrite(
          MEDICATION_BUZZER_PIN,
          medicationAlarmState
        );
      }
    }
    else {
      medicationAlarmState = false;
      digitalWrite(
        MEDICATION_LED_PIN,
        LOW
      );
      digitalWrite(
        MEDICATION_BUZZER_PIN,
        networkBuzzerOn ? HIGH : LOW
      );
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
// ============================================================
// TASK 4 - BED ELEVATION TASK
// ============================================================
void bedElevationTask(void *param) {
  while (1) {
    taskHeartbeat[TASK_BED] = millis();
    // AUTO mode continuously recalculates target.
    if (bedMode == BED_AUTO) {
      updateBedTarget();
    }
    uint32_t now = millis();
    if (
      now - bedLastStep >=
      BED_STEP_INTERVAL_MS
    ) {
      if (
        bedCurrentAngle <
        bedTargetAngle
      ) {
        bedCurrentAngle +=
          BED_STEP_DEGREES;
        if (
          bedCurrentAngle >
          bedTargetAngle
        ) {
          bedCurrentAngle =
            bedTargetAngle;
        }
        bedServo.write(
          bedCurrentAngle
        );
        bedMoving = true;
        bedLastStep = now;
      }
      else if (
        bedCurrentAngle >
        bedTargetAngle
      ) {
        bedCurrentAngle -=
          BED_STEP_DEGREES;
        if (
          bedCurrentAngle <
          bedTargetAngle
        ) {
          bedCurrentAngle =
            bedTargetAngle;
        }
        bedServo.write(
          bedCurrentAngle
        );
        bedMoving = true;
        bedLastStep = now;
      }
      else {
        bedMoving = false;
      }
      if (
        xSemaphoreTake(
          dataMutex,
          pdMS_TO_TICKS(10)
        ) == pdTRUE
      ) {
        systemData.bed.currentAngle =
          bedCurrentAngle;
        systemData.bed.targetAngle =
          bedTargetAngle;
        systemData.bed.mode =
          bedMode;
        systemData.bed.moving =
          bedMoving;
        xSemaphoreGive(dataMutex);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
// ============================================================
// TASK 2 - MEDICAL ALERT
// ============================================================
void updateMedicalAlert() {
  strcpy(
    systemData.medicalAlert,
    "OK"
  );
  if (
    !digitalRead(SW_TEMP) &&
    systemData.medical.bodyTemperature >
    BODY_TEMP_HIGH
  ) {
    strcpy(
      systemData.medicalAlert,
      "High body temperature"
    );
    return;
  }
  if (
    !digitalRead(SW_HR) &&
    systemData.medical.heartRate <
    HEART_RATE_LOW
  ) {
    strcpy(
      systemData.medicalAlert,
      "HR below normal"
    );
    return;
  }
  if (
    !digitalRead(SW_HR) &&
    systemData.medical.heartRate >
    HEART_RATE_HIGH
  ) {
    strcpy(
      systemData.medicalAlert,
      "HR above normal"
    );
    return;
  }
  if (
    !digitalRead(SW_SPO2) &&
    systemData.medical.spo2 <
    SPO2_LOW
  ) {
    strcpy(
      systemData.medicalAlert,
      "SpO2 below normal"
    );
    return;
  }
  if (
    systemData.motion &&
    !digitalRead(SW_MOTION)
  ) {
    strcpy(
      systemData.medicalAlert,
      "Motion detected"
    );
  }
}
// ============================================================
// TASK 2 - FACILITY ALERT
// ============================================================
void updateFacilityAlert() {
  strcpy(
    systemData.facilityAlert,
    "OK"
  );
  if (
    systemData.facility.roomTemperature <
    ROOM_TEMP_LOW
  ) {
    strcpy(
      systemData.facilityAlert,
      "Room temperature low"
    );
    return;
  }
  if (
    systemData.facility.roomTemperature >
    ROOM_TEMP_HIGH
  ) {
    strcpy(
      systemData.facilityAlert,
      "Room temperature high"
    );
    return;
  }
  if (
    systemData.facility.oxygenLevel <
    OXYGEN_LOW
  ) {
    strcpy(
      systemData.facilityAlert,
      "Oxygen level low"
    );
    return;
  }
  if (
    systemData.facility.oxygenLevel >
    OXYGEN_HIGH
  ) {
    strcpy(
      systemData.facilityAlert,
      "Oxygen level high"
    );
    return;
  }
  if (
    systemData.facility.aqi >
    AQI_HIGH
  ) {
    strcpy(
      systemData.facilityAlert,
      "AQI above safe level"
    );
  }
}
// ============================================================
// DATA MANAGER TASK
// ============================================================
void dataManagerTask(void *param) {
  MedicalData medicalData;
  FacilityData facilityData;
  bool motion;
  int ecgValue;
  while (1) {
    taskHeartbeat[TASK_DATA_MANAGER] =
      millis();
    if (
      xQueueReceive(
        medicalQueue,
        &medicalData,
        0
      ) == pdPASS
    ) {
      if (
        xSemaphoreTake(
          dataMutex,
          pdMS_TO_TICKS(100)
        ) == pdTRUE
      ) {
        systemData.medical =
          medicalData;
        xSemaphoreGive(dataMutex);
      }
    }
    if (
      xQueueReceive(
        facilityQueue,
        &facilityData,
        0
      ) == pdPASS
    ) {
      if (
        xSemaphoreTake(
          dataMutex,
          pdMS_TO_TICKS(100)
        ) == pdTRUE
      ) {
        systemData.facility =
          facilityData;
        xSemaphoreGive(dataMutex);
      }
    }
    if (
      xQueueReceive(
        environmentQueue,
        &motion,
        0
      ) == pdPASS
    ) {
      if (
        xSemaphoreTake(
          dataMutex,
          pdMS_TO_TICKS(100)
        ) == pdTRUE
      ) {
        systemData.motion =
          motion;
        xSemaphoreGive(dataMutex);
      }
    }
    if (
      xQueueReceive(
        ecgQueue,
        &ecgValue,
        0
      ) == pdPASS
    ) {
      if (
        xSemaphoreTake(
          dataMutex,
          pdMS_TO_TICKS(100)
        ) == pdTRUE
      ) {
        systemData.ecg =
          ecgValue;
        xSemaphoreGive(dataMutex);
      }
    }
    if (
      xSemaphoreTake(
        dataMutex,
        pdMS_TO_TICKS(100)
      ) == pdTRUE
    ) {
      updateMedicalAlert();
      updateFacilityAlert();
      xSemaphoreGive(dataMutex);
    }
    xSemaphoreGive(displaySemaphore);
    xSemaphoreGive(alertSemaphore);
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
// ============================================================
// DISPLAY TASK
// ============================================================
void displayTask(void *param) {
  SystemData data;
  uint32_t lastScreenChange = 0;
  while (1) {
    taskHeartbeat[TASK_DISPLAY] =
      millis();
    if (
      millis() - lastScreenChange >=
      5000
    ) {
      displayMode++;
      if (displayMode > 3) {
        displayMode = 0;
      }
      lastScreenChange =
        millis();
    }
    if (
      xSemaphoreTake(
        displaySemaphore,
        pdMS_TO_TICKS(1000)
      ) == pdTRUE
    ) {
      if (
        xSemaphoreTake(
          dataMutex,
          pdMS_TO_TICKS(100)
        ) == pdTRUE
      ) {
        data = systemData;
        xSemaphoreGive(dataMutex);
      }
      if (
        xSemaphoreTake(
          displayMutex,
          pdMS_TO_TICKS(100)
        ) == pdTRUE
      ) {
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 0);
        if (connectivityState != STATE_ONLINE) {
          display.println("SYSTEM CONNECTION");
          display.println();
          if (connectivityState == STATE_OFFLINE) {
            display.println("LOGGING OFFLINE");
            display.println("WiFi disconnected");
          } else {
            display.println("MQTT DISCONNECTED");
            display.println("Retrying connection");
          }
          display.println();
          display.println(offlineStorageReady ? "Local buffer active" : "BUFFER UNAVAILABLE");
        }
        else if (offlineBacklogPending) {
          display.println("ONLINE - SYNCING");
          display.println();
          display.println("Uploading saved data");
          display.println("Please wait...");
        }
        else if (displayMode == 0) {
          display.println("MEDICAL DASHBOARD");
          display.print("Body: ");
          display.print(
            data.medical.bodyTemperature,
            1
          );
          display.println(" C");
          display.print("HR: ");
          display.println(
            data.medical.heartRate
          );
          display.print("SpO2: ");
          display.print(
            data.medical.spo2
          );
          display.println(" %");
          display.print("ECG: ");
          display.println(
            data.ecg
          );
          display.print("Alert: ");
          display.println(
            data.medicalAlert
          );
        }
        else if (displayMode == 1) {
          display.println(
            "FACILITY DASHBOARD"
          );
          display.print("Room: ");
          display.print(
            data.facility.roomTemperature,
            1
          );
          display.println(" C");
          display.print("O2: ");
          display.print(
            data.facility.oxygenLevel,
            1
          );
          display.println(" %");
          display.print("AQI: ");
          display.println(
            data.facility.aqi
          );
          display.print("Motion: ");
          display.println(
            data.motion ? "YES" : "NO"
          );
          display.print("Alert: ");
          display.println(
            data.facilityAlert
          );
        }
        else if (displayMode == 2) {
          display.println(
            "MEDICATION CONTROL"
          );
          display.print("Request: ");
          display.print(
            data.medication.requestedDosage
          );
          display.println(" mg/hr");
          display.print("Dosage: ");
          display.print(
            data.medication.currentDosage
          );
          display.println(" mg/hr");
          display.print("Status: ");
          display.println(
            data.medication.status
          );
          if (
            data.medication.confirmationRequired
          ) {
            display.println(
              "CONFIRM REQUIRED"
            );
          }
          else if (
            data.medication.currentDosage >=
            MEDICATION_CRITICAL_MIN
          ) {
            display.println(
              "ALARM ACTIVE"
            );
          }
          else {
            display.println(
              "Control: ACTIVE"
            );
          }
        }
        else {
          display.println(
            "BED ELEVATION"
          );
          display.print("Mode: ");
          display.println(
            getBedModeName()
          );
          display.print("Target: ");
          display.print(
            data.bed.targetAngle
          );
          display.println(" deg");
          display.print("Angle: ");
          display.print(
            data.bed.currentAngle
          );
          display.println(" deg");
          display.print("Status: ");
          display.println(
            data.bed.moving ?
            "MOVING" :
            "STABLE"
          );
        }
        display.display();
        xSemaphoreGive(displayMutex);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
// ============================================================
// ALERT TASK
// ============================================================
void alertTask(void *param) {
  SystemData data;
  while (1) {
    taskHeartbeat[TASK_ALERT] =
      millis();
    if (
      xSemaphoreTake(
        alertSemaphore,
        pdMS_TO_TICKS(1000)
      ) == pdTRUE
    ) {
      if (
        xSemaphoreTake(
          dataMutex,
          pdMS_TO_TICKS(100)
        ) == pdTRUE
      ) {
        data = systemData;
        xSemaphoreGive(dataMutex);
      }
      if (
        !digitalRead(SW_TEMP) &&
        data.medical.bodyTemperature >
        BODY_TEMP_HIGH
      ) {
        digitalWrite(
          BUZZER_PIN_1,
          HIGH
        );
        vTaskDelay(
          pdMS_TO_TICKS(100)
        );
        digitalWrite(
          BUZZER_PIN_1,
          LOW
        );
      }
      if (
        !digitalRead(SW_HR) &&
        (
          data.medical.heartRate <
          HEART_RATE_LOW ||
          data.medical.heartRate >
          HEART_RATE_HIGH
        )
      ) {
        digitalWrite(
          BUZZER_PIN_2,
          HIGH
        );
        vTaskDelay(
          pdMS_TO_TICKS(100)
        );
        digitalWrite(
          BUZZER_PIN_2,
          LOW
        );
      }
      if (
        !digitalRead(SW_SPO2) &&
        data.medical.spo2 <
        SPO2_LOW
      ) {
        digitalWrite(
          BUZZER_PIN_3,
          HIGH
        );
        vTaskDelay(
          pdMS_TO_TICKS(100)
        );
        digitalWrite(
          BUZZER_PIN_3,
          LOW
        );
      }
      if (
        data.facility.roomTemperature <
        ROOM_TEMP_LOW ||
        data.facility.roomTemperature >
        ROOM_TEMP_HIGH ||
        data.facility.oxygenLevel <
        OXYGEN_LOW ||
        data.facility.oxygenLevel >
        OXYGEN_HIGH ||
        data.facility.aqi >
        AQI_HIGH ||
        (
          data.motion &&
          !digitalRead(SW_MOTION)
        )
      ) {
        digitalWrite(
          BUZZER_PIN_4,
          HIGH
        );
        vTaskDelay(
          pdMS_TO_TICKS(100)
        );
        digitalWrite(
          BUZZER_PIN_4,
          LOW
        );
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
// ============================================================
// MQTT CONNECTION
// ============================================================
bool connectMQTT() {
  if (mqtt.connected()) {
    return true;
  }
  Serial.println(
    "MQTT: attempting connection..."
  );
  int8_t ret = mqtt.connect();
  if (ret != 0) {
    Serial.print(
      "MQTT connection failed: "
    );
    Serial.println(ret);
    mqtt.disconnect();
    return false;
  }
  Serial.println(
    "MQTT: connected!"
  );
  if (
    mqtt.subscribe(
      &medicationCurrentControlFeed
    )
  ) {
    Serial.println(
      "MQTT: subscribed to medication-current"
    );
  }
  if (
    mqtt.subscribe(
      &medicationAlertControlFeed
    )
  ) {
    Serial.println(
      "MQTT: subscribed to medication-alert"
    );
  }
  if (
    mqtt.subscribe(
      &bedControlFeed
    )
  ) {
    Serial.println(
      "MQTT: subscribed to bed-control"
    );
  }
  if (mqtt.subscribe(&samplingRateFeed)) {
    Serial.println("MQTT: subscribed to sampling-rate");
  }
  if (mqtt.subscribe(&samplingModeFeed)) {
    Serial.println("MQTT: subscribed to sampling-mode");
  }
  return true;
}
// ============================================================
// MQTT TASK
// ============================================================
void mqttTask(void *param) {
  SystemData data;
  uint32_t lastPublish = 0;
  uint32_t lastMedicationAlertPublish = 0;
  uint32_t lastMqttPing = 0;
  while (1) {
    taskHeartbeat[TASK_MQTT] = millis();
    uint32_t now = millis();

    // TEMPORARY Task 6 test controls. Send one command per line in Serial Monitor.
    if (Serial.available() > 0) {
      String serialCommand = Serial.readStringUntil('\n');
      serialCommand.trim();
      serialCommand.toLowerCase();

      if (serialCommand == "mqtt_off") {
        mqttTestDisconnect = true;
        mqtt.disconnect();
        mqttPublishHealthy = false;
        Serial.println("TASK 6 TEST | MQTT deliberately disconnected; Wi-Fi stays enabled");
      } else if (serialCommand == "mqtt_on") {
        mqttTestDisconnect = false;
        nextMqttAttemptMs = millis();
        Serial.println("TASK 6 TEST | MQTT reconnection enabled");
      } else if (serialCommand.length() > 0) {
        Serial.println("TASK 6 TEST | Commands: mqtt_off, mqtt_on");
      }
    }
    // Wi-Fi retry with exponential backoff (1s, 2s, 4s ... max 30s).
    if (WiFi.status() != WL_CONNECTED && (int32_t)(now - nextWifiAttemptMs) >= 0) {
      Serial.println("TASK 6 | WiFi reconnect attempt");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
      nextWifiAttemptMs = now + wifiRetryDelayMs;
      wifiRetryDelayMs = min(wifiRetryDelayMs * 2, MAX_RETRY_DELAY_MS);
    } else if (WiFi.status() == WL_CONNECTED) {
      wifiRetryDelayMs = 1000;
    }
    // MQTT retry with independent exponential backoff.
    if (WiFi.status() == WL_CONNECTED && !mqtt.connected() &&
        !mqttTestDisconnect &&
        (int32_t)(now - nextMqttAttemptMs) >= 0) {
      if (connectMQTT()) {
        mqttPublishHealthy = true;
        mqttRetryDelayMs = 1000;
        nextMqttAttemptMs = now + 1000;
      } else {
        nextMqttAttemptMs = now + mqttRetryDelayMs;
        mqttRetryDelayMs = min(mqttRetryDelayMs * 2, MAX_RETRY_DELAY_MS);
      }
    }
    updateConnectivityState();
    serviceNetworkBuzzer();
    // Snapshot and publish every 30 seconds. If disconnected or any publish fails,
    // persist this sample locally so it can be replayed after reconnection.
    if ((uint32_t)(now - lastPublish) >= TELEMETRY_INTERVAL_MS) {
      lastPublish = now;
      bool gotData = false;
      if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        data = systemData;
        xSemaphoreGive(dataMutex);
        gotData = true;
      }
      if (gotData) {
        if (!mqtt.connected() || WiFi.status() != WL_CONNECTED) {
          appendOfflineSnapshot(data);
        } else {
          bool allPublished = true;
          if (!medicalBodyTempFeed.publish(data.medical.bodyTemperature)) allPublished = false;
          if (!medicalHRFeed.publish((int32_t)data.medical.heartRate)) allPublished = false;
          if (!medicalSpO2Feed.publish((int32_t)data.medical.spo2)) allPublished = false;
          if (!medicalAlertFeed.publish(data.medicalAlert)) allPublished = false;
          if (!facilityRoomTempFeed.publish(data.facility.roomTemperature)) allPublished = false;
          if (!facilityOxygenFeed.publish(data.facility.oxygenLevel)) allPublished = false;
          if (!facilityAQIFeed.publish((int32_t)data.facility.aqi)) allPublished = false;
          if (!facilityAlertFeed.publish(data.facilityAlert)) allPublished = false;
          mqttPublishHealthy = allPublished;
          if (!allPublished) {
            Serial.println("TASK 6 | One or more telemetry publishes failed; buffering snapshot");
            appendOfflineSnapshot(data);
            mqtt.disconnect();
            nextMqttAttemptMs = millis() + mqttRetryDelayMs;
            mqttRetryDelayMs = min(mqttRetryDelayMs * 2, MAX_RETRY_DELAY_MS);
          }
        }
      }
      updateConnectivityState();
    }
    // Upload at most one historical snapshot every 10 seconds to avoid flooding IO.
    if (mqtt.connected() && (uint32_t)(millis() - lastOfflineSyncMs) >= 10000) {
      lastOfflineSyncMs = millis();
      syncOneOfflineSnapshot();
    }
    // MQTT subscriptions and existing commands (Task 3, Task 4 and Task 5).
    if (mqtt.connected()) {
      Adafruit_MQTT_Subscribe *subscription;
      while ((subscription = mqtt.readSubscription(100))) {
        Serial.println("MQTT: incoming message received!");
        if (subscription == &medicationCurrentControlFeed) {
          int requested = atoi((char *)medicationCurrentControlFeed.lastread);
          requested = constrain(requested, MEDICATION_MIN_DOSAGE, MEDICATION_MAX_DOSAGE);
          Serial.printf("MQTT dosage request: %d mg/hr\n", requested);
          requestMedicationDosage(requested);
        }
        else if (subscription == &medicationAlertControlFeed) {
          String command = String((char *)medicationAlertControlFeed.lastread);
          command.trim();
          command.toUpperCase();
          Serial.print("MQTT confirmation: ");
          Serial.println(command);
          if (command == "CONFIRM" || command == "YES" || command == "1") {
            medicationCriticalConfirmed = true;
            medicationConfirmationRequired = false;
            Serial.println("Medication CRITICAL dosage CONFIRMED");
            requestMedicationDosage(medicationRequestedDosage);
          }
        }
        else if (subscription == &samplingRateFeed) {
          int seconds = constrain(atoi((char *)samplingRateFeed.lastread), 5, MAX_SAMPLING_SECONDS);
          currentSamplingConfig.intervalSeconds = seconds;
          xQueueOverwrite(medicalSamplingQueue, &currentSamplingConfig);
          xQueueOverwrite(facilitySamplingQueue, &currentSamplingConfig);
          Serial.printf("TASK 5: interval set to %d seconds\n", seconds);
        }
        else if (subscription == &samplingModeFeed) {
          String mode = String((char *)samplingModeFeed.lastread);
          mode.trim();
          mode.toUpperCase();
          if (mode == "AUTO" || mode == "1") currentSamplingConfig.autoMode = true;
          else if (mode == "MANUAL" || mode == "0") currentSamplingConfig.autoMode = false;
          else {
            Serial.println("TASK 5: use AUTO or MANUAL");
            continue;
          }
          xQueueOverwrite(medicalSamplingQueue, &currentSamplingConfig);
          xQueueOverwrite(facilitySamplingQueue, &currentSamplingConfig);
          Serial.printf("TASK 5: mode set to %s\n", currentSamplingConfig.autoMode ? "AUTO" : "MANUAL");
        }
        else if (subscription == &bedControlFeed) {
          String command = String((char *)bedControlFeed.lastread);
          command.trim();
          String upperCommand = command;
          upperCommand.toUpperCase();
          bool numeric = true;
          for (unsigned int i = 0; i < command.length(); i++) {
            if (!isDigit(command[i])) { numeric = false; break; }
          }
          if (numeric && command.length() > 0) {
            int angle = constrain(command.toInt(), BED_MIN_ANGLE, BED_MAX_ANGLE);
            bedMode = BED_MANUAL;
            bedRequestedAngle = angle;
            updateBedTarget();
            Serial.printf("Bed manual angle requested: %d degrees\n", angle);
          }
          else if (upperCommand == "SLEEP" || upperCommand == "SLEEPING") {
            bedMode = BED_SLEEP;
            bedRequestedAngle = BED_SLEEP_ANGLE;
            updateBedTarget();
            Serial.println("BED: SLEEPING mode -> 10 degrees");
          }
          else if (upperCommand == "BREATHING" || upperCommand == "BREATHING SUPPORT") {
            bedMode = BED_BREATHING;
            bedRequestedAngle = BED_BREATHING_ANGLE;
            updateBedTarget();
            Serial.println("BED: BREATHING SUPPORT mode -> 45 degrees");
          }
          else if (upperCommand == "EMERGENCY") {
            bedMode = BED_EMERGENCY;
            bedRequestedAngle = BED_EMERGENCY_ANGLE;
            updateBedTarget();
            Serial.println("BED: EMERGENCY mode -> 90 degrees");
          }
          else if (upperCommand == "AUTO") {
            bedMode = BED_AUTO;
            updateBedTarget();
            Serial.println("BED: AUTO mode enabled");
          }
          else {
            Serial.print("BED: unknown command: ");
            Serial.println(command);
          }
        }
      }
    }
    // Preserve existing medication status feed publishing.
    if (mqtt.connected() && (uint32_t)(millis() - lastMedicationAlertPublish) >= 10000) {
      lastMedicationAlertPublish = millis();
      if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        data = systemData;
        xSemaphoreGive(dataMutex);
        char medicationMessage[160];
        snprintf(medicationMessage, sizeof(medicationMessage),
                 "Status=%s | Request=%d mg/hr | Current=%d mg/hr | %s",
                 data.medication.status,
                 data.medication.requestedDosage,
                 data.medication.currentDosage,
                 data.medication.alert);
        if (!medicationAlertFeed.publish(medicationMessage)) mqttPublishHealthy = false;
      }
    }
    if (mqtt.connected() && (uint32_t)(millis() - lastMqttPing) >= 15000) {
      lastMqttPing = millis();
      if (!mqtt.ping()) {
        mqttPublishHealthy = false;
        mqtt.disconnect();
        nextMqttAttemptMs = millis() + mqttRetryDelayMs;
        mqttRetryDelayMs = min(mqttRetryDelayMs * 2, MAX_RETRY_DELAY_MS);
      }
    }
    updateConnectivityState();
    serviceNetworkBuzzer();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
// ============================================================
// SETUP
// ============================================================
void watchdogTask(void *param) {
  esp_task_wdt_add(NULL);
  const char *taskNames[TASK_COUNT] = {
    "MedicalSensorTask",
    "FacilitySensorTask",
    "ECGTask",
    "EnvironmentTask",
    "DataManagerTask",
    "DisplayTask",
    "AlertTask",
    "MQTTTask",
    "MedicationTask",
    "BedElevationTask"
  };
  Serial.println(
    "WatchdogTask: monitoring tasks"
  );
  while (1) {
    bool allTasksAlive = true;
    uint32_t currentTime =
      millis();
    for (
      int i = 0;
      i < TASK_COUNT;
      i++
    ) {
      uint32_t elapsed =
        currentTime -
        taskHeartbeat[i];
      if (
        elapsed >
        TASK_TIMEOUT_MS
      ) {
        allTasksAlive = false;
        Serial.print(
          "WATCHDOG WARNING: "
        );
        Serial.print(
          taskNames[i]
        );
        Serial.print(
          " not responding for "
        );
        Serial.print(
          elapsed
        );
        Serial.println(
          " ms"
        );
      }
    }
    if (allTasksAlive) {
      esp_task_wdt_reset();
    }
    else {
      Serial.println(
        "WATCHDOG FAILURE: system task stalled!"
      );
    }
    vTaskDelay(
      pdMS_TO_TICKS(
        WATCHDOG_CHECK_INTERVAL_MS
      )
    );
  }
}
// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println(
    "========================================"
  );
  Serial.println(
    "TASK 5 - ADAPTIVE SAMPLING + TASKS 2-4"
  );
  Serial.println(
    "Task 2 + Task 3 + Task 4"
  );
  Serial.println(
    "========================================"
  );
  // -------------------------
  // TASK 6 persistent offline log storage.
  offlineStorageReady = initializeOfflineStorage();
  // Existing buzzers
  // -------------------------
  pinMode(BUZZER_PIN_1, OUTPUT);
  pinMode(BUZZER_PIN_2, OUTPUT);
  pinMode(BUZZER_PIN_3, OUTPUT);
  pinMode(BUZZER_PIN_4, OUTPUT);
  digitalWrite(BUZZER_PIN_1, LOW);
  digitalWrite(BUZZER_PIN_2, LOW);
  digitalWrite(BUZZER_PIN_3, LOW);
  digitalWrite(BUZZER_PIN_4, LOW);
  // -------------------------
  // Medication hardware
  // -------------------------
  pinMode(
    MEDICATION_LED_PIN,
    OUTPUT
  );
  pinMode(
    MEDICATION_BUZZER_PIN,
    OUTPUT
  );
  digitalWrite(
    MEDICATION_LED_PIN,
    LOW
  );
  digitalWrite(
    MEDICATION_BUZZER_PIN,
    LOW
  );
  // -------------------------
  // PIR
  // -------------------------
  pinMode(
    MOTION_DETECTOR,
    INPUT
  );
  // -------------------------
  // DIP switches
  // -------------------------
  pinMode(
    SW_TEMP,
    INPUT_PULLUP
  );
  pinMode(
    SW_HR,
    INPUT_PULLUP
  );
  pinMode(
    SW_SPO2,
    INPUT_PULLUP
  );
  pinMode(
    SW_MOTION,
    INPUT_PULLUP
  );
  analogReadResolution(12);
  // -------------------------
  // Temperature sensors
  // -------------------------
  bodyTempSensor.begin();
  roomTempSensor.begin();
  // -------------------------
  // OLED
  // -------------------------
  Wire.begin(21, 22);
  if (
    !display.begin(
      SSD1306_SWITCHCAPVCC,
      OLED_ADDRESS
    )
  ) {
    Serial.println(
      "OLED initialization FAILED!"
    );
  }
  else {
    Serial.println(
      "OLED initialized successfully."
    );
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(
      SSD1306_WHITE
    );
    display.setCursor(0, 0);
    display.println(
      "TASK 4 SMART BED"
    );
    display.println();
    display.println(
      "Medical"
    );
    display.println(
      "Facility"
    );
    display.println(
      "Medication"
    );
    display.println(
      "Bed Elevation"
    );
    display.println();
    display.println(
      "Starting..."
    );
    display.display();
    delay(1500);
  }
  // ==========================================================
  // TASK 4 SERVO
  // ==========================================================
  bedServo.setPeriodHertz(50);
  bedServo.attach(
    BED_SERVO_PIN,
    500,
    2400
  );
  bedCurrentAngle =
    BED_SLEEP_ANGLE;
  bedTargetAngle =
    BED_SLEEP_ANGLE;
  bedRequestedAngle =
    BED_SLEEP_ANGLE;
  bedServo.write(
    bedCurrentAngle
  );
  Serial.println(
    "Bed servo initialized on GPIO 13"
  );
  Serial.println(
    "Initial bed angle: 10 degrees"
  );
  // -------------------------
  // WiFi
  // -------------------------
  Serial.println(
    "Connecting to WiFi..."
  );
  WiFi.begin(
    WIFI_SSID,
    WIFI_PASS
  );
  int wifiAttempts = 0;
  while (
    WiFi.status() != WL_CONNECTED &&
    wifiAttempts < 30
  ) {
    delay(500);
    Serial.print(".");
    wifiAttempts++;
  }
  Serial.println();
  if (
    WiFi.status() ==
    WL_CONNECTED
  ) {
    Serial.println(
      "WiFi connected!"
    );
    Serial.print(
      "IP Address: "
    );
    Serial.println(
      WiFi.localIP()
    );
  }
  else {
    Serial.println(
      "WiFi connection failed!"
    );
  }
  // -------------------------
  // Queues
  // -------------------------
  medicalQueue =
    xQueueCreate(
      1,
      sizeof(MedicalData)
    );
  facilityQueue =
    xQueueCreate(
      1,
      sizeof(FacilityData)
    );
  environmentQueue =
    xQueueCreate(
      1,
      sizeof(bool)
    );
  ecgQueue =
    xQueueCreate(
      1,
      sizeof(int)
    );
  medicalSamplingQueue = xQueueCreate(1, sizeof(SamplingConfig));
  facilitySamplingQueue = xQueueCreate(1, sizeof(SamplingConfig));
  if (medicalSamplingQueue == nullptr || facilitySamplingQueue == nullptr) {
    Serial.println("ERROR: sampling queues could not be created");
    while (true) { delay(1000); }
  }
  // -------------------------
  // Mutexes
  // -------------------------
  dataMutex =
    xSemaphoreCreateMutex();
  displayMutex =
    xSemaphoreCreateMutex();
  // -------------------------
  // Semaphores
  // -------------------------
  displaySemaphore =
    xSemaphoreCreateBinary();
  alertSemaphore =
    xSemaphoreCreateBinary();
  // -------------------------
  // Initial data
  // -------------------------
  systemData.medical.bodyTemperature =
    0.0;
  systemData.medical.heartRate =
    0;
  systemData.medical.spo2 =
    0;
  systemData.facility.roomTemperature =
    0.0;
  systemData.facility.oxygenLevel =
    0.0;
  systemData.facility.aqi =
    0;
  systemData.motion =
    false;
  systemData.ecg =
    0;
  strcpy(
    systemData.medicalAlert,
    "System Starting"
  );
  strcpy(
    systemData.facilityAlert,
    "System Starting"
  );
  // Medication
  systemData.medication.requestedDosage =
    0;
  systemData.medication.currentDosage =
    0;
  strcpy(
    systemData.medication.status,
    "NORMAL"
  );
  strcpy(
    systemData.medication.alert,
    "Dosage normal"
  );
  systemData.medication.confirmationRequired =
    false;
  systemData.medication.criticalConfirmed =
    false;
  medicationRequestedDosage =
    0;
  medicationCurrentDosage =
    0;
  medicationCriticalConfirmed =
    false;
  medicationConfirmationRequired =
    false;
  medicationLastIncrease =
    millis();
  medicationLastAlarm =
    millis();
  medicationAlarmState =
    false;
  // Bed
  systemData.bed.requestedAngle =
    BED_SLEEP_ANGLE;
  systemData.bed.targetAngle =
    BED_SLEEP_ANGLE;
  systemData.bed.currentAngle =
    BED_SLEEP_ANGLE;
  systemData.bed.mode =
    BED_MANUAL;
  systemData.bed.moving =
    false;
  bedMode =
    BED_MANUAL;
  bedLastStep =
    millis();
  // -------------------------
  // Watchdog heartbeat
  // -------------------------
  uint32_t startTime =
    millis();
  for (
    int i = 0;
    i < TASK_COUNT;
    i++
  ) {
    taskHeartbeat[i] =
      startTime;
  }
  // ==========================================================
  // CREATE TASKS
  // ==========================================================
  xTaskCreatePinnedToCore(
    medicalSensorTask,
    "MedicalSensorTask",
    4096,
    NULL,
    3,
    NULL,
    1
  );
  xTaskCreatePinnedToCore(
    facilitySensorTask,
    "FacilitySensorTask",
    4096,
    NULL,
    3,
    NULL,
    1
  );
  xTaskCreatePinnedToCore(
    ecgTask,
    "ECGTask",
    4096,
    NULL,
    3,
    NULL,
    1
  );
  xTaskCreatePinnedToCore(
    environmentTask,
    "EnvironmentTask",
    2048,
    NULL,
    3,
    NULL,
    1
  );
  xTaskCreatePinnedToCore(
    dataManagerTask,
    "DataManagerTask",
    4096,
    NULL,
    4,
    NULL,
    1
  );
  xTaskCreatePinnedToCore(
    displayTask,
    "DisplayTask",
    4096,
    NULL,
    2,
    NULL,
    0
  );
  xTaskCreatePinnedToCore(
    alertTask,
    "AlertTask",
    2048,
    NULL,
    5,
    NULL,
    1
  );
  xTaskCreatePinnedToCore(
    mqttTask,
    "MQTTTask",
    8192,
    NULL,
    1,
    NULL,
    0
  );
  xTaskCreatePinnedToCore(
    medicationTask,
    "MedicationTask",
    4096,
    NULL,
    5,
    NULL,
    1
  );
  // ==========================================================
  // TASK 4 BED TASK
  // ==========================================================
  xTaskCreatePinnedToCore(
    bedElevationTask,
    "BedElevationTask",
    4096,
    NULL,
    4,
    NULL,
    1
  );
  // ==========================================================
  // WATCHDOG
  // ==========================================================
  xTaskCreatePinnedToCore(
    watchdogTask,
    "WatchdogTask",
    4096,
    NULL,
    6,
    NULL,
    0
  );
  Serial.println();
  Serial.println(
    "========================================"
  );
  Serial.println(
    "FreeRTOS tasks started"
  );
  Serial.println(
    "========================================"
  );
  Serial.println(
    "MedicalSensorTask   : Core 1"
  );
  Serial.println(
    "FacilitySensorTask  : Core 1"
  );
  Serial.println(
    "ECGTask             : Core 1"
  );
  Serial.println(
    "EnvironmentTask     : Core 1"
  );
  Serial.println(
    "DataManagerTask     : Core 1"
  );
  Serial.println(
    "DisplayTask         : Core 0"
  );
  Serial.println(
    "AlertTask           : Core 1"
  );
  Serial.println(
    "MQTTTask            : Core 0"
  );
  Serial.println(
    "MedicationTask      : Core 1"
  );
  Serial.println(
    "BedElevationTask    : Core 1"
  );
  Serial.println(
    "WatchdogTask        : Core 0"
  );
  Serial.println(
    "========================================"
  );
  Serial.println(
    "Task 4 smart bed system running..."
  );
  Serial.println(
    "========================================"
  );
}
// ============================================================
// LOOP
// ============================================================
void loop() {
  vTaskDelay(portMAX_DELAY);
}
