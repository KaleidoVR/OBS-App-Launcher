//=============================================================================
// [KaleidoVR - Part 1: Dependencies & Core Project Module Headers]
// Sets up basic metadata definitions and pulls in external operating system wrappers
//=============================================================================

#include <obs-module.h>
#include <obs-frontend-api.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("KaleidoVR")

// Global runtime registry pointer managed natively by the active OBS Scene Collection
static obs_data_t *plugin_settings = nullptr;

// One live handle per process this session started. Keeping the handle open reserves the
// PID, so cleanup can never land on an unrelated process that inherited a recycled ID.
static std::vector<HANDLE> active_launched_handles;
static std::mutex launch_state_mutex;

// The startup sequence sleeps between spawns, so it runs on a worker instead of the UI thread
static std::thread launch_worker;
static std::atomic<bool> launch_in_progress{false};

static void apply_default_settings(obs_data_t *settings)
{
    if (!settings) return;
    obs_data_set_bool(settings, "auto_start_enabled", true);
    obs_data_set_bool(settings, "auto_close_enabled", false);
    obs_data_set_string(settings, "active_profile", "Default Profile");

    obs_data_t *profiles_obj = obs_data_create();
    obs_data_set_obj(settings, "profiles", profiles_obj);
    obs_data_release(profiles_obj);
}
//=============================================================================
// [KaleidoVR - Part 2: Graphical User Interface Framework Dependencies]
// Pulls in targeted Qt components to render structural frames, tables, and buttons
//=============================================================================

#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLabel>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QTimer>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QPointer>
//=============================================================================
// [KaleidoVR - Part 3: Compact Application Row Element Widget]
// Defines the item row layout template containing path labeling and state controls
//=============================================================================

class AppItemWidget : public QWidget {
public:
    AppItemWidget(const QString &path, bool isMinimized, QWidget *parent = nullptr) : QWidget(parent), appPath(path) {
        QHBoxLayout *layout = new QHBoxLayout(this);
        layout->setContentsMargins(2, 1, 2, 1);
        layout->setSpacing(6);

        QFileInfo fileInfo(path);
        QLabel *nameLabel = new QLabel(fileInfo.fileName(), this);
        nameLabel->setToolTip(path);

        minCheck = new QCheckBox("Start Minimized", this);
        minCheck->setChecked(isMinimized);

        layout->addWidget(nameLabel, 1);
        layout->addWidget(minCheck);

        if (parent) {
            connect(minCheck, &QCheckBox::toggled, [parent](bool) {
                QMetaObject::invokeMethod(parent, "AutoSaveConfig");
            });
        }
    }

    QString GetPath() const { return appPath; }
    bool IsMinimized() const { return minCheck->isChecked(); }

private:
    QString appPath;
    QCheckBox *minCheck;
};
//=============================================================================
// [KaleidoVR - Part 4: Low-Level Win32 Spawning, Handle Tracking & Window State Control]
// Manages process execution, single-instance window state checks, and process snapshots
//=============================================================================

// Text helpers. These run on the launch worker, so they stay free of Qt types.
static std::wstring utf8_to_wide(const char *value) {
    if (!value || !*value) return std::wstring();
    int needed = MultiByteToWideChar(CP_UTF8, 0, value, -1, nullptr, 0);
    if (needed <= 1) return std::wstring();

    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value, -1, out.data(), needed);
    out.pop_back();
    return out;
}

static std::string wide_to_utf8(const std::wstring &value) {
    if (value.empty()) return std::string();
    int needed = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr,
                                     nullptr);
    if (needed <= 0) return std::string();

    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

static std::wstring file_name_of(const std::wstring &path) {
    size_t sep = path.find_last_of(L"\\/");
    return (sep == std::wstring::npos) ? path : path.substr(sep + 1);
}

static std::wstring directory_of(const std::wstring &path) {
    size_t sep = path.find_last_of(L"\\/");
    return (sep == std::wstring::npos) ? std::wstring() : path.substr(0, sep);
}

// A single pass over the process table, reused for every window we inspect. Taking a
// fresh snapshot per window makes EnumWindows quadratic and costs hundreds of ms.
struct ProcessSnapshot {
    std::vector<std::pair<DWORD, std::wstring>> entries;
};

static ProcessSnapshot take_process_snapshot() {
    ProcessSnapshot snap;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return snap;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(hSnap, &pe)) {
        do {
            snap.entries.emplace_back(pe.th32ProcessID, pe.szExeFile);
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return snap;
}

static bool snapshot_pid_matches(const ProcessSnapshot &snap, DWORD pid, const std::wstring &exeName) {
    for (const auto &entry : snap.entries) {
        if (entry.first == pid) return _wcsicmp(entry.second.c_str(), exeName.c_str()) == 0;
    }
    return false;
}

static std::vector<DWORD> get_running_pids_for_exe(const ProcessSnapshot &snap, const std::wstring &exeName) {
    std::vector<DWORD> pids;
    for (const auto &entry : snap.entries) {
        if (_wcsicmp(entry.second.c_str(), exeName.c_str()) == 0) pids.push_back(entry.first);
    }
    return pids;
}

struct WindowSearchContext {
    const ProcessSnapshot *snapshot;
    std::wstring targetExeName;
    std::vector<HWND> foundWindows;
};

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    WindowSearchContext *ctx = reinterpret_cast<WindowSearchContext *>(lParam);
    if (!IsWindowVisible(hwnd)) return TRUE;

    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (processId && snapshot_pid_matches(*ctx->snapshot, processId, ctx->targetExeName)) {
        ctx->foundWindows.push_back(hwnd);
    }
    return TRUE;
}

