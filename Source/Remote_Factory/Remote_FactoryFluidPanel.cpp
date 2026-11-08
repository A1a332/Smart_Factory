#include "Remote_FactoryFluidPanel.h"

#include "Remote_FactoryLoc.h"
#include "Remote_FactoryLog.h"
#include "Remote_FactoryUpgrades.h"          // AWTUpgradeCloudFluidUploader / AWTUpgradeCloudFluidDownloader 完整类型
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"            // ★1.0.8AV UBorderSlot（内容槽内边距，与固体面板对齐）
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"   // ★1.0.8AV FInputModeGameAndUI/GameOnly + SetInputMode/SetIgnore*Input
#include "FGPlayerController.h"                // ★1.0.8AV AFGPlayerController::GetGameUI()（摘交互栈防 E 锁死）
#include "UI/FGGameUI.h"                        // ★1.0.8AV UFGGameUI 完整类型（RemoveInteractWidget / HasActiveInteractWidget）
#include "EngineUtils.h"                     // ★本版：TActorIterator<AWTUpgradeCloudFluidUploader>（聚合我们的液态账本云）
#include "FGCentralStorageSubsystem.h"       // 原版中央存储（下载站取货先查它，面板一并列出）
#include "Resources/FGItemDescriptor.h"      // UFGItemDescriptor::GetForm / GetItemName / EResourceForm
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/Anchors.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SWidget.h"
#include "Input/Reply.h"

// 与 Remote_FactoryPanelWidget 同一套 DSP 风（独立文件，各自 static 即可）。
static const FLinearColor P_BG      = FLinearColor(0.043f, 0.055f, 0.078f, 0.97f);
static const FLinearColor P_ACCENT  = FLinearColor(0.290f, 0.780f, 0.900f, 1.f);
static const FLinearColor P_LABEL   = FLinearColor(0.560f, 0.660f, 0.730f, 1.f);
static const FLinearColor P_VALUE   = FLinearColor(0.870f, 0.940f, 0.970f, 1.f);
static const FLinearColor P_GREEN   = FLinearColor(0.380f, 0.900f, 0.640f, 1.f);
static const FLinearColor P_BTN     = FLinearColor(0.095f, 0.125f, 0.165f, 1.f);
static const FLinearColor P_BTN_SEL = FLinearColor(0.290f, 0.780f, 0.900f, 0.90f);
static const FLinearColor P_ON_SEL  = FLinearColor(0.035f, 0.060f, 0.080f, 1.f);

static UTextBlock* FP_MakeText(UWidgetTree* Tree, const FText& InText, int32 FontSize,
                               bool bBold, FLinearColor Color)
{
    UTextBlock* T = Tree->ConstructWidget<UTextBlock>();
    T->SetText(InText);
    T->SetFont(FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), FontSize));
    T->SetColorAndOpacity(FSlateColor(Color));
    T->SetAutoWrapText(true);
    // ★UI 文本对齐铁律：所有文本显式左对齐（名称/数量均在行内左起）。
    T->SetJustification(ETextJustify::Left);
    return T;
}

static UButton* FP_MakeButton(UWidgetTree* Tree, const FText& InText, FLinearColor BgColor,
                              UTextBlock** OutLabel = nullptr)
{
    UButton* B = Tree->ConstructWidget<UButton>();
    B->SetBackgroundColor(BgColor);
    UTextBlock* T = FP_MakeText(Tree, InText, 13, false, P_VALUE);
    B->AddChild(T);
    if (OutLabel)
    {
        *OutLabel = T;
    }
    return B;
}

// ★1.0.8AV 标题下行分隔线 —— 与固体面板 ISEL_MakeDivider / 终端面板 MakeDivider 逐字同构：
//   显式取引擎 WhiteBrush（默认构造 FSlateBrush 无资源、画出空），染成 P_ACCENT 半透明（线厚靠纵向 padding 3）。
static UBorder* FP_MakeDivider(UWidgetTree* Tree)
{
    UBorder* D = Tree->ConstructWidget<UBorder>();
    D->SetBrush(*FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")));
    D->SetBrushColor(FLinearColor(P_ACCENT.R, P_ACCENT.G, P_ACCENT.B, 0.45f));
    D->SetPadding(FMargin(0.f, 3.f));
    return D;
}

// ★1.0.8AV 从 UFGGameUI 交互栈摘除本面板（防「关面板后 E 交互锁死 / 面板再也打不开」）。
//   与 ItemSelectPanel 同款静态辅助；用显式标记做幂等，不靠 IsInViewport()（NativeDestruct 里恒为 false）。
static void WT_FluidUnregisterInteractWidget(UUserWidget* Self, bool& bAlreadyReleased)
{
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
                }
            }
        }
    }
}

