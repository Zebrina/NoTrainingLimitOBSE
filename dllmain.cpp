#include "obse64\PluginAPI.h"
#include "obse64_common\obse64_version.h"

#include <Windows.h>
#include <TlHelp32.h>

static void SafeWrite(BYTE* address, BYTE* buffer, DWORD bufferSize)
{
    DWORD oldProtect;
    VirtualProtect(address, bufferSize, PAGE_EXECUTE_READWRITE, &oldProtect);
    memcpy(address, buffer, bufferSize);
    VirtualProtect(address, bufferSize, oldProtect, &oldProtect);
}

static int FindSignature(BYTE* buffer, DWORD bufferSize, short* signature, DWORD signatureSize)
{
    for (int i = 0, n = bufferSize - signatureSize; i < n; ++i)
    {
        int j = 0, m = signatureSize;
        for (; j < m; ++j)
        {
            if (signature[j] != -1 && signature[j] != buffer[i + j])
                break;
        }

        if (j == m)
            return i;
    }
    return -1;
}

static bool ApplyPatch()
{
    HMODULE obHModule = GetModuleHandle(NULL);

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);
    MODULEENTRY32 entry{ 0 };
    entry.dwSize = sizeof(MODULEENTRY32);

    if (!Module32First(hSnapshot, &entry))
    {
        // https://learn.microsoft.com/en-us/windows/win32/api/tlhelp32/nf-tlhelp32-createtoolhelp32snapshot
        // "If the function fails with ERROR_BAD_LENGTH, retry the function until it succeeds."
        /*
        DWORD error = GetLastError();
        if (error == ERROR_BAD_LENGTH)
            continue;
        */
        CloseHandle(hSnapshot);
        return false;
    }

    BYTE* base = NULL;
    DWORD size = 0;

    do
    {
        if (entry.hModule = obHModule)
        {
            base = entry.modBaseAddr;
            size = entry.modBaseSize;
            CloseHandle(hSnapshot);
            break;
        }
    }
    while (Module32Next(hSnapshot, &entry));

    CloseHandle(hSnapshot);

    if (size == 0)
        return false;

    BYTE* buffer = new BYTE[size];
    BOOL readSuccess = ReadProcessMemory(GetCurrentProcess(), (LPCVOID)base, buffer, size, NULL);

    bool patchSuccess = false;

    if (readSuccess)
    {
        // Initial comparison.
        short signature1[]
        {
            0x8B, 0x05, -1, -1, -1, -1,                         // mov eax,[OblivionRemastered-Win64-Shipping.exe + 8FE77F8] { (5) }
            0x39, 0x81, 0x34, 0x08, 0x00, 0x00,                 // cmp [rcx+00000834],eax
            0x7C, 0x45,                                         // jl OblivionRemastered-Win64-Shipping.exe+67C4D83
            0x4C, 0x8B, 0x05, -1, -1, -1, -1,                   // mov r8,[OblivionRemastered-Win64-Shipping.exe+8FDEE40] { (203B3A86194) }
            0xBA, 0xDE, 0x0F, 0x00, 0x00,                       // mov edx,00000FDE { 4062 }
            0x49, 0x8B, 0x8E, 0x90, 0x00, 0x00, 0x00,           // mov rcx,[r14+00000090]
            0xE8, -1, -1, -1, -1,                               // call OblivionRemastered-Win64-Shipping.exe+671E770
            0x40, 0xB6, 0x01,                                   // mov sil,01 { 1 }
            0x0F, 0x28, 0xD6,                                   // movaps xmm2,xmm6
            0xBA, 0xA1, 0x0F, 0x00, 0x00,                       // mov edx,00000FA1 { 4001 }
            0x49, 0x8B, 0x4E, 0x70,                             // mov rcx,[r14+70]
            0xE8, -1, -1, -1, -1,                               // call OblivionRemastered-Win64-Shipping.exe+671E700
            0xF3, 0x0F, 0x10, 0x15, -1, -1, -1, -1,             // movss xmm2,[OblivionRemastered-Win64-Shipping.exe+8740FF8] { (2.00) }
            0xBA, 0xA1, 0x0F, 0x00, 0x00,                       // mov edx,00000FA1 { 4001 }
            0x49, 0x8B, 0x8E, 0x90, 0x00, 0x00, 0x00,           // mov rcx,[r14+00000090]
            0xE8, -1, -1, -1, -1,                               // call OblivionRemastered-Win64-Shipping.exe+671E700
            0x48, 0x8D, 0x4D, 0x50,                             // lea rcx,[rbp+50]
        };

        // After trained comparison.
        short signature2[]
        {
            0x8B, 0x05, -1, -1, -1, -1,                         // mov eax,[OblivionRemastered-Win64-Shipping.exe + 8FE77F8] { (5) }
            0x39, 0x81, 0x34, 0x08, 0x00, 0x00,                 // cmp [rcx+00000834],eax
            0x7C, 0x4A,                                         // jl OblivionRemastered-Win64-Shipping.exe+67C5244
            0x4C, 0x8B, 0x05, -1, -1, -1, -1,                   // mov r8,[OblivionRemastered-Win64-Shipping.exe+8FDEE40] { (203B3A86194) }
            0xBA, 0xDE, 0x0F, 0x00, 0x00,                       // mov edx,00000FDE { 4062 }
            0x48, 0x8B, 0x8E, 0x90, 0x00, 0x00, 0x00,           // mov rcx,[rsi+00000090]
            0xE8, -1, -1, -1, -1,                               // call OblivionRemastered-Win64-Shipping.exe+671E770
            0x40, 0xB7, 0x01,                                   // mov dil,01 { 1 }
            0xF3, 0x0F, 0x10, 0x15, -1, -1, -1, -1,             // movss xmm2,[OblivionRemastered - Win64 - Shipping.exe + 8740FAC] { (1.00) }
            0xBA, 0xA1, 0x0F, 0x00, 0x00,                       // mov edx,00000FA1 { 4001 }
            0x48, 0x8B, 0x4E, 0x70,                             // mov rcx,[rsi + 70]
            0xE8, -1, -1, -1, -1,                               // call OblivionRemastered-Win64-Shipping.exe+671E700
            0xF3, 0x0F, 0x10, 0x15, -1, -1, -1, -1,             // movss xmm2,[OblivionRemastered-Win64-Shipping.exe+8740FF8] { (2.00) }
            0xBA, 0xA1, 0x0F, 0x00, 0x00,                       // mov edx,00000FA1 { 4001 }
            0x48, 0x8B, 0x8E, 0x90, 0x00, 0x00, 0x00,           // mov rcx,[rsi+00000090]
            0xE8, -1, -1, -1, -1,                               // call OblivionRemastered-Win64-Shipping.exe+671E700
            0x48, 0x8D, 0x8C, 0x24, 0x50, 0x01, 0x00, 0x00,     // lea rcx,[rsp + 00000150]
        };

        BYTE patch[]
        {
            0xB8, 0xFF, 0xFF, 0xFF, 0x7F,   // mov eax, 0x7FFFFFFF { 2147483647 }
            0x90,                           // nop
        };

        int offset1 = FindSignature(buffer, size, signature1, sizeof(signature1) / 2);
        int offset2 = FindSignature(buffer, size, signature2, sizeof(signature2) / 2);

        if (offset1 >= 0 && offset2 >= 0)
        {
            SafeWrite(base + offset1, patch, sizeof(patch));
            SafeWrite(base + offset2, patch, sizeof(patch));
            patchSuccess = true;
        }
    }

    delete[] buffer;

    return patchSuccess;
}

extern "C" __declspec(dllexport) OBSEPluginVersionData OBSEPlugin_Version =
{
    OBSEPluginVersionData::kVersion,
    1,
    "No Training Limit",
    "Zebrina",
    OBSEPluginVersionData::kAddressIndependence_Signatures,
    OBSEPluginVersionData::kStructureIndependence_NoStructs,
    { },
    0,
    0, 0, 0	// Reserved
};

extern "C" __declspec(dllexport) bool __cdecl OBSEPlugin_Load(const OBSEInterface* obse)
{
    return ApplyPatch();
}
