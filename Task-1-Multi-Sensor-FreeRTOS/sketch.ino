#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_task_wdt.h"

// ============================================================
// PIN DEFINITIONS
// ============================================================

#define TEMP_SENSOR 14
#define MOTION_DETECTOR 12
#define ECG_SENSOR        34

#define BUZZER_PIN_1 4
#define BUZZER_PIN_2 16
#define BUZZER_PIN_3 17
#define BUZZER_PIN_4 5

#define SW_TEMP 32
#define SW_HR 33
#define SW_SPO2 25
#define SW_MOTION 26


// ============================================================
// OLED
// ============================================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDRESS 0x3C


// ============================================================
// WIFI
// ============================================================

#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASS ""


// ============================================================
// ADAFRUIT IO
// ============================================================

#define IO_USERNAME "jssg1234"

// Keep your existing IO_KEY here locally.
// Do NOT share it publicly.

#define IO_KEY "PASTE_YOUR_SECRET_KEY"

#define AIO_SERVER "io.adafruit.com"
#define AIO_SERVERPORT 1883


#define TEMP_FEED \
  IO_USERNAME "/feeds/body-temperature-degrees-c"

#define HR_FEED \
  IO_USERNAME "/feeds/heart-rate-monitor"

#define MOTION_FEED \
  IO_USERNAME "/feeds/motion-detector"

#define SPO2_FEED \
  IO_USERNAME "/feeds/spo2-percent"

#define ECG_FEED \
  IO_USERNAME "/feeds/ecg-signal"

#define FLAG \
  IO_USERNAME "/feeds/flag"


// ============================================================
// WIFI / MQTT OBJECTS
// ============================================================

WiFiClient client;

Adafruit_MQTT_Client mqtt(
  &client,
  AIO_SERVER,
  AIO_SERVERPORT,
  IO_USERNAME,
  IO_KEY
);

Adafruit_MQTT_Publish tempFeed =
  Adafruit_MQTT_Publish(&mqtt, TEMP_FEED);

Adafruit_MQTT_Publish hrFeed =
  Adafruit_MQTT_Publish(&mqtt, HR_FEED);

Adafruit_MQTT_Publish spo2Feed =
  Adafruit_MQTT_Publish(&mqtt, SPO2_FEED);

  Adafruit_MQTT_Publish ecgFeed =
  Adafruit_MQTT_Publish(
    &mqtt,
    ECG_FEED
  );

Adafruit_MQTT_Publish statusFeed =
  Adafruit_MQTT_Publish(&mqtt, FLAG);

Adafruit_MQTT_Publish motiondetectorFeed =
  Adafruit_MQTT_Publish(&mqtt, MOTION_FEED);


// ============================================================
// SENSOR OBJECTS
// ============================================================

OneWire oneWire(TEMP_SENSOR);

DallasTemperature sensors(&oneWire);

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  OLED_RESET
);


// ============================================================
// PATIENT DATA STRUCTURE
// ============================================================

struct PatientData {

  float temperature;

  int heartRate;

  int spo2;

  bool motion;
  
  int ecg;

  char status[32];
};

// ============================================================
// TASK WATCHDOG HEARTBEAT MONITOR
// ============================================================

#define TASK_COUNT 7

enum TaskID
{
  TASK_SENSOR = 0,
  TASK_ECG,
  TASK_ENVIRONMENT,
  TASK_DATA_MANAGER,
  TASK_DISPLAY,
  TASK_ALERT,
  TASK_MQTT
};

volatile uint32_t taskHeartbeat[TASK_COUNT] = {0};

#define WATCHDOG_CHECK_INTERVAL_MS 1000
#define TASK_TIMEOUT_MS 4000


// ============================================================
// QUEUES
// ============================================================

QueueHandle_t sensorQueue;

QueueHandle_t environmentQueue;

QueueHandle_t ecgQueue;


// ============================================================
// MUTEX
// ============================================================

SemaphoreHandle_t dataMutex;

SemaphoreHandle_t displayMutex;


// ============================================================
// TASK NOTIFICATION SEMAPHORES
// ============================================================

SemaphoreHandle_t displaySemaphore;

SemaphoreHandle_t alertSemaphore;

SemaphoreHandle_t mqttSemaphore;


// ============================================================
// SHARED PATIENT DATA
// ============================================================

