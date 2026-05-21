#pragma once
#include <cstdint>

namespace osc {

// Encode a VRChat chatbox OSC message ("/chatbox/input", string, T/F, T/F).
// VRChat docs: arg2 = send-now (true = display, false = open keyboard),
//              arg3 = play notification SFX.
// Returns number of bytes written, or 0 if buf too small.
int EncodeChatbox(const char* text, bool send_now, bool play_sfx,
                  uint8_t* out_buf, int buf_size);

}
