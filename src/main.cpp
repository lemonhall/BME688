#include <M5Unified.h>
#include <Wire.h>
#include <bsec2.h>  // BSEC2 library (v2.x API)
#include <Preferences.h>

// Sea level pressure (hPa) for altitude calculation - can calibrate later
static float gSeaLevelPressure = 1013.25f;

// BSEC2 objects
Bsec2 envSensor;

Preferences prefs;
const char *PREF_NAMESPACE = "bsec2";
const char *PREF_KEY_STATE = "state";

// Timing
unsigned long lastUpdate = 0;
const unsigned long UPDATE_INTERVAL_MS = 5000; // auto refresh
unsigned long lastStateSave = 0;
const unsigned long STATE_SAVE_INTERVAL_MS = 10UL * 60UL * 1000UL; // 10 minutes

// Forward declarations
void drawStaticUI();
struct SensorValues;
void updateDynamicUI(const SensorValues &vals);
void i2cScan();
bool initBsec2();
void loadState();
void saveState();
float calcAltitude(float pressure_hPa);

// Regions for partial refresh
struct ValueRegion { int16_t x,y,w,h; };
ValueRegion regionTemp{10, 40, 220, 30};
ValueRegion regionHum{10, 85, 220, 30};
ValueRegion regionPress{10, 130, 100, 30};
ValueRegion regionGas{120, 130, 110, 30};
ValueRegion regionAlt{10, 175, 150, 20};
ValueRegion regionIndicator{200, 175, 20, 20};

// Simple flag to know first draw
bool uiDrawn = false;

struct SensorValues {
  float temperature{NAN};
  float humidity{NAN};
  float pressure_hPa{NAN};
  float gas_kOhm{NAN};
  float altitude_m{NAN};
  float iaq{NAN};
  uint8_t iaqAccuracy{0};
  float co2eq{NAN};
  float vocEq{NAN};
  uint32_t readMs{0};
};

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  Wire.begin();

  Serial.println("\n=== 启动: M5Stack CoreS3 + ENV Pro (BME688) ===");

  drawStaticUI();
  uiDrawn = true;

  if (!initBsec2()) {
    Serial.println("BME688 初始化失败 (BSEC2)");
  } else {
    Serial.println("✓ BME688 初始化成功 (BSEC2)");
  }
}

void loop() {
  M5.update();

  if (M5.BtnA.wasPressed()) {
    Serial.println("[BtnA] 手动刷新");
    lastUpdate = 0; // force
  }
  if (M5.BtnB.wasPressed()) {
    Serial.println("[BtnB] I2C 扫描");
    i2cScan();
  }
  if (M5.BtnC.wasPressed()) {
    Serial.println("[BtnC] 重新初始化传感器");
    initBsec2();
  }

  unsigned long now = millis();
  if (now - lastUpdate >= UPDATE_INTERVAL_MS) {
    lastUpdate = now;
    uint32_t tStart = millis();

    if (envSensor.run()) {
      uint32_t readMs = millis() - tStart;
      SensorValues vals;
      vals.readMs = readMs;
      auto dTemp = envSensor.getData(BSEC_OUTPUT_RAW_TEMPERATURE);
      auto dHum = envSensor.getData(BSEC_OUTPUT_RAW_HUMIDITY);
      auto dPress = envSensor.getData(BSEC_OUTPUT_RAW_PRESSURE);
      auto dGas = envSensor.getData(BSEC_OUTPUT_RAW_GAS);
      auto dIaq = envSensor.getData(BSEC_OUTPUT_IAQ);
      auto dCo2 = envSensor.getData(BSEC_OUTPUT_CO2_EQUIVALENT);
      auto dVoc = envSensor.getData(BSEC_OUTPUT_BREATH_VOC_EQUIVALENT);

      vals.temperature = dTemp.signal;
      vals.humidity = dHum.signal;
      vals.pressure_hPa = dPress.signal / 100.0f; // Pa -> hPa
      vals.gas_kOhm = dGas.signal / 1000.0f; // Ohm -> kOhm
      vals.altitude_m = calcAltitude(vals.pressure_hPa);
      vals.iaq = dIaq.signal;
      vals.iaqAccuracy = dIaq.accuracy;
      vals.co2eq = dCo2.signal;
      vals.vocEq = dVoc.signal;

      updateDynamicUI(vals);

      // Periodic state save
      if (now - lastStateSave >= STATE_SAVE_INTERVAL_MS) {
        saveState();
        lastStateSave = now;
      }

      // Serial formatted block
      Serial.println("\n╔════════════════════════════════════╗");
      Serial.println("║     BME688 环境传感器数据 (BSEC2) ║");
      Serial.println("╠════════════════════════════════════╣");
      Serial.printf("║ 温度:      %6.2f °C            ║\n", vals.temperature);
      Serial.printf("║ 湿度:      %6.2f %%             ║\n", vals.humidity);
      Serial.printf("║ 气压:    %7.2f hPa           ║\n", vals.pressure_hPa);
      Serial.printf("║ 气体阻值: %6.2f kΩ            ║\n", vals.gas_kOhm);
      Serial.printf("║ 海拔高度: %6.2f m             ║\n", vals.altitude_m);
      Serial.printf("║ IAQ:       %6.2f (精度:%d)      ║\n", vals.iaq, vals.iaqAccuracy);
      Serial.printf("║ CO2eq:     %6.2f ppm           ║\n", vals.co2eq);
      Serial.printf("║ VOCeq:     %6.2f ppm           ║\n", vals.vocEq);
      Serial.printf("║ 读取耗时:  %3u ms               ║\n", vals.readMs);
      Serial.println("╚════════════════════════════════════╝");
    } else {
      Serial.println("读取 BSEC2 数据失败");
    }
  }
}

