<div align="center">

# VRChat Lyrics

**把正在听的歌推到 VRChat chatbox · 可选音频中继 · Bilibili 视频直链**

C++ + ImGui · Windows 10/11 · 中 / 英 / 繁 · 暗亮主题

[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Windows%2010%2F11-0078D6.svg)]()
[![Language](https://img.shields.io/badge/C%2B%2B-20-00599C.svg)]()
[![Release](https://img.shields.io/badge/Release-v3.3-brightgreen.svg)](../../releases/tag/v3.3)

</div>

---

## ✨ 能做什么

| | 功能 | 说明 |
|---|---|---|
| 🎵 | **多源 Now Playing** | 网易云 / Spotify / YouTube Music（含浏览器网页端） |
| 📝 | **同步歌词** | 网易云直链 + LRCLib 兜底；暂停时也能保留当前歌词 |
| 🎤 | **VRChat chatbox** | OSC 推送，可限速，相同文案 keep-alive 防气泡消失 |
| 🧩 | **格式构建器** | 勾选字段、调顺序、单行/两行布局，不用手敲模板 |
| 🔊 | **音频中继** | 只抓网易云 → VB-Cable → VRChat 麦克风 |
| 🎬 | **Bilibili 解析** | BV / 链接 / b23 短链 → `.mp4` 直链；支持 `?p=N` 多 P |
| 🪪 | **状态中心** | 自定义状态 · AFK · 前台应用分类 emoji |
| 🖥 | **系统托盘** | 可关「关闭时最小化到托盘」 |

当前版本：**v3.3**

---

## 📸 截图

> 上方是 VRChat 里别人能看到的 chatbox，下方是本程序界面。

<div align="center">
  <img src="assets/screenshot-lyrics.png" width="80%" alt="歌词页" />
  <br><br>
  <img src="assets/screenshot-activity.png" width="80%" alt="应用 / 状态中心" />
  <br><br>
  <img src="assets/screenshot-audio.png" width="80%" alt="音频中继" />
</div>

---

## 🚀 快速开始

### 前置

1. **Windows 10 / 11**
2. **网易云 + BetterNCM**（[安装教程](https://github.com/std-microblock/BetterNCM-Installer)）
3. **inflink-rs**（BetterNCM 插件商店，或 [Releases](https://github.com/apoint123/inflink-rs/releases)）—— 没有它就拿不到 NCM-ID，网易云也会走 LRCLib 模糊匹配
4. **VRChat 打开 OSC**：Action Menu → Options → OSC → Enabled

### 跑起来

1. 从 [Releases](../../releases) 下载 [`vrc-lyrics-3.3.exe`](../../releases/tag/v3.3)
2. 打开网易云（或 Spotify / YouTube Music）随便播一首
3. 双击 exe，确认曲目和封面已识别
4. 点 **Start**，chatbox 出现 `▶ 歌名 - 艺人` 和当前歌词
5. 点窗口 X：默认藏到托盘继续跑；托盘右键 **Exit** 才真正退出  
   （设置里可关掉「关闭时最小化到托盘」）

### 可选：音频中继（朋友能听见你的歌）

1. **音频** 页 → 未装 VB-Cable 时点 **下载并安装**
2. 输出设备选 **CABLE Input** → **启动中继**
3. VRChat 麦克风改成 **CABLE Output**
4. **强烈建议**：VRChat → Settings → Audio & Voice → **Voice Processing = None**（默认降噪会把音乐高频削闷）
5. 请只在**私人 / 朋友房间**使用，公共房外放容易被举报

### 可选：Bilibili → 视频墙

1. **视频** 页粘贴 BV / 完整链接 / `b23.tv` 短链（多 P 带 `?p=6` 会解析对应分集）
2. **解析** → **复制链接** → 贴进房间视频播放器
3. 直链约 **2 小时**有效；VRChat 需开启 **Allow Untrusted URLs**

---

## 🆕 v3.3 更新摘要

- **进度 / 歌词**：SMTC 外推 + 单调钳位；YouTube Music **网页端粘滞外推**，避免卡在十几秒前
- **Bilibili**：`?p=N` / `page=N` 正确解析多 P
- **Chatbox**：keep-alive；暂停仍可保留歌词；格式改为可视化构建器
- **UI**：亮色主题层次、惯性滚动、端口输入框、下拉动画、分类 emoji 对齐、VL 应用图标、Win11 圆角
- **稳定性**：关闭窗口不再未响应

完整下载与说明见 [Release v3.3](../../releases/tag/v3.3)。

---

## 🔧 自己编译

**环境**：Visual Studio 2022 Build Tools（MSVC v143 + Windows 10 SDK）

```powershell
git clone https://github.com/Naven-reborn/VRChat-Lyrics.git
cd VRChat-Lyrics

git clone --branch docking --depth 1 https://github.com/ocornut/imgui.git deps/imgui

build.bat Release
```

产物：`out\Release\vrc-lyrics-3.3.exe`  
也可打开 `vrc-lyrics.sln` 用 VS 调试。

---

## 🧠 工作原理（简图）

**歌词**

```
播放器 ──SMTC──▶ SmtcWatcher ──切歌──▶ lyrics worker (网易云 / LRCLib)
                      │                        │
                      │                        ▼
                      │                 当前行 + 格式构建器
                      ▼                        │
                 圆形封面纹理                   ▼
                                      OSC /chatbox/input → VRChat
```

**音频中继**

```
cloudmusic.exe ──进程 loopback──▶ RingBuffer ──增益/软限幅──▶ CABLE Input
                                                              │
                                                     VRChat 麦克风 = CABLE Output
```

---

## 📂 结构（精简）

```
src/
├── main.cpp                 # 入口 + 主循环
├── host/                    # 窗口 / D3D11 / ImGui / 托盘 / 应用图标
├── menu/                    # UI、主题、格式构建器
├── playback/                # SMTC + 粘滞时间线
├── lyrics/                  # 网易云 / LRCLib / LRC 解析
├── osc/                     # chatbox UDP + keep-alive
├── audio/                   # 进程 loopback 中继 + VB-Cable 安装
├── bilibili/                # 视频解析（含多 P）
├── net/                     # WinHTTP
├── util/                    # 封面圆形纹理、前台应用
├── config/                  # %APPDATA%\vrc-lyrics\config.json
└── i18n/                    # 中英繁
```

---

## 🛠 技术栈

- C++20 · Win32 · D3D11 · Dear ImGui（docking）
- C++/WinRT SMTC · WinHTTP · nlohmann/json · WIC
- WASAPI 进程级 loopback（Win10 20348+）· VB-Cable · AvRT
- DPI Per-Monitor V2 · DWM 阴影 / Win11 圆角

---

## ⚙ 配置

路径：`%APPDATA%\vrc-lyrics\config.json`

| 字段 | 说明 |
|------|------|
| `language` / `theme` | 语言、暗亮主题 |
| `osc_host` / `osc_port` / `rate_limit_ms` | OSC 目标与限速（默认 127.0.0.1:9000 / 1300ms） |
| `lyrics_provider` | 0 仅网易云 · 1 网易云→LRCLib · 2 仅 LRCLib |
| `include_translation` / `send_while_paused` / `show_foreground_app` | 翻译、暂停仍发送、前台应用前缀 |
| `minimize_to_tray` | 关闭时是否藏托盘（默认 true） |
| `fmt_builder_lyrics` / `fmt_builder_no_lyrics` | 格式构建器（字段顺序、开关、布局、分隔符） |
| `fmt_lyrics` / `fmt_no_lyrics` / `fmt_paused` | 旧字符串模板（兼容迁移） |
| `audio_*` | 中继设备、增益、限幅、自动启动 |
| `afk_*` / `emoji_*` | AFK 与分类 emoji |

---

## ⚠ 已知限制

- VIP / 试听受限曲，网易云可能不回歌词  
- Spotify / YT Music 依赖 **LRCLib**，中文覆盖不如网易云；冷门歌可能无词或略偏  
- 浏览器 YT Music 标题需带 `YouTube Music` 标记  
- Bilibili 直链约 2h 有效；需 Allow Untrusted URLs；番剧 ss/ep 暂不支持  
- 音频中继：Win10 20348+ / Win11；请仅私人房使用；VRChat Opus 会再损一点音质  
- 仓库内 MuseoSans 为**商业字体**

---

## 🙏 鸣谢

- [BigAtomikku/VRC-Lyrics](https://github.com/BigAtomikku/VRC-Lyrics) — 功能蓝本  
- [apoint123/inflink-rs](https://github.com/apoint123/inflink-rs) — 网易云 SMTC 桥  
- [VB-Audio Software](https://vb-audio.com/Cable/) — 虚拟声卡  
- [wangure0329/bilibili_parse_vrchat](https://github.com/wangure0329/bilibili_parse_vrchat) · [bilibili-API-collect](https://github.com/SocialSisterYi/bilibili-API-collect)  
- [ocornut/imgui](https://github.com/ocornut/imgui) · [nlohmann/json](https://github.com/nlohmann/json)

---

## 📜 License

MIT — 见 [LICENSE](LICENSE)。

`src/menu/MuseoSans*.h` **不在** MIT 范围内，使用前请自行处理字体版权。