static std::vector<HWND> find_windows_for_exe(const ProcessSnapshot &snap, const std::wstring &exeName) {
    WindowSearchContext ctx;
    ctx.snapshot = &snap;
    ctx.targetExeName = exeName;
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.foundWindows;
}

static void track_launched_handle(HANDLE hProcess) {
    if (!hProcess) return;
    std::lock_guard<std::mutex> lock(launch_state_mutex);
    active_launched_handles.push_back(hProcess);
}

static void minimize_windows_for_pid(DWORD pid, const std::wstring &exeName) {
    ProcessSnapshot snap = take_process_snapshot();
    for (HWND hwnd : find_windows_for_exe(snap, exeName)) {
        DWORD hwndPid = 0;
        GetWindowThreadProcessId(hwnd, &hwndPid);
        if (hwndPid == pid) ShowWindow(hwnd, SW_MINIMIZE);
    }
}

static void spawn_target_process_silent(const std::wstring &path, bool minimize, bool allowFocusChange)
{
    if (path.empty()) return;

    const std::wstring exeName = file_name_of(path);
    const std::wstring workDir = directory_of(path);
    const std::string exeNameUtf8 = wide_to_utf8(exeName);

    ProcessSnapshot preSnap = take_process_snapshot();

    // Already on screen: adjust the existing window rather than starting a second copy.
    std::vector<HWND> existingWindows = find_windows_for_exe(preSnap, exeName);
    if (!existingWindows.empty()) {
        for (HWND hwnd : existingWindows) {
            if (minimize) {
                ShowWindow(hwnd, SW_MINIMIZE);
            } else if (allowFocusChange) {
                if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
                SetForegroundWindow(hwnd);
            }
        }
        blog(LOG_INFO, "[Kaleido] App already running: %s", exeNameUtf8.c_str());
        return;
    }

    std::vector<DWORD> existingPids = get_running_pids_for_exe(preSnap, exeName);

    // ShellExecuteEx hands back the process handle directly and lets us set the working
    // directory, which apps that load data files next to their exe depend on.
    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    info.lpVerb = L"open";
    info.lpFile = path.c_str();
    info.lpDirectory = workDir.empty() ? nullptr : workDir.c_str();
    info.nShow = minimize ? SW_SHOWMINNOACTIVE : SW_SHOWNORMAL;

    if (!ShellExecuteExW(&info)) {
        blog(LOG_WARNING, "[Kaleido] Failed to launch %s (error %lu)", exeNameUtf8.c_str(), GetLastError());
        return;
    }

    if (info.hProcess) {
        DWORD pid = GetProcessId(info.hProcess);
        track_launched_handle(info.hProcess);
        blog(LOG_INFO, "[Kaleido] Launched PID %lu (%s)", pid, exeNameUtf8.c_str());
        if (minimize) {
            Sleep(600);
            minimize_windows_for_pid(pid, exeName);
        }
        return;
    }

    // Bootstrappers that hand off to a helper process leave hProcess null, so fall back
    // to diffing the process table by name and adopt every new match, not just the first.
    Sleep(400);

    ProcessSnapshot postSnap = take_process_snapshot();
    for (DWORD pid : get_running_pids_for_exe(postSnap, exeName)) {
        if (std::find(existingPids.begin(), existingPids.end(), pid) != existingPids.end()) continue;

        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (hProcess) {
            track_launched_handle(hProcess);
            blog(LOG_INFO, "[Kaleido] Launched PID %lu (%s)", pid, exeNameUtf8.c_str());
        }

        if (minimize) {
            Sleep(200);
            minimize_windows_for_pid(pid, exeName);
        }
    }
}
//=============================================================================
// [KaleidoVR - Part 5: Application Startup Pipeline Matrix]
// Sequentially executes application arrays matching the active layout profile
//=============================================================================

struct StartupEntry {
    std::wstring path;
    bool minimized;
};

