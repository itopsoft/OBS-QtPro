/******************************************************************************
    Copyright (C) 2013 by Hugh Bailey <obs.jim@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include <time.h>
#include <stdio.h>
#include <wchar.h>
#include <chrono>
#include <ratio>
#include <string>
#include <sstream>
#include <mutex>
#include <util/bmem.h>
#include <util/dstr.hpp>
#include <util/platform.h>
#include <util/profiler.hpp>
#include <util/cf-parser.h>
#include <obs-config.h>
#include <obs.hpp>

#include <QGuiApplication>
#include <QProxyStyle>
#include <QScreen>
#include <QProcess>
#include <QAccessible>

#include "qt-wrappers.hpp"
#include "obs-app.hpp"
#include "log-viewer.hpp"
#include "slider-ignorewheel.hpp"
#include "OBSBasic.hpp"
#include "window-basic-settings.hpp"
#include "crash-report.hpp"
#include "platform.hpp"

#include <fstream>

#include <curl/curl.h>

#ifdef _WIN32
#include <windows.h>
#include <filesystem>
#else
#include <signal.h>
#include <pthread.h>
#endif

#include <iostream>

#include "ui-config.h"

using namespace std;

string currentLogFile;
string lastLogFile;
string lastCrashLogFile;

bool portable_mode = false;

bool opt_start_streaming = false;
bool opt_start_recording = false;
bool opt_studio_mode = false;
bool opt_start_replaybuffer = false;
bool opt_start_virtualcam = false;
bool opt_minimize_tray = false;
bool opt_allow_opengl = false;
bool opt_always_on_top = false;
string opt_starting_collection;
string opt_starting_profile;
string opt_starting_scene;

bool restart = false;

// GPU hint exports for AMD/NVIDIA laptops
#ifdef _MSC_VER
extern "C" __declspec(dllexport) DWORD NvOptimusEnablement = 1;
extern "C" __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
#endif

QObject *CreateShortcutFilter()
{
    return new OBSEventFilter([](QObject *obj, QEvent *event) {
        auto mouse_event = [](QMouseEvent &event) {
            if (!App()->HotkeysEnabledInFocus() &&
                event.button() != Qt::LeftButton)
                return true;

            obs_key_combination_t hotkey = {0, OBS_KEY_NONE};
            bool pressed = event.type() == QEvent::MouseButtonPress;

            switch (event.button()) {
            case Qt::NoButton:
            case Qt::LeftButton:
            case Qt::RightButton:
            case Qt::AllButtons:
            case Qt::MouseButtonMask:
                return false;

            case Qt::MiddleButton:
                hotkey.key = OBS_KEY_MOUSE3;
                break;

#define MAP_BUTTON(i, j)                       \
    case Qt::ExtraButton##i:               \
        hotkey.key = OBS_KEY_MOUSE##j; \
        break;
                MAP_BUTTON(1, 4);
                MAP_BUTTON(2, 5);
                MAP_BUTTON(3, 6);
                MAP_BUTTON(4, 7);
                MAP_BUTTON(5, 8);
                MAP_BUTTON(6, 9);
                MAP_BUTTON(7, 10);
                MAP_BUTTON(8, 11);
                MAP_BUTTON(9, 12);
                MAP_BUTTON(10, 13);
                MAP_BUTTON(11, 14);
                MAP_BUTTON(12, 15);
                MAP_BUTTON(13, 16);
                MAP_BUTTON(14, 17);
                MAP_BUTTON(15, 18);
                MAP_BUTTON(16, 19);
                MAP_BUTTON(17, 20);
                MAP_BUTTON(18, 21);
                MAP_BUTTON(19, 22);
                MAP_BUTTON(20, 23);
                MAP_BUTTON(21, 24);
                MAP_BUTTON(22, 25);
                MAP_BUTTON(23, 26);
                MAP_BUTTON(24, 27);
#undef MAP_BUTTON
            }

            hotkey.modifiers = TranslateQtKeyboardEventModifiers(
                event.modifiers());

            obs_hotkey_inject_event(hotkey, pressed);
            return true;
        };

        auto key_event = [&](QKeyEvent *event) {
            if (!App()->HotkeysEnabledInFocus())
                return true;

            QDialog *dialog = qobject_cast<QDialog *>(obj);

            obs_key_combination_t hotkey = {0, OBS_KEY_NONE};
            bool pressed = event->type() == QEvent::KeyPress;

            switch (event->key()) {
            case Qt::Key_Shift:
            case Qt::Key_Control:
            case Qt::Key_Alt:
            case Qt::Key_Meta:
                break;

#ifdef __APPLE__
            case Qt::Key_CapsLock:
                // kVK_CapsLock == 57
                hotkey.key = obs_key_from_virtual_key(57);
                pressed = true;
                break;
#endif

            case Qt::Key_Enter:
            case Qt::Key_Escape:
            case Qt::Key_Return:
                if (dialog && pressed)
                    return false;
                /* Falls through. */
            default:
                hotkey.key = obs_key_from_virtual_key(
                    event->nativeVirtualKey());
            }

            if (event->isAutoRepeat())
                return true;

            hotkey.modifiers = TranslateQtKeyboardEventModifiers(
                event->modifiers());

            obs_hotkey_inject_event(hotkey, pressed);
            return true;
        };

        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
            return mouse_event(*static_cast<QMouseEvent *>(event));

        /*case QEvent::MouseButtonDblClick:
        case QEvent::Wheel:*/
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
            return key_event(static_cast<QKeyEvent *>(event));

        default:
            return false;
        }
    });
}

string CurrentTimeString()
{
    using namespace std::chrono;

    struct tm tstruct;
    char buf[80];

    auto tp = system_clock::now();
    auto now = system_clock::to_time_t(tp);
    tstruct = *localtime(&now);

    size_t written = strftime(buf, sizeof(buf), "%X", &tstruct);
    if (ratio_less<system_clock::period, seconds::period>::value &&
        written && (sizeof(buf) - written) > 5) {
        auto tp_secs = time_point_cast<seconds>(tp);
        auto millis = duration_cast<milliseconds>(tp - tp_secs).count();

        snprintf(buf + written, sizeof(buf) - written, ".%03u",
             static_cast<unsigned>(millis));
    }

    return buf;
}

