#include "ETWMonitor.h"
#include <iostream>
#include <fstream>
#include <tlhelp32.h>
#include <tdh.h>
#include <chrono>
#include <ctime>
#include <unordered_set>
#include <string>

#pragma comment(lib, "tdh.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

// Microsoft-Windows-TCPIP Provider GUID
// {2F07E2EE-15DB-40F1-90EF-9D7BA282188A}
static const GUID TCPIP_PROVIDER_GUID = { 0x2F07E2EE, 0x15DB, 0x40F1, { 0x90, 0xEF, 0x9D, 0x7B, 0xA2, 0x82, 0x18, 0x8A } };

ETWMonitor& ETWMonitor::GetInstance() {
    static ETWMonitor instance;
    return instance;
}

ETWMonitor::ETWMonitor() 
    : m_SessionHandle(0), m_TraceHandle(0), m_pSessionProperties(nullptr),
      m_IsRunning(false), m_SessionName(L"BDO_Ping_ETW_Session"), m_TargetPid(0) {
}

ETWMonitor::~ETWMonitor() {
    Shutdown();
}

void ETWMonitor::Log(const std::string& message) {
    std::ofstream logFile("BDO_Ping_Plugin_Log.txt", std::ios_base::app);
    if (logFile.is_open()) {
        auto now = std::chrono::system_clock::now();
        std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        char timeBuffer[26];
        ctime_s(timeBuffer, sizeof(timeBuffer), &now_time);
        
        // Remove trailing newline from ctime_s
        std::string timeStr(timeBuffer);
        if (!timeStr.empty() && timeStr.back() == '\n') timeStr.pop_back();

        logFile << "[" << timeStr << "] " << message << std::endl;
    }
}

DWORD ETWMonitor::GetTargetProcessId() {
    PROCESSENTRY32W pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32W);
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return 0;

    DWORD pid = 0;
    if (Process32FirstW(hSnapshot, &pe32)) {
        do {
            if (_wcsicmp(pe32.szExeFile, L"BlackDesert64.exe") == 0) {
                pid = pe32.th32ProcessID;
                break;
            }
        } while (Process32NextW(hSnapshot, &pe32));
    }
    CloseHandle(hSnapshot);
    return pid;
}

bool ETWMonitor::IsGamePortActive() {
    if (m_TargetPid == 0) return false;

    PMIB_TCPTABLE2 pTcpTable = nullptr;
    ULONG ulSize = 0;
    DWORD dwRetVal = 0;

    if ((dwRetVal = GetTcpTable2(pTcpTable, &ulSize, TRUE)) == ERROR_INSUFFICIENT_BUFFER) {
        pTcpTable = (PMIB_TCPTABLE2)malloc(ulSize);
        if (pTcpTable == nullptr) {
            return false;
        }
    } else {
        return false;
    }

    if ((dwRetVal = GetTcpTable2(pTcpTable, &ulSize, TRUE)) == NO_ERROR) {
        for (int i = 0; i < (int)pTcpTable->dwNumEntries; i++) {
            if (pTcpTable->table[i].dwOwningPid == m_TargetPid &&
                ntohs((u_short)pTcpTable->table[i].dwRemotePort) == 8889 &&
                pTcpTable->table[i].dwState == MIB_TCP_STATE_ESTAB) {
                free(pTcpTable);
                return true;
            }
        }
    }

    free(pTcpTable);
    return false;
}

bool ETWMonitor::Initialize() {
    if (m_IsRunning) return true;

    Log("Initializing ETW Monitor...");

    m_TargetPid = GetTargetProcessId();
    if (m_TargetPid == 0) {
        Log("Warning: BlackDesert64.exe not running at initialization.");
    } else {
        Log("Found BlackDesert64.exe PID: " + std::to_string(m_TargetPid));
    }

    size_t bufferSize = sizeof(EVENT_TRACE_PROPERTIES) + (m_SessionName.length() + 1) * sizeof(WCHAR);
    m_SessionPropertiesBuffer.resize(bufferSize);
    m_pSessionProperties = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(m_SessionPropertiesBuffer.data());
    
    ZeroMemory(m_pSessionProperties, bufferSize);
    m_pSessionProperties->Wnode.BufferSize = static_cast<ULONG>(bufferSize);
    m_pSessionProperties->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    m_pSessionProperties->Wnode.ClientContext = 1; // QPC clock resolution for accurate RTT timing
    m_pSessionProperties->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
    m_pSessionProperties->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);

    // Stop existing session if it was left running abruptly
    ControlTraceW(0, m_SessionName.c_str(), m_pSessionProperties, EVENT_TRACE_CONTROL_STOP);

    ULONG status = StartTraceW(&m_SessionHandle, m_SessionName.c_str(), m_pSessionProperties);
    if (status != ERROR_SUCCESS) {
        Log("StartTraceW failed with status: " + std::to_string(status));
        return false;
    }

    status = EnableTraceEx2(m_SessionHandle, &TCPIP_PROVIDER_GUID, EVENT_CONTROL_CODE_ENABLE_PROVIDER, TRACE_LEVEL_INFORMATION, 0xFFFFFFFFFFFFFFFF, 0, 0, nullptr);
    if (status != ERROR_SUCCESS) {
        Log("EnableTraceEx2 failed with status: " + std::to_string(status));
        ControlTraceW(m_SessionHandle, m_SessionName.c_str(), m_pSessionProperties, EVENT_TRACE_CONTROL_STOP);
        return false;
    }

    m_IsRunning = true;
    m_EtwThread = std::thread(&ETWMonitor::RunETW, this);
    
    Log("ETW Monitor initialized successfully.");
    return true;
}

