#include "app/app_controller.h"
#include "app/app_state.h"
#include "board/analog_input.h"
#include "board/board_config.h"
#include "board/display_driver.h"
#include "board/power_driver.h"
#include "board/rtc_driver.h"
#include "board/touch_driver.h"
#include "sensor_discovery.h"
#include "calibration_profile_store.h"
#include "sensor_profile.h"
#include "sensor_profile_store.h"
#include "sensor_profile_update.h"
#include "services/rs485_service.h"
#include "sd_storage.h"
#include "data_logger.h"
#include "debug_log.h"
#include "log_export_server.h"
#include "services/storage_service.h"
#include "services/time_service.h"
#include "services/time_sync_server.h"
#include "services/wifi_time_service.h"
#include "ui/ui_input.h"
#include "ui/ui_manager.h"
#include "usb/usb_service.h"
#include "usb/storage_usb_bridge.h"

#include <esp_system.h>
#include <esp32s3/rom/rtc.h>

#define LOCAL_SLAVE_ID           247
#define LOCAL_REG_COUNT          43

#ifdef AUTO_VERSION
#define FIRMWARE_VERSION_TEXT AUTO_VERSION
#else
#define FIRMWARE_VERSION_TEXT "development"
#endif

// ==========================================
// 2. 前置函数声明与全局变量
// ==========================================
void drawAuthorPage();
/**
 * 把 ESP-IDF 的启动复位枚举转换成适合小屏显示的稳定短文本。
 *
 * 该文本会一直保留到下一次复位，用来区分正常上电、软件重启、看门狗
 * 和电源 Brownout，不依赖复位发生时 USB 串口是否已经连接。
 */
const char *resetReasonText(esp_reset_reason_t reason);
/**
 * 补充解码 ESP32-S3 ROM 原始复位码。
 *
 * esp_reset_reason() 会把 USB-UART/JTAG 芯片复位折叠成 UNKNOWN；ROM 码
 * 21/22 能保留这个关键信息，因此仅在高层原因未知时采用原始短文本。
 */
const char *rawResetReasonText(RESET_REASON reason);
SystemInfoView buildSystemInfoView();
void drawSensorInfoPage();
void drawSensorParameterArea();
SensorInfoView buildSensorInfoView();
DashboardView buildDashboardView();
UiOperatingMode currentUiOperatingMode();
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
void enterTimeSyncMode();
void drawTimeSyncPage();
TimeSyncView buildTimeSyncView();
void updateTimeSyncStateSnapshot();
void drawCalibrationPage();
void handleCalibrationTouch(int x, int y);
bool startSelectedCalibration();
void processRs485Events();
void refreshRs485Status();
void sendLocalModbusResponse(const uint8_t *request, size_t length);
uint16_t modbusCrc16(const uint8_t *data, size_t length);

AppState appState;
unsigned long lastSampleTime = 0;
bool touchOnline = false; 
uint16_t adc1Raw = 0;
uint16_t adc2Raw = 0;
float current1Ma = 0.0f;
float current2Ma = 0.0f;
const SensorProfile *activeProfile = nullptr;
bool profileLookupComplete = false;
Rs485Service::Status rs485Status;
Rs485Service::Event latestScanEvent;
bool latestScanAvailable = false;
esp_reset_reason_t startupResetReason = ESP_RST_UNKNOWN;
RESET_REASON startupRawResetReasonCpu0 = NO_MEAN;
RESET_REASON startupRawResetReasonCpu1 = NO_MEAN;

static_assert(kAppMaxSensorFields == kMaxSensorFields,
              "AppState and SensorProfile field limits must match");

Rs485Service rs485Service(Serial1);
SensorProfileStore profileStore;
CalibrationProfileStore calibrationProfileStore;
SensorProfileUpdater profileUpdater;
SdStorage sdStorage;
DataLogger dataLogger(sdStorage);
LogExportServer logExportServer;
UiManager ui;
StorageUsbBridge logExportUsbBridge;
StorageService storageService(sdStorage, dataLogger, logExportServer,
                              profileStore, logExportUsbBridge);
BoardRtc::Driver rtcDriver;
TimeService timeService(rtcDriver);
WifiTimeService wifiTimeService(timeService);
TimeSyncServer timeSyncServer(timeService, wifiTimeService);
UsbService usbService(Serial, rs485Service, profileUpdater, logExportUsbBridge,
                      timeSyncServer, LOCAL_SLAVE_ID, sendLocalModbusResponse);
AppController appController(
    appState, storageService, rs485Service, profileStore, profileUpdater,
    timeSyncServer, usbService, Serial, activeProfile, profileLookupComplete,
    appState.sensor.parameterPage);

