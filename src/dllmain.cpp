#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include "MAHMPlugin.h"
#include "ETWMonitor.h"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        break;
    case DLL_PROCESS_DETACH:
        ETWMonitor::GetInstance().Shutdown();
        break;
    }
    return TRUE;
}

DWORD GetSourcesNum() {
    return 1;
}

BOOL GetSourceDesc(DWORD dwIndex, PLUGIN_SOURCE_DESC* pDesc) {
    if (dwIndex != 0 || !pDesc) return FALSE;

    strcpy_s(pDesc->szName, sizeof(pDesc->szName), "BDO Ping (RTT)");
    strcpy_s(pDesc->szUnits, sizeof(pDesc->szUnits), "ms");
    strcpy_s(pDesc->szGroup, sizeof(pDesc->szGroup), "Network");
    strcpy_s(pDesc->szFormat, sizeof(pDesc->szFormat), "%.1f");
    pDesc->dwID = 0xBDBDBDBD; // Unique ID

    return TRUE;
}

FLOAT GetSourceData(DWORD dwIndex) {
    if (dwIndex != 0) return 0.0f;
    
    // Lazy initialization ensures ETW starts as soon as MSI Afterburner starts reading
    ETWMonitor::GetInstance().Initialize();
    
    return ETWMonitor::GetInstance().GetLatestRTT();
}

void SetupSource(DWORD dwIndex, HWND hWnd) {
    // Optional setup dialog logic
}

void Uninit() {
    ETWMonitor::GetInstance().Shutdown();
}
