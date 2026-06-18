#include <windows.h>

#pragma comment(lib, "Advapi32.lib")

#define SERVICE_NAME L"DPAPIDienst"

// --- Globals ---
SERVICE_STATUS gStatus = {};
SERVICE_STATUS_HANDLE gStatusHandle = nullptr;
HANDLE gStopEvent = nullptr;

// --- Forward ---
void WINAPI ServiceMain(DWORD argc, LPWSTR* argv);
void WINAPI ServiceCtrlHandler(DWORD ctrlCode);

// ------------------------------------------------------------
// Entry (für leeres Projekt!)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    SERVICE_TABLE_ENTRY table[] =
    {
        { (LPWSTR)SERVICE_NAME, ServiceMain },
        { nullptr, nullptr }
    };

    if (!StartServiceCtrlDispatcher(table))
    {
        // 👉 Debug-Modus (nicht als Service gestartet)
        MessageBox(nullptr, L"Läuft NICHT als Service (Debugmodus)", SERVICE_NAME, MB_OK);
        ServiceMain(0, nullptr);
    }

    return 0;
}

// ------------------------------------------------------------
void WINAPI ServiceMain(DWORD, LPWSTR*)
{
    gStatusHandle = RegisterServiceCtrlHandler(SERVICE_NAME, ServiceCtrlHandler);

    gStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    gStatus.dwCurrentState = SERVICE_START_PENDING;
    gStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;

    SetServiceStatus(gStatusHandle, &gStatus);

    gStopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

    // 👉 „Hello“
    OutputDebugString(L"DPAPIDienst gestartet 🚀\n");

    gStatus.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(gStatusHandle, &gStatus);

    // --- Hauptloop ---
    while (WaitForSingleObject(gStopEvent, 2000) == WAIT_TIMEOUT)
    {
        OutputDebugString(L"Hello Dienst läuft...\n");
    }

    OutputDebugString(L"DPAPIDienst wird beendet 👋\n");

    gStatus.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(gStatusHandle, &gStatus);
}

// ------------------------------------------------------------
void WINAPI ServiceCtrlHandler(DWORD ctrlCode)
{
    if (ctrlCode == SERVICE_CONTROL_STOP)
    {
        gStatus.dwCurrentState = SERVICE_STOP_PENDING;
        SetServiceStatus(gStatusHandle, &gStatus);

        SetEvent(gStopEvent);
    }
}