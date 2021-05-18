#include "mainwindow.h"

#include <QApplication>

//int main(int argc, char *argv[])
//{
//    QApplication a(argc, argv);

//    MainWindow w;
//    w.show();
//    return a.exec();
//}

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
#include "window-basic-main.hpp"
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

static log_handler_t def_log_handler;

static bool multi = false;
static bool log_verbose = false;
static bool unfiltered_log = false;

QPointer<OBSLogViewer> obsLogViewer;

static inline void LogString(fstream &logFile, const char *timeString,
                 char *str, int log_level)
{
    string msg;
    msg += timeString;
    msg += str;

    logFile << msg << endl;

    if (!!obsLogViewer)
        QMetaObject::invokeMethod(obsLogViewer.data(), "AddLine",
                      Qt::QueuedConnection,
                      Q_ARG(int, log_level),
                      Q_ARG(QString, QString(msg.c_str())));
}

static inline void LogStringChunk(fstream &logFile, char *str, int log_level)
{
    char *nextLine = str;
    string timeString = CurrentTimeString();
    timeString += ": ";

    while (*nextLine) {
        char *nextLine = strchr(str, '\n');
        if (!nextLine)
            break;

        if (nextLine != str && nextLine[-1] == '\r') {
            nextLine[-1] = 0;
        } else {
            nextLine[0] = 0;
        }

        LogString(logFile, timeString.c_str(), str, log_level);
        nextLine++;
        str = nextLine;
    }

    LogString(logFile, timeString.c_str(), str, log_level);
}

#define MAX_REPEATED_LINES 30
#define MAX_CHAR_VARIATION (255 * 3)

static inline int sum_chars(const char *str)
{
    int val = 0;
    for (; *str != 0; str++)
        val += *str;

    return val;
}

static inline bool too_many_repeated_entries(fstream &logFile, const char *msg,
                         const char *output_str)
{
    static mutex log_mutex;
    static const char *last_msg_ptr = nullptr;
    static int last_char_sum = 0;
    static char cmp_str[4096];
    static int rep_count = 0;

    int new_sum = sum_chars(output_str);

    lock_guard<mutex> guard(log_mutex);

    if (unfiltered_log) {
        return false;
    }

    if (last_msg_ptr == msg) {
        int diff = std::abs(new_sum - last_char_sum);
        if (diff < MAX_CHAR_VARIATION) {
            return (rep_count++ >= MAX_REPEATED_LINES);
        }
    }

    if (rep_count > MAX_REPEATED_LINES) {
        logFile << CurrentTimeString()
            << ": Last log entry repeated for "
            << to_string(rep_count - MAX_REPEATED_LINES)
            << " more lines" << endl;
    }

    last_msg_ptr = msg;
    strcpy(cmp_str, output_str);
    last_char_sum = new_sum;
    rep_count = 0;

    return false;
}

static void do_log(int log_level, const char *msg, va_list args, void *param)
{
    fstream &logFile = *static_cast<fstream *>(param);
    char str[4096];

#ifndef _WIN32
    va_list args2;
    va_copy(args2, args);
#endif

    vsnprintf(str, 4095, msg, args);

#ifdef _WIN32
    if (IsDebuggerPresent()) {
        int wNum = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
        if (wNum > 1) {
            static wstring wide_buf;
            static mutex wide_mutex;

            lock_guard<mutex> lock(wide_mutex);
            wide_buf.reserve(wNum + 1);
            wide_buf.resize(wNum - 1);
            MultiByteToWideChar(CP_UTF8, 0, str, -1, &wide_buf[0],
                        wNum);
            wide_buf.push_back('\n');

            OutputDebugStringW(wide_buf.c_str());
        }
    }
#else
    def_log_handler(log_level, msg, args2, nullptr);
    va_end(args2);
#endif

    if (log_level <= LOG_INFO || log_verbose) {
        if (too_many_repeated_entries(logFile, msg, str))
            return;
        LogStringChunk(logFile, str, log_level);
    }

#if defined(_WIN32) && defined(OBS_DEBUGBREAK_ON_ERROR)
    if (log_level <= LOG_ERROR && IsDebuggerPresent())
        __debugbreak();
#endif
}

#define DEFAULT_LANG "en-US"

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

