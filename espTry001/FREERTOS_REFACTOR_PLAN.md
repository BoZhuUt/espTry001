# ESP32-S3 1.47 平台模块化与 FreeRTOS 重构规划

> 创建日期：2026-07-23  
> 当前平台：Waveshare ESP32-S3 1.47 LCD，172 × 320，ST7789 + AXS5106L  
> 当前目标：只整理并稳定现有 1.47 平台，不接入 2.8 平台驱动，不引入 LVGL  
> 长期目标：形成可复用的业务层和清晰的硬件/UI边界，为 2.8 平台及 LVGL 迁移做准备

## 1. 为什么现在重构

当前工程的主要功能已经可用：

- 两路 4-20mA 采样；
- USB-RS485 PASS 转发；
- Modbus 主站发现、PN识别和JSON Profile轮询；
- Profile热更新；
- SD CSV记录；
- USB日志导出和删除；
- 触摸与多页面显示。

已有业务模块大多已经独立，但 `src/main.cpp` 超过 1200 行，仍同时承担硬件初始化、触摸路由、页面绘制、模式切换、USB协议、RS485编排、ADC采样和模块生命周期控制。继续直接增加功能，会让定位、修改和回归越来越困难。

需要特别说明：ESP32 Arduino 的 `setup()` 和 `loop()` 本身已经运行在 FreeRTOS 的 `loopTask` 中。因此本次工作的重点不是“为了使用 FreeRTOS 而创建很多任务”，而是先建立清晰模块边界，再将存在阻塞风险或需要独占资源的工作迁入少量任务。

## 2. 重构原则

1. **现有功能优先**：每一步都必须保持 1.47 平台功能与协议兼容。
2. **先拆文件，后拆任务**：第一阶段不改变执行时序，只做等价迁移。
3. **单一资源所有者**：显示、RS485、SD和USB各自只有一个执行上下文可以直接访问。
4. **任务通过消息协作**：不允许多个任务依靠无保护的全局变量互相控制。
5. **不滥用Task**：简单的ADC定时采样和UI刷新不单独创建任务。
6. **固定内存优先**：任务队列和数据快照使用固定上限，避免长期运行中的堆碎片。
7. **协议保持不变**：ESPCFG1、ESPLOG1、Modbus地址及CSV格式本阶段不改变。
8. **一次只改变一个维度**：不同时进行FreeRTOS重构、2.8移植、Arduino Core升级和LVGL接入。

## 3. 本阶段明确不做

- 不编译或集成 `waveshare28/` 中的任何代码；
- 不修改2.8平台的LCD、触摸、SD或引脚；
- 不引入LVGL；
- 不升级Arduino-ESP32主版本；
- 不改变JSON Profile格式；
- 不实现传感器YMODEM升级；
- 不为了“任务化”而把每个类都变成独立Task。

`waveshare28/` 仅作为未来迁移参考，当前构建必须忽略该目录。

## 4. 目标目录结构

```text
src/
├─ main.cpp                         程序入口，只做初始化和调度
│
├─ app/
│  ├─ app_mode.h                   全局业务模式定义
│  ├─ app_state.h                  UI需要的只读状态快照
│  ├─ app_event.h                  模块间事件与命令
│  └─ app_controller.h/.cpp        模式切换和模块生命周期编排
│
├─ board/
│  ├─ board_config.h               1.47平台尺寸、引脚和通信参数
│  ├─ display_driver.h/.cpp         ST7789和背光初始化
│  ├─ touch_driver.h/.cpp           AXS5106L初始化和坐标输出
│  └─ analog_input.h/.cpp           两路4-20mA采样与校准
│
├─ ui/
│  ├─ ui_manager.h/.cpp             页面切换、触摸路由和统一刷新
│  ├─ dashboard_page.h/.cpp
│  ├─ sensor_info_page.h/.cpp
│  ├─ system_info_page.h/.cpp
│  ├─ tools_page.h/.cpp
│  ├─ config_page.h/.cpp
│  └─ log_export_page.h/.cpp
│
├─ services/
│  ├─ rs485_service.h/.cpp          Serial1唯一所有者
│  ├─ storage_service.h/.cpp        SD_MMC唯一所有者
│  └─ usb_service.h/.cpp            USB CDC协议和模式仲裁
│
└─ 现有业务模块
   ├─ sensor_discovery.*
   ├─ sensor_profile.*
   ├─ sensor_profile_store.*
   ├─ sensor_profile_update.*
   ├─ data_logger.*
   ├─ sd_storage.*
   └─ log_export_server.*
```

