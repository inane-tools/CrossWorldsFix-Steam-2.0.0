#include "stdafx.h"
#include "version.h"
#include "helper.hpp"

#ifdef _DEBUG
    #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#else
    #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
#endif

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <inipp/inipp.h>
#include <safetyhook.hpp>

#include "SDK/Basic.hpp"
#include "SDK/Engine_classes.hpp"
#include "SDK/UMG_classes.hpp"

#include "SDK/WBP_Race_HUD_Player01_classes.hpp"
#include "SDK/WBP_PauseMenu_classes.hpp"
#include "SDK/WBP_Popup_Window_classes.hpp"

HMODULE exeModule = GetModuleHandle(NULL);
HMODULE thisModule;

// Ini
inipp::Ini<char> ini;
std::string sConfigFile = sFixName + ".ini";
std::filesystem::path FixPath;

// Logger
std::shared_ptr<spdlog::logger> logger;
std::string sLogFile = sFixName + ".log";
std::filesystem::path ExePath;
std::string sExeName;

// Aspect ratio / FOV / HUD
std::pair<int, int> DesktopDimensions = { 0, 0 };
constexpr float fPi = std::numbers::pi_v<float>;
constexpr float fNativeAspect = 16.0f / 9.0f;
float fAspectRatio = 16.0f / 9.0f;
float fAspectMultiplier = 1.0f;
float fHUDWidth = 1920.0f;
float fHUDWidthOffset = 0.0f;
float fHUDHeight = 1080.0f;
float fHUDHeightOffset = 0.0f;

// Ini settings
bool bEnableConsole = false;
bool bFixAspect = false;
bool bFixHUD = false;
bool bSpanRaceHUD = false;

// Variables
int iCurrentResX = 0;
int iCurrentResY = 0;
SDK::UEngine* Engine = nullptr;
SDK::UObject* WidgetObject = nullptr;
std::string sWidgetName;

template<typename T>
T GetConfigAndLog(inipp::Ini<char>& ini, const std::string& section, const std::string& key, T defaultValue)
{
    T value = defaultValue;
    inipp::get_value(ini.sections[section], key, value);
    SPDLOG_INFO("Config Parse: [{}]: {} = {}", section, key, value);
    return value;
}

void CalculateAspectRatio(bool bLog)
{
    if (iCurrentResX <= 0 || iCurrentResY <= 0)
        return;

    fAspectRatio = static_cast<float>(iCurrentResX) / iCurrentResY;
    fAspectMultiplier = fAspectRatio / fNativeAspect;

    fHUDWidth = static_cast<float>(iCurrentResY) * fNativeAspect;
    fHUDHeight = static_cast<float>(iCurrentResY);
    fHUDWidthOffset = (static_cast<float>(iCurrentResX) - fHUDWidth) / 2.0f;
    fHUDHeightOffset = 0.0f;

    if (fAspectRatio < fNativeAspect) {
        fHUDWidth = static_cast<float>(iCurrentResX);
        fHUDHeight = static_cast<float>(iCurrentResX) / fNativeAspect;
        fHUDWidthOffset = 0.0f;
        fHUDHeightOffset = (static_cast<float>(iCurrentResY) - fHUDHeight) / 2.0f;
    }

    if (bLog) {
        SPDLOG_INFO("----------");
        SPDLOG_INFO("Current Resolution: Resolution: {:d}x{:d}", iCurrentResX, iCurrentResY);
        SPDLOG_INFO("Current Resolution: fAspectRatio: {}", fAspectRatio);
        SPDLOG_INFO("Current Resolution: fAspectMultiplier: {}", fAspectMultiplier);
        SPDLOG_INFO("Current Resolution: fHUDWidth: {}", fHUDWidth);
        SPDLOG_INFO("Current Resolution: fHUDHeight: {}", fHUDHeight);
        SPDLOG_INFO("Current Resolution: fHUDWidthOffset: {}", fHUDWidthOffset);
        SPDLOG_INFO("Current Resolution: fHUDHeightOffset: {}", fHUDHeightOffset);
        SPDLOG_INFO("----------");
    }
}