struct CFParser {
    cf_parser cfp = {};
    inline ~CFParser() { cf_parser_free(&cfp); }
    inline operator cf_parser *() { return &cfp; }
    inline cf_parser *operator->() { return &cfp; }
};

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

static bool get_token(lexer *lex, string &str, base_token_type type)
{
    base_token token;
    if (!lexer_getbasetoken(lex, &token, IGNORE_WHITESPACE))
        return false;
    if (token.type != type)
        return false;

    str.assign(token.text.array, token.text.len);
    return true;
}

static bool expect_token(lexer *lex, const char *str, base_token_type type)
{
    base_token token;
    if (!lexer_getbasetoken(lex, &token, IGNORE_WHITESPACE))
        return false;
    if (token.type != type)
        return false;

    return strref_cmp(&token.text, str) == 0;
}

static uint64_t convert_log_name(bool has_prefix, const char *name)
{
    BaseLexer lex;
    string year, month, day, hour, minute, second;

    lexer_start(lex, name);

    if (has_prefix) {
        string temp;
        if (!get_token(lex, temp, BASETOKEN_ALPHA))
            return 0;
    }

    if (!get_token(lex, year, BASETOKEN_DIGIT))
        return 0;
    if (!expect_token(lex, "-", BASETOKEN_OTHER))
        return 0;
    if (!get_token(lex, month, BASETOKEN_DIGIT))
        return 0;
    if (!expect_token(lex, "-", BASETOKEN_OTHER))
        return 0;
    if (!get_token(lex, day, BASETOKEN_DIGIT))
        return 0;
    if (!get_token(lex, hour, BASETOKEN_DIGIT))
        return 0;
    if (!expect_token(lex, "-", BASETOKEN_OTHER))
        return 0;
    if (!get_token(lex, minute, BASETOKEN_DIGIT))
        return 0;
    if (!expect_token(lex, "-", BASETOKEN_OTHER))
        return 0;
    if (!get_token(lex, second, BASETOKEN_DIGIT))
        return 0;

    stringstream timestring;
    timestring << year << month << day << hour << minute << second;
    return std::stoull(timestring.str());
}

static void delete_oldest_file(bool has_prefix, const char *location)
{
    BPtr<char> logDir(GetConfigPathPtr(location));
    string oldestLog;
    uint64_t oldest_ts = (uint64_t)-1;
    struct os_dirent *entry;

    unsigned int maxLogs = (unsigned int)config_get_uint(
        App()->GlobalConfig(), "General", "MaxLogs");

    os_dir_t *dir = os_opendir(logDir);
    if (dir) {
        unsigned int count = 0;

        while ((entry = os_readdir(dir)) != NULL) {
            if (entry->directory || *entry->d_name == '.')
                continue;

            uint64_t ts =
                convert_log_name(has_prefix, entry->d_name);

            if (ts) {
                if (ts < oldest_ts) {
                    oldestLog = entry->d_name;
                    oldest_ts = ts;
                }

                count++;
            }
        }

        os_closedir(dir);

        if (count > maxLogs) {
            stringstream delPath;

            delPath << logDir << "/" << oldestLog;
            os_unlink(delPath.str().c_str());
        }
    }
}

static void get_last_log(bool has_prefix, const char *subdir_to_use,
             std::string &last)
{
    BPtr<char> logDir(GetConfigPathPtr(subdir_to_use));
    struct os_dirent *entry;
    os_dir_t *dir = os_opendir(logDir);
    uint64_t highest_ts = 0;

    if (dir) {
        while ((entry = os_readdir(dir)) != NULL) {
            if (entry->directory || *entry->d_name == '.')
                continue;

            uint64_t ts =
                convert_log_name(has_prefix, entry->d_name);

            if (ts > highest_ts) {
                last = entry->d_name;
                highest_ts = ts;
            }
        }

        os_closedir(dir);
    }
}