// ==========================================
// 5. 核心逻辑主程序
// ==========================================
void setup()
{
  // Capture this before peripheral initialization. esp_reset_reason() remains
  // readable later, but storing it here makes the diagnostic's boot scope clear.
  startupResetReason = esp_reset_reason();
  startupRawResetReasonCpu0 = rtc_get_reset_reason(0);
  startupRawResetReasonCpu1 = rtc_get_reset_reason(1);
  // Blank the panel before any startup delays or LCD initialization can expose
  // uninitialized frame memory through the backlight.
  prepareBoardDisplay();
  beginBoardPower();
  // Native USB CDC defaults to a 256-byte RX queue. A complete YMODEM-1K
  // packet is 1029 bytes and can arrive in one host write, so enlarge the
  // queue before begin(); otherwise bytes are lost before UsbService can move
  // them into its 4096-byte raw bridge stream.
  Serial.setRxBufferSize(Rs485Service::kRawRequestCapacity);
  Serial.begin(115200); 
  delay(100);

  // 优先加载 LittleFS JSON；挂载、文件或校验失败时自动使用固件内置表。
  profileStore.begin();
  profileUpdater.attachCalibrationStore(calibrationProfileStore);
  storageService.setMetadata(profileStore.configVersion(),
                             FIRMWARE_VERSION_TEXT);

  // 1. 屏幕底层硬件初始化
  bool displayOnline = beginBoardDisplay();
  uint32_t startupPageTimeMs = 0;
  if (displayOnline) {
    if (BoardConfig::kShowStartupScreen) {
      ui.drawStartupPage(FIRMWARE_VERSION_TEXT);
      startupPageTimeMs = millis();
    } else {
      ui.drawDashboardStatic(currentUiOperatingMode());
    }
    setBoardDisplayBacklight(true);
  }

  // 2. 初始化串口与 485
  // RS485 is initialized and exclusively owned by Rs485Task.
  if (!rs485Service.begin() || !rs485Service.enterPassThrough()) {
    DEBUG_LOG_PRINTLN("[RS485] Rs485Task initialization failed");
    appState.mode = AppMode::Fault;
  }

  // 3. 初始化 ADC
  BoardAnalog::begin();

  // 4. 初始化触摸屏
  touchOnline = BoardTouch::begin(
      gfx->getRotation(), gfx->width(), gfx->height());

  // 5. Initialize RTC on its dedicated I2C controller. An invalid or missing
  // RTC is diagnostic only and must never block the rest of the application.
  timeService.begin();
  DEBUG_LOG_PRINTF("[RTC] %s\n", timeService.statusText());

  // Wi-Fi/NTP starts as a non-blocking boot attempt. It never delays the
  // remaining SD/USB initialization and turns its radio off after completion.
  wifiTimeService.begin();

  // 6. 初始化 SD 卡
  if (!logExportUsbBridge.begin()) {
    DEBUG_LOG_PRINTLN("[USB] ESPLOG1 bridge initialization failed");
  }
  if (storageService.begin()) {
    StorageService::Status storageStatus;
    storageService.getStatus(storageStatus);
    bool sdReady = storageStatus.storageState == SdStorage::State::Ready;
    if (sdReady) {
      DEBUG_LOG_PRINTLN("[SD] SD card initialized OK");
    } else {
      DEBUG_LOG_PRINTLN("[SD] No SD card, logging disabled");
    }
  } else {
    DEBUG_LOG_PRINTLN("[SD] StorageTask initialization failed");
  }

  if (displayOnline && BoardConfig::kShowStartupScreen) {
    uint32_t elapsed = millis() - startupPageTimeMs;
    if (elapsed < BoardConfig::kMinimumStartupScreenMs) {
      delay(BoardConfig::kMinimumStartupScreenMs - elapsed);
    }
    ui.clearScreen();
    ui.drawDashboardStatic(currentUiOperatingMode());
  }
}

void loop()
{
  timeService.update();
  bool rtcWriteAllowed = true;
  if (wifiTimeService.state() == WifiTimeService::State::PendingRtcWrite) {
    rtcWriteAllowed = !storageService.isRecording();
  }
  wifiTimeService.update(rtcWriteAllowed);
  processRs485Events();
  refreshRs485Status();

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
        rs485Status.discoveryState == SensorDiscovery::State::Success;
    inputContext.calibrationAvailable = false;
#if defined(PYXIS_BOARD_WAVESHARE_28)
    inputContext.calibrationAvailable =
        calibrationProfileStore.profile() != nullptr;
#endif

    UiAction action = UiAction::None;
    if (appState.page == UiPage::Calibration) {
      handleCalibrationTouch(tx, ty);
      delay(180);
    } else {
      action = resolveTouchAction(appState.page, tx, ty, inputContext);
    }

    switch (action) {
      case UiAction::CancelConfigAndRestart:
        // CONFIG 模式退出时重启，重新加载最后一个有效配置。
        appController.cancelConfigUpload();
        Serial.flush();
        delay(50);
        ESP.restart();
        break;

      case UiAction::CycleOperatingMode:
        appController.cycleOperatingMode();
        ui.clearScreen();
        ui.drawDashboardStatic(currentUiOperatingMode());
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

      case UiAction::EnterTimeSync:
        enterTimeSyncMode();
        delay(400);
        break;

      case UiAction::OpenCalibration:
        appState.calibration = CalibrationState{};
        appState.page = UiPage::Calibration;
        drawCalibrationPage();
        delay(300);
        break;

      case UiAction::BackToSystemInfo:
        if (appState.page == UiPage::LogExport) {
          appController.exitLogExport();
        } else if (appState.page == UiPage::TimeSync) {
          appController.exitTimeSync();
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
        ui.drawDashboardStatic(currentUiOperatingMode());
        delay(400);
        break;

      case UiAction::None:
        break;
    }
  }

  // UsbService is the single USB protocol router. It advances exactly one of
  // Modbus RTU, ESPCFG1, ESPLOG1 or ESPTIME1 according to AppMode.
  usbService.update(appState.mode);

  // CONFIG 模式独占 USB CDC，禁止 JSON 被本地 Modbus 解析或转发到 RS485。
  if (appState.isConfigUpload()) {
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
    static LogExportServer::State lastExportState =
        LogExportServer::State::Idle;
    static uint32_t lastExportDrawMs = 0;
    StorageService::Status storageStatus;
    storageService.getStatus(storageStatus);
    LogExportServer::State exportState = storageStatus.exportState;
    bool stateChanged = exportState != lastExportState;
    bool progressDue = (uint32_t)(millis() - lastExportDrawMs) >= 300;
    if (stateChanged || progressDue) {
      drawLogExportPage();
      lastExportState = exportState;
      lastExportDrawMs = millis();
    }
    if (storageStatus.exportShouldExit) {
      appController.exitLogExport();
      ui.clearScreen();
      drawAuthorPage();
    }
    delay(1);
    return;
  }

  // TIME SYNC 独占 USB CDC，但解析保持非阻塞，触摸和 UI 仍能正常响应。
  if (appState.isTimeSync()) {
    static TimeSyncServer::State lastTimeSyncState =
        TimeSyncServer::State::Idle;
    static uint32_t lastTimeSyncDrawMs = 0;
    TimeSyncServer::State state = timeSyncServer.state();
    bool stateChanged = state != lastTimeSyncState;
    bool refreshDue = (uint32_t)(millis() - lastTimeSyncDrawMs) >= 500;
    if (stateChanged || refreshDue) {
      drawTimeSyncPage();
      lastTimeSyncState = state;
      lastTimeSyncDrawMs = millis();
    }
    if (timeSyncServer.closeRequested()) {
      appController.exitTimeSync();
      ui.clearScreen();
      drawAuthorPage();
    }
    delay(1);
    return;
  }

  // ----------------------------------------------------------------
  // 核心功能 2：后台双向透传与 Modbus 轮询
  // ----------------------------------------------------------------
  // UsbService owns USB RTU framing, local address 247 dispatch, and PASS
  // forwarding. It reports only a semantic activity event back to the UI.
  UsbTransferActivity usbActivity = usbService.takeTransferActivity();
  if (appState.page == UiPage::Dashboard) {
    if (usbActivity == UsbTransferActivity::UsbToRs485) {
      ui.showTransferStatus("USB -> RS485 (Pass)", RGB565_RED);
    } else if (usbActivity == UsbTransferActivity::Rs485ToUsb) {
      ui.showTransferStatus("RS485 -> USB (Pass)", RGB565_BLUE);
    }
  }

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
  if (appState.isPassThrough() && (millis() - lastClearTime > 150)) {
    if (appState.page == UiPage::Dashboard) {
      // 传输方向只短暂显示；底部模式切换提示始终保留。
      ui.clearTransferStatus(currentUiOperatingMode());
    }
    ui.flush();
    lastClearTime = millis();
  }
}

