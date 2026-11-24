#include "lvgl_theme.h"

LvglTheme::LvglTheme(const std::string& name) : Theme(name) {
}

static int HexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool LvglTheme::TryParseColor(const std::string& color, lv_color_t* out) {
    size_t start = 0;
    size_t end = color.size();
    while (start < end && (color[start] == ' ' || color[start] == '\t' || color[start] == '\r' || color[start] == '\n')) {
        ++start;
    }
    while (end > start && (color[end - 1] == ' ' || color[end - 1] == '\t' || color[end - 1] == '\r' || color[end - 1] == '\n')) {
        --end;
    }

    std::string trimmed = color.substr(start, end - start);
    if (trimmed.rfind("#", 0) == 0) {
        trimmed = trimmed.substr(1);
    } else if (trimmed.rfind("0x", 0) == 0 || trimmed.rfind("0X", 0) == 0) {
        trimmed = trimmed.substr(2);
    } else {
        return false;
    }

    if (trimmed.size() == 3) {
        int r = HexNibble(trimmed[0]);
        int g = HexNibble(trimmed[1]);
        int b = HexNibble(trimmed[2]);
        if (r >= 0 && g >= 0 && b >= 0) {
            if (out != nullptr) {
                *out = lv_color_make(static_cast<uint8_t>(r * 17), static_cast<uint8_t>(g * 17), static_cast<uint8_t>(b * 17));
            }
            return true;
        }
        return false;
    }

    if (trimmed.size() == 6) {
        int nibbles[6];
        for (size_t i = 0; i < 6; ++i) {
            nibbles[i] = HexNibble(trimmed[i]);
            if (nibbles[i] < 0) {
                return false;
            }
        }
        if (out != nullptr) {
            uint8_t r = static_cast<uint8_t>((nibbles[0] << 4) | nibbles[1]);
            uint8_t g = static_cast<uint8_t>((nibbles[2] << 4) | nibbles[3]);
            uint8_t b = static_cast<uint8_t>((nibbles[4] << 4) | nibbles[5]);
            *out = lv_color_make(r, g, b);
        }
        return true;
    }
    return false;
}

lv_color_t LvglTheme::ParseColor(const std::string& color) {
    lv_color_t parsed;
    if (TryParseColor(color, &parsed)) {
        return parsed;
    }
    return lv_color_black();
}

std::string LvglTheme::FormatHex(lv_color_t color) {
    static const char kHex[] = "0123456789ABCDEF";
    uint32_t rgb = lv_color_to_int(color) & 0xFFFFFF;
    std::string out(7, '#');
    for (int i = 5; i >= 0; --i) {
        out[1 + i] = kHex[rgb & 0xF];
        rgb >>= 4;
    }
    return out;
}

void LvglTheme::ApplySolidBackground(lv_color_t bg) {
    uint32_t rgb = lv_color_to_int(bg);
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >> 8) & 0xFF;
    uint8_t b = rgb & 0xFF;
    int lum = static_cast<int>(0.299f * r + 0.587f * g + 0.114f * b);
    bool is_light_bg = (lum > 140);

    set_background_color(bg);
    set_chat_background_color(bg);
    set_text_color(is_light_bg ? lv_color_black() : lv_color_white());
    set_user_bubble_color(is_light_bg ? lv_color_hex(0x0284C7) : lv_color_hex(0x38BDF8));
    set_assistant_bubble_color(is_light_bg ? lv_color_white() : lv_color_hex(0x1E293B));
    set_system_bubble_color(is_light_bg ? lv_color_hex(0xE2E8F0) : lv_color_hex(0x334155));
    set_system_text_color(is_light_bg ? lv_color_hex(0x1E293B) : lv_color_hex(0xF8FAFC));
    set_border_color(is_light_bg ? lv_color_hex(0x94A3B8) : lv_color_hex(0x64748B));
    set_low_battery_color(lv_color_hex(0xFF1744));
}

LvglThemeManager::LvglThemeManager() {
}

LvglTheme* LvglThemeManager::GetTheme(const std::string& theme_name) {
    auto it = themes_.find(theme_name);
    if (it != themes_.end()) {
        return it->second;
    }
    return nullptr;
}

void LvglThemeManager::RegisterTheme(const std::string& theme_name, LvglTheme* theme) {
    themes_[theme_name] = theme;
}
