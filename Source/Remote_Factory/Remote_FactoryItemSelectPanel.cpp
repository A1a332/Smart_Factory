#include "Remote_FactoryItemSelectPanel.h"

#include "Remote_FactoryLoc.h"
#include "Remote_FactoryLog.h"
#include "Remote_FactoryUpgrades.h"          // AWTUpgradeCloudDownloader / AWTUpgradeStorage 完整类型
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"             // ★1.0.8BE 内容槽 padding 下沉（UBorder::GetContentSlot）
#include "Components/Button.h"
#include "Components/ButtonSlot.h"           // ★1.0.7f：UButton 的内容槽是 UButtonSlot（居中对齐可调）
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ComboBoxString.h"         // ★1.0.7k 阶段过滤下拉
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"          // ★1.0.7k 搜索框下方的过滤行
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/SizeBox.h"               // ★1.0.7l 过滤行固定高度（防下拉 desired size 塌成 0）
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"                     // TActorIterator
#include "FGCentralStorageSubsystem.h"
#include "FGInventoryComponent.h"               // ★1.0.7g 本站缓冲实到数：GetNumItems()（FGInventoryComponent.h:415）
#include "FGInventoryLibrary.h"                 // ★1.0.8BB 批量取货：MoveItemFromCentralStorage / GrabItemFromCentralStorage（FGInventoryLibrary.h:192/202）
#include "FGCharacterPlayer.h"                  // ★1.0.8BB 玩家背包：AFGCharacterPlayer::GetInventory()（FGCharacterPlayer.h:722，FORCEINLINE 非虚 ⇒ 可安全直调）
#include "FGPlayerController.h"                 // AFGPlayerController（面板已有用法；此处保证 Cast 可见）
#include "Buildables/FGBuildableStorage.h"      // ★1.0.8BD 固体上传站"正在输入的物品"：GetStorageInventory()（FGBuildableStorage.h:30）
#include "Buildables/FGBuildablePipeReservoir.h"// ★1.0.8BD 液体上传站"正在输入的流体"：GetFluidDescriptor()（FGBuildablePipeReservoir.h:87）      // ★1.0.8BD 固体上传站"正在输入的物品"：GetStorageInventory()（FGBuildableStorage.h:30）
// ★1.0.7d 目录口径改成「生产栏建筑能产出的物品」所需的三件套：
#include "FGRecipe.h"                          // UFGRecipe::GetProducts / GetProducedIn（FGRecipe.h:63/83）
#include "FGRecipeManager.h"                   // AFGRecipeManager::GetAllRecipes()（FGRecipeManager.h:77）
#include "FGSchematic.h"                       // UFGSchematic::GetType/GetTechTier/GetUnlocks + ESchematicType
#include "FGSchematicManager.h"                // AFGSchematicManager::IsSchematicPurchased（FGSchematicManager.h:223）
#include "Unlocks/FGUnlockRecipe.h"            // UFGUnlockRecipe::GetRecipesToUnlock（FGUnlockRecipe.h:33）
#include "Buildables/FGBuildableFactory.h"     // 「生产」栏全部建筑的共同基类（FGBuildableFactory.h:27）
#include "Resources/FGBuildingDescriptor.h"  // 排除建筑描述符支系
#include "Resources/FGItemDescriptor.h"
#include "Resources/FGResourceDescriptor.h"    // ★1.0.7i 原矿条目（Desc_OreIron_C 等的基类）
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"              // ★1.0.7f FEditableTextBoxStyle（搜索框配色）
#include "UObject/UObjectIterator.h"         // TObjectIterator（目录构建）
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"           // ★1.0.7k UWTItemFilterComboBox::HandleGenerateWidget
#include "Widgets/Layout/Anchors.h"
#include "Widgets/SWidget.h"
#include "GameFramework/PlayerController.h"   // ★1.0.7 FInputModeUIOnly/GameOnly + SetInputMode
#include "Engine/GameViewportClient.h"         // ★1.0.7 EMouseLockMode
#include "FGPlayerController.h"                // ★1.0.7b 诊断：GetGameUI()
#include "UI/FGGameUI.h"                       // ★1.0.7b 诊断：HasActiveInteractWidget()
#include "TimerManager.h"                      // ★1.0.8BH 延迟开背包：World->GetTimerManager().SetTimer（FTimerManager 完整类型）

// 与 Remote_FactoryPanelWidget 同一套 DSP 风（各自文件内 static，
// 不会与另外两个文件的同名常量冲突）。
static const FLinearColor ISEL_BG      = FLinearColor(0.043f, 0.055f, 0.078f, 0.97f);
static const FLinearColor ISEL_ACCENT  = FLinearColor(0.290f, 0.780f, 0.900f, 1.f);
static const FLinearColor ISEL_VALUE   = FLinearColor(0.870f, 0.940f, 0.970f, 1.f);
static const FLinearColor ISEL_DIM     = FLinearColor(0.480f, 0.560f, 0.630f, 1.f);
static const FLinearColor ISEL_GREEN   = FLinearColor(0.380f, 0.900f, 0.640f, 1.f);
static const FLinearColor ISEL_BTN     = FLinearColor(0.095f, 0.125f, 0.165f, 1.f);
static const FLinearColor ISEL_BTN_SEL = FLinearColor(0.290f, 0.780f, 0.900f, 0.90f);
static const FLinearColor ISEL_ON_SEL  = FLinearColor(0.035f, 0.060f, 0.080f, 1.f);
// ★1.0.8 行尾按钮的「第二态」——选中行上的【取消】。刻意用暗红而不是"置灰"：
//   ① 置灰在 UMG 里读作"不可点"，而这颗恰恰是全行唯一可点的地方；
//   ② 选中行整行底色是亮青（ISEL_BTN_SEL），同色的按钮会融进行里看不见，
//      必须换成对比色才能让玩家一眼看到「再点一下就取消」。
//   暗红 + 近白色的字，在亮青行底上是整行唯一的暖色块，位置固定在行尾，不跳位。
static const FLinearColor ISEL_BTN_CANCEL = FLinearColor(0.560f, 0.190f, 0.180f, 0.96f);
// ★1.0.8BB 批量取货按钮：正向动作（把货拿进背包），用冷绿。
//   四颗按钮的颜色分工：【取到背包】冷绿=拿走 / 【取消选择】中性蓝=清筛选 / 【关闭】选中青=退出 /
//   行尾【取消】暗红=解除该物品。任意两颗都不同色，且与行底亮青都不撞。
static const FLinearColor ISEL_BTN_TAKE   = FLinearColor(0.106f, 0.353f, 0.243f, 0.96f);
static const FLinearColor ISEL_ON_CANCEL  = FLinearColor(1.000f, 0.900f, 0.880f, 1.f);
// ★1.0.8BI 云端净变化列的颜色：**涨红 / 跌绿 / 平灰**（用户环境的通用读数习惯）。
//   刻意与速度列的「在搬=绿」区分开：那一列由 `↑`/`↓` 箭头 + 「/分」表达"本机方向"，
//   这一列由 `净+`/`净−` 前缀表达"池子方向"，两者同屏也不会被读成同一件事。
static const FLinearColor ISEL_RISE      = FLinearColor(0.980f, 0.420f, 0.380f, 1.f);
// 注：行尾按钮两种身份（【选择】/【取消】）**都可用**。保留 ISEL_DIM 供数量为 0 的行使用。

// ★1.0.7f 列宽（像素）。写成常量而不是散在调用点，是因为「所有行对齐」正是靠这几列宽度一致。
// ★1.0.7y 用户 2026-10-27 定案（截图红框）：「数量往前挪，这个位置放【正在下载/未下载】」
//   ⇒ 数量列不再右对齐吃 96px，改成紧贴物品名的自然宽；原位置（红框）留给状态列。
static const float ISEL_COL_STATE   = 96.f;   // 状态列（正在下载 / 未下载），沿用原数量列所占宽度
static const float ISEL_COL_RATE    = 78.f;   // ★1.0.7p 行内下载速度列（「1,200/分」最宽约 70px）
static const float ISEL_COL_ACTION  = 64.f;   // 行尾唯一操作按钮（选择 / 取消 同一位置）
static const float ISEL_COL_NET     = 74.f;   // ★1.0.8BI 云端净变化（「净+1,200/分」最宽约 68px）

// ★1.0.7k 阶段过滤下拉的四色（与 Remote_FactoryPanelWidget 的 C_FLT_* 同值，保持两块面板观感一致）。
static const FLinearColor ISEL_FLT_BG    = FLinearColor(0.098f, 0.135f, 0.180f, 1.f);
static const FLinearColor ISEL_FLT_HOVER = FLinearColor(0.160f, 0.255f, 0.320f, 1.f);
static const FLinearColor ISEL_FLT_ROW   = FLinearColor(0.070f, 0.094f, 0.125f, 1.f);

// ★1.0.8BE 与终端面板（Remote_FactoryPanelWidget）对齐：终端面板每块区域之间有一条
//   青色细线（C_DIV = 0.290/0.780/0.900 @0.45），云端面板此前没有，两块面板放一起能一眼看出
//   「不是一个东西」。用户 2026-10-28：「云端UI要与终端UI一致」。取值与 C_DIV 逐字相同。
static const FLinearColor ISEL_DIV        = FLinearColor(0.290f, 0.780f, 0.900f, 0.45f);

/**
 * ★1.0.7k 纯色画刷。默认构造的 FSlateBrush 没有资源对象，画出来是空的
 * （本项目踩过多次）—— 必须拿引擎自带 WhiteBrush 再染色。
 */
static FSlateBrush ISEL_SolidBrush(const FLinearColor& Tint)
{
    FSlateBrush B;
    if (const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
    {
        B = *White;
    }
    B.DrawAs = ESlateBrushDrawType::Box;
    B.TintColor = FSlateColor(Tint);
    B.SetImageSize(FVector2D(4.f, 4.f));
    return B;
}

/**
 * ★1.0.8BJ 「值真的变了才写」的两个助手 —— 专治用户 2026-10-28 报的「下拉选择框有点抖动」。
 *
 * ## 病根
 * 行刷新原来是**无条件** SetText / SetColorAndOpacity：152 行 × 5 个文本块，每 0.5s 写一遍，
 * 哪怕内容一个字节没变。`UTextBlock::SetText` 会让该控件的布局失效（invalidate + 重新量测），
 * 于是整块面板每半秒重排一次 —— 下拉菜单开着的时候，主窗口重排会让菜单与按钮的相对位置
 * 一缩一胀，肉眼就是「下拉框在抖」。
 *
 * ## 为什么不是"少刷几次"
 * 刷得少 = 数字看着不实时（用户同时在报的另一件事）。正确做法是**保持刷新频率、把无效写入去掉**：
 * 数据每 0.3s 读一次，只有值真的变了才碰 Slate。两个诉求因此不冲突。
 *
 * 比较用 `FText::EqualTo`（同一个字符串表内的逐段比较，不做本地化格式化），成本远低于一次重排。
 */
static void ISEL_SetTextIfChanged(UTextBlock* Block, const FText& NewText)
{
    if (Block && !Block->GetText().EqualTo(NewText))
    {
        Block->SetText(NewText);
    }
}

static void ISEL_SetColorIfChanged(UTextBlock* Block, const FLinearColor& NewColor)
{
    if (Block && !Block->GetColorAndOpacity().GetSpecifiedColor().Equals(NewColor))
    {
        Block->SetColorAndOpacity(FSlateColor(NewColor));
    }
}

/**
 * ★1.0.7k 给下拉套深色样式。**必须在 TakeWidget() 之前调**（WidgetStyle/ItemStyle 只在
 * RebuildWidget 里一次性交给 SComboBox，Slate 控件建好后再改不生效）。
 * 细节与 Remote_FactoryPanelWidget::WT_StyleFilterCombo 同款，注释见那边。
 */
static void ISEL_StyleCombo(UComboBoxString* Combo)
{
    if (!Combo)
    {
        return;
    }

    FButtonStyle Button;
    Button.SetNormal(ISEL_SolidBrush(ISEL_FLT_BG));
    Button.SetHovered(ISEL_SolidBrush(ISEL_FLT_HOVER));
    Button.SetPressed(ISEL_SolidBrush(ISEL_FLT_HOVER));
    Button.SetDisabled(ISEL_SolidBrush(ISEL_FLT_BG));
    Button.SetNormalPadding(FMargin(2.f, 2.f));
    Button.SetPressedPadding(FMargin(2.f, 3.f));

    FComboBoxStyle Style = Combo->GetWidgetStyle();
    FComboButtonStyle ComboBtn = Style.ComboButtonStyle;
    ComboBtn.SetButtonStyle(Button);
    ComboBtn.SetMenuBorderBrush(ISEL_SolidBrush(ISEL_FLT_BG));
    ComboBtn.SetMenuBorderPadding(FMargin(1.f));
    ComboBtn.SetContentPadding(FMargin(6.f, 2.f));
    ComboBtn.DownArrowImage.TintColor = FSlateColor(ISEL_ACCENT);   // 深底上的箭头必须提亮
    Style.SetComboButtonStyle(ComboBtn);
    Combo->SetWidgetStyle(Style);

    FTableRowStyle Rows = Combo->GetItemStyle();
    Rows.SetEvenRowBackgroundBrush(ISEL_SolidBrush(ISEL_FLT_ROW));
    Rows.SetEvenRowBackgroundHoveredBrush(ISEL_SolidBrush(ISEL_FLT_HOVER));
    Rows.SetOddRowBackgroundBrush(ISEL_SolidBrush(ISEL_FLT_ROW));
    Rows.SetOddRowBackgroundHoveredBrush(ISEL_SolidBrush(ISEL_FLT_HOVER));
    Rows.SetActiveBrush(ISEL_SolidBrush(ISEL_FLT_HOVER));
    Rows.SetActiveHoveredBrush(ISEL_SolidBrush(ISEL_FLT_HOVER));
    Rows.SetInactiveBrush(ISEL_SolidBrush(ISEL_FLT_ROW));
    Rows.SetInactiveHoveredBrush(ISEL_SolidBrush(ISEL_FLT_HOVER));
    Rows.SetTextColor(FSlateColor(ISEL_VALUE));
    Rows.SetSelectedTextColor(FSlateColor(ISEL_ACCENT));
    Combo->SetItemStyle(Rows);

    if (UWTItemFilterComboBox* Self = Cast<UWTItemFilterComboBox>(Combo))
    {
        Self->WT_ItemColor = FSlateColor(ISEL_VALUE);
        Self->WT_FontSize  = 12.f;
    }
    Combo->SetContentPadding(FMargin(6.f, 2.f));
    Combo->SetMaxListHeight(420.f);
}

/**
 * ★1.0.7k 原始资源描述符 → 里程碑阶段。
 *
 * 为什么需要：原矿没有配方，`GetItemTier` 的配方路径对它无效；而「里程碑顺序排列」不能
 * 把铁矿石和铀矿混在一起排。
 *
 * 口径来源：直接沿用 `Remote_FactoryResourceNodes.cpp` 的 `WT_LookupNodeTier`（该函数在
 * 匿名命名空间里 ⇒ 跨文件不可用，只能复刻同一张表）。**两边改一处必须同步改另一处**，
 * 依据同为 Wiki 的 Milestones / Space Elevator 阶段解锁关系。
 */
static int32 ISEL_ResourceTierByClassName(const FString& ShortName)
{
    if (ShortName == TEXT("Desc_Stone_C") || ShortName == TEXT("Desc_OreIron_C") ||
        ShortName == TEXT("Desc_OreCopper_C"))
    {
        return 0;   // Tier 0 · 起步资源
    }
    if (ShortName == TEXT("Desc_Coal_C") || ShortName == TEXT("Desc_OreGold_C"))
    {
        return 3;   // Tier 3 · 燃煤发电
    }
    if (ShortName == TEXT("Desc_LiquidOil_C"))
    {
        return 4;   // Tier 4 · 石油加工（液体，固体目录里通常不会出现，留作口径完整）
    }
    if (ShortName == TEXT("Desc_Sulfur_C"))
    {
        return 5;
    }
    if (ShortName == TEXT("Desc_SAM_C"))
    {
        return 6;
    }
    if (ShortName == TEXT("Desc_OreBauxite_C") || ShortName == TEXT("Desc_RawQuartz_C"))
    {
        return 7;
    }
    if (ShortName == TEXT("Desc_OreUranium_C"))
    {
        return 8;
    }
    return 99;      // 未识别（MOD 资源等）⇒ 沉底
}

TSharedRef<SWidget> UWTItemFilterComboBox::HandleGenerateWidget(TSharedPtr<FString> Item) const
{
    // 显式上色：原版生成器只设 Font，颜色只来自构造期捕获的 ForegroundColor，
    // 而原版 UMG 皮肤是「浅灰底 + 深字」⇒ 压在本面板深蓝底上完全看不清。
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
 * ★1.0.7f 建文本块。**强制给对齐方式**（用户 2026-10-26 定案「以后做UI，文本左侧/右侧对齐」）——
 * UTextBlock 默认是 Center，不显式指定就整列居中，纵向扫读时参差不齐。
 */
static UTextBlock* ISEL_MakeText(UWidgetTree* Tree, const FText& InText, int32 FontSize,
                                 bool bBold, FLinearColor Color,
                                 ETextJustify::Type Justify = ETextJustify::Left)
{
    UTextBlock* T = Tree->ConstructWidget<UTextBlock>();
    T->SetText(InText);
    T->SetFont(FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), FontSize));
    T->SetColorAndOpacity(FSlateColor(Color));
    T->SetAutoWrapText(false);
    T->SetJustification(Justify);
    return T;
}

/**
 * ★1.0.7f 建按钮，并把内容槽设成 HAlign_Fill。
 * UButton 的内容默认居中（UButtonSlot::HorizontalAlignment=Center），文字长度不同的按钮并排时
 * 标签位置会左右漂；填满后由文本自身的 Justification 决定左右边界，视觉才整齐。
 */
/**
 * ★1.0.8BE 分隔线 —— 与终端面板 `MakeDivider`（Remote_FactoryPanelWidget.cpp:259）**逐字同构**：
 * 显式取引擎 WhiteBrush（默认构造的 FSlateBrush 没有资源，画出来是空的，本工程踩过多次），
 * 再染成 ISEL_DIV，纵向 padding 3 给出线厚。
 */
static UBorder* ISEL_MakeDivider(UWidgetTree* Tree)
{
    UBorder* D = Tree->ConstructWidget<UBorder>();
    D->SetBrush(*FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")));
    D->SetBrushColor(ISEL_DIV);
    D->SetPadding(FMargin(0.f, 3.f));
    return D;
}

static UButton* ISEL_MakeButton(UWidgetTree* Tree, const FText& InText, FLinearColor BgColor,
                                UTextBlock** OutLabel = nullptr,
                                ETextJustify::Type Justify = ETextJustify::Center,
                                int32 FontSize = 13)
{
    UButton* B = Tree->ConstructWidget<UButton>();
    B->SetBackgroundColor(BgColor);
    UTextBlock* T = ISEL_MakeText(Tree, InText, FontSize, false, ISEL_VALUE, Justify);
    if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(B->AddChild(T)))
    {
        ButtonSlot->SetHorizontalAlignment(HAlign_Fill);
        ButtonSlot->SetVerticalAlignment(VAlign_Center);
        ButtonSlot->SetPadding(FMargin(4.f, 0.f));
    }
    if (OutLabel)
    {
        *OutLabel = T;
    }
    return B;
}

// ----------------------------------------------------------------------------

UWTItemSelectPanelWidget::UWTItemSelectPanelWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    // ★1.0.7 修复「面板打开吞鼠标」根因（对齐终端面板 v0.2.53 定案）：
    // 原版交互面板靠 mUseMouse/mUseKeyboard=true 让游戏在面板激活时屏蔽键鼠、出栈时还原；
    // 本模组不在 GameUI 栈上注册，若二者为 true 会「屏蔽被施加却无人还原 → 永久卡死/吞鼠标」。
    // 故显式关闭游戏自动屏蔽，改由本类在 SetInputMode / OnCloseClicked / NativeDestruct 对称接管。
    mUseKeyboard = false;
    mUseMouse    = false;
    mMouseLockMode = EMouseLockMode::DoNotLock;
    mSupportsStacking = true;
    mDesiredHorizontalAlignment = HAlign_Center;
    mDesiredVerticalAlignment   = VAlign_Center;
}

