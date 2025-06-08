#include "obse64\PluginAPI.h"
#include "obse64_common\obse64_version.h"
#include "obse64_common\BranchTrampoline.h"
#include "obse64\GameObjects.h"

#include <Windows.h>
#include <TlHelp32.h>

static void SafeWrite(BYTE* address, BYTE* buffer, DWORD bufferSize)
{
    DWORD oldProtect;
    VirtualProtect(address, bufferSize, PAGE_EXECUTE_READWRITE, &oldProtect);
    memcpy(address, buffer, bufferSize);
    VirtualProtect(address, bufferSize, oldProtect, &oldProtect);
}

static intptr_t FindSignature(BYTE* buffer, DWORD bufferSize, short* signature, DWORD signatureSize)
{
    for (intptr_t i = 0, n = bufferSize - signatureSize; i < n; ++i)
    {
        intptr_t j = 0, m = signatureSize;
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

static bool gUnlimitedTraining = true;
static float gOverCapTrainingCostMult = 1.0f;

static BranchTrampoline gTrampoline;

static PlayerCharacter** gPlayer = nullptr;
static int* gTrainingPerLevel = nullptr;
static float* gTrainingCostMult = nullptr;

static int gMaxTrainingThisLevel = 5;

static byte GetPlayerLevel(PlayerCharacter* player)
{
    struct TESObjectNPC
    {
        byte pad[0x5A];
        short level;
    };
    return reinterpret_cast<TESObjectNPC*>(player->GetBaseForm())->level;
}

static int GetMaxTrainingSessionsInitial(PlayerCharacter* player)
{
    gMaxTrainingThisLevel = (*gTrainingPerLevel * GetPlayerLevel(player)) - player->timesTrainedTotal + player->timesTrainedThisLevel;
    return gMaxTrainingThisLevel;
}

static int GetMaxTrainingSessionsUpdate(PlayerCharacter* player)
{
    return gMaxTrainingThisLevel;
}

static float GetTrainingCost(float skillActorValue)
{
    PlayerCharacter* player = *gPlayer;

    float defaultCost = *gTrainingCostMult * skillActorValue;

    if (player->timesTrainedTotal >= (*gTrainingPerLevel * GetPlayerLevel(player)))
        defaultCost *= gOverCapTrainingCostMult;

    return defaultCost;
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

    if (size == 0)
        return false;

    BYTE* buffer = new BYTE[size];
    BOOL readSuccess = ReadProcessMemory(GetCurrentProcess(), (LPCVOID)base, buffer, size, NULL);

    bool patchSuccess = false;

    if (readSuccess)
    {
        // Initial comparison.
        short sigTrainingLimit1[]
        {
            // OblivionRemastered-Win64-Shipping.exe+67C4D30 - 8B 05 C22A8202        - mov eax,[OblivionRemastered-Win64-Shipping.exe+8FE77F8] { (5) }
            0x8B, 0x05, -1, -1, -1, -1,
            // OblivionRemastered-Win64-Shipping.exe+67C4D36 - 39 81 34080000        - cmp [rcx+00000834],eax
            0x39, 0x81, 0x34, 0x08, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C4D3C - 7C 45                 - jl OblivionRemastered-Win64-Shipping.exe+67C4D83
            0x7C, 0x45,
            // OblivionRemastered-Win64-Shipping.exe+67C4D3E - 4C 8B 05 FBA08102     - mov r8,[OblivionRemastered-Win64-Shipping.exe+8FDEE40] { (203B3A86194) }
            0x4C, 0x8B, 0x05, -1, -1, -1, -1,
            // OblivionRemastered-Win64-Shipping.exe+67C4D45 - BA DE0F0000           - mov edx,00000FDE { 4062 }
            0xBA, 0xDE, 0x0F, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C4D4A - 49 8B 8E 90000000     - mov rcx,[r14+00000090]
            0x49, 0x8B, 0x8E, 0x90, 0x00, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C4D51 - E8 1A9AF5FF           - call OblivionRemastered-Win64-Shipping.exe+671E770
            0xE8, -1, -1, -1, -1,
            // OblivionRemastered-Win64-Shipping.exe+67C4D56 - 40 B6 01              - mov sil,01 { 1 }
            0x40, 0xB6, 0x01,
            // OblivionRemastered-Win64-Shipping.exe+67C4D59 - 0F28 D6               - movaps xmm2,xmm6
            0x0F, 0x28, 0xD6,
            // OblivionRemastered-Win64-Shipping.exe+67C4D5C - BA A10F0000           - mov edx,00000FA1 { 4001 }
            0xBA, 0xA1, 0x0F, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C4D61 - 49 8B 4E 70           - mov rcx,[r14+70]
            0x49, 0x8B, 0x4E, 0x70,
            // OblivionRemastered-Win64-Shipping.exe+67C4D65 - E8 9699F5FF           - call OblivionRemastered-Win64-Shipping.exe+671E700
            0xE8, -1, -1, -1, -1,
            // OblivionRemastered-Win64-Shipping.exe+67C4D6A - F3 0F10 15 86C2F701   - movss xmm2,[OblivionRemastered-Win64-Shipping.exe+8740FF8] { (2.00) }
            0xF3, 0x0F, 0x10, 0x15, -1, -1, -1, -1,
            // OblivionRemastered-Win64-Shipping.exe+67C4D72 - BA A10F0000           - mov edx,00000FA1 { 4001 }
            0xBA, 0xA1, 0x0F, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C4D77 - 49 8B 8E 90000000     - mov rcx,[r14+00000090]
            0x49, 0x8B, 0x8E, 0x90, 0x00, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C4D7E - E8 7D99F5FF           - call OblivionRemastered-Win64-Shipping.exe+671E700
            0xE8, -1, -1, -1, -1,
            // OblivionRemastered-Win64-Shipping.exe+67C4D83 - 48 8D 4D 50           - lea rcx,[rbp+50]
            0x48, 0x8D, 0x4D, 0x50,
        };

        // After trained comparison.
        short sigTrainingLimit2[]
        {
            // OblivionRemastered-Win64-Shipping.exe+67C51EC - 8B 05 06268202        - mov eax,[OblivionRemastered-Win64-Shipping.exe+8FE77F8] { (5) }
            0x8B, 0x05, -1, -1, -1, -1,
            // OblivionRemastered-Win64-Shipping.exe+67C51F2 - 39 81 34080000        - cmp [rcx+00000834],eax
            0x39, 0x81, 0x34, 0x08, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C51F8 - 7C 4A                 - jl OblivionRemastered-Win64-Shipping.exe+67C5244
            0x7C, 0x4A,
            // OblivionRemastered-Win64-Shipping.exe+67C51FA - 4C 8B 05 3F9C8102     - mov r8,[OblivionRemastered-Win64-Shipping.exe+8FDEE40] { (203B3A86194) }
            0x4C, 0x8B, 0x05, -1, -1, -1, -1,
            // OblivionRemastered-Win64-Shipping.exe+67C5201 - BA DE0F0000           - mov edx,00000FDE { 4062 }
            0xBA, 0xDE, 0x0F, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C5206 - 48 8B 8E 90000000     - mov rcx,[rsi+00000090]
            0x48, 0x8B, 0x8E, 0x90, 0x00, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C520D - E8 5E95F5FF           - call OblivionRemastered-Win64-Shipping.exe+671E770
            0xE8, -1, -1, -1, -1,
            // OblivionRemastered-Win64-Shipping.exe+67C5212 - 40 B7 01              - mov dil,01 { 1 }
            0x40, 0xB7, 0x01,
            // OblivionRemastered-Win64-Shipping.exe+67C5215 - F3 0F10 15 8FBDF701   - movss xmm2,[OblivionRemastered-Win64-Shipping.exe+8740FAC] { (1.00) }
            0xF3, 0x0F, 0x10, 0x15, -1, -1, -1, -1,
            // OblivionRemastered-Win64-Shipping.exe+67C521D - BA A10F0000           - mov edx,00000FA1 { 4001 }
            0xBA, 0xA1, 0x0F, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C5222 - 48 8B 4E 70           - mov rcx,[rsi+70]
            0x48, 0x8B, 0x4E, 0x70,
            // OblivionRemastered-Win64-Shipping.exe+67C5226 - E8 D594F5FF           - call OblivionRemastered-Win64-Shipping.exe+671E700
            0xE8, -1, -1, -1, -1,
            // OblivionRemastered-Win64-Shipping.exe+67C522B - F3 0F10 15 C5BDF701   - movss xmm2,[OblivionRemastered-Win64-Shipping.exe+8740FF8] { (2.00) }
            0xF3, 0x0F, 0x10, 0x15, -1, -1, -1, -1,
            // OblivionRemastered-Win64-Shipping.exe+67C5233 - BA A10F0000           - mov edx,00000FA1 { 4001 }
            0xBA, 0xA1, 0x0F, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C5238 - 48 8B 8E 90000000     - mov rcx,[rsi+00000090]
            0x48, 0x8B, 0x8E, 0x90, 0x00, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C523F - E8 BC94F5FF           - call OblivionRemastered-Win64-Shipping.exe+671E700
            0xE8, -1, -1, -1, -1,
            // OblivionRemastered-Win64-Shipping.exe+67C5244 - 48 8D 8C 24 50010000  - lea rcx,[rsp+00000150]
            0x48, 0x8D, 0x8C, 0x24, 0x50, 0x01, 0x00, 0x00,
        };

        short sigTrainingLimitUI1[]
        {
            // OblivionRemastered-Win64-Shipping.exe+67C4F62 - 8B 05 90288202        - mov eax,[OblivionRemastered-Win64-Shipping.exe+8FE77F8] { (5) }
            0x8B, 0x05, -1, -1, -1, -1,
            // OblivionRemastered-Win64-Shipping.exe+67C4F68 - 89 85 54010000        - mov [rbp+00000154],eax
            0x89, 0x85, 0x54, 0x01, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C4F6E - 41 8B 86 B8000000     - mov eax,[r14+000000B8]
            0x41, 0x8B, 0x86, 0xB8, 0x00, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C4F75 - 89 85 58010000        - mov [rbp+00000158],eax
            0x89, 0x85, 0x58, 0x01, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C4F7B - E8 7054E1FF           - call OblivionRemastered-Win64-Shipping.exe+65DA3F0
            0xE8, -1, -1, -1, -1,
            // OblivionRemastered-Win64-Shipping.exe+67C4F80 - 89 85 5C010000        - mov [rbp+0000015C],eax
            0x89, 0x85, 0x5C, 0x01, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C4F86 - 49 8B 8E A8000000     - mov rcx,[r14+000000A8]
            0x49, 0x8B, 0x8E, 0xA8, 0x00, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C4F8D - E8 5E54E1FF           - call OblivionRemastered-Win64-Shipping.exe+65DA3F0
            0xE8, -1, -1, -1, -1,
            // OblivionRemastered-Win64-Shipping.exe+67C4F92 - 89 85 60010000        - mov [rbp+00000160],eax
            0x89, 0x85, 0x60, 0x01, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C4F98 - 40 80 F6 01           - xor sil,01 { 1 }
            0x40, 0x80, 0xF6, 0x01,
            // OblivionRemastered-Win64-Shipping.exe+67C4F9C - 40 88 B5 64010000     - mov [rbp+00000164],sil
            0x40, 0x88, 0xB5, 0x64, 0x01, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C4FA3 - 48 8D 44 24 38        - lea rax,[rsp+38]
            0x48, 0x8D, 0x44, 0x24, 0x38,
            // OblivionRemastered-Win64-Shipping.exe+67C4FA8 - 48 89 44 24 30        - mov [rsp+30],rax
            0x48, 0x89, 0x44, 0x24, 0x30,
            // OblivionRemastered-Win64-Shipping.exe+67C4FAD - 48 8D 55 50           - lea rdx,[rbp+50]
            0x48, 0x8D, 0x55, 0x50,
            // OblivionRemastered-Win64-Shipping.exe+67C4FB1 - 48 8D 4C 24 38        - lea rcx,[rsp+38]
            0x48, 0x8D, 0x4C, 0x24, 0x38,
        };

        short sigTrainingLimitUI2[]
        {
            // OblivionRemastered-Win64-Shipping.exe+67C5425 - 8B 05 CD238202        - mov eax,[OblivionRemastered-Win64-Shipping.exe+8FE77F8] { (5) }
            0x8B, 0x05, -1, -1, -1, -1,
            // OblivionRemastered-Win64-Shipping.exe+67C542B - 89 84 24 54020000     - mov [rsp+00000254],eax
            0x89, 0x84, 0x24, 0x54, 0x02, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C5432 - 8B 86 B8000000        - mov eax,[rsi+000000B8]
            0x8B, 0x86, 0xB8, 0x00, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C5438 - 89 84 24 58020000     - mov [rsp+00000258],eax
            0x89, 0x84, 0x24, 0x58, 0x02, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C543F - E8 AC4FE1FF           - call OblivionRemastered-Win64-Shipping.exe+65DA3F0
            0xE8, -1, -1, -1, -1,
            // OblivionRemastered-Win64-Shipping.exe+67C5444 - 89 84 24 5C020000     - mov [rsp+0000025C],eax
            0x89, 0x84, 0x24, 0x5C, 0x02, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C544B - 48 8B 8E A8000000     - mov rcx,[rsi+000000A8]
            0x48, 0x8B, 0x8E, 0xA8, 0x00, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C5452 - E8 994FE1FF           - call OblivionRemastered-Win64-Shipping.exe+65DA3F0
            0xE8, -1, -1, -1, -1,
            // OblivionRemastered-Win64-Shipping.exe+67C5457 - 89 84 24 60020000     - mov [rsp+00000260],eax
            0x89, 0x84, 0x24, 0x60, 0x02, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C545E - 40 80 F7 01           - xor dil,01 { 1 }
            0x40, 0x80, 0xF7, 0x01,
            // OblivionRemastered-Win64-Shipping.exe+67C5462 - 40 88 BC 24 64020000  - mov [rsp+00000264],dil
            0x40, 0x88, 0xBC, 0x24, 0x64, 0x02, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C546A - 48 8D 44 24 38        - lea rax,[rsp+38]
            0x48, 0x8D, 0x44, 0x24, 0x38,
            // OblivionRemastered-Win64-Shipping.exe+67C546F - 48 89 44 24 30        - mov [rsp+30],rax
            0x48, 0x89, 0x44, 0x24, 0x30,
            // OblivionRemastered-Win64-Shipping.exe+67C5474 - 48 8D 94 24 50010000  - lea rdx,[rsp+00000150]
            0x48, 0x8D, 0x94, 0x24, 0x50, 0x01, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C547C - 48 8D 4C 24 38        - lea rcx,[rsp+38]
            0x48, 0x8D, 0x4C, 0x24, 0x38,
        };

        short sigTrainingCost1[]
        {
            // OblivionRemastered-Win64-Shipping.exe+67C4A33 - F3 0F59 05 FD2D8202   - mulss xmm0,[OblivionRemastered-Win64-Shipping.exe+8FE7838] { (10.00) }
            0xF3, 0x0F, 0x59, 0x05, -1, -1, -1, -1,
            // OblivionRemastered-Win64-Shipping.exe+67C4A3B - F3 0F2C C0            - cvttss2si eax,xmm0
            0xF3, 0x0F, 0x2C, 0xC0,
            // OblivionRemastered-Win64-Shipping.exe+67C4A3F - 41 89 86 B8000000     - mov [r14+000000B8],eax
            0x41, 0x89, 0x86, 0xB8, 0x00, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C4A46 - 48 8B 0D 3B09C902     - mov rcx,[OblivionRemastered-Win64-Shipping.exe+9455388] { (2C9EFF9C040) }
            0x48, 0x8B, 0x0D, -1, -1, -1, -1,
            // OblivionRemastered-Win64-Shipping.exe+67C4A4D - E8 9E59E1FF           - call OblivionRemastered-Win64-Shipping.exe+65DA3F0
            0xE8, -1, -1, -1, -1,
            // OblivionRemastered-Win64-Shipping.exe+67C4A52 - F3 0F10 35 52C5F701   - movss xmm6,[OblivionRemastered-Win64-Shipping.exe+8740FAC] { (1.00) }
            0xF3, 0x0F, 0x10, 0x35, -1, -1, -1, -1,
            // OblivionRemastered-Win64-Shipping.exe+67C4A5A - 41 39 86 B8000000     - cmp [r14+000000B8],eax
            0x41, 0x39, 0x86, 0xB8, 0x00, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C4A61 - 7E 11                 - jle OblivionRemastered-Win64-Shipping.exe+67C4A74
            0x7E, 0x11,
            // OblivionRemastered-Win64-Shipping.exe+67C4A63 - 0F28 D6               - movaps xmm2,xmm6
            0x0F, 0x28, 0xD6,
            // OblivionRemastered-Win64-Shipping.exe+67C4A66 - BA AF0F0000           - mov edx,00000FAF { 4015 }
            0xBA, 0xAF, 0x0F, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C4A6B - 49 8B 4E 70           - mov rcx,[r14+70]
            0x49, 0x8B, 0x4E, 0x70,
            // OblivionRemastered-Win64-Shipping.exe+67C4A6F - E8 8C9CF5FF           - call OblivionRemastered-Win64-Shipping.exe+671E700
            0xE8, -1, -1, -1, -1,
            // OblivionRemastered-Win64-Shipping.exe+67C4A74 - 41 8B 8E BC000000     - mov ecx,[r14+000000BC]
            0x41, 0x8B, 0x8E, 0xBC, 0x00, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C4A7B - E8 C0140E00           - call OblivionRemastered-Win64-Shipping.exe+68A5F40
            0xE8, -1, -1, -1, -1,
            // OblivionRemastered-Win64-Shipping.exe+67C4A80 - 8B C8                 - mov ecx,eax
            0x8B, 0xC8,
            // OblivionRemastered-Win64-Shipping.exe+67C4A82 - E8 39150E00           - call OblivionRemastered-Win64-Shipping.exe+68A5FC0
            0xE8, -1, -1, -1, -1,
            // OblivionRemastered-Win64-Shipping.exe+67C4A87 - 48 8B F0              - mov rsi,rax
            0x48, 0x8B, 0xF0,
        };

        short sigTrainingCost2[]
        {
            // OblivionRemastered-Win64-Shipping.exe+67C51A7 - F3 0F59 05 89268202   - mulss xmm0,[OblivionRemastered-Win64-Shipping.exe+8FE7838] { (10.00) }
            0xF3, 0x0F, 0x59, 0x05, -1, -1, -1, -1,
            // OblivionRemastered-Win64-Shipping.exe+67C51AF - F3 0F2C C0            - cvttss2si eax,xmm0
            0xF3, 0x0F, 0x2C, 0xC0,
            // OblivionRemastered-Win64-Shipping.exe+67C51B3 - 89 86 B8000000        - mov [rsi+000000B8],eax
            0x89, 0x86, 0xB8, 0x00, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C51B9 - 48 8B 0D C801C902     - mov rcx,[OblivionRemastered-Win64-Shipping.exe+9455388] { (2C9EFF9C040) }
            0x48, 0x8B, 0x0D, -1, -1, -1, -1,
            // OblivionRemastered-Win64-Shipping.exe+67C51C0 - 4C 8B 01              - mov r8,[rcx]
            0x4C, 0x8B, 0x01,
            // OblivionRemastered-Win64-Shipping.exe+67C51C3 - 48 8B 86 B0000000     - mov rax,[rsi+000000B0]
            0x48, 0x8B, 0x86, 0xB0, 0x00, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C51CA - 8B 50 58              - mov edx,[rax+58]
            0x8B, 0x50, 0x58,
            // OblivionRemastered-Win64-Shipping.exe+67C51CD - 41 FF 90 78050000     - call qword ptr [r8+00000578]
            0x41, 0xFF, 0x90, 0x78, 0x05, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C51D4 - 3B 86 BC000000        - cmp eax,[rsi+000000BC]
            0x3B, 0x86, 0xBC, 0x00, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C51DA - 7C 09                 - jl OblivionRemastered-Win64-Shipping.exe+67C51E5
            0x7C, 0x09,
            // OblivionRemastered-Win64-Shipping.exe+67C51DC - 4C 8B 05 6D9C8102     - mov r8,[OblivionRemastered-Win64-Shipping.exe+8FDEE50] { (2C9F183FF53) }
            0x4C, 0x8B, 0x05, -1, -1, -1, -1,
            // OblivionRemastered-Win64-Shipping.exe+67C51E3 - EB 1C                 - jmp OblivionRemastered-Win64-Shipping.exe+67C5201
            0xEB, 0x1C,
            // OblivionRemastered-Win64-Shipping.exe+67C51E5 - 48 8B 0D 9C01C902     - mov rcx,[OblivionRemastered-Win64-Shipping.exe+9455388] { (2C9EFF9C040) }
            0x48, 0x8B, 0x0D, -1, -1, -1, -1,
            // OblivionRemastered-Win64-Shipping.exe+67C51EC - 8B 05 06268202        - mov eax,[OblivionRemastered-Win64-Shipping.exe+8FE77F8] { (5) }
            //0x8B, 0x05,
            // OblivionRemastered-Win64-Shipping.exe+67C51F2 - 39 81 34080000        - cmp [rcx+00000834],eax
            //0x39, 0x81, 0x34, 0x08, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C51F8 - 7C 4A                 - jl OblivionRemastered-Win64-Shipping.exe+67C5244
            //0x7C, 0x4A,
            // OblivionRemastered-Win64-Shipping.exe+67C51FA - 4C 8B 05 3F9C8102     - mov r8,[OblivionRemastered-Win64-Shipping.exe+8FDEE40] { (2C9DA3CE1D4) }
            //0x4C, 0x8B, 0x05, -1, -1, -1, -1,
            // OblivionRemastered-Win64-Shipping.exe+67C5201 - BA DE0F0000           - mov edx,00000FDE { 4062 }
            //0xBA, 0xDE, 0x0F, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C5206 - 48 8B 8E 90000000     - mov rcx,[rsi+00000090]
            //0x48, 0x8B, 0x8E, 0x90, 0x00, 0x00, 0x00,
            // OblivionRemastered-Win64-Shipping.exe+67C520D - E8 5E95F5FF           - call OblivionRemastered-Win64-Shipping.exe+671E770
            //0xE8, -1, -1, -1, -1,
            // OblivionRemastered-Win64-Shipping.exe+67C5212 - 40 B7 01              - mov dil,01 { 1 }
            //0x40, 0xB7, 0x01,
        };

        intptr_t offsetTrainingLimit1 = FindSignature(buffer, size, sigTrainingLimit1, sizeof(sigTrainingLimit1) / 2);
        intptr_t offsetTrainingLimit2 = FindSignature(buffer, size, sigTrainingLimit2, sizeof(sigTrainingLimit2) / 2);
        intptr_t offsetTrainingLimitUI1 = FindSignature(buffer, size, sigTrainingLimitUI1, sizeof(sigTrainingLimitUI1) / 2);
        intptr_t offsetTrainingLimitUI2 = FindSignature(buffer, size, sigTrainingLimitUI2, sizeof(sigTrainingLimitUI2) / 2);
        intptr_t offsetTrainingCost1 = FindSignature(buffer, size, sigTrainingCost1, sizeof(sigTrainingCost1) / 2);
        intptr_t offsetTrainingCost2 = FindSignature(buffer, size, sigTrainingCost2, sizeof(sigTrainingCost2) / 2);

        if (offsetTrainingLimit1 >= 0 && offsetTrainingLimit2 >= 0 &&
            offsetTrainingLimitUI1 >= 0 && offsetTrainingLimitUI2 >= 0 &&
            offsetTrainingCost1 >= 0 && offsetTrainingCost2 >= 0)
        {
            gPlayer = reinterpret_cast<PlayerCharacter**>(base + offsetTrainingCost1 + *reinterpret_cast<int*>(base + offsetTrainingCost1 + 22) + 26);
            gTrainingPerLevel = reinterpret_cast<int*>(base + offsetTrainingLimit1 + *reinterpret_cast<int*>(base + offsetTrainingLimit1 + 2) + 6);
            gTrainingCostMult = reinterpret_cast<float*>(base + offsetTrainingCost1 + *reinterpret_cast<int*>(base + offsetTrainingCost1 + 4) + 8);

            BYTE nop[]
            {
                0x90, 0x90, 0x90,
            };

            if (gUnlimitedTraining)
            {
                BYTE patch[]
                {
                    0xB8, 0xFF, 0xFF, 0xFF, 0x7F,   // mov eax, 0x7FFFFFFF { 2147483647 }
                    0x90,                           // nop
                };

                SafeWrite(base + offsetTrainingLimit1, patch, sizeof(patch));
                SafeWrite(base + offsetTrainingLimit2, patch, sizeof(patch));
            }
            else
            {
                gTrampoline.write5Call((uintptr_t)base + offsetTrainingLimit1, (uintptr_t)GetMaxTrainingSessionsInitial);
                SafeWrite(base + offsetTrainingLimit1 + 5, nop, 1);
                gTrampoline.write5Call((uintptr_t)base + offsetTrainingLimit2, (uintptr_t)GetMaxTrainingSessionsUpdate);
                SafeWrite(base + offsetTrainingLimit2 + 5, nop, 1);
            }

            gTrampoline.write5Call((uintptr_t)base + offsetTrainingLimitUI1, (uintptr_t)GetMaxTrainingSessionsInitial);
            SafeWrite(base + offsetTrainingLimitUI1 + 5, nop, 1);
            gTrampoline.write5Call((uintptr_t)base + offsetTrainingLimitUI2, (uintptr_t)GetMaxTrainingSessionsUpdate);
            SafeWrite(base + offsetTrainingLimitUI2 + 5, nop, 1);

            if (gUnlimitedTraining && gOverCapTrainingCostMult > 1.0f)
            {
                BYTE patch[]
                {
                    0x0F, 0x28, 0xC8,   // movaps xmm1,xmm0
                };

                gTrampoline.write5Call((uintptr_t)base + offsetTrainingCost1, (uintptr_t)GetTrainingCost);
                SafeWrite(base + offsetTrainingCost1 + 5, nop, 3);
                gTrampoline.write5Call((uintptr_t)base + offsetTrainingCost2, (uintptr_t)GetTrainingCost);
                SafeWrite(base + offsetTrainingCost2 + 5, nop, 3);
            }

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
    constexpr const char* filePath = "OBSE\\Plugins\\NoTrainingLimit.ini";
    gUnlimitedTraining = GetPrivateProfileIntA("Settings", "bUnlimitedTraining", 1, filePath);
    char floatBuffer[16]{};
    if (GetPrivateProfileStringA("Settings", "fOverCapTrainingCostMult", nullptr, floatBuffer, 16, filePath) > 0)
        gOverCapTrainingCostMult = atof(floatBuffer);

    if (gOverCapTrainingCostMult <= 0.0f)
        gOverCapTrainingCostMult = 1.0;

    OBSETrampolineInterface* trampolineInterface = static_cast<OBSETrampolineInterface*>(obse->QueryInterface(kInterface_Trampoline));
    if (!trampolineInterface)
        return false;

    constexpr const size_t trampolineSize = 128;
    gTrampoline.setBase(trampolineSize, trampolineInterface->AllocateFromBranchPool(obse->GetPluginHandle(), trampolineSize));

    return ApplyPatch();
}