string CurrentDateTimeString()
{
    time_t now = time(0);
    struct tm tstruct;
    char buf[80];
    tstruct = *localtime(&now);
    strftime(buf, sizeof(buf), "%Y-%m-%d, %X", &tstruct);
    return buf;
}

#define MAX_REPEATED_LINES 30
#define MAX_CHAR_VARIATION (255 * 3)
#define DEFAULT_LANG "en-US"

bool OBSApp::InitGlobalConfigDefaults()
{
    config_set_default_string(globalConfig, "General", "Language",
                  DEFAULT_LANG);
    config_set_default_uint(globalConfig, "General", "MaxLogs", 10);
    config_set_default_int(globalConfig, "General", "InfoIncrement", -1);
    config_set_default_string(globalConfig, "General", "ProcessPriority",
                  "Normal");
    config_set_default_bool(globalConfig, "General", "EnableAutoUpdates",
                true);

#if _WIN32
    config_set_default_string(globalConfig, "Video", "Renderer",
                  "Direct3D 11");
#else
    config_set_default_string(globalConfig, "Video", "Renderer", "OpenGL");
#endif

    config_set_default_bool(globalConfig, "BasicWindow", "PreviewEnabled",
                true);
    config_set_default_bool(globalConfig, "BasicWindow",
                "PreviewProgramMode", false);
    config_set_default_bool(globalConfig, "BasicWindow",
                "SceneDuplicationMode", true);
    config_set_default_bool(globalConfig, "BasicWindow", "SwapScenesMode",
                true);
    config_set_default_bool(globalConfig, "BasicWindow", "SnappingEnabled",
                true);
    config_set_default_bool(globalConfig, "BasicWindow", "ScreenSnapping",
                true);
    config_set_default_bool(globalConfig, "BasicWindow", "SourceSnapping",
                true);
    config_set_default_bool(globalConfig, "BasicWindow", "CenterSnapping",
                false);
    config_set_default_double(globalConfig, "BasicWindow", "SnapDistance",
                  10.0);
    config_set_default_bool(globalConfig, "BasicWindow",
                "RecordWhenStreaming", false);
    config_set_default_bool(globalConfig, "BasicWindow",
                "KeepRecordingWhenStreamStops", false);
    config_set_default_bool(globalConfig, "BasicWindow", "SysTrayEnabled",
                true);
    config_set_default_bool(globalConfig, "BasicWindow",
                "SysTrayWhenStarted", false);
    config_set_default_bool(globalConfig, "BasicWindow", "SaveProjectors",
                false);
    config_set_default_bool(globalConfig, "BasicWindow", "ShowTransitions",
                true);
    config_set_default_bool(globalConfig, "BasicWindow",
                "ShowListboxToolbars", true);
    config_set_default_bool(globalConfig, "BasicWindow", "ShowStatusBar",
                true);
    config_set_default_bool(globalConfig, "BasicWindow", "ShowSourceIcons",
                true);
    config_set_default_bool(globalConfig, "BasicWindow",
                "ShowContextToolbars", true);
    config_set_default_bool(globalConfig, "BasicWindow", "StudioModeLabels",
                true);

    if (!config_get_bool(globalConfig, "General", "Pre21Defaults")) {
        config_set_default_string(globalConfig, "General",
                      "CurrentTheme", DEFAULT_THEME);
    }

    config_set_default_string(globalConfig, "General", "HotkeyFocusType",
                  "NeverDisableHotkeys");

    config_set_default_bool(globalConfig, "BasicWindow",
                "VerticalVolControl", false);

    config_set_default_bool(globalConfig, "BasicWindow",
                "MultiviewMouseSwitch", true);

    config_set_default_bool(globalConfig, "BasicWindow",
                "MultiviewDrawNames", true);

    config_set_default_bool(globalConfig, "BasicWindow",
                "MultiviewDrawAreas", true);

#ifdef _WIN32
    uint32_t winver = GetWindowsVersion();

    config_set_default_bool(globalConfig, "Audio", "DisableAudioDucking",
                true);
    config_set_default_bool(globalConfig, "General", "BrowserHWAccel",
                winver > 0x601);
#endif

#ifdef __APPLE__
    config_set_default_bool(globalConfig, "General", "BrowserHWAccel",
                true);
    config_set_default_bool(globalConfig, "Video", "DisableOSXVSync", true);
    config_set_default_bool(globalConfig, "Video", "ResetOSXVSyncOnExit",
                true);
#endif

    config_set_default_bool(globalConfig, "BasicWindow",
                "MediaControlsCountdownTimer", true);

    return true;
}

static bool do_mkdir(const char *path)
{
    if (os_mkdirs(path) == MKDIR_ERROR) {
        OBSErrorBox(NULL, "Failed to create directory %s", path);
        return false;
    }

    return true;
}

static bool MakeUserDirs()
{
    char path[512];

    if (GetConfigPath(path, sizeof(path), "obs-studio/basic") <= 0)
        return false;
    if (!do_mkdir(path))
        return false;

    if (GetConfigPath(path, sizeof(path), "obs-studio/logs") <= 0)
        return false;
    if (!do_mkdir(path))
        return false;

    if (GetConfigPath(path, sizeof(path), "obs-studio/profiler_data") <= 0)
        return false;
    if (!do_mkdir(path))
        return false;

#ifdef _WIN32
    if (GetConfigPath(path, sizeof(path), "obs-studio/crashes") <= 0)
        return false;
    if (!do_mkdir(path))
        return false;

    if (GetConfigPath(path, sizeof(path), "obs-studio/updates") <= 0)
        return false;
    if (!do_mkdir(path))
        return false;
#endif

    if (GetConfigPath(path, sizeof(path), "obs-studio/plugin_config") <= 0)
        return false;
    if (!do_mkdir(path))
        return false;

    return true;
}

