#include <ModbusMaster.h> 
#include "board/analog_input.h"
#include "board/board_config.h"
#include "board/display_driver.h"
#include "board/touch_driver.h"
#include "sensor_discovery.h"
#include "sensor_profile.h"
#include "sensor_profile_store.h"
#include "sensor_profile_update.h"
#include "sd_storage.h"
#include "data_logger.h"
#include "log_export_server.h"
#include "ui/ui_input.h"
#include "ui/ui_manager.h"

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
void drawAuthorPage();
void drawSensorInfoPage();
void drawSensorParameterArea();
SensorInfoView buildSensorInfoView();
DashboardView buildDashboardView();
void enterConfigMode();
void drawConfigUpdatePage();
void enterToolsMenu();
void enterLogExportMode();
void drawLogExportPage();
void processUsbModbusFrame();
void sendLocalModbusResponse(const uint8_t *request, size_t length);
uint16_t modbusCrc16(const uint8_t *data, size_t length);

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
bool configMode = false;
bool logExportMode = false;
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

ModbusMaster node; 
SensorDiscovery sensorDiscovery(Serial1);
SensorProfilePoller profilePoller(Serial1);
SensorProfileStore profileStore;
SensorProfileUpdater profileUpdater;
SdStorage sdStorage;
DataLogger dataLogger(sdStorage);
LogExportServer logExportServer;
UiManager ui;

// SD 卡模拟数据测试：无传感器时每秒递增并写入
#define SD_SIMULATE_LOG 0  // 1: synthetic SD test; 0: log real MASTER scans.
#if SD_SIMULATE_LOG
static constexpr uint32_t kSimulatedProductNum = 53607;
static uint32_t simScanSeq = 0;
static uint32_t simLastLogMs = 0;
static constexpr uint32_t simLogIntervalMs = 1000;  // 模拟每秒一条
static float simValues[kMaxSensorFields] = {0};
static bool simValid[kMaxSensorFields] = {false};
#endif