static void create_log_file(fstream &logFile)
{
    stringstream dst;

    get_last_log(false, "obs-studio/logs", lastLogFile);
#ifdef _WIN32
    get_last_log(true, "obs-studio/crashes", lastCrashLogFile);
#endif

    currentLogFile = GenerateTimeDateFilename("txt");
    dst << "obs-studio/logs/" << currentLogFile.c_str();

    BPtr<char> path(GetConfigPathPtr(dst.str().c_str()));

#ifdef _WIN32
    BPtr<wchar_t> wpath;
    os_utf8_to_wcs_ptr(path, 0, &wpath);
    logFile.open(wpath, ios_base::in | ios_base::out | ios_base::trunc);
#else
    logFile.open(path, ios_base::in | ios_base::out | ios_base::trunc);
#endif

    if (logFile.is_open()) {
        delete_oldest_file(false, "obs-studio/logs");
        base_set_log_handler(do_log, &logFile);
    } else {
        blog(LOG_ERROR, "Failed to open log file");
    }
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

static auto SnapshotRelease = [](profiler_snapshot_t *snap) {
    profile_snapshot_free(snap);
};

using ProfilerSnapshot =
    std::unique_ptr<profiler_snapshot_t, decltype(SnapshotRelease)>;

ProfilerSnapshot GetSnapshot()
{
    return ProfilerSnapshot{profile_snapshot_create(), SnapshotRelease};
}

static void SaveProfilerData(const ProfilerSnapshot &snap)
{
    if (currentLogFile.empty())
        return;

    auto pos = currentLogFile.rfind('.');
    if (pos == currentLogFile.npos)
        return;

#define LITERAL_SIZE(x) x, (sizeof(x) - 1)
    ostringstream dst;
    dst.write(LITERAL_SIZE("obs-studio/profiler_data/"));
    dst.write(currentLogFile.c_str(), pos);
    dst.write(LITERAL_SIZE(".csv.gz"));
#undef LITERAL_SIZE

    BPtr<char> path = GetConfigPathPtr(dst.str().c_str());
    if (!profiler_snapshot_dump_csv_gz(snap.get(), path))
        blog(LOG_WARNING, "Could not save profiler data to '%s'",
             static_cast<const char *>(path));
}

static auto ProfilerFree = [](void *) {
    profiler_stop();

    auto snap = GetSnapshot();

    profiler_print(snap.get());
    profiler_print_time_between_calls(snap.get());

    SaveProfilerData(snap);

    profiler_free();
};

QAccessibleInterface *accessibleFactory(const QString &classname,
                    QObject *object)
{
    if (classname == QLatin1String("VolumeSlider") && object &&
        object->isWidgetType())
        return new VolumeAccessibleInterface(
            static_cast<QWidget *>(object));

    return nullptr;
}

static const char *run_program_init = "run_program_init";
static int run_program(fstream &logFile, int argc, char *argv[])
{
    int ret = -1;

    auto profilerNameStore = CreateNameStore();

    std::unique_ptr<void, decltype(ProfilerFree)> prof_release(
        static_cast<void *>(&ProfilerFree), ProfilerFree);

    profiler_start();
    profile_register_root(run_program_init, 0);

    ScopeProfiler prof{run_program_init};

#if (QT_VERSION >= QT_VERSION_CHECK(5, 11, 0))
    QGuiApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif

#if !defined(_WIN32) && !defined(__APPLE__) && BROWSER_AVAILABLE
    setenv("QT_NO_GLIB", "1", true);
#endif

    QCoreApplication::addLibraryPath(".");

#if __APPLE__
    InstallNSApplicationSubclass();
#endif

    OBSApp program(argc, argv, profilerNameStore.get());

    try {
        QAccessible::installFactory(accessibleFactory);

        bool created_log = false;

        program.AppInit();
        delete_oldest_file(false, "obs-studio/profiler_data");

        OBSTranslator translator;
        program.installTranslator(&translator);

        /* --------------------------------------- */
        /* check and warn if already running       */

        bool cancel_launch = false;
        bool already_running = false;

#if defined(_WIN32)
        RunOnceMutex rom = GetRunOnceMutex(already_running);
#elif defined(__APPLE__)
        CheckAppWithSameBundleID(already_running);
#elif defined(__linux__)
        RunningInstanceCheck(already_running);
#elif defined(__FreeBSD__) || defined(__DragonFly__)
        PIDFileCheck(already_running);
#endif

        if (!already_running)
        {
            goto run;
        }

        if (!multi)
        {
            QMessageBox::StandardButtons buttons(
                QMessageBox::Yes | QMessageBox::Cancel);
            QMessageBox mb(QMessageBox::Question,
                       QTStr("AlreadyRunning.Title"),
                       QTStr("AlreadyRunning.Text"), buttons,
                       nullptr);
            mb.setButtonText(QMessageBox::Yes,
                     QTStr("AlreadyRunning.LaunchAnyway"));
            mb.setButtonText(QMessageBox::Cancel, QTStr("Cancel"));
            mb.setDefaultButton(QMessageBox::Cancel);

            QMessageBox::StandardButton button;
            button = (QMessageBox::StandardButton)mb.exec();
            cancel_launch = button == QMessageBox::Cancel;
        }

        if (cancel_launch)
            return 0;

        if (!created_log) {
            create_log_file(logFile);
            created_log = true;
        }

        if (multi)
        {
            blog(LOG_INFO, "User enabled --multi flag and is now "
                       "running multiple instances of OBS.");
        }
        else
        {
            blog(LOG_WARNING, "================================");
            blog(LOG_WARNING, "Warning: OBS is already running!");
            blog(LOG_WARNING, "================================");
            blog(LOG_WARNING, "User is now running multiple "
                      "instances of OBS!");
        }

        /* --------------------------------------- */
    run:

#if !defined(_WIN32) && !defined(__APPLE__) && !defined(__FreeBSD__)
        // Mounted by termina during chromeOS linux container startup
        // https://chromium.googlesource.com/chromiumos/overlays/board-overlays/+/master/project-termina/chromeos-base/termina-lxd-scripts/files/lxd_setup.sh
        os_dir_t *crosDir = os_opendir("/opt/google/cros-containers");
        if (crosDir) {
            QMessageBox::StandardButtons buttons(QMessageBox::Ok);
            QMessageBox mb(QMessageBox::Critical,
                       QTStr("ChromeOS.Title"),
                       QTStr("ChromeOS.Text"), buttons,
                       nullptr);

            mb.exec();
            return 0;
        }
#endif

        if (!created_log)
        {
            create_log_file(logFile);
            created_log = true;
        }

        if (argc > 1)
        {
            stringstream stor;
            stor << argv[1];
            for (int i = 2; i < argc; ++i)
            {
                stor << " " << argv[i];
            }
            blog(LOG_INFO, "Command Line Arguments: %s",
                 stor.str().c_str());
        }

        if (!program.OBSInit())
            return 0;

        prof.Stop();

        ret = program.exec();

    }
    catch (const char *error)
    {
        blog(LOG_ERROR, "%s", error);
        OBSErrorBox(nullptr, "%s", error);
    }

    if (restart)
        QProcess::startDetached(qApp->arguments()[0],
                    qApp->arguments());

    return ret;
}

#define MAX_CRASH_REPORT_SIZE (150 * 1024)

#ifdef _WIN32

#define CRASH_MESSAGE                                                      \
    "Woops, OBS has crashed!\n\nWould you like to copy the crash log " \
    "to the clipboard? The crash log will still be saved to:\n\n%s"

static void main_crash_handler(const char *format, va_list args, void *param)
{
    char *text = new char[MAX_CRASH_REPORT_SIZE];

    vsnprintf(text, MAX_CRASH_REPORT_SIZE, format, args);
    text[MAX_CRASH_REPORT_SIZE - 1] = 0;

    string crashFilePath = "obs-studio/crashes";

    delete_oldest_file(true, crashFilePath.c_str());

    string name = crashFilePath + "/";
    name += "Crash " + GenerateTimeDateFilename("txt");

    BPtr<char> path(GetConfigPathPtr(name.c_str()));

    fstream file;

#ifdef _WIN32
    BPtr<wchar_t> wpath;
    os_utf8_to_wcs_ptr(path, 0, &wpath);
    file.open(wpath, ios_base::in | ios_base::out | ios_base::trunc |
                 ios_base::binary);
#else
    file.open(path, ios_base::in | ios_base::out | ios_base::trunc |
                ios_base::binary);
#endif
    file << text;
    file.close();

    string pathString(path.Get());

#ifdef _WIN32
    std::replace(pathString.begin(), pathString.end(), '/', '\\');
#endif

    string absolutePath =
        canonical(filesystem::path(pathString)).u8string();

    size_t size = snprintf(nullptr, 0, CRASH_MESSAGE, absolutePath.c_str());

    unique_ptr<char[]> message_buffer(new char[size + 1]);

    snprintf(message_buffer.get(), size + 1, CRASH_MESSAGE,
         absolutePath.c_str());

    string finalMessage =
        string(message_buffer.get(), message_buffer.get() + size);

    int ret = MessageBoxA(NULL, finalMessage.c_str(), "OBS has crashed!",
                  MB_YESNO | MB_ICONERROR | MB_TASKMODAL);

    if (ret == IDYES) {
        size_t len = strlen(text);

        HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, len);
        memcpy(GlobalLock(mem), text, len);
        GlobalUnlock(mem);

        OpenClipboard(0);
        EmptyClipboard();
        SetClipboardData(CF_TEXT, mem);
        CloseClipboard();
    }

    exit(-1);

    UNUSED_PARAMETER(param);
}

