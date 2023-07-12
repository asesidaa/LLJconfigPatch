#include <windows.h>
#include "MinHook.h"
#include "winusb.h"
#include <array>
#include <format>

static std::array<BYTE, 23> cardData=
    {0x04, 0xE3, 0xDA, 0xC2, 0x8C, 0x56, 0x80,
  0x37, 0x30, 0x32, 0x30, 0x33, 0x39, 0x32, 0x30, 0x31, 0x30, 0x32, 0x38, 0x31, 0x35, 0x30, 0x32 };
static bool cardInserted = false;

static DWORD WINAPI InsertCardThread(LPVOID)
{
    static bool keyDown;
    while (true)
    {
        if (GetAsyncKeyState(0x50)& 0x01)
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

static DWORD WINAPI InitThread(LPVOID)
{
    Sleep(1000);
    const auto handle = reinterpret_cast<char*>(GetModuleHandleA("S6145-5Husb.dll"));
    if (handle == INVALID_HANDLE_VALUE)
    {
        OutputDebugStringA("GetModuleHandleA failed");
    }
    OutputDebugStringA(std::format("Handle address: 0x{:X}", (unsigned long long)handle).c_str());

    MH_Initialize();
    auto status = MH_CreateHook(handle+0x9010, WinUsb_ReadPipe_Wrap, reinterpret_cast<LPVOID*>(&gOrigWinUsb_ReadPipe));
    if (status != MH_OK)
    {
        OutputDebugStringA(std::format("MH_CreateHook failed, error: {:X}", (int)status).c_str());
    }
    MH_EnableHook(nullptr);

    return 1;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        CreateThread(nullptr, 0, InsertCardThread, nullptr, 0, nullptr);
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
	
    return TRUE;
}