// ==========================================
// 5. 核心逻辑主程序
// ==========================================
void setup()
{
  Serial.begin(115200); 
  delay(100);

  // 优先加载 LittleFS JSON；挂载、文件或校验失败时自动使用固件内置表。
  profileStore.begin();
#ifdef AUTO_VERSION
  dataLogger.setMetadata(profileStore.configVersion(), AUTO_VERSION);
#else
  dataLogger.setMetadata(profileStore.configVersion(), "development");
#endif

  // 1. 屏幕底层硬件初始化
  if (beginBoardDisplay()) {
    ui.drawDashboardStatic(masterMode);
  }

  // 2. 初始化串口与 485
  pinMode(BoardConfig::kRs485Rx, INPUT_PULLUP);
  Serial1.begin(BoardConfig::kRs485Baudrate, SERIAL_8E1,
                BoardConfig::kRs485Rx, BoardConfig::kRs485Tx);
  node.begin(SLAVE_ID, Serial1);

  // 3. 初始化 ADC
  BoardAnalog::begin();

  // 4. 初始化触摸屏
  touchOnline = BoardTouch::begin(
      gfx->getRotation(), gfx->width(), gfx->height());

  // 5. 初始化 SD 卡
  if (sdStorage.begin()) {
    Serial.println("[SD] SD card initialized OK");
#if SD_SIMULATE_LOG
    // 模拟数据测试：使用内置 Profile 第一个 PN 构造模拟日志
    const SensorProfile *simProfile = profileStore.find(kSimulatedProductNum);
    if (simProfile == nullptr) {
      // 如果配置中没有 53607，取第一个
      simProfile = findSensorProfile(kSimulatedProductNum);
    }
    if (simProfile != nullptr) {
      dataLogger.setIdentity(15, simProfile->productNum, simProfile->name, simProfile);
      for (uint8_t i = 0; i < simProfile->fieldCount; i++) {
        simValid[i] = true;
      }
      dataLogger.start();
      simLastLogMs = millis();
    }
#endif
  } else {
    Serial.println("[SD] No SD card, logging disabled");
  }
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
  if (touchOnline && BoardTouch::read(tx, ty)) {
#if TOUCH_DEBUG_OUTPUT
    Serial.printf("Touch Event -> X: %d, Y: %d\n", tx, ty);
#endif
    UiInputContext inputContext;
    inputContext.configMode = configMode;
    inputContext.sensorDetailsAvailable =
        masterMode &&
        sensorDiscovery.state() == SensorDiscovery::State::Success;
    UiAction action = resolveTouchAction(currentPage, tx, ty, inputContext);

    switch (action) {
      case UiAction::CancelConfigAndRestart:
        // CONFIG 模式退出时重启，重新加载最后一个有效配置。
        profileUpdater.cancel();
        Serial.flush();
        delay(50);
        ESP.restart();
        break;

      case UiAction::TogglePassMaster:
        // Finish the current CSV before changing sensor/discovery state.
        dataLogger.stop();
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
        ui.clearScreen();
        ui.drawDashboardStatic(masterMode);
        delay(400);
        break;

      case UiAction::OpenSensorInfo:
        currentPage = UiPage::SensorInfo;
        parameterPage = 0;
        drawSensorInfoPage();
        delay(400);
        break;

      case UiAction::OpenSystemInfo:
        ui.clearScreen();
        currentPage = UiPage::SystemInfo;
        drawAuthorPage();
        delay(400);
        break;

      case UiAction::OpenToolsMenu:
        ui.clearScreen();
        currentPage = UiPage::ToolsMenu;
        ui.drawToolsMenuPage();
        delay(400);
        break;

      case UiAction::EnterConfigUpload:
        enterConfigMode();
        delay(400);
        break;

      case UiAction::EnterLogExport:
        enterLogExportMode();
        delay(400);
        break;

      case UiAction::BackToSystemInfo:
        if (currentPage == UiPage::LogExport) {
          logExportServer.cancel();
          logExportMode = false;
        }
        ui.clearScreen();
        currentPage = UiPage::SystemInfo;
        drawAuthorPage();
        delay(400);
        break;

      case UiAction::NextSensorParameterPage:
        if (activeProfile != nullptr && activeProfile->fieldCount > 4) {
          uint8_t pageCount = (activeProfile->fieldCount + 3) / 4;
          parameterPage = (parameterPage + 1) % pageCount;
          drawSensorParameterArea();
          ui.flush();
        }
        delay(400);
        break;

      case UiAction::BackToDashboard:
        ui.clearScreen();
        currentPage = UiPage::Dashboard;
        ui.drawDashboardStatic(masterMode);
        delay(400);
        break;

      case UiAction::None:
        break;
    }
  }

  // CONFIG 模式独占 USB CDC，禁止 JSON 被本地 Modbus 解析或转发到 RS485。
  if (configMode) {
    profileUpdater.update(Serial);
    static SensorProfileUpdater::State lastUpdateState =
        SensorProfileUpdater::State::Idle;
    static uint32_t lastUpdateLength = UINT32_MAX;
    static uint32_t lastUpdateDrawMs = 0;
    SensorProfileUpdater::State state = profileUpdater.state();
    uint32_t received = profileUpdater.receivedLength();
    bool stateChanged = state != lastUpdateState;
    bool progressDue = received != lastUpdateLength &&
                       (uint32_t)(millis() - lastUpdateDrawMs) >= 200;
    if (stateChanged || progressDue) {
      drawConfigUpdatePage();
      lastUpdateState = state;
      lastUpdateLength = received;
      lastUpdateDrawMs = millis();
    }
    if (profileUpdater.shouldRestart()) {
      Serial.flush();
      delay(50);
      ESP.restart();
    }
    delay(1);
    return;
  }

  // LOG EXPORT 模式独占 USB CDC，禁止 Modbus 透传和本地从站响应。
  if (logExportMode) {
    logExportServer.update(Serial);
    static LogExportServer::State lastExportState =
        LogExportServer::State::Idle;
    static uint32_t lastExportDrawMs = 0;
    LogExportServer::State exportState = logExportServer.state();
    bool stateChanged = exportState != lastExportState;
    bool progressDue = (uint32_t)(millis() - lastExportDrawMs) >= 300;
    if (stateChanged || progressDue) {
      drawLogExportPage();
      lastExportState = exportState;
      lastExportDrawMs = millis();
    }
    if (logExportServer.shouldExit()) {
      logExportServer.cancel();
      logExportMode = false;
      ui.clearScreen();
      currentPage = UiPage::SystemInfo;
      drawAuthorPage();
    }
    delay(1);
    return;
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
      if (currentPage == UiPage::Dashboard) ui.showTransferStatus("RS485 -> USB (Pass)", RGB565_BLUE);
    }
  }
  else {
    sensorDiscovery.update();
    if (sensorDiscovery.state() == SensorDiscovery::State::Success) {
      if (!profileLookupComplete) {
        activeProfile = profileStore.find(sensorDiscovery.result().productNum);
        profileLookupComplete = true;
        if (activeProfile != nullptr) {
          profilePoller.begin(sensorDiscovery.result().slaveAddress,
                              *activeProfile, profileValues,
                              profileValueValid);
          // One logging session belongs to one discovered address/PN/Profile.
          // start() safely does nothing when the SD card is unavailable.
          dataLogger.setIdentity(sensorDiscovery.result().slaveAddress,
                                 sensorDiscovery.result().productNum,
                                 activeProfile->name, activeProfile);
          dataLogger.start();
        }
      }

      if (activeProfile != nullptr && profilePoller.update(modbusResult)) {
        if (currentPage == UiPage::SensorInfo) {
          drawSensorParameterArea();
          ui.flush();
        }
      }

      // Real logging is disabled while the dedicated SD simulation is enabled,
      // so actual sensor rows cannot be mixed into the synthetic test file.
#if !SD_SIMULATE_LOG
      if (activeProfile != nullptr && profilePoller.takeScanCompleted()) {
        dataLogger.onScanComplete(
            profilePoller.scanSequence(),
            profileValues, profileValueValid, activeProfile->fieldCount,
            current1Ma, current2Ma);
      }
#else
      profilePoller.takeScanCompleted();
#endif
    }
  }

  // ----------------------------------------------------------------
  // SD 卡模拟数据测试
  // ----------------------------------------------------------------