static bool MakeUserProfileDirs()
{
    char path[512];

    if (GetConfigPath(path, sizeof(path), "obs-studio/basic/profiles") <= 0)
        return false;
    if (!do_mkdir(path))
        return false;

    if (GetConfigPath(path, sizeof(path), "obs-studio/basic/scenes") <= 0)
        return false;
    if (!do_mkdir(path))
        return false;

    return true;
}

static string GetProfileDirFromName(const char *name)
{
    string outputPath;
    os_glob_t *glob;
    char path[512];

    if (GetConfigPath(path, sizeof(path), "obs-studio/basic/profiles") <= 0)
        return outputPath;

    strcat(path, "/*");

    if (os_glob(path, 0, &glob) != 0)
        return outputPath;

    for (size_t i = 0; i < glob->gl_pathc; i++) {
        struct os_globent ent = glob->gl_pathv[i];
        if (!ent.directory)
            continue;

        strcpy(path, ent.path);
        strcat(path, "/basic.ini");

        ConfigFile config;
        if (config.Open(path, CONFIG_OPEN_EXISTING) != 0)
            continue;

        const char *curName =
            config_get_string(config, "General", "Name");
        if (astrcmpi(curName, name) == 0) {
            outputPath = ent.path;
            break;
        }
    }

    os_globfree(glob);

    if (!outputPath.empty()) {
        replace(outputPath.begin(), outputPath.end(), '\\', '/');
        const char *start = strrchr(outputPath.c_str(), '/');
        if (start)
            outputPath.erase(0, start - outputPath.c_str() + 1);
    }

    return outputPath;
}

static string GetSceneCollectionFileFromName(const char *name)
{
    string outputPath;
    os_glob_t *glob;
    char path[512];

    if (GetConfigPath(path, sizeof(path), "obs-studio/basic/scenes") <= 0)
        return outputPath;

    strcat(path, "/*.json");

    if (os_glob(path, 0, &glob) != 0)
        return outputPath;

    for (size_t i = 0; i < glob->gl_pathc; i++) {
        struct os_globent ent = glob->gl_pathv[i];
        if (ent.directory)
            continue;

        obs_data_t *data =
            obs_data_create_from_json_file_safe(ent.path, "bak");
        const char *curName = obs_data_get_string(data, "name");

        if (astrcmpi(name, curName) == 0) {
            outputPath = ent.path;
            obs_data_release(data);
            break;
        }

        obs_data_release(data);
    }

    os_globfree(glob);

    if (!outputPath.empty()) {
        outputPath.resize(outputPath.size() - 5);
        replace(outputPath.begin(), outputPath.end(), '\\', '/');
        const char *start = strrchr(outputPath.c_str(), '/');
        if (start)
            outputPath.erase(0, start - outputPath.c_str() + 1);
    }

    return outputPath;
}

bool OBSApp::UpdatePre22MultiviewLayout(const char *layout)
{
    if (!layout)
        return false;

    if (astrcmpi(layout, "horizontaltop") == 0) {
        config_set_int(
            globalConfig, "BasicWindow", "MultiviewLayout",
            static_cast<int>(
                MultiviewLayout::HORIZONTAL_TOP_8_SCENES));
        return true;
    }

    if (astrcmpi(layout, "horizontalbottom") == 0) {
        config_set_int(
            globalConfig, "BasicWindow", "MultiviewLayout",
            static_cast<int>(
                MultiviewLayout::HORIZONTAL_BOTTOM_8_SCENES));
        return true;
    }

    if (astrcmpi(layout, "verticalleft") == 0) {
        config_set_int(
            globalConfig, "BasicWindow", "MultiviewLayout",
            static_cast<int>(
                MultiviewLayout::VERTICAL_LEFT_8_SCENES));
        return true;
    }

    if (astrcmpi(layout, "verticalright") == 0) {
        config_set_int(
            globalConfig, "BasicWindow", "MultiviewLayout",
            static_cast<int>(
                MultiviewLayout::VERTICAL_RIGHT_8_SCENES));
        return true;
    }

    return false;
}