static void load_debug_privilege(void)
{
    const DWORD flags = TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY;
    TOKEN_PRIVILEGES tp;
    HANDLE token;
    LUID val;

    if (!OpenProcessToken(GetCurrentProcess(), flags, &token)) {
        return;
    }

    if (!!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &val)) {
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = val;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        AdjustTokenPrivileges(token, false, &tp, sizeof(tp), NULL,
                      NULL);
    }

    if (!!LookupPrivilegeValue(NULL, SE_INC_BASE_PRIORITY_NAME, &val)) {
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = val;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        if (!AdjustTokenPrivileges(token, false, &tp, sizeof(tp), NULL,
                       NULL)) {
            blog(LOG_INFO, "Could not set privilege to "
                       "increase GPU priority");
        }
    }

    CloseHandle(token);
}
#endif

#ifdef __APPLE__
#define BASE_PATH ".."
#else
#define BASE_PATH "../.."
#endif

#define CONFIG_PATH BASE_PATH "/config"

#ifndef OBS_UNIX_STRUCTURE
#define OBS_UNIX_STRUCTURE 0
#endif

int GetProgramDataPath(char *path, size_t size, const char *name)
{
    return os_get_program_data_path(path, size, name);
}

char *GetProgramDataPathPtr(const char *name)
{
    return os_get_program_data_path_ptr(name);
}