目录结构是目标，不要求一次全部建立。每次只移动一类职责，并保持可编译、可回退。

## 5. 应用模式统一

当前的 `masterMode`、`configMode`、页面状态以及日志导出状态分散在不同判断中。重构后使用一个明确的业务模式：

```cpp
enum class AppMode : uint8_t {
  PassThrough,
  MasterDiscovery,
  MasterPolling,
  ConfigUpload,
  LogExport,
  Fault
};
```

页面不是业务模式。Dashboard、SystemInfo、SensorInfo和ToolsMenu只是视图状态，不能直接决定UART或SD的所有权。

模式切换统一由 `AppController` 执行。例如进入 `ConfigUpload`：

1. 请求RS485停止发现、轮询或PASS；
2. 请求DataLogger flush并关闭；
3. 清空USB接收帧缓存；
4. 将USB所有权交给ESPCFG1；
5. 更新 `AppState`；
6. 通知UI显示CONFIG页面。

禁止页面代码直接取消多个模块或重配串口。

## 6. 资源所有权

| 资源 | 唯一所有者 | 其他模块访问方式 |
|---|---|---|
| `gfx`、LCD、背光 | UI/Arduino loopTask | UI命令或AppState |
| AXS5106L触摸 | UI/Arduino loopTask | 转换为UI Action |
| `Serial1`、RS485 | Rs485Service/Rs485Task | 命令队列与事件队列 |
| `SD_MMC`文件操作 | StorageService/StorageTask | 日志快照或文件命令队列 |
| USB CDC `Serial` | UsbService | 模式化协议接口 |
| ADC1/ADC2 | 初期由loopTask采样 | 写入AppState快照 |
| LittleFS Profile | ProfileStore/Updater | CONFIG模式独占更新 |

### 6.1 禁止事项

- UI Task不能直接调用 `Serial1.read/write`；
- RS485 Task不能调用 `gfx`；
- RS485 Task不能直接写SD；
- Storage Task不能直接刷新UI；
- CONFIG和LOG EXPORT期间，其他模块不能向USB `Serial` 输出调试文本；
- 不允许两个Task同时调用同一个 `SensorDiscovery`、`SensorProfilePoller` 或 `DataLogger` 实例。

## 7. AppState设计

UI不直接读取各业务对象的内部成员，而是读取固定结构的状态快照：

```cpp
struct AppState {
  AppMode mode;
  UiPage page;

  float current1Ma;
  float current2Ma;

  DiscoveryState discoveryState;
  uint8_t slaveAddress;
  uint32_t productNum;
  char sensorName[24];

  float profileValues[kMaxSensorFields];
  bool profileValueValid[kMaxSensorFields];
  uint8_t profileFieldCount;
  uint32_t scanSequence;

  SdState sdState;
  bool recording;
  uint32_t logRecordCount;

  char statusText[64];
  char errorText[64];
};
```

实际实现可拆成更小的结构。关键规则是：

- 结构大小固定；
- UI读取时获得一致快照；
- 字符串有固定容量；
- 业务对象本身不暴露给UI；
- 更新方式使用队列、互斥锁或短临界区，不能一边写数组一边绘制。

## 8. 任务设计

最终建议只保留三个主要执行上下文。

### 8.1 UI/App执行上下文

继续使用Arduino自带的 `loopTask`，不额外创建UI Task。

职责：

- 触摸读取和去抖；
- UI Action生成；
- 页面切换与绘制；
- ADC定时采样；
- AppController更新；
- 发送RS485和Storage命令；
- 消费业务事件并更新AppState。

只有此执行上下文可以操作 `gfx`。未来引入LVGL后仍遵守同一规则。

### 8.2 Rs485Task

唯一拥有 `Serial1`，统一承载：

- USB-RS485 PASS数据；
- 普通地址0发现；
- 特殊 `00 67 ... 21 E6` 发现；
- PN和名称读取；
- JSON Profile参数轮询；
- 未来的传感器固件升级。