PatientData patientData;


// ============================================================
// SENSOR TASK
// ============================================================

void sensorTask(void *param) {

  PatientData sensorData;

  while (1) {

    taskHeartbeat[TASK_SENSOR] = millis();

    sensorData.temperature = 0.0;

    sensorData.heartRate = 0;

    sensorData.spo2 = 0;


    // --------------------------------------------------------
    // TEMPERATURE
    // --------------------------------------------------------

    if (!digitalRead(SW_TEMP)) {

      sensors.requestTemperatures();

      sensorData.temperature =
        sensors.getTempCByIndex(0);

    }


    // --------------------------------------------------------
    // HEART RATE
    // --------------------------------------------------------

    if (!digitalRead(SW_HR)) {

      sensorData.heartRate =
        random(45, 351);

    }
    else {

      sensorData.heartRate = 0;

    }


    // --------------------------------------------------------
    // SPO2
    // --------------------------------------------------------

    if (!digitalRead(SW_SPO2)) {

      sensorData.spo2 =
        random(60, 100);

    }
    else {

      sensorData.spo2 = 0;

    }


    // --------------------------------------------------------
    // SEND SENSOR DATA TO DATA MANAGER
    // --------------------------------------------------------

    xQueueOverwrite(
      sensorQueue,
      &sensorData
    );


    Serial.println("SensorTask: data updated");


    vTaskDelay(
      pdMS_TO_TICKS(2000)
    );
  }
}


// ============================================================
// ENVIRONMENT TASK
// ============================================================

void environmentTask(void *param) {

  bool motion;

  while (1) {

    taskHeartbeat[TASK_ENVIRONMENT] = millis();

    motion = digitalRead(MOTION_DETECTOR);


    xQueueOverwrite(
      environmentQueue,
      &motion
    );


    Serial.print("EnvironmentTask: Motion = ");

    Serial.println(motion);


    vTaskDelay(
      pdMS_TO_TICKS(500)
    );
  }
}

// ============================================================
// ECG TASK
// ============================================================

void ecgTask(void *param)
{
  int ecgValue;
  int printCounter = 0;

  while (1)
  {
    // --------------------------------------------------------
    // READ ECG
    // --------------------------------------------------------

    taskHeartbeat[TASK_ECG] = millis();

    ecgValue = analogRead(ECG_SENSOR);


    // --------------------------------------------------------
    // SEND ECG DATA TO DATA MANAGER
    // --------------------------------------------------------

    xQueueOverwrite(
      ecgQueue,
      &ecgValue
    );


    // --------------------------------------------------------
    // SERIAL DEBUG
    // Print only every 500 ms
    // --------------------------------------------------------

    printCounter++;

    if (printCounter >= 50)
    {
      Serial.print("ECGTask: ECG = ");
      Serial.println(ecgValue);

      printCounter = 0;
    }


    // --------------------------------------------------------
    // 100 Hz SAMPLING
    // 10 ms
    // --------------------------------------------------------

    vTaskDelay(
      pdMS_TO_TICKS(10)
    );
  }
}
// ============================================================
// DATA MANAGER TASK
// ============================================================

void dataManagerTask(void *param) {

  PatientData sensorData;

  bool motion;

  int ecgValue;


  while (1) {

    taskHeartbeat[TASK_DATA_MANAGER] = millis();

    // --------------------------------------------------------
    // GET SENSOR DATA
    // --------------------------------------------------------

    if (
      xQueueReceive(
        sensorQueue,
        &sensorData,
        pdMS_TO_TICKS(100)
      ) == pdPASS
    ) {

      // Lock shared data

      if (
        xSemaphoreTake(
          dataMutex,
          pdMS_TO_TICKS(100)
        ) == pdTRUE
      ) {

        patientData.temperature =
          sensorData.temperature;

        patientData.heartRate =
          sensorData.heartRate;

        patientData.spo2 =
          sensorData.spo2;


        xSemaphoreGive(dataMutex);


        // Notify other tasks

        xSemaphoreGive(displaySemaphore);

        xSemaphoreGive(alertSemaphore);

        xSemaphoreGive(mqttSemaphore);
      }
    }


    // --------------------------------------------------------
    // GET ENVIRONMENT DATA
    // --------------------------------------------------------

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

        patientData.motion = motion;

        xSemaphoreGive(dataMutex);

      }

      xSemaphoreGive(displaySemaphore);

      xSemaphoreGive(alertSemaphore);

      xSemaphoreGive(mqttSemaphore);
    }

    // --------------------------------------------------------
