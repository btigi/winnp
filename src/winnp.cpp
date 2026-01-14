#include "winnp.h"
#include "sqlite3.h"
#include <windows.h>
#include <shlobj.h>
#include <string>
#include <ctime>

// Global variables
HWND hwndWinamp = NULL;
char currentTitle[2048] = "";
char dbPath[MAX_PATH] = "";
HANDLE hTimer = NULL;
HANDLE hTimerQueue = NULL;
winampGeneralPurposePlugin* g_plugin = NULL;
HMODULE g_hModule = NULL;
sqlite3* db = NULL;

// DLL entry point
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        g_hModule = hModule;
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

// Forward declarations
void CALLBACK TimerCallback(PVOID lpParam, BOOLEAN TimerOrWaitFired);
void LogToDatabase(const char* title, const char* filepath);
void GetDatabasePath();
bool InitDatabase();
void CloseDatabase();

// Plugin description string
static char pluginDescription[] = "winnp - Now Playing Logger (SQLite)";

// Plugin structure
winampGeneralPurposePlugin plugin = {
    GPPHDR_VER,
    pluginDescription,
    init,
    config,
    quit,
    NULL,
    NULL
};

// Get the database path (in user's Documents folder)
void GetDatabasePath() {
    if (strlen(dbPath) > 0) return;
    
    char documentsPath[MAX_PATH];
    
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_MYDOCUMENTS, NULL, 0, documentsPath))) {
        snprintf(dbPath, MAX_PATH, "%s\\nowplaying.db", documentsPath);
    } else {
        DWORD size = MAX_PATH;
        char userProfile[MAX_PATH];
        if (GetEnvironmentVariableA("USERPROFILE", userProfile, size) > 0) {
            snprintf(dbPath, MAX_PATH, "%s\\Documents\\nowplaying.db", userProfile);
        } else {
            strncpy_s(dbPath, MAX_PATH, "C:\\nowplaying.db", _TRUNCATE);
        }
    }
}

// Initialize SQLite database
bool InitDatabase() {
    GetDatabasePath();
    
    int rc = sqlite3_open(dbPath, &db);
    if (rc != SQLITE_OK) {
        db = NULL;
        return false;
    }
    
    // Create table if it doesn't exist
    const char* createTableSQL = 
        "CREATE TABLE IF NOT EXISTS play_history ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "    title TEXT NOT NULL,"
        "    filepath TEXT,"
        "    played_at TEXT"
        ");";
    
    char* errMsg = NULL;
    rc = sqlite3_exec(db, createTableSQL, NULL, NULL, &errMsg);
    if (rc != SQLITE_OK) {
        if (errMsg) sqlite3_free(errMsg);
        sqlite3_close(db);
        db = NULL;
        return false;
    }
    
    // Create index on timestamp for faster queries
    const char* createIndexSQL = 
        "CREATE INDEX IF NOT EXISTS idx_timestamp ON play_history(timestamp);";
    sqlite3_exec(db, createIndexSQL, NULL, NULL, NULL);
    
    return true;
}

// Close database connection
void CloseDatabase() {
    if (db) {
        sqlite3_close(db);
        db = NULL;
    }
}