// ----------------------------------------------------------------------------

TSharedRef<SWidget> UWTFluidSelectPanelWidget::RebuildWidget()
{
    RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>();

    RootBorder = WidgetTree->ConstructWidget<UBorder>();
    RootBorder->SetBrush(*FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")));
    RootBorder->SetBrushColor(P_BG);
    RootBorder->SetPadding(FMargin(0.f));
    RootBorder->SetClipping(EWidgetClipping::ClipToBounds);

    if (UCanvasPanelSlot* CanvasSlot = RootCanvas->AddChildToCanvas(RootBorder))
    {
        CanvasSlot->SetAnchors(FAnchors(0.5f, 0.5f));
        CanvasSlot->SetAlignment(FVector2D(0.5f, 0.5f));
        CanvasSlot->SetSize(FVector2D(1180.f, 760.f));
    }

    UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>();

    // ---- title row -----------------------------------------------------------
    UHorizontalBox* TitleRow = WidgetTree->ConstructWidget<UHorizontalBox>();
    TitleText = FP_MakeText(WidgetTree,
        WT_LOCTEXT("FluidPanel_Title", "云端液态下载站 · 选择流体"), 22, true, P_ACCENT);
    if (UHorizontalBoxSlot* S = TitleRow->AddChildToHorizontalBox(TitleText))
    {
        S->SetPadding(FMargin(2.f, 0.f, 12.f, 0.f));
        S->SetVerticalAlignment(VAlign_Center);
        S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }

    UButton* RefreshButton = FP_MakeButton(WidgetTree, WT_LOCTEXT("Btn_Refresh", "刷新"), P_BTN);
    RefreshButton->OnClicked.AddDynamic(this, &UWTFluidSelectPanelWidget::OnRefreshClicked);
    if (UHorizontalBoxSlot* S = TitleRow->AddChildToHorizontalBox(RefreshButton))
    {
        S->SetPadding(FMargin(4.f, 0.f));
        S->SetVerticalAlignment(VAlign_Center);
    }

    UButton* CloseButton = FP_MakeButton(WidgetTree, WT_LOCTEXT("Btn_Close", "关闭"), P_BTN_SEL);
    CloseButton->OnClicked.AddDynamic(this, &UWTFluidSelectPanelWidget::OnCloseClicked);
    if (UHorizontalBoxSlot* S = TitleRow->AddChildToHorizontalBox(CloseButton))
    {
        S->SetPadding(FMargin(4.f, 0.f));
        S->SetVerticalAlignment(VAlign_Center);
    }
    Root->AddChildToVerticalBox(TitleRow);
    Root->AddChildToVerticalBox(FP_MakeDivider(WidgetTree));   // ★1.0.8AV 与固体面板同款标题下分隔线

    // ---- current selection line ----------------------------------------------
    CurrentText = FP_MakeText(WidgetTree, FText::GetEmpty(), 13, false, P_GREEN);
    if (UVerticalBoxSlot* S = Root->AddChildToVerticalBox(CurrentText))
    {
        S->SetPadding(FMargin(2.f, 8.f));
    }

    // ---- fluid rows ------------------------------------------------------------
    FluidListBox = WidgetTree->ConstructWidget<UScrollBox>();
    if (UVerticalBoxSlot* S = Root->AddChildToVerticalBox(FluidListBox))
    {
        S->SetPadding(FMargin(0.f, 4.f));
        S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }

    RootBorder->SetContent(Root);
    if (UBorderSlot* S = Cast<UBorderSlot>(RootBorder->GetContentSlot()))
    {
        S->SetHorizontalAlignment(HAlign_Fill);
        S->SetVerticalAlignment(VAlign_Fill);
        S->SetPadding(FMargin(16.f));   // ★1.0.8AV 与固体面板对齐：内边距下沉到内容槽
    }
    return RootCanvas->TakeWidget();
}

void UWTFluidSelectPanelWidget::NativeConstruct()
{
    Super::NativeConstruct();

    // ★1.0.8AV 与固体面板/终端面板同一套视口自适应：视口 90% 宽 / 85% 高，上限 1280x820，居中。
    //   两块面板前后脚打开，尺寸公式分叉会让玩家看到忽大忽小。
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

    // ★1.0.8AU 诊断：确认面板确实被实例化并拿到了交互目标（按 E 不弹面板时看这行有没有出现）。
    WT_LOGF(TEXT("[fluid-panel] NativeConstruct (downloader=%s)."),
            GetDownloader() ? *GetDownloader()->GetName() : TEXT("<none>"));
    RebuildFluidList();
}

void UWTFluidSelectPanelWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    // 云端是别人在写（上传站持续入账），周期刷新但只在内容变化时才重建行。
    RebuildFluidList();
}