bool OBSApp::InitGlobalConfig()
{
    char path[512];
    bool changed = false;

    int len = GetConfigPath(path, sizeof(path), "obs-studio/global.ini");
    if (len <= 0) {
        return false;
    }

    int errorcode = globalConfig.Open(path, CONFIG_OPEN_ALWAYS);
    if (errorcode != CONFIG_SUCCESS) {
        OBSErrorBox(NULL, "Failed to open global.ini: %d", errorcode);
        return false;
    }

    if (!opt_starting_collection.empty()) {
        string path = GetSceneCollectionFileFromName(
            opt_starting_collection.c_str());
        if (!path.empty()) {
            config_set_string(globalConfig, "Basic",
                      "SceneCollection",
                      opt_starting_collection.c_str());
            config_set_string(globalConfig, "Basic",
                      "SceneCollectionFile", path.c_str());
            changed = true;
        }
    }

    if (!opt_starting_profile.empty()) {
        string path =
            GetProfileDirFromName(opt_starting_profile.c_str());
        if (!path.empty()) {
            config_set_string(globalConfig, "Basic", "Profile",
                      opt_starting_profile.c_str());
            config_set_string(globalConfig, "Basic", "ProfileDir",
                      path.c_str());
            changed = true;
        }
    }

    uint32_t lastVersion =
        config_get_int(globalConfig, "General", "LastVersion");

    if (!config_has_user_value(globalConfig, "General", "Pre19Defaults")) {
        bool useOldDefaults = lastVersion &&
                      lastVersion <
                          MAKE_SEMANTIC_VERSION(19, 0, 0);

        config_set_bool(globalConfig, "General", "Pre19Defaults",
                useOldDefaults);
        changed = true;
    }

    if (!config_has_user_value(globalConfig, "General", "Pre21Defaults")) {
        bool useOldDefaults = lastVersion &&
                      lastVersion <
                          MAKE_SEMANTIC_VERSION(21, 0, 0);

        config_set_bool(globalConfig, "General", "Pre21Defaults",
                useOldDefaults);
        changed = true;
    }

    if (!config_has_user_value(globalConfig, "General", "Pre23Defaults")) {
        bool useOldDefaults = lastVersion &&
                      lastVersion <
                          MAKE_SEMANTIC_VERSION(23, 0, 0);

        config_set_bool(globalConfig, "General", "Pre23Defaults",
                useOldDefaults);
        changed = true;
    }

#define PRE_24_1_DEFS "Pre24.1Defaults"
    if (!config_has_user_value(globalConfig, "General", PRE_24_1_DEFS)) {
        bool useOldDefaults = lastVersion &&
                      lastVersion <
                          MAKE_SEMANTIC_VERSION(24, 1, 0);

        config_set_bool(globalConfig, "General", PRE_24_1_DEFS,
                useOldDefaults);
        changed = true;
    }
#undef PRE_24_1_DEFS

    if (config_has_user_value(globalConfig, "BasicWindow",
                  "MultiviewLayout")) {
        const char *layout = config_get_string(
            globalConfig, "BasicWindow", "MultiviewLayout");
        changed |= UpdatePre22MultiviewLayout(layout);
    }

    if (lastVersion && lastVersion < MAKE_SEMANTIC_VERSION(24, 0, 0)) {
        bool disableHotkeysInFocus = config_get_bool(
            globalConfig, "General", "DisableHotkeysInFocus");
        if (disableHotkeysInFocus)
            config_set_string(globalConfig, "General",
                      "HotkeyFocusType",
                      "DisableHotkeysInFocus");
        changed = true;
    }

    if (changed)
        config_save_safe(globalConfig, "tmp", nullptr);

    return InitGlobalConfigDefaults();
}

bool OBSApp::InitLocale()
{
    ProfileScope("OBSApp::InitLocale");
    const char *lang =
        config_get_string(globalConfig, "General", "Language");

    locale = lang;

    string englishPath;
    if (!GetDataFilePath("locale/" DEFAULT_LANG ".ini", englishPath)) {
        OBSErrorBox(NULL, "Failed to find locale/" DEFAULT_LANG ".ini");
        return false;
    }

    textLookup = text_lookup_create(englishPath.c_str());
    if (!textLookup) {
        OBSErrorBox(NULL, "Failed to create locale from file '%s'",
                englishPath.c_str());
        return false;
    }

    bool userLocale =
        config_has_user_value(globalConfig, "General", "Language");
    bool defaultLang = astrcmpi(lang, DEFAULT_LANG) == 0;

    if (userLocale && defaultLang)
        return true;

    if (!userLocale && defaultLang) {
        for (auto &locale_ : GetPreferredLocales()) {
            if (locale_ == lang)
                return true;

            stringstream file;
            file << "locale/" << locale_ << ".ini";

            string path;
            if (!GetDataFilePath(file.str().c_str(), path))
                continue;

            if (!text_lookup_add(textLookup, path.c_str()))
                continue;

            blog(LOG_INFO, "Using preferred locale '%s'",
                 locale_.c_str());
            locale = locale_;
            return true;
        }

        return true;
    }

    stringstream file;
    file << "locale/" << lang << ".ini";

    string path;
    if (GetDataFilePath(file.str().c_str(), path)) {
        if (!text_lookup_add(textLookup, path.c_str()))
            blog(LOG_ERROR, "Failed to add locale file '%s'",
                 path.c_str());
    } else {
        blog(LOG_ERROR, "Could not find locale file '%s'",
             file.str().c_str());
    }

    return true;
}

void OBSApp::AddExtraThemeColor(QPalette &pal, int group, const char *name,
                uint32_t color)
{
    std::function<void(QPalette::ColorGroup)> func;

#define DEF_PALETTE_ASSIGN(name)                              \
    do {                                                  \
        func = [&](QPalette::ColorGroup group) {      \
            pal.setColor(group, QPalette::name,   \
                     QColor::fromRgb(color)); \
        };                                            \
    } while (false)

    if (astrcmpi(name, "alternateBase") == 0) {
        DEF_PALETTE_ASSIGN(AlternateBase);
    } else if (astrcmpi(name, "base") == 0) {
        DEF_PALETTE_ASSIGN(Base);
    } else if (astrcmpi(name, "brightText") == 0) {
        DEF_PALETTE_ASSIGN(BrightText);
    } else if (astrcmpi(name, "button") == 0) {
        DEF_PALETTE_ASSIGN(Button);
    } else if (astrcmpi(name, "buttonText") == 0) {
        DEF_PALETTE_ASSIGN(ButtonText);
    } else if (astrcmpi(name, "brightText") == 0) {
        DEF_PALETTE_ASSIGN(BrightText);
    } else if (astrcmpi(name, "dark") == 0) {
        DEF_PALETTE_ASSIGN(Dark);
    } else if (astrcmpi(name, "highlight") == 0) {
        DEF_PALETTE_ASSIGN(Highlight);
    } else if (astrcmpi(name, "highlightedText") == 0) {
        DEF_PALETTE_ASSIGN(HighlightedText);
    } else if (astrcmpi(name, "light") == 0) {
        DEF_PALETTE_ASSIGN(Light);
    } else if (astrcmpi(name, "link") == 0) {
        DEF_PALETTE_ASSIGN(Link);
    } else if (astrcmpi(name, "linkVisited") == 0) {
        DEF_PALETTE_ASSIGN(LinkVisited);
    } else if (astrcmpi(name, "mid") == 0) {
        DEF_PALETTE_ASSIGN(Mid);
    } else if (astrcmpi(name, "midlight") == 0) {
        DEF_PALETTE_ASSIGN(Midlight);
    } else if (astrcmpi(name, "shadow") == 0) {
        DEF_PALETTE_ASSIGN(Shadow);
    } else if (astrcmpi(name, "text") == 0 ||
           astrcmpi(name, "foreground") == 0) {
        DEF_PALETTE_ASSIGN(Text);
    } else if (astrcmpi(name, "toolTipBase") == 0) {
        DEF_PALETTE_ASSIGN(ToolTipBase);
    } else if (astrcmpi(name, "toolTipText") == 0) {
        DEF_PALETTE_ASSIGN(ToolTipText);
    } else if (astrcmpi(name, "windowText") == 0) {
        DEF_PALETTE_ASSIGN(WindowText);
    } else if (astrcmpi(name, "window") == 0 ||
           astrcmpi(name, "background") == 0) {
        DEF_PALETTE_ASSIGN(Window);
    } else {
        return;
    }

#undef DEF_PALETTE_ASSIGN

    switch (group) {
    case QPalette::Disabled:
    case QPalette::Active:
    case QPalette::Inactive:
        func((QPalette::ColorGroup)group);
        break;
    default:
        func((QPalette::ColorGroup)QPalette::Disabled);
        func((QPalette::ColorGroup)QPalette::Active);
        func((QPalette::ColorGroup)QPalette::Inactive);
    }
}

