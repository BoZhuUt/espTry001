#include <Arduino_GFX_Library.h>
#include <ModbusMaster.h> 
#include <Wire.h> 
#include "esp_lcd_touch_axs5106l.h"  // 引入同级目录下的微雪触摸库
#include "sensor_discovery.h"
#include "sensor_profile.h"

// ==========================================
// 1. 硬件引脚与通信参数配置
// ==========================================
#define GFX_BL 46           // 屏幕背光
#define DISPLAY_ROTATION 2  // 屏幕与触摸旋转 180 度

#define RS485_RX 43          // RS-485 接收（硬件 RO/R 接到 GPIO43）
#define RS485_TX 44          // RS-485 发送（硬件 DI/T 接到 GPIO44）
#define RS485_BAUDRATE 9600 
#define RS485_TX_TEST_MODE 0 // 1: GPIO44 连续发送 0x55；0: 正常透传/Modbus

#define ADC_MA1_PIN 1       // mA1 采样
#define ADC_MA2_PIN 2       // mA2 采样
#define ADC_SAMPLE_COUNT 16
#define SHUNT_RESISTOR_OHMS 100.0f

// 【触摸硬件引脚】
#define Touch_I2C_SDA  42
#define Touch_I2C_SCL  41
#define Touch_RST      47
#define Touch_INT      48

#define SLAVE_ID            15
#define REG_START_ADDR      46036  // 表中 46037，按 1-based 地址换算为协议地址 46036
#define REG_COUNT           4      // 连续读取 46037~46040，共两个 float
#define MODBUS_FLOAT_WORD_SWAP 1   // 0: 高字在前(ABCD)；1: 低字在前(CDAB)

#define LOCAL_SLAVE_ID           247
#define LOCAL_REG_COUNT          7
#define USB_MODBUS_MAX_FRAME     256
#define USB_MODBUS_FRAME_GAP_US  5000

// ==========================================
// 2. 前置函数声明与全局变量
// ==========================================
void lcd_reg_init();
void drawStaticUI();
void showTransferStatus(const char* text, uint16_t color);
void drawAuthorPage();
void drawSensorInfoPage();
void drawSensorParameterArea();
void initAXS5106L();               
bool checkTouchAXS(int &x, int &y);
void processUsbModbusFrame();
void sendLocalModbusResponse(const uint8_t *request, size_t length);
uint16_t modbusCrc16(const uint8_t *data, size_t length);
float readCalibratedMilliVolts(uint8_t pin);

enum class UiPage : uint8_t {
  Dashboard,
  SystemInfo,
  SensorInfo
};

UiPage currentPage = UiPage::Dashboard;
unsigned long lastSampleTime = 0;
unsigned long lastModbusTime = 0;
uint16_t modbusRegValues[REG_COUNT] = {0};
uint8_t modbusResult = 0xFF;               
bool touchOnline = false; 
bool masterMode = false; // false: USB-RS485 透传；true: Modbus 主站轮询
uint16_t adc1Raw = 0;
uint16_t adc2Raw = 0;
float current1Ma = 0.0f;
float current2Ma = 0.0f;
uint8_t usbModbusFrame[USB_MODBUS_MAX_FRAME] = {0};
size_t usbModbusFrameLength = 0;
unsigned long lastUsbByteTimeUs = 0;
const SensorProfile *activeProfile = nullptr;
bool profileLookupComplete = false;
float profileValues[kMaxSensorFields] = {0};
bool profileValueValid[kMaxSensorFields] = {false};
uint8_t parameterPage = 0;

float registersToFloat(uint16_t firstWord, uint16_t secondWord) {
#if MODBUS_FLOAT_WORD_SWAP
  uint32_t raw = ((uint32_t)secondWord << 16) | firstWord;
#else
  uint32_t raw = ((uint32_t)firstWord << 16) | secondWord;
#endif
  float value;
  memcpy(&value, &raw, sizeof(value));
  return value;
}

float readCalibratedMilliVolts(uint8_t pin) {
  uint32_t sum = 0;
  uint32_t minimum = UINT32_MAX;
  uint32_t maximum = 0;

  for (uint8_t i = 0; i < ADC_SAMPLE_COUNT; i++) {
    uint32_t millivolts = analogReadMilliVolts(pin);
    sum += millivolts;
    if (millivolts < minimum) minimum = millivolts;
    if (millivolts > maximum) maximum = millivolts;
  }

  // 去掉一个最大值和一个最小值，降低偶发尖峰对结果的影响。
  return (float)(sum - minimum - maximum) / (ADC_SAMPLE_COUNT - 2);
}