// GET ECG DATA
// --------------------------------------------------------

if (
  xQueueReceive(
    ecgQueue,
    &ecgValue,
    0
  ) == pdPASS
)
{
  if (
    xSemaphoreTake(
      dataMutex,
      pdMS_TO_TICKS(100)
    ) == pdTRUE
  )
  {
    patientData.ecg =
      ecgValue;

    xSemaphoreGive(
      dataMutex
    );
  }

  xSemaphoreGive(
    displaySemaphore
  );

  xSemaphoreGive(
    mqttSemaphore
  );
}


    // --------------------------------------------------------
    // DETERMINE STATUS
    // --------------------------------------------------------

    if (
      xSemaphoreTake(
        dataMutex,
        pdMS_TO_TICKS(100)
      ) == pdTRUE
    ) {

      strcpy(patientData.status, "OK");


      if (
        !digitalRead(SW_TEMP) &&
        patientData.temperature > 38.0
      ) {

        strcpy(
          patientData.status,
          "High temperature"
        );

      }

      else if (
        !digitalRead(SW_HR) &&
        patientData.heartRate < 60
      ) {

        strcpy(
          patientData.status,
          "HR below normal"
        );

      }

      else if (
        !digitalRead(SW_HR) &&
        patientData.heartRate > 300
      ) {

        strcpy(
          patientData.status,
          "HR above normal"
        );

      }

      else if (
        !digitalRead(SW_SPO2) &&
        patientData.spo2 < 85
      ) {

        strcpy(
          patientData.status,
          "SpO2 below normal"
        );

      }

      else if (
        patientData.motion &&
        !digitalRead(SW_MOTION)
      ) {

        strcpy(
          patientData.status,
          "Motion Detected!"
        );
      }


      xSemaphoreGive(dataMutex);
    }


    vTaskDelay(
      pdMS_TO_TICKS(100)
    );
  }
}


// ============================================================
// DISPLAY TASK
// ============================================================

void displayTask(void *param) {

  PatientData data;

  while (1) {

    taskHeartbeat[TASK_DISPLAY] = millis();

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

        data = patientData;

        xSemaphoreGive(dataMutex);
      }

      if (
        xSemaphoreTake(
          displayMutex,
          pdMS_TO_TICKS(100)
        ) == pdTRUE
      ) {

        display.clearDisplay();

        display.setCursor(0, 0);
        display.print("Temp: ");
        display.print(data.temperature, 1);
        display.print(" C");

        display.setCursor(0, 12);
        display.print("HR: ");
        display.print(data.heartRate);

        display.setCursor(0, 24);
        display.print("SpO2: ");
        display.print(data.spo2);
        display.print(" %");

        display.setCursor(0, 36);
        display.print("ECG: ");
        display.print(data.ecg);

        display.setCursor(0, 48);
        display.print(data.status);

        display.display();

        xSemaphoreGive(displayMutex);
      }
    }

    vTaskDelay(
      pdMS_TO_TICKS(100)
    );
  }
}


// ============================================================
// ALERT TASK
// ============================================================

void alertTask(void *param) {

  PatientData data;

  while (1) {

    taskHeartbeat[TASK_ALERT] = millis();

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

        data = patientData;

        xSemaphoreGive(dataMutex);
      }

      // --------------------------------------------------------
      // TEMPERATURE ALERT
      // --------------------------------------------------------

      if (
        !digitalRead(SW_TEMP) &&
        data.temperature > 38.0
      ) {

        digitalWrite(BUZZER_PIN_1, HIGH);

        vTaskDelay(
          pdMS_TO_TICKS(50)
        );

        digitalWrite(BUZZER_PIN_1, LOW);
      }

      // --------------------------------------------------------
      // HEART RATE ALERT
      // --------------------------------------------------------

      if (
        !digitalRead(SW_HR) &&
        (
          data.heartRate < 60 ||
          data.heartRate > 300
        )
      ) {

        digitalWrite(BUZZER_PIN_2, HIGH);

        vTaskDelay(
          pdMS_TO_TICKS(50)
        );

        digitalWrite(BUZZER_PIN_2, LOW);
      }

      // --------------------------------------------------------
      // SPO2 ALERT
      // --------------------------------------------------------

      if (
        !digitalRead(SW_SPO2) &&
        data.spo2 < 85
      ) {

        digitalWrite(BUZZER_PIN_3, HIGH);

        vTaskDelay(
          pdMS_TO_TICKS(50)
        );

        digitalWrite(BUZZER_PIN_3, LOW);
      }

      // --------------------------------------------------------
      // MOTION ALERT
      // --------------------------------------------------------

      if (
        data.motion &&
        !digitalRead(SW_MOTION)
      ) {

        digitalWrite(BUZZER_PIN_4, HIGH);

        vTaskDelay(
          pdMS_TO_TICKS(50)
        );

        digitalWrite(BUZZER_PIN_4, LOW);
      }
    }

    vTaskDelay(
      pdMS_TO_TICKS(100)
    );
  }
}


