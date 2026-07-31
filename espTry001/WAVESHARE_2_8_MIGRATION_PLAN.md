# Waveshare ESP32-S3-Touch-LCD-2.8 迁移计划

更新时间：2026-07-29

## 1. 目标与边界

目标是在不破坏现有 1.47 寸稳定版本的前提下，让同一套业务层同时支持
Waveshare ESP32-S3-Touch-LCD-2.8。

本阶段遵循以下边界：

- 保留现有 1.47 寸 PlatformIO 环境和固件行为；
- 新增独立的 2.8 寸 PlatformIO 环境；
- Rs485Service、StorageService、UsbService、AppController 和传感器业务逻辑不做
  2.8 寸专用分支；
- 第一轮继续使用 Arduino_GFX 和现有 UiManager，不直接迁移 LVGL；
- 先完成板级驱动和全部功能回归，最后才建立 LVGL UI；
- 不引入当前产品不需要的音频、IMU、RTC、Wi-Fi 或蓝牙功能。

## 2. 资料来源

- 微雪主板原理图：
  `C:\Users\15189\Downloads\ESP32-S3-Touch-LCD-2.8.pdf`
- 配套外围板原理图：
  `C:\Users\15189\Downloads\SCH_Schematic2_2026-07-29.pdf`
- 本地微雪示例：
  `C:\Users\15189\Downloads\ESP32-S3-LCD-2.8-Demo`
- 微雪官方文档：
  <https://docs.waveshare.net/ESP32-S3-Touch-LCD-2.8/>
- 微雪官方资源页：
  <https://docs.waveshare.net/ESP32-S3-Touch-LCD-2.8/Resources-And-Documents/>

本地示例中的 `Simulated_Gesture` 是按键模拟输入，不能作为触摸硬件依据。
触摸芯片和协议必须以实物背面版本标签及对应的官方 V1/V2 资料为准。

## 3. 关键版本确认

微雪自 2026-06-05 起切换产品版本：

- V1：ST7789 + CST328；
- V2：ST7789 + CST3530；
- 两者通过开发板背面标签区分。

当前实物已确认是 V2，目标触摸控制器锁定为 CST3530。触摸公共接口保持一致，
但 CST328 和 CST3530 使用独立的协议实现，不能把其中一种协议写死在通用板级
配置中。

## 4. 已确认硬件映射

| 功能 | 1.47 寸 | 2.8 寸 |
|---|---:|---:|
| 显示分辨率 | 172 × 320 | 240 × 320 |
| LCD 控制器 | ST7789 | ST7789 |
| LCD MOSI | GPIO39 | GPIO45 |
| LCD SCLK | GPIO38 | GPIO40 |
| LCD CS | GPIO21 | GPIO42 |
| LCD DC | GPIO45 | GPIO41 |
| LCD RST | GPIO40 | GPIO39 |
| LCD 背光 | GPIO46 | GPIO5 |
| 触摸 SDA | GPIO42 | GPIO1 |
| 触摸 SCL | GPIO41 | GPIO3 |
| 触摸 RST | GPIO47 | GPIO2 |
| 触摸 INT | GPIO48 | GPIO4 |
| RS485 RX | GPIO43 | GPIO43 |
| RS485 TX | GPIO44 | GPIO44 |
| CH1 ADC | GPIO1 | GPIO15 |
| CH2 ADC | GPIO2 | GPIO18 |
| SD CLK | GPIO16 | GPIO14 |
| SD CMD | GPIO15 | GPIO17 |
| SD D0 | GPIO17 | GPIO16 |
| SD 模式 | 4-bit | 1-bit |
| SD D3/CS | GPIO14 | GPIO21，初始化时保持高电平 |
| Flash / PSRAM | 8MB / 无 | 16MB / 8MB |

外围板的 INA193 使用 5Ω 采样电阻和约 20 倍增益，等效为 100V/A。现有
`kShuntResistorOhms = 100.0f` 的换算关系可以保留，只切换 ADC 引脚。