发现和轮询不能拆成两个Task，因为它们共用同一UART和同一总线。

建议命令：

```cpp
enum class Rs485CommandType : uint8_t {
  EnterPass,
  StartDiscovery,
  StartPolling,
  Stop,
  Shutdown
};
```

建议事件：

```cpp
enum class Rs485EventType : uint8_t {
  PassActivity,
  DiscoveryProgress,
  SensorFound,
  UnsupportedProfile,
  ScanCompleted,
  CommunicationError,
  Stopped
};
```

Task循环不得长时间阻塞。现有发现和Profile轮询状态机应继续采用短周期 `update()`，而不是改回同步Modbus读取。

### 8.3 StorageTask

唯一执行SD文件读写，承载：

- CSV创建和记录；
- 定时flush；
- 日志关闭和分卷；
- 日志目录列表；
- ESPLOG1文件读取和删除；
- 后续固件包读取。

RS485完成一轮Profile扫描后生成固定大小快照：

```cpp
struct SensorSnapshot {
  uint32_t scanSequence;
  uint32_t uptimeMs;
  uint8_t slaveAddress;
  uint32_t productNum;
  float current1Ma;
  float current2Ma;
  uint8_t fieldCount;
  float values[kMaxSensorFields];
  bool valid[kMaxSensorFields];
};
```

快照通过有限长度队列交给StorageTask。队列满时必须增加丢失计数并在UI提示，不能静默覆盖旧记录。

## 9. USB CDC设计注意事项

USB CDC当前承载：

- 本地Modbus响应；
- PASS转发；
- ESPCFG1配置上传；
- ESPLOG1日志导出；
- 调试输出。

重构后由 `UsbService` 统一判断USB协议所有权。CONFIG和LOG EXPORT是独占模式，其他任务不得同时向USB写调试信息。

可选策略：

1. 发布版本关闭普通 `Serial.printf`；
2. 调试信息先写入固定日志队列，仅在USB空闲时输出；
3. 所有二进制协议发送都集中到UsbService；
4. 为每种模式保留明确的进入、运行和退出动作。

## 10. 推荐优先级与调度

初始建议值，仅在实机测试后调整：

| 执行上下文 | 建议优先级 | 说明 |
|---|---:|---|
| Rs485Task | 4 | 及时处理UART和协议超时 |
| Arduino loopTask/UI | 保持框架默认 | 绘制、触摸和应用编排 |
| StorageTask | 2 | SD可能阻塞，但不能抢占通信 |

原则：

- 不要一开始绑定CPU核心，先使用默认调度并测量；
- Task循环必须 `vTaskDelay()`、阻塞等待队列或及时让出CPU；
- 不允许无限等待SD或串口；
- 开启Task Watchdog后，长文件操作必须分块；
- 队列、栈和高水位需要可观测。

## 11. 分阶段实施计划

### 阶段0：固定基线

- [ ] 确认当前Git工作区只包含预期内容；
- [ ] 给当前1.47稳定版本建立Git标签；
- [ ] 记录RAM、Flash、启动时间和主要功能测试结果；
- [ ] 确认 `waveshare28/` 不参与当前PlatformIO构建；
- [ ] 建立回归检查表。

### 阶段1：提取板级代码，不改变行为

- [ ] 新建 `board/board_config.h`，移动当前LCD、触摸、RS485、ADC和SD引脚；
- [ ] 提取ST7789初始化和背光控制；
- [ ] 提取AXS5106L初始化、读取与旋转转换；
- [ ] 提取ADC滤波和mA换算；
- [ ] 保持函数调用顺序和刷新周期不变；
- [ ] 编译并完成显示、触摸和ADC回归。

### 阶段2：提取UI页面，不改变行为

- [ ] 建立UiManager；
- [ ] 按页面移动绘制函数；
- [ ] 将固定触摸判断转换为UI Action；
- [ ] 页面只绘制传入状态，不直接启动或停止业务模块；
- [ ] 保持当前172×320布局完全一致；
- [ ] 回归所有页面和点击区域。

### 阶段3：建立AppMode、AppState和AppController

- [ ] 统一PASS、MASTER、CONFIG、LOG EXPORT模式；
- [ ] 将模式切换顺序集中到AppController；
- [ ] 建立状态快照；
- [ ] 删除分散的跨模块启停代码；
- [ ] 保持现有单线程执行；
- [ ] 回归全部工作流。