void ETWMonitor::Shutdown() {
    if (!m_IsRunning) return;

    Log("Shutting down ETW Monitor...");
    m_IsRunning = false;

    if (m_SessionHandle) {
        EnableTraceEx2(m_SessionHandle, &TCPIP_PROVIDER_GUID, EVENT_CONTROL_CODE_DISABLE_PROVIDER, 0, 0, 0, 0, nullptr);
        ControlTraceW(m_SessionHandle, m_SessionName.c_str(), m_pSessionProperties, EVENT_TRACE_CONTROL_STOP);
        m_SessionHandle = 0;
    }

    if (m_TraceHandle) {
        CloseTrace(m_TraceHandle);
        m_TraceHandle = 0;
    }

    if (m_EtwThread.joinable()) {
        m_EtwThread.join();
    }

    Log("ETW Monitor shut down successfully.");
}

float ETWMonitor::GetLatestRTT() {
    std::lock_guard<std::mutex> lock(m_DataMutex);

    // --- Process liveness check ---
    // If we have a PID, verify the process is still alive.
    // GetExitCodeProcess with STILL_ACTIVE (259) means it's running.
    if (m_TargetPid > 0) {
        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, m_TargetPid);
        bool processAlive = false;
        if (hProc != nullptr) {
            DWORD exitCode = 0;
            if (GetExitCodeProcess(hProc, &exitCode) && exitCode == STILL_ACTIVE) {
                processAlive = true;
            }
            CloseHandle(hProc);
        }
        if (!processAlive) {
            Log("Process PID " + std::to_string(m_TargetPid) + " has exited. Searching for new game client...");
            m_TargetPid = 0;
            // Clear stale TCB data so we start fresh for the new client.
            m_BdoTcbs.clear();
            m_GameTcbs.clear();
            m_TcbActivity.clear();
            m_TcbPeakRtt.clear();
            m_TcbLastRtt.clear();
            m_TcbLast1167Tick.clear();
            // Try to find a newly launched client (RU or EU).
            m_TargetPid = GetTargetProcessId();
            if (m_TargetPid != 0) {
                Log("Found new game client PID: " + std::to_string(m_TargetPid));
            } else {
                Log("No running game client found. Waiting...");
            }
        }
    } else {
        // No PID at all — try to find the game on every poll.
        m_TargetPid = GetTargetProcessId();
        if (m_TargetPid != 0) {
            Log("Game client found, PID: " + std::to_string(m_TargetPid));
        }
    }
    // --- End process liveness check ---

    if (!IsGamePortActive()) {
        for (auto& pair : m_TcbPeakRtt) {
            pair.second = 0.0f;
        }
        for (auto& pair : m_TcbActivity) {
            pair.second = 0;
        }
        return 0.0f;
    }

    uint64_t bestTcb = 0;
    uint32_t maxActivity = 0;

    // Search among game TCBs first
    for (uint64_t tcb : m_GameTcbs) {
        if (m_TcbActivity[tcb] > maxActivity) {
            maxActivity = m_TcbActivity[tcb];
            bestTcb = tcb;
        }
    }

    // Fallback to all BDO TCBs if no game TCB was identified yet
    if (bestTcb == 0) {
        for (uint64_t tcb : m_BdoTcbs) {
            if (m_TcbActivity[tcb] > maxActivity) {
                maxActivity = m_TcbActivity[tcb];
                bestTcb = tcb;
            }
        }
    }

    if (bestTcb != 0 && maxActivity > 0) {
        float result = m_TcbPeakRtt[bestTcb];
        if (result == 0.0f) result = m_TcbLastRtt[bestTcb];

        for (auto& pair : m_TcbPeakRtt) {
            pair.second = 0.0f;
        }

        for (auto& pair : m_TcbActivity) {
            pair.second /= 2;
        }

        return result;
    }

    return 0.0f;
}