#if SD_SIMULATE_LOG
  if (dataLogger.isRecording() && !masterMode) {
    uint32_t now = millis();
    if ((uint32_t)(now - simLastLogMs) >= simLogIntervalMs) {
      simLastLogMs = now;
      simScanSeq++;
      // 模拟数据：每个字段递增
      const SensorProfile *simProfile = profileStore.find(kSimulatedProductNum);
      if (simProfile == nullptr) simProfile = findSensorProfile(kSimulatedProductNum);
      if (simProfile != nullptr) {
        for (uint8_t i = 0; i < simProfile->fieldCount; i++) {
          simValues[i] = (float)(simScanSeq * simProfile->fieldCount + i) * 0.1f;
          simValid[i] = true;
        }
        dataLogger.onScanComplete(simScanSeq, simValues, simValid,
                                  simProfile->fieldCount, current1Ma, current2Ma);
      }
    }
  }
#endif

  // SD 日志写入推进（非阻塞）
  dataLogger.update();

  // ----------------------------------------------------------------
  // 核心功能 3：UI 动态数据刷新
  // ----------------------------------------------------------------
  if (millis() - lastSampleTime > 500) {
    lastSampleTime = millis();
    BoardAnalog::Reading channel1;
    BoardAnalog::Reading channel2;
    BoardAnalog::sample(channel1, channel2);
    adc1Raw = channel1.raw;
    adc2Raw = channel2.raw;
    current1Ma = channel1.milliAmps;
    current2Ma = channel2.milliAmps;

    if (currentPage == UiPage::Dashboard) {
      ui.drawDashboardDynamic(buildDashboardView());
    }
  }

  static unsigned long lastClearTime = 0;
  if (!masterMode && (millis() - lastClearTime > 150)) {
    if (currentPage == UiPage::Dashboard) {
      // 传输方向只短暂显示；底部模式切换提示始终保留。
      ui.clearTransferStatus(masterMode);
    }
    ui.flush();
    lastClearTime = millis();
  }
}