TSharedRef<SWidget> UWTItemSelectPanelWidget::RebuildWidget()
{
    RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>();

    RootBorder = WidgetTree->ConstructWidget<UBorder>();
    RootBorder->SetBrush(*FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")));
    RootBorder->SetBrushColor(ISEL_BG);
    // ★1.0.8BE 与终端面板对齐：border 只做"不透明底色 + 裁剪外壳"，内边距下沉到内容槽
    //   （终端是 RootBorder(padding 0) + 内容槽 16，见 Remote_FactoryPanelWidget.cpp:614/935）。
    //   两者视觉结果相同，但结构一致之后，将来给面板加背景贴图/描边会同时生效。
    RootBorder->SetPadding(FMargin(0.f));
    RootBorder->SetClipping(EWidgetClipping::ClipToBounds);

    if (UCanvasPanelSlot* CanvasSlot = RootCanvas->AddChildToCanvas(RootBorder))
    {
        CanvasSlot->SetAnchors(FAnchors(0.5f, 0.5f));
        CanvasSlot->SetAlignment(FVector2D(0.5f, 0.5f));
        // ★1.0.8BE 设计尺寸对齐终端面板（1180x760）。真实尺寸在 NativeConstruct 里
        //   按视口再收一遍（同一套公式：视口 90% 宽 / 85% 高，上限 1280x820）。
        CanvasSlot->SetSize(FVector2D(1180.f, 760.f));
    }

    UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>();

    // ---- title row -----------------------------------------------------------
    UHorizontalBox* TitleRow = WidgetTree->ConstructWidget<UHorizontalBox>();
    // ★1.0.8BE 字号 18 → 22：与终端面板标题同号（Remote_FactoryPanelWidget.cpp:634 用的是 22）。
    TitleText = ISEL_MakeText(WidgetTree,
        WT_LOCTEXT("ItemPanel_Title", "云端存储下载站 · 输出物品"),
        22, true, ISEL_ACCENT, ETextJustify::Left);
    if (UHorizontalBoxSlot* S = TitleRow->AddChildToHorizontalBox(TitleText))
    {
        S->SetPadding(FMargin(2.f, 0.f, 12.f, 0.f));
        S->SetVerticalAlignment(VAlign_Center);
        S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }

    // ★1.0.8BG 标题行的批量按钮【取走列表全部】/【清空列表全部】**已删除**。
    //   用户 2026-10-28 定案：「不要选择清空全部，选择单一的」「每一行不是一个物品吗，
    //   在后面增加一个清空/转移等等」——批量按钮的作用范围天生比玩家看到的大（上传站只显示
    //   1 种却会清掉云端另外几种，液体那更是不可逆），所以整颗去掉，动作全部下沉到各行行尾。
    //   行尾按钮的构建见 RebuildRows 的「列⑦ / 列⑧」；逻辑见 OnRowTakeClicked。

    UTextBlock* ClearLabel = nullptr;
    // ★1.0.8BB 文案「清空」→「取消选择」：原来那个"清空"只清**筛选选择**、一件货都不动，
    //   和旁边新加的"取到背包"放一起，玩家一定会以为它清的是云端存货（那正是本轮要做的另一件事）。
    //   改名后两个按钮各说各话：一个清选择、一个取货。
    UButton* ClearButton = ISEL_MakeButton(WidgetTree,
        WT_LOCTEXT("Btn_ClearSelection", "取消选择"), ISEL_BTN, &ClearLabel);
    ClearButton->OnClicked.AddDynamic(this, &UWTItemSelectPanelWidget::OnClearSelectionClicked);
    if (UHorizontalBoxSlot* S = TitleRow->AddChildToHorizontalBox(ClearButton))
    {
        S->SetPadding(FMargin(4.f, 0.f));
        S->SetVerticalAlignment(VAlign_Center);
    }

    UButton* CloseButton = ISEL_MakeButton(WidgetTree, WT_LOCTEXT("Btn_Close", "关闭"), ISEL_BTN_SEL);
    CloseButton->OnClicked.AddDynamic(this, &UWTItemSelectPanelWidget::OnCloseClicked);
    if (UHorizontalBoxSlot* S = TitleRow->AddChildToHorizontalBox(CloseButton))
    {
        S->SetPadding(FMargin(4.f, 0.f));
        S->SetVerticalAlignment(VAlign_Center);
    }
    if (UVerticalBoxSlot* S = Root->AddChildToVerticalBox(TitleRow))
    {
        S->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));   // 与终端标题行的下方留白一致
    }
    // ★1.0.8BE 标题与正文之间的青色分隔线（终端同款，见 ISEL_MakeDivider）。
    Root->AddChildToVerticalBox(ISEL_MakeDivider(WidgetTree));

    // ---- ★1.0.7s 状态提示行（默认折叠，正常态不出现）-------------------------
    // 用户 2026-10-27 诉求（截图红框圈住「当前只下载：铁矿石　云端剩余 1,399 · 本站缓冲 0
    //   —— 要更换请点该行最右侧的【更换】」）：「这里没有移除？」⇒ 这一行整行去掉。
    // 依据：这些信息列表行里**已经全有了** —— 物品名（列②）、云端数量（列③）、
    //   每分速度（列④）、右侧【更换】按钮（列⑤）。顶部再复述一遍只是白占一行高度（≈30px），
    //   选中之后没有任何新增信息。
    // 控件本身保留，唯一用途 = **异常态兜底**：无目标 / 目录为空 / 搜索无匹配 / 云端池空。
    //   那几种情况下列表是空的，没有别的地方能告诉玩家「为什么没有东西」。
    // 默认 Collapsed（不是 Hidden）：Collapsed 不参与布局，列表起点立刻上移一行。
    SelectionText = ISEL_MakeText(WidgetTree, FText::GetEmpty(), 12, false, ISEL_GREEN,
                                  ETextJustify::Left);
    SelectionText->SetVisibility(ESlateVisibility::Collapsed);
    if (UVerticalBoxSlot* S = Root->AddChildToVerticalBox(SelectionText))
    {
        S->SetPadding(FMargin(2.f, 6.f, 2.f, 2.f));
    }

    // ---- ★1.0.7q 搜索框（只构造，**不单独占行**：下面并入过滤行的「物品」格）--------
    // 用户 2026-10-27 原话：「搜索框加到物品里面」。
    // 依据：这个搜索框从头到尾只过滤**物品名**，和「物品」下拉是同一个筛选维度的两种输入方式
    //   （打字找 / 列表挑），单独占一行纯属浪费垂直空间（一行 ≈ 40px，全让给列表）。
    //
    // ★1.0.7f「搜索框看不见文本」的根因：UEditableTextBox 默认用 CoreStyle 的深色输入框样式，
    // 而本面板底色是深蓝 ⇒ 输入的文字（默认近黑）与背景同色，只能看见闪烁光标。
    // 修法：一次性拼好 WidgetStyle（底刷 + 底色 + 前景/聚焦/只读三色 + 内边距 + 提示字体），
    // 赋值**只做一次** —— 分两次赋值虽然结果一样，但维护时极易看漏后一次覆盖了前一次。
    SearchBox = WidgetTree->ConstructWidget<UEditableTextBox>();
    {
        FEditableTextBoxStyle BoxStyle = FEditableTextBoxStyle::GetDefault();

        // 浅色底：与面板深蓝底拉开层次，同时让深色文字有对比度。
        // 用引擎自带 WhiteBrush（CoreStyle.cpp:261 `Style->Set("WhiteBrush", new FSlateColorBrush(White))`）
        // —— 默认构造的 FSlateBrush 没有资源对象，画出来是空的（本项目踩过多次）。
        const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
        if (WhiteBrush)
        {
            FSlateBrush Fill = *WhiteBrush;
            Fill.DrawAs = ESlateBrushDrawType::Box;
            Fill.SetImageSize(FVector2D(1.f, 1.f));
            BoxStyle.SetBackgroundImageNormal(Fill);
            BoxStyle.SetBackgroundImageHovered(Fill);
            BoxStyle.SetBackgroundImageFocused(Fill);
            BoxStyle.SetBackgroundImageReadOnly(Fill);
        }
        BoxStyle.SetBackgroundColor(FSlateColor(FLinearColor(0.860f, 0.905f, 0.930f, 1.f)));
        // 输入中的文字：深蓝黑（在浅底上清晰）；聚焦时再压深一档。
        BoxStyle.SetForegroundColor(FSlateColor(FLinearColor(0.055f, 0.070f, 0.090f, 1.f)));
        BoxStyle.SetFocusedForegroundColor(FSlateColor(FLinearColor(0.020f, 0.030f, 0.045f, 1.f)));
        BoxStyle.SetReadOnlyForegroundColor(FSlateColor(FLinearColor(0.055f, 0.070f, 0.090f, 1.f)));
        // ★1.0.7q 上下内边距 6 → 2：搜索框现在塞进被 USizeBox 锁成 28px 的过滤行里，
        //   原值（文字行高 17 + 上下 12 + 边框）≈ 31px 会被行高裁掉文字上下沿。
        //   水平内边距 10 → 7：物品格被一分为二后每半只有 150~180px，留白要省。
        BoxStyle.SetPadding(FMargin(7.f, 2.f));

        // 字号与 hint 色：Style.TextStyle 同时决定「输入文字」与「hint」的字体，
        // 只在这里改才真的生效（GEngine->GetMediumFont() 是默认值，不是这里的赋值）。
        FSlateFontInfo BoxFont = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 13);
        BoxStyle.TextStyle.Font = BoxFont;
        BoxStyle.TextStyle.ColorAndOpacity = FSlateColor(FLinearColor(0.300f, 0.350f, 0.400f, 1.f));

        SearchBox->WidgetStyle = BoxStyle;
    }
    SearchBox->SetJustification(ETextJustify::Left);   // ★文本靠左（UI 对齐铁律）
    // ★1.0.8BC 提示文案点明"这里是唯一入口"：列表默认空、由输入驱动，
    //   所以 hint 不能只是"搜索物品…"这种可选修饰，得让人知道不输入就看不到东西。
    SearchBox->SetHintText(WT_LOCTEXT("ItemPanel_SearchHint", "输入物品名（列表只显示匹配项）…"));
    SearchBox->OnTextChanged.AddDynamic(this, &UWTItemSelectPanelWidget::OnSearchChanged);
    // ★1.0.7q 这里**不再** `Root->AddChildToVerticalBox(SearchBox)`。
    //   它在下方的过滤行里作为「物品」格的右半边入树（见 ItemUnit）。

    // ---- ★1.0.7k 过滤条件行（用户诉求「云端加上面板过滤筛选条件」）--------------
    // 控件横向铺满、等高、行数恒定，切过滤条件不会让下面的列表起点跳动。
    // ★1.0.7p 起这一行是：「阶段」+「物品」（用户 2026-10-27「在阶段后面加个物品」）
    //   + 最右侧的下载速度读数。
    // ★1.0.7q 搜索框并入「物品」格（用户 2026-10-27「搜索框加到物品里面」）⇒
    //   这一行现在是「阶段 ｜ 物品▾+搜索框 ｜ 下载速度」，上面那行独立的搜索行已删除，
    //   列表因此上移一行（≈40px）。
    FilterRow = WidgetTree->ConstructWidget<UHorizontalBox>();
    {
        // ★1.0.7r 维度名从「每个选项都带前缀」改成**下拉左侧的固定标签**。
        //   旧态的问题（用户 2026-10-27 截图批注「这是什么玩意」）：展开时每一行都是
        //   「阶段：全部（126）」「阶段：T0（50）」「阶段：T1（1）」… 重复八遍「阶段：」。
        //   而收起态与展开态共用同一个字符串（SComboBox 的 content 也走 OnGenerateWidget），
        //   想只去掉展开态的前缀就得重写 RebuildWidget ⇒ 不如把标签拎到外面：
        //   标签常驻、选项只写值，两态都干净。
        //
        // ---- 阶段：[阶段] [全部（126）▾] ----
        UHorizontalBox* TierUnit = WidgetTree->ConstructWidget<UHorizontalBox>();
        {
            UTextBlock* TierLabel = ISEL_MakeText(WidgetTree,
                WT_LOCTEXT("ItemPanel_TierLabel", "阶段"), 12, false, ISEL_DIM, ETextJustify::Left);
            if (UHorizontalBoxSlot* LS = TierUnit->AddChildToHorizontalBox(TierLabel))
            {
                LS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
                LS->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
                LS->SetVerticalAlignment(VAlign_Center);
            }

            UWTItemFilterComboBox* TierCombo = WidgetTree->ConstructWidget<UWTItemFilterComboBox>();
            // ★样式必须在 TakeWidget() 之前刷（WidgetStyle/ItemStyle 只在 RebuildWidget 里交一次）。
            ISEL_StyleCombo(TierCombo);
            TierCombo->OnSelectionChanged.AddDynamic(this, &UWTItemSelectPanelWidget::OnMilestoneFilterChanged);
            if (UHorizontalBoxSlot* CS = TierUnit->AddChildToHorizontalBox(TierCombo))
            {
                CS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
                CS->SetPadding(FMargin(0.f));
                CS->SetVerticalAlignment(VAlign_Center);
            }
            MilestoneCombo = TierCombo;
        }
        if (UHorizontalBoxSlot* CS = FilterRow->AddChildToHorizontalBox(TierUnit))
        {
            CS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            CS->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
            CS->SetVerticalAlignment(VAlign_Center);
        }

        // ★1.0.7q 「物品」格 = 组合单元：[物品] + 下拉「挑」+ 搜索框「打字找」。
        //   三者作用在同一个维度（物品名）上，所以合并成一格 —— 用户原话「搜索框加到物品里面」。
        //   权重 1.7（阶段是 1.0）：这格要塞标签 + 下拉 + 搜索框，等分宽度下三样都不够。
        UHorizontalBox* ItemUnit = WidgetTree->ConstructWidget<UHorizontalBox>();

        {
            UTextBlock* ItemLabel = ISEL_MakeText(WidgetTree,
                WT_LOCTEXT("ItemPanel_ItemLabel", "物品"), 12, false, ISEL_DIM, ETextJustify::Left);
            if (UHorizontalBoxSlot* LS = ItemUnit->AddChildToHorizontalBox(ItemLabel))
            {
                LS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
                LS->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
                LS->SetVerticalAlignment(VAlign_Center);
            }
        }

        UWTItemFilterComboBox* ItemCombo = WidgetTree->ConstructWidget<UWTItemFilterComboBox>();
        ISEL_StyleCombo(ItemCombo);
        ItemCombo->OnSelectionChanged.AddDynamic(this, &UWTItemSelectPanelWidget::OnItemFilterChanged);
        if (UHorizontalBoxSlot* CS = ItemUnit->AddChildToHorizontalBox(ItemCombo))
        {
            // 下拉略宽于搜索框：下拉要装下「物品：全部（126）」+ 下拉箭头，
            // 搜索框只需装下提示语（已缩短为「搜索物品…」）。
            FSlateChildSize SlotSize(ESlateSizeRule::Fill);
            SlotSize.Value = 1.15f;
            CS->SetSize(SlotSize);
            CS->SetPadding(FMargin(0.f));
            CS->SetVerticalAlignment(VAlign_Center);
        }
        ItemFilterCombo = ItemCombo;

        // 搜索框：紧贴下拉右侧，只留 4px 缝 —— 「有分界但明显同一格」。
        // 高度不用担心：外面那层 USizeBox 锁 28px，搜索框样式内边距已相应改成 (7,2)。
        if (UHorizontalBoxSlot* SS = ItemUnit->AddChildToHorizontalBox(SearchBox))
        {
            SS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            SS->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
            SS->SetVerticalAlignment(VAlign_Center);
        }

        if (UHorizontalBoxSlot* CS = FilterRow->AddChildToHorizontalBox(ItemUnit))
        {
            FSlateChildSize SlotSize(ESlateSizeRule::Fill);
            SlotSize.Value = 1.7f;
            CS->SetSize(SlotSize);
            CS->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
            CS->SetVerticalAlignment(VAlign_Center);
        }
    }
    // ---- ★1.0.7n 过滤行最右侧：下载速度读数（用户诉求「加上一个下载速度」）----
    // 为什么不单开一行：这一行高度已被下方 USizeBox 锁成 28px、右侧本来就空着，
    // 塞进同一行既不新增行、也不会让「列表起点」随速度文字出现/消失而跳动。
    SpeedText = ISEL_MakeText(WidgetTree, FText::GetEmpty(), 12, false, ISEL_GREEN,
                              ETextJustify::Right);
    if (SpeedText)
    {
        // ★固定最小宽：数字位数会变（0 → 12,480/分）。不锁宽的话每 0.5s 刷新一次，
        //   左边的阶段下拉就会跟着一伸一缩地抖。210px 够放「下载速度 60/分（上限 600/分）」。
        SpeedText->SetMinDesiredWidth(210.f);
        SpeedText->SetJustification(ETextJustify::Right);
        if (UHorizontalBoxSlot* SS = FilterRow->AddChildToHorizontalBox(SpeedText))
        {
            SS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
            SS->SetPadding(FMargin(8.f, 0.f, 4.f, 0.f));
            SS->SetVerticalAlignment(VAlign_Center);
        }
    }
    // ★1.0.7l 用 USizeBox 锁死这一行的高度。
    //   实测教训：`UComboBoxString` 的 desired size 来自 SComboBox 的内容，
    //   在「选项还没填 / 皮肤 brush 没有 ImageSize」时可能塌成 0 ⇒ 整行在面板里**看不见**，
    //   看起来就像「根本没有过滤条件」。固定高度是与内容无关的硬保证。
    USizeBox* FilterSize = WidgetTree->ConstructWidget<USizeBox>();
    FilterSize->SetHeightOverride(28.f);
    FilterSize->SetContent(FilterRow);
    if (UVerticalBoxSlot* FS = Root->AddChildToVerticalBox(FilterSize))
    {
        FS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
        FS->SetPadding(FMargin(2.f, 0.f, 2.f, 6.f));
    }

    // ---- item rows -----------------------------------------------------------
    ItemListBox = WidgetTree->ConstructWidget<UScrollBox>();
    if (UVerticalBoxSlot* S = Root->AddChildToVerticalBox(ItemListBox))
    {
        S->SetPadding(FMargin(0.f, 4.f));
        S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }

    RootBorder->SetContent(Root);
    if (UBorderSlot* S = Cast<UBorderSlot>(RootBorder->GetContentSlot()))
    {
        S->SetHorizontalAlignment(HAlign_Fill);
        S->SetVerticalAlignment(VAlign_Fill);
        S->SetPadding(FMargin(16.f));   // ★1.0.8BE 从 RootBorder 下沉到这里（见上）
    }

    // ★1.0.7l UI 自证：这一行用来回答「过滤条件到底建没建」。
    //   上一轮用户反馈「过滤条件也没有」，但代码/二进制都已确认存在 ⇒ 只能用一条日志把
    //   「控件建了没 / 高度锁了什么值」钉死，避免再来一轮纯推测。
    WT_WARNF(TEXT("[item-panel] UI built: tierCombo=%s itemCombo=%s heightOverride=%.0f | verticalChildren=%d"),
             MilestoneCombo ? TEXT("yes") : TEXT("NO"),
             ItemFilterCombo ? TEXT("yes") : TEXT("NO"),
             28.f,
             Root->GetChildrenCount());

    return RootCanvas->TakeWidget();
}

void UWTItemSelectPanelWidget::SetInputMode_Implementation()
{
    // ★1.0.7c 这是「按 E 打开自建面板后鼠标转视角、键盘动角色」的根治点。
    //
    // 事实链（全部一手核实，不靠猜）：
    //   ① UFGInteractWidget::SetInputMode 是 BlueprintNativeEvent（FGInteractWidget.h:43-44）；
    //   ② SML 桩 FGInteractWidget.cpp:35 里它的 _Implementation 是空函数 `{}`；
    //   ③ 原版交互面板（BPW_CentralStorage_C 等）是蓝图，在蓝图图表里实现了这个事件 ⇒
    //      按 E 打开原版面板输入被正常接管（鼠标可见可点、角色不动）；
    //   ④ 本面板是纯 C++ 类且此前**没有覆写**这个事件 ⇒ 游戏在 PushWidget 后调用它时什么也没发生
    //      ⇒ 输入模式仍是游戏态，鼠标转视角、键盘动角色。日志 `activeWidget=1` 说明互交栈没问题，
    //      缺的正是这一步。
    //
    // 对照 MD 铁律「要接管输入必须自己管」：接管 = 在游戏会调用的这个钩子里做，而不是在
    // NativeConstruct 里抢（那时面板还没进视口，日志实测 inViewport=0，做了也会被游戏覆盖）。
    APlayerController* PC = GetOwningPlayer();
    if (!PC)
    {
        return;
    }

    // ① 输入模式：GameAndUI 而非 UIOnly —— 面板要能收鼠标点按，同时保留游戏侧对 ESC 等的处理；
    //    DoNotLock 保证光标不被捕获进视口（否则点不到 UI）；HideCursorDuringCapture=false
    //    防止按键捕获期间游戏把光标藏起来（FGPlayerController 的默认行为，终端面板 0.2.132 踩过）。
    FInputModeGameAndUI InputMode;
    InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    InputMode.SetHideCursorDuringCapture(false);
    if (TSharedPtr<SWidget> SlateWidget = TakeWidget())
    {
        InputMode.SetWidgetToFocus(SlateWidget.ToSharedRef());
    }
    PC->SetInputMode(InputMode);
    PC->bShowMouseCursor = true;

    // ② 锁视角/锁移动：SetInputMode 只管 Slate 层，EnhancedInput 的视角与移动仍会流到 Pawn。
    //    用 Reset* 而不是 Set*(false)：Controller.h:353-376 明确写着 SetIgnore* 连续调用会**叠加**、
    //    需要同次数撤销才恢复，反复打开面板会让计数越积越高、最后再也解不开。
    PC->ResetIgnoreLookInput();
    PC->SetIgnoreLookInput(true);
    PC->ResetIgnoreMoveInput();
    PC->SetIgnoreMoveInput(true);

    WT_WARNF(TEXT("[item-panel] SetInputMode: GameAndUI + cursor + look/move suppressed (inViewport=%d)"),
             IsInViewport() ? 1 : 0);
}