void Logging()
{
    // Get path to DLL
    WCHAR dllPath[_MAX_PATH] = {0};
    GetModuleFileNameW(thisModule, dllPath, MAX_PATH);
    FixPath = dllPath;
    FixPath = FixPath.remove_filename();

    // Get game name and exe path
    WCHAR gameExePath[_MAX_PATH] = {0};
    GetModuleFileNameW(exeModule, gameExePath, MAX_PATH);
    ExePath = gameExePath;
    sExeName = ExePath.filename().string();
    ExePath = ExePath.remove_filename();

    // spdlog initialisation
    try
    {
        std::ofstream file(ExePath / sLogFile, std::ios::trunc);
        if (file.is_open()) file.close();

        logger = std::make_shared<spdlog::logger>(sFixName, std::make_shared<spdlog::sinks::rotating_file_sink_st>((ExePath / sLogFile).string(), 10 * 1024 * 1024, 1));
        spdlog::set_default_logger(logger);
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
        spdlog::flush_on(spdlog::level::debug);
        spdlog::set_level(spdlog::level::debug); 
        
        SPDLOG_INFO("----------");
        SPDLOG_INFO("{:s} v{:s}", sFixName, sFixVersion);
        SPDLOG_INFO("----------");
        SPDLOG_INFO("Log File: {}", (FixPath / sLogFile).string());
        SPDLOG_INFO("----------");
        SPDLOG_INFO("Module Name: {:s}", sExeName);
        SPDLOG_INFO("Module Path: {}", ExePath.string());
        SPDLOG_INFO("Module Address: 0x{:x}", reinterpret_cast<uintptr_t>(exeModule));
        SPDLOG_INFO("Module Timestamp: {:d}", Memory::GetModuleTimestamp(exeModule));
        SPDLOG_INFO("----------");
    }
    catch (const spdlog::spdlog_ex &ex) {
        Util::ConsoleExit(std::string("Log initialisation failed: ") + ex.what(), thisModule);
    }
}

void Configuration()
{
    // Inipp initialisation
    std::ifstream iniFile(FixPath / sConfigFile);
    if (!iniFile) {
        spdlog::shutdown(); // Flush log
        Util::ConsoleExit("Could not locate config file.\nMake sure " + sConfigFile + " is located in " + FixPath.string() + sConfigFile, thisModule);
    }
    
    SPDLOG_INFO("Config File: {}", (FixPath / sConfigFile).string());
    ini.parse(iniFile);
    ini.strip_trailing_comments();
    
    // Read ini
    bEnableConsole = GetConfigAndLog(ini, "Developer Console", "Enabled", false);
    bFixAspect = GetConfigAndLog(ini, "Fix Aspect Ratio", "Enabled", false);
    bFixHUD = GetConfigAndLog(ini, "Fix HUD", "Enabled", false);
    bSpanRaceHUD = GetConfigAndLog(ini, "Fix HUD", "SpanRaceHUD", false);
    
    SPDLOG_INFO("----------");
}

