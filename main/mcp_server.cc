/*
 * MCP Server Implementation
 * Reference: https://modelcontextprotocol.io/specification/2024-11-05
 */

#include "mcp_server.h"
#include <esp_app_desc.h>
#include <esp_log.h>
#include <esp_pthread.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

#include <esp_heap_caps.h>
#include "application.h"
#include "board.h"
#include "cjson_utils.h"
#include "display.h"
#include "lcd_display.h"
#include "lvgl_display.h"
#include "lvgl_theme.h"
#include "oled_display.h"
#include "settings.h"
#include "system_info.h"
#include "wifi_board.h"
#include "youtube_audio_poller.h"

#define TAG "MCP"

McpServer::McpServer() {}

McpServer::~McpServer() {
    for (auto tool : tools_) {
        delete tool;
    }
    tools_.clear();
}

#ifdef HAVE_LVGL
namespace {

struct ThemeColorMeta {
    const char* name;
    const char* description;
    const char* fallback_hex;
};

constexpr ThemeColorMeta kThemeColorMeta[] = {
    {"dark", "Hitam (Dark)", "#000000"},
    {"light", "Kuning (Cyber Yellow)", "#FFE600"},
    {"blue", "Biru (Royal Blue)", "#1565C0"},
    {"red", "Merah (Crimson Red)", "#C62828"},
    {"green", "Hijau (Emerald Green)", "#15803D"},
    {"purple", "Ungu (Royal Purple)", "#7E22CE"},
    {"orange", "Oranye (Sunset Orange)", "#EA580C"},
    {"white", "Putih (WeChat Style)", "#F5F5F5"},
    {"wechat", "Putih (WeChat Style)", "#F5F5F5"},
    {"pink", "Merah Muda (Hot Pink)", "#DB2777"},
    {"cyan", "Biru Muda / Toska (Cyan)", "#0097A7"},
    {"gray", "Abu-abu (Gray)", "#4B5563"},
    {"brown", "Coklat (Brown)", "#6D4C41"},
};

struct ColorAliasEntry {
    const char* keyword;
    const char* theme_name;
};

// Ordered from specific multi-word phrases to single words so e.g. "biru muda" beats "biru".
constexpr ColorAliasEntry kColorAliases[] = {
    {"biru muda", "cyan"}, {"biru laut", "cyan"}, {"merah muda", "pink"}, {"abu-abu", "gray"},
    {"abu abu", "gray"},   {"toska", "cyan"},     {"tosca", "cyan"},      {"aqua", "cyan"},
    {"teal", "cyan"},      {"cyan", "cyan"},      {"pink", "pink"},       {"jambu", "pink"},
    {"hitam", "dark"},     {"black", "dark"},     {"gelap", "dark"},      {"dark", "dark"},
    {"putih", "white"},    {"white", "white"},    {"wechat", "white"},    {"kuning", "light"},
    {"yellow", "light"},   {"terang", "light"},   {"light", "light"},     {"biru", "blue"},
    {"blue", "blue"},      {"navy", "blue"},      {"merah", "red"},       {"red", "red"},
    {"crimson", "red"},    {"hijau", "green"},    {"green", "green"},     {"emerald", "green"},
    {"ijo", "green"},      {"ungu", "purple"},    {"purple", "purple"},   {"violet", "purple"},
    {"oranye", "orange"},  {"orange", "orange"},  {"jingga", "orange"},   {"oren", "orange"},
    {"abu", "gray"},       {"gray", "gray"},      {"grey", "gray"},       {"kelabu", "gray"},
    {"coklat", "brown"},   {"cokelat", "brown"},  {"brown", "brown"},
};

std::string TrimLowerAscii(const std::string& s) {
    size_t start = 0;
    size_t end = s.size();
    while (start < end && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    std::string out = s.substr(start, end - start);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

void DescribeActiveThemeColor(Display* display, std::string* theme_name, std::string* hex_color,
                              std::string* description) {
    *theme_name = "dark";
    *hex_color = "#000000";
    *description = "Hitam (Dark)";

    if (!display || !display->GetTheme()) {
        return;
    }

    *theme_name = display->GetTheme()->name();
    *description = *theme_name;
    for (const auto& meta : kThemeColorMeta) {
        if (*theme_name == meta.name) {
            *hex_color = meta.fallback_hex;
            *description = meta.description;
            break;
        }
    }

    if (auto* lvgl_theme = LvglThemeManager::GetInstance().GetTheme(*theme_name)) {
        *hex_color = LvglTheme::FormatHex(lvgl_theme->background_color());
        if (*theme_name == "custom") {
            *description = "Kustom (" + *hex_color + ")";
        }
    }
}

std::string NextPresetThemeName(Display* display) {
    static constexpr const char* kCycleThemes[] = {"blue",   "red",   "green", "purple",
                                                   "orange", "pink",  "cyan",  "gray",
                                                   "brown",  "white", "light", "dark"};
    static constexpr size_t kCycleCount = sizeof(kCycleThemes) / sizeof(kCycleThemes[0]);

    std::string cur_theme = (display && display->GetTheme()) ? display->GetTheme()->name() : "dark";
    for (size_t i = 0; i < kCycleCount; ++i) {
        if (cur_theme == kCycleThemes[i]) {
            return kCycleThemes[(i + 1) % kCycleCount];
        }
    }
    return kCycleThemes[0];
}

std::string ResolveColorThemeName(const std::string& col_lower, Display* display) {
    if (col_lower.empty() || col_lower == "acak" || col_lower == "random" || col_lower == "next" ||
        col_lower == "ganti" || col_lower == "baground" || col_lower == "background" ||
        col_lower == "lain") {
        return NextPresetThemeName(display);
    }

    // Match exact alias first, then phrase substring match.
    for (const auto& entry : kColorAliases) {
        if (col_lower == entry.keyword) {
            return entry.theme_name;
        }
    }
    for (const auto& entry : kColorAliases) {
        if (col_lower.find(entry.keyword) != std::string::npos) {
            return entry.theme_name;
        }
    }
    return col_lower;
}

}  // namespace
#endif

void McpServer::AddCommonTools() {
    // *Important* To speed up the response time, we add the common tools to the beginning of
    // the tools list to utilize the prompt cache.
    // **重要** 为了提升响应速度，我们把常用的工具放在前面，利用 prompt cache 的特性。

    // Backup the original tools list and restore it after adding the common tools.
    auto original_tools = std::move(tools_);
    auto& board = Board::GetInstance();

    // Do not add custom tools here.
    // Custom tools must be added in the board's InitializeTools function.

    AddTool("self.get_device_status",
            "Provides real-time information of the device, including: battery level percent, "
            "charging status (sedang di-cas atau tidak), audio speaker volume, screen brightness, "
            "background color/theme, network status, etc.\n"
            "Use this tool for: \n"
            "1. Checking device status (e.g. status baterai, lagi di-cas atau tidak, warna "
            "background saat ini, volume, wifi)\n"
            "2. As the first step to control device settings.",
            PropertyList(), [&board](const PropertyList& properties) -> ReturnValue {
                return board.GetDeviceStatusJson();
            });

    AddTool(
        "self.get_wake_words",
        "DAPATKAN DAFTAR KATA BANGUN / WAKE WORD / NAMA PANGGILAN PERANGKAT (Get Device Wake "
        "Words):\n"
        "Gunakan tool ini ketika pengguna bertanya: 'kamu bisa dipanggil apa', 'apa kata bangun "
        "kamu', 'wake word kamu apa', 'bisa dipanggil apa aja', 'gimana cara bangunin kamu', 'nama "
        "panggilanmu siapa', 'kamu namanya siapa', atau menanyakan bagaimana cara memanggil "
        "perangkat.\n"
        "Tool ini mengembalikan daftar kata bangun (wake word) resmi yang dapat digunakan pengguna "
        "untuk memanggil XiaoZhi.",
        PropertyList(), [](const PropertyList& properties) -> ReturnValue {
            cJSON* root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "active_wake_word", "Hi Jason");
            cJSON_AddStringToObject(
                root, "supported_wake_words",
                "Hi Jason (Aktif), Jarvis, Alexa, Computer, Hi ESP, Hey Willow, Sophia, Mycroft, "
                "Hey Wanda, Hi Andy, Hey Ivy, Ni Hao Xiao Zhi (你好小智)");
            cJSON_AddStringToObject(
                root, "button_trigger",
                "Tombol BOOT (GPIO 0) dapat ditekan 1x untuk langsung berbicara tanpa wake word.");
            cJSON_AddStringToObject(
                root, "instruction_to_ai",
                "Jelaskan kepada pengguna dengan ramah bahwa kamu sekarang aktif merespons kata "
                "bangun utama 'Hi Jason'. Sebutkan juga bahwa pengguna bisa memanggilmu dengan 'Hi "
                "Jason' atau langsung menekan tombol BOOT untuk berbicara.");

            char* json_str = cJSON_PrintUnformatted(root);
            std::string result(json_str ? json_str : "{}");
            if (json_str)
                free(json_str);
            cJSON_Delete(root);
            return result;
        });

    AddTool("self.get_buttons_guide",
        "PANDUAN LENGKAP FUNGSI TOMBOL FISIK PERANGKAT UNTUK MODE XIAOZHI & MODE CHRONOS (Device Buttons Guide for XiaoZhi & Chronos Modes):\n"
        "Wajib gunakan tool ini SETIAP KALI pengguna bertanya tentang tombol fisik pada perangkat, misalnya: "
        "'tombol ini bisa apa aja', 'fungsi tombol apa aja baik untuk xiaozhi maupun chronos', 'tombol ini buat apa', 'harus di klik berapa kali', "
        "'tombol boot buat apa', 'tombol volume atas buat apa', 'tombol volume bawah buat apa', 'berapa kali klik tombol boot', "
        "'cara pakai tombol di mode jam / chronos', 'cara ganti mode pakai tombol', 'apa guna tombol di alat ini', "
        "'kombinasi tombol apa aja', atau pertanyaan apa pun mengenai tombol fisik dan jumlah kliknya pada mode XiaoZhi maupun Chronos.\n"
        "Tool ini mengembalikan daftar 3 tombol fisik (BOOT GPIO 0, VOL UP GPIO 14, VOL DOWN GPIO 46) dengan aksi lengkap di kedua mode: 1x klik, 2x klik, 3x klik, dan tekan tahan.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            cJSON* root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "title", "Panduan Lengkap Fungsi 3 Tombol Fisik (Mode XiaoZhi & Mode Chronos)");
            cJSON_AddStringToObject(root, "summary", "Perangkat memiliki 3 tombol fisik: 1. BOOT (GPIO 0), 2. Volume Atas / VOL UP (GPIO 14), 3. Volume Bawah / VOL DOWN (GPIO 46). Setiap tombol memiliki fungsi cerdas berbeda tergantung mode aktif (XiaoZhi AI atau Chronos Jam Pintar).");

            cJSON* switch_mode = cJSON_CreateObject();
            cJSON_AddStringToObject(switch_mode, "ke_mode_chronos", "Tekan 3x Tombol BOOT atau 2x Tombol VOL DOWN saat di mode XiaoZhi.");
            cJSON_AddStringToObject(switch_mode, "kembali_ke_xiaozhi", "Tekan 3x Tombol BOOT atau 2x Tombol VOL DOWN saat di mode Chronos.");
            cJSON_AddItemToObject(root, "cara_beralih_mode", switch_mode);

            // 1. Mode XiaoZhi (AI Voice Assistant)
            cJSON* xz = cJSON_CreateObject();
            cJSON_AddStringToObject(xz, "mode_name", "Mode XiaoZhi (Asisten Suara AI)");

            cJSON* xz_b1 = cJSON_CreateObject();
            cJSON_AddStringToObject(xz_b1, "name", "Tombol 1: BOOT (GPIO 0)");
            cJSON* xz_f1 = cJSON_CreateObject();
            cJSON_AddStringToObject(xz_f1, "1x_klik", "Mulai Bicara / Interupsi / Stop: Jika standby, langsung mulai mendengarkan/bicara tanpa kata bangun (Toggle Chat). Jika AI sedang berbicara, interupsi/hentikan suara AI. Jika lagu YouTube sedang diputar, stop lagu.");
            cJSON_AddStringToObject(xz_f1, "2x_klik", "Ganti Tema Tampilan Layar: Beralih seketika antara Thema 1 Robot Mascot (wajah animasi robot) dan Thema 2 WhatsApp Bubble Chat UI.");
            cJSON_AddStringToObject(xz_f1, "3x_klik", "Beralih ke Mode Chronos (Jam Pintar / Smartwatch Companion).");
            cJSON_AddStringToObject(xz_f1, "tekan_tahan", "Reset / Portal Wi-Fi (>3 detik): Membuka hotspot Web Config (192.168.4.1) untuk mengganti Wi-Fi.");
            cJSON_AddItemToObject(xz_b1, "functions", xz_f1);
            cJSON_AddItemToObject(xz, "tombol_boot", xz_b1);

            cJSON* xz_b2 = cJSON_CreateObject();
            cJSON_AddStringToObject(xz_b2, "name", "Tombol 2: Volume Atas / VOL UP (GPIO 14)");
            cJSON* xz_f2 = cJSON_CreateObject();
            cJSON_AddStringToObject(xz_f2, "1x_klik", "Naikkan Volume Speaker +10% (atau Zoom In kamera 1x s/d 10x jika live streaming kamera sedang tampil di layar).");
            cJSON_AddStringToObject(xz_f2, "2x_klik", "Rotasi Layar 180° seketika tanpa perlu restart perangkat.");
            cJSON_AddStringToObject(xz_f2, "tekan_tahan", "Volume Maksimal 100% (MAX).");
            cJSON_AddItemToObject(xz_b2, "functions", xz_f2);
            cJSON_AddItemToObject(xz, "tombol_vol_up", xz_b2);

            cJSON* xz_b3 = cJSON_CreateObject();
            cJSON_AddStringToObject(xz_b3, "name", "Tombol 3: Volume Bawah / VOL DOWN (GPIO 46)");
            cJSON* xz_f3 = cJSON_CreateObject();
            cJSON_AddStringToObject(xz_f3, "1x_klik", "Turunkan Volume Speaker -10% (atau Zoom Out kamera jika live streaming kamera sedang tampil).");
            cJSON_AddStringToObject(xz_f3, "2x_klik", "Beralih cepat ke Mode Chronos (Jam Pintar).");
            cJSON_AddStringToObject(xz_f3, "tekan_tahan", "Mute Suara 0% seketika (Hening).");
            cJSON_AddItemToObject(xz_b3, "functions", xz_f3);
            cJSON_AddItemToObject(xz, "tombol_vol_down", xz_b3);
            cJSON_AddItemToObject(root, "mode_xiaozhi", xz);

            // 2. Mode Chronos (Smartwatch / BLE Companion)
            cJSON* ch = cJSON_CreateObject();
            cJSON_AddStringToObject(ch, "mode_name", "Mode Chronos (Smartwatch / Jam Pintar BLE)");
            cJSON_AddStringToObject(ch, "fitur_alarm", "Jika ada Alarm atau Pengingat berbunyi, menekan tombol mana pun (Tombol 1, 2, atau 3) akan langsung mematikan alarm.");

            cJSON* ch_b1 = cJSON_CreateObject();
            cJSON_AddStringToObject(ch_b1, "name", "Tombol 1: BOOT (GPIO 0)");
            cJSON* ch_f1 = cJSON_CreateObject();
            cJSON_AddStringToObject(ch_f1, "1x_klik", "Ganti Halaman Layar Jam (Pindah antara Jam Utama, Cuaca, Notifikasi HP, Pemutar Musik, dan Info Baterai).");
            cJSON_AddStringToObject(ch_f1, "2x_klik", "Ganti Mode Warna Layar (Beralih antara Tema Gelap / Dark Mode dan Tema Terang / Light Mode).");
            cJSON_AddStringToObject(ch_f1, "3x_klik", "Beralih kembali ke Mode XiaoZhi (Asisten Suara AI).");
            cJSON_AddStringToObject(ch_f1, "tekan_tahan", "Find Phone / Cari Ponsel (1.5 detik): Mengirim sinyal Bluetooth ke smartphone Anda agar ponsel berdering kencang.");
            cJSON_AddItemToObject(ch_b1, "functions", ch_f1);
            cJSON_AddItemToObject(ch, "tombol_boot", ch_b1);

            cJSON* ch_b2 = cJSON_CreateObject();
            cJSON_AddStringToObject(ch_b2, "name", "Tombol 2: Volume Atas / VOL UP (GPIO 14)");
            cJSON* ch_f2 = cJSON_CreateObject();
            cJSON_AddStringToObject(ch_f2, "1x_klik", "Volume +10% & Lagu Selanjutnya (Next Track di pemutar musik HP, atau Tombol Shutter Ambil Foto jika kamera HP aktif).");
            cJSON_AddStringToObject(ch_f2, "2x_klik", "Rotasi Layar 180° secara instan.");
            cJSON_AddStringToObject(ch_f2, "tekan_tahan", "Volume 100% MAX & Play / Pause musik di HP (Music Toggle).");
            cJSON_AddItemToObject(ch_b2, "functions", ch_f2);
            cJSON_AddItemToObject(ch, "tombol_vol_up", ch_b2);

            cJSON* ch_b3 = cJSON_CreateObject();
            cJSON_AddStringToObject(ch_b3, "name", "Tombol 3: Volume Bawah / VOL DOWN (GPIO 46)");
            cJSON* ch_f3 = cJSON_CreateObject();
            cJSON_AddStringToObject(ch_f3, "1x_klik", "Volume -10% & Lagu Sebelumnya (Previous Track di pemutar musik HP).");
            cJSON_AddStringToObject(ch_f3, "2x_klik", "Beralih cepat kembali ke Mode XiaoZhi (Asisten Suara AI).");
            cJSON_AddStringToObject(ch_f3, "tekan_tahan", "Mute Suara 0% seketika.");
            cJSON_AddItemToObject(ch_b3, "functions", ch_f3);
            cJSON_AddItemToObject(ch, "tombol_vol_down", ch_b3);
            cJSON_AddItemToObject(root, "mode_chronos", ch);

            cJSON_AddStringToObject(root, "instruction_to_ai", "Jawab pertanyaan pengguna dengan ramah, lengkap, dan terstruktur dalam Bahasa Indonesia. Jelaskan fungsi tombol secara detail baik pada Mode XiaoZhi maupun Mode Chronos, sebutkan tombol apa dan berapa kali harus diklik (1x klik, 2x klik, 3x klik, atau tekan tahan).");

            char* json_str = cJSON_PrintUnformatted(root);
            std::string result(json_str ? json_str : "{}");
            if (json_str)
                free(json_str);
            cJSON_Delete(root);
            return result;
        });