void UWTItemSelectPanelWidget::NativeConstruct()
{
    Super::NativeConstruct();

    // ★1.0.8BE 与终端面板（Remote_FactoryPanelWidget::NativeConstruct，1082-1101 行）**同一套**
    //   视口自适应：视口 90% 宽 / 85% 高，上限 1280x820，居中。
    //   为什么要抄而不是各写一套：两块面板会在游戏里前后脚打开，尺寸公式一旦分叉，
    //   玩家切来切去就会看到面板忽大忽小 —— 这正是"UI 不一致"最直观的那一面。
    if (RootCanvas && RootCanvas->GetSlots().Num() > 0)
    {
        if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(RootCanvas->GetSlots()[0]))
        {
            FVector2D Viewport(1920.f, 1080.f);
            if (UGameViewportClient* VP = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
            {
                VP->GetViewportSize(Viewport);
            }
            const float W = FMath::Min(Viewport.X * 0.90f, 1280.f);
            const float H = FMath::Min(Viewport.Y * 0.85f, 820.f);
            CanvasSlot->SetSize(FVector2D(W, H));
            CanvasSlot->SetAnchors(FAnchors(0.5f, 0.5f));
            CanvasSlot->SetAlignment(FVector2D(0.5f, 0.5f));
            CanvasSlot->SetPosition(FVector2D::ZeroVector);
        }
    }

    // ★1.0.7z 上传站模式：同一个面板类被下载站/上传站共用（用户 2026-10-27 定案
    //   「我们的云端上传站也采用此UI即可，输出就不显示【更换/选择】」）。
    //   必须在 EnsureCatalog/RebuildRows 之前定下来 —— 行构建要据此决定建不建行尾按钮与标记。
    // ★1.0.8AQ 扩展成四种站（固/液 × 上/下），先解析种类，再派生上传侧开关。
    PanelKind = ResolvePanelKind();
    bUploaderMode = IsUploaderKind();

    // ★1.0.8BH 幂等标记复位：两个标记都是"这一次打开期间的一次性事件"。
    //   正常情况下每次交互都是新实例（默认值即 false），但**不能靠这个假设**——
    //   一旦某个版本改成复用实例，残留的 true 会让"关闭时还原输入模式"整段被跳过，
    //   症状是关面板后角色还锁着视角/走不动。复位成本为零。
    bInteractStackReleased = false;
    bInputRestoredBySelf   = false;

    // ★1.0.8BI 净值采样的基线也必须复位：PrevPoolTotals 是**上一台机器/上一次打开**的池快照，
    //   沿用会让第一拍算出一个巨大的假净值（池在两次打开之间可能变化成千上万）。
    bPoolBaselineReady = false;
    PrevPoolTotals.Reset();
    NetRateCache.Reset();
    // ★1.0.8BJ 「还没测到净值」同理复位：不 reset 的话第二次打开会先显示上一次的「净 0」。
    bNetMeasured = false;

    // ★1.0.7c 这里**不再**自己 SetInputMode。日志实测（`[item-panel] construct: cursor=1 focus=0
    // inViewport=0 activeWidget=1`）证明 NativeConstruct 执行时面板还没进视口，此时抢来的输入模式
    // 会被游戏随后的 PushWidget 流程覆盖，纯属白做。输入接管统一交给游戏调用的
    // SetInputMode_Implementation()（见上方实现）。

    EnsureCatalog();
    // ★1.0.7k 兜底填一次阶段下拉：EnsureCatalog 在「管理器未就绪」时会提前 return（不置
    // bCatalogBuilt 以便重试），那时下拉会是空的 —— 这里补一次保证控件至少是「阶段：全部（N）」。
    // 目录真的重建时 EnsureCatalog 末尾还会再调一次（幂等）。
    RebuildMilestoneFilter();
    // ★1.0.7p 物品下拉同样兜底填一次（它依赖目录 + 阶段，两者任一未就绪都会是空下拉）。
    RebuildItemFilter();
    RebuildVisible();
    RebuildRows();
    RefreshAmounts();
    RefreshRowStyles();
    // ★1.0.7n 面板一打开就有读数（不等第一次 0.5s tick，否则开局那半秒是空白）。
    RefreshSpeedText();

    // ★1.0.8BJ 数据刷新的驱动换成世界定时器（0.30s 循环）。
    //   旧实现挂在 NativeTick 的 0.5s 累加块里，用户实测「上传下载不实时」——
    //   那条路径依赖 UMG 的 tick 策略，没走到时界面就变成打开瞬间的静态快照，且无任何报错。
    //   定时器与 Slate tick 无关；绑 UObject 成员函数 ⇒ 面板销毁后自动失效。
    if (UWorld* TimerWorld = GetWorld())
    {
        TimerWorld->GetTimerManager().SetTimer(
            RefreshTimerHandle, this, &UWTItemSelectPanelWidget::OnPanelRefreshTick,
            0.30f, /*bLoop=*/true);
    }
    else
    {
        WT_WARNF(TEXT("[item-panel] WARNING: no world at construct -> refresh timer NOT armed "
                      "(panel will show a static snapshot)"));
    }
}

void UWTItemSelectPanelWidget::OnPanelRefreshTick()
{
    // 只在第一次打一条，证明"数据刷新驱动"确实活着（与 tick 取证日志互为对照）。
    if (!bLoggedRefreshTimer)
    {
        bLoggedRefreshTimer = true;
        WT_WARNF(TEXT("[item-panel] refresh timer alive (0.30s loop, kind=%d)"),
                 static_cast<int32>(PanelKind));
    }

    // ★1.0.7e 进度变化检测：玩家在面板开着的时候升了阶段 / 研究完一个 MAM 节点，
    //   目录应当立刻把新解锁的产物补进来。代价 = 每 0.5s 走一遍 schematic 判定，
    //   与「重建目录」本身的成本相比可以忽略（重建只在签名真的变了时才发生）。
    if (bCatalogBuilt)
    {
        const int32 NowSig = ComputeProgressSig();
        if (NowSig >= 0 && NowSig != ProgressSig)
        {
            // 进度变了：让 EnsureCatalog 重跑，然后整表重建（行集可能大幅变化）。
            WT_WARNF(TEXT("[item-panel] progress changed (%d -> %d); rebuilding catalog."),
                     ProgressSig, NowSig);
            bCatalogBuilt = false;
            EnsureCatalog();
            RebuildVisible();
            RebuildRows();
        }
    }

    RefreshAmounts();
    RefreshRowStyles();
    RefreshSpeedText();

    // ★1.0.8BJ 取证日志（每 3 秒一条）：把「界面没动」拆成「没刷新」与「源头没变」两件事。
    //   取第一行（= 玩家最关心的那一类）作为样本，避免刷屏。
    if (UWorld* DiagWorld = GetWorld())
    {
        const double NowD = DiagWorld->GetTimeSeconds();
        if (NowD - LastDiagLogTime >= 3.0)
        {
            LastDiagLogTime = NowD;
            const UClass* HeadCls = (Visible.Num() > 0) ? Visible[0].Get() : nullptr;
            const UClass* Picked  = GetSelectedClass();
            const int32 HeadAmount  = AmountCache.IsValidIndex(0) ? AmountCache[0] : 0;
            const float HeadRate    = HeadCls ? LastItemRates.FindRef(const_cast<UClass*>(HeadCls)) : 0.f;
            const float HeadNet     = HeadCls ? NetRateCache.FindRef(const_cast<UClass*>(HeadCls)) : 0.f;
            WT_WARNF(TEXT("[item-panel] refresh: kind=%d rows=%d/%d head=%s amount=%d rate=%.0f net=%.0f ")
                     TEXT("total=%.0f picked=%s baseline=%d"),
                     static_cast<int32>(PanelKind), NumShownRows, Visible.Num(),
                     HeadCls ? *HeadCls->GetName() : TEXT("none"), HeadAmount, HeadRate, HeadNet,
                     WT_GetTotalRatePerMinute(),
                     Picked ? *Picked->GetName() : TEXT("none"),
                     bPoolBaselineReady ? 1 : 0);
        }
    }
}

void UWTItemSelectPanelWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    // ★1.0.8BJ 这里**只**做两件与输入/取证有关的事，数据刷新已搬到 OnPanelRefreshTick
    //   （世界定时器）。原因见 NativeConstruct 末尾的注释与头文件 OnPanelRefreshTick 的说明。
    if (!bLoggedNativeTick)
    {
        bLoggedNativeTick = true;
        WT_WARNF(TEXT("[item-panel] native tick alive (dt=%.3f, inViewport=%d)"),
                 InDeltaTime, IsInViewport() ? 1 : 0);
    }

    // ★1.0.7c 只补光标，不抢输入模式。输入模式由 SetInputMode_Implementation() 统一接管
    //   （游戏 PushWidget 之后会调它）；在 tick 里再 SetInputMode 反而可能与游戏流程打架。
    //   光标单独每帧拉回：Satisfactory 每次 SetInputMode 都会重写 bShowMouseCursor
    //   （终端面板 0.2.132 实锤），不拉就会「面板开着但看不见鼠标」。
    if (IsInViewport())
    {
        if (APlayerController* PC = GetOwningPlayer())
        {
            if (!PC->bShowMouseCursor)
            {
                PC->bShowMouseCursor = true;
            }
        }
    }
}

AWTUpgradeCloudDownloader* UWTItemSelectPanelWidget::GetDownloader() const
{
    // mInteractObject 是原版交互路径开面板时填的目标（与 Remote_FactoryPanelWidget /
    // Remote_FactoryPanelWidget 同一读法）。
    if (FObjectProperty* OP = CastField<FObjectProperty>(
            UFGInteractWidget::StaticClass()->FindPropertyByName(TEXT("mInteractObject"))))
    {
        UObject* Raw = OP->GetObjectPropertyValue(OP->ContainerPtrToValuePtr<void>(this));
        return Cast<AWTUpgradeCloudDownloader>(Raw);
    }
    return nullptr;
}

EWTCloudPanelKind UWTItemSelectPanelWidget::ResolvePanelKind() const
{
    // mInteractObject 是原版交互路径开面板时填的目标（与 Remote_FactoryPanelWidget /
    // Remote_FactoryPanelWidget 同一读法）。
    if (FObjectProperty* OP = CastField<FObjectProperty>(
            UFGInteractWidget::StaticClass()->FindPropertyByName(TEXT("mInteractObject"))))
    {
        UObject* Raw = OP->GetObjectPropertyValue(OP->ContainerPtrToValuePtr<void>(this));
        if (!Raw)
        {
            return EWTCloudPanelKind::None;
        }
        // 两类之间没有继承交叉：固体上传站是 AFGCentralStorageContainer 支系，
        // 固体下载站是 AFGBuildableStorage 支系 ⇒ 顺序无关。
        if (Raw->IsA(AWTUpgradeCloudDownloader::StaticClass()))
        {
            return EWTCloudPanelKind::SolidDownloader;

        }
        if (Raw->IsA(AWTUpgradeStorage::StaticClass()))
        {
            return EWTCloudPanelKind::SolidUploader;
        }
    }
    return EWTCloudPanelKind::None;
}



AWTUpgradeStorage* UWTItemSelectPanelWidget::GetUploaderStation() const
{
    if (FObjectProperty* OP = CastField<FObjectProperty>(
            UFGInteractWidget::StaticClass()->FindPropertyByName(TEXT("mInteractObject"))))
    {
        UObject* Raw = OP->GetObjectPropertyValue(OP->ContainerPtrToValuePtr<void>(this));
        return Cast<AWTUpgradeStorage>(Raw);
    }
    return nullptr;
}



// ---- cloud-fluid panel helpers (1.0.8AQ) -----------------------------------------------










void UWTItemSelectPanelWidget::RefreshInboundCache()
{
    InboundCache.Reset();

    if (PanelKind == EWTCloudPanelKind::SolidUploader)
    {
        // 固体上传站：站内库存里实际存在的东西 = 「这台机器正在输入的物品」。
        // 用 GetStorageInventory() 而不是反射 handler：**只读**场景不需要"活跃库存"那层语义
        // （那层是为写入准备的，见 Remote_FactoryUpgrades.cpp 的 WT_ResolveActiveStorageInventory）。
        if (AWTUpgradeStorage* Station = GetUploaderStation())
        {
            if (UFGInventoryComponent* Inv = Station->GetStorageInventory())
            {
                TArray<FInventoryStack> Stacks;
                Inv->GetInventoryStacks(Stacks);
                for (const FInventoryStack& Stack : Stacks)
                {
                    if (UClass* K = Stack.Item.GetItemClass().Get())
                    {
                        InboundCache.Add(K);
                    }
                }
            }
        }
    }
}



bool UWTItemSelectPanelWidget::IsUploaderKind() const
{
    return PanelKind == EWTCloudPanelKind::SolidUploader;
}

void UWTItemSelectPanelWidget::WT_GetItemRates(TMap<UClass*, float>& OutRates) const
{
    // ★1.0.8BI 两个固体站各有自己的速率源（上传侧此前**完全没有**，见站体注释）。
    //   语义随 PanelKind 变：下载侧 = 搬进本站缓冲；上传侧 = 推进云端池。
    //   面板只关心"这一行本机的流量是多少"，方向由 RefreshRowStyles 按 IsUploaderKind 加箭头。
    if (const AWTUpgradeCloudDownloader* D = GetDownloader())
    {
        D->GetItemRatesPerMinute(OutRates);
        return;
    }
    if (const AWTUpgradeStorage* U = GetUploaderStation())
    {
        U->GetItemRatesPerMinute(OutRates);
        return;
    }
}

float UWTItemSelectPanelWidget::WT_GetRateLimitPerMinute() const
{
    if (const AWTUpgradeCloudDownloader* D = GetDownloader())
    {
        return D->GetDownloadRateLimitPerMinute();
    }
    return 0.f;
}

float UWTItemSelectPanelWidget::WT_GetTotalRatePerMinute() const
{
    // ★1.0.8BI 两个固体站都有本机读数（上传侧 = 推进云端池的速度）。
    //   面板顶栏用它显示「上传速度 X/分」/「下载速度 X/分」，方向由 RefreshSpeedText 决定。
    if (const AWTUpgradeCloudDownloader* D = GetDownloader())
    {
        return D->GetDownloadRatePerMinute();
    }
    if (const AWTUpgradeStorage* U = GetUploaderStation())
    {
        return U->GetUploadRatePerMinute();
    }
    return 0.f;
}

UClass* UWTItemSelectPanelWidget::GetSelectedClass() const
{
    // 「下载仅限 1 种」⇒ 白名单里最多一个元素，取第一个即当前选中。
    // 老存档可能遗留多个（0.2.36~1.0.7e 允许勾多项），那种情况下取第一个并保持原样不动 ——
    // 不主动裁剪玩家的既有存档，等他点【更换】时自然收敛成单选。
    const AWTUpgradeCloudDownloader* Downloader = GetDownloader();
    if (!Downloader)
    {
        return nullptr;
    }
    const TArray<TSubclassOf<UFGItemDescriptor>>& Filter = Downloader->GetOutputFilter();
    if (Filter.Num() == 0)
    {
        return nullptr;
    }
    return Filter[0].Get();
}

