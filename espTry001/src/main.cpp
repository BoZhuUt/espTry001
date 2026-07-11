#include <Arduino_GFX_Library.h>
#include <ModbusMaster.h> 
#include <Wire.h> 
#include "esp_lcd_touch_axs5106l.h"  // 引入同级目录下的微雪触摸库

// ==========================================
// 1. 硬件引脚与通信参数配置
// ==========================================
#define GFX_BL 46           // 屏幕背光

#define RS485_RX 5          // RS-485 接收
#define RS485_TX 7          // RS-485 发送
#define RS485_BAUDRATE 9600 

#define ADC_MA1_PIN 1       // mA1 采样
#define ADC_MA2_PIN 2       // mA2 采样

// 【触摸硬件引脚】
#define Touch_I2C_SDA  42
#define Touch_I2C_SCL  41
#define Touch_RST      47
#define Touch_INT      48

#define SLAVE_ID            1      
#define REG_START_ADDR      0x0000 
#define REG_COUNT           3      

// ==========================================
// 2. 前置函数声明与全局变量
// ==========================================
void lcd_reg_init();
void drawStaticUI();
void showTransferStatus(const char* text, uint16_t color);
void drawAuthorPage();
void initAXS5106L();               
bool checkTouchAXS(int &x, int &y);

int currentPage = 0; 
unsigned long lastSampleTime = 0;
unsigned long lastModbusTime = 0;
uint16_t modbusRegValues[REG_COUNT] = {0};
uint8_t modbusResult = 0xFF;               
bool touchOnline = false; 

// ==========================================
// 3. 实例化屏幕驱动对象
// ==========================================
Arduino_DataBus *bus = new Arduino_ESP32SPI(45 /* DC */, 21 /* CS */, 38 /* SCK */, 39 /* MOSI */);
Arduino_GFX *gfx = new Arduino_ST7789(
  bus, 40 /* RST */, 0 /* rotation */, false /* IPS */,
  172 /* width */, 320 /* height */,
  34 /* col_offset1 */, 0 /* row_offset1 */,
  34 /* col_offset2 */, 0 /* row_offset2 */);
ModbusMaster node; 

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
    gfx->setRotation(0);
#ifdef GFX_BL
    pinMode(GFX_BL, OUTPUT);
    digitalWrite(GFX_BL, HIGH);