// ============================================================
// MQTT CONNECTION
// ============================================================

void connectMQTT()
{
  if (mqtt.connected())
  {
    return;
  }

  Serial.println();
  Serial.println("MQTT: attempting connection...");
  Serial.print("MQTT Server: ");
  Serial.println(AIO_SERVER);
  Serial.print("MQTT Port: ");
  Serial.println(AIO_SERVERPORT);
  Serial.print("WiFi Status: ");
  Serial.println(WiFi.status());

  int8_t ret = mqtt.connect();

  if (ret == 0)
  {
    Serial.println("MQTT: connected!");
  }
  else
  {
    Serial.print("MQTT connection failed: ");
    Serial.println(ret);

    mqtt.disconnect();

    vTaskDelay(
      pdMS_TO_TICKS(3000)
    );
  }
}


// ============================================================
// MQTT TASK
// ============================================================
void mqttTask(void *param) {

  PatientData data;

  while (1) {

    taskHeartbeat[TASK_MQTT] = millis();

    if (
      xSemaphoreTake(
        mqttSemaphore,
        pdMS_TO_TICKS(1000)
      ) == pdTRUE
    ) {

      if (!mqtt.connected()) {

        connectMQTT();
      }

      if (mqtt.connected()) {

        mqtt.processPackets(10);

        mqtt.ping();

        if (
          xSemaphoreTake(
            dataMutex,
            pdMS_TO_TICKS(100)
          ) == pdTRUE
        ) {

          data = patientData;

          xSemaphoreGive(dataMutex);
        }

        tempFeed.publish(
          data.temperature
        );

        hrFeed.publish(
          (int32_t)data.heartRate
        );

        spo2Feed.publish(
          (int32_t)data.spo2
        );

        motiondetectorFeed.publish(
          (int32_t)data.motion
        );

        ecgFeed.publish(
          (int32_t)data.ecg
        );

        statusFeed.publish(
          data.status
        );

        Serial.println(
          "MQTTTask: data published"
        );
      }
    }

    vTaskDelay(
      pdMS_TO_TICKS(500)
    );
  }
}


// ============================================================
// WATCHDOG TASK
// ============================================================