struct CFParser {
    cf_parser cfp = {};
    inline ~CFParser() { cf_parser_free(&cfp); }
    inline operator cf_parser *() { return &cfp; }
    inline cf_parser *operator->() { return &cfp; }
};

void OBSApp::ParseExtraThemeData(const char *path)
{
    BPtr<char> data = os_quick_read_utf8_file(path);
    QPalette pal = palette();
    CFParser cfp;
    int ret;

    cf_parser_parse(cfp, data, path);

    while (cf_go_to_token(cfp, "OBSTheme", nullptr)) {
        if (!cf_next_token(cfp))
            return;

        int group = -1;

        if (cf_token_is(cfp, ":")) {
            ret = cf_next_token_should_be(cfp, ":", nullptr,
                              nullptr);
            if (ret != PARSE_SUCCESS)
                continue;

            if (!cf_next_token(cfp))
                return;

            if (cf_token_is(cfp, "disabled")) {
                group = QPalette::Disabled;
            } else if (cf_token_is(cfp, "active")) {
                group = QPalette::Active;
            } else if (cf_token_is(cfp, "inactive")) {
                group = QPalette::Inactive;
            } else {
                continue;
            }

            if (!cf_next_token(cfp))
                return;
        }

        if (!cf_token_is(cfp, "{"))
            continue;

        for (;;) {
            if (!cf_next_token(cfp))
                return;

            ret = cf_token_is_type(cfp, CFTOKEN_NAME, "name",
                           nullptr);
            if (ret != PARSE_SUCCESS)
                break;

            DStr name;
            dstr_copy_strref(name, &cfp->cur_token->str);

            ret = cf_next_token_should_be(cfp, ":", ";", nullptr);
            if (ret != PARSE_SUCCESS)
                continue;

            if (!cf_next_token(cfp))
                return;

            const char *array;
            uint32_t color = 0;

            if (cf_token_is(cfp, "#")) {
                array = cfp->cur_token->str.array;
                color = strtol(array + 1, nullptr, 16);

            } else if (cf_token_is(cfp, "rgb")) {
                ret = cf_next_token_should_be(cfp, "(", ";",
                                  nullptr);
                if (ret != PARSE_SUCCESS)
                    continue;
                if (!cf_next_token(cfp))
                    return;

                array = cfp->cur_token->str.array;
                color |= strtol(array, nullptr, 10) << 16;

                ret = cf_next_token_should_be(cfp, ",", ";",
                                  nullptr);
                if (ret != PARSE_SUCCESS)
                    continue;
                if (!cf_next_token(cfp))
                    return;

                array = cfp->cur_token->str.array;
                color |= strtol(array, nullptr, 10) << 8;

                ret = cf_next_token_should_be(cfp, ",", ";",
                                  nullptr);
                if (ret != PARSE_SUCCESS)
                    continue;
                if (!cf_next_token(cfp))
                    return;

                array = cfp->cur_token->str.array;
                color |= strtol(array, nullptr, 10);

            } else if (cf_token_is(cfp, "white")) {
                color = 0xFFFFFF;

            } else if (cf_token_is(cfp, "black")) {
                color = 0;
            }

            if (!cf_go_to_token(cfp, ";", nullptr))
                return;

            AddExtraThemeColor(pal, group, name->array, color);
        }

        ret = cf_token_should_be(cfp, "}", "}", nullptr);
        if (ret != PARSE_SUCCESS)
            continue;
    }

    setPalette(pal);
}

bool OBSApp::SetTheme(std::string name, std::string path)
{
    theme = name;

    /* Check user dir first, then preinstalled themes. */
    if (path == "") {
        char userDir[512];
        name = "themes/" + name + ".qss";
        string temp = "obs-studio/" + name;
        int ret = GetConfigPath(userDir, sizeof(userDir), temp.c_str());

        if (ret > 0 && QFile::exists(userDir)) {
            path = string(userDir);
        } else if (!GetDataFilePath(name.c_str(), path)) {
            OBSErrorBox(NULL, "Failed to find %s.", name.c_str());
            return false;
        }
    }

    QString mpath = QString("file:///") + path.c_str();
    setPalette(defaultPalette);
    setStyleSheet(mpath);
    ParseExtraThemeData(path.c_str());

    emit StyleChanged();
    return true;
}

bool OBSApp::InitTheme()
{
    defaultPalette = palette();

    const char *themeName =
        config_get_string(globalConfig, "General", "CurrentTheme2");

    if (!themeName)
        /* Use deprecated "CurrentTheme" value if available */
        themeName = config_get_string(globalConfig, "General",
                          "CurrentTheme");
    if (!themeName)
        /* Use deprecated "Theme" value if available */
        themeName = config_get_string(globalConfig, "General", "Theme");
    if (!themeName)
        themeName = DEFAULT_THEME;
    if (!themeName)
        themeName = "Dark";

    if (strcmp(themeName, "Default") == 0)
        themeName = "System";

    if (strcmp(themeName, "System") != 0 && SetTheme(themeName))
        return true;

    return SetTheme("System");
}

