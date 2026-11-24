#include "lcd_display.h"
#include "assets/lang_config.h"
#include "gif/lvgl_gif.h"
#include "lvgl_theme.h"
#include "settings.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <esp_psram.h>
#include <esp_heap_caps.h>
#include <material_symbols.h>
#include <noto_emoji.h>
#include <src/misc/cache/lv_cache.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "board.h"

#define TAG "LcdDisplay"

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);
LV_FONT_DECLARE(BUILTIN_ICON_FONT);
LV_FONT_DECLARE(font_material_symbols_30_4);
LV_FONT_DECLARE(font_noto_emoji_30_4);

void LcdDisplay::InitializeLcdThemes() {
    auto text_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_TEXT_FONT);
    auto icon_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_ICON_FONT);
    auto large_icon_font = std::make_shared<LvglBuiltInFont>(&font_material_symbols_30_4);
    auto emoji_font = std::make_shared<LvglBuiltInFont>(&font_noto_emoji_30_4);

    // Neo-Brutalism Cyber Yellow Theme
    auto light_theme = new LvglTheme("light");
    light_theme->set_background_color(lv_color_hex(0xFFE600)); // Iconic Cyber Yellow #FFE600
    light_theme->set_text_color(lv_color_hex(0x000000));       // Deep solid black #000000
    light_theme->set_chat_background_color(lv_color_hex(0xFFE600));
    light_theme->set_user_bubble_color(lv_color_hex(0x00F0FF)); // Neo Cyan
    light_theme->set_assistant_bubble_color(lv_color_hex(0xFFFFFF)); // Stark White with black border
    light_theme->set_system_bubble_color(lv_color_hex(0xFFFFFF));
    light_theme->set_system_text_color(lv_color_hex(0x000000));
    light_theme->set_border_color(lv_color_hex(0x000000));      // Solid 2px black border
    light_theme->set_low_battery_color(lv_color_hex(0xFF1744));
    light_theme->set_text_font(text_font);
    light_theme->set_icon_font(icon_font);
    light_theme->set_large_icon_font(large_icon_font);
    light_theme->set_emoji_font(emoji_font);

    // Neo-Brutalism Dark Stabilo Green Theme
    auto dark_theme = new LvglTheme("dark");
    dark_theme->set_background_color(lv_color_hex(0x000000));       // Pitch Black #000000
    dark_theme->set_text_color(lv_color_hex(0x39FF14));             // Stabilo Neon Green #39FF14
    dark_theme->set_chat_background_color(lv_color_hex(0x000000));  // Dark chat background #000000
    dark_theme->set_user_bubble_color(lv_color_hex(0x39FF14));      // Neon Green user bubble
    dark_theme->set_assistant_bubble_color(lv_color_hex(0x121212)); // Deep charcoal with neon border
    dark_theme->set_system_bubble_color(lv_color_hex(0x121212));
    dark_theme->set_system_text_color(lv_color_hex(0x39FF14));      // Stabilo Neon Green
    dark_theme->set_border_color(lv_color_hex(0x39FF14));           // Bold Neon Green border #39FF14
    dark_theme->set_low_battery_color(lv_color_hex(0xFF0055));      // Hot Red/Pink
    dark_theme->set_text_font(text_font);
    dark_theme->set_icon_font(icon_font);
    dark_theme->set_large_icon_font(large_icon_font);
    dark_theme->set_emoji_font(emoji_font);

    struct ThemePresetSpec {
        const char* name;
        uint32_t bg;
        uint32_t text;
        uint32_t user_bubble;
        uint32_t assistant_bubble;
        uint32_t system_bubble;
        uint32_t system_text;
        uint32_t border;
        uint32_t low_battery;
    };

    static const ThemePresetSpec kColorPresets[] = {
        {"blue",   0x1565C0, 0xFFFFFF, 0x38BDF8, 0x1E3A8A, 0x1E3A8A, 0xE0F2FE, 0x60A5FA, 0xFF1744},
        {"red",    0xC62828, 0xFFFFFF, 0xEF4444, 0x7F1D1D, 0x7F1D1D, 0xFEE2E2, 0xFCA5A5, 0xFF0055},
        {"green",  0x15803D, 0xFFFFFF, 0x22C55E, 0x14532D, 0x14532D, 0xDCFCE7, 0x4ADE80, 0xFF1744},
        {"purple", 0x7E22CE, 0xFFFFFF, 0xC084FC, 0x581C87, 0x581C87, 0xF3E8FF, 0xD8B4FE, 0xFF1744},
        {"white",  0xF5F5F5, 0x000000, 0x95EC69, 0xFFFFFF, 0xE5E5E5, 0x555555, 0xD4D4D4, 0xDC2626},
        {"orange", 0xEA580C, 0xFFFFFF, 0xFDBA74, 0x9A3412, 0x9A3412, 0xFFEDD5, 0xFDBA74, 0xFF1744},
        {"pink",   0xDB2777, 0xFFFFFF, 0xF472B6, 0x9D174D, 0x9D174D, 0xFCE7F3, 0xF9A8D4, 0xFF1744},
        {"cyan",   0x0097A7, 0xFFFFFF, 0x22D3EE, 0x0E7490, 0x0E7490, 0xECFEFF, 0x67E8F9, 0xFF1744},
        {"gray",   0x4B5563, 0xFFFFFF, 0x9CA3AF, 0x374151, 0x374151, 0xF3F4F6, 0x9CA3AF, 0xFF1744},
        {"brown",  0x6D4C41, 0xFFFFFF, 0xA1887F, 0x4E342E, 0x4E342E, 0xEFEBE9, 0xBCAAA4, 0xFF1744},
    };

    auto apply_fonts = [&](LvglTheme* theme) {
        theme->set_text_font(text_font);
        theme->set_icon_font(icon_font);
        theme->set_large_icon_font(large_icon_font);
        theme->set_emoji_font(emoji_font);
    };

    auto& theme_manager = LvglThemeManager::GetInstance();
    theme_manager.RegisterTheme("light", light_theme);
    theme_manager.RegisterTheme("dark", dark_theme);

    auto theme_1 = new LvglTheme("theme_1");
    theme_1->set_background_color(lv_color_hex(0xFFFFFF)); // Default pure white #FFFFFF
    theme_1->set_text_color(lv_color_hex(0x000000));       // Deep solid black #000000
    theme_1->set_chat_background_color(lv_color_hex(0xFFFFFF));
    theme_1->set_user_bubble_color(lv_color_hex(0x0284C7));
    theme_1->set_assistant_bubble_color(lv_color_hex(0xFFFFFF));
    theme_1->set_system_bubble_color(lv_color_hex(0xF1F5F9));
    theme_1->set_system_text_color(lv_color_hex(0x000000));
    theme_1->set_border_color(lv_color_hex(0xCBD5E1));
    theme_1->set_low_battery_color(lv_color_hex(0xFF1744));
    apply_fonts(theme_1);
    theme_manager.RegisterTheme("theme_1", theme_1);

    for (const auto& spec : kColorPresets) {
        auto theme = new LvglTheme(spec.name);
        theme->set_background_color(lv_color_hex(spec.bg));
        theme->set_text_color(lv_color_hex(spec.text));
        theme->set_chat_background_color(lv_color_hex(spec.bg));
        theme->set_user_bubble_color(lv_color_hex(spec.user_bubble));
        theme->set_assistant_bubble_color(lv_color_hex(spec.assistant_bubble));
        theme->set_system_bubble_color(lv_color_hex(spec.system_bubble));
        theme->set_system_text_color(lv_color_hex(spec.system_text));
        theme->set_border_color(lv_color_hex(spec.border));
        theme->set_low_battery_color(lv_color_hex(spec.low_battery));
        apply_fonts(theme);
        theme_manager.RegisterTheme(spec.name, theme);
        if (strcmp(spec.name, "white") == 0) {
            theme_manager.RegisterTheme("wechat", theme);
        }
    }

    auto custom_theme = new LvglTheme("custom");
    apply_fonts(custom_theme);
    lv_color_t custom_bg = lv_color_hex(0x1E293B);
    Settings display_settings("display", false);
    std::string saved_hex = display_settings.GetString("custom_hex", "");
    lv_color_t parsed_bg;
    if (!saved_hex.empty() && LvglTheme::TryParseColor(saved_hex, &parsed_bg)) {
        custom_bg = parsed_bg;
    }
    custom_theme->ApplySolidBackground(custom_bg);
    theme_manager.RegisterTheme("custom", custom_theme);
}

LcdDisplay::LcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                       int height)
    : panel_io_(panel_io), panel_(panel) {
    width_ = width;
    height_ = height;

    // Initialize LCD themes
    InitializeLcdThemes();

    // Load UI theme and color from settings (default UI is Theme 2 WhatsApp/WeChat chat style)
    Settings settings("display", true);
    ui_theme_id_ = settings.GetInt("ui_theme", 2);
    if (ui_theme_id_ < 1 || ui_theme_id_ > 2) {
        ui_theme_id_ = 2;
    }

    std::string default_color = (ui_theme_id_ == 2) ? "white" : "theme_1";
    std::string theme_name = settings.GetString("theme", default_color);
    if (ui_theme_id_ == 1 && (theme_name == "dark" || theme_name == "light")) {
        theme_name = "theme_1";
    }
    if (theme_name.empty() || LvglThemeManager::GetInstance().GetTheme(theme_name) == nullptr) {
        theme_name = default_color;
        settings.SetString("theme", default_color);
    }
    current_theme_ = LvglThemeManager::GetInstance().GetTheme(theme_name);

    // Create a timer to hide the preview image
    esp_timer_create_args_t preview_timer_args = {
        .callback =
            [](void* arg) {
                LcdDisplay* display = static_cast<LcdDisplay*>(arg);
                display->SetPreviewImage(nullptr);
            },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "preview_timer",
        .skip_unhandled_events = false,
    };
    esp_timer_create(&preview_timer_args, &preview_timer_);
}

SpiLcdDisplay::SpiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                             int width, int height, int offset_x, int offset_y, bool mirror_x,
                             bool mirror_y, bool swap_xy)
    : LcdDisplay(panel_io, panel, width, height) {
    // Initial black screen to prevent blinding flash on cold boot
    uint16_t bg_color = 0x0000;
    std::vector<uint16_t> buffer(width_, bg_color);
    for (int y = 0; y < height_; y++) {
        esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y + 1, buffer.data());
    }

    // Set the display to on
    ESP_LOGI(TAG, "Turning display on");
    {
        esp_err_t __err = esp_lcd_panel_disp_on_off(panel_, true);
        if (__err == ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGW(TAG, "Panel does not support disp_on_off; assuming ON");
        } else {
            ESP_ERROR_CHECK(__err);
        }
    }

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