    AddTool(
        "self.battery.get_status",
        "Dapatkan status baterai saat ini: persentase daya baterai (0-100%), voltase, dan status "
        "apakah sedang di-cas (charging) atau tidak. "
        "Gunakan tool ini ketika pengguna menanyakan status baterai, sisa baterai, persentase "
        "baterai, atau apakah baterai sedang di-cas / charger dicolok atau dicabut (misal 'baterai "
        "lagi di cas gak', 'cek status baterai', 'sisa baterai berapa', 'apakah lagi ngecas').",
        PropertyList(), [&board](const PropertyList& properties) -> ReturnValue {
            int level = 0;
            bool charging = false, discharging = false;
            board.GetBatteryLevel(level, charging, discharging);

            cJSON* root = cJSON_CreateObject();
            cJSON_AddNumberToObject(root, "battery_level_percent", level);
            cJSON_AddBoolToObject(root, "is_charging", charging);
            cJSON_AddStringToObject(root, "charging_status",
                                    charging ? "Sedang di-cas (Charging ⚡)"
                                             : "Tidak di-cas (Menggunakan daya baterai)");

            char* json_str = cJSON_PrintUnformatted(root);
            std::string result(json_str ? json_str : "{}");
            if (json_str)
                free(json_str);
            cJSON_Delete(root);
            return result;
        });

    AddTool("self.audio_speaker.set_volume",
            "Set the volume of the audio speaker. If the current volume is unknown, you must call "
            "`self.get_device_status` tool first and then call this tool.",
            PropertyList({Property("volume", kPropertyTypeInteger, 0, 100)}),
            [&board](const PropertyList& properties) -> ReturnValue {
                auto codec = board.GetAudioCodec();
                codec->SetOutputVolume(properties["volume"].value<int>());
                return true;
            });