// Log track to database
void LogToDatabase(const char* title, const char* filepath) {
    if (!db || !title || strlen(title) == 0) return;
    
    // Get current local time as string
    time_t now = time(0);
    struct tm timeinfo;
    localtime_s(&timeinfo, &now);
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
    
    // Prepare SQL statement
    const char* insertSQL = "INSERT INTO play_history (title, filepath, played_at) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt = NULL;
    
    int rc = sqlite3_prepare_v2(db, insertSQL, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return;
    
    // Bind parameters
    sqlite3_bind_text(stmt, 1, title, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, filepath ? filepath : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, timeStr, -1, SQLITE_TRANSIENT);
    
    // Execute
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

// Timer callback to check for track changes
void CALLBACK TimerCallback(PVOID lpParam, BOOLEAN TimerOrWaitFired) {
    // Try to get Winamp window handle if we don't have it yet
    if (!hwndWinamp) {
        if (g_plugin && g_plugin->hwndParent) {
            hwndWinamp = g_plugin->hwndParent;
        } else {
            hwndWinamp = FindWindowA("Winamp v1.x", NULL);
        }
        if (!hwndWinamp) {
            return;
        }
    }
    
    // Check if Winamp is playing
    int isPlaying = (int)SendMessage(hwndWinamp, WM_WA_IPC, 0, IPC_ISPLAYING);
    if (isPlaying != 1) return; // 1 = playing, 0 = stopped, 3 = paused
    
    // Get current track info
    char title[2048] = "";
    char filepath[MAX_PATH] = "";
    
    int position = (int)SendMessage(hwndWinamp, WM_WA_IPC, 0, IPC_GETLISTPOS);
    
    if (position >= 0) {
        // Get title
        char* titlePtr = (char*)SendMessage(hwndWinamp, WM_WA_IPC, position, IPC_GETPLAYLISTTITLE);
        if (titlePtr && titlePtr != (char*)-1) {
            strncpy_s(title, sizeof(title), titlePtr, _TRUNCATE);
        }
        
        // Get filepath
        char* filePtr = (char*)SendMessage(hwndWinamp, WM_WA_IPC, position, IPC_GETPLAYLISTFILE);
        if (filePtr && filePtr != (char*)-1) {
            strncpy_s(filepath, sizeof(filepath), filePtr, _TRUNCATE);
        }
    }
    
    // Fallback: get title from window
    if (strlen(title) == 0) {
        char windowTitle[512];
        if (GetWindowTextA(hwndWinamp, windowTitle, sizeof(windowTitle)) > 0) {
            char* dashPos = strstr(windowTitle, " - Winamp");
            if (dashPos) {
                size_t len = dashPos - windowTitle;
                strncpy_s(title, sizeof(title), windowTitle, len);
            }
        }
    }
    
    // Check if track has changed
    if (strlen(title) > 0 && strcmp(title, currentTitle) != 0) {
        strncpy_s(currentTitle, sizeof(currentTitle), title, _TRUNCATE);
        LogToDatabase(title, filepath);
    }
}

// Plugin initialization
int init() {
    if (g_plugin && g_plugin->hwndParent) {
        hwndWinamp = g_plugin->hwndParent;
    } else {
        hwndWinamp = FindWindowA("Winamp v1.x", NULL);
    }
    
    // Initialize database
    if (!InitDatabase()) {
        return 1;
    }
    
    // Create timer queue for periodic checking
    hTimerQueue = CreateTimerQueue();
    if (hTimerQueue) {
        CreateTimerQueueTimer(&hTimer, hTimerQueue, TimerCallback, NULL, 500, 500, WT_EXECUTEINTIMERTHREAD);
    }
    
    return 0;
}

// Plugin configuration
void config() {
    char msg[512];
    snprintf(msg, sizeof(msg),
        "winnp - Now Playing Logger\n\n"
        "Logs currently playing songs to SQLite database:\n"
        "%s\n\n"
        "Table: play_history\n"
        "Columns: id, timestamp, title, filepath, played_at",
        dbPath);
    
    MessageBoxA(NULL, msg, "winnp Configuration", MB_OK | MB_ICONINFORMATION);
}

// Plugin cleanup
void quit() {
    if (hTimer && hTimerQueue) {
        DeleteTimerQueueTimer(hTimerQueue, hTimer, INVALID_HANDLE_VALUE);
    }
    if (hTimerQueue) {
        DeleteTimerQueue(hTimerQueue);
    }
    
    CloseDatabase();
    
    hwndWinamp = NULL;
    hTimer = NULL;
    hTimerQueue = NULL;
}

// Plugin export function
extern "C" __declspec(dllexport) winampGeneralPurposePlugin* winampGetGeneralPurposePlugin() {
    g_plugin = &plugin;
    return &plugin;
}