#if CONFIG_SPIRAM
    // lv image cache, currently only PNG is supported
    size_t psram_size_mb = esp_psram_get_size() / 1024 / 1024;
    if (psram_size_mb >= 8) {
        lv_image_cache_resize(2 * 1024 * 1024, true);
        ESP_LOGI(TAG, "Use 2MB of PSRAM for image cache");
    } else if (psram_size_mb >= 2) {
        lv_image_cache_resize(512 * 1024, true);
        ESP_LOGI(TAG, "Use 512KB of PSRAM for image cache");
    }
#endif

    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
#if CONFIG_SOC_CPU_CORES_NUM > 1
    port_cfg.task_affinity = 1;
#endif
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding LCD display");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width_ * 20),
        .double_buffer = false,
        .trans_size = 0,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .monochrome = false,
        .rotation =
            {
                .swap_xy = swap_xy,
                .mirror_x = mirror_x,
                .mirror_y = mirror_y,
            },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags =
            {
                .buff_dma = 1,
                .buff_spiram = 0,
                .sw_rotate = 0,
                .swap_bytes = 1,
                .full_refresh = 0,
                .direct_mode = 0,
            },
    };

    display_ = lvgl_port_add_disp(&display_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    if (offset_x != 0 || offset_y != 0) {
        lv_display_set_offset(display_, offset_x, offset_y);
    }
}

// RGB LCD implementation
RgbLcdDisplay::RgbLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                             int width, int height, int offset_x, int offset_y, bool mirror_x,
                             bool mirror_y, bool swap_xy)
    : LcdDisplay(panel_io, panel, width, height) {
    // draw white
    std::vector<uint16_t> buffer(width_, 0xFFFF);
    for (int y = 0; y < height_; y++) {
        esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y + 1, buffer.data());
    }

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
    port_cfg.timer_period_ms = 50;
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding LCD display");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .buffer_size = static_cast<uint32_t>(width_ * 20),
        .double_buffer = true,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .rotation =
            {
                .swap_xy = swap_xy,
                .mirror_x = mirror_x,
                .mirror_y = mirror_y,
            },
        .flags =
            {
                .buff_dma = 1,
                .swap_bytes = 0,
                .full_refresh = 1,
                .direct_mode = 1,
            },
    };

    const lvgl_port_display_rgb_cfg_t rgb_cfg = {.flags = {
                                                     .bb_mode = true,
                                                     .avoid_tearing = true,
                                                 }};

    display_ = lvgl_port_add_disp_rgb(&display_cfg, &rgb_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add RGB display");
        return;
    }

    if (offset_x != 0 || offset_y != 0) {
        lv_display_set_offset(display_, offset_x, offset_y);
    }
}

MipiLcdDisplay::MipiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                               int width, int height, int offset_x, int offset_y, bool mirror_x,
                               bool mirror_y, bool swap_xy)
    : LcdDisplay(panel_io, panel, width, height) {
    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding LCD display");
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = panel_io,
        .panel_handle = panel,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width_ * 50),
        .double_buffer = false,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .monochrome = false,
        /* Rotation values must be same as used in esp_lcd for initial settings of the screen */
        .rotation =
            {
                .swap_xy = swap_xy,
                .mirror_x = mirror_x,
                .mirror_y = mirror_y,
            },
        .flags =
            {
                .buff_dma = true,
                .buff_spiram = false,
                .sw_rotate = true,
            },
    };

    const lvgl_port_display_dsi_cfg_t dpi_cfg = {.flags = {
                                                     .avoid_tearing = false,
                                                 }};
    display_ = lvgl_port_add_disp_dsi(&disp_cfg, &dpi_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    if (offset_x != 0 || offset_y != 0) {
        lv_display_set_offset(display_, offset_x, offset_y);
    }
}

LcdDisplay::~LcdDisplay() {
    SetPreviewImage(nullptr);

    // Clean up GIF controller
    if (gif_controller_) {
        gif_controller_->Stop();
        gif_controller_.reset();
    }

    if (music_anim_timer_ != nullptr) {
        lv_timer_del(music_anim_timer_);
        music_anim_timer_ = nullptr;
    }
    if (music_container_ != nullptr) {
        lv_obj_del(music_container_);
        music_container_ = nullptr;
        music_duration_label_ = nullptr;
        music_progress_bar_ = nullptr;
    }

    if (preview_timer_ != nullptr) {
        esp_timer_stop(preview_timer_);
        esp_timer_delete(preview_timer_);
    }

    if (preview_image_ != nullptr) {
        lv_obj_del(preview_image_);
    }
    if (chat_message_label_ != nullptr) {
        lv_obj_del(chat_message_label_);
    }
    if (emoji_label_ != nullptr) {
        lv_obj_del(emoji_label_);
    }
    if (emoji_image_ != nullptr) {
        lv_obj_del(emoji_image_);
    }
    if (emoji_box_ != nullptr) {
        lv_obj_del(emoji_box_);
    }
    if (content_ != nullptr) {
        lv_obj_del(content_);
    }
    if (bottom_bar_ != nullptr) {
        lv_obj_del(bottom_bar_);
    }
    if (status_bar_ != nullptr) {
        lv_obj_del(status_bar_);
    }
    if (top_bar_ != nullptr) {
        lv_obj_del(top_bar_);
    }
    if (side_bar_ != nullptr) {
        lv_obj_del(side_bar_);
    }
    if (container_ != nullptr) {
        lv_obj_del(container_);
    }
    if (display_ != nullptr) {
        lv_display_delete(display_);
    }

    if (panel_ != nullptr) {
        esp_lcd_panel_del(panel_);
    }
    if (panel_io_ != nullptr) {
        esp_lcd_panel_io_del(panel_io_);
    }
}

bool LcdDisplay::Lock(int timeout_ms) { return lvgl_port_lock(timeout_ms); }

void LcdDisplay::Unlock() { lvgl_port_unlock(); }

// Helper to determine whether text should be dark or light for optimal contrast on any bubble background
static inline lv_color_t GetContrastTextColor(lv_color_t bg) {
    uint32_t rgb = lv_color_to_int(bg);
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >> 8) & 0xFF;
    uint8_t b = rgb & 0xFF;
    int lum = (int)(0.299f * r + 0.587f * g + 0.114f * b);
    return (lum > 140) ? lv_color_black() : lv_color_white();
}