void watchdogTask(void *param)
{
  esp_task_wdt_add(NULL);

  const char *taskNames[TASK_COUNT] =
  {
    "SensorTask",
    "ECGTask",
    "EnvironmentTask",
    "DataManagerTask",
    "DisplayTask",
    "AlertTask",
    "MQTTTask"
  };

  Serial.println(
    "WatchdogTask: monitoring all critical tasks"
  );

  while (1)
  {
    bool allTasksAlive = true;

    uint32_t currentTime = millis();

    for (int i = 0; i < TASK_COUNT; i++)
    {
      uint32_t elapsed =
        currentTime - taskHeartbeat[i];

      if (elapsed > TASK_TIMEOUT_MS)
      {
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

    if (allTasksAlive)
    {
      esp_task_wdt_reset();

      Serial.println(
        "WatchdogTask: all tasks healthy"
      );
    }
    else
    {
      Serial.println(
        "WATCHDOG FAILURE: system task stalled!"
      );

      Serial.println(
        "Watchdog will NOT be fed."
      );

      // Intentionally do not call
      // esp_task_wdt_reset().
      //
      // Hardware watchdog will eventually
      // reset the ESP32.
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


  Serial.println();
  Serial.println(
    "================================"
  );
  Serial.println(
    "Concurrent Patient Monitoring"
  );
  Serial.println(
    "================================"
  );


  // ----------------------------------------------------------
  // BUZZERS
  // ----------------------------------------------------------

  pinMode(
    BUZZER_PIN_1,
    OUTPUT
  );

  pinMode(
    BUZZER_PIN_2,
    OUTPUT
  );

  pinMode(
    BUZZER_PIN_3,
    OUTPUT
  );

  pinMode(
    BUZZER_PIN_4,
    OUTPUT
  );


  digitalWrite(BUZZER_PIN_1, LOW);
  digitalWrite(BUZZER_PIN_2, LOW);
  digitalWrite(BUZZER_PIN_3, LOW);
  digitalWrite(BUZZER_PIN_4, LOW);


  // ----------------------------------------------------------
  // PIR
  // ----------------------------------------------------------

  pinMode(
    MOTION_DETECTOR,
    INPUT
  );


  // ----------------------------------------------------------
  // SWITCHES
  // ----------------------------------------------------------

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


  // ----------------------------------------------------------
  // TEMPERATURE SENSOR
  // ----------------------------------------------------------

  sensors.begin();


  Serial.print(
    "DS18B20 devices found: "
  );

  Serial.println(
    sensors.getDeviceCount()
  );


  // ----------------------------------------------------------
  // OLED
  // ----------------------------------------------------------

  Wire.begin(21, 22);


  Serial.println(
    "Initializing OLED..."
  );


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
      "Patient Monitor"
    );

    display.println();

    display.println(
      "FreeRTOS Starting..."
    );

    display.display();

    delay(1000);
  }


  // ----------------------------------------------------------
  // WIFI
  // ----------------------------------------------------------

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
    WiFi.status() == WL_CONNECTED
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


  // ==========================================================
  // CREATE QUEUES
  // ==========================================================

  sensorQueue =
  xQueueCreate(
    1,
    sizeof(PatientData)
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


  // ==========================================================
  // CREATE MUTEXES
  // ==========================================================

  dataMutex =
    xSemaphoreCreateMutex();


  displayMutex =
    xSemaphoreCreateMutex();


  // ==========================================================
  // CREATE SEMAPHORES
  // ==========================================================

  displaySemaphore =
    xSemaphoreCreateBinary();


  alertSemaphore =
    xSemaphoreCreateBinary();


  mqttSemaphore =
    xSemaphoreCreateBinary();


  // ==========================================================
  // INITIAL PATIENT DATA
  // ==========================================================

  patientData.temperature = 0.0;

  patientData.heartRate = 0;

  patientData.spo2 = 0;

  patientData.motion = false;

  patientData.ecg = 0;

  strcpy(
    patientData.status,
    "System Starting"
  );


// ==========================================================
// INITIALIZE WATCHDOG HEARTBEATS
// ==========================================================

uint32_t startTime = millis();

for (int i = 0; i < TASK_COUNT; i++)
{
  taskHeartbeat[i] = startTime;
}


// ==========================================================
// CREATE FREE RTOS TASKS
// ==========================================================

xTaskCreatePinnedToCore(
  sensorTask,
  "SensorTask",
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
    4096,
    NULL,
    1,
    NULL,
    0
  );

  xTaskCreatePinnedToCore(
  watchdogTask,
  "WatchdogTask",
  2048,
  NULL,
  6,
  NULL,
  0
);


  Serial.println();
  Serial.println(
    "================================"
  );

  Serial.println(
    "FreeRTOS tasks started"
  );

  Serial.println(
    "SensorTask       : Core 1"
  );

  Serial.println(
  "ECGTask          : Core 1"
);

  Serial.println(
    "EnvironmentTask  : Core 1"
  );

  Serial.println(
    "DataManagerTask  : Core 1"
  );

  Serial.println(
    "AlertTask        : Core 1"
  );

  Serial.println(
    "DisplayTask      : Core 0"
  );

  Serial.println(
    "MQTTTask         : Core 0"
  );

  Serial.println(
  "WatchdogTask    : Core 0"
);

  Serial.println(
    "================================"
  );
}


// ============================================================
// LOOP
// ============================================================

void loop() {

  vTaskDelay(
    portMAX_DELAY
  );
}