外围板 USB-C 直接转接主板 GPIO19/20 的原生 USB D-/D+，它不是第二个 USB
控制器。USB CDC、ESPCFG1 和 ESPLOG1 的软件架构保持不变。

## 5. PlatformIO 环境规划

保留：

- `env:esp32s3`：当前 1.47 寸、8MB Flash、无 PSRAM。

新增：

- `env:waveshare28`：2.8 寸、16MB Flash、8MB OPI PSRAM。

2.8 环境至少需要：

- `default_16MB.csv` 分区；
- `qio_opi` memory type；
- `BOARD_HAS_PSRAM`；
- 独立板卡宏，例如 `PYXIS_BOARD_WAVESHARE_28`；
- 继续启用原生 USB CDC；
- 第一阶段沿用当前 Arduino Core，避免把框架升级和板级迁移混在一起；
- 如果对应 V2 驱动明确依赖 Arduino Core 3，再只对 2.8 环境评估升级，
  不改变 1.47 环境。

默认编译环境在 2.8 实机回归完成前仍保持 `esp32s3`。

## 6. 板级代码结构

计划采用以下职责边界：

```text
src/board/
  board_config.h                 选择当前板卡配置
  boards/
    waveshare_147_config.h       1.47 引脚与能力
    waveshare_28_config.h        2.8 引脚与能力
  display_driver.*               统一显示初始化接口
  touch_driver.*                 统一 begin/read 接口
  touch/
    axs5106l_touch.*             现有 1.47 驱动
    cst328_touch.*               仅 V1
    cst3530_touch.*              仅 V2
  analog_input.*                 从板级配置读取 ADC 引脚
```

SdStorage 继续作为存储薄封装，但从板级配置读取：

- SD_MMC 是 1-bit 还是 4-bit；
- CLK/CMD/D0/D1/D2/D3 引脚；
- 初始化前是否需要将 D3/CS 拉高；
- 禁止初始化失败时自动格式化用户 SD 卡。

业务层不得直接包含 `PYXIS_BOARD_*` 条件分支。

## 7. 分阶段实施与提交边界

### 2.8-A：双环境和配置骨架

状态：已完成（2026-07-29）。

- [x] 新增 `waveshare28` 环境；
- [x] 拆分两套板级配置；
- [x] 暂不改变 1.47 的驱动行为；
- [x] 两个环境均完成 release 编译。

编译基线：

- `esp32s3`：RAM 20.5%，Flash 16.0%，`firmware.bin` 533760 字节；
- `waveshare28`：RAM 20.5%，Flash 8.2%，`firmware.bin` 537248 字节；
- 2.8 环境已解析为 16MB Flash、`default_16MB.csv`、`qio_opi` 和
  `BOARD_HAS_PSRAM`。

当前 2.8 产物已接入显示、电源保持、CST3530、1-bit SD_MMC、ADC 和
RS485 板级配置，可作为首个完整实机测试固件。UI 仍沿用 1.47 的 172 像素
布局，显示在 240 像素屏幕左侧属于 2.8-D 之前的预期现象。

建议提交：

```text
refactor: 建立1.47与2.8双板编译环境
```

### 2.8-B：显示、电源和触摸最小验证

- [x] 代码完成并通过 `esp32s3`、`waveshare28` 双环境编译（2026-07-29）；
- [ ] 2.8 V2 实机验证显示方向、背光、电源保持和触摸四角坐标；

- 初始化 2.8 的 ST7789、240 × 320 分辨率和背光；
- 必要时初始化板载电源保持逻辑；
- 接入已确认的 V2 CST3530；
- 串口输出原始触摸坐标；
- 校验旋转方向、四角坐标和松手事件；
- 暂时只绘制测试色块和坐标，不进入业务回归。

建议提交：

```text
feat: 增加2.8寸显示与触摸板级驱动
```

### 2.8-C：SD、ADC 和 RS485 板级适配

状态：已完成（2026-07-31）。