    auto backlight = board.GetBacklight();
    if (backlight) {
        AddTool("self.screen.set_brightness", "Set the brightness of the screen.",
                PropertyList({Property("brightness", kPropertyTypeInteger, 0, 100)}),
                [backlight](const PropertyList& properties) -> ReturnValue {
                    uint8_t brightness =
                        static_cast<uint8_t>(properties["brightness"].value<int>());
                    backlight->SetBrightness(brightness, true);
                    return true;
                });
    }

#ifdef HAVE_LVGL
    auto display = board.GetDisplay();
    if (display) {
        AddTool(
            "self.screen.switch_theme",
            "Ganti atau beralih tata letak tema tampilan layar (Thema 1: Tampilan Default Robot "
            "dengan emoji, Thema 2: Tampilan Chat Bubble WeChat seperti pada gambar). "
            "Gunakan tool ini ketika pengguna meminta ganti tema, ubah tema, ganti thema, beralih "
            "tema, ganti ke tema 2, ganti ke tema 1, atau minta tampilkan tema seperti di gambar "
            "(misal 'ganti thema', 'ganti tema', 'ubah tema', 'ganti tema 2', 'pindah ke tema 1', "
            "'pakai tema 2', 'tampilkan tema seperti di gambar'). "
            "PERHATIAN: Pergantian tata letak tema ini HANYA berlaku untuk jenis layar ST7789.\n"
            "Args:\n"
            "  `theme_id`: (Opsional) Angka tema yang diinginkan (1 untuk Thema 1 Default Robot, 2 "
            "untuk Thema 2 Chat Bubble WeChat). Jika 0 atau tidak ditentukan, akan otomatis "
            "beralih ke tema berikutnya.",
            PropertyList({Property("theme_id", kPropertyTypeInteger, 0, 2)}),
            [display](const PropertyList& properties) -> ReturnValue {
                if (!display->IsSt7789()) {
                    return std::string(
                        "Gagal: Pergantian tata letak tema hanya berlaku untuk jenis layar ST7789. "
                        "Layar perangkat saat ini bukan ST7789.");
                }
                int cur = display->GetUiThemeId();
                int target = properties["theme_id"].value<int>();
                if (target <= 0) {
                    target = (cur == 1) ? 2 : 1;
                }
                bool ok = display->SetUiThemeId(target);
                if (ok) {
                    if (target == 2) {
                        return std::string(
                            "Berhasil berganti ke Thema 2 (Tampilan Chat Bubble WeChat seperti di "
                            "gambar). Layar ST7789 aktif.");
                    } else {
                        return std::string(
                            "Berhasil berganti ke Thema 1 (Tampilan Default Robot dengan emoji). "
                            "Layar ST7789 aktif.");
                    }
                }
                return std::string("Gagal mengganti tema layar.");
            });

        AddTool(
            "self.screen.get_theme",
            "Dapatkan dan baca informasi tema layar saat ini secara lengkap: tata letak UI (Thema "
            "1: Default Robot atau Thema 2: Chat Bubble WeChat seperti pada gambar), warna "
            "background/tema aktif (hitam, putih, biru, dll), dan tipe layar (ST7789). "
            "Gunakan tool ini ketika pengguna meminta baca tema saat ini, tema saat ini apa, cek "
            "tema sekarang, tema berapa yang aktif, atau tanya tampilan apa yang sedang digunakan "
            "(misal 'baca thema saat ini', 'tema saat ini apa', 'cek tema', 'tema berapa "
            "sekarang', 'tampilan apa saat ini').",
            PropertyList(), [display](const PropertyList& properties) -> ReturnValue {
                int ui_theme_id = display->GetUiThemeId();
                bool is_st7789 = display->IsSt7789();
                std::string ui_theme_name =
                    (ui_theme_id == 2) ? "Thema 2 (Tampilan Chat Bubble WeChat seperti di gambar)"
                                       : "Thema 1 (Tampilan Default Robot dengan emoji)";

                std::string bg_name;
                std::string hex_color;
                std::string bg_desc;
                DescribeActiveThemeColor(display, &bg_name, &hex_color, &bg_desc);

                cJSON* root = cJSON_CreateObject();
                cJSON_AddNumberToObject(root, "ui_theme_id", ui_theme_id);
                cJSON_AddStringToObject(root, "ui_theme_name", ui_theme_name.c_str());
                cJSON_AddStringToObject(root, "background_color", bg_desc.c_str());
                cJSON_AddStringToObject(root, "background_color_name", bg_name.c_str());
                cJSON_AddStringToObject(root, "hex_color", hex_color.c_str());
                cJSON_AddBoolToObject(root, "is_st7789", is_st7789);
                cJSON_AddStringToObject(root, "display_type", is_st7789 ? "ST7789" : "Non-ST7789");

                std::string summary = "Saat ini perangkat menggunakan " + ui_theme_name +
                                      " dengan warna background " + bg_desc +
                                      (is_st7789 ? " pada layar jenis ST7789." : ".");
                cJSON_AddStringToObject(root, "summary", summary.c_str());

                char* json_str = cJSON_PrintUnformatted(root);
                std::string result(json_str ? json_str : "{}");
                if (json_str)
                    free(json_str);
                cJSON_Delete(root);
                return result;
            });

        AddTool(
            "self.screen.set_theme",
            "GANTI TEMA TAMPILAN / TATA LETAK LAYAR (Theme Layout Mode):\n"
            "- Tema 2 ('2' / 'chat' / 'whatsapp' / 'wechat' / 'gambar'): Mode Tampilan Chat "
            "WhatsApp / WeChat (gelembung balon pesan chat hijau untuk user di kanan, putih untuk "
            "asisten di kiri, avatar kuning di kiri atas, baterai di samping wifi di kanan atas, "
            "persis seperti di gambar).\n"
            "- Tema 1 ('1' / 'robot' / 'default'): Mode Tampilan Robot bawaan (emoji robot besar "
            "di tengah layar dan teks subtitle di bawah).\n"
            "GUNAKAN TOOL INI ketika pengguna meminta: 'ganti tema', 'ganti thema', 'tema 2', "
            "'thema 2', 'ganti ke tema 2', 'tema whatsapp', 'tema seperti di whatsapp', 'tema "
            "seperti di gambar', 'tema chat', 'tema 1', 'ganti ke tema 1', 'tampilan robot'.\n"
            "PERINGATAN: JANGAN gunakan tool ini untuk mengganti warna background! Untuk ganti "
            "warna latar belakang gunakan `self.screen.set_background_color`.",
            PropertyList({Property("theme", kPropertyTypeString)}),
            [display](const PropertyList& properties) -> ReturnValue {
                if (!display->IsSt7789()) {
                    return std::string(
                        "Gagal: Pergantian tata letak tema hanya berlaku untuk jenis layar "
                        "ST7789.");
                }
                auto theme_name = properties["theme"].value<std::string>();
                std::string t_lower = theme_name;
                std::transform(t_lower.begin(), t_lower.end(), t_lower.begin(), ::tolower);

                int target_id = 2;  // Default to Tema 2 (Chat WhatsApp)
                if (t_lower == "1" || t_lower == "tema 1" || t_lower == "thema 1" ||
                    t_lower == "theme 1" || t_lower == "tampilan 1" || t_lower == "default" ||
                    t_lower == "robot" || t_lower == "satu" ||
                    t_lower.find("tema 1") != std::string::npos ||
                    t_lower.find("robot") != std::string::npos) {
                    target_id = 1;
                } else if (t_lower == "switch" || t_lower == "toggle" || t_lower == "ganti" ||
                           t_lower.empty() || t_lower == "ganti tema" || t_lower == "ganti thema" ||
                           t_lower == "next") {
                    target_id = (display->GetUiThemeId() == 1) ? 2 : 1;
                } else {
                    target_id = 2;
                }

                bool ok = display->SetUiThemeId(target_id);
                if (ok) {
                    if (target_id == 2) {
                        return std::string(
                            "Berhasil berganti ke Tema 2 (Tampilan Chat Bubble seperti WhatsApp / "
                            "di gambar).");
                    } else {
                        return std::string(
                            "Berhasil berganti ke Tema 1 (Tampilan Default Robot dengan emoji).");
                    }
                }
                return std::string("Gagal mengganti tema tampilan layar.");
            });

        AddTool(
            "self.screen.set_preset",
            "Atur jenis/preset layar TFT ST7789 atau OLED (misal 'ganti ke layar 1.3 inch', 'ubah "
            "layar ke 240x240', 'atur layar ke 1.69 inch', 'setel display 240x280', 'ganti layar "
            "oled').\\n"
            "Gunakan tool ini ketika pengguna meminta mengganti jenis/ukuran layar yang dipakai.\\n"
            "Perangkat akan otomatis menyimpan pengaturan ke NVS dan me-restart agar resolusi "
            "layar baru aktif.\\n"
            "Args:\\n"
            "  `preset`: Pilihan preset layar:\\n"
            "    - '1.3' atau 'st7789_240x240' (Layar TFT 1.3 inch 240x240)\\n"
            "    - '1.69' atau 'st7789_240x280' (Layar TFT 1.69 inch 240x280)\\n"
            "    - 'oled_128x64' (Layar OLED I2C 0.96 inch 128x64)\\n"
            "Return:\\n"
            "  Pesan konfirmasi pergantian preset layar dan pemberitahuan restart.",
            PropertyList({Property("preset", kPropertyTypeString)}),
            [](const PropertyList& properties) -> ReturnValue {
                std::string p = properties["preset"].value<std::string>();
                std::transform(p.begin(), p.end(), p.begin(), ::tolower);

                std::string target_preset = "st7789_240x240_8p";
                std::string desc = "TFT 1.3 inch (240x240)";

                if (p.find("280") != std::string::npos || p.find("1.69") != std::string::npos) {
                    target_preset = "st7789_240x280_8p";
                    desc = "TFT 1.69 inch (240x280)";
                } else if (p.find("oled") != std::string::npos ||
                           p.find("128") != std::string::npos ||
                           p.find("0.96") != std::string::npos) {
                    target_preset = "oled_128x64";
                    desc = "OLED 0.96 inch (128x64)";
                } else if (p.find("320") != std::string::npos ||
                           p.find("2.0") != std::string::npos ||
                           p.find("2.4") != std::string::npos) {
                    target_preset = "st7789_240x320_8p";
                    desc = "TFT 2.0/2.4 inch (240x320)";
                } else {
                    target_preset = "st7789_240x240_8p";
                    desc = "TFT 1.3 inch (240x240)";
                }

                Settings settings("display", true);
                settings.SetString("preset", target_preset);

                auto disp = Board::GetInstance().GetDisplay();
                if (disp) {
                    disp->ShowNotification("Ganti: " + desc + "...", 2000);
                }

                esp_timer_handle_t timer;
                esp_timer_create_args_t timer_args = {.callback = [](void* arg) { esp_restart(); },
                                                      .name = "restart_timer"};
                esp_timer_create(&timer_args, &timer);
                esp_timer_start_once(timer, 600 * 1000);

                return "Layar berhasil diatur ke " + desc +
                       ". Perangkat akan segera restart untuk menerapkan resolusi baru.";
            });

        AddTool("self.screen.get_presets",
                "Dapatkan informasi jenis layar yang sedang aktif saat ini dan daftar pilihan "
                "layar yang didukung (misal 'cek jenis layar', 'layar apa yang dipakai sekarang', "
                "'resolusi layar saat ini apa').",
                PropertyList(), [](const PropertyList& properties) -> ReturnValue {
                    Settings settings("display", false);
                    std::string cur = settings.GetString("preset", "st7789_240x240_8p");
                    std::string cur_desc = "TFT 1.3 inch (240x240)";
                    if (cur.find("280") != std::string::npos)
                        cur_desc = "TFT 1.69 inch (240x280)";
                    else if (cur.find("oled") != std::string::npos)
                        cur_desc = "OLED 0.96 inch (128x64)";

                    return "Preset layar aktif saat ini: " + cur_desc + " (" + cur +
                           ").\n"
                           "Daftar layar yang didukung:\n"
                           "1. TFT 1.3 inch ST7789 (240x240) -> '1.3' atau 'st7789_240x240'\n"
                           "2. TFT 1.69 inch ST7789 (240x280) -> '1.69' atau 'st7789_240x280'\n"
                           "3. OLED 0.96 inch I2C (128x64) -> 'oled_128x64'\n"
                           "Tips: Anda dapat mengganti layar kapan saja dengan perintah suara "
                           "'ganti layar ke 1.3 inch' atau tekan tombol BOOT 4x.";
                });

        AddTool(
            "self.screen.set_background_color",
            "HANYA UNTUK GANTI WARNA LATAR BELAKANG / BACKGROUND SAJA (misal 'ganti warna "
            "background jadi hitam', 'ubah background jadi biru', 'ganti latar belakang merah'). "
            "Pilihan warna: hitam, putih, biru, merah, hijau, ungu, kuning, oranye, pink, cyan, "
            "abu-abu, coklat, atau kode HEX (#RGB / #RRGGBB). "
            "PERINGATAN: JANGAN GUNAKAN TOOL INI jika pengguna meminta 'ganti tema', 'tema 2', "
            "'thema 2', 'tema whatsapp', atau 'tema chat'!",
            PropertyList({Property("color", kPropertyTypeString)}),
            [display](const PropertyList& properties) -> ReturnValue {
                std::string color = properties["color"].value<std::string>();
                std::string col_lower = TrimLowerAscii(color);

                // Intercept if AI mistakenly calls set_background_color when user asks to switch
                // theme layout
                if (col_lower.find("tema 2") != std::string::npos ||
                    col_lower.find("thema 2") != std::string::npos || col_lower == "2" ||
                    col_lower.find("chat") != std::string::npos ||
                    col_lower.find("whatsapp") != std::string::npos ||
                    col_lower.find("wechat") != std::string::npos ||
                    col_lower.find("gambar") != std::string::npos) {
                    if (display->IsSt7789()) {
                        display->SetUiThemeId(2);
                        return std::string(
                            "Berhasil berganti ke Tema 2 (Tampilan Chat Bubble WhatsApp seperti "
                            "pada gambar).");
                    } else {
                        return std::string(
                            "Gagal: Pergantian tema tampilan hanya berlaku untuk jenis layar "
                            "ST7789.");
                    }
                }
                if (col_lower.find("tema 1") != std::string::npos ||
                    col_lower.find("thema 1") != std::string::npos || col_lower == "1" ||
                    col_lower.find("robot") != std::string::npos) {
                    if (display->IsSt7789()) {
                        display->SetUiThemeId(1);
                        return std::string("Berhasil berganti ke Tema 1 (Tampilan Default Robot).");
                    } else {
                        return std::string(
                            "Gagal: Pergantian tema tampilan hanya berlaku untuk jenis layar "
                            "ST7789.");
                    }
                }
                if (col_lower.find("tema") != std::string::npos ||
                    col_lower.find("thema") != std::string::npos) {
                    if (display->IsSt7789()) {
                        int next_id = (display->GetUiThemeId() == 1) ? 2 : 1;
                        display->SetUiThemeId(next_id);
                        return std::string("Berhasil beralih tema ke Tema ") +
                               std::to_string(next_id) +
                               (next_id == 2
                                    ? " (Tampilan Chat Bubble WhatsApp seperti pada gambar)."
                                    : " (Tampilan Default Robot).");
                    } else {
                        return std::string(
                            "Gagal: Pergantian tema tampilan hanya berlaku untuk jenis layar "
                            "ST7789.");
                    }
                }

                // Pada Thema 1 (Robot): Warna background tidak bisa diubah lewat suara (harus dari
                // assets.bin xiaozhi.me)
                if (display->GetUiThemeId() == 1) {
                    return std::string(
                        "Warna background pada Tema 1 (Tampilan Robot) dikunci oleh aset sistem "
                        "(assets.bin). Untuk mengubah warna atau latar belakang Tema 1, silakan "
                        "gunakan file assets.bin yang di-flash dari xiaozhi.me. Jika ingin "
                        "mengganti warna background dengan suara, silakan beralih ke Tema 2 "
                        "terlebih dahulu (katakan: 'ganti ke tema 2').");
                }

                auto& theme_manager = LvglThemeManager::GetInstance();
                lv_color_t bg_col;
                if (LvglTheme::TryParseColor(col_lower, &bg_col)) {
                    auto custom_theme = theme_manager.GetTheme("custom");
                    if (!custom_theme) {
                        auto dark = theme_manager.GetTheme("dark");
                        custom_theme = new LvglTheme("custom");
                        if (dark) {
                            custom_theme->set_text_font(dark->text_font());
                            custom_theme->set_icon_font(dark->icon_font());
                            custom_theme->set_large_icon_font(dark->large_icon_font());
                            custom_theme->set_emoji_font(dark->emoji_font());
                        }
                        theme_manager.RegisterTheme("custom", custom_theme);
                    }
                    custom_theme->ApplySolidBackground(bg_col);
                    std::string canon_hex = LvglTheme::FormatHex(bg_col);
                    Settings display_settings("display", true);
                    display_settings.SetString("custom_hex", canon_hex);
                    display_settings.SetString("theme_2", "custom");
                    display->SetTheme(custom_theme);
                    return std::string("Warna background layar berhasil diubah ke kode HEX: ") +
                           canon_hex;
                }

                std::string theme_name = ResolveColorThemeName(col_lower, display);
                auto theme = theme_manager.GetTheme(theme_name);
                if (theme != nullptr) {
                    Settings display_settings("display", true);
                    display_settings.SetString("theme_2", theme_name);
                    display->SetTheme(theme);
                    return std::string("Warna background layar berhasil diubah ke: ") + theme_name +
                           " (" + LvglTheme::FormatHex(theme->background_color()) + ")";
                }
                return std::string(
                    "Gagal mengubah warna background. Pilihan: hitam, putih, biru, merah, hijau, "
                    "ungu, kuning, oranye, pink, cyan, abu-abu, coklat, atau #RRGGBB.");
            });

        AddTool("self.screen.get_background_color",
                "Dapatkan status tema dan warna background layar saat ini secara lengkap (tata "
                "letak tema aktif Thema 1 / Thema 2, nama warna, kode HEX). "
                "Gunakan tool ini ketika pengguna menanyakan tema apa atau warna background apa "
                "yang sedang dipakai (misal: 'baca tema saat ini', 'warna background saat ini "
                "apa', 'apa warna layar sekarang', 'cek tema sekarang').",
                PropertyList(), [display](const PropertyList& properties) -> ReturnValue {
                    int ui_theme_id = display->GetUiThemeId();
                    bool is_st7789 = display->IsSt7789();
                    std::string ui_theme_name =
                        (ui_theme_id == 2)
                            ? "Thema 2 (Tampilan Chat Bubble WhatsApp seperti di gambar)"
                            : "Thema 1 (Tampilan Default Robot dengan emoji)";

                    std::string theme_name;
                    std::string hex_color;
                    std::string color_description;
                    DescribeActiveThemeColor(display, &theme_name, &hex_color, &color_description);

                    cJSON* root = cJSON_CreateObject();
                    cJSON_AddNumberToObject(root, "ui_theme_id", ui_theme_id);
                    cJSON_AddStringToObject(root, "ui_theme_name", ui_theme_name.c_str());
                    cJSON_AddStringToObject(root, "current_color", color_description.c_str());
                    cJSON_AddStringToObject(root, "theme_name", theme_name.c_str());
                    cJSON_AddStringToObject(root, "hex_color", hex_color.c_str());
                    cJSON_AddBoolToObject(root, "is_st7789", is_st7789);

                    std::string summary = "Saat ini perangkat menggunakan " + ui_theme_name +
                                          " dengan warna background " + color_description +
                                          (is_st7789 ? " pada layar jenis ST7789." : ".");
                    cJSON_AddStringToObject(root, "summary", summary.c_str());

                    char* json_str = cJSON_PrintUnformatted(root);
                    std::string result(json_str ? json_str : "{}");
                    if (json_str)
                        free(json_str);
                    cJSON_Delete(root);
                    return result;
                });

        AddTool("self.screen.fix_display_color",
                "Perbaiki masalah warna tampilan layar fisik ST7789 yang tertukar (merah jadi biru "
                "/ RGB-BGR) atau warna terbalik/negatif, lalu restart perangkat agar pengaturan "
                "baru diterapkan. "
                "Args:\n"
                "  `mode`: 'swap_rb' (tukar merah-biru / toggle BGR-RGB), 'invert' (balikkan warna "
                "negatif/normal), atau 'reset' (kembalikan ke default board RGB + invert aktif).",
                PropertyList({Property("mode", kPropertyTypeString)}),
                [](const PropertyList& properties) -> ReturnValue {
                    std::string mode = TrimLowerAscii(properties["mode"].value<std::string>());
                    Settings settings("display", true);
                    std::string desc;

                    if (mode.find("reset") != std::string::npos ||
                        mode.find("default") != std::string::npos ||
                        mode.find("normal") != std::string::npos) {
                        settings.SetInt("bgr_order", 0);
                        settings.SetInt("invert_color", 1);
                        desc =
                            "Pengaturan warna panel dikembalikan ke default (RGB + invert aktif)";
                    } else if (mode.find("swap") != std::string::npos ||
                               mode.find("bgr") != std::string::npos ||
                               mode.find("rgb") != std::string::npos ||
                               mode.find("rb") != std::string::npos ||
                               mode.find("merah") != std::string::npos ||
                               mode.find("biru") != std::string::npos ||
                               mode.find("tukar") != std::string::npos) {
                        int cur_bgr = settings.GetInt("bgr_order", -1);
                        int next_bgr = (cur_bgr == 1) ? 0 : 1;
                        settings.SetInt("bgr_order", next_bgr);
                        desc = std::string("Urutan subpiksel panel diubah ke ") +
                               (next_bgr ? "BGR" : "RGB");
                    } else if (mode.find("invert") != std::string::npos ||
                               mode.find("negatif") != std::string::npos ||
                               mode.find("negative") != std::string::npos ||
                               mode.find("inverse") != std::string::npos) {
                        int cur_invert = settings.GetInt("invert_color", -1);
                        int next_invert = (cur_invert < 0) ? 0 : (cur_invert ? 0 : 1);
                        settings.SetInt("invert_color", next_invert);
                        desc = std::string("Mode invert warna panel diubah ke ") +
                               (next_invert ? "AKTIF" : "NONAKTIF");
                    } else {
                        return std::string(
                            "Mode tidak dikenal. Gunakan: 'swap_rb', 'invert', atau 'reset'.");
                    }

                    auto disp = Board::GetInstance().GetDisplay();
                    if (disp) {
                        disp->ShowNotification("Restart: Kalibrasi warna...", 2000);
                    }

                    esp_timer_handle_t timer;
                    esp_timer_create_args_t timer_args = {
                        .callback = [](void* arg) { esp_restart(); }, .name = "color_fix_restart"};
                    esp_timer_create(&timer_args, &timer);
                    esp_timer_start_once(timer, 600 * 1000);

                    return desc +
                           ". Perangkat akan segera restart untuk menerapkan koreksi warna layar.";
                });

        AddTool("self.screen.rotate_180",
                "Putar posisi/orientasi tampilan layar display 180 derajat (membalik tampilan "
                "layar atas-bawah). "
                "Gunakan tool ini ketika pengguna meminta: 'putar layar', 'putar layar 180 "
                "derajat', 'balik layar', 'layar terbalik', 'rotasi layar', atau membalikkan "
                "orientasi tampilan layar. "
                "Status orientasi layar otomatis disimpan permanen ke memori (NVS) sehingga tetap "
                "tersimpan setelah restart/reboot.\n"
                "Return:\n"
                "  Konfirmasi status sudut orientasi layar saat ini (0 derajat atau 180 derajat).",
                PropertyList(), [&board](const PropertyList& properties) -> ReturnValue {
                    int new_rot = board.RotateDisplay();
                    if (new_rot >= 0) {
                        return "Tampilan layar berhasil diputar ke " + std::to_string(new_rot) +
                               " derajat dan disimpan permanen.";
                    }
                    return std::string(
                        "Gagal memutar layar: fitur rotasi layar tidak didukung pada hardware "
                        "display perangkat ini.");
                });
    }
#endif