// ==========================================
// 3. 实例化屏幕驱动对象
// ==========================================
Arduino_DataBus *bus = new Arduino_ESP32SPI(45 /* DC */, 21 /* CS */, 38 /* SCK */, 39 /* MOSI */);
Arduino_GFX *gfx = new Arduino_ST7789(
  bus, 40 /* RST */, DISPLAY_ROTATION, false /* IPS */,
  172 /* width */, 320 /* height */,
  34 /* col_offset1 */, 0 /* row_offset1 */,
  34 /* col_offset2 */, 0 /* row_offset2 */);
ModbusMaster node; 
SensorDiscovery sensorDiscovery(Serial1);
SensorProfilePoller profilePoller(Serial1);

// ==========================================
// 4. 微雪官方原厂屏幕初始化微代码
// ==========================================
void lcd_reg_init(void) {
  static const uint8_t init_operations[] = {
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x11, 
    END_WRITE,
    DELAY, 120,

    BEGIN_WRITE,
    WRITE_C8_D16, 0xDF, 0x98, 0x53,
    WRITE_C8_D8, 0xB2, 0x23, 

    WRITE_COMMAND_8, 0xB7,
    WRITE_BYTES, 4,
    0x00, 0x47, 0x00, 0x6F,

    WRITE_COMMAND_8, 0xBB,
    WRITE_BYTES, 6,
    0x1C, 0x1A, 0x55, 0x73, 0x63, 0xF0,

    WRITE_C8_D16, 0xC0, 0x44, 0xA4,
    WRITE_C8_D8, 0xC1, 0x16, 

    WRITE_COMMAND_8, 0xC3,
    WRITE_BYTES, 8,
    0x7D, 0x07, 0x14, 0x06, 0xCF, 0x71, 0x72, 0x77,

    WRITE_COMMAND_8, 0xC4,
    WRITE_BYTES, 12,
    0x00, 0x00, 0xA0, 0x79, 0x0B, 0x0A, 0x16, 0x79, 0x0B, 0x0A, 0x16, 0x82,

    WRITE_COMMAND_8, 0xC8,
    WRITE_BYTES, 32,
    0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28, 0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00, 0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28, 0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,

    WRITE_COMMAND_8, 0xD0,
    WRITE_BYTES, 5,
    0x04, 0x06, 0x6B, 0x0F, 0x00,

    WRITE_C8_D16, 0xD7, 0x00, 0x30,
    WRITE_C8_D8, 0xE6, 0x14, 
    WRITE_C8_D8, 0xDE, 0x01, 

    WRITE_COMMAND_8, 0xB7,
    WRITE_BYTES, 5,
    0x03, 0x13, 0xEF, 0x35, 0x35,

    WRITE_COMMAND_8, 0xC1,
    WRITE_BYTES, 3,
    0x14, 0x15, 0xC0,

    WRITE_C8_D16, 0xC2, 0x06, 0x3A,
    WRITE_C8_D16, 0xC4, 0x72, 0x12,   
    WRITE_C8_D8, 0xBE, 0x00, 
    WRITE_C8_D8, 0xDE, 0x02, 

    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 3,
    0x00, 0x02, 0x00,

    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 3,
    0x01, 0x02, 0x00,

    WRITE_C8_D8, 0xDE, 0x00, 
    WRITE_C8_D8, 0x35, 0x00, 
    WRITE_C8_D8, 0x3A, 0x05, 

    WRITE_COMMAND_8, 0x2A,
    WRITE_BYTES, 4,
    0x00, 0x22, 0x00, 0xCD,

    WRITE_COMMAND_8, 0x2B,
    WRITE_BYTES, 4,
    0x00, 0x00, 0x01, 0x3F,

    WRITE_C8_D8, 0xDE, 0x02, 

    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 3,
    0x00, 0x02, 0x00,
    
    WRITE_C8_D8, 0xDE, 0x00, 
    WRITE_C8_D8, 0x36, 0x60, // 镜像控制
    WRITE_COMMAND_8, 0x21,
    END_WRITE,
    
    DELAY, 10,

    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x29,  
    END_WRITE
  };
  bus->batchOperation(init_operations, sizeof(init_operations));
}