int32 UWTItemSelectPanelWidget::ComputeProgressSig() const
{
    // 签名 = (已解锁的里程碑/MAM/替代配方来源数 << 8) | 最高阶段号。
    // 只要玩家多解锁一个来源或推进一个阶段，签名就变 ⇒ 触发目录重建。
    AFGSchematicManager* SM = AFGSchematicManager::Get(GetWorld());
    if (!SM)
    {
        return -1;   // 管理器未就绪
    }

    int32 Sources = 0;
    int32 HighestTier = 0;
    for (TObjectIterator<UClass> It; It; ++It)
    {
        UClass* SchC = *It;
        if (!SchC || !SchC->IsChildOf(UFGSchematic::StaticClass()))
        {
            continue;
        }
        if (SchC->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
        {
            continue;
        }
        const ESchematicType T = UFGSchematic::GetType(SchC);
        // 只认「会发放配方」的四类来源：阶段里程碑 / MAM 研究 / 替代配方 / 硬盘。
        if (T != ESchematicType::EST_Milestone && T != ESchematicType::EST_MAM &&
            T != ESchematicType::EST_Alternate && T != ESchematicType::EST_HardDrive)
        {
            continue;
        }
        if (!SM->IsSchematicPurchased(SchC))
        {
            continue;
        }
        ++Sources;
        HighestTier = FMath::Max(HighestTier, UFGSchematic::GetTechTier(SchC));
    }
    return (Sources << 8) | (HighestTier & 0xFF);
}



void UWTItemSelectPanelWidget::EnsureCatalog()
{
    if (bCatalogBuilt)
    {
        return;
    }

    // ★★★1.0.7e 用户定案（2026-10-26）：「当前列表内的物品，非截图中建筑产出的不应写入」+
    //   「列表产出会根据当前阶段的解锁和里程碑进度（包括 MAM）解锁」。
    //
    // 一、哪些物品算「生产栏建筑产出」——判定口径（一手核实，不靠物品名猜）：
    //   ① AFGBuildableFactory（Buildables/FGBuildableFactory.h:27）是 Satisfactory 建造器「生产」栏
    //      **全部建筑**的共同基类。继承链逐环核实（SML SDK 头文件）：
    //        AFGBuildableFactory
    //          ├─ AFGBuildableManufacturer（制造站/装配站/构造站/冶炼站/铸造站/精炼站/搅拌站/
    //          │   灌装站/粒子加速站/转化站/量子编码站…）
    //          └─ AFGBuildableResourceExtractorBase（FGBuildableResourceExtractorBase.h:65）
    //               └─ AFGBuildableResourceExtractor（FGBuildableResourceExtractor.h:13）
    //                    ├─ AFGBuildableFrackingExtractor（FGBuildableFrackingExtractor.h:14）
    //                    └─ AFGBuildableWaterPump（FGBuildableWaterPump.h:13）
    //      ⇒ **采矿机**（BP 侧是 AFGBuildableResourceExtractor 的蓝图子类）、抽油机、水泵
    //        全部落在 AFGBuildableFactory 支系内 ⇒ 用户要的「采矿机等资源采集产出」天然被覆盖。
    //   ② UFGRecipe::GetProducts(Recipe)   取配方产出（FGRecipe.h:63）；
    //      UFGRecipe::GetProducedIn(Recipe) 取该配方能在哪些建筑里生产（FGRecipe.h:83）。
    //   ③ 于是「生产栏建筑产出」= 至少有一个生产栏建筑能造出来的物品。
    //      桶装流体是灌装站的产物、本身是固体 ⇒ 自然收录（用户明确要收录它们）。
    //      矿石/原油/水这类**原始资源**不是配方产物（它们由资源节点的开采逻辑产出，不过 recipe
    //      链），所以不会出现在这里 —— 这与「本面板只列配方产物」的口径一致，不是漏项。
    //
    // 二、按进度解锁——游戏自身的解锁链路（一手核实）：
    //   ① 配方由「解锁项」发放：UFGUnlockRecipe::GetRecipesToUnlock()（FGUnlockRecipe.h:33）；
    //   ② 解锁项挂在蓝图（schematic）上：UFGSchematic::GetUnlocks()（FGSchematic.h:116）；
    //   ③ 蓝图类型决定来源：UFGSchematic::GetType()（FGSchematic.h:80）→ ESchematicType
    //      （FGSchematic.h:18-32）里的 EST_Milestone（阶段里程碑）/ EST_MAM（MAM 研究）
    //      / EST_Alternate·EST_HardDrive（硬盘替代配方）等；
    //   ④ 阶段号：UFGSchematic::GetTechTier()（FGSchematic.h:120）；
    //   ⑤ 是否已解锁：AFGSchematicManager::IsSchematicPurchased()（FGSchematicManager.h:223）。
    //   ⇒ **只收录「产出它的某个配方，被某个已解锁的里程碑/MAM/替代配方发放」的物品。**
    //     玩家还没解锁的阶段的产物、以及还没研究的 MAM 产物，一律不出现在列表里。
    //
    // 三、注意「未解锁 ≠ 永远不列」：玩家升阶段/MAM 研究后，面板下次打开（或进度签名变化时）
    //   会重建目录，新解锁的东西自动出现。所以这里缓存了一个进度签名 ProgressSig。

    // ---- ① 收集「由里程碑 / MAM / 替代配方解锁的配方」----
    // 同一个配方可能被多个来源发放；这里只关心「有没有被某个已解锁来源发过」。
    TSet<UClass*> UnlockedRecipes;

    // ★1.0.7r 配方类 → 解锁它的最小 TechTier，**按来源类型分成两张表**。
    //
    // 为什么必须分开（用户 2026-10-27「配方要写到适合的阶段，哪怕是替代配方」的根因）：
    //   `UFGSchematic::GetTechTier` 对**里程碑**返回的就是玩家看到的 Tier 号（0/1/2…9），可信；
    //   但对 MAM 研究（EST_MAM）和硬盘替代配方（EST_Alternate/EST_HardDrive），这个字段
    //   几乎恒为 **0** —— 它们本来就不按 Tier 分组（MAM 靠研究解锁、替代配方靠硬盘）。
    //   旧代码把三类来源混在一张表里取 min ⇒ 只要某物品有一个 MAM/替代来源，它就被判成
    //   **T0**，于是「阶段：全部（126）」下面 T0 堆了 50 个、T1 只剩 1 个 —— 正是下拉里
    //   那个荒谬的分布。
    //   ⇒ 里程碑来源是**定阶段的可信依据**；MAM/替代来源只在「该物品压根没有里程碑来源」时
    //     才拿来兜底，且 tier==0 视为无效（0 对它们不表达任何阶段含义）。
    TMap<UClass*, int32> TierByRecipeMilestone;   // 可信：里程碑解锁的配方
    TMap<UClass*, int32> TierByRecipeInfoOnly;    // 仅参考：MAM / 替代配方 / 硬盘

    AFGSchematicManager* SchematicMgr = AFGSchematicManager::Get(GetWorld());
    AFGRecipeManager* RecipeMgr = AFGRecipeManager::Get(GetWorld());
    if (!SchematicMgr || !RecipeMgr)
    {
        // 开局极早期管理器还没就绪。★不能把 bCatalogBuilt 留成 true —— 否则目录永久为空且
        // 再也不重建。回滚标记，下次打开面板重试。
        WT_WARNF(TEXT("[item-panel] managers not ready (schematic=%d recipe=%d); catalog build deferred."),
                 SchematicMgr ? 1 : 0, RecipeMgr ? 1 : 0);
        return;
    }

    // 进度签名：已解锁来源的数量 + 最高阶段号。任一变化 ⇒ 说明玩家有进展 ⇒ 目录需要重建。
    int32 UnlockedSourceCount = 0;
    int32 HighestTier = 0;

    for (TObjectIterator<UClass> It; It; ++It)
    {
        UClass* SchC = *It;
        if (!SchC || !SchC->IsChildOf(UFGSchematic::StaticClass()))
        {
            continue;
        }
        if (SchC->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
        {
            continue;
        }

        const ESchematicType Type = UFGSchematic::GetType(SchC);
        // 只认「会发放配方」的四类来源：阶段里程碑、MAM 研究、替代配方（硬驱开出的替代配方）。
        // 商店/自定义/教程等不发生产配方，跳过（省掉 IsSchematicPurchased 的调用）。
        if (Type != ESchematicType::EST_Milestone &&
            Type != ESchematicType::EST_MAM &&
            Type != ESchematicType::EST_Alternate &&
            Type != ESchematicType::EST_HardDrive)
        {
            continue;
        }

        if (!SchematicMgr->IsSchematicPurchased(SchC))
        {
            continue;   // 尚未解锁
        }

        ++UnlockedSourceCount;
        const int32 SchTier = UFGSchematic::GetTechTier(SchC);
        HighestTier = FMath::Max(HighestTier, SchTier);

        // 取该蓝图发放的解锁项，挑出 UFGUnlockRecipe，把它们解锁的配方记下来。
        for (UFGUnlock* Unlock : UFGSchematic::GetUnlocks(SchC))
        {
            if (UFGUnlockRecipe* RecipeUnlock = Cast<UFGUnlockRecipe>(Unlock))
            {
                for (const TSubclassOf<UFGRecipe>& R : RecipeUnlock->GetRecipesToUnlock())
                {
                    if (UClass* RC = R.Get())
                    {
                        UnlockedRecipes.Add(RC);
                        // ★1.0.7r 同一配方被多个来源发放时取最小阶段；**按来源类型分表**：
                        //   里程碑＝可信依据；MAM/替代配方的 TechTier 基本恒为 0，只能当兜底参考。
                        TMap<UClass*, int32>& Sink =
                            (Type == ESchematicType::EST_Milestone) ? TierByRecipeMilestone
                                                                    : TierByRecipeInfoOnly;
                        if (int32* Found = Sink.Find(RC))
                        {
                            *Found = FMath::Min(*Found, SchTier);
                        }
                        else
                        {
                            Sink.Add(RC, SchTier);
                        }
                    }
                }
            }
        }
    }

    ProgressSig = ComputeProgressSig();
    bCatalogBuilt = true;
    Catalog.Reset();
    TierByItem.Reset();   // ★1.0.7k 与 Catalog 严格同步重建，否则旧阶段会挂在新目录上

    // ---- ② 从「已解锁配方」里筛出生产栏建筑能产出的固体物品 ----
    // GetAllRecipes() 返回 `const TArray<TSubclassOf<UFGRecipe>>&`（FGRecipeManager.h:77）——用引用别拷。
    // 保留的附加过滤（与旧版一致，不放松）：
    //   * 只要 RF_SOLID（★液体/气体排除 = 用户明确要求，流体走云端流体下载站）；
    //   * 排除 UFGItemDescriptor 基类自身 / 建筑描述符支系 / 抽象·弃用·占位类 / 空名。
    static const TCHAR* SkipNameTokens[] = {
        TEXT("NoneDescriptor"), TEXT("AnyUndefinedDescriptor"),
        TEXT("OverflowDescriptor"), TEXT("InvalidDescriptor"),
        TEXT("AbstractDescriptor"), TEXT("UndefinedDescriptor"),
    };

    // 用 TSet 去重（同一物品常有多种配方）。
    TSet<UClass*> Producible;

    // ★1.0.7r 待估阶段的「物品 ← 配方」对：该配方是唯一来源、但来源给不出阶段（MAM/替代的 0）。
    // 整轮扫完后用「原料阶段」估一个（见 ②b）。局部结构体，不入 GC（且只在本函数内用完）。
    struct FISEL_PendingTier
    {
        UClass* Item = nullptr;
        UClass* Recipe = nullptr;
    };
    TArray<FISEL_PendingTier> PendingTierByIngredients;
    const TArray<TSubclassOf<UFGRecipe>>& AllRecipes = RecipeMgr->GetAllRecipes();
    for (const TSubclassOf<UFGRecipe>& Recipe : AllRecipes)
    {
        UClass* RecipeClass = Recipe.Get();
        if (!RecipeClass)
        {
            continue;
        }
        // ★进度判定：该配方必须已被某个已解锁的里程碑/MAM/替代配方发放。
        if (!UnlockedRecipes.Contains(RecipeClass))
        {
            continue;
        }

        // 该配方能否在「生产栏建筑」里生产？
        bool bProducedInFactory = false;
        for (const TSubclassOf<UObject>& Producer : UFGRecipe::GetProducedIn(Recipe))
        {
            UClass* P = Producer.Get();
            if (P && P->IsChildOf(AFGBuildableFactory::StaticClass()))
            {
                bProducedInFactory = true;
                break;
            }
        }
        if (!bProducedInFactory)
        {
            continue;   // 工作台/装备工坊/商店等非「生产栏」建筑
        }

        for (const FItemAmount& Out : UFGRecipe::GetProducts(Recipe))
        {
            if (UClass* Cls = Out.ItemClass.Get())
            {
                Producible.Add(Cls);
                // ★1.0.7r 阶段判定（用户 2026-10-27「配方要写到适合的阶段，哪怕是替代配方」）：
                //   ① 里程碑来源的 TechTier 直接采信（那就是玩家在里程碑界面看到的 Tier）；
                //   ② 没有里程碑来源时，才拿 MAM/替代配方的 TechTier 兜底，且 ==0 视为无效
                //      （MAM/替代本来就不按 Tier 分组，字段恒 0，当 T0 用就是这次的错源）；
                //   ③ 两样都拿不到 ⇒ 本配方给不出阶段，换该物品的其它配方再看。
                int32 Candidate = -1;
                if (const int32* Mile = TierByRecipeMilestone.Find(RecipeClass))
                {
                    Candidate = *Mile;
                }
                else if (const int32* Info = TierByRecipeInfoOnly.Find(RecipeClass))
                {
                    if (*Info > 0)
                    {
                        Candidate = *Info;
                    }
                }
                if (Candidate < 0)
                {
                    // 记下来：等整轮扫完，再用「原料阶段」给它估一个（见 ②b）。
                    FISEL_PendingTier Pending;
                    Pending.Item = Cls;
                    Pending.Recipe = RecipeClass;
                    PendingTierByIngredients.Add(Pending);
                    continue;
                }
                if (int32* Found = TierByItem.Find(Cls))
                {
                    *Found = FMath::Min(*Found, Candidate);
                }
                else
                {
                    TierByItem.Add(Cls, Candidate);
                }
            }
        }
    }

    for (UClass* C : Producible)
    {
        if (!C || C == UFGItemDescriptor::StaticClass())
        {
            continue;
        }
        if (!C->IsChildOf(UFGItemDescriptor::StaticClass()))
        {
            continue;   // 配方产物也可能是建筑描述符等非物品类
        }
        if (C->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
        {
            continue;
        }
        if (C->IsChildOf(UFGBuildingDescriptor::StaticClass()))
        {
            continue;
        }
        if (UFGItemDescriptor::GetForm(C) != EResourceForm::RF_SOLID)
        {
            continue;
        }
        if (UFGItemDescriptor::GetItemName(C).IsEmpty())
        {
            continue;
        }

        const FString ClassName = C->GetName();
        bool bSkip = false;
        for (const TCHAR* Token : SkipNameTokens)
        {
            if (ClassName.Contains(Token))
            {
                bSkip = true;
                break;
            }
        }
        if (bSkip)
        {
            continue;
        }

        Catalog.AddUnique(C);
    }

    // ---- ③ ★1.0.7i 追加「原始资源」（用户诉求：下载站补充缺失的原矿条目，例如铁矿石）----
    //
    // 1.0.7e 定的口径是「只列生产栏建筑的**配方产物**」，于是矿石 / 煤 / 硫 / 铝土矿这些
    // 由资源节点**开采**出来、根本不经过配方的东西一条都进不来，玩家在云端下载站里拿不到原矿。
    // 这里单开一路把它们补进目录。
    //
    // 判定用 UFGResourceDescriptor（Resources/FGResourceDescriptor.h:13，Abstract 基类；
    // Desc_OreIron_C / Desc_Coal_C / Desc_Sulfur_C / Desc_RawQuartz_C … 全在它支系下），
    // 不靠物品名或图标猜。与 ② 的唯一口径差异：**这一路不做解锁过滤** —— 原矿没有配方，
    // 也就没有「哪个里程碑发了它」可言（手挖 / 采矿机随时可采，Coal 之类只是阶段任务要求），
    // 强行套解锁链会把铁矿石也挡在外面，正是用户报的问题。基本卫生过滤照样全过一遍。
    {
        int32 RawAdded = 0;
        for (TObjectIterator<UClass> It; It; ++It)
        {
            UClass* C = *It;
            if (!C || C == UFGResourceDescriptor::StaticClass())
            {
                continue;
            }
            if (!C->IsChildOf(UFGResourceDescriptor::StaticClass()))
            {
                continue;
            }
            if (C->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
            {
                continue;
            }
            if (C->IsChildOf(UFGBuildingDescriptor::StaticClass()))
            {
                continue;
            }
            // 液体/气体资源（原油、水、氮气）走云端流体站，这里只要固体，与 ② 同一口径。
            if (UFGItemDescriptor::GetForm(C) != EResourceForm::RF_SOLID)
            {
                continue;
            }
            if (UFGItemDescriptor::GetItemName(C).IsEmpty())
            {
                continue;
            }
            const int32 Before = Catalog.Num();
            Catalog.AddUnique(C);
            if (Catalog.Num() != Before)
            {
                ++RawAdded;
                // ★1.0.7k 原矿没有配方 ⇒ 走资源阶段表。② 里可能已给它记过配方阶段（极少：矿渣之类），
                // 以资源表为准（原矿的真实获取阶段就是「能采它的阶段」，不是某个产矿配方）。
                TierByItem.Add(C, ISEL_ResourceTierByClassName(C->GetName()));
            }
        }
        WT_WARNF(TEXT("[item-panel] raw resources appended: %d (catalog now %d)"),
                 RawAdded, Catalog.Num());
    }

    // ---- ②b ★1.0.7r 阶段兜底：只靠 MAM / 替代配方解锁、而来源又没写阶段的物品 ----
    // 口径（用户「配方要写到适合的阶段，哪怕是替代配方」）：
    //   **能造它 = 至少已经能拿到它的原料** ⇒ 取该配方所有原料里最高的阶段当它的阶段。
    //   例：某替代配方专属产物要「钢板(T3)+塑料(T5)」⇒ 归 T5，而不是 0（旧 bug）也不是 99。
    // 只处理 ② 里拿不到阶段的物品；已有阶段的一个都不动（里程碑口径永远优先）。
    // 放在 ③ 之后：原矿的阶段那时才由资源表填好，否则「矿石类原料」会被当成未知而漏算。
    if (PendingTierByIngredients.Num() > 0)
    {
        int32 Estimated = 0;
        for (const FISEL_PendingTier& Pending : PendingTierByIngredients)
        {
            UClass* Item = Pending.Item;
            if (!Item || TierByItem.Contains(Item))
            {
                continue;   // 同一物品的另一个配方已经给出可信阶段
            }
            const TArray<FItemAmount> Ingredients =
                UFGRecipe::GetIngredients(this, TSubclassOf<UFGRecipe>(Pending.Recipe));
            int32 NeedTier = -1;
            for (const FItemAmount& Ing : Ingredients)
            {
                UClass* IngCls = Ing.ItemClass.Get();
                const int32* IngTier = IngCls ? TierByItem.Find(IngCls) : nullptr;
                if (IngTier && *IngTier < 99)
                {
                    NeedTier = FMath::Max(NeedTier, *IngTier);
                }
            }
            if (NeedTier >= 0)
            {
                TierByItem.Add(Item, NeedTier);
                ++Estimated;
            }
        }
        WT_WARNF(TEXT("[item-panel] tier fallback by ingredients: %d of %d unresolved items"),
                 Estimated, PendingTierByIngredients.Num());
    }

    // ★1.0.7r 阶段分布自证：一条日志回答「T0 怎么会有 50 个」。
    //   正常分布应当两头小、中间鼓（T0~T2 是起步物品、T7~T9 是终局物品）；
    //   若某档异常膨胀，看下面 ②b 的 fallback 计数与前面的 milestone 统计即可定位。
    {
        TMap<int32, int32> Dist;
        for (const TSubclassOf<UFGItemDescriptor>& C : Catalog)
        {
            Dist.FindOrAdd(GetItemTier(C.Get()))++;
        }
        TArray<int32> Keys;
        Dist.GetKeys(Keys);
        Keys.Sort();
        FString Line;
        for (const int32 Key : Keys)
        {
            Line += FString::Printf(TEXT("T%d=%d "), Key, Dist[Key]);
        }
        WT_WARNF(TEXT("[item-panel] tier distribution: %s(milestone=%d infoOnly=%d of %d recipes)"),
                 *Line, TierByRecipeMilestone.Num(), TierByRecipeInfoOnly.Num(),
                 UnlockedRecipes.Num());
    }

    // 稳定顺序：**先按里程碑阶段**（用户诉求「里程碑顺序排列」），同阶段再按显示名、
    // 同名再按类名 —— 面板每次打开顺序一致，且玩家是按「我现在在哪个阶段」来找东西的。
    // 未归类（99）自然沉底。
    Catalog.Sort([this](const TSubclassOf<UFGItemDescriptor>& A, const TSubclassOf<UFGItemDescriptor>& B)
    {
        const int32 TierA = GetItemTier(A.Get());
        const int32 TierB = GetItemTier(B.Get());
        if (TierA != TierB)
        {
            return TierA < TierB;
        }

        const FString AN = UFGItemDescriptor::GetItemName(A).ToString();
        const FString BN = UFGItemDescriptor::GetItemName(B).ToString();
        const int32 Cmp = AN.Compare(BN, ESearchCase::IgnoreCase);
        if (Cmp != 0)
        {
            return Cmp < 0;
        }
        return A->GetName().Compare(B->GetName(), ESearchCase::IgnoreCase) < 0;
    });

    // ★1.0.7k 目录变了 ⇒ 阶段下拉的选项集合也变了，必须同步重建（否则选中的阶段可能已经没数据）。
    RebuildMilestoneFilter();
    // ★1.0.7p 同理：物品下拉的候选集合同样来自目录（且叠加阶段），一并重建。
    RebuildItemFilter();

    // ★Warning 级（WT_LOGF 被编译期剔除，Shipping 日志里看不到）—— 目录规模是排查「列表是空的」
    // 的第一手数据。同时打印进度签名，便于确认「升阶段后目录有没有变多」。
    WT_WARNF(TEXT("[item-panel] catalog built: %d producible solid items | unlockedSources=%d highestTier=%d (milestone/MAM/alternate only)."),
             Catalog.Num(), UnlockedSourceCount, HighestTier);
}

// ----------------------------------------------------------------------------
// ★1.0.7k 里程碑阶段过滤（用户诉求「云端加上面板过滤筛选条件」+「里程碑顺序排列」）
// ----------------------------------------------------------------------------

int32 UWTItemSelectPanelWidget::GetItemTier(UClass* ItemClass) const
{
    if (!ItemClass)
    {
        return 99;
    }
    if (const int32* Found = TierByItem.Find(ItemClass))
    {
        return *Found;
    }
    return 99;   // 查不到 = 未归类（排序沉底、只在「全部」里出现）
}

bool UWTItemSelectPanelWidget::PassesMilestoneFilter(UClass* ItemClass) const
{
    // -1 = 不筛选（默认）。这是向后兼容底线：没选阶段就当没这个维度。
    if (SelectedMilestone < 0)
    {
        return true;
    }
    return GetItemTier(ItemClass) == SelectedMilestone;
}

TArray<int32> UWTItemSelectPanelWidget::CollectCatalogTiers() const
{
    // 只列「当前目录里实际出现过」的阶段，不列全游戏表 —— 否则下拉里一堆点了没结果的档位。
    TSet<int32> Seen;
    for (const TSubclassOf<UFGItemDescriptor>& C : Catalog)
    {
        Seen.Add(GetItemTier(C.Get()));
    }
    TArray<int32> Out = Seen.Array();
    Out.Sort();          // 升序；99（未归类）自然沉底
    return Out;
}

void UWTItemSelectPanelWidget::RebuildMilestoneFilter()
{
    if (!MilestoneCombo)
    {
        return;   // 目录构建早于 UI（或 UI 还没建）—— 由 NativeConstruct 再补一次
    }

    // 重建期间挡住回调：ClearOptions / SetSelectedIndex 都会广播 OnSelectionChanged，
    // 不挡就会在「选项只填了一半」时先跑一次过滤，读到半成品状态。
    TGuardValue<bool> RebuildGuard(bRebuildingMilestoneFilter, true);

    MilestoneOptionKeys.Reset();
    MilestoneCombo->ClearOptions();

    // 第 0 项固定「全部」，带上总条目数 —— 玩家一眼知道放开后有多少行。
    MilestoneOptionKeys.Add(-1);
    MilestoneCombo->AddOption(FText::Format(
        WT_LOCTEXT("ItemPanel_TierAllFmt", "全部（{0}）"),
        FText::AsNumber(Catalog.Num())).ToString());

    for (const int32 Tier : CollectCatalogTiers())
    {
        int32 Count = 0;
        for (const TSubclassOf<UFGItemDescriptor>& C : Catalog)
        {
            if (GetItemTier(C.Get()) == Tier)
            {
                ++Count;
            }
        }
        MilestoneOptionKeys.Add(Tier);

        const FText Label = (Tier == 99)
            ? FText::Format(WT_LOCTEXT("ItemPanel_TierUnknownFmt", "未归类（{0}）"),
                            FText::AsNumber(Count))
            : FText::Format(WT_LOCTEXT("ItemPanel_TierFmt", "T{0}（{1}）"),
                            FText::AsNumber(Tier), FText::AsNumber(Count));
        MilestoneCombo->AddOption(Label.ToString());
    }

    // 恢复重建前的选择；那个阶段在最新目录里已经不存在 ⇒ 回落到「全部」（状态的唯一真相仍在
    // SelectedMilestone，控件只是视图）。
    int32 DesiredIndex = 0;
    if (SelectedMilestone >= 0)
    {
        const int32 Found = MilestoneOptionKeys.IndexOfByKey(SelectedMilestone);
        if (Found != INDEX_NONE)
        {
            DesiredIndex = Found;
        }
        else
        {
            SelectedMilestone = -1;
        }
    }
    MilestoneCombo->SetSelectedIndex(DesiredIndex);

    // ★1.0.7l 选项自证：catalog=N 但 options 只有 1（只有「全部」）说明阶段表没建起来。
    WT_WARNF(TEXT("[item-panel] milestone options=%d (catalog=%d, tiers=%d) -> index %d"),
             MilestoneCombo->GetOptionCount(), Catalog.Num(),
             MilestoneOptionKeys.Num() - 1, DesiredIndex);
}

void UWTItemSelectPanelWidget::OnMilestoneFilterChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    if (bRebuildingMilestoneFilter)
    {
        return;
    }
    if (SelectionType == ESelectInfo::Direct)
    {
        return;   // 代码 SetSelectedIndex 触发的，不是玩家操作
    }
    if (!MilestoneCombo)
    {
        return;
    }

    const int32 Index = MilestoneCombo->GetSelectedIndex();
    if (!MilestoneOptionKeys.IsValidIndex(Index))
    {
        return;
    }
    const int32 Wanted = MilestoneOptionKeys[Index];
    if (Wanted == SelectedMilestone)
    {
        return;
    }
    SelectedMilestone = Wanted;

    // ★1.0.7p 阶段变了 ⇒ 物品下拉的候选集合也变了（T3 的物品不该出现在 T1 的列表里）。
    //   重建时若原来选的物品已不在新集合里，RebuildItemFilter 会把状态一并回落成「全部」。
    RebuildItemFilter();

    // 只改行集，不重建目录（目录与阶段无关）。
    RebuildVisible();
    RebuildRows();
    RefreshAmounts();
    RefreshRowStyles();

    WT_WARNF(TEXT("[item-panel] milestone filter <= %d (%s) | rows=%d of catalog=%d"),
             SelectedMilestone, *SelectedItem, Visible.Num(), Catalog.Num());
}

void UWTItemSelectPanelWidget::RebuildItemFilter()
{
    if (!ItemFilterCombo)
    {
        return;   // UI 还没建（目录构建早于 RebuildWidget）—— NativeConstruct 会再补一次
    }

    // 选项 = **当前阶段过滤后**仍在目录里的物品。顺序沿用 Catalog（阶段 → 显示名 → 类名），
    // 所以下拉本身就是「按里程碑顺序排列」的，与列表同序。
    struct FISEL_ItemOption
    {
        FString Key;      // 类名（唯一、稳定）
        FString Label;    // 显示名（玩家看到的）
    };
    TArray<FISEL_ItemOption> Options;
    for (const TSubclassOf<UFGItemDescriptor>& C : Catalog)
    {
        UClass* Cls = C.Get();
        if (!Cls || !PassesMilestoneFilter(Cls))
        {
            continue;
        }
        FISEL_ItemOption Opt;
        Opt.Key   = Cls->GetName();
        Opt.Label = UFGItemDescriptor::GetItemName(C).ToString();
        Options.Add(Opt);
    }

    // 期望的键序列：第 0 项固定「全部」（空串），其后按顺序是各物品类名。
    TArray<FString> WantedKeys;
    WantedKeys.Reserve(Options.Num() + 1);
    WantedKeys.Add(FString());
    for (const FISEL_ItemOption& Opt : Options)
    {
        WantedKeys.Add(Opt.Key);
    }

    int32 DesiredIdx = 0;
    if (!SelectedItemKey.IsEmpty())
    {
        const int32 Found = WantedKeys.IndexOfByKey(SelectedItemKey);
        if (Found != INDEX_NONE)
        {
            DesiredIdx = Found;
        }
        else
        {
            // ★记忆失效兜底：上次筛的物品在当前阶段里已经不存在 ⇒ 把状态清掉。
            //   否则会出现「下拉显示『全部』、列表却仍按旧物品过滤」这种自相矛盾。
            SelectedItemKey.Empty();
        }
    }

    // ★★★1.0.8BJ 选项集合没变 ⇒ **只对齐选中项，绝不 ClearOptions**（用户 2026-10-28
    //   「下拉选择框有点抖动」的主要来源之一）。
    //
    //   `UComboBoxString::ClearOptions()` 会把 SComboBox 的选项表清空并重建 —— 153 个
    //   STextBlock 先全销毁再全新建，中间那一帧下拉的 desired size 会塌、列表内容物化两次。
    //   切换「阶段」时会走到本函数（阶段变了 ⇒ 候选物品确实变了），但**同一个阶段内**
    //   （例如反复点开下拉看一眼再关掉、或者阶段没变只是重进面板）候选集合是一模一样的，
    //   那种情况下重建纯属白闪。
    if (ItemOptionKeys == WantedKeys)
    {
        if (ItemFilterCombo->GetSelectedIndex() != DesiredIdx)
        {
            TGuardValue<bool> RebuildGuard(bRebuildingItemFilter, true);
            ItemFilterCombo->SetSelectedIndex(DesiredIdx);
        }
        return;
    }

    // 重入守卫：ClearOptions / SetSelectedIndex 会广播 OnSelectionChanged。
    TGuardValue<bool> RebuildGuard(bRebuildingItemFilter, true);

    ItemOptionKeys = WantedKeys;
    ItemFilterCombo->ClearOptions();

    // 第 0 项固定「全部」，带上候选条目数 —— 玩家一眼知道放开后有多少可以选择的行。
    ItemFilterCombo->AddOption(FText::Format(
        WT_LOCTEXT("ItemPanel_ItemAllFmt", "全部（{0}）"),
        FText::AsNumber(Options.Num())).ToString());
    for (const FISEL_ItemOption& Opt : Options)
    {
        ItemFilterCombo->AddOption(FText::Format(
            WT_LOCTEXT("ItemPanel_ItemFmt", "{0}"),
            FText::FromString(Opt.Label)).ToString());
    }

    ItemFilterCombo->SetSelectedIndex(DesiredIdx);

    WT_WARNF(TEXT("[item-panel] item filter options=%d (in tier=%d) -> index %d"),
             ItemFilterCombo->GetOptionCount(), Options.Num(), DesiredIdx);
}

bool UWTItemSelectPanelWidget::PassesItemFilter(UClass* ItemClass) const
{
    // 空键 = 不过滤（向后兼容底线：没选物品就当没这个维度）。
    if (SelectedItemKey.IsEmpty())
    {
        return true;
    }
    return ItemClass != nullptr && ItemClass->GetName() == SelectedItemKey;
}

void UWTItemSelectPanelWidget::OnItemFilterChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    if (bRebuildingItemFilter)
    {
        return;
    }
    if (SelectionType == ESelectInfo::Direct)
    {
        return;   // 代码 SetSelectedIndex 触发的，不是玩家操作
    }
    if (!ItemFilterCombo)
    {
        return;
    }

    const int32 Index = ItemFilterCombo->GetSelectedIndex();
    if (!ItemOptionKeys.IsValidIndex(Index))
    {
        return;
    }
    const FString Wanted = ItemOptionKeys[Index];
    if (Wanted == SelectedItemKey)
    {
        return;
    }
    SelectedItemKey = Wanted;

    // 只改行集，不动目录（物品过滤与目录无关）。
    RebuildVisible();
    RebuildRows();
    RefreshAmounts();
    RefreshRowStyles();

    WT_WARNF(TEXT("[item-panel] item filter <= '%s' (%s) | rows=%d of catalog=%d"),
             SelectedItemKey.IsEmpty() ? TEXT("<all>") : *SelectedItemKey,
             *SelectedItem, Visible.Num(), Catalog.Num());
}

bool UWTItemSelectPanelWidget::MatchesSearch(const FText& ItemName) const
{
    if (SearchText.IsEmpty())
    {
        return true;
    }
    return ItemName.ToString().Contains(SearchText, ESearchCase::IgnoreCase);
}

void UWTItemSelectPanelWidget::RebuildVisible()
{
    Visible.Reset();

    // ★1.0.7s 用户 2026-10-27 定案「全部显示出来，别隐藏部分文字，所有类目的全部都是」：
    //   **撤掉 1.0.7f 的「选中一种后其余全部隐藏」**。那张截图的矛盾就是这条造成的 ——
    //   下拉写着「全部（126）」（= 目录），列表却只剩铁矿石 1 行（= Visible 被砍成 1）。
    //   现在选中只影响「下载哪几种」（白名单仍是单选，由建筑的 mOutputFilter 决定），
    //   **不影响列表内容**：想换一种，直接点另一行的【选择】即可，不必先点【更换】。
    //
    // 本函数的产物始终是「目录 ∩ 阶段 ∩ 物品 ∩ 搜索词」，与选中态、云端库存都无关 ——
    //   行集因此稳定（不会每 0.5s 因库存波动而闪烁）；云端 0 的行由 ShouldShowRow 折叠掉。
    for (const TSubclassOf<UFGItemDescriptor>& C : Catalog)
    {
        // ★1.0.7k 阶段过滤先过一道（不通过就不必再查搜索词，也省一次 GetItemName）。
        if (!PassesMilestoneFilter(C.Get()))
        {
            continue;
        }
        // ★1.0.7p 物品过滤（「在阶段后面加个物品」那一维）。
        if (!PassesItemFilter(C.Get()))
        {
            continue;
        }
        if (MatchesSearch(UFGItemDescriptor::GetItemName(C)))
        {
            Visible.Add(C);
        }
    }
}

void UWTItemSelectPanelWidget::RebuildRows()
{
    if (!ItemListBox)
    {
        return;
    }
    ItemListBox->ClearChildren();
    RowRoots.Reset();
    RowActionButtons.Reset();
    RowActionLabels.Reset();
    RowLabels.Reset();
    RowAmounts.Reset();
    RowNets.Reset();
    RowStates.Reset();
    RowRates.Reset();
    RowTakeButtons.Reset();
    RowTakeLabels.Reset();

    UClass* Picked = GetSelectedClass();

    for (int32 i = 0; i < Visible.Num(); ++i)
    {
        const TSubclassOf<UFGItemDescriptor> Cls = Visible[i];
        const bool bThisPicked = (Picked && Cls.Get() == Picked);

        UButton* Row = WidgetTree->ConstructWidget<UButton>();
        Row->SetBackgroundColor(bThisPicked ? ISEL_BTN_SEL : ISEL_BTN);

        UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>();

        // ★1.0.7p 用户定案的行布局（2026-10-27），★1.0.7y 按截图红框再调一次：
        //
        //   【图标】【文本】【数量】【——弹——】【正在下载/未下载】【下载速度】【选择/更换】
        //             ↑紧贴文本        ↑吃掉剩余宽度      ↑红框原位
        //
        // ★1.0.7y 变化：数量列从「右对齐 + 96px 固定宽」改成「左对齐 + 自然宽」紧跟物品名，
        //   原来那块（红框）让给状态列。旧版数量贴在行右侧，与名称之间是一大片空白，
        //   而「这一种现在到底在不在搬」只能用速度列的「--」间接猜。
        // 弹性列只剩中间的空柱 ⇒ 状态/速度/按钮三列仍严格右对齐。

        // ---- 列①：图标 ----------------------------------------------------------
        // 显式 FSlateBrush + SetImageSize（不靠贴图原生尺寸，否则巨型图标把行撑爆 ——
        // 这是 Remote_FactoryPanelWidget 1.0.28c 踩过的坑）。
        UImage* Icon = WidgetTree->ConstructWidget<UImage>();
        {
            FSlateBrush Brush;
            Brush.SetImageSize(FVector2D(26.f, 26.f));
            if (UTexture2D* Tex = UFGItemDescriptor::GetSmallIcon(Cls))
            {
                Brush.SetResourceObject(Tex);
            }
            Icon->SetBrush(Brush);
        }
        if (UHorizontalBoxSlot* S = Line->AddChildToHorizontalBox(Icon))
        {
            S->SetPadding(FMargin(10.f, 5.f, 8.f, 5.f));
            S->SetVerticalAlignment(VAlign_Center);
            S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
            S->SetHorizontalAlignment(HAlign_Left);
        }

        // ---- 列②：物品名（左对齐，自然宽 —— ★1.0.7y 起不再是弹性列）------------------
        // 用户 2026-10-27 定案：「数量往前挪」⇒ 数量必须紧贴名称。而只要名称列还吃 Fill，
        // 数量就会被一路推到行右侧（弹性列会把剩余宽度全吞掉），所以弹性让给列③b 的空柱，
        // 名称只按自己的自然宽排版。
        UTextBlock* NameLabel = ISEL_MakeText(WidgetTree, UFGItemDescriptor::GetItemName(Cls),
                                              13, false, ISEL_VALUE, ETextJustify::Left);
        if (UHorizontalBoxSlot* S = Line->AddChildToHorizontalBox(NameLabel))
        {
            S->SetPadding(FMargin(0.f, 4.f, 4.f, 4.f));
            S->SetVerticalAlignment(VAlign_Center);
            S->SetHorizontalAlignment(HAlign_Left);
            S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
        }

        // ---- 列③：云端数量（★1.0.7y 前移到名称右侧，左对齐、自然宽）------------------
        UTextBlock* AmountLabel = ISEL_MakeText(WidgetTree, FText::GetEmpty(), 12, false,
                                                ISEL_DIM, ETextJustify::Left);
        if (UHorizontalBoxSlot* S = Line->AddChildToHorizontalBox(AmountLabel))
        {
            S->SetPadding(FMargin(12.f, 4.f, 0.f, 4.f));
            S->SetVerticalAlignment(VAlign_Center);
            S->SetHorizontalAlignment(HAlign_Left);
            S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
        }

        // ---- 列③c：云端**净变化**（★1.0.8BI 新增，紧跟数量列）-----------------------
        // 用户 2026-10-28「同一个物品，一边上传，一边下载」的第二个答案：
        //   一个上传站往池里灌 +60/分、一个下载站从池里抽 −60/分 ⇒ 数量列那个数字**不动**。
        //   数量列只说明"现在有多少"，说明不了"它为什么不变"。这里把**净变化**直接写出来：
        //     `+60/分` 红 = 池子在涨（上传更快）   `−60/分` 绿 = 池子在跌（下载更快）
        //     `0`       灰 = 真的收支平衡（两边速度相当，或都没动）
        // ★口径是**整个云端池**的变化（含别的站、含原版维度仓库的手动存取），不是本机的 ——
        //   所以文本带一个「净」字，避免被读成"这台机器搬了多少"（那由速度列回答）。
        // ★数据来自 RefreshAmounts 对池总量的差分采样（0.4s 节流），不是站体读数：
        //   池是全局的，只有差分Global快照才能得到真实净值。
        UTextBlock* NetLabel = ISEL_MakeText(WidgetTree, FText::GetEmpty(), 12, false,
                                             ISEL_DIM, ETextJustify::Right);
        if (UHorizontalBoxSlot* S = Line->AddChildToHorizontalBox(NetLabel))
        {
            S->SetPadding(FMargin(8.f, 4.f, 0.f, 4.f));
            S->SetVerticalAlignment(VAlign_Center);
            S->SetHorizontalAlignment(HAlign_Right);
            S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
            NetLabel->SetMinDesiredWidth(ISEL_COL_NET);
        }

        // ---- 列③b：弹性空柱（★1.0.7y 新增）------------------------------------------
        // 唯一吃剩余宽度的一列，把「状态 / 速度 / 按钮」三列整体顶到行右侧贴齐，
        // 同时保证数量列停在名称右侧。空 TextBlock 的 desired size 是 0，不占视觉空间。
        {
            UTextBlock* Flex = WidgetTree->ConstructWidget<UTextBlock>();
            if (UHorizontalBoxSlot* S = Line->AddChildToHorizontalBox(Flex))
            {
                S->SetPadding(FMargin(0.f));
                S->SetVerticalAlignment(VAlign_Center);
                S->SetHorizontalAlignment(HAlign_Fill);
                S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            }
        }

        // ---- 列④：下载状态（★1.0.7y 新增，占用原「云端数量」列所在的右侧位置）--------
        // 用户定案：红框那块位置不写数量，改写【正在下载 / 未下载】——「云端 1,400」这种
        // 静态库存一眼就能看到，但「这一种现在到底在不在搬」以前只能靠速度列的「--」猜。
        // 判定与速度列同源（GetItemRatesPerMinute 的 5 秒窗口，>0 即在搬），只是措辞更直白。
        //
        // ★★1.0.8BI 四站**全建**状态列与速度列（1.0.8BG 那条「上传站两列都不建」作废）。
        //   作废理由（用户 2026-10-28 原话「同一个物品，一边上传，一边下载，这个UI你要怎么改」）：
        //   上传站与下载站共用**同一个**云端池（AFGCentralStorageSubsystem::mStoredItems），
        //   池里那个数字是两边相抵后的**净值** —— 一边传 60/分、一边取 60/分时它一动不动，
        //   玩家会以为两台机器都没干活。只有让每台机器报出**自己的方向与流量**，
        //   才能解释"数字为什么不动"。1.0.8BG 删这两列时上传站确实没有可执行动作、
        //   也没有速率接口；现在两个前提都不成立了（行尾有【转移】，且站体已加采样）。
        UTextBlock* StateLabel = ISEL_MakeText(WidgetTree,
                                   bUploaderMode ? WT_LOCTEXT("ItemPanel_RowStateIdleUp", "未上传")
                                                 : WT_LOCTEXT("ItemPanel_RowStateIdle", "未下载"),
                                   12, false, ISEL_DIM, ETextJustify::Right);
        if (UHorizontalBoxSlot* S = Line->AddChildToHorizontalBox(StateLabel))
        {
            S->SetPadding(FMargin(0.f, 4.f, 10.f, 4.f));
            S->SetVerticalAlignment(VAlign_Center);
            S->SetHorizontalAlignment(HAlign_Right);
            S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
            StateLabel->SetMinDesiredWidth(ISEL_COL_STATE);
        }

        // ---- 列⑤：该物品的**本机**速度（方向由箭头表达：↑上传 / ↓下载）------------
        // 数值由 RefreshRowStyles 每 0.5s 从 GetItemRatesPerMinute() 取；没在搬这一类时「--」。
        // ★上传站这个读数是 1.0.8BI 新加的（站体此前完全没有速率采样，见 AWTUpgradeStorage
        //   ::GetItemRatesPerMinute 的注释）。
        UTextBlock* RateLabel = ISEL_MakeText(WidgetTree, WT_LOCTEXT("ItemPanel_RowRateNone", "--"),
                                  12, false, ISEL_DIM, ETextJustify::Right);
        if (UHorizontalBoxSlot* S = Line->AddChildToHorizontalBox(RateLabel))
        {
            S->SetPadding(FMargin(0.f, 4.f, 8.f, 4.f));
            S->SetVerticalAlignment(VAlign_Center);
            S->SetHorizontalAlignment(HAlign_Right);
            S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
            RateLabel->SetMinDesiredWidth(ISEL_COL_RATE);
        }

        // ---- 列⑥：行尾唯一按钮（★1.0.8 双态：未选中【选择】/ 选中【取消】）------------
        // 用户 2026-10-27 直接问「为什么不能在点一下取消？」⇒ 选中态**不再**把按钮隐藏成
        //   【✔ 已选】这种只能看的死标记，而是就地变成【取消】：同一个位置、同一颗按钮，
        //   再点一下 = 解除选择（机器回到「不下载」）。「已选」这件事由整行底色高亮承担 ——
        //   同一句话不该用两件控件说两遍，而"能点一下取消"这件事只能由按钮说。
        // 上传站模式（bUploaderMode）：整列不建 —— 上传站没有"输出物品"这回事。
        UButton*   ActionButton = nullptr;
        UTextBlock* ActionLabel = nullptr;
        if (!bUploaderMode)
        {
            ActionButton = WidgetTree->ConstructWidget<UButton>();
            ActionButton->SetBackgroundColor(bThisPicked ? ISEL_BTN_CANCEL : ISEL_BTN_SEL);
            ActionLabel = ISEL_MakeText(WidgetTree,
                bThisPicked ? WT_LOCTEXT("Btn_CancelPick", "取消")
                            : WT_LOCTEXT("Btn_Pick", "选择"),
                12, false, bThisPicked ? ISEL_ON_CANCEL : ISEL_ON_SEL, ETextJustify::Center);
            // ★最小宽必须在 AddChild **之后**设：SetMinDesiredWidth 只在底层 STextBlock 已存在时
            // 才真正落到 Slate 上（TextBlock.cpp:223-230 `if (MyTextBlock.IsValid())`），
            // 而 STextBlock 是在 AddChild → TakeWidget 时才建的 —— 顺序反了就是静默无效（宽度不生效）。
            if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(ActionButton->AddChild(ActionLabel)))
            {
                ButtonSlot->SetHorizontalAlignment(HAlign_Fill);
                ButtonSlot->SetVerticalAlignment(VAlign_Center);
                ButtonSlot->SetPadding(FMargin(4.f, 2.f));
            }
            ActionLabel->SetMinDesiredWidth(ISEL_COL_ACTION);
            if (UHorizontalBoxSlot* S = Line->AddChildToHorizontalBox(ActionButton))
            {
                S->SetPadding(FMargin(8.f, 2.f, 8.f, 2.f));
                S->SetVerticalAlignment(VAlign_Center);
                S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
                S->SetHorizontalAlignment(HAlign_Right);
            }
        }

        // ---- 列⑦：★1.0.8BG 行内【转移 / 清空】按钮（本轮用户定案的核心）---------------
        // 用户 2026-10-28：「不要选择清空全部，选择单一的」「每一行不是一个物品吗，
        //   在后面增加一个清空/转移等等，选择转移的时候，背包 UI 或者其他 UI 要打开，方便转移」。
        //   · 作用范围 = **这一行的物品**，点一下只动这一种（标题行那颗批量按钮已整颗删除）；
        //   · 固体【转移】= 把这一类云端存量整批搬进玩家背包（货还在，走 MoveItemFromCentralStorage）；
        //   · 液体【清空】= 把这一类从云端池销毁（液体进不了背包，用户已确认"直接清空"）；
        //   · 云端数量 0 的行按钮禁用（点了也没货，不给假反馈）；
        //   · 上传站也建这一列（1.0.7z 的"上传站整列不建"作废：玩家清理积压就靠它）。
        UButton*    TakeButton = nullptr;
        UTextBlock* TakeLabel  = nullptr;
        {
            TakeButton = WidgetTree->ConstructWidget<UButton>();
            TakeButton->SetBackgroundColor(ISEL_BTN_TAKE);
            TakeLabel = ISEL_MakeText(WidgetTree,
                WT_LOCTEXT("Btn_TakeRow", "转移"),
                12, false, ISEL_ON_SEL, ETextJustify::Center);
            if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(TakeButton->AddChild(TakeLabel)))
            {
                ButtonSlot->SetHorizontalAlignment(HAlign_Fill);
                ButtonSlot->SetVerticalAlignment(VAlign_Center);
                ButtonSlot->SetPadding(FMargin(4.f, 2.f));
            }
            TakeLabel->SetMinDesiredWidth(ISEL_COL_ACTION);
            if (UHorizontalBoxSlot* S = Line->AddChildToHorizontalBox(TakeButton))
            {
                // 右边距 8 与列⑥一致 ⇒ 两颗按钮贴到行尾、间距均匀，不会一个顶到边框。
                S->SetPadding(FMargin(0.f, 2.f, 8.f, 2.f));
                S->SetVerticalAlignment(VAlign_Center);
                S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
                S->SetHorizontalAlignment(HAlign_Right);
            }
        }

        // ★1.0.7u 这一句是「五列全挤在行中间」的真凶，必须显式设 HAlign_Fill：
        //   `UButtonSlot` 的构造函数默认 `HorizontalAlignment = HAlign_Center`
        //   （ButtonSlot.cpp 里 UButtonSlot::UButtonSlot 只给默认值、不跟随父级），
        //   所以 `Row->AddChild(Line)` 得到的 HorizontalBox 只按**内容自然宽**排版、
        //   然后整块居中 ⇒ 实测图标离左边框 165px、按钮右侧留白 185px（用户截图）。
        //   里面那个 Fill 列（名称）此时毫无意义：Fill 只会在父级真的给足宽度时才生效，
        //   而父级（Line 本身）宽度被算成"内容宽"，没有剩余宽度可分配。
        //   撑满之后才是用户红字标注要的样子：图标贴最左 → 图标文本 → 云端数量 →
        //   下载速度 → 【更换/选择】贴最右。
        if (UButtonSlot* RowSlot = Cast<UButtonSlot>(Row->AddChild(Line)))
        {
            RowSlot->SetHorizontalAlignment(HAlign_Fill);
            RowSlot->SetVerticalAlignment(VAlign_Center);
            RowSlot->SetPadding(FMargin(0.f));
        }

        // UMG 的 OnClicked 是动态多播、带不了参数 —— 直达底层 Slate SButton 装闭包
        // （行序号按值捕获；与 Remote_FactoryPanelWidget 同一手法）。
        const int32 RowIndex = i;

        Row->TakeWidget();
        if (TSharedPtr<SWidget> Cached = Row->GetCachedWidget())
        {
            if (TSharedPtr<SButton> SlateButton = StaticCastSharedPtr<SButton>(Cached))
            {
                // 点行本身与点行尾按钮走**同一个**入口（★1.0.8：两处行为完全一致，
                //   「点一下选中、再点一下取消」在行的任何位置都成立）。
                SlateButton->SetOnClicked(FOnClicked::CreateLambda([this, RowIndex]() -> FReply
                {
                    OnRowAction(RowIndex);
                    return FReply::Handled();
                }));
            }
        }

        // 右侧按钮：**同一个**按钮承担【选择】与【取消】两种身份，闭包只有一个入口，
        //   当前该显示哪种、点了做什么，全由 OnRowAction 按「是否已选中」分流。
        if (ActionButton)
        {
            ActionButton->TakeWidget();
            if (TSharedPtr<SWidget> Cached = ActionButton->GetCachedWidget())
            {
                if (TSharedPtr<SButton> SlateButton = StaticCastSharedPtr<SButton>(Cached))
                {
                    SlateButton->SetOnClicked(FOnClicked::CreateLambda([this, RowIndex]() -> FReply
                    {
                        OnRowAction(RowIndex);
                        return FReply::Handled();
                    }));
                }
            }
            // 恒可见：选中态靠【取消】文案 + 暗红底色表达，按钮**不许消失**（否则没东西可点）。
            ActionButton->SetVisibility(ESlateVisibility::Visible);
        }

        // ★1.0.8BG 列⑦的行内【转移/清空】：与列⑥同样装 Slate 闭包（UMG 的 OnClicked 带不了
        //   行下标）。它**不**复用 OnRowAction —— 那颗按钮改的是"下载哪几种"（建筑白名单），
        //   这颗动的是云端存货（液体还不可逆），两条语义必须分开。
        if (TakeButton)
        {
            TakeButton->TakeWidget();
            if (TSharedPtr<SWidget> Cached = TakeButton->GetCachedWidget())
            {
                if (TSharedPtr<SButton> SlateButton = StaticCastSharedPtr<SButton>(Cached))
                {
                    SlateButton->SetOnClicked(FOnClicked::CreateLambda([this, RowIndex]() -> FReply
                    {
                        OnRowTakeClicked(RowIndex);
                        return FReply::Handled();
                    }));
                }
            }
            TakeButton->SetVisibility(ESlateVisibility::Visible);
        }

        ItemListBox->AddChild(Row);
        RowRoots.Add(Row);
        RowActionButtons.Add(ActionButton);
        RowActionLabels.Add(ActionLabel);
        RowLabels.Add(NameLabel);
        RowAmounts.Add(AmountLabel);
        RowNets.Add(NetLabel);
        RowStates.Add(StateLabel);
        RowRates.Add(RateLabel);
        RowTakeButtons.Add(TakeButton);
        RowTakeLabels.Add(TakeLabel);
    }

    // 状态行的文案统一由 RefreshRowStyles 负责（NativeConstruct / 点击 / 搜索 / 定时刷新都走它），
    // 这里不再另写一份，免得两处文案打架。
}

