#include <windows.h>
#include <windowsx.h>
#include "MinHook.h"
#include "winusb.h"
#include <array>
#include <chrono>
#include <format>

static std::array<BYTE, 23> cardData =
{
    0x04, 0xE3, 0xDA, 0xC2, 0x8C, 0x56, 0x80,
    0x37, 0x30, 0x32, 0x30, 0x33, 0x39, 0x32, 0x30, 0x31, 0x30, 0x32, 0x38, 0x31, 0x35, 0x30, 0x32
};
static bool cardInserted = false;

static DWORD InsertCardThread(LPVOID)
{
    static bool keyDown;
    while (true)
    {
        if (GetAsyncKeyState(0x50) & 0x01)
        {
            if (!keyDown)
            {
                cardInserted = !cardInserted;
                keyDown = true;
            }
        }
        else
        {
            keyDown = false;
        }
        Sleep(100);
    }
}

BOOL (* __fastcall gOrigWinUsb_ReadPipe)(
    WINUSB_INTERFACE_HANDLE InterfaceHandle,
    UCHAR PipeID,
    PUCHAR Buffer,
    ULONG BufferLength,
    PULONG LengthTransferred,
    LPOVERLAPPED Overlapped);

BOOL __fastcall WinUsb_ReadPipe_Wrap(
    WINUSB_INTERFACE_HANDLE InterfaceHandle,
    UCHAR PipeID,
    PUCHAR Buffer,
    ULONG BufferLength,
    PULONG LengthTransferred,
    LPOVERLAPPED Overlapped)
{
    //OutputDebugStringA("Success");
    auto ret = gOrigWinUsb_ReadPipe(InterfaceHandle, PipeID, Buffer, BufferLength, LengthTransferred, Overlapped);
    if (cardInserted)
    {
        memcpy(Buffer + BufferLength - 23, cardData.data(), 23);
        Buffer[40] = 0x19;
    }
    return ret;
}

typedef struct
{
    POINT pos;
    bool lastButtonPressed;
    DWORD touchEvent;
} mouse_state_t;

mouse_state_t mouseState;
POINTER_TOUCH_INFO previousUpdate{};

static DWORD TouchDetectThread(LPVOID)
{
    while (true)
    {
        bool buttonPressed = ((GetKeyState(VK_LBUTTON) & 0x100) != 0);

        // figure out what kind of touch event to simulate
        if (buttonPressed && !mouseState.lastButtonPressed) {
            mouseState.touchEvent = TOUCHEVENTF_DOWN;
        } else if (buttonPressed && mouseState.lastButtonPressed) {
            mouseState.touchEvent = TOUCHEVENTF_MOVE;
        } else if (!buttonPressed && mouseState.lastButtonPressed) {
            mouseState.touchEvent = TOUCHEVENTF_UP;
        }

        mouseState.lastButtonPressed = buttonPressed;
        if (mouseState.touchEvent)
        {
            GetCursorPos(&mouseState.pos);
            POINTER_TOUCH_INFO touchInfo = {};
            touchInfo.pointerInfo.pointerType = PT_TOUCH;
            touchInfo.pointerInfo.pointerId = 1; 
            touchInfo.touchFlags = TOUCH_FLAG_NONE;
            touchInfo.touchMask = TOUCH_MASK_CONTACTAREA | TOUCH_MASK_ORIENTATION | TOUCH_MASK_PRESSURE;
            touchInfo.orientation = 90;
            touchInfo.pressure = 32000;
            touchInfo.rcContact.top = mouseState.pos.y - 2;
            touchInfo.rcContact.bottom = mouseState.pos.y + 2;
            touchInfo.rcContact.left = mouseState.pos.x - 2;
            touchInfo.rcContact.right = mouseState.pos.x + 2;
            touchInfo.pointerInfo.ptPixelLocation.x = mouseState.pos.x;
            touchInfo.pointerInfo.ptPixelLocation.y = mouseState.pos.y;
            switch (mouseState.touchEvent)
            {
                case TOUCHEVENTF_DOWN:
                    touchInfo.pointerInfo.pointerFlags = POINTER_FLAG_DOWN | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT;
                    break;
                case TOUCHEVENTF_MOVE:
                    touchInfo.pointerInfo.pointerFlags = POINTER_FLAG_UPDATE | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT;
                    previousUpdate = touchInfo;
                    break;
                case TOUCHEVENTF_UP:
                    if (previousUpdate.pressure != 0)
                    {
                        touchInfo = previousUpdate;
                    }
                    touchInfo.pointerInfo.pointerFlags = POINTER_FLAG_UP;
                    break;
                default:
                    break;
            }
            InjectTouchInput(1, &touchInfo);
            mouseState.touchEvent = 0;
        }
        Sleep(10);
    }
}

