#include "Remote_FactoryPanelWidget.h"

#include "Remote_FactoryLog.h"
#include "Remote_FactoryRCO.h"
#include "Remote_FactoryResourceNodes.h"
#include "Remote_FactoryCrashTrace.h"
#include "Remote_FactoryButtonInfo.h"
#include "Remote_FactorySmartWarehouse.h"   // ★2026-11-02 智能仓储开关（IsSmartWarehouseEnabled / SetSmartWarehouseEnabled）
#include "Remote_FactoryMilestoneCargoReturn.h"   // ★2026-11-02 里程碑货舱即刻返回开关
#include "Remote_FactoryPowerGraph.h"     // ★1.0.9 电网类目的折线图控件（SLeafWidget 自绘）
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableManufacturer.h"
#include "Buildables/FGBuildableResourceExtractorBase.h"
#include "FGInventoryComponent.h"
#include "Buildables/FGBuildableStorage.h"   // ★本轮 客户端直读固体容器库存总量（GetStorageInventory）
#include "FGPlayerController.h"
#include "FGHUD.h"
#include "FGPortableMiner.h"   // ★2026-11-03 便携式采矿机打开 UI：直接读公开 mInteractWidgetClass
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
#include "Components/SizeBox.h"          // ★2026-10-25 左列定宽（防长提示行撑爆布局）
#include "Components/SizeBoxSlot.h"
#include "Components/EditableTextBox.h"   // ★1.0.8 内嵌在中列列表顶部的搜索框
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"           // ★1.0.8 SetTimerForNextTick 需要完整 UWorld
#include "TimerManager.h"           // ★1.0.8 FTimerManager::SetTimerForNextTick（筛选条延迟重建）
#include "Components/ComboBoxString.h"    // ★1.0.8 子类目筛选下拉（点箭头选条件）
#include "Types/SlateEnums.h"            // ★1.0.8 ESelectInfo::Type（下拉 OnSelectionChanged 第二参数）
#include "Blueprint/WidgetTree.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/Layout/Anchors.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"      // ★1.0.7i UWTFilterComboBox::HandleGenerateWidget 直造 STextBlock
#include "Widgets/SWidget.h"
#include "Input/Reply.h"
#include "Remote_FactoryTechBackground.h"
#include "Remote_FactoryCollectibles.h"
#include "FGMapAreaTexture.h"
#include "UObject/UObjectIterator.h"      // ★1.0.7i 按类名扫已加载描述符（枢纽未建造图标兜底）
#include "FGMapArea.h"

// ----------------------------------------------------------------------------
// ★1.0.28 自定义/skyblock 地图门禁：判定当前世界是否原版地图。
// 原版 Satisfactory 主地图为 /Game/FactoryGame/Map/GameLevel01/Persistent_Level；
// 自定义/skyblock 地图的关卡路径不含该标记 ⇒ 视为非原版，隐藏原版收集品/资源节点。
// 保守策略：世界未知时按原版处理（显示全部），避免误伤正常游戏。
static bool WT_IsVanillaMap(UWorld* World)
{
    if (!World)
    {
        return true;   // 世界未知 ⇒ 保守按原版（显示全部），不误伤
    }
    if (World->GetMapName().Contains(TEXT("GameLevel01")))
    {
        return true;
    }
    for (const ULevel* Lvl : World->GetLevels())
    {
        if (!Lvl)
        {
            continue;
        }
        UObject* Outer = Lvl->GetOuter();
        if (Outer && Outer->GetPathName().Contains(TEXT("FactoryGame/Map/GameLevel01")))
        {
            return true;
        }
    }
    return false;
}

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
// ★1.0.28h 展开态行背景（中青色）：与选中亮青(C_BTN_SEL)、空闲深蓝(C_BTN)三者明显区分；
// 展开行=「箭头向下▼」时以此高亮，让用户一眼看出当前展开的是哪一项（选择时同理=选中用 C_BTN_SEL）。
static const FLinearColor C_BTN_EXP  = FLinearColor(0.150f, 0.360f, 0.430f, 0.95f);  // expanded row

// ★1.0.7i 筛选条控件配色（用户实测：默认浅灰底 + 暗字在深色面板上对比度不足、文字看不清）。
// 三个值都是「比面板底色略亮一档」的冷蓝，配近白文字 ⇒ 关闭态与展开列表都高对比。
static const FLinearColor C_FLT_BG    = FLinearColor(0.098f, 0.135f, 0.180f, 1.f);  // 下拉关闭态底
static const FLinearColor C_FLT_HOVER = FLinearColor(0.160f, 0.255f, 0.320f, 1.f);  // 悬停/选中
static const FLinearColor C_FLT_ROW   = FLinearColor(0.070f, 0.094f, 0.125f, 1.f);  // 展开列表行底
static const FLinearColor C_FLT_DIM   = FLinearColor(0.400f, 0.470f, 0.540f, 1.f);  // 禁用/灰显文字

// ----------------------------------------------------------------------------
// ★1.0.7i 筛选下拉的深色主题样式
//
// 为什么必须自己刷样式：UComboBoxString 走的是 UMG 默认（浅灰）皮肤，本 mod 的面板是
// 近黑深蓝底 ⇒ 关闭态文字压在浅灰块上、展开列表又是浅灰行 + 深字，在深色面板里既刺眼
// 又看不清（用户实测「黑色主题下对比度不足」）。这里把关闭态按钮、下拉列表行、文字三处
// 全部换成同一套冷蓝色板，并统一字号（11）与内容边距，保证「一行能塞下 7~8 个」还不糊。
//
// 用 WhiteBrush 染色而不是自定义贴图：本 mod 不发行任何贴图资产，白图 + TintColor 就是
// 一个纯色块，且这个 brush 一定存在（MakeDivider 同样的理由：空 brush 会在绘制期崩）。
// ----------------------------------------------------------------------------
static FSlateBrush WT_SolidBrush(const FLinearColor& Tint)
{
    FSlateBrush B = *FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
    B.TintColor = FSlateColor(Tint);
    B.DrawAs = ESlateBrushDrawType::Image;
    B.SetImageSize(FVector2D(4.f, 4.f));
    return B;
}

/**
 * ★1.0.7i 下拉每一行的 Slate 生成器 —— **显式上色**，这是「深色主题下文字看不清」的正解。
 *
 * 原版实现是 `SNew(STextBlock).Text(...).Font(Font)`，颜色交给构造期捕获的
 * ForegroundColor（且 Init* 系列是 protected，外部改不了 —— 见 .h 里的说明）。
 * 这里直接给 ColorAndOpacity，关闭态（选中项走同一个生成器）与展开列表因此**必然同色**。
 */
TSharedRef<SWidget> UWTFilterComboBox::HandleGenerateWidget(TSharedPtr<FString> Item) const
{
    if (!Item.IsValid())
    {
        return SNew(STextBlock);
    }
    return SNew(STextBlock)
        .Text(FText::FromString(*Item))
        .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), WT_FontSize))
        .ColorAndOpacity(WT_ItemColor)
        .AutoWrapText(false);
}

/**
 * 统一给筛选下拉刷样式。**必须在 TakeWidget() 之前调用**：WidgetStyle/ItemStyle 是在
 * RebuildWidget 里一次性传给 SComboBox 的，Slate 控件建好后再改不会作用到已生成的控件。
 */
static void WT_StyleFilterCombo(UComboBoxString* Combo)
{
    if (!Combo)
    {
        return;
    }

    FButtonStyle Button;
    Button.SetNormal(WT_SolidBrush(C_FLT_BG));
    Button.SetHovered(WT_SolidBrush(C_FLT_HOVER));
    Button.SetPressed(WT_SolidBrush(C_FLT_HOVER));
    Button.SetDisabled(WT_SolidBrush(C_FLT_BG));
    Button.SetNormalPadding(FMargin(2.f, 2.f));
    Button.SetPressedPadding(FMargin(2.f, 3.f));

    // 从现有样式出发（保住游戏自带的箭头贴图），只覆盖底/边/内边距三处。
    FComboBoxStyle Style = Combo->GetWidgetStyle();
    FComboButtonStyle ComboBtn = Style.ComboButtonStyle;
    ComboBtn.SetButtonStyle(Button);
    ComboBtn.SetMenuBorderBrush(WT_SolidBrush(C_FLT_BG));
    ComboBtn.SetMenuBorderPadding(FMargin(1.f));
    ComboBtn.SetContentPadding(FMargin(4.f, 1.f));
    ComboBtn.DownArrowImage.TintColor = FSlateColor(C_ACCENT);   // 深底上的箭头必须提亮
    Style.SetComboButtonStyle(ComboBtn);
    Combo->SetWidgetStyle(Style);

    // 展开列表：奇数/偶数行、悬停、选中四种状态全部给深底，文字近白、选中项青色。
    FTableRowStyle Rows = Combo->GetItemStyle();
    Rows.SetEvenRowBackgroundBrush(WT_SolidBrush(C_FLT_ROW));
    Rows.SetEvenRowBackgroundHoveredBrush(WT_SolidBrush(C_FLT_HOVER));
    Rows.SetOddRowBackgroundBrush(WT_SolidBrush(C_FLT_ROW));
    Rows.SetOddRowBackgroundHoveredBrush(WT_SolidBrush(C_FLT_HOVER));
    Rows.SetActiveBrush(WT_SolidBrush(C_FLT_HOVER));
    Rows.SetActiveHoveredBrush(WT_SolidBrush(C_FLT_HOVER));
    Rows.SetInactiveBrush(WT_SolidBrush(C_FLT_ROW));
    Rows.SetInactiveHoveredBrush(WT_SolidBrush(C_FLT_HOVER));
    Rows.SetTextColor(FSlateColor(C_VALUE));
    Rows.SetSelectedTextColor(FSlateColor(C_ACCENT));
    Combo->SetItemStyle(Rows);

    // ★1.0.7i 文字色/字号走本 mod 的子类字段（原版 InitForegroundColor/InitFont 是 protected，
    // 外部不可调用）。必须在 TakeWidget() 之前赋值 —— HandleGenerateWidget 是建控件时读的。
    if (UWTFilterComboBox* FilterCombo = Cast<UWTFilterComboBox>(Combo))
    {
        FilterCombo->WT_ItemColor = FSlateColor(C_VALUE);
        FilterCombo->WT_FontSize  = 11.f;
    }
    Combo->SetContentPadding(FMargin(4.f, 1.f));
    Combo->SetMaxListHeight(420.f);
}

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
                           UTextBlock** OutLabel, ETextJustify::Type Justify = ETextJustify::Center,
                           int32 FontSize = 13, bool bFillContent = false)
{
    UButton* B = Tree->ConstructWidget<UButton>();
    B->SetBackgroundColor(BgColor);
    UTextBlock* T = MakeText(Tree, InText, FontSize, false, C_VALUE, Justify);
    if (UButtonSlot* BS = Cast<UButtonSlot>(B->AddChild(T)))
    {
        // ★1.0.10 UButtonSlot 默认 HAlign_Center：对「占满整行」的按钮（列表分组头）来说，
        // 文本会缩到中间，与下面缩进对齐的行完全对不上。bFillContent=true 时让内容撑满整宽，
        // Justify=Left 才能真正生效。
        BS->SetHorizontalAlignment(bFillContent ? HAlign_Fill : HAlign_Center);
        if (bFillContent)
        {
            BS->SetPadding(FMargin(6.f, 2.f));
        }
    }
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

// ----------------------------------------------------------------------------
// ★1.0.8 生产状态：颜色与文字的唯一来源
// ----------------------------------------------------------------------------
// 编码见 FWT_MachineEntry::ProdState（255=不适用，0=空闲，1=运行中，2=待机，3=阻塞）。
// 下面两个函数必须**成对**修改：灯色与文字共用同一套 case，所以「绿灯配运行中」这种对应
// 关系在结构上就成立，不存在「灯绿着、字写待机」的漂移。
static FLinearColor WT_ProdStateColor(uint8 Code)
{
    switch (Code)
    {
        case 1:  return C_GREEN;                        // 运行中
        case 2:  return C_AMBER;                        // 待机
        case 3:  return C_RED;                          // 阻塞
        case 0:  return C_LABEL;                        // 空闲（冷灰，不抢眼）
        default: return FLinearColor::Transparent;      // 255 = 该类目无状态，调用方不建灯
    }
}

static FText WT_ProdStateLabel(uint8 Code)
{
    switch (Code)
    {
        case 1:  return WT_LOCTEXT("ProdState_Running", "运行中");
        case 2:  return WT_LOCTEXT("ProdState_Standby", "待机");
        case 3:  return WT_LOCTEXT("ProdState_Blocked", "阻塞");
        case 0:  return WT_LOCTEXT("ProdState_Idle",    "空闲");
        default: return FText::GetEmpty();
    }
}

/**
 * ★1.0.8 状态指示灯：8×8 实心圆点。
 *
 * 用引擎 WhiteBrush 染色而不是自定义贴图 —— 本 mod 不发 UMG/贴图资产，
 * WhiteBrush 是 CoreStyle 里必然存在的资源（同 MakeDivider 的既有做法）。
 * UImage 默认按贴图原始尺寸撑开，WhiteBrush 是 1×1，必须显式给 DesiredSizeOverride，
 * 否则要么看不见、要么被拉伸成整行。
 */
static UImage* MakeStateDot(UWidgetTree* Tree, FLinearColor Color)
{
    UImage* Dot = Tree->ConstructWidget<UImage>();
    Dot->SetBrush(*FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")));
    // ★一手核对：Image.h:68 `void SetColorAndOpacity(FLinearColor InColorAndOpacity)`
    // —— 只吃 FLinearColor，包一层 FSlateColor 会 C2664（本轮实锤）。
    Dot->SetColorAndOpacity(Color);
    Dot->SetDesiredSizeOverride(FVector2D(8.f, 8.f));
    return Dot;
}

/**
 * ★1.0.8 单条机器/容器记录是否匹配搜索词。
 *
 * 与旧逻辑的区别：只在**当前类目**内匹配（用户定案「每个搜索对应当前列表」），
 * 不再跨类目汇总。匹配字段 = 名称 / 产物名 / 状态描述 / MOD 名，
 * 这样搜「铁板」能命中生产铁板的机器，搜「铁矿」能命中采矿机。
 */
static bool WT_EntryMatchesSearch(const FWT_MachineEntry& E, const FString& Token)
{
    if (Token.IsEmpty())
    {
        return true;
    }
    if (E.DisplayName.ToString().Contains(Token, ESearchCase::IgnoreCase)
        || E.ProductName.ToString().Contains(Token, ESearchCase::IgnoreCase)
        || E.StatusDetail.ToString().Contains(Token, ESearchCase::IgnoreCase)
        || E.ModName.Contains(Token, ESearchCase::IgnoreCase))
    {
        return true;
    }
    // ★1.0.10 物品标签也参与搜索：用户要求「物品（可用搜索框搜索）」。
    // 只匹配 StatusLine 是不够的 —— 那只是**首格**存货，第二格装着铁板的箱子搜不到。
    // 这里查的是整箱/整机全部物品标签，等价于「按内容物全局搜箱子」。
    for (const FWT_ItemTag& Tag : E.ItemTags)
    {
        if (Tag.Label.Contains(Token, ESearchCase::IgnoreCase))
        {
            return true;
        }
    }
    return false;
}

/**
 * ★1.0.10 库存件数 → 数量档（筛选条「数量」维度与下拉计数共用同一份判定）。
 * 档位：0 空 / 1 少量(<1000) / 2 中量(<10000) / 3 大量(≥10000)。
 *
 * ★必须被「过滤」和「计数」同时使用：两处各写一遍 if-else，改档位边界时只改一处就会出现
 * 「下拉写 3 行、点下去 5 行」的假数字（这类偏差之前已用穷举验证过一遍）。
 * 传入值必须 ≥ 0；负数是「不可计量」，由调用点先排除，不会走到这里。
 */
static int32 WT_AmountBandOf(int32 Count)
{
    if (Count <= 0)
    {
        return 0;
    }
    if (Count < 1000)
    {
        return 1;
    }
    if (Count < 10000)
    {
        return 2;
    }
    return 3;
}

/**
 * ★1.0.11 比例档（效率 / 填充率共用）：把 0~100 的百分比分到 4 档。
 * 负数 = 不可计量，返回 -1；调用点据此让该行「不参与」比例维筛选（不是当 0 处理）。
 */
static int32 WT_RatioBandOf(float Percent)
{
    if (Percent < 0.f)
    {
        return -1;
    }
    if (Percent <= 25.f)  { return 0; }
    if (Percent <= 50.f)  { return 1; }
    if (Percent <= 75.f)  { return 2; }
    return 3;
}

/**
 * ★1.0.11 问题维匹配。Sel 的编码与下拉选项一一对应：
 *   -1 = 全部（不过滤）；0 = 任意问题（IssueFlags != 0）；1..8 = EWT_Issue 枚举值 + 1（按位与）。
 *
 * 为什么 +1：下拉第 0 项必须留给「全部」，否则「全部」与「OutputBlocked(0)」会撞成同一个值，
 * 玩家选「全部」实际筛出的是堵塞机器 —— 这种编码歧义在 UI 上完全看不出来，只能靠编码规避。
 */
static bool WT_IssueMatches(uint8 Flags, int32 Sel)
{
    if (Sel < 0)
    {
        return true;
    }
    if (Sel == 0)
    {
        return Flags != 0;
    }
    const uint8 Bit = static_cast<uint8>(1u << static_cast<uint8>(Sel - 1));
    return (Flags & Bit) != 0;
}

/**
 * ★1.0.11 本行参与「比例」维度筛选的百分比值：
 *   容器 → 填充率（FillPercent）；其余 → 效率（Efficiency01 × 100）。
 * 两者都不可计量时返回 -1 ⇒ 该行不参与比例筛选（与「数量」维度同一套「不参与」语义）。
 */
static float WT_RowRatioPercent(const FWT_MachineEntry& E)
{
    if (E.Category == static_cast<uint8>(EWT_MachineCategory::Container) && E.FillPercent >= 0.f)
    {
        return E.FillPercent;
    }
    return (E.Efficiency01 >= 0.f) ? (E.Efficiency01 * 100.f) : -1.f;
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
        case EWT_MachineCategory::Vehicle:         return WT_LOCTEXT("Cat_Vehicle",         "运输工具");
        case EWT_MachineCategory::Portal:          return WT_LOCTEXT("Cat_Portal",          "传送门");
        case EWT_MachineCategory::Station:         return WT_LOCTEXT("Cat_Station",         "车站");
        case EWT_MachineCategory::ResourceNode_Vanilla: return WT_LOCTEXT("Cat_RNVanilla", "原版");
        case EWT_MachineCategory::ResourceNode_Mod:     return WT_LOCTEXT("Cat_RNMod",     "MOD");
        // ★1.0.9 电网（内容 = 电力曲线 + 五项图例 + 每条电路一行，见 RebuildGridView）
        case EWT_MachineCategory::PowerGrid:            return WT_LOCTEXT("Cat_PowerGrid", "电网");
        default:                                 return WT_LOCTEXT("Cat_Other",         "Other");
    }
}

/**
 * ★1.0.9 电网曲线的通道配色 / 名称：**唯一来源**。
 *
 * 曲线（UWTPowerGraphWidget 的线）和图例（左边那个小色块 + 文字）都调这两个函数，
 * 所以「橙色那条线 == 图例里的消耗」在结构上成立，不会出现改了一处另一处漂掉。
 * 顺序与原版电线杆 UI 的图例一致：消耗 / 最大消耗 / 发电 / 上限 / 能源增幅
 * （色板也对齐原版：橙 / 青 / 灰白 / 浅灰 / 紫）。
 */
static FLinearColor WT_GridSeriesColor(uint8 Kind)
{
    switch (Kind)
    {
        case 0:  return C_AMBER;    // 消耗
        case 1:  return C_ACCENT;   // 最大消耗（用户点名要看的「电力峰值」）
        case 2:  return C_VALUE;    // 发电
        case 3:  return C_LABEL;    // 上限
        case 4:  return C_CAVE;     // 能源增幅
        default: return C_LABEL;
    }
}

static FText WT_GridSeriesLabel(uint8 Kind)
{
    switch (Kind)
    {
        case 0:  return WT_LOCTEXT("Grid_Consumed", "消耗");
        case 1:  return WT_LOCTEXT("Grid_Peak",     "最大消耗");
        case 2:  return WT_LOCTEXT("Grid_Produced", "发电");
        case 3:  return WT_LOCTEXT("Grid_Capacity", "上限");
        case 4:  return WT_LOCTEXT("Grid_Boost",    "能源增幅");
        default: return FText::GetEmpty();
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
    // 面板占满全屏、字号巨大）。改成 CanvasPanel 做根，把面板按"视口的 90% 宽 / 85% 高、
    // 上限 1280x820"居中（屏幕正中）；分辨率变化在 NativeConstruct 里再校一次。
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
        // ★2026-11-03 用户要求：真正居中（屏幕正中）。
        CanvasSlot->SetAnchors(FAnchors(0.5f, 0.5f));
        CanvasSlot->SetAlignment(FVector2D(0.5f, 0.5f));
        CanvasSlot->SetPosition(FVector2D(0.f, 0.f));
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

    // ★2026-11-02 智能仓储开关（标题栏，紧跟 Refresh 之后）。
    {
        SmartWarehouseButton = MakeButton(WidgetTree, FText(), C_BTN, &SmartWarehouseLabel);
        SmartWarehouseButton->OnClicked.AddDynamic(this, &URemote_FactoryPanelWidget::OnSmartWarehouseToggleClicked);
        if (UHorizontalBoxSlot* S = TitleRow->AddChildToHorizontalBox(SmartWarehouseButton))
        {
            S->SetPadding(FMargin(4.f, 0.f));
            S->SetVerticalAlignment(VAlign_Center);
            S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
        }
        if (SmartWarehouseLabel)
        {
            SmartWarehouseLabel->SetText(Remote_FactorySmartWarehouse::IsSmartWarehouseEnabled()
                ? WT_LOCTEXT("SW_On_Init", "智能仓储: 开")
                : WT_LOCTEXT("SW_Off_Init", "智能仓储: 关"));
        }
    }

    // ★2026-11-02 里程碑货舱即刻返回开关（标题栏，紧跟智能仓储之后）。
    {
        MilestoneCargoReturnButton = MakeButton(WidgetTree, FText(), C_BTN, &MilestoneCargoReturnLabel);
        MilestoneCargoReturnButton->OnClicked.AddDynamic(this, &URemote_FactoryPanelWidget::OnMilestoneCargoReturnToggleClicked);
        if (UHorizontalBoxSlot* S = TitleRow->AddChildToHorizontalBox(MilestoneCargoReturnButton))
        {
            S->SetPadding(FMargin(4.f, 0.f));
            S->SetVerticalAlignment(VAlign_Center);
            S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
        }
        if (MilestoneCargoReturnLabel)
        {
            MilestoneCargoReturnLabel->SetText(Remote_FactoryMilestoneCargoReturn::IsMilestoneCargoReturnEnabled()
                ? WT_LOCTEXT("MCR_On_Init", "里程碑货舱即刻返回: 开")
                : WT_LOCTEXT("MCR_Off_Init", "里程碑货舱即刻返回: 关"));
        }
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

    // ---- ★本轮 顶部枢纽快捷图标栏（搜索框上方）----
    // ★1.0.28c 实测截图修正：WrapBox 在面板宽度内装不下 7 格会换行成两排且按钮错位 ——
    // 改用 HorizontalBox 单行、7 格 Fill=1 均分可用宽度，永不换行。
    HubShortcutBox = WidgetTree->ConstructWidget<UHorizontalBox>();
    if (UVerticalBoxSlot* S = Root->AddChildToVerticalBox(HubShortcutBox))
    {
        S->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
    }

    // ---- ★1.0.8 搜索框（原「顶部统一搜索框」→ 现内嵌在中列列表顶部）----------
    // 用户定案：「将搜索框改为内嵌在每个独立列表中、不再统一直接显示，每个列表仅搜索自身条目」。
    // 物理上仍只有一个 SearchBox，但它已建在**中列列表**里（MachineCol 内、筛选条之上），
    // 视觉上就长在当前列表头上；每个子类目各记一份输入（CategorySearchTexts），
    // 切类目时还原各自的关键词，列表也只过滤本类目自己的条目。
    // 提示文案同步改口径：新键（SearchHintLocal / SearchTipLocal）而不是沿用旧键 ——
    // 旧键在已编译的 locres 里有「跨所有类目」的旧译文，改 Source 不会生效。
    SearchBox = WidgetTree->ConstructWidget<UEditableTextBox>();
    SearchBox->SetHintText(WT_LOCTEXT("SearchHintLocal", "搜索当前列表…"));
    SearchBox->SetToolTipText(WT_LOCTEXT("SearchTipLocal", "只过滤当前子类目列表内的条目；每个类目各记一份关键词"));
    SearchBox->OnTextChanged.AddDynamic(this, &URemote_FactoryPanelWidget::OnSearchTextChanged);
    // ★本轮 修「输入字体看不见 + 框太大」：显式设置字体与前景色（亮色），并约束高度避免默认过大。
    // 直接改写 WidgetStyle 成员（本引擎快照无 SetWidgetStyle / SetCaretColor）。
    {
        FEditableTextBoxStyle& ES = SearchBox->WidgetStyle;
        FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 14);
        ES.SetFont(Font);
        ES.SetForegroundColor(FSlateColor(C_VALUE));          // 已输入文本颜色（亮）
        ES.SetReadOnlyForegroundColor(FSlateColor(C_LABEL));  // 占位提示颜色
        ES.SetBackgroundColor(FSlateColor(FLinearColor(0.13f, 0.17f, 0.22f, 1.f)));  // ★1.0.28b 提亮：近黑在深色面板上几乎隐形（实测截图）
    }
    USizeBox* SearchBoxWrap = WidgetTree->ConstructWidget<USizeBox>();
    SearchBoxWrap->SetHeightOverride(32.f);
    if (USizeBoxSlot* WS = Cast<USizeBoxSlot>(SearchBoxWrap->AddChild(SearchBox)))
    {
        WS->SetPadding(FMargin(0.f));
    }
    // ★1.0.8 这里**不再**往 Root 顶部挂：SearchBoxWrap 改由下方中列 MachineCol 接管，
    // 使搜索框成为「列表自己的」控件而非全局控件。变量在本函数作用域内，稍后直接用。

    // ---- three-column body ---------------------------------------------------
    UHorizontalBox* Body = WidgetTree->ConstructWidget<UHorizontalBox>();

    // left: categories -- v0.2.55: vertical stack (分类标签移到左侧一列)
    // v0.2.66: wrap the stack in a ScrollBox so the expanded alien-tech (37+) / machine lists
    // can scroll instead of overflowing and clipping the panel.
    // ★2026-10-25 左列定宽（用户实测反馈）：此前该列是 auto 宽，类目树里出现长提示行
    // （纯度来源/矿点数据闸门，单行 ~700px）时整列被撑到 ~950px，机器列表被挤到面板
    // 最右 ~300px（"列表占据大半屏、传送点被挤到最右边"）。用 SizeBox 钉住左列宽度，
    // 长提示行在列内换行（MakeButton 的文本已开 SetAutoWrapText），机器列表恢复为主列。
    USizeBox* CategoryWidthBox = WidgetTree->ConstructWidget<USizeBox>();
    CategoryWidthBox->SetWidthOverride(300.f);
    UScrollBox* CategoryScroll = WidgetTree->ConstructWidget<UScrollBox>();
    CategoryScroll->SetScrollbarThickness(FVector2D(8.f, 8.f));
    CategoryBox = WidgetTree->ConstructWidget<UVerticalBox>();
    if (UScrollBoxSlot* CS = Cast<UScrollBoxSlot>(CategoryScroll->AddChild(CategoryBox)))
    {
        CS->SetPadding(FMargin(0.f));
    }
    if (USizeBoxSlot* WBS = Cast<USizeBoxSlot>(CategoryWidthBox->AddChild(CategoryScroll)))
    {
        WBS->SetPadding(FMargin(0.f));
    }
    if (UHorizontalBoxSlot* S = Body->AddChildToHorizontalBox(CategoryWidthBox))
    {
        S->SetVerticalAlignment(VAlign_Fill);
        S->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
    }

    // middle: machine rows
    // ★1.0.28i 中列改 VerticalBox：顶部纯度过滤条(PurityBarBox) + 滚动列表(MachineListBox)，
    // 过滤条在 ScrollBox 之外，不随列表滚动消失；仅资源节点类目才往 PurityBarBox 里塞内容。
    // ★1.0.8 中列顺序定为「搜索框 → 筛选条 → 列表」：三者都在 ScrollBox 之外，
    // 列表滚动时搜索框与筛选条始终固定可见（内嵌进列表正是用户要的形态）。
    UVerticalBox* MachineCol = WidgetTree->ConstructWidget<UVerticalBox>();
    if (UVerticalBoxSlot* SS = MachineCol->AddChildToVerticalBox(SearchBoxWrap))
    {
        SS->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
    }
    PurityBarBox = WidgetTree->ConstructWidget<UVerticalBox>();
    {
        // ★1.0.11 用户方案稿「控件区高度固定：切类目时列表不能跳动」。
        // 各类目维度数不同（收集品 1 行 / 生产设备 2 行），直接挂进 VerticalBox 会让列表起点
        // 随类目上下跳几十像素。外面套一个固定高度的 SizeBox：永远预留两行的高度，
        // 行少时下方留白（留白比跳动好，跳动会让玩家点错行）。
        // ★1.0.11 局部名刻意不叫 FilterHeightBox：与成员同名会触发 C4458
        //（声明隐藏类成员），本项目把警告当错误，直接编不过。
        USizeBox* FilterWrap = WidgetTree->ConstructWidget<USizeBox>();
        // ★1.0.7i 筛选条压缩到**一行**（用户定案），所以固定高度从「预留两行」的 62 降到 30。
        // 行数恒为 1 ⇒ 切类目时列表起点仍然不跳（固定高度的初衷保留）。
        FilterWrap->SetHeightOverride(30.f);
        // ★1.0.11 存成员：RebuildFilterBar 要在「该类目一个维度都不建」时把它压成 0。
        FilterHeightBox = FilterWrap;
        if (USizeBoxSlot* FS = Cast<USizeBoxSlot>(FilterWrap->AddChild(PurityBarBox)))
        {
            FS->SetHorizontalAlignment(HAlign_Fill);
            FS->SetVerticalAlignment(VAlign_Top);
        }
        if (UVerticalBoxSlot* PS = MachineCol->AddChildToVerticalBox(FilterWrap))
        {
            // ★1.0.7i 用户诉求「列表与过滤条件别贴太近」：下边距 4 → 12。
            // 单行化之后筛选条自己只占 30px，靠这点留白把「筛选区」和「数据区」视觉上分开。
            PS->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));
        }
    }

    // ---- ★1.0.9 【电网】类目：曲线图 + 五项图例 --------------------------------
    // 用户定案：「新增一个电网类目，打开这个类目直接可以看到截图那块」。
    // 所以图挂在**中列**（列表自己的头顶），不是右列 —— 右列详情面板 0.2.57 起就没接进布局。
    //
    // 常驻但默认 Collapsed：SWTPowerGraph 是自绘 Slate 控件，销毁后要重建 Slate 资源，
    // 而切类目很频繁；这里只切 Visibility，控件本身一直活着。
    GridHeaderBox = WidgetTree->ConstructWidget<UVerticalBox>();
    GridHeaderBox->SetVisibility(ESlateVisibility::Collapsed);
    {
        USizeBox* GraphSize = WidgetTree->ConstructWidget<USizeBox>();
        GraphSize->SetHeightOverride(132.f);   // 与原版那块图的观感接近，且不把列表压没
        GridGraph = WidgetTree->ConstructWidget<UWTPowerGraphWidget>();
        if (USizeBoxSlot* GS = Cast<USizeBoxSlot>(GraphSize->AddChild(GridGraph)))
        {
            GS->SetPadding(FMargin(0.f));
        }
        if (UVerticalBoxSlot* S = GridHeaderBox->AddChildToVerticalBox(GraphSize))
        {
            S->SetPadding(FMargin(0.f, 0.f, 12.f, 4.f));
        }

        // 图例：3 行 × 2 列（最后一格空着），信息量与原版一致但只占 ~48px 高。
        // 色块颜色与曲线同源（都走 WT_GridSeriesColor），所以「橙线 == 消耗」不会漂。
        GridLegendValueTexts.Reset();
        GridLegendValueTexts.SetNum(5);
        for (int32 Row = 0; Row < 3; ++Row)
        {
            UHorizontalBox* RowBox = WidgetTree->ConstructWidget<UHorizontalBox>();
            for (int32 Col = 0; Col < 2; ++Col)
            {
                const int32 Kind = Row * 2 + Col;
                if (Kind > 4) { break; }

                UHorizontalBox* Cell = WidgetTree->ConstructWidget<UHorizontalBox>();

                UImage* Swatch = WidgetTree->ConstructWidget<UImage>();
                Swatch->SetBrush(*FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")));
                Swatch->SetColorAndOpacity(WT_GridSeriesColor(static_cast<uint8>(Kind)));
                Swatch->SetDesiredSizeOverride(FVector2D(8.f, 8.f));
                if (UHorizontalBoxSlot* SS2 = Cell->AddChildToHorizontalBox(Swatch))
                {
                    SS2->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
                    SS2->SetVerticalAlignment(VAlign_Center);
                }

                UTextBlock* NameTb = MakeText(WidgetTree, WT_GridSeriesLabel(static_cast<uint8>(Kind)),
                                              12, false, WT_GridSeriesColor(static_cast<uint8>(Kind)));
                NameTb->SetAutoWrapText(false);
                if (UHorizontalBoxSlot* NS = Cell->AddChildToHorizontalBox(NameTb))
                {
                    NS->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
                    NS->SetVerticalAlignment(VAlign_Center);
                }

                UTextBlock* ValTb = MakeText(WidgetTree, FText::FromString(TEXT("—")),
                                             12, false, C_VALUE);
                ValTb->SetAutoWrapText(false);
                GridLegendValueTexts[Kind] = ValTb;
                if (UHorizontalBoxSlot* VS2 = Cell->AddChildToHorizontalBox(ValTb))
                {
                    VS2->SetVerticalAlignment(VAlign_Center);
                }

                if (UHorizontalBoxSlot* CS2 = RowBox->AddChildToHorizontalBox(Cell))
                {
                    CS2->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
                    CS2->SetPadding(FMargin(0.f, 1.f));
                }
            }
            if (UVerticalBoxSlot* S = GridHeaderBox->AddChildToVerticalBox(RowBox))
            {
                S->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
            }
        }
    }
    if (UVerticalBoxSlot* GS2 = MachineCol->AddChildToVerticalBox(GridHeaderBox))
    {
        GS2->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
    }

    MachineListBox = WidgetTree->ConstructWidget<UScrollBox>();
    if (UVerticalBoxSlot* LS = MachineCol->AddChildToVerticalBox(MachineListBox))
    {
        LS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));  // 列表占满中列剩余高度
        LS->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
    }
    if (UHorizontalBoxSlot* S = Body->AddChildToHorizontalBox(MachineCol))
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

    // ★v0.2.24 按真实视口把面板尺寸收敛到"视口 90% 宽 / 85% 高，上限 1280x820"，居中（锚点正中）。
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
            // ★2026-11-03 用户要求：真正居中（与 RebuildWidget 对齐）。
            CanvasSlot->SetAnchors(FAnchors(0.5f, 0.5f));
            CanvasSlot->SetAlignment(FVector2D(0.5f, 0.5f));
            CanvasSlot->SetPosition(FVector2D(0.f, 0.f));
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
    RebuildHubShortcuts();   // ★本轮 顶部枢纽图标栏：开面板即展示固定 6+1 个（未建造时灰显）
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