// ==========================================
// 5. 核心逻辑主程序
// ==========================================
void setup()
{
  Serial.begin(115200); 
  delay(100);

  // 1. 屏幕底层硬件初始化
  if (gfx->begin()) {
    lcd_reg_init();   
    gfx->setRotation(DISPLAY_ROTATION);
#ifdef GFX_BL
    pinMode(GFX_BL, OUTPUT);
    digitalWrite(GFX_BL, HIGH);
#endif
    gfx->fillScreen(RGB565_BLACK);
    drawStaticUI();
  }

  // 2. 初始化串口与 485
  pinMode(RS485_RX, INPUT_PULLUP);
  Serial1.begin(RS485_BAUDRATE, SERIAL_8E1, RS485_RX, RS485_TX); 
  node.begin(SLAVE_ID, Serial1);

  // 3. 初始化 ADC
  pinMode(ADC_MA1_PIN, INPUT);
  pinMode(ADC_MA2_PIN, INPUT);
  analogSetAttenuation(ADC_11db); 

  // 4. 初始化触摸屏
  initAXS5106L();
}

void loop()
{
#if RS485_TX_TEST_MODE
  // 示波器发送通道测试：9600、8E1 连续发送 0x55。
  // write() 在发送缓冲区满时会等待，因此输出为连续、稳定的 UART 波形。
  Serial1.write(0x55);
  return;
#endif

  // ----------------------------------------------------------------
  // 核心功能 1：AXS5106L 触摸事件检测
  // ----------------------------------------------------------------
  int tx = 0, ty = 0;
  if (touchOnline && checkTouchAXS(tx, ty)) {
#if TOUCH_DEBUG_OUTPUT
    Serial.printf("Touch Event -> X: %d, Y: %d\n", tx, ty);
#endif
    if (currentPage == UiPage::Dashboard && ty >= 40 && ty < 80) {
      // 点击 Mode 区域，在纯透传与 Modbus 主站之间手动切换。
      masterMode = !masterMode;
      while (Serial.available()) Serial.read();
      while (Serial1.available()) Serial1.read();
      usbModbusFrameLength = 0;
      modbusResult = 0xFF;
      lastModbusTime = 0;
      activeProfile = nullptr;
      profilePoller.cancel();
      profileLookupComplete = false;
      parameterPage = 0;
      memset(profileValueValid, 0, sizeof(profileValueValid));
      if (masterMode) {
        sensorDiscovery.start();
      } else {
        sensorDiscovery.cancel();
      }
      gfx->fillScreen(RGB565_BLACK);
      drawStaticUI();
      delay(400);
    } else if (currentPage == UiPage::Dashboard && ty >= 280 &&
               masterMode && sensorDiscovery.state() == SensorDiscovery::State::Success) {
      currentPage = UiPage::SensorInfo;
      parameterPage = 0;
      drawSensorInfoPage();
      delay(400);
    } else if (currentPage == UiPage::Dashboard && ty > 0 && ty < 40) {
      gfx->fillScreen(RGB565_BLACK);
      currentPage = UiPage::SystemInfo;
      drawAuthorPage();
      delay(400);
    } else if (currentPage == UiPage::SensorInfo && ty >= 280) {
      if (activeProfile != nullptr && activeProfile->fieldCount > 4) {
        parameterPage = parameterPage == 0 ? 1 : 0;
        drawSensorParameterArea();
        gfx->flush();
      }
      delay(400);
    } else if ((currentPage == UiPage::SystemInfo && ty > 0 && ty < 80) ||
               (currentPage == UiPage::SensorInfo && ty > 0 && ty < 40)) {
      gfx->fillScreen(RGB565_BLACK);
      currentPage = UiPage::Dashboard;
      drawStaticUI();
      delay(400); 
    }
  }

  // ----------------------------------------------------------------
  // 核心功能 2：后台双向透传与 Modbus 轮询
  // ----------------------------------------------------------------
  // USB 侧先按 RTU 帧间隔收包：ID 247 本地响应，其余 ID 在 PASS 模式下转发。
  while (Serial.available()) {
    uint8_t value = Serial.read();
    if (usbModbusFrameLength < USB_MODBUS_MAX_FRAME) {
      usbModbusFrame[usbModbusFrameLength++] = value;
    } else {
      // 超长帧无效，丢弃并重新同步下一帧。
      usbModbusFrameLength = 0;
    }
    lastUsbByteTimeUs = micros();
  }
  if (usbModbusFrameLength > 0 &&
      (unsigned long)(micros() - lastUsbByteTimeUs) >= USB_MODBUS_FRAME_GAP_US) {
    processUsbModbusFrame();
  }

  if (!masterMode) {
    if (Serial1.available()) {
      while (Serial1.available()) Serial.write(Serial1.read());
      if (currentPage == UiPage::Dashboard) showTransferStatus("RS485 -> USB (Pass)", RGB565_BLUE);
    }
  }
  else {
    sensorDiscovery.update();
    if (sensorDiscovery.state() == SensorDiscovery::State::Success) {
      if (!profileLookupComplete) {
        activeProfile = findSensorProfile(sensorDiscovery.result().productNum);
        profileLookupComplete = true;
        if (activeProfile != nullptr) {
          profilePoller.begin(sensorDiscovery.result().slaveAddress,
                              *activeProfile, profileValues,
                              profileValueValid);
        }
      }

      if (activeProfile != nullptr && profilePoller.update(modbusResult)) {
        if (currentPage == UiPage::SensorInfo) {
          drawSensorParameterArea();
          gfx->flush();
        }
      }
    }
  }

  // ----------------------------------------------------------------
  // 核心功能 3：UI 动态数据刷新
  // ----------------------------------------------------------------
  if (millis() - lastSampleTime > 500) {
    lastSampleTime = millis();
    adc1Raw = analogRead(ADC_MA1_PIN);
    adc2Raw = analogRead(ADC_MA2_PIN);
    float vol1_mv = readCalibratedMilliVolts(ADC_MA1_PIN);
    float vol2_mv = readCalibratedMilliVolts(ADC_MA2_PIN);
    current1Ma = vol1_mv / SHUNT_RESISTOR_OHMS;
    current2Ma = vol2_mv / SHUNT_RESISTOR_OHMS;

    if (currentPage == UiPage::Dashboard) {
      gfx->setTextSize(1);
      gfx->setCursor(95, 48);
      if (!masterMode) {
        gfx->setTextColor(RGB565_MAGENTA, RGB565_BLACK);
        gfx->print("[PASS]    ");
      } else {
        gfx->setTextColor(RGB565_ORANGE, RGB565_BLACK);
        gfx->print("[MASTER]  ");
      }

      gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
      gfx->setTextSize(2);
      gfx->setCursor(5, 104);
      gfx->printf("CH1:%4.1fmA ", current1Ma);
      gfx->setCursor(5, 138);
      gfx->printf("CH2:%4.1fmA ", current2Ma);

      gfx->setTextSize(1);
      gfx->setCursor(5, 202);
      if (!masterMode) {
        // 已修复：改用库内标准英式拼写 DARKGREY
        gfx->setTextColor(DARKGREY, RGB565_BLACK);
        gfx->println("Link: USB PassThrough");
        gfx->setCursor(5, 217);
        gfx->println("Master is computer.   ");
      } else {
        gfx->fillRect(5, 200, 165, 115, RGB565_BLACK);
        switch (sensorDiscovery.state()) {
          case SensorDiscovery::State::Idle:
            gfx->setTextColor(RGB565_ORANGE, RGB565_BLACK);
            gfx->print("Discovery idle");
            break;

          case SensorDiscovery::State::DiscoveringAddress:
            gfx->setTextColor(RGB565_YELLOW, RGB565_BLACK);
            gfx->print("Discovering address...");
            gfx->setCursor(5, 217);
            gfx->printf("Try: %u/%u", sensorDiscovery.attempt(),
                        sensorDiscovery.maxAttempts());
            gfx->setCursor(5, 232);
            gfx->print("TX: 00 03 A028 0001");
            break;

          case SensorDiscovery::State::ReadingIdentity:
            gfx->setTextColor(RGB565_CYAN, RGB565_BLACK);
            gfx->print("Reading identity...");
            gfx->setCursor(5, 217);
            gfx->printf("Addr: %u  Try: %u/%u",
                        sensorDiscovery.result().slaveAddress,
                        sensorDiscovery.attempt(),
                        sensorDiscovery.maxAttempts());
            gfx->setCursor(5, 232);
            gfx->print("Regs: 41001-41014");
            break;

          case SensorDiscovery::State::Success:
            gfx->setTextColor(RGB565_GREEN, RGB565_BLACK);
            gfx->printf("Sensor found  Addr:%u", sensorDiscovery.result().slaveAddress);
            gfx->setCursor(5, 220);
            gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
            gfx->print("Name: ");
            if (sensorDiscovery.result().deviceName[0] != '\0') {
              gfx->print(sensorDiscovery.result().deviceName);
            } else {
              gfx->print("(empty)");
            }
            gfx->setCursor(5, 237);
            gfx->printf("PN: %lu", (unsigned long)sensorDiscovery.result().productNum);
            gfx->fillRect(5, 286, 165, 29, RGB565_BLUE);
            gfx->setTextColor(RGB565_WHITE, RGB565_BLUE);
            gfx->setCursor(28, 296);
            gfx->print("Tap for details >");
            break;

          case SensorDiscovery::State::Failed:
            gfx->setTextColor(RGB565_RED, RGB565_BLACK);
            gfx->print("Discovery failed");
            gfx->setCursor(5, 217);
            gfx->print(sensorDiscovery.errorText());
            if (sensorDiscovery.error() == SensorDiscovery::Error::ExceptionResponse) {
              gfx->setCursor(5, 232);
              gfx->printf("Exception: 0x%02X", sensorDiscovery.exceptionCode());
            }
            break;
        }
      }
    } 
    else if (currentPage == UiPage::SystemInfo) {
      gfx->setTextSize(2);
      gfx->setTextColor(RGB565_RED, RGB565_BLACK);
      gfx->setCursor(10, 270);
      gfx->printf(" Time Flows");
    }
    
    gfx->flush();
  }

  static unsigned long lastClearTime = 0;
  if (!masterMode && (millis() - lastClearTime > 150)) {
    if (currentPage == UiPage::Dashboard) gfx->fillRect(5, 295, 160, 20, RGB565_BLACK);
    gfx->flush();
    lastClearTime = millis();
  }
}

