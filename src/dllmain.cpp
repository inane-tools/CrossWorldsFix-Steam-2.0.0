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
bool bFixAspect = false;
bool bFixHUD = false;
bool bSpanRaceHUD = false;
bool bSpanMenus = false;

// Variables
int iCurrentResX = 0;
int iCurrentResY = 0;

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
    bFixAspect = GetConfigAndLog(ini, "Fix Aspect Ratio", "Enabled", false);
    bFixHUD = GetConfigAndLog(ini, "Fix HUD", "Enabled", false);
    bSpanRaceHUD = GetConfigAndLog(ini, "Span Racing HUD", "Enabled", false);
    bSpanMenus = GetConfigAndLog(ini, "Span Menus", "Enabled", false);
    
    SPDLOG_INFO("----------");
}

bool UpdateOffsets()
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
    std::uint8_t* AppendStringScanResult = Memory::PatternScan(exeModule, "48 8D ?? ?? ?? 48 89 ?? ?? ?? E8 ?? ?? ?? ?? 83 ?? ?? ?? 00 48 8D ?? ?? ?? ?? ?? 41 ?? 01 00 00 00 48 8B ??");
    if (AppendStringScanResult) {
        SPDLOG_INFO("Offsets: AppendString: Address is {:s}+{:x}", sExeName, AppendStringScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
        std::uint8_t* AppendStringAddr = Memory::GetRelativeAddr(AppendStringScanResult + 0xB);
        SDK::Offsets::AppendString = static_cast<UC::uint32>(AppendStringAddr - reinterpret_cast<std::uint8_t*>(exeModule));
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
    return GObjectsScanResult && AppendStringScanResult && ProcessEventScanResult;
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
        // AspectRatioAxisConstraint
        std::uint8_t* AspectRatioAxisConstraintScanResult = Memory::PatternScan(exeModule, "48 ?? ?? ?? ?? 00 00 4C ?? ?? 4D ?? ?? E8 ?? ?? ?? ?? 48 8B ?? ?? ?? ?? ??");
        if (AspectRatioAxisConstraintScanResult) {
            SPDLOG_INFO("Aspect Ratio: FOV Axis Constraint: Address is {:s}+{:x}", sExeName.c_str(), AspectRatioAxisConstraintScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
            static SafetyHookMid AspectRatioAxisConstraintMidHook{};
            AspectRatioAxisConstraintMidHook = safetyhook::create_mid(AspectRatioAxisConstraintScanResult,
                [](SafetyHookContext& ctx) {
                    if (fAspectRatio > fNativeAspect)
                        ctx.rdx = 0;
                });
        }
        else {
            SPDLOG_ERROR("Aspect Ratio: FOV Axis Constraint: Pattern scan failed.");
        } 

        // bConstrainAspectRatio
        std::uint8_t* ConstrainAspectRatioScanResult = Memory::PatternScan(exeModule, "89 ?? ?? 0F B6 ?? ?? ?? ?? ?? D1 ?? 33 ?? 83 ?? 02 33 ?? 89 ?? ??");
        if (ConstrainAspectRatioScanResult) {
            SPDLOG_INFO("Aspect Ratio: Constrain Aspect Ratio: Address is {:s}+{:x}", sExeName.c_str(), ConstrainAspectRatioScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
            static SafetyHookMid ConstrainAspectRatioMidHook{};
            ConstrainAspectRatioMidHook = safetyhook::create_mid(ConstrainAspectRatioScanResult,
                [](SafetyHookContext& ctx) {
                    ctx.rcx = 0;
                });
        }
        else {
            SPDLOG_ERROR("Aspect Ratio: Constrain Aspect Ratio: Pattern scan failed.");
        }   
    }
}

template<typename T>
T* WidgetCast(SDK::UObject* object)
{
    auto type = T::StaticClass();
    return object && type && object->IsA(type) ? static_cast<T*>(object) : nullptr;
}

SDK::UWidget* RootWidget(SDK::UUserWidget* widget)
{
    return widget && widget->WidgetTree ? widget->WidgetTree->RootWidget : nullptr;
}

SDK::UPanelSlot* PanelSlot(SDK::UPanelWidget* panel, int index)
{
    return panel && panel->Slots.IsValidIndex(index) ? panel->Slots[index] : nullptr;
}

SDK::UWidget* PanelChild(SDK::UPanelWidget* panel, int index)
{
    auto slot = PanelSlot(panel, index);
    return slot ? slot->Content : nullptr;
}

// Blueprint members can move when the game's HUD is rebuilt. Look up the
// live property rather than reading the offsets from the bundled SDK dump.
SDK::UWidget* FindWidgetProperty(SDK::UUserWidget* widget, std::string_view name)
{
    for (auto type = static_cast<SDK::UStruct*>(widget->Class); type; type = type->Super) {
        for (auto field = type->ChildProperties; field; field = field->Next) {
            if (field->Name.ToString() != name)
                continue;

            auto property = static_cast<SDK::FProperty*>(field);
            if (property->ElementSize != sizeof(void*) || property->ArrayDim != 1 ||
                property->Offset < sizeof(SDK::UUserWidget) ||
                property->Offset > widget->Class->Size - static_cast<int>(sizeof(void*)))
                return nullptr;

            auto address = reinterpret_cast<std::uint8_t*>(widget) + property->Offset;
            return WidgetCast<SDK::UWidget>(*reinterpret_cast<SDK::UObject**>(address));
        }
    }
    return nullptr;
}

void WidenMenuCanvas(SDK::UWidget* widget)
{
    auto slot = WidgetCast<SDK::UCanvasPanelSlot>(widget ? widget->Slot : nullptr);
    if (!slot || slot->GetAutoSize())
        return;

    const auto anchors = slot->GetAnchors();
    auto offsets = slot->GetOffsets();
    // With a fixed horizontal anchor, Right is the width. With stretched
    // anchors it is a margin, so leave that layout to the canvas.
    if (anchors.Minimum.X != anchors.Maximum.X ||
        !((offsets.Right >= 3840.0f && offsets.Right < 3848.0f && offsets.Bottom >= 2160.0f && offsets.Bottom < 2168.0f) ||
          (offsets.Right == 1920.0f && offsets.Bottom == 1080.0f)))
        return;

    const float extraWidth = offsets.Bottom * fAspectRatio - offsets.Right;
    offsets.Left -= static_cast<float>(0.5 - slot->GetAlignment().X) * extraWidth;
    offsets.Right += extraWidth;
    slot->SetOffsets(offsets);
    SPDLOG_INFO("Span Menus: widened {}", widget->GetName());
}

void ReanchorMenuSlot(SDK::UCanvasPanelSlot* slot, float designWidth, double leftAnchor, double rightAnchor)
{
    auto anchors = slot->GetAnchors();
    auto offsets = slot->GetOffsets();
    const float left = static_cast<float>(anchors.Minimum.X) * designWidth + offsets.Left -
        static_cast<float>(slot->GetAlignment().X) * offsets.Right;
    if (leftAnchor != rightAnchor) {
        offsets.Left = left;
        offsets.Right = designWidth - left - offsets.Right;
    }
    else {
        offsets.Left += static_cast<float>(anchors.Minimum.X - leftAnchor) * designWidth;
    }
    anchors.Minimum.X = leftAnchor;
    anchors.Maximum.X = rightAnchor;
    slot->SetAnchors(anchors);
    slot->SetOffsets(offsets);
}

void AnchorHeaderControls(SDK::UPanelWidget* panel, float designWidth)
{
    if (!panel)
        return;

    for (int index = 0; index < panel->Slots.Num(); ++index) {
        auto slot = WidgetCast<SDK::UCanvasPanelSlot>(PanelSlot(panel, index));
        if (!slot || !slot->Content)
            continue;

        const auto anchors = slot->GetAnchors();
        const auto offsets = slot->GetOffsets();
        if (anchors.Minimum.X != 0.5 || anchors.Maximum.X != 0.5 ||
            offsets.Right > designWidth * 0.5f)
            continue;

        const double center = offsets.Left + (0.5 - slot->GetAlignment().X) * offsets.Right;
        const auto name = slot->Content->GetName();
        const bool schedule = name.contains("TimeTable") || name.contains("FestaSchedule");
        if (schedule || center < -designWidth * 0.25f || center > designWidth * 0.25f) {
            const double edge = !schedule && center < 0.0 ? 0.0 : 1.0;
            ReanchorMenuSlot(slot, designWidth, edge, edge);
            SPDLOG_INFO("Span Menus: moved header control {} toward the edge", slot->Content->GetName());
        }
    }
}

void SpanMenuBackgrounds(SDK::UPanelWidget* panel, float designWidth)
{
    if (!panel)
        return;

    for (int index = 0; index < panel->Slots.Num(); ++index) {
        auto slot = WidgetCast<SDK::UCanvasPanelSlot>(PanelSlot(panel, index));
        if (!slot || !slot->Content)
            continue;

        const auto name = slot->Content->GetName();
        if (name != "Panel" && name != "Black" && name != "DSP_RacePark_BG" &&
            name != "WidgetSwitcher_BG" && name != "DSP_BackgroundBlur" &&
            name != "BackgroundBlur_57" && name != "BackgroundBlur_45" &&
            name != "DMY_Banner" && name != "Filter" &&
            name != "BG_Black")
            continue;

        const auto anchors = slot->GetAnchors();
        const auto offsets = slot->GetOffsets();
        if (anchors.Minimum.X == anchors.Maximum.X && offsets.Right >= designWidth * 0.9f &&
            offsets.Right <= designWidth * 1.01f) {
            slot->SetAutoSize(false);
            ReanchorMenuSlot(slot, designWidth, 0.0, 1.0);
            SPDLOG_INFO("Span Menus: stretched background layer {}", name);
        }
    }
}

void SpanMenuControls(SDK::UPanelWidget* panel, float designWidth, bool header = false)
{
    if (!panel)
        return;

    for (int index = 0; index < panel->Slots.Num(); ++index) {
        auto child = PanelChild(panel, index);
        auto safeZone = WidgetCast<SDK::USafeZone>(child);
        auto slot = WidgetCast<SDK::UCanvasPanelSlot>(safeZone ? safeZone->Slot : nullptr);
        if (!slot || slot->GetAutoSize())
            continue;

        const auto anchors = slot->GetAnchors();
        const auto offsets = slot->GetOffsets();
        if (anchors.Minimum.X != 0.5 || anchors.Maximum.X != 0.5 ||
            offsets.Right < designWidth * 0.75f || offsets.Right > designWidth)
            continue;

        // Preserve the original edge margins and let the controls inside the
        // safe zone follow their existing left, center, and right anchors.
        ReanchorMenuSlot(slot, designWidth, 0.0, 1.0);
        if (header)
            AnchorHeaderControls(WidgetCast<SDK::UCanvasPanel>(PanelChild(safeZone, 0)), offsets.Right);
        SPDLOG_INFO("Span Menus: reanchored {}", safeZone->GetName());
    }
}

void CoverSelectionArtwork(SDK::UWidget* widget, int depth)
{
    if (!widget || depth > 6)
        return;

    const auto name = widget->GetName();
    if (name == "WBP_SelectBG_M1" || name == "WBP_CMN_BG_01") {
        if (auto root = RootWidget(WidgetCast<SDK::UUserWidget>(widget))) {
            widget->SetClipping(SDK::EWidgetClipping::Inherit);
            root->SetClipping(SDK::EWidgetClipping::Inherit);
            root->SetRenderTransformPivot(SDK::FVector2D{0.5, 0.5});
            root->SetRenderScale(SDK::FVector2D{fAspectMultiplier, fAspectMultiplier});
            SPDLOG_INFO("Span Menus: uniformly scaled selection artwork {}", name);
        }
        return;
    }

    if (auto panel = WidgetCast<SDK::UPanelWidget>(widget)) {
        for (int index = 0; index < panel->Slots.Num(); ++index)
            CoverSelectionArtwork(PanelChild(panel, index), depth + 1);
    }
}

void SpanMenu(SDK::UUserWidget* menu, const std::string& className)
{
    if (className != "WBP_TopMenu_C" && className != "WBP_MachineCharaSelect_M1_C" &&
        className != "WBP_CourseSelect_C" && className != "WBP_Ready_M1_C" &&
        className != "WBP_HeaderMenu_C" && className != "WBP_FooterMenu_C" &&
        className != "WBP_ClassSelect_C" &&
        className != "WBP_CMN_SubMenu_C")
        return;

    auto rootWidget = RootWidget(menu);
    if (!rootWidget)
        return;

    rootWidget->SetClipping(SDK::EWidgetClipping::Inherit);
    if (className == "WBP_CourseSelect_C" || className == "WBP_MachineCharaSelect_M1_C")
        CoverSelectionArtwork(rootWidget, 0);

    if (className == "WBP_TopMenu_C") {
        auto scaleBox = WidgetCast<SDK::UScaleBox>(rootWidget);
        if (auto sizeBox = WidgetCast<SDK::USizeBox>(PanelChild(scaleBox, 0))) {
            sizeBox->SetClipping(SDK::EWidgetClipping::Inherit);
            if (sizeBox->HeightOverride > 0.0f) {
                sizeBox->SetWidthOverride(sizeBox->HeightOverride * fAspectRatio);
                auto canvas = WidgetCast<SDK::UCanvasPanel>(PanelChild(sizeBox, 0));
                SpanMenuControls(canvas, sizeBox->HeightOverride * fNativeAspect);
                SpanMenuBackgrounds(canvas, sizeBox->HeightOverride * fNativeAspect);
                SPDLOG_INFO("Span Menus: main menu size {}x{}", sizeBox->HeightOverride * fAspectRatio, sizeBox->HeightOverride);
            }
        }
        return;
    }

    auto root = WidgetCast<SDK::UCanvasPanel>(rootWidget);
    if (!root)
        return;

    if (className == "WBP_MachineCharaSelect_M1_C")
        SpanMenuControls(root, 3840.0f);
    SpanMenuBackgrounds(root, 3840.0f);

    for (int index = 0; index < root->Slots.Num(); ++index) {
        auto child = PanelChild(root, index);
        if (child) {
            WidenMenuCanvas(child);
            if (auto canvas = WidgetCast<SDK::UCanvasPanel>(child)) {
                canvas->SetClipping(SDK::EWidgetClipping::Inherit);
                if (className == "WBP_CMN_SubMenu_C") {
                    auto buttons = WidgetCast<SDK::UCanvasPanelSlot>(PanelSlot(canvas, 0));
                    if (buttons && buttons->Content && buttons->Content->GetName() == "Overlay_0") {
                        const auto anchors = buttons->GetAnchors();
                        if (anchors.Minimum.X == 0.5 && anchors.Maximum.X == 0.5) {
                            ReanchorMenuSlot(buttons, 3840.0f, 0.0, 0.0);
                            SPDLOG_INFO("Span Menus: moved the customization buttons to the left");
                        }
                    }
                }
                if (className == "WBP_CourseSelect_C" || className == "WBP_HeaderMenu_C" ||
                    className == "WBP_FooterMenu_C")
                    SpanMenuControls(canvas, 3840.0f, className == "WBP_HeaderMenu_C");
                if (className == "WBP_HeaderMenu_C")
                    AnchorHeaderControls(canvas, 3840.0f);
                SpanMenuBackgrounds(canvas, 3840.0f);
            }
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
                    if (!bSpanRaceHUD && fAspectRatio > fNativeAspect)
                        ctx.xmm0.f64[0] = fHUDWidthOffset;
                });

            static SafetyHookMid ProjectWorldToScreenHeightMidHook{};
            ProjectWorldToScreenHeightMidHook = safetyhook::create_mid(ProjectWorldToScreenScanResult + 0x10,
                [](SafetyHookContext& ctx) {
                    if (!bSpanRaceHUD && fAspectRatio < fNativeAspect)
                        ctx.xmm0.f64[0] = fHUDHeightOffset;
                });
        }
        else {
            SPDLOG_ERROR("HUD: Project World To Screen: Pattern scan failed.");
        }

        // Fades
        std::uint8_t* FadesScanResult = Memory::PatternScan(exeModule, "49 8B ?? 41 0F ?? ?? 0F ?? ?? 48 8B ?? 48 85 ?? 0F 84 ?? ?? ?? ?? 8B ?? ?? C1 ?? ?? F6 ??");
        if (FadesScanResult) {
            SPDLOG_INFO("HUD: Fades: Address is {:s}+{:x}", sExeName.c_str(), FadesScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
            static SafetyHookMid FadesMidHook{};
            FadesMidHook = safetyhook::create_mid(FadesScanResult,
                [](SafetyHookContext& ctx) {
                    if (!ctx.rax) return;

                    auto transition = WidgetCast<SDK::UUserWidget>(reinterpret_cast<SDK::UObject*>(ctx.rax));
                    auto panel = WidgetCast<SDK::UCanvasPanel>(RootWidget(transition));
                    if (auto panelSlot = WidgetCast<SDK::UCanvasPanelSlot>(PanelSlot(panel, 0))) {

                        if (fAspectRatio > fNativeAspect)
                            panelSlot->SetOffsets(SDK::FMargin(0.0f, 0.0f, 2160.0f * fAspectRatio, 2160.0f));
                        else
                            panelSlot->SetOffsets(SDK::FMargin(0.0f, 0.0f, 3840.0f, 3840.0f / fAspectRatio));
                    }
                });
        }
        else {
            SPDLOG_ERROR("HUD: Fades: Pattern scan failed.");
        }
    }

    if (bFixHUD || bSpanRaceHUD) 
    {
        // Popup Window
        std::uint8_t* CreatePopupWindowScanResult = Memory::PatternScan(exeModule, "49 8B ?? ?? 48 89 ?? ?? 41 ?? ?? ?? 00 7E ?? 49 ?? ?? ?? 41 0F ?? ?? E8 ?? ?? ?? ?? 48 8B ?? ?? 48 85 ?? 74 ??");
        if (CreatePopupWindowScanResult) {
            SPDLOG_INFO("HUD: Create Popup Window: Address is {:s}+{:x}", sExeName.c_str(), CreatePopupWindowScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
            static SafetyHookMid CreatePopupWindowMidHook{};
            CreatePopupWindowMidHook = safetyhook::create_mid(CreatePopupWindowScanResult,
                [](SafetyHookContext& ctx) {
                    if (!ctx.rsi) return;

                    auto widget = reinterpret_cast<SDK::UUserWidget*>(ctx.rsi);

                    if (widget->Class && widget->Class->GetName() == "WBP_Popup_Window_C") {
                        auto panel = WidgetCast<SDK::UCanvasPanel>(RootWidget(widget));

                        float widthOffset = (2160.0f * fAspectRatio - 3840.0f) / 2.0f;
                        float heightOffset = (3840.0f / fAspectRatio - 2160.0f) / 2.0f;

                        if (auto backgroundBlurSlot = WidgetCast<SDK::UCanvasPanelSlot>(PanelSlot(panel, 2));
                            bFixHUD && backgroundBlurSlot) {

                            if (bFixHUD) {
                                if (fAspectRatio > fNativeAspect)
                                    backgroundBlurSlot->SetOffsets(SDK::FMargin(-1920.0f - widthOffset, -1080.0f, 2160.0f * fAspectRatio, 2160.0f));
                                else
                                    backgroundBlurSlot->SetOffsets(SDK::FMargin(-1920.0f, -1080.0f - heightOffset, 3840.0f, 3840.0f / fAspectRatio));
                            }

                            if (bSpanRaceHUD && !bSpanMenus) {
                                if (fAspectRatio > fNativeAspect)
                                    backgroundBlurSlot->SetOffsets(SDK::FMargin(-1920.0f, -1080.0f, 2160.0f * fAspectRatio, 2160.0f));
                                else
                                    backgroundBlurSlot->SetOffsets(SDK::FMargin(-1920.0f, -1080.0f, 3840.0f, 3840.0f / fAspectRatio));
                            }
                        }
                        
                        if (bSpanRaceHUD && !bSpanMenus) {
                            auto safeZone = WidgetCast<SDK::USafeZone>(PanelChild(panel, 3));
                            auto innerPanel = WidgetCast<SDK::UCanvasPanel>(PanelChild(safeZone, 0));
                            auto panelSlot = WidgetCast<SDK::UCanvasPanelSlot>(PanelSlot(innerPanel, 0));
                            if (!panelSlot) return;

                            if (fAspectRatio > fNativeAspect)
                                panelSlot->SetOffsets(SDK::FMargin(widthOffset, 0.0f, 3200.0f, 1000.0f));
                            else
                                panelSlot->SetOffsets(SDK::FMargin(0.0f, heightOffset, 3200.0f, 1000.0f));
                        }
                    }
                });
        }
        else {
            SPDLOG_ERROR("HUD: Create Popup Window: Pattern scan failed.");
        }

    }

    if (bFixHUD || bSpanRaceHUD || bSpanMenus)
    {
        // Widgets
        std::uint8_t* UWidgetAddToViewportScanResult = Memory::PatternScan(exeModule, "48 83 ?? ?? 4D 8B ?? 45 33 ?? E8 ?? ?? ?? ?? 48 83 ?? ?? C3");
        if (UWidgetAddToViewportScanResult) {
            SPDLOG_INFO("HUD: Widgets: Address is {:s}+{:x}", sExeName.c_str(), UWidgetAddToViewportScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
            static SafetyHookMid UWidgetAddToViewportMidHook{};
            UWidgetAddToViewportMidHook = safetyhook::create_mid(UWidgetAddToViewportScanResult,
                [](SafetyHookContext& ctx) {
                    if (!ctx.rdx) return;

                    auto obj = reinterpret_cast<SDK::UObject*>(ctx.rdx);

                    if (!obj->Class || !obj->Class->Class) 
                        return;

                    auto className = obj->Class->GetName();
                    if (bSpanMenus && fAspectRatio > fNativeAspect) {
                        if (auto widget = WidgetCast<SDK::UUserWidget>(obj))
                            SpanMenu(widget, className);
                    }

                    if ((!bFixHUD && !bSpanRaceHUD) || className != "WBP_Race_HUD_Player01_C")
                        return;

                    float widthOffset = (2160.0f * fAspectRatio - 3840.0f) / 2.0f;
                    float heightOffset = (3840.0f / fAspectRatio - 2160.0f) / 2.0f;

                    auto hud = static_cast<SDK::UUserWidget*>(obj);
                    auto scaleBox = WidgetCast<SDK::UScaleBox>(RootWidget(hud));
                    if (auto sizeBox = WidgetCast<SDK::USizeBox>(PanelChild(scaleBox, 0))) {

                        if (bSpanRaceHUD) {
                            if (fAspectRatio > fNativeAspect) {
                                sizeBox->SetWidthOverride(2160.0f * fAspectRatio);
                                sizeBox->SetHeightOverride(2160.0f);
                            }
                            else {
                                sizeBox->SetWidthOverride(3840.0f);
                                sizeBox->SetHeightOverride(3840.0f / fAspectRatio);
                            }

                            auto textDirection = FindWidgetProperty(hud, "CLIP_TextDirection");
                            if (auto clippingPanel = WidgetCast<SDK::UCanvasPanelSlot>(
                                textDirection ? textDirection->Slot : nullptr)) {

                                if (fAspectRatio > fNativeAspect)
                                    clippingPanel->SetOffsets(SDK::FMargin(0.0f, 0.0f, 2160.0f * fAspectRatio, 2160.0f));
                                else
                                    clippingPanel->SetOffsets(SDK::FMargin(0.0f, 0.0f, 3840.0f, 3840.0f / fAspectRatio));
                            }
                        }

                        if (bFixHUD) {
                            // Disable clipping so that player markers don't disappear outside 16:9 bounds
                            sizeBox->SetClipping(SDK::EWidgetClipping::Inherit);

                            // Span background of pause menu
                            auto pauseMenu = WidgetCast<SDK::UUserWidget>(FindWidgetProperty(hud, "WBP_PauseMenu"));
                            auto pauseScaleBox = WidgetCast<SDK::UScaleBox>(RootWidget(pauseMenu));
                            if (auto panel = WidgetCast<SDK::UCanvasPanel>(PanelChild(pauseScaleBox, 0))) {
                                panel->SetClipping(SDK::EWidgetClipping::Inherit);

                                auto backgroundBlurSlot = WidgetCast<SDK::UCanvasPanelSlot>(PanelSlot(panel, 2));
                                auto bgBlackSlot = WidgetCast<SDK::UCanvasPanelSlot>(PanelSlot(panel, 3));
                                if (backgroundBlurSlot && bgBlackSlot) {

                                    if (fAspectRatio > fNativeAspect) {
                                        backgroundBlurSlot->SetOffsets(SDK::FMargin(-widthOffset, 0.0f, -widthOffset, 0.0f));
                                        bgBlackSlot->SetOffsets(SDK::FMargin(-widthOffset, 0.0f, -widthOffset, 0.0f));
                                    }
                                    else {
                                        backgroundBlurSlot->SetOffsets(SDK::FMargin(0.0f, -heightOffset, 0.0f, -heightOffset));
                                        bgBlackSlot->SetOffsets(SDK::FMargin(0.0f, -heightOffset, 0.0f, -heightOffset));
                                    }
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

DWORD __stdcall Main(void*)
{
    Logging();
    Configuration();
    CurrentResolution();
    AspectRatioFOV();
    if ((bFixHUD || bSpanRaceHUD || bSpanMenus) && UpdateOffsets())
        HUD();
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
