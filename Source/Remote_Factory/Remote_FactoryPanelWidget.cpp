#include "Remote_FactoryPanelWidget.h"

#include "Remote_FactoryLog.h"
#include "Remote_FactoryRCO.h"
#include "Remote_FactoryCrashTrace.h"
#include "Remote_FactoryButtonInfo.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableManufacturer.h"
#include "Buildables/FGBuildableResourceExtractorBase.h"
#include "FGInventoryComponent.h"
#include "FGPlayerController.h"
#include "FGHUD.h"
#include "UI/FGGameUI.h"      // ★0.2.132 补光标护栏：GetGameUI()->HasActiveInteractWidget()（FGGameUI.h:88）
#include "Containers/Ticker.h" // ★0.2.132 FTSTicker 逐帧补光标（同款模式见 CrashTrace/Equipment watchdog）
#include "GameFramework/Pawn.h"
#include "Resources/FGItemDescriptor.h"
#include "UI/FGInteractWidget.h"
#include "InputCoreTypes.h"   // EKeys::Escape for the panel's ESC-to-close handler
#include "GameFramework/PlayerController.h"   // FInputModeUIOnly for re-asserting keyboard focus (0.2.130)
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"        // ★0.2.153m UImage（列表行图标）
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/Layout/Anchors.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SWidget.h"
#include "Input/Reply.h"
#include "Remote_FactoryTechBackground.h"
#include "Remote_FactoryCollectibles.h"
#include "FGMapAreaTexture.h"
#include "FGMapArea.h"

// ----------------------------------------------------------------------------
// Theme: Dyson Sphere Program inspired.
//
// DSP's HUD is a cool, high-density technical console: near-black desaturated navy panels, a cyan
// / aqua accent for anything interactive or selected, thin bright dividers, and amber reserved for
// warnings. Values are bright and slightly cool rather than pure white, and the accent is used
// sparingly so the selected row reads instantly.
// ----------------------------------------------------------------------------
static const FLinearColor C_BG      = FLinearColor(0.043f, 0.055f, 0.078f, 0.97f);  // deep navy
static const FLinearColor C_PANEL   = FLinearColor(0.070f, 0.090f, 0.125f, 1.f);    // raised panel
static const FLinearColor C_ACCENT  = FLinearColor(0.290f, 0.780f, 0.900f, 1.f);    // DSP cyan
static const FLinearColor C_ACCENT_D= FLinearColor(0.145f, 0.400f, 0.470f, 1.f);    // dimmed cyan
static const FLinearColor C_LABEL   = FLinearColor(0.560f, 0.660f, 0.730f, 1.f);    // cool grey text
static const FLinearColor C_VALUE   = FLinearColor(0.870f, 0.940f, 0.970f, 1.f);    // near-white
static const FLinearColor C_GREEN   = FLinearColor(0.380f, 0.900f, 0.640f, 1.f);    // DSP mint
static const FLinearColor C_RED     = FLinearColor(0.960f, 0.400f, 0.360f, 1.f);
static const FLinearColor C_AMBER   = FLinearColor(0.980f, 0.700f, 0.250f, 1.f);    // warning
static const FLinearColor C_CAVE    = FLinearColor(0.760f, 0.620f, 1.f, 1.f);       // ★0.2.136 溶洞条目标注色（淡紫，不与青/绿/琥珀冲突）
static const FLinearColor C_DIV     = FLinearColor(0.290f, 0.780f, 0.900f, 0.45f);  // cyan hairline
static const FLinearColor C_BTN     = FLinearColor(0.095f, 0.125f, 0.165f, 1.f);    // idle button
static const FLinearColor C_BTN_SEL = FLinearColor(0.290f, 0.780f, 0.900f, 0.90f);  // selected row
static const FLinearColor C_ON_SEL  = FLinearColor(0.035f, 0.060f, 0.080f, 1.f);    // text on selection

// ----------------------------------------------------------------------------
// small widget helpers (mirrors WirelessPowerPanelWidget so the look is consistent)
// ----------------------------------------------------------------------------

static UTextBlock* MakeText(UWidgetTree* Tree, const FText& InText, int32 FontSize,
                            bool bBold, FLinearColor Color, ETextJustify::Type Justify = ETextJustify::Left)
{
    UTextBlock* T = Tree->ConstructWidget<UTextBlock>();
    T->SetText(InText);
    T->SetFont(FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), FontSize));
    T->SetColorAndOpacity(FSlateColor(Color));
    T->SetJustification(Justify);
    // ★v0.2.35 长文本（机器详情/传送目标行）允许换行：不换行时 TextBlock 的期望宽度会把
    // 所在列的最小宽度撑到超出面板，DetailBox 整列被顶出右边界（0.2.34 窗口化实测）。
    T->SetAutoWrapText(true);
    return T;
}

static UButton* MakeButton(UWidgetTree* Tree, const FText& InText, FLinearColor BgColor,
                           UTextBlock** OutLabel, ETextJustify::Type Justify = ETextJustify::Center)
{
    UButton* B = Tree->ConstructWidget<UButton>();
    B->SetBackgroundColor(BgColor);
    UTextBlock* T = MakeText(Tree, InText, 13, false, C_VALUE, Justify);
    B->AddChild(T);
    if (OutLabel) { *OutLabel = T; }
    return B;
}

static UBorder* MakeDivider(UWidgetTree* Tree)
{
    UBorder* D = Tree->ConstructWidget<UBorder>();
    // Explicit engine white brush: a default-constructed FSlateBrush has NO resource, and painting
    // a zero-resource box was the crash signature we hit in WirelessPower (access violation in
    // FName::ToString during OnPaint). Always paint a brush whose texture really exists.
    D->SetBrush(*FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")));
    D->SetBrushColor(C_DIV);
    D->SetPadding(FMargin(0.f, 3.f));
    return D;
}

// ★0.2.70 centralised collectible-row label so the live-distance refresh (NativeTick) and the full
// rebuild share one format string. Distance: < 1 km shows the concrete metre count (e.g. "523m");
// >= 1 km switches to km (e.g. "52.3km") so far spawns stay readable.
static FText WTBuildCollectibleRowText(const FWT_CollectibleEntry& C)
{
    const float M = C.DistanceMeters;
    const FString DistStr = (M < 1000.f)
        ? (FString::FromInt(FMath::RoundToInt(M)) + TEXT("m"))
        : FString::Printf(TEXT("%.1fkm"), M / 1000.f);
    const FText StateText = C.bCollected ? WT_LOCTEXT("ColState_Collected", "已拾取")
                                        : (C.RegionName.IsEmpty() ? WT_LOCTEXT("ColState_Uncollected", "未拾取")
                                                                 : C.RegionName);
    // ★0.2.136 溶洞条目在行尾追加 [溶洞] 标签。用另一条格式串而不是改 ColRow，避免动到既有
    // 译文（zh/en 两条本地化都带 {0}{1}{2} 的占位顺序）。
    return C.bCave
        ? FText::Format(WT_LOCTEXT("ColRow_Cave", "{0}   {1}   {2}   [溶洞]"),
                        C.DisplayName, FText::FromString(DistStr), StateText)
        : FText::Format(WT_LOCTEXT("ColRow", "{0}   {1}   {2}"),
                        C.DisplayName, FText::FromString(DistStr), StateText);
}

// Localization: the English string is the source language (see
// Config/Localization/Remote_Factory_Gather.ini). Chinese lives in
// Content/Localization/Remote_Factory/zh-Hans/Remote_Factory.po.
// WT_LOCTEXT is defined in Remote_FactoryTypes.h.

static FText CategoryLabel(int32 Index)
{
    switch (static_cast<EWT_MachineCategory>(Index))
    {
        case EWT_MachineCategory::Producer:      return WT_LOCTEXT("Cat_Producer",      "Production");
        case EWT_MachineCategory::Container:     return WT_LOCTEXT("Cat_Container",     "容器");
        case EWT_MachineCategory::Miner:         return WT_LOCTEXT("Cat_Miner",         "Extraction");
        case EWT_MachineCategory::Generator:     return WT_LOCTEXT("Cat_Generator",     "Power Generation");
        case EWT_MachineCategory::PowerSwitch:   return WT_LOCTEXT("Cat_PowerSwitch",   "Power Switch");
        case EWT_MachineCategory::AwesomeSink:   return WT_LOCTEXT("Cat_AwesomeSink",   "Resource Sink");
        case EWT_MachineCategory::Hub:           return WT_LOCTEXT("Cat_Hub",           "HUB");
        case EWT_MachineCategory::SpaceElevator: return WT_LOCTEXT("Cat_SpaceElevator", "Space Elevator");
        case EWT_MachineCategory::MAM:           return WT_LOCTEXT("Cat_MAM",           "MAM");
        case EWT_MachineCategory::DropPod:       return WT_LOCTEXT("Cat_DropPod",       "Drop Pod");
        case EWT_MachineCategory::Modded:        return WT_LOCTEXT("Cat_Modded",        "MOD建筑");
        case EWT_MachineCategory::Shop:          return WT_LOCTEXT("Cat_Shop",          "AWESOME商店");
        case EWT_MachineCategory::CraftBench:    return WT_LOCTEXT("Cat_CraftBench",    "制作台");
        case EWT_MachineCategory::EquipWorkshop: return WT_LOCTEXT("Cat_EquipWorkshop", "装备工坊");
        case EWT_MachineCategory::LiquidContainer: return WT_LOCTEXT("Cat_LiquidContainer", "液体容器");
        case EWT_MachineCategory::SolidContainer:  return WT_LOCTEXT("Cat_SolidContainer",  "固体容器");
        case EWT_MachineCategory::Vehicle:         return WT_LOCTEXT("Cat_Vehicle",         "运输工具");
        case EWT_MachineCategory::Portal:          return WT_LOCTEXT("Cat_Portal",          "传送门");
        default:                                 return WT_LOCTEXT("Cat_Other",         "Other");
    }
}

// ★0.2.67: official whole-map totals. The live scan only sees the streaming-loaded cells near
// the player, so its per-kind counts under-report the entire map. The headline numbers shown to
// the player therefore come from here (when collectibles.json is not loaded), not from
// CollectibleCounts. From 0.2.84 the alien numbers are the Satisfactory wiki 1.0/1.1 figures and
// the flora numbers are the full-map SCIM (Satisfactory-Calculator) counts — identical to the
// collectibles.json totals shipped in SmartFactoryLib.
// Index order matches EWT_CollectibleKind: Blue, Yellow, Purple, MercerSphere, Somersloop, DropPod,
// BerylNut, PaleBerry, BaconAgaric.
static constexpr int32 WT_WikiCollectibleTotal[] =
{
    596,   // BlueSlug     蓝能量蛞蝓
    389,   // YellowSlug   黄能量蛞蝓
    257,   // PurpleSlug   紫能量蛞蝓
    298,   // MercerSphere 梅瑟尔球
    106,   // Somersloop    索莫晶体
    118,   // DropPod      空投舱
    1524,  // BerylNut     绿石果 (SCIM 全图)
    2162,  // PaleBerry    古莓  (SCIM 全图)
    1615   // BaconAgaric  培根伞菌  (SCIM 全图)
};

static FText CollectibleKindLabel(int32 Index)
{
    switch (static_cast<EWT_CollectibleKind>(Index))
    {
        case EWT_CollectibleKind::BlueSlug:      return WT_LOCTEXT("Col_SlugBlue",    "Power Slug (Blue)");
        case EWT_CollectibleKind::YellowSlug:    return WT_LOCTEXT("Col_SlugYellow",  "Power Slug (Yellow)");
        case EWT_CollectibleKind::PurpleSlug:    return WT_LOCTEXT("Col_SlugPurple",  "Power Slug (Purple)");
        case EWT_CollectibleKind::MercerSphere:  return WT_LOCTEXT("Col_MercerSphere", "Mercer Sphere");
        case EWT_CollectibleKind::Somersloop:    return WT_LOCTEXT("Col_Somersloop",   "Somersloop");
        case EWT_CollectibleKind::DropPod:       return WT_LOCTEXT("Col_DropPod",      "Drop Pod");
        case EWT_CollectibleKind::BerylNut:      return WT_LOCTEXT("Col_BerylNut",     "绿石果");
        case EWT_CollectibleKind::PaleBerry:     return WT_LOCTEXT("Col_PaleBerry",    "古莓");
        case EWT_CollectibleKind::BaconAgaric:   return WT_LOCTEXT("Col_BaconAgaric",  "培根伞菌");
        default:                                 return WT_LOCTEXT("Col_Other",        "Other");
    }
}

/**
 * ★0.2.64 label of the picked-up sub-row under an alien-energy kind.
 * @param Picked 0 = 未拾取 (still lying on the map), 1 = 已拾取 / already opened.
 */
static FText CollectiblePickedLabel(int32 Picked)
{
    return Picked == 1 ? WT_LOCTEXT("ColPick_Done", "【已拾取】")
                       : WT_LOCTEXT("ColPick_Open", "【未拾取】");
}

// ----------------------------------------------------------------------------

URemote_FactoryPanelWidget::URemote_FactoryPanelWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    // ★v0.2.53 修复「关面板后卡鼠标+键盘」根因（已对照 SDK 桩确认：UFGInteractWidget::SetInputMode_Implementation
    // 为空、mUseMouse/mUseKeyboard 字段在提取源码里从不被动读取——游戏对该机制的真正行为在编译二进制里）：
    // 原版交互面板靠 mUseKeyboard/mUseMouse=true 让游戏在面板激活时屏蔽玩家键鼠、出栈时还原；本模组自 v0.2.20
    // 起停用 GameUI 注册（与 FasterManualCraftingRedux 冲突崩溃），于是"施加→还原"链被腰斩：屏蔽被施加却
    // 无人还原 → 关面板后永久卡死。0.2.52 只在 ClosePanel 调 SetInputMode(GameOnly) 还原，但游戏内部仍认定
    // 有活跃交互控件在屏蔽输入、每帧压回 UI 模式 → 还原无效。
    // 改法（不猜游戏内部、只走标准 UE SetInputMode，已被 0.2.52 证明可编译可运行）：
    //   ① 这里 mUseKeyboard=mUseMouse=false，彻底退出游戏的自动屏蔽机制（我们本就不在 GameUI 栈上）；
    //   ② OpenPanel 显式 SetInputMode(FInputModeGameAndUI)+bShowMouseCursor=true（面板可点按）；
    //   ③ ClosePanel / NativeDestruct 显式 SetInputMode(FInputModeGameOnly)+bShowMouseCursor=false（对称还原）。
    // 开/关两路都是我们自己调的同一对 API，无"施加缺还原"缝隙，从根上不可能再锁。Slate 点击/滚动/聚焦照常。
    mUseKeyboard = false;
    mUseMouse = false;
    mMouseLockMode = EMouseLockMode::DoNotLock;
    mSupportsStacking = true;
    mDesiredHorizontalAlignment = HAlign_Center;
    mDesiredVerticalAlignment = VAlign_Center;
}

URemote_FactoryRCO* URemote_FactoryPanelWidget::GetRCO() const
{
    // The panel is a client-side widget; the owning controller is the local player's.
    if (AFGPlayerController* PC = Cast<AFGPlayerController>(GetOwningPlayer()))
    {
        return URemote_FactoryRCO::Get(PC);
    }
    return nullptr;
}