// ==========================================
// 6. USB Modbus RTU 网关与本地模拟量从站
// ==========================================
uint16_t modbusCrc16(const uint8_t *data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 0x0001) ? ((crc >> 1) ^ 0xA001) : (crc >> 1);
    }
  }
  return crc;
}

static void floatToModbusRegisters(float value, uint16_t &firstWord, uint16_t &secondWord) {
  uint32_t raw;
  memcpy(&raw, &value, sizeof(raw));
#if MODBUS_FLOAT_WORD_SWAP
  firstWord = raw & 0xFFFF;
  secondWord = raw >> 16;
#else
  firstWord = raw >> 16;
  secondWord = raw & 0xFFFF;
#endif
}

static void sendLocalModbusException(uint8_t function, uint8_t exceptionCode) {
  uint8_t response[5] = {LOCAL_SLAVE_ID, (uint8_t)(function | 0x80), exceptionCode, 0, 0};
  uint16_t crc = modbusCrc16(response, 3);
  response[3] = crc & 0xFF;
  response[4] = crc >> 8;
  Serial.write(response, sizeof(response));
}

void sendLocalModbusResponse(const uint8_t *request, size_t length) {
  if (length < 4) return;

  uint16_t receivedCrc = request[length - 2] | ((uint16_t)request[length - 1] << 8);
  if (modbusCrc16(request, length - 2) != receivedCrc) return;

  uint8_t function = request[1];
  if (function != 0x03) {
    sendLocalModbusException(function, 0x01); // Illegal Function
    return;
  }
  if (length != 8) return;

  uint16_t startAddress = ((uint16_t)request[2] << 8) | request[3];
  uint16_t quantity = ((uint16_t)request[4] << 8) | request[5];
  if (quantity == 0 || quantity > 125) {
    sendLocalModbusException(function, 0x03); // Illegal Data Value
    return;
  }
  if (startAddress >= LOCAL_REG_COUNT || quantity > LOCAL_REG_COUNT - startAddress) {
    sendLocalModbusException(function, 0x02); // Illegal Data Address
    return;
  }

  uint16_t localRegisters[LOCAL_REG_COUNT] = {0};
  floatToModbusRegisters(current1Ma, localRegisters[0], localRegisters[1]);
  floatToModbusRegisters(current2Ma, localRegisters[2], localRegisters[3]);
  localRegisters[4] = masterMode ? 1 : 0;
  localRegisters[5] = adc1Raw;
  localRegisters[6] = adc2Raw;

  uint8_t response[3 + LOCAL_REG_COUNT * 2 + 2] = {0};
  response[0] = LOCAL_SLAVE_ID;
  response[1] = function;
  response[2] = quantity * 2;
  size_t responseLength = 3;
  for (uint16_t i = 0; i < quantity; i++) {
    uint16_t value = localRegisters[startAddress + i];
    response[responseLength++] = value >> 8;
    response[responseLength++] = value & 0xFF;
  }
  uint16_t crc = modbusCrc16(response, responseLength);
  response[responseLength++] = crc & 0xFF;
  response[responseLength++] = crc >> 8;
  Serial.write(response, responseLength);
}