// ★本轮 客户端直读固体容器复制的库存组件，实时算总量（0.1s tick 用）。
// 与 RCO 的 WT_InvTotalItems 同口径；这里走 Actor 而非扫描快照，所以能反映实时变化。
// 返回 -1 表示非固体容器 / 无库存组件（调用方跳过，如液体罐无数量 API）。
static int32 WT_LiveStorageTotal(AActor* A)
{
    if (AFGBuildableStorage* S = Cast<AFGBuildableStorage>(A))
    {
        if (const UFGInventoryComponent* Inv = S->GetStorageInventory())
        {
            TArray<FInventoryStack> Stacks;
            Inv->GetInventoryStacks(Stacks, /*getEmptyStacks=*/false);
            int32 T = 0;
            for (const FInventoryStack& St : Stacks)
            {
                if (St.HasItems())
                {
                    T += St.NumItems;
                }
            }
            return T;
        }
    }
    return -1;
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

            // ★本轮 容器存量实时刷新：直读复制的库存组件，总量变化时刷新行文本（避免每帧 SetText）。
            for (auto It = WT_LiveContainerRows.CreateIterator(); It; ++It)
            {
                FWT_LiveContainerRow& R = It.Value();
                UTextBlock* Tb = R.Text.Get();
                AActor*     A  = R.Actor.Get();
                if (!Tb || !A)
                {
                    It.RemoveCurrent();
                    continue;
                }
                const int32 T = WT_LiveStorageTotal(A);
                if (T < 0 || T == R.LastTotal)
                {
                    continue;
                }
                R.LastTotal = T;
                Tb->SetText(FText::Format(FText::FromString(TEXT("共 {0} 件")), FText::AsNumber(T)));
            }
            // 选中容器：详情面板同步实时总量（与列表行同口径）。
            if (SelectedEntry >= 0 && SelectedEntry < CurrentList.Entries.Num())
            {
                const FWT_MachineEntry& SE = CurrentList.Entries[SelectedEntry];
                if (SE.Category == static_cast<uint8>(EWT_MachineCategory::Container))
                {
                    if (AActor* A = SE.Target.Get())
                    {
                        const int32 T = WT_LiveStorageTotal(A);
                        if (T >= 0 && DetailStatusText)
                        {
                            DetailStatusText->SetText(FText::Format(
                                FText::FromString(TEXT("{0}m away · 共 {1} 件")),
                                FText::AsNumber(FMath::RoundToInt(SE.DistanceMeters)),
                                FText::AsNumber(T)));
                        }
                    }
                }
            }
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
    MergeResourceNodesIntoList();   // ★本轮: 从 Lib 并入全图资源节点（建筑组-资源节点子目）

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
    RebuildHubShortcuts();   // ★本轮 顶部枢纽图标栏随扫描刷新（建筑存在性/图标/距离）

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
    static UClass* WT_ResolveInteractWidgetClass(AActor* Target)
    {
        if (!IsValid(Target))
        {
            return nullptr;
        }

        // ★2026-11-03 便携式采矿机：它是 AActor（非 AFGBuildable），UI 类挂在公开的
        // mInteractWidgetClass（TSubclassOf<UFGInteractWidget>，FGPortableMiner.h:134-135），
        // 不走建筑的 mInteractWidgetSoftClass。直接读公开属性即可。
        if (AFGPortableMiner* PM = Cast<AFGPortableMiner>(Target))
        {
            return PM->mInteractWidgetClass;
        }

        // ★2026-11-03 崩溃修复：死亡箱子(AFGCrate 非 AFGBuildable)点「打开面板」会崩——
        // FindFProperty 取的是 AFGBuildable 的属性，却把非子类实例套上去触发 IsA 断言。
        // 必须先校验 Target 真是 AFGBuildable 再读其属性；非 AFGBuildable(箱子)落入此守卫外 → 返回 nullptr 不崩。
        if (AFGBuildable* B = Cast<AFGBuildable>(Target))
        {
            if (FSoftClassProperty* Prop = FindFProperty<FSoftClassProperty>(
                    AFGBuildable::StaticClass(), TEXT("mInteractWidgetSoftClass")))
            {
                const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(B);
                if (ValuePtr)
                {
                    const FSoftObjectPtr& SoftRef = *static_cast<const FSoftObjectPtr*>(ValuePtr);
                    if (UClass* Loaded = Cast<UClass>(SoftRef.LoadSynchronous()))
                    {
                        return Loaded;
                    }
                }
            }
        }

        return nullptr;
    }
}

void URemote_FactoryPanelWidget::HandleInteractUIRequested(AActor* Target)
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

void URemote_FactoryPanelWidget::HandlePullResult(AActor* Target, int32 ItemsMoved, bool bSuccess)
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

void URemote_FactoryPanelWidget::OnSmartWarehouseToggleClicked()
{
    // ★2026-11-04 联机修复：开关状态是**存档里的服务端状态**，不能只改本地就完事。
    //   联机时若只改本地，会出现"客户端面板显示有集装箱货、但服务端没开 ⇒ 取货点了没反应"。
    //   现在：只发请求 → 服务端权威写入并回下发 → Client_SetSmartWarehouse 写本地并刷新。
    //   本地那一份由 RCO 的 Client 回调统一写，这里**不写**，避免两边打架。
    //   单机/Host 也走同一路径（Server RPC 就地执行），不需要分支。
    const bool bNew = !Remote_FactorySmartWarehouse::IsSmartWarehouseEnabled();
    bool bSent = false;
    if (URemote_FactoryRCO* RCO = GetRCO())
    {
        RCO->Server_SetSmartWarehouse(bNew);
        bSent = true;
    }
    if (!bSent)
    {
        // 兜底：拿不到 RCO（极早期/非标准环境）时退回本地切换，至少让单人玩家还能用。
        Remote_FactorySmartWarehouse::SetSmartWarehouseEnabled(bNew);
    }
    if (SmartWarehouseLabel)
    {
        SmartWarehouseLabel->SetText(bNew
            ? WT_LOCTEXT("SW_On_Toggle", "智能仓储: 开")
            : WT_LOCTEXT("SW_Off_Toggle", "智能仓储: 关"));
    }
}

