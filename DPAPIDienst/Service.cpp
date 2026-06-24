#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <wincrypt.h>
#include <winhttp.h>
#include <objbase.h>

#include <string>
#include <fstream>
#include <cstdlib>
#include <iostream>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "Advapi32.lib")

#define SERVICE_NAME L"DPAPIDienst"

// --- Globals ---
SERVICE_STATUS gStatus = {};
SERVICE_STATUS_HANDLE gStatusHandle = nullptr;
HANDLE gStopEvent = nullptr;

// --- Forward ---
void WINAPI ServiceMain(DWORD argc, LPWSTR* argv);
void WINAPI ServiceCtrlHandler(DWORD ctrlCode);
std::wstring DecryptClientIdFromFile();
DWORD WINAPI LocalHttpServerThread(LPVOID);
std::string WStringToUtf8(const std::wstring& text);
void RunConsoleMode();

// ------------------------------------------------------------
// Entry (für leeres Projekt!)
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    SERVICE_TABLE_ENTRYW table[] =
    {
        { const_cast<LPWSTR>(SERVICE_NAME), ServiceMain },
        { nullptr, nullptr }
    };

    if (!StartServiceCtrlDispatcherW(table))
    {
        DWORD err = GetLastError();

        if (err == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT)
        {
            // Debug-Modus (nicht als Service gestartet)
            MessageBox(nullptr, L"Läuft NICHT als Service (Debugmodus)", SERVICE_NAME, MB_OK);
            RunConsoleMode();
            return 0;
        }

        return 1;
    }

    return 0;
}

void RunConsoleMode()
{
    gStopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!gStopEvent)
    {
        OutputDebugString(L"CreateEvent fehlgeschlagen (ConsoleMode).\n");
        return;
    }

    OutputDebugString(L"DPAPIDienst startet im ConsoleMode.\n");

    HANDLE hHttpThread = CreateThread(
        nullptr,
        0,
        LocalHttpServerThread,
        nullptr,
        0,
        nullptr);

    if (hHttpThread)
    {
        OutputDebugString(L"Lokaler HTTP-Thread wurde gestartet (ConsoleMode).\n");
    }
    else
    {
        OutputDebugString(L"FEHLER: Lokaler HTTP-Thread konnte nicht gestartet werden (ConsoleMode).\n");
    }

    while (WaitForSingleObject(gStopEvent, 10000) == WAIT_TIMEOUT)
    {
        OutputDebugString(L"DPAPIDienst läuft im ConsoleMode. Wartet auf Blazor-Anfrage.\n");
    }

    if (hHttpThread)
    {
        WaitForSingleObject(hHttpThread, 5000);
        CloseHandle(hHttpThread);
    }

    CloseHandle(gStopEvent);
    gStopEvent = nullptr;
    OutputDebugString(L"DPAPIDienst beendet ConsoleMode.\n");
}