TSharedRef<SWidget> URemote_FactoryPanelWidget::RebuildWidget()
{
    // ★v0.2.24 UI 太大：原来根就是 UBorder，AddToViewport 后它会铺满整个屏幕（用户截图里
    // 面板占满全屏、字号巨大）。改成 CanvasPanel 做根，把面板按"视口的 62% 宽 / 76% 高、
    // 上限 1280x820"居中放置；分辨率变化在 NativeConstruct 里再校一次。
    RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>();

    RootBorder = WidgetTree->ConstructWidget<UBorder>();
    RootBorder->SetBrush(*FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")));
    RootBorder->SetBrushColor(C_BG);
    // ★v0.2.65: padding moved from the border onto the content widget (see "assemble" below) so
    // the modern-tech background deck can paint edge to edge; the border itself is now only the
    // opaque base colour / clipping shell.
    RootBorder->SetPadding(FMargin(0.f));
    // ★v0.2.35 裁剪兜底：三列正文（尤其 DetailBox 的长文本行）最小宽度可能超过面板宽度，
    // UBorder 默认不裁剪、子内容会整列溢出到面板右边界之外（0.2.34 窗口化实测）。
    // ClipToBounds 保证任何情况下内容都不会画出面板边框。
    RootBorder->SetClipping(EWidgetClipping::ClipToBounds);

    if (UCanvasPanelSlot* CanvasSlot = RootCanvas->AddChildToCanvas(RootBorder))
    {
        CanvasSlot->SetAnchors(FAnchors(0.5f, 0.5f));
        CanvasSlot->SetAlignment(FVector2D(0.5f, 0.5f));
        // 先给一个设计尺寸占位；NativeConstruct 里按真实视口再收一遍。
        CanvasSlot->SetSize(FVector2D(1180.f, 760.f));
        CanvasSlot->SetZOrder(0);
    }

    UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>();

    // ---- title row -----------------------------------------------------------
    UHorizontalBox* TitleRow = WidgetTree->ConstructWidget<UHorizontalBox>();

    TitleText = MakeText(WidgetTree, WT_LOCTEXT("Title", "智能工厂 · 终端面板"), 22, true, C_ACCENT);
    if (UHorizontalBoxSlot* S = TitleRow->AddChildToHorizontalBox(TitleText))
    {
        S->SetPadding(FMargin(2.f, 0.f, 12.f, 0.f));
        S->SetVerticalAlignment(VAlign_Center);
    }

    RefreshButton = MakeButton(WidgetTree, WT_LOCTEXT("Btn_Refresh", "Refresh"), C_BTN, nullptr);
    RefreshButton->OnClicked.AddDynamic(this, &URemote_FactoryPanelWidget::OnRefreshClicked);
    if (UHorizontalBoxSlot* S = TitleRow->AddChildToHorizontalBox(RefreshButton))
    {
        S->SetPadding(FMargin(4.f, 0.f));
        S->SetVerticalAlignment(VAlign_Center);
    }

    StatusText = MakeText(WidgetTree, WT_LOCTEXT("Status_Scanning", "Scanning…"), 12, false, C_LABEL);
    if (UHorizontalBoxSlot* S = TitleRow->AddChildToHorizontalBox(StatusText))
    {
        S->SetPadding(FMargin(12.f, 0.f));
        S->SetVerticalAlignment(VAlign_Center);
    }

    // ---- calculator launcher ------------------------------------------------
    // Opens the in-game calculator window (replaces the old AI chat launcher).
    {
        UHorizontalBox* Spacer = WidgetTree->ConstructWidget<UHorizontalBox>();
        if (UHorizontalBoxSlot* S = TitleRow->AddChildToHorizontalBox(Spacer))
        {
            S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        }

        // ★v0.2.15 ✕ 关闭按钮：暂停菜单被面板压住时的保底出口（用户定案）。
        // ★v0.2.16 文字改"关闭"：✕ 符号在部分字体下渲染不佳，且中文界面更直观（用户定案"把 x 换成字"）。
        // ★v0.2.23 源文本改用英文 "Close"（v0.2.16 曾写中文 "关闭"）。原因是同一个 key Btn_Close
        // 在 ChatWidget 里也用了一次、源文本是 "Close" —— GatherText 报 key 冲突，locres 里只能留一条，
        // 英文环境下面板关闭按钮会显示成中文。两处源文本统一成 "Close" 后冲突消失，
        // 中文照旧由 .po 的 msgstr「关闭」提供。
        UButton* CloseButton = MakeButton(WidgetTree, WT_LOCTEXT("Btn_Close", "Close"), C_BTN, nullptr);
        CloseButton->OnClicked.AddDynamic(this, &URemote_FactoryPanelWidget::OnCloseClicked);
        if (UHorizontalBoxSlot* S = TitleRow->AddChildToHorizontalBox(CloseButton))
        {
            S->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
            S->SetVerticalAlignment(VAlign_Center);
            S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
        }
    }

    if (UVerticalBoxSlot* S = Root->AddChildToVerticalBox(TitleRow))
    {
        S->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
    }
    Root->AddChildToVerticalBox(MakeDivider(WidgetTree));

    // ---- factory power overview ----------------------------------------------
    // A DSP-style readout: the numbers sit on one line, with a slim bar underneath whose fill
    // tracks current draw against installed capacity.
    PowerText = MakeText(WidgetTree, WT_LOCTEXT("Power_Reading", "Power  reading…"), 13, false, C_VALUE);
    if (UVerticalBoxSlot* S = Root->AddChildToVerticalBox(PowerText))
    {
        S->SetPadding(FMargin(2.f, 4.f, 0.f, 2.f));
    }

    PowerBar = WidgetTree->ConstructWidget<UProgressBar>();
    PowerBar->SetPercent(0.f);
    PowerBar->SetFillColorAndOpacity(C_ACCENT);
    if (UVerticalBoxSlot* S = Root->AddChildToVerticalBox(PowerBar))
    {
        S->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
    }

    Root->AddChildToVerticalBox(MakeDivider(WidgetTree));

    // ---- three-column body ---------------------------------------------------
    UHorizontalBox* Body = WidgetTree->ConstructWidget<UHorizontalBox>();

    // left: categories -- v0.2.55: vertical stack (分类标签移到左侧一列)
    // v0.2.66: wrap the stack in a ScrollBox so the expanded alien-tech (37+) / machine lists
    // can scroll instead of overflowing and clipping the panel.
    UScrollBox* CategoryScroll = WidgetTree->ConstructWidget<UScrollBox>();
    CategoryScroll->SetScrollbarThickness(FVector2D(8.f, 8.f));
    CategoryBox = WidgetTree->ConstructWidget<UVerticalBox>();
    if (UScrollBoxSlot* CS = Cast<UScrollBoxSlot>(CategoryScroll->AddChild(CategoryBox)))
    {
        CS->SetPadding(FMargin(0.f));
    }
    if (UHorizontalBoxSlot* S = Body->AddChildToHorizontalBox(CategoryScroll))
    {
        S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
        S->SetVerticalAlignment(VAlign_Fill);
        S->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
    }

    // middle: machine rows
    MachineListBox = WidgetTree->ConstructWidget<UScrollBox>();
    if (UHorizontalBoxSlot* S = Body->AddChildToHorizontalBox(MachineListBox))
    {
        FSlateChildSize Size(ESlateSizeRule::Fill);
        Size.Value = 1.2f;
        S->SetSize(Size);
        S->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
    }

    // right: detail pane
    // ★0.2.57 (user decision): the whole right detail pane is retired. The widgets are still
    // constructed below (selection/refresh code keeps writing into them), but the pane is never
    // attached to the Body layout, so it renders nowhere -- zero risk to the many update sites.
    DetailBox = WidgetTree->ConstructWidget<UVerticalBox>();

    if (UVerticalBoxSlot* S = Root->AddChildToVerticalBox(Body))
    {
        S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }

    // ---- assemble ------------------------------------------------------------
    // ★v0.2.65 modern-tech shell: an overlay with two layers --
    //   layer 0: SWTTechDeck (gradient + grid + scan lines + outline + corner brackets)
    //   layer 1: the real content, inset by 16 px so it never touches the deck's own frame.
    // The deck declares zero desired size, so the panel keeps being sized by its content.
    {
        UOverlay* Shell = WidgetTree->ConstructWidget<UOverlay>();

        UWTTechBackground* TechDeck = WidgetTree->ConstructWidget<UWTTechBackground>();
        if (UOverlaySlot* BgSlot = Shell->AddChildToOverlay(TechDeck))
        {
            BgSlot->SetHorizontalAlignment(HAlign_Fill);
            BgSlot->SetVerticalAlignment(VAlign_Fill);
        }

        if (UOverlaySlot* ContentSlot = Shell->AddChildToOverlay(Root))
        {
            ContentSlot->SetHorizontalAlignment(HAlign_Fill);
            ContentSlot->SetVerticalAlignment(VAlign_Fill);
            ContentSlot->SetPadding(FMargin(16.f));
        }

        RootBorder->AddChild(Shell);
        if (UBorderSlot* S = Cast<UBorderSlot>(RootBorder->GetContentSlot()))
        {
            S->SetHorizontalAlignment(HAlign_Fill);
            S->SetVerticalAlignment(VAlign_Fill);
        }
    }

    // Build the static parts of the detail pane once; values are filled in on selection.
    DetailNameText = MakeText(WidgetTree, WT_LOCTEXT("Detail_NoMachine", "No machine selected"), 16, true, C_VALUE);
    DetailBox->AddChildToVerticalBox(DetailNameText);
    DetailBox->AddChildToVerticalBox(MakeDivider(WidgetTree));

    DetailStatusText = MakeText(WidgetTree, FText::GetEmpty(), 13, false, C_LABEL);
    if (UVerticalBoxSlot* S = DetailBox->AddChildToVerticalBox(DetailStatusText))
    {
        S->SetPadding(FMargin(0.f, 2.f));
    }

    DetailOutputText = MakeText(WidgetTree, FText::GetEmpty(), 13, false, C_VALUE);
    if (UVerticalBoxSlot* S = DetailBox->AddChildToVerticalBox(DetailOutputText))
    {
        S->SetPadding(FMargin(0.f, 2.f));
    }

    DetailBox->AddChildToVerticalBox(MakeDivider(WidgetTree));

    OpenMachineButton = MakeButton(WidgetTree, WT_LOCTEXT("Btn_OpenMachinePanel", "Open machine panel"), C_BTN, nullptr);
    OpenMachineButton->OnClicked.AddDynamic(this, &URemote_FactoryPanelWidget::OnOpenMachineClicked);
    if (UVerticalBoxSlot* S = DetailBox->AddChildToVerticalBox(OpenMachineButton))
    {
        S->SetPadding(FMargin(0.f, 6.f));
    }

    PullOutputButton = MakeButton(WidgetTree, WT_LOCTEXT("Btn_PullOutput", "Take output"), C_ACCENT, nullptr);
    PullOutputButton->OnClicked.AddDynamic(this, &URemote_FactoryPanelWidget::OnPullOutputClicked);
    if (UVerticalBoxSlot* S = DetailBox->AddChildToVerticalBox(PullOutputButton))
    {
        S->SetPadding(FMargin(0.f, 6.f));
    }

    TeleportToMachineButton = MakeButton(WidgetTree, WT_LOCTEXT("Btn_TeleportToMachine", "Teleport to this machine"), C_BTN, nullptr);
    TeleportToMachineButton->OnClicked.AddDynamic(this, &URemote_FactoryPanelWidget::OnTeleportToMachineClicked);
    if (UVerticalBoxSlot* S = DetailBox->AddChildToVerticalBox(TeleportToMachineButton))
    {
        S->SetPadding(FMargin(0.f, 6.f));
    }

    // ---- Form 3 "A": power-switch operation block -------------------------------
    // Built unconditionally and then hidden, rather than created on demand. Creating widgets at
    // click time is what the mod does for the AI chat window (a whole separate window), but here the
    // block sits inside a pane that is rebuilt constantly on selection changes -- keeping it alive
    // and toggling its visibility avoids churning the widget tree on every row click.
    DetailBox->AddChildToVerticalBox(MakeDivider(WidgetTree));

    SwitchBox = WidgetTree->ConstructWidget<UVerticalBox>();
    SwitchBox->SetVisibility(ESlateVisibility::Collapsed);

    SwitchHeaderText = MakeText(WidgetTree, WT_LOCTEXT("Header_Switch", "Power Switch"), 15, true, C_ACCENT);
    if (UVerticalBoxSlot* S = SwitchBox->AddChildToVerticalBox(SwitchHeaderText))
    {
        S->SetPadding(FMargin(0.f, 4.f, 0.f, 2.f));
    }

    SwitchStateText = MakeText(WidgetTree, FText::GetEmpty(), 12, false, C_LABEL);
    if (UVerticalBoxSlot* S = SwitchBox->AddChildToVerticalBox(SwitchStateText))
    {
        S->SetPadding(FMargin(0.f, 2.f));
    }

    // SwitchToggleLabel is a TObjectPtr (UPROPERTY-friendly), but MakeButton hands back a raw
    // pointer; take the raw first and let TObjectPtr convert on assignment.
    UTextBlock* ToggleLabelRaw = nullptr;
    SwitchToggleButton = MakeButton(WidgetTree, WT_LOCTEXT("Btn_SwitchOn", "Turn on"), C_ACCENT,
                                    &ToggleLabelRaw);
    SwitchToggleLabel = ToggleLabelRaw;
    SwitchToggleButton->OnClicked.AddDynamic(this, &URemote_FactoryPanelWidget::OnToggleSwitchClicked);
    if (UVerticalBoxSlot* S = SwitchBox->AddChildToVerticalBox(SwitchToggleButton))
    {
        S->SetPadding(FMargin(0.f, 6.f));
    }

    // Priority row: [ - ]  [ 不自动限电 ]  [ + ]. A stepper rather than a numeric field because the
    // panel has no numeric input widget in this mod's C++-only widget set, and because the value is
    // a small integer where "one more / one less" is the whole interaction.
    UHorizontalBox* PriorityRow = WidgetTree->ConstructWidget<UHorizontalBox>();

    PriorityDownButton = MakeButton(WidgetTree, WT_LOCTEXT("Btn_PriorityDown", "Priority -"), C_BTN, nullptr);
    PriorityDownButton->OnClicked.AddDynamic(this, &URemote_FactoryPanelWidget::OnPriorityDownClicked);
    if (UHorizontalBoxSlot* S = PriorityRow->AddChildToHorizontalBox(PriorityDownButton))
    {
        S->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
        S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }

    PriorityClearButton = MakeButton(WidgetTree, WT_LOCTEXT("Btn_NoAutoLimit", "Never auto-limit"), C_BTN, nullptr);
    PriorityClearButton->OnClicked.AddDynamic(this, &URemote_FactoryPanelWidget::OnPriorityClearClicked);
    if (UHorizontalBoxSlot* S = PriorityRow->AddChildToHorizontalBox(PriorityClearButton))
    {
        S->SetPadding(FMargin(2.f, 0.f));
        S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }

    PriorityUpButton = MakeButton(WidgetTree, WT_LOCTEXT("Btn_PriorityUp", "Priority +"), C_BTN, nullptr);
    PriorityUpButton->OnClicked.AddDynamic(this, &URemote_FactoryPanelWidget::OnPriorityUpClicked);
    if (UHorizontalBoxSlot* S = PriorityRow->AddChildToHorizontalBox(PriorityUpButton))
    {
        S->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
        S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }

    if (UVerticalBoxSlot* S = SwitchBox->AddChildToVerticalBox(PriorityRow))
    {
        S->SetPadding(FMargin(0.f, 2.f));
    }

    if (UVerticalBoxSlot* S = DetailBox->AddChildToVerticalBox(SwitchBox))
    {
        S->SetPadding(FMargin(0.f, 0.f));
    }

    // ---- Form 3 "D": shared-session banner --------------------------------------
    // Deliberately placed low and styled as a footnote: it is information about who else can see the
    // numbers, not an action. Green when this player is in control, amber when they are only watching.
    SharedSessionText = MakeText(WidgetTree, FText::GetEmpty(), 12, false, C_GREEN);
    SharedSessionText->SetVisibility(ESlateVisibility::Collapsed);
    if (UVerticalBoxSlot* S = DetailBox->AddChildToVerticalBox(SharedSessionText))
    {
        S->SetPadding(FMargin(0.f, 8.f, 0.f, 2.f));
    }

    // v0.2.55: the alien-energy collectible board was removed from this panel (user scope).
    // The data/scan code in the RCO is kept (源码留着), only the UI is gone. A trailing divider
    // keeps the detail pane visually closed.
    DetailBox->AddChildToVerticalBox(MakeDivider(WidgetTree));

    // ★v0.2.24 根换成 CanvasPanel（见 RebuildWidget 顶部注释）。
    return RootCanvas->TakeWidget();
}

void URemote_FactoryPanelWidget::NativeConstruct()
{
    Super::NativeConstruct();

    // ★v0.2.24 按真实视口把面板尺寸收敛到"视口 62% 宽 / 76% 高，上限 1280x820"，居中。
    if (RootCanvas && RootCanvas->GetSlots().Num() > 0)
    {
        if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(RootCanvas->GetSlots()[0]))
        {
            FVector2D Viewport(1920.f, 1080.f);
            if (UGameViewportClient* VP = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
            {
                VP->GetViewportSize(Viewport);
            }
            // ★v0.2.35 分辨率适配：窗口化后视口只有 ~1010px，旧公式(62%)给出 626px 面板，
            // 三列正文的最小宽度装不下、DetailBox 长文本整列溢出到面板右边界之外。
            // 改成"视口 90% 宽 / 85% 高，上限 1280x820"——全屏时依旧被上限钳到 1280（观感不变），
            // 小窗口时面板能占满窗口主体，配合 RootBorder 的 ClipToBounds 裁剪兜底。
            const float W = FMath::Min(Viewport.X * 0.90f, 1280.f);
            const float H = FMath::Min(Viewport.Y * 0.85f, 820.f);
            CanvasSlot->SetSize(FVector2D(W, H));
            CanvasSlot->SetAnchors(FAnchors(0.5f, 0.5f));
            CanvasSlot->SetAlignment(FVector2D(0.5f, 0.5f));
            CanvasSlot->SetPosition(FVector2D::ZeroVector);
        }
    }

    BindRCO();

    // Ask for the first scan immediately; the terminal opens showing live data.
    if (URemote_FactoryRCO* RCO = GetRCO())
    {
        // ★崩溃修复（2026-10-07 全代码自检）：GetOwningPlayer() **可能为 null** ——
        //   面板会在原版交互路径 / CreateWidget 的早期时点被构造，此时 OwningPlayer 未必已赋值。
        //   原写法 `GetOwningPlayer()->GetPawn()` 是裸解引用，一旦为空就是必崩。
        //   同文件 569/893/1049 行本来就是判空写法，这里统一成同一套。
        APlayerController* OwningPC = GetOwningPlayer();
        RCO->Server_ScanMachines((OwningPC && OwningPC->GetPawn())
                                     ? OwningPC->GetPawn()->GetActorLocation()
                                     : FVector::ZeroVector,
                                 /*MaxRangeMeters=*/0.f);

        // Form 3 "D": tell the server this player has a panel open. The reply to the very next scan
        // then carries the roster, which is how the banner learns whether anyone else is watching.
        // mInteractObject is the terminal this panel was spawned for, when there is one -- the
        // handheld form has no buildable behind it, so the announce simply carries null there and
        // every handheld user shares a single "roaming" session.
        RCO->Server_AnnounceViewer(/*bViewing=*/true, GetInteractBuildable());
    }
    RebuildDisplayCollectibles();   // ★0.2.69: load collectibles.json up front so the board shows the full map immediately
    RebuildCategoryList();
}

void URemote_FactoryPanelWidget::NativeDestruct()
{
    // Announce the departure BEFORE unbinding: the RCO lookup below goes through the controller, not
    // through the delegate list, so the order does not actually matter -- but doing it first keeps
    // the "closed the panel" fact from being lost if Super ever tears the controller down.
    if (URemote_FactoryRCO* RCO = GetRCO())
    {
        RCO->Server_AnnounceViewer(/*bViewing=*/false, GetInteractBuildable());
    }

    // ★v0.2.52 关闭输入卡死修复：任何拆毁路径（✕按钮 / 右键 / ESC / 看门狗）都走到这里，统一把
    //   输入模式还原为 FInputModeGameOnly（游戏默认：鼠标自由转视角 + 键盘全归游戏）。
    //   此前用 FInputModeGameAndUI（无焦点控件）会把鼠标锁在 UI 模式（卡鼠标）、键盘路由到无焦点
    //   Slate（卡键盘）。ClosePanel 已先行还原 GameOnly，这里同款兜底，避免任何拆毁路径漏还原卡死。
    if (APlayerController* PC = GetOwningPlayer())
    {
        FInputModeGameOnly InputMode;
        PC->SetInputMode(InputMode);
        PC->bShowMouseCursor = false;
    }

    UnbindRCO();
    Super::NativeDestruct();
}

AFGBuildable* URemote_FactoryPanelWidget::GetInteractBuildable() const
{
    // mInteractObject is the UPROPERTY the vanilla interaction path fills in when a panel is opened
    // by pressing E; the mod sets it from the server's Client_OpenInteractUI handler too. Reading it
    // by reflection here keeps this class from depending on the accessor's access specifier, which
    // differs between SDK snapshots.
    if (FObjectProperty* Prop = FindFProperty<FObjectProperty>(
            UFGInteractWidget::StaticClass(), TEXT("mInteractObject")))
    {
        if (UObject* Obj = Prop->GetObjectPropertyValue_InContainer(this))
        {
            return Cast<AFGBuildable>(Obj);
        }
    }
    return nullptr;
}

void URemote_FactoryPanelWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    WT_ZONE(TEXT("Panel.NativeTick"));   // 每帧；超预算会报「fine zone ... over budget」
    Super::NativeTick(MyGeometry, InDeltaTime);

    // ★用户诉求：传送成功后自动关面板。在 RCO 委托回调里同步 RemoveFromParent 会在广播中改委托列表，
    // 故延迟到这里（下一帧）执行，关面板与开面板的收尾走同一出口（PanelCloseRequested -> ClosePanel）。
    if (bPendingAutoClose)
    {
        bPendingAutoClose = false;
        if (PanelCloseRequested.IsBound())
        {
            PanelCloseRequested.Broadcast();
        }
        else
        {
            RemoveFromParent();
        }
        return;
    }

    // ★0.2.130 修复「ESC 关不掉面板」焦点被冲掉：Satisfactory 会反复重设输入模式/焦点，开面板时给的
    // 焦点也会被冲掉，导致 ESC 又落回 viewport。这里每帧检测——只要焦点不在本面板（也不在任一子控件）
    // 就重设 FInputModeUIOnly + 焦点到本面板。已聚焦或焦点在子控件（如按钮）时不抢，避免影响点击。
    if (IsInViewport() && GetOwningPlayer())
    {
        if (!HasKeyboardFocus() && !HasFocusedDescendants())
        {
            FInputModeUIOnly FocusMode;
            FocusMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
            if (TSharedPtr<SWidget> SW = TakeWidget())
            {
                FocusMode.SetWidgetToFocus(SW.ToSharedRef());
            }
            GetOwningPlayer()->SetInputMode(FocusMode);
            GetOwningPlayer()->bShowMouseCursor = true;
        }
    }

    // Periodic rescan so the overview stays live. v0.2.4: dropped from 5 s to 15 s -- the scan walks
    // every buildable in the world plus two full-map actor iterators, and a 5 s cadence was a visible
    // periodic hitch on large factories. 15 s is still "live" for an overview board.
    RescanAccum += InDeltaTime;
    if (RescanAccum >= 15.f)
    {
        RescanAccum = 0.f;
        if (URemote_FactoryRCO* RCO = GetRCO())
        {
            const FVector Origin = GetOwningPlayer() && GetOwningPlayer()->GetPawn()
                ? GetOwningPlayer()->GetPawn()->GetActorLocation()
                : FVector::ZeroVector;
            RCO->Server_ScanMachines(Origin, /*MaxRangeMeters=*/0.f);
        }
    }

    // ★0.2.70: live-update the collectible distances shown in the list as the player moves, without
    // a full widget rebuild. 0.1 s cadence is smooth enough for a readout and keeps Slate cheap
    // (only the drawn rows -- at most a few hundred -- are touched).
    DistRefreshAccum += InDeltaTime;
    if (DistRefreshAccum >= 0.1f)
    {
        DistRefreshAccum = 0.f;
        UpdateCollectibleDistances();
    }
}