void UWTItemSelectPanelWidget::RefreshAmounts()
{
    AmountCache.Reset();
    AmountCache.SetNumZeroed(Visible.Num());
    if (Visible.Num() == 0)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    // 一次遍历算出「每类 = 原版池 + 我们各上传站账本」，不按行重复遍历世界。
    TMap<UClass*, int32> Totals;
    if (AFGCentralStorageSubsystem* Sub = AFGCentralStorageSubsystem::Get(World))
    {
        TArray<FItemAmount> CloudItems;
        Sub->GetAllItemsFromCentralStorage(CloudItems);
        for (const FItemAmount& IA : CloudItems)
        {
            if (UClass* Cls = IA.ItemClass.Get())
            {
                if (IA.Amount > 0)
                {
                    Totals.FindOrAdd(Cls) += IA.Amount;
                }
            }
        }
    }
    for (TActorIterator<AWTUpgradeStorage> It(World); It; ++It)
    {
        const AWTUpgradeStorage* Station = *It;
        if (!Station)
        {
            continue;
        }
        for (const FItemAmount& IA : Station->GetLedger())
        {
            if (UClass* Cls = IA.ItemClass.Get())
            {
                if (IA.Amount > 0)
                {
                    Totals.FindOrAdd(Cls) += IA.Amount;
                }
            }
        }
    }

    for (int32 i = 0; i < Visible.Num(); ++i)
    {
        AmountCache[i] = Totals.FindRef(Visible[i].Get());
    }

    // ---- ★1.0.8BI 云端净变化（差分采样）------------------------------------------------
    //
    // 用户 2026-10-28「同一个物品，一边上传，一边下载」：两台机器的流量相抵，数量列不动。
    // 这一列要说清"它为什么不动"，唯一可信的算法就是**对池子总量做差分** ——
    //   站体速率只覆盖自己那台，相抵后的净值只有全局快照能算出来。
    //
    // 细节：
    //   · 第一拍只记基线、不出读数（没有前值，任何速度都是编的）；
    //   · ≥0.4s 才更新一次：RefreshAmounts 会被"用户点了按钮"这类事件额外触发，
    //     不节流的话 Dt 会小到毫秒级，读数直接爆炸（1 件 / 0.01s = 6000/分）；
    //   · 只算 `Visible` 里的类（行显示用），不跟踪全目录，避免每帧几百次 TMap 操作；
    //   · 口径与 AmountCache 完全一致（原版池 + 各站账本），否则"数量列 1000、净值却不是 1000 的变化"很难解释。
    const double NowD = World->GetTimeSeconds();
    if (!bPoolBaselineReady)
    {
        PrevPoolTotals = Totals;
        PrevPoolSampleTime = NowD;
        bPoolBaselineReady = true;
    }
    else
    {
        const double DtD = NowD - PrevPoolSampleTime;
        // ★1.0.8BJ 门槛从 0.4s 提到 1.0s（配合 0.30s 的刷新节拍，实际跨度约 1.2s）。
        //
        // 为什么不是"越快越好"：净值是**整数件**的差分 —— 池里有 1400 件、流量 240/分（4 件/秒），
        // 0.6s 跨度只走了 2~3 件，量化误差就是 ±100/分，读数会在 200/300 之间乱跳，
        // 比"慢一点但稳"更难读。1.2s 跨度 ⇒ 量子 50/分，240/分 的信号一眼可辨。
        // 方向（涨/跌）从第一拍就正确，玩家真正要看的是方向，不是小数。
        if (DtD >= 1.0)
        {
            NetRateCache.Reset();
            for (const TSubclassOf<UFGItemDescriptor>& RowCls : Visible)
            {
                UClass* Cls = RowCls.Get();
                if (!Cls)
                {
                    continue;
                }
                const int32 Cur  = Totals.FindRef(Cls);
                const int32 Prev = PrevPoolTotals.FindRef(Cls);
                if (Cur == Prev)
                {
                    continue;   // 没变化就不进表，读的时候 FindRef 得 0 = 「净 0」
                }
                NetRateCache.Add(Cls, static_cast<float>(Cur - Prev) * 60.f / static_cast<float>(DtD));
            }
            PrevPoolTotals = MoveTemp(Totals);
            PrevPoolSampleTime = NowD;
            // ★1.0.8BJ 从这一拍起，净值列才有资格显示「净 0」——之前一律「净 --」（没测到）。
            bNetMeasured = true;
        }
    }
}

