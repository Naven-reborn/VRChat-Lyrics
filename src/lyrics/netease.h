#pragma once
#include "lrc_parser.h"
#include <string>
#include <vector>

namespace lyrics {

// Fetches and parses lyrics for a netease cloud song.
// Returns empty vector on any failure (network, JSON, instrumental track).
// If include_translation is true, lines from tlyric are merged below each
// matching timestamp.
std::vector<LrcLine> FetchNeteaseLyrics(const std::string& ncm_id,
                                        bool include_translation);

}