    AddTool(
        "self.check_music",
        "CARI & PERIKSA INFO LAGU YOUTUBE (Search & Check YouTube Song Info):\n"
        "WAJIB DIPANGGIL PERTAMA KALI setiap kali pengguna meminta memutar lagu/musik (misal: "
        "'putar lagu [judul]', 'mainkan lagu [artis]', 'setel musik di youtube', 'play lagu', "
        "'dengarkan musik', dll).\n"
        "Tool ini akan memeriksa ketersediaan lagu di YouTube, mengambil judul resmi video dan "
        "durasinya.\n"
        "PANDUAN ALUR (SANGAT PENTING):\n"
        "1. Panggil tool ini terlebih dahulu untuk mendapatkan judul dan info lagu dari YouTube.\n"
        "2. SETELAH MENDAPATKAN INFO LAGU, KAMU WAJIB MENANYAKAN KONFIRMASI KEPADA PENGGUNA "
        "TERLEBIH DAHULU!\n"
        "   Contoh: 'Saya menemukan lagu \"[Judul Lagu]\" di YouTube (durasi [Durasi]). Apakah "
        "kamu ingin saya memutarnya sekarang?'\n"
        "3. JANGAN LANGSUNG MEMUTAR LAGU sebelum pengguna memberikan konfirmasi persetujuan "
        "(seperti 'ya', 'yes', 'putar', 'lanjut', 'oke', 'iya')!\n"
        "4. Hanya setelah pengguna mengonfirmasi (menjawab Ya/Putar), kamu baru memanggil tool "
        "`self.play_music`.\n"
        "Args:\n"
        "  `query`: Judul lagu, nama artis/penyanyi, atau kata kunci pencarian YouTube.",
        PropertyList({Property("query", kPropertyTypeString)}),
        [](const PropertyList& properties) -> ReturnValue {
            auto query = properties["query"].value<std::string>();
            std::string video_id, title, duration;
            bool found =
                YouTubeAudioPoller::GetInstance().SearchSong(query, video_id, title, duration);
            if (!found) {
                return "Lagu dengan pencarian '" + query +
                       "' tidak ditemukan di YouTube. Silakan tanyakan kepada pengguna apakah "
                       "ingin mencari judul lain.";
            }
            std::string res =
                "Lagu ditemukan di YouTube!\n- Judul: " + title + "\n- Video ID: " + video_id;
            if (!duration.empty()) {
                res += "\n- Durasi: " + duration;
            }
            res +=
                "\n\nINSTRUKSI WAJIB: JANGAN langsung memutar lagu ini! Sekarang tanyakan "
                "konfirmasi kepada pengguna: sebutkan judul lagunya (\"" +
                title + "\")" + (duration.empty() ? "" : " dan durasi (" + duration + ")") +
                ", lalu tanyakan apakah pengguna ingin memutarnya. Tunggu jawaban pengguna. Jika "
                "pengguna menjawab 'ya'/'putar'/'lanjut', panggil tool self.play_music.";
            return res;
        });

