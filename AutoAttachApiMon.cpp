#include <windows.h>
#include <commctrl.h>
#include <iostream>
#include <vector>
#include <string>
#include <conio.h>

#include "ProcessCreatedEventDispatcher.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "comctl32.lib")

BOOL CALLBACK EnumChildProc(HWND hwnd, LPARAM lParam);
HWND FindChildWindowByClass(HWND parent, LPCWSTR className);
int GetListViewItemCount(HWND hwndListView);
int GetListViewColumnCount(HWND hwndListView);
std::wstring GetListViewItemText(HWND hwndListView, int itemIndex, int subItemIndex);
bool WildcardMatch(const std::wstring& str, const std::wstring& pattern);
bool EnableDebugPrivilege();
void BringWindowToForeground(HWND hWnd);
// Global variables
HWND g_hListView = nullptr;
HWND g_hwndMain = nullptr;
std::wstring processFilter;

int main()
{   
    LPWSTR commandLine = GetCommandLineW();
    int argc;
    LPWSTR* argv = CommandLineToArgvW(commandLine, &argc);

    if (argc < 2) {
        std::cout << "At least one argument is required." << std::endl;
        return 1; // Exit with error code 1
    }

    processFilter = std::wstring(argv[1]);
    LocalFree(argv);

    // needed to monitor when running elevated
    EnableDebugPrivilege();

    // Find the main window
    #ifdef _WIN64 // Check if building for 64-bit architecture
        g_hwndMain = FindWindow(NULL, L"Monitoring - API Monitor v2 64-bit (Administrator)");
        if (!g_hwndMain)
        {
            g_hwndMain = FindWindow(NULL, L"Monitoring - API Monitor v2 64-bit");
        }
    #else
        g_hwndMain = FindWindow(NULL, L"Monitoring - API Monitor v2 32-bit (Administrator)");
        if (!g_hwndMain)
        {
            g_hwndMain = FindWindow(NULL, L"Monitoring - API Monitor v2 32-bit");
        }
    #endif

    if (!g_hwndMain) {
        std::wcerr << L"API Monitor 64-bit not running!" << std::endl;
        return 1;
    }

    HWND hwndRunningProcesses = FindWindowEx(g_hwndMain, NULL, NULL, L"Running Processes");
    if (!hwndRunningProcesses)
    {
        std::wcerr << L"Running processes not found - make sure monitoring is on!" << std::endl;
        return 1;
    }

    g_hListView = FindChildWindowByClass(hwndRunningProcesses, L"SysListView32");


    if (!g_hListView) {
        std::wcerr << L"SysListView32 control not found" << std::endl;
        return 1;
    }

    

    // Extract table info

    int columnCount = GetListViewColumnCount(g_hListView);

    if (columnCount == 0)
    {
        std::wcout << L"Unable to detect any running processes in API Monitor. If API monitor is running as admin, make sure this is running as admin too." << std::endl;
        return 1;
    }
    
    std::wcout << L"Current running processes in API monitor ...";

    std::wcout << L"ColumnCount = " << columnCount << std::endl;

    int rowCount = GetListViewItemCount(g_hListView);
    std::wcout << L"RowCount = " << rowCount << std::endl;

    // Print table content
    for (int i = 0; i < rowCount; ++i) {
        for (int j = 0; j < columnCount; ++j) {
            std::wcout << GetListViewItemText(g_hListView, i, j) << L"\t";
        }
        std::wcout << std::endl;
    }

    ProcessCreatedEventDispatcher ProcessCreatedEventDispatcher{};
    ProcessCreatedEventDispatcher.NewProcessCreatedListeners.emplace_back([](auto processName, auto processId) {
        std::wcout << L"Process Name: " << processName << L" Process Id:" << processId << std::endl;
        if (WildcardMatch(processName, processFilter))
        {
            std::wcout << "monitoring!" << std::endl;
            ClickContextMenuItem(g_hListView, processId);
        }
        std::flush(std::cout);
        });

#ifdef _WIN64
    std::wcout << L"Waiting for 64-bit processes matching '" << processFilter << L"'" << std::endl;
#else
    std::wcout << L"Waiting for 32-bit processes matching '" << processFilter << L"'" << std::endl;
#endif
    // Wait for key press to exit the program
    std::cout << "Press any key to terminate" << std::endl;
    while (!_kbhit()) {}

    return 0;
}

void BringWindowToForeground(HWND hwnd) {
    if (!hwnd) {
        std::cerr << "Invalid window handle." << std::endl;
        return;

    }

    // Show the window if it is minimized
    if (IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    }
    else {
        ShowWindow(hwnd, SW_SHOW);
    }

    // Bring the window to the foreground and set focus
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
}


