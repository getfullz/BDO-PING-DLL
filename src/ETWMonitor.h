#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <iphlpapi.h>
#include <string>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <vector>

class ETWMonitor {
public:
    static ETWMonitor& GetInstance();

    bool Initialize();
    void Shutdown();
    float GetLatestRTT();

private:
    ETWMonitor();
    ~ETWMonitor();
    ETWMonitor(const ETWMonitor&) = delete;
    ETWMonitor& operator=(const ETWMonitor&) = delete;

    void Log(const std::string& message);
    DWORD GetTargetProcessId();
    bool IsGamePortActive();
    void RunETW();

    static void WINAPI EventRecordCallback(PEVENT_RECORD pEvent);
    void ProcessEvent(PEVENT_RECORD pEvent);
    bool ReadProperty(PEVENT_RECORD pEvent, const wchar_t* name, std::vector<uint8_t>& outData);

    TRACEHANDLE m_SessionHandle;
    TRACEHANDLE m_TraceHandle;
    EVENT_TRACE_PROPERTIES* m_pSessionProperties;
    std::vector<uint8_t> m_SessionPropertiesBuffer;

    std::thread m_EtwThread;
    std::atomic<bool> m_IsRunning;

    std::wstring m_SessionName;
    DWORD m_TargetPid;
    
    std::mutex m_DataMutex;
    // Connections belonging to BDO
    std::unordered_set<uint64_t> m_BdoTcbs;
    std::unordered_set<uint64_t> m_GameTcbs;
    std::unordered_map<uint64_t, uint32_t> m_TcbActivity;
    std::unordered_map<uint64_t, float> m_TcbPeakRtt;
    std::unordered_map<uint64_t, float> m_TcbLastRtt;
    std::unordered_map<uint64_t, DWORD> m_TcbLast1167Tick;
};
