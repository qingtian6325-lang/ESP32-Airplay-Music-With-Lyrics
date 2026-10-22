# ESP32 歌词音箱：集成 lrc.cx 官方 API 双源级联设计规范

**文档版本**：v1.0  
**日期**：2026-10-22  
**作者**：Antigravity  
**状态**：待用户评审（Pending Approval）  

---

## 1. 背景与目标 (Background & Goals)

在当前的固件设计中，歌词获取服务通过 Core 1（`CORE_UI`）向 `https://lrclib.net` 发起异步 HTTPS 请求。虽然 LRCLIB 能够较好地支持部分国际歌曲，但在华语流行、粤语、日语及动漫等曲库覆盖率相对有限。

用户指定引入 **[lrc.cx (LrcAPI)](https://docs.lrc.cx/docs/QuickStart/)** 官方公开服务（`https://api.lrc.cx/lyrics`），并选择**方案 A（双源级联回退）**：
1. **优先查询 lrc.cx**：使用轻量级直接纯文本 LRC 获取接口，命中率极高且响应极快。
2. **LRCLIB 自动回退**：若 `lrc.cx` 返回 404（未收录）、非 200 或解析失败，Core 1 自动发起第二路对 LRCLIB 的回退请求。
3. **严格遵守双核实时性**：歌词请求与网络重试严格限定在 Core 1（`CORE_UI`），网络超时设为 4000ms，绝不阻塞 Core 0 音频 DMA 与 FreeRTOS 任务队列。

---

## 2. API 规范与对比 (API Specifications)

| 特性 | lrc.cx 官方 API (Primary) | LRCLIB (Secondary Fallback) |
| :--- | :--- | :--- |
| **请求 URL** | `https://api.lrc.cx/lyrics?title={title}&artist={artist}` | `https://lrclib.net/api/get?track_name={title}&artist_name={artist}` |
| **HTTP 方法** | `GET` | `GET` |
| **参数格式** | RFC 3986 URL 编码 UTF-8 | RFC 3986 URL 编码 UTF-8 |
| **响应格式** | 纯文本标准 LRC（`text/plain` 或 `text/html`） | JSON（`application/json`） |
| **内容处理** | 无需 JSON 解析，直接传给 `lrc_parse()` | 提取 `doc["syncedLyrics"]` 后传给 `lrc_parse()` |
| **失败标识** | HTTP 404 Not Found 或空内容 | HTTP 404 Not Found 或 `syncedLyrics` 为 null |
| **单次超时** | 4000ms | 4000ms |

---

## 3. 架构设计与状态机流程 (Architecture & Flow)

### 3.1 歌词请求状态机流程图

```mermaid
flowchart TD
    A[切歌事件: title & artist] --> B[放入 Core 1 歌词工作队列]
    B --> C[Core 1: 构建 lrc.cx 请求]
    C --> D[GET https://api.lrc.cx/lyrics]
    D --> E{状态码 200 且非空?}
    E -- 是 --> F[调用 lrc_parse 解析纯文本 LRC]
    F --> G{解析出有效时间戳?}
    G -- 是 --> H[触发 on_lyrics_loaded(success=true)]
    G -- 否 (解析失败) --> I{启用 Fallback?}
    E -- 否 (404/超时/网络异常) --> I
    I -- 是 (默认启用) --> J[Core 1: 构建 LRCLIB 请求]
    J --> K[GET https://lrclib.net/api/get]
    K --> L{状态码 200 且含 syncedLyrics?}
    L -- 是 --> M[调用 lrc_parse 解析 JSON 中的歌词]
    M --> N{解析出有效时间戳?}
    N -- 是 --> H
    N -- 否 --> O[触发 on_lyrics_loaded(success=false)]
    L -- 否 --> O
    I -- 否 --> O
    O --> P[UI 显示回退文本: 未找到歌词]
```

### 3.2 内存与任务安全保障
1. **零调用栈内存分配**：`LyricsData` 实例依然驻留在静态 BSS 或 8MB PSRAM 堆中，严禁在栈上分配 33KB 的结构体。
2. **HTTP 连接复用与安全释放**：每次 HTTP 请求结束立即调用 `http.end()`，确保 ESP32 的 WiFi 客户端套接字及时归还，防止内存泄漏。
3. **URL 编码增强**：`url_encode()` 确保华语歌曲中包含的特殊字符（如空格、括号、`&`、`feat.`、多语言字符）均被标准百分号转义。

---

## 4. 接口与配置变更 (Interface & Config Changes)

### 4.1 `include/config.h`
新增提供商常量与端点配置：
```c
// 歌词服务提供商常量
#define LYRICS_PROVIDER_LRCCX       1
#define LYRICS_PROVIDER_LRCLIB      2

// 默认首选提供商及级联回退开关
#define LYRICS_DEFAULT_PROVIDER     LYRICS_PROVIDER_LRCCX
#define LYRICS_ENABLE_FALLBACK      1   // 1: 启用自动回退; 0: 仅使用首选

// 官方端点定义
#define LRC_CX_API_ENDPOINT        "https://api.lrc.cx/lyrics"
#define LRCLIB_API_ENDPOINT        "https://lrclib.net/api/get"
```

### 4.2 `include/lyrics_service.h`
保持既有对外接口向后兼容：
* `lyrics_service_init()`
* `lyrics_service_stop()`
* `lyrics_fetch_async(const char *title, const char *artist, LyricsLoadedCallback cb)`
* 新增提供商查询与单测模拟辅助函数（用于 Host 原生单元测试覆盖回退逻辑）：
  * `lyrics_set_mock_provider_result(int provider, const char *lrc, bool success)`

---

## 5. 测试与验证策略 (Testing & Verification)

1. **Host Native 单元测试 (`test/test_lrc/test_lrc.cpp`)**：
   * 测试 `lrc.cx` 返回的真实 LRC 格式（包含 UTF-8 中文、毫秒时间戳 `[00:26.510]`、`[ti:]`、`[ar:]`）。
   * 测试 `lrc.cx` 成功获取时的单步回调路径。
   * 测试 `lrc.cx` 返回 404/失败时，自动回退至 LRCLIB 并成功的级联路径。
   * 测试两源均失败时的优雅降级（触发失败回调，保留元数据，不导致崩溃）。
   * 测试关闭 `LYRICS_ENABLE_FALLBACK` 时的单一源行为。
2. **嵌入式固件编译 (`pio run -e esp32dev`)**：
   * 确保固件正常编译，Flash 与 PSRAM 占用正常。
3. **全套回归测试 (`pio test -e native`)**：
   * 确保所有原有的 109 项测试（音频、蓝牙、AirPlay、UI、状态机）和新增测试全部通过。
