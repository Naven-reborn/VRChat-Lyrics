#pragma once
#include "lrc_parser.h"
#include <string>
#include <vector>

namespace lyrics {

// 通过 LRCLib.net 公共 API 查歌词。匹配维度:title + artist + album + duration_sec。
// duration_sec 传 0 表示不参与匹配(LRCLib 仍能用 ±2s 容差找到)。
// 返回空 vector 表示:网络失败 / 没找到 / 只有纯文本歌词没时间戳。
// include_translation 在 LRCLib 这边不生效 —— LRCLib 不提供翻译版本。
std::vector<LrcLine> FetchLrclibLyrics(const std::string& title,
                                       const std::string& artist,
                                       const std::string& album,
                                       int duration_sec);

}