void refreshRs485Status() {
  rs485Service.getStatus(rs485Status);
}

void processRs485Events() {
  Rs485Service::Event event;
  while (rs485Service.takeEvent(event)) {
    switch (event.type) {
      case Rs485Service::EventType::SensorFound:
        if (!appState.isMaster() ||
            event.sourceMode != Rs485Service::Mode::Discovery) {
          break;
        }
        appController.markMasterPolling();
        if (profileLookupComplete) break;

        latestScanAvailable = false;
#if defined(PYXIS_BOARD_WAVESHARE_28)
        if (calibrationProfileStore.loadForProduct(event.identity.productNum)) {
          const CalibrationProfile *calibration =
              calibrationProfileStore.profile();
          DEBUG_LOG_PRINTF("[CAL] Loaded %s with %u procedure(s)\n",
                           calibration->profileId,
                           calibration->procedureCount);
        } else {
          DEBUG_LOG_PRINTF("[CAL] PN %lu unavailable: %s\n",
                           static_cast<unsigned long>(event.identity.productNum),
                           calibrationProfileStore.lastError());
        }
#else
        calibrationProfileStore.clear();
#endif
        activeProfile = profileStore.find(event.identity.productNum);
        profileLookupComplete = true;
        if (activeProfile != nullptr) {
          if (!rs485Service.startPolling(activeProfile)) {
            DEBUG_LOG_PRINTLN("[RS485] Polling command queue full");
            appState.mode = AppMode::Fault;
            break;
          }
          // One logging session belongs to one discovered address/PN/Profile.
          // startLogging safely does nothing when the SD card is unavailable.
          char logFileTimestamp[DataLogger::kLogFileTimestampLen + 1] = "";
#if defined(PYXIS_BOARD_WAVESHARE_28)
          // Freeze the local start time into the filename once. Later RTC/NTP
          // adjustments cannot rename an open file or split one session across
          // multiple timestamps. An invalid RTC deliberately leaves it empty.
          timeService.formatLogFileTimestamp(logFileTimestamp,
                                             sizeof(logFileTimestamp));
#endif
          storageService.startLogging(
              event.identity.slaveAddress, event.identity.productNum,
              activeProfile->name, activeProfile, logFileTimestamp);
        }
        break;

      case Rs485Service::EventType::ScanCompleted:
        if (appState.isPrecheck() &&
            event.sourceMode == Rs485Service::Mode::FixedPolling) {
          PrecheckState &precheck = appState.precheck;
          precheck.s470Raw = event.values[0];
          precheck.t365Raw = event.values[1];
          precheck.s470Valid = event.fieldCount > 0 && event.valid[0];
          precheck.t365Valid = event.fieldCount > 1 && event.valid[1];
          precheck.scanSequence = event.scanSequence;
          precheck.lastModbusError = event.error;
        } else if (appState.isMaster() && activeProfile != nullptr &&
                   event.sourceMode == Rs485Service::Mode::Polling) {
          latestScanEvent = event;
          // Freeze both clocks at scan completion. StorageTask may write later,
          // but its queue delay must never change the sample's timestamp.
          const uint32_t snapshotUptimeMs = millis();
          char snapshotTimestamp[SensorSnapshot::kTimestampCapacity] = "";
          timeService.formatIso8601(snapshotTimestamp,
                                    sizeof(snapshotTimestamp));
          storageService.enqueueSnapshot(
              event.scanSequence, snapshotUptimeMs, snapshotTimestamp,
              event.values, event.valid, event.fieldCount,
              current1Ma, current2Ma);
          latestScanAvailable = true;
          if (appState.page == UiPage::SensorInfo) {
            drawSensorParameterArea();
            ui.flush();
          }
        }
        break;

      case Rs485Service::EventType::CommunicationError:
        if (appState.isPrecheck() &&
            event.sourceMode == Rs485Service::Mode::FixedPolling) {
          appState.precheck.lastModbusError = event.error;
        }
        break;
      case Rs485Service::EventType::CalibrationProgress:
        if (appState.isCalibration()) {
          appState.calibration.executorState = event.calibrationState;
          appState.calibration.commandIndex = event.calibrationCommandIndex;
          appState.calibration.commandCount = event.calibrationCommandCount;
          if (event.calibrationMessage[0] != '\0') {
            snprintf(appState.calibration.statusText,
                     sizeof(appState.calibration.statusText), "%s",
                     event.calibrationMessage);
          } else {
            snprintf(appState.calibration.statusText,
                     sizeof(appState.calibration.statusText),
                     "Executing command %u/%u",
                     event.calibrationCommandIndex + 1,
                     event.calibrationCommandCount);
          }
          if (appState.page == UiPage::Calibration) drawCalibrationPage();
        }
        break;
      case Rs485Service::EventType::CalibrationFinished:
        if (appState.isCalibration()) {
          appState.calibration.executorState = event.calibrationState;
          appState.calibration.commandIndex = event.calibrationCommandIndex;
          appState.calibration.commandCount = event.calibrationCommandCount;
          const CalibrationProfile *profile = calibrationProfileStore.profile();
          const bool outputSpan =
              profile != nullptr &&
              appState.calibration.selectedProcedure < profile->procedureCount &&
              profile->procedures[appState.calibration.selectedProcedure].category ==
                  CalibrationProcedureCategory::OutputSpan;
          if (outputSpan &&
              event.calibrationState == CalibrationExecutor::State::Completed) {
            snprintf(appState.calibration.statusText,
                     sizeof(appState.calibration.statusText),
                     "Output range saved");
          } else {
            snprintf(appState.calibration.statusText,
                     sizeof(appState.calibration.statusText), "%s",
                     event.calibrationMessage);
          }
          appState.calibration.phase = CalibrationUiPhase::Finished;
          if (appState.page == UiPage::Calibration) drawCalibrationPage();
        }
        break;
      case Rs485Service::EventType::Stopped:
        break;
    }
  }
}

