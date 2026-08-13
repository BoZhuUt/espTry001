# 2.8 英寸校准模式实机验收清单

## 范围

本清单当前用于 `waveshare28` 和 PN 50661（ST-500）。1.47 英寸机型不提供校准入口。

首次部署必须分别烧录应用固件和 LittleFS：

```powershell
pio run -e waveshare28 -t upload
pio run -e waveshare28 -t uploadfs
```

`firmware.factory.bin` 当前不包含 LittleFS，不能代替第二条命令。执行烧录前确认端口和目标板，校准时不要连接真实工艺过程或可能造成危险输出的设备。

## 烧录前自动验证

```powershell
python -m unittest tools/test_import_calibration_profiles.py
python tools/test_calibration_executor_native.py
python tools/build_calibration_bundle.py --validate-only
pio run -e waveshare28
pio run -e waveshare28 -t buildfs
```

预期结果：

- 7 个导入/发布测试通过；
- 原生 C++ 校准执行器测试全部通过；
- 3 个 pilot Profile 可解析，57769 和 50661 为 approved；
- 固件和 `littlefs.bin` 构建成功。

## 发现与入口

1. 连接 PN 50661，进入 MASTER 并等待发现成功。
2. 打开 Sensor Info。
3. 确认底部出现 `CALIBRATE`。
4. 确认其他没有 approved Profile 的 PN 不显示该入口。
5. 确认 1.47 英寸固件不加载校准配置、不显示入口。

## PN 50661 零点校准

1. 选择 `zero_title`，阅读说明后点击 START。
2. 预期向从站发送 FC06：JSON 1-based 地址 1025 转为协议地址 `0x0400`，值为 4。
3. 设备合法回显后，界面至少等待 1000 ms。
4. 使用 FC03 读取 JSON 1-based 地址 1031（协议地址 `0x0406`）。
5. 结果值 0 显示成功；2048 显示探头污染；4096 显示 DI 信号过高。
6. 任何未定义结果值都显示结果未知，不宣称成功。
7. 退出后自动重新发现设备，并恢复正常参数轮询和新的日志会话。

## PN 50661 斜率校准

1. 选择 `slope_title`。
2. 数字键盘默认显示 100；输入范围为 1～300 ppb，只接受整数。
3. 输入实际使用的 PTSA 标液浓度并确认；不要在没有正确标准液时点击 START。
4. 第一条命令预期为 FC06：JSON 地址 1026 转为协议地址 `0x0401`，写入输入的整数浓度。
5. 合法响应后等待至少 100 ms。
6. 第二条命令预期为 FC06：JSON 地址 1025 转为协议地址 `0x0400`，值为 3。
7. 合法回显后等待至少 1000 ms，再用 FC03 读取地址 1031（协议地址 `0x0406`）。
8. 结果值 0 显示成功；256 显示信号过高；1024 显示信号过低。
9. 分别尝试 0、301 和 100.5，确认界面拒绝越界或非整数输入且不会发送 Modbus 写命令。

## 异常场景

- 写请求发出后无响应：显示结果未知，不宣称成功；
- CRC、从站地址、功能码或写回显不一致：显示通信失败；
- Modbus 异常响应：显示设备失败；
- 校准期间从 USB 发送网关数据：不得转发到 RS485；
- 校准过程中拔掉 RS485：界面必须结束于失败或结果未知，触摸和其他任务不能卡死；
- 结束页点击 EXIT：停止校准、重新发现，不继续旧轮询或旧日志会话；
- 校准运行中禁止重复 START，防止同一写命令被重复排队。

## 记录

验收时记录固件版本、校准 Profile ID、从站地址、输入值、每条请求/响应帧、界面终态和重新发现结果。若写请求已经发出但响应丢失，设备状态可能已经改变，应先重新读取设备状态，不要直接重复校准。

## 已完成实测（2026-08-13）

- [x] PN 50661 连续 3 次进入 MASTER 并完成发现，无重启、无新增 PANIC。
- [x] PN 50661 斜率校准成功。
- [x] 校准结束后自动返回 MASTER，并重新发现探头。
- [ ] PN 50661 零点校准。
- [ ] 断线、超时、异常响应及输入边界场景。
- [x] PN 50661 在 `CAL & OUTPUT` 中显示 `[OUTPUT] PTSA 4-20mA` / `20mA RANGE`。
- [x] 输入合法 ppb 值后完成 4–20 mA 输出量程设置，完成页显示 `Output range saved`。
- [ ] 0、301 和非整数输入被 UI 拒绝，不发送 Modbus 写命令。