// Reads the active profile from the OBS data store. Must run on the thread that owns
// plugin_settings; the returned list is self-contained so the worker never touches obs_data.
static std::vector<StartupEntry> collect_startup_entries()
{
    std::vector<StartupEntry> entries;
    if (!plugin_settings) return entries;

    const char *profileName = obs_data_get_string(plugin_settings, "active_profile");
    if (!profileName || strlen(profileName) == 0) profileName = "Default Profile";

    obs_data_t *profiles_obj = obs_data_get_obj(plugin_settings, "profiles");
    if (!profiles_obj) return entries;

    obs_data_array_t *app_array = obs_data_get_array(profiles_obj, profileName);
    obs_data_release(profiles_obj);
    if (!app_array) return entries;

    size_t count = obs_data_array_count(app_array);
    for (size_t i = 0; i < count; ++i) {
        obs_data_t *app_obj = obs_data_array_item(app_array, i);
        if (!app_obj) continue;

        const char *path_utf8 = obs_data_get_string(app_obj, "path");
        if (path_utf8 && strlen(path_utf8) > 0) {
            StartupEntry entry;
            entry.path = utf8_to_wide(path_utf8);
            entry.minimized = obs_data_get_bool(app_obj, "minimized");
            if (!entry.path.empty()) entries.push_back(std::move(entry));
        }
        obs_data_release(app_obj);
    }
    obs_data_array_release(app_array);

    if (!entries.empty()) {
        blog(LOG_INFO, "[Kaleido] Starting profile '%s' (%zu apps)", profileName, entries.size());
    }
    return entries;
}

static void run_startup_entries(const std::vector<StartupEntry> &entries, bool allowFocusChange)
{
    // ShellExecuteEx needs an initialized COM apartment on whichever thread calls it.
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    for (const StartupEntry &entry : entries) {
        DWORD attrs = GetFileAttributesW(entry.path.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            blog(LOG_WARNING, "[Kaleido] Skipped missing app: %s", wide_to_utf8(file_name_of(entry.path)).c_str());
            continue;
        }
        spawn_target_process_silent(entry.path, entry.minimized, allowFocusChange);
    }

    if (SUCCEEDED(hr)) CoUninitialize();
}

// Snapshots the profile on the caller's thread, then performs the blocking spawn work on
// a worker so OBS stays responsive while apps come up.
static void execute_live_startup_sequence(bool allowFocusChange)
{
    std::vector<StartupEntry> entries = collect_startup_entries();
    if (entries.empty()) return;

    bool expected = false;
    if (!launch_in_progress.compare_exchange_strong(expected, true)) {
        blog(LOG_INFO, "[Kaleido] Launch already in progress, ignoring request.");
        return;
    }

    if (launch_worker.joinable()) launch_worker.join();
    launch_worker = std::thread([entries, allowFocusChange]() {
        run_startup_entries(entries, allowFocusChange);
        launch_in_progress = false;
    });
}
//=============================================================================
// [KaleidoVR - Part 6: Targeted Task Termination Engine]
// Closes exclusively the processes that were spawned by this plugin session
//=============================================================================

static BOOL CALLBACK PostCloseProc(HWND hwnd, LPARAM lParam) {
    DWORD targetPid = static_cast<DWORD>(lParam);
    DWORD hwndPid = 0;
    GetWindowThreadProcessId(hwnd, &hwndPid);
    if (hwndPid == targetPid) PostMessageW(hwnd, WM_CLOSE, 0, 0);
    return TRUE;
}

static void execute_live_termination_sequence()
{
    std::vector<HANDLE> handles;
    {
        std::lock_guard<std::mutex> lock(launch_state_mutex);
        handles.swap(active_launched_handles);
    }
    if (handles.empty()) return;

    // Ask each app to shut down first so it can flush its own state, then force whatever
    // is still alive once the grace period expires.
    for (HANDLE hProcess : handles) {
        DWORD pid = GetProcessId(hProcess);
        if (pid) EnumWindows(PostCloseProc, static_cast<LPARAM>(pid));
    }

    const ULONGLONG deadline = GetTickCount64() + 2000;
    for (HANDLE hProcess : handles) {
        ULONGLONG now = GetTickCount64();
        DWORD remaining = (deadline > now) ? static_cast<DWORD>(deadline - now) : 0;
        if (WaitForSingleObject(hProcess, remaining) != WAIT_OBJECT_0) {
            TerminateProcess(hProcess, 0);
        }
        CloseHandle(hProcess);
    }

    blog(LOG_INFO, "[Kaleido] Closed %zu session-launched processes", handles.size());
}
//=============================================================================
// [KaleidoVR - Part 7: Dashboard Window Class Framework & Geometry Init]
// Sets up user interface constraints and restores UI positions from Scene Collection
//=============================================================================