bool UWTItemSelectPanelWidget::ShouldShowRow(int32 VisibleIndex, UClass* Picked) const
{
    if (!Visible.IsValidIndex(VisibleIndex))
    {
        return false;
    }

    // ★★★1.0.8BC 用户 2026-10-28 定案：「UI 中不再显示当前列表，输入什么就显示什么」。
    //
    //   这个函数从"要不要列这一行"变成了严格的二分：
    //     · 玩家**没**表达要看什么（搜索框空 + 物品下拉未选）⇒ 只留**机器当前选中的那一行**；
    //     · 玩家**表达了**（搜索词或下拉非空）⇒ 剩下的行**全显示**，因为 `RebuildVisible`
    //       已经用 `MatchesSearch` 把不匹配的剔出 `Visible` 了。这里再按"云端有没有货"折叠
    //       就是二次过滤 —— 那会让玩家搜了某个物品却看不到它（云端 0 时），正是本轮要消灭的观感。
    //
    //   历史（保留裁决理由，别回退）：
    //     · 1.0.7t：固体侧折叠「云端 0」行（用户 2026-10-27 看到满屏 0/-- 后亲自否决"一律显示"）；
    //     · 1.0.8AR：液体侧改回**一律显示**（能接管道的流体/气体全版本只有 15 种，一屏能看完，
    //       且新存档云端空时列表不该是空的）；
    //     · 1.0.8BC（本轮）：上面两版都建立在"打开就先列一批"的前提上。用户实际用下来要的是
    //       搜索驱动 —— 不输入就不列，输入什么就显示什么。前提变了，"云端 0 要不要折叠"
    //       这个问题自然消失：**列表里出现的行，本来就是玩家点名要找的**，如实显示它的云端数量
    //       （哪怕是 0）才叫答所问。
    //
    //   ★选中行例外：无输入时它必须恒显示 —— 那是"这台机器现在在搬什么"的唯一显示处，
    //     也是行尾那颗【取消】唯一能点到的地方。折叠掉就等于让玩家失去撤销入口。
    const bool bHasQuery = !SearchText.IsEmpty() || !SelectedItemKey.IsEmpty();
    if (!bHasQuery)
    {
        UClass* RowClass = Visible[VisibleIndex].Get();
        if (Picked != nullptr && RowClass == Picked)
        {
            return true;    // 下载站：机器当前**输出**的那一种（也是行尾【取消】唯一能点到的地方）
        }
        if (IsUploaderKind())
        {
            // 上传站：机器当前**输入**的东西 —— 站内缓冲里实际有的物品类（见 RefreshInboundCache）。
            // ★这是"输入什么就显示什么"的正解（用户 2026-10-28 明确否掉了"搜索框过滤"那条）：
            //   列表内容由**机器实际在收什么**决定，不是由云端总库存、也不是由全目录决定。
            // ★1.0.8BI 再补「本机上一拍还在搬」：站内缓冲被泵空的瞬间（货已进云、缓冲清零），
            //   InboundCache 会变空 ⇒ 行闪一下消失。速率窗口告诉我们它 5 秒内真的搬过，留住。
            if (!RowClass)
            {
                return false;
            }
            return InboundCache.Contains(RowClass) || LastItemRates.FindRef(RowClass) > 0.f;
        }
        // ★★1.0.8BG 下载站恢复「云端当前有存货就显示」（用户 2026-10-28 原话：
        //   「下载站要显示出当前云端已经有存货的物品」）。
        //   1.0.8BC 曾把这里改成"无输入一律不显示"，玩家打开下载站看到的是空列表 ——
        //   那等于逼他先把物品名打一遍才能看见云端有什么，违背"云端面板"的本意。
        //   现在的口径：**有货就列**（1.0.7h 的原始口径），搜索/物品下拉仍然是可选的收窄手段
        //   （有输入时上面那个 bHasQuery 分支直接放行，由 RebuildVisible 决定行集）。
        // ★1.0.8BI 再加一条「本机正在搬的这一类恒显示」—— 正对用户 2026-10-28 的场景：
        //   同一物品一边上传一边下载时，池里数量会在 0 附近抖动（上传刚补、下载立刻取走），
        //   只看"云端 > 0"会让正在下载的那一行忽隐忽现。速率窗口（5 秒）比数量稳定得多。
        const bool bHasStock = AmountCache.IsValidIndex(VisibleIndex) && AmountCache[VisibleIndex] > 0;
        if (bHasStock)
        {
            return true;
        }
        return RowClass != nullptr && LastItemRates.FindRef(RowClass) > 0.f;
    }

    // 有输入 ⇒ 输入什么就显示什么（匹配已由 RebuildVisible 完成）。
    return true;
}

