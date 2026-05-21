#pragma once
#include "menu/menu.h"

namespace config {

// Load %APPDATA%\vrc-lyrics\config.json into the menu state.
// Missing or invalid file: state is left at defaults.
void Load(menu::State& s);

// Atomically write the current state to %APPDATA%\vrc-lyrics\config.json.
// Returns true on success.
bool Save(const menu::State& s);

}
