# ESP32-S3 RS485 Utility

基于微雪 ESP32-S3-1.47TOUCH 核心版的 RS-485 调试工具。

## 功能

- **USB ↔ RS485 双向透传**：透传模式下串口数据双向转发，电脑作为 Modbus 主站
- **Modbus RTU 轮询**：主站模式下轮询从站 15，读取 1-based 地址 46037 起的 4 个保持寄存器，并显示 `s470Raw`、`t365Raw` 两个 float
- **4-20mA 采样**：两路 ADC 采集 4-20mA 信号并显示
- **触摸交互**：点击主页 Mode 区域切换透传/主站模式，点击标题区切换系统信息页

## 硬件配置

| 模块 | 引脚 | 参数 |
|------|------|------|
| LCD (ST7789) | DC=45, CS=21, SCK=38, MOSI=39, RST=40, BL=46 | 172×320, SPI |
| RS-485 | RX=43, TX=44 | 9600, 8E1, 自动收发芯片（软件交叉映射） |
| 4-20mA ADC | CH1=IO1, CH2=IO2 | ADC_11db |
| 触摸 (AXS5106L) | SDA=42, SCL=41, RST=47, INT=48 | I2C, 0x63 |

## 构建

```bash
pio run          # 编译
pio run -t upload # 烧录
pio device monitor # 串口监视 (115200)
```

## 版本号

通过 `get_git_version.py` 自动从 git 提交计数生成 `V1.0.n`，编译时注入 `AUTO_VERSION` 宏。

## 依赖

- [GFX Library for Arduino](https://github.com/moononournation/Arduino_GFX) @ 1.4.7
- [ModbusMaster](https://github.com/4-20ma/ModbusMaster) @ ^2.0.1