void URemote_FactoryPanelWidget::OnMilestoneCargoReturnToggleClicked()
{
    const bool bNew = !Remote_FactoryMilestoneCargoReturn::IsMilestoneCargoReturnEnabled();
    Remote_FactoryMilestoneCargoReturn::SetMilestoneCargoReturnEnabled(bNew);
    if (MilestoneCargoReturnLabel)
    {
        MilestoneCargoReturnLabel->SetText(bNew
            ? WT_LOCTEXT("MCR_On_Toggle", "里程碑货舱即刻返回: 开")
            : WT_LOCTEXT("MCR_Off_Toggle", "里程碑货舱即刻返回: 关"));
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
        // ★2026-11-03 放宽为 AActor：便携式采矿机（AActor 非 AFGBuildable）也能经此打开 UI。
        RCO->Server_RequestOpenUI(Entry->Target);
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
        // ★1.0.8 死亡箱子修复：取料按钮原本只接受 AFGBuildable，死亡箱(AFGCrate)被漏掉→点了没反应。
        // Server_PullOutput 收 AActor*，内部已分派 AFGBuildableManufacturer / AFGCrate 两条支线，
        // 故这里直接喂 AActor*，制造商与死亡箱都能取料。
        if (AActor* Act = Cast<AActor>(Entry->Target))
        {
            // ★验证标记（测试版用，发布前删）：确认"取料支持死亡箱"已编进 DLL
            WT_WARNF(TEXT("[pull-108fix] actor-path pull -> Server_PullOutput(%s)"), *GetNameSafe(Act));
            RCO->Server_PullOutput(Act);
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
                                                     UTextBlock** OutLabel, const FText* ArrowGlyph,
                                                     bool bExpanded)
{
    WT_ZONE(TEXT("Panel.AddListButton"));   // 每行一个；上千行时这里会累积成可见卡顿
    UTextBlock* LabelText = nullptr;
    UButton* Button = WidgetTree->ConstructWidget<UButton>();
    // ★1.0.28h 三级背景：选中(亮青) > 展开(中青) > 空闲(深蓝)，三者一眼可辨
    Button->SetBackgroundColor(bSelected ? C_BTN_SEL : (bExpanded ? C_BTN_EXP : C_BTN));

    // ★1.0.28e 左列行按钮内容对齐修复：UButtonSlot 默认 HAlign_Center/VAlign_Center
    //（引擎源码 ButtonSlot.cpp:19-20 实锤）⇒ 内容 HorizontalBox 自动收缩后被整体居中，
    // 表现为「文本居中、三角贴文本右侧」。这里把水平改成 HAlign_Fill 让内容撑满按钮宽
    // ⇒ 文本居左（TextJustify=Left）、三角贴右；竖直保持 VAlign_Center（行高按内容自适应）。
    // 注意：AddChild 直接返回 UPanelSlot*（本会话已实锤），Cast<UButtonSlot> 后再设对齐。
    auto ApplyButtonSlotFill = [](UPanelSlot* InSlot)
    {
        if (UButtonSlot* BS = Cast<UButtonSlot>(InSlot))
        {
            BS->SetHorizontalAlignment(HAlign_Fill);
            BS->SetVerticalAlignment(VAlign_Center);
        }
    };

    if (ArrowGlyph)
    {
        // ★本轮 文本靠左填充 + 折叠三角靠右对齐（用户诉求：左侧列表左对齐、三角统一靠右）
        UHorizontalBox* HB = WidgetTree->ConstructWidget<UHorizontalBox>();
        UTextBlock* T = MakeText(WidgetTree, Label, 13, false, bSelected ? C_ON_SEL : C_VALUE, ETextJustify::Left);
        if (UHorizontalBoxSlot* S = HB->AddChildToHorizontalBox(T))
        {
            S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            S->SetVerticalAlignment(VAlign_Center);
        }
        UTextBlock* A = MakeText(WidgetTree, *ArrowGlyph, 13, false, bSelected ? C_ON_SEL : (bExpanded ? C_ACCENT : C_LABEL), ETextJustify::Right);
        if (UHorizontalBoxSlot* S = HB->AddChildToHorizontalBox(A))
        {
            S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
            S->SetHorizontalAlignment(HAlign_Right);
            S->SetVerticalAlignment(VAlign_Center);
        }
        ApplyButtonSlotFill(Button->AddChild(HB));
        LabelText = T;
    }
    else
    {
        UTextBlock* T = MakeText(WidgetTree, Label, 13, false, bSelected ? C_ON_SEL : C_VALUE, ETextJustify::Left);
        ApplyButtonSlotFill(Button->AddChild(T));
        LabelText = T;
    }

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
                DispatchRowClick(InfoCopy);
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

void URemote_FactoryPanelWidget::DispatchRowClick(const FWT_ButtonInfo& Info)
{
    // ★1.0.8 唯一派发点：AddListButton（纯文本行）与 AddRichButton（带状态灯的富内容行）
    // 共用这里，新增一种 EWT_ButtonKind 只需改这一处，不会出现「某种按钮点了没反应」。
    switch (static_cast<EWT_ButtonKind>(Info.Kind))
    {
        case EWT_ButtonKind::Category:        SelectCategory(Info.Index);        break;
        case EWT_ButtonKind::CollectibleRow:  SelectCollectible(Info.Index);     break;
        case EWT_ButtonKind::TopToggle:       ToggleTopCategory(Info.Index);     break;
        case EWT_ButtonKind::CollectibleKind: SelectCollectibleKind(Info.Index); break;
        // ★1.0.9 电网行：Index 指向 Grids 数组（不是 Entries），所以绝不能落到 default 分支
        // —— 那会把电网下标当成条目下标去选中一台机器，轻则选错、重则越界。
        case EWT_ButtonKind::GridRow:         SelectGrid(Info.Index);            break;
        // ★1.0.8 所有筛选维度（状态 / 库存 / 纯度 / 距离）都走 UComboBoxString 的下拉回调，
        // 不再有筛选类行按钮 ⇒ 这里无需任何 filter 分支。
        default:                              SelectEntry(Info.Index);           break;
    }
}

UButton* URemote_FactoryPanelWidget::AddRichButton(UWidget* Content, bool bSelected,
                                                   const FWT_ButtonInfo& Info, bool bEnabled)
{
    WT_ZONE(TEXT("Panel.AddRichButton"));
    UButton* Button = WidgetTree->ConstructWidget<UButton>();
    Button->SetBackgroundColor(bSelected ? C_BTN_SEL : C_BTN);

    if (Content)
    {
        if (UButtonSlot* BS = Cast<UButtonSlot>(Button->AddChild(Content)))
        {
            // 与 AddListButton 同样的对齐修正：UButtonSlot 默认 HAlign_Center 会让内容整体居中，
            // 行内 [名称 距离][灯][状态] 就会缩在按钮中间。填满后内容才能从左边开始排。
            BS->SetHorizontalAlignment(HAlign_Fill);
            BS->SetVerticalAlignment(VAlign_Center);
            BS->SetPadding(FMargin(6.f, 2.f));
        }
    }
    Button->SetIsEnabled(bEnabled);

    const FWT_ButtonInfo InfoCopy = Info;
    Button->TakeWidget();
    if (TSharedPtr<SWidget> Cached = Button->GetCachedWidget())
    {
        if (TSharedPtr<SButton> SlateButton = StaticCastSharedPtr<SButton>(Cached))
        {
            SlateButton->SetOnClicked(FOnClicked::CreateLambda([this, InfoCopy]() -> FReply
            {
                DispatchRowClick(InfoCopy);
                return FReply::Handled();
            }));
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

    // ★1.0.28 自定义/skyblock 地图门禁：本帧判定一次，供资源节点/收集品类目复用。
    const bool bIsVanillaMap = WT_IsVanillaMap(GetWorld());

    // ★0.2.62: 可折叠顶层分组（原 11 个平铺类目按钮）。
    // ★0.2.153p 用户定案：**「容器」顶层组并入「建筑」**（液体容器/固体容器作为建筑的两个子类，
    // 插在采矿设备与发电机之间）⇒ 顶层由 3 组变 2 组（建筑 / 收集品）。
    static const FText TopLabels[3] = {
        WT_LOCTEXT("Top_ResourceNode", "资源节点"),   // ★本轮 提成【建筑】上方的独立顶层类目
        WT_LOCTEXT("Top_Building",     "建筑"),
        WT_LOCTEXT("Top_Alien",        "收集品"),
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
        // ★本轮 枢纽组（6 个枢纽建筑 + 回收站）已从【建筑】移除，统一挪到顶部图标栏（RebuildHubShortcuts）；
        // 此处不再列 Hub，避免与顶部栏重复。
        EWT_MachineCategory::Miner, EWT_MachineCategory::Producer,   // ★本轮 采矿设备 与 生产设备 互换（采矿设备排在生产设备上方）
        EWT_MachineCategory::Container,                              // ★本轮 液体/固体容器 合并为「容器」
        EWT_MachineCategory::Generator,
        // ★1.0.9 电网（用户定案：点开这个类目直接看到原版的电力曲线图 + 五项图例）。
        // 内容是「一条电路」而不是「一根杆」—— 杆只用来聚合与定位，不单独成行。
        EWT_MachineCategory::PowerGrid,
        // ★1.0.8 补漏：2026-10-24 用户定案「移除【电源开关】」当时只改了需求没落到代码，
        // PowerSwitch 一直留在两处 BuildingCats 里 ⇒ 面板上始终显示这个类目。
        // 现在真正删掉：开关条目仍会被扫描（Category 字段保留 PowerSwitch 值不动，避免动枚举值），
        // 但没有任何类目能选中它们 ⇒ 列表不再出现。枚举值 4 保留不复用。
        EWT_MachineCategory::Vehicle, EWT_MachineCategory::Portal, EWT_MachineCategory::Modded,
        // ★本轮 资源节点已提成【建筑】上方的独立顶层类目（见下方 ResourceNodeCats），不再列在建筑下
    };
    // ★本轮 资源节点组内两个子目（原版 / MOD）；与上方 BuildingCats 必须逐项一致（和 TopCount 两处数组同步）。
    static const EWT_MachineCategory ResourceNodeCats[] = {
        EWT_MachineCategory::ResourceNode_Vanilla,   // 原版矿点
        EWT_MachineCategory::ResourceNode_Mod,       // 模组矿点
    };

    /**
     * ★1.0.11 类目按钮（带问题徽标）—— 用户方案稿「问题徽标用醒目色：红色/橙色小圆点 + 数字，别用灰色」。
     *
     * 无问题 ⇒ 直接走原来的 AddListButton（纯文本行，不多建任何控件，行为/外观完全不变）；
     * 有问题 ⇒ 富行 [名称 (N)] [●] [数字]，圆点用 C_RED。
     *
     * 为什么不做「红/橙分级」：分级需要一套阈值（几个问题算严重？），没有实测依据；
     * 用户的原话是「红/橙都行，别用灰色」，那就统一红，避免引入一个会被质疑的判据。
     */
    auto MakeCatButton = [this](const FText& Label, bool bSel, const FWT_ButtonInfo& Info,
                                bool bEnabled, int32 IssueN) -> UButton*
    {
        if (IssueN <= 0)
        {
            UTextBlock* Lb = nullptr;
            UButton* Btn = AddListButton(Label, bSel, Info, bEnabled, &Lb);
            // ★1.0.7i 0 计数子类目现在**常显**（用户定案：类目不许少一项），所以必须真的灰下来：
            // SetIsEnabled(false) 只是不回事件，字号/颜色跟可用行一模一样，玩家看不出它不可点。
            if (!bEnabled && Lb)
            {
                Lb->SetColorAndOpacity(FSlateColor(C_FLT_DIM));
            }
            return Btn;
        }
        UHorizontalBox* Content = WidgetTree->ConstructWidget<UHorizontalBox>();

        UTextBlock* NameTb = MakeText(WidgetTree, Label, 13, bSel, bSel ? C_ON_SEL : C_VALUE, ETextJustify::Left);
        NameTb->SetAutoWrapText(false);
        if (UHorizontalBoxSlot* S = Content->AddChildToHorizontalBox(NameTb))
        {
            S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
            S->SetVerticalAlignment(VAlign_Center);
        }
        UImage* Dot = MakeStateDot(WidgetTree, C_RED);
        if (UHorizontalBoxSlot* S = Content->AddChildToHorizontalBox(Dot))
        {
            S->SetPadding(FMargin(8.f, 0.f, 3.f, 0.f));
            S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
            S->SetVerticalAlignment(VAlign_Center);
        }
        UTextBlock* NumTb = MakeText(WidgetTree, FText::AsNumber(IssueN), 13, /*bBold=*/true,
                                     bSel ? C_ON_SEL : C_RED, ETextJustify::Left);
        if (UHorizontalBoxSlot* S = Content->AddChildToHorizontalBox(NumTb))
        {
            S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
            S->SetVerticalAlignment(VAlign_Center);
        }
        // ★1.0.7i 同一纪律：禁用行必须灰显（带问题计数的这条路径文字是单独 set 的，
        // AddRichButton 只改背景，不会替我们变灰）。
        UButton* IssueBtn = AddRichButton(Content, bSel, Info, bEnabled);
        if (!bEnabled)
        {
            NameTb->SetColorAndOpacity(FSlateColor(C_FLT_DIM));
            NumTb->SetColorAndOpacity(FSlateColor(C_FLT_DIM));
        }
        return IssueBtn;
    };

    for (int32 t = 0; t < 3; ++t)
    {
        const int32 Count = TopCount(t);        const FText Arrow = bTopExpanded[t] ? WT_LOCTEXT("Arrow_Down", "▼") : WT_LOCTEXT("Arrow_Right", "▶");
        const FText Label = FText::Format(
            WT_LOCTEXT("TopLabel", "{0}  ({1})"), TopLabels[t], FText::AsNumber(Count));

        FWT_ButtonInfo TopInfo;
        TopInfo.Kind = static_cast<uint8>(EWT_ButtonKind::TopToggle);
        TopInfo.Index = t;
        // ★1.0.28h 展开态高亮：顶层类目箭头向下▼时整行中青高亮（bExpanded=bTopExpanded[t]）
        UButton* TopBtn = AddListButton(Label, /*bSelected=*/false, TopInfo, /*bEnabled=*/true, nullptr, &Arrow, /*bExpanded=*/bTopExpanded[t]);
        if (UVerticalBoxSlot* S = CategoryBox->AddChildToVerticalBox(TopBtn))
        {
            S->SetPadding(FMargin(0.f, 2.f, 6.f, 2.f));
        }

        if (!bTopExpanded[t])
        {
            continue;
        }

        if (t == 0) // 资源节点（建筑上方独立顶层类目）：原版 / MOD 两个子目
        {
            for (EWT_MachineCategory c : ResourceNodeCats)
            {
                const int32 ci = static_cast<int32>(c);
                const int32 cCount = CategoryCount(ci);
                const bool bSel = (ci == SelectedCategory && SelectedCollectibleKind < 0);
                // ★1.0.7i 用户定案：**全部子类目常显**，0 计数也保留为灰色不可用行 ——
                // 推翻 1.0.28c「0 计数不渲染」的做法（当时理由是 skyblock 下「原版」恒 0 的灰行
                // 像坏了；但现在玩家一致反馈「类目会莫名其妙少一项，找不到入口」，隐去比灰显更困惑）。
                // 禁用态由 MakeCatButton 的 bEnabled 控制，视觉上仍是灰行，但位置固定不乱跳。
                const bool bEnabled = (cCount > 0) || bSel;
                // ★1.0.28f 子行 label 去掉前导空格缩进（原 "    {0}"）：用户实测左列「行与行左缘参差」
                // ——顶行(带箭头)齐左、子行多缩 ~17px(=4空格) 交替出现，视觉即「文本没对齐」。
                // 现全部行统一齐左，层级靠 ▼/▶ 折叠箭头区分。全文件同型前导空格一并清除。
                const FText sub = cCount > 0
                    ? FText::Format(WT_LOCTEXT("Sub_WithCount", "{0}  ({1})"), CategoryLabel(ci), FText::AsNumber(cCount))
                    : FText::Format(WT_LOCTEXT("Sub_Plain", "{0}"), CategoryLabel(ci));
                FWT_ButtonInfo Info;
                Info.Kind = static_cast<uint8>(EWT_ButtonKind::Category);
                Info.Index = ci;
                UButton* B = MakeCatButton(sub, bSel, Info, bEnabled, CategoryIssueCount(ci));
                if (UVerticalBoxSlot* S = CategoryBox->AddChildToVerticalBox(B))
                {
                    S->SetPadding(FMargin(0.f, 1.f, 6.f, 1.f));
                }
            }
            // ★1.0.24 矿点数据闸门：resourcenodes.json 不存在/为空（从未跑过 wt.DumpResourceNodes）时
            // 两个子目恒为 0，用户无从知道要先跑 dump ⇒ 镜像收集品「本图无收集品」方案，渲染禁用提示行写明操作。
            // 与收集品的差异：收集品数据随 Lib 出厂自带，矿点数据只能玩家跑图 dump 积累（World Partition 只驻留附近格子）。
            {
                // ★1.0.9 统一走 CategoryCount（资源节点与电网同源口径，改一处不会漏另一处）
                const int32 VN = CategoryCount(static_cast<int32>(EWT_MachineCategory::ResourceNode_Vanilla));
                const int32 MN = CategoryCount(static_cast<int32>(EWT_MachineCategory::ResourceNode_Mod));
                if (VN + MN == 0)
                {
                    const FText Note = (!bIsVanillaMap)
                        ? WT_LOCTEXT("ResourceNode_CustomMap",
                            "自定义地图 · 原版矿点不适用")
                        : (bResourceNodeFileLoaded
                            ? WT_LOCTEXT("ResourceNode_FileEmpty",
                                "矿点数据为空 · 控制台执行 wt.DumpResourceNodes")
                            : WT_LOCTEXT("ResourceNode_NoFile",
                                "矿点数据未生成 · 控制台执行 wt.DumpResourceNodes（多次跑图积累全图）"));
                    FWT_ButtonInfo NoteInfo;
                    NoteInfo.Kind = static_cast<uint8>(EWT_ButtonKind::TopToggle);
                    UButton* NB = AddListButton(Note, /*bSelected=*/false, NoteInfo, /*bEnabled=*/false);
                    if (UVerticalBoxSlot* S = CategoryBox->AddChildToVerticalBox(NB))
                    {
                        S->SetPadding(FMargin(0.f, 2.f, 6.f, 2.f));
                    }
                }
                }
                }
        // ★2026-10-25 修「收集品串进资源节点区」：此处必须是 else if ——
        // 3 组结构（资源节点/建筑/收集品）下若写成独立 `if (t == 1) ... else ...`，
        // t==0（资源节点展开）时 else 分支也会跑，把 9 个收集品 kind 行插进资源节点区，
        // 且收集品顶层组下还会再渲染一遍（用户实测截图：重复+串区）。
        else if (t == 1) // 建筑
        {
            for (EWT_MachineCategory c : BuildingCats)
            {
                const int32 ci = static_cast<int32>(c);
                const int32 cCount = CategoryCount(ci);
                const bool bSel = (ci == SelectedCategory && SelectedCollectibleKind < 0);
                // ★1.0.7i 用户定案：建筑下**每个子类目都常显**（0 计数 = 灰显不可点），
                // 不再像 1.0.28c 那样把空类目整行隐去。类目数量固定 ⇒ 左列不会「少一项」。
                const bool bEnabled = (cCount > 0) || bSel;
                const FText sub = cCount > 0
                    ? FText::Format(WT_LOCTEXT("Sub_WithCount", "{0}  ({1})"), CategoryLabel(ci), FText::AsNumber(cCount))
                    : FText::Format(WT_LOCTEXT("Sub_Plain", "{0}"), CategoryLabel(ci));
                FWT_ButtonInfo Info;
                Info.Kind = static_cast<uint8>(EWT_ButtonKind::Category);
                Info.Index = ci;
                UButton* B = MakeCatButton(sub, bSel, Info, bEnabled, CategoryIssueCount(ci));
                if (UVerticalBoxSlot* S = CategoryBox->AddChildToVerticalBox(B))
                {
                    S->SetPadding(FMargin(0.f, 1.f, 6.f, 1.f));
                }
            }
        }
        else // t == 2 收集品（外星能量 + 可采集果实）。★0.2.153p 容器已并入建筑，顶层由 3 组变为（资源节点/建筑/收集品）
        {
            // ★0.2.153z 无收集品地图闸门（方案 D）：扫描到 0 个收集品（如 Platform Expansion Program
            // 这类无地形 / 无收集品 MOD 地图）时，收集品传送入口成摆设 ⇒ 直接提示不可用，不再列空类目。
            // ★本轮 修「非空地图也显示『本图无收集品』」：原闸门只查实时扫描回包（CurrentList.Collectibles，
            // 仅覆盖玩家附近已流式加载的格子），玩家站在无收集品格子上就误判。改为「实时扫描空 且 文件也无数据」
            // 才提示不可用；文件已加载（bCollectibleFileLoaded）即渲染各子目（计数回退 collectibles.json）。
            const bool bCustomMap = !bIsVanillaMap;
            if (bCustomMap || (CurrentList.Collectibles.Num() == 0 && !(bCollectibleFileLoaded && DisplayCollectibles.Num() > 0)))
            {
                const FText Note = bCustomMap
                    ? WT_LOCTEXT("Collectible_CustomMap",
                        "本图为自定义地图 · 原版收集品不适用")
                    : WT_LOCTEXT("Collectible_None",
                        "本图无收集品 · 传送不可用");
                FWT_ButtonInfo NoteInfo;
                NoteInfo.Kind = static_cast<uint8>(EWT_ButtonKind::TopToggle);
                UButton* NB = AddListButton(Note, /*bSelected=*/false, NoteInfo, /*bEnabled=*/false);
                if (UVerticalBoxSlot* S = CategoryBox->AddChildToVerticalBox(NB))
                {
                    S->SetPadding(FMargin(0.f, 2.f, 6.f, 2.f));
                }
            }
            else
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
                const bool bSel = (k == SelectedCollectibleKind);

                const FText sub = kPickup > 0
                    ? FText::Format(WT_LOCTEXT("Sub_WithCount", "{0}  ({1})"), CollectibleKindLabel(k), FText::AsNumber(kPickup))
                    : FText::Format(WT_LOCTEXT("Sub_Plain", "{0}"), CollectibleKindLabel(k));
                FWT_ButtonInfo Info;
                Info.Kind = static_cast<uint8>(EWT_ButtonKind::CollectibleKind);
                Info.Index = k * 3; // ★0.2.64: low digit 0 = 全部
                // ★1.0.7i 种类行不再带折叠三角：【未拾取】/【已拾取】已上移为筛选条上的两个
                // 独立过滤条件（用户定案），这一行下面不再有任何子行，三角就成了空操作。
                UButton* LabelBtn = AddListButton(sub, bSel, Info, /*bEnabled=*/kTotal > 0 || bSel);
                if (UVerticalBoxSlot* LS = CategoryBox->AddChildToVerticalBox(LabelBtn))
                {
                    LS->SetPadding(FMargin(0.f, 1.f, 6.f, 1.f));
                }
            }
            }
        }
    }
}

/**
 * ★1.0.7i 枢纽快捷格「未建造」时的图标兜底：按**描述符类名**取图标。
 *
 * 背景（用户诉求「修复图标中未建造状态贴图丢失」）：`RebuildHubShortcuts` 原来只给
 * HubTerminal / HubWorkbench / SpaceElevator / ResourceSink 写了 StaticLoadClass 路径，
 * MAM / 装备工坊 / AWESOME 商店 三个走 `default: break` ⇒ 未建造时永远是灰色占位块。
 *
 * 为什么扫类而不写死 `/Game/...` 路径：蓝图类的包路径跟着版本改就会失效（该项目已因
 * 硬编码资产路径栽过一次），而**类名** `Desc_Mam_C` 是蓝图/存档引用的稳定标识。
 * 先找已加载的类（建造枪在开局就把描述符全加载进内存），找不到才由调用方走别路。
 * 结果按类名缓存（含 miss），一次重建只付一次类遍历。
 */
static UTexture2D* WT_IconByDescriptorClassName(const TCHAR* DescName)
{
    struct FCacheEntry { TWeakObjectPtr<UTexture2D> Icon; bool bResolved = false; };
    static TMap<FString, FCacheEntry> Cache;

    const FString Want(DescName);
    if (const FCacheEntry* Hit = Cache.Find(Want))
    {
        if (Hit->bResolved)
        {
            return Hit->Icon.Get();
        }
    }

    UClass* Found = nullptr;
    for (TObjectIterator<UClass> It; It; ++It)
    {
        UClass* Cls = *It;
        if (!Cls || !Cls->IsChildOf(UFGItemDescriptor::StaticClass()))
        {
            continue;
        }
        if (Cls->GetName() == Want)
        {
            Found = Cls;
            break;
        }
    }

    UTexture2D* Tex = nullptr;
    if (Found)
    {
        // 枢纽类建筑不是「物品」，mBigIcon 常为空（1.0.28d 已实测）⇒ 必须再退小图标。
        Tex = UFGItemDescriptor::GetBigIcon(Found);
        if (!Tex)
        {
            Tex = UFGItemDescriptor::GetSmallIcon(Found);
        }
    }

    FCacheEntry Entry;
    Entry.Icon = Tex;
    Entry.bResolved = true;
    Cache.Add(Want, Entry);

    if (Tex)
    {
        WT_WARNF(TEXT("[panel-hub] desc icon hit: %s -> %s"), DescName, *Tex->GetPathName());
    }
    else
    {
        WT_WARNF(TEXT("[panel-hub] desc icon MISS: %s (class %s)"), DescName,
                 Found ? TEXT("loaded but iconless") : TEXT("not loaded"));
    }
    return Tex;
}

// ★本轮 ------------------------------------------------------------------
void URemote_FactoryPanelWidget::RebuildHubShortcuts()
{
    if (!HubShortcutBox)
    {
        return;
    }
    HubShortcutBox->ClearChildren();

    // 固定顺序：用户指定的 6 个枢纽建筑 + 回收站（回收站共享 Hub 桶，挪到这里避免从列表消失）。
    struct FHubDef { EWT_HubKind Kind; const TCHAR* Name; };
    static const FHubDef Defs[] = {
        { EWT_HubKind::HubTerminal,       TEXT("枢纽终端") },
        { EWT_HubKind::HubWorkbench,      TEXT("枢纽制作台") },
        { EWT_HubKind::SpaceElevator,     TEXT("太空电梯") },
        { EWT_HubKind::MAM,               TEXT("分子分析仪") },
        { EWT_HubKind::EquipmentWorkshop, TEXT("装备工坊") },
        { EWT_HubKind::AwesomeShop,       TEXT("AWESOME商店") },
        { EWT_HubKind::ResourceSink,      TEXT("回收站") },
    };

    for (const FHubDef& Def : Defs)
    {
        // 找到该类型最近的已扫描实例（未建造则 Found<0，灰显禁用）
        int32 Found = -1;
        float Best = 1e30f;
        for (int32 i = 0; i < CurrentList.Entries.Num(); ++i)
        {
            const FWT_MachineEntry& E = CurrentList.Entries[i];
            if (static_cast<EWT_HubKind>(E.HubKind) == Def.Kind)
            {
                if (E.DistanceMeters < Best) { Best = E.DistanceMeters; Found = i; }
            }
        }

        // 单元格：图标(上) + 名称(中) + 传送/打开(下)
        // ★1.0.28c 布局修正（实测截图二连）：① WrapBox 换行成两排 → 容器已改 HorizontalBox 单行均分；
        // ② SetBrushFromSoftTexture(bMatchSize=false) 后图标按贴图原生尺寸渲染，巨型图标把单元格撑爆
        //    与名字叠压 → 改显式 FSlateBrush + SetImageSize(32) 强制像素尺寸，与贴图原生大小无关。
        UVerticalBox* Cell = WidgetTree->ConstructWidget<UVerticalBox>();

        UImage* Img = WidgetTree->ConstructWidget<UImage>();
        {
            FSlateBrush B;   // 默认 DrawAs=Image
            B.SetImageSize(FVector2D(32.f, 32.f));
            UTexture2D* Tex = (Found >= 0 && !CurrentList.Entries[Found].Icon.IsNull())
                                  ? CurrentList.Entries[Found].Icon.LoadSynchronous()
                                  : nullptr;
            // ★1.0.28c 未建造/无图标格的图标兜底：按 HubKind 加载原版 descriptor 走 GetBigIcon
            // （资产存在性经游戏 .utoc 目录索引实锤；目录经游戏真实 DefaultEngine.ini 交叉验证；
            // 加载失败保持空 → 占位色块，零风险）。太空电梯未建造时条目不存在，只有这里能救。
            if (!Tex)
            {
                // ★1.0.28f 枢纽终端直连 HUB 建筑图标：TradingPost/UI/Hub_256、Hub_512
                // （utoc 走树实锤存在，含 .ubulk 真贴图）。必须排在 Desc 链之前——
                // Desc_HubTerminal 的 GetSmallIcon 返回的是通用占位图，Tex 非空会短路后面全部候选。
                if (Def.Kind == EWT_HubKind::HubTerminal)
                {
                    static const TCHAR* HubDirectTex[] = {
                        TEXT("/Game/FactoryGame/Buildable/Factory/TradingPost/UI/Hub_256.Hub_256"),
                        TEXT("/Game/FactoryGame/Buildable/Factory/TradingPost/UI/Hub_512.Hub_512"),
                    };
                    for (const TCHAR* TexPath : HubDirectTex)
                    {
                        if (UTexture2D* HubTex = LoadObject<UTexture2D>(nullptr, TexPath))
                        {
                            Tex = HubTex;
                            WT_WARNF(TEXT("[panel-hub] HubTerminal direct icon hit: %s"), TexPath);
                            break;
                        }
                    }
                }
                const TCHAR* FallbackDesc = nullptr;
                switch (Def.Kind)
                {
                case EWT_HubKind::HubTerminal:   FallbackDesc = TEXT("/Game/FactoryGame/Buildable/Factory/HubTerminal/Desc_HubTerminal.Desc_HubTerminal_C"); break;
                case EWT_HubKind::HubWorkbench:  FallbackDesc = TEXT("/Game/FactoryGame/Buildable/Factory/WorkBench/Desc_WorkBench.Desc_WorkBench_C"); break;
                case EWT_HubKind::SpaceElevator: FallbackDesc = TEXT("/Game/FactoryGame/Buildable/Factory/SpaceElevator/Desc_SpaceElevator.Desc_SpaceElevator_C"); break;
                case EWT_HubKind::ResourceSink:  FallbackDesc = TEXT("/Game/FactoryGame/Buildable/Factory/ResourceSink/Desc_ResourceSink.Desc_ResourceSink_C"); break;
                default: break;
                }
                // ★1.0.28f 加 !Tex 守卫：上方 Hub_256 直连已命中时绝不能再进 Desc 链——
                // GetBigIcon/GetSmallIcon 是无条件赋值，会把 Hub_256 覆盖回 Desc 占位图（深度自检①实抓）。
                if (!Tex && FallbackDesc)
                {
                    if (UClass* DescCls = StaticLoadClass(UFGItemDescriptor::StaticClass(), nullptr, FallbackDesc))
                    {
                        Tex = UFGItemDescriptor::GetBigIcon(DescCls);
                        if (!Tex)
                        {
                            // ★1.0.28d 实测：Desc_HubTerminal 的 mBigIcon 为空（枢纽非物品）→ 退小图标。
                            Tex = UFGItemDescriptor::GetSmallIcon(DescCls);
                        }
                    }
                }
                // ★1.0.7i 三个在上面 switch 里落到 `default: break` 的枢纽（MAM / 装备工坊 /
                // AWESOME 商店）走「描述符类名」兜底 —— 这就是用户报的「未建造状态贴图丢失」。
                // 顺带对 4 个已有路径的也兜一层：路径哪天被官方改版改掉，这里还能救回来。
                if (!Tex)
                {
                    const TCHAR* DescName = nullptr;
                    switch (Def.Kind)
                    {
                    case EWT_HubKind::HubTerminal:       DescName = TEXT("Desc_HubTerminal_C");     break;
                    case EWT_HubKind::HubWorkbench:      DescName = TEXT("Desc_WorkBench_C");       break;
                    case EWT_HubKind::SpaceElevator:     DescName = TEXT("Desc_SpaceElevator_C");    break;
                    case EWT_HubKind::MAM:               DescName = TEXT("Desc_Mam_C");             break;
                    case EWT_HubKind::EquipmentWorkshop: DescName = TEXT("Desc_Workshop_C");        break;
                    case EWT_HubKind::AwesomeShop:       DescName = TEXT("Desc_ResourceSinkShop_C"); break;
                    case EWT_HubKind::ResourceSink:      DescName = TEXT("Desc_ResourceSink_C");     break;
                    default: break;
                    }
                    if (DescName)
                    {
                        Tex = WT_IconByDescriptorClassName(DescName);
                    }
                }
                // ★1.0.28e 候选链换权威实锤路径（与 RCO 端同步）：自写 utoc 目录索引解析器走树实锤
                // SchematicIcon_Hub_1..5 / HubUpgrade 全在 .../SchematicIcons/OldIcons/ 子目录。
                if (!Tex && Def.Kind == EWT_HubKind::HubTerminal)
                {
                    static const TCHAR* HubTexCandidates[] = {
                        TEXT("/Game/FactoryGame/Buildable/Factory/TradingPost/UI/SchematicIcons/OldIcons/SchematicIcon_Hub_1.SchematicIcon_Hub_1"),
                        TEXT("/Game/FactoryGame/Buildable/Factory/TradingPost/UI/SchematicIcons/OldIcons/SchematicIcon_Hub_2.SchematicIcon_Hub_2"),
                        TEXT("/Game/FactoryGame/Buildable/Factory/TradingPost/UI/SchematicIcons/OldIcons/SchematicIcon_HubUpgrade.SchematicIcon_HubUpgrade"),
                        TEXT("/Game/FactoryGame/Buildable/Factory/TradingPost/UI/SchematicIcons/SchematicIcon_Hub_1.SchematicIcon_Hub_1"),   // 旧路径兜底（utoc 无此包，防引擎改版）
                        TEXT("/Game/FactoryGame/Buildable/Factory/TradingPost/UI/SchematicIcons/Icon_Hub_1.Icon_Hub_1"),
                        TEXT("/Game/FactoryGame/Buildable/Factory/TradingPost/UI/SchematicIcons/Icon_Hub.Icon_Hub"),
                    };
                    for (const TCHAR* TexPath : HubTexCandidates)
                    {
                        if (UTexture2D* HubTex = LoadObject<UTexture2D>(nullptr, TexPath))
                        {
                            Tex = HubTex;
                            WT_WARNF(TEXT("[panel-hub] HubTerminal icon fallback hit: %s"), TexPath);
                            break;
                        }
                    }
                }
            }
            if (Tex)
            {
                B.SetResourceObject(Tex);
            }
            else
            {
                B = *FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
                B.SetImageSize(FVector2D(32.f, 32.f));
            }
            Img->SetBrush(B);
            if (!Tex)
            {
                Img->SetColorAndOpacity(FLinearColor(0.28f, 0.34f, 0.44f, 1.f));   // 可见的中性占位色
            }
        }
        if (UVerticalBoxSlot* S = Cell->AddChildToVerticalBox(Img))
        {
            S->SetHorizontalAlignment(HAlign_Center);
            S->SetPadding(FMargin(1.f));
        }

        UTextBlock* Name = MakeText(WidgetTree, FText::FromString(Def.Name), 9, false, C_LABEL, ETextJustify::Center);
        Name->SetAutoWrapText(false);   // 单行名称，超宽裁剪也不折行错位
        if (UVerticalBoxSlot* S = Cell->AddChildToVerticalBox(Name))
        {
            S->SetHorizontalAlignment(HAlign_Center);
            S->SetPadding(FMargin(1.f, 2.f, 1.f, 2.f));
        }

        UHorizontalBox* Acts = WidgetTree->ConstructWidget<UHorizontalBox>();
        const int32 EntryIdx = Found;
        UButton* TpBtn = MakeButton(WidgetTree, WT_LOCTEXT("Hub_Tp", "传送"), C_BTN, nullptr, ETextJustify::Center, 10);
        TpBtn->SetIsEnabled(Found >= 0);
        UButton* OpenBtn = MakeButton(WidgetTree, WT_LOCTEXT("Hub_Open", "打开"), C_BTN, nullptr, ETextJustify::Center, 10);
        OpenBtn->SetIsEnabled(Found >= 0 && CurrentList.Entries[Found].bHasInteractUI);
        // UMG 的 OnClicked 不能带参；用 Slate 直接挂 lambda，复用现有 SelectedEntry + 现成处理器。
        auto BindHubAction = [&](UButton* B, TFunction<FReply()> Fn)
        {
            B->TakeWidget();
            if (TSharedPtr<SWidget> C = B->GetCachedWidget())
            {
                if (TSharedPtr<SButton> SB = StaticCastSharedPtr<SButton>(C))
                {
                    SB->SetOnClicked(FOnClicked::CreateLambda(Fn));
                }
            }
        };
        BindHubAction(TpBtn, [this, EntryIdx]() -> FReply {
            if (EntryIdx >= 0) { SelectedEntry = EntryIdx; OnTeleportToMachineClicked(); }
            return FReply::Handled();
        });
        BindHubAction(OpenBtn, [this, EntryIdx]() -> FReply {
            if (EntryIdx >= 0) { SelectedEntry = EntryIdx; OnOpenMachineClicked(); }
            return FReply::Handled();
        });
        if (UHorizontalBoxSlot* S = Acts->AddChildToHorizontalBox(TpBtn)) { S->SetPadding(FMargin(1.f)); S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic)); }
        if (UHorizontalBoxSlot* S = Acts->AddChildToHorizontalBox(OpenBtn)) { S->SetPadding(FMargin(1.f)); S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic)); }
        if (UVerticalBoxSlot* S = Cell->AddChildToVerticalBox(Acts)) { S->SetHorizontalAlignment(HAlign_Center); }

        if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(HubShortcutBox->AddChild(Cell)))
        {
            S->SetPadding(FMargin(2.f, 1.f));
            // 本引擎 FSlateChildSize 无 (float, Type) 双参构造（SlateWrapperTypes.h:156/162），用默认 Fill(Value=1) 即 7 格均分
            S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            S->SetVerticalAlignment(VAlign_Fill);
        }
    }
}