void processUsbModbusFrame() {
  if (usbModbusFrameLength == 0) return;

  if (usbModbusFrame[0] == LOCAL_SLAVE_ID) {
    sendLocalModbusResponse(usbModbusFrame, usbModbusFrameLength);
  } else if (!masterMode) {
    // 非本地地址保持透明转发，CRC 与功能码由下游设备处理。
    Serial1.write(usbModbusFrame, usbModbusFrameLength);
    Serial1.flush();
    if (currentPage == UiPage::Dashboard) showTransferStatus("USB -> RS485 (Pass)", RGB565_RED);
  }
  usbModbusFrameLength = 0;
}

// ==========================================
// 7. 微雪原厂 BSP 托管初始化
// ==========================================
void initAXS5106L() {
  Wire.begin(Touch_I2C_SDA, Touch_I2C_SCL);
  bsp_touch_init(&Wire, Touch_RST, Touch_INT, gfx->getRotation(), gfx->width(), gfx->height());
  touchOnline = true;
#if TOUCH_DEBUG_OUTPUT
  Serial.println("AXS5106L Touch BSP Initialized.");
#endif
}

// ==========================================
// 8. 微雪原厂 BSP 托管读取
// ==========================================
bool checkTouchAXS(int &x, int &y) {
  touch_data_t touch_data;
  bsp_touch_read();
  bool touchpad_pressed = bsp_touch_get_coordinates(&touch_data);

  if (touchpad_pressed) {
    x = touch_data.coords[0].x;
    y = touch_data.coords[0].y;
    return true;
  }
  return false;
}