void EnsureVisible(HWND hwndListView, int itemIndex) {

    SendMessage(hwndListView, LVM_ENSUREVISIBLE, (WPARAM)itemIndex, TRUE);

}


bool ClickContextMenuItem(HWND hwndListView, const std::wstring& matchText) {

    HWND hwndForeground = GetForegroundWindow();
   
    BringWindowToForeground(g_hwndMain);
    
    int itemCount = GetListViewItemCount(hwndListView);
    
    bool itemFound = false;
    
    int itemIndex = -1;


    for (int i = 0; i < itemCount; ++i) {
        std::wstring itemText = GetListViewItemText(hwndListView, i, 1);  // 2nd column is index 1
        if (itemText == matchText) {
            itemIndex = i;
            itemFound = true;
            break;
        }
    }

    if (!itemFound) {
        std::wcerr << L"No matching item found." << std::endl;
        return false;
    }

    // Select the found item
    // Ensure the item is visible
    EnsureVisible(hwndListView, itemIndex);

    // Get the process ID of the target process
    DWORD processId;
    GetWindowThreadProcessId(hwndListView, &processId);

    // Open the target process
    HANDLE hProcess = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, FALSE, processId);
    if (!hProcess) {
        std::wcerr << L"Failed to open process. Error: " << GetLastError() << std::endl;
        return false;
    }

    // Allocate memory in the target process for the RECT structure
    RECT* pRemoteRect = (RECT*)VirtualAllocEx(hProcess, NULL, sizeof(RECT), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!pRemoteRect) {
        std::wcerr << L"Failed to allocate memory in target process. Error: " << GetLastError() << std::endl;
        CloseHandle(hProcess);
        return false;
    }

    // Send the LVM_GETITEMRECT message to the ListView control
    SendMessage(hwndListView, LVM_GETITEMRECT, (WPARAM)itemIndex, (LPARAM)pRemoteRect);

    // Read the RECT structure from the target process
    RECT itemRect;
    if (!ReadProcessMemory(hProcess, pRemoteRect, &itemRect, sizeof(RECT), NULL)) {
        std::wcerr << L"Failed to read item rectangle from target process. Error: " << GetLastError() << std::endl;
        VirtualFreeEx(hProcess, pRemoteRect, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    // Free the allocated memory in the target process
    VirtualFreeEx(hProcess, pRemoteRect, 0, MEM_RELEASE);
    CloseHandle(hProcess);
    // Calculate the middle point of the item rectangle
    POINT pt = {
        (itemRect.left + itemRect.right) / 2,
        (itemRect.top + itemRect.bottom) / 2
    };

    // Convert to screen coordinates
    ClientToScreen(hwndListView, &pt);

    // Set the cursor position to the item's center
    SetCursorPos(pt.x, pt.y);

    // Simulate mouse down and up to perform a left click
    INPUT inputs[7] = {};
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;

    inputs[1].type = INPUT_MOUSE;
    inputs[1].mi.dwFlags = MOUSEEVENTF_RIGHTUP;

    // DOWN arrow key down
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = VK_DOWN;        // Virtual-key code for the DOWN arrow key
    inputs[2].ki.dwFlags = 0;          // Key down event

    // DOWN arrow key up
    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_DOWN;        // Virtual-key code for the DOWN arrow key
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP; // Key up event

    // ENTER key down
    inputs[4].type = INPUT_KEYBOARD;
    inputs[4].ki.wVk = VK_RETURN;      // Virtual-key code for the ENTER key
    inputs[4].ki.dwFlags = 0;          // Key down event

    // ENTER key up
    inputs[5].type = INPUT_KEYBOARD;
    inputs[5].ki.wVk = VK_RETURN;      // Virtual-key code for the ENTER key
    inputs[5].ki.dwFlags = KEYEVENTF_KEYUP; // Key up event

    SendInput(6, inputs, sizeof(INPUT));

    BringWindowToForeground(hwndForeground);
    return true;
}

bool EnableDebugPrivilege()
{
    HANDLE hToken;

    // Open a handle to the access token for the calling process.
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        std::cerr << "OpenProcessToken error: " << GetLastError() << std::endl;
        return false;
    }
    TOKEN_PRIVILEGES tp;
    LUID luid;

    if (!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luid)) {
        std::cerr << "LookupPrivilegeValue error: " << GetLastError() << std::endl;
        return false;
    }

    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    // Adjust Token Privileges
    if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), (PTOKEN_PRIVILEGES)NULL, (PDWORD)NULL)) {
        std::cerr << "AdjustTokenPrivileges error: " << GetLastError() << std::endl;
        return false;
    }

    if (GetLastError() == ERROR_NOT_ALL_ASSIGNED) {
        std::cerr << "Unable to enable debug privilege. This is expected if not running elevated." << std::endl;
        return false;
    }

    return true;
}