OBSApp::OBSApp(int &argc, char **argv, profiler_name_store_t *store)
    : QApplication(argc, argv), profilerNameStore(store)
{
    sleepInhibitor = os_inhibit_sleep_create("OBS Video/audio");

#ifdef __APPLE__
    setWindowIcon(
        QIcon::fromTheme("obs", QIcon(":/res/images/obs_256x256.png")));
#else
    setWindowIcon(QIcon::fromTheme("obs", QIcon(":/res/images/obs.png")));
#endif
}

OBSApp::~OBSApp()
{
#ifdef _WIN32
    bool disableAudioDucking =
        config_get_bool(globalConfig, "Audio", "DisableAudioDucking");
    if (disableAudioDucking)
        DisableAudioDucking(false);
#endif

#ifdef __APPLE__
    bool vsyncDiabled =
        config_get_bool(globalConfig, "Video", "DisableOSXVSync");
    bool resetVSync =
        config_get_bool(globalConfig, "Video", "ResetOSXVSyncOnExit");
    if (vsyncDiabled && resetVSync)
        EnableOSXVSync(true);
#endif

    os_inhibit_sleep_set_active(sleepInhibitor, false);
    os_inhibit_sleep_destroy(sleepInhibitor);

    if (libobs_initialized)
        obs_shutdown();
}

static void move_basic_to_profiles(void)
{
    char path[512];
    char new_path[512];
    os_glob_t *glob;

    /* if not first time use */
    if (GetConfigPath(path, 512, "obs-studio/basic") <= 0)
        return;
    if (!os_file_exists(path))
        return;

    /* if the profiles directory doesn't already exist */
    if (GetConfigPath(new_path, 512, "obs-studio/basic/profiles") <= 0)
        return;
    if (os_file_exists(new_path))
        return;

    if (os_mkdir(new_path) == MKDIR_ERROR)
        return;

    strcat(new_path, "/");
    strcat(new_path, Str("Untitled"));
    if (os_mkdir(new_path) == MKDIR_ERROR)
        return;

    strcat(path, "/*.*");
    if (os_glob(path, 0, &glob) != 0)
        return;

    strcpy(path, new_path);

    for (size_t i = 0; i < glob->gl_pathc; i++) {
        struct os_globent ent = glob->gl_pathv[i];
        char *file;

        if (ent.directory)
            continue;

        file = strrchr(ent.path, '/');
        if (!file++)
            continue;

        if (astrcmpi(file, "scenes.json") == 0)
            continue;

        strcpy(new_path, path);
        strcat(new_path, "/");
        strcat(new_path, file);
        os_rename(ent.path, new_path);
    }

    os_globfree(glob);
}

static void move_basic_to_scene_collections(void)
{
    char path[512];
    char new_path[512];

    if (GetConfigPath(path, 512, "obs-studio/basic") <= 0)
        return;
    if (!os_file_exists(path))
        return;

    if (GetConfigPath(new_path, 512, "obs-studio/basic/scenes") <= 0)
        return;
    if (os_file_exists(new_path))
        return;

    if (os_mkdir(new_path) == MKDIR_ERROR)
        return;

    strcat(path, "/scenes.json");
    strcat(new_path, "/");
    strcat(new_path, Str("Untitled"));
    strcat(new_path, ".json");

    os_rename(path, new_path);
}

void OBSApp::AppInit()
{
    ProfileScope("OBSApp::AppInit");

    if (!InitApplicationBundle())
        throw "Failed to initialize application bundle";
    if (!MakeUserDirs())
        throw "Failed to create required user directories";
    if (!InitGlobalConfig())
        throw "Failed to initialize global config";
    if (!InitLocale())
        throw "Failed to load locale";
    if (!InitTheme())
        throw "Failed to load theme";

    config_set_default_string(globalConfig, "Basic", "Profile",
                  Str("Untitled"));
    config_set_default_string(globalConfig, "Basic", "ProfileDir",
                  Str("Untitled"));
    config_set_default_string(globalConfig, "Basic", "SceneCollection",
                  Str("Untitled"));
    config_set_default_string(globalConfig, "Basic", "SceneCollectionFile",
                  Str("Untitled"));
    config_set_default_bool(globalConfig, "Basic", "ConfigOnNewProfile",
                true);

    if (!config_has_user_value(globalConfig, "Basic", "Profile")) {
        config_set_string(globalConfig, "Basic", "Profile",
                  Str("Untitled"));
        config_set_string(globalConfig, "Basic", "ProfileDir",
                  Str("Untitled"));
    }

    if (!config_has_user_value(globalConfig, "Basic", "SceneCollection")) {
        config_set_string(globalConfig, "Basic", "SceneCollection",
                  Str("Untitled"));
        config_set_string(globalConfig, "Basic", "SceneCollectionFile",
                  Str("Untitled"));
    }

#ifdef _WIN32
    bool disableAudioDucking =
        config_get_bool(globalConfig, "Audio", "DisableAudioDucking");
    if (disableAudioDucking)
        DisableAudioDucking(true);
#endif

#ifdef __APPLE__
    if (config_get_bool(globalConfig, "Video", "DisableOSXVSync"))
        EnableOSXVSync(false);
#endif

    UpdateHotkeyFocusSetting(false);

    move_basic_to_profiles();
    move_basic_to_scene_collections();

    if (!MakeUserProfileDirs())
        throw "Failed to create profile directories";
}

const char *OBSApp::GetRenderModule() const
{
    const char *renderer =
        config_get_string(globalConfig, "Video", "Renderer");

    return (astrcmpi(renderer, "Direct3D 11") == 0) ? DL_D3D11 : DL_OPENGL;
}