void ETWMonitor::RunETW() {
    EVENT_TRACE_LOGFILEW logFile = { 0 };
    logFile.LoggerName = const_cast<LPWSTR>(m_SessionName.c_str());
    logFile.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    logFile.EventRecordCallback = EventRecordCallback;
    logFile.Context = this;

    m_TraceHandle = OpenTraceW(&logFile);
    if (m_TraceHandle == INVALID_PROCESSTRACE_HANDLE) {
        Log("OpenTraceW failed with status: " + std::to_string(GetLastError()));
        return;
    }

    Log("Starting ProcessTrace message loop...");
    ProcessTrace(&m_TraceHandle, 1, 0, 0);
    Log("ProcessTrace exited.");
}

void WINAPI ETWMonitor::EventRecordCallback(PEVENT_RECORD pEvent) {
    ETWMonitor* monitor = static_cast<ETWMonitor*>(pEvent->UserContext);
    if (monitor) {
        monitor->ProcessEvent(pEvent);
    }
}

bool ETWMonitor::ReadProperty(PEVENT_RECORD pEvent, const wchar_t* name, std::vector<uint8_t>& outData) {
    PROPERTY_DATA_DESCRIPTOR desc = { 0 };
    desc.PropertyName = reinterpret_cast<ULONGLONG>(name);
    desc.ArrayIndex = ULONG_MAX;

    DWORD reqSize = 0;
    DWORD status = TdhGetPropertySize(pEvent, 0, nullptr, 1, &desc, &reqSize);
    if (status != ERROR_SUCCESS) return false;

    outData.resize(reqSize);
    status = TdhGetProperty(pEvent, 0, nullptr, 1, &desc, reqSize, outData.data());
    return status == ERROR_SUCCESS;
}