DashboardView buildDashboardView() {
  DashboardView view;
  view.masterMode = masterMode;
  view.channel1Ma = current1Ma;
  view.channel2Ma = current2Ma;
  view.attempt = sensorDiscovery.attempt();
  view.maxAttempts = sensorDiscovery.maxAttempts();
  view.slaveAddress = sensorDiscovery.result().slaveAddress;
  view.productNum = sensorDiscovery.result().productNum;
  view.deviceName = activeProfile != nullptr && activeProfile->name != nullptr
                        ? activeProfile->name
                        : "Unsupported PN";
  view.errorText = sensorDiscovery.errorText();
  view.exceptionResponse =
      sensorDiscovery.error() == SensorDiscovery::Error::ExceptionResponse;
  view.exceptionCode = sensorDiscovery.exceptionCode();

  switch (sensorDiscovery.state()) {
    case SensorDiscovery::State::DiscoveringAddress:
      view.discoveryState = UiDiscoveryState::DiscoveringAddress;
      break;
    case SensorDiscovery::State::DiscoveringSpecialAddress:
      view.discoveryState = UiDiscoveryState::DiscoveringSpecialAddress;
      break;
    case SensorDiscovery::State::ReadingIdentity:
      view.discoveryState = UiDiscoveryState::ReadingIdentity;
      break;
    case SensorDiscovery::State::ReadingProductNumber:
      view.discoveryState = UiDiscoveryState::ReadingProductNumber;
      break;
    case SensorDiscovery::State::Success:
      view.discoveryState = UiDiscoveryState::Success;
      break;
    case SensorDiscovery::State::Failed:
      view.discoveryState = UiDiscoveryState::Failed;
      break;
    case SensorDiscovery::State::Idle:
    default:
      view.discoveryState = UiDiscoveryState::Idle;
      break;
  }
  return view;
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
    if (currentPage == UiPage::Dashboard) ui.showTransferStatus("USB -> RS485 (Pass)", RGB565_RED);
  }
  usbModbusFrameLength = 0;
}

void drawAuthorPage() {
  SystemInfoView view;
#ifdef AUTO_VERSION
  view.firmwareVersion = AUTO_VERSION;
#else
  view.firmwareVersion = "V1.0.1";
#endif
  view.profileSource = profileStore.sourceText();
  view.configVersion = profileStore.configVersion();
  view.profileFallback = !profileStore.usingLittleFs();
  view.profileError = profileStore.lastError();
  view.sdStateText = sdStorage.stateText();
  view.sdFreeBytes = sdStorage.freeBytes();
  view.recording = dataLogger.isRecording();
  view.logRecordCount = dataLogger.recordCount();
  view.sdError = sdStorage.lastError();
  switch (sdStorage.state()) {
    case SdStorage::State::Ready:
      view.sdState = UiSdState::Ready;
      break;
    case SdStorage::State::Error:
      view.sdState = UiSdState::Error;
      break;
    default:
      view.sdState = UiSdState::Missing;
      break;
  }
  ui.drawSystemInfoPage(view);
}

void enterConfigMode() {
  // 停止所有使用 USB/RS485 的业务，避免 JSON 上传与 Modbus 混流。
  dataLogger.stop();  // Flush and close the SD file before CONFIG UPLOAD.
  configMode = true;
  masterMode = false;
  sensorDiscovery.cancel();
  profilePoller.cancel();
  activeProfile = nullptr;
  profileLookupComplete = false;
  usbModbusFrameLength = 0;
  while (Serial.available()) Serial.read();
  while (Serial1.available()) Serial1.read();
  profileUpdater.begin(profileStore);
  drawConfigUpdatePage();
}

