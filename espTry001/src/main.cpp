#include <ModbusMaster.h> 
#include "app/app_controller.h"
#include "app/app_state.h"
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
SystemInfoView buildSystemInfoView();
void drawSensorInfoPage();
void drawSensorParameterArea();
SensorInfoView buildSensorInfoView();
DashboardView buildDashboardView();
void updateDashboardStateSnapshot();
void updateSensorStateSnapshot();
void updateSystemStateSnapshot();
void enterConfigMode();
void drawConfigUpdatePage();
ConfigUpdateView buildConfigUpdateView();
void updateConfigUpdateStateSnapshot();
void enterToolsMenu();
void enterLogExportMode();
void drawLogExportPage();
LogExportView buildLogExportView();
void updateLogExportStateSnapshot();
void processUsbModbusFrame();
void sendLocalModbusResponse(const uint8_t *request, size_t length);
uint16_t modbusCrc16(const uint8_t *data, size_t length);

AppState appState;
unsigned long lastSampleTime = 0;
unsigned long lastModbusTime = 0;
uint16_t modbusRegValues[REG_COUNT] = {0};
uint8_t modbusResult = 0xFF;               
bool touchOnline = false; 
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

static_assert(kAppMaxSensorFields == kMaxSensorFields,
              "AppState and SensorProfile field limits must match");

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
AppController appController(
    appState, dataLogger, sensorDiscovery, profilePoller, profileStore,
    profileUpdater, logExportServer, sdStorage, Serial, Serial1,
    usbModbusFrameLength, modbusResult, lastModbusTime, activeProfile,
    profileLookupComplete, appState.sensor.parameterPage, profileValueValid,
    kMaxSensorFields);

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
    ui.drawDashboardStatic(appState.isMaster());
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
    inputContext.configMode = appState.isConfigUpload();
    inputContext.sensorDetailsAvailable =
        appState.isMaster() &&
        sensorDiscovery.state() == SensorDiscovery::State::Success;
    UiAction action = resolveTouchAction(appState.page, tx, ty, inputContext);

    switch (action) {
      case UiAction::CancelConfigAndRestart:
        // CONFIG 模式退出时重启，重新加载最后一个有效配置。
        appController.cancelConfigUpload();
        Serial.flush();
        delay(50);
        ESP.restart();
        break;

      case UiAction::TogglePassMaster:
        appController.togglePassMaster();
        ui.clearScreen();
        ui.drawDashboardStatic(appState.isMaster());
        delay(400);
        break;

      case UiAction::OpenSensorInfo:
        appController.showPage(UiPage::SensorInfo);
        appState.sensor.parameterPage = 0;
        drawSensorInfoPage();
        delay(400);
        break;

      case UiAction::OpenSystemInfo:
        ui.clearScreen();
        appController.showPage(UiPage::SystemInfo);
        drawAuthorPage();
        delay(400);
        break;

      case UiAction::OpenToolsMenu:
        ui.clearScreen();
        appController.showPage(UiPage::ToolsMenu);
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
        if (appState.page == UiPage::LogExport) {
          appController.exitLogExport();
        } else {
          appController.showPage(UiPage::SystemInfo);
        }
        ui.clearScreen();
        drawAuthorPage();
        delay(400);
        break;

      case UiAction::NextSensorParameterPage:
        if (appState.sensor.fieldCount > kUiSensorRowsPerPage) {
          uint8_t pageCount =
              (appState.sensor.fieldCount + kUiSensorRowsPerPage - 1) /
              kUiSensorRowsPerPage;
          appState.sensor.parameterPage =
              (appState.sensor.parameterPage + 1) % pageCount;
          drawSensorParameterArea();
          ui.flush();
        }
        delay(400);
        break;

      case UiAction::BackToDashboard:
        ui.clearScreen();
        appController.showPage(UiPage::Dashboard);
        ui.drawDashboardStatic(appState.isMaster());
        delay(400);
        break;

      case UiAction::None:
        break;
    }
  }

  // CONFIG 模式独占 USB CDC，禁止 JSON 被本地 Modbus 解析或转发到 RS485。
  if (appState.isConfigUpload()) {
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
  if (appState.isLogExport()) {
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
      appController.exitLogExport();
      ui.clearScreen();
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

  if (!appState.isMaster()) {
    if (Serial1.available()) {
      while (Serial1.available()) Serial.write(Serial1.read());
      if (appState.page == UiPage::Dashboard) ui.showTransferStatus("RS485 -> USB (Pass)", RGB565_BLUE);
    }
  }
  else {
    sensorDiscovery.update();
    if (sensorDiscovery.state() == SensorDiscovery::State::Success) {
      appController.markMasterPolling();
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
        if (appState.page == UiPage::SensorInfo) {
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
  if (dataLogger.isRecording() && !appState.isMaster()) {
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

    if (appState.page == UiPage::Dashboard) {
      updateDashboardStateSnapshot();
      ui.drawDashboardDynamic(buildDashboardView());
    }
  }

  static unsigned long lastClearTime = 0;
  if (!appState.isMaster() && (millis() - lastClearTime > 150)) {
    if (appState.page == UiPage::Dashboard) {
      // 传输方向只短暂显示；底部模式切换提示始终保留。
      ui.clearTransferStatus(appState.isMaster());
    }
    ui.flush();
    lastClearTime = millis();
  }
}

DashboardView buildDashboardView() {
  DashboardView view;
  view.masterMode = appState.isMaster();
  const DashboardState &snapshot = appState.dashboard;
  view.channel1Ma = snapshot.channel1Ma;
  view.channel2Ma = snapshot.channel2Ma;
  view.attempt = snapshot.attempt;
  view.maxAttempts = snapshot.maxAttempts;
  view.slaveAddress = snapshot.slaveAddress;
  view.productNum = snapshot.productNum;
  view.deviceName = snapshot.deviceName;
  view.errorText = snapshot.errorText;
  view.exceptionResponse = snapshot.exceptionResponse;
  view.exceptionCode = snapshot.exceptionCode;

  switch (snapshot.discoveryPhase) {
    case AppDiscoveryPhase::DiscoveringAddress:
      view.discoveryState = UiDiscoveryState::DiscoveringAddress;
      break;
    case AppDiscoveryPhase::DiscoveringSpecialAddress:
      view.discoveryState = UiDiscoveryState::DiscoveringSpecialAddress;
      break;
    case AppDiscoveryPhase::ReadingIdentity:
      view.discoveryState = UiDiscoveryState::ReadingIdentity;
      break;
    case AppDiscoveryPhase::ReadingProductNumber:
      view.discoveryState = UiDiscoveryState::ReadingProductNumber;
      break;
    case AppDiscoveryPhase::Success:
      view.discoveryState = UiDiscoveryState::Success;
      break;
    case AppDiscoveryPhase::Failed:
      view.discoveryState = UiDiscoveryState::Failed;
      break;
    case AppDiscoveryPhase::Idle:
    default:
      view.discoveryState = UiDiscoveryState::Idle;
      break;
  }
  return view;
}

void updateDashboardStateSnapshot() {
  DashboardState &snapshot = appState.dashboard;
  snapshot.channel1Ma = current1Ma;
  snapshot.channel2Ma = current2Ma;
  snapshot.attempt = sensorDiscovery.attempt();
  snapshot.maxAttempts = sensorDiscovery.maxAttempts();
  snapshot.slaveAddress = sensorDiscovery.result().slaveAddress;
  snapshot.productNum = sensorDiscovery.result().productNum;
  snapshot.exceptionResponse =
      sensorDiscovery.error() == SensorDiscovery::Error::ExceptionResponse;
  snapshot.exceptionCode = sensorDiscovery.exceptionCode();

  // Copy text into AppState-owned buffers. This avoids retaining pointers into
  // the hot-update Profile store or SensorDiscovery's mutable error storage.
  const char *deviceName =
      activeProfile != nullptr && activeProfile->name != nullptr
          ? activeProfile->name
          : "Unsupported PN";
  snprintf(snapshot.deviceName, sizeof(snapshot.deviceName), "%s", deviceName);
  snprintf(snapshot.errorText, sizeof(snapshot.errorText), "%s",
           sensorDiscovery.errorText());

  switch (sensorDiscovery.state()) {
    case SensorDiscovery::State::DiscoveringAddress:
      snapshot.discoveryPhase = AppDiscoveryPhase::DiscoveringAddress;
      break;
    case SensorDiscovery::State::DiscoveringSpecialAddress:
      snapshot.discoveryPhase = AppDiscoveryPhase::DiscoveringSpecialAddress;
      break;
    case SensorDiscovery::State::ReadingIdentity:
      snapshot.discoveryPhase = AppDiscoveryPhase::ReadingIdentity;
      break;
    case SensorDiscovery::State::ReadingProductNumber:
      snapshot.discoveryPhase = AppDiscoveryPhase::ReadingProductNumber;
      break;
    case SensorDiscovery::State::Success:
      snapshot.discoveryPhase = AppDiscoveryPhase::Success;
      break;
    case SensorDiscovery::State::Failed:
      snapshot.discoveryPhase = AppDiscoveryPhase::Failed;
      break;
    case SensorDiscovery::State::Idle:
    default:
      snapshot.discoveryPhase = AppDiscoveryPhase::Idle;
      break;
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
  localRegisters[4] = appState.isMaster() ? 1 : 0;
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
  } else if (!appState.isMaster()) {
    // 非本地地址保持透明转发，CRC 与功能码由下游设备处理。
    Serial1.write(usbModbusFrame, usbModbusFrameLength);
    Serial1.flush();
    if (appState.page == UiPage::Dashboard) ui.showTransferStatus("USB -> RS485 (Pass)", RGB565_RED);
  }
  usbModbusFrameLength = 0;
}

void drawAuthorPage() {
  updateSystemStateSnapshot();
  ui.drawSystemInfoPage(buildSystemInfoView());
}

SystemInfoView buildSystemInfoView() {
  const SystemState &snapshot = appState.system;
  SystemInfoView view;
  view.firmwareVersion = snapshot.firmwareVersion;
  view.profileSource = snapshot.profileSource;
  view.configVersion = snapshot.configVersion;
  view.profileFallback = snapshot.profileFallback;
  view.profileError = snapshot.profileError;
  view.sdStateText = snapshot.storageStateText;
  view.sdFreeBytes = snapshot.storageFreeBytes;
  view.recording = snapshot.recording;
  view.logRecordCount = snapshot.logRecordCount;
  view.sdError = snapshot.storageError;
  switch (snapshot.storageState) {
    case AppStorageState::Ready:
      view.sdState = UiSdState::Ready;
      break;
    case AppStorageState::Error:
      view.sdState = UiSdState::Error;
      break;
    case AppStorageState::Missing:
    default:
      view.sdState = UiSdState::Missing;
      break;
  }
  return view;
}

void updateSystemStateSnapshot() {
  SystemState &snapshot = appState.system;
#ifdef AUTO_VERSION
  const char *firmwareVersion = AUTO_VERSION;
#else
  const char *firmwareVersion = "V1.0.1";
#endif

  // Own every string in AppState. Profile hot update and SD error reporting
  // may reuse their internal buffers after this snapshot has been published.
  snprintf(snapshot.firmwareVersion, sizeof(snapshot.firmwareVersion), "%s",
           firmwareVersion);
  snprintf(snapshot.profileSource, sizeof(snapshot.profileSource), "%s",
           profileStore.sourceText());
  snprintf(snapshot.configVersion, sizeof(snapshot.configVersion), "%s",
           profileStore.configVersion());
  snapshot.profileFallback = !profileStore.usingLittleFs();
  snprintf(snapshot.profileError, sizeof(snapshot.profileError), "%s",
           profileStore.lastError());
  snprintf(snapshot.storageStateText, sizeof(snapshot.storageStateText), "%s",
           sdStorage.stateText());
  snapshot.storageFreeBytes = sdStorage.freeBytes();
  snapshot.recording = dataLogger.isRecording();
  snapshot.logRecordCount = dataLogger.recordCount();
  snprintf(snapshot.storageError, sizeof(snapshot.storageError), "%s",
           sdStorage.lastError());

  switch (sdStorage.state()) {
    case SdStorage::State::Ready:
      snapshot.storageState = AppStorageState::Ready;
      break;
    case SdStorage::State::Error:
      snapshot.storageState = AppStorageState::Error;
      break;
    case SdStorage::State::NotInitialized:
    default:
      snapshot.storageState = AppStorageState::Missing;
      break;
  }
}

void enterConfigMode() {
  // AppController按固定顺序停止日志/RS485并清空残留字节，然后启动ESPCFG1。
  appController.enterConfigUpload();
  drawConfigUpdatePage();
}

void drawConfigUpdatePage() {
  updateConfigUpdateStateSnapshot();
  ui.drawConfigUpdatePage(buildConfigUpdateView());
}

ConfigUpdateView buildConfigUpdateView() {
  const ConfigUpdateState &snapshot = appState.configUpdate;
  ConfigUpdateView view;
  view.statusText = snapshot.statusText;
  view.expectedLength = snapshot.expectedLength;
  view.receivedLength = snapshot.receivedLength;
  view.errorText = snapshot.errorText;
  if (snapshot.state == AppConfigUpdateState::Failed) {
    view.state = UiConfigState::Failed;
  } else if (snapshot.state == AppConfigUpdateState::Success) {
    view.state = UiConfigState::Success;
  }
  return view;
}

void updateConfigUpdateStateSnapshot() {
  ConfigUpdateState &snapshot = appState.configUpdate;
  snprintf(snapshot.statusText, sizeof(snapshot.statusText), "%s",
           profileUpdater.statusText());
  snapshot.expectedLength = profileUpdater.expectedLength();
  snapshot.receivedLength = profileUpdater.receivedLength();
  snprintf(snapshot.errorText, sizeof(snapshot.errorText), "%s",
           profileUpdater.errorText());
  snapshot.state = AppConfigUpdateState::Active;
  if (profileUpdater.state() == SensorProfileUpdater::State::Failed) {
    snapshot.state = AppConfigUpdateState::Failed;
  } else if (profileUpdater.state() == SensorProfileUpdater::State::Success) {
    snapshot.state = AppConfigUpdateState::Success;
  }
}

void enterLogExportMode() {
  // AppController会先flush/close CSV，再让ESPLOG1取得SD与USB使用权。
#ifdef AUTO_VERSION
  appController.enterLogExport(AUTO_VERSION);
#else
  appController.enterLogExport("development");
#endif
  ui.clearScreen();
  drawLogExportPage();
}

void drawLogExportPage() {
  updateLogExportStateSnapshot();
  ui.drawLogExportPage(buildLogExportView());
}

LogExportView buildLogExportView() {
  const LogExportState &snapshot = appState.logExport;
  LogExportView view;
  view.statusText = snapshot.statusText;
  view.currentFile = snapshot.currentFile;
  view.transferOffset = snapshot.transferOffset;
  view.transferTotal = snapshot.transferTotal;
  view.errorText = snapshot.errorText;
  if (snapshot.state == AppLogExportState::Failed) {
    view.state = UiLogExportState::Failed;
  } else if (snapshot.state == AppLogExportState::WaitingCommand) {
    view.state = UiLogExportState::WaitingCommand;
  }
  return view;
}

void updateLogExportStateSnapshot() {
  LogExportState &snapshot = appState.logExport;
  snprintf(snapshot.statusText, sizeof(snapshot.statusText), "%s",
           logExportServer.statusText());
  snprintf(snapshot.currentFile, sizeof(snapshot.currentFile), "%s",
           logExportServer.currentFile());
  snapshot.transferOffset = logExportServer.transferOffset();
  snapshot.transferTotal = logExportServer.transferTotal();
  snprintf(snapshot.errorText, sizeof(snapshot.errorText), "%s",
           logExportServer.errorText());
  snapshot.state = AppLogExportState::Active;
  if (logExportServer.state() == LogExportServer::State::Failed) {
    snapshot.state = AppLogExportState::Failed;
  } else if (logExportServer.state() ==
             LogExportServer::State::WaitingCommand) {
    snapshot.state = AppLogExportState::WaitingCommand;
  }
}

void drawSensorInfoPage() {
  updateSensorStateSnapshot();
  ui.drawSensorInfoPage(buildSensorInfoView());
}

void drawSensorParameterArea() {
  updateSensorStateSnapshot();
  ui.drawSensorParameterArea(buildSensorInfoView());
}

SensorInfoView buildSensorInfoView() {
  const SensorState &snapshot = appState.sensor;
  SensorInfoView view;
  view.slaveAddress = snapshot.slaveAddress;
  view.productNum = snapshot.productNum;
  view.deviceName = snapshot.deviceName;
  view.supported = snapshot.supported;
  if (!snapshot.supported) return view;

  view.pageCount =
      (snapshot.fieldCount + kUiSensorRowsPerPage - 1) /
      kUiSensorRowsPerPage;
  view.pageNumber = snapshot.parameterPage + 1;

  uint8_t startIndex = snapshot.parameterPage * kUiSensorRowsPerPage;
  for (uint8_t row = 0; row < kUiSensorRowsPerPage; ++row) {
    uint8_t fieldIndex = startIndex + row;
    if (fieldIndex >= snapshot.fieldCount) break;
    const SensorFieldState &field = snapshot.fields[fieldIndex];
    SensorParameterView &parameter = view.rows[view.rowCount++];
    parameter.label = field.label;
    parameter.value = field.value;
    parameter.valid = field.valid;
    parameter.decimals = field.decimals;
  }
  return view;
}

void updateSensorStateSnapshot() {
  SensorState &snapshot = appState.sensor;
  const SensorDiscovery::Result &identity = sensorDiscovery.result();
  snapshot.slaveAddress = identity.slaveAddress;
  snapshot.productNum = identity.productNum;
  snapshot.scanSequence = profilePoller.scanSequence();
  snapshot.supported = activeProfile != nullptr;
  snapshot.fieldCount = 0;

  const char *deviceName =
      activeProfile != nullptr && activeProfile->name != nullptr
          ? activeProfile->name
          : "Unsupported PN";
  snprintf(snapshot.deviceName, sizeof(snapshot.deviceName), "%s", deviceName);

  if (activeProfile == nullptr) return;

  // Copy all configured fields, not only the visible page. A future UI task
  // can therefore change pages without touching ProfileStore or poller memory.
  snapshot.fieldCount =
      min(activeProfile->fieldCount, (uint8_t)kAppMaxSensorFields);
  for (uint8_t index = 0; index < snapshot.fieldCount; ++index) {
    const SensorField &source = activeProfile->fields[index];
    SensorFieldState &target = snapshot.fields[index];
    snprintf(target.label, sizeof(target.label), "%s", source.label);
    target.value = profileValues[index];
    target.valid = profileValueValid[index];
    target.decimals = source.decimals;
  }
}
