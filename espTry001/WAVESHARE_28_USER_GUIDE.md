# Pyxis SensorLink 2.8 英寸设备使用说明

## 1. 适用范围

本文适用于使用 Waveshare 2.8 英寸触摸屏、ESP32-S3 和本项目
`waveshare28` 固件的 Pyxis SensorLink 设备。

设备主要功能：

- USB 与 RS485 Modbus RTU 双向透传；
- 自动发现探头并显示实时参数；
- 采集两路 4–20 mA 输入；
- 将实时数据记录到 microSD 卡；
- 执行探头校准和 4–20 mA 输出量程设置；
- 通过 USB 在线更新传感器显示配置和校准配置；
- 通过 USB 导出日志及同步 RTC 时间。

校准界面仅支持 2.8 英寸机型，1.47 英寸机型不提供校准入口。

## 2. 使用前准备

### 2.1 接线和安全

1. 断电状态下连接探头电源、RS485 A/B 和公共地。
2. 确认 A/B 极性及探头供电电压正确。
3. MASTER 自动发现时，总线上只连接一个会响应地址 0 的探头，避免总线冲突。
4. 校准前准备正确的标准液和工艺条件，不要在不确定命令含义时执行校准。
5. 4–20 mA 输出量程设置会改变探头输出配置，执行前确认目标通道和单位。

正常通信参数为 `9600, 8E1`。只有 `BOOT PASS` 固件升级透传模式使用
`9600, 8N1`。

### 2.2 首次部署

全新下位机首次使用时，需要分别烧录应用固件和 LittleFS：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" `
  run -e waveshare28 -t upload --upload-port COM7

& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" `
  run -e waveshare28 -t uploadfs --upload-port COM7