DashboardView buildDashboardView() {
  DashboardView view;
  view.mode = currentUiOperatingMode();
  const DashboardState &snapshot = appState.dashboard;
  view.channel1Ma = snapshot.channel1Ma;
  view.channel2Ma = snapshot.channel2Ma;
  view.rtcSupported = snapshot.rtcSupported;
  view.rtcValid = snapshot.rtcValid;
  view.rtcTimeText = snapshot.rtcTimeText;
  view.attempt = snapshot.attempt;
  view.maxAttempts = snapshot.maxAttempts;
  view.slaveAddress = snapshot.slaveAddress;
  view.productNum = snapshot.productNum;
  view.deviceName = snapshot.deviceName;
  view.errorText = snapshot.errorText;
  view.exceptionResponse = snapshot.exceptionResponse;
  view.exceptionCode = snapshot.exceptionCode;
  const PrecheckState &precheck = appState.precheck;
  view.s470Raw = precheck.s470Raw;
  view.t365Raw = precheck.t365Raw;
  view.s470Valid = precheck.s470Valid;
  view.t365Valid = precheck.t365Valid;
  view.precheckScanSequence = precheck.scanSequence;
  view.precheckError = precheck.lastModbusError;
  view.rawUsbToUartBytes = rs485Status.rawUsbToUartBytes;
  view.rawUartToUsbBytes = rs485Status.rawUartToUsbBytes;
  view.rawUsbToUartDrops = rs485Status.rawUsbToUartDrops;
  view.rawUartToUsbDrops = rs485Status.rawUartToUsbDrops;
  view.rawRequestPeak = rs485Status.rawRequestPeak;
  view.rawResponsePeak = rs485Status.rawResponsePeak;

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

UiOperatingMode currentUiOperatingMode() {
  if (appState.isMaster()) return UiOperatingMode::Master;
  if (appState.isPrecheck()) return UiOperatingMode::Precheck;
  if (appState.isUpgradePass()) return UiOperatingMode::UpgradePass;
  return UiOperatingMode::Pass;
}

void updateDashboardStateSnapshot() {
  DashboardState &snapshot = appState.dashboard;
  snapshot.channel1Ma = current1Ma;
  snapshot.channel2Ma = current2Ma;
  snapshot.rtcSupported = timeService.supported();
  snapshot.rtcValid = timeService.valid();
  timeService.formatLocalTime(snapshot.rtcTimeText,
                              sizeof(snapshot.rtcTimeText));
  snapshot.attempt = rs485Status.discoveryAttempt;
  snapshot.maxAttempts = rs485Status.discoveryMaxAttempts;
  snapshot.slaveAddress = rs485Status.identity.slaveAddress;
  snapshot.productNum = rs485Status.identity.productNum;
  snapshot.exceptionResponse =
      rs485Status.discoveryError ==
      SensorDiscovery::Error::ExceptionResponse;
  snapshot.exceptionCode = rs485Status.discoveryExceptionCode;

  // Copy text into AppState-owned buffers. This avoids retaining pointers into
  // the hot-update Profile store or SensorDiscovery's mutable error storage.
  const CalibrationProfile *calibration = calibrationProfileStore.profile();
  const char *deviceName =
      activeProfile != nullptr && activeProfile->name != nullptr
          ? activeProfile->name
          : (calibration != nullptr && calibration->modelName[0] != '\0'
                 ? calibration->modelName
                 : "Unsupported PN");
  snprintf(snapshot.deviceName, sizeof(snapshot.deviceName), "%s", deviceName);
  snprintf(snapshot.errorText, sizeof(snapshot.errorText), "%s",
           rs485Status.discoveryErrorText);

  switch (rs485Status.discoveryState) {
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

static void floatToCdabRegisters(float value, uint16_t &firstWord,
                                 uint16_t &secondWord) {
  uint32_t raw;
  memcpy(&raw, &value, sizeof(raw));
  firstWord = raw & 0xFFFF;
  secondWord = raw >> 16;
}

static void uint32ToModbusRegisters(uint32_t value, uint16_t &highWord,
                                    uint16_t &lowWord) {
  highWord = value >> 16;
  lowWord = value & 0xFFFF;
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
  floatToCdabRegisters(current1Ma, localRegisters[0], localRegisters[1]);
  floatToCdabRegisters(current2Ma, localRegisters[2], localRegisters[3]);
  localRegisters[4] = appState.isMaster() ? 1 : 0;
  localRegisters[5] = adc1Raw;
  localRegisters[6] = adc2Raw;

  // Registers 40008+ are a read-only stability diagnostic extension. The
  // original 40001~40007 map remains unchanged and backward compatible.
  Rs485Service::Status rs485Diagnostics;
  StorageService::Status storageDiagnostics;
  rs485Service.getStatus(rs485Diagnostics);
  storageService.getStatus(storageDiagnostics);

  localRegisters[7] = static_cast<uint16_t>(appState.mode);
  localRegisters[8] = static_cast<uint16_t>(rs485Diagnostics.mode);
  localRegisters[9] = rs485Diagnostics.lastModbusError;
  localRegisters[10] = rs485Diagnostics.commandQueueDepth;
  localRegisters[11] = rs485Diagnostics.commandQueuePeak;
  localRegisters[12] = rs485Diagnostics.eventQueueDepth;
  localRegisters[13] = rs485Diagnostics.eventQueuePeak;
  uint32ToModbusRegisters(rs485Diagnostics.droppedEvents,
                          localRegisters[14], localRegisters[15]);
  uint32ToModbusRegisters(rs485Diagnostics.passRequestDrops,
                          localRegisters[16], localRegisters[17]);
  uint32ToModbusRegisters(rs485Diagnostics.passResponseDrops,
                          localRegisters[18], localRegisters[19]);
  uint32ToModbusRegisters(
      static_cast<uint32_t>(rs485Diagnostics.taskStackHighWater),
      localRegisters[20], localRegisters[21]);

  localRegisters[22] =
      static_cast<uint16_t>(storageDiagnostics.storageState);
  localRegisters[23] = storageDiagnostics.recording ? 1 : 0;
  localRegisters[24] = storageDiagnostics.snapshotQueueDepth;
  localRegisters[25] = storageDiagnostics.snapshotQueuePeak;
  uint32ToModbusRegisters(storageDiagnostics.droppedSnapshots,
                          localRegisters[26], localRegisters[27]);
  uint32ToModbusRegisters(storageDiagnostics.commandFailures,
                          localRegisters[28], localRegisters[29]);
  uint32ToModbusRegisters(
      static_cast<uint32_t>(storageDiagnostics.taskStackHighWater),
      localRegisters[30], localRegisters[31]);
  uint32ToModbusRegisters(ESP.getFreeHeap(),
                          localRegisters[32], localRegisters[33]);
  uint32ToModbusRegisters(ESP.getMinFreeHeap(),
                          localRegisters[34], localRegisters[35]);
  uint32ToModbusRegisters(storageDiagnostics.logRecordCount,
                          localRegisters[36], localRegisters[37]);
  uint32ToModbusRegisters(storageDiagnostics.lastSuccessfulFlushMs,
                          localRegisters[38], localRegisters[39]);
  uint32ToModbusRegisters(millis(),
                          localRegisters[40], localRegisters[41]);
  localRegisters[42] =
      static_cast<uint16_t>(storageDiagnostics.exportState);

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

void drawAuthorPage() {
  updateSystemStateSnapshot();
  ui.drawSystemInfoPage(buildSystemInfoView());
}

const char *resetReasonText(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "POWER_ON";
    case ESP_RST_EXT:
      return "EXTERNAL";
    case ESP_RST_SW:
      return "SOFTWARE";
    case ESP_RST_PANIC:
      return "PANIC";
    case ESP_RST_INT_WDT:
      return "INT_WDT";
    case ESP_RST_TASK_WDT:
      return "TASK_WDT";
    case ESP_RST_WDT:
      return "OTHER_WDT";
    case ESP_RST_DEEPSLEEP:
      return "DEEP_SLEEP";
    case ESP_RST_BROWNOUT:
      return "BROWNOUT";
    case ESP_RST_SDIO:
      return "SDIO";
    case ESP_RST_UNKNOWN:
    default:
      return "UNKNOWN";
  }
}

const char *rawResetReasonText(RESET_REASON reason) {
  switch (reason) {
    case USB_UART_CHIP_RESET:
      return "USB_UART";
    case USB_JTAG_CHIP_RESET:
      return "USB_JTAG";
    case POWER_GLITCH_RESET:
      return "POWER_GLITCH";
    case GLITCH_RTC_RESET:
      return "CLOCK_GLITCH";
    case EFUSE_RESET:
      return "EFUSE";
    case NO_MEAN:
      return "UNKNOWN";
    default:
      return resetReasonText(startupResetReason);
  }
}

SystemInfoView buildSystemInfoView() {
  const SystemState &snapshot = appState.system;
  SystemInfoView view;
  view.firmwareVersion = snapshot.firmwareVersion;
  view.resetReasonText = snapshot.resetReasonText;
  view.resetReasonCode = snapshot.resetReasonCode;
  view.rawResetReasonCpu0 = snapshot.rawResetReasonCpu0;
  view.rawResetReasonCpu1 = snapshot.rawResetReasonCpu1;
  view.resetReasonFault = snapshot.resetReasonFault;
  view.profileSource = snapshot.profileSource;
  view.configVersion = snapshot.configVersion;
  view.profileFallback = snapshot.profileFallback;
  view.profileError = snapshot.profileError;
  view.sdStateText = snapshot.storageStateText;
  view.sdFreeBytes = snapshot.storageFreeBytes;
  view.recording = snapshot.recording;
  view.logRecordCount = snapshot.logRecordCount;
  view.storageQueueDepth = snapshot.storageQueueDepth;
  view.storageQueuePeak = snapshot.storageQueuePeak;
  view.storageDroppedSnapshots = snapshot.storageDroppedSnapshots;
  view.storageTaskStackHighWater = snapshot.storageTaskStackHighWater;
  view.sdError = snapshot.storageError;
  view.rtcSupported = snapshot.rtcSupported;
  view.rtcValid = snapshot.rtcValid;
  view.rtcHoldover = snapshot.rtcHoldover;
  view.rtcStatusText = snapshot.rtcStatusText;
  view.rtcDateTimeText = snapshot.rtcDateTimeText;
  view.ntpStatusText = snapshot.ntpStatusText;
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
  StorageService::Status storageStatus;
  storageService.getStatus(storageStatus);
  const char *firmwareVersion = FIRMWARE_VERSION_TEXT;

  // Own every string in AppState. Profile hot update and SD error reporting
  // may reuse their internal buffers after this snapshot has been published.
  snprintf(snapshot.firmwareVersion, sizeof(snapshot.firmwareVersion), "%s",
           firmwareVersion);
  const char *startupResetText =
      startupResetReason == ESP_RST_UNKNOWN
          ? rawResetReasonText(startupRawResetReasonCpu0)
          : resetReasonText(startupResetReason);
  snprintf(snapshot.resetReasonText, sizeof(snapshot.resetReasonText), "%s",
           startupResetText);
  snapshot.resetReasonCode = static_cast<uint8_t>(startupResetReason);
  snapshot.rawResetReasonCpu0 =
      static_cast<uint8_t>(startupRawResetReasonCpu0);
  snapshot.rawResetReasonCpu1 =
      static_cast<uint8_t>(startupRawResetReasonCpu1);
  snapshot.resetReasonFault =
      startupResetReason == ESP_RST_PANIC ||
      startupResetReason == ESP_RST_INT_WDT ||
      startupResetReason == ESP_RST_TASK_WDT ||
      startupResetReason == ESP_RST_WDT ||
      startupResetReason == ESP_RST_BROWNOUT ||
      startupRawResetReasonCpu0 == USB_UART_CHIP_RESET ||
      startupRawResetReasonCpu0 == USB_JTAG_CHIP_RESET ||
      startupRawResetReasonCpu0 == POWER_GLITCH_RESET;
  snprintf(snapshot.profileSource, sizeof(snapshot.profileSource), "%s",
           profileStore.sourceText());
  snprintf(snapshot.configVersion, sizeof(snapshot.configVersion), "%s",
           profileStore.configVersion());
  snapshot.profileFallback = !profileStore.usingLittleFs();
  snprintf(snapshot.profileError, sizeof(snapshot.profileError), "%s",
           profileStore.lastError());
  snprintf(snapshot.storageStateText, sizeof(snapshot.storageStateText), "%s",
           storageStatus.storageStateText);
  snapshot.storageFreeBytes = storageStatus.storageFreeBytes;
  snapshot.recording = storageStatus.recording;
  snapshot.logRecordCount = storageStatus.logRecordCount;
  snapshot.storageQueueDepth = storageStatus.snapshotQueueDepth;
  snapshot.storageQueuePeak = storageStatus.snapshotQueuePeak;
  snapshot.storageDroppedSnapshots = storageStatus.droppedSnapshots;
  snapshot.storageTaskStackHighWater = storageStatus.taskStackHighWater;
  snprintf(snapshot.storageError, sizeof(snapshot.storageError), "%s",
           storageStatus.storageError[0] ? storageStatus.storageError
                                         : storageStatus.loggerError);
  snapshot.rtcSupported = timeService.supported();
  snapshot.rtcValid = timeService.valid();
  snapshot.rtcHoldover = timeService.holdover();
  snprintf(snapshot.rtcStatusText, sizeof(snapshot.rtcStatusText), "%s",
           timeService.statusText());
  timeService.formatLocalDateTime(snapshot.rtcDateTimeText,
                                  sizeof(snapshot.rtcDateTimeText));
  snprintf(snapshot.ntpStatusText, sizeof(snapshot.ntpStatusText), "%s",
           wifiTimeService.statusText());

  switch (storageStatus.storageState) {
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
  if (appController.enterConfigUpload()) {
    drawConfigUpdatePage();
  }
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
  bool entered = appController.enterLogExport(FIRMWARE_VERSION_TEXT);
  if (!entered) return;
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
  StorageService::Status storageStatus;
  storageService.getStatus(storageStatus);
  snprintf(snapshot.statusText, sizeof(snapshot.statusText), "%s",
           storageStatus.exportStatus);
  snprintf(snapshot.currentFile, sizeof(snapshot.currentFile), "%s",
           storageStatus.exportFile);
  snapshot.transferOffset = storageStatus.exportOffset;
  snapshot.transferTotal = storageStatus.exportTotal;
  snprintf(snapshot.errorText, sizeof(snapshot.errorText), "%s",
           storageStatus.exportError);
  snapshot.state = AppLogExportState::Active;
  if (storageStatus.exportState == LogExportServer::State::Failed) {
    snapshot.state = AppLogExportState::Failed;
  } else if (storageStatus.exportState ==
             LogExportServer::State::WaitingCommand) {
    snapshot.state = AppLogExportState::WaitingCommand;
  }
}

/**
 * 切换到 TIME SYNC 独占模式并绘制初始页面。
 *
 * AppController 负责先关闭日志、停止 RS485 和清空 USB 残留；失败时保留
 * Fault 状态，不让 ESPTIME1 与其他协议同时读取串口。
 */
void enterTimeSyncMode() {
  if (!appController.enterTimeSync()) return;
  ui.clearScreen();
  drawTimeSyncPage();
}

// 从协议服务与 TimeService 生成固定快照，再交给 UI 绘制。
void drawTimeSyncPage() {
  updateTimeSyncStateSnapshot();
  ui.drawTimeSyncPage(buildTimeSyncView());
}

// 将 AppState 中自有存储的文本转换成无所有权问题的只读 UI View。
TimeSyncView buildTimeSyncView() {
  const TimeSyncState &snapshot = appState.timeSync;
  TimeSyncView view;
  view.statusText = snapshot.statusText;
  view.rtcValid = snapshot.rtcValid;
  view.rtcDateTimeText = snapshot.rtcDateTimeText;
  view.timeSourceText = snapshot.timeSourceText;
  view.ntpStatusText = snapshot.ntpStatusText;
  view.maskedSsid = snapshot.maskedSsid;
  view.errorText = snapshot.errorText;
  return view;
}

/**
 * 原子化整理校时页面需要的数据。
 *
 * RTC 时间来自 TimeService 的单调锚点；协议错误复制到 AppState，
 * 避免 UI 持有 TimeSyncServer 内部缓冲区的可变指针。
 */
void updateTimeSyncStateSnapshot() {
  TimeSyncState &snapshot = appState.timeSync;
  snprintf(snapshot.statusText, sizeof(snapshot.statusText), "%s",
           timeSyncServer.statusText());
  snapshot.rtcValid = timeService.valid();
  timeService.formatLocalDateTime(snapshot.rtcDateTimeText,
                                  sizeof(snapshot.rtcDateTimeText));
  snprintf(snapshot.timeSourceText, sizeof(snapshot.timeSourceText), "%s",
           timeService.sourceText());
  snprintf(snapshot.ntpStatusText, sizeof(snapshot.ntpStatusText), "%s",
           wifiTimeService.statusText());
  wifiTimeService.maskedSsid(snapshot.maskedSsid,
                             sizeof(snapshot.maskedSsid));
  snprintf(snapshot.errorText, sizeof(snapshot.errorText), "%s",
           timeSyncServer.errorText());
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
#if defined(PYXIS_BOARD_WAVESHARE_28)
  view.calibrationAvailable = calibrationProfileStore.profile() != nullptr;
#else
  view.calibrationAvailable = false;
#endif
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
  const SensorDiscovery::Result &identity = rs485Status.identity;
  snapshot.slaveAddress = identity.slaveAddress;
  snapshot.productNum = identity.productNum;
  snapshot.scanSequence =
      latestScanAvailable ? latestScanEvent.scanSequence : 0;
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
    target.value =
        latestScanAvailable ? latestScanEvent.values[index] : 0.0f;
    target.valid =
        latestScanAvailable && latestScanEvent.valid[index];
    target.decimals = source.decimals;
  }
}

void drawCalibrationPage() {
  CalibrationView view;
  view.phase = appState.calibration.phase;
  view.profile = calibrationProfileStore.profile();
  view.procedurePage = appState.calibration.procedurePage;
  view.selectedProcedure = appState.calibration.selectedProcedure;
  view.currentInput = appState.calibration.currentInput;
  view.numericText = appState.calibration.numericText;
  view.statusText = appState.calibration.statusText;
  view.commandIndex = appState.calibration.commandIndex;
  view.commandCount = appState.calibration.commandCount;
  ui.drawCalibrationPage(view);
}

bool startSelectedCalibration() {
  const CalibrationProfile *profile = calibrationProfileStore.profile();
  if (profile == nullptr ||
      appState.calibration.selectedProcedure >= profile->procedureCount) {
    return false;
  }
  const CalibrationProcedure &procedure =
      profile->procedures[appState.calibration.selectedProcedure];
  const bool outputSpan =
      procedure.category == CalibrationProcedureCategory::OutputSpan;
  uint8_t slaveAddress = rs485Status.identity.slaveAddress;
  if (!appController.beginCalibration(
          slaveAddress, profile, appState.calibration.selectedProcedure,
          appState.calibration.inputValues, procedure.inputCount)) {
    snprintf(appState.calibration.statusText,
             sizeof(appState.calibration.statusText),
             "%s", outputSpan ? "Unable to save output range"
                               : "Unable to start calibration");
    appState.calibration.phase = CalibrationUiPhase::Finished;
    return false;
  }
  appState.calibration.phase = CalibrationUiPhase::Running;
  appState.calibration.commandIndex = 0;
  appState.calibration.commandCount = procedure.commandCount;
  snprintf(appState.calibration.statusText,
           sizeof(appState.calibration.statusText), "%s",
           outputSpan ? "Saving output range..." : "Starting calibration...");
  return true;
}

void handleCalibrationTouch(int x, int y) {
  CalibrationState &state = appState.calibration;
  const CalibrationProfile *profile = calibrationProfileStore.profile();
  if (profile == nullptr) {
    appState.page = UiPage::SensorInfo;
    drawSensorInfoPage();
    return;
  }

  if (state.phase == CalibrationUiPhase::ProcedureList) {
    if (y < 40) {
      appState.page = UiPage::SensorInfo;
      drawSensorInfoPage();
      return;
    }
    constexpr uint8_t kOperationsPerPage = 5;
    const uint8_t pageCount =
        (profile->procedureCount + kOperationsPerPage - 1) / kOperationsPerPage;
    if (y >= 262) {
      if (x < BoardConfig::kDisplayWidth / 2) {
        if (state.procedurePage > 0) state.procedurePage--;
      } else if (state.procedurePage + 1 < pageCount) {
        state.procedurePage++;
      }
      drawCalibrationPage();
      return;
    }
    if (y >= 66 && y < 256) {
      uint8_t localIndex = static_cast<uint8_t>((y - 66) / 38);
      uint8_t selected = state.procedurePage * kOperationsPerPage + localIndex;
      if (selected >= profile->procedureCount || localIndex >= kOperationsPerPage)
        return;
      state.selectedProcedure = selected;
      state.currentInput = 0;
      const CalibrationProcedure &procedure = profile->procedures[selected];
      for (uint8_t index = 0; index < procedure.inputCount; ++index) {
        state.inputValues[index] =
            profile->inputs[procedure.firstInput + index].defaultValue;
      }
      if (procedure.inputCount == 0) {
        state.phase = CalibrationUiPhase::Confirm;
        snprintf(state.statusText, sizeof(state.statusText),
                 "Review instructions before START");
      } else {
        state.phase = CalibrationUiPhase::NumericInput;
        const CalibrationInput &input = profile->inputs[procedure.firstInput];
        snprintf(state.numericText, sizeof(state.numericText), "%.*f",
                 input.decimals, input.defaultValue);
        state.numericDefaultShown = true;
      }
      drawCalibrationPage();
    }
    return;
  }

  if (state.phase == CalibrationUiPhase::NumericInput) {
    const CalibrationProcedure &procedure =
        profile->procedures[state.selectedProcedure];
    const CalibrationInput &input =
        profile->inputs[procedure.firstInput + state.currentInput];
    if (y >= 120 && y < 272) {
      const int displayWidth = BoardConfig::kDisplayWidth;
      uint8_t column = min(static_cast<uint8_t>(x * 3 / displayWidth),
                           static_cast<uint8_t>(2));
      uint8_t row = min(static_cast<uint8_t>((y - 120) / 38),
                        static_cast<uint8_t>(3));
      static const char keys[12] = {'1', '2', '3', '4', '5', '6',
                                    '7', '8', '9', '-', '0', '.'};
      char key = keys[row * 3 + column];
      if (state.numericDefaultShown) {
        state.numericText[0] = '\0';
        state.numericDefaultShown = false;
      }
      size_t length = strlen(state.numericText);
      bool allowed = length + 1 < sizeof(state.numericText);
      if (key == '-' && length != 0) allowed = false;
      if (key == '.' && strchr(state.numericText, '.') != nullptr) allowed = false;
      if (allowed) {
        state.numericText[length] = key;
        state.numericText[length + 1] = '\0';
        state.statusText[0] = '\0';
        drawCalibrationPage();
      }
      return;
    }
    if (y >= 276) {
      if (x < BoardConfig::kDisplayWidth / 3) {
        state = CalibrationState{};
        drawCalibrationPage();
      } else if (x < (BoardConfig::kDisplayWidth * 2) / 3) {
        size_t length = strlen(state.numericText);
        if (length > 0) state.numericText[length - 1] = '\0';
        state.numericDefaultShown = false;
        state.statusText[0] = '\0';
        drawCalibrationPage();
      } else {
        char *end = nullptr;
        float value = strtof(state.numericText, &end);
        bool parsed = end != state.numericText && end != nullptr && *end == '\0' &&
                      isfinite(value);
        if (parsed && input.type == CalibrationValueType::UInt16 &&
            fabsf(value - roundf(value)) >= 0.0001f) {
          parsed = false;
        }
        bool below = input.minimumInclusive ? value < input.minimum
                                            : value <= input.minimum;
        bool above = input.maximumInclusive ? value > input.maximum
                                            : value >= input.maximum;
        if (!parsed || below || above) {
          snprintf(state.statusText, sizeof(state.statusText),
                   "Value must be %s%g to %s%g %s",
                   input.minimumInclusive ? "" : ">", input.minimum,
                   input.maximumInclusive ? "" : "<", input.maximum,
                   input.unit);
          drawCalibrationPage();
          return;
        }
        state.inputValues[state.currentInput] = value;
        state.currentInput++;
        if (state.currentInput < procedure.inputCount) {
          const CalibrationInput &next =
              profile->inputs[procedure.firstInput + state.currentInput];
          snprintf(state.numericText, sizeof(state.numericText), "%.*f",
                   next.decimals, next.defaultValue);
          state.numericDefaultShown = true;
        } else {
          state.phase = CalibrationUiPhase::Confirm;
          snprintf(state.statusText, sizeof(state.statusText),
                   "Review value and instructions");
        }
        drawCalibrationPage();
      }
    }
    return;
  }

  if (state.phase == CalibrationUiPhase::Confirm && y >= 235) {
    if (x < BoardConfig::kDisplayWidth / 2) {
      state = CalibrationState{};
    } else {
      startSelectedCalibration();
    }
    drawCalibrationPage();
    return;
  }

  if (state.phase == CalibrationUiPhase::Finished && y >= 235) {
    appController.exitCalibrationAndRediscover();
    ui.clearScreen();
    ui.drawDashboardStatic(currentUiOperatingMode());
  }
}