void LcdDisplay::SetupUI_Theme2() {
    Display::SetupUI();  // Mark SetupUI as called
    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();
    auto icon_font = lvgl_theme->icon_font()->font();
    auto large_icon_font = lvgl_theme->large_icon_font()->font();

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);
    lv_obj_set_style_text_color(screen, lvgl_theme->text_color(), 0);
    lv_obj_set_style_bg_color(screen, lvgl_theme->background_color(), 0);

    /* Container */
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_radius(container_, 0, 0);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_row(container_, 0, 0);
    lv_obj_set_style_bg_color(container_, lvgl_theme->background_color(), 0);
    lv_obj_set_style_border_color(container_, lvgl_theme->border_color(), 0);

    /* Preview image for camera stream - full screen display like Theme 1 */
    preview_image_ = lv_image_create(screen);
    lv_obj_set_size(preview_image_, width_, height_);
    lv_obj_align(preview_image_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);

    /* Layer 1: Top bar - header with avatar on left, wifi & battery on right */
    top_bar_ = lv_obj_create(container_);
    lv_obj_set_size(top_bar_, LV_HOR_RES, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(top_bar_, 0, 0);
    lv_obj_set_style_bg_opa(top_bar_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(top_bar_, lvgl_theme->background_color(), 0);
    lv_obj_set_style_border_width(top_bar_, 0, 0);
    lv_obj_set_style_border_side(top_bar_, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(top_bar_, 1, 0);
    lv_obj_set_style_border_color(top_bar_, lvgl_theme->border_color(), 0);
    lv_obj_set_style_pad_all(top_bar_, 0, 0);
    lv_obj_set_style_pad_top(top_bar_, 4, 0);
    lv_obj_set_style_pad_bottom(top_bar_, 4, 0);
    lv_obj_set_style_pad_left(top_bar_, 8, 0);
    lv_obj_set_style_pad_right(top_bar_, 8, 0);
    lv_obj_set_flex_flow(top_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(top_bar_, LV_SCROLLBAR_MODE_OFF);

    // Left avatar box (Yellow circular avatar with cute eyes as shown in reference image)
    lv_obj_t* left_box = lv_obj_create(top_bar_);
    lv_obj_set_size(left_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(left_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left_box, 0, 0);
    lv_obj_set_style_pad_all(left_box, 0, 0);

    avatar_circle_ = lv_obj_create(left_box);
    lv_obj_set_size(avatar_circle_, 18, 18);
    lv_obj_set_style_radius(avatar_circle_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(avatar_circle_, lv_color_hex(0xFFD600), 0); // Vibrant yellow
    lv_obj_set_style_border_width(avatar_circle_, 0, 0);
    lv_obj_set_style_pad_all(avatar_circle_, 0, 0);

    lv_obj_t* avatar_dots = lv_label_create(avatar_circle_);
    lv_label_set_text(avatar_dots, ".. ");
    lv_obj_set_style_text_color(avatar_dots, lv_color_black(), 0);
    lv_obj_center(avatar_dots);

    // Right icons container: Battery icon NEXT TO Wifi icon
    lv_obj_t* right_icons = lv_obj_create(top_bar_);
    lv_obj_set_size(right_icons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(right_icons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_icons, 0, 0);
    lv_obj_set_style_pad_all(right_icons, 0, 0);
    lv_obj_set_flex_flow(right_icons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right_icons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(right_icons, 4, 0);

    mute_label_ = lv_label_create(right_icons);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, icon_font, 0);
    lv_obj_set_style_text_color(mute_label_, lvgl_theme->text_color(), 0);

    // Battery icon
    battery_label_ = lv_label_create(right_icons);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, icon_font, 0);
    lv_obj_set_style_text_color(battery_label_, lvgl_theme->text_color(), 0);

    // Wifi icon (placed right next to battery icon)
    network_label_ = lv_label_create(right_icons);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, icon_font, 0);
    lv_obj_set_style_text_color(network_label_, lvgl_theme->text_color(), 0);

    /* Layer 2: Status bar - for center text labels */
    status_bar_ = lv_obj_create(screen);
    lv_obj_set_size(status_bar_, LV_HOR_RES - 80, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_style_bg_opa(status_bar_, LV_OPA_TRANSP, 0);  // Transparent background
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_style_pad_top(status_bar_, 4, 0);
    lv_obj_set_scrollbar_mode(status_bar_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_layout(status_bar_, LV_LAYOUT_NONE, 0);  // Use absolute positioning
    lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, 0);        // Overlap with top_bar_

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(notification_label_, LV_HOR_RES - 80);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(notification_label_, lvgl_theme->text_color(), 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_align(notification_label_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(status_label_, LV_HOR_RES - 80);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label_, lvgl_theme->text_color(), 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);
    lv_obj_align(status_label_, LV_ALIGN_CENTER, 0, 0);

    /* Content - Chat area */
    content_ = lv_obj_create(container_);
    lv_obj_set_style_radius(content_, 0, 0);
    lv_obj_set_width(content_, LV_HOR_RES);
    lv_obj_set_flex_grow(content_, 1);
    lv_obj_set_style_pad_all(content_, lvgl_theme->spacing(4), 0);
    lv_obj_set_style_border_width(content_, 0, 0);
    lv_obj_set_style_bg_color(content_, lvgl_theme->chat_background_color(),
                              0);  // Background for chat area

    // Enable scrolling for chat content
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(content_, LV_DIR_VER);

    // Create a flex container for chat messages
    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(content_, lvgl_theme->spacing(4), 0);  // Space between messages

    // We'll create chat messages dynamically in SetChatMessage
    chat_message_label_ = nullptr;

    low_battery_popup_ = lv_obj_create(screen);
    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, text_font->line_height * 2);
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, -lvgl_theme->spacing(4));
    lv_obj_set_style_bg_color(low_battery_popup_, lvgl_theme->low_battery_color(), 0);
    lv_obj_set_style_radius(low_battery_popup_, lvgl_theme->spacing(4), 0);
    low_battery_label_ = lv_label_create(low_battery_popup_);
    lv_label_set_text(low_battery_label_, Lang::Strings::BATTERY_NEED_CHARGE);
    lv_obj_set_style_text_color(low_battery_label_, lv_color_white(), 0);
    lv_obj_center(low_battery_label_);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);

    emoji_image_ = lv_img_create(screen);
    lv_obj_align(emoji_image_, LV_ALIGN_TOP_MID, 0,
                 text_font->line_height + lvgl_theme->spacing(8));
    lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);

    // Display AI logo (hidden by default in Theme 2)
    emoji_label_ = lv_label_create(screen);
    lv_obj_center(emoji_label_);
    lv_obj_set_style_text_font(emoji_label_, large_icon_font, 0);
    lv_obj_set_style_text_color(emoji_label_, lvgl_theme->text_color(), 0);
    lv_label_set_text(emoji_label_, MATERIAL_SYMBOLS_ROBOT_2);
    lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
}
#if CONFIG_IDF_TARGET_ESP32P4
#define MAX_MESSAGES 40
#else
#define MAX_MESSAGES 20
#endif
void LcdDisplay::SetChatMessage_Theme2(const char* role, const char* content) {
    if (!setup_ui_called_) {
        ESP_LOGW(TAG, "SetChatMessage_Theme2('%s', '%s') called before SetupUI() - message will be lost!",
                 role, content);
    }
    DisplayLockGuard lock(this);
    if (content_ == nullptr) {
        if (setup_ui_called_) {
            ESP_LOGW(TAG,
                     "SetChatMessage_Theme2('%s', '%s') failed: content_ is nullptr",
                     role, content);
        }
        return;
    }

    // Check if message count exceeds limit
    uint32_t child_count = lv_obj_get_child_cnt(content_);
    if (child_count >= MAX_MESSAGES) {
        // Delete the oldest message (first child object)
        lv_obj_t* first_child = lv_obj_get_child(content_, 0);
        if (first_child != nullptr) {
            lv_obj_del(first_child);
            // Refresh child count after deletion
            child_count = lv_obj_get_child_cnt(content_);
        }
        // Scroll to the last message immediately (get last_child after deletion)
        if (child_count > 0) {
            lv_obj_t* last_child = lv_obj_get_child(content_, child_count - 1);
            if (last_child != nullptr && lv_obj_is_valid(last_child)) {
                lv_obj_scroll_to_view_recursive(last_child, LV_ANIM_OFF);
            }
        }
    }

    // Collapse system messages (if it's a system message, check if the last message is also a
    // system message)
    if (strcmp(role, "system") == 0) {
        // Refresh child count to get accurate count after potential deletion above
        child_count = lv_obj_get_child_cnt(content_);
        if (child_count > 0) {
            // Get the last message container
            lv_obj_t* last_container = lv_obj_get_child(content_, child_count - 1);
            if (last_container != nullptr && lv_obj_is_valid(last_container) &&
                lv_obj_get_child_cnt(last_container) > 0) {
                // Get the bubble inside the container
                lv_obj_t* last_bubble = lv_obj_get_child(last_container, 0);
                if (last_bubble != nullptr && lv_obj_is_valid(last_bubble)) {
                    // Check if bubble type is system message
                    void* bubble_type_ptr = lv_obj_get_user_data(last_bubble);
                    if (bubble_type_ptr != nullptr &&
                        strcmp((const char*)bubble_type_ptr, "system") == 0) {
                        // If the last message is also a system message, delete it
                        lv_obj_del(last_container);
                    }
                }
            }
        }
    } else {
        // Hide the centered AI logo
        if (emoji_label_ != nullptr) {
            lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Avoid empty message boxes
    if (strlen(content) == 0) {
        return;
    }

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);

    // Create a message bubble
    lv_obj_t* msg_bubble = lv_obj_create(content_);
    lv_obj_set_style_radius(msg_bubble, 8, 0);
    lv_obj_set_scrollbar_mode(msg_bubble, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(msg_bubble, 0, 0);
    lv_obj_set_style_pad_left(msg_bubble, 8, 0);
    lv_obj_set_style_pad_right(msg_bubble, 8, 0);
    lv_obj_set_style_pad_top(msg_bubble, 5, 0);
    lv_obj_set_style_pad_bottom(msg_bubble, 3, 0);
    lv_obj_set_flex_flow(msg_bubble, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(msg_bubble, 1, 0);

    // Create the message text
    lv_obj_t* msg_text = lv_label_create(msg_bubble);
    lv_label_set_text(msg_text, content);

    // Calculate bubble width constraints
    lv_coord_t max_width = LV_HOR_RES * 85 / 100 - 16;  // 85% of screen width
    lv_coord_t min_width = 50;

    // Let LVGL calculate the natural text width first
    lv_obj_set_width(msg_text, LV_SIZE_CONTENT);
    lv_obj_update_layout(msg_text);
    lv_coord_t text_width = lv_obj_get_width(msg_text);

    // Ensure text width is not less than minimum width
    if (text_width < min_width) {
        text_width = min_width;
    }

    // Constrain to max width
    lv_coord_t bubble_width = (text_width < max_width) ? text_width : max_width;

    // Set message text width
    lv_obj_set_width(msg_text, bubble_width);
    lv_label_set_long_mode(msg_text, LV_LABEL_LONG_WRAP);

    // Set bubble width
    lv_obj_set_width(msg_bubble, bubble_width);
    lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);

    // Set alignment and style based on message role and current theme
    if (strcmp(role, "user") == 0) {
        lv_color_t bubble_bg = lvgl_theme->user_bubble_color();
        lv_color_t text_col = GetContrastTextColor(bubble_bg);
        lv_obj_set_style_bg_color(msg_bubble, bubble_bg, 0);
        lv_obj_set_style_bg_opa(msg_bubble, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(msg_bubble, 1, 0);
        lv_obj_set_style_border_color(msg_bubble, lvgl_theme->border_color(), 0);
        lv_obj_set_style_text_color(msg_text, text_col, 0);

        // Set custom attribute to mark bubble type
        lv_obj_set_user_data(msg_bubble, (void*)"user");
        lv_obj_set_width(msg_bubble, LV_SIZE_CONTENT);
        lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);
        lv_obj_set_style_flex_grow(msg_bubble, 0, 0);
    } else if (strcmp(role, "assistant") == 0) {
        lv_color_t bubble_bg = lvgl_theme->assistant_bubble_color();
        lv_color_t text_col = GetContrastTextColor(bubble_bg);
        lv_obj_set_style_bg_color(msg_bubble, bubble_bg, 0);
        lv_obj_set_style_bg_opa(msg_bubble, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(msg_bubble, 1, 0);
        lv_obj_set_style_border_color(msg_bubble, lvgl_theme->border_color(), 0);
        lv_obj_set_style_text_color(msg_text, text_col, 0);

        // Set custom attribute to mark bubble type
        lv_obj_set_user_data(msg_bubble, (void*)"assistant");
        lv_obj_set_width(msg_bubble, LV_SIZE_CONTENT);
        lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);
        lv_obj_set_style_flex_grow(msg_bubble, 0, 0);
    } else if (strcmp(role, "system") == 0) {
        lv_obj_set_style_bg_color(msg_bubble, lvgl_theme->system_bubble_color(), 0);
        lv_obj_set_style_bg_opa(msg_bubble, LV_OPA_70, 0);
        lv_obj_set_style_text_color(msg_text, lvgl_theme->system_text_color(), 0);

        // Set custom attribute to mark bubble type
        lv_obj_set_user_data(msg_bubble, (void*)"system");

        // Set appropriate width for content
        lv_obj_set_width(msg_bubble, LV_SIZE_CONTENT);
        lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);

        // Don't grow
        lv_obj_set_style_flex_grow(msg_bubble, 0, 0);
    }

    // Add WhatsApp-style timestamp footer for user and assistant messages
    if (strcmp(role, "system") != 0) {
        time_t now = time(NULL);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        char time_str[16];
        if (timeinfo.tm_year > (2020 - 1900)) {
            snprintf(time_str, sizeof(time_str), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
        } else {
            int sec = (int)(esp_timer_get_time() / 1000000);
            snprintf(time_str, sizeof(time_str), "%02d:%02d", (sec / 3600) % 24, (sec / 60) % 60);
        }

        lv_color_t stamp_color = (strcmp(role, "user") == 0)
                                     ? GetContrastTextColor(lvgl_theme->user_bubble_color())
                                     : GetContrastTextColor(lvgl_theme->assistant_bubble_color());

        lv_obj_t* footer_row = lv_obj_create(msg_bubble);
        lv_obj_set_width(footer_row, LV_PCT(100));
        lv_obj_set_height(footer_row, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(footer_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(footer_row, 0, 0);
        lv_obj_set_style_pad_all(footer_row, 0, 0);
        lv_obj_set_flex_flow(footer_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(footer_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_scrollbar_mode(footer_row, LV_SCROLLBAR_MODE_OFF);

        lv_obj_t* time_label = lv_label_create(footer_row);
        lv_label_set_text(time_label, time_str);
        lv_obj_set_style_text_font(time_label, lvgl_theme->text_font()->font(), 0);
        lv_obj_set_style_text_color(time_label, stamp_color, 0);
        lv_obj_set_style_text_opa(time_label, LV_OPA_60, 0);
    }

    // Create a full-width container for user messages to ensure right alignment
    if (strcmp(role, "user") == 0) {
        // Create a full-width container
        lv_obj_t* container = lv_obj_create(content_);
        lv_obj_set_width(container, LV_HOR_RES);
        lv_obj_set_height(container, LV_SIZE_CONTENT);

        // Make container transparent and borderless
        lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(container, 0, 0);
        lv_obj_set_style_pad_all(container, 0, 0);

        // Move the message bubble into this container
        lv_obj_set_parent(msg_bubble, container);

        // Right align the bubble in the container
        lv_obj_align(msg_bubble, LV_ALIGN_RIGHT_MID, -25, 0);

        // Auto-scroll to this container
        lv_obj_scroll_to_view_recursive(container, LV_ANIM_ON);
    } else if (strcmp(role, "system") == 0) {
        // Create full-width container for system messages to ensure center alignment
        lv_obj_t* container = lv_obj_create(content_);
        lv_obj_set_width(container, LV_HOR_RES);
        lv_obj_set_height(container, LV_SIZE_CONTENT);

        lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(container, 0, 0);
        lv_obj_set_style_pad_all(container, 0, 0);

        lv_obj_set_parent(msg_bubble, container);
        lv_obj_align(msg_bubble, LV_ALIGN_CENTER, 0, 0);
        lv_obj_scroll_to_view_recursive(container, LV_ANIM_ON);
    } else {
        // For assistant messages
        // Left align assistant messages
        lv_obj_align(msg_bubble, LV_ALIGN_LEFT_MID, 0, 0);

        // Auto-scroll to the message bubble
        lv_obj_scroll_to_view_recursive(msg_bubble, LV_ANIM_ON);
    }

    // Store reference to the latest message label
    chat_message_label_ = msg_text;
}

void LcdDisplay::SetPreviewImage_Theme2(std::unique_ptr<LvglImage> image) {
    DisplayLockGuard lock(this);
    if (preview_image_ == nullptr) {
        ESP_LOGE(TAG, "Preview image is not initialized");
        return;
    }

    if (image == nullptr) {
        esp_timer_stop(preview_timer_);
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
        preview_image_cached_.reset();
        return;
    }

    preview_image_cached_ = std::move(image);
    auto img_dsc = preview_image_cached_->image_dsc();
    lv_image_set_src(preview_image_, img_dsc);
    if (img_dsc->header.w > 0 && img_dsc->header.h > 0) {
        // Scale to fill screen height (maximize display area, same as Theme 1)
        int scale_w = 128 * width_ / img_dsc->header.w;
        int scale_h = 128 * height_ / img_dsc->header.h;
        int scale = (scale_w > scale_h) ? scale_w : scale_h;  // Fill screen
        
        // Apply zoom if active (1x to 10x)
        if (zoom_level_ > 1) {
            scale *= zoom_level_;
        }
        
        lv_image_set_scale(preview_image_, scale);
    }

    lv_obj_remove_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
    esp_timer_stop(preview_timer_);
    ESP_ERROR_CHECK(esp_timer_start_once(preview_timer_, PREVIEW_IMAGE_DURATION_MS * 1000));
}

void LcdDisplay::ClearChatMessages_Theme2() {
    DisplayLockGuard lock(this);
    if (content_ == nullptr) {
        return;
    }

    if (music_visualizer_active_) {
        StopMusicVisualizer();
    }
    music_container_ = nullptr;
    for (int i = 0; i < 7; i++) {
        dj_bars_[i] = nullptr;
    }
    music_title_label_ = nullptr;
    music_sub_label_ = nullptr;

    // Use lv_obj_clean to delete all children of content_ (chat message bubbles)
    lv_obj_clean(content_);

    // Reset chat_message_label_ as it has been deleted
    chat_message_label_ = nullptr;

    // Restore the emoji label or image based on what's available
    if (emoji_image_ != nullptr) {
        lv_obj_remove_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
    } else if (emoji_label_ != nullptr) {
        lv_obj_remove_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
    }

    ESP_LOGI(TAG, "Chat messages cleared (Theme 2)");
}

void LcdDisplay::SetupUI_Theme1() {
    Display::SetupUI();  // Mark SetupUI as called
    DisplayLockGuard lock(this);
    LvglTheme* lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();
    auto icon_font = lvgl_theme->icon_font()->font();
    auto large_icon_font = lvgl_theme->large_icon_font()->font();

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);
    lv_obj_set_style_text_color(screen, lvgl_theme->text_color(), 0);
    lv_obj_set_style_bg_color(screen, lvgl_theme->background_color(), 0);

    /* Clean Solid Dynamic Background Container */
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_radius(container_, 0, 0);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    if (lvgl_theme->background_image() != nullptr) {
        lv_obj_set_style_bg_image_src(container_, lvgl_theme->background_image()->image_dsc(), 0);
        lv_obj_set_style_bg_image_opa(container_, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_opa(container_, LV_OPA_TRANSP, 0);
    } else {
        lv_obj_set_style_bg_image_src(container_, nullptr, 0);
        lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(container_, lvgl_theme->background_color(), 0);
    }
    lv_obj_align(container_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(container_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(container_, LV_OBJ_FLAG_SCROLLABLE);

    /* Bottom layer: emoji_box_ - centered display */
    emoji_box_ = lv_obj_create(screen);
    lv_obj_set_size(emoji_box_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(emoji_box_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(emoji_box_, 0, 0);
    lv_obj_set_style_border_width(emoji_box_, 0, 0);
    lv_obj_align(emoji_box_, LV_ALIGN_CENTER, 0, 0);

    emoji_label_ = lv_label_create(emoji_box_);
    lv_obj_set_style_text_font(emoji_label_, large_icon_font, 0);
    lv_obj_set_style_text_color(emoji_label_, lvgl_theme->text_color(), 0);
    lv_label_set_text(emoji_label_, MATERIAL_SYMBOLS_ROBOT_2);

    emoji_image_ = lv_img_create(emoji_box_);
    lv_obj_center(emoji_image_);
    lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);

    /* Middle layer: preview_image_ - full screen display */
    preview_image_ = lv_image_create(screen);
    lv_obj_set_size(preview_image_, width_, height_);
    lv_obj_align(preview_image_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);

    /* Layer 1: Top bar - for status icons */
    top_bar_ = lv_obj_create(screen);
    lv_obj_set_size(top_bar_, LV_HOR_RES, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(top_bar_, 0, 0);
    lv_obj_set_style_bg_opa(top_bar_, LV_OPA_TRANSP, 0);  // Transparent to let cyber grid show
    lv_obj_set_style_bg_color(top_bar_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(top_bar_, 0, 0);
    lv_obj_set_style_pad_all(top_bar_, 0, 0);
    lv_obj_set_style_pad_top(top_bar_, lvgl_theme->spacing(2), 0);
    lv_obj_set_style_pad_bottom(top_bar_, lvgl_theme->spacing(2), 0);
    lv_obj_set_style_pad_left(top_bar_, lvgl_theme->spacing(8), 0);
    lv_obj_set_style_pad_right(top_bar_, lvgl_theme->spacing(8), 0);
    lv_obj_set_flex_flow(top_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(top_bar_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_align(top_bar_, LV_ALIGN_TOP_MID, 0, 0);

    // Left icon
    network_label_ = lv_label_create(top_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, icon_font, 0);
    lv_obj_set_style_text_color(network_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_margin_left(network_label_, 4, 0); // Geser 0.4mm (~4px) ke arah tengah

    // Right icons container
    lv_obj_t* right_icons = lv_obj_create(top_bar_);
    lv_obj_set_size(right_icons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(right_icons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_icons, 0, 0);
    lv_obj_set_style_pad_all(right_icons, 0, 0);
    lv_obj_set_flex_flow(right_icons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right_icons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    mute_label_ = lv_label_create(right_icons);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, icon_font, 0);
    lv_obj_set_style_text_color(mute_label_, lvgl_theme->text_color(), 0);

    battery_label_ = lv_label_create(right_icons);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, icon_font, 0);
    lv_obj_set_style_text_color(battery_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_margin_left(battery_label_, lvgl_theme->spacing(2), 0);

    /* Layer 2: Status bar - for center text labels */
    status_bar_ = lv_obj_create(screen);
    lv_obj_set_size(status_bar_, LV_HOR_RES, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_style_bg_opa(status_bar_, LV_OPA_TRANSP, 0);  // Transparent background
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_style_pad_top(status_bar_, lvgl_theme->spacing(2), 0);
    lv_obj_set_style_pad_bottom(status_bar_, lvgl_theme->spacing(2), 0);
    lv_obj_set_scrollbar_mode(status_bar_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_layout(status_bar_, LV_LAYOUT_NONE, 0);  // Use absolute positioning
    lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, 0);        // Overlap with top_bar_

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(notification_label_, LV_HOR_RES * 0.75);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(notification_label_, lvgl_theme->text_color(), 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_align(notification_label_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(status_label_, LV_HOR_RES * 0.75);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label_, lvgl_theme->text_color(), 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);
    lv_obj_align(status_label_, LV_ALIGN_CENTER, 0, 0);

#if CONFIG_USE_MULTILINE_CHAT_MESSAGE
    /* Bottom bar - auto height, grows upward with wrapped text */
    bottom_bar_ = lv_obj_create(screen);
    lv_obj_set_width(bottom_bar_, LV_HOR_RES);
    lv_obj_set_height(bottom_bar_, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(bottom_bar_, 0, 0);
    lv_obj_set_style_bg_color(bottom_bar_, lvgl_theme->background_color(), 0);
    lv_obj_set_style_bg_opa(bottom_bar_, LV_OPA_50, 0);
    lv_obj_set_style_text_color(bottom_bar_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_pad_all(bottom_bar_, lvgl_theme->spacing(4), 0);
    lv_obj_set_style_border_width(bottom_bar_, 0, 0);
    lv_obj_set_scrollbar_mode(bottom_bar_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_align(bottom_bar_, LV_ALIGN_BOTTOM_MID, 0, 0);

    /* chat_message_label_ placed in bottom_bar_, multiline wrapped display */
    chat_message_label_ = lv_label_create(bottom_bar_);
    lv_label_set_text(chat_message_label_, "");
    lv_obj_set_width(chat_message_label_, LV_HOR_RES - lvgl_theme->spacing(8));
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(chat_message_label_, lvgl_theme->text_color(), 0);
    lv_obj_align(chat_message_label_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);  // Hide until there is content
#else
    /* Top layer: Bottom bar - seamless borderless text */
    bottom_bar_ = lv_obj_create(screen);
    lv_obj_set_size(bottom_bar_, LV_HOR_RES, text_font->line_height + lvgl_theme->spacing(8));
    lv_obj_set_style_radius(bottom_bar_, 0, 0);
    lv_obj_set_style_bg_opa(bottom_bar_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(bottom_bar_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_border_width(bottom_bar_, 0, 0);
    lv_obj_set_style_pad_all(bottom_bar_, 0, 0);
    lv_obj_set_style_pad_left(bottom_bar_, lvgl_theme->spacing(4), 0);
    lv_obj_set_style_pad_right(bottom_bar_, lvgl_theme->spacing(4), 0);
    lv_obj_set_scrollbar_mode(bottom_bar_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_align(bottom_bar_, LV_ALIGN_BOTTOM_MID, 0, 0);

    /* chat_message_label_ placed in bottom_bar_, single-line horizontal scroll */
    chat_message_label_ = lv_label_create(bottom_bar_);
    lv_label_set_text(chat_message_label_, "");
    lv_obj_set_width(chat_message_label_, LV_HOR_RES - lvgl_theme->spacing(8));
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(chat_message_label_, lvgl_theme->text_color(), 0);
    lv_obj_align(chat_message_label_, LV_ALIGN_CENTER, 0, 0);

    // Start scrolling after a delay (short text won't scroll)
    static lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_delay(&a, 1000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_obj_set_style_anim(chat_message_label_, &a, LV_PART_MAIN);
    lv_obj_set_style_anim_duration(chat_message_label_, lv_anim_speed_clamped(60, 300, 60000),
                                   LV_PART_MAIN);
    lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);  // Hide until there is content
#endif

    low_battery_popup_ = lv_obj_create(screen);
    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, text_font->line_height * 2);
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, -lvgl_theme->spacing(4));
    lv_obj_set_style_bg_color(low_battery_popup_, lvgl_theme->low_battery_color(), 0);
    lv_obj_set_style_radius(low_battery_popup_, lvgl_theme->spacing(4), 0);

    low_battery_label_ = lv_label_create(low_battery_popup_);
    lv_label_set_text(low_battery_label_, Lang::Strings::BATTERY_NEED_CHARGE);
    lv_obj_set_style_text_color(low_battery_label_, lv_color_white(), 0);
    lv_obj_center(low_battery_label_);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);

    // Create custom idle screen
}

void LcdDisplay::SetBrightness(uint8_t brightness) {
    DisplayLockGuard lock(this);
    if (brightness > 100) brightness = 100;

    // Hitung kegelapan overlay pada top layer:
    // 100% brightness = 0% opacity (transparan penuh, terang 100%)
    // 70% brightness  = 30% opacity (kecerahan 70%)
    // 50% brightness  = 50% opacity (redup 50%)
    // 0% brightness   = 100% opacity (gelap total)
    uint8_t dark_opa = (uint8_t)((100 - brightness) * 255 / 100);
    lv_obj_t* top = lv_layer_top();
    if (top != nullptr) {
        if (lv_obj_get_style_bg_opa(top, LV_PART_MAIN) == dark_opa) {
            return;
        }
        lv_obj_set_style_bg_color(top, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(top, dark_opa, 0);
        lv_obj_remove_flag(top, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_invalidate(top);
    }
}

void LcdDisplay::SetPreviewImage_Theme1(std::unique_ptr<LvglImage> image) {
    DisplayLockGuard lock(this);
    if (preview_image_ == nullptr) {
        ESP_LOGE(TAG, "Preview image is not initialized");
        return;
    }

    if (image == nullptr) {
        esp_timer_stop(preview_timer_);
        lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
        preview_image_cached_.reset();
        if (gif_controller_) {
            gif_controller_->Start();
        }
        return;
    }

    preview_image_cached_ = std::move(image);
    auto img_dsc = preview_image_cached_->image_dsc();
    lv_image_set_src(preview_image_, img_dsc);
    if (img_dsc->header.w > 0 && img_dsc->header.h > 0) {
        // Scale to fill screen height (maximize display area)
        int scale_w = 128 * width_ / img_dsc->header.w;
        int scale_h = 128 * height_ / img_dsc->header.h;
        int scale = (scale_w > scale_h) ? scale_w : scale_h;  // Fill screen
        
        // Apply zoom if active (1x to 10x)
        if (zoom_level_ > 1) {
            scale *= zoom_level_;
        }
        
        lv_image_set_scale(preview_image_, scale);
    }

    // Hide emoji_box_
    if (gif_controller_) {
        gif_controller_->Stop();
    }
    lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
    esp_timer_stop(preview_timer_);
    ESP_ERROR_CHECK(esp_timer_start_once(preview_timer_, PREVIEW_IMAGE_DURATION_MS * 1000));
}

void LcdDisplay::ShowCaptureFlash() {
    DisplayLockGuard lock(this);
    
    // Create white flash overlay
    lv_obj_t* flash = lv_obj_create(lv_screen_active());
    lv_obj_set_size(flash, width_, height_);
    lv_obj_set_style_bg_color(flash, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(flash, LV_OPA_80, 0);
    lv_obj_set_style_border_width(flash, 0, 0);
    lv_obj_set_style_radius(flash, 0, 0);
    lv_obj_center(flash);
    
    // Camera icon in center
    lv_obj_t* icon = lv_label_create(flash);
    lv_label_set_text(icon, MATERIAL_SYMBOLS_PHOTO_CAMERA);
    lv_obj_set_style_text_color(icon, lv_color_hex(0x333333), 0);
    lv_obj_set_style_text_font(icon, &font_material_symbols_30_4, 0);
    lv_obj_center(icon);
    
    // Use LVGL timer to remove flash after 250ms
    lv_timer_t* timer = lv_timer_create([](lv_timer_t* t) {
        lv_obj_t* obj = static_cast<lv_obj_t*>(lv_timer_get_user_data(t));
        if (obj && lv_obj_is_valid(obj)) {
            lv_obj_del(obj);
        }
        lv_timer_del(t);
    }, 250, flash);
    lv_timer_set_repeat_count(timer, 1);
}

void LcdDisplay::SetZoomLevel(int level) {
    DisplayLockGuard lock(this);
    zoom_level_ = std::clamp(level, 1, 10);
    is_zoomed_ = (zoom_level_ > 1);
    
    // Update current preview image scale if visible
    if (preview_image_ && !lv_obj_has_flag(preview_image_, LV_OBJ_FLAG_HIDDEN) && preview_image_cached_) {
        auto img_dsc = preview_image_cached_->image_dsc();
        if (img_dsc->header.w > 0 && img_dsc->header.h > 0) {
            int scale_w = 128 * width_ / img_dsc->header.w;
            int scale_h = 128 * height_ / img_dsc->header.h;
            int base_scale = (scale_w > scale_h) ? scale_w : scale_h;
            lv_image_set_scale(preview_image_, base_scale * zoom_level_);
        }
    }
    
    ESP_LOGI(TAG, "Camera zoom level: %dx", zoom_level_);
}

int LcdDisplay::ZoomIn() {
    int new_level = std::min(10, zoom_level_ + 1);
    SetZoomLevel(new_level);
    return zoom_level_;
}

int LcdDisplay::ZoomOut() {
    int new_level = std::max(1, zoom_level_ - 1);
    SetZoomLevel(new_level);
    return zoom_level_;
}

void LcdDisplay::SetZoom(bool zoomed) {
    SetZoomLevel(zoomed ? 2 : 1);
}

void LcdDisplay::SetChatMessage_Theme1(const char* role, const char* content) {
    if (!setup_ui_called_) {
        ESP_LOGW(TAG, "SetChatMessage_Theme1('%s', '%s') called before SetupUI() - message will be lost!",
                 role, content);
    }
    DisplayLockGuard lock(this);
    if (chat_message_label_ == nullptr) {
        if (setup_ui_called_) {
            ESP_LOGW(TAG,
                     "SetChatMessage_Theme1('%s', '%s') failed: chat_message_label_ is nullptr (SetupUI() "
                     "was called but label not created)",
                     role, content);
        }
        return;
    }
    lv_anim_delete(chat_message_label_, nullptr);
    lv_label_set_text(chat_message_label_, content);
    // Show bottom_bar_ only when there is content (and subtitle is not globally hidden)
    if (bottom_bar_ != nullptr) {
        if (content == nullptr || content[0] == '\0') {
            lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
        } else if (!hide_subtitle_) {
            lv_obj_remove_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
        }
    }
#if CONFIG_USE_MULTILINE_CHAT_MESSAGE
    // Re-align bottom_bar_ after text change so it stays anchored to the bottom
    // as its height adapts to the wrapped content.
    if (bottom_bar_ != nullptr) {
        lv_obj_align(bottom_bar_, LV_ALIGN_BOTTOM_MID, 0, 0);
    }
#endif
}

void LcdDisplay::ClearChatMessages_Theme1() {
    DisplayLockGuard lock(this);
    // In non-wechat mode, just clear the chat message label and hide the bar
    if (chat_message_label_ != nullptr) {
        lv_label_set_text(chat_message_label_, "");
    }
    if (bottom_bar_ != nullptr) {
        lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
    }
}

void LcdDisplay::SetupUI() {
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI() called multiple times, skipping duplicate call");
        return;
    }
    if (ui_theme_id_ == 2) {
        SetupUI_Theme2();
    } else {
        SetupUI_Theme1();
    }
}

void LcdDisplay::SetChatMessage(const char* role, const char* content) {
    if (ui_theme_id_ == 2) {
        SetChatMessage_Theme2(role, content);
    } else {
        SetChatMessage_Theme1(role, content);
    }
}

void LcdDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image) {
    if (ui_theme_id_ == 2) {
        SetPreviewImage_Theme2(std::move(image));
    } else {
        SetPreviewImage_Theme1(std::move(image));
    }
}

void LcdDisplay::ClearChatMessages() {
    if (ui_theme_id_ == 2) {
        ClearChatMessages_Theme2();
    } else {
        ClearChatMessages_Theme1();
    }
}

bool LcdDisplay::SetUiThemeId(int theme_id) {
    if (!is_st7789_) {
        ESP_LOGW(TAG, "SetUiThemeId only supported on ST7789 displays");
        return false;
    }
    if (theme_id < 1 || theme_id > 2) {
        return false;
    }
    if (theme_id == ui_theme_id_ && setup_ui_called_) {
        return true;
    }
    DisplayLockGuard lock(this);
    DestroyUI();
    ui_theme_id_ = theme_id;
    Settings settings("display", true);
    settings.SetInt("ui_theme", ui_theme_id_);

    if (ui_theme_id_ == 1) {
        auto t1 = LvglThemeManager::GetInstance().GetTheme("theme_1");
        if (t1 != nullptr) {
            current_theme_ = t1;
            settings.SetString("theme", "theme_1");
        }
    } else if (ui_theme_id_ == 2) {
        std::string saved_theme = settings.GetString("theme_2", "white");
        auto t2 = LvglThemeManager::GetInstance().GetTheme(saved_theme);
        if (!t2) t2 = LvglThemeManager::GetInstance().GetTheme("white");
        if (t2 != nullptr) {
            current_theme_ = t2;
            settings.SetString("theme", t2->name());
        }
    }

    SetupUI();
    UpdateStatusBar(true);
    SetTheme(current_theme_);
    int cur_brightness = settings.GetInt("brightness", 100);
    SetBrightness(cur_brightness);
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(lv_display_get_default());
    ESP_LOGI(TAG, "UI theme switched to Theme %d", ui_theme_id_);
    return true;
}

void LcdDisplay::DestroyUI() {
    if (music_visualizer_active_) {
        StopMusicVisualizer();
    }
    if (music_anim_timer_ != nullptr) {
        lv_timer_delete(music_anim_timer_);
        music_anim_timer_ = nullptr;
    }
    if (music_container_ != nullptr) {
        lv_obj_del(music_container_);
        music_container_ = nullptr;
        dj_icon_label_ = nullptr;
        music_title_label_ = nullptr;
        music_sub_label_ = nullptr;
        music_duration_label_ = nullptr;
        music_progress_bar_ = nullptr;
        for (int i = 0; i < 7; i++) {
            dj_bars_[i] = nullptr;
        }
    }
    if (gif_controller_) {
        gif_controller_->Stop();
        gif_controller_.reset();
    }
    if (preview_image_ != nullptr) {
        lv_obj_del(preview_image_);
        preview_image_ = nullptr;
    }
    if (emoji_box_ != nullptr) {
        lv_obj_del(emoji_box_);
        emoji_box_ = nullptr;
    }
    if (emoji_label_ != nullptr) {
        lv_obj_del(emoji_label_);
        emoji_label_ = nullptr;
    }
    if (emoji_image_ != nullptr) {
        lv_obj_del(emoji_image_);
        emoji_image_ = nullptr;
    }
    if (low_battery_popup_ != nullptr) {
        lv_obj_del(low_battery_popup_);
        low_battery_popup_ = nullptr;
        low_battery_label_ = nullptr;
    }
    if (status_bar_ != nullptr) {
        lv_obj_del(status_bar_);
        status_bar_ = nullptr;
        status_label_ = nullptr;
        notification_label_ = nullptr;
    }
    if (bottom_bar_ != nullptr) {
        lv_obj_del(bottom_bar_);
        bottom_bar_ = nullptr;
    }
    if (side_bar_ != nullptr) {
        lv_obj_del(side_bar_);
        side_bar_ = nullptr;
    }
    if (container_ != nullptr) {
        lv_obj_del(container_);
        container_ = nullptr;
        top_bar_ = nullptr;
        content_ = nullptr;
        network_label_ = nullptr;
        mute_label_ = nullptr;
        battery_label_ = nullptr;
        chat_message_label_ = nullptr;
        avatar_circle_ = nullptr;
    }
    battery_icon_ = nullptr;
    network_icon_ = nullptr;
    setup_ui_called_ = false;
}

// ── Custom Idle Screen (Retro Dashboard - Clean) ──




void LcdDisplay::SetStatus(const char* status) {
    if (music_visualizer_active_ && status != nullptr) {
        // Allow music status updates while visualizer is active, prevent standard state strings (like SPEAKING) from replacing it
        if (strcmp(status, "🎧 DJ PLAYING") != 0 && strncmp(status, "🎵", 3) != 0) {
            return;
        }
    }
    LvglDisplay::SetStatus(status);
}

void LcdDisplay::SetEmotion(const char* emotion) {
    if (music_visualizer_active_) {
        return;
    }
    if (ui_theme_id_ == 2) {
        // Theme 2 uses WeChat chat bubble UI without big center emoji
        return;
    }
    if (!setup_ui_called_) {
        ESP_LOGW(TAG, "SetEmotion('%s') called before SetupUI() - emotion will not be displayed!",
                 emotion);
    }
    if (emoji_image_ == nullptr) {
        if (setup_ui_called_) {
            ESP_LOGW(TAG,
                     "SetEmotion('%s') failed: emoji_image_ is nullptr (SetupUI() was called but "
                     "emoji image not created)",
                     emotion);
        }
        return;
    }

    auto emoji_collection = static_cast<LvglTheme*>(current_theme_)->emoji_collection();
    auto image = emoji_collection != nullptr ? emoji_collection->GetEmojiImage(emotion) : nullptr;
    if (image == nullptr) {
        auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
        const char* utf8 = noto_emoji_get_utf8(emotion);
        const lv_font_t* emotion_font = lvgl_theme->emoji_font()->font();
        if (utf8 == nullptr) {
            utf8 = material_symbols_get_utf8(emotion);
            emotion_font = lvgl_theme->large_icon_font()->font();
        }
        if (utf8 != nullptr && emoji_label_ != nullptr) {
            DisplayLockGuard lock(this);
            if (gif_controller_) {
                gif_controller_->Stop();
                gif_controller_.reset();
            }
            lv_obj_set_style_text_font(emoji_label_, emotion_font, 0);
            lv_label_set_text(emoji_label_, utf8);
            lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    DisplayLockGuard lock(this);
    // Stop any running GIF animation in the same lock scope as setting new image
    // to prevent LVGL from accessing freed image data between operations
    if (gif_controller_) {
        gif_controller_->Stop();
        gif_controller_.reset();
    }
    if (image->IsGif()) {
        // Create new GIF controller
        gif_controller_ = std::make_unique<LvglGif>(image->image_dsc());

        if (gif_controller_->IsLoaded()) {
            // Set up frame update callback
            gif_controller_->SetFrameCallback(
                [this]() { lv_image_set_src(emoji_image_, gif_controller_->image_dsc()); });

            // Set initial frame and start animation
            lv_image_set_src(emoji_image_, gif_controller_->image_dsc());
            gif_controller_->Start();

            // Show GIF, hide others
            lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
        } else {
            ESP_LOGE(TAG, "Failed to load GIF for emotion: %s", emotion);
            gif_controller_.reset();
        }
    } else {
        lv_image_set_src(emoji_image_, image->image_dsc());
        lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
    }
}

void LcdDisplay::SetTheme(Theme* theme) {
    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(theme);

    // Get the screen belonging to container_
    lv_obj_t* screen = (container_ != nullptr) ? lv_obj_get_screen(container_) : lv_screen_active();

    // Set font
    auto text_font = lvgl_theme->text_font()->font();
    auto icon_font = lvgl_theme->icon_font()->font();
    auto large_icon_font = lvgl_theme->large_icon_font()->font();

    if (text_font->line_height >= 40) {
        lv_obj_set_style_text_font(mute_label_, large_icon_font, 0);
        lv_obj_set_style_text_font(battery_label_, large_icon_font, 0);
        lv_obj_set_style_text_font(network_label_, large_icon_font, 0);
    } else {
        lv_obj_set_style_text_font(mute_label_, icon_font, 0);
        lv_obj_set_style_text_font(battery_label_, icon_font, 0);
        lv_obj_set_style_text_font(network_label_, icon_font, 0);
    }

    // Set parent text color and solid background fallback
    lv_obj_set_style_text_font(screen, text_font, 0);
    lv_obj_set_style_text_color(screen, lvgl_theme->text_color(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(screen, lvgl_theme->background_color(), 0);

    // Set background image or solid color (Background image only for Theme 1!)
    if (container_ != nullptr) {
        if (ui_theme_id_ == 1 && lvgl_theme->background_image() != nullptr) {
            lv_obj_set_style_bg_image_src(container_, lvgl_theme->background_image()->image_dsc(), 0);
            lv_obj_set_style_bg_image_opa(container_, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_opa(container_, LV_OPA_TRANSP, 0);
        } else {
            lv_obj_set_style_bg_image_src(container_, nullptr, 0);
            lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(container_, lvgl_theme->background_color(), 0);
        }
    }

    // Update top bar styling based on UI theme
    if (top_bar_ != nullptr) {
        if (ui_theme_id_ == 2) {
            lv_obj_set_style_bg_opa(top_bar_, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(top_bar_, lvgl_theme->background_color(), 0);
            lv_obj_set_style_border_color(top_bar_, lvgl_theme->border_color(), 0);
        } else {
            lv_obj_set_style_bg_opa(top_bar_, LV_OPA_TRANSP, 0);
        }
    }

    // Update status bar elements
    lv_obj_set_style_text_color(network_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_text_color(status_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_text_color(notification_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_text_color(mute_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_text_color(battery_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_text_color(emoji_label_, lvgl_theme->text_color(), 0);

    if (ui_theme_id_ == 2) {
        // Theme 2: WeChat Chat Bubble UI - dynamically adapt content background & existing bubbles
        if (content_ != nullptr) {
            lv_obj_set_style_bg_color(content_, lvgl_theme->chat_background_color(), 0);
            lv_obj_set_style_bg_opa(content_, LV_OPA_COVER, 0);

            // Iterate through all children of content (message containers or bubbles)
            uint32_t child_count = lv_obj_get_child_cnt(content_);
            for (uint32_t i = 0; i < child_count; i++) {
                lv_obj_t* obj = lv_obj_get_child(content_, i);
                if (obj == nullptr)
                    continue;

                lv_obj_t* bubble = nullptr;
                if (lv_obj_get_child_cnt(obj) > 0) {
                    lv_opa_t bg_opa = lv_obj_get_style_bg_opa(obj, LV_PART_MAIN);
                    if (bg_opa == LV_OPA_TRANSP) {
                        bubble = lv_obj_get_child(obj, 0);
                    } else {
                        bubble = obj;
                    }
                } else {
                    continue;
                }

                if (bubble == nullptr || bubble == music_container_)
                    continue;

                void* bubble_type_ptr = lv_obj_get_user_data(bubble);
                if (bubble_type_ptr != nullptr) {
                    const char* bubble_type = static_cast<const char*>(bubble_type_ptr);
                    lv_color_t bubble_bg;

                    if (strcmp(bubble_type, "user") == 0) {
                        bubble_bg = lvgl_theme->user_bubble_color();
                    } else if (strcmp(bubble_type, "assistant") == 0) {
                        bubble_bg = lvgl_theme->assistant_bubble_color();
                    } else if (strcmp(bubble_type, "system") == 0) {
                        bubble_bg = lvgl_theme->system_bubble_color();
                    } else {
                        bubble_bg = lvgl_theme->system_bubble_color();
                    }

                    lv_obj_set_style_bg_color(bubble, bubble_bg, 0);
                    lv_obj_set_style_border_color(bubble, lvgl_theme->border_color(), 0);

                    // Update text color ensuring high contrast (without overwriting YouTube badge on finalized music cards)
                    lv_color_t contrast_text = GetContrastTextColor(bubble_bg);
                    uint32_t bubble_children = lv_obj_get_child_cnt(bubble);
                    if (bubble_children >= 5) {
                        lv_obj_t* title = lv_obj_get_child(bubble, 1);
                        if (title != nullptr) {
                            lv_obj_set_style_text_color(title, contrast_text, 0);
                        }
                        lv_obj_t* dur = lv_obj_get_child(bubble, 4);
                        if (dur != nullptr) {
                            lv_obj_set_style_text_color(dur, contrast_text, 0);
                        }
                        lv_obj_t* footer = lv_obj_get_child(bubble, 5);
                        if (footer != nullptr && lv_obj_get_child_cnt(footer) > 0) {
                            lv_obj_t* time_lbl = lv_obj_get_child(footer, 0);
                            if (time_lbl != nullptr) {
                                lv_obj_set_style_text_color(time_lbl, contrast_text, 0);
                            }
                        }
                    } else {
                        if (bubble_children > 0) {
                            lv_obj_t* text = lv_obj_get_child(bubble, 0);
                            if (text != nullptr) {
                                lv_color_t msg_col = (strcmp(bubble_type, "system") == 0)
                                    ? lvgl_theme->system_text_color()
                                    : contrast_text;
                                lv_obj_set_style_text_color(text, msg_col, 0);
                            }
                        }
                        if (bubble_children > 1) {
                            lv_obj_t* footer = lv_obj_get_child(bubble, 1);
                            if (footer != nullptr && lv_obj_get_child_cnt(footer) > 0) {
                                lv_obj_t* time_lbl = lv_obj_get_child(footer, 0);
                                if (time_lbl != nullptr) {
                                    lv_obj_set_style_text_color(time_lbl, contrast_text, 0);
                                }
                            }
                        }
                    }
                }
            }
        }
        if (music_container_ != nullptr) {
            lv_color_t card_bg = lvgl_theme->assistant_bubble_color();
            lv_color_t text_col = GetContrastTextColor(card_bg);
            lv_obj_set_style_bg_color(music_container_, card_bg, 0);
            lv_obj_set_style_border_color(music_container_, lvgl_theme->border_color(), 0);
            if (music_title_label_ != nullptr) {
                lv_obj_set_style_text_color(music_title_label_, text_col, 0);
            }
            if (music_duration_label_ != nullptr) {
                lv_obj_set_style_text_color(music_duration_label_, text_col, 0);
            }
        }
    } else {
        // Theme 1: Simple UI mode - just update the main chat message
        if (chat_message_label_ != nullptr) {
            lv_obj_set_style_text_color(chat_message_label_, lvgl_theme->text_color(), 0);
        }

        if (emoji_label_ != nullptr) {
            lv_obj_set_style_text_color(emoji_label_, lvgl_theme->text_color(), 0);
        }

        // Update bottom bar background color with 50% opacity and text color
        if (bottom_bar_ != nullptr) {
            lv_obj_set_style_bg_opa(bottom_bar_, LV_OPA_50, 0);
            lv_obj_set_style_bg_color(bottom_bar_, lvgl_theme->background_color(), 0);
            lv_obj_set_style_text_color(bottom_bar_, lvgl_theme->text_color(), 0);
        }
    }

    // Update low battery popup
    lv_obj_set_style_bg_color(low_battery_popup_, lvgl_theme->low_battery_color(), 0);
    lv_obj_invalidate(screen);

    // No errors occurred. Save theme to settings
    Display::SetTheme(lvgl_theme);
}

void LcdDisplay::SetHideSubtitle(bool hide) {
    DisplayLockGuard lock(this);
    hide_subtitle_ = hide;

    // Immediately update UI visibility based on the setting
    if (bottom_bar_ != nullptr) {
        if (hide) {
            lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
        } else {
            // Only show if there is actual content to display
            const char* text =
                (chat_message_label_ != nullptr) ? lv_label_get_text(chat_message_label_) : nullptr;
            if (text != nullptr && text[0] != '\0') {
                lv_obj_remove_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

static void FormatDurationComponent(int total_sec, bool include_hours, char* out, size_t out_size) {
    if (total_sec < 0) {
        total_sec = 0;
    }
    if (include_hours) {
        int h = std::min(total_sec / 3600, 99);
        int m = (total_sec % 3600) / 60;
        int s = total_sec % 60;
        snprintf(out, out_size, "%02d:%02d:%02d", h, m, s);
    } else {
        int m = std::min(total_sec / 60, 59);
        int s = total_sec % 60;
        snprintf(out, out_size, "%02d:%02d", m, s);
    }
}

static void FormatMusicDuration(int current_sec, int total_sec, char* buf, size_t buf_size) {
    if (current_sec < 0) {
        current_sec = 0;
    }
    bool use_hours = (current_sec >= 3600) || (total_sec >= 3600);
    char cur_buf[24];
    FormatDurationComponent(current_sec, use_hours, cur_buf, sizeof(cur_buf));
    if (total_sec > 0) {
        char tot_buf[24];
        FormatDurationComponent(total_sec, use_hours, tot_buf, sizeof(tot_buf));
        snprintf(buf, buf_size, "%s / %s", cur_buf, tot_buf);
    } else {
        snprintf(buf, buf_size, "%s / %s", cur_buf, use_hours ? "--:--:--" : "--:--");
    }
}

void LcdDisplay::CreateMusicVisualizerUI() {
    if (music_container_ != nullptr) {
        return;
    }

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);

    if (ui_theme_id_ == 2) {
        if (content_ == nullptr) {
            return;
        }

        // WhatsApp Chat Column Music Player Bubble inside content_
        uint32_t child_count = lv_obj_get_child_cnt(content_);
        if (child_count >= MAX_MESSAGES) {
            lv_obj_t* first_child = lv_obj_get_child(content_, 0);
            if (first_child != nullptr) {
                lv_obj_del(first_child);
            }
        }

        music_container_ = lv_obj_create(content_);
        lv_coord_t bubble_w = LV_HOR_RES * 85 / 100 - 16;
        lv_obj_set_width(music_container_, bubble_w);
        lv_obj_set_height(music_container_, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(music_container_, 10, 0);

        lv_color_t card_bg = lvgl_theme->assistant_bubble_color();
        lv_color_t text_col = GetContrastTextColor(card_bg);
        lv_obj_set_style_bg_color(music_container_, card_bg, 0);
        lv_obj_set_style_bg_opa(music_container_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(music_container_, 1, 0);
        lv_obj_set_style_border_color(music_container_, lvgl_theme->border_color(), 0);
        lv_obj_set_style_pad_left(music_container_, 10, 0);
        lv_obj_set_style_pad_right(music_container_, 10, 0);
        lv_obj_set_style_pad_top(music_container_, 8, 0);
        lv_obj_set_style_pad_bottom(music_container_, 6, 0);
        lv_obj_set_style_pad_row(music_container_, 4, 0);
        lv_obj_set_scrollbar_mode(music_container_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_flex_flow(music_container_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_user_data(music_container_, (void*)"assistant");

        // YouTube Music Header Badge (Single clean header label without colliding "Playing..." text)
        music_sub_label_ = lv_label_create(music_container_);
        lv_label_set_text(music_sub_label_, "▶ YouTube Music");
        lv_obj_set_style_text_color(music_sub_label_, lv_color_hex(0xE53935), 0); // YouTube Red

        // Title Label with circular marquee
        music_title_label_ = lv_label_create(music_container_);
        lv_obj_set_width(music_title_label_, LV_PCT(100));
        lv_label_set_long_mode(music_title_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_style_text_align(music_title_label_, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_color(music_title_label_, text_col, 0);
        lv_label_set_text(music_title_label_, "YouTube Audio");

        // Spectrum bars container
        lv_obj_t* bars_box = lv_obj_create(music_container_);
        lv_obj_set_size(bars_box, LV_PCT(100), 34);
        lv_obj_set_style_bg_opa(bars_box, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(bars_box, 0, 0);
        lv_obj_set_style_pad_all(bars_box, 0, 0);
        lv_obj_set_scrollbar_mode(bars_box, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_flex_flow(bars_box, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(bars_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(bars_box, 5, 0);

        static const uint32_t kTheme2BarColors[7] = {
            0x07C160, // WhatsApp Green
            0x25D366, // Light WhatsApp Green
            0x00E676, // Spring Neon Green
            0x00B0FF, // Electric Cyan
            0x7C4DFF, // Purple
            0xFFB300, // Amber Yellow
            0xFF5252  // Coral Red
        };

        for (int i = 0; i < 7; i++) {
            dj_bars_[i] = lv_obj_create(bars_box);
            lv_obj_set_size(dj_bars_[i], 9, 8);
            lv_obj_set_style_bg_color(dj_bars_[i], lv_color_hex(kTheme2BarColors[i]), 0);
            lv_obj_set_style_bg_opa(dj_bars_[i], LV_OPA_COVER, 0);
            lv_obj_set_style_radius(dj_bars_[i], 4, 0);
            lv_obj_set_style_border_width(dj_bars_[i], 0, 0);
            lv_obj_set_scrollbar_mode(dj_bars_[i], LV_SCROLLBAR_MODE_OFF);
        }
        dj_icon_label_ = nullptr;

        // Song Progress Bar
        music_progress_bar_ = lv_bar_create(music_container_);
        lv_obj_set_size(music_progress_bar_, LV_PCT(100), 4);
        lv_bar_set_range(music_progress_bar_, 0, 100);
        lv_bar_set_value(music_progress_bar_, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(music_progress_bar_, lv_color_hex(0x555555), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(music_progress_bar_, LV_OPA_30, LV_PART_MAIN);
        lv_obj_set_style_bg_color(music_progress_bar_, lv_color_hex(0xE53935), LV_PART_INDICATOR);
        lv_obj_set_style_radius(music_progress_bar_, 2, 0);

        // WhatsApp-style timestamp footer
        time_t now = time(NULL);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        char time_str[16];
        if (timeinfo.tm_year > (2020 - 1900)) {
            snprintf(time_str, sizeof(time_str), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
        } else {
            int sec = (int)(esp_timer_get_time() / 1000000);
            snprintf(time_str, sizeof(time_str), "%02d:%02d", (sec / 3600) % 24, (sec / 60) % 60);
        }

        // Duration label on its own centered row so "MM:SS / MM:SS" never collides with clock
        music_duration_label_ = lv_label_create(music_container_);
        lv_obj_set_width(music_duration_label_, LV_PCT(100));
        lv_label_set_text(music_duration_label_, "00:00 / --:--");
        lv_obj_set_style_text_font(music_duration_label_, lvgl_theme->text_font()->font(), 0);
        lv_obj_set_style_text_align(music_duration_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(music_duration_label_, text_col, 0);
        lv_obj_set_style_text_opa(music_duration_label_, LV_OPA_90, 0);

        lv_obj_t* footer_row = lv_obj_create(music_container_);
        lv_obj_set_width(footer_row, LV_PCT(100));
        lv_obj_set_height(footer_row, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(footer_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(footer_row, 0, 0);
        lv_obj_set_style_pad_all(footer_row, 0, 0);
        lv_obj_set_flex_flow(footer_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(footer_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_scrollbar_mode(footer_row, LV_SCROLLBAR_MODE_OFF);

        lv_obj_t* time_label = lv_label_create(footer_row);
        lv_label_set_text(time_label, time_str);
        lv_obj_set_style_text_font(time_label, lvgl_theme->text_font()->font(), 0);
        lv_obj_set_style_text_color(time_label, text_col, 0);
        lv_obj_set_style_text_opa(time_label, LV_OPA_60, 0);

        // Align bubble to left (assistant message)
        lv_obj_align(music_container_, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_scroll_to_view_recursive(music_container_, LV_ANIM_ON);
    } else {
        // Theme 1: Default Retro DJ Visualizer
        auto screen = lv_screen_active();
        auto large_icon_font = lvgl_theme->large_icon_font()->font();

        music_container_ = lv_obj_create(screen);
        lv_obj_set_size(music_container_, LV_HOR_RES - 20, LV_SIZE_CONTENT);
        lv_obj_align(music_container_, LV_ALIGN_CENTER, 0, -6);
        lv_obj_set_style_bg_opa(music_container_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(music_container_, 0, 0);
        lv_obj_set_style_pad_all(music_container_, 0, 0);
        lv_obj_set_scrollbar_mode(music_container_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_flex_flow(music_container_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(music_container_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(music_container_, 5, 0);

        // DJ Headphone Icon
        dj_icon_label_ = lv_label_create(music_container_);
        lv_obj_set_style_text_font(dj_icon_label_, large_icon_font, 0);
        lv_obj_set_style_text_color(dj_icon_label_, lv_color_hex(0x7C4DFF), 0);
        lv_label_set_text(dj_icon_label_, MATERIAL_SYMBOLS_HEADPHONES);

        // Spectrum Bars container
        lv_obj_t* bars_box = lv_obj_create(music_container_);
        lv_obj_set_size(bars_box, LV_SIZE_CONTENT, 64);
        lv_obj_set_style_bg_opa(bars_box, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(bars_box, 0, 0);
        lv_obj_set_style_pad_all(bars_box, 0, 0);
        lv_obj_set_scrollbar_mode(bars_box, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_flex_flow(bars_box, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(bars_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(bars_box, 6, 0);

        static const uint32_t kBarColors[7] = {
            0x7C4DFF, // Violet
            0x2979FF, // Electric Blue
            0x00B0FF, // Cyan
            0x00E676, // Spring Neon Green
            0xFFD600, // Electric Yellow
            0xFF6D00, // Neon Orange
            0xFF1744  // Hot Pink / Red
        };

        for (int i = 0; i < 7; i++) {
            dj_bars_[i] = lv_obj_create(bars_box);
            lv_obj_set_size(dj_bars_[i], 12, 16);
            lv_obj_set_style_bg_color(dj_bars_[i], lv_color_hex(kBarColors[i]), 0);
            lv_obj_set_style_bg_opa(dj_bars_[i], LV_OPA_COVER, 0);
            lv_obj_set_style_radius(dj_bars_[i], 6, 0);
            lv_obj_set_style_border_width(dj_bars_[i], 0, 0);
            lv_obj_set_scrollbar_mode(dj_bars_[i], LV_SCROLLBAR_MODE_OFF);
        }

        // Song Progress Bar for Theme 1
        music_progress_bar_ = lv_bar_create(music_container_);
        lv_obj_set_size(music_progress_bar_, LV_HOR_RES - 60, 4);
        lv_bar_set_range(music_progress_bar_, 0, 100);
        lv_bar_set_value(music_progress_bar_, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(music_progress_bar_, lv_color_hex(0x333333), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(music_progress_bar_, LV_OPA_60, LV_PART_MAIN);
        lv_obj_set_style_bg_color(music_progress_bar_, lv_color_hex(0x00E676), LV_PART_INDICATOR);
        lv_obj_set_style_radius(music_progress_bar_, 2, 0);

        // Duration label for Theme 1
        music_duration_label_ = lv_label_create(music_container_);
        lv_obj_set_width(music_duration_label_, LV_PCT(100));
        lv_label_set_text(music_duration_label_, "00:00 / --:--");
        lv_obj_set_style_text_font(music_duration_label_, lvgl_theme->text_font()->font(), 0);
        lv_obj_set_style_text_align(music_duration_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(music_duration_label_, lv_color_hex(0x00E676), 0);
        lv_obj_set_style_text_opa(music_duration_label_, LV_OPA_90, 0);

        music_title_label_ = nullptr;
        music_sub_label_ = nullptr;
    }
}

void LcdDisplay::AnimateMusicVisualizer() {
    if (!music_visualizer_active_ || music_container_ == nullptr) return;

    music_anim_phase_++;

    // Pulse DJ headphone icon color between Electric Violet and Glowing Cyan in Theme 1
    if (dj_icon_label_ != nullptr) {
        if ((music_anim_phase_ / 8) % 2 == 0) {
            lv_obj_set_style_text_color(dj_icon_label_, lv_color_hex(0x7C4DFF), 0);
        } else {
            lv_obj_set_style_text_color(dj_icon_label_, lv_color_hex(0x00B0FF), 0);
        }
    }

    int max_h = (ui_theme_id_ == 2) ? 30 : 64;
    int min_h = (ui_theme_id_ == 2) ? 6 : 12;

    // Dynamic spectrum heights calculation for 7 bars
    for (int i = 0; i < 7; i++) {
        if (dj_bars_[i] == nullptr) continue;

        float t = (float)music_anim_phase_ * 0.22f;
        float wave1 = sinf(t * (1.1f + i * 0.28f) + i * 0.8f);
        float wave2 = cosf(t * (0.75f + (6 - i) * 0.22f) - i * 0.5f);
        int beat_pulse = ((music_anim_phase_ + i * 2) % 10 < 3) ? (ui_theme_id_ == 2 ? 8 : 22) : 0;

        int h = (int)(min_h + (wave1 * 0.5f + 0.5f) * (max_h * 0.55f) + (wave2 * 0.5f + 0.5f) * (max_h * 0.25f) + beat_pulse);
        if (h < min_h) h = min_h;
        if (h > max_h) h = max_h;

        lv_obj_set_height(dj_bars_[i], h);
    }
}

void LcdDisplay::StartMusicVisualizer(const char* title, int total_sec) {
    DisplayLockGuard lock(this);
    if (!setup_ui_called_) return;

    music_visualizer_active_ = true;
    music_total_seconds_ = total_sec;
    music_current_seconds_ = 0;

    // Hide preview image or camera if any
    if (preview_image_ != nullptr) {
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
    }

    // Hide emoji_box_ in Theme 1
    if (emoji_box_ != nullptr) {
        lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    }
    if (emoji_label_ != nullptr) {
        lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
    }

    // In Theme 2: if previous music_container_ was active, finalize it cleanly
    if (ui_theme_id_ == 2 && music_container_ != nullptr) {
        if (music_sub_label_ != nullptr) {
            lv_label_set_text(music_sub_label_, "⏹ YouTube Music");
            lv_obj_set_style_text_opa(music_sub_label_, LV_OPA_60, 0);
        }
        for (int i = 0; i < 7; i++) {
            if (dj_bars_[i] != nullptr) {
                lv_obj_set_height(dj_bars_[i], 4);
                dj_bars_[i] = nullptr;
            }
        }
        music_container_ = nullptr;
        music_title_label_ = nullptr;
        music_sub_label_ = nullptr;
        music_duration_label_ = nullptr;
        music_progress_bar_ = nullptr;
    }

    // Create music visualizer UI if not created yet
    CreateMusicVisualizerUI();

    if (music_container_ != nullptr) {
        lv_obj_remove_flag(music_container_, LV_OBJ_FLAG_HIDDEN);
    }

    // Update title label if available
    if (music_title_label_ != nullptr && title && title[0] != '\0') {
        lv_label_set_text(music_title_label_, title);
    }

    // Initialize progress bar & duration label
    if (music_duration_label_ != nullptr) {
        char buf[64];
        FormatMusicDuration(0, music_total_seconds_, buf, sizeof(buf));
        lv_label_set_text(music_duration_label_, buf);
    }
    if (music_progress_bar_ != nullptr) {
        lv_bar_set_value(music_progress_bar_, 0, LV_ANIM_OFF);
    }

    // Start animation timer if not already created
    if (music_anim_timer_ == nullptr) {
        music_anim_timer_ = lv_timer_create([](lv_timer_t* timer) {
            auto* self = static_cast<LcdDisplay*>(lv_timer_get_user_data(timer));
            if (self) {
                self->AnimateMusicVisualizer();
            }
        }, 60, this);
    } else {
        lv_timer_resume(music_anim_timer_);
    }

    if (ui_theme_id_ == 2) {
        if (title && title[0] != '\0') {
            std::string st = "🎵 " + std::string(title);
            SetStatus(st.c_str());
        } else {
            SetStatus("🎵 YouTube Playing");
        }
        if (music_container_ != nullptr) {
            lv_obj_scroll_to_view_recursive(music_container_, LV_ANIM_ON);
        }
    } else {
        SetStatus("🎧 DJ PLAYING");
        if (title && title[0] != '\0') {
            std::string song_display = "🎵 " + std::string(title);
            SetChatMessage("assistant", song_display.c_str());
        }
    }
}

void LcdDisplay::SetMusicProgress(int current_sec, int total_sec) {
    DisplayLockGuard lock(this);
    if (!music_visualizer_active_ || music_container_ == nullptr) return;

    if (total_sec > 0) {
        music_total_seconds_ = total_sec;
    }
    if (current_sec < 0) {
        current_sec = 0;
    }
    music_current_seconds_ = current_sec;

    char dur_buf[64];
    FormatMusicDuration(current_sec, music_total_seconds_, dur_buf, sizeof(dur_buf));

    if (music_progress_bar_ != nullptr) {
        if (music_total_seconds_ > 0) {
            int pct = (current_sec * 100) / music_total_seconds_;
            if (pct > 100) pct = 100;
            lv_bar_set_value(music_progress_bar_, pct, LV_ANIM_OFF);
        } else {
            lv_bar_set_value(music_progress_bar_, 0, LV_ANIM_OFF);
        }
    }

    if (music_duration_label_ != nullptr) {
        lv_label_set_text(music_duration_label_, dur_buf);
    }
}

void LcdDisplay::StopMusicVisualizer() {
    DisplayLockGuard lock(this);
    if (!music_visualizer_active_) return;

    music_visualizer_active_ = false;

    if (music_anim_timer_ != nullptr) {
        lv_timer_pause(music_anim_timer_);
    }

    if (ui_theme_id_ == 2) {
        // In Theme 2 (WhatsApp Chat): keep the music bubble in the chat log, set status to finished
        if (music_sub_label_ != nullptr) {
            lv_label_set_text(music_sub_label_, "⏹ YouTube Music");
            lv_obj_set_style_text_opa(music_sub_label_, LV_OPA_60, 0);
        }
        for (int i = 0; i < 7; i++) {
            if (dj_bars_[i] != nullptr) {
                lv_obj_set_height(dj_bars_[i], 4);
                dj_bars_[i] = nullptr;
            }
        }
        if (music_progress_bar_ != nullptr && music_total_seconds_ > 0) {
            lv_bar_set_value(music_progress_bar_, 100, LV_ANIM_OFF);
        }
        if (music_duration_label_ != nullptr && music_total_seconds_ > 0) {
            char buf[64];
            FormatMusicDuration(music_total_seconds_, music_total_seconds_, buf, sizeof(buf));
            lv_label_set_text(music_duration_label_, buf);
        }
        music_container_ = nullptr;
        music_title_label_ = nullptr;
        music_sub_label_ = nullptr;
        music_duration_label_ = nullptr;
        music_progress_bar_ = nullptr;
    } else {
        // Theme 1: Hide the overlay container and restore emoji
        if (music_container_ != nullptr) {
            lv_obj_add_flag(music_container_, LV_OBJ_FLAG_HIDDEN);
        }
        if (emoji_box_ != nullptr) {
            lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
        }
        if (emoji_label_ != nullptr) {
            lv_obj_remove_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    SetStatus(Lang::Strings::STANDBY);
    SetEmotion("neutral");
}