bool GetFileSafeName(const char *name, std::string &file)
{
    size_t base_len = strlen(name);
    size_t len = os_utf8_to_wcs(name, base_len, nullptr, 0);
    std::wstring wfile;

    if (!len)
        return false;

    wfile.resize(len);
    os_utf8_to_wcs(name, base_len, &wfile[0], len + 1);

    for (size_t i = wfile.size(); i > 0; i--) {
        size_t im1 = i - 1;

        if (iswspace(wfile[im1])) {
            wfile[im1] = '_';
        } else if (wfile[im1] != '_' && !iswalnum(wfile[im1])) {
            wfile.erase(im1, 1);
        }
    }

    if (wfile.size() == 0)
        wfile = L"characters_only";

    len = os_wcs_to_utf8(wfile.c_str(), wfile.size(), nullptr, 0);
    if (!len)
        return false;

    file.resize(len);
    os_wcs_to_utf8(wfile.c_str(), wfile.size(), &file[0], len + 1);
    return true;
}

bool GetClosestUnusedFileName(std::string &path, const char *extension)
{
    size_t len = path.size();
    if (extension) {
        path += ".";
        path += extension;
    }

    if (!os_file_exists(path.c_str()))
        return true;

    int index = 1;

    do {
        path.resize(len);
        path += std::to_string(++index);
        if (extension) {
            path += ".";
            path += extension;
        }
    } while (os_file_exists(path.c_str()));

    return true;
}

bool WindowPositionValid(QRect rect)
{
    for (QScreen *screen : QGuiApplication::screens()) {
        if (screen->availableGeometry().intersects(rect))
            return true;
    }
    return false;
}

static inline bool arg_is(const char *arg, const char *long_form,
              const char *short_form)
{
    return (long_form && strcmp(arg, long_form) == 0) ||
           (short_form && strcmp(arg, short_form) == 0);
}