    AddTool(
        "self.play_music",
        "PUTAR LAGU YOUTUBE SETELAH DIKONFIRMASI (Play Confirmed YouTube Song):\n"
        "Panggil tool ini HANYA SETELAH pengguna mengonfirmasi (menjawab 'ya', 'putar', 'lanjut', "
        "'yes', 'oke', 'mainkan') setelah sebelumnya kamu menanyakan konfirmasi lagu.\n"
        "JANGAN memanggil tool ini pada permintaan pertama sebelum pengguna mengonfirmasi "
        "persetujuan.\n"
        "Tool ini akan langsung memulai pemutaran audio lagu ke speaker perangkat.\n"
        "Args:\n"
        "  `query`: (Opsional) Judul lagu atau video_id yang dikonfirmasi. Jika kosong, akan "
        "memutar lagu yang baru saja dicek.",
        PropertyList({Property("query", kPropertyTypeString, std::string(""))}),
        [](const PropertyList& properties) -> ReturnValue {
            auto& yt = YouTubeAudioPoller::GetInstance();
            auto query = properties["query"].value<std::string>();

            // Jika belum ada lagu yang diperiksa / pending konfirmasi:
            if (!yt.HasPendingSong()) {
                if (!query.empty()) {
                    std::string video_id, title, duration;
                    bool found = yt.SearchSong(query, video_id, title, duration);
                    if (found) {
                        return "PERINGATAN: JANGAN langsung memutar lagu ini! Kamu belum "
                               "menanyakan konfirmasi kepada pengguna. Lagu ditemukan: \"" +
                               title + "\"" +
                               (duration.empty() ? "" : " (durasi " + duration + ")") +
                               ". Sekarang tanyakan kepada pengguna: 'Saya menemukan lagu " +
                               title +
                               ". Apakah kamu ingin saya memutarnya sekarang?' dan TUNGGU "
                               "konfirmasi persetujuan pengguna (seperti 'ya'/'putar'/'lanjut') "
                               "sebelum memanggil self.play_music lagi.";
                    } else {
                        return "Lagu '" + query +
                               "' tidak ditemukan di YouTube. Silakan tanyakan kepada pengguna "
                               "apakah ingin mencari judul lain.";
                    }
                }
                return "Belum ada lagu yang diperiksa. Silakan gunakan self.check_music terlebih "
                       "dahulu untuk mencari lagu dan meminta konfirmasi persetujuan pengguna.";
            }

            if (!query.empty()) {
                yt.PlaySong("", query);
            } else {
                yt.PlaySong("", "");
            }
            return "Baik, lagu sedang diputar sekarang.";
        });

    AddTool("self.stop_music",
            "HENTIKAN / STOP PEMUTARAN MUSIK YOUTUBE (Stop Music):\n"
            "Gunakan tool ini ketika pengguna meminta: 'stop lagu', 'hentikan lagu', 'matikan "
            "musik', 'berhenti memutar lagu', 'stop musik', atau 'hentikan pemutaran musik'.",
            PropertyList(), [](const PropertyList& properties) -> ReturnValue {
                YouTubeAudioPoller::GetInstance().Abort();
                auto display = Board::GetInstance().GetDisplay();
                if (display) {
                    display->StopMusicVisualizer();
                }
                return "Pemutaran musik telah dihentikan.";
            });

