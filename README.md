<div align="center">

<img src="assets/VL.png" width="88" alt="VRChat Lyrics">

# VRChat Lyrics

把正在听的歌、歌词和状态同步到 VRChat。

**Windows · C++20 · ImGui · 简体中文 / 繁體中文 / English**

[下载与发布记录](https://github.com/Naven-reborn/VRChat-Lyrics/releases) · [反馈问题](https://github.com/Naven-reborn/VRChat-Lyrics/issues)

</div>

## 功能

| 功能 | 内容 |
| --- | --- |
| 歌词同步 | 识别网易云、Spotify、YouTube Music；支持翻译和播放进度 |
| Chatbox | OSC 发送、暂停时继续发送、实时预览、可视化格式编辑 |
| 音频中继 | 捕获网易云音频，通过 VB-Cable 接入 VRChat 麦克风 |
| Bilibili | BV / AV / 分享链接解析，分集、清晰度、主备线路选择 |
| 直播解析 | Bilibili 直播间链接，HLS / FLV 地址及备用线路 |
| 状态中心 | 自定义状态、AFK 自动检测、前台应用展示 |
| 外观与动效 | 深色、浅色、毛玻璃主题；歌词换行、按钮回弹、旋转封面、RGB 故障标题 |
| 后台运行 | 系统托盘、关闭窗口后继续运行 |

## v3.4

本次更新重做了歌词主页和界面动效，也重新整理了 Bilibili 视频与直播解析流程。

- 大字聚焦当前歌词，换行时平滑上移、淡入淡出；下方直接预览 Chatbox 内容。
- 按钮按下轻微缩小，松开柔和回弹；切页与主题切换使用统一过渡。
- 标题放大居中，加入随机 RGB 故障与恢复动画：每 3 秒一轮，1 秒效果、2 秒正常。
- 毛玻璃下拉框使用独立配色；分集、画质、线路统一复用自定义选择控件。
- 支持视频分集列表、清晰度切换、主备线路，以及 Bilibili 直播间解析。

历史更新见 [GitHub Releases](https://github.com/Naven-reborn/VRChat-Lyrics/releases)。

## 开始使用

1. 从 [Releases](https://github.com/Naven-reborn/VRChat-Lyrics/releases) 下载程序，或自行编译 `vrc-lyrics-3.4.exe`。
2. 在 VRChat 中开启 **Action Menu → Options → OSC → Enabled**。
3. 打开网易云、Spotify 或 YouTube Music 播放歌曲。
4. 启动程序，确认曲目已识别，点击 **开始发送歌词**。

网易云推荐安装 [BetterNCM](https://github.com/std-microblock/BetterNCM-Installer) 和 [inflink-rs](https://github.com/apoint123/inflink-rs)，通过曲目 ID 匹配歌词。Spotify 和 YouTube Music 使用 LRCLib 匹配。

### 调整发送格式

歌词页右下角点击 **调整格式**，直接进入设置中的格式构建器。可调整字段顺序、显示项目、分隔符以及单行 / 两行布局。

### 音频中继

1. 进入 **音频** 页，安装 VB-Cable。
2. 输出设备选择 **CABLE Input**，点击 **启动中继**。
3. VRChat 麦克风选择 **CABLE Output**。
4. 在 VRChat 的 **Audio & Voice** 中，将 **Voice Processing** 设置为 **None**。

### 视频和直播

1. 在 **视频** 页粘贴 BV / AV 号、Bilibili 视频链接、b23.tv 分享文本或直播间链接。
2. 点击 **解析 / 刷新地址**，加载分集和清晰度选项。
3. 选择分集、画质后再次解析，再选择主线路或备用线路。
4. 点击 **复制播放地址**，粘贴到房间视频播放器。

VRChat 中需开启 **Allow Untrusted URLs**。地址失效时重新解析；结果区会显示实际画质、直播状态和解析错误。

### 主题与动画

在 **设置 → 外观** 中切换深色、浅色和毛玻璃主题，调整毛玻璃不透明度与界面动画。歌词动画可在歌词设置中单独开关。

## 自己编译

准备 **Visual Studio 2022 C++ Build Tools / Visual Studio 2022** 和 **Windows 10/11 SDK**。

```powershell
git clone https://github.com/Naven-reborn/VRChat-Lyrics.git
cd VRChat-Lyrics

git clone --branch docking https://github.com/ocornut/imgui.git deps/imgui
git -C deps/imgui checkout 5a76f2adf1b0403b86a45010121fb32a6bff8680

./build_release.ps1
```

输出：`out/Release/vrc-lyrics-3.4.exe`。也可打开 `vrc-lyrics.sln` 构建。

### 开发验证

```powershell
./build_ui_test.ps1
cd out/qa-build-3.4
./vrc-lyrics-3.4-qa.exe --ui-capture
./vrc-lyrics-3.4-qa.exe --bili-network-check
```

界面检查结果保存在 `captures/`；联网检查结果保存在 `bilibili-network-checks.txt`。

## 配置与结构

正式配置保存在 `%APPDATA%/vrc-lyrics/config.json`。界面测试使用独立配置目录。

```text
src/
├── menu/       界面、主题、动效与格式构建器
├── playback/   播放器识别与进度同步
├── lyrics/     网易云 / LRCLib 歌词服务
├── osc/        VRChat Chatbox 通信
├── audio/      音频捕获、中继与设备管理
├── bilibili/   视频、分集、画质与直播解析
├── host/       Win32 窗口、D3D11 与托盘
├── config/     配置读写
└── net/        网络请求
```

## 鸣谢

- [BigAtomikku/VRC-Lyrics](https://github.com/BigAtomikku/VRC-Lyrics)
- [BetterNCM](https://github.com/std-microblock/BetterNCM-Installer) / [inflink-rs](https://github.com/apoint123/inflink-rs)
- [LRCLib](https://lrclib.net/) / [VB-Audio](https://vb-audio.com/Cable/)
- [bilibili_parse_vrchat](https://github.com/wangure0329/bilibili_parse_vrchat) / [yt-dlp](https://github.com/yt-dlp/yt-dlp)
- [Dear ImGui](https://github.com/ocornut/imgui) / [nlohmann/json](https://github.com/nlohmann/json)

## License

代码使用 [MIT License](LICENSE)。MuseoSans 字体不在 MIT 许可范围内。