int (*OrigGetSystemMetrics)(int nIndex);
int GetSystemMetricsHook(int nIndex)
{
    if (nIndex == SM_DIGITIZER)
    {
        return 1;
    }
    return OrigGetSystemMetrics(nIndex);
}

bool IsTouchDeviceAvailableHook()
{
    OutputDebugStringA("IsTouchDeviceAvailableHook called");
    return true;
}

BOOL (*OrigRegisterTouchWindow)(HWND hwnd, ULONG ulFlags);
BOOL WINAPI RegisterTouchWindowHook(HWND hwnd, ULONG ulFlags)
{
    OutputDebugStringA("RegisterTouchWindowHook called");
    return true;
}

int64_t getPrinterStatusHook(int64_t a1, int64_t a2)
{
    *(BYTE*)(a1 + 112) = 1;
    *(BYTE*)(a1 + 189) = 0;
    *(int64_t*)(a1 + 152) = 0i64;
    return 0;
}

void Init()
{
    InitializeTouchInjection(10, TOUCH_FEEDBACK_DEFAULT);
    // If folder "printSave" does not exist, create it
    CreateDirectoryA("printSave", nullptr);
    MH_Initialize();

    LoadLibraryA("jconfig.dll");
    const auto handle = reinterpret_cast<char*>(GetModuleHandleA("jconfig.dll"));
    if (handle != INVALID_HANDLE_VALUE)
    {
        OutputDebugStringA(std::format("Handle address: 0x{:X}", (unsigned long long)handle).c_str());
        auto status = MH_CreateHook(handle + 0x9010, WinUsb_ReadPipe_Wrap,
                                    reinterpret_cast<LPVOID*>(&gOrigWinUsb_ReadPipe));
        if (status != MH_OK)
        {
            OutputDebugStringA(std::format("MH_CreateHook failed, reason: {}", MH_StatusToString(status)).c_str());
        }
    }

    const auto gameHandle = reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
    uintptr_t offset = 0x14023AAC0 - 0x140000000;
    MH_CreateHook((LPVOID)(gameHandle + offset), getPrinterStatusHook, nullptr);
    MH_CreateHookApi(L"user32.dll", "GetSystemMetrics", GetSystemMetricsHook,
                     reinterpret_cast<LPVOID*>(&OrigGetSystemMetrics));
    /*MH_CreateHookApi(L"user32.dll", "RegisterTouchWindow", RegisterTouchWindowHook,
                     reinterpret_cast<LPVOID*>(&OrigRegisterTouchWindow));*/
    offset = 0x140356390 - 0x140000000;
    MH_CreateHook((LPVOID)(gameHandle + offset), IsTouchDeviceAvailableHook, nullptr);

    MH_EnableHook(nullptr);
}


BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        Init();
        CreateThread(nullptr, 0, InsertCardThread, nullptr, 0, nullptr);
        CreateThread(nullptr, 0, TouchDetectThread, nullptr, 0, nullptr);
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }

    return TRUE;
}