#endif
    gfx->fillScreen(RGB565_BLACK);
    drawStaticUI();
  }

  // 2. 初始化串口与 485
  Serial1.begin(RS485_BAUDRATE, SERIAL_8E1, RS485_RX, RS485_TX);
  pinMode(RS485_RX, INPUT_PULLUP); 
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
  // 已修复：移除了不支持的 isConnected()，直接检测 Serial 状态
  bool isUsbConnected = false;
  if (Serial) {
    isUsbConnected = true;
  }

  // ----------------------------------------------------------------
  // 核心功能 1：AXS5106L 触摸事件检测
  // ----------------------------------------------------------------
  int tx = 0, ty = 0;
  if (touchOnline && checkTouchAXS(tx, ty)) {
    Serial.printf("Touch Event -> X: %d, Y: %d\n", tx, ty);
    if (ty < 80 && ty > 0) {
      gfx->fillScreen(RGB565_BLACK);
      if (currentPage == 0) {
        currentPage = 1;
        drawAuthorPage();
      } else {
        currentPage = 0;
        drawStaticUI();
      }
      delay(400); 
    }
  }

  // ----------------------------------------------------------------
  // 核心功能 2：后台双向透传与 Modbus 轮询
  // ----------------------------------------------------------------
  if (isUsbConnected) {
    if (Serial.available()) {
      while (Serial.available()) Serial1.write(Serial.read());
      if (currentPage == 0) showTransferStatus("USB -> RS485 (Pass)", RGB565_RED);
    }
    if (Serial1.available()) {
      while (Serial1.available()) Serial.write(Serial1.read());
      if (currentPage == 0) showTransferStatus("RS485 -> USB (Pass)", RGB565_BLUE);
    }
  } else {
    if (millis() - lastModbusTime > 1000) {
      lastModbusTime = millis();
      if (currentPage == 0) showTransferStatus("Modbus Polling...", RGB565_ORANGE);
      
      modbusResult = node.readHoldingRegisters(REG_START_ADDR, REG_COUNT);
      if (modbusResult == node.ku8MBSuccess) {
        for (int i = 0; i < REG_COUNT; i++) {
          modbusRegValues[i] = node.getResponseBuffer(i);
        }
      }
      if (currentPage == 0) gfx->fillRect(5, 295, 160, 20, RGB565_BLACK);
    }
  }

  // ----------------------------------------------------------------
  // 核心功能 3：UI 动态数据刷新
  // ----------------------------------------------------------------
  if (millis() - lastSampleTime > 500) {
    lastSampleTime = millis();
    int adc1 = analogRead(ADC_MA1_PIN);
    int adc2 = analogRead(ADC_MA2_PIN);
    float vol1_mv = (adc1 * 3100.0) / 4095.0;
    float vol2_mv = (adc2 * 3100.0) / 4095.0;
    float current1_ma = vol1_mv / 100.0; 
    float current2_ma = vol2_mv / 100.0;

    if (currentPage == 0) {
      gfx->setTextSize(1);
      gfx->setCursor(95, 48);
      if (isUsbConnected) {
        gfx->setTextColor(RGB565_MAGENTA, RGB565_BLACK);
        gfx->print("[SLAVE]   ");
      } else {
        gfx->setTextColor(RGB565_ORANGE, RGB565_BLACK);
        gfx->print("[MASTER]  ");
      }

      gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
      gfx->setCursor(5, 108);
      gfx->printf("CH1(IO1): %4.1f mA  ", current1_ma);
      gfx->setCursor(5, 142);
      gfx->printf("CH2(IO2): %4.1f mA  ", current2_ma);

      gfx->setCursor(5, 202);
      if (isUsbConnected) {
        // 已修复：改用库内标准英式拼写 DARKGREY
        gfx->setTextColor(DARKGREY, RGB565_BLACK);
        gfx->println("Link: USB PassThrough");
        gfx->setCursor(5, 217);
        gfx->println("Master is computer.   ");
      } else {
        if (modbusResult == node.ku8MBSuccess) {
          gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
          gfx->printf("Status: OK (Slave:%d) \n", SLAVE_ID);
          for (int i = 0; i < REG_COUNT; i++) {
            gfx->setCursor(5, 217 + i * 14);
            uint16_t val = modbusRegValues[i];
            if (val == 0) {
              gfx->setTextColor(RGB565_YELLOW, RGB565_BLACK);
              gfx->printf("Reg[%02X]: 0 (Empty)    ", REG_START_ADDR + i);
            } else {
              gfx->setTextColor(RGB565_GREEN, RGB565_BLACK);
              gfx->printf("Reg[%02X]: %u            ", REG_START_ADDR + i, val);
            }
          }
        } else {
          gfx->setTextColor(RGB565_RED, RGB565_BLACK);
          gfx->printf("Status: ERR (0x%02X)  \n", modbusResult);
        }
      }
    } 
    else if (currentPage == 1) {
      gfx->setTextSize(2);
      gfx->setTextColor(RGB565_RED, RGB565_BLACK);
      gfx->setCursor(10, 270);
      gfx->printf(" Time Flows");
    }
    
    gfx->flush();
  }

  static unsigned long lastClearTime = 0;
  if (isUsbConnected && (millis() - lastClearTime > 150)) {
    if (currentPage == 0) gfx->fillRect(5, 295, 160, 20, RGB565_BLACK);
    gfx->flush();
    lastClearTime = millis();
  }
}

// ==========================================
// 6. 微雪原厂 BSP 托管初始化
// ==========================================
void initAXS5106L() {
  Wire.begin(Touch_I2C_SDA, Touch_I2C_SCL);
  bsp_touch_init(&Wire, Touch_RST, Touch_INT, gfx->getRotation(), gfx->width(), gfx->height());
  touchOnline = true;
  Serial.println("AXS5106L Touch BSP Initialized.");
}

// ==========================================
// 7. 微雪原厂 BSP 托管读取
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
// 8. 页面静态 UI 渲染
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
  gfx->printf("V1.0.4\n");
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

void showTransferStatus(const char* text, uint16_t color) {
  gfx->fillRect(5, 295, 160, 20, color);
  gfx->setTextColor(RGB565_WHITE, color);
  gfx->setTextSize(1);
  gfx->setCursor(12, 301);
  gfx->print(text);
  gfx->flush();
}