- [x] 代码完成并通过 `esp32s3`、`waveshare28` 双环境编译（2026-07-29）；
- [x] 2.8 V2 实机验证 CH1/CH2 4–20mA 采样正常（2026-07-31）；
- [x] 2.8 V2 实机确认线缆方向为 RS485 RX=GPIO43、TX=GPIO44，PASS 与 MASTER 均正常（2026-07-31）；
- [x] 2.8 V2 实机验证 SD 初始化与日志写入正常（2026-07-31）；
- [x] ESPLOG1 下载约 2KB 日志文件成功（2026-07-31）；
- [x] ESPLOG1 下载约 7KB 的跨 4096 字节边界日志文件并通过 CRC32（2026-07-31）；

- 2.8 使用 1-bit SD_MMC；
- GPIO21 在 SD 初始化前保持高电平；
- CH1/CH2 改为 GPIO15/18；
- RS485 保持 GPIO43/44、9600bps；
- 本地地址 247 的寄存器和字序不变。

建议提交：

```text
feat: 完成2.8寸存储与采集接口适配
```

### 2.8-D：现有 UI 功能回归

- [x] 横向尺寸改为跟随板卡屏幕宽度，2.8 使用完整 240 像素（2026-07-29）；
- [x] `waveshare28` 编译通过（2026-07-29）；
- [x] 2.8 实机逐页检查布局和触摸命中区域（2026-07-29）；

- 先让现有 UiManager 在 240 × 320 上完整运行；
- 保持现有 UiAction 和触摸命中语义；
- 将硬编码的 172/165 像素宽度逐步改成布局参数；
- 不在此阶段引入 LVGL。

建议提交：

```text
feat: 适配240x320现有UI布局
```

### 2.8-E：完整业务回归

已完成的不依赖底板项目：

- [x] ESPCFG1 正常上传并在重启后从 LittleFS 加载（2026-07-29）；
- [x] 错误 Magic、零长度、超长、CRC32 错误、损坏 JSON 和错误 Schema 均正确拒绝（2026-07-29）；
- [x] 失败后不退出页面直接重试成功（2026-07-29）；
- [x] 连续 5 次上传、安装和重启全部通过（2026-07-29）；
- [x] PASS 透传与 MASTER 发现均通过实机验证（2026-07-31）；
- [x] SD 初始化、日志写入和约 2KB ESPLOG1 下载通过（2026-07-31）；

按顺序验证：

1. 上电、显示、背光和触摸；
2. CH1/CH2 读数及地址 247；
3. PASS 双向转发；
4. 地址 0 普通发现和厂商 0x67 特殊发现；
5. 最多 16 个 Profile 字段和分页；
6. MASTER 轮询；
7. 每轮一条完整 CSV；
8. PASS/MASTER 切换时安全关闭日志；
9. ESPCFG1 上传；
10. ESPLOG1 列表、下载、删除和 CRC32；
11. 无 SD、断开传感器和 USB 独占模式恢复；
12. 2 小时稳定性监控。

### 2.8-F：LVGL

只有 2.8-E 全部通过后才开始：

- LVGL 只读取 AppState/View 快照；
- LVGL 回调只产生 UiAction；
- 不允许 LVGL 直接访问 RS485、SD、ProfileStore 或任务内部状态；
- 先替换 Dashboard，再逐页替换；
- Arduino_GFX 版本保留到 LVGL 完整回归结束。

## 8. 进入实现前的确认项

- [x] 实物背面标签确认为 V2，使用 CST3530（2026-07-29）；
- [x] 外围板最终版本使用 GPIO15/18 采集，已按 V2 原理图确认（2026-07-29）；
- [x] 外围板和成品线缆实测要求 RX=GPIO43、TX=GPIO44（2026-07-31）；
- [x] 外接 USB-C 是 ESP32-S3 原生 USB D+/D- 延伸，作为日常 USB 端口（2026-07-29）；
- [ ] 使用微雪原厂固件先验证屏幕和触摸硬件本身正常；
- [ ] 保留当前 1.47 固件和测试方法作为回归基线。