    auto camera = board.GetCamera();
    if (camera) {
        AddTool("self.camera.take_photo",
                "Always remember you have a camera. If the user asks you to see something, use "
                "this tool to take a photo and then explain it.\n"
                "Args:\n"
                "  `question`: The question that you want to ask about the photo.\n"
                "Return:\n"
                "  A JSON object that provides the photo information.",
                PropertyList({Property("question", kPropertyTypeString)}),
                [camera](const PropertyList& properties) -> ReturnValue {
                    {
                        // Lower priority only during camera hardware capture
                        TaskPriorityReset priority_reset(1);
                        if (!camera->Capture()) {
                            throw std::runtime_error("Failed to capture photo");
                        }
                    }
                    auto question = properties["question"].value<std::string>();
                    return camera->Explain(question);
                });

        AddTool("self.camera.start_streaming",
                "Start camera streaming to the display. The camera feed will be shown on the TFT "
                "screen in real-time.\\n"
                "The user can still talk to XiaoZhi normally while streaming is active.\\n"
                "Use this when the user wants to see what the camera sees.\\n"
                "Return:\\n"
                "  A message confirming streaming has started.",
                PropertyList(std::vector<Property>()),
                [camera](const PropertyList& properties) -> ReturnValue {
                    if (camera->IsStreaming()) {
                        return std::string("Camera streaming is already active.");
                    }
                    if (camera->StartStreaming()) {
                        return std::string(
                            "Camera streaming started. You can now see the camera feed on the "
                            "display.");
                    } else {
                        throw std::runtime_error("Failed to start camera streaming");
                    }
                });

        AddTool(
            "self.camera.stop_streaming",
            "Stop camera streaming. The display will return to normal mode.\\n"
            "Use this when the user wants to stop seeing the camera feed.\\n"
            "Return:\\n"
            "  A message confirming streaming has stopped.",
            PropertyList(std::vector<Property>()),
            [camera](const PropertyList& properties) -> ReturnValue {
                if (!camera->IsStreaming()) {
                    return std::string("Camera streaming is not active.");
                }
                camera->StopStreaming();
                return std::string("Camera streaming stopped. Display returned to normal mode.");
            });

        AddTool("self.camera.get_streaming_resolutions",
                "Dapatkan daftar resolusi kamera yang didukung untuk live streaming dan resolusi "
                "streaming yang sedang aktif saat ini.\\n"
                "Gunakan tool ini ketika pengguna bertanya tentang resolusi kamera apa saja yang "
                "didukung, menanyakan pilihan resolusi streaming, atau menanyakan resolusi yang "
                "sedang aktif.\\n"
                "Maksimal resolusi streaming adalah VGA (640x480).\\n"
                "Return:\\n"
                "  Daftar resolusi streaming yang didukung beserta status resolusi saat ini.",
                PropertyList(std::vector<Property>()),
                [camera](const PropertyList& properties) -> ReturnValue {
                    return camera->GetSupportedStreamingResolutions();
                });

        AddTool("self.camera.set_streaming_resolution",
                "Ubah resolusi live streaming kamera.\\n"
                "Gunakan tool ini ketika pengguna meminta mengubah, menaikkan, atau memilih "
                "resolusi streaming kamera.\\n"
                "Pilihan resolusi streaming yang didukung:\\n"
                "- 'qvga' (320x240, default streaming)\\n"
                "- 'vga' (640x480, resolusi maksimal streaming)\\n"
                "PENTING: Maksimal resolusi streaming dibatasi hingga VGA (640x480). Resolusi di "
                "atas VGA tidak didukung.\\n"
                "Resolusi untuk mengambil foto (self.camera.take_photo) selalu tetap dan wajib "
                "QVGA (320x240).\\n"
                "Args:\\n"
                "  `resolution`: Pilihan resolusi streaming ('qvga' atau 'vga').\\n"
                "Return:\\n"
                "  Pesan konfirmasi keberhasilan atau penjelasan jika gagal.",
                PropertyList({Property("resolution", kPropertyTypeString)}),
                [camera](const PropertyList& properties) -> ReturnValue {
                    auto res = properties["resolution"].value<std::string>();
                    if (camera->SetStreamingResolution(res)) {
                        return "Resolusi streaming berhasil diubah ke: " +
                               camera->GetStreamingResolution();
                    } else {
                        return "Gagal mengubah resolusi streaming. Resolusi yang diminta tidak "
                               "didukung atau melebihi batas maksimal VGA (640x480). Pilihan yang "
                               "tersedia: qvga (320x240, default) dan vga (640x480, maksimal).";
                    }
                });

        AddTool("self.camera.zoom_in",
                "Zoom in tampilan kamera (perbesar 1 tingkat). Maksimal zoom hingga 10x.\\n"
                "Hanya berfungsi ketika streaming kamera aktif di layar.\\n"
                "Gunakan tool ini ketika pengguna meminta memperbesar tampilan kamera (zoom in).\\n"
                "Return:\\n"
                "  Konfirmasi status zoom kamera.",
                PropertyList(std::vector<Property>()),
                [camera](const PropertyList& properties) -> ReturnValue {
                    if (!camera->IsStreaming()) {
                        return std::string(
                            "Zoom hanya bisa digunakan saat live streaming aktif. Silakan mulai "
                            "streaming kamera terlebih dahulu.");
                    }
                    auto display = dynamic_cast<LcdDisplay*>(Board::GetInstance().GetDisplay());
                    if (display) {
                        int level = display->ZoomIn();
                        return "Kamera di-zoom in ke " + std::to_string(level) +
                               "x (maksimal 10x).";
                    }
                    return std::string("Gagal mengubah zoom tampilan.");
                });

        AddTool(
            "self.camera.zoom_out",
            "Zoom out tampilan kamera (perkecil 1 tingkat). Minimal hingga 1x (normal view).\\n"
            "Hanya berfungsi ketika streaming kamera aktif di layar.\\n"
            "Gunakan tool ini ketika pengguna meminta memperkecil tampilan kamera (zoom out).\\n"
            "Return:\\n"
            "  Konfirmasi status zoom kamera.",
            PropertyList(std::vector<Property>()),
            [camera](const PropertyList& properties) -> ReturnValue {
                if (!camera->IsStreaming()) {
                    return std::string(
                        "Zoom hanya bisa digunakan saat live streaming aktif. Silakan mulai "
                        "streaming kamera terlebih dahulu.");
                }
                auto display = dynamic_cast<LcdDisplay*>(Board::GetInstance().GetDisplay());
                if (display) {
                    int level = display->ZoomOut();
                    return "Kamera di-zoom out ke " + std::to_string(level) + "x (minimal 1x).";
                }
                return std::string("Gagal mengubah zoom tampilan.");
            });

        AddTool("self.camera.set_mirror",
                "Atur mode cermin kamera (mirror horizontal).\n"
                "Gunakan tool ini ketika pengguna meminta mengaktifkan atau menonaktifkan mode "
                "cermin kamera (misal 'ubah jadi cermin', 'aktifkan mode cermin', 'matikan mode "
                "cermin', 'jangan cermin').\n"
                "Status disimpan secara permanen ke memori (NVS) sehingga tetap tersimpan setelah "
                "restart/reboot.\n"
                "Args:\n"
                "  `enabled`: true untuk mengaktifkan mode cermin (seperti cermin selfie), false "
                "untuk mode normal (teks terbaca normal).\n"
                "Return:\n"
                "  Konfirmasi status mode cermin kamera.",
                PropertyList({Property("enabled", kPropertyTypeBoolean)}),
                [camera](const PropertyList& properties) -> ReturnValue {
                    bool enabled = properties["enabled"].value<bool>();
                    if (camera->SetHMirror(enabled)) {
                        Settings settings("camera", true);
                        settings.SetInt("hmirror", enabled ? 1 : 0);
                        return std::string("Mode cermin kamera berhasil diubah ke: ") +
                               (enabled ? "Aktif (Cermin ON)" : "Nonaktif (Normal / Cermin OFF)") +
                               " dan disimpan permanen.";
                    }
                    return std::string("Gagal mengubah mode cermin kamera.");
                });

        AddTool("self.camera.rotate_180",
                "Putar posisi/orientasi kamera 180 derajat (membalik tampilan kamera atas-bawah "
                "dan kiri-kanan). "
                "Gunakan tool ini jika posisi kamera terbalik atau pengguna meminta: 'putar "
                "kamera', 'putar kamera 180 derajat', 'balik kamera', 'kamera terbalik', atau "
                "membalik orientasi kamera. "
                "Status orientasi kamera otomatis disimpan permanen ke memori (NVS) sehingga tetap "
                "tersimpan setelah restart/reboot.\n"
                "Return:\n"
                "  Konfirmasi status orientasi kamera setelah diputar 180 derajat.",
                PropertyList(), [camera](const PropertyList& properties) -> ReturnValue {
                    bool new_hmirror = !camera->IsHMirror();
                    bool new_vflip = !camera->IsVFlip();
                    camera->SetHMirror(new_hmirror);
                    camera->SetVFlip(new_vflip);
                    Settings settings("camera", true);
                    settings.SetInt("hmirror", new_hmirror ? 1 : 0);
                    settings.SetInt("vflip", new_vflip ? 1 : 0);
                    return "Posisi kamera berhasil diputar 180 derajat (H-Mirror: " +
                           std::string(new_hmirror ? "ON" : "OFF") +
                           ", V-Flip: " + std::string(new_vflip ? "ON" : "OFF") +
                           ") dan disimpan permanen.";
                });

        AddTool("self.camera.set_vflip",
                "Atur pembalikan vertikal kamera (flip atas-bawah).\n"
                "Gunakan tool ini ketika pengguna meminta membalik tampilan kamera atas-bawah.\n"
                "Status disimpan secara permanen ke memori (NVS) sehingga tetap tersimpan setelah "
                "restart/reboot.\n"
                "Args:\n"
                "  `enabled`: true untuk membalik vertikal (flip atas-bawah), false untuk normal.\n"
                "Return:\n"
                "  Konfirmasi status flip vertikal kamera.",
                PropertyList({Property("enabled", kPropertyTypeBoolean)}),
                [camera](const PropertyList& properties) -> ReturnValue {
                    bool enabled = properties["enabled"].value<bool>();
                    if (camera->SetVFlip(enabled)) {
                        Settings settings("camera", true);
                        settings.SetInt("vflip", enabled ? 1 : 0);
                        return std::string("Flip vertikal kamera berhasil diubah ke: ") +
                               (enabled ? "Aktif (VFlip ON)" : "Nonaktif (VFlip OFF)") +
                               " dan disimpan permanen.";
                    }
                    return std::string("Gagal mengubah flip vertikal kamera.");
                });
    }

    AddTool(
        "self.wifi.reset",
        "Enter Wi-Fi configuration / reset mode. Use this tool when the user asks to reset Wi-Fi, "
        "change Wi-Fi, change SSID, delete Wi-Fi credentials, or reconfigure Wi-Fi (e.g. 'tolong "
        "hapus wifi', 'ganti wifi', 'ganti ssid', 'coba masuk ke konfigurasi wifi').\n"
        "IMPORTANT:\n"
        "1. When the user asks to reset or change Wi-Fi (e.g. 'tolong hapus wifi', 'ganti wifi', "
        "'ganti ssid', 'masuk ke konfigurasi wifi'), you MUST ALWAYS ask for confirmation first by "
        "saying 'Apakah Anda yakin ingin masuk ke mode konfigurasi Wi-Fi? Jawab ya atau yes untuk "
        "konfirmasi, atau batal untuk membatalkan.' and DO NOT call this tool yet.\n"
        "2. Only call this tool when the user explicitly confirms (e.g. 'yes', 'ya', 'konfirmasi', "
        "'setuju').\n"
        "3. If the user cancels (e.g. 'batal', 'batalkan', 'enggak jadi', 'tidak'), do NOT call "
        "this tool and simply reply that the action was canceled.",
        PropertyList(), [](const PropertyList& properties) -> ReturnValue {
            auto& board = Board::GetInstance();
            auto wifi_board = dynamic_cast<WifiBoard*>(&board);
            if (wifi_board != nullptr) {
                wifi_board->EnterWifiConfigMode();
                return std::string("Memasuki mode konfigurasi Wi-Fi...");
            }
            return std::string("Gagal: Papan tidak mendukung mode konfigurasi Wi-Fi.");
        });