void ETWMonitor::ProcessEvent(PEVENT_RECORD pEvent) {
    static std::unordered_set<USHORT> loggedEventIds;
    USHORT eventId = pEvent->EventHeader.EventDescriptor.Id;

    if (loggedEventIds.find(eventId) == loggedEventIds.end()) {
        loggedEventIds.insert(eventId);

        Log("New Event ID encountered: " + std::to_string(eventId));

        DWORD bufferSize = 0;
        if (TdhGetEventInformation(pEvent, 0, nullptr, nullptr, &bufferSize) == ERROR_INSUFFICIENT_BUFFER) {
            std::vector<uint8_t> infoBuffer(bufferSize);
            TRACE_EVENT_INFO* pInfo = reinterpret_cast<TRACE_EVENT_INFO*>(infoBuffer.data());
            
            if (TdhGetEventInformation(pEvent, 0, nullptr, pInfo, &bufferSize) == ERROR_SUCCESS) {
                Log("  Property Count: " + std::to_string(pInfo->TopLevelPropertyCount));
                for (ULONG i = 0; i < pInfo->TopLevelPropertyCount; ++i) {
                    std::wstring propNameW(reinterpret_cast<WCHAR*>(infoBuffer.data() + pInfo->EventPropertyInfoArray[i].NameOffset));
#pragma warning(push)
#pragma warning(disable: 4244)
                    std::string propName(propNameW.begin(), propNameW.end());
#pragma warning(pop)
                    Log("  Property [" + std::to_string(i) + "]: " + propName);
                }
            } else {
                Log("  Failed to get event information for ID " + std::to_string(eventId));
            }
        } else {
            Log("  Failed to get buffer size for ID " + std::to_string(eventId));
        }
    }

    std::vector<uint8_t> data;
    uint64_t tcb = 0;
    
    // Read Tcb dynamically (4 bytes or 8 bytes)
    if (ReadProperty(pEvent, L"Tcb", data)) {
        if (data.size() == 8) {
            tcb = *reinterpret_cast<uint64_t*>(data.data());
        } else if (data.size() == 4) {
            tcb = *reinterpret_cast<uint32_t*>(data.data());
        }
    }

    if (tcb != 0) {
        std::lock_guard<std::mutex> lock(m_DataMutex);

        bool isGameEvent = false;
        if (m_TargetPid != 0) {
            if (pEvent->EventHeader.ProcessId == m_TargetPid) {
                isGameEvent = true;
            } else {
                std::vector<uint8_t> pidData;
                if (ReadProperty(pEvent, L"Pid", pidData)) {
                    uint32_t pidVal = 0;
                    if (pidData.size() == 4) pidVal = *reinterpret_cast<uint32_t*>(pidData.data());
                    else if (pidData.size() == 8) pidVal = static_cast<uint32_t>(*reinterpret_cast<uint64_t*>(pidData.data()));
                    if (pidVal == m_TargetPid) {
                        isGameEvent = true;
                    }
                } else if (ReadProperty(pEvent, L"ProcessId", pidData)) {
                    uint32_t pidVal = 0;
                    if (pidData.size() == 4) pidVal = *reinterpret_cast<uint32_t*>(pidData.data());
                    else if (pidData.size() == 8) pidVal = static_cast<uint32_t>(*reinterpret_cast<uint64_t*>(pidData.data()));
                    if (pidVal == m_TargetPid) {
                        isGameEvent = true;
                    }
                }
            }
        }

        if (isGameEvent) {
            if (m_BdoTcbs.find(tcb) == m_BdoTcbs.end()) {
                Log("Mapped new BDO TCB: " + std::to_string(tcb) + " (Event ID: " + std::to_string(eventId) + ")");
            }
            m_BdoTcbs.insert(tcb);
            m_TcbActivity[tcb]++;
        }

        // Try to identify if this is the game port (8889) connection
        if (m_BdoTcbs.count(tcb) && m_GameTcbs.find(tcb) == m_GameTcbs.end()) {
            std::vector<uint8_t> addrData;
            if (ReadProperty(pEvent, L"RemoteAddress", addrData) || ReadProperty(pEvent, L"RemoteSockAddr", addrData)) {
                if (addrData.size() >= 4) {
                    USHORT port = *reinterpret_cast<USHORT*>(&addrData[2]);
                    port = ntohs(port);
                    if (port == 8889) {
                        m_GameTcbs.insert(tcb);
                        Log("Identified BDO Game Server TCB: " + std::to_string(tcb) + " (Remote Port: " + std::to_string(port) + ")");
                    }
                }
            }
        }

        // Update RTT
        if (m_BdoTcbs.count(tcb)) {
            float rttFloat = -1.0f;
            if (eventId == 1167) {
                if (ReadProperty(pEvent, L"RttSample", data) && data.size() == 4) {
                    uint32_t rttSample = *reinterpret_cast<uint32_t*>(data.data());
                    if (rttSample > 0) {
                        rttFloat = static_cast<float>(rttSample); // already in ms
                        m_TcbLast1167Tick[tcb] = GetTickCount();
                    }
                }
            } else {
                // Only process fallback SRtt events if we haven't seen 1167 events recently (within 5 seconds) on this TCB
                DWORD last1167 = m_TcbLast1167Tick[tcb]; // default is 0 if not exists
                bool useFallback = (last1167 == 0) || (GetTickCount() - last1167 > 5000);
                if (useFallback) {
                    if (eventId == 1332 || eventId == 1159 || eventId == 1637) {
                        if (ReadProperty(pEvent, L"SRtt", data)) {
                            uint32_t srttVal = 0;
                            if (data.size() == 4) {
                                srttVal = *reinterpret_cast<uint32_t*>(data.data());
                            } else if (data.size() == 8) {
                                srttVal = static_cast<uint32_t>(*reinterpret_cast<uint64_t*>(data.data()));
                            }
                            if (srttVal > 0) {
                                rttFloat = static_cast<float>(srttVal) / 1000.0f; // microseconds to ms
                            }
                        }
                    } else if (eventId == 1351) {
                        if (ReadProperty(pEvent, L"SRTT", data)) {
                            uint32_t srttVal = 0;
                            if (data.size() == 4) {
                                srttVal = *reinterpret_cast<uint32_t*>(data.data());
                            } else if (data.size() == 8) {
                                srttVal = static_cast<uint32_t>(*reinterpret_cast<uint64_t*>(data.data()));
                            }
                            if (srttVal > 0) {
                                rttFloat = static_cast<float>(srttVal) / 1000.0f; // microseconds to ms
                            }
                        }
                    }
                }
            }

            if (rttFloat > 0.0f) {
                if (rttFloat > m_TcbPeakRtt[tcb]) {
                    m_TcbPeakRtt[tcb] = rttFloat;
                }
                m_TcbLastRtt[tcb] = rttFloat;
            }
        }
    }
}