// ★本轮 ------------------------------------------------------------------
void URemote_FactoryPanelWidget::OnSearchTextChanged(const FText& Text)
{
    SearchText = Text.ToString().TrimStartAndEnd();
    bSearchActive = !SearchText.IsEmpty();

    // ★1.0.8 按类目隔离（用户定案「每个子类目列表各一个，每个搜索对应当前列表」）：
    // 输入记到当前类目名下，切到别的类目不影响它，切回来时 SelectCategory 会还原。
    if (SelectedCategory >= 0)
    {
        CategorySearchTexts.Add(static_cast<uint8>(SelectedCategory), SearchText);
    }

    RebuildMachineList();   // 只重画当前类目的列表
}

void URemote_FactoryPanelWidget::ClearSearch()
{
    bSearchActive = false;
    SearchText.Empty();
    // ★1.0.8 把当前类目记成「空词」而不是 Remove：
    // 两者在还原时等价（找不到就当空），但写成空词表达的是「屏幕现在确实没有过滤条件」，
    // 语义清楚，也不会让表里出现「查不到但其实是刚清空」的歧义。
    // 其它类目各自的搜索词一律不动。
    if (SelectedCategory >= 0)
    {
        CategorySearchTexts.Add(static_cast<uint8>(SelectedCategory), FString());
    }
    if (SearchBox)
    {
        // SetText 不触发 OnTextChanged（仅用户输入触发），无递归风险。
        SearchBox->SetText(FText::GetEmpty());
    }
}

// ----------------------------------------------------------------------------
// ★1.0.8 子类目筛选：横列 N 个下拉选择器
// ----------------------------------------------------------------------------
// 用户定案：「子类目列表横列 N 个筛选条件，用下选箭头选择」「只要是建筑类目的，
// 每个子列表都写入符合当前列表的」。
//
// 维度表（每个子类目只给贴合自身的那几维，不硬塞）：
//   采矿设备 / 生产设备 / 发电机 : [状态] [距离]
//   容器（含死亡箱子）           : [库存] [距离]
//   资源节点（原版 / MOD）       : [纯度] [距离]
//   运输工具 / 传送门 / MOD建筑  : [距离]
//   其余（收集品等）             : 无筛选条
//
// 多个下拉之间是 **AND（叠加）** 关系；选项文本自带计数，所以下拉本身就是计数条。

/**
 * ★1.0.8 切类目时复位所有筛选维度（避免「切到容器后还留着上类目的状态筛选」）。
 * ★1.0.10 物品键是字符串，跨类目残留的危害比整型维度更大：旧类目的物品 Key 在新类目里
 * 根本不存在 ⇒ 交集恒空，列表会一片空白且看似「没数据」。所以必须在这里一并清掉。
 */
void URemote_FactoryPanelWidget::ResetCategoryFilters()
{
    SelectedProdState  = -1;
    SelectedDistBand   = -1;
    SelectedNodePurity = -1;
    SelectedItemKey.Empty();
    SelectedAmountBand = -1;
    // ★1.0.11 新增维度同样必须归零：漏一个就会出现「切到新类目仍在按上一类目的问题筛」，
    // 表现为列表莫名其妙只有几行、而下拉看起来是「全部」。
    SelectedTypeKey.Empty();
    SelectedIssue = -1;
    SelectedRatioBand = -1;
    SelectedSort = 0;
    // ★1.0.7i 里程碑阶段同样归零：它的键是「蓝图 TechTier」，跨类目残留时问题比整型更隐蔽
    //（同一个 3 在两类目里都有意义，筛出来「少了一半建筑」却看不出是残留）。
    SelectedMilestone = -1;
    CurrentItemOptionKeys.Reset();
    CurrentTypeOptionKeys.Reset();
    CurrentMilestoneOptionKeys.Reset();
}

void URemote_FactoryPanelWidget::SaveCategoryFilters(uint8 Cat)
{
    FWT_CatFilterSnapshot S;
    S.ProdState  = SelectedProdState;
    S.DistBand   = SelectedDistBand;
    S.NodePurity = SelectedNodePurity;
    S.AmountBand = SelectedAmountBand;
    S.Issue      = SelectedIssue;
    S.RatioBand  = SelectedRatioBand;
    S.Sort       = SelectedSort;
    S.ItemKey    = SelectedItemKey;
    S.TypeKey    = SelectedTypeKey;
    S.Milestone  = SelectedMilestone;
    CategoryFilterStates.Add(Cat, S);
}

void URemote_FactoryPanelWidget::LoadCategoryFilters(uint8 Cat)
{
    const FWT_CatFilterSnapshot* S = CategoryFilterStates.Find(Cat);
    if (!S)
    {
        return;   // 首次进入该类目 = 保持 ResetCategoryFilters() 的全清状态
    }
    SelectedProdState  = S->ProdState;
    SelectedDistBand   = S->DistBand;
    SelectedNodePurity = S->NodePurity;
    SelectedAmountBand = S->AmountBand;
    SelectedIssue      = S->Issue;
    SelectedRatioBand  = S->RatioBand;
    SelectedSort       = S->Sort;
    SelectedItemKey    = S->ItemKey;
    SelectedTypeKey    = S->TypeKey;
    SelectedMilestone  = S->Milestone;
}

bool URemote_FactoryPanelWidget::PassesCategoryFilters(const FWT_MachineEntry& Entry) const
{
    // 每一维都是「未选中 ⇒ 跳过」语义，所以默认状态等价于不过滤（向后兼容）。
    if (SelectedProdState >= 0 && Entry.ProdState != static_cast<uint8>(SelectedProdState))
    {
        return false;
    }
    if (SelectedNodePurity >= 0 && Entry.PurityFilter != SelectedNodePurity)
    {
        return false;
    }
    // ★1.0.10 物品维度：行的物品标签里必须真有这一项（按类名比对）。
    // 空 ItemTags 的行在选中某物品后一律不出现 —— 它本来就没这件货。
    if (!SelectedItemKey.IsEmpty())
    {
        bool bHasItem = false;
        for (const FWT_ItemTag& Tag : Entry.ItemTags)
        {
            if (Tag.Key == SelectedItemKey)
            {
                bHasItem = true;
                break;
            }
        }
        if (!bHasItem)
        {
            return false;
        }
    }
    // ★1.0.10 数量维度：ContentStackCount < 0 = 不可计量（机器 / 矿点 / 液体罐），不参与该维。
    // 用「不参与」而不是「当 0」：否则筛「空箱子」会把一堆机器也算进去。
    if (SelectedAmountBand >= 0)
    {
        if (Entry.ContentStackCount < 0 || WT_AmountBandOf(Entry.ContentStackCount) != SelectedAmountBand)
        {
            return false;
        }
    }
    if (SelectedDistBand >= 0 && SelectedDistBand <= 2)
    {
        static const float BandMaxM[3] = { 100.f, 500.f, 1000.f };
        if (Entry.DistanceMeters > BandMaxM[SelectedDistBand])
        {
            return false;
        }
    }
    // ★1.0.11 类型维度：空 = 全部类型；否则必须与下拉选中的类型名逐字相等。
    if (!SelectedTypeKey.IsEmpty() && Entry.DisplayName.ToString() != SelectedTypeKey)
    {
        return false;
    }
    // ★1.0.11 问题维度：位掩码匹配（-1 全部 / 0 任意问题 / 1..8 单一位）。
    if (!WT_IssueMatches(Entry.IssueFlags, SelectedIssue))
    {
        return false;
    }
    // ★1.0.11 比例维度：不可计量（-1）的行不参与 —— 否则筛「0~25%」会把机器和矿点一起捞进来。
    if (SelectedRatioBand >= 0)
    {
        const float Pct = WT_RowRatioPercent(Entry);
        if (Pct < 0.f || WT_RatioBandOf(Pct) != SelectedRatioBand)
        {
            return false;
        }
    }
    // ★1.0.7i 里程碑阶段维度：-1 = 不过滤；99 = 未归类（扫描侧查不到解锁蓝图的条目）。
    if (SelectedMilestone >= 0 && Entry.MilestoneTier != SelectedMilestone)
    {
        return false;
    }
    return true;
}