void URemote_FactoryPanelWidget::BindRCO()
{
    if (URemote_FactoryRCO* RCO = GetRCO())
    {
        RCO->OnMachineListReceived.AddUObject(this, &URemote_FactoryPanelWidget::HandleMachineListReceived);
        RCO->OnInteractUIRequested.AddUObject(this, &URemote_FactoryPanelWidget::HandleInteractUIRequested);
        RCO->OnPullResult.AddUObject(this, &URemote_FactoryPanelWidget::HandlePullResult);
        RCO->OnTeleportResult.AddUObject(this, &URemote_FactoryPanelWidget::HandleTeleportResult);
        RCO->OnSwitchResult.AddUObject(this, &URemote_FactoryPanelWidget::HandleSwitchResult);
    }
}

void URemote_FactoryPanelWidget::UnbindRCO()
{
    if (URemote_FactoryRCO* RCO = GetRCO())
    {
        RCO->OnMachineListReceived.RemoveAll(this);
        RCO->OnInteractUIRequested.RemoveAll(this);
        RCO->OnPullResult.RemoveAll(this);
        RCO->OnTeleportResult.RemoveAll(this);
        RCO->OnSwitchResult.RemoveAll(this);
    }
}

// v0.2.4 PERF: cheap stable signature of the collectible set, so the (potentially hundreds-of-
// widgets) collectible board is only rebuilt when the set actually changed between scans.
static uint32 WTCollectibleSig(const TArray<FWT_CollectibleEntry>& Arr)
{
    uint32 H = 2166136261u;
    for (const FWT_CollectibleEntry& E : Arr)
    {
        const FString D = E.DisplayName.ToString();
        for (const TCHAR* P = *D; *P; ++P) { H ^= static_cast<uint8>(*P); H *= 16777619u; }
        H ^= E.bCollected ? 0x9e3779b9u : 0u; H *= 16777619u;
        H ^= static_cast<uint32>(E.Kind); H *= 16777619u;
        const int32 QX = static_cast<int32>(E.Location.X * 0.1f);
        const int32 QY = static_cast<int32>(E.Location.Y * 0.1f);
        const int32 QZ = static_cast<int32>(E.Location.Z * 0.1f);
        H ^= static_cast<uint32>(QX ^ QY ^ QZ); H *= 16777619u;
    }
    return H;
}

// vX: hard ceiling on how many machine *rows* we actually draw as Slate widgets. With ★v0.2.76
// type-folding the normal render load is (one header per machine type) + (expanded groups'
// nearest rows), so this is now only a runaway safety net, not the primary cap.
static constexpr int32 WT_MaxRenderedRows = 20000;

// ★v0.2.76: per-type cap on individually drawn rows when a group is expanded — the nearest N
// are shown (distance-sorted), then a "…另有 N 台" hint. Keeps one mega-type (e.g. 20k smelters)
// from freezing Slate while the group header still reports the full ×N count.
static constexpr int32 WT_MaxRowsPerGroup = 500;

// vX PERF: stable signature of the machine-list *content*. Like WTCollectibleSig, this lets the
// (potentially 10k+ row) machine board be rebuilt only when the set actually changed, instead of on
// every 15s periodic rescan. Distances are quantized so tiny player movement does not thrash it, but
// building/dismantling a machine or a status flip still changes the hash.
static uint32 WTMachineSig(const FWT_MachineListReply& Reply)
{
    uint32 H = 2166136261u;
    H ^= static_cast<uint32>(Reply.Entries.Num()); H *= 16777619u;
    for (int32 i = 0; i < Reply.CategoryCounts.Num(); ++i)
    {
        H ^= static_cast<uint32>(Reply.CategoryCounts[i]); H *= 16777619u;
    }
    for (const FWT_MachineEntry& E : Reply.Entries)
    {
        const FString D = E.DisplayName.ToString();
        for (const TCHAR* P = *D; *P; ++P) { H ^= static_cast<uint8>(*P); H *= 16777619u; }
        H ^= static_cast<uint32>(E.Category); H *= 16777619u;
        const int32 Q = static_cast<int32>(E.DistanceMeters * 0.1f); // 0.1 m quantisation
        H ^= static_cast<uint32>(Q); H *= 16777619u;
        const FString S = E.StatusLine.ToString();
        for (const TCHAR* P = *S; *P; ++P) { H ^= static_cast<uint8>(*P); H *= 16777619u; }
    }
    return H;
}

void URemote_FactoryPanelWidget::HandleMachineListReceived(const FWT_MachineListReply& Reply)
{
    // 一次扫描的落地：状态条 + 电量读数 + 分类/机器/收集品三块列表。
    // 这是「开面板卡一下 / 定时刷新卡一下」最可能的那一环。
    WT_ZONE_COARSE(TEXT("Panel.ApplyScanReply"));
    CurrentList = Reply;
    bAwaitingFirstScan = false;
    RebuildDisplayCollectibles();   // ★0.2.69: merge collectibles.json (full map) + live picked state

    if (StatusText)
    {
        const int32 Total = Reply.Entries.Num();
        if (Reply.bTruncated)
        {
            // Factory has more machines than the list cap (WT_MaxListEntries on the server); the
            // nearest ones are shown. Power/category overview is still exact for the whole factory.
            StatusText->SetText(FText::Format(
                WT_LOCTEXT("Status_MachineCountTruncated", "{0} machines listed (factory exceeds list cap - nearest shown)"),
                FText::AsNumber(Total)));
        }
        else if (Total > WT_MaxRenderedRows)
        {
            // Everything is in the list, but we only draw the nearest WT_MaxRenderedRows widgets.
            StatusText->SetText(FText::Format(
                WT_LOCTEXT("Status_MachineCountRendered", "{0} machines (list draws nearest {1})"),
                FText::AsNumber(Total), FText::AsNumber(WT_MaxRenderedRows)));
        }
        else
        {
            StatusText->SetText(FText::Format(WT_LOCTEXT("Status_MachineCount", "{0} machines"),
                                             FText::AsNumber(Total)));
        }
    }

    RefreshPowerReadout();
    RefreshSharedSessionBanner();
    RebuildCategoryList();

    // vX PERF: only rebuild the (potentially 10k+) machine board when its content actually changed,
    // not on every 15s periodic rescan -- a rebuild walks every row and creates a widget per row.
    const uint32 NewMachineSig = WTMachineSig(Reply);
    if (NewMachineSig != CachedMachineSig)
    {
        CachedMachineSig = NewMachineSig;
        RebuildMachineList();
    }

    // v0.2.55: the alien-energy collectible board was removed from this panel (user scope), so its
    // rebuild is skipped here. WTCollectibleSig() and RebuildCollectibleList() are kept in source
    // (源码留着) but no longer driven from the scan callback.
}