#if !defined(_WIN32) && !defined(__APPLE__)
#define IS_UNIX 1
#endif

/* if using XDG and was previously using an older build of OBS, move config
 * files to XDG directory */
#if defined(USE_XDG) && defined(IS_UNIX)
static void move_to_xdg(void)
{
    char old_path[512];
    char new_path[512];
    char *home = getenv("HOME");
    if (!home)
        return;

    if (snprintf(old_path, 512, "%s/.obs-studio", home) <= 0)
        return;

    /* make base xdg path if it doesn't already exist */
    if (GetConfigPath(new_path, 512, "") <= 0)
        return;
    if (os_mkdirs(new_path) == MKDIR_ERROR)
        return;

    if (GetConfigPath(new_path, 512, "obs-studio") <= 0)
        return;

    if (os_file_exists(old_path) && !os_file_exists(new_path)) {
        rename(old_path, new_path);
    }
}
#endif

static bool update_ffmpeg_output(ConfigFile &config)
{
    if (config_has_user_value(config, "AdvOut", "FFOutputToFile"))
        return false;

    const char *url = config_get_string(config, "AdvOut", "FFURL");
    if (!url)
        return false;

    bool isActualURL = strstr(url, "://") != nullptr;
    if (isActualURL)
        return false;

    string urlStr = url;
    string extension;

    for (size_t i = urlStr.length(); i > 0; i--) {
        size_t idx = i - 1;

        if (urlStr[idx] == '.') {
            extension = &urlStr[i];
        }

        if (urlStr[idx] == '\\' || urlStr[idx] == '/') {
            urlStr[idx] = 0;
            break;
        }
    }

    if (urlStr.empty() || extension.empty())
        return false;

    config_remove_value(config, "AdvOut", "FFURL");
    config_set_string(config, "AdvOut", "FFFilePath", urlStr.c_str());
    config_set_string(config, "AdvOut", "FFExtension", extension.c_str());
    config_set_bool(config, "AdvOut", "FFOutputToFile", true);
    return true;
}

static bool move_reconnect_settings(ConfigFile &config, const char *sec)
{
    bool changed = false;

    if (config_has_user_value(config, sec, "Reconnect")) {
        bool reconnect = config_get_bool(config, sec, "Reconnect");
        config_set_bool(config, "Output", "Reconnect", reconnect);
        changed = true;
    }
    if (config_has_user_value(config, sec, "RetryDelay")) {
        int delay = (int)config_get_uint(config, sec, "RetryDelay");
        config_set_uint(config, "Output", "RetryDelay", delay);
        changed = true;
    }
    if (config_has_user_value(config, sec, "MaxRetries")) {
        int retries = (int)config_get_uint(config, sec, "MaxRetries");
        config_set_uint(config, "Output", "MaxRetries", retries);
        changed = true;
    }

    return changed;
}

static bool update_reconnect(ConfigFile &config)
{
    if (!config_has_user_value(config, "Output", "Mode"))
        return false;

    const char *mode = config_get_string(config, "Output", "Mode");
    if (!mode)
        return false;

    const char *section = (strcmp(mode, "Advanced") == 0) ? "AdvOut"
                                  : "SimpleOutput";

    if (move_reconnect_settings(config, section)) {
        config_remove_value(config, "SimpleOutput", "Reconnect");
        config_remove_value(config, "SimpleOutput", "RetryDelay");
        config_remove_value(config, "SimpleOutput", "MaxRetries");
        config_remove_value(config, "AdvOut", "Reconnect");
        config_remove_value(config, "AdvOut", "RetryDelay");
        config_remove_value(config, "AdvOut", "MaxRetries");
        return true;
    }

    return false;
}

static void convert_x264_settings(obs_data_t *data)
{
    bool use_bufsize = obs_data_get_bool(data, "use_bufsize");

    if (use_bufsize) {
        int buffer_size = (int)obs_data_get_int(data, "buffer_size");
        if (buffer_size == 0)
            obs_data_set_string(data, "rate_control", "CRF");
    }
}

