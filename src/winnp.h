#ifndef WINNP_H
#define WINNP_H

#include <windows.h>

// Winamp IPC messages
#define WM_WA_IPC WM_USER
#define IPC_GETLISTPOS 125
#define IPC_GETPLAYLISTTITLE 212
#define IPC_GETPLAYLISTFILE 211
#define IPC_ISPLAYING 104
#define IPC_GETOUTPUTTIME 105  // wparam=0: position ms, wparam=1: track length ms
#define IPC_GET_EXTENDED_FILE_INFO 290

// Structure for getting extended file info
typedef struct {
    const char* filename;
    const char* metadata;
    char* ret;
    size_t retlen;
} extendedFileInfoStruct;

// Winamp General Purpose Plugin structure
#define GPPHDR_VER 0x10

typedef struct {
    int version;
    char *description;
    int (*init)();
    void (*config)();
    void (*quit)();
    HWND hwndParent;
    HINSTANCE hDllInstance;
} winampGeneralPurposePlugin;

// Plugin functions
int init();
void config();
void quit();

#endif // WINNP_H
