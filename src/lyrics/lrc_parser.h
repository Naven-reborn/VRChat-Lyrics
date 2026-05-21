#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace lyrics {

struct LrcLine {
    int64_t     ms;
    std::string text;
};

// Parses LRC body (one or more "[mm:ss.xx]text" lines). Metadata-only lines
// like "[ti:Title]" / "[ar:Artist]" / "[al:...]" / "[by:...]" are skipped.
// Lines with multiple timestamps "[00:01.23][00:30.45]text" expand to two
// entries. Empty-text lines are kept as breath gaps (matches the Python
// reference). Result is sorted ascending by ms.
std::vector<LrcLine> ParseLrc(const std::string& body);

// Binary search: returns index of the last line with ms <= now_ms, or -1.
int FindCurrentLine(const std::vector<LrcLine>& lines, int64_t now_ms);

}
