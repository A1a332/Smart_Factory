#include "Remote_FactoryFluidPanel.h"

#include "Remote_FactoryLoc.h"
#include "Remote_FactoryLog.h"
#include "Remote_FactoryUpgrades.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
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
#include "FGCentralStorageSubsystem.h"
#include "Resources/FGItemDescriptor.h"
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

// ----------------------------------------------------------------------------

TSharedRef<SWidget> UWTFluidSelectPanelWidget::RebuildWidget()
{
    RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>();

    RootBorder = WidgetTree->ConstructWidget<UBorder>();
    RootBorder->SetBrush(*FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")));
    RootBorder->SetBrushColor(P_BG);
    RootBorder->SetPadding(FMargin(16.f));
    RootBorder->SetClipping(EWidgetClipping::ClipToBounds);

    if (UCanvasPanelSlot* CanvasSlot = RootCanvas->AddChildToCanvas(RootBorder))
    {
        CanvasSlot->SetAnchors(FAnchors(0.5f, 0.5f));
        CanvasSlot->SetAlignment(FVector2D(0.5f, 0.5f));
        CanvasSlot->SetSize(FVector2D(520.f, 480.f));
    }

    UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>();

    // ---- title row -----------------------------------------------------------
    UHorizontalBox* TitleRow = WidgetTree->ConstructWidget<UHorizontalBox>();
    TitleText = FP_MakeText(WidgetTree,
        WT_LOCTEXT("FluidPanel_Title", "Cloud Fluid Download Station · Pick a Fluid"), 18, true, P_ACCENT);
    if (UHorizontalBoxSlot* S = TitleRow->AddChildToHorizontalBox(TitleText))
    {
        S->SetPadding(FMargin(2.f, 0.f, 12.f, 0.f));
        S->SetVerticalAlignment(VAlign_Center);
        S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }

    UButton* RefreshButton = FP_MakeButton(WidgetTree, WT_LOCTEXT("Btn_Refresh", "Refresh"), P_BTN);
    RefreshButton->OnClicked.AddDynamic(this, &UWTFluidSelectPanelWidget::OnRefreshClicked);
    if (UHorizontalBoxSlot* S = TitleRow->AddChildToHorizontalBox(RefreshButton))
    {
        S->SetPadding(FMargin(4.f, 0.f));
        S->SetVerticalAlignment(VAlign_Center);
    }

    UButton* CloseButton = FP_MakeButton(WidgetTree, WT_LOCTEXT("Btn_Close", "Close"), P_BTN_SEL);
    CloseButton->OnClicked.AddDynamic(this, &UWTFluidSelectPanelWidget::OnCloseClicked);
    if (UHorizontalBoxSlot* S = TitleRow->AddChildToHorizontalBox(CloseButton))
    {
        S->SetPadding(FMargin(4.f, 0.f));
        S->SetVerticalAlignment(VAlign_Center);
    }
    Root->AddChildToVerticalBox(TitleRow);

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
    return RootCanvas->TakeWidget();
}

void UWTFluidSelectPanelWidget::NativeConstruct()
{
    Super::NativeConstruct();
    RebuildFluidList();
}

void UWTFluidSelectPanelWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    // 云端池是别人在写（固态上传站等），周期刷新但只在内容变化时才重建行。
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

    // ---- collect the fluid rows ----------------------------------------------
    CachedFluids.Reset();
    CachedAmounts.Reset();

    if (UWorld* World = GetWorld())
    {
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
                    CachedFluids.Add(IA.ItemClass);
                    CachedAmounts.Add(IA.Amount);
                }
            }
        }
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

    // ---- rebuild ----------------------------------------------------------------
    // ★自检加固（2026-10-07）：下面每条分支都会用 CurrentText->SetText，而原来只判了
    //   FluidListBox —— 两个成员同在某次 RebuildWidget 里创建，一起判掉更稳。
    if (!FluidListBox || !CurrentText)
    {
        return;
    }
    FluidListBox->ClearChildren();

    if (!Downloader)
    {
        CurrentText->SetText(WT_LOCTEXT("FluidPanel_NoTarget", "Open this panel on a Cloud Fluid Download Station."));
        return;
    }

    if (CachedFluids.Num() == 0)
    {
        CurrentText->SetText(WT_LOCTEXT("FluidPanel_NoFluids",
            "The cloud holds no fluids yet. Connect a Cloud Fluid Upload Station to a pipe."));
        return;
    }

    const TSubclassOf<UFGItemDescriptor> Selected = Downloader->GetSelectedFluid();
    const FText CurrentFmt = WT_LOCTEXT("FluidPanel_CurrentFmt", "Current: {0}");
    CurrentText->SetText(FText::Format(CurrentFmt,
        Selected ? UFGItemDescriptor::GetItemName(Selected)
                 : WT_LOCTEXT("FluidPanel_None", "<none>")));

    // ★2026-10-07 自检加固：CachedFluids / CachedAmounts 是一对**并行数组**（上方 161/162 一起
    //   Reset、179/180 一起 Add，当前必然等长）。但循环只拿了 CachedFluids 的长度去索引
    //   CachedAmounts —— 将来任何一次「只在一边 continue」的改动都会变成静默越界（读越界在
    //   Shipping 下不报错，直接读到垃圾 int32 或直接崩）。这里取二者较小值，把这条隐患一次性钉死。
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

void UWTFluidSelectPanelWidget::OnCloseClicked()
{
    // 原版交互面板的关闭入口（UFGInteractWidget 的收尾走同一条 vanilla 栈）。
    RemoveFromParent();
}