void URemote_FactoryPanelWidget::RefreshSharedSessionBanner()
{
    if (!SharedSessionText)
    {
        return;
    }

    const FWT_MachineListReply& R = CurrentList;

    // Hidden when nobody else is watching: a "sharing is on" banner in single-player is noise, and
    // the mod cannot force a test audience into existence.
    if (!R.bSharedSession)
    {
        SharedSessionText->SetVisibility(ESlateVisibility::Collapsed);
        return;
    }

    SharedSessionText->SetVisibility(ESlateVisibility::Visible);

    // The audience count is viewers MINUS this player: "N others watching" is about the others, so
    // including yourself makes the number read as one too high.
    const int32 Others = FMath::Max(0, R.SessionViewers.Num() - 1);

    if (R.bIsSessionOwner)
    {
        SharedSessionText->SetColorAndOpacity(FSlateColor(C_GREEN));
        SharedSessionText->SetText(FText::Format(
            WT_LOCTEXT("Shared_YouAreOwner", "Sharing · you are the host ({0} others watching)"),
            FText::AsNumber(Others)));
    }
    else
    {
        // Amber, and it names the owner: in watch mode the buttons are disabled, and "why can't I
        // click this" is the only question the banner has to answer.
        SharedSessionText->SetColorAndOpacity(FSlateColor(C_AMBER));
        SharedSessionText->SetText(FText::Format(
            WT_LOCTEXT("Shared_ViewerMode", "Sharing · host: {0} (view-only, cannot operate)"),
            R.SessionOwnerName.IsEmpty() ? WT_LOCTEXT("Shared_OtherPlayer", "another player")
                                         : FText::FromString(R.SessionOwnerName)));
    }
}

void URemote_FactoryPanelWidget::RefreshPowerReadout()
{
    WT_ZONE(TEXT("Panel.RefreshPowerReadout"));
    if (!PowerText)
    {
        return;
    }

    const FWT_MachineListReply& R = CurrentList;

    // Two lines rather than one: the power figures and the stored-energy figures answer different
    // questions, and squeezing both onto a single line pushed the labels off narrow layouts.
    PowerText->SetText(FText::Format(
        WT_LOCTEXT("Power_Readout",
            "Gen {0} / {1} MW      Use {2} / {3} MW\nStored {4} / {5} MWh      Grids {6}"),
        FText::AsNumber(FMath::RoundToInt(R.PowerProducedMW)),
        FText::AsNumber(FMath::RoundToInt(R.PowerCapacityMW)),
        FText::AsNumber(FMath::RoundToInt(R.PowerConsumedMW)),
        FText::AsNumber(FMath::RoundToInt(R.PowerMaxConsumptionMW)),
        FText::AsNumber(FMath::RoundToInt(R.BatteryStoredMWh)),
        FText::AsNumber(FMath::RoundToInt(R.BatteryCapacityMWh)),
        FText::AsNumber(R.CircuitCount > 0 ? R.CircuitCount : 1)));

    if (PowerBar)
    {
        // Fill = current draw against installed generation capacity. Over 1.0 means the grid is
        // asking for more than it can make, so clamp and recolour to make that obvious.
        const float Percent = R.PowerCapacityMW > KINDA_SMALL_NUMBER
            ? R.PowerConsumedMW / R.PowerCapacityMW
            : 0.f;
        PowerBar->SetPercent(FMath::Clamp(Percent, 0.f, 1.f));

        // Green while there is headroom, amber when close to the limit, red when over budget.
        const FLinearColor BarColor = Percent > 1.0f ? C_RED
                                   : Percent > 0.85f ? C_AMBER
                                   : C_GREEN;
        PowerBar->SetFillColorAndOpacity(BarColor);
    }
}

namespace
{
    /**
     * Resolve the interact widget class a buildable wants to open on E.
     *
     * AFGBuildable::GetInteractWidgetClass() would be the one-liner, but it is declared in a
     * `protected:` section of the shipped header, so we cannot call it from outside the hierarchy.
     * We read the backing property instead -- mInteractWidgetSoftClass, a TSoftClassPtr the game
     * loads synchronously at the very same point. Reflection keeps this working even though both the
     * accessor and the member are off-limits to us at compile time.
     */
    static UClass* WT_ResolveInteractWidgetClass(AFGBuildable* Target)
    {
        if (!IsValid(Target))
        {
            return nullptr;
        }

        if (FSoftClassProperty* Prop = FindFProperty<FSoftClassProperty>(
                AFGBuildable::StaticClass(), TEXT("mInteractWidgetSoftClass")))
        {
            // Go through the generic container API rather than naming the concrete property type:
            // in UE5.6 the backing storage is FSoftObjectPtr but FSoftClassProperty's template
            // base is TObjectPtr-flavoured, and ContainerPtrToValuePtr<T> type-checks against it.
            // void* + a typed reinterpret_cast sidesteps that mismatch entirely.
            const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Target);
            if (ValuePtr)
            {
                const FSoftObjectPtr& SoftRef = *static_cast<const FSoftObjectPtr*>(ValuePtr);
                // LoadSynchronous is what the accessor does; the widget class is tiny and local.
                if (UClass* Loaded = Cast<UClass>(SoftRef.LoadSynchronous()))
                {
                    return Loaded;
                }
            }
        }

        return nullptr;
    }
}

void URemote_FactoryPanelWidget::HandleInteractUIRequested(AFGBuildable* Target)
{
    // Note: guard on the *class*, then reuse it, so we never load the soft class twice.
    UClass* WidgetClass = WT_ResolveInteractWidgetClass(Target);
    if (!WidgetClass)
    {
        WT_WARNF(TEXT("'%s' has no interact widget class; nothing to open."),
                 Target ? *Target->GetName() : TEXT("<null>"));
        return;
    }

    APlayerController* PC = GetOwningPlayer();
    if (!PC)
    {
        return;
    }

    // ★0.2.71 修复「终端页打开机器面板：布局不对 + 关不掉 + 卡死/卡鼠标」：
    // 旧实现 CreateWidget + AddToViewport(10) 完全绕过游戏官方 UI 管线——
    //   ① 面板没进游戏 UI 层级 → 布局与走近机器按 E 打开的不一致；
    //   ② 没注册进游戏 UI 栈 → ESC/关闭按钮路由失效（关不掉）；
    //   ③ 机器面板 NativeConstruct 把输入切成 GameAndUI 后，本面板 RemoveFromParent 触发的
    //      NativeDestruct 又立刻覆盖成 GameOnly+隐藏鼠标 → 输入态永久错乱（卡死/卡鼠标）。
    // 改走官方唯一入口 AFGHUD::OpenInteractUI（FGHUD.h:167，BlueprintImplementableEvent，
    // 官方注释："All widgets that inherits from UFGInteractWidget can be opened by calling this"）：
    // 入栈、输入模式、关闭路由、mInteractObject 绑定全部交给游戏自己做，与按 E 交互同一条管线。
    AFGHUD* HUD = Cast<AFGHUD>(PC->GetHUD());
    if (!HUD)
    {
        WT_WARNF(TEXT("No AFGHUD on the player controller; cannot open machine UI via the official pipeline."));
        return;
    }

    // 顺序硬约束：先关终端面板（NativeDestruct 把输入还原为 GameOnly——与按 E 时一致的干净状态），
    // 再调 OpenInteractUI；反过来机器面板设好的输入态会被我们的 NativeDestruct 覆盖，症状复发。
    // ★0.2.74 修复「卡鼠标」（0.2.73 日志实锤）：旧实现裸调 RemoveFromParent，装备侧 PanelWidget
    //   指针没清——每次交接都留一个野指针，下次右键走 stale 分支重开新面板，旧面板的输入态
    //   UIOnly 与机器面板的 GameAndUI 互相打架（日志：16:53:19 开→:23 交接→:26 又"opened"）。
    //   改为先广播 PanelCloseRequested（装备侧 ClosePanel 完整收尾：摘视口+清指针+停看门狗+
    //   还原 GameOnly），再开机器 UI，最后补 GameAndUI——三步顺序与 0.2.72 注释的硬约束一致。
    if (PanelCloseRequested.IsBound())
    {
        PanelCloseRequested.Broadcast();
    }
    else
    {
        RemoveFromParent();   // 无绑定方（理论不可达）：退回旧行为保底
    }
    // ★0.2.131 修「打开建筑面板依旧卡鼠标」：0.2.72 的裸 GameAndUI 是在 OpenInteractUI 之后补的，
    // 会把原版机器面板在 construct 里自己 SetInputMode()（GameAndUI+焦点控件+光标）的成果踩掉
    // （UFGInteractWidget::SetInputMode 是公开方法，FGInteractWidget.h:44；原版机器 UI 就靠它接管输入）
    // ——焦点丢失 + 锁鼠回落默认值，光标僵住/点不动。改成与原版按 E 交互相同的顺序：
    // 先 GameAndUI+显光标做保底，再 OpenInteractUI，让机器面板自己的 SetInputMode() 最后生效。
    {
        FInputModeGameAndUI InputMode;
        InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        // ★0.2.132（大肥鱼提示 + 引擎头核实 PlayerController.h:192）：GameAndUI 默认
        // bHideCursorDuringCapture=true——按键捕获期间自己把光标藏起来，正是「开了又被压回去」的
        // 来源之一。显式关掉。
        InputMode.SetHideCursorDuringCapture(false);
        PC->SetInputMode(InputMode);
        PC->bShowMouseCursor = true;
    }
    HUD->OpenInteractUI(WidgetClass, Target);

    // ★0.2.132 可重入补光标（大肥鱼：每次 SetInputMode 都会重写一次 bShowMouseCursor，只补一次
    // 会被后续任意切换冲掉）：OpenInteractUI 之后逐帧查游戏 UI 交互栈——
    // AFGHUD::GetGameUI()（FGHUD.h:193）-> UFGGameUI::HasActiveInteractWidget()（FGGameUI.h:88，
    // 即 mInteractWidgetStack.Num()>0）——栈上有交互 widget 就把 bShowMouseCursor 拉回 true。
    // 自停条件：见过激活后栈空（UI 已关，绝不误亮游戏态光标）/ 10s 绝对上限 / 5s grace 仍未入栈。
    // 单飞：护栏 ticker 活着时不重复挂（连续多次交接只保留一条）。
    {
        static bool GCursorGuardActive = false;
        if (!GCursorGuardActive)
        {
            GCursorGuardActive = true;
            struct FCursorGuardCtx { bool bSeenActive = false; uint32 Ticks = 0; };
            TSharedPtr<FCursorGuardCtx> Ctx = MakeShared<FCursorGuardCtx>();
            TWeakObjectPtr<APlayerController> WPC(PC);
            TWeakObjectPtr<AFGHUD> WHUD(HUD);
            FTSTicker::GetCoreTicker().AddTicker(
                FTickerDelegate::CreateLambda([Ctx, WPC, WHUD](float) -> bool
                {
                    APlayerController* PCPtr = WPC.Get();
                    AFGHUD* HUDPtr = WHUD.Get();
                    if (!PCPtr || !HUDPtr)
                    {
                        GCursorGuardActive = false;
                        return false;   // PC/HUD 已失效，停
                    }
                    UFGGameUI* GameUI = HUDPtr->GetGameUI();
                    const bool bActive = GameUI && GameUI->HasActiveInteractWidget();
                    if (bActive)
                    {
                        Ctx->bSeenActive = true;
                        if (!PCPtr->bShowMouseCursor)
                        {
                            PCPtr->bShowMouseCursor = true;   // 只补光标，不动输入模式
                        }
                    }
                    ++Ctx->Ticks;
                    const bool bStop = (Ctx->bSeenActive && !bActive) || Ctx->Ticks > 600 ||
                                       (!Ctx->bSeenActive && Ctx->Ticks > 300);
                    if (bStop)
                    {
                        GCursorGuardActive = false;
                    }
                    return !bStop;
                }), 0.f);
        }
    }

    // ★0.2.131 交接诊断痕迹：原成功路径零日志，「卡鼠标」复发时无从定位。每次交接打一条。
    WT_WARNF(TEXT("[equip] machine UI handoff: target=%s class=%s (panel closed -> GameAndUI -> OpenInteractUI)"),
             *GetNameSafe(Target), *GetNameSafe(WidgetClass));
}

void URemote_FactoryPanelWidget::HandlePullResult(AFGBuildable* Target, int32 ItemsMoved, bool bSuccess)
{
    if (StatusText)
    {
        StatusText->SetText(bSuccess
            ? FText::Format(WT_LOCTEXT("Status_PulledItems", "Took {0} items"),
                            FText::AsNumber(ItemsMoved))
            : WT_LOCTEXT("Status_NothingToPull", "There is no output to take"));
    }
    RebuildMachineList();
    RebuildDetailPane();
}

void URemote_FactoryPanelWidget::OnRefreshClicked()
{
    if (URemote_FactoryRCO* RCO = GetRCO())
    {
        const FVector Origin = GetOwningPlayer() && GetOwningPlayer()->GetPawn()
            ? GetOwningPlayer()->GetPawn()->GetActorLocation()
            : FVector::ZeroVector;
        RCO->Server_ScanMachines(Origin, /*MaxRangeMeters=*/0.f);
    }
}

void URemote_FactoryPanelWidget::OnOpenMachineClicked()
{
    WT_ZONE_COARSE(TEXT("Panel.OpenMachineUI"));
    const FWT_MachineEntry* Entry = CurrentList.Entries.IsValidIndex(SelectedEntry)
        ? &CurrentList.Entries[SelectedEntry]
        : nullptr;
    if (!Entry || !IsValid(Entry->Target))
    {
        return;
    }

    if (URemote_FactoryRCO* RCO = GetRCO())
    {
        // ★0.2.153m Target 已放宽为 AActor（运输工具进同一张列表）⇒ 只收建筑的 RPC 先收敛类型。
        if (AFGBuildable* Bld = Cast<AFGBuildable>(Entry->Target))
        {
            RCO->Server_RequestOpenUI(Bld);
        }
    }
}

void URemote_FactoryPanelWidget::OnPullOutputClicked()
{
    WT_ZONE_COARSE(TEXT("Panel.PullOutput"));
    const FWT_MachineEntry* Entry = CurrentList.Entries.IsValidIndex(SelectedEntry)
        ? &CurrentList.Entries[SelectedEntry]
        : nullptr;
    if (!Entry || !IsValid(Entry->Target))
    {
        return;
    }

    if (URemote_FactoryRCO* RCO = GetRCO())
    {
        if (AFGBuildable* Bld = Cast<AFGBuildable>(Entry->Target))
        {
            RCO->Server_PullOutput(Bld);
        }
    }
}

void URemote_FactoryPanelWidget::OnTeleportToMachineClicked()
{
    WT_ZONE_COARSE(TEXT("Panel.TeleportToMachine"));
    const FWT_MachineEntry* Entry = CurrentList.Entries.IsValidIndex(SelectedEntry)
        ? &CurrentList.Entries[SelectedEntry]
        : nullptr;
    if (!Entry)
    {
        return;
    }

    if (URemote_FactoryRCO* RCO = GetRCO())
    {
        if (IsValid(Entry->Target))
        {
            RCO->Server_TeleportTo(Entry->Target);
        }
        // ★0.2.153e 坐标直传：远端机器客户端未复制（Target 为 null）→ 按坐标传，不受 relevancy 限制
        else if (!Entry->Location.IsNearlyZero())
        {
            RCO->Server_TeleportToCoord(Entry->Location, Entry->BaseRadius);
        }
    }
}

void URemote_FactoryPanelWidget::OnTeleportToCollectibleClicked()
{
    WT_ZONE_COARSE(TEXT("Panel.TeleportToCollectible"));
    // ★0.2.69: read from the file-driven display list (covers unloaded cells with no live actor).
    const FWT_CollectibleEntry* Entry = DisplayCollectibles.IsValidIndex(SelectedCollectible)
        ? &DisplayCollectibles[SelectedCollectible]
        : nullptr;
    if (!Entry)
    {
        return;
    }

    if (URemote_FactoryRCO* RCO = GetRCO())
    {
        if (IsValid(Entry->Actor))
        {
            RCO->Server_TeleportTo(Entry->Actor);
        }
        else
        {
            RCO->Server_TeleportToLocation(Entry->Location);
        }
    }
}