    AddTool("self.get_hardware_specs",
            "Dapatkan spesifikasi lengkap perangkat keras (hardware) dari alat ini (chip ESP32-S3, "
            "RAM/PSRAM, Flash, sensor kamera, layar LCD/OLED, audio mic & speaker, baterai, dan "
            "pinout). "
            "Gunakan tool ini ketika pengguna menanyakan spesifikasi hardware, komponen perangkat, "
            "chip/prosesor apa yang dipakai, RAM, memori, kamera, layar, atau pinout (misal "
            "'spesifikasi alat ini apa aja', 'alat ini pakai chip apa', 'spek hardware', 'berapa "
            "RAM dan memorinya', 'kamera apa yang dipakai', 'layar apa yang dipakai').",
            PropertyList(), [](const PropertyList& properties) -> ReturnValue {
                auto& board = Board::GetInstance();
                int batt_level = 0;
                bool charging = false, discharging = false;
                board.GetBatteryLevel(batt_level, charging, discharging);

                size_t free_heap = esp_get_free_heap_size();
                size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

                cJSON* root = cJSON_CreateObject();
                cJSON_AddStringToObject(root, "device_name", "XiaoZhi AI Smart Assistant");
                cJSON_AddStringToObject(root, "board_sku", BOARD_NAME);
                cJSON_AddStringToObject(root, "chip",
                                        "ESP32-S3 (Xtensa Dual-Core 32-bit LX7 @ 240 MHz, Wi-Fi "
                                        "2.4GHz 802.11 b/g/n & Bluetooth 5 BLE)");
                cJSON_AddStringToObject(root, "flash_memory", "16 MB Flash (Quad/Octal SPI)");
                cJSON_AddStringToObject(root, "psram_memory", "8 MB Octal SPI PSRAM (80 MHz)");
                cJSON_AddStringToObject(root, "sram_internal", "512 KB Internal SRAM");
                cJSON_AddNumberToObject(root, "free_heap_bytes", (double)free_heap);
                cJSON_AddNumberToObject(root, "free_psram_bytes", (double)free_psram);

                cJSON* cam = cJSON_CreateObject();
                cJSON_AddStringToObject(
                    cam, "sensor",
                    "OmniVision OV5640 5MP (auto-detected, mendukung juga OV2640/OV3660/GC2145)");
                cJSON_AddStringToObject(
                    cam, "interface",
                    "DVP 8-bit Parallel (D0=11, D1=9, D2=8, D3=10, D4=12, D5=18, D6=17, D7=16, "
                    "XCLK=15, PCLK=13, VSYNC=6, HREF=7, SIOD=4, SIOC=5)");
                cJSON_AddStringToObject(cam, "live_stream_resolution",
                                        "QVGA 320x240 RGB565 (15 FPS) / VGA 640x480");
                cJSON_AddStringToObject(cam, "photo_capture_resolution",
                                        "QVGA 320x240 JPEG (Direct vision input)");
                cJSON_AddItemToObject(root, "camera", cam);

                cJSON* dsp = cJSON_CreateObject();
                cJSON_AddStringToObject(
                    dsp, "panel", "ST7789 IPS LCD (240x280 / 240x240, 1.69\" / 1.54\" 8-Pin SPI)");
                cJSON_AddStringToObject(
                    dsp, "pinout", "CS=45, SCL=19, SDA=20, DC=47, RST=21, Backlight BL=38 (PWM)");
                cJSON_AddStringToObject(
                    dsp, "alternative_display",
                    "OLED 128x64 SSD1306/SH1106 I2C (SDA=20, SCL=19) atau Headless");
                cJSON_AddItemToObject(root, "display", dsp);

                cJSON* aud = cJSON_CreateObject();
                cJSON_AddStringToObject(aud, "type", "I2S Simplex Codec");
                cJSON_AddStringToObject(aud, "microphone",
                                        "INMP441 Digital MEMS Microphone (SCK=2, WS=1, DIN=42)");
                cJSON_AddStringToObject(
                    aud, "speaker_amplifier",
                    "MAX98357A I2S Class D Audio Amp 3W (BCLK=40, WS=41, DOUT=39)");
                cJSON_AddItemToObject(root, "audio", aud);

                cJSON* pwr = cJSON_CreateObject();
                cJSON_AddStringToObject(pwr, "battery_support",
                                        "3.7V Li-ion / LiPo Battery via ADC1 CH2 (GPIO 3)");
                cJSON_AddNumberToObject(pwr, "battery_level_percent", batt_level);
                cJSON_AddBoolToObject(pwr, "charging", charging);
                cJSON_AddItemToObject(root, "power", pwr);

                cJSON* btn = cJSON_CreateObject();
                cJSON_AddNumberToObject(btn, "total_buttons", 3);
                cJSON_AddStringToObject(btn, "boot_action_button", "GPIO 0");
                cJSON_AddStringToObject(btn, "volume_up_button", "GPIO 14");
                cJSON_AddStringToObject(btn, "volume_down_button", "GPIO 46");
                cJSON_AddItemToObject(root, "buttons", btn);

                cJSON* led = cJSON_CreateObject();
                cJSON_AddStringToObject(led, "blue",
                                        "AI Berbicara (Speaking) / Pemutaran Musik YouTube");
                cJSON_AddStringToObject(led, "red", "AI Mendengarkan (Listening)");
                cJSON_AddStringToObject(led, "off", "Standby / Idle");
                cJSON_AddItemToObject(root, "indicator_led", led);

                char* json_str = cJSON_PrintUnformatted(root);
                std::string result(json_str ? json_str : "{}");
                if (json_str)
                    free(json_str);
                cJSON_Delete(root);
                return result;
            });

    // Restore the original tools list to the end of the tools list
    tools_.insert(tools_.end(), original_tools.begin(), original_tools.end());
}

void McpServer::AddUserOnlyTools() {
    // System tools
    AddUserOnlyTool("self.get_system_info", "Get the system information", PropertyList(),
                    [this](const PropertyList& properties) -> ReturnValue {
                        auto& board = Board::GetInstance();
                        return board.GetSystemInfoJson();
                    });

    AddUserOnlyTool("self.reboot", "Reboot the system", PropertyList(),
                    [this](const PropertyList& properties) -> ReturnValue {
                        auto& app = Application::GetInstance();
                        app.Schedule([&app]() {
                            ESP_LOGW(TAG, "User requested reboot");
                            vTaskDelay(pdMS_TO_TICKS(1000));

                            app.Reboot();
                        });
                        return true;
                    });

    // Firmware upgrade
    AddUserOnlyTool(
        "self.upgrade_firmware",
        "Upgrade firmware from a specific URL. This will download and install the firmware, then "
        "reboot the device.",
        PropertyList({Property("url", kPropertyTypeString,
                               "The URL of the firmware binary file to download and install")}),
        [this](const PropertyList& properties) -> ReturnValue {
            auto url = properties["url"].value<std::string>();
            ESP_LOGI(TAG, "User requested firmware upgrade from URL: %s", url.c_str());

            auto& app = Application::GetInstance();
            app.Schedule([url, &app]() {
                bool success = app.UpgradeFirmware(url);
                if (!success) {
                    ESP_LOGE(TAG, "Firmware upgrade failed");
                }
            });

            return true;
        });

    // Display control
#ifdef HAVE_LVGL
    auto display = dynamic_cast<LvglDisplay*>(Board::GetInstance().GetDisplay());
    if (display) {
        AddUserOnlyTool("self.screen.get_info",
                        "Information about the screen, including width, height, etc.",
                        PropertyList(), [display](const PropertyList& properties) -> ReturnValue {
                            cJSON* json = cJSON_CreateObject();
                            cJSON_AddNumberToObject(json, "width", display->width());
                            cJSON_AddNumberToObject(json, "height", display->height());
                            if (dynamic_cast<OledDisplay*>(display)) {
                                cJSON_AddBoolToObject(json, "monochrome", true);
                            } else {
                                cJSON_AddBoolToObject(json, "monochrome", false);
                            }
                            return json;
                        });

#if CONFIG_LV_USE_SNAPSHOT
        AddUserOnlyTool(
            "self.screen.snapshot", "Snapshot the screen and upload it to a specific URL",
            PropertyList({Property("url", kPropertyTypeString),
                          Property("quality", kPropertyTypeInteger, 80, 1, 100)}),
            [display](const PropertyList& properties) -> ReturnValue {
                auto url = properties["url"].value<std::string>();
                auto quality = properties["quality"].value<int>();

                std::string jpeg_data;
                if (!display->SnapshotToJpeg(jpeg_data, quality)) {
                    throw std::runtime_error("Failed to snapshot screen");
                }

                ESP_LOGI(TAG, "Upload snapshot %u bytes to %s", jpeg_data.size(), url.c_str());

                // 构造multipart/form-data请求体
                std::string boundary = "----ESP32_SCREEN_SNAPSHOT_BOUNDARY";

                auto http = Board::GetInstance().GetNetwork()->CreateHttp(3);
                http->SetHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
                if (!http->Open("POST", url)) {
                    throw std::runtime_error("Failed to open URL: " + url);
                }
                {
                    // 文件字段头部
                    std::string file_header;
                    file_header += "--" + boundary + "\r\n";
                    file_header +=
                        "Content-Disposition: form-data; name=\"file\"; "
                        "filename=\"screenshot.jpg\"\r\n";
                    file_header += "Content-Type: image/jpeg\r\n";
                    file_header += "\r\n";
                    http->Write(file_header.c_str(), file_header.size());
                }

                // JPEG数据
                http->Write((const char*)jpeg_data.data(), jpeg_data.size());

                {
                    // multipart尾部
                    std::string multipart_footer;
                    multipart_footer += "\r\n--" + boundary + "--\r\n";
                    http->Write(multipart_footer.c_str(), multipart_footer.size());
                }
                http->Write("", 0);

                if (http->GetStatusCode() != 200) {
                    throw std::runtime_error("Unexpected status code: " +
                                             std::to_string(http->GetStatusCode()));
                }
                std::string result = http->ReadAll();
                http->Close();
                ESP_LOGI(TAG, "Snapshot screen result: %s", result.c_str());
                return true;
            });

        AddUserOnlyTool(
            "self.screen.preview_image", "Preview an image on the screen",
            PropertyList({Property("url", kPropertyTypeString)}),
            [display](const PropertyList& properties) -> ReturnValue {
                auto url = properties["url"].value<std::string>();
                auto http = Board::GetInstance().GetNetwork()->CreateHttp(3);

                if (!http->Open("GET", url)) {
                    throw std::runtime_error("Failed to open URL: " + url);
                }
                int status_code = http->GetStatusCode();
                if (status_code != 200) {
                    throw std::runtime_error("Unexpected status code: " +
                                             std::to_string(status_code));
                }

                size_t content_length = http->GetBodyLength();
                char* data = (char*)heap_caps_malloc(content_length, MALLOC_CAP_8BIT);
                if (data == nullptr) {
                    throw std::runtime_error("Failed to allocate memory for image: " + url);
                }
                size_t total_read = 0;
                while (total_read < content_length) {
                    int ret = http->Read(data + total_read, content_length - total_read);
                    if (ret < 0) {
                        heap_caps_free(data);
                        throw std::runtime_error("Failed to download image: " + url);
                    }
                    if (ret == 0) {
                        break;
                    }
                    total_read += ret;
                }
                http->Close();

                auto image = std::make_unique<LvglAllocatedImage>(data, content_length);
                display->SetPreviewImage(std::move(image));
                return true;
            });
#endif  // CONFIG_LV_USE_SNAPSHOT
    }
#endif  // HAVE_LVGL

    // Assets download url (always registered — Settings storage works regardless of partition
    // layout)
    AddUserOnlyTool("self.assets.set_download_url", "Set the download url for the assets",
                    PropertyList({Property("url", kPropertyTypeString)}),
                    [](const PropertyList& properties) -> ReturnValue {
                        auto url = properties["url"].value<std::string>();
                        Settings settings("assets", true);
                        settings.SetString("download_url", url);
                        return true;
                    });
}