```

将 `COM7` 替换为设备实际串口。烧录或重启后 COM 号可能发生变化，这是正常现象。

完成首次部署后，日常增删传感器或校准 JSON 可以使用在线更新，不需要再次烧录
LittleFS。

## 3. 主界面和触摸操作

设备上电后进入 Dashboard。常用触摸区域：

- 点击顶部当前模式名称：切换运行模式；
- 点击最上方标题区域：进入 `SYSTEM INFO`；
- MASTER 发现探头后，点击 Dashboard 底部：进入探头详情；
- 大多数子页面点击顶部标题：返回上一页或退出当前工具。

模式按以下顺序循环：

```text
PASS → MASTER → PRECHECK → BOOT PASS → PASS
```

切换模式时，设备会先停止当前 RS485、日志或 USB 会话，再进入下一模式。请等待界面
完成切换，不要连续快速点击。

## 4. 四种运行模式

### 4.1 PASS

PASS 是普通 USB–RS485 透传模式：

- USB 收到的非本机 Modbus 请求转发到 RS485；
- RS485 响应转回 USB；
- 通信参数为 `9600, 8E1`；
- 本机 Modbus 从站地址为 247，可读取两路 4–20 mA 和诊断数据。

屏幕会短暂显示 `USB -> RS485` 或 `RS485 -> USB` 传输方向。

### 4.2 MASTER

MASTER 用于自动发现和轮询探头：

1. 先尝试普通地址 0 发现；
2. 普通发现失败后，自动尝试特殊 `0x67` 发现；
3. 读取探头 PN；
4. 按 Sensor Profile 显示实时参数；
5. 按 Calibration Profile 决定是否显示校准入口；
6. microSD 可用时自动创建日志会话。

发现完成后，点击 Dashboard 底部进入 Sensor Info。详情页显示：

- 从站地址、PN 和型号；
- 实时参数及有效状态；
- 多页参数的页码；
- 有校准配置时显示 `CALIBRATE`。

Sensor Profile 和 Calibration Profile 相互独立。没有实时参数 Profile、但存在校准
Profile 时，页面会显示 `No live parameter profile` 和
`Calibration available`，仍可进入校准。

### 4.3 PRECHECK

PRECHECK 是固定预检模式：

- 固定访问从站地址 15；
- 读取预定义的 `s470Raw` 和 `t365Raw`；
- 用于生产或维护阶段的快速检查。

如果目标设备不是地址 15，数值会显示无效或通信错误。

### 4.4 BOOT PASS

BOOT PASS 用于探头固件升级数据透传：

- 通信参数为 `9600, 8N1`；
- USB 数据直接与 RS485 升级链路交互；
- 不用于更新本机 JSON 配置。

更新 Sensor Profile 或 Calibration Profile 时应进入 `CONFIG UPLOAD`，不要使用
BOOT PASS。

## 5. 系统信息和工具菜单

在 Dashboard 点击顶部标题进入 `SYSTEM INFO`。页面可查看：

- 固件版本；
- 复位原因及 CPU 原始复位码；
- Sensor Profile 来源和配置版本；
- RTC、NTP 和时间状态；
- microSD 状态、剩余空间和日志计数。

点击页面底部 `TOOLS MENU` 进入工具菜单：

- `CONFIG UPLOAD`：在线更新 Sensor Profile 或 Calibration Profile；
- `LOG EXPORT`：通过 USB 下载或删除 microSD 日志；
- `TIME SYNC`：通过电脑设置 RTC，必要时配置 NTP。

这些工具会独占 USB。进入工具页面前应关闭串口监视器、Modbus Poll 或其他占用 COM
口的软件。

## 6. 校准和 4–20 mA 输出设置

### 6.1 进入校准

1. 切换到 MASTER；
2. 等待探头发现完成；
3. 点击 Dashboard 底部进入 Sensor Info；
4. 点击右下角 `CALIBRATE`；
5. 在 `CAL & OUTPUT` 页面选择操作。

设备最多显示 10 个操作，分为两页，每页最多 5 个。使用页面底部的上一页和下一页
区域翻页。

### 6.2 输入数值

- 数字键盘会显示 JSON 中配置的默认值、单位和允许范围；
- `UInt16` 输入只允许整数；
- `float32` 输入可使用小数；
- 超出范围、格式错误或缺少必要小数位时不会发送 Modbus 写命令；
- 输入完成后检查确认页，再点击 START。

### 6.3 执行和退出

执行过程中不要断开探头、切换模式或重复点击 START。界面会显示命令进度以及探头返回
的成功、失败或等待信息。

完成后点击退出区域，设备会返回 MASTER，并重新发现探头及恢复实时参数轮询。若写命令
已经发出但响应丢失，不要立即重复校准，应先重新读取探头状态。

## 7. 在线更新配置

### 7.1 进入 CONFIG UPLOAD

```text
Dashboard 顶部
→ SYSTEM INFO
→ TOOLS MENU
→ CONFIG UPLOAD
```

屏幕显示 `Waiting for ESPCFG1/ESPCAL1` 后再操作电脑。更新成功时显示 ACK，随后设备
自动重启；点击页面顶部可以取消并重启。

### 7.2 更新 Sensor Profile

Sensor Profile 决定 MASTER 中显示和轮询哪些实时参数。

1. 打开 `SensorProfileManager.exe` 编辑并保存 `sensor_profiles.json`；
2. 让设备进入 `CONFIG UPLOAD`；
3. 打开 `SensorProfileUpdater.exe`；
4. 选择设备 COM 和 `sensor_profiles.json`；
5. 点击上传并等待设备重启。

### 7.3 更新 Calibration Profile

Calibration Profile 决定校准命令、输入范围、结果映射和 4–20 mA 输出设置。

1. 打开 `CalibrationProfileManager.exe`；
2. 导入手机 APP JSON，或打开已有规范化 Profile；
3. 审核每个操作并设为 approved；
4. 让设备进入 `CONFIG UPLOAD`；
5. 点击 `Publish to Device`；
6. 选择 COM，确认后等待 ACK 和自动重启。

发布是完整替换：只有 approved Profile 会写入下位机。draft、blocked 或已从发布目录删除
的 PN 不会出现在新的活动索引中。

## 8. microSD 日志

建议在上电前插入已正确格式化的 microSD 卡。MASTER 成功识别并轮询受支持探头时，
设备自动在 `/logs` 目录创建 CSV 日志，记录探头参数、两路 4–20 mA 和校验信息。

注意事项：

- 记录过程中不要直接拔卡；
- 切换模式或进入 LOG EXPORT 时，设备会先刷新并关闭当前日志；
- `SYSTEM INFO` 显示 `SD not ready` 时，检查卡、重新插入并重启；
- 存储队列 Lost 数值不应持续增加。

导出日志：

1. 进入 `TOOLS MENU → LOG EXPORT`；
2. 等待屏幕显示 `Ready for PC commands`；
3. 打开 `SensorDataExporter.exe`；
4. 选择 COM，刷新文件列表；
5. 下载需要的 CSV；确认备份后才删除设备文件。

## 9. 时间同步

1. 进入 `TOOLS MENU → TIME SYNC`；
2. 确认没有其他程序占用串口；
3. 在电脑运行：

```powershell
python sync_rtc.py COM7
```

将 `COM7` 替换为实际端口。工具可设置 RTC，并按参数选择是否配置 Wi-Fi/NTP。完成后在
SYSTEM INFO 查看 RTC 和 NTP 状态。

## 10. 一键打包电脑工具

开发电脑双击：

```text
tools\One_Click_Package_All_Tools.bat
```

输入包名，直接回车使用默认名称 `Pyxis_SensorLink_Tools_`。脚本会生成完整工具文件夹
和同名 ZIP，内容包括 EXE、JSON、Calibration Profiles、导入/构建/上传脚本和本文档。

复制使用时应复制整个工具目录，不要只复制单个 EXE。校准在线发布需要 PlatformIO
Python，或安装了 `pyserial` 的系统 Python。

## 11. 常见问题

### MASTER 一直找不到探头

- 检查供电、A/B 极性和公共地；
- 确认通信参数为 `9600, 8E1`；
- 确认总线上没有多个同时响应地址 0 的设备；
- 切回 PASS 后再进入 MASTER，重新启动发现。

### 显示 Unsupported PN

探头已发现，但 Sensor Profile 和 Calibration Profile 都没有该 PN。先在电脑工具中增加
并发布对应配置。

### 没有 CALIBRATE 按钮

- 对应 PN 没有 Calibration Profile；
- Profile 仍是 draft/blocked；
- 修改后尚未执行 `Publish to Device`；
- 发布成功后尚未重新进入 MASTER 发现探头。

### 上传提示超时或串口拒绝访问

- 先让屏幕进入 `CONFIG UPLOAD`；
- 关闭串口监视器、Modbus Poll 和其他串口工具；
- 刷新并选择设备重启后重新枚举的 COM；
- 拔插 USB 后重新尝试。

### SD not ready

- 断电后重新插卡；
- 检查卡格式和触点；
- 重启后在 SYSTEM INFO 查看状态；
- 设备不会自动格式化存储卡。

### 设备异常重启

记录 SYSTEM INFO 中完整的 `Reset` 文本、`Rxx/xx` 原始复位码、固件版本、操作步骤和
所连接探头 PN。出现 `PANIC` 时不要反复执行导致重启的操作，应先保留这些信息用于定位。