bool WildcardMatch(const std::wstring& str, const std::wstring& pattern) {
    if (pattern.empty()) return str.empty();
    if (pattern[0] == L'*') {
        return WildcardMatch(str, pattern.substr(1)) || (!str.empty() && WildcardMatch(str.substr(1), pattern));
    }
    else if (pattern[0] == L'?' || pattern[0] == str[0]) {
        return WildcardMatch(str.substr(1), pattern.substr(1));
    }
    else {
        return false;
    }
}


// Enumerate child windows and find the target window
BOOL CALLBACK EnumChildProc(HWND hwnd, LPARAM lParam)
{
    HWND* result = reinterpret_cast<HWND*>(lParam);
    wchar_t className[256];
    GetClassName(hwnd, className, sizeof(className) / sizeof(className[0]));

    if (wcscmp(className, L"SysListView32") == 0) {
        *result = hwnd;
        return FALSE;
    }
    return TRUE;
}

// Find child window by class name
HWND FindChildWindowByClass(HWND parent, LPCWSTR className)
{
    HWND result = nullptr;
    EnumChildWindows(parent, EnumChildProc, reinterpret_cast<LPARAM>(&result));
    return result;
}

// Get the number of items in the ListView
int GetListViewItemCount(HWND hwndListView)
{
    return static_cast<int>(SendMessage(hwndListView, LVM_GETITEMCOUNT, 0, 0));
}

// Get the number of columns in the ListView
int GetListViewColumnCount(HWND hwndListView)
{
    HWND header = (HWND)SendMessage(hwndListView, LVM_GETHEADER, 0, 0);
    return static_cast<int>(SendMessage(header, HDM_GETITEMCOUNT, 0, 0));
}

// Get the text of a specific item in the ListView
std::wstring GetListViewItemText(HWND hwndListView, int itemIndex, int subItemIndex)
{
    DWORD processId;
    GetWindowThreadProcessId(hwndListView, &processId);

    HANDLE hProcess = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, FALSE, processId);
    if (!hProcess) {
        int err = GetLastError();
        std::wcerr << L"Failed to open process err#" << err << std::endl;
        return L"";
    }

    // Allocate memory in the target process
    SIZE_T bufferSize = 256 * sizeof(wchar_t);
    SIZE_T lvItemSize = sizeof(LVITEM);
    SIZE_T totalSize = lvItemSize + bufferSize;
    LPVOID pRemoteBuffer = VirtualAllocEx(hProcess, nullptr, totalSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!pRemoteBuffer) {
        std::wcerr << L"Failed to allocate memory in target process" << std::endl;
        CloseHandle(hProcess);
        return L"";
    }

    // Write the LVITEM structure to the target process memory
    LVITEM lvItem = { 0 };
    lvItem.mask = LVIF_TEXT;
    lvItem.iItem = itemIndex;
    lvItem.iSubItem = subItemIndex;
    lvItem.pszText = (LPWSTR)((LPBYTE)pRemoteBuffer + lvItemSize);
    lvItem.cchTextMax = 256;

    if (!WriteProcessMemory(hProcess, pRemoteBuffer, &lvItem, lvItemSize, nullptr)) {
        std::wcerr << L"Failed to write LVITEM to target process memory" << std::endl;
        VirtualFreeEx(hProcess, pRemoteBuffer, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return L"";
    }

    // Send the LVM_GETITEMTEXT message to the ListView control
    SendMessage(hwndListView, LVM_GETITEMTEXT, itemIndex, (LPARAM)pRemoteBuffer);

    // Read the text back from the target process memory
    wchar_t buffer[256];
    if (!ReadProcessMemory(hProcess, (LPBYTE)pRemoteBuffer + lvItemSize, buffer, bufferSize, nullptr)) {
        std::wcerr << L"Failed to read item text from target process memory" << std::endl;
        VirtualFreeEx(hProcess, pRemoteBuffer, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return L"";
    }

    // Clean up
    VirtualFreeEx(hProcess, pRemoteBuffer, 0, MEM_RELEASE);
    CloseHandle(hProcess);

    return std::wstring(buffer);
}