AWTUpgradeCloudFluidDownloader* UWTFluidSelectPanelWidget::GetDownloader() const
{
    // mInteractObject 是原版交互路径开面板时填的目标（Remote_FactoryPanelWidget 同款读法）。
    if (FObjectProperty* OP = CastField<FObjectProperty>(
            UFGInteractWidget::StaticClass()->FindPropertyByName(TEXT("mInteractObject"))))
    {
        UObject* Raw = OP->GetObjectPropertyValue(OP->ContainerPtrToValuePtr<void>(this));
        return Cast<AWTUpgradeCloudFluidDownloader>(Raw);
    }
    return nullptr;
}

void UWTFluidSelectPanelWidget::RebuildFluidList()
{
    AWTUpgradeCloudFluidDownloader* Downloader = GetDownloader();

    // ---- ① 聚合我们自己的液态账本云（所有云端流体上传站的 mFluidLedger） -------
    //     ② 再并入原版中央存储里的流体（下载站取货先查原版池，合并计数保持一致）。
    TMap<TSubclassOf<UFGItemDescriptor>, int32> Aggregate;

    if (UWorld* World = GetWorld())
    {
        for (TActorIterator<AWTUpgradeCloudFluidUploader> It(World); It; ++It)
        {
            AWTUpgradeCloudFluidUploader* Station = *It;
            if (!Station)
            {
                continue;
            }
            for (const FItemAmount& IA : Station->GetFluidLedger())
            {
                if (IA.ItemClass.Get() == nullptr || IA.Amount <= 0)
                {
                    continue;
                }
                Aggregate.FindOrAdd(IA.ItemClass) += IA.Amount;
            }
        }

        if (AFGCentralStorageSubsystem* Sub = AFGCentralStorageSubsystem::Get(World))
        {
            TArray<FItemAmount> CloudItems;
            Sub->GetAllItemsFromCentralStorage(CloudItems);
            for (const FItemAmount& IA : CloudItems)
            {
                if (IA.ItemClass.Get() == nullptr || IA.Amount <= 0)
                {
                    continue;
                }
                const EResourceForm Form = UFGItemDescriptor::GetForm(IA.ItemClass);
                if (Form == EResourceForm::RF_LIQUID || Form == EResourceForm::RF_GAS)
                {
                    Aggregate.FindOrAdd(IA.ItemClass) += IA.Amount;
                }
            }
        }
    }

    // ---- 落到并行数组（CachedFluids / CachedAmounts） -------------------------
    CachedFluids.Reset();
    CachedAmounts.Reset();
    for (const TPair<TSubclassOf<UFGItemDescriptor>, int32>& Pair : Aggregate)
    {
        CachedFluids.Add(Pair.Key);
        CachedAmounts.Add(Pair.Value);
    }

    // ---- signature guard -------------------------------------------------------
    uint32 Sig = 0;
    for (const TSubclassOf<UFGItemDescriptor>& C : CachedFluids)
    {
        Sig = HashCombine(Sig, PointerHash(C.Get()));
    }
    for (const int32 A : CachedAmounts)
    {
        Sig = HashCombine(Sig, GetTypeHash(A));
    }
    if (Downloader)
    {
        Sig = HashCombine(Sig, GetTypeHash(Downloader->GetSelectedFluid()));
    }
    if (Sig == CachedSig && FluidListBox && FluidListBox->GetChildrenCount() > 0)
    {
        return;
    }
    CachedSig = Sig;

    // ★自检加固：下面每条分支都会用 CurrentText->SetText，而原来只判了 FluidListBox ——
    //   两个成员同在某次 RebuildWidget 里创建，一起判掉更稳。
    if (!FluidListBox || !CurrentText)
    {
        return;
    }
    FluidListBox->ClearChildren();

    if (!Downloader)
    {
        CurrentText->SetText(WT_LOCTEXT("FluidPanel_NoTarget",
            "请在云端液态下载站上打开本面板。"));
        return;
    }

    if (CachedFluids.Num() == 0)
    {
        CurrentText->SetText(WT_LOCTEXT("FluidPanel_NoFluids",
            "云端暂无流体。请将云端流体上传站接入管道。"));
        return;
    }

    const TSubclassOf<UFGItemDescriptor> Selected = Downloader->GetSelectedFluid();
    const FText CurrentFmt = WT_LOCTEXT("FluidPanel_CurrentFmt", "当前：{0}");
    CurrentText->SetText(FText::Format(CurrentFmt,
        Selected ? UFGItemDescriptor::GetItemName(Selected)
                 : WT_LOCTEXT("FluidPanel_None", "<未选择>")));

    // ★自检加固：CachedFluids / CachedAmounts 是并行数组（上面一起 Reset、一起 Add，必然等长），
    //   取二者较小值把「只在一边 continue」的潜在越界隐患一次性钉死。
    const int32 RowCount = FMath::Min(CachedFluids.Num(), CachedAmounts.Num());
    for (int32 i = 0; i < RowCount; ++i)
    {
        const bool bSelected = (CachedFluids[i] == Selected);
        const FText Row = FText::Format(WT_LOCTEXT("FluidPanel_RowFmt", "{0} · {1}"),
            UFGItemDescriptor::GetItemName(CachedFluids[i]),
            FText::AsNumber(CachedAmounts[i]));

        UTextBlock* RowLabel = nullptr;
        UButton* RowButton = FP_MakeButton(WidgetTree, Row,
            bSelected ? P_BTN_SEL : P_BTN, &RowLabel);
        if (RowLabel)
        {
            RowLabel->SetColorAndOpacity(FSlateColor(bSelected ? P_ON_SEL : P_VALUE));
        }

        // UMG 的 OnClicked 是动态多播、带不了参数 —— 与 Remote_FactoryPanelWidget 一样
        // 直达底层 Slate SButton 装闭包（行序号按值捕获）。
        const int32 RowIndex = i;
        RowButton->TakeWidget();
        if (TSharedPtr<SWidget> Cached = RowButton->GetCachedWidget())
        {
            if (TSharedPtr<SButton> SlateButton = StaticCastSharedPtr<SButton>(Cached))
            {
                SlateButton->SetOnClicked(FOnClicked::CreateLambda([this, RowIndex]() -> FReply
                {
                    SelectFluid(RowIndex);
                    return FReply::Handled();
                }));
            }
        }

        FluidListBox->AddChild(RowButton);
    }
}