### 阶段4：统一USB服务

- [ ] 集中USB帧接收和协议选择；
- [ ] 保持本地Modbus、ESPCFG1和ESPLOG1协议不变；
- [ ] 明确独占模式；
- [ ] 收敛调试输出，避免污染二进制协议；
- [ ] 回归三个USB协议和PASS转发。

### 阶段5：迁移StorageTask

- [ ] 建立日志命令队列和SensorSnapshot队列；
- [ ] DataLogger只由StorageTask调用；
- [ ] LogExportServer的SD访问与记录互斥；
- [ ] 实现停止确认事件，确认flush/close后再切换模式；
- [ ] 增加队列满、写入失败和Task栈水位诊断；
- [ ] 回归CSV、Session、分卷、导出和删除。

### 阶段6：迁移Rs485Task

- [ ] 建立RS485命令和事件队列；
- [ ] Serial1只在Rs485Task初始化和访问；
- [ ] PASS、发现和Profile轮询统一在同一Task；
- [ ] ScanCompleted生成不可变快照；
- [ ] UI只消费事件，不读取轮询器中间状态；
- [ ] 回归普通/特殊发现、16字段读取和通信失败。

### 阶段7：稳定性与清理

- [ ] 连续运行2小时；
- [ ] 运行24小时并检查队列、堆和Task栈高水位；
- [ ] 反复切换PASS/MASTER至少100次；
- [ ] 配置上传、日志导出各重复20次；
- [ ] 从站断开、SD缺失、SD写失败时UI和USB仍可响应；
- [ ] 删除已经不再使用的全局变量和兼容代码；
- [ ] 更新README和运行流程文档。

### 阶段8：为未来平台保留接口

只有1.47版本完成长期验证后才开始：

- [ ] 增加2.8板级实现；
- [ ] 增加第二个PlatformIO环境；
- [ ] 不修改业务服务层；
- [ ] 先沿用Arduino_GFX完成2.8功能回归；
- [ ] 最后建立LVGL UI实现。

## 12. 每阶段最低回归清单

每一个阶段完成后至少验证：

- [ ] 上电正常进入Dashboard；
- [ ] 触摸方向、点击区域和页面返回正确；
- [ ] CH1/CH2显示和本地Modbus寄存器正确；
- [ ] PASS双向转发正常；
- [ ] 地址0普通发现正常；
- [ ] 特殊0x67发现正常；
- [ ] PN和JSON名称正确；
- [ ] 最多16个Profile字段分页显示正确；
- [ ] MASTER轮询正常；
- [ ] SD每轮生成一条完整CSV；
- [ ] 切回PASS后CSV安全关闭；
- [ ] ESPCFG1上传正常；
- [ ] ESPLOG1列表、下载和删除正常；
- [ ] 无SD卡时核心功能不受影响。

## 13. 可观测性要求

进入多Task后，System Info或调试接口至少能够查看：

- 当前AppMode；
- RS485 Task状态和最近错误；
- Storage Task状态和最近错误；
- 日志快照队列当前深度、峰值和丢失数量；
- RS485命令/事件队列峰值；
- 各Task剩余栈高水位；
- 当前空闲堆和历史最低空闲堆；
- SD记录数和最后一次flush时间。

这些信息必须使用固定缓冲，不应引入持续增长的日志对象。

## 14. 完成判定

满足以下条件后，才认为本轮FreeRTOS重构完成：

1. `main.cpp` 只保留入口、对象装配和少量顶层调度；
2. UI、RS485、Storage和USB的所有权无交叉；
3. RS485和Storage通过固定队列交换数据；
4. UI只读取AppState，不直接操作底层业务对象；
5. 所有现有协议和文件格式保持兼容；
6. 1.47平台通过完整回归和24小时运行；
7. 没有队列静默丢数据、任务看门狗复位或持续内存下降；
8. 2.8和LVGL仍未混入本阶段代码。

## 15. 下一步

第一项实际代码工作应是“阶段1：提取板级代码”。先移动引脚、显示、触摸和ADC实现，不创建任何新Task，也不改变现有行为。完成实机回归后，再进行UI页面拆分。
