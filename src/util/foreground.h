#pragma once
#include <cstdint>
#include <string>

namespace util {

enum class AppCategory {
    Unknown = 0,
    Game    = 1,
    Browser = 2,
    Chat    = 3,
    Dev     = 4,
    Music   = 5,
    Office  = 6,
    Stream  = 7,
};

struct ForegroundInfo {
    std::string  name;                          // 短别名,如 "VRChat" / "VSCode"
    AppCategory  category = AppCategory::Unknown;
};

// 返回当前前台应用的友好名 + 分类。如果是我们自己或检测失败,name 为空。
ForegroundInfo ForegroundApp();

// 兼容旧接口:只返回名字。新代码用 ForegroundApp()。
std::string ForegroundAppName();

// 默认 emoji,UI 没自定义时用。返回 UTF-8 编码的 emoji 字面串。
const char* DefaultCategoryEmoji(AppCategory cat);

// 自上次键盘/鼠标输入以来的秒数。GetLastInputInfo 包装。
// 注意:这是系统级空闲(整个 Windows session 没动),不是单个进程空闲。
uint32_t IdleSeconds();

}