void McpServer::AddTool(McpTool* tool) {
    // Prevent adding duplicate tools
    if (std::find_if(tools_.begin(), tools_.end(), [tool](const McpTool* t) {
            return t->name() == tool->name();
        }) != tools_.end()) {
        ESP_LOGW(TAG, "Tool %s already added", tool->name().c_str());
        return;
    }

    ESP_LOGI(TAG, "Add tool: %s%s", tool->name().c_str(), tool->user_only() ? " [user]" : "");
    tools_.push_back(tool);
}

void McpServer::AddTool(const std::string& name, const std::string& description,
                        const PropertyList& properties,
                        std::function<ReturnValue(const PropertyList&)> callback) {
    AddTool(new McpTool(name, description, properties, callback));
}

void McpServer::AddUserOnlyTool(const std::string& name, const std::string& description,
                                const PropertyList& properties,
                                std::function<ReturnValue(const PropertyList&)> callback) {
    auto tool = new McpTool(name, description, properties, callback);
    tool->set_user_only(true);
    AddTool(tool);
}

void McpServer::ParseMessage(const std::string& message) {
    cJSON* json = cJSON_Parse(message.c_str());
    if (json == nullptr) {
        ESP_LOGE(TAG, "Failed to parse MCP message: %s", message.c_str());
        return;
    }
    ParseMessage(json);
    cJSON_Delete(json);
}

void McpServer::ParseCapabilities(const cJSON* capabilities) {
    auto vision = cJSON_GetObjectItem(capabilities, "vision");
    if (cJSON_IsObject(vision)) {
        auto url = cJSON_GetObjectItem(vision, "url");
        auto token = cJSON_GetObjectItem(vision, "token");
        if (cJSON_IsString(url)) {
            auto camera = Board::GetInstance().GetCamera();
            if (camera) {
                std::string url_str = std::string(url->valuestring);
                std::string token_str;
                if (cJSON_IsString(token)) {
                    token_str = std::string(token->valuestring);
                }
                camera->SetExplainUrl(url_str, token_str);
            }
        }
    }
}

void McpServer::ParseMessage(const cJSON* json) {
    // Check JSONRPC version
    auto version = cJSON_GetObjectItem(json, "jsonrpc");
    if (version == nullptr || !cJSON_IsString(version) ||
        strcmp(version->valuestring, "2.0") != 0) {
        ESP_LOGE(TAG, "Invalid JSONRPC version: %s", version ? version->valuestring : "null");
        return;
    }

    // Check method
    auto method = cJSON_GetObjectItem(json, "method");
    if (method == nullptr || !cJSON_IsString(method)) {
        ESP_LOGE(TAG, "Missing method");
        return;
    }

    auto method_str = std::string(method->valuestring);
    if (method_str.find("notifications") == 0) {
        return;
    }

    // Check params
    auto params = cJSON_GetObjectItem(json, "params");
    if (params != nullptr && !cJSON_IsObject(params)) {
        ESP_LOGE(TAG, "Invalid params for method: %s", method_str.c_str());
        return;
    }

    auto id = cJSON_GetObjectItem(json, "id");
    if (id == nullptr || !cJSON_IsNumber(id)) {
        ESP_LOGE(TAG, "Invalid id for method: %s", method_str.c_str());
        return;
    }
    auto id_int = id->valueint;

    if (method_str == "initialize") {
        if (cJSON_IsObject(params)) {
            auto capabilities = cJSON_GetObjectItem(params, "capabilities");
            if (cJSON_IsObject(capabilities)) {
                ParseCapabilities(capabilities);
            }
        }
        auto app_desc = esp_app_get_description();
        std::string message =
            "{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{\"tools\":{}},\"serverInfo\":{"
            "\"name\":\"" BOARD_NAME "\",\"version\":\"";
        message += app_desc->version;
        message += "\"}}";
        ReplyResult(id_int, message);
    } else if (method_str == "tools/list") {
        std::string cursor_str = "";
        bool list_user_only_tools = false;
        if (params != nullptr) {
            auto cursor = cJSON_GetObjectItem(params, "cursor");
            if (cJSON_IsString(cursor)) {
                cursor_str = std::string(cursor->valuestring);
            }
            auto with_user_tools = cJSON_GetObjectItem(params, "withUserTools");
            if (cJSON_IsBool(with_user_tools)) {
                list_user_only_tools = with_user_tools->valueint == 1;
            }
        }
        GetToolsList(id_int, cursor_str, list_user_only_tools);
    } else if (method_str == "tools/call") {
        if (!cJSON_IsObject(params)) {
            ESP_LOGE(TAG, "tools/call: Missing params");
            ReplyError(id_int, "Missing params");
            return;
        }
        auto tool_name = cJSON_GetObjectItem(params, "name");
        if (!cJSON_IsString(tool_name)) {
            ESP_LOGE(TAG, "tools/call: Missing name");
            ReplyError(id_int, "Missing name");
            return;
        }
        auto tool_arguments = cJSON_GetObjectItem(params, "arguments");
        if (tool_arguments != nullptr && !cJSON_IsObject(tool_arguments)) {
            ESP_LOGE(TAG, "tools/call: Invalid arguments");
            ReplyError(id_int, "Invalid arguments");
            return;
        }
        DoToolCall(id_int, std::string(tool_name->valuestring), tool_arguments);
    } else {
        ESP_LOGE(TAG, "Method not implemented: %s", method_str.c_str());
        ReplyError(id_int, "Method not implemented: " + method_str);
    }
}

void McpServer::ReplyResult(int id, const std::string& result) {
    std::string payload = "{\"jsonrpc\":\"2.0\",\"id\":";
    payload += std::to_string(id) + ",\"result\":";
    payload += result;
    payload += "}";
    Application::GetInstance().SendMcpMessage(payload);
}

void McpServer::ReplyError(int id, const std::string& message) {
    CJsonUniquePtr json_message(cJSON_CreateString(message.c_str()));
    if (json_message == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate MCP error message");
        return;
    }
    CJsonStringUniquePtr encoded_message(cJSON_PrintUnformatted(json_message.get()));
    if (encoded_message == nullptr) {
        ESP_LOGE(TAG, "Failed to serialize MCP error message");
        return;
    }

    std::string payload = "{\"jsonrpc\":\"2.0\",\"id\":";
    payload += std::to_string(id);
    payload += ",\"error\":{\"message\":";
    payload += encoded_message.get();
    payload += "}}";
    Application::GetInstance().SendMcpMessage(payload);
}

void McpServer::GetToolsList(int id, const std::string& cursor, bool list_user_only_tools) {
    const int max_payload_size = 8000;
    std::string json = "{\"tools\":[";

    bool found_cursor = cursor.empty();
    auto it = tools_.begin();
    std::string next_cursor = "";

    while (it != tools_.end()) {
        // 如果我们还没有找到起始位置，继续搜索
        if (!found_cursor) {
            if ((*it)->name() == cursor) {
                found_cursor = true;
            } else {
                ++it;
                continue;
            }
        }

        if (!list_user_only_tools && (*it)->user_only()) {
            ++it;
            continue;
        }

        // 添加tool前检查大小
        std::string tool_json = (*it)->to_json() + ",";
        if (json.length() + tool_json.length() + 30 > max_payload_size) {
            // 如果添加这个tool会超出大小限制，设置next_cursor并退出循环
            next_cursor = (*it)->name();
            break;
        }

        json += tool_json;
        ++it;
    }

    if (json.back() == ',') {
        json.pop_back();
    }

    if (json.back() == '[' && !tools_.empty()) {
        // 如果没有添加任何tool，返回错误
        ESP_LOGE(TAG, "tools/list: Failed to add tool %s because of payload size limit",
                 next_cursor.c_str());
        ReplyError(id, "Failed to add tool " + next_cursor + " because of payload size limit");
        return;
    }

    if (next_cursor.empty()) {
        json += "]}";
    } else {
        json += "],\"nextCursor\":\"" + next_cursor + "\"}";
    }

    ReplyResult(id, json);
}

void McpServer::DoToolCall(int id, const std::string& tool_name, const cJSON* tool_arguments) {
    auto tool_iter = std::find_if(tools_.begin(), tools_.end(), [&tool_name](const McpTool* tool) {
        return tool->name() == tool_name;
    });

    if (tool_iter == tools_.end()) {
        ESP_LOGE(TAG, "tools/call: Unknown tool: %s", tool_name.c_str());
        ReplyError(id, "Unknown tool: " + tool_name);
        return;
    }

    PropertyList arguments = (*tool_iter)->properties();
    try {
        for (auto& argument : arguments) {
            bool found = false;
            if (cJSON_IsObject(tool_arguments)) {
                auto value = cJSON_GetObjectItem(tool_arguments, argument.name().c_str());
                if (argument.type() == kPropertyTypeBoolean && cJSON_IsBool(value)) {
                    argument.set_value<bool>(value->valueint == 1);
                    found = true;
                } else if (argument.type() == kPropertyTypeInteger && cJSON_IsNumber(value)) {
                    argument.set_value<int>(value->valueint);
                    found = true;
                } else if (argument.type() == kPropertyTypeString && cJSON_IsString(value)) {
                    argument.set_value<std::string>(value->valuestring);
                    found = true;
                }
            }

            if (!argument.has_default_value() && !found) {
                ESP_LOGE(TAG, "tools/call: Missing valid argument: %s", argument.name().c_str());
                ReplyError(id, "Missing valid argument: " + argument.name());
                return;
            }
        }
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "tools/call: %s", e.what());
        ReplyError(id, e.what());
        return;
    }

    // Use main thread to call the tool
    auto& app = Application::GetInstance();
    app.Schedule([this, id, tool_iter, arguments = std::move(arguments)]() {
        try {
            ReplyResult(id, (*tool_iter)->Call(arguments));
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "tools/call: %s", e.what());
            ReplyError(id, e.what());
        }
    });
}