class KaleidoLauncherWindow : public QDialog {
    Q_OBJECT 

public:
    KaleidoLauncherWindow(QWidget *parent = nullptr) : QDialog(parent) {
        setWindowTitle("Kaleido Launcher");
        setMinimumSize(400, 500);
        resize(460, 620);
        
        QVBoxLayout *mainLayout = new QVBoxLayout(this);
        mainLayout->setSpacing(14);
        mainLayout->setContentsMargins(18, 18, 18, 18);

        enableAutoStartCheck = new QCheckBox("Enable Automatic Startup", this);
        enableAutoStartCheck->setStyleSheet("font-weight: bold; font-size: 12px; color: #1588e6;");
        enableAutoStartCheck->setChecked(plugin_settings ? obs_data_get_bool(plugin_settings, "auto_start_enabled") : true);
        enableAutoStartCheck->setFixedHeight(24);
        mainLayout->addWidget(enableAutoStartCheck);

        QLabel *loadoutTitle = new QLabel("Select Profile:", this);
        loadoutTitle->setStyleSheet("font-weight: bold;");
        mainLayout->addWidget(loadoutTitle);

        QHBoxLayout *loadoutLayout = new QHBoxLayout();
        loadoutCombo = new QComboBox(this);
        loadoutCombo->addItem("Default Profile");
        
        if (plugin_settings) {
            obs_data_t *profiles_obj = obs_data_get_obj(plugin_settings, "profiles");
            if (profiles_obj) {
                obs_data_item_t *item = obs_data_first(profiles_obj);
                while (item) {
                    const char *prof_name = obs_data_item_get_name(item);
                    if (prof_name && strcmp(prof_name, "Default Profile") != 0) {
                        loadoutCombo->addItem(prof_name);
                    }
                    obs_data_item_next(&item);
                }
                obs_data_release(profiles_obj);
            }
            
            const char* savedProf = obs_data_get_string(plugin_settings, "active_profile");
            if (savedProf && strlen(savedProf) > 0) {
                loadoutCombo->setCurrentText(savedProf);
            }
        }
        
        loadoutCombo->setFixedHeight(40);
        loadoutCombo->setStyleSheet(
            "QComboBox { font-size: 13px; font-weight: bold; padding-left: 12px; padding-right: 12px; }"
            "QComboBox::drop-down { width: 24px; border: none; }"
        );
        
        QPushButton *addLoadoutBtn = new QPushButton("New Profile", this);
        QPushButton *removeLoadoutBtn = new QPushButton("Delete Profile", this);
        addLoadoutBtn->setMaximumWidth(100);
        removeLoadoutBtn->setMaximumWidth(110);
        
        style_premium_large_btn(addLoadoutBtn);
        style_premium_large_btn(removeLoadoutBtn);
        
        loadoutLayout->addWidget(loadoutCombo, 1);
        loadoutLayout->addWidget(addLoadoutBtn);
        layout_handling_btn_append(loadoutLayout, removeLoadoutBtn, mainLayout, addLoadoutBtn);

        if (plugin_settings) {
            const char *geom_hex = obs_data_get_string(plugin_settings, "ui_window_geometry");
            if (geom_hex && strlen(geom_hex) > 0) {
                restoreGeometry(QByteArray::fromHex(geom_hex));
            }
        }
    }
//=============================================================================
// [KaleidoVR - Part 8: Control Element Insertion & Button Layout Assemblies]
// Procedurally instantiates interaction grids and builds main tracking rows
//=============================================================================

private:
    void layout_handling_btn_append(QHBoxLayout *loadoutLayout, QPushButton *removeLoadoutBtn, QVBoxLayout *mainLayout, QPushButton *addLoadoutBtn) {
        loadoutLayout->addWidget(removeLoadoutBtn);
        mainLayout->addLayout(loadoutLayout);
        
        QLabel *listTitle = new QLabel("Startup Applications:", this);
        mainLayout->addWidget(listTitle);

        programList = new QListWidget(this);
        programList->setStyleSheet("QListWidget { font-size: 11px; padding: 4px; }");
        mainLayout->addWidget(programList);
        
        QHBoxLayout *itemActionLayout = new QHBoxLayout();
        QPushButton *addAppBtn = new QPushButton("+ Add App", this);
        QPushButton *removeAppBtn = new QPushButton("- Remove App", this);
        QPushButton *saveProfileBtn = new QPushButton("💾 Save to Scene Collection", this);
        
        style_premium_large_btn(addAppBtn);
        style_premium_large_btn(removeAppBtn);
        style_premium_large_btn(saveProfileBtn);
        
        itemActionLayout->addWidget(addAppBtn);
        itemActionLayout->addWidget(removeAppBtn);
        itemActionLayout->addWidget(saveProfileBtn);
        mainLayout->addLayout(itemActionLayout);
        
        autocloseCheck = new QCheckBox("Close apps automatically when OBS exits", this);
        autocloseCheck->setChecked(plugin_settings ? obs_data_get_bool(plugin_settings, "auto_close_enabled") : false);
        autocloseCheck->setMinimumHeight(24);
        mainLayout->addWidget(autocloseCheck);
        
        QHBoxLayout *executionLayout = new QHBoxLayout();
        QPushButton *launchBtn = new QPushButton("⚡ Launch Apps", this);
        QPushButton *quitBtn = new QPushButton("🛑 Close Apps", this);
        style_premium_large_btn(launchBtn);
        style_premium_large_btn(quitBtn);
        executionLayout->addWidget(launchBtn);
        executionLayout->addWidget(quitBtn);
        mainLayout->addLayout(executionLayout);
        
        QHBoxLayout *backupActionLayout = new QHBoxLayout();
        QPushButton *exportBtn = new QPushButton("📁 Export Settings Backup", this);
        QPushButton *importBtn = new QPushButton("📥 Import Settings Backup", this);
        style_premium_large_btn(exportBtn);
        style_premium_large_btn(importBtn);
        backupActionLayout->addWidget(exportBtn);
        backupActionLayout->addWidget(importBtn);
        mainLayout->addLayout(backupActionLayout);

        QHBoxLayout *footerButtons = new QHBoxLayout();
        QPushButton *closeBtn = new QPushButton("Close", this);
        closeBtn->setFixedWidth(90);
        closeBtn->setFixedHeight(30);
        footerButtons->addStretch();
        footerButtons->addWidget(closeBtn);
        mainLayout->addLayout(footerButtons);

        QLabel *creditsLabel = new QLabel(this);
        creditsLabel->setText("Kaleido Launcher — <a href=\"https://kalivr.com\" style=\"color: #61afef; text-decoration: none;\">https://kalivr.com</a>");
        creditsLabel->setOpenExternalLinks(true);
        creditsLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
        creditsLabel->setStyleSheet("color: #666; font-size: 10px; font-style: italic;");
        mainLayout->addWidget(creditsLabel);
        
        connect_ui_signals_to_slots(closeBtn, addAppBtn, removeAppBtn, saveProfileBtn, launchBtn, quitBtn, exportBtn, importBtn, addLoadoutBtn, removeLoadoutBtn);
    }
//=============================================================================
// [KaleidoVR - Part 9: Signal Slot Routing & Dropdown Signal Controls]
// Maps widget events and implements signal controls to safely load config structures
//=============================================================================