static void convert_14_2_encoder_setting(const char *encoder, const char *file)
{
    obs_data_t *data = obs_data_create_from_json_file_safe(file, "bak");
    obs_data_item_t *cbr_item = obs_data_item_byname(data, "cbr");
    obs_data_item_t *rc_item = obs_data_item_byname(data, "rate_control");
    bool modified = false;
    bool cbr = true;

    if (cbr_item) {
        cbr = obs_data_item_get_bool(cbr_item);
        obs_data_item_unset_user_value(cbr_item);

        obs_data_set_string(data, "rate_control", cbr ? "CBR" : "VBR");

        modified = true;
    }

    if (!rc_item && astrcmpi(encoder, "obs_x264") == 0) {
        if (!cbr_item)
            obs_data_set_string(data, "rate_control", "CBR");
        else if (!cbr)
            convert_x264_settings(data);

        modified = true;
    }

    if (modified)
        obs_data_save_json_safe(data, file, "tmp", "bak");

    obs_data_item_release(&rc_item);
    obs_data_item_release(&cbr_item);
    obs_data_release(data);
}

static void upgrade_settings(void)
{
    char path[512];
    int pathlen = GetConfigPath(path, 512, "obs-studio/basic/profiles");

    if (pathlen <= 0)
        return;
    if (!os_file_exists(path))
        return;

    os_dir_t *dir = os_opendir(path);
    if (!dir)
        return;

    struct os_dirent *ent = os_readdir(dir);

    while (ent) {
        if (ent->directory && strcmp(ent->d_name, ".") != 0 &&
            strcmp(ent->d_name, "..") != 0) {
            strcat(path, "/");
            strcat(path, ent->d_name);
            strcat(path, "/basic.ini");

            ConfigFile config;
            int ret;

            ret = config.Open(path, CONFIG_OPEN_EXISTING);
            if (ret == CONFIG_SUCCESS) {
                if (update_ffmpeg_output(config) ||
                    update_reconnect(config)) {
                    config_save_safe(config, "tmp",
                             nullptr);
                }
            }

            if (config) {
                const char *sEnc = config_get_string(
                    config, "AdvOut", "Encoder");
                const char *rEnc = config_get_string(
                    config, "AdvOut", "RecEncoder");

                /* replace "cbr" option with "rate_control" for
                 * each profile's encoder data */
                path[pathlen] = 0;
                strcat(path, "/");
                strcat(path, ent->d_name);
                strcat(path, "/recordEncoder.json");
                convert_14_2_encoder_setting(rEnc, path);

                path[pathlen] = 0;
                strcat(path, "/");
                strcat(path, ent->d_name);
                strcat(path, "/streamEncoder.json");
                convert_14_2_encoder_setting(sEnc, path);
            }

            path[pathlen] = 0;
        }

        ent = os_readdir(dir);
    }

    os_closedir(dir);
}

void ctrlc_handler(int s)
{
    UNUSED_PARAMETER(s);

    OBSBasic *main = reinterpret_cast<OBSBasic *>(App()->GetMainWindow());
    main->close();
}

int main(int argc, char *argv[])
{
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);

    struct sigaction sig_handler;

    sig_handler.sa_handler = ctrlc_handler;
    sigemptyset(&sig_handler.sa_mask);
    sig_handler.sa_flags = 0;

    sigaction(SIGINT, &sig_handler, NULL);

    /* Block SIGPIPE in all threads, this can happen if a thread calls write on
    a closed pipe. */
    sigset_t sigpipe_mask;
    sigemptyset(&sigpipe_mask);
    sigaddset(&sigpipe_mask, SIGPIPE);
    sigset_t saved_mask;
    if (pthread_sigmask(SIG_BLOCK, &sigpipe_mask, &saved_mask) == -1) {
        perror("pthread_sigmask");
        exit(1);
    }
#endif

#ifdef _WIN32
    obs_init_win32_crash_handler();
    SetErrorMode(SEM_FAILCRITICALERRORS);
    load_debug_privilege();
    base_set_crash_handler(main_crash_handler, nullptr);
#endif

    base_get_log_handler(&def_log_handler, nullptr);

#if defined(USE_XDG) && defined(IS_UNIX)
    move_to_xdg();
