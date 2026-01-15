#include "winnp.h"
#include "sqlite3.h"
#include <windows.h>
#include <shlobj.h>
#include <string>
#include <ctime>
#include <cstdlib>

// Global variables
HWND hwndWinamp = NULL;
char currentTitle[2048] = "";
char dbPath[MAX_PATH] = "";
HANDLE hTimer = NULL;
HANDLE hTimerQueue = NULL;
winampGeneralPurposePlugin* g_plugin = NULL;
HMODULE g_hModule = NULL;
sqlite3* db = NULL;
int lastPositionPercent = 0;       // Track position percentage (0-100)
char lastFilepath[MAX_PATH] = "";  // Track filepath for repeat detection

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
void GetExtendedFileInfo(const char* filepath, const char* field, char* buffer, size_t bufferSize);
void GetFilenameFromPath(const char* filepath, char* filename, size_t bufferSize);

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

// Get the database path
void GetDatabasePath() {
    if (strlen(dbPath) > 0) return;
    
    // Read environment variable (read from the registry directly, which feels like a hack
    // but means I don't have to log out/restart anything in order to pick up the env. var.
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Environment", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char regValue[MAX_PATH] = "";
        DWORD regSize = sizeof(regValue);
        DWORD regType = 0;
        
        if (RegQueryValueExA(hKey, "winnp_db_path", NULL, &regType, (LPBYTE)regValue, &regSize) == ERROR_SUCCESS) {
            if (strlen(regValue) > 0) {
                strncpy_s(dbPath, MAX_PATH, regValue, _TRUNCATE);
                RegCloseKey(hKey);
                return;
            }
        }
        RegCloseKey(hKey);
    }
    
    // Fallback to Documents folder
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
    
    // Create table with extended metadata
    const char* createTableSQL = 
        "CREATE TABLE IF NOT EXISTS play_history ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    played_at TEXT NOT NULL,"
        "    filepath TEXT,"
        "    filename TEXT,"
        "    title TEXT,"
        "    artist TEXT,"
        "    album TEXT,"
        "    genre TEXT,"
        "    track_number TEXT,"
        "    year TEXT,"
        "    duration_ms INTEGER"
        ");";
    
    char* errMsg = NULL;
    rc = sqlite3_exec(db, createTableSQL, NULL, NULL, &errMsg);
    if (rc != SQLITE_OK) {
        if (errMsg) sqlite3_free(errMsg);
        sqlite3_close(db);
        db = NULL;
        return false;
    }
    
    // Create index on played_at for faster queries
    const char* createIndexSQL = 
        "CREATE INDEX IF NOT EXISTS idx_played_at ON play_history(played_at);";
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

// Get extended file info from Winamp
void GetExtendedFileInfo(const char* filepath, const char* field, char* buffer, size_t bufferSize) {
    buffer[0] = '\0';
    if (!hwndWinamp || !filepath || strlen(filepath) == 0) return;
    
    extendedFileInfoStruct info;
    info.filename = filepath;
    info.metadata = field;
    info.ret = buffer;
    info.retlen = bufferSize;
    
    SendMessage(hwndWinamp, WM_WA_IPC, (WPARAM)&info, IPC_GET_EXTENDED_FILE_INFO);
}

// Extract filename from full path
void GetFilenameFromPath(const char* filepath, char* filename, size_t bufferSize) {
    filename[0] = '\0';
    if (!filepath) return;
    
    const char* lastSlash = strrchr(filepath, '\\');
    if (!lastSlash) lastSlash = strrchr(filepath, '/');
    
    if (lastSlash) {
        strncpy_s(filename, bufferSize, lastSlash + 1, _TRUNCATE);
    } else {
        strncpy_s(filename, bufferSize, filepath, _TRUNCATE);
    }
}

