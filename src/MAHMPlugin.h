#pragma once
#include <windows.h>

#define MAX_PATH 260

struct PLUGIN_SOURCE_DESC
{
	char	szName[MAX_PATH];
	char	szUnits[MAX_PATH];
	char	szGroup[MAX_PATH];
	char	szFormat[MAX_PATH];
	DWORD	dwID;
};

extern "C" {
    DWORD GetSourcesNum();
    BOOL GetSourceDesc(DWORD dwIndex, PLUGIN_SOURCE_DESC* pDesc);
    FLOAT GetSourceData(DWORD dwIndex);
    void SetupSource(DWORD dwIndex, HWND hWnd);
    void Uninit();
}
