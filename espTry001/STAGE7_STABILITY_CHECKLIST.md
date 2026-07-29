# 阶段 7 稳定性验收表

适用固件：V1.0.26 及以后。测试平台仍为 ESP32-S3-1.47TOUCH；阶段 7
通过前不要混入 2.8 寸或 LVGL 代码。

## 1. 烧录后基础回归

- [ ] 上电进入 Dashboard，触摸方向和所有页面返回正常；
- [ ] CH1/CH2 屏幕值及地址 247 的 40001～40007 与旧版一致；
- [ ] PASS 双向转发正常；
- [ ] 地址 0 普通发现、`0x67` 特殊发现及 PN/名称正确；
- [ ] 最多 16 字段分页、MASTER 轮询和每轮一条 CSV 正常；
- [ ] 切回 PASS 后 CSV 可下载且 CRC32 校验通过；
- [ ] ESPCFG1 上传、ESPLOG1 列出/下载/删除正常。

## 2. 两小时与二十四小时运行

让设备进入 MASTER 并保持传感器和 SD 卡连接。监控脚本与 ESPLOG1/ESPCFG1
不能同时占用同一 USB 端口。监控文件已存在时脚本会自动追加，夜间进程意外
退出后可使用相同命令和输出文件续跑。

```powershell
python tools/monitor_stability.py COM21 --interval 60 --duration 2 --output stability_2h.csv
python tools/monitor_stability.py COM21 --interval 60 --duration 24 --output stability_24h.csv
```

通过条件：

- [ ] `uptime_ms` 始终递增，没有 `DEVICE_RESTART`；
- [ ] `rs_dropped_events`、PASS 两项 drops、`storage_dropped_snapshots`
      和 `storage_command_failures` 均为 0；
- [ ] `rs_stack_high_water`、`storage_stack_high_water` 始终大于 1024；
- [ ] `minimum_free_heap` 预热后稳定，没有持续单向下降；
- [ ] UI、触摸、轮询和 CSV 记录持续响应；
- [ ] 24 小时结束后 CSV 可完整下载并通过 CRC32。

## 3. 重复切换

执行安排：由同事并行完成，不阻塞 2.8 寸平台准备工作。

- [ ] PASS ↔ MASTER 切换 100 次，可分为 10 组，每组 10 次；
- [ ] 每次进入 MASTER 都能重新发现并产生完整扫描；
- [ ] 每次返回 PASS 都能继续透传，CSV 文件可正常关闭；
- [ ] 完成后读取诊断寄存器，队列丢失和命令失败仍为 0。

## 4. 独占 USB 协议

执行安排：ESPCFG1/ESPLOG1 的剩余重复轮次由同事并行完成。

- [ ] ESPCFG1 使用当前有效 JSON 连续上传 20 次，每次 ACK 后正常重启并加载配置；
- [ ] ESPLOG1 连续执行 20 次列表及下载，逐文件 CRC32 通过；
  - [x] 第 1 次：列出 19 个文件，批量下载首次成功 18 个；`PN50661_S000019.csv`
    数据块读取超时，随后单独重试成功，19 个文件最终均通过 CRC32（2026-07-29）；
- [ ] 每次退出独占模式后 PASS、本地地址 247 和 MASTER 均能恢复。

## 5. 异常场景

### 从站断开

- [ ] MASTER 轮询时断开传感器，字段最终显示 `--`；
- [x] 断开期间 UI、触摸和 USB 仍响应（2026-07-29 实机通过）；
- [ ] 断开期间 ADC 和本地地址 247 仍响应；
- [x] 恢复连接后无需切换模式，MASTER 自动继续读数（2026-07-29 实机通过）；
- [ ] 恢复读数后 CSV 记录继续正常写入。

### 无 SD 卡

- [ ] 断电后移除 SD 再上电，System Info 显示 Missing/Error；
- [ ] PASS、MASTER、触摸、ADC 和本地地址 247 不受影响；
- [ ] 进入 LOG EXPORT 会明确报告 SD 不可用。

### SD 写失败

此项使用可丢弃的测试 SD 卡；运行中拔卡可能损坏当前文件或文件系统。

- [ ] MASTER 已记录至少一轮后制造写失败；
- [ ] System Info/诊断寄存器能看到存储错误；
- [ ] UI、触摸、RS485 和本地地址 247 仍响应；
- [ ] 测试后断电，再处理或格式化测试卡，不在通电状态重新插卡。

## 6. 完成记录

| 项目 | 结果/文件 |
|---|---|
| 固件版本 | V1.0.26 |
| 2 小时 CSV | 已由15小时长测覆盖；`stability_15h.csv` |
| 24 小时 CSV | |
| PASS/MASTER 100 次 | 已转交同事并行验证 |
| ESPCFG1 20 次 | 已转交同事并行验证 |
| ESPLOG1 20 次 | 1/20；首轮 19 个文件最终全部通过 CRC32，其余轮次已转交同事 |
| 异常场景 | 从站断开期间 UI/触摸/USB 正常，重新连接后自动恢复读数；其余检查待完成 |
| 最低 RS485 栈水位 | 6300 B |
| 最低 Storage 栈水位 | 5084 B |
| 历史最低空闲堆 | 253032 B |
| 最终结论 | 15小时范围通过；完整阶段7仍待其余项目 |