#endif

    obs_set_cmdline_args(argc, argv);

    for (int i = 1; i < argc; i++) {
        if (arg_is(argv[i], "--portable", "-p")) {
            portable_mode = true;

        } else if (arg_is(argv[i], "--multi", "-m")) {
            multi = true;

        } else if (arg_is(argv[i], "--verbose", nullptr)) {
            log_verbose = true;

        } else if (arg_is(argv[i], "--always-on-top", nullptr)) {
            opt_always_on_top = true;

        } else if (arg_is(argv[i], "--unfiltered_log", nullptr)) {
            unfiltered_log = true;

        } else if (arg_is(argv[i], "--startstreaming", nullptr)) {
            opt_start_streaming = true;

        } else if (arg_is(argv[i], "--startrecording", nullptr)) {
            opt_start_recording = true;

        } else if (arg_is(argv[i], "--startreplaybuffer", nullptr)) {
            opt_start_replaybuffer = true;

        } else if (arg_is(argv[i], "--startvirtualcam", nullptr)) {
            opt_start_virtualcam = true;

        } else if (arg_is(argv[i], "--collection", nullptr)) {
            if (++i < argc)
                opt_starting_collection = argv[i];

        } else if (arg_is(argv[i], "--profile", nullptr)) {
            if (++i < argc)
                opt_starting_profile = argv[i];

        } else if (arg_is(argv[i], "--scene", nullptr)) {
            if (++i < argc)
                opt_starting_scene = argv[i];

        } else if (arg_is(argv[i], "--minimize-to-tray", nullptr)) {
            opt_minimize_tray = true;

        } else if (arg_is(argv[i], "--studio-mode", nullptr)) {
            opt_studio_mode = true;

        } else if (arg_is(argv[i], "--allow-opengl", nullptr)) {
            opt_allow_opengl = true;

        } else if (arg_is(argv[i], "--disable-updater", nullptr)) {
            opt_disable_updater = true;

        } else if (arg_is(argv[i], "--help", "-h")) {
            std::string help =
                "--help, -h: Get list of available commands.\n\n"
                "--startstreaming: Automatically start streaming.\n"
                "--startrecording: Automatically start recording.\n"
                "--startreplaybuffer: Start replay buffer.\n"
                "--startvirtualcam: Start virtual camera (if available).\n\n"
                "--collection <string>: Use specific scene collection."
                "\n"
                "--profile <string>: Use specific profile.\n"
                "--scene <string>: Start with specific scene.\n\n"
                "--studio-mode: Enable studio mode.\n"
                "--minimize-to-tray: Minimize to system tray.\n"
                "--portable, -p: Use portable mode.\n"
                "--multi, -m: Don't warn when launching multiple instances.\n\n"
                "--verbose: Make log more verbose.\n"
                "--always-on-top: Start in 'always on top' mode.\n\n"
                "--unfiltered_log: Make log unfiltered.\n\n"
                "--disable-updater: Disable built-in updater (Windows/Mac only)\n\n";

#ifdef _WIN32
            MessageBoxA(NULL, help.c_str(), "Help",
                    MB_OK | MB_ICONASTERISK);
#else
            std::cout << help
                  << "--version, -V: Get current version.\n";
#endif
            exit(0);

        } else if (arg_is(argv[i], "--version", "-V")) {
            std::cout << "OBS Studio - "
                  << App()->GetVersionString() << "\n";
            exit(0);
        }
    }

#if !OBS_UNIX_STRUCTURE
    if (!portable_mode) {
        portable_mode =
            os_file_exists(BASE_PATH "/portable_mode") ||
            os_file_exists(BASE_PATH "/obs_portable_mode") ||
            os_file_exists(BASE_PATH "/portable_mode.txt") ||
            os_file_exists(BASE_PATH "/obs_portable_mode.txt");
    }

    if (!opt_disable_updater) {
        opt_disable_updater =
            os_file_exists(BASE_PATH "/disable_updater") ||
            os_file_exists(BASE_PATH "/disable_updater.txt");
    }
#endif

    upgrade_settings();

    fstream logFile;

    curl_global_init(CURL_GLOBAL_ALL);
    int ret = run_program(logFile, argc, argv);

    blog(LOG_INFO, "Number of memory leaks: %ld", bnum_allocs());
    base_set_log_handler(nullptr, nullptr);
    return ret;
}
