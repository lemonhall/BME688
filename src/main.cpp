/**
 * M5Stack CoreS3 + ENV Pro (BME688) 环境传感器读取示例
 * 
 * 硬件连接:
 *   - ENV Pro 模块通过 Grove I2C 接口连接到 M5Stack S3
 *   - BME688 默认 I2C 地址: 0x76 或 0x77
 * 
 * 功能:
 *   - 读取温度、湿度、气压、气体阻值
 *   - 串口与屏幕同步显示
 *   - 支持 I2C 地址扫描
 */

#include <M5Unified.h>
#include <Wire.h>
#include <Adafruit_BME680.h>

// BME688 传感器对象
Adafruit_BME680 bme;

// 传感器状态
bool sensorReady = false;

// 海平面气压 (hPa) - 用于计算海拔高度
#define SEALEVELPRESSURE_HPA (1013.25)

/**
 * I2C 总线扫描函数
 * 用于检测 BME688 设备地址
 */
void scanI2C() {
  Serial.println("\n=== I2C 设备扫描 ===");
  byte error, address;
  int nDevices = 0;

  for (address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      Serial.printf("发现 I2C 设备于地址 0x%02X\n", address);
      nDevices++;
    }
  }

  if (nDevices == 0) {
    Serial.println("未发现 I2C 设备!");
  } else {
    Serial.printf("扫描完成, 共发现 %d 个设备\n", nDevices);
  }
  Serial.println("==================\n");
}

/**
 * 初始化 BME688 传感器
 */
bool initBME688() {
  // 尝试地址 0x76
  if (bme.begin(0x76)) {
    Serial.println("✓ BME688 初始化成功 (地址: 0x76)");
  } 
  // 尝试地址 0x77
  else if (bme.begin(0x77)) {
    Serial.println("✓ BME688 初始化成功 (地址: 0x77)");
  } 
  else {
    Serial.println("✗ BME688 初始化失败! 请检查:");
    Serial.println("  1. 硬件连接是否正确");
    Serial.println("  2. ENV Pro 模块是否通电");
    Serial.println("  3. I2C 地址是否正确 (0x76 或 0x77)");
    return false;
  }

  // 配置传感器参数
  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme.setGasHeater(320, 150); // 320°C 加热 150ms

  Serial.println("传感器配置完成:");
  Serial.println("  - 温度过采样: 8x");
  Serial.println("  - 湿度过采样: 2x");
  Serial.println("  - 气压过采样: 4x");
  Serial.println("  - IIR 滤波器: 3");
  Serial.println("  - 气体加热器: 320°C / 150ms\n");

  return true;
}

/**
 * 在屏幕上显示传感器数据
 */
void displayOnScreen(float temp, float humi, float pres, float gas, float alt) {
  M5.Display.clear();
  M5.Display.setCursor(10, 10);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_WHITE);

  M5.Display.println("=== ENV Pro (BME688) ===");
  M5.Display.println();
  
  M5.Display.setTextColor(TFT_CYAN);
  M5.Display.printf("温度: %.2f C\n", temp);
  
  M5.Display.setTextColor(TFT_GREEN);
  M5.Display.printf("湿度: %.2f %%\n", humi);
  
  M5.Display.setTextColor(TFT_YELLOW);
  M5.Display.printf("气压: %.2f hPa\n", pres);
  
  M5.Display.setTextColor(TFT_ORANGE);
  M5.Display.printf("海拔: %.2f m\n", alt);
  
  M5.Display.setTextColor(TFT_MAGENTA);
  M5.Display.printf("气体: %.2f kOhm\n", gas / 1000.0);
  
  M5.Display.println();
  M5.Display.setTextColor(TFT_LIGHTGREY);
  M5.Display.setTextSize(1);
  M5.Display.println("按 BtnA 刷新 | BtnB 扫描I2C");
}

/**
 * 读取并显示传感器数据
 */