static bool StartupOBS(const char *locale, profiler_name_store_t *store)
{
    char path[512];

    if (GetConfigPath(path, sizeof(path), "obs-studio/plugin_config") <= 0)
        return false;

    return obs_startup(locale, path, store);
}

inline void OBSApp::ResetHotkeyState(bool inFocus)
{
    obs_hotkey_enable_background_press(
        (inFocus && enableHotkeysInFocus) ||
        (!inFocus && enableHotkeysOutOfFocus));
}

void OBSApp::UpdateHotkeyFocusSetting(bool resetState)
{
    enableHotkeysInFocus = true;
    enableHotkeysOutOfFocus = true;

    const char *hotkeyFocusType =
        config_get_string(globalConfig, "General", "HotkeyFocusType");

    if (astrcmpi(hotkeyFocusType, "DisableHotkeysInFocus") == 0) {
        enableHotkeysInFocus = false;
    } else if (astrcmpi(hotkeyFocusType, "DisableHotkeysOutOfFocus") == 0) {
        enableHotkeysOutOfFocus = false;
    }

    if (resetState)
        ResetHotkeyState(applicationState() == Qt::ApplicationActive);
}

void OBSApp::DisableHotkeys()
{
    enableHotkeysInFocus = false;
    enableHotkeysOutOfFocus = false;
    ResetHotkeyState(applicationState() == Qt::ApplicationActive);
}

Q_DECLARE_METATYPE(VoidFunc)

void OBSApp::Exec(VoidFunc func)
{
    func();
}

static void ui_task_handler(obs_task_t task, void *param, bool wait)
{
    auto doTask = [=]() {
        /* to get clang-format to behave */
        task(param);
    };
    QMetaObject::invokeMethod(App(), "Exec",
                  wait ? WaitConnection() : Qt::AutoConnection,
                  Q_ARG(VoidFunc, doTask));
}

bool OBSApp::OBSInit()
{
    ProfileScope("OBSApp::OBSInit");

    setAttribute(Qt::AA_UseHighDpiPixmaps);

    qRegisterMetaType<VoidFunc>();

    if (!StartupOBS(locale.c_str(), GetProfilerNameStore()))
        return false;

    libobs_initialized = true;

    obs_set_ui_task_handler(ui_task_handler);

#if defined(_WIN32) || defined(__APPLE__)
    bool browserHWAccel =
        config_get_bool(globalConfig, "General", "BrowserHWAccel");

    obs_data_t *settings = obs_data_create();
    obs_data_set_bool(settings, "BrowserHWAccel", browserHWAccel);
    obs_apply_private_data(settings);
    obs_data_release(settings);

    blog(LOG_INFO, "Current Date/Time: %s",
         CurrentDateTimeString().c_str());

    blog(LOG_INFO, "Browser Hardware Acceleration: %s",
         browserHWAccel ? "true" : "false");
#endif

    blog(LOG_INFO, "Portable mode: %s", portable_mode ? "true" : "false");

    setQuitOnLastWindowClosed(false);

    mainWindow = new OBSBasic();

    mainWindow->setAttribute(Qt::WA_DeleteOnClose, true);
    connect(mainWindow, SIGNAL(destroyed()), this, SLOT(quit()));

    mainWindow->OBSInit();

    connect(this, &QGuiApplication::applicationStateChanged,
        [this](Qt::ApplicationState state) {
            ResetHotkeyState(state == Qt::ApplicationActive);
        });
    ResetHotkeyState(applicationState() == Qt::ApplicationActive);
    return true;
}

string OBSApp::GetVersionString() const
{
    stringstream ver;

#ifdef HAVE_OBSCONFIG_H
    ver << OBS_VERSION;
#else
    ver << LIBOBS_API_MAJOR_VER << "." << LIBOBS_API_MINOR_VER << "."
        << LIBOBS_API_PATCH_VER;

#endif
    ver << " (";

#ifdef _WIN32
    if (sizeof(void *) == 8)
        ver << "64-bit, ";
    else
        ver << "32-bit, ";

    ver << "windows)";
#elif __APPLE__
    ver << "mac)";
#elif __OpenBSD__
    ver << "openbsd)";
#elif __FreeBSD__
    ver << "freebsd)";
#else /* assume linux for the time being */
    ver << "linux)";
#endif

    return ver.str();
}

bool OBSApp::IsPortableMode()
{
    return portable_mode;
}

#ifdef __APPLE__
#define INPUT_AUDIO_SOURCE "coreaudio_input_capture"
#define OUTPUT_AUDIO_SOURCE "coreaudio_output_capture"
#elif _WIN32
#define INPUT_AUDIO_SOURCE "wasapi_input_capture"
#define OUTPUT_AUDIO_SOURCE "wasapi_output_capture"
#else
#define INPUT_AUDIO_SOURCE "pulse_input_capture"
#define OUTPUT_AUDIO_SOURCE "pulse_output_capture"
#endif

const char *OBSApp::InputAudioSource() const
{
    return INPUT_AUDIO_SOURCE;
}

const char *OBSApp::OutputAudioSource() const
{
    return OUTPUT_AUDIO_SOURCE;
}

const char *OBSApp::GetLastLog() const
{
    return lastLogFile.c_str();
}

const char *OBSApp::GetCurrentLog() const
{
    return currentLogFile.c_str();
}

const char *OBSApp::GetLastCrashLog() const
{
    return lastCrashLogFile.c_str();
}

bool OBSApp::TranslateString(const char *lookupVal, const char **out) const
{
    for (obs_frontend_translate_ui_cb cb : translatorHooks) {
        if (cb(lookupVal, out))
            return true;
    }

    return text_lookup_getstr(App()->GetTextLookup(), lookupVal, out);
}

