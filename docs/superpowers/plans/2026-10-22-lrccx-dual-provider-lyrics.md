# ESP32 歌词音箱：lrc.cx 双源级联歌词获取实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 ESP32-WROVER-E 桌面歌词音箱接入 lrc.cx 官方公开歌词 API（`https://api.lrc.cx/lyrics`），并与原有 LRCLIB 构成高可用双源自动级联回退引擎。

**Architecture:** 
切歌时，Core 1（`CORE_UI`）歌词工作线程优先向 `https://api.lrc.cx/lyrics` 发起 HTTP 请求，直接获取纯文本 LRC 格式并由 `lrc_parse()` 原生解析；若返回 404 或解析失败，且启用了回退机制，Core 1 自动向 `https://lrclib.net/api/get` 发起二次检索。全程异步执行，严禁占用栈空间，网络单次超时控制在 4000ms，绝不影响 Core 0 音频 DMA。

**Tech Stack:** ESP-IDF / Arduino ESP32, FreeRTOS, HTTPClient, WiFiClientSecure, ArduinoJson, Unity Test Framework.

**Spec:** [docs/superpowers/specs/2026-10-22-lrccx-dual-provider-lyrics-design.md](file:///c:/Users/hp/Documents/antigravity/vibrant-hubble/docs/superpowers/specs/2026-10-22-lrccx-dual-provider-lyrics-design.md)

## Global Constraints

- MCU 硬件规格：ESP32-WROVER-E（16MB Flash, 8MB PSRAM）。
- 引脚绝对红线：严禁占用 GPIO 16 与 GPIO 17（内部 PSRAM 总线）。
- 双核任务绑定：所有歌词 HTTP 请求与回退必须在 Core 1（`CORE_UI`）执行，严禁在 Core 0 运行。
- 内存分配约束：`LyricsData`（~33KB）严禁在任务局部调用栈上分配，必须使用静态 BSS 或 PSRAM 堆空间。
- 网络超时控制：每个提供商单次 HTTP 请求超时不得超过 4000ms。

## Review Focus

1. `lrc.cx` 返回中文 UTF-8 歌词时的解析与特殊字符 URL 编码安全性。
2. `lrc.cx` 返回 404 Not Found 时，系统能够毫秒级平滑转入 LRCLIB 请求而不断连或泄漏套接字。
3. 双源皆未命中或断网时，优雅触发 `success=false` 回调，UI 显示“未收录歌词”而不死锁。
4. `config.h` 中开关 `LYRICS_ENABLE_FALLBACK = 0` 时，关闭自动回退行为，只访问首选源。
5. 每次 HTTPClient 请求结束必须显式调用 `http.end()`，避免多次切歌导致 ESP32 套接字耗尽。

---

### Task 1: 歌词多源配置与宏定义扩展 (Configuration & Interface Expansion)

**Files:**
- Modify: `include/config.h`
- Modify: `include/lyrics_service.h`
- Test: `test/test_config/test_config.cpp`

- [ ] **Step 1: 在 `test/test_config/test_config.cpp` 中编写配置宏测试用例**
  添加对 `LYRICS_PROVIDER_LRCCX`、`LYRICS_PROVIDER_LRCLIB`、`LYRICS_DEFAULT_PROVIDER`、`LYRICS_ENABLE_FALLBACK`、`LRC_CX_API_ENDPOINT`、`LRCLIB_API_ENDPOINT` 的宏存在性与有效性测试。
- [ ] **Step 2: 运行测试确认其失败（Red）**
  运行：`& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" test -e native -f test_config`
- [ ] **Step 3: 更新 `include/config.h` 与 `include/lyrics_service.h`**
  - 在 `include/config.h` 中定义提供商常量、默认提供商（`LYRICS_PROVIDER_LRCCX`）、自动回退开关（`1`）与 API URL 端点。
  - 在 `include/lyrics_service.h` 中声明多源 Mock 注入接口（`lyrics_mock_set_provider_response`），便于 Native Host 测试双源回退逻辑。
- [ ] **Step 4: 运行测试确认通过（Green）**
  再次运行：`& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" test -e native -f test_config`
- [ ] **Step 5: Git 提交**
  提交信息：`feat(config): add lrc.cx and dual-provider lyrics configuration`

---

### Task 2: 双源级联歌词获取引擎与原生 LRC 解析实现 (Dual-Provider Cascading Engine)

**Files:**
- Modify: `src/lyrics_service.cpp`
- Test: `test/test_lrc/test_lrc.cpp`

- [ ] **Step 1: 在 `test/test_lrc/test_lrc.cpp` 编写真实 `lrc.cx` 响应与级联回退测试用例**
  - 用例 1：`lrc.cx` 返回标准 LRC 纯文本（含毫秒时间戳 `[00:26.510]`、`[ti:]`、`[ar:]` 与中文歌词）能被准确解析。
  - 用例 2：首选 `lrc.cx` 返回成功时，不触发 LRCLIB 回退，直接回调 `success=true`。
  - 用例 3：首选 `lrc.cx` 模拟失败（404 或空），次级 LRCLIB 成功时，系统自动切换并回调 `success=true`。
  - 用例 4：两源皆失败时，正确回调 `success=false`，结构体中保留原请求的 title 与 artist。
  - 用例 5：禁用 Fallback 时，首选失败立即终止，不再请求次级源。
- [ ] **Step 2: 运行测试确认新增用例失败（Red）**
  运行：`& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" test -e native -f test_lrc`
- [ ] **Step 3: 在 `src/lyrics_service.cpp` 中实现双源逻辑与 Mock 分发**
  - 实现 `fetch_from_lrccx()`：针对 `https://api.lrc.cx/lyrics?title=...&artist=...` 请求，设置超时 4000ms，状态码 200 且返回非空内容时，直接通过 `lrc_parse()` 进行纯文本解析。
  - 实现 `fetch_from_lrclib()`：保持对 `https://lrclib.net/api/get?...` 的 JSON 反序列化获取。
  - 在 Core 1 的 `lyrics_worker_task` 中实现级联状态机：先查 Preferred Provider，若未成功且 `LYRICS_ENABLE_FALLBACK` 为真，则切换并查询 Secondary Provider。
  - 在 Native Mock 模式中实现多源响应模拟支持。
- [ ] **Step 4: 运行测试确认全量 LRC 测试通过（Green）**
  运行：`& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" test -e native -f test_lrc`
- [ ] **Step 5: Git 提交**
  提交信息：`feat(lyrics): implement lrc.cx integration and dual-provider cascading fallback`

---

### Task 3: 固件全量回归测试与文档同步 (Regression Testing & Documentation)

**Files:**
- Modify: `README.md`
- Verify: 全套原生测试与 ESP32 交叉编译

- [ ] **Step 1: 运行全量原生单元测试**
  运行：`& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" test -e native`
  确保全部 7 个测试套件（115+ 测试用例）100% 绿色通过。
- [ ] **Step 2: 执行 ESP32-WROVER-E 固件编译**
  运行：`& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e esp32dev`
  确保生成 `firmware.bin`，零编译警告与内存溢出。
- [ ] **Step 3: 更新 `README.md` 文档**
  在 README 中详细说明：
  - 歌词双源机制说明（`lrc.cx` 首选 + `LRCLIB` 回退）。
  - 华语与流行曲库支持特性。
  - `config.h` 中如何自定义首选服务或配置私有化自建 LrcAPI 服务的地址。
- [ ] **Step 4: Git 提交与同步远程仓库**
  - 提交：`docs: update README with lrc.cx dual-provider lyrics documentation`
  - 推送至 GitHub：`git push origin master` 与 `git push origin main`