    void connect_ui_signals_to_slots(QPushButton *closeBtn, QPushButton *addAppBtn, QPushButton *removeAppBtn, QPushButton *saveProfileBtn, QPushButton *launchBtn, QPushButton *quitBtn, QPushButton *exportBtn, QPushButton *importBtn, QPushButton *addLoadoutBtn, QPushButton *removeLoadoutBtn) {
        connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
        connect(addAppBtn, &QPushButton::clicked, this, &KaleidoLauncherWindow::OnAddApplication);
        connect(removeAppBtn, &QPushButton::clicked, this, &KaleidoLauncherWindow::OnRemoveApplication);
        connect(saveProfileBtn, &QPushButton::clicked, this, &KaleidoLauncherWindow::OnSaveToCollection);
        connect(launchBtn, &QPushButton::clicked, this, &KaleidoLauncherWindow::OnManualLaunch);
        connect(quitBtn, &QPushButton::clicked, this, &KaleidoLauncherWindow::OnManualClose);
        connect(exportBtn, &QPushButton::clicked, this, &KaleidoLauncherWindow::OnExportSettings);
        connect(importBtn, &QPushButton::clicked, this, &KaleidoLauncherWindow::OnImportSettings);
        
        connect(addLoadoutBtn, &QPushButton::clicked, this, &KaleidoLauncherWindow::OnAddProfile);
        connect(removeLoadoutBtn, &QPushButton::clicked, this, &KaleidoLauncherWindow::OnRemoveProfile);
        
        connect(enableAutoStartCheck, &QCheckBox::clicked, this, &KaleidoLauncherWindow::AutoSaveConfig);
        connect(autocloseCheck, &QCheckBox::clicked, this, &KaleidoLauncherWindow::AutoSaveConfig);
        
        connect(loadoutCombo, &QComboBox::currentTextChanged, [this](const QString &text) {
            if (!text.isEmpty()) {
                programList->clear();
                loadoutCombo->blockSignals(true);
                if (plugin_settings) {
                    obs_data_set_string(plugin_settings, "active_profile", text.toUtf8().constData());
                }
                LoadSavedConfig();
                loadoutCombo->blockSignals(false);
            }
        });
        
        loadoutCombo->blockSignals(true);
        LoadSavedConfig();
        loadoutCombo->blockSignals(false);
    }
//=============================================================================
// [KaleidoVR - Part 10: Dynamic JSON Object Serialization System]
// Packs current window states, profiles, and path tables directly to the collection registry
//=============================================================================

public slots:
    void AutoSaveConfig() {
        if (!plugin_settings) return;
        obs_data_set_bool(plugin_settings, "auto_start_enabled", enableAutoStartCheck->isChecked());
        obs_data_set_bool(plugin_settings, "auto_close_enabled", autocloseCheck->isChecked());
        
        std::string current_prof = loadoutCombo->currentText().toUtf8().constData();
        if (current_prof.empty()) current_prof = "Default Profile";
        obs_data_set_string(plugin_settings, "active_profile", current_prof.c_str());

        QByteArray geom = saveGeometry();
        obs_data_set_string(plugin_settings, "ui_window_geometry", geom.toHex().constData());

        obs_data_t *profiles_obj = obs_data_get_obj(plugin_settings, "profiles");
        if (!profiles_obj) profiles_obj = obs_data_create();

        obs_data_array_t *app_array = obs_data_array_create();
        for (int i = 0; i < programList->count(); ++i) {
            QListWidgetItem *item = programList->item(i);
            if (!item) continue;
            
            AppItemWidget *widget = static_cast<AppItemWidget*>(programList->itemWidget(item));
            if (widget) {
                obs_data_t *app_obj = obs_data_create();
                obs_data_set_string(app_obj, "path", widget->GetPath().toUtf8().constData());
                obs_data_set_bool(app_obj, "minimized", widget->IsMinimized());
                obs_data_array_push_back(app_array, app_obj);
                obs_data_release(app_obj);
            }
        }
        
        obs_data_set_array(profiles_obj, current_prof.c_str(), app_array);
        obs_data_array_release(app_array);

        obs_data_set_obj(plugin_settings, "profiles", profiles_obj);
        obs_data_release(profiles_obj);
    }
//=============================================================================
// [KaleidoVR - Part 11: File Parsing Deserialization & Helper Layout Context]
// Implements manual backup, formatting buttons, and loads saved configurations
//=============================================================================

private:
    void style_premium_large_btn(QPushButton *btn) {
        btn->setFixedHeight(40); 
        btn->setStyleSheet("QPushButton { font-size: 13px; font-weight: bold; padding-left: 16px; padding-right: 16px; }");
    }

