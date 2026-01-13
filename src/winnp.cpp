#include "winnp.h"
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <string>
#include <sstream>
#include <ctime>

// Global variables
HWND hwndWinamp = NULL;
char currentTitle[2048] = "";
char logFilePath[MAX_PATH] = "";
HANDLE hTimer = NULL;
HANDLE hTimerQueue = NULL;
winampGeneralPurposePlugin* g_plugin = NULL;
HMODULE g_hModule = NULL;

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
void WriteToLogFile(const char* title);
void GetLogFilePath();

// Plugin description string (non-const for Winamp API)
static char pluginDescription[] = "winnp - Now Playing Logger";

// Plugin description
winampGeneralPurposePlugin plugin = {
    GPPHDR_VER,
    pluginDescription,
    init,
    config,
    quit,
    NULL,  // hwndParent - set by Winamp
    NULL   // hDllInstance - set by Winamp
};

// Get the log file path (in user's Documents folder)
void GetLogFilePath() {
    if (strlen(logFilePath) > 0) return;
    
    char documentsPath[MAX_PATH];
    
    // Get the user's Documents folder
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_MYDOCUMENTS, NULL, 0, documentsPath))) {
        snprintf(logFilePath, MAX_PATH, "%s\\nowplaying.txt", documentsPath);
    } else {
        // Fallback to user profile directory using Windows API
        DWORD size = MAX_PATH;
        char userProfile[MAX_PATH];
        if (GetEnvironmentVariableA("USERPROFILE", userProfile, size) > 0) {
            snprintf(logFilePath, MAX_PATH, "%s\\Documents\\nowplaying.txt", userProfile);
        } else {
            // Last resort fallback
            strncpy_s(logFilePath, MAX_PATH, "C:\\nowplaying.txt", _TRUNCATE);
        }
    }
}

// Write current track info to log file
void WriteToLogFile(const char* title) {
    if (!title || strlen(title) == 0) return;
    
    GetLogFilePath();
    
    std::ofstream logFile(logFilePath, std::ios::app);
    if (logFile.is_open()) {
        // Get current time
        time_t now = time(0);
        struct tm timeinfo;
        localtime_s(&timeinfo, &now);
        
        char timeStr[64];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
        
        // Write to file
        logFile << "[" << timeStr << "] " << title << std::endl;
        logFile.close();
    }
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
            return; // Still no window, skip this check
        }
    }
    
    // Get current track info from Winamp
    char title[2048];
    title[0] = '\0';
    
    // Get current playlist position
    int position = (int)SendMessage(hwndWinamp, WM_WA_IPC, 0, IPC_GETLISTPOS);
    
    if (position >= 0) {
        // Get the title string for the current position
        // IPC_GETPLAYLISTTITLE returns a pointer to the title string
        char* titlePtr = (char*)SendMessage(hwndWinamp, WM_WA_IPC, position, IPC_GETPLAYLISTTITLE);
        if (titlePtr && titlePtr != (char*)-1) {
            strncpy_s(title, sizeof(title), titlePtr, _TRUNCATE);
        }
    }
    
    // If we still don't have a title, try getting it from the window title
    if (strlen(title) == 0) {
        char windowTitle[512];
        if (GetWindowTextA(hwndWinamp, windowTitle, sizeof(windowTitle)) > 0) {
            // Winamp window title format: "Winamp - [track info]"
            char* dashPos = strstr(windowTitle, " - ");
            if (dashPos) {
                strncpy_s(title, sizeof(title), dashPos + 3, _TRUNCATE);
            }
        }
    }
    
    // Check if track has changed
    if (strlen(title) > 0 && strcmp(title, currentTitle) != 0) {
        strncpy_s(currentTitle, sizeof(currentTitle), title, _TRUNCATE);
        WriteToLogFile(title);
    }
}

// Plugin initialization
int init() {
    // Get Winamp window handle from the plugin structure
    // Winamp sets hwndParent to the main window handle when it loads the plugin
    if (g_plugin && g_plugin->hwndParent) {
        hwndWinamp = g_plugin->hwndParent;
    } else {
        // Fallback: try to find Winamp window by class name
        hwndWinamp = FindWindowA("Winamp v1.x", NULL);
    }
    
    // Don't fail initialization if window isn't found yet - we'll try again in the timer
    // Some versions of Winamp may not have the window ready immediately
    
    // Initialize log file path
    GetLogFilePath();
    
    // Create timer queue for periodic checking
    hTimerQueue = CreateTimerQueue();
    if (hTimerQueue) {
        // Create a timer that fires every 500ms to check for track changes
        CreateTimerQueueTimer(&hTimer, hTimerQueue, TimerCallback, NULL, 500, 500, WT_EXECUTEINTIMERTHREAD);
    }
    
    // Always return 0 (success) to allow plugin to load
    // The timer callback will handle getting the window handle if needed
    return 0;
}

// Plugin configuration (optional - can be empty)
void config() {
    // Could open a configuration dialog here
    // For now, just show a message
    MessageBoxA(NULL, 
                "winnp - Now Playing Logger\n\n"
                "This plugin logs the currently playing song to:\n"
                "Documents\\nowplaying.txt\n\n"
                "The log file is updated each time the track changes.",
                "winnp Configuration",
                MB_OK | MB_ICONINFORMATION);
}

// Plugin cleanup
void quit() {
    // Cancel and delete timer
    if (hTimer && hTimerQueue) {
        DeleteTimerQueueTimer(hTimerQueue, hTimer, NULL);
    }
    if (hTimerQueue) {
        DeleteTimerQueue(hTimerQueue);
    }
    
    // Clear variables
    hwndWinamp = NULL;
    hTimer = NULL;
    hTimerQueue = NULL;
}

// DLL entry point
extern "C" __declspec(dllexport) winampGeneralPurposePlugin* winampGetGeneralPurposePlugin() {
    g_plugin = &plugin;
    return &plugin;
}