bool initBsec2() {
  // Initialize bsec2 library
  // load state if available
  if (!envSensor.begin(BME68X_I2C_ADDR_LOW, Wire)) { // try 0x76 first
    if (!envSensor.begin(BME68X_I2C_ADDR_HIGH, Wire)) { // 0x77
      return false;
    }
  }
  loadState();

  // Subscribe to BSEC outputs of interest
  bsec_virtual_sensor_t sensorList[] = {
      BSEC_OUTPUT_RAW_TEMPERATURE,
      BSEC_OUTPUT_RAW_PRESSURE,
      BSEC_OUTPUT_RAW_HUMIDITY,
      BSEC_OUTPUT_RAW_GAS,
      BSEC_OUTPUT_IAQ,
      BSEC_OUTPUT_STATIC_IAQ,
      BSEC_OUTPUT_CO2_EQUIVALENT,
      BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,
      BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
      BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY
  };
  bsec_sensor_configuration_t requestedSettings[sizeof(sensorList)/sizeof(sensorList[0])];
  uint8_t numRequested = sizeof(sensorList)/sizeof(sensorList[0]);

  if (!envSensor.updateSubscription(sensorList, numRequested, BSEC_SAMPLE_RATE_ULP)) {
    Serial.println("BSEC2 订阅失败");
    return false;
  }

  lastUpdate = 0;
  return true;
}

float calcAltitude(float pressure_hPa) {
  return 44330.0f * (1.0f - pow(pressure_hPa / gSeaLevelPressure, 0.1903f));
}

void drawCard(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t color,const char *label) {
  M5.Display.fillRoundRect(x,y,w,h,8,color);
  M5.Display.drawRoundRect(x,y,w,h,8,TFT_WHITE);
  M5.Display.setTextDatum(TL_DATUM);
  M5.Display.setTextColor(TFT_WHITE, color);
  M5.Display.setFont(&efontCN_12);
  M5.Display.setCursor(x+8,y+6);
  M5.Display.print(label);
}

void drawStaticUI() {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setFont(&efontCN_16);
  M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
  M5.Display.setCursor(10, 10);
  M5.Display.print("🌿 环境监测站");

  drawCard(5, 35, 230, 40, M5.Display.color565(0,40,120), "[T] 温度");
  drawCard(5, 80, 230, 40, M5.Display.color565(0,80,40), "[H] 湿度");
  drawCard(5, 125, 110, 40, M5.Display.color565(80,0,80), "[P] 气压");
  drawCard(120,125,115,40, M5.Display.color565(40,40,0), "[G] 气体");

  // bottom info line
  M5.Display.setFont(&efontCN_10);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setCursor(regionAlt.x, regionAlt.y);
  M5.Display.print("海拔: --.-m");
}

void updateRegion(ValueRegion r) {
  M5.Display.fillRect(r.x, r.y, r.w, r.h, TFT_BLACK); // clear
}

void updateDynamicUI(const SensorValues &vals) {
  // Temperature
  updateRegion(regionTemp);
  M5.Display.setFont(&efontCN_12);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setCursor(regionTemp.x+12, regionTemp.y+5);
  M5.Display.printf("%5.2f °C", vals.temperature);

  // Humidity
  updateRegion(regionHum);
  M5.Display.setCursor(regionHum.x+12, regionHum.y+5);
  M5.Display.printf("%5.2f %%", vals.humidity);

  // Pressure
  updateRegion(regionPress);
  M5.Display.setCursor(regionPress.x+8, regionPress.y+5);
  M5.Display.printf("%6.2f hPa", vals.pressure_hPa);

  // Gas resistance
  updateRegion(regionGas);
  M5.Display.setCursor(regionGas.x+8, regionGas.y+5);
  M5.Display.printf("%5.2f kΩ", vals.gas_kOhm);

  // Altitude + indicator
  updateRegion(regionAlt);
  M5.Display.setFont(&efontCN_10);
  M5.Display.setCursor(regionAlt.x, regionAlt.y);
  M5.Display.printf("海拔: %.1fm", vals.altitude_m);

  // Indicator green dot (blinks based on IAQ accuracy maybe later)
  M5.Display.fillCircle(regionIndicator.x, regionIndicator.y+5, 5, TFT_GREEN);
}

void i2cScan() {
  Serial.println("=== I2C 设备扫描 ===");
  uint8_t count = 0;
  for (uint8_t addr = 1; addr < 127; ++addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("发现 I2C 设备于地址 0x%02X\n", addr);
      ++count;
    }
  }
  Serial.printf("扫描完成, 共发现 %u 个设备\n", count);
  Serial.println("==================");
}

void loadState() {
  prefs.begin(PREF_NAMESPACE, true);
  size_t len = prefs.getBytesLength(PREF_KEY_STATE);
  if (len > 0 && len <= BSEC_MAX_STATE_BLOB_SIZE) {
    uint8_t blob[BSEC_MAX_STATE_BLOB_SIZE];
    prefs.getBytes(PREF_KEY_STATE, blob, len);
    if (envSensor.setState(blob)) {
      Serial.println("已加载 BSEC2 状态");
    }
  }
  prefs.end();
}

void saveState() {
  uint8_t blob[BSEC_MAX_STATE_BLOB_SIZE];
  if (envSensor.getState(blob)) {
    prefs.begin(PREF_NAMESPACE, false);
    prefs.putBytes(PREF_KEY_STATE, blob, BSEC_MAX_STATE_BLOB_SIZE);
    prefs.end();
    Serial.println("已保存 BSEC2 状态");
  }
}