// ------------------------------------------------------------
void WINAPI ServiceMain(DWORD, LPWSTR*)
{
    gStatusHandle = RegisterServiceCtrlHandler(SERVICE_NAME, ServiceCtrlHandler);
    if (!gStatusHandle)
    {
        OutputDebugString(L"RegisterServiceCtrlHandler fehlgeschlagen.\n");
        return; // kein SetServiceStatus mit NULL-Handle
    }

    gStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    gStatus.dwCurrentState = SERVICE_START_PENDING;
    gStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    SetServiceStatus(gStatusHandle, &gStatus);

    gStopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

    // „Hello“
    OutputDebugString(L"DPAPIDienst gestartet \n");

    gStatus.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(gStatusHandle, &gStatus);

    OutputDebugString(L"Erzeuge lokalen HTTP-Thread...\n");

    HANDLE hHttpThread = CreateThread(
        nullptr,
        0,
        LocalHttpServerThread,
        nullptr,
        0,
        nullptr);

    if (hHttpThread)
    {
        OutputDebugString(L"Lokaler HTTP-Thread wurde gestartet.\n");
    }
    else
    {
        OutputDebugString(L"FEHLER: Lokaler HTTP-Thread konnte nicht gestartet werden.\n");
    }

    // --- Hauptloop ---
    while (WaitForSingleObject(gStopEvent, 10000) == WAIT_TIMEOUT)
    {

        OutputDebugString(L"DPAPIDienst läuft. Wartet auf Blazor-Anfrage.\n");
    }

    OutputDebugString(L"DPAPIDienst wird beendet \n");

    if (hHttpThread)
    {
        WaitForSingleObject(hHttpThread, 5000);
        CloseHandle(hHttpThread);
    }

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

// -------------  DecryptClientIdFromFile() ----------------
std::wstring DecryptClientIdFromFile()
{
    wchar_t* localAppData = nullptr;
    size_t len = 0;

    _wdupenv_s(&localAppData, &len, L"LOCALAPPDATA");

    if (localAppData == nullptr)
    {
        OutputDebugString(L"LOCALAPPDATA nicht gefunden.\n");
        return L"";
    }

    std::wstring filePath =
        std::wstring(localAppData) +
        L"\\ImpulsAufKurs\\client.dat";

    OutputDebugString(filePath.c_str());
    OutputDebugString(L"\n");

    free(localAppData);
    std::ifstream file(filePath, std::ios::binary);

    if (!file.is_open())
    {
        OutputDebugString(L"client.dat nicht gefunden.\n");
        return L"";
    }

    std::string encryptedData(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());

    DATA_BLOB inputBlob{};
    inputBlob.pbData = reinterpret_cast<BYTE*>(encryptedData.data());
    inputBlob.cbData = static_cast<DWORD>(encryptedData.size());

    DATA_BLOB outputBlob{};

    BOOL ok = CryptUnprotectData(
        &inputBlob,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        0,
        &outputBlob);

    if (!ok)
    {
        OutputDebugString(L"DPAPI Entschlüsselung fehlgeschlagen.\n");
        return L"";
    }

    std::wstring clientId(
        reinterpret_cast<wchar_t*>(outputBlob.pbData));

    LocalFree(outputBlob.pbData);

    return clientId;
}

std::string WStringToUtf8(const std::wstring& text)
{
    int sizeNeeded = WideCharToMultiByte(
        CP_UTF8,
        0,
        text.c_str(),
        -1,
        nullptr,
        0,
        nullptr,
        nullptr);

    if (sizeNeeded <= 0)
    {
        return "";
    }

    std::string result(sizeNeeded - 1, '\0');

    WideCharToMultiByte(
        CP_UTF8,
        0,
        text.c_str(),
        -1,
        result.data(),
        sizeNeeded,
        nullptr,
        nullptr);

    return result;
}

DWORD WINAPI LocalHttpServerThread(LPVOID)
{
    OutputDebugString(L"Lokaler HTTP-Server Thread startet...\n");

    WSADATA wsaData{};

    int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsaData);

    if (wsaResult != 0)
    {
        OutputDebugString(L"FEHLER: WSAStartup fehlgeschlagen.\n");
        return 1;
    }

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (listenSocket == INVALID_SOCKET)
    {
        OutputDebugString(L"FEHLER: socket() fehlgeschlagen.\n");
        WSACleanup();
        return 1;
    }

    sockaddr_in service{};
    service.sin_family = AF_INET;
    service.sin_addr.s_addr = inet_addr("127.0.0.1");
    service.sin_port = htons(5055);

    if (bind(
        listenSocket,
        reinterpret_cast<SOCKADDR*>(&service),
        sizeof(service)) == SOCKET_ERROR)
    {
        wchar_t buffer[256]{};
        wsprintf(buffer, L"FEHLER: bind() fehlgeschlagen. WSA Fehler: %d\n", WSAGetLastError());
        OutputDebugString(buffer);

        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    OutputDebugString(L"bind() erfolgreich: 127.0.0.1:5055\n");

    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        wchar_t buffer[256]{};
        wsprintf(buffer, L"FEHLER: listen() fehlgeschlagen. WSA Fehler: %d\n", WSAGetLastError());
        OutputDebugString(buffer);

        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    OutputDebugString(L"HTTP Server lauscht auf 127.0.0.1:5055\n");

    while (WaitForSingleObject(gStopEvent, 100) == WAIT_TIMEOUT)
    {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listenSocket, &readSet);

        timeval timeout{};
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int selectResult = select(0, &readSet, nullptr, nullptr, &timeout);

        if (selectResult > 0)
        {
            SOCKET clientSocket = accept(listenSocket, nullptr, nullptr);

            if (clientSocket == INVALID_SOCKET)
            {
                continue;
            }

            char requestBuffer[2048]{};
            recv(clientSocket, requestBuffer, sizeof(requestBuffer) - 1, 0);

            std::wstring clientId = DecryptClientIdFromFile();

            std::wstring xml;

            if (!clientId.empty())
            {
                xml =
                    L"<?xml version=\"1.0\"?>"
                    L"<Client>"
                    L"<Status>OK</Status>"
                    L"<ClientId>" + clientId + L"</ClientId>"
                    L"</Client>";
            }
            else
            {
                xml =
                    L"<?xml version=\"1.0\"?>"
                    L"<Client>"
                    L"<Status>ERROR</Status>"
                    L"<Message>Keine ClientId gefunden</Message>"
                    L"</Client>";
            }

            std::string body = WStringToUtf8(xml);

            std::string response =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/xml; charset=utf-8\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Connection: close\r\n"
                "Content-Length: " + std::to_string(body.size()) + "\r\n"
                "\r\n" +
                body;

            send(
                clientSocket,
                response.c_str(),
                static_cast<int>(response.size()),
                0);

            closesocket(clientSocket);
        }
    }

    closesocket(listenSocket);
    WSACleanup();

    OutputDebugString(L"Lokaler HTTP-Server Thread beendet.\n");

    return 0;
}

// --- Ende ---