QString OBSTranslator::translate(const char *context, const char *sourceText,
                 const char *disambiguation, int n) const
{
    const char *out = nullptr;
    if (!App()->TranslateString(sourceText, &out))
        return QString(sourceText);

    UNUSED_PARAMETER(context);
    UNUSED_PARAMETER(disambiguation);
    UNUSED_PARAMETER(n);
    return QT_UTF8(out);
}

string GenerateTimeDateFilename(const char *extension, bool noSpace)
{
    time_t now = time(0);
    char file[256] = {};
    struct tm *cur_time;

    cur_time = localtime(&now);
    snprintf(file, sizeof(file), "%d-%02d-%02d%c%02d-%02d-%02d.%s",
         cur_time->tm_year + 1900, cur_time->tm_mon + 1,
         cur_time->tm_mday, noSpace ? '_' : ' ', cur_time->tm_hour,
         cur_time->tm_min, cur_time->tm_sec, extension);

    return string(file);
}

string GenerateSpecifiedFilename(const char *extension, bool noSpace,
                 const char *format)
{
    BPtr<char> filename =
        os_generate_formatted_filename(extension, !noSpace, format);
    return string(filename);
}

static void FindBestFilename(string &strPath, bool noSpace)
{
    int num = 2;

    if (!os_file_exists(strPath.c_str()))
        return;

    const char *ext = strrchr(strPath.c_str(), '.');
    if (!ext)
        return;

    int extStart = int(ext - strPath.c_str());
    for (;;) {
        string testPath = strPath;
        string numStr;

        numStr = noSpace ? "_" : " (";
        numStr += to_string(num++);
        if (!noSpace)
            numStr += ")";

        testPath.insert(extStart, numStr);

        if (!os_file_exists(testPath.c_str())) {
            strPath = testPath;
            break;
        }
    }
}

static void ensure_directory_exists(string &path)
{
    replace(path.begin(), path.end(), '\\', '/');

    size_t last = path.rfind('/');
    if (last == string::npos)
        return;

    string directory = path.substr(0, last);
    os_mkdirs(directory.c_str());
}

static void remove_reserved_file_characters(string &s)
{
    replace(s.begin(), s.end(), '\\', '/');
    replace(s.begin(), s.end(), '*', '_');
    replace(s.begin(), s.end(), '?', '_');
    replace(s.begin(), s.end(), '"', '_');
    replace(s.begin(), s.end(), '|', '_');
    replace(s.begin(), s.end(), ':', '_');
    replace(s.begin(), s.end(), '>', '_');
    replace(s.begin(), s.end(), '<', '_');
}

string GetFormatString(const char *format, const char *prefix,
               const char *suffix)
{
    string f;

    if (prefix && *prefix) {
        f += prefix;
        if (f.back() != ' ')
            f += " ";
    }

    f += format;

    if (suffix && *suffix) {
        if (*suffix != ' ')
            f += " ";
        f += suffix;
    }

    remove_reserved_file_characters(f);

    return f;
}

string GetOutputFilename(const char *path, const char *ext, bool noSpace,
             bool overwrite, const char *format)
{
    OBSBasic *main = reinterpret_cast<OBSBasic *>(App()->GetMainWindow());

    os_dir_t *dir = path && path[0] ? os_opendir(path) : nullptr;

    if (!dir) {
        if (main->isVisible())
            OBSMessageBox::warning(main,
                           QTStr("Output.BadPath.Title"),
                           QTStr("Output.BadPath.Text"));
        else
            main->SysTrayNotify(QTStr("Output.BadPath.Text"),
                        QSystemTrayIcon::Warning);
        return "";
    }

    os_closedir(dir);

    string strPath;
    strPath += path;

    char lastChar = strPath.back();
    if (lastChar != '/' && lastChar != '\\')
        strPath += "/";

    strPath += GenerateSpecifiedFilename(ext, noSpace, format);
    ensure_directory_exists(strPath);
    if (!overwrite)
        FindBestFilename(strPath, noSpace);

    return strPath;
}

vector<pair<string, string>> GetLocaleNames()
{
    string path;
    if (!GetDataFilePath("locale.ini", path))
        throw "Could not find locale.ini path";

    ConfigFile ini;
    if (ini.Open(path.c_str(), CONFIG_OPEN_EXISTING) != 0)
        throw "Could not open locale.ini";

    size_t sections = config_num_sections(ini);

    vector<pair<string, string>> names;
    names.reserve(sections);
    for (size_t i = 0; i < sections; i++) {
        const char *tag = config_get_section(ini, i);
        const char *name = config_get_string(ini, tag, "Name");
        names.emplace_back(tag, name);
    }

    return names;
}

static auto ProfilerNameStoreRelease = [](profiler_name_store_t *store) {
    profiler_name_store_free(store);
};

using ProfilerNameStore = std::unique_ptr<profiler_name_store_t,
                      decltype(ProfilerNameStoreRelease)>;

ProfilerNameStore CreateNameStore()
{
    return ProfilerNameStore{profiler_name_store_create(),
                 ProfilerNameStoreRelease};
}

#ifdef __APPLE__
#define BASE_PATH ".."
#else
#define BASE_PATH "../.."
#endif

#define CONFIG_PATH BASE_PATH "/config"

#ifndef OBS_UNIX_STRUCTURE
#define OBS_UNIX_STRUCTURE 0
#endif

int GetConfigPath(char *path, size_t size, const char *name)
{
    if (!OBS_UNIX_STRUCTURE && portable_mode) {
        if (name && *name) {
            return snprintf(path, size, CONFIG_PATH "/%s", name);
        } else {
            return snprintf(path, size, CONFIG_PATH);
        }
    } else {
        return os_get_config_path(path, size, name);
    }
}

char *GetConfigPathPtr(const char *name)
{
    if (!OBS_UNIX_STRUCTURE && portable_mode) {
        char path[512];

        if (snprintf(path, sizeof(path), CONFIG_PATH "/%s", name) > 0) {
            return bstrdup(path);
        } else {
            return NULL;
        }
    } else {
        return os_get_config_path_ptr(name);
    }
}