void UWTItemSelectPanelWidget::RefreshRowStyles()
{
    // ★1.0.8AQ 两种站共用本函数：不再写死 `AWTUpgradeCloudDownloader*`，
    //   否则液体模式下 Downloader 恒 nullptr ⇒ 整片「无目标」文案误报、速率列永远 --。
    // ★1.0.8BD 上传站的"正在输入"缓存必须**先于** ShouldShowRow 更新（ShouldShowRow 是 const，
    //   只读缓存）。本函数由 NativeTick 的 0.5s 节流块 + 用户操作触发，频率够用；
    //   非上传站模式下 RefreshInboundCache 什么都不做（第一层就判断 PanelKind）。
    if (IsUploaderKind())
    {
        RefreshInboundCache();
    }

    const bool bHasTarget    = (PanelKind != EWTCloudPanelKind::None);
    const bool bDownloadSide = (PanelKind == EWTCloudPanelKind::SolidDownloader);
    UClass* Picked = GetSelectedClass();

    // ★1.0.7p 每类物品的速度：**一次算全表**（窗口内最多几个样本，代价可忽略），
    //   行内只做一次 TMap 查表 —— 上百行也不会变成上百次速率计算。
    // ★1.0.8BI 上传侧也要取（此前只有下载侧取，上传站的状态/速度列因此恒为「未下载 / --」）。
    //   两个站的语义随 PanelKind 变：下载 = 搬进缓冲，上传 = 推进云端池。
    TMap<UClass*, float> ItemRates;
    WT_GetItemRates(ItemRates);
    // ★1.0.8BI 存一份给 ShouldShowRow 用（它是 const，拿不到这里的局部变量）。
    LastItemRates = ItemRates;

    // ★★★1.0.7v 「当前输出范围」必须一眼可见；★1.0.7x 语气改成**显式选择制**。
    //   用户两次报障的根子都在状态不可见：第一次是白名单被误清空后机器照搬全部，
    //   第二次是「列表并未选择任何材料，但仍在下载」。现在机器侧已改成
    //   **空名单 = 一件都不搬**（见 AWTUpgradeCloudDownloader::IsOutputAllowed），
    //   所以这里必须把「没选 = 不下载」和「选了哪几种」直接写在标题上，否则玩家会以为机器坏了。
    //   写法：直接挂在标题行**行内后缀**（不新增控件、不占新行、不复活被删掉的状态行），三态：
    //     · 无白名单  → 输出：未选择（不会下载）
    //     · 恰好一种  → 输出：铁矿石
    //     · 多条残留  → 输出：N 种（点该行即收敛为单选）← 老存档多选会让机器搬 N 种，
    //                   这一句把"为什么搬了好几种"直接说给玩家听。
    if (TitleText)
    {
        // ★1.0.8AQ 选择容器是白名单数组（老存档可能残留多项），折算成「选了几种」再拼标题。
        const int32 FilterCount = (GetDownloader() ? GetDownloader()->GetOutputFilter().Num() : 0);

        FText WantedTitle;
        if (!bHasTarget)
        {
            // 被别的建筑打开（或 mInteractObject 还没填）：只能说清「没目标」。
            WantedTitle = WT_LOCTEXT("ItemPanel_Title", "云端存储下载站 · 输出物品");
        }
        else if (!bDownloadSide)
        {
            WantedTitle = WT_LOCTEXT("ItemPanel_TitleUploader", "云端存储上传站 · 云端物品");
        }
        else if (FilterCount == 0)
        {
            WantedTitle = WT_LOCTEXT("ItemPanel_TitleAll", "云端存储下载站 · 输出：未选择（不会下载）");
        }
        else if (FilterCount == 1 && Picked)
        {
            WantedTitle = FText::Format(
                WT_LOCTEXT("ItemPanel_TitleOne", "云端存储下载站 · 输出：{0}"),
                UFGItemDescriptor::GetItemName(Picked));
        }
        else
        {
            WantedTitle = FText::Format(
                WT_LOCTEXT("ItemPanel_TitleMany", "云端存储下载站 · 输出：{0} 种（点该行即收敛为单选）"),
                FText::AsNumber(FilterCount));
        }
        // 只在真的变了才 SetText —— 本函数每 0.5s 跑一次，无条件 SetText 会让 Slate 每拍
        // 重新量一次标题（与行样式刷新同一个变化检测纪律）。
        if (!TitleText->GetText().EqualTo(WantedTitle))
        {
            TitleText->SetText(WantedTitle);
        }
    }

    // 选中集合由建筑的 mOutputFilter 决定（面板只是它的视图，不自己藏一份状态）。
    NumShownRows = 0;
    // ★1.0.7z 循环上界改成 RowLabels（每行必建），不再用 RowActionButtons：
    //   上传站模式下按钮列整列不存在，用它做上界会让整个刷新循环空转（列表一行都不更新）。
    for (int32 i = 0; i < RowLabels.Num(); ++i)
    {
        if (!RowLabels[i])
        {
            continue;
        }
        if (!Visible.IsValidIndex(i))
        {
            continue;
        }

        const TSubclassOf<UFGItemDescriptor> Cls = Visible[i];
        const bool bSelected = (Picked && Cls.Get() == Picked);
        const int32 Amount = AmountCache.IsValidIndex(i) ? AmountCache[i] : 0;

        // ★1.0.8 选中态的视觉表达集中在这里（用户 2026-10-27：「选中后切换为高亮形式显示」，
        //   随后又问「为什么不能在点一下取消？」）：
        //     · 已选行 = 整行底色高亮（这本身就是"已选"标记，不再另设 ✔ 文字标记）
        //     · 行尾按钮 = 未选中【选择】(亮青) / 已选【取消】(暗红)，**恒可见恒可点**
        //     · 文案与字色同一处处理，避免「按钮写取消、字色还是深色」这类两处各写一份的错位
        //   只在真的变了才 Set*（本函数每 0.5s 跑一次，无条件设会让 Slate 白刷）。
        if (RowRoots.IsValidIndex(i) && RowRoots[i])
        {
            const FLinearColor WantedRow = bSelected ? ISEL_BTN_SEL : ISEL_BTN;
            if (!RowRoots[i]->GetBackgroundColor().Equals(WantedRow))
            {
                RowRoots[i]->SetBackgroundColor(WantedRow);
            }
        }
        if (RowActionButtons.IsValidIndex(i) && RowActionButtons[i])
        {
            if (RowActionButtons[i]->GetVisibility() != ESlateVisibility::Visible)
            {
                RowActionButtons[i]->SetVisibility(ESlateVisibility::Visible);
            }
            const FLinearColor WantedBtn = bSelected ? ISEL_BTN_CANCEL : ISEL_BTN_SEL;
            if (!RowActionButtons[i]->GetBackgroundColor().Equals(WantedBtn))
            {
                RowActionButtons[i]->SetBackgroundColor(WantedBtn);
            }
        }
        if (RowActionLabels.IsValidIndex(i) && RowActionLabels[i])
        {
            const FText WantedLabel = bSelected ? WT_LOCTEXT("Btn_CancelPick", "取消")
                                               : WT_LOCTEXT("Btn_Pick", "选择");
            if (!RowActionLabels[i]->GetText().EqualTo(WantedLabel))
            {
                RowActionLabels[i]->SetText(WantedLabel);
            }
            ISEL_SetColorIfChanged(RowActionLabels[i], bSelected ? ISEL_ON_CANCEL : ISEL_ON_SEL);
        }

        // ★1.0.7t 云端没有存货 ⇒ 整行折叠（见 ShouldShowRow）。仍然只在可见性真的变了才
        //   调 SetVisibility —— 每 0.5s 无条件设一次会让 Slate 每帧白刷一遍布局。
        const bool bShow = ShouldShowRow(i, Picked);
        if (RowRoots.IsValidIndex(i) && RowRoots[i])
        {
            const ESlateVisibility Wanted =
                bShow ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
            if (RowRoots[i]->GetVisibility() != Wanted)
            {
                RowRoots[i]->SetVisibility(Wanted);
            }
        }
        if (bShow)
        {
            ++NumShownRows;
        }

        // ★1.0.8BG 行内【转移 / 清空】的可用态：云端 0 ⇒ 禁用 + 压暗。
        //   为什么用 GetIsEnabled() 比对再设：本函数每 0.5s 跑一次，无条件 SetIsEnabled 会让
        //   Slate 每拍重建一次按钮的导航状态（与本文件其它"变了才设"的纪律一致）。
        if (RowTakeButtons.IsValidIndex(i) && RowTakeButtons[i])
        {
            const bool bCanTake = (Amount > 0);
            if (RowTakeButtons[i]->GetIsEnabled() != bCanTake)
            {
                RowTakeButtons[i]->SetIsEnabled(bCanTake);
            }
            const FLinearColor WantedTakeBg = bCanTake ? ISEL_BTN_TAKE : ISEL_BTN;
            if (!RowTakeButtons[i]->GetBackgroundColor().Equals(WantedTakeBg))
            {
                RowTakeButtons[i]->SetBackgroundColor(WantedTakeBg);
            }
        }
        if (RowTakeLabels.IsValidIndex(i) && RowTakeLabels[i])
        {
            // 文案在构建时就按固/液定死了（面板同一时刻只服务一种站，PanelKind 在
            // NativeConstruct 里定死，不会中途变），这里只管字色跟着可用态走。
            ISEL_SetColorIfChanged(RowTakeLabels[i], (Amount > 0) ? ISEL_ON_SEL : ISEL_DIM);
        }

        // 名称列：不加 ✓ 前缀（前缀会让名称整体右移一截，破坏左边界对齐）；
        // 选中态改由整行底色 + 文字色表达。
        if (RowLabels.IsValidIndex(i) && RowLabels[i])
        {
            // ★1.0.8BJ 名称按构建期就是常量，但仍走"变了才写"——面板重开后行会被重建，
            //   走同一条通道可以避免"某天名称需要跟着状态变"时漏掉刷新。
            ISEL_SetTextIfChanged(RowLabels[i], UFGItemDescriptor::GetItemName(Cls));
            // ★1.0.7t 云端 0 的行正常是被 ShouldShowRow 折叠掉的，只有两种残留会走到这里：
            //   ① 选中的那一种已搬空（行必须留着让玩家能点【更换】）
            //   ② 玩家用「物品」下拉明确筛了这一类
            //   两种都压暗名称，防止玩家以为「云端 0 还能搬」。
            ISEL_SetColorIfChanged(RowLabels[i],
                bSelected ? ISEL_ON_SEL : (Amount > 0 ? ISEL_VALUE : ISEL_DIM));
        }

        // 数量列：★1.0.7y 起左对齐、自然宽，紧贴物品名（不再右对齐吃 96px；那块位置让给状态列）。
        if (RowAmounts.IsValidIndex(i) && RowAmounts[i])
        {
            // ★1.0.8BJ 数量是**唯一每拍都在变**的列之一。走"变了才写"不只是省 CPU：
            //   每 0.5s 无条件 SetText 会让整个 ScrollBox 重新量测，滚动条的滑块位置跟着抖。
            ISEL_SetTextIfChanged(RowAmounts[i], Amount > 0
                ? FText::Format(WT_LOCTEXT("ItemPanel_AmountFmt", "云端 {0}"), FText::AsNumber(Amount))
                : WT_LOCTEXT("ItemPanel_AmountZero", "云端 0"));
            ISEL_SetColorIfChanged(RowAmounts[i],
                Amount > 0 ? (bSelected ? ISEL_ON_SEL : ISEL_GREEN) : (bSelected ? ISEL_ON_SEL : ISEL_DIM));
        }

        // ★1.0.8BI 列④：本机状态 —— 上传站【正在上传/未上传】，下载站【正在下载/未下载】。
        //   用户 2026-10-28 场景「同一个物品，一边上传，一边下载」：两台机器共用**同一个**
        //   云端池，池里的数字是两边相抵后的净值；不写明本机方向，玩家就看不出谁在干什么。
        //   判据统一 = **本机** 5 秒窗口内搬运速率 > 0（与速度列同源，绝不出现"正在下载配 --"）。
        const float* FoundRate = ItemRates.Find(Cls.Get());
        const int32 PerMin = FoundRate ? FMath::RoundToInt(*FoundRate) : 0;
        const bool bUploaderRow = IsUploaderKind();

        if (RowStates.IsValidIndex(i) && RowStates[i])
        {
            const FText WantedState = bUploaderRow
                ? (PerMin > 0 ? WT_LOCTEXT("ItemPanel_RowStateBusyUp", "正在上传")
                              : WT_LOCTEXT("ItemPanel_RowStateIdleUp", "未上传"))
                : (PerMin > 0 ? WT_LOCTEXT("ItemPanel_RowStateBusy", "正在下载")
                              : WT_LOCTEXT("ItemPanel_RowStateIdle", "未下载"));
            ISEL_SetTextIfChanged(RowStates[i], WantedState);
            ISEL_SetColorIfChanged(RowStates[i],
                bSelected ? ISEL_ON_SEL : (PerMin > 0 ? ISEL_GREEN : ISEL_DIM));
        }

        // ★1.0.8BI 列⑤：本机速度，**箭头即方向**（↑ 上传 / ↓ 下载）。
        //   与下面净值列的 `净+`/`净-` 分工：这一列答"我这台在搬多少"，那一列答"池子整体在涨还是跌"。
        if (RowRates.IsValidIndex(i) && RowRates[i])
        {
            if (PerMin > 0)
            {
                ISEL_SetTextIfChanged(RowRates[i], FText::Format(
                    bUploaderRow ? WT_LOCTEXT("ItemPanel_RowRateUpFmt", "↑ {0}/分")
                                 : WT_LOCTEXT("ItemPanel_RowRateDownFmt", "↓ {0}/分"),
                    FText::AsNumber(PerMin)));
                ISEL_SetColorIfChanged(RowRates[i], bSelected ? ISEL_ON_SEL : ISEL_GREEN);
            }
            else
            {
                ISEL_SetTextIfChanged(RowRates[i], WT_LOCTEXT("ItemPanel_RowRateNone", "--"));
                ISEL_SetColorIfChanged(RowRates[i], bSelected ? ISEL_ON_SEL : ISEL_DIM);
            }
        }

        // ★1.0.8BI 列③c：云端**净变化**（整个池，不是本机）—— 解释"数量为什么一动不动"。
        //   一边上传 +60/分、一边下载 −60/分 ⇒ 这里正好显示「净 0」，玩家立刻明白不是坏了。
        //   ★1.0.8BJ 首拍（还没做差分）显示「净 --」：那时"没测到"与"真的没变"必须分开，
        //   否则面板一打开就是满屏「净 0」，看起来像池子被人按住了。
        if (RowNets.IsValidIndex(i) && RowNets[i])
        {
            if (!bNetMeasured)
            {
                ISEL_SetTextIfChanged(RowNets[i], WT_LOCTEXT("ItemPanel_NetPending", "净 --"));
                ISEL_SetColorIfChanged(RowNets[i], bSelected ? ISEL_ON_SEL : ISEL_DIM);
            }
            else
            {
                const int32 NetPerMin = FMath::RoundToInt(NetRateCache.FindRef(Cls.Get()));
                if (NetPerMin == 0)
                {
                    ISEL_SetTextIfChanged(RowNets[i], WT_LOCTEXT("ItemPanel_NetZero", "净 0"));
                    ISEL_SetColorIfChanged(RowNets[i], bSelected ? ISEL_ON_SEL : ISEL_DIM);
                }
                else
                {
                    ISEL_SetTextIfChanged(RowNets[i], FText::Format(
                        NetPerMin > 0 ? WT_LOCTEXT("ItemPanel_NetRiseFmt", "净+{0}/分")
                                      : WT_LOCTEXT("ItemPanel_NetFallFmt", "净-{0}/分"),
                        FText::AsNumber(FMath::Abs(NetPerMin))));
                    ISEL_SetColorIfChanged(RowNets[i],
                        bSelected ? ISEL_ON_SEL : (NetPerMin > 0 ? ISEL_RISE : ISEL_GREEN));
                }
            }
        }

    }

    // 只在「显示行数」真的变化时打一条 —— 这是排查「东西上传了但列表里看不到」的第一手数据。
    // ★1.0.7t 起 rows shown = 云端有货的行（+ 选中行、物品过滤态下的行），
    //   hidden 段就是「云端 0 被折叠的数量」。若 shown 远小于 visible 而玩家说「列表是空的」，
    //   说明云端确实没货（不是列表坏了）。
    if (NumShownRows != LastLoggedShownRows)
    {
        WT_WARNF(TEXT("[item-panel] rows shown %d/%d (hidden %d with zero cloud stock) | picked=%s"),
                 NumShownRows, Visible.Num(), Visible.Num() - NumShownRows,
                 Picked ? *Picked->GetName() : TEXT("none"));
        LastLoggedShownRows = NumShownRows;
    }

    // ★1.0.8BG 标题行批量按钮的文案四态**已删除**（那颗按钮整颗不存在了）。
    //   取而代之的是每行行尾的【转移 / 清空】，文案与可用态在下方的行循环里按行刷新 ——
    //   液体是不可逆销毁，所以文案必须逐行写明是"清空"而不是"取到背包"（见列⑦的构建处）。

    if (SelectionText)
    {
        // ★1.0.7t 正常态（列表里有行可看）这段文字**一律不显示** —— 理由见 RebuildWidget 建控件那段。
        //   只留三种「列表是空的、没有别处可看」的异常态让它说话：
        //   ① 没有下载站目标 ② 目录里没有固体物品 / 搜索没命中 ③ 有匹配但云端一件都没存货（全被折叠）。
        //
        // 历史注记（保留裁决理由）：1.0.7g 曾定案「下载站不另开一块原版面板，输出量就在文字行里报」，
        //   原因是两块 UUserWidget 同时压在 FGGameUI 交互栈上会互相覆盖输入模式（本模组 0.2.18 崩过）。
        //   现在输出量已由图例行内的【云端数量】列 + 【每分速度】列承担，**没有**引入新控件、
        //   也没有动交互栈 —— 所以「撤掉文字行」不违背当初那条决定。
        bool bNeedNote = false;
        FText Note;

        // ★1.0.8BB 取货结果**最高优先**：它是"刚刚发生的事"，比"列表为什么是空的"重要得多。
        //   6 秒后自动过期，回落到下面那套常态判断（不要手动清，玩家可能连点两下）。
        const bool bTakeNote = !TakeResultNote.IsEmpty()
                            && GetWorld() != nullptr
                            && GetWorld()->GetTimeSeconds() < TakeResultUntil;
        if (bTakeNote)
        {
            bNeedNote = true;
            Note = FText::FromString(TakeResultNote);
        }
        else if (!bHasTarget)
        {
            bNeedNote = true;
            Note = WT_LOCTEXT("ItemPanel_NoTarget", "请在云端存储站上打开本面板。");
        }
        else if (Visible.Num() == 0)
        {
            // 一条都没有（目录为空 / 搜索没命中）必须说出来 ——
            // 只留一片白玩家分不清是「搜索没命中」还是「面板坏了」。
            bNeedNote = true;
            if (!SearchText.IsEmpty())
            {
                Note = WT_LOCTEXT("ItemPanel_NoMatch", "没有匹配该搜索词的物品。");
            }
            else
            {
                Note = WT_LOCTEXT("ItemPanel_Empty", "没有可下载的固体物品。请查看日志中的 [item-panel]。");
            }
        }
        else if (IsUploaderKind() && SearchText.IsEmpty() && SelectedItemKey.IsEmpty()
                 && NumShownRows == 0)
        {
            // 上传站且列表空 ⇒ 本站**此刻没有正在输入的物品**（缓冲空 / 传送带没送货）。
            // 这跟「云端没有物品」是两件事，必须分开说，否则玩家会以为上传的东西丢了。
            bNeedNote = true;
            Note = WT_LOCTEXT("ItemPanel_NoInbound",
                "本站此刻没有正在输入的物品 —— 列表只显示这台机器实际在收的东西。");
        }
        else if (NumShownRows == 0)
        {
            // ★1.0.7h 有匹配的条目、但云端一件都没有 ⇒ 全被折叠了。
            //   和「搜索没命中」必须分开说，否则玩家会以为自己的搜索词写错了。
            bNeedNote = true;
            Note = SearchText.IsEmpty()
                ? WT_LOCTEXT("ItemPanel_EmptyCloud",
                        "云端池里现在没有任何物品 —— 先上传一些物品，这里才会列出它们。")
                : FText::Format(WT_LOCTEXT("ItemPanel_AllZeroFmt",
                    "搜索到的 {0} 种物品在云端都没有存货。"), FText::AsNumber(Visible.Num()));
        }

        const ESlateVisibility WantedNote =
            bNeedNote ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed;
        if (SelectionText->GetVisibility() != WantedNote)
        {
            SelectionText->SetVisibility(WantedNote);
        }
        if (bNeedNote)
        {
            SelectionText->SetText(Note);
        }
    }
}

void UWTItemSelectPanelWidget::RefreshSpeedText()
{
    if (!SpeedText)
    {
        return;
    }

    // ★1.0.8BI 两个固体站都有本机读数（上传侧以前只能写「上传站 · 只读」）。
    //   用户 2026-10-28「同一个物品，一边上传，一边下载」：顶栏必须直接说清这台机器
    //   在往哪个方向搬多少 —— 池子里的数字是两边相抵后的净值，指望它反映本机流量是错的。
    const bool bDownloadSide = (PanelKind == EWTCloudPanelKind::SolidDownloader);
    const bool bUploadSide   = IsUploaderKind();
    if (!bDownloadSide && !bUploadSide)
    {
        // 面板不是从云端站打开的（PanelKind 解析失败）。
        SpeedText->SetText(WT_LOCTEXT("ItemPanel_SpeedNoTarget", "速度 --"));
        SpeedText->SetColorAndOpacity(FSlateColor(ISEL_DIM));
        return;
    }

    // ★口径：最近 5 秒**真正搬动**的件数折算成「件/分钟」。
    //   下载 = 真的进了本站缓冲；上传 = 真的推进了原版云端池（拒收/配额满时自然掉到 0）。
    const int32 PerMin = FMath::RoundToInt(WT_GetTotalRatePerMinute());

    if (bUploadSide)
    {
        // 上传侧**不报上限**：原版的上传节流是全局的（GetCentralStorageTimeToUpload），
        // 不是"本站的上限"，写出来会被读成本机能力，反而误导。只报实际速度。
        SpeedText->SetText(FText::Format(WT_LOCTEXT("ItemPanel_SpeedUpFmt", "上传速度 {0}/分"),
                                         FText::AsNumber(PerMin)));
        SpeedText->SetColorAndOpacity(FSlateColor(PerMin > 0 ? ISEL_GREEN : ISEL_DIM));
        return;
    }

    const int32 Limit = FMath::RoundToInt(WT_GetRateLimitPerMinute());
    // ★1.0.8AQ Limit <= 0 = 本站没有可信的上限口径（液体站就是这样，管网流量是动态值）
    //   ⇒ 只报速度，不编一个「上限」出来骗人。
    if (Limit > 0)
    {
        SpeedText->SetText(FText::Format(WT_LOCTEXT("ItemPanel_SpeedFmt", "下载速度 {0}/分（上限 {1}/分）"),
                                         FText::AsNumber(PerMin), FText::AsNumber(Limit)));
    }
    else
    {
        SpeedText->SetText(FText::Format(WT_LOCTEXT("ItemPanel_SpeedFmtNoLimit", "下载速度 {0}/分"),
                                         FText::AsNumber(PerMin)));
    }
    // 有进料 = 绿色（与「云端剩余」同色系）；0 = 暗色（一眼看出已停）。
    SpeedText->SetColorAndOpacity(FSlateColor(PerMin > 0 ? ISEL_GREEN : ISEL_DIM));
}

void UWTItemSelectPanelWidget::OnRowAction(int32 VisibleIndex)
{
    // ★1.0.7z 上传站模式：面板只显示云端库存，没有「输出物品」可选（按钮列也整列没建）。
    //   这里再挡一道，防止游戏把行点击当成交互事件后仍然走进选择逻辑。
    if (bUploaderMode)
    {
        return;
    }
    if (!Visible.IsValidIndex(VisibleIndex))
    {
        return;
    }
    const UClass* Cls = Visible[VisibleIndex].Get();
    if (!Cls)
    {
        return;
    }

    // ★1.0.8 行尾按钮 = 【选择】/【取消】双态，行点击与按钮点击走**同一条**逻辑：
    //   点一下选中，再点一下取消（用户 2026-10-27：「为什么不能在点一下取消？」）。
    //
    //   旧版（1.0.7v~1.0.7z）的两条限制现在都撤掉了，理由各自消失：
    //     ① 「只有写着【更换】的按钮才能取消」—— 那颗按钮现在就叫【取消】，名字已经说明一切；
    //     ② 「点行绝不取消」—— 当时禁止是因为白名单空 = **搬全部**，玩家手滑再点一下，
    //        机器就从"只搬铁矿石"变成"什么都搬"，才会有「无论选不选都会下载」的观感。
    //        1.0.7x 起空名单 = **一件都不搬**（AWTUpgradeCloudDownloader::IsOutputAllowed），
    //        取消只是停止下载、列表与标题立刻显示「未选择（不会下载）」，没有灾难性后果。
    if (GetSelectedClass() == Cls)
    {
        // 老存档多项残留（0.2.36~1.0.7e 多选时代）：点第 0 项时先收敛成单选而不是清空 ——
        //   此时机器按整张白名单搬 N 种，玩家的意图多半是"就只搬这个"，清空会让他以为点错了。
        if (const AWTUpgradeCloudDownloader* Cur = GetDownloader())
        {
            if (Cur->GetOutputFilter().Num() > 1)
            {
                SelectItem(VisibleIndex);
                return;
            }
        }
        ClearSelection();
        return;
    }
    SelectItem(VisibleIndex);
}

void UWTItemSelectPanelWidget::SelectItem(int32 VisibleIndex)
{
    if (!Visible.IsValidIndex(VisibleIndex))
    {
        return;
    }
    AWTUpgradeCloudDownloader* Downloader = GetDownloader();
    if (!Downloader)
    {
        return;
    }

    const TSubclassOf<UFGItemDescriptor> Cls = Visible[VisibleIndex];
    if (!Cls.Get())
    {
        return;
    }
    // ★★★1.0.7v 真 bug 修复（用户 2026-10-27：「云端的物品无论选不选都会下载」）：
    //
    //   判重条件**不能**写成 `GetSelectedClass() == Cls` —— 那个函数只返回白名单的
    //   **第 0 个元素**（见 GetSelectedClass 的实现与注释）。而白名单在老存档里可能
    //   遗留**多项**（0.2.36~1.0.7e 允许多选），于是：
    //     · 面板只高亮 [0]，看着像单选；
    //     · 机器侧 `IsOutputAllowed` 放行**整张白名单**，包括玩家在界面上根本看不见的第 2..N 项；
    //     · 玩家点"已经高亮的那一行"（想确认选它）⇒ 这里提前 return ⇒ 多项原封不动
    //       ⇒ 机器继续搬好几种 ⇒ 玩家的结论就是「无论选不选都会下载」。
    //   ⇒ 改成**只有"白名单恰好只有它一个"才提前返回**，其余情况一律整体覆盖成单选，
    //     多选残留被顺手收敛掉（不新增 UI、不要求玩家清存档）。
    if (const AWTUpgradeCloudDownloader* Cur = Downloader)
    {
        const TArray<TSubclassOf<UFGItemDescriptor>>& CurFilter = Cur->GetOutputFilter();
        if (CurFilter.Num() == 1 && CurFilter[0].Get() == Cls.Get())
        {
            return;   // 确实"只有它一个"，不用重写（重写会白打一条日志）
        }
    }

    // ★1.0.7f「下载仅限 1 种」：直接把白名单整体设成这一种（不是 ToggleOutputItem，
    // 那样会把别的留着、变成多选）。SetOutputFilter 内部还会判空/去重/排序/打日志。
    TArray<TSubclassOf<UFGItemDescriptor>> One;
    One.Add(Cls);
    Downloader->SetOutputFilter(One);

    // ★1.0.7s 选择变了只改「下载哪几种」，**行集不变**（选中不再隐藏其它行）⇒ 刷样式即可。
    //   这里刻意**不重建行**：RebuildRows 会清空 ItemListBox 重挂上百个子项，
    //   ScrollBox 的滚动位置会一起被重置回顶部 —— 玩家点到第 80 行会被弹回第一行。
    RefreshRowStyles();
}

