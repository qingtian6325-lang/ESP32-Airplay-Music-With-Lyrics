# ESP32 AirPlay & Bluetooth 实时滚动歌词桌面音箱

基于 **ESP32-WROVER-E (8MB PSRAM)** 的开源智能音频桌面摆件。支持通过 **苹果 AirPlay 1 (RAOP)** 及 **手机/电脑经典蓝牙 (A2DP)** 无线播放音乐（完美兼容 Apple Music 与 YouTube Music），并外接一块彩色 TFT 屏幕，实时通过 **[lrc.cx (LrcAPI)](https://docs.lrc.cx/docs/QuickStart/)** 官方公开服务与 **LRCLIB** 双源引擎拉取歌词并同步高亮滚动显示。

---

## ✨ 核心特性

- **双无线音频输入 (双模免切换)**：
  - **Apple AirPlay 1 (RAOP)**：支持 iPhone、iPad、Mac 在控制中心一键投屏，44.1kHz 16-bit 无损/CD 级传输。
  - **经典蓝牙 A2DP Sink**：通用手机、电脑蓝牙配对连接，支持 AVRCP 播放控制与歌曲元数据回传。
  - **智能音频仲裁**：AirPlay 优先抢占，AirPlay 开始播放时自动暂停蓝牙，停止后自动恢复。
- **高可用双源自动级联歌词引擎**：
  - **首选源 (Primary)**：官方公开 **[lrc.cx (LrcAPI)](https://docs.lrc.cx/docs/QuickStart/)** 接口（`https://api.lrc.cx/lyrics`），聚合 QQ 音乐、网易云、酷狗及 Apple Music，华语/流行/动漫曲库匹配率极高。
  - **备选兜底 (Fallback)**：若 `lrc.cx` 未命中或网络波动，Core 1 自动无缝切换至 **LRCLIB**（`https://lrclib.net`）进行二次检索。
  - **原生极速解析**：`lrc.cx` 纯文本 LRC 直接交由原生 `lrc_parse()` 处理，零 JSON 反序列化开销，毫秒级二分查找索引高亮行。
- **I2S 高保真数字输出**：
  - 通过 I2S 协议直推 **MAX98357A** D 类功放模块，免去模拟 DAC 损耗，驱动 4Ω 3W~5W 扬声器。
  - 支持 FreeRTOS 互斥保护的 DMA 双缓冲推流与软件平滑音量控制。
- **实时同步滚动歌词 (Layout A 布局)**：
  - 屏幕呈现 **5 行平滑滚动歌词**，当前演唱行以青蓝色 (`#00D2FF`) 加粗高亮放大显示。
  - 顶部显示音源图标（AirPlay / 蓝牙 / Wi-Fi 状态），底部显示已播时间 / 总时长进度条。
  - 无网络或纯音乐自动优雅回退为大字号歌曲信息与状态界面。
- **FreeRTOS 双核高效调度**：
  - **Core 0 (`CORE_AUDIO`)**：专职负责 Wi-Fi、蓝牙协议栈、AirPlay RTSP/RTP 服务及 I2S DMA 音频推流，杜绝卡顿爆音。
  - **Core 1 (`CORE_UI`)**：负责 LVGL 8.x 界面渲染、网络歌词拉取及 10Hz 播放进度与歌词对齐计时器。
  - **8MB 片外 PSRAM 深度优化**：动态将歌词缓存与 LVGL 帧缓冲挂载至 PSRAM，保障内部内存充沛。

---

## 🛠️ 硬件物料清单 (BOM)

| 部件 | 推荐型号 / 规格 | 说明 |
| :--- | :--- | :--- |
| **主控板** | **ESP32-WROVER-E (CH340)** | 必须带 **8MB PSRAM**、16MB Flash、板载天线、出厂已焊接好排针 |
| **音频功放** | **MAX98357A I2S 功放模块** | 单声道 3W，支持 5V 供电，I2S 数字接口直连 |
| **显示屏** | **2.8 寸或 3.2 寸 SPI 彩色 TFT 屏** | ST7789 或 ILI9341 驱动芯片，分辨率 320×240 |
| **扬声器** | **4Ω 3W ~ 5W 全频微型小喇叭** | 建议带小音腔/共振盒，低音更饱满 |
| **配件** | 面包板 1 块、杜邦线若干 (公对母/母对母)、Type-C 数据线 (5V 2A 供电) | 用于免焊接连线组装 |

---

## 🔌 硬件接线引脚图 (Wiring Pinout)

> [!WARNING]
> **绝对避坑原则**：ESP32-WROVER-E 模组内部已将 **GPIO 16** 和 **GPIO 17** 用于连接外部 PSRAM 芯片，**严禁外部引线连接 GPIO 16 和 17**！本项目已在编译期和代码中完全避开这两个引脚。

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

> 功放模块上的 **+** 和 **-** 接线端子直接用红黑双绞线接到 4Ω 3W 喇叭的两个焊盘上即可。

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
| **BLK** | **GPIO 21** | 屏幕背光控制引脚 (高电平点亮) |

---

## 🚀 从零制作教学：一步一步教你做出来

想要亲手做出这个桌面歌词音箱，跟着下面这 4 个阶段操作即可：

### 第一阶段：连接硬件电路线路
1. **插上面包板**：将 ESP32-WROVER-E、MAX98357A 功放板插在面包板上。
2. **连接喇叭**：用螺丝刀把喇叭的正负极两根线分别拧进 MAX98357A 的喇叭输出端子（`+` 和 `-`）。
3. **连接功放与 ESP32**：对照上面的《接线表 1》，将 MAX98357A 的 VIN 接 5V，GND 接 GND，BCLK 接 GPIO 25，LRC 接 GPIO 26，DIN 接 GPIO 27。
4. **连接屏幕与 ESP32**：对照上面的《接线表 2》，用杜邦线将屏幕的 8 根引脚分别连接到 ESP32 对应的引脚上。
5. **再次检查**：确认 GPIO 16 和 17 上没有任何导线。

---

### 第二阶段：电脑软件环境安装
1. **下载安装 VS Code**：前往 [Visual Studio Code 官网](https://code.visualstudio.com/) 下载安装。
2. **安装 PlatformIO 插件**：
   - 打开 VS Code，点击左侧菜单栏的扩展图标（Extensions）。
   - 搜索 `PlatformIO IDE`，点击 **Install** 安装。
3. **安装串口驱动**：
   - 我们的板子采用 CH340 USB 芯片，若电脑插上后无法识别串口，前往沁恒官网下载并安装 [CH341SER 驱动](https://www.wch.cn/downloads/CH341SER_EXE.html)。

---

### 第三阶段：克隆代码、配置 Wi-Fi 与一键烧录
1. **获取代码**：
   ```bash
   git clone https://github.com/qingtian6325-lang/ESP32-Airplay-Music-With-Lyrics.git
   cd ESP32-Airplay-Music-With-Lyrics
   ```
2. **在 VS Code 中打开此项目**：
   - 点击 `File` -> `Open Folder`，选择项目文件夹。
3. **配置你的家用 Wi-Fi**：
   - 打开 `include/config.h` 文件。
   - 找到并修改第 53-56 行的 Wi-Fi 名称和密码（注意：ESP32 仅支持 2.4GHz 频段 Wi-Fi）：
     ```c
     #define WIFI_SSID           "你家的2.4G_WiFi名称"
     #define WIFI_PASSWORD       "你家的WiFi密码"
     ```
   - *（歌词源已默认配置为官方公开 API `https://api.lrc.cx/lyrics`，并开启双源回退，无需额外修改）*
4. **连接 ESP32 开发板并一键烧录**：
   - 用 Type-C 数据线将 ESP32 插入电脑 USB 口。
   - 在 VS Code 底部状态栏，点击 **PlatformIO: Upload** 按钮（右箭头 `→` 图标），或者在终端输入：
     ```bash
     pio run -e esp32dev --target upload
     ```
   - PlatformIO 会自动编译所有组件并烧录进 ESP32。

---

### 第四阶段：通电开机与日常听歌体验

1. **开机自检**：
   - 烧录完成后 ESP32 自动重启。屏幕点亮并显示音响界面，右上角 Wi-Fi 图标连接成功后亮起。
2. **连接经典蓝牙播放音乐（Android / iPhone / PC）**：
   - 手机打开系统设置 -> 蓝牙，搜索名为 **`ESP32-Lyrics-Speaker`** 的设备并点击配对。
   - 打开网易云音乐、QQ 音乐、Spotify 或 YouTube Music 播放歌曲。
   - 音乐通过 MAX98357A 喇叭放出，屏幕右上角显示蓝牙图标，中下部即刻从 `lrc.cx` 匹配并高亮滚动当前歌词！
3. **使用苹果 AirPlay 播放（iPhone / iPad / Mac）**：
   - 确保 iPhone 与音箱连接在同一个家用 Wi-Fi 下。
   - 在 iPhone 右上角滑下**控制中心**，点击右上角音频卡片中的 **AirPlay 隔空播放图标**。
   - 播放设备列表中将出现 **`ESP32-Lyrics-Speaker`**，点击投送。
   - 屏幕左上角显示 AirPlay 徽标，自动进入 44.1kHz CD 级无损推流，歌词同步滚动呈现！

---

## 🧪 自动化测试验证 (CI / Native Unit Tests)

本项目内置完整的本地 Native 单元测试套件，在不连接硬件的情况下即可完成全算法与状态机验证：

```bash
# 运行全套 114 项单元测试
pio test -e native
```

测试覆盖范围：
- `test_config`：PSRAM GPIO 16/17 禁区检查、外设引脚映射与歌词多源配置校验。
- `test_audio`：I2S 初始化、软件音量衰减算法与缓冲区防溢出保护。
- `test_bt_metadata`：蓝牙 AVRCP 属性防抖、时长延迟到达与多参数通知分发。
- `test_airplay`：AirPlay DAAP（`minm`/`asar`/`asal`）解包器、异常断连重置。
- `test_lrc`：`lrc.cx` 官方纯文本 LRC 解析、UTF-8 中文与毫秒时间戳匹配、双源级联回退状态机。
- `test_ui`：时间格式化、文本省略截断、5 行滚动歌词窗口滑动算法。
- `test_integration`：双核音频仲裁状态机（AirPlay 抢占蓝牙、空闲恢复）。

---

## ❓ 常见问题排查 (FAQ)

1. **Q: 屏幕白屏或不亮？**
   - 检查 `BLK`（背光引脚）是否接到了 `GPIO 21`。
   - 检查 3.3V 与 GND 接线是否牢固。
2. **Q: 喇叭有杂音或没有声音？**
   - MAX98357A 的 VIN 引脚务必接 **5V**，不要接 3.3V。
   - 检查 GPIO 25 (BCLK)、GPIO 26 (LRC)、GPIO 27 (DIN) 是否接反。
3. **Q: 屏幕显示“未收录歌词”？**
   - 检查 ESP32 是否成功连上家用 Wi-Fi（路由器需开启 2.4GHz 频段）。
   - 若歌曲为极冷门伴奏或纯音乐，系统会自动显示歌曲基本信息而无需显示歌词。
4. **Q: 我可以换成自建的 LrcAPI 服务器吗？**
   - 可以！若您在 NAS 或 Docker 部署了 `hisatri/lrcapi`，只需在 `include/config.h` 中将 `LRC_CX_API_ENDPOINT` 改为您局域网的地址（如 `http://192.168.1.100:28883/lyrics`）即可。

---

## 📄 开源许可证

本项目基于 [MIT License](LICENSE) 开源。
