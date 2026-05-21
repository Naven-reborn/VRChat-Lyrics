#pragma once

namespace i18n {

enum class Lang { EN = 0, SC = 1, TC = 2 };

inline Lang current = Lang::EN;

// Pick string by current language. Use as t("English", "简体", "繁體").
inline const char* t(const char* en, const char* sc, const char* tc) {
    switch (current) {
        case Lang::SC: return sc;
        case Lang::TC: return tc;
        default:       return en;
    }
}

}