void readAndDisplay() {
  if (!sensorReady) {
    Serial.println("传感器未就绪!");
    return;
  }

  // 执行测量
  unsigned long startTime = millis();
  if (!bme.performReading()) {
    Serial.println("✗ 读取失败!");
    M5.Display.clear();
    M5.Display.setCursor(10, 50);
    M5.Display.setTextColor(TFT_RED);
    M5.Display.println("读取传感器失败!");
    return;
  }
  unsigned long readTime = millis() - startTime;

  // 获取数据
  float temperature = bme.temperature;
  float humidity = bme.humidity;
  float pressure = bme.pressure / 100.0; // Pa -> hPa
  float gasResistance = bme.gas_resistance;
  float altitude = bme.readAltitude(SEALEVELPRESSURE_HPA);

  // 串口输出
  Serial.println("\n╔════════════════════════════════════╗");
  Serial.println("║     BME688 环境传感器数据          ║");
  Serial.println("╠════════════════════════════════════╣");
  Serial.printf("║ 温度:     %6.2f °C             ║\n", temperature);
  Serial.printf("║ 湿度:     %6.2f %%              ║\n", humidity);
  Serial.printf("║ 气压:     %7.2f hPa           ║\n", pressure);
  Serial.printf("║ 气体阻值: %7.2f kΩ           ║\n", gasResistance / 1000.0);
  Serial.printf("║ 海拔高度: %7.2f m            ║\n", altitude);
  Serial.println("╠════════════════════════════════════╣");
  Serial.printf("║ 读取耗时: %3lu ms                 ║\n", readTime);
  Serial.println("╚════════════════════════════════════╝\n");

  // 屏幕显示
  displayOnScreen(temperature, humidity, pressure, gasResistance, altitude);
}

void setup() {
  // 初始化 M5Stack
  auto cfg = M5.config();
  M5.begin(cfg);
  
  // 初始化串口
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n");
  Serial.println("╔══════════════════════════════════════════╗");
  Serial.println("║  M5Stack CoreS3 + ENV Pro (BME688)      ║");
  Serial.println("║  环境传感器监测系统                      ║");
  Serial.println("╚══════════════════════════════════════════╝");
  Serial.println();

  // 屏幕欢迎信息
  M5.Display.setTextSize(2);
  M5.Display.setCursor(10, 50);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.println("BME688 初始化中...");

  // 初始化 I2C
  Wire.begin();
  delay(100);

  // 扫描 I2C 设备
  scanI2C();

  // 初始化 BME688
  sensorReady = initBME688();

  if (sensorReady) {
    M5.Display.clear();
    M5.Display.setCursor(10, 50);
    M5.Display.setTextColor(TFT_GREEN);
    M5.Display.println("BME688 就绪!");
    delay(1500);
    
    // 首次读取
    readAndDisplay();
  } else {
    M5.Display.clear();
    M5.Display.setCursor(10, 30);
    M5.Display.setTextColor(TFT_RED);
    M5.Display.println("BME688 初始化失败!");
    M5.Display.println();
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_YELLOW);
    M5.Display.println("请检查:");
    M5.Display.println("1. Grove 线缆连接");
    M5.Display.println("2. ENV Pro 模块供电");
    M5.Display.println("3. 查看串口日志");
  }

  Serial.println("系统启动完成!");
  Serial.println("提示: 传感器预热约 5-10 分钟后数据更稳定\n");
}

void loop() {
  M5.update();

  // 按钮 A: 手动刷新数据
  if (M5.BtnA.wasPressed()) {
    Serial.println(">> 手动刷新数据...");
    readAndDisplay();
  }

  // 按钮 B: 重新扫描 I2C
  if (M5.BtnB.wasPressed()) {
    Serial.println(">> 重新扫描 I2C 总线...");
    scanI2C();
  }

  // 按钮 C: 重新初始化传感器
  if (M5.BtnC.wasPressed()) {
    Serial.println(">> 重新初始化传感器...");
    sensorReady = initBME688();
    if (sensorReady) {
      readAndDisplay();
    }
  }

  // 每 5 秒自动更新一次
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 5000) {
    lastUpdate = millis();
    readAndDisplay();
  }

  delay(100);
}

// ============================================
// 可选: BSEC2 高级气体传感 (需要单独配置)
// ============================================
#ifdef USE_BSEC2
/*
 * BSEC2 (Bosch Sensortec Environmental Cluster 2.0)
 * 提供高级功能:
 *   - IAQ (室内空气质量指数, 0-500)
 *   - CO₂ 等效浓度
 *   - VOC 等效浓度
 *   - 气体精度指示
 * 
 * 启用步骤:
 *   1. platformio.ini 中添加:
 *      lib_deps += boschsensortec/BSEC2-Library
 *      build_flags += -D USE_BSEC2
 * 
 *   2. 替换上面的 Adafruit_BME680 为 BSEC2 API
 *   3. 配置 BSEC2 状态保存 (可选, 用于快速预热)
 * 
 * 注意: BSEC2 库需遵守 Bosch 许可协议
 */
#warning "BSEC2 已启用, 需要额外配置代码"
#include <bsec2.h>
// ... BSEC2 实现代码 ...
#endif