    void AddAppRow(const QString &path, bool minimized) {
        QListWidgetItem *item = new QListWidgetItem(programList);
        AppItemWidget *widget = new AppItemWidget(path, minimized, this);
        item->setSizeHint(widget->sizeHint());
        programList->addItem(item);
        programList->setItemWidget(item, widget);
    }

    void OnAddApplication() {
        QString file = QFileDialog::getOpenFileName(this, "Select Application", "", "Executables (*.exe)");
        if (!file.isEmpty()) { 
            AddAppRow(file, false); 
            AutoSaveConfig();
        }
    }

    void OnRemoveApplication() {
        int currentRow = programList->currentRow();
        if (currentRow >= 0) { 
            delete programList->takeItem(currentRow); 
            AutoSaveConfig();
        }
    }

    void OnAddProfile() {
        bool ok;
        QString text = QInputDialog::getText(this, "New Profile", "Enter Profile Name:", QLineEdit::Normal, "", &ok);
        if (ok && !text.trimmed().isEmpty()) {
            QString name = text.trimmed();
            // Two combo entries with the same name would share one storage key.
            if (loadoutCombo->findText(name) >= 0) {
                loadoutCombo->setCurrentText(name);
                return;
            }
            loadoutCombo->addItem(name);
            loadoutCombo->setCurrentText(name);
            AutoSaveConfig();
        }
    }

    void OnRemoveProfile() {
        if (loadoutCombo->count() <= 1) return;
        if (!plugin_settings) return;
        std::string target_prof = loadoutCombo->currentText().toUtf8().constData();
        
        obs_data_t *profiles_obj = obs_data_get_obj(plugin_settings, "profiles");
        if (profiles_obj) {
            obs_data_erase(profiles_obj, target_prof.c_str());
            obs_data_set_obj(plugin_settings, "profiles", profiles_obj);
            obs_data_release(profiles_obj);
        }
        
        loadoutCombo->removeItem(loadoutCombo->currentIndex());
        AutoSaveConfig();
    }

    void OnManualLaunch() { 
        AutoSaveConfig(); 
        execute_live_startup_sequence(true); 
    }
    
    void OnManualClose() { execute_live_termination_sequence(); }

    // AutoSaveConfig only updates the in-memory store; ask OBS to flush the scene
    // collection so the button actually writes to disk like its label promises.
    void OnSaveToCollection() {
        AutoSaveConfig();
        obs_frontend_save();
    }