int32 URemote_FactoryPanelWidget::CountEntriesForDim(EWT_FilterDim Dim, int32 Value) const
{
    static const float BandMaxM[3] = { 100.f, 500.f, 1000.f };
    // ★1.0.8 搜索词也参与计数：搜索框内嵌在列表头上，玩家一边搜一边看下拉，
    // 若下拉写「运行中 (12)」而点下去只有 3 行（因为搜索砍掉了 9 行），数字就是假的。
    const FString SearchToken = bSearchActive ? SearchText : FString();

    // ★1.0.10 收集品类目：它的行不在 Entries 里（走 DisplayCollectibles 独立渲染），
    // 所以距离 / 拾取这两个维度的计数必须去那边数，否则下拉会一直显示 (0) 而列表里明明有东西。
    // ★1.0.8AN：加上拾取维度（Picked）。数某一维时，这一维用**候选值**、其余维用**当前选中值**
    //   —— 这样「选它还剩几个」的数字才与点下去后的实际行数一致（和建筑各维同一套口径）。
    if (SelectedCollectibleKind >= 0)
    {
        if (Dim != EWT_FilterDim::Distance && Dim != EWT_FilterDim::Picked)
        {
            return 0;
        }
        const int32 PickSel = (Dim == EWT_FilterDim::Picked) ? Value : SelectedCollectiblePicked;
        int32 NC = 0;
        for (const FWT_CollectibleEntry& C : DisplayCollectibles)
        {
            if (C.Kind != static_cast<uint8>(SelectedCollectibleKind))                       { continue; }
            if (PickSel >= 0 && (C.bCollected ? 1 : 0) != PickSel)                           { continue; }
            if (!SearchToken.IsEmpty()
                && !C.DisplayName.ToString().Contains(SearchToken, ESearchCase::IgnoreCase)) { continue; }
            if (Dim == EWT_FilterDim::Distance && Value >= 0 && Value <= 2
                && C.DistanceMeters > BandMaxM[Value])                                       { continue; }
            ++NC;
        }
        return NC;
    }

    int32 N = 0;
    for (const FWT_MachineEntry& E : CurrentList.Entries)
    {
        if (E.Category != static_cast<uint8>(SelectedCategory))
        {
            continue;
        }
        if (!WT_EntryMatchesSearch(E, SearchToken))
        {
            continue;
        }

        // ★计数口径：本维取 Value，其余维度沿用已选值 —— 这样「运行中 (12)」这个 12
        // 就是真的点下去后能看到的行数，而不是忽略其它筛选的虚数。
        bool bPass = true;
        if (Dim != EWT_FilterDim::ProdState && SelectedProdState >= 0
            && E.ProdState != static_cast<uint8>(SelectedProdState))                    { bPass = false; }
        if (Dim != EWT_FilterDim::Purity && SelectedNodePurity >= 0
            && E.PurityFilter != SelectedNodePurity)                                    { bPass = false; }
        if (Dim != EWT_FilterDim::Distance && SelectedDistBand >= 0 && SelectedDistBand <= 2
            && E.DistanceMeters > BandMaxM[SelectedDistBand])                           { bPass = false; }
        if (Dim != EWT_FilterDim::Amount && SelectedAmountBand >= 0)
        {
            if (E.ContentStackCount < 0)                                                { bPass = false; }
            else if (WT_AmountBandOf(E.ContentStackCount) != SelectedAmountBand)         { bPass = false; }
        }
        if (Dim != EWT_FilterDim::Item && !SelectedItemKey.IsEmpty())
        {
            bool bHasItem = false;
            for (const FWT_ItemTag& Tag : E.ItemTags)
            {
                if (Tag.Key == SelectedItemKey) { bHasItem = true; break; }
            }
            if (!bHasItem)                                                              { bPass = false; }
        }
        // ★1.0.11 新增三维同样要参与「其余维度」的叠加，否则下拉里的计数会偏大
        //（点了才发现行数比括号里的数字少）。
        if (Dim != EWT_FilterDim::Type && !SelectedTypeKey.IsEmpty()
            && E.DisplayName.ToString() != SelectedTypeKey)                             { bPass = false; }
        if (Dim != EWT_FilterDim::Issue && !WT_IssueMatches(E.IssueFlags, SelectedIssue)) { bPass = false; }
        if (Dim != EWT_FilterDim::Ratio && SelectedRatioBand >= 0)
        {
            const float Pct = WT_RowRatioPercent(E);
            if (Pct < 0.f || WT_RatioBandOf(Pct) != SelectedRatioBand)                   { bPass = false; }
        }
        // ★1.0.7i 里程碑阶段：本维取 Value，其余维度沿用已选值（与上面各维同一条纪律）。
        if (Dim != EWT_FilterDim::Milestone && SelectedMilestone >= 0
            && E.MilestoneTier != SelectedMilestone)                                    { bPass = false; }

        if (bPass)
        {
            switch (Dim)
            {
                case EWT_FilterDim::ProdState:
                    if (Value >= 0 && E.ProdState != static_cast<uint8>(Value))          { bPass = false; }
                    break;
                case EWT_FilterDim::Purity:
                    if (Value >= 0 && E.PurityFilter != Value)                           { bPass = false; }
                    break;
                case EWT_FilterDim::Distance:
                    if (Value >= 0 && Value <= 2 && E.DistanceMeters > BandMaxM[Value])  { bPass = false; }
                    break;
                case EWT_FilterDim::Amount:
                    if (Value >= 0)
                    {
                        if (E.ContentStackCount < 0)                                     { bPass = false; }
                        else if (WT_AmountBandOf(E.ContentStackCount) != Value)          { bPass = false; }
                    }
                    break;
                case EWT_FilterDim::Issue:
                    if (!WT_IssueMatches(E.IssueFlags, Value))                           { bPass = false; }
                    break;
                case EWT_FilterDim::Ratio:
                    if (Value >= 0)
                    {
                        const float Pct = WT_RowRatioPercent(E);
                        if (Pct < 0.f || WT_RatioBandOf(Pct) != Value)                   { bPass = false; }
                    }
                    break;
                case EWT_FilterDim::Milestone:
                    if (Value >= 0 && E.MilestoneTier != Value)                          { bPass = false; }
                    break;
                case EWT_FilterDim::Item:
                case EWT_FilterDim::Type:
                default:
                    // 物品 / 类型维度的键是字符串，不在本函数范围（见 CountEntriesWithItem / WithType）
                    break;
            }
        }

        if (bPass)
        {
            ++N;
        }
    }
    return N;
}

int32 URemote_FactoryPanelWidget::CountEntriesWithItem(const FString& ItemKey) const
{
    static const float BandMaxM[3] = { 100.f, 500.f, 1000.f };
    const FString SearchToken = bSearchActive ? SearchText : FString();

    int32 N = 0;
    for (const FWT_MachineEntry& E : CurrentList.Entries)
    {
        if (E.Category != static_cast<uint8>(SelectedCategory))                            { continue; }
        if (!WT_EntryMatchesSearch(E, SearchToken))                                        { continue; }

        if (SelectedProdState >= 0 && E.ProdState != static_cast<uint8>(SelectedProdState)) { continue; }
        if (SelectedNodePurity >= 0 && E.PurityFilter != SelectedNodePurity)                { continue; }
        if (SelectedDistBand >= 0 && SelectedDistBand <= 2
            && E.DistanceMeters > BandMaxM[SelectedDistBand])                               { continue; }
        if (SelectedAmountBand >= 0)
        {
            if (E.ContentStackCount < 0)                                                    { continue; }
            if (WT_AmountBandOf(E.ContentStackCount) != SelectedAmountBand)                  { continue; }
        }
        if (!ItemKey.IsEmpty())
        {
            bool bHasItem = false;
            for (const FWT_ItemTag& Tag : E.ItemTags)
            {
                if (Tag.Key == ItemKey) { bHasItem = true; break; }
            }
            if (!bHasItem)                                                                  { continue; }
        }
        // ★1.0.11 其余维度照样叠加
        if (!SelectedTypeKey.IsEmpty() && E.DisplayName.ToString() != SelectedTypeKey)     { continue; }
        if (!WT_IssueMatches(E.IssueFlags, SelectedIssue))                                  { continue; }
        if (SelectedRatioBand >= 0)
        {
            const float Pct = WT_RowRatioPercent(E);
            if (Pct < 0.f || WT_RatioBandOf(Pct) != SelectedRatioBand)                      { continue; }
        }
        // ★1.0.7i 里程碑阶段照样叠加（否则「T3 (40)」会虚高）
        if (SelectedMilestone >= 0 && E.MilestoneTier != SelectedMilestone)                  { continue; }
        ++N;
    }
    return N;
}

int32 URemote_FactoryPanelWidget::CountEntriesWithType(const FString& TypeKey) const
{
    static const float BandMaxM[3] = { 100.f, 500.f, 1000.f };
    const FString SearchToken = bSearchActive ? SearchText : FString();

    int32 N = 0;
    for (const FWT_MachineEntry& E : CurrentList.Entries)
    {
        if (E.Category != static_cast<uint8>(SelectedCategory))                            { continue; }
        if (!WT_EntryMatchesSearch(E, SearchToken))                                        { continue; }

        // 本维（类型）单独判，其余维度按已选值叠加
        if (!TypeKey.IsEmpty() && E.DisplayName.ToString() != TypeKey)                      { continue; }

        if (SelectedProdState >= 0 && E.ProdState != static_cast<uint8>(SelectedProdState)) { continue; }
        if (SelectedNodePurity >= 0 && E.PurityFilter != SelectedNodePurity)                { continue; }
        if (SelectedDistBand >= 0 && SelectedDistBand <= 2
            && E.DistanceMeters > BandMaxM[SelectedDistBand])                               { continue; }
        if (SelectedAmountBand >= 0)
        {
            if (E.ContentStackCount < 0)                                                    { continue; }
            if (WT_AmountBandOf(E.ContentStackCount) != SelectedAmountBand)                  { continue; }
        }
        if (!SelectedItemKey.IsEmpty())
        {
            bool bHasItem = false;
            for (const FWT_ItemTag& Tag : E.ItemTags)
            {
                if (Tag.Key == SelectedItemKey) { bHasItem = true; break; }
            }
            if (!bHasItem)                                                                  { continue; }
        }
        if (!WT_IssueMatches(E.IssueFlags, SelectedIssue))                                  { continue; }
        if (SelectedRatioBand >= 0)
        {
            const float Pct = WT_RowRatioPercent(E);
            if (Pct < 0.f || WT_RatioBandOf(Pct) != SelectedRatioBand)                      { continue; }
        }
        // ★1.0.7i 里程碑阶段照样叠加（否则「T3 (40)」会虚高）
        if (SelectedMilestone >= 0 && E.MilestoneTier != SelectedMilestone)                  { continue; }
        ++N;
    }
    return N;
}

TArray<FWT_ItemTag> URemote_FactoryPanelWidget::CollectCategoryTypes() const
{
    TArray<FWT_ItemTag> Out;
    // 与 CollectCategoryItems 同一纪律：选项来源是**整份列表**，不跟筛选结果走，
    // 否则选完一个类型后下拉里只剩那一项，玩家再也切不回去。
    for (const FWT_MachineEntry& E : CurrentList.Entries)
    {
        if (E.Category != static_cast<uint8>(SelectedCategory))
        {
            continue;
        }
        const FString Name = E.DisplayName.ToString();
        if (Name.IsEmpty())
        {
            continue;
        }
        bool bDup = false;
        for (const FWT_ItemTag& Existing : Out)
        {
            if (Existing.Key == Name)
            {
                bDup = true;
                break;
            }
        }
        if (!bDup)
        {
            FWT_ItemTag Tag;
            Tag.Key = Name;
            Tag.Label = Name;
            Out.Add(Tag);
        }
    }
    Out.Sort([](const FWT_ItemTag& A, const FWT_ItemTag& B) { return A.Label < B.Label; });
    return Out;
}

/**
 * ★1.0.7i 当前类目里实际出现过的里程碑阶段（升序去重）。
 *
 * 与 CollectCategoryItems / CollectCategoryTypes 同一纪律：选项来源是**整份列表**，
 * 不跟当前筛选结果走 —— 否则选完 T3 之后下拉里只剩 T3，玩家再也切不回其它阶段。
 */
TArray<int32> URemote_FactoryPanelWidget::CollectCategoryMilestones() const
{
    TArray<int32> Out;
    for (const FWT_MachineEntry& E : CurrentList.Entries)
    {
        if (E.Category != static_cast<uint8>(SelectedCategory))
        {
            continue;
        }
        if (!Out.Contains(E.MilestoneTier))
        {
            Out.Add(E.MilestoneTier);
        }
    }
    Out.Sort();
    return Out;
}

/**
 * ★1.0.7i 里程碑阶段维度计数：本维取 Tier（<0 = 全部阶段），其余维度沿用已选值。
 *
 * 注意 99「未归类」是**真实的键**而不是「全部」的哨兵：扫描侧把查不到解锁蓝图的条目
 * 一律填 99，所以玩家点「未归类」时应该只看到那些行。哨兵是 -1。
 */
int32 URemote_FactoryPanelWidget::CountEntriesWithMilestone(int32 Tier) const
{
    static const float BandMaxM[3] = { 100.f, 500.f, 1000.f };
    const FString SearchToken = bSearchActive ? SearchText : FString();

    int32 N = 0;
    for (const FWT_MachineEntry& E : CurrentList.Entries)
    {
        if (E.Category != static_cast<uint8>(SelectedCategory))                            { continue; }
        if (!WT_EntryMatchesSearch(E, SearchToken))                                        { continue; }

        // 本维（阶段）单独判，其余维度按已选值叠加
        if (Tier >= 0 && E.MilestoneTier != Tier)                                          { continue; }

        if (SelectedProdState >= 0 && E.ProdState != static_cast<uint8>(SelectedProdState)) { continue; }
        if (SelectedNodePurity >= 0 && E.PurityFilter != SelectedNodePurity)                { continue; }
        if (SelectedDistBand >= 0 && SelectedDistBand <= 2
            && E.DistanceMeters > BandMaxM[SelectedDistBand])                               { continue; }
        if (SelectedAmountBand >= 0)
        {
            if (E.ContentStackCount < 0)                                                    { continue; }
            if (WT_AmountBandOf(E.ContentStackCount) != SelectedAmountBand)                  { continue; }
        }
        if (!SelectedItemKey.IsEmpty())
        {
            bool bHasItem = false;
            for (const FWT_ItemTag& Tag : E.ItemTags)
            {
                if (Tag.Key == SelectedItemKey) { bHasItem = true; break; }
            }
            if (!bHasItem)                                                                  { continue; }
        }
        if (!SelectedTypeKey.IsEmpty() && E.DisplayName.ToString() != SelectedTypeKey)      { continue; }
        if (!WT_IssueMatches(E.IssueFlags, SelectedIssue))                                  { continue; }
        if (SelectedRatioBand >= 0)
        {
            const float Pct = WT_RowRatioPercent(E);
            if (Pct < 0.f || WT_RatioBandOf(Pct) != SelectedRatioBand)                      { continue; }
        }
        ++N;
    }
    return N;
}

TArray<FWT_ItemTag> URemote_FactoryPanelWidget::CollectCategoryItems() const
{
    TArray<FWT_ItemTag> Out;
    // ★选项来源刻意是**整份列表**，不受搜索词与其它维度影响。
    // 若跟着筛选结果走，玩家选了「铁板」之后下拉里就只剩铁板一项，再也切不回别的物品 ——
    // 典型的自锁：筛完只能靠切类目才恢复。
    for (const FWT_MachineEntry& E : CurrentList.Entries)
    {
        if (E.Category != static_cast<uint8>(SelectedCategory))
        {
            continue;
        }
        for (const FWT_ItemTag& Tag : E.ItemTags)
        {
            if (Tag.Key.IsEmpty())
            {
                continue;
            }
            bool bDup = false;
            for (const FWT_ItemTag& Existing : Out)
            {
                if (Existing.Key == Tag.Key)
                {
                    bDup = true;
                    break;
                }
            }
            if (!bDup)
            {
                Out.Add(Tag);
            }
        }
    }
    Out.Sort([](const FWT_ItemTag& A, const FWT_ItemTag& B) { return A.Label < B.Label; });
    return Out;
}

