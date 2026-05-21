#include "lrc_parser.h"
#include <algorithm>
#include <cctype>

namespace lyrics {

static bool IsMetaTag(const std::string& inside) {
    // "[ti:...]", "[ar:...]", "[al:...]", "[by:...]", "[offset:...]", "[length:...]"
    // i.e., not a timestamp. Timestamps start with a digit.
    if (inside.empty()) return true;
    return !std::isdigit((unsigned char)inside[0]);
}

static int64_t ParseTimestamp(const std::string& s) {
    // "mm:ss.xx" or "mm:ss.xxx" or "mm:ss"
    int mm = 0, ss = 0, frac_ms = 0;
    int n = (int)s.size();
    int i = 0;
    while (i < n && std::isdigit((unsigned char)s[i])) {
        mm = mm * 10 + (s[i] - '0');
        ++i;
    }
    if (i >= n || s[i] != ':') return -1;
    ++i;
    while (i < n && std::isdigit((unsigned char)s[i])) {
        ss = ss * 10 + (s[i] - '0');
        ++i;
    }
    if (i < n && (s[i] == '.' || s[i] == ':')) {
        ++i;
        int digits = 0;
        while (i < n && std::isdigit((unsigned char)s[i]) && digits < 3) {
            frac_ms = frac_ms * 10 + (s[i] - '0');
            ++i; ++digits;
        }
        if (digits == 2) frac_ms *= 10;       // .xx → xxx
        else if (digits == 1) frac_ms *= 100; // .x  → xxx
    }
    return (int64_t)mm * 60000 + (int64_t)ss * 1000 + frac_ms;
}

std::vector<LrcLine> ParseLrc(const std::string& body) {
    std::vector<LrcLine> out;
    if (body.empty()) return out;

    size_t pos = 0;
    while (pos < body.size()) {
        size_t eol = body.find('\n', pos);
        std::string line = (eol == std::string::npos)
            ? body.substr(pos)
            : body.substr(pos, eol - pos);
        pos = (eol == std::string::npos) ? body.size() : eol + 1;

        // Trim trailing \r and spaces
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (line.empty()) continue;

        // Collect all leading "[...]" tags.
        std::vector<int64_t> stamps;
        size_t p = 0;
        while (p < line.size() && line[p] == '[') {
            size_t close = line.find(']', p);
            if (close == std::string::npos) break;
            std::string inside = line.substr(p + 1, close - p - 1);
            if (!IsMetaTag(inside)) {
                int64_t t = ParseTimestamp(inside);
                if (t >= 0) stamps.push_back(t);
            }
            p = close + 1;
        }
        if (stamps.empty()) continue;

        std::string text = line.substr(p);
        // Trim leading spaces
        size_t lp = 0;
        while (lp < text.size() && text[lp] == ' ') ++lp;
        if (lp) text.erase(0, lp);

        for (int64_t t : stamps) out.push_back({ t, text });
    }

    std::sort(out.begin(), out.end(),
              [](const LrcLine& a, const LrcLine& b) { return a.ms < b.ms; });
    return out;
}

int FindCurrentLine(const std::vector<LrcLine>& lines, int64_t now_ms) {
    if (lines.empty() || now_ms < lines.front().ms) return -1;
    int lo = 0, hi = (int)lines.size() - 1, ans = -1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (lines[mid].ms <= now_ms) { ans = mid; lo = mid + 1; }
        else hi = mid - 1;
    }
    return ans;
}

}