void UWTFluidSelectPanelWidget::SelectFluid(int32 Index)
{
    if (!CachedFluids.IsValidIndex(Index))
    {
        return;
    }
    if (AWTUpgradeCloudFluidDownloader* Downloader = GetDownloader())
    {
        Downloader->SetSelectedFluid(CachedFluids[Index]);
        RebuildFluidList();   // 立即高亮新选择
    }
}

void UWTFluidSelectPanelWidget::OnRefreshClicked()
{
    CachedSig = 0;   // 强制下个 Tick 全量重建
    RebuildFluidList();
}

void UWTFluidSelectPanelWidget::SetInputMode_Implementation()
{
    // ★1.0.8AV 输入接管（根治「开面板后鼠标转视角、键盘动角色」）。
    //   UFGInteractWidget::SetInputMode 是 BlueprintNativeEvent，SML 桩 _Implementation 为空；
    //   原版面板在蓝图里实现了它（GameAndUI + 光标 + 锁视角/移动）。我们是纯 C++ 面板，
    //   不覆写就什么都没做 ⇒ 输入模式仍是游戏态。游戏在 PushWidget 后会调它，这里接管。
    APlayerController* PC = GetOwningPlayer();
    if (!PC)
    {
        return;
    }
    FInputModeGameAndUI InputMode;
    InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    InputMode.SetHideCursorDuringCapture(false);
    if (TSharedPtr<SWidget> SlateWidget = TakeWidget())
    {
        InputMode.SetWidgetToFocus(SlateWidget.ToSharedRef());
    }
    PC->SetInputMode(InputMode);
    PC->bShowMouseCursor = true;
    // 必须用 Reset 而非 Set(false)：Controller.h 里 SetIgnore* 是计数式叠加，对不上就关面板后角色卡死。
    PC->ResetIgnoreLookInput(); PC->SetIgnoreLookInput(true);
    PC->ResetIgnoreMoveInput(); PC->SetIgnoreMoveInput(true);
}