// Log track to database with extended metadata
void LogToDatabase(const char* title, const char* filepath) {
    if (!db) return;
    
    // Get current local time
    time_t now = time(0);
    struct tm timeinfo;
    localtime_s(&timeinfo, &now);
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
    
    // Get filename from path
    char filename[MAX_PATH] = "";
    GetFilenameFromPath(filepath, filename, sizeof(filename));
    
    // Get extended metadata from Winamp
    char artist[256] = "";
    char album[256] = "";
    char genre[128] = "";
    char trackNum[32] = "";
    char year[32] = "";
    char lengthStr[32] = "";
    
    if (filepath && strlen(filepath) > 0) {
        GetExtendedFileInfo(filepath, "artist", artist, sizeof(artist));
        GetExtendedFileInfo(filepath, "album", album, sizeof(album));
        GetExtendedFileInfo(filepath, "genre", genre, sizeof(genre));
        GetExtendedFileInfo(filepath, "track", trackNum, sizeof(trackNum));
        GetExtendedFileInfo(filepath, "year", year, sizeof(year));
        GetExtendedFileInfo(filepath, "length", lengthStr, sizeof(lengthStr));
    }
    
    // Parse duration (length is in milliseconds as string, or might be in seconds)
    int durationMs = 0;
    if (strlen(lengthStr) > 0) {
        durationMs = atoi(lengthStr);
        // If it looks like seconds (< 10000), convert to ms
        if (durationMs > 0 && durationMs < 10000) {
            durationMs *= 1000;
        }
    }
    
    // Use title from parameter if metadata title is empty
    const char* finalTitle = title;
    char metaTitle[512] = "";
    if (filepath && strlen(filepath) > 0) {
        GetExtendedFileInfo(filepath, "title", metaTitle, sizeof(metaTitle));
        if (strlen(metaTitle) > 0) {
            finalTitle = metaTitle;
        }
    }
    
    // Prepare SQL statement
    const char* insertSQL = 
        "INSERT INTO play_history (played_at, filepath, filename, title, artist, album, genre, track_number, year, duration_ms) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = NULL;
    
    int rc = sqlite3_prepare_v2(db, insertSQL, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return;
    
    // Bind parameters
    sqlite3_bind_text(stmt, 1, timeStr, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, filepath ? filepath : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, filename, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, finalTitle ? finalTitle : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, artist, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, album, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, genre, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, trackNum, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, year, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 10, durationMs);
    
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
    
    // Get track position and length for repeat detection
    int currentPosMs = (int)SendMessage(hwndWinamp, WM_WA_IPC, 0, IPC_GETOUTPUTTIME);
    int trackLengthMs = (int)SendMessage(hwndWinamp, WM_WA_IPC, 1, IPC_GETOUTPUTTIME);
    
    int currentPercent = 0;
    if (trackLengthMs > 0 && currentPosMs >= 0) {
        currentPercent = (currentPosMs * 100) / trackLengthMs;
    }
    
    bool shouldLog = false;
    
    // Check if track has changed
    if (strlen(title) > 0 && strcmp(title, currentTitle) != 0) {
        shouldLog = true;
    }
    // Check for repeat: same track, was at 90%+, now at <5%
    else if (strlen(filepath) > 0 && strcmp(filepath, lastFilepath) == 0 &&
             lastPositionPercent >= 90 && currentPercent < 5) {
        shouldLog = true;
    }
    
    if (shouldLog && strlen(title) > 0) {
        strncpy_s(currentTitle, sizeof(currentTitle), title, _TRUNCATE);
        strncpy_s(lastFilepath, sizeof(lastFilepath), filepath, _TRUNCATE);
        LogToDatabase(title, filepath);
    }
    
    // Update last position (only if we got a valid reading)
    if (trackLengthMs > 0) {
        lastPositionPercent = currentPercent;
        if (strlen(filepath) > 0) {
            strncpy_s(lastFilepath, sizeof(lastFilepath), filepath, _TRUNCATE);
        }
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
    char msg[600];
    snprintf(msg, sizeof(msg),
        "winnp - Now Playing Logger\n\n"
        "Logs currently playing songs to SQLite database:\n"
        "%s\n\n"
        "Table: play_history\n"
        "Columns: id, played_at, filepath, filename,\n"
        "title, artist, album, genre, track_number, year, duration_ms",
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
