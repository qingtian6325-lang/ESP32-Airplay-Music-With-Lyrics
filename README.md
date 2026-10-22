# ESP32 AirPlay & Bluetooth 实时滚动歌词桌面音箱

基于 **ESP32-WROVER-E (8MB PSRAM)** 的开源智能音频桌面摆件。支持通过 **苹果 AirPlay 1 (RAOP)** 及 **手机/电脑经典蓝牙 (A2DP)** 无线播放音乐（完美兼容 Apple Music 与 YouTube Music），并外接一块 2.8 ~ 3.5 寸彩色 TFT 屏幕，实时从在线歌词库（LRCLIB）拉取歌词并同步高亮滚动显示。

---

## ✨ 核心特性

- **双无线音频输入 (双模免切换)**：
  - **Apple AirPlay 1 (RAOP)**：支持 iPhone、iPad、Mac 在控制中心一键投屏，44.1kHz 16-bit 无损/CD 级传输。
  - **经典蓝牙 A2DP Sink**：通用手机、电脑蓝牙配对连接，支持 AVRCP 播放控制与歌曲元数据回传。
  - **智能音频仲裁**：AirPlay 优先抢占，AirPlay 开始播放时自动暂停蓝牙，停止后自动恢复。
- **I2S 高保真数字输出**：
  - 通过 I2S 协议直推 **MAX98357A** D 类功放模块，免去模拟 DAC 损耗，驱动 4Ω 3W~5W 扬声器。
  - 支持 FreeRTOS 互斥保护的 DMA 双缓冲推流与软件平滑音量控制。
- **实时同步滚动歌词 (Layout A 布局)**：
  - 音频流提取歌名与歌手信息后，在 Core 1 异步通过 Wi-Fi 调用 **LRCLIB** 开放 API 获取时间戳 `.lrc` 文本。
  - 支持毫秒级二分查找索引，屏幕呈现 **5 行滚动歌词**，当前演唱行以青蓝色 (`#00D2FF`) 加粗高亮放大显示。
  - 无网络或纯音乐自动优雅回退为大字号歌曲信息与状态界面。
- **FreeRTOS 双核高效调度**：
  - **Core 0 (`CORE_AUDIO`)**：专职负责 Wi-Fi、蓝牙协议栈、AirPlay RTSP/RTP 服务及 I2S DMA 音频推流，杜绝卡顿爆音。
  - **Core 1 (`CORE_UI`)**：负责 LVGL 8.x 界面渲染、网络歌词拉取及 10Hz 播放进度与歌词对齐计时器。
  - **8MB 片外 PSRAM 深度优化**：动态将歌词缓存与 LVGL 帧缓冲挂载至 PSRAM，保障内部内存充沛。

---

## 🛠️ 硬件物料清单 (BOM)

| 部件 | 推荐型号 / 规格 | 说明 |
| :--- | :--- | :--- |
| **主控板** | **ESP32-WROVER-E (CH340)** | 必须带 **8MB PSRAM**、板载天线、出厂已焊接好排针 |
| **音频功放** | **MAX98357A I2S 功放模块** | 单声道 3W，支持 5V 供电，I2S 数字接口直连 |
| **显示屏** | **2.8 寸或 3.2 寸 SPI 彩色 TFT 屏** | ST7789 或 ILI9341 驱动，分辨率 320×240 |
| **扬声器** | **4Ω 3W ~ 5W 全频微型小喇叭** | 建议带小音腔/共振盒，低音更饱满 |
| **配件** | 面包板 1 块、杜邦线若干、Type-C 数据线 (5V 2A 供电) | 用于免焊接连线组装 |

---

## 🔌 硬件接线引脚图 (Wiring Pinout)

> [!WARNING]
> **绝对避坑原则**：ESP32-WROVER-E 模组内部已将 **GPIO 16** 和 **GPIO 17** 用于连接外部 PSRAM 芯片，**严禁外部引线连接 GPIO 16 和 17**！本项目已完全避开这两个引脚。

### 1. I2S 数字音频功放模块 (MAX98357A)
| MAX98357A 引脚 | ESP32-WROVER-E 引脚 | 说明 |
| :--- | :--- | :--- |
| **VIN** | **5V / VBUS** | 必须接 5V 保证功放输出功率 |
| **GND** | **GND** | 电源共地 |
| **BCLK** | **GPIO 25** | 位时钟 (Bit Clock) |
| **LRC** | **GPIO 26** | 声道选择时钟 (Word Select) |
| **DIN** | **GPIO 27** | I2S 串行音频数据 |
| **GAIN** | 悬空 | 默认 9dB 增益 |
| **SD_MODE** | 悬空 | 默认混音左右声道为单声道 |

### 2. SPI TFT 彩色液晶屏 (ST7789 / ILI9341 320×240)
| 屏幕引脚 | ESP32-WROVER-E 引脚 | 说明 |
| :--- | :--- | :--- |
| **VCC** | **3.3V** | 屏幕逻辑与芯片供电 |
| **GND** | **GND** | 电源共地 |
| **CS** | **GPIO 5** | SPI 片选信号 (VSPI CS0) |
| **DC / RS** | **GPIO 2** | 命令 / 数据切换控制 |
| **RST** | **GPIO 4** | 硬件复位引脚 |
| **SCL / SCK** | **GPIO 18** | SPI 时钟 (VSPI SCK) |
| **SDA / MOSI**| **GPIO 23** | SPI 主机数据输出 (VSPI MOSI) |
| **BLK** | **GPIO 21** | 屏幕背光控制引脚 |

---

## 💻 固件编译与烧录

本项目使用 [PlatformIO](https://platformio.org/) 进行管理与编译。

### 1. 运行本地全套自动化测试
```bash
pio test -e native
```
*(通过 109 项涵盖 I2S 音量、蓝牙 AVRCP、AirPlay DAAP 解析、LRC 时间戳查找、LVGL 状态、双核音频仲裁的单元测试)*

### 2. 编译并烧录固件至 ESP32
1. 使用 Type-C 数据线将 ESP32-WROVER-E 板子连接电脑。
2. 打开 `src/main.cpp`，填入你的家用 Wi-Fi 名称和密码：
   ```cpp
   #define WIFI_SSID     "你的WiFi名称"
   #define WIFI_PASSWORD "你的WiFi密码"
   ```
3. 在终端执行一键编译与烧录：
   ```bash
   pio run -e esp32dev --target upload
   ```
4. 打开串口监视器查看开机日志：
   ```bash
   pio device monitor -b 115200
   ```

---

## 🎵 使用说明

1. **连接蓝牙播放**：
   - 手机/电脑打开蓝牙，搜索并连接设备名称：`ESP32-Lyrics-Speaker`。
   - 打开 YouTube Music 或 Apple Music 播放歌曲，声音从音箱喇叭播出，屏幕自动显示当前歌词与进度条。
2. **连接 Apple AirPlay 投屏**：
   - 确保 iPhone / iPad / Mac 与音箱处于同一个 Wi-Fi 局域网。
   - 在苹果控制中心点击音频投送图标，选择 `ESP32-Lyrics-Speaker`，直接投送高清音频。