void UWTFluidSelectPanelWidget::OnCloseClicked()
{
    // ★1.0.8AV 与 SetInputMode_Implementation 严格对称：① 先摘交互栈（防 E 锁死）
    //   ② 再还原输入模式（GameOnly + 隐光标 + Reset* 解除视角/移动锁定）③ 最后 RemoveFromParent。
    WT_FluidUnregisterInteractWidget(this, bInputRestoredBySelf);

    if (APlayerController* PC = GetOwningPlayer())
    {
        FInputModeGameOnly InputMode;
        PC->SetInputMode(InputMode);
        PC->bShowMouseCursor = false;
        PC->ResetIgnoreLookInput();
        PC->ResetIgnoreMoveInput();
    }
    // 标记已主动还原：随后 RemoveFromParent() 触发的 NativeDestruct 可能被 Slate 延后到下一帧，
    // 那份晚到的还原会覆盖用户刚打开的原版背包的 GameAndUI + 光标 ⇒ 背包丢光标。已还原就不再重复。
    bInputRestoredBySelf = true;
    RemoveFromParent();
}

void UWTFluidSelectPanelWidget::NativeDestruct()
{
    // ★1.0.8AV 对称还原兜底（ESC 关面板 / 游戏摘栈 / 看门狗拆毁都走这里）。
    //   未走过 OnCloseClicked（bInputRestoredBySelf 仍 false）才执行，避免覆盖随后打开的原版背包。
    if (!bInputRestoredBySelf)
    {
        WT_FluidUnregisterInteractWidget(this, bInputRestoredBySelf);
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

// ----------------------------------------------------------------------------
// 云端液态上传站 · 只读状态面板（★1.0.8AU：替代不适配的原版位面存储站 UMG 面板）
// ----------------------------------------------------------------------------

TSharedRef<SWidget> UWTFluidStatusPanelWidget::RebuildWidget()
{
    RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>();

    RootBorder = WidgetTree->ConstructWidget<UBorder>();
    RootBorder->SetBrush(*FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")));
    RootBorder->SetBrushColor(P_BG);
    RootBorder->SetPadding(FMargin(0.f));
    RootBorder->SetClipping(EWidgetClipping::ClipToBounds);

    if (UCanvasPanelSlot* CanvasSlot = RootCanvas->AddChildToCanvas(RootBorder))
    {
        CanvasSlot->SetAnchors(FAnchors(0.5f, 0.5f));
        CanvasSlot->SetAlignment(FVector2D(0.5f, 0.5f));
        CanvasSlot->SetSize(FVector2D(1180.f, 760.f));
    }

    UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>();

    // ---- title row -----------------------------------------------------------
    UHorizontalBox* TitleRow = WidgetTree->ConstructWidget<UHorizontalBox>();
    TitleText = FP_MakeText(WidgetTree,
        WT_LOCTEXT("FluidStatus_Title", "云端液态上传站 · 云端流体状态（只读）"), 22, true, P_ACCENT);
    if (UHorizontalBoxSlot* S = TitleRow->AddChildToHorizontalBox(TitleText))
    {
        S->SetPadding(FMargin(2.f, 0.f, 12.f, 0.f));
        S->SetVerticalAlignment(VAlign_Center);
        S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }

    UButton* CloseButton = FP_MakeButton(WidgetTree, WT_LOCTEXT("Btn_Close", "关闭"), P_BTN_SEL);
    CloseButton->OnClicked.AddDynamic(this, &UWTFluidStatusPanelWidget::OnCloseClicked);
    if (UHorizontalBoxSlot* S = TitleRow->AddChildToHorizontalBox(CloseButton))
    {
        S->SetPadding(FMargin(4.f, 0.f));
        S->SetVerticalAlignment(VAlign_Center);
    }
    Root->AddChildToVerticalBox(TitleRow);
    Root->AddChildToVerticalBox(FP_MakeDivider(WidgetTree));   // ★1.0.8AV 与固体面板同款标题下分隔线

    // ---- current selection / hint line ---------------------------------------
    CurrentText = FP_MakeText(WidgetTree, FText::GetEmpty(), 13, false, P_GREEN);
    if (UVerticalBoxSlot* S = Root->AddChildToVerticalBox(CurrentText))
    {
        S->SetPadding(FMargin(2.f, 8.f));
    }

    // ---- fluid rows ----------------------------------------------------------
    FluidListBox = WidgetTree->ConstructWidget<UScrollBox>();
    if (UVerticalBoxSlot* S = Root->AddChildToVerticalBox(FluidListBox))
    {
        S->SetPadding(FMargin(0.f, 4.f));
        S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }

    RootBorder->SetContent(Root);
    if (UBorderSlot* S = Cast<UBorderSlot>(RootBorder->GetContentSlot()))
    {
        S->SetHorizontalAlignment(HAlign_Fill);
        S->SetVerticalAlignment(VAlign_Fill);
        S->SetPadding(FMargin(16.f));   // ★1.0.8AV 与固体面板对齐：内边距下沉到内容槽
    }
    return RootCanvas->TakeWidget();
}

void UWTFluidStatusPanelWidget::NativeConstruct()
{
    Super::NativeConstruct();

    // ★1.0.8AV 与固体面板/终端面板同一套视口自适应：视口 90% 宽 / 85% 高，上限 1280x820，居中。
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

    WT_LOGF(TEXT("[fluid-status] NativeConstruct (uploader=%s)."),
            GetUploader() ? *GetUploader()->GetName() : TEXT("<none>"));
    RebuildFluidList();
}

void UWTFluidStatusPanelWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    RebuildFluidList();
}

AWTUpgradeCloudFluidUploader* UWTFluidStatusPanelWidget::GetUploader() const
{
    if (FObjectProperty* OP = CastField<FObjectProperty>(
            UFGInteractWidget::StaticClass()->FindPropertyByName(TEXT("mInteractObject"))))
    {
        UObject* Raw = OP->GetObjectPropertyValue(OP->ContainerPtrToValuePtr<void>(this));
        return Cast<AWTUpgradeCloudFluidUploader>(Raw);
    }
    return nullptr;
}

void UWTFluidStatusPanelWidget::RebuildFluidList()
{
    AWTUpgradeCloudFluidUploader* Uploader = GetUploader();

    // 聚合我们自己的液态账本云（所有上传站的 mFluidLedger）+ 原版中央存储里的流体（合并计数）。
    TMap<TSubclassOf<UFGItemDescriptor>, int32> Aggregate;
    if (UWorld* World = GetWorld())
    {
        for (TActorIterator<AWTUpgradeCloudFluidUploader> It(World); It; ++It)
        {
            AWTUpgradeCloudFluidUploader* Station = *It;
            if (!Station)
            {
                continue;
            }
            for (const FItemAmount& IA : Station->GetFluidLedger())
            {
                if (IA.ItemClass.Get() == nullptr || IA.Amount <= 0)
                {
                    continue;
                }
                Aggregate.FindOrAdd(IA.ItemClass) += IA.Amount;
            }
        }

        if (AFGCentralStorageSubsystem* Sub = AFGCentralStorageSubsystem::Get(World))
        {
            TArray<FItemAmount> CloudItems;
            Sub->GetAllItemsFromCentralStorage(CloudItems);
            for (const FItemAmount& IA : CloudItems)
            {
                if (IA.ItemClass.Get() == nullptr || IA.Amount <= 0)
                {
                    continue;
                }
                const EResourceForm Form = UFGItemDescriptor::GetForm(IA.ItemClass);
                if (Form == EResourceForm::RF_LIQUID || Form == EResourceForm::RF_GAS)
                {
                    Aggregate.FindOrAdd(IA.ItemClass) += IA.Amount;
                }
            }
        }
    }

    CachedFluids.Reset();
    CachedAmounts.Reset();
    for (const TPair<TSubclassOf<UFGItemDescriptor>, int32>& Pair : Aggregate)
    {
        CachedFluids.Add(Pair.Key);
        CachedAmounts.Add(Pair.Value);
    }

    // 签名守卫：仅在内容变化时重建行。
    uint32 Sig = 0;
    for (const TSubclassOf<UFGItemDescriptor>& C : CachedFluids)
    {
        Sig = HashCombine(Sig, PointerHash(C.Get()));
    }
    for (const int32 A : CachedAmounts)
    {
        Sig = HashCombine(Sig, GetTypeHash(A));
    }
    if (Sig == CachedSig && FluidListBox && FluidListBox->GetChildrenCount() > 0)
    {
        return;
    }
    CachedSig = Sig;

    if (!FluidListBox || !CurrentText)
    {
        return;
    }
    FluidListBox->ClearChildren();

    if (!Uploader)
    {
        CurrentText->SetText(WT_LOCTEXT("FluidStatus_NoTarget", "请在云端液态上传站上打开本面板。"));
        return;
    }

    if (CachedFluids.Num() == 0)
    {
        CurrentText->SetText(WT_LOCTEXT("FluidStatus_Empty",
            "云端暂无流体。请将上传站接入管道，流体将自动上传到这里。"));
        return;
    }

    CurrentText->SetText(WT_LOCTEXT("FluidStatus_Hint", "云端液态云当前库存（上传站持续写入）："));

    const int32 RowCount = FMath::Min(CachedFluids.Num(), CachedAmounts.Num());
    for (int32 i = 0; i < RowCount; ++i)
    {
        const FText Row = FText::Format(WT_LOCTEXT("FluidStatus_RowFmt", "{0} · {1}"),
            UFGItemDescriptor::GetItemName(CachedFluids[i]),
            FText::AsNumber(CachedAmounts[i]));
        UTextBlock* RowLabel = nullptr;
        UButton* RowButton = FP_MakeButton(WidgetTree, Row, P_BTN, &RowLabel);
        if (RowLabel)
        {
            RowLabel->SetColorAndOpacity(FSlateColor(P_VALUE));
        }
        FluidListBox->AddChild(RowButton);
    }
}

void UWTFluidStatusPanelWidget::SetInputMode_Implementation()
{
    // ★1.0.8AV 输入接管（同下载站选型面板，根治「开面板后鼠标转视角、键盘动角色」）。
    APlayerController* PC = GetOwningPlayer();
    if (!PC)
    {
        return;
    }
    FInputModeGameAndUI InputMode;
    InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    InputMode.SetHideCursorDuringCapture(false);
    if (TSharedPtr<SWidget> SlateWidget = TakeWidget())
    {
        InputMode.SetWidgetToFocus(SlateWidget.ToSharedRef());
    }
    PC->SetInputMode(InputMode);
    PC->bShowMouseCursor = true;
    PC->ResetIgnoreLookInput(); PC->SetIgnoreLookInput(true);
    PC->ResetIgnoreMoveInput(); PC->SetIgnoreMoveInput(true);
}

void UWTFluidStatusPanelWidget::OnCloseClicked()
{
    // ★1.0.8AV 与下载站面板同套对称还原（摘栈 → 还原输入 → RemoveFromParent）。
    WT_FluidUnregisterInteractWidget(this, bInputRestoredBySelf);

    if (APlayerController* PC = GetOwningPlayer())
    {
        FInputModeGameOnly InputMode;
        PC->SetInputMode(InputMode);
        PC->bShowMouseCursor = false;
        PC->ResetIgnoreLookInput();
        PC->ResetIgnoreMoveInput();
    }
    bInputRestoredBySelf = true;
    RemoveFromParent();
}

void UWTFluidStatusPanelWidget::NativeDestruct()
{
    // ★1.0.8AV 对称还原兜底（同下载站面板）。
    if (!bInputRestoredBySelf)
    {
        WT_FluidUnregisterInteractWidget(this, bInputRestoredBySelf);
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