void drawConfigUpdatePage() {
  ConfigUpdateView view;
  view.statusText = profileUpdater.statusText();
  view.expectedLength = profileUpdater.expectedLength();
  view.receivedLength = profileUpdater.receivedLength();
  view.errorText = profileUpdater.errorText();
  if (profileUpdater.state() == SensorProfileUpdater::State::Failed) {
    view.state = UiConfigState::Failed;
  } else if (profileUpdater.state() == SensorProfileUpdater::State::Success) {
    view.state = UiConfigState::Success;
  }
  ui.drawConfigUpdatePage(view);
}

void enterLogExportMode() {
  // 停止所有使用 USB/RS485 的业务，避免 ESPLOG1 与 Modbus 混流。
  dataLogger.stop();  // Flush and close the SD CSV before export.
  masterMode = false;
  sensorDiscovery.cancel();
  profilePoller.cancel();
  activeProfile = nullptr;
  profileLookupComplete = false;
  usbModbusFrameLength = 0;
  while (Serial.available()) Serial.read();
  while (Serial1.available()) Serial1.read();
#ifdef AUTO_VERSION
  logExportServer.begin(sdStorage, profileStore, dataLogger, AUTO_VERSION);
#else
  logExportServer.begin(sdStorage, profileStore, dataLogger, "development");
#endif
  if (logExportServer.state() == LogExportServer::State::Failed) {
    // SD 不可用时仍进入页面，由 UI 显示错误，允许触摸退出。
  }
  logExportMode = true;
  ui.clearScreen();
  currentPage = UiPage::LogExport;
  drawLogExportPage();
}

void drawLogExportPage() {
  LogExportView view;
  view.statusText = logExportServer.statusText();
  view.currentFile = logExportServer.currentFile();
  view.transferOffset = logExportServer.transferOffset();
  view.transferTotal = logExportServer.transferTotal();
  view.errorText = logExportServer.errorText();
  if (logExportServer.state() == LogExportServer::State::Failed) {
    view.state = UiLogExportState::Failed;
  } else if (logExportServer.state() ==
             LogExportServer::State::WaitingCommand) {
    view.state = UiLogExportState::WaitingCommand;
  }
  ui.drawLogExportPage(view);
}

void drawSensorInfoPage() {
  ui.drawSensorInfoPage(buildSensorInfoView());
}

void drawSensorParameterArea() {
  ui.drawSensorParameterArea(buildSensorInfoView());
}

SensorInfoView buildSensorInfoView() {
  const SensorDiscovery::Result &identity = sensorDiscovery.result();
  SensorInfoView view;
  view.slaveAddress = identity.slaveAddress;
  view.productNum = identity.productNum;
  if (activeProfile == nullptr) return view;

  view.supported = true;
  view.deviceName = activeProfile->name != nullptr
                        ? activeProfile->name
                        : "Unsupported PN";
  view.pageCount =
      (activeProfile->fieldCount + kUiSensorRowsPerPage - 1) /
      kUiSensorRowsPerPage;
  view.pageNumber = parameterPage + 1;

  uint8_t startIndex = parameterPage * kUiSensorRowsPerPage;
  for (uint8_t row = 0; row < kUiSensorRowsPerPage; ++row) {
    uint8_t fieldIndex = startIndex + row;
    if (fieldIndex >= activeProfile->fieldCount) break;
    const SensorField &field = activeProfile->fields[fieldIndex];
    SensorParameterView &parameter = view.rows[view.rowCount++];
    parameter.label = field.label;
    parameter.value = profileValues[fieldIndex];
    parameter.valid = profileValueValid[fieldIndex];
    parameter.decimals = field.decimals;
  }
  return view;
}