// ==========================================
// 9. 页面静态 UI 渲染
// ==========================================
void drawStaticUI() {
  gfx->setTextColor(RGB565_GREEN, RGB565_BLACK);
  gfx->setTextSize(2);                           
  gfx->setCursor(5, 15);                        
  gfx->println("ESP32-S3 485");
  gfx->drawLine(5, 38, 165, 38, RGB565_YELLOW);
  
  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
  gfx->setTextSize(1);                           
  gfx->setCursor(5, 48);
  gfx->printf("Mode: ");
  gfx->setCursor(5, 62);
  gfx->printf("485 Config: 9600,8E1\n");
  gfx->drawLine(5, 78, 165, 78, RGB565_YELLOW);
  gfx->setTextColor(RGB565_CYAN, RGB565_BLACK);
  gfx->setCursor(5, 88);
  gfx->println("[ 4-20mA Sampling ]");
  gfx->drawLine(5, 175, 165, 175, RGB565_YELLOW);
  gfx->setTextColor(RGB565_CYAN, RGB565_BLACK);
  gfx->setCursor(5, 185);
  gfx->println("[ Modbus RTU Data ]");
}

void drawAuthorPage() {
  gfx->setTextColor(RGB565_YELLOW, RGB565_BLACK);
  gfx->setTextSize(2);                           
  gfx->setCursor(5, 15);                        
  gfx->println("SYSTEM INFO");
  gfx->drawLine(5, 38, 165, 38, RGB565_GREEN);

  gfx->setTextSize(2);
  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
  
  gfx->setCursor(5, 60);
  gfx->printf("Author: Bo\n"); 
  
  gfx->setCursor(5, 100);
 #ifdef AUTO_VERSION
    gfx->printf("%s\n", AUTO_VERSION); // 自动打印 Git 编译的版本号
  #else
    gfx->printf("V1.0.1\n");           // 保底显示
  #endif

  gfx->setCursor(5, 140);
  gfx->printf("esp32-utility\n");

  gfx->setTextSize(1);
  gfx->setCursor(5, 180);
  gfx->setTextColor(RGB565_CYAN, RGB565_BLACK);
  gfx->printf("Contact: Info Support\n");

  // 已修复：改用库内标准英式拼写 RGB565_DARKGREY
  gfx->drawLine(5, 210, 165, 210, RGB565_DARKGREY);
  gfx->setCursor(5, 230);
  gfx->setTextColor(RGB565_GREEN, RGB565_BLACK);
  gfx->println("Click Header To Return");
}