void UpdateOffsets()
{
    // GObjects
    std::uint8_t* GObjectsScanResult = Memory::PatternScan(exeModule, "48 8B ?? ?? ?? ?? ?? 48 8B ?? ?? 48 8D ?? ?? EB ?? 33 ?? 8B ?? ?? C1 ??");
    if (GObjectsScanResult) {
        SPDLOG_INFO("Offsets: GObjects: Address is {:s}+{:x}", sExeName, GObjectsScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
        std::uint8_t* GObjectsAddr = Memory::GetRelativeAddr(GObjectsScanResult + 0x3);
        SDK::Offsets::GObjects = static_cast<UC::uint32>(GObjectsAddr - reinterpret_cast<std::uint8_t*>(exeModule));
        SPDLOG_INFO("Offsets: GObjects: 0x{:x}", SDK::Offsets::GObjects);
    }
    else {
        SPDLOG_ERROR("Offsets: GObjects: Pattern scan failed.");
    }

    // AppendString
    std::uint8_t* AppendStringScanResult = Memory::PatternScan(exeModule, "48 89 ?? ?? ?? 48 89 ?? ?? ?? ?? 48 83 ?? ?? 80 3D ?? ?? ?? ?? 00 48 8B F2 8B ?? 48 8B ??");
    if (AppendStringScanResult) {
        SPDLOG_INFO("Offsets: AppendString: Address is {:s}+{:x}", sExeName, AppendStringScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
        SDK::Offsets::AppendString = static_cast<UC::uint32>(AppendStringScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
        SPDLOG_INFO("Offsets: AppendString: 0x{:x}", SDK::Offsets::AppendString);
    }
    else {
        SPDLOG_ERROR("Offsets: AppendString: Pattern scan failed.");
    }

    // ProcessEvent
    std::uint8_t* ProcessEventScanResult = Memory::PatternScan(exeModule, "40 ?? 56 57 41 ?? 41 ?? 41 ?? 41 ?? 48 81 ?? ?? ?? ?? ?? 48 8D ?? ?? ?? 48 89 ?? ?? ?? ?? ?? 48 8B ?? ?? ?? ?? ?? 48 33 ?? 48 89 ?? ?? ?? ?? ?? 4D 8B ??");
    if (ProcessEventScanResult) {
        SPDLOG_INFO("Offsets: ProcessEvent: Address is {:s}+{:x}", sExeName, ProcessEventScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
        SDK::Offsets::ProcessEvent = static_cast<UC::uint32>(ProcessEventScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
        SPDLOG_INFO("Offsets: ProcessEvent: 0x{:x}", SDK::Offsets::ProcessEvent);
    }
    else {
        SPDLOG_ERROR("Offsets: ProcessEvent: Pattern scan failed.");
    }

    SPDLOG_INFO("----------");
}

void CurrentResolution() 
{
    // Get current resolution
    std::uint8_t* CurrentResolutionScanResult = Memory::PatternScan(exeModule, "89 ?? ?? ?? 48 85 ?? 0F 84 ?? ?? ?? ?? 48 8B ?? ?? ?? ?? ?? 48 85 ?? 4C 8D ?? ??");
    if (CurrentResolutionScanResult) {
        SPDLOG_INFO("Current Resolution: Address is {:s}+{:x}", sExeName.c_str(), CurrentResolutionScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
        static SafetyHookMid CurrentResolutionMidHook{};
        CurrentResolutionMidHook = safetyhook::create_mid(CurrentResolutionScanResult,
            [](SafetyHookContext& ctx) {
                if (!ctx.rdx) return;

                int ResX = *reinterpret_cast<int*>(ctx.rdx + 0xA0);
                int ResY = *reinterpret_cast<int*>(ctx.rdx + 0xA4);
  
                if (iCurrentResX != ResX || iCurrentResY != ResY) {
                    iCurrentResX = ResX;
                    iCurrentResY = ResY;
                    CalculateAspectRatio(true);
                }
            });
    }
    else {
        SPDLOG_ERROR("Current Resolution: Pattern scan failed.");
    }
}

void AspectRatioFOV()
{
    if (bFixAspect) 
    {
        // Aspect Ratio / FOV
        std::uint8_t* AspectRatioFOVScanResult = Memory::PatternScan(exeModule, "F3 0F ?? ?? ?? 8B ?? ?? ?? ?? ?? 89 ?? ?? 0F ?? ?? ?? ?? ?? ?? 33 ?? ?? 83 ?? 01");
        if (AspectRatioFOVScanResult) {
            SPDLOG_INFO("Aspect Ratio/FOV: Address is {:s}+{:x}", sExeName.c_str(), AspectRatioFOVScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
            static SafetyHookMid FOVMidHook{};
            FOVMidHook = safetyhook::create_mid(AspectRatioFOVScanResult,
                [](SafetyHookContext& ctx) {
                    if (fAspectRatio > fNativeAspect)
                        ctx.xmm0.f32[0] = atanf(tanf(ctx.xmm0.f32[0] * (fPi / 360)) / fNativeAspect * fAspectRatio) * (360 / fPi);
                });

            static SafetyHookMid AspectRatioMidHook{};
            AspectRatioMidHook = safetyhook::create_mid(AspectRatioFOVScanResult + 0xB,
                [](SafetyHookContext& ctx) {
                    ctx.rax = static_cast<uintptr_t>(std::bit_cast<uint32_t>(fAspectRatio));
                });
        }
        else {
            SPDLOG_ERROR("Aspect Ratio/FOV: Pattern scan failed.");
        } 
    }  
}

void HUD()
{
    if (bFixHUD) 
    {
        // Fix racing position markers
        std::uint8_t* ProjectWorldToScreenScanResult = Memory::PatternScan(exeModule, "48 ?? ?? 20 F2 0F ?? ?? 66 0F ?? ?? F3 0F ?? ?? F2 0F ?? ?? F2 0F ?? ?? ?? F2 0F ?? ?? F2 0F ?? ?? ?? 84 ??");
        if (ProjectWorldToScreenScanResult) {
            SPDLOG_INFO("HUD: Project World To Screen: Address is {:s}+{:x}", sExeName.c_str(), ProjectWorldToScreenScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
            static SafetyHookMid ProjectWorldToScreenWidthMidHook{};
            ProjectWorldToScreenWidthMidHook = safetyhook::create_mid(ProjectWorldToScreenScanResult,
                [](SafetyHookContext& ctx) {
                    if (fAspectRatio > fNativeAspect)
                        ctx.xmm0.f64[0] = fHUDWidthOffset;
                });

            static SafetyHookMid ProjectWorldToScreenHeightMidHook{};
            ProjectWorldToScreenHeightMidHook = safetyhook::create_mid(ProjectWorldToScreenScanResult + 0x10,
                [](SafetyHookContext& ctx) {
                    if (fAspectRatio < fNativeAspect)
                        ctx.xmm0.f64[0] = fHUDHeightOffset;
                });
        }
        else {
            SPDLOG_ERROR("HUD: Project World To Screen: Pattern scan failed.");
        }

        // Popup Window
        std::uint8_t* CreatePopupWindowScanResult = Memory::PatternScan(exeModule, "49 8B ?? ?? 48 89 ?? ?? 41 ?? ?? ?? 00 7E ?? 49 ?? ?? ?? 41 0F ?? ?? E8 ?? ?? ?? ?? 48 8B ?? ?? 48 85 ?? 74 ??");
        if (CreatePopupWindowScanResult) {
            SPDLOG_INFO("HUD: Create Popup Window: Address is {:s}+{:x}", sExeName.c_str(), CreatePopupWindowScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
            static SafetyHookMid CreatePopupWindowMidHook{};
            CreatePopupWindowMidHook = safetyhook::create_mid(CreatePopupWindowScanResult,
                [](SafetyHookContext& ctx) {
                    if (!ctx.rsi) return;

                    auto widget = reinterpret_cast<SDK::UUserWidget*>(ctx.rsi);

                    if (widget->IsA(SDK::UWBP_Popup_Window_C::StaticClass())) {
                        auto popupWidget = static_cast<SDK::UWBP_Popup_Window_C*>(widget);
                        auto panel = static_cast<SDK::UCanvasPanel*>(popupWidget->WidgetTree->RootWidget);      

                        if (panel->Slots.IsValidIndex(2)) {
                            auto backgroundBlurSlot = static_cast<SDK::UCanvasPanelSlot*>(panel->Slots[2]);

                            float widthOffset = (2160.0f * fAspectRatio - 3840.0f) / 2.0f;
                            float heightOffset = (3840.0f / fAspectRatio - 2160.0f) / 2.0f;

                            if (fAspectRatio > fNativeAspect)
                                backgroundBlurSlot->SetOffsets(SDK::FMargin(-1920.0f - widthOffset, -1080.0f, 2160.0f * fAspectRatio, 2160.0f));
                            else if (fAspectRatio < fNativeAspect)
                                backgroundBlurSlot->SetOffsets(SDK::FMargin(-1920.0f, -1080.0f - heightOffset, 3840.0f, 3840.0f / fAspectRatio));
                        }     
                    }
                });
        }
        else {
            SPDLOG_ERROR("HUD: Create Popup Window: Pattern scan failed.");
        }

        // Widgets
        std::uint8_t* UWidgetAddToViewportScanResult = Memory::PatternScan(exeModule, "48 8B ?? ?? 48 8B ?? 48 85 ?? 40 0F ?? ?? 48 ?? ?? 48 89 ?? ?? 48 8B ?? 8B ?? ?? ?? ?? ?? ?? FF 90 ?? ?? ?? ??");
        if (UWidgetAddToViewportScanResult) {
            SPDLOG_INFO("HUD: Widgets: Address is {:s}+{:x}", sExeName.c_str(), UWidgetAddToViewportScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
            static SafetyHookMid UWidgetAddToViewportMidHook{};
            UWidgetAddToViewportMidHook = safetyhook::create_mid(UWidgetAddToViewportScanResult,
                [](SafetyHookContext& ctx) {
                    if (!ctx.rsi) return;

                    auto obj = reinterpret_cast<SDK::UObject*>(ctx.rsi);

                    if (!obj->Class || !obj->Class->Class) 
                        return;

                    if (WidgetObject != obj) {
                        WidgetObject = obj;
                        sWidgetName = WidgetObject->GetName();
                        SPDLOG_INFO("HUD: Widgets: {} @ 0x{:x}", sWidgetName, reinterpret_cast<uintptr_t>(WidgetObject));
                    }
                    
                    float widthOffset = (2160.0f * fAspectRatio - 3840.0f) / 2.0f;
                    float heightOffset = (3840.0f / fAspectRatio - 2160.0f) / 2.0f;

                    if (sWidgetName.contains("WBP_Race_HUD_Player01_C")) {
                        auto hud = static_cast<SDK::UWBP_Race_HUD_Player01_C*>(WidgetObject);
                        if (hud->WidgetTree->RootWidget->IsA(SDK::UScaleBox::StaticClass())) {
                            auto scaleBox = static_cast<SDK::UScaleBox*>(hud->WidgetTree->RootWidget);
                            auto sizeBox = static_cast<SDK::USizeBox*>(scaleBox->Slots[0]->Content);

                            // Disable clipping so that player markers don't disappear outside 16:9 bounds
                            sizeBox->SetClipping(SDK::EWidgetClipping::Inherit);

                            // Span background of pause menu
                            if (hud->WBP_PauseMenu) {
                                auto pauseMenu = static_cast<SDK::UWBP_PauseMenu_C*>(hud->WBP_PauseMenu);
                                auto scaleBox = static_cast<SDK::UScaleBox*>(pauseMenu->WidgetTree->RootWidget);

                                if (scaleBox->Slots[0] && scaleBox->Slots[0]->Content) {
                                    auto panel = static_cast<SDK::UCanvasPanel*>(scaleBox->Slots[0]->Content);
                                    panel->SetClipping(SDK::EWidgetClipping::Inherit);

                                    if (panel->Slots.IsValidIndex(3)) {
                                        auto backgroundBlurSlot = static_cast<SDK::UCanvasPanelSlot*>(panel->Slots[2]);
                                        auto bgBlackSlot = static_cast<SDK::UCanvasPanelSlot*>(panel->Slots[3]);

                                        if (fAspectRatio > fNativeAspect) {
                                            backgroundBlurSlot->SetOffsets(SDK::FMargin(-widthOffset, 0.0f, -widthOffset, 0.0f));
                                            bgBlackSlot->SetOffsets(SDK::FMargin(-widthOffset, 0.0f, -widthOffset, 0.0f));
                                        }
                                        else if (fAspectRatio < fNativeAspect) {
                                            backgroundBlurSlot->SetOffsets(SDK::FMargin(0.0f, -heightOffset, 0.0f, -heightOffset));
                                            bgBlackSlot->SetOffsets(SDK::FMargin(0.0f, -heightOffset, 0.0f, -heightOffset));
                                        }
                                    }  
                                }
                            }

                            // Span racing HUD
                            if (bSpanRaceHUD) {
                                auto speed = (SDK::UUserWidget*)hud->WBP_Race_HUD_Sub_SpeedNum;
                                auto speedSlot = static_cast<SDK::UCanvasPanelSlot*>(speed->Slot);
                                //(Left=40.000000,Top=0.000000,Right=100.000000,Bottom=30.000000)

                                auto rank = (SDK::UUserWidget*)hud->WBP_Race_HUD_Sub_Rank;
                                auto rankSlot = static_cast<SDK::UCanvasPanelSlot*>(rank->Slot);
                                //(Left=-20.000000,Top=40.000000,Right=100.000000,Bottom=30.000000)

                                auto rsr_5rings = (SDK::UUserWidget*)hud->WBP_Race_HUD_Sub_RSR_5rings_Simple;
                                auto rsr_5ringsSlot = static_cast<SDK::UCanvasPanelSlot*>(rsr_5rings->Slot);
                                //(Left=60.000000,Top=-20.000000,Right=100.000000,Bottom=30.000000)

                                auto courseMap = (SDK::UUserWidget*)hud->WBP_Race_HUD_CourseMap;
                                auto courseMapSlot = static_cast<SDK::UCanvasPanelSlot*>(courseMap->Slot);
                                //(Left=200.000000,Top=-20.000000,Right=1024.000000,Bottom=1024.000000)

                                auto laps = (SDK::UUserWidget*)hud->WBP_Race_HUD_Sub_LAP;
                                auto lapsSlot = static_cast<SDK::UCanvasPanelSlot*>(laps->Slot);
                                //(Left=400.000000,Top=0.000000,Right=330.000000,Bottom=120.000000)

                                auto gadgetIcons = (SDK::UUserWidget*)hud->WBP_Race_HUD_Sub_GadgetIcon_Simple;
                                auto gadgetIconsSlot = static_cast<SDK::UCanvasPanelSlot*>(gadgetIcons->Slot);
                                //(Left=-50.000000,Top=180.000000,Right=100.000000,Bottom=30.000000)

                                auto itemIcons = (SDK::UUserWidget*)hud->WBP_Race_HUD_Sub_ItemIconSet_Simple;
                                auto itemIconsSlot = static_cast<SDK::UCanvasPanelSlot*>(itemIcons->Slot);
                                //(Left=-50.000000,Top=-100.000000,Right=100.000000,Bottom=30.000000)
                                
                                auto coins = (SDK::UUserWidget*)hud->WBP_Race_HUD_Sub_Coin;
                                auto coinsSlot = static_cast<SDK::UCanvasPanelSlot*>(coins->Slot);
                                //(Left=48.000000,Top=-304.000000,Right=468.000000,Bottom=160.000000)

                                if (fAspectRatio > fNativeAspect) {
                                    // Left
                                    lapsSlot->SetOffsets(SDK::FMargin(400.0f - widthOffset, 0.0f, 330.0f, 120.0f));
                                    rankSlot->SetOffsets(SDK::FMargin(-20.0f - widthOffset, 40.0f, 100.0f, 30.0f));
                                    gadgetIconsSlot->SetOffsets(SDK::FMargin(-50.0f - widthOffset, 180.0f, 100.0f, 30.0f));
                                    itemIconsSlot->SetOffsets(SDK::FMargin(-50.0f - widthOffset, -100.0f, 100.0f, 30.0f));

                                    // Right
                                    speedSlot->SetOffsets(SDK::FMargin(40.0f + widthOffset, 0.0f, 100.0f, 30.0f));
                                    rsr_5ringsSlot->SetOffsets(SDK::FMargin(60.0f + widthOffset, -20.0f, 100.0f, 30.0f));
                                    courseMapSlot->SetOffsets(SDK::FMargin(200.0f + widthOffset, -20.0f, 1024.0f, 1024.0f));
                                    coinsSlot->SetOffsets(SDK::FMargin(48.0f + widthOffset, -304.0f, 468.0f, 160.0f));
                                }
                            }
                        }
                    }
                });
        }
        else {
            SPDLOG_ERROR("HUD: Widgets: Pattern scan failed.");
        }
    }
}

void EnableConsole()
{
    if (bEnableConsole) 
    {
        // Get GEngine
        for (int i = 0; i < 200; ++i) { // 20s
            Engine = SDK::UEngine::GetEngine();

            if (Engine && Engine->ConsoleClass && Engine->GameViewport)
                break;

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (!Engine || !Engine->ConsoleClass || !Engine->GameViewport) {
            SPDLOG_ERROR("Enable Console: Failed to find GEngine address after 20 seconds.");
            return;
        }

        // Construct console
        SDK::UObject* NewObject = SDK::UGameplayStatics::SpawnObject(Engine->ConsoleClass, Engine->GameViewport);
        if (NewObject) {
            Engine->GameViewport->ViewportConsole = static_cast<SDK::UConsole*>(NewObject);
            SPDLOG_INFO("Enable Console: Console object constructed.");
        }
        else {
            SPDLOG_ERROR("Enable Console: Failed to construct console object.");
            return;
        }

        // Get input settings
        SDK::UInputSettings* InputSettings = SDK::UInputSettings::GetDefaultObj();
        if (InputSettings) {
            if (InputSettings->ConsoleKeys && InputSettings->ConsoleKeys.Num() > 0)
                SPDLOG_INFO("Enable Console: Console enabled - access it using the '{}' key.", InputSettings->ConsoleKeys[0].KeyName.ToString());
            else
                SPDLOG_ERROR("Enable Console: Console enabled but no console key is bound.");
        }
        else {
            SPDLOG_ERROR("Enable Console: Failed to retreive input settings.");
        }
    }
}

DWORD __stdcall Main(void*)
{
    Logging();
    Configuration();
    UpdateOffsets();
    CurrentResolution();
    AspectRatioFOV();
    HUD();
    EnableConsole();
    return true;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call) 
    {
    case DLL_PROCESS_ATTACH:
    {  
        // Detach from "crashpad_handler.exe"
        char exeName[MAX_PATH];
        GetModuleFileNameA(NULL, exeName, MAX_PATH);
        std::string exeStr(exeName);
        if (exeStr.find("crashpad_handler.exe") != std::string::npos)
            return FALSE;

        DisableThreadLibraryCalls(hModule);
        thisModule = hModule;

        HANDLE mainHandle = CreateThread(NULL, 0, Main, 0, NULL, 0);
        if (mainHandle)
        {
            SetThreadPriority(mainHandle, THREAD_PRIORITY_HIGHEST);
            CloseHandle(mainHandle);
        }
        break;
    }
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}