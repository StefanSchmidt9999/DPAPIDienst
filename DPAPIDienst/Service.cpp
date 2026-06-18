#include <windows.h>
#include <winhttp.h>
#include <string>
#include <sstream>
#include <objbase.h>

#pragma comment(lib, "Winhttp.lib")
#pragma comment(lib, "Ole32.lib")
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
        // Debug-Modus (nicht als Service gestartet)
        MessageBox(nullptr, L"Läuft NICHT als Service (Debugmodus)", SERVICE_NAME, MB_OK);
        ServiceMain(0, nullptr);
    }

    return 0;
}
// ------------------------------------------------------------
std::wstring CreateGuidString()
{
    GUID guid;
    CoCreateGuid(&guid);

    wchar_t buffer[64]{};
    StringFromGUID2(guid, buffer, 64);

    return std::wstring(buffer);
}

std::wstring CreateSoapXml(
    const std::wstring& clientId,
    const std::wstring& requestId,
    const std::wstring& message)
{
    std::wstringstream ss;

    ss << L"<?xml version=\"1.0\"?>"
        << L"<soap:Envelope xmlns:soap=\"http://schemas.xmlsoap.org/soap/envelope/\">"
        << L"<soap:Body>"
        << L"<Request>"
        << L"<ClientId>" << clientId << L"</ClientId>"
        << L"<RequestId>" << requestId << L"</RequestId>"
        << L"<Message>" << message << L"</Message>"
        << L"</Request>"
        << L"</soap:Body>"
        << L"</soap:Envelope>";

    return ss.str();
}

bool SendSoapToAzure(const std::wstring& soapXml)
{
    HINTERNET hSession = WinHttpOpen(
        L"DPAPIDienst/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);

    if (!hSession)
        return false;

    HINTERNET hConnect = WinHttpConnect(
        hSession,
        L"mysoapapp-eqdtckhxd0enegft.canadacentral-01.azurewebsites.net",
        INTERNET_DEFAULT_HTTPS_PORT,
        0);

    if (!hConnect)
    {
        WinHttpCloseHandle(hSession);
        return false;
    }

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        L"POST",
        L"/api/hello",
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);

    if (!hRequest)
    {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    int sizeNeeded = WideCharToMultiByte(
        CP_UTF8,
        0,
        soapXml.c_str(),
        -1,
        nullptr,
        0,
        nullptr,
        nullptr);

    std::string utf8Xml(sizeNeeded - 1, '\0');

    WideCharToMultiByte(
        CP_UTF8,
        0,
        soapXml.c_str(),
        -1,
        utf8Xml.data(),
        sizeNeeded,
        nullptr,
        nullptr);

    std::wstring headers =
        L"Content-Type: application/xml; charset=utf-8\r\n";

    BOOL ok = WinHttpSendRequest(
        hRequest,
        headers.c_str(),
        static_cast<DWORD>(headers.length()),
        reinterpret_cast<LPVOID>(utf8Xml.data()),
        static_cast<DWORD>(utf8Xml.size()),
        static_cast<DWORD>(utf8Xml.size()),
        0);

    if (ok)
    {
        ok = WinHttpReceiveResponse(hRequest, nullptr);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return ok == TRUE;
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

    // „Hello“
    OutputDebugString(L"DPAPIDienst gestartet \n");

    gStatus.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(gStatusHandle, &gStatus);

    // --- Hauptloop ---
    while (WaitForSingleObject(gStopEvent, 10000) == WAIT_TIMEOUT)
    {
        OutputDebugString(L"DPAPIDienst sendet Testnachricht...\n");

        std::wstring clientId = L"CPP-CLIENT-001";
        std::wstring requestId = CreateGuidString();

        std::wstring soapXml = CreateSoapXml(
            clientId,
            requestId,
            L"Hallo von C++ Windows Dienst");

        bool ok = SendSoapToAzure(soapXml);

        if (ok)
        {
            OutputDebugString(L"SOAP/XML erfolgreich an Azure gesendet.\n");
        }
        else
        {
            OutputDebugString(L"FEHLER beim Senden an Azure.\n");
        }
    }

    OutputDebugString(L"DPAPIDienst wird beendet \n");

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