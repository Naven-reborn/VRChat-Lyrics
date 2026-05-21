#include "osc_message.h"
#include <cstring>

namespace osc {

// OSC strings are null-terminated, then padded with zeros so the total length
// is a multiple of 4 bytes.
static int WritePaddedString(const char* s, uint8_t* buf, int buf_size) {
    int len = (int)std::strlen(s) + 1; // include null
    int padded = (len + 3) & ~3;
    if (padded > buf_size) return 0;
    std::memcpy(buf, s, len);
    for (int i = len; i < padded; ++i) buf[i] = 0;
    return padded;
}

int EncodeChatbox(const char* text, bool send_now, bool play_sfx,
                  uint8_t* out_buf, int buf_size) {
    int off = 0;

    int n = WritePaddedString("/chatbox/input", out_buf + off, buf_size - off);
    if (n == 0) return 0;
    off += n;

    // Type tag: ',sTF' or ',sTT' / ',sFF' depending on bools.
    // T/F carry no data — the type tag itself encodes the value.
    char tags[5] = ",s..";
    tags[2] = send_now ? 'T' : 'F';
    tags[3] = play_sfx ? 'T' : 'F';
    tags[4] = 0;
    n = WritePaddedString(tags, out_buf + off, buf_size - off);
    if (n == 0) return 0;
    off += n;

    n = WritePaddedString(text, out_buf + off, buf_size - off);
    if (n == 0) return 0;
    off += n;

    return off;
}

}