// ----------------------------------------------------------------------------
// Form 3 "A": power switch controls
// ----------------------------------------------------------------------------

/** The currently selected machine row, or null. Shared by the four switch handlers. */
static const FWT_MachineEntry* WT_SelectedEntry(const FWT_MachineListReply& List, int32 Index)
{
    return List.Entries.IsValidIndex(Index) ? &List.Entries[Index] : nullptr;
}

void URemote_FactoryPanelWidget::OnToggleSwitchClicked()
{
    const FWT_MachineEntry* Entry = WT_SelectedEntry(CurrentList, SelectedEntry);
    if (!Entry || !IsValid(Entry->Target))
    {
        return;
    }

    if (URemote_FactoryRCO* RCO = GetRCO())
    {
        // Send the OPPOSITE of what the row says it is. This is safe against a stale row for one
        // round-trip: the server re-reads the switch and writes that value, so a double-click from a
        // laggy client converges rather than toggling twice into the same state.
        if (AFGBuildable* Bld = Cast<AFGBuildable>(Entry->Target))
        {
            RCO->Server_SetSwitchOn(Bld, !Entry->bSwitchOn);
        }
    }
}

void URemote_FactoryPanelWidget::OnPriorityUpClicked()
{
    const FWT_MachineEntry* Entry = WT_SelectedEntry(CurrentList, SelectedEntry);
    if (!Entry || !IsValid(Entry->Target) || Entry->SwitchPriority < 0)
    {
        return;
    }

    if (URemote_FactoryRCO* RCO = GetRCO())
    {
        // Vanilla clamps this to a small range; stepping past the top simply reports the clamped
        // value back through the result message rather than being rejected.
        if (AFGBuildable* Bld = Cast<AFGBuildable>(Entry->Target))
        {
            RCO->Server_SetSwitchPriority(Bld, FMath::Min(Entry->SwitchPriority + 1, 9));
        }
    }
}

void URemote_FactoryPanelWidget::OnPriorityDownClicked()
{
    const FWT_MachineEntry* Entry = WT_SelectedEntry(CurrentList, SelectedEntry);
    if (!Entry || !IsValid(Entry->Target) || Entry->SwitchPriority < 0)
    {
        return;
    }

    if (URemote_FactoryRCO* RCO = GetRCO())
    {
        if (AFGBuildable* Bld = Cast<AFGBuildable>(Entry->Target))
        {
            RCO->Server_SetSwitchPriority(Bld, FMath::Max(Entry->SwitchPriority - 1, 0));
        }
    }
}

void URemote_FactoryPanelWidget::OnPriorityClearClicked()
{
    const FWT_MachineEntry* Entry = WT_SelectedEntry(CurrentList, SelectedEntry);
    if (!Entry || !IsValid(Entry->Target) || Entry->SwitchPriority < 0)
    {
        return;
    }

    if (URemote_FactoryRCO* RCO = GetRCO())
    {
        if (AFGBuildable* Bld = Cast<AFGBuildable>(Entry->Target))
        {
            RCO->Server_SetSwitchPriority(Bld, 0);
        }
    }
}

void URemote_FactoryPanelWidget::HandleSwitchResult(AFGBuildable* Target, bool bSuccess, const FText& Message)
{
    if (StatusText)
    {
        StatusText->SetText(Message);
    }

    // The row data is stale the moment the server accepts a write, and waiting for the next 5 s
    // rescan would make the button feel broken. Ask for a fresh scan immediately so the toggle
    // reflects reality within a round-trip.
    if (bSuccess)
    {
        if (URemote_FactoryRCO* RCO = GetRCO())
        {
            const FVector Origin = GetOwningPlayer() && GetOwningPlayer()->GetPawn()
                ? GetOwningPlayer()->GetPawn()->GetActorLocation()
                : FVector::ZeroVector;
            RCO->Server_ScanMachines(Origin, /*MaxRangeMeters=*/0.f);
        }
    }
}

void URemote_FactoryPanelWidget::SelectCollectible(int32 CollectibleIndex)
{
    SelectedCollectible = CollectibleIndex;
    RebuildMachineList();      // ★0.2.62: refresh the (collectible) middle list highlight
    RebuildDetailPane();
    RebuildCollectibleList();  // 右侧板已退役（null 保护，无害）
}

// ★v0.2.15 ✕ 关闭按钮：优先广播给持有方（手持装备会走 ClosePanel 完整收尾）；
// 没有监听者时（例如其它打开路径）兜底自移除，UI 永远有出口。
void URemote_FactoryPanelWidget::OnCloseClicked()
{
    if (PanelCloseRequested.IsBound())
    {
        PanelCloseRequested.Broadcast();
    }
    else
    {
        RemoveFromParent();
    }
}

UButton* URemote_FactoryPanelWidget::AddListButton(const FText& Label, bool bSelected,
                                                     const FWT_ButtonInfo& Info, bool bEnabled,
                                                     UTextBlock** OutLabel)
{
    WT_ZONE(TEXT("Panel.AddListButton"));   // 每行一个；上千行时这里会累积成可见卡顿
    UTextBlock* LabelText = nullptr;
    UButton* Button = MakeButton(WidgetTree, Label, bSelected ? C_BTN_SEL : C_BTN,
                                 &LabelText, ETextJustify::Left);
    if (LabelText)
    {
        LabelText->SetColorAndOpacity(FSlateColor(bSelected ? C_ON_SEL : C_VALUE));
    }
    if (OutLabel)
    {
        *OutLabel = LabelText;
    }
    Button->SetIsEnabled(bEnabled);

    // UMG's OnClicked is a *dynamic* multicast delegate (FOnButtonClickedEvent), and dynamic
    // delegates have no AddLambda -- they can only be bound to UFUNCTIONs. Our rows all share one
    // handler signature, so the only way to give each row its own payload is to reach past UMG to
    // the underlying Slate SButton.
    //
    // Note the member SButton::OnClicked is private (it is set through FArguments during Construct),
    // so we do NOT touch it directly: SButton exposes the public setter SetOnClicked(FOnClicked).
    // FOnClicked is a plain, non-dynamic delegate taking no args and returning FReply, so it
    // accepts a lambda.
    //
    // Force the Slate widget to exist first: GetCachedWidget() returns null until the UMG widget has
    // been constructed, which happens lazily inside TakeWidget().
    const FWT_ButtonInfo InfoCopy = Info;
    Button->TakeWidget();
    if (TSharedPtr<SWidget> Cached = Button->GetCachedWidget())
    {
        TSharedPtr<SButton> SlateButton = StaticCastSharedPtr<SButton>(Cached);
        if (SlateButton.IsValid())
        {
            // `this` is a raw UObject pointer: safe here because the whole tree -- including this
            // closure -- is torn down with the widget. The payload holds only PODs.
            SlateButton->SetOnClicked(FOnClicked::CreateLambda([this, InfoCopy]() -> FReply
            {
            switch (static_cast<EWT_ButtonKind>(InfoCopy.Kind))
            {
                case EWT_ButtonKind::Category:        SelectCategory(InfoCopy.Index);        break;
                case EWT_ButtonKind::CollectibleRow:  SelectCollectible(InfoCopy.Index);     break;
                case EWT_ButtonKind::TopToggle:       ToggleTopCategory(InfoCopy.Index);     break;
                case EWT_ButtonKind::CollectibleKind: SelectCollectibleKind(InfoCopy.Index); break;
                default:                              SelectEntry(InfoCopy.Index);           break;
            }
                return FReply::Handled();
            }));
        }
        else
        {
            WT_WARNF(TEXT("AddListButton: cached widget is not an SButton; this row will be inert."));
        }
    }

    return Button;
}

void URemote_FactoryPanelWidget::RebuildCategoryList()
{
    WT_ZONE(TEXT("Panel.RebuildCategories"));
    if (!CategoryBox)
    {
        return;
    }
    CategoryBox->ClearChildren();

    // ★0.2.62: 可折叠顶层分组（原 11 个平铺类目按钮）。
    // ★0.2.153p 用户定案：**「容器」顶层组并入「建筑」**（液体容器/固体容器作为建筑的两个子类，
    // 插在采矿设备与发电机之间）⇒ 顶层由 3 组变 2 组（建筑 / 收集品）。
    static const FText TopLabels[2] = {
        WT_LOCTEXT("Top_Building", "建筑"),
        WT_LOCTEXT("Top_Alien",    "收集品"),
    };
    // Machine categories under the 建筑 group (DropPod is filed under 收集品 as a collectible,
    // per the user's taxonomy, so it is NOT listed here). From 0.2.84 the 收集品 group also holds
    // the foragable flora (Beryl Nut / Pale Berry / Bacon Agaric) alongside the alien-energy kinds.
    // ★0.2.146: Modded joins the 建筑 group — every loaded ficsit mod's uncategorised buildings.
    // ★0.2.146: +制作台/装备工坊/AWESOME商店（此前落 Other 被丢）。
    // ★0.2.153p 容器（液体/固体）并入建筑，位置在「采矿设备」与「发电机」之间（用户定案）。
    // ★0.2.153r 制作台 / 装备工坊 / 分子分析机 / 商店 / 回收站 / 太空电梯 已并入【枢纽】
    // （扫描侧统一归 Hub 桶，用户定案）⇒ 这里同步移除这六项，否则会显示一排恒为 0 的空类目。
    static const EWT_MachineCategory BuildingCats[] = {
        EWT_MachineCategory::Producer, EWT_MachineCategory::Miner,
        EWT_MachineCategory::LiquidContainer, EWT_MachineCategory::SolidContainer,
        EWT_MachineCategory::Generator,
        EWT_MachineCategory::PowerSwitch, EWT_MachineCategory::Hub,
        EWT_MachineCategory::Vehicle, EWT_MachineCategory::Portal, EWT_MachineCategory::Modded,
    };

    for (int32 t = 0; t < 2; ++t)
    {
        const int32 Count = TopCount(t);
        const FText Arrow = bTopExpanded[t] ? WT_LOCTEXT("Arrow_Down", "▼") : WT_LOCTEXT("Arrow_Right", "▶");
        const FText Label = FText::Format(
            WT_LOCTEXT("TopLabel", "{0}  {1}  ({2})"), Arrow, TopLabels[t], FText::AsNumber(Count));

        FWT_ButtonInfo TopInfo;
        TopInfo.Kind = static_cast<uint8>(EWT_ButtonKind::TopToggle);
        TopInfo.Index = t;
        UButton* TopBtn = AddListButton(Label, /*bSelected=*/false, TopInfo, /*bEnabled=*/true);
        if (UVerticalBoxSlot* S = CategoryBox->AddChildToVerticalBox(TopBtn))
        {
            S->SetPadding(FMargin(0.f, 2.f, 6.f, 2.f));
        }

        if (!bTopExpanded[t])
        {
            continue;
        }

        if (t == 0) // 建筑
        {
            for (EWT_MachineCategory c : BuildingCats)
            {
                const int32 ci = static_cast<int32>(c);
                const int32 cCount = CurrentList.CategoryCounts.IsValidIndex(ci) ? CurrentList.CategoryCounts[ci] : 0;
                const bool bSel = (ci == SelectedCategory && SelectedCollectibleKind < 0);
                const FText sub = cCount > 0
                    ? FText::Format(WT_LOCTEXT("Sub_WithCount", "    {0}  ({1})"), CategoryLabel(ci), FText::AsNumber(cCount))
                    : FText::Format(WT_LOCTEXT("Sub_Plain", "    {0}"), CategoryLabel(ci));
                FWT_ButtonInfo Info;
                Info.Kind = static_cast<uint8>(EWT_ButtonKind::Category);
                Info.Index = ci;
                UButton* B = AddListButton(sub, bSel, Info, /*bEnabled=*/cCount > 0 || bSel);
                if (UVerticalBoxSlot* S = CategoryBox->AddChildToVerticalBox(B))
                {
                    S->SetPadding(FMargin(0.f, 1.f, 6.f, 1.f));
                }
            }
        }
        else // t == 1 收集品（外星能量 + 可采集果实）。★0.2.153p 容器已并入建筑，顶层只剩两组
        {
            for (int32 k = 0; k < static_cast<int32>(EWT_CollectibleKind::Count); ++k)
            {
                // ★0.2.67b: 顶行括号显示实时「可拾取数量」= 未拾取数（来自扫描）。
                // wiki 总数仅用于让该行保持可点击：流式加载尚未装入含该类的格子时扫描会少算，
                // 这时不应把整行禁用（否则远处种类会诡异地灰掉）。
                const int32 kPickup = CountCollectibles(k, 0);   // 可拾取数量 = 未拾取数（来自已加载区的实时扫描叠加）
                // 总数：collectibles.json 已加载且有内容时以文件为准，否则回退 wiki 全图总数（尚未跑过 dump）。
                const int32 kTotal = (bCollectibleFileLoaded && DisplayCollectibles.Num() > 0)
                    ? CountCollectibles(k, -1)
                    : WT_WikiCollectibleTotal[k];
                // ★0.2.64: the kind row means "全部"; the two sub-rows narrow it down by picked state.
                const bool bSel = (k == SelectedCollectibleKind && SelectedCollectiblePicked < 0);

                // ---- ★0.2.67: each kind gets its own collapse arrow + label row --------------
                // The arrow toggles per-kind expansion (so the user can open only the kinds they
                // care about). The label still selects the kind (全部 view) for the right pane,
                // exactly as before -- selection and folding are now independent.
                UHorizontalBox* KindRow = WidgetTree->ConstructWidget<UHorizontalBox>();

                const FText ArrowGlyph = bKindExpanded[k]
                    ? WT_LOCTEXT("Arrow_Down", "▼")
                    : WT_LOCTEXT("Arrow_Right", "▶");
                UButton* ArrowBtn = MakeButton(WidgetTree, ArrowGlyph, C_BTN, nullptr, ETextJustify::Center);
                ArrowBtn->TakeWidget();
                if (TSharedPtr<SWidget> ACached = ArrowBtn->GetCachedWidget())
                {
                    if (TSharedPtr<SButton> ASB = StaticCastSharedPtr<SButton>(ACached))
                    {
                        const int32 KindIdx = k;
                        ASB->SetOnClicked(FOnClicked::CreateLambda([this, KindIdx]() -> FReply
                        {
                            bKindExpanded[KindIdx] = !bKindExpanded[KindIdx];
                            RebuildCategoryList();
                            return FReply::Handled();
                        }));
                    }
                }
                if (UHorizontalBoxSlot* AS = KindRow->AddChildToHorizontalBox(ArrowBtn))
                {
                    AS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
                    AS->SetPadding(FMargin(2.f, 1.f, 2.f, 1.f));
                }

                const FText sub = kPickup > 0
                    ? FText::Format(WT_LOCTEXT("Sub_WithCount", "    {0}  ({1})"), CollectibleKindLabel(k), FText::AsNumber(kPickup))
                    : FText::Format(WT_LOCTEXT("Sub_Plain", "    {0}"), CollectibleKindLabel(k));
                FWT_ButtonInfo Info;
                Info.Kind = static_cast<uint8>(EWT_ButtonKind::CollectibleKind);
                Info.Index = k * 3; // ★0.2.64: low digit 0 = 全部
                UButton* LabelBtn = AddListButton(sub, bSel, Info, /*bEnabled=*/kTotal > 0 || bSel);
                if (UHorizontalBoxSlot* LS = KindRow->AddChildToHorizontalBox(LabelBtn))
                {
                    LS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
                    LS->SetPadding(FMargin(0.f, 1.f, 6.f, 1.f));
                }

                if (UVerticalBoxSlot* S = CategoryBox->AddChildToVerticalBox(KindRow))
                {
                    S->SetPadding(FMargin(0.f, 1.f, 6.f, 1.f));
                }

                // ---- ★0.2.64/0.2.67: 【未拾取】 / 【已拾取】 children (only when this kind is expanded) ----
                if (bKindExpanded[k])
                {
                    for (int32 st = 1; st <= 2; ++st)
                    {
                        const int32 Picked = st - 1;             // 0 = 未拾取, 1 = 已拾取
                        const int32 stCount = CountCollectibles(k, Picked);
                        const bool bSel2 = (k == SelectedCollectibleKind && Picked == SelectedCollectiblePicked);
                        const FText sub2 = stCount > 0
                            ? FText::Format(WT_LOCTEXT("Sub2_WithCount", "        {0}  ({1})"),
                                            CollectiblePickedLabel(Picked), FText::AsNumber(stCount))
                            : FText::Format(WT_LOCTEXT("Sub2_Plain", "        {0}"), CollectiblePickedLabel(Picked));

                        FWT_ButtonInfo Info2;
                        Info2.Kind = static_cast<uint8>(EWT_ButtonKind::CollectibleKind);
                        Info2.Index = k * 3 + st;               // low digit 1 = 未拾取, 2 = 已拾取
                        UButton* B2 = AddListButton(sub2, bSel2, Info2, /*bEnabled=*/stCount > 0 || bSel2);
                        if (UVerticalBoxSlot* S2 = CategoryBox->AddChildToVerticalBox(B2))
                        {
                            S2->SetPadding(FMargin(0.f, 1.f, 6.f, 1.f));
                        }
                    }
                }
            }
        }
    }
}

void URemote_FactoryPanelWidget::RebuildMachineList()
{
    // 最多 WT_MaxRenderedRows 个按钮 = 同步创建上千个 Slate widget，属最贵的一步。
    WT_ZONE_COARSE(TEXT("Panel.RebuildMachineList"));
    if (!MachineListBox)
    {
        return;
    }
    MachineListBox->ClearChildren();
    CollectibleRowTexts.Reset();
    CollectibleRowSrcIdx.Reset();

    // ★0.2.62: when an alien-energy kind is selected, list that kind's collectibles instead of machines.
    if (SelectedCollectibleKind >= 0)
    {
        int32 Rendered = 0;
        for (int32 i = 0; i < DisplayCollectibles.Num(); ++i)
        {
            const FWT_CollectibleEntry& C = DisplayCollectibles[i];
            if (C.Kind != static_cast<uint8>(SelectedCollectibleKind))
            {
                continue;
            }
            // ★0.2.64 【已拾取】/【未拾取】: narrow the kind down further when one of those was picked.
            if (SelectedCollectiblePicked >= 0 &&
                (C.bCollected ? 1 : 0) != SelectedCollectiblePicked)
            {
                continue;
            }
            if (Rendered >= WT_MaxRenderedRows)
            {
                break;
            }
            UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();

            const FText RowLabel = WTBuildCollectibleRowText(C);

            FWT_ButtonInfo Info;
            Info.Kind = static_cast<uint8>(EWT_ButtonKind::CollectibleRow);
            Info.Index = i;

            UTextBlock* RowLabelText = nullptr;
            UButton* LabelBtn = AddListButton(RowLabel, /*bSelected=*/(i == SelectedCollectible),
                                              Info, /*bEnabled=*/true, &RowLabelText);
            if (RowLabelText)
            {
                CollectibleRowTexts.Add(RowLabelText);
                CollectibleRowSrcIdx.Add(i);
                // ★0.2.136 溶洞条目整行上洞穴色，一眼可辨。选中行保持 C_ON_SEL 的高对比深色
                // （选中态底色是亮青，淡紫在上面对比度不够）。UpdateCollectibleDistances 只
                // SetText，不会把这层颜色冲掉。
                if (C.bCave && !(i == SelectedCollectible))
                {
                    RowLabelText->SetColorAndOpacity(FSlateColor(C_CAVE));
                }
            }
            if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(LabelBtn))
            {
                S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
                S->SetPadding(FMargin(0.f, 1.f));
            }

            // [传送] to this collectible. ★0.2.69: file-listed spawns may be in an unloaded cell with
            // no live actor -- fall back to teleport-by-coordinate in that case.
            AActor* RowActor = C.Actor.Get();
            const FVector RowLocation = C.Location;
            {
                UTextBlock* TpLabel = nullptr;
                UButton* TpBtn = MakeButton(WidgetTree, WT_LOCTEXT("Btn_RowTeleport", "传送"), C_ACCENT, &TpLabel);
                TpBtn->TakeWidget();
                if (TSharedPtr<SWidget> Cached = TpBtn->GetCachedWidget())
                {
                    if (TSharedPtr<SButton> SB = StaticCastSharedPtr<SButton>(Cached))
                    {
                        SB->SetOnClicked(FOnClicked::CreateLambda([this, RowActor, RowLocation]() -> FReply
                        {
                            if (URemote_FactoryRCO* R = GetRCO())
                            {
                                if (IsValid(RowActor))
                                {
                                    R->Server_TeleportTo(RowActor);
                                }
                                else
                                {
                                    R->Server_TeleportToLocation(RowLocation);
                                }
                            }
                            return FReply::Handled();
                        }));
                    }
                }
                if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(TpBtn))
                {
                    S->SetPadding(FMargin(4.f, 1.f, 0.f, 1.f));
                    S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
                }
            }

            if (UScrollBoxSlot* S = Cast<UScrollBoxSlot>(MachineListBox->AddChild(Row)))
            {
                S->SetPadding(FMargin(0.f, 1.f));
            }
            ++Rendered;
        }
        return;
    }

    // ★v0.2.76 同类型机器折叠：一个类型一个可展开分组头（▸/▾ + ×数量 + 最近距离），展开后按
    // 距离从近到远画成员行（每组最多 WT_MaxRowsPerGroup 行 + "另有 N 台"提示）。服务端已剔除
    // Other 并把名额抬到 500000（见 RCO 的 WT_MaxListEntries），分组头的 ×N 就是该类型全部实机数。
    // 行内的传送/打开面板仍按捕获的 Target 指针动作；SelectedEntry 依旧存 Entries 扁平下标，
    // 详情面板/选择逻辑零改动。展开状态存 ExpandedMachineGroups，15s 周期重扫不清除。
    struct FWTTypeGroup
    {
        FString Key;
        FText Name;
        float NearestM = TNumericLimits<float>::Max();
        TArray<int32> Members;   // CurrentList.Entries 的扁平下标
    };

    TMap<FString, int32> GroupByKey;
    TArray<FWTTypeGroup> Groups;
    for (int32 i = 0; i < CurrentList.Entries.Num(); ++i)
    {
        const FWT_MachineEntry& Entry = CurrentList.Entries[i];
        if (Entry.Category != static_cast<uint8>(SelectedCategory))
        {
            continue;
        }
        // ★0.2.146 MOD建筑类目：按 MOD 友好名分子组（组头=「MOD名 ×N」，展开后成员行
        // 逐台显示建筑原名），其余类目维持「类型 ×N」分组不变。
        const bool bModdedGroup = Entry.Category == static_cast<uint8>(EWT_MachineCategory::Modded)
                                  && !Entry.ModName.IsEmpty();
        const FString& GroupLabel = bModdedGroup ? Entry.ModName : Entry.DisplayName.ToString();
        FString Key = FString::FromInt(Entry.Category) + TEXT("|") + GroupLabel;
        if (int32* GroupSlot = GroupByKey.Find(Key))
        {
            FWTTypeGroup& G = Groups[*GroupSlot];
            G.NearestM = FMath::Min(G.NearestM, Entry.DistanceMeters);
            G.Members.Add(i);
        }
        else
        {
            FWTTypeGroup G;
            G.Key = Key;
            G.Name = bModdedGroup ? FText::FromString(Entry.ModName) : Entry.DisplayName;
            G.NearestM = Entry.DistanceMeters;
            G.Members.Add(i);
            GroupByKey.Add(Key, Groups.Num());
            Groups.Add(MoveTemp(G));
        }
    }

    // 行渲染器（折叠后各组共用，内容与折叠前的平铺行完全一致）：
    // [名称 · 距离 · 状态] [传送] [打开面板 if interactive]，动作按钮捕获 Target 指针，
    // 重建列表也绝不会点错机器。
    auto RenderMachineRow = [this](int32 EntryIndex, int32& Rendered) -> bool
    {
        if (Rendered >= WT_MaxRenderedRows)
        {
            return false;
        }
        const FWT_MachineEntry& Entry = CurrentList.Entries[EntryIndex];
        UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();

        const FText RowLabel = FText::Format(
            WT_LOCTEXT("MachineRow", "{0}   {1}m   {2}"),
            Entry.DisplayName,
            FText::AsNumber(FMath::RoundToInt(Entry.DistanceMeters)),
            Entry.StatusLine);

        FWT_ButtonInfo Info;
        Info.Kind = static_cast<uint8>(EWT_ButtonKind::MachineRow);
        Info.Index = EntryIndex;

        // ★0.2.153m 行首图标（生产机器/采矿设备显示该建筑物品图标）。服务端只给这两个类目填了
        // 软引用路径，其余行 IsNull() 直接跳过不建图，零开销。SetBrushFromSoftTexture 内部走
        // 异步加载 + 弱引用回调（Image.cpp:361），不会同步 LoadSynchronous 卡帧。
        if (!Entry.Icon.IsNull())
        {
            UImage* RowIcon = WidgetTree->ConstructWidget<UImage>();
            RowIcon->SetDesiredSizeOverride(FVector2D(24.f, 24.f));
            RowIcon->SetBrushFromSoftTexture(Entry.Icon, /*bMatchSize=*/false);
            if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(RowIcon))
            {
                S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
                S->SetPadding(FMargin(2.f, 1.f, 4.f, 1.f));
            }
        }

        UButton* LabelBtn = AddListButton(RowLabel, /*bSelected=*/(EntryIndex == SelectedEntry),
                                          Info, /*bEnabled=*/true);
        if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(LabelBtn))
        {
            S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            S->SetPadding(FMargin(0.f, 1.f));
        }

        // Capture the live target: the lambda holds a pointer, not a list index, so a later rebuild
        // that shifts indices cannot make the button act on the wrong machine.
        // ★0.2.153m 放宽为 AActor：运输工具（AFGVehicle 派生）不是 AFGBuildable。
        AActor* RowTarget = Entry.Target.Get();
        const FVector RowCoordLoc = Entry.Location;      // ★0.2.153e 坐标直传兜底（远端未复制时）
        const float RowCoordRadius = Entry.BaseRadius;

        // ★0.2.153p 产出物图标（用户要求：放在「传送」按钮左边）。服务端只给生产机器/采矿设备填了
        // 产物图标软引用（未设定配方/未连节点的机器为空），其余行 IsNull() 跳过不建图。
        // 与行首建筑图标区分：行首=这台机器是什么，此处=它正在产什么。
        if (!Entry.ProductIcon.IsNull())
        {
            UImage* OutIcon = WidgetTree->ConstructWidget<UImage>();
            OutIcon->SetDesiredSizeOverride(FVector2D(24.f, 24.f));
            OutIcon->SetBrushFromSoftTexture(Entry.ProductIcon, /*bMatchSize=*/false);
            if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(OutIcon))
            {
                S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
                S->SetPadding(FMargin(4.f, 1.f, 4.f, 1.f));
            }
        }

        // [传送] — every building, machine or not.
        {
            UTextBlock* TpLabel = nullptr;
            UButton* TpBtn = MakeButton(WidgetTree, WT_LOCTEXT("Btn_RowTeleport", "传送"), C_ACCENT, &TpLabel);
            TpBtn->TakeWidget();
            if (TSharedPtr<SWidget> Cached = TpBtn->GetCachedWidget())
            {
                if (TSharedPtr<SButton> SB = StaticCastSharedPtr<SButton>(Cached))
                {
                    SB->SetOnClicked(FOnClicked::CreateLambda([this, RowTarget, RowCoordLoc, RowCoordRadius]() -> FReply
                    {
                        if (URemote_FactoryRCO* R = GetRCO())
                        {
                            if (IsValid(RowTarget))
                            {
                                R->Server_TeleportTo(RowTarget);
                            }
                            // ★0.2.153e 坐标直传：远端机器客户端未复制（Target 为 null）→ 按坐标传
                            else if (!RowCoordLoc.IsNearlyZero())
                            {
                                R->Server_TeleportToCoord(RowCoordLoc, RowCoordRadius);
                            }
                        }
                        return FReply::Handled();
                    }));
                }
            }
            if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(TpBtn))
            {
                S->SetPadding(FMargin(4.f, 1.f, 0.f, 1.f));
                S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
            }
        }

        // [打开面板] — only for buildings that expose an interact widget (i.e. machines the player
        // actually operates). Bare props / passive buildings skip this and keep just [传送].
        if (Entry.bHasInteractUI)
        {
            UTextBlock* OpenLabel = nullptr;
            UButton* OpenBtn = MakeButton(WidgetTree, WT_LOCTEXT("Btn_RowOpen", "打开面板"), C_BTN, &OpenLabel);
            OpenBtn->TakeWidget();
            if (TSharedPtr<SWidget> Cached = OpenBtn->GetCachedWidget())
            {
                if (TSharedPtr<SButton> SB = StaticCastSharedPtr<SButton>(Cached))
                {
                    SB->SetOnClicked(FOnClicked::CreateLambda([this, RowTarget]() -> FReply
                    {
                        if (URemote_FactoryRCO* R = GetRCO())
                        {
                            if (AFGBuildable* Bld = Cast<AFGBuildable>(RowTarget))
                            {
                                R->Server_RequestOpenUI(Bld);
                            }
                        }
                        return FReply::Handled();
                    }));
                }
            }
            if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(OpenBtn))
            {
                S->SetPadding(FMargin(4.f, 1.f, 0.f, 1.f));
                S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
            }
        }

        if (UScrollBoxSlot* S = Cast<UScrollBoxSlot>(MachineListBox->AddChild(Row)))
        {
            S->SetPadding(FMargin(0.f, 1.f));
        }
        ++Rendered;
        return true;
    };

    int32 Rendered = 0;
    for (FWTTypeGroup& G : Groups)
    {
        if (Rendered >= WT_MaxRenderedRows)
        {
            break;
        }
        const bool bExpanded = ExpandedMachineGroups.Contains(G.Key);
        // ★v0.2.76b（用户反馈）：箭头从标签文本里拆出来，改为**贴右侧**的独立下拉箭头
        // （名称在左占满、箭头在最右），与常见折叠列表的视觉习惯一致。
        const FText Arrow = bExpanded
            ? WT_LOCTEXT("FoldOpen", "▼")
            : WT_LOCTEXT("FoldClosed", "▶");
        const FText HeaderLabel = FText::Format(
            WT_LOCTEXT("MachineGroupHeader", "{0} ×{1} · 最近 {2}m"),
            G.Name,
            FText::AsNumber(G.Members.Num()),
            FText::AsNumber(FMath::RoundToInt(G.NearestM)));

        // 分组头：不走中央分发器，直接捕获 Key（与传送按钮同模式），点击即折叠/展开并重画。
        // 头部行 = [名称按钮(占满)] [箭头按钮(贴右)]，两者共用同一折叠切换。
        {
            UHorizontalBox* HdrRow = WidgetTree->ConstructWidget<UHorizontalBox>();

            UTextBlock* HdrLabel = nullptr;
            UButton* HdrBtn = MakeButton(WidgetTree, HeaderLabel, C_BTN, &HdrLabel);
            HdrBtn->TakeWidget();
            if (TSharedPtr<SWidget> Cached = HdrBtn->GetCachedWidget())
            {
                if (TSharedPtr<SButton> SB = StaticCastSharedPtr<SButton>(Cached))
                {
                    SB->SetOnClicked(FOnClicked::CreateLambda([this, Key = G.Key]() -> FReply
                    {
                        if (!ExpandedMachineGroups.Remove(Key))
                        {
                            ExpandedMachineGroups.Add(Key);
                        }
                        RebuildMachineList();
                        return FReply::Handled();
                    }));
                }
            }
            if (UHorizontalBoxSlot* HS = HdrRow->AddChildToHorizontalBox(HdrBtn))
            {
                HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
                HS->SetPadding(FMargin(0.f, 1.f));
            }

            // 右侧下拉箭头：同样是可点按钮（点击效果与名称按钮一致）。
            UTextBlock* ArrowLabel = nullptr;
            UButton* ArrowBtn = MakeButton(WidgetTree, Arrow, C_BTN, &ArrowLabel);
            ArrowBtn->TakeWidget();
            if (TSharedPtr<SWidget> ACached = ArrowBtn->GetCachedWidget())
            {
                if (TSharedPtr<SButton> ASB = StaticCastSharedPtr<SButton>(ACached))
                {
                    ASB->SetOnClicked(FOnClicked::CreateLambda([this, Key = G.Key]() -> FReply
                    {
                        if (!ExpandedMachineGroups.Remove(Key))
                        {
                            ExpandedMachineGroups.Add(Key);
                        }
                        RebuildMachineList();
                        return FReply::Handled();
                    }));
                }
            }
            if (UHorizontalBoxSlot* AS = HdrRow->AddChildToHorizontalBox(ArrowBtn))
            {
                AS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
                AS->SetPadding(FMargin(4.f, 1.f, 0.f, 1.f));
            }

            if (UScrollBoxSlot* S = Cast<UScrollBoxSlot>(MachineListBox->AddChild(HdrRow)))
            {
                S->SetPadding(FMargin(0.f, 1.f));
            }
            ++Rendered;
        }

        if (!bExpanded)
        {
            continue;
        }

        // 展开时按距离从近到远排成员；只画最近 WT_MaxRowsPerGroup 行，超出给提示行。
        G.Members.Sort([this](int32 A, int32 B)
        {
            return CurrentList.Entries[A].DistanceMeters < CurrentList.Entries[B].DistanceMeters;
        });
        int32 Shown = 0;
        for (int32 MemberIdx : G.Members)
        {
            if (Shown >= WT_MaxRowsPerGroup || !RenderMachineRow(MemberIdx, Rendered))
            {
                break;
            }
            ++Shown;
        }
        if (G.Members.Num() > Shown && Rendered < WT_MaxRenderedRows)
        {
            UTextBlock* More = WidgetTree->ConstructWidget<UTextBlock>();
            More->SetText(FText::Format(
                WT_LOCTEXT("MachineGroupMore", "      …另有 {0} 台同型机器（已显示最近的 {1} 台）"),
                FText::AsNumber(G.Members.Num() - Shown),
                FText::AsNumber(Shown)));
            if (UScrollBoxSlot* S = Cast<UScrollBoxSlot>(MachineListBox->AddChild(More)))
            {
                S->SetPadding(FMargin(0.f, 2.f));
            }
            ++Rendered;
        }
    }
}

void URemote_FactoryPanelWidget::SelectCategory(int32 CategoryIndex)
{
    SelectedCategory = CategoryIndex;
    SelectedCollectibleKind = -1; // ★0.2.62: machine selection clears alien-energy selection
    SelectedCollectiblePicked = -1; // ★0.2.64: and its picked-state filter too
    SelectedEntry = -1;
    RebuildCategoryList();
    RebuildMachineList();
    RebuildDetailPane();
}

// ★0.2.62 ------------------------------------------------------------------
void URemote_FactoryPanelWidget::ToggleTopCategory(int32 TopIndex)
{
    if (TopIndex >= 0 && TopIndex < 2)   // ★0.2.153p 顶层只剩 建筑(0) / 收集品(1)
    {
        bTopExpanded[TopIndex] = !bTopExpanded[TopIndex];
        RebuildCategoryList();
    }
}

int32 URemote_FactoryPanelWidget::CountCollectibles(int32 Kind, int32 Picked) const
{
    if (Kind < 0 || Kind >= static_cast<int32>(EWT_CollectibleKind::Count))
    {
        return 0;
    }
    int32 N = 0;
    for (const FWT_CollectibleEntry& C : DisplayCollectibles)
    {
        if (C.Kind != static_cast<uint8>(Kind))
        {
            continue;
        }
        // Picked == -1 counts both states; otherwise keep only the matching one.
        if (Picked >= 0 && (C.bCollected ? 1 : 0) != Picked)
        {
            continue;
        }
        ++N;
    }
    return N;
}

void URemote_FactoryPanelWidget::SelectCollectibleKind(int32 Encoded)
{
    // ★0.2.64: Index was encoded as kind*3 + (state+1) so one int32 carries both the kind
    // and the picked filter.  0 => 全部, 1 => 未拾取, 2 => 已拾取.
    const int32 Kind   = Encoded / 3;
    const int32 Picked = (Encoded % 3) - 1;
    if (Kind < 0 || Kind >= static_cast<int32>(EWT_CollectibleKind::Count))
    {
        return;
    }
    SelectedCollectibleKind   = Kind;
    SelectedCollectiblePicked = Picked;
    SelectedCategory = -1; // 互斥：选外星科技种类时清空机器分类
    SelectedEntry = -1;
    SelectedCollectible = -1;
    RebuildCategoryList();
    RebuildMachineList();
    RebuildDetailPane();
}

int32 URemote_FactoryPanelWidget::TopCount(int32 TopIndex) const
{
    // ★0.2.153p 容器（液体/固体）并入建筑（采矿设备与发电机之间）⇒ 顶层只剩 建筑(0) / 收集品(1)。
    if (TopIndex == 0) // 建筑：16 个子类之和（★0.2.153l 去掉「自动工作台」：它派生 Manufacturer，已并入 Production），★0.2.146 含 MOD建筑/制作台/装备工坊/AWESOME商店（★0.2.153p 含液体/固体容器；不含 DropPod / Other）
    {
        // ★0.2.153r 与上面 RenderCategoryBar 的数组必须**逐项一致**（制作台/工坊/MAM/商店/
        // 回收站/太空电梯 已并入 Hub）：两处不一致会导致「类目里显示 3 条、顶部却显示 5」。
        static const EWT_MachineCategory BuildingCats[] = {
            EWT_MachineCategory::Producer, EWT_MachineCategory::Miner,
            EWT_MachineCategory::LiquidContainer, EWT_MachineCategory::SolidContainer,
            EWT_MachineCategory::Generator,
            EWT_MachineCategory::PowerSwitch, EWT_MachineCategory::Hub,
            EWT_MachineCategory::Vehicle, EWT_MachineCategory::Portal, EWT_MachineCategory::Modded,
        };
        int32 Sum = 0;
        for (EWT_MachineCategory c : BuildingCats)
        {
            const int32 ci = static_cast<int32>(c);
            if (CurrentList.CategoryCounts.IsValidIndex(ci)) Sum += CurrentList.CategoryCounts[ci];
        }
        return Sum;
    }
    // TopIndex == 1: 收集品（9 类 collectible 之和：6 类外星能量 + 3 类可采集果实）。文件已加载且有内容时以 collectibles.json 为准，
    // 否则回退 wiki 全图总数（玩家尚未跑过 wt.DumpCollectibles 时）。
    int32 Sum = 0;
    if (bCollectibleFileLoaded && DisplayCollectibles.Num() > 0)
    {
        for (int32 k = 0; k < static_cast<int32>(EWT_CollectibleKind::Count); ++k)
        {
            Sum += CountCollectibles(k, -1);
        }
    }
    else
    {
        for (int32 k = 0; k < static_cast<int32>(EWT_CollectibleKind::Count); ++k)
        {
            Sum += WT_WikiCollectibleTotal[k];
        }
    }
    return Sum;
}

void URemote_FactoryPanelWidget::SelectEntry(int32 EntryIndex)
{
    SelectedEntry = EntryIndex;
    RebuildMachineList();
    RebuildDetailPane();
}

void URemote_FactoryPanelWidget::RebuildDetailPane()
{
    WT_ZONE(TEXT("Panel.RebuildDetailPane"));
    if (!DetailNameText)
    {
        return;
    }

    const FWT_MachineEntry* Entry = CurrentList.Entries.IsValidIndex(SelectedEntry)
        ? &CurrentList.Entries[SelectedEntry]
        : nullptr;

    if (!Entry)
    {
        DetailNameText->SetText(WT_LOCTEXT("Detail_NoMachineSelected", "No machine selected"));
        if (DetailStatusText) { DetailStatusText->SetText(FText::GetEmpty()); }
        if (DetailOutputText) { DetailOutputText->SetText(FText::GetEmpty()); }
        // Collapse the switch block: with no row selected there is nothing to operate, and leaving
        // it up would invite clicks that silently do nothing.
        RefreshSwitchControls();
        return;
    }

    DetailNameText->SetText(Entry->DisplayName);
    if (DetailStatusText)
    {
        DetailStatusText->SetText(FText::Format(
            WT_LOCTEXT("Detail_DistanceAndStatus", "{0}m away · {1}"),
            FText::AsNumber(FMath::RoundToInt(Entry->DistanceMeters)),
            Entry->StatusLine));
    }

    // Live output contents, read straight off the replicated buildable.
    if (DetailOutputText)
    {
        FString OutputDesc;
        if (AFGBuildable* Target = Cast<AFGBuildable>(Entry->Target))
        {
            UFGInventoryComponent* Output = nullptr;
            if (AFGBuildableManufacturer* M = Cast<AFGBuildableManufacturer>(Target))
            {
                Output = M->GetOutputInventory();
            }
            else if (AFGBuildableResourceExtractorBase* E = Cast<AFGBuildableResourceExtractorBase>(Target))
            {
                Output = E->GetOutputInventory();
            }

            if (Output)
            {
                TArray<FInventoryStack> Stacks;
                Output->GetInventoryStacks(Stacks, /*getEmptyStacks=*/false);
                for (const FInventoryStack& Stack : Stacks)
                {
                    // GetDisplayNameText() does not exist on UClass; item names live on the
                    // descriptor, which is what GetItemClass() hands back.
                    const TSubclassOf<UFGItemDescriptor> ItemClass = Stack.Item.GetItemClass();
                    const FString ItemName = ItemClass
                        ? UFGItemDescriptor::GetItemName(ItemClass).ToString()
                        : WT_LOCTEXT("Item_Unknown", "Unknown item").ToString();
                    OutputDesc += FString::Printf(TEXT("%s x%d\n"), *ItemName, Stack.NumItems);
                }
            }
        }
        DetailOutputText->SetText(OutputDesc.IsEmpty() ? WT_LOCTEXT("Output_None", "(no output)")
                                                       : FText::FromString(OutputDesc));
    }

    // Form 3 "A"/"C": the switch block and the upgrade readouts both hang off the selected row, so
    // they are refreshed from here rather than from the scan callback -- selection is what makes
    // them relevant.
    RefreshSwitchControls();
}

void URemote_FactoryPanelWidget::RefreshSwitchControls()
{
    if (!SwitchBox)
    {
        return;
    }

    const FWT_MachineEntry* Entry = WT_SelectedEntry(CurrentList, SelectedEntry);

    // A switch is identified by its CATEGORY rather than by re-casting the target. The category was
    // decided on the server during the scan, where the class hierarchy was authoritative; trusting
    // it here keeps the client from needing the switch headers at all.
    const bool bIsSwitch = Entry
        && Entry->Category == static_cast<uint8>(EWT_MachineCategory::PowerSwitch);

    if (!bIsSwitch)
    {
        SwitchBox->SetVisibility(ESlateVisibility::Collapsed);
        return;
    }

    SwitchBox->SetVisibility(ESlateVisibility::Visible);

    // ---- state text ----
    if (SwitchStateText)
    {
        if (!Entry->bSwitchConnected)
        {
            // Spelled out because it is the single most confusing switch state: the button is
            // present, clicking it does nothing, and nothing in the vanilla UI says why.
            SwitchStateText->SetText(
                WT_LOCTEXT("SwitchState_NotWired",
                           "Not wired — both ends of the switch must be connected to a power grid."));
            SwitchStateText->SetColorAndOpacity(FSlateColor(C_AMBER));
        }
        else
        {
            const FText StateWord = Entry->bSwitchOn ? WT_LOCTEXT("Switch_On", "On")
                                                     : WT_LOCTEXT("Switch_Off", "Off");
            if (Entry->SwitchPriority > 0)
            {
                SwitchStateText->SetText(FText::Format(
                    WT_LOCTEXT("SwitchState_WithPriority", "Now: {0}　limit priority {1}"),
                    StateWord, FText::AsNumber(Entry->SwitchPriority)));
            }
            else if (Entry->SwitchPriority == 0)
            {
                SwitchStateText->SetText(FText::Format(
                    WT_LOCTEXT("SwitchState_NoLimiter", "Now: {0}　never auto-limits"),
                    StateWord));
            }
            else
            {
                SwitchStateText->SetText(FText::Format(
                    WT_LOCTEXT("SwitchState_Plain", "Now: {0}"), StateWord));
            }
            SwitchStateText->SetColorAndOpacity(FSlateColor(C_LABEL));
        }
    }

    // ---- toggle button ----
    // bIsSessionOwner lives on the REPLY, not on the entry: it describes this viewer's rights in the
    // shared session, not anything about the machine. Read it from the reply we are rendering.
    const bool bCanToggle = Entry->bSwitchConnected && CurrentList.bIsSessionOwner;
    if (SwitchToggleLabel)
    {
        // Label the ACTION, not the state: a button reading "Turn on" while the switch is already on
        // is ambiguous about whether it is a status light or a control.
        SwitchToggleLabel->SetText(Entry->bSwitchOn ? WT_LOCTEXT("Btn_SwitchOff", "Turn off")
                                                    : WT_LOCTEXT("Btn_SwitchOn", "Turn on"));
    }
    if (SwitchToggleButton)
    {
        SwitchToggleButton->SetIsEnabled(bCanToggle);
        // Override the accent when the switch is currently ON: colour then carries the state, so the
        // row reads at a glance without parsing the word.
        SwitchToggleButton->SetBackgroundColor(Entry->bSwitchOn ? C_AMBER : C_ACCENT);
    }

    // ---- priority steppers ----
    // Only the priority variant has a priority at all; SwitchPriority == -1 marks a plain switch.
    const bool bHasPriority = Entry->SwitchPriority >= 0;
    const bool bCanWrite = bHasPriority && CurrentList.bIsSessionOwner;
    if (PriorityUpButton)    { PriorityUpButton->SetIsEnabled(bCanWrite); }
    if (PriorityDownButton)  { PriorityDownButton->SetIsEnabled(bCanWrite); }
    if (PriorityClearButton) { PriorityClearButton->SetIsEnabled(bCanWrite); }

    if (SwitchHeaderText)
    {
        SwitchHeaderText->SetText(bHasPriority
            ? WT_LOCTEXT("Header_PrioritySwitch", "Power Switch · Priority Switch")
            : WT_LOCTEXT("Header_Switch", "Power Switch"));
    }
}

FReply URemote_FactoryPanelWidget::NativeOnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
    // ESC closes the panel the same way the ✕ button / right-click toggle does -- fully tear down
    // and restore game input. This replaces the disabled FGGameUI ESC path (v0.2.20).
    if (InKeyEvent.GetKey() == EKeys::Escape)
    {
        if (PanelCloseRequested.IsBound())
        {
            PanelCloseRequested.Broadcast();
        }
        else
        {
            RemoveFromParent();
        }
        return FReply::Handled();
    }
    return Super::NativeOnKeyDown(MyGeometry, InKeyEvent);
}

void URemote_FactoryPanelWidget::HandleTeleportResult(AActor* Target, bool bSuccess, int32 Reason)
{
    WT_ZONE(TEXT("Panel.HandleTeleportResult"));   // 目标可能已在传送途中被销毁（拾取/拆除）
    if (StatusText)
    {
        if (bSuccess)
        {
            // ★0.2.137 源串一律用中文：本 mod 的中文环境显示的是**代码字面量**（zh po 运行期并不可靠，
            // 0.2.135 的英文源串在中文客户端直接漏英），英文走 en archive 的 Translation 兜底。
            StatusText->SetText(FText::Format(WT_LOCTEXT("Status_TeleportedTo", "已传送到 {0}"),
                            FText::FromString(Target ? Target->GetName()
                                                     : WT_LOCTEXT("Target_Generic", "目标").ToString())));
        }
        else if (Reason == WTTeleportFail::NoSafeLanding)
        {
            // ★0.2.135 拒绝硬落专用文案：目标周围找不到「脚下有地面」的落点——溶洞深处/未流送
            // 单元格就是这种情况。以前会把玩家丢过去直接掉出地形（用户实测：外星类目标在溶洞里
            // 传送会把角色卡出地形外），现在服务端直接取消，这里要说清「不是目标没了，是那边没地面」。
            StatusText->SetText(WT_LOCTEXT("Status_TeleportNoSafeLanding",
                "传送已取消：目标附近没有可站立的地面（该区域尚未加载，例如溶洞深处），请先靠近后再传送"));
        }
        else
        {
            StatusText->SetText(WT_LOCTEXT("Status_TeleportFailed", "传送失败：目标已失效"));
        }
    }

    // ★用户诉求：传送成功后下一帧自动关面板（便于落地后直接拾取/操作）。置标记，由 NativeTick 执行。
    if (bSuccess)
    {
        bPendingAutoClose = true;
    }
}

void URemote_FactoryPanelWidget::RebuildDisplayCollectibles()
{
    WT_ZONE_COARSE(TEXT("Panel.RebuildDisplayCollectibles"));   // collectibles.json 全图 -> 展示列表

    // ★0.2.127: the panel opening is one of the monitor's lazy-creation triggers (the RCO scan is
    // the other). No-op if it already exists.
    FWTCollectibles::EnsurePickupMonitor();

    // ★0.2.127: reload the persisted collected set on EVERY rebuild (file is tiny; the pickup
    // monitor may have recorded new pickups while the panel was closed). The present->absent
    // scan-diff scheme from 0.2.126 is GONE -- it missed pickups that happened between the 15 s
    // scans; the monitor (Remote_FactoryPickupMonitor) is the sole writer of RF_collected.json now.
    {
        TSet<FString> IgnoredPresent;
        FWTCollectibles::LoadCollectedState(CollectedSpawnKeys, IgnoredPresent);
        WT_LOGF(TEXT("[collect] rebuild loaded %d collected keys from RF_collected.json"),
                CollectedSpawnKeys.Num());
    }

    TArray<FWT_CollectibleSpawn> Spawns;
    bCollectibleFileLoaded = FWTCollectibles::LoadSpawns(Spawns);

    DisplayCollectibles.Reset();
    DisplayCollectibles.Reserve(Spawns.Num() + CurrentList.Collectibles.Num());

    // ★0.2.70: the live scan only covers a small radius around the player, so without this every
    // file-only row read "0m". Compute the real distance for each file spawn against the local
    // player pawn. If the pawn is missing (panel open while dead / transitioning) we keep 0 and the
    // row still works -- distance is cosmetic, teleport is by Location either way.
    FVector PlayerLoc = FVector::ZeroVector;
    if (const APawn* OwningPawn = GetOwningPlayerPawn())
    {
        PlayerLoc = OwningPawn->GetActorLocation();
    }

    // ---- pass 1: every spawn from collectibles.json (the authoritative full-map set) ----
    // ★0.2.126: a spawn already in the persisted collected set starts as bCollected = true, so it
    // lands in 【已拾取】 instead of staying stuck in 【未拾取】.
    for (const FWT_CollectibleSpawn& S : Spawns)
    {
        if (S.Kind >= static_cast<uint8>(EWT_CollectibleKind::Count))
        {
            continue;
        }
        FWT_CollectibleEntry E;
        E.Actor = nullptr;
        E.Kind = S.Kind;
        E.DisplayName = CollectibleKindLabel(static_cast<int32>(S.Kind));
        E.Location = S.Location;
        E.DistanceMeters = FVector::Dist(PlayerLoc, S.Location) / 100.f;
        E.bCollected = CollectedSpawnKeys.Contains(FWTCollectibles::CollectibleKey(S.Kind, S.Location));
        E.RegionName = ComputeRegionForLocation(S.Location);
        // ★0.2.136 溶洞标记：json 里已算好（显式 cave 字段或海拔判定），这里再兜一次海拔判定，
        // 保证任何来源（含以后 dump 写入的新点）都不会漏标。
        E.bCave = S.bCave || WT_IsCaveLocation(S.Location);
        DisplayCollectibles.Add(MoveTemp(E));
    }

    // ---- pass 2: overlay the collected-state from the live scan ONLY. collectibles.json is the
    //      single source of truth for the row set, so we NEVER append live entries -- that is what
    //      previously produced the "1,766" phantom rows. ★0.2.70: nearest-match within 15 m (was:
    //      first-hit within 2 m) so a live pickup a few metres off its baked coord still flags the
    //      right row without swallowing a legitimately-close neighbour (~8 m same-kind pairs exist in
    //      the data). Distance stays at the file-computed real value from pass 1 and is NOT
    //      overwritten by the live scan; NativeTick refreshes it live against the current player
    //      position. ----
    // ★0.2.127: the present-diff bookkeeping is gone; the live overlay is read-only now (the
    // pickup monitor owns the collected set).
    for (const FWT_CollectibleEntry& Live : CurrentList.Collectibles)
    {
        int32 BestIdx = INDEX_NONE;
        float BestDistSq = 1500.f * 1500.f;
        for (int32 Fi = 0; Fi < DisplayCollectibles.Num(); ++Fi)
        {
            const FWT_CollectibleEntry& E = DisplayCollectibles[Fi];
            if (E.Kind != Live.Kind)
            {
                continue;
            }
            const float Dsq = FVector::DistSquared(E.Location, Live.Location);
            if (Dsq < BestDistSq)
            {
                BestDistSq = Dsq;
                BestIdx = Fi;
            }
        }
        if (BestIdx != INDEX_NONE)
        {
            FWT_CollectibleEntry& E = DisplayCollectibles[BestIdx];
            // ★0.2.127: the persisted collected set is the authority for "player picked this up".
            // Live only FLIPS a row to picked when the actor says so, and only clears it when the
            // monitor has NOT recorded the spawn -- prevents a respawned pickup (IsPickedUp back to
            // false) from fighting the persisted set every rebuild.
            const FString Key = FWTCollectibles::CollectibleKey(E.Kind, E.Location);
            if (Live.bCollected)
            {
                E.bCollected = true;
            }
            else if (!CollectedSpawnKeys.Contains(Key))
            {
                E.bCollected = false;
            }
            E.Actor = Live.Actor;
            // ★ keep E.DistanceMeters as the file-computed real distance -- do NOT overwrite.
        }
    }

    // ★0.2.84: user request — sort DISTANCE first (nearest -> farthest), then by map area (region /
    // 地形), then uncollected-first as a tiebreak so greyed-out collected rows sink within a block.
    // Distance is what the player cares about when flying out; region is a secondary grouping only.
    DisplayCollectibles.Sort([](const FWT_CollectibleEntry& A, const FWT_CollectibleEntry& B)
    {
        if (!FMath::IsNearlyEqual(A.DistanceMeters, B.DistanceMeters, 0.5f))
        {
            return A.DistanceMeters < B.DistanceMeters;
        }
        const FString RA = A.RegionName.ToString();
        const FString RB = B.RegionName.ToString();
        if (RA != RB)
        {
            return RA < RB;
        }
        return !A.bCollected;
    });
}

void URemote_FactoryPanelWidget::UpdateCollectibleDistances()
{
    WT_ZONE(TEXT("Panel.UpdateCollectibleDistances"));   // live distance refresh, no widget rebuild
    const int32 N = CollectibleRowTexts.Num();
    if (N == 0 || CollectibleRowSrcIdx.Num() != N)
    {
        return;
    }
    FVector PlayerLoc = FVector::ZeroVector;
    if (const APawn* P = GetOwningPlayerPawn())
    {
        PlayerLoc = P->GetActorLocation();
    }
    for (int32 r = 0; r < N; ++r)
    {
        const int32 Src = CollectibleRowSrcIdx[r];
        if (!DisplayCollectibles.IsValidIndex(Src))
        {
            continue;
        }
        FWT_CollectibleEntry& E = DisplayCollectibles[Src];
        E.DistanceMeters = FVector::Dist(PlayerLoc, E.Location) / 100.f;
        if (UTextBlock* T = CollectibleRowTexts[r])
        {
            T->SetText(WTBuildCollectibleRowText(E));
        }
    }
}

FText URemote_FactoryPanelWidget::ComputeRegionForLocation(const FVector& Loc) const
{
    if (AFGPlayerController* PC = Cast<AFGPlayerController>(GetOwningPlayer()))
    {
        if (UFGMapAreaTexture* MapTex = PC->GetMapAreaTexture())
        {
            const TSubclassOf<UFGMapArea> Area = MapTex->GetMapAreaForWorldLocation(Loc);
            if (Area)
            {
                return UFGMapArea::GetAreaDisplayName(Area);
            }
        }
    }
    return FText::GetEmpty();
}

void URemote_FactoryPanelWidget::RebuildCollectibleList()
{
    WT_ZONE_COARSE(TEXT("Panel.RebuildCollectibleList"));
    if (!CollectibleListBox)
    {
        return;
    }
    CollectibleListBox->ClearChildren();

    // ---- per-kind tally ----
    if (CollectibleHeaderText)
    {
        FString Tally = WT_LOCTEXT("Col_Header", "收集品").ToString();
        for (int32 i = 0; i < static_cast<int32>(EWT_CollectibleKind::Count); ++i)
        {
            // ★0.2.69: whole-map total from collectibles.json when available, else wiki (live scan under-counts).
            const int32 Count = (bCollectibleFileLoaded && DisplayCollectibles.Num() > 0)
                ? CountCollectibles(i, -1)
                : WT_WikiCollectibleTotal[i];
            Tally += FString::Printf(TEXT("   %s %d"), *CollectibleKindLabel(i).ToString(), Count);
        }
        CollectibleHeaderText->SetText(FText::FromString(Tally));
    }

    // ---- rows grouped by official map-area name, collected rows greyed out ----
    // The server sorted by (region, uncollected-first, distance); we draw a region header
    // whenever the name changes, so each of 大草原/白石沙漠/北部森林/沙丘荒漠/… becomes its own
    // labelled block. State suffix: 空投舱【已开启】 vs 能量蛹【已拾取】 (user wording).
    FString LastRegion;
    for (int32 i = 0; i < DisplayCollectibles.Num(); ++i)
    {
        const FWT_CollectibleEntry& Entry = DisplayCollectibles[i];

        const FString Region = Entry.RegionName.ToString();
        if (Region != LastRegion)
        {
            LastRegion = Region;
            const FText HeaderText = Region.IsEmpty()
                ? WT_LOCTEXT("Col_RegionUnknown", "未知区域")
                : FText::FromString(FString::Printf(TEXT("—— %s ——"), *Region));
            if (UTextBlock* Header = MakeText(WidgetTree, HeaderText, 13, true, C_ACCENT))
            {
                if (UScrollBoxSlot* S = Cast<UScrollBoxSlot>(CollectibleListBox->AddChild(Header)))
                {
                    S->SetPadding(FMargin(0.f, 6.f, 0.f, 2.f));
                }
            }
        }

        // "空投舱【已开启】   X 12345  Y -6789  Z 123  1.2km"
        const TCHAR* StateSuffix = TEXT("");
        if (Entry.bCollected)
        {
            StateSuffix = (static_cast<EWT_CollectibleKind>(Entry.Kind) == EWT_CollectibleKind::DropPod)
                ? TEXT("【已开启】") : TEXT("【已拾取】");
        }
        // ★0.2.136 溶洞后缀（与上面的【已拾取】同为行内状态标记，保持同一套硬编码风格）。
        const TCHAR* CaveSuffix = Entry.bCave ? TEXT("   【溶洞】") : TEXT("");
        const FString Row = FString::Printf(TEXT("%s%s   X %.0f  Y %.0f  Z %.0f   %.1fkm%s"),
                                            *Entry.DisplayName.ToString(), StateSuffix,
                                            Entry.Location.X, Entry.Location.Y, Entry.Location.Z,
                                            Entry.DistanceMeters / 1000.f, CaveSuffix);

        FWT_ButtonInfo Info;
        Info.Kind = static_cast<uint8>(EWT_ButtonKind::CollectibleRow);
        Info.Index = i;

        // Collected targets stay listed but are not teleport candidates any more.
        UTextBlock* RegionRowLabel = nullptr;
        UButton* Button = AddListButton(FText::FromString(Row), /*bSelected=*/(i == SelectedCollectible),
                                        Info, /*bEnabled=*/!Entry.bCollected, &RegionRowLabel);
        if (RegionRowLabel && Entry.bCave && !(i == SelectedCollectible))
        {
            RegionRowLabel->SetColorAndOpacity(FSlateColor(C_CAVE));
        }
        if (UScrollBoxSlot* S = Cast<UScrollBoxSlot>(CollectibleListBox->AddChild(Button)))
        {
            S->SetPadding(FMargin(0.f, 1.f));
        }
    }

    // ---- selected-row detail + teleport affordance ----
    const FWT_CollectibleEntry* Sel = DisplayCollectibles.IsValidIndex(SelectedCollectible)
        ? &DisplayCollectibles[SelectedCollectible]
        : nullptr;

    if (CollectibleDetailText)
    {
        if (Sel)
        {
            FString Detail = FText::Format(WT_LOCTEXT("Col_SelectedDistance", "Selected: {0}   {1} km away"),
                                           Sel->DisplayName,
                                           FText::AsNumber(Sel->DistanceMeters / 1000.f)).ToString();
            if (!Sel->CostText.IsEmpty())
            {
                Detail += TEXT("   ");
                Detail += Sel->CostText.ToString();
            }
            CollectibleDetailText->SetText(FText::FromString(Detail));
        }
        else
        {
            CollectibleDetailText->SetText(WT_LOCTEXT("Col_PickToTeleport", "Pick an alien-energy target to teleport to"));
        }
    }

    if (TeleportToCollectibleButton)
    {
        // ★0.2.69: a file-listed spawn in an unloaded cell has no live actor, but is still teleportable by coordinate.
        TeleportToCollectibleButton->SetIsEnabled(Sel != nullptr && (IsValid(Sel->Actor) || !Sel->Location.IsZero()));
    }
}