void drawSensorInfoPage() {
  const SensorDiscovery::Result &identity = sensorDiscovery.result();

  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextColor(RGB565_CYAN, RGB565_BLACK);
  gfx->setTextSize(2);
  gfx->setCursor(5, 15);
  gfx->println("SENSOR INFO");
  gfx->drawLine(5, 38, 165, 38, RGB565_GREEN);

  gfx->setTextSize(1);
  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
  gfx->setCursor(5, 52);
  gfx->printf("Address: %u", identity.slaveAddress);

  gfx->setCursor(5, 72);
  gfx->setTextColor(RGB565_YELLOW, RGB565_BLACK);
  gfx->print("Product Number:");
  gfx->setCursor(5, 86);
  gfx->setTextSize(2);
  gfx->printf("%lu", (unsigned long)identity.productNum);

  gfx->setTextSize(1);
  gfx->setCursor(5, 110);
  gfx->setTextColor(RGB565_YELLOW, RGB565_BLACK);
  gfx->print("Device Name:");
  gfx->setCursor(5, 126);
  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
  gfx->print(identity.deviceName[0] != '\0' ? identity.deviceName : "(empty)");

  drawSensorParameterArea();
  gfx->flush();
}

void drawSensorParameterArea() {
  gfx->fillRect(0, 148, 172, 172, RGB565_BLACK);
  gfx->drawLine(5, 148, 165, 148, RGB565_DARKGREY);
  gfx->setTextSize(1);

  if (activeProfile == nullptr) {
    gfx->setTextColor(RGB565_RED, RGB565_BLACK);
    gfx->setCursor(5, 165);
    gfx->print("Unsupported PN");
    gfx->setCursor(5, 185);
    gfx->print("No parameter profile");
  } else {
    uint8_t startIndex = parameterPage * 4;
    for (uint8_t row = 0; row < 4; row++) {
      uint8_t fieldIndex = startIndex + row;
      if (fieldIndex >= activeProfile->fieldCount) break;

      const SensorField &field = activeProfile->fields[fieldIndex];
      gfx->setCursor(5, 162 + row * 25);
      gfx->setTextColor(RGB565_CYAN, RGB565_BLACK);
      gfx->printf("%s: ", field.label);
      if (!profileValueValid[fieldIndex]) {
        gfx->setTextColor(RGB565_RED, RGB565_BLACK);
        gfx->print("--");
      } else {
        gfx->setTextColor(RGB565_GREEN, RGB565_BLACK);
        switch (field.decimals) {
          case 0: gfx->printf("%.0f", profileValues[fieldIndex]); break;
          case 1: gfx->printf("%.1f", profileValues[fieldIndex]); break;
          case 2: gfx->printf("%.2f", profileValues[fieldIndex]); break;
          default: gfx->printf("%.3f", profileValues[fieldIndex]); break;
        }
      }
    }
  }

  gfx->drawLine(5, 270, 165, 270, RGB565_DARKGREY);
  gfx->setTextColor(RGB565_YELLOW, RGB565_BLACK);
  gfx->setCursor(12, 282);
  if (activeProfile != nullptr && activeProfile->fieldCount > 4) {
    uint8_t pageCount = (activeProfile->fieldCount + 3) / 4;
    gfx->printf("Page %u/%u - tap bottom", parameterPage + 1, pageCount);
  } else {
    gfx->print("No more parameters");
  }
  gfx->setTextColor(RGB565_CYAN, RGB565_BLACK);
  gfx->setCursor(45, 302);
  gfx->print("Header = back");
}

void showTransferStatus(const char* text, uint16_t color) {
  gfx->fillRect(5, 295, 160, 20, color);
  gfx->setTextColor(RGB565_WHITE, color);
  gfx->setTextSize(1);
  gfx->setCursor(12, 301);
  gfx->print(text);
  gfx->flush();
}