    void OnExportSettings() {
        QString file = QFileDialog::getSaveFileName(this, "Export Configuration Backup", "kaleido_backup.json", "JSON Configuration (*.json)");
        if (!file.isEmpty() && plugin_settings) {
            AutoSaveConfig();
            const char *json_str = obs_data_get_json_pretty(plugin_settings);
            if (json_str) {
                // Format the JSON string with indented formatting and line breaks
                std::string formattedJson = json_str;
                
                QFile f(file);
                if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                    f.write(formattedJson.c_str());
                    f.close();
                    blog(LOG_INFO, "[Kaleido] Exported formatted config backup: %s", QFileInfo(file).fileName().toUtf8().constData());
                    QMessageBox::information(this, "Export Complete", "Backup configuration written successfully.");
                } else {
                    QMessageBox::warning(this, "Export Error", "Failed to open backup file for writing.");
                }
            }
        }
    }

    // Counts every application entry across all profiles in a candidate backup.
    static int CountBackupApps(obs_data_t *backup) {
        int total = 0;
        obs_data_t *profiles_obj = obs_data_get_obj(backup, "profiles");
        if (!profiles_obj) return 0;

        obs_data_item_t *prof = obs_data_first(profiles_obj);
        while (prof) {
            obs_data_array_t *arr = obs_data_item_get_array(prof);
            if (arr) {
                total += static_cast<int>(obs_data_array_count(arr));
                obs_data_array_release(arr);
            }
            obs_data_item_next(&prof);
        }
        obs_data_release(profiles_obj);
        return total;
    }

    void OnImportSettings() {
        QString file = QFileDialog::getOpenFileName(this, "Import Configuration Backup", "", "JSON Configuration (*.json)");
        if (file.isEmpty()) return;

        obs_data_t *imported = obs_data_create_from_json_file(file.toUtf8().constData());
        if (!imported) {
            QMessageBox::warning(this, "Import Error", "That file could not be read as a valid JSON configuration.");
            return;
        }

        // A backup names executables this plugin will start on its own, so confirm before
        // trusting a file the user may have received from someone else.
        int appCount = CountBackupApps(imported);
        QMessageBox::StandardButton answer = QMessageBox::question(
            this, "Import Configuration",
            QString("This backup contains %1 application entr%2 that Kaleido Launcher can start automatically.\n\n"
                    "Only import backups you trust. Replace your current settings?")
                .arg(appCount)
                .arg(appCount == 1 ? "y" : "ies"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

        if (answer != QMessageBox::Yes) {
            obs_data_release(imported);
            return;
        }

        // Layer the backup over a fresh set of defaults so a partial file cannot leave
        // required keys missing.
        obs_data_t *merged = obs_data_create();
        apply_default_settings(merged);
        obs_data_apply(merged, imported);
        obs_data_release(imported);

        if (plugin_settings) obs_data_release(plugin_settings);
        plugin_settings = merged;
        programList->clear();

        loadoutCombo->blockSignals(true);
        loadoutCombo->clear();
        loadoutCombo->addItem("Default Profile");

        obs_data_t *profiles_obj = obs_data_get_obj(plugin_settings, "profiles");
        if (profiles_obj) {
            obs_data_item_t *item = obs_data_first(profiles_obj);
            while (item) {
                const char *prof_name = obs_data_item_get_name(item);
                if (prof_name && strcmp(prof_name, "Default Profile") != 0) {
                    loadoutCombo->addItem(prof_name);
                }
                obs_data_item_next(&item);
            }
            obs_data_release(profiles_obj);
        }

        enableAutoStartCheck->setChecked(obs_data_get_bool(plugin_settings, "auto_start_enabled"));
        autocloseCheck->setChecked(obs_data_get_bool(plugin_settings, "auto_close_enabled"));
        loadoutCombo->setCurrentText(obs_data_get_string(plugin_settings, "active_profile"));
        loadoutCombo->blockSignals(false);

        LoadSavedConfig();
        AutoSaveConfig();
        blog(LOG_INFO, "[Kaleido] Imported config backup successfully.");
    }

    void LoadSavedConfig() {
        programList->clear();
        if (!plugin_settings) return;
        std::string current_prof = loadoutCombo->currentText().toUtf8().constData();
        if (current_prof.empty()) current_prof = "Default Profile";
        
        obs_data_t *profiles_obj = obs_data_get_obj(plugin_settings, "profiles");
        if (profiles_obj) {
            obs_data_array_t *app_array = obs_data_get_array(profiles_obj, current_prof.c_str());
            if (app_array) {
                size_t count = obs_data_array_count(app_array);
                for (size_t i = 0; i < count; ++i) {
                    obs_data_t *app_obj = obs_data_array_item(app_array, i);
                    if (app_obj) {
                        const char *path = obs_data_get_string(app_obj, "path");
                        bool minimized = obs_data_get_bool(app_obj, "minimized");
                        if (path && strlen(path) > 0) {
                            AddAppRow(QString::fromUtf8(path), minimized);
                        }
                        obs_data_release(app_obj);
                    }
                }
                obs_data_array_release(app_array);
            }
            obs_data_release(profiles_obj);
        }
    }

    QCheckBox *enableAutoStartCheck;
    QComboBox *loadoutCombo;
    QListWidget *programList;
    QCheckBox *autocloseCheck;
};
//=============================================================================
// [KaleidoVR - Part 12: OBS Lifecycle Hooks & Save Callbacks]
// Hooks layout generation directly to native OBS Scene Collection save states
//=============================================================================

#include "plugin-main.moc"

// save_data is the scene collection root, shared with OBS and every other plugin, so our
// settings live under a single namespaced key instead of being merged into the top level.
static const char *const KALEIDO_SETTINGS_KEY = "kaleido_launcher";

// Keys that earlier builds wrote directly into the scene collection root.
static const char *const KALEIDO_LEGACY_KEYS[] = {"auto_start_enabled", "auto_close_enabled", "active_profile",
                                                  "ui_window_geometry", "profiles"};

static QPointer<KaleidoLauncherWindow> launcher_window;

// Lifts a pre-namespace configuration out of the collection root so existing setups
// survive the move without the user having to rebuild their profiles.
static obs_data_t *extract_legacy_settings(obs_data_t *save_data) {
    if (!obs_data_has_user_value(save_data, "profiles") && !obs_data_has_user_value(save_data, "active_profile")) {
        return nullptr;
    }

    obs_data_t *legacy = obs_data_create();
    if (obs_data_has_user_value(save_data, "auto_start_enabled")) {
        obs_data_set_bool(legacy, "auto_start_enabled", obs_data_get_bool(save_data, "auto_start_enabled"));
    }
    if (obs_data_has_user_value(save_data, "auto_close_enabled")) {
        obs_data_set_bool(legacy, "auto_close_enabled", obs_data_get_bool(save_data, "auto_close_enabled"));
    }
    if (obs_data_has_user_value(save_data, "active_profile")) {
        obs_data_set_string(legacy, "active_profile", obs_data_get_string(save_data, "active_profile"));
    }
    if (obs_data_has_user_value(save_data, "ui_window_geometry")) {
        obs_data_set_string(legacy, "ui_window_geometry", obs_data_get_string(save_data, "ui_window_geometry"));
    }

    obs_data_t *profiles = obs_data_get_obj(save_data, "profiles");
    if (profiles) {
        obs_data_set_obj(legacy, "profiles", profiles);
        obs_data_release(profiles);
    }

    blog(LOG_INFO, "[Kaleido] Migrated settings from the pre-1.0.1 scene collection layout.");
    return legacy;
}

static void kaleido_launcher_save(obs_data_t *save_data, bool saving, void *) {
    if (!save_data) return;

    if (saving) {
        if (!plugin_settings) return;
        obs_data_set_obj(save_data, KALEIDO_SETTINGS_KEY, plugin_settings);

        // Drop the stale root-level copies once the namespaced object is in place.
        for (const char *key : KALEIDO_LEGACY_KEYS) obs_data_erase(save_data, key);
        return;
    }

    obs_data_t *stored = obs_data_get_obj(save_data, KALEIDO_SETTINGS_KEY);
    if (!stored) stored = extract_legacy_settings(save_data);

    // Rebuild from defaults so switching collections cannot leak the previous one's
    // profiles, while keys the collection omits still get their intended default.
    if (!plugin_settings) plugin_settings = obs_data_create();
    obs_data_clear(plugin_settings);
    apply_default_settings(plugin_settings);

    if (stored) {
        obs_data_apply(plugin_settings, stored);
        obs_data_release(stored);
    }
}

static void on_obs_frontend_event(enum obs_frontend_event event, void *) {
    if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
        QMainWindow *obsMainWin = static_cast<QMainWindow*>(obs_frontend_get_main_window());
        if (obsMainWin && obsMainWin->menuBar()) {
            QMenu *customMenu = new QMenu("KaleidoVR", obsMainWin->menuBar());
            customMenu->addAction("App Autostarter", []() {
                QMainWindow *win = static_cast<QMainWindow*>(obs_frontend_get_main_window());
                if (!win) return;

                // Several dialogs editing the same global settings would fight each other.
                if (launcher_window) {
                    launcher_window->show();
                    launcher_window->raise();
                    launcher_window->activateWindow();
                    return;
                }

                KaleidoLauncherWindow *w = new KaleidoLauncherWindow(win);
                w->setAttribute(Qt::WA_DeleteOnClose);
                launcher_window = w;
                w->show();
            });
            obsMainWin->menuBar()->addMenu(customMenu);
        }
        
        QTimer::singleShot(2500, []() {
            if (plugin_settings && obs_data_get_bool(plugin_settings, "auto_start_enabled")) {
                // Do not pull an already-running app in front of OBS during its own startup.
                execute_live_startup_sequence(false);
            } else {
                blog(LOG_INFO, "[Kaleido] Auto-start bypassed by settings.");
            }
        });
    }
}

bool obs_module_load(void) {
    plugin_settings = obs_data_create();
    apply_default_settings(plugin_settings);

    obs_frontend_add_save_callback(kaleido_launcher_save, nullptr);
    obs_frontend_add_event_callback(on_obs_frontend_event, nullptr);
    return true;
}

void obs_module_unload(void) {
    // OBS holds these function pointers into a DLL that is about to disappear.
    obs_frontend_remove_save_callback(kaleido_launcher_save, nullptr);
    obs_frontend_remove_event_callback(on_obs_frontend_event, nullptr);

    if (launch_worker.joinable()) launch_worker.join();

    if (plugin_settings) {
        if (obs_data_get_bool(plugin_settings, "auto_close_enabled")) {
            execute_live_termination_sequence();
        }
        obs_data_release(plugin_settings);
        plugin_settings = nullptr;
    }

    // Release anything still tracked when auto-close is turned off.
    std::vector<HANDLE> leftover;
    {
        std::lock_guard<std::mutex> lock(launch_state_mutex);
        leftover.swap(active_launched_handles);
    }
    for (HANDLE hProcess : leftover) CloseHandle(hProcess);
}