void URemote_FactoryPanelWidget::RebuildFilterBar()
{
    if (!PurityBarBox)
    {
        return;
    }
    PurityBarBox->ClearChildren();
    // ★1.0.8 必须在下面「没有维度就 return」之前清引用：ClearChildren 已经把旧下拉从树里摘掉，
    // 若这个分支直接返回，FilterCombos 里就留着指向已摘除控件的悬空引用，
    // 下一次回调 FindRef 拿到的就是「树外的孤儿」，改了也没人看得见。
    FilterCombos.Reset();
    CurrentItemOptionKeys.Reset();
    // ★1.0.7i 补两处清理：类型键数组原来是「只加不清」，第二次重建后下标就整体错位
    //（下拉显示「全部类型」但实际按上一次的键过滤），属于 1.0.11 遗留的真 bug。
    CurrentTypeOptionKeys.Reset();
    CurrentMilestoneOptionKeys.Reset();

    /**
     * ★1.0.7i 筛选条回到**单行**（用户定案：「过滤条件全部缩小至一行内」，并把与列表的间距拉开）。
     *
     * 1.0.11 当初为了防止 7 个下拉挤成一排被截成「状...」，改成每行最多 4 个自动折行 ——
     * 实测观感是「两行下拉 + 一条提示行」把列表顶下去，过滤区反而更占高度。
     * 这一版换成「一行 + 每个控件收窄」：
     *   * 选项文案去掉与维度名重复的后缀（全部状态 → 全部，全部物品 → 全部），每个省 2 字；
     *   * 统一字号 11 / 内容边距 4,1，深色底 + 近白字（WT_StyleFilterCombo）；
     *   * 每个控件 Fill 均分、永不换行 ⇒ 行数恒为 1，切类目时列表起点不跳。
     */
    UHorizontalBox* FilterRow = WidgetTree->ConstructWidget<UHorizontalBox>();

    /** 控件计数：高度闸门（FilterHeightBox）按「这一行有没有东西」决定占位高度。 */
    int32 FilterItemCount = 0;

    /** 把建好的控件挂到这一行（先刷深色样式，再 Fill 均分 + 4px 间距）。 */
    auto Attach = [this, FilterRow, &FilterItemCount](UWidget* W)
    {
        if (!W)
        {
            return;
        }
        if (UComboBoxString* Combo = Cast<UComboBoxString>(W))
        {
            WT_StyleFilterCombo(Combo);
        }
        if (UHorizontalBoxSlot* S = FilterRow->AddChildToHorizontalBox(W))
        {
            S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            S->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
            S->SetVerticalAlignment(VAlign_Center);
        }
        ++FilterItemCount;
    };

    /** 建一个档位下拉：选项文本 = "维度名: 取值 (计数)"，选中项按当前状态值定位。 */
    auto BuildCombo = [this, &Attach](EWT_FilterDim Dim, const FText& Caption,
                                  const TArray<TPair<int32, FText>>& Options,
                                  int32 CurrentValue) -> UComboBoxString*
    {
        UComboBoxString* Combo = WidgetTree->ConstructWidget<UWTFilterComboBox>();
        Combo->ClearOptions();

        int32 SelectedIdx = 0;
        for (int32 i = 0; i < Options.Num(); ++i)
        {
            const int32 Cnt = CountEntriesForDim(Dim, Options[i].Key);
            Combo->AddOption(FString::Printf(TEXT("%s: %s (%d)"),
                *Caption.ToString(), *Options[i].Value.ToString(), Cnt));
            if (Options[i].Key == CurrentValue)
            {
                SelectedIdx = i;
            }
        }
        Combo->SetSelectedIndex(SelectedIdx);
        FilterCombos.Add(static_cast<int32>(Dim), Combo);   // 回调靠它反查「是哪个维度变了」
        Attach(Combo);
        return Combo;
    };

    /**
     * ★1.0.10 物品下拉：选项 = 这一列里**实际出现过**的物品（用户要的「已存在物品」），
     * 第 0 项固定「全部物品」。键是物品类名，按下标另存在 CurrentItemOptionKeys 里。
     *
     * 为什么不去解析下拉文本取物品名：选项文本是「产物: 铁板 (12)」这种拼装串，
     * 解析它等于拿 UI 文案当协议，改一次文案筛选就静默失效。
     */
    auto BuildItemCombo = [this, &Attach](const FText& Caption) -> UComboBoxString*
    {
        UComboBoxString* Combo = WidgetTree->ConstructWidget<UWTFilterComboBox>();
        Combo->ClearOptions();

        const TArray<FWT_ItemTag> Items = CollectCategoryItems();

        // 第 0 项：全部物品
        CurrentItemOptionKeys.Add(FString());
        Combo->AddOption(FString::Printf(TEXT("%s: %s (%d)"),
            *Caption.ToString(),
            *WT_LOCTEXT("FltItem_All", "全部物品").ToString(),
            CountEntriesWithItem(FString())));

        int32 SelectedIdx = 0;
        for (const FWT_ItemTag& Tag : Items)
        {
            CurrentItemOptionKeys.Add(Tag.Key);
            Combo->AddOption(FString::Printf(TEXT("%s: %s (%d)"),
                *Caption.ToString(), *Tag.Label, CountEntriesWithItem(Tag.Key)));
            if (!SelectedItemKey.IsEmpty() && Tag.Key == SelectedItemKey)
            {
                SelectedIdx = CurrentItemOptionKeys.Num() - 1;
            }
        }

        // ★记忆失效兜底：上次选的物品在这一列已不存在（箱子被搬空 / 刚换了类目），
        // 若下拉显示「全部物品」却仍按旧 Key 过滤，就会出现「屏幕说全部、列表却是空的」
        // 这种自相矛盾。这里把状态一并清掉，保证下拉显示 == 实际过滤条件。
        if (!SelectedItemKey.IsEmpty() && SelectedIdx == 0)
        {
            SelectedItemKey.Empty();
        }

        Combo->SetSelectedIndex(SelectedIdx);
        FilterCombos.Add(static_cast<int32>(EWT_FilterDim::Item), Combo);
        Attach(Combo);
        return Combo;
    };

    // ★注意 AddDynamic 宏会把第二个参数**字符串化**再按名字查找，所以这里每个回调都必须
    //   写成字面量成员函数名；把函数指针当参数传进去会编过但运行时不触发。
    /**
     * ★1.0.11 类型下拉：选项 = 这一类目里**实际出现过**的建筑类型。
     * 用户方案稿里每个子类目的第一项都是【建筑类型】，而列表的分组头本来就是按
     * DisplayName 分的 —— 这里沿用同一个键，下拉里选「组装机」和列表里看到的组头必然一致。
     */
    auto BuildTypeCombo = [this, &Attach](const FText& Caption) -> UComboBoxString*
    {
        UComboBoxString* Combo = WidgetTree->ConstructWidget<UWTFilterComboBox>();
        Combo->ClearOptions();

        const TArray<FWT_ItemTag> Types = CollectCategoryTypes();

        CurrentTypeOptionKeys.Add(FString());
        Combo->AddOption(FString::Printf(TEXT("%s: %s (%d)"),
            *Caption.ToString(),
            *WT_LOCTEXT("FltType_All", "全部").ToString(),
            CountEntriesWithType(FString())));

        int32 SelectedIdx = 0;
        for (const FWT_ItemTag& Tag : Types)
        {
            CurrentTypeOptionKeys.Add(Tag.Key);
            Combo->AddOption(FString::Printf(TEXT("%s: %s (%d)"),
                *Caption.ToString(), *Tag.Label, CountEntriesWithType(Tag.Key)));
            if (!SelectedTypeKey.IsEmpty() && Tag.Key == SelectedTypeKey)
            {
                SelectedIdx = CurrentTypeOptionKeys.Num() - 1;
            }
        }
        // 记忆失效兜底：上次选的类型这一列已经没有了 ⇒ 把状态一并清掉，
        // 保证「下拉显示全部类型」与「实际不过滤」一致（否则列表会莫名其妙全空）。
        if (!SelectedTypeKey.IsEmpty() && SelectedIdx == 0)
        {
            SelectedTypeKey.Empty();
        }

        Combo->SetSelectedIndex(SelectedIdx);
        FilterCombos.Add(static_cast<int32>(EWT_FilterDim::Type), Combo);
        Attach(Combo);
        return Combo;
    };

    /**
     * ★1.0.11 排序下拉（用户方案稿统一模板里的 [↑↓ 排序]）。
     *
     * 排序**不是**过滤：它不参与 CountEntriesForDim / PassesCategoryFilters，
     * 只在 RebuildMachineList 的分组排序里生效。选项顺序即 SelectedSort 的取值。
     */
    auto BuildSortCombo = [this, &Attach]() -> UComboBoxString*
    {
        UComboBoxString* Combo = WidgetTree->ConstructWidget<UWTFilterComboBox>();
        Combo->ClearOptions();

        const FText SortCap = WT_LOCTEXT("FltCap_Sort", "排序");
        static const TCHAR* SortLabels[] = {
            TEXT("距离近→远"), TEXT("距离远→近"), TEXT("类型名"),
            TEXT("比例低→高"), TEXT("比例高→低"), TEXT("数量多→少"),
        };
        int32 SelectedIdx = 0;
        for (int32 i = 0; i < UE_ARRAY_COUNT(SortLabels); ++i)
        {
            Combo->AddOption(FString::Printf(TEXT("%s: %s"), *SortCap.ToString(), SortLabels[i]));
            if (i == SelectedSort)
            {
                SelectedIdx = i;
            }
        }
        if (SelectedSort < 0 || SelectedSort >= static_cast<int32>(UE_ARRAY_COUNT(SortLabels)))
        {
            SelectedSort = 0;
            SelectedIdx = 0;
        }
        Combo->SetSelectedIndex(SelectedIdx);
        FilterCombos.Add(static_cast<int32>(EWT_FilterDim::Sort), Combo);
        Attach(Combo);
        return Combo;
    };

    /**
     * ★1.0.7i 里程碑阶段下拉：选项 = 这一类目里**实际出现过**的阶段（用户诉求「新增里程碑阶段」）。
     * 键是阶段号（int32），按下标另存在 CurrentMilestoneOptionKeys 里 —— 与物品/类型同一套做法，
     * 绝不解析下拉文本。
     */
    auto BuildMilestoneCombo = [this, &Attach](const FText& Caption) -> UComboBoxString*
    {
        UComboBoxString* Combo = WidgetTree->ConstructWidget<UWTFilterComboBox>();
        Combo->ClearOptions();

        const TArray<int32> Tiers = CollectCategoryMilestones();

        CurrentMilestoneOptionKeys.Add(-1);
        Combo->AddOption(FString::Printf(TEXT("%s: %s (%d)"),
            *Caption.ToString(),
            *WT_LOCTEXT("FltMilestone_All", "全部").ToString(),
            CountEntriesWithMilestone(-1)));

        int32 SelectedIdx = 0;
        for (int32 Tier : Tiers)
        {
            CurrentMilestoneOptionKeys.Add(Tier);
            const FText TierLabel = (Tier >= 99)
                ? WT_LOCTEXT("FltMilestone_Unknown", "未归类")
                : FText::Format(WT_LOCTEXT("FltMilestone_TierFmt", "T{0}"), FText::AsNumber(Tier));
            Combo->AddOption(FString::Printf(TEXT("%s: %s (%d)"),
                *Caption.ToString(), *TierLabel.ToString(), CountEntriesWithMilestone(Tier)));
            if (Tier == SelectedMilestone)
            {
                SelectedIdx = CurrentMilestoneOptionKeys.Num() - 1;
            }
        }
        // 记忆失效兜底：上次选的阶段在这一列已不存在 ⇒ 连状态一起清掉（同物品/类型维度的理由）。
        if (SelectedMilestone != -1 && SelectedIdx == 0)
        {
            SelectedMilestone = -1;
        }

        Combo->SetSelectedIndex(SelectedIdx);
        FilterCombos.Add(static_cast<int32>(EWT_FilterDim::Milestone), Combo);
        Attach(Combo);
        return Combo;
    };

    const TArray<TPair<int32, FText>> StateOpts = {
        { -1, WT_LOCTEXT("FltState_All",     "全部") },
        {  0, WT_LOCTEXT("FltState_Idle",    "空闲")     },
        {  1, WT_LOCTEXT("FltState_Running", "运行中")   },
        {  2, WT_LOCTEXT("FltState_Standby", "待机")     },
        {  3, WT_LOCTEXT("FltState_Blocked", "阻塞")     },
    };
    const TArray<TPair<int32, FText>> AmountOpts = {
        { -1, WT_LOCTEXT("FltAmt_All",   "全部") },
        {  0, WT_LOCTEXT("FltAmt_Empty", "空")       },
        {  1, WT_LOCTEXT("FltAmt_Low",   "少量")     },
        {  2, WT_LOCTEXT("FltAmt_Mid",   "中量")     },
        {  3, WT_LOCTEXT("FltAmt_High",  "大量")     },
    };
    const TArray<TPair<int32, FText>> PurityOpts = {
        { -1, WT_LOCTEXT("FltPur_All",    "全部") },
        {  0, WT_LOCTEXT("FltPur_Impure", "贫瘠")     },
        {  1, WT_LOCTEXT("FltPur_Normal", "普通")     },
        {  2, WT_LOCTEXT("FltPur_Pure",   "纯")       },
    };
    const TArray<TPair<int32, FText>> DistOpts = {
        { -1, WT_LOCTEXT("FltDist_All", "全部") },
        {  0, WT_LOCTEXT("FltDist_100", "100m 内")  },
        {  1, WT_LOCTEXT("FltDist_500", "500m 内")  },
        {  2, WT_LOCTEXT("FltDist_1k",  "1km 内")   },
    };
    /**
     * ★1.0.8AN 收集品「拾取」下拉（用户 05:15：「收集品中【拾取】我需要和后面的【距离】
     * 【排序】要一样」）。选项与 `SelectedCollectiblePicked` 一一对应，计数走 CountEntriesForDim。
     */
    const TArray<TPair<int32, FText>> PickedOpts = {
        { -1, WT_LOCTEXT("FltPicked_All",    "全部")   },
        {  0, WT_LOCTEXT("FltPicked_Open",   "未拾取") },
        {  1, WT_LOCTEXT("FltPicked_Done",   "已拾取") },
    };
    const TArray<TPair<int32, FText>> RatioOpts = {
        { -1, WT_LOCTEXT("FltRatio_All", "全部") },
        {  0, WT_LOCTEXT("FltRatio_25",  "0-25%")    },
        {  1, WT_LOCTEXT("FltRatio_50",  "25-50%")   },
        {  2, WT_LOCTEXT("FltRatio_75",  "50-75%")   },
        {  3, WT_LOCTEXT("FltRatio_100", "75-100%")  },
    };
    const EWT_MachineCategory Cat = static_cast<EWT_MachineCategory>(SelectedCategory);

    // ★1.0.10 每个子类目「贴合自身」的维度表（用户口径：所有子类目都要有筛选条）。
    // 顺序 = 界面从左到右；「距离」永远排在最后一位，所有类目共用。
    // ★1.0.8AN 用户 05:15「建筑栏移除【问题】过滤条件」⇒ 建筑组下**所有**子类目都不再建【问题】下拉。
    //   理由（也是这次要撤的现实原因）：这一排 7 个下拉在面板宽度内本来就已经挤到尾巴被裁字，
    //   而「问题」这类信息在类目行上还有红色圆点 + 数字徽标可用 ⇒ 撤掉不丢信息，只腾出宽度。
    //   注意：`SelectedIssue` 字段本身保留（切类目时统一归 -1），过滤逻辑不受影响，只是界面上不再有入口。
    switch (Cat)
    {
        case EWT_MachineCategory::Producer:
            // ★1.0.11 用户方案稿：类型 | 状态 | 产物 | 效率 | 阶段 | 距离 | 排序（1.0.8AN 撤【问题】）
            BuildTypeCombo(WT_LOCTEXT("FltCap_Type", "类型"))
                ->OnSelectionChanged.AddDynamic(this, &URemote_FactoryPanelWidget::OnTypeComboChanged);
            BuildCombo(EWT_FilterDim::ProdState, WT_LOCTEXT("FltCap_State", "状态"), StateOpts, SelectedProdState)
                ->OnSelectionChanged.AddDynamic(this, &URemote_FactoryPanelWidget::OnProdStateComboChanged);
            BuildItemCombo(WT_LOCTEXT("FltCap_Product", "产物"))
                ->OnSelectionChanged.AddDynamic(this, &URemote_FactoryPanelWidget::OnItemComboChanged);
            BuildCombo(EWT_FilterDim::Ratio, WT_LOCTEXT("FltCap_Eff", "效率"), RatioOpts, SelectedRatioBand)
                ->OnSelectionChanged.AddDynamic(this, &URemote_FactoryPanelWidget::OnRatioComboChanged);
            BuildMilestoneCombo(WT_LOCTEXT("FltCap_Milestone", "阶段"))
                ->OnSelectionChanged.AddDynamic(this, &URemote_FactoryPanelWidget::OnMilestoneComboChanged);
            break;

        case EWT_MachineCategory::Miner:
            // 用户口径：采矿设备 = 【资源节点（可用搜索框搜索）】【纯度】【距离】
            // 资源节点这一维就是「矿种物品」，选项随当前列表里的矿点自动生成，
            // 同时搜索框会匹配 ItemTags ⇒ 也可以直接打字搜矿种。
            // ★1.0.11 补【类型】（矿机 Mk.1/2/3 就靠它筛）。（1.0.8AN 撤【问题】）
            BuildTypeCombo(WT_LOCTEXT("FltCap_Type", "类型"))
                ->OnSelectionChanged.AddDynamic(this, &URemote_FactoryPanelWidget::OnTypeComboChanged);
            BuildItemCombo(WT_LOCTEXT("FltCap_Node", "资源节点"))
                ->OnSelectionChanged.AddDynamic(this, &URemote_FactoryPanelWidget::OnItemComboChanged);
            BuildCombo(EWT_FilterDim::Purity, WT_LOCTEXT("FltCap_Purity", "纯度"), PurityOpts, SelectedNodePurity)
                ->OnSelectionChanged.AddDynamic(this, &URemote_FactoryPanelWidget::OnPurityComboChanged);
            BuildMilestoneCombo(WT_LOCTEXT("FltCap_Milestone", "阶段"))
                ->OnSelectionChanged.AddDynamic(this, &URemote_FactoryPanelWidget::OnMilestoneComboChanged);
            break;

        case EWT_MachineCategory::Generator:
            // ★1.0.11 用户方案稿：类型 | 状态 | 燃料 | 阶段 | 距离 | 排序（1.0.8AN 撤【问题】）
            BuildTypeCombo(WT_LOCTEXT("FltCap_Type", "类型"))
                ->OnSelectionChanged.AddDynamic(this, &URemote_FactoryPanelWidget::OnTypeComboChanged);
            BuildCombo(EWT_FilterDim::ProdState, WT_LOCTEXT("FltCap_State", "状态"), StateOpts, SelectedProdState)
                ->OnSelectionChanged.AddDynamic(this, &URemote_FactoryPanelWidget::OnProdStateComboChanged);
            BuildItemCombo(WT_LOCTEXT("FltCap_Fuel", "燃料"))
                ->OnSelectionChanged.AddDynamic(this, &URemote_FactoryPanelWidget::OnItemComboChanged);
            BuildMilestoneCombo(WT_LOCTEXT("FltCap_Milestone", "阶段"))
                ->OnSelectionChanged.AddDynamic(this, &URemote_FactoryPanelWidget::OnMilestoneComboChanged);
            break;

        case EWT_MachineCategory::Container:
            // 用户口径：容器 = 【物品】【数量】【距离】
            // ★1.0.11 补【类型】（箱子/工业箱/液体罐）+【填充率】。（1.0.8AN 撤【问题】）
            BuildTypeCombo(WT_LOCTEXT("FltCap_Type", "类型"))
                ->OnSelectionChanged.AddDynamic(this, &URemote_FactoryPanelWidget::OnTypeComboChanged);
            BuildItemCombo(WT_LOCTEXT("FltCap_Item", "物品"))
                ->OnSelectionChanged.AddDynamic(this, &URemote_FactoryPanelWidget::OnItemComboChanged);
            BuildCombo(EWT_FilterDim::Amount, WT_LOCTEXT("FltCap_Amount", "数量"), AmountOpts, SelectedAmountBand)
                ->OnSelectionChanged.AddDynamic(this, &URemote_FactoryPanelWidget::OnAmountComboChanged);
            BuildCombo(EWT_FilterDim::Ratio, WT_LOCTEXT("FltCap_Fill", "填充率"), RatioOpts, SelectedRatioBand)
                ->OnSelectionChanged.AddDynamic(this, &URemote_FactoryPanelWidget::OnRatioComboChanged);
            BuildMilestoneCombo(WT_LOCTEXT("FltCap_Milestone", "阶段"))
                ->OnSelectionChanged.AddDynamic(this, &URemote_FactoryPanelWidget::OnMilestoneComboChanged);
            break;

        case EWT_MachineCategory::Station:
        case EWT_MachineCategory::Vehicle:
            // ★1.0.11 用户方案稿：类型 | 物品 | 阶段 | 距离 | 排序（1.0.8AN 撤【问题】）
            BuildTypeCombo(WT_LOCTEXT("FltCap_Type", "类型"))
                ->OnSelectionChanged.AddDynamic(this, &URemote_FactoryPanelWidget::OnTypeComboChanged);
            BuildItemCombo(WT_LOCTEXT("FltCap_Item", "物品"))
                ->OnSelectionChanged.AddDynamic(this, &URemote_FactoryPanelWidget::OnItemComboChanged);
            BuildCombo(EWT_FilterDim::Amount, WT_LOCTEXT("FltCap_Amount", "数量"), AmountOpts, SelectedAmountBand)
                ->OnSelectionChanged.AddDynamic(this, &URemote_FactoryPanelWidget::OnAmountComboChanged);
            BuildMilestoneCombo(WT_LOCTEXT("FltCap_Milestone", "阶段"))
                ->OnSelectionChanged.AddDynamic(this, &URemote_FactoryPanelWidget::OnMilestoneComboChanged);
            break;

        case EWT_MachineCategory::Modded:
            BuildTypeCombo(WT_LOCTEXT("FltCap_Type", "类型"))
                ->OnSelectionChanged.AddDynamic(this, &URemote_FactoryPanelWidget::OnTypeComboChanged);
            BuildItemCombo(WT_LOCTEXT("FltCap_Item", "物品"))
                ->OnSelectionChanged.AddDynamic(this, &URemote_FactoryPanelWidget::OnItemComboChanged);
            BuildMilestoneCombo(WT_LOCTEXT("FltCap_Milestone", "阶段"))
                ->OnSelectionChanged.AddDynamic(this, &URemote_FactoryPanelWidget::OnMilestoneComboChanged);
            break;

        case EWT_MachineCategory::Hub:
        case EWT_MachineCategory::Portal:
        case EWT_MachineCategory::PowerSwitch:
            // ★1.0.11 枢纽/传送门/开关：类型 | 阶段 | 距离 | 排序（1.0.8AN 撤【问题】）。
            BuildTypeCombo(WT_LOCTEXT("FltCap_Type", "类型"))
                ->OnSelectionChanged.AddDynamic(this, &URemote_FactoryPanelWidget::OnTypeComboChanged);
            BuildMilestoneCombo(WT_LOCTEXT("FltCap_Milestone", "阶段"))
                ->OnSelectionChanged.AddDynamic(this, &URemote_FactoryPanelWidget::OnMilestoneComboChanged);
            break;

        case EWT_MachineCategory::PowerGrid:
            // ★1.0.11 电网**不建筛选条**：它的内容不是 Entries，而是电路聚合 + 曲线，
            // 任何维度都数不出东西。建一排恒为 (0) 的下拉等于给玩家一组点了没反应的死控件，
            // 比没有更糟。所以这里什么都不建，末尾的距离/排序也一并跳过（见下面的 bNoFilterBar）。
            break;

        case EWT_MachineCategory::ResourceNode_Vanilla:
        case EWT_MachineCategory::ResourceNode_Mod:
            BuildCombo(EWT_FilterDim::Purity, WT_LOCTEXT("FltCap_Purity", "纯度"), PurityOpts, SelectedNodePurity)
                ->OnSelectionChanged.AddDynamic(this, &URemote_FactoryPanelWidget::OnPurityComboChanged);
            BuildMilestoneCombo(WT_LOCTEXT("FltCap_Milestone", "阶段"))
                ->OnSelectionChanged.AddDynamic(this, &URemote_FactoryPanelWidget::OnMilestoneComboChanged);
            break;

        default:
            // ★1.0.8AN 收集品：「拾取」改成**下拉**，与后面的【距离】【排序】完全同款
            // （同一个 UWTFilterComboBox、同样的「标题: 值 (N)」文案、同样的展开方式）。
            // 用户 05:15 原话：「收集品中【拾取】我需要和后面的【距离】【排序】要一样」。
            // 仍然是独立的一个过滤条件（不再挂在种类下面），状态仍由 SelectedCollectiblePicked 驱动，
            // 只是从「两个按钮」变成一个「全部 / 未拾取 / 已拾取」三选项下拉。
            if (SelectedCollectibleKind >= 0)
            {
                BuildCombo(EWT_FilterDim::Picked, WT_LOCTEXT("FltCap_Picked", "拾取"), PickedOpts, SelectedCollectiblePicked)
                    ->OnSelectionChanged.AddDynamic(this, &URemote_FactoryPanelWidget::OnPickedComboChanged);
            }
            break;
    }

    // ★1.0.8AN 保险丝：建筑组已经不再建【问题】下拉（用户 05:15），但「每个类目记住的旧选择」
    //   仍可能把 SelectedIssue 从状态栈里恢复成非 -1 ⇒ 列表会被一个**界面上根本看不见**的
    //   条件悄悄筛掉（表现是「某些机器莫名消失」）。这一轮没建 Issue 下拉就把状态压回「全部」。
    if (!FilterCombos.Contains(static_cast<int32>(EWT_FilterDim::Issue)))
    {
        SelectedIssue = -1;
    }

    // 电网类目没有任何可用维度（见上面的 case），连距离/排序都省略 ——
    // 否则会出现「筛了距离但曲线不变」的假控件。
    if (Cat != EWT_MachineCategory::PowerGrid)
    {
        BuildCombo(EWT_FilterDim::Distance, WT_LOCTEXT("FltCap_Dist", "距离"), DistOpts, SelectedDistBand)
            ->OnSelectionChanged.AddDynamic(this, &URemote_FactoryPanelWidget::OnDistComboChanged);

        // ★1.0.11 排序永远放最后（用户方案稿统一模板：[搜索][分类][多选过滤][开关][滑动条][排序]）
        BuildSortCombo()->OnSelectionChanged.AddDynamic(this, &URemote_FactoryPanelWidget::OnSortComboChanged);
    }

    // ★1.0.7i 唯一一行挂进容器（不再有多行 Bars）。
    // 右侧留 12px 与列表同宽；上留 2px、下留 4px —— 与搜索框/列表各留一口气，
    // 免得「过滤条贴着列表第一行」（用户实测反馈两者过于贴近）。
    if (FilterItemCount > 0)
    {
        if (UVerticalBoxSlot* S = PurityBarBox->AddChildToVerticalBox(FilterRow))
        {
            S->SetPadding(FMargin(0.f, 2.f, 12.f, 4.f));
        }
    }

    // ★1.0.7i 高度闸门改成「一行」：有控件 = 30px（字号 11 + 内容边距 1*2 + 按钮内边距 2*2），
    // 没控件（电网类目）= 0，曲线图上不会顶出空白。
    if (FilterHeightBox)
    {
        FilterHeightBox->SetHeightOverride(FilterItemCount > 0 ? 30.f : 0.f);
    }

    // ★1.0.7i 用户定案「移除指定的元素」：原来那条「纯度来源：游戏模式强制（非原版值）· 分布/…」
    // 提示行被删掉了。它有三个问题：① 单行长度超过面板宽度，会把整行挤成两行并压住下面一行下拉
    //（用户截图里两段文字叠在一起）；② 信息只在纯度被改造时才有意义，而这类玩家自己知道；
    // ③ 它占据了「过滤条必须只有一行」的唯一余量。数据本身仍在（ResourceNodePurityAudit），
    // 需要时按下面的 Warning 日志查。
    if ((Cat == EWT_MachineCategory::ResourceNode_Vanilla || Cat == EWT_MachineCategory::ResourceNode_Mod
         || Cat == EWT_MachineCategory::Miner)
        && (ResourceNodePurityAudit.Source == EWT_PuritySource::Modded
            || ResourceNodePurityAudit.Source == EWT_PuritySource::GameModeForced))
    {
        WT_WARNF(TEXT("[panel] purity source is NOT vanilla: %s | %s"),
                 ResourceNodePurityAudit.Source == EWT_PuritySource::Modded ? TEXT("modded") : TEXT("gamemode"),
                 *ResourceNodePurityAudit.Detail);
    }
}

// ---- 下拉回调：把「选中的第几项」翻回筛选值 -----------------------------------
// 选项顺序在 RebuildFilterBar 里固定（全部永远是第 0 项），所以这里用下标映射即可，
// 不需要解析选项文本。

void URemote_FactoryPanelWidget::OnProdStateComboChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    // ESelectInfo::Direct = 代码里 SetSelectedIndex 触发的，重绘时不该当成用户操作，
    // 否则每次重建筛选条都会被自己的 SetSelectedIndex 递归拉回 RebuildMachineList。
    if (SelectionType == ESelectInfo::Direct)
    {
        return;
    }
    static const int32 Values[] = { -1, 0, 1, 2, 3 };
    // 用当前下拉的实际选中项反查，而不是解析 SelectedItem 文本
    if (UComboBoxString* C = FilterCombos.FindRef(static_cast<int32>(EWT_FilterDim::ProdState)).Get())
    {
        const int32 Idx = C->GetSelectedIndex();
        SelectedProdState = (Idx >= 0 && Idx < UE_ARRAY_COUNT(Values)) ? Values[Idx] : -1;
        ApplyFilterChange();
    }
}

void URemote_FactoryPanelWidget::OnPurityComboChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    if (SelectionType == ESelectInfo::Direct)
    {
        return;
    }
    static const int32 Values[] = { -1, 0, 1, 2 };
    if (UComboBoxString* C = FilterCombos.FindRef(static_cast<int32>(EWT_FilterDim::Purity)).Get())
    {
        const int32 Idx = C->GetSelectedIndex();
        SelectedNodePurity = (Idx >= 0 && Idx < UE_ARRAY_COUNT(Values)) ? Values[Idx] : -1;
        ApplyFilterChange();
    }
}

void URemote_FactoryPanelWidget::OnDistComboChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    if (SelectionType == ESelectInfo::Direct)
    {
        return;
    }
    static const int32 Values[] = { -1, 0, 1, 2 };
    if (UComboBoxString* C = FilterCombos.FindRef(static_cast<int32>(EWT_FilterDim::Distance)).Get())
    {
        const int32 Idx = C->GetSelectedIndex();
        SelectedDistBand = (Idx >= 0 && Idx < UE_ARRAY_COUNT(Values)) ? Values[Idx] : -1;
        ApplyFilterChange();
    }
}

void URemote_FactoryPanelWidget::OnPickedComboChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    // ESelectInfo::Direct = 重建筛选条时 SetSelectedIndex 触发的，不是玩家操作。
    if (SelectionType == ESelectInfo::Direct)
    {
        return;
    }
    static const int32 Values[] = { -1, 0, 1 };   // 全部 / 未拾取 / 已拾取（与 PickedOpts 顺序一致）
    if (UComboBoxString* C = FilterCombos.FindRef(static_cast<int32>(EWT_FilterDim::Picked)).Get())
    {
        const int32 Idx = C->GetSelectedIndex();
        SetCollectiblePickedFilter((Idx >= 0 && Idx < static_cast<int32>(UE_ARRAY_COUNT(Values))) ? Values[Idx] : -1);
    }
}

void URemote_FactoryPanelWidget::OnItemComboChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    // ESelectInfo::Direct = 代码里 SetSelectedIndex 触发的（重建筛选条时恢复选中项），
    // 不该当成用户操作，否则每次重建都会被自己拉回去重画一遍列表。
    if (SelectionType == ESelectInfo::Direct)
    {
        return;
    }
    UComboBoxString* C = FilterCombos.FindRef(static_cast<int32>(EWT_FilterDim::Item)).Get();
    if (!C)
    {
        return;
    }
    // 下标与 CurrentItemOptionKeys 一一对齐（第 0 项恒为空串 = 全部物品）。
    // 越界一律当「全部」——宁可不过滤，也不要因为一个错位把列表筛成全空。
    const int32 Idx = C->GetSelectedIndex();
    SelectedItemKey = CurrentItemOptionKeys.IsValidIndex(Idx) ? CurrentItemOptionKeys[Idx] : FString();
    ApplyFilterChange();
}

void URemote_FactoryPanelWidget::OnAmountComboChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    if (SelectionType == ESelectInfo::Direct)
    {
        return;
    }
    // 顺序必须与 RebuildFilterBar 里的 AmountOpts 逐项一致
    static const int32 Values[] = { -1, 0, 1, 2, 3 };
    if (UComboBoxString* C = FilterCombos.FindRef(static_cast<int32>(EWT_FilterDim::Amount)).Get())
    {
        const int32 Idx = C->GetSelectedIndex();
        SelectedAmountBand = (Idx >= 0 && Idx < UE_ARRAY_COUNT(Values)) ? Values[Idx] : -1;
        ApplyFilterChange();
    }
}

void URemote_FactoryPanelWidget::OnTypeComboChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    if (SelectionType == ESelectInfo::Direct)
    {
        return;
    }
    UComboBoxString* C = FilterCombos.FindRef(static_cast<int32>(EWT_FilterDim::Type)).Get();
    if (!C)
    {
        return;
    }
    // 下标与 CurrentTypeOptionKeys 一一对齐（第 0 项恒为空串 = 全部类型）。
    // 越界一律当「全部」—— 宁可不过滤，也不要因一个错位把列表筛成全空。
    const int32 Idx = C->GetSelectedIndex();
    SelectedTypeKey = CurrentTypeOptionKeys.IsValidIndex(Idx) ? CurrentTypeOptionKeys[Idx] : FString();
    ApplyFilterChange();
}

/**
 * ★1.0.7i 里程碑阶段下拉变更。
 * 与物品 / 类型维度同一套：下标反查 CurrentMilestoneOptionKeys，**绝不解析下拉文本**
 *（文本前半截是「类目: 」这类可翻译前缀，解析必错）。
 */
void URemote_FactoryPanelWidget::OnMilestoneComboChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    if (SelectionType == ESelectInfo::Direct)
    {
        return;
    }
    UComboBoxString* C = FilterCombos.FindRef(static_cast<int32>(EWT_FilterDim::Milestone)).Get();
    if (!C)
    {
        return;
    }
    // 第 0 项恒为 -1（全部阶段）；越界一律当「全部」—— 宁可不过滤，也不要因错位筛成全空。
    const int32 Idx = C->GetSelectedIndex();
    SelectedMilestone = CurrentMilestoneOptionKeys.IsValidIndex(Idx) ? CurrentMilestoneOptionKeys[Idx] : -1;
    ApplyFilterChange();
}

// ★1.0.8AN：**当前没有绑定点** —— 用户 05:15 要求「建筑栏移除【问题】过滤条件」，
//   建筑组下所有子类目都不再建这个下拉。回调与过滤逻辑（WT_IssueMatches）全部保留：
//   哪天要把【问题】加回来，只需在对应 case 里重新 BuildCombo 并 AddDynamic 到这里。
void URemote_FactoryPanelWidget::OnIssueComboChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    if (SelectionType == ESelectInfo::Direct)
    {
        return;
    }
    // 取值编码（-1 全部 / 0 任意 / 1..8 = 枚举+1）必须与 WT_IssueMatches 的 `Sel - 1` 位移一致。
    static const int32 Values[] = { -1, 0, 1, 2, 3, 4, 5, 6, 7, 8 };
    if (UComboBoxString* C = FilterCombos.FindRef(static_cast<int32>(EWT_FilterDim::Issue)).Get())
    {
        const int32 Idx = C->GetSelectedIndex();
        SelectedIssue = (Idx >= 0 && Idx < UE_ARRAY_COUNT(Values)) ? Values[Idx] : -1;
        ApplyFilterChange();
    }
}

void URemote_FactoryPanelWidget::OnRatioComboChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    if (SelectionType == ESelectInfo::Direct)
    {
        return;
    }
    static const int32 Values[] = { -1, 0, 1, 2, 3 };
    if (UComboBoxString* C = FilterCombos.FindRef(static_cast<int32>(EWT_FilterDim::Ratio)).Get())
    {
        const int32 Idx = C->GetSelectedIndex();
        SelectedRatioBand = (Idx >= 0 && Idx < UE_ARRAY_COUNT(Values)) ? Values[Idx] : -1;
        ApplyFilterChange();
    }
}

void URemote_FactoryPanelWidget::OnSortComboChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    if (SelectionType == ESelectInfo::Direct)
    {
        return;
    }
    UComboBoxString* C = FilterCombos.FindRef(static_cast<int32>(EWT_FilterDim::Sort)).Get();
    if (!C)
    {
        return;
    }
    SelectedSort = FMath::Clamp(C->GetSelectedIndex(), 0, 5);
    // 排序**不改变任何计数**，所以不需要像其它维度那样延迟重建筛选条；
    // 这里只要绕过 RebuildMachineList 内部那次 RebuildFilterBar()（否则会在 ComboBox
    // 自己的回调栈里把它删掉），筛选条本身保持原样、选中项也不变。
    bDeferFilterBarRebuild = true;
    RebuildMachineList();
    bDeferFilterBarRebuild = false;
}

void URemote_FactoryPanelWidget::ApplyFilterChange()
{
    // ★1.0.8 关键：RebuildMachineList() 内部**也**会调 RebuildFilterBar()（那是为了让
    // 扫描/切类目后下拉里的计数同步刷新）。但此刻我们正处在某个 ComboBox 自己的
    // OnSelectionChanged 回调栈里 —— 若让它顺手重建筛选条，就等于在回调栈里删掉正在
    // 派发事件的那个下拉，正是要避免的「自己删自己」。
    // 所以这里打一个「本次跳过筛选条重建」的临时标记，把重建留给下一帧。
    bDeferFilterBarRebuild = true;
    RebuildMachineList();
    bDeferFilterBarRebuild = false;

    // CreateWeakLambda 绑 this：面板先被关掉时 lambda 自动失效，不会打到野指针。
    if (UWorld* W = GetWorld())
    {
        W->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
        {
            RebuildFilterBar();
        }));
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
    // ★1.0.28i 中列顶部纯度过滤条随右列一起重建（仅资源节点类目有内容，其余清空）
    // ★1.0.8 例外：调用方来自下拉回调时（bDeferFilterBarRebuild）跳过 —— 那种场景下
    // 重建筛选条会销毁正在派发事件的下拉，改由 ApplyFilterChange 延迟一帧处理。
    if (!bDeferFilterBarRebuild)
    {
        RebuildFilterBar();
    }
    CollectibleRowTexts.Reset();
    CollectibleRowSrcIdx.Reset();
    WT_LiveContainerRows.Reset();   // ★本轮 容器实时刷新：每次重建列表清空登记，下面逐行重新登记

    // ★1.0.9 【电网】类目走独立渲染路径。
    // 它的内容不是 Entries 里的行（电线杆在扫描时就被聚合进 Reply.Grids，不产生条目），
    // 而是「一张曲线 + 五项图例 + 每条电路一行」。
    if (SelectedCategory == static_cast<int32>(EWT_MachineCategory::PowerGrid))
    {
        RebuildGridView();
        return;
    }
    // 其它类目：把电网那块收起来（只切 Visibility，控件继续活着，切回来不用重建 Slate 资源）
    if (GridHeaderBox)
    {
        GridHeaderBox->SetVisibility(ESlateVisibility::Collapsed);
    }

    // ★1.0.8 搜索语义收敛：**只过滤当前类目**，不再跳到跨类目汇总视图。
    // SearchToken 在下面两条分支（收集品 / 机器）里各自参与过滤。
    const FString SearchToken = bSearchActive ? SearchText : FString();

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
            // ★1.0.8 收集品类目也要吃内嵌搜索框的词：搜索框现在就挂在列表头上，
            // 若这里不过滤，玩家在这一类目输入关键词会「按了没反应」，语义上说不通。
            // 收集品没有产物名/状态，只按显示名匹配。
            if (!SearchToken.IsEmpty() &&
                !C.DisplayName.ToString().Contains(SearchToken, ESearchCase::IgnoreCase))
            {
                continue;
            }
            // ★1.0.10 距离筛选：收集品类目现在也有筛选条（所有子类目都要有），
            // 这个下拉必须真的生效，否则就是「点了没反应」的死控件。
            if (SelectedDistBand >= 0 && SelectedDistBand <= 2)
            {
                static const float BandMaxM[3] = { 100.f, 500.f, 1000.f };
                if (C.DistanceMeters > BandMaxM[SelectedDistBand])
                {
                    continue;
                }
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
        int32 MilestoneTier = 99;   // ★本轮 资源节点按里程碑排序序位
        FString MilestoneLabel;     // ★本轮 里程碑 / 太空电梯阶段标签
        // ★1.0.11 排序键：比例取组内可计量成员的平均值，数量取组内求和。
        // 用平均而不是最大：一台 100% 的机器不该把整组 5 台 20% 的平均效率掩盖成 100%。
        float RatioSum = 0.f;
        int32 RatioN = 0;
        int64 CountSum = 0;
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
        // ★1.0.8 本类目筛选（状态下拉 / 库存下拉 / 纯度下拉 / 距离下拉，AND 叠加）。
        // 取代原来只对资源节点生效的单条纯度判断 —— 现在四个维度都由 PassesCategoryFilters 统一裁决。
        if (!PassesCategoryFilters(Entry))
        {
            continue;
        }
        // ★1.0.8 本类目搜索（只搜当前列表自己的条目）
        if (!WT_EntryMatchesSearch(Entry, SearchToken))
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
            // ★1.0.11 排序键累加（比例 / 数量）
            const float Pct = WT_RowRatioPercent(Entry);
            if (Pct >= 0.f) { G.RatioSum += Pct; ++G.RatioN; }
            if (Entry.ContentStackCount > 0) { G.CountSum += Entry.ContentStackCount; }
        }
        else
        {
            FWTTypeGroup G;
            G.Key = Key;
            G.Name = bModdedGroup ? FText::FromString(Entry.ModName) : Entry.DisplayName;
            G.NearestM = Entry.DistanceMeters;
            G.Members.Add(i);
            // ★本轮 资源节点里程碑排序：组携带其里程碑序位 + 标签（同组资源共享，取首个成员即可）
            G.MilestoneTier = Entry.MilestoneTier;
            G.MilestoneLabel = Entry.MilestoneLabel;
            // ★1.0.11 排序键（第一个成员）
            {
                const float Pct = WT_RowRatioPercent(Entry);
                if (Pct >= 0.f) { G.RatioSum += Pct; ++G.RatioN; }
                if (Entry.ContentStackCount > 0) { G.CountSum += Entry.ContentStackCount; }
            }
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

        // ★1.0.8 行内容：状态灯插在「状态文字」之前（用户定案）。
        //
        //   [名称  距离m]  [StatusDetail（产物名/矿种·纯度）]  [●灯]  [状态文字]
        //   [名称  距离m]  [StatusLine]                          ← 无状态类目（容器/矿点/车站…）
        //
        // 灯与文字同色（都出自 WT_ProdStateColor），所以「绿灯=运行中」是结构上绑定的，
        // 不是两处各写一份碰巧对上。
        UButton* LabelBtn = nullptr;
        {
            const bool bSelectedRow = (EntryIndex == SelectedEntry);
            const FLinearColor TextColor = bSelectedRow ? C_ON_SEL : C_VALUE;

            UHorizontalBox* Content = WidgetTree->ConstructWidget<UHorizontalBox>();

            UTextBlock* NameTb = MakeText(WidgetTree,
                FText::Format(WT_LOCTEXT("MachineRowName", "{0}   {1}m"),
                              Entry.DisplayName,
                              FText::AsNumber(FMath::RoundToInt(Entry.DistanceMeters))),
                13, false, TextColor, ETextJustify::Left);
            NameTb->SetAutoWrapText(false);   // 行内单行，换行会把整行撑高
            if (UHorizontalBoxSlot* S = Content->AddChildToHorizontalBox(NameTb))
            {
                S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
                S->SetVerticalAlignment(VAlign_Center);
            }

            // ★1.0.7i 材料/产物图标统一放在**列表文本前方**（用户诉求）。
            // 原来它挂在行尾「传送」按钮左边，读起来像「这台机器的第二个图标」；
            // 现在紧跟行首名称之后、任何状态文本之前 ⇒ 所有类目（含无生产状态的容器/矿点行）
            // 都是「图标 → 文本」的同一顺序。
            if (!Entry.ProductIcon.IsNull())
            {
                UImage* ProdImg = WidgetTree->ConstructWidget<UImage>();
                ProdImg->SetDesiredSizeOverride(FVector2D(18.f, 18.f));
                ProdImg->SetBrushFromSoftTexture(Entry.ProductIcon, /*bMatchSize=*/false);
                if (UHorizontalBoxSlot* S = Content->AddChildToHorizontalBox(ProdImg))
                {
                    S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
                    S->SetPadding(FMargin(10.f, 0.f, 2.f, 0.f));
                    S->SetVerticalAlignment(VAlign_Center);
                }
            }

            if (Entry.ProdState != 255)
            {
                // 有生产状态：产物名 + 灯 + 状态字
                if (!Entry.StatusDetail.IsEmpty())
                {
                    UTextBlock* DetailTb = MakeText(WidgetTree, Entry.StatusDetail, 13, false,
                                                    bSelectedRow ? C_ON_SEL : C_LABEL, ETextJustify::Left);
                    DetailTb->SetAutoWrapText(false);
                    if (UHorizontalBoxSlot* S = Content->AddChildToHorizontalBox(DetailTb))
                    {
                        S->SetPadding(FMargin(10.f, 0.f, 0.f, 0.f));
                        S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
                        S->SetVerticalAlignment(VAlign_Center);
                    }
                }

                UImage* Dot = MakeStateDot(WidgetTree, WT_ProdStateColor(Entry.ProdState));
                if (UHorizontalBoxSlot* S = Content->AddChildToHorizontalBox(Dot))
                {
                    S->SetPadding(FMargin(10.f, 0.f, 4.f, 0.f));
                    S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
                    S->SetVerticalAlignment(VAlign_Center);
                }

                // 选中行底色是亮青，状态色（绿/红/琥珀）压不住 ⇒ 选中时文字改深色；
                // 灯本身保持状态色（8px 小面积，仍是颜色标识而非文字）。
                UTextBlock* StateTb = MakeText(WidgetTree, WT_ProdStateLabel(Entry.ProdState), 13, false,
                                               bSelectedRow ? C_ON_SEL : WT_ProdStateColor(Entry.ProdState),
                                               ETextJustify::Left);
                StateTb->SetAutoWrapText(false);
                if (UHorizontalBoxSlot* S = Content->AddChildToHorizontalBox(StateTb))
                {
                    S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
                    S->SetVerticalAlignment(VAlign_Center);
                }
            }
            else if (!Entry.StatusLine.IsEmpty())
            {
                // 无状态类目：整行内容原样显示（容器存货 / 矿点纯度 / 车站装卸 …）
                UTextBlock* LineTb = MakeText(WidgetTree, Entry.StatusLine, 13, false, TextColor, ETextJustify::Left);
                LineTb->SetAutoWrapText(false);
                if (UHorizontalBoxSlot* S = Content->AddChildToHorizontalBox(LineTb))
                {
                    S->SetPadding(FMargin(10.f, 0.f, 0.f, 0.f));
                    S->SetVerticalAlignment(VAlign_Center);
                }
                // ★本轮 容器行：登记实时刷新（仅固体容器有量接口，液体罐跳过）
                if (Entry.Category == static_cast<uint8>(EWT_MachineCategory::Container))
                {
                    if (AActor* CA = Entry.Target.Get())
                    {
                        if (Cast<AFGBuildableStorage>(CA))
                        {
                            FWT_LiveContainerRow& R = WT_LiveContainerRows.FindOrAdd(EntryIndex);
                            R.Text = LineTb;
                            R.Actor = CA;
                            R.LastTotal = -1;
                        }
                    }
                }
            }

            LabelBtn = AddRichButton(Content, bSelectedRow, Info, /*bEnabled=*/true);
        }
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

        // ★1.0.7i 产物/材料图标已上移到文本区（见上面 Content 内 ProdImg 段），
        // 行尾不再重复挂一个图标 —— 同一件材料出两个图标正是「不统一」的来源。

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
                            // ★2026-11-03 放宽为 AActor：便携式采矿机（非 AFGBuildable）也能打开 UI。
                            R->Server_RequestOpenUI(RowTarget);
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

    // ★本轮 资源节点类目：按里程碑/太空电梯阶段序位从上往下排（先里程碑、同里程碑内按资源名）。
    // 其他类目维持原有进入顺序（按距离由近及远由组内成员控制），不插入里程碑分组头。
    const bool bIsResourceNodeCat = SelectedCategory == static_cast<uint8>(EWT_MachineCategory::ResourceNode_Vanilla)
                                 || SelectedCategory == static_cast<uint8>(EWT_MachineCategory::ResourceNode_Mod);
    if (bIsResourceNodeCat)
    {
        Groups.Sort([](const FWTTypeGroup& A, const FWTTypeGroup& B)
        {
            if (A.MilestoneTier != B.MilestoneTier)
            {
                return A.MilestoneTier < B.MilestoneTier;
            }
            return A.Name.ToString() < B.Name.ToString();
        });
    }
    else if (SelectedSort != 0)
    {
        // ★1.0.11 排序下拉（用户方案稿统一模板的 [↑↓ 排序]）。
        // 资源节点类目保持里程碑序（上面那一支），因为它的「排序」本质是按科技阶段分组，
        // 被距离/比例排序打散后就失去了把玩家引到下一阶段矿点的意义。
        const int32 SortMode = SelectedSort;
        Groups.Sort([SortMode](const FWTTypeGroup& A, const FWTTypeGroup& B)
        {
            switch (SortMode)
            {
                case 1:   // 距离远→近
                    return A.NearestM > B.NearestM;
                case 2:   // 类型名
                    return A.Name.ToString() < B.Name.ToString();
                case 3:   // 比例低→高（不可计量 = 0，排在最低端）
                {
                    const float RA = (A.RatioN > 0) ? (A.RatioSum / A.RatioN) : 0.f;
                    const float RB = (B.RatioN > 0) ? (B.RatioSum / B.RatioN) : 0.f;
                    return RA < RB;
                }
                case 4:   // 比例高→低
                {
                    const float RA = (A.RatioN > 0) ? (A.RatioSum / A.RatioN) : 0.f;
                    const float RB = (B.RatioN > 0) ? (B.RatioSum / B.RatioN) : 0.f;
                    return RA > RB;
                }
                case 5:   // 数量多→少
                    return A.CountSum > B.CountSum;
                case 0:
                default:
                    return A.NearestM < B.NearestM;
            }
        });
    }

    int32 Rendered = 0;
    int32 LastMilestoneTier = -1;   // ★本轮 里程碑分组头去重：仅里程碑序位变化时插一行
    for (FWTTypeGroup& G : Groups)
    {
        if (Rendered >= WT_MaxRenderedRows)
        {
            break;
        }
        // ★本轮 资源节点：里程碑/太空电梯阶段分组头（标签非空且序位变化时插一行，置于该里程碑各组之上）
        if (bIsResourceNodeCat && !G.MilestoneLabel.IsEmpty() && G.MilestoneTier != LastMilestoneTier)
        {
            UTextBlock* Section = MakeText(WidgetTree, FText::FromString(G.MilestoneLabel), 14, /*bBold=*/true, C_ACCENT);
            if (UScrollBoxSlot* S = Cast<UScrollBoxSlot>(MachineListBox->AddChild(Section)))
            {
                S->SetPadding(FMargin(2.f, 5.f, 0.f, 1.f));
            }
            ++Rendered;
            LastMilestoneTier = G.MilestoneTier;
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
            // ★1.0.28h 机器分组头展开态高亮（与左侧列表同类目一致）：箭头向下▼时整行中青高亮
            // ★1.0.10 文本居左（用户反馈）：分组头是占满整行的按钮，默认 ButtonSlot 居中会让
            // 「铁矿 ×3 · 最近 120m」缩在行中间，与下面左起缩进的行错位。bFillContent=true 拉满。
            UButton* HdrBtn = MakeButton(WidgetTree, HeaderLabel, bExpanded ? C_BTN_EXP : C_BTN, &HdrLabel,
                                         ETextJustify::Left, /*FontSize=*/13, /*bFillContent=*/true);
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
            // ★1.0.28h 箭头按钮背景随展开态中青高亮，展开时箭头转亮青（与 HdrBtn 一致）
            UTextBlock* ArrowLabel = nullptr;
            UButton* ArrowBtn = MakeButton(WidgetTree, Arrow, bExpanded ? C_BTN_EXP : C_BTN, &ArrowLabel);
            if (bExpanded && ArrowLabel) { ArrowLabel->SetColorAndOpacity(FSlateColor(C_ACCENT)); }
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
                WT_LOCTEXT("MachineGroupMore", "…另有 {0} 台同型机器（已显示最近的 {1} 台）"),
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

int32 URemote_FactoryPanelWidget::CategoryCount(int32 CategoryIndex) const
{
    // ★1.0.9 电网：Entries 里一条都没有（电线杆在扫描时被聚合成电路记录），
    // 所以必须改用 Reply.Grids 的条数，否则类目按钮永远灰着、点不进去。
    if (CategoryIndex == static_cast<int32>(EWT_MachineCategory::PowerGrid))
    {
        return CurrentList.Grids.Num();
    }
    return CurrentList.CategoryCounts.IsValidIndex(CategoryIndex)
        ? CurrentList.CategoryCounts[CategoryIndex]
        : 0;
}

int32 URemote_FactoryPanelWidget::CategoryIssueCount(int32 CategoryIndex) const
{
    // 电网没有单建筑问题可言（条目是电路聚合），与 CategoryCount 同一支兜底。
    if (CategoryIndex == static_cast<int32>(EWT_MachineCategory::PowerGrid))
    {
        return 0;
    }
    int32 N = 0;
    for (const FWT_MachineEntry& E : CurrentList.Entries)
    {
        if (E.Category == static_cast<uint8>(CategoryIndex) && E.IssueFlags != 0)
        {
            ++N;
        }
    }
    return N;
}

// ----------------------------------------------------------------------------
// ★1.0.9 【电网】类目渲染
//
// 用户定案：「新增一个电网类目，打开这个类目直接可以看到截图那块」—— 即点开类目
// 就见到曲线 + 五项图例，不需要先在列表里选一根杆。
//
// 内容是「一条电路一行」而不是「一根电线杆一行」：同一个电网上的所有杆共享同一份
// FPowerCircuitStats，逐杆列出来只会让同一组数字重复几十遍。杆只在扫描侧当聚合把手。
//
// 数据来自 FWT_MachineListReply::Grids（服务端扫描时按 UFGPowerCircuit 去重生成，
// 曲线点已降采样到 ≤120 点/条）。
// ----------------------------------------------------------------------------
void URemote_FactoryPanelWidget::RebuildGridView()
{
    WT_ZONE_COARSE(TEXT("Panel.RebuildGridView"));
    if (!MachineListBox)
    {
        return;
    }

    const int32 NumGrids = CurrentList.Grids.Num();

    // 没有电网时把图整体收起来：留一张空网格比直接不显示更像"坏了"。
    if (GridHeaderBox)
    {
        GridHeaderBox->SetVisibility(NumGrids > 0 ? ESlateVisibility::Visible
                                                  : ESlateVisibility::Collapsed);
    }

    if (NumGrids <= 0)
    {
        UTextBlock* Empty = MakeText(WidgetTree,
            WT_LOCTEXT("Grid_None", "还没有电网：铺好电线杆并接入电路后，这里会显示电力曲线。"),
            12, false, C_LABEL);
        if (UScrollBoxSlot* S = Cast<UScrollBoxSlot>(MachineListBox->AddChild(Empty)))
        {
            S->SetPadding(FMargin(0.f, 6.f));
        }
        return;
    }

    // 选中的电网：越界（换了存档/电网被拆）就回落到第一条（最近的）。
    if (!CurrentList.Grids.IsValidIndex(SelectedGridIndex))
    {
        SelectedGridIndex = 0;
    }
    const FWT_GridEntry& G = CurrentList.Grids[SelectedGridIndex];

    // ---- 曲线 ----
    // Y 轴上限定为「本组所有线的最大值」，而不是上限（Capacity）：
    // 上限常年是一条贴着顶部的直线，用它定标会把消耗/峰值压在底部看不见（原版那块图
    // 之所以看起来"都贴着底"，就是因为上限 1200 而峰值只有 147）。
    // 这里按实际数据定标，五条线各自可见；上限线被裁掉也无所谓 —— 它的数值在图例里。
    if (GridGraph)
    {
        TArray<FWTGraphSeriesRaw> Series;
        float DataMax = 0.f;
        for (const FWT_GridSeries& S : G.Series)
        {
            if (S.Points.Num() < 2)
            {
                continue;
            }
            FWTGraphSeriesRaw Raw;
            Raw.Values = S.Points;
            Raw.Color = WT_GridSeriesColor(S.Kind);
            // 消耗/峰值两条主线略粗，一眼能分出主次。
            Raw.Thickness = (S.Kind == 0 || S.Kind == 1) ? 1.8f : 1.2f;
            for (float V : S.Points)
            {
                DataMax = FMath::Max(DataMax, V);
            }
            Series.Add(MoveTemp(Raw));
        }
        // 留 8% 顶部余量，免得最高点贴着上边框。
        GridGraph->SetSeries(Series, DataMax * 1.08f);
    }

    // ---- 图例（数值右随）----
    // 五格常驻，这里只改文本；Kind 与 WT_GridSeriesColor 的顺序一一对应。
    if (GridLegendValueTexts.Num() == 5)
    {
        const float Val[5] = { G.ConsumedMW, G.PeakConsumedMW, G.ProducedMW, G.CapacityMW, G.BoostMW };
        for (int32 Kind = 0; Kind < 5; ++Kind)
        {
            if (UTextBlock* Tb = GridLegendValueTexts[Kind].Get())
            {
                Tb->SetText(FText::FromString(FString::Printf(TEXT("%.1f MW"), Val[Kind])));
            }
        }
    }

    // ---- 电路行 ----
    // 一行 = 一条电网。选中哪条，上面的图就是哪条。
    // 只有一条电网时这一行仍然显示 —— 它承载「多少根杆 / 接线几个口 / 有没有电」，
    // 是这块图唯一能说明"这条曲线属于哪"的地方。
    for (int32 i = 0; i < NumGrids; ++i)
    {
        const FWT_GridEntry& E = CurrentList.Grids[i];
        const bool bSel = (i == SelectedGridIndex);

        UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();

        // ① 名称：电网 #ID（多条时靠它区分；单条时也说明"这是哪条线"）
        UTextBlock* NameTb = MakeText(WidgetTree,
            FText::Format(WT_LOCTEXT("Grid_RowName", "电网 #{0}"), FText::AsNumber(E.CircuitId)),
            13, bSel, bSel ? C_ON_SEL : C_VALUE);
        NameTb->SetAutoWrapText(false);
        if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(NameTb))
        {
            S->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));
            S->SetVerticalAlignment(VAlign_Center);
        }

        // ② 距离
        UTextBlock* DistTb = MakeText(WidgetTree,
            FText::AsNumber(FMath::RoundToInt(E.DistanceMeters)),
            12, false, bSel ? C_ON_SEL : C_LABEL);
        DistTb->SetAutoWrapText(false);
        if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(DistTb))
        {
            S->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
            S->SetVerticalAlignment(VAlign_Center);
        }

        // ③ 规模：杆数 · 接线口
        UTextBlock* ScaleTb = MakeText(WidgetTree,
            FText::Format(WT_LOCTEXT("Grid_RowScale", "{0} 根杆 · 接线 {1}/{2}"),
                          FText::AsNumber(E.PoleCount),
                          FText::AsNumber(E.LinkUsed),
                          FText::AsNumber(E.LinkMax)),
            12, false, bSel ? C_ON_SEL : C_LABEL);
        ScaleTb->SetAutoWrapText(false);
        if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(ScaleTb))
        {
            S->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
            S->SetVerticalAlignment(VAlign_Center);
        }

        // ④ 状态：跳闸 > 无电 > 有电。颜色与文字同源（同一处 switch 决定），不会出现
        //    "绿灯写着跳闸"这种漂移。
        FLinearColor StateColor = C_GREEN;
        FText StateLabel = WT_LOCTEXT("Grid_Powered", "供电中");
        if (E.bFuseBlown)
        {
            StateColor = C_RED;
            StateLabel = WT_LOCTEXT("Grid_FuseBlown", "保险丝已跳闸");
        }
        else if (!E.bHasPower)
        {
            StateColor = C_AMBER;
            StateLabel = WT_LOCTEXT("Grid_NoPower", "无电");
        }
        UImage* Dot = WidgetTree->ConstructWidget<UImage>();
        Dot->SetBrush(*FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")));
        Dot->SetColorAndOpacity(StateColor);
        Dot->SetDesiredSizeOverride(FVector2D(8.f, 8.f));
        if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(Dot))
        {
            S->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
            S->SetVerticalAlignment(VAlign_Center);
        }
        UTextBlock* StateTb = MakeText(WidgetTree, StateLabel, 12, false,
                                       bSel ? C_ON_SEL : StateColor);
        StateTb->SetAutoWrapText(false);
        if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(StateTb))
        {
            S->SetVerticalAlignment(VAlign_Center);
        }

        // 行点击 = 切换上方曲线。复用 AddRichButton 的同一套选中语义（不另造一套行行为）。
        FWT_ButtonInfo Info;
        Info.Kind = static_cast<uint8>(EWT_ButtonKind::GridRow);
        Info.Index = i;
        UButton* RowBtn = AddRichButton(Row, bSel, Info, /*bEnabled=*/true);
        if (UScrollBoxSlot* S = Cast<UScrollBoxSlot>(MachineListBox->AddChild(RowBtn)))
        {
            S->SetPadding(FMargin(0.f, 1.f));
        }
    }

    // ---- 脚注：曲线覆盖时长 ----
    // 原版那块图没有标注横轴，但玩家看到"一条线从左到右"会自然问"这是多久"。
    if (G.HistorySeconds > 0.f)
    {
        // 「每格」= 降采样后一个采样点对应的秒数（HistorySeconds / 点数）。
        // 先取出一条线的点数再算，别在 FText::Format 的参数里塞三层嵌套三元 —— 那种写法
        // 一旦点数/历史时长为 0 就会除零，而且读代码的人没法一眼看出口径。
        const int32 PtCount = (G.Series.Num() > 0) ? G.Series[0].Points.Num() : 0;
        const int32 PerPointSec = (PtCount > 0)
            ? FMath::Max(1, FMath::RoundToInt(G.HistorySeconds / static_cast<float>(PtCount)))
            : 0;
        UTextBlock* Foot = MakeText(WidgetTree,
            FText::Format(WT_LOCTEXT("Grid_HistoryNote", "曲线覆盖近 {0} 秒（每格约 {1} 秒）· 快照，点顶部「刷新」重新采集"),
                          FText::AsNumber(FMath::RoundToInt(G.HistorySeconds)),
                          FText::AsNumber(PerPointSec)),
            11, false, C_LABEL);
        if (UScrollBoxSlot* S = Cast<UScrollBoxSlot>(MachineListBox->AddChild(Foot)))
        {
            S->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
        }
    }
}

void URemote_FactoryPanelWidget::SelectGrid(int32 GridIndex)
{
    if (!CurrentList.Grids.IsValidIndex(GridIndex) || GridIndex == SelectedGridIndex)
    {
        return;
    }
    SelectedGridIndex = GridIndex;
    RebuildMachineList();   // 电网类目下这会再进 RebuildGridView，重画曲线与选中态
    RebuildDetailPane();
}

void URemote_FactoryPanelWidget::SelectCategory(int32 CategoryIndex)
{
    // ★1.0.8 这里**不能**调 ClearSearch()：它会把「当前类目」的记词清成空，
    // 而此刻 SelectedCategory 还是旧类目 ⇒ 等于把旧类目的搜索记忆抹掉，
    // 「切走再切回来」就再也还原不出原关键词，与「每个类目各记一份」的定案相冲突。
    // 旧类目那份记忆保持原样，新类目的关键词在下面从表里还原。

    // ★1.0.11 离开旧类目前先存一份筛选快照（必须在下面改写 SelectedCategory **之前**，
    // 否则读到的已经是新类目，快照会存错桶）。
    if (SelectedCategory >= 0 && SelectedCategory != CategoryIndex)
    {
        SaveCategoryFilters(static_cast<uint8>(SelectedCategory));
    }

    SelectedCategory = CategoryIndex;
    SelectedCollectibleKind = -1; // ★0.2.62: machine selection clears alien-energy selection
    SelectedCollectiblePicked = -1; // ★0.2.64: and its picked-state filter too
    // ★1.0.9 电网选中项随类目切换复位：否则从「电网 #3」切到别的类目再切回来，
    // 会指向一条早已被拆掉/换了序号的电路（RebuildGridView 里虽有越界兜底，
    // 但这里显式归零更符合「切类目 = 重新开始看」的预期）。
    SelectedGridIndex = 0;

    // ★1.0.8 所有筛选维度随类目复位：上类目选了「只显示阻塞」，切到容器后不能还留着这个条件。
    // ★1.0.11 但对**已经访问过的**类目不再复位：离开时存快照，回来时还原
    //（用户方案稿「切换类目保持子列表筛选状态」）。没访问过的类目仍走全清。
    ResetCategoryFilters();
    LoadCategoryFilters(static_cast<uint8>(CategoryIndex));

    // ★1.0.8 还原**新类目**自己的搜索词：每个子类目各记一份，互不干扰。
    {
        const FString* Saved = CategorySearchTexts.Find(static_cast<uint8>(CategoryIndex));
        SearchText = Saved ? *Saved : FString();
        bSearchActive = !SearchText.IsEmpty();
        if (SearchBox)
        {
            // SetText 不触发 OnTextChanged，不会反过来污染 CategorySearchTexts
            SearchBox->SetText(FText::FromString(SearchText));
        }
    }

    SelectedEntry = -1;
    RebuildCategoryList();
    RebuildMachineList();
    RebuildDetailPane();
}

// ★0.2.62 ------------------------------------------------------------------
void URemote_FactoryPanelWidget::ToggleTopCategory(int32 TopIndex)
{
    ClearSearch();   // ★本轮 点顶层组即退出搜索模式
    if (TopIndex >= 0 && TopIndex < 3)   // ★本轮 顶层 3 组：资源节点(0) / 建筑(1) / 收集品(2)
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
    ClearSearch();   // ★本轮 点外星科技种类即退出搜索模式
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

/**
 * ★1.0.8AN 收集品「拾取」过滤（下拉，与后面的【距离】【排序】同款）。
 * 直接赋值（不再是旧按钮版的「再点一次回到全部」翻转语义）；-1 全部 / 0 未拾取 / 1 已拾取。
 */
void URemote_FactoryPanelWidget::SetCollectiblePickedFilter(int32 Picked)
{
    // 只出现在收集品类目的筛选条上；Kind < 0 时属于异常路径，直接不动状态。
    if (SelectedCollectibleKind < 0)
    {
        return;
    }
    const int32 Want = (Picked == 0 || Picked == 1) ? Picked : -1;
    // ★1.0.8AN：改成「直接设置」。旧版是给两个互斥按钮写的「再点一次回到全部」，
    //   现在入口换成下拉（选项本身就含「全部」），再保留翻转语义会出现
    //   「选了未拾取、再选一次未拾取却跳到全部」的怪行为。
    if (SelectedCollectiblePicked == Want)
    {
        return;
    }
    SelectedCollectiblePicked = Want;
    SelectedCollectible = -1;
    // 关键：此刻正处在原下拉自己的 OnSelectionChanged 派发栈里，直接重建筛选条等于在自己的
    // 回调里删掉自己（与其它维度同一个坑）⇒ 走 ApplyFilterChange 的「列表立即刷、筛选条延一帧」通道。
    ApplyFilterChange();
}

int32 URemote_FactoryPanelWidget::TopCount(int32 TopIndex) const
{
    // ★本轮 顶层由 2 组变 3 组：资源节点(0) / 建筑(1) / 收集品(2)。资源节点提成【建筑】上方的独立顶层类目。
    if (TopIndex == 0) // 资源节点（建筑上方独立顶层类目）：原版 + MOD 之和
    {
        static const EWT_MachineCategory ResourceNodeCats[] = {
            EWT_MachineCategory::ResourceNode_Vanilla,   // 原版矿点
            EWT_MachineCategory::ResourceNode_Mod,       // 模组矿点
        };
        int32 Sum = 0;
        for (EWT_MachineCategory c : ResourceNodeCats)
        {
            const int32 ci = static_cast<int32>(c);
            Sum += CategoryCount(ci);   // ★1.0.9 统一口径（电网条目不在 CategoryCounts 里）
        }
        return Sum;
    }
    if (TopIndex == 1) // 建筑：...（★0.2.153p 含液体/固体容器；不含 DropPod / Other / 资源节点）
    {
        // ★0.2.153r 与上面 RenderCategoryBar 的数组必须**逐项一致**：两处不一致会导致「类目里显示 3 条、顶部却显示 5」。
        // ★本轮 枢纽组（6 个枢纽建筑 + 回收站）已挪到顶部图标栏，两份数组同步移除 Hub。
        static const EWT_MachineCategory BuildingCats[] = {
            EWT_MachineCategory::Miner, EWT_MachineCategory::Producer,   // ★本轮 互换
            EWT_MachineCategory::Container,                              // ★本轮 合并
            EWT_MachineCategory::Generator,
            EWT_MachineCategory::PowerGrid,   // ★1.0.9 同步上方（两处数组必须逐项一致）
            // ★1.0.8 同步上方 RenderCategoryBar 移除 PowerSwitch（两处数组必须逐项一致）
            EWT_MachineCategory::Vehicle, EWT_MachineCategory::Portal, EWT_MachineCategory::Modded,
            // ★本轮 资源节点已提成顶层独立类目，建筑组不再含 ResourceNode
        };
        int32 Sum = 0;
        for (EWT_MachineCategory c : BuildingCats)
        {
            const int32 ci = static_cast<int32>(c);
            Sum += CategoryCount(ci);   // ★1.0.9 统一口径（电网条目不在 CategoryCounts 里）
        }
        return Sum;
    }
    // TopIndex == 2: 收集品（9 类 collectible 之和：6 类外星能量 + 3 类可采集果实）。文件已加载且有内容时以 collectibles.json 为准，
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
        // ★0.2.153z 无收集品地图（如 Platform Expansion Program）无 wiki/文件数据，且实时扫描 0 个收集品 ⇒
        // 用真实扫描结果作为顶行计数，让「收集品 (0)」与下方「本图无收集品 · 传送不可用」一致。
        Sum = CurrentList.Collectibles.Num();
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

    // 打开面板按钮仅对"有交互 UI"的条目显示（死亡箱/矿点等无 UI，点了也无反应）；
    // 死亡箱改用"取料"按钮经 Server_PullOutput 取回物品。
    if (OpenMachineButton)
    {
        OpenMachineButton->SetVisibility(Entry->bHasInteractUI
            ? ESlateVisibility::Visible
            : ESlateVisibility::Collapsed);
    }
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

void URemote_FactoryPanelWidget::MergeResourceNodesIntoList()
{
    // ★本轮 从 SmartFactoryLib/resourcenodes.json 读取全图资源节点，并入 CurrentList（资源节点独立顶层类目，位于建筑上方）。
    // 与收集品同源：数据在 Lib，不在实时扫描。资源节点不是建筑 ⇒ Target 恒 null，传送走坐标（Server_TeleportToCoord）。
    // 模组矿点只要派生自 AFGResourceNode 即归入 ResourceNode_Mod（DumpResourceNodes 已按包路径标 bIsMod），绝不进 MOD建筑。
    TArray<FWT_ResourceNodeSpawn> Nodes;
    bResourceNodeFileLoaded = FWTResourceNodes::LoadSpawns(Nodes, &ResourceNodePurityAudit);   // ★1.0.24 记录文件是否可解析（供类目空数据提示区分「未生成/为空」）；★2026-10-25 同读纯度审计
    if (!bResourceNodeFileLoaded || Nodes.Num() == 0)
    {
        return;
    }
    // ★1.0.28 自定义/skyblock 地图门禁：当前世界非原版时，resourcenodes.json 里的原版矿点
    // 实体不存在 ⇒ 隐藏，只保留 MOD 矿点（用户在 skyblock 里放置的真实矿点）。
    const bool bIsVanillaMap = WT_IsVanillaMap(GetWorld());
    // 距离参考：本地玩家坐标（资源节点是全图，按到玩家距离排序比到终端更直观）。
    FVector RefLoc = FVector::ZeroVector;
    if (const APawn* P = GetOwningPlayerPawn())
    {
        RefLoc = P->GetActorLocation();
    }
    // ★1.0.28 陈旧库条目过滤（用户实测：「拆除/放置后【资源节点】类目下的 MOD 节点
    // 并未同步更新」）。静态库是全图快照，拆除的库条目会被每次合并重新加回列表。
    // 实时扫描回报的 LiveNodeKeys = 世界中实际存在的矿点键集合（服务端记账）：
    // 玩家附近（StaleRadiusM 内，该半径必然处于 World Partition 流送范围内，不存在
    // 「实体存在但未驻留」）的库条目若不在集合中 ⇒ 已拆除，剔除。更远的库条目无法
    // 验证存在性（流送外），保持展示——全图浏览功能不受影响。
    // 瞬态说明：玩家刚传送落地、流送未成型时附近条目可能被误判一轮，15s 后下次扫描
    // 自动恢复；LiveNodeKeys 为空（世界里一颗矿点都没驻留）时不过滤，防止误清空。
    TSet<FString> LiveKeys;
    LiveKeys.Reserve(CurrentList.LiveNodeKeys.Num());
    for (const FString& K : CurrentList.LiveNodeKeys)
    {
        LiveKeys.Add(K);
    }
    const float StaleRadiusM = 250.f;
    for (const FWT_ResourceNodeSpawn& N : Nodes)
    {
        if (!N.bIsMod && !bIsVanillaMap)
        {
            continue;   // ★1.0.28 自定义/skyblock 门禁：原版矿点数据来自 resourcenodes.json（原版地图全图），
                        // 自定义地图实体不存在 ⇒ 不进列表，只保留 MOD 矿点。
        }
        FWT_ResourceNodeSpawn KeySpawn;
        KeySpawn.ResourceName = N.ResourceName;
        KeySpawn.Location = N.Location;
        const FString NodeKeyStr = FWTResourceNodes::NodeKey(KeySpawn);
        const float NodeDistM = FVector::Dist(RefLoc, N.Location) / 100.f;
        if (LiveKeys.Num() > 0 && NodeDistM <= StaleRadiusM && !LiveKeys.Contains(NodeKeyStr))
        {
            continue;   // 附近且世界中已不存在 ⇒ 已拆除，不进列表
        }
        FWT_MachineEntry E;
        E.Target = nullptr;                                  // 非建筑，无 actor 可引用 ⇒ 坐标传送
        E.DisplayName = FText::FromString(N.ResourceName);   // 如「铁矿石」
        E.DistanceMeters = FVector::Dist(RefLoc, N.Location) / 100.f;
        E.Location = N.Location;
        E.BaseRadius = 200.f;                                // 矿点小，落点半径取 2m
        E.bHasInteractUI = false;                            // 矿点无交互面板 ⇒ 只给[传送]
        // ★本轮 资源节点提成独立顶层类目，内部按原版/MOD 拆两个子目。
        E.Category = static_cast<uint8>(N.bIsMod ? EWT_MachineCategory::ResourceNode_Mod
                                                 : EWT_MachineCategory::ResourceNode_Vanilla);
        E.StatusLine = FText::FromString(N.Purity);           // 如「纯 / 普通 / 贫瘠」
        // ★2026-10-25 纯度筛选键（与 StatusLine 同源）：面板资源节点类目按纯度过滤用。
        if (N.Purity == TEXT("贫瘠"))      { E.PurityFilter = 0; }
        else if (N.Purity == TEXT("普通")) { E.PurityFilter = 1; }
        else if (N.Purity == TEXT("纯"))   { E.PurityFilter = 2; }
        else                               { E.PurityFilter = -1; }
        // ★本轮 里程碑排序序位 + 标签（资源节点按里程碑/太空电梯阶段从上往下排）
        E.MilestoneTier = N.MilestoneTier;
        E.MilestoneLabel = N.MilestoneLabel;
        // 图标 best-effort：用 dump 时记录的描述符路径解析（mod 资源描述符未加载则解析失败，留空）。
        if (!N.ResourceClassPath.IsEmpty())
        {
            // ResourceClassPath 是资源描述符类路径 ⇒ 解析为 UClass 传给 GetBigIcon(TSubclassOf)。
            if (UClass* Cls = FindObject<UClass>(nullptr, *N.ResourceClassPath))
            {
                if (UTexture2D* T = UFGItemDescriptor::GetBigIcon(Cls))
                {
                    E.ProductIcon = TSoftObjectPtr<UTexture2D>(T);
                }
            }
        }
        CurrentList.Entries.Add(E);
        const int32 ci = static_cast<int32>(E.Category);
        if (CurrentList.CategoryCounts.IsValidIndex(ci))
        {
            CurrentList.CategoryCounts[ci]++;
        }
    }
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