void UWTItemSelectPanelWidget::ClearSelection()
{
    // ★1.0.8 「取消」= 撤销输出物品选择 ⇒ 白名单空 ⇒ 机器一件都不搬
    //   （AWTUpgradeCloudDownloader::IsOutputAllowed 里空名单直接 return false）。
    //
    // 刻意**不**动搜索词与物品过滤：玩家取消多半是马上要换一个，把筛选一起清掉反而要他重输。
    // （旧版叫 ChangeSelection「更换」，那语义需要放开列表全量，才顺手清搜索；现在按钮叫【取消】，不越界。）
    if (GetSelectedClass() == nullptr)
    {
        return;   // 本来就没选，什么也不用做
    }
    if (AWTUpgradeCloudDownloader* Downloader = GetDownloader())
    {
        Downloader->SetOutputFilter(TArray<TSubclassOf<UFGItemDescriptor>>());
    }
    // 行集不依赖选择（目录 ∩ 阶段 ∩ 物品 ∩ 搜索词）⇒ 不必 RebuildRows：
    //   重建会 ClearChildren 再重挂上百个 UButton，ScrollBox 的滚动位置会被弹回顶部。
    // RefreshRowStyles 内部同时刷标题三态、行底色、按钮双态与可见性，足够。
    RefreshRowStyles();
}

void UWTItemSelectPanelWidget::OnSearchChanged(const FText& Text)
{
    SearchText = Text.ToString().TrimStartAndEnd();

    // ★1.0.7s 单选态下搜索现在**照常生效**（列表已不再只剩选中那一行）⇒ 输入即刻筛列表。
    RebuildVisible();
    RebuildRows();
    RefreshAmounts();
    RefreshRowStyles();
}

void UWTItemSelectPanelWidget::OnClearSelectionClicked()
{
    if (AWTUpgradeCloudDownloader* Downloader = GetDownloader())
    {
        Downloader->SetOutputFilter(TArray<TSubclassOf<UFGItemDescriptor>>());
    }
    SearchText.Reset();
    if (SearchBox)
    {
        SearchBox->SetText(FText::GetEmpty());
    }
    RebuildVisible();
    RebuildRows();
    RefreshAmounts();
    RefreshRowStyles();
}

// ============================================================================
// ★1.0.8BB 批量取货到背包（固体 / 液体 四站通用）
//
// 用户 2026-10-28 的三句话定下了这个功能的全部语义：
//   ① 「云端固体可以像原版那样手动取出物品」   ⇒ 要能从云端把货拿到身上；
//   ② 「就是连带原版的」                       ⇒ 接受原版维度仓库的同类物品一起被搬走
//                                                （池里不记谁上传的，这一点绕不过去）；
//   ③ 「一组一组处理太慢了」「增加一个背包批量操作方式」 ⇒ **一次搬完一整类**，不要外面写循环一格一格塞。
//
// ★为什么"一次搬完"是免费的：`UFGInventoryLibrary::MoveItemFromCentralStorage(itemAmount, dest)`
//   的 itemAmount.Amount 本身就接受任意数量，原版内部会按目标背包的空槽自行分配
//   （FGInventoryLibrary.h:195-202 "it will move as many items as possible"）。
//   所以传「全部存量」进去，一次调用就是批量；"一组一组"只有在我们自己按 StackSize 循环时才会发生。
//
// ★为什么不用 TryRemoveItemsFromCentralStorage：那是「从池里销毁」，取出后没有去处。
//   本轮要的是**转移**，销毁会把玩家不想丢的货真吃掉。它现在只服务液体站「推进管网」那条路。
// ============================================================================



int32 UWTItemSelectPanelWidget::TakeCloudItemsToBackpack(UClass* Cls)
{
    if (!Cls)
    {
        return -1;
    }
    UWorld* World = GetWorld();
    if (!World)
    {
        return -2;
    }
    AFGCentralStorageSubsystem* Sub = AFGCentralStorageSubsystem::Get(World);
    if (!Sub)
    {
        return -2;
    }

    const TSubclassOf<UFGItemDescriptor> WantClass(Cls);
    const int32 Available = Sub->GetNumItemsFromCentralStorage(WantClass);
    if (Available <= 0)
    {
        return -3;
    }

    // 目标库存 = **玩家背包**（不是本站缓冲）。
    //   GetOwningPlayer() 是本 widget 的拥有者控制器；GetPawn() → AFGCharacterPlayer。
    //   GetInventory() 是 FORCEINLINE 访问器（FGCharacterPlayer.h:722），不经过 SML 桩、无虚调用风险。
    UFGInventoryComponent* Inv = nullptr;
    if (APlayerController* PC = GetOwningPlayer())
    {
        if (AFGCharacterPlayer* Char = Cast<AFGCharacterPlayer>(PC->GetPawn()))
        {
            Inv = Char->GetInventory();
        }
    }
    if (!Inv)
    {
        return -2;
    }

    // ---- 主路径：一次请求整类存量 ----
    FItemAmount Want;
    Want.ItemClass = WantClass;
    Want.Amount    = Available;
    int32 Moved = UFGInventoryLibrary::MoveItemFromCentralStorage(Want, Inv);

    // ---- 兜底：自动选槽放不下（背包里有半格/碎片格）时逐格试 ----
    //   GrabItemFromCentralStorage 是"往**指定格**搬，放得下多少搬多少"，所以对每个格子问一次即可。
    // ★绝不"先 TryRemove 再从池外 AddStack"：那条路在放不下时每次真丢一组货（1.0.7m 定案，见
    //   Remote_FactoryUpgrades.cpp 里同一段的注释）。这里逐格问，最坏情况只是搬不动，池不动。
    if (Moved < Available)
    {
        const int32 Slots = Inv->GetSizeLinear();
        // ★变量名不能叫 Slot：UWidget 有同名成员，MSVC 的 C4458（声明遮蔽类成员）在本工程被当错误。
        for (int32 SlotIndex = 0; SlotIndex < Slots && Moved < Available; ++SlotIndex)
        {
            FItemAmount Rest;
            Rest.ItemClass = WantClass;
            Rest.Amount    = Available - Moved;
            const int32 Got = UFGInventoryLibrary::GrabItemFromCentralStorage(Rest, Inv, SlotIndex);
            if (Got > 0)
            {
                Moved += Got;
            }
        }
    }

    WT_WARNF(TEXT("[item-panel] take-to-backpack '%s' cloud=%d moved=%d left=%d slots=%d"),
             *Cls->GetName(), Available, Moved, Available - Moved, Inv->GetSizeLinear());
    return Moved;
}

void UWTItemSelectPanelWidget::OnRowTakeClicked(int32 VisibleIndex)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }
    if (!Visible.IsValidIndex(VisibleIndex))
    {
        return;
    }
    UClass* Cls = Visible[VisibleIndex].Get();
    if (!Cls)
    {
        return;
    }

    // ★★1.0.8BG 一次只动**这一行的物品**（用户 2026-10-28：「不要选择清空全部，选择单一的」
    //   「每一行不是一个物品吗，在后面增加一个清空/转移等等」）。
    //   标题行那颗"作用于整个列表"的批量按钮已经整颗删掉了 —— 它的作用范围天生比玩家
    //   看到的大（液体更不可逆），这颗按行按钮才是"所见即所动"。
    const int32 Got   = TakeCloudItemsToBackpack(Cls);

    // ★1.0.8BH 开背包**不在**这里做，只登记意图；真正触发放在函数末尾（刷新完行状态之后）。
    //   原因见 OpenInventoryAfterClosingSelf() 的注释：开背包 = 本面板被顶掉 = 本类的
    //   NativeDestruct 会把玩家控制器按回 GameOnly + 隐光标，而那正是"背包里没有光标"。
    bool bOpenBagAfter = false;

    const float Now = World->GetTimeSeconds();
    if (Got > 0)
    {
        TakeResultNote = FText::Format(
            WT_LOCTEXT("ItemPanel_TakeOneOkFmt", "已把「{0}」{1} 件转移到背包。"),
            UFGItemDescriptor::GetItemName(Cls), FText::AsNumber(Got)).ToString();
        // ★用户 2026-10-28：「选择转移的时候，背包 UI 或者其他 UI 要打开，方便转移」。
        //   货**已经进背包了**（上面那步），打开界面只是让玩家当场看到并整理 ——
        //   即便打开失败也不会丢东西，所以这里不做错误提示、只打日志。
        // ★1.0.8BH 不在本帧直接开：必须先让本面板退出（见函数注释，丢光标根因）。
        //   实际触发放在函数末尾（刷新完行状态之后），顺序写死在那里。
        bOpenBagAfter = true;
        TakeResultUntil = Now + 6.0;
    }
    else if (Got == -3)
    {
        TakeResultNote = WT_LOCTEXT("ItemPanel_TakeOneEmpty", "这一种在云端已经没有存货了。").ToString();
        TakeResultUntil = Now + 6.0;
    }
    else if (Got == 0)
    {
        TakeResultNote = WT_LOCTEXT("ItemPanel_TakeOneFull",
            "背包放不下了 —— 先腾出空格再来。云端一件没动。").ToString();
        TakeResultUntil = Now + 6.0;
    }
    else
    {
        TakeResultNote = WT_LOCTEXT("ItemPanel_TakeUnavailable",
            "取货失败：找不到维度仓库子系统或玩家背包。").ToString();
        TakeResultUntil = Now + 6.0;
    }

    WT_WARNF(TEXT("[item-panel] row-action %s '%s' got=%d"),
             TEXT("take-to-backpack"), *Cls->GetName(), Got);

    // ★这里**不** RebuildRows：清空/转移只改"云端有多少"，不改行集（Visible 由目录 ∩ 筛选
    //   决定）。而且当前点击的正是列表里那颗按钮，重建会在它自己的回调里把控件拆掉
    //   （Slate 有引用保护不会立刻炸，但纯属给自己找麻烦）。行集缩小交给 ShouldShowRow 折叠。
    RefreshAmounts();
    RefreshRowStyles();

    // ★★1.0.8BH 最后一步：先刷完界（面板还活着），再把"关闭自己 + 下一拍开背包"交出去。
    //   顺序反过来就是丢光标（本面板的还原会覆盖背包的输入模式）。
    if (bOpenBagAfter)
    {
        OpenInventoryAfterClosingSelf();
    }
}

void UWTItemSelectPanelWidget::OpenInventoryAfterClosingSelf()
{
    UWorld* World = GetWorld();
    APlayerController* PC = GetOwningPlayer();
    if (!World || !PC)
    {
        WT_WARNF(TEXT("[item-panel] open-inventory skipped: no world / owning player."));
        return;
    }

    // ① 先把自己**彻底**退出：摘交互栈 → GameOnly + Reset* → RemoveFromParent（全在 OnCloseClicked 里）。
    //    ★★这一步就是「弹出背包丢失光标」的修复点，顺序不可换。
    //    不先退出的话：原版背包打开时会把本面板从交互栈顶掉 ⇒ 本类 NativeDestruct 随后把
    //    玩家控制器按回 GameOnly + bShowMouseCursor=false ⇒ 背包界面在、鼠标点不动。
    // ★World 与 PC 必须在这里先缓存：OnCloseClicked 之后本控件已移出视口。
    OnCloseClicked();

    // ② **下一拍**才发原版开背包事件（0.12s，不是同帧）。此时 Slate 与交互栈里已经没有本面板，
    //    原版背包自己设 GameAndUI + 光标，没有任何东西能反向覆盖它。
    //    ★lambda 只捕获玩家控制器的**弱引用**，绝不捕获 this —— 本函数返回前面板已移除，
    //      下一拍时它可能已被 GC。
    TWeakObjectPtr<APlayerController> WeakPC(PC);
    FTimerHandle DeferredHandle;
    World->GetTimerManager().SetTimer(DeferredHandle,
        FTimerDelegate::CreateLambda([WeakPC]()
        {
            AFGPlayerController* FGP = Cast<AFGPlayerController>(WeakPC.Get());
            if (!FGP)
            {
                WT_WARNF(TEXT("[item-panel] open-inventory(deferred) skipped: controller gone."));
                return;
            }

            // 两条原版切面都**尽力而为**（货早已进背包，这里失败只是界面没弹出来）：
            //   ① AFGCharacterPlayer::NotifyOnToggleInventory()：Tab 键蓝图事件的同名落点；
            //   ② AFGPlayerController::OnToggleInventory 广播：原版 UI 自己挂在这个委托上。
            bool bCharCall = false;
            if (AFGCharacterPlayer* Char = Cast<AFGCharacterPlayer>(FGP->GetPawn()))
            {
                Char->NotifyOnToggleInventory();
                bCharCall = true;
            }
            const bool bBound = FGP->OnToggleInventory.IsBound();
            if (bBound)
            {
                FGP->OnToggleInventory.Broadcast(true);
            }

            // 判据：charEvent=1 表示蓝图事件已发；delegateBound=0 表示没人监听（那条链就断在这里）。
            WT_WARNF(TEXT("[item-panel] open-inventory(deferred): charEvent=%d delegateBound=%d"),
                     bCharCall ? 1 : 0, bBound ? 1 : 0);
        }),
        0.12f, false);
}

/**
 * ★1.0.7j 把本面板从 UFGGameUI 的交互栈上摘掉 —— 「关面板后所有 E 交互锁死 / 面板再也打不开」的唯一修复点。
 *
 * 为什么必须做：本类不是自己 AddToViewport 的，而是靠 AWTUpgradeCloudDownloader 的
 * `mInteractWidgetSoftClass`（WT_UseNativeInteractPanel，Remote_FactoryUpgrades.cpp:352）交给**游戏**
 * 走原生交互管线生成的 —— 游戏在 OnToggleInteractWidget 里会 `AddInteractWidget` 把它压进
 * `UFGGameUI::mInteractWidgetStack`（FGGameUI.h:104/108/343）。UMG 侧的 RemoveFromParent() 只摘 Slate
 * 树，**不动这个栈**：
 *   • 栈残留 ⇒ `HasActiveInteractWidget()`（FGGameUI.h:88）恒为 true ⇒ 游戏认为玩家仍在看交互面板
 *     ⇒ E 键被吞（所有 E 交互哑掉）；
 *   • 下次按 E 时 OnToggleInteractWidget（FGGameUI.h:114）见栈里已有同类面板 ⇒ 不再开新的
 *     ⇒ 「关掉一次以后再也打不开」。
 * 这两个症状是同一个泄漏的两个面，必须走 RemoveInteractWidget 摘栈。
 * 幂等性：ESC 关面板时游戏自己已经摘过，再调是安全 no-op（同 Remote_FactoryEquipment::ClosePanel 的判断）。
 */
static void WT_UnregisterInteractWidget(UUserWidget* Self, bool& bAlreadyReleased)
{
    // 幂等守卫用**显式标记**，不用 IsInViewport()：
    //   NativeDestruct 执行时 widget 通常已不在视口（引擎已清 CachedViewportWidget），
    //   拿 IsInViewport() 当守卫，会在「外部直接拆毁」这条路径上静默跳过摘栈 —— 那正是本 bug 的泄漏源。
    if (!Self || bAlreadyReleased)
    {
        return;
    }
    bAlreadyReleased = true;

    if (APlayerController* PC = Self->GetOwningPlayer())
    {
        if (AFGPlayerController* FGP = Cast<AFGPlayerController>(PC))
        {
            if (UFGGameUI* GameUI = FGP->GetGameUI())
            {
                if (GameUI->HasActiveInteractWidget())
                {
                    GameUI->RemoveInteractWidget(Cast<UFGInteractWidget>(Self));
                    WT_WARNF(TEXT("[item-panel] unregistered from FGGameUI interact stack "
                                  "(stack still active=%d)."),
                             GameUI->HasActiveInteractWidget() ? 1 : 0);
                }
            }
        }
    }
}

void UWTItemSelectPanelWidget::OnCloseClicked()
{
    // ★1.0.7c 关闭即还原（与 SetInputMode_Implementation 的接管严格对称）：
    //   回 GameOnly + 隐光标，并用 Reset* 把视角/移动锁定**彻底**解除。
    //   为什么要 Reset 而不是 SetIgnore*(false)：Controller.h:353-376 明确写着 SetIgnore* 是计数式的，
    //   连续调用会叠加、需要同次数撤销；一旦计数对不上，关面板后角色就再也走不动/转不了视角。
    //   Reset* 一次归零，是唯一安全解。
    //
    // ★1.0.7j 顺序：① 先摘交互栈（否则游戏侧仍认定面板开着，E 全锁死）
    //              ② 再还原输入模式  ③ 最后 RemoveFromParent。
    //   摘栈若内部触发 RemoveFromParent，②③ 都退化成安全 no-op，不会重复异常。
    WT_UnregisterInteractWidget(this, bInteractStackReleased);

    if (APlayerController* PC = GetOwningPlayer())
    {
        FInputModeGameOnly InputMode;
        PC->SetInputMode(InputMode);
        PC->bShowMouseCursor = false;
        PC->ResetIgnoreLookInput();
        PC->ResetIgnoreMoveInput();
    }
    // ★1.0.8BH 标记"已主动还原"：随后 RemoveFromParent() 触发的 NativeDestruct 可能被 Slate
    //   延后到下一帧才执行（本函数正处在点击回调里，SObjectWidget 还有额外引用）。那次晚到的
    //   还原会把此刻/之后打开的原版背包按回 GameOnly + 隐光标 ⇒ 用户实测的"背包里没有光标"。
    //   已还原过就不再重复还原（还原不是越多次越安全，它会覆盖别人设好的输入模式）。
    // ★必须在 RemoveFromParent() **之前**置位，因为 RemoveFromParent 可能同步走到 NativeDestruct。
    bInputRestoredBySelf = true;
    RemoveFromParent();
}

void UWTItemSelectPanelWidget::NativeDestruct()
{
    // ★1.0.8BJ 先停刷新定时器：拆毁后不该再有任何一拍去刷一行已经不存在的界面。
    //   定时器绑的是 UObject 成员函数，漏清也不会打野指针（TimerManager 会检查对象有效性），
    //   但显式清掉能让「面板关闭后仍在扫世界」这类假象彻底消失。
    if (UWorld* DestructWorld = GetWorld())
    {
        DestructWorld->GetTimerManager().ClearTimer(RefreshTimerHandle);
    }

    // ★1.0.7c 对称还原兜底（ESC 关面板 / 游戏摘栈 / 看门狗拆毁都走这里）。
    //   与 OnCloseClicked 同款；漏掉 Reset* 就是「关面板后角色不动」的必然结果。
    //
    // ★1.0.7j 摘栈兜底：任何拆毁路径（ESC 由游戏摘 / 外部直接 RemoveFromParent / 看门狗拆毁）
    //   只要没走过 OnCloseClicked，这里就是唯一兜底。幂等由 bInteractStackReleased 保证，
    //   所以不再需要 IsInViewport() 判断（那个判断在 NativeDestruct 里恰好恒为 false）。
    WT_UnregisterInteractWidget(this, bInteractStackReleased);

    // ★1.0.8BH 主动关闭已经还原过输入模式时，这里**不再**重复还原。
    //   NativeDestruct 可能被 Slate 延后到原版背包打开之后才执行，那份晚到的 GameOnly
    //   会把背包的 GameAndUI + 光标按掉（用户实测「弹出背包丢失光标」）。
    //   其余所有拆毁路径（ESC / 游戏摘栈 / 看门狗）没走过 OnCloseClicked，
    //   bInputRestoredBySelf 仍是 false，下面的兜底照常执行。
    if (!bInputRestoredBySelf)
    {
        if (APlayerController* PC = GetOwningPlayer())
        {
            FInputModeGameOnly InputMode;
            PC->SetInputMode(InputMode);
            PC->bShowMouseCursor = false;
            PC->ResetIgnoreLookInput();
            PC->ResetIgnoreMoveInput();
        }
    }
    Super::NativeDestruct();
}
