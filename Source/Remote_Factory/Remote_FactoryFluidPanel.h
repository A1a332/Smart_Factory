#pragma once

#include "CoreMinimal.h"
#include "UI/FGInteractWidget.h"
#include "Remote_FactoryFluidPanel.generated.h"

class UBorder;
class UButton;
class UCanvasPanel;
class UScrollBox;
class UTextBlock;
class UVerticalBox;
class UFGItemDescriptor;
class AWTUpgradeCloudFluidDownloader;

/**
 * 云端液态下载站 · 流体选型面板（★0.2.36 方案B，用户定案）。
 *
 * 纯 C++ 构建（WidgetTree->ConstructWidget，参考 Remote_FactoryPanelWidget 先例，
 * 本模组不发行任何 UMG 资产）。布局：
 *
 *   +--------------------------------------------------+
 *   |  云端液态下载站 · 选择流体            [刷新] [关闭] |
 *   |  当前：<流体名 / 未选择>                           |
 *   |  ------------------------------------------------ |
 *   |  [ 水 · 12,000 ]                                  |
 *   |  [ 原油 · 3,500 ]        <- 云端池里现有的流体      |
 *   |  [ 氮气 · 800 ]                                   |
 *   +--------------------------------------------------+
 *
 * 每行 = 云端池（AFGCentralStorageSubsystem::mStoredItems）里的一种流体
 * （RF_LIQUID / RF_GAS），点击即写回下载站的 mSelectedFluidClass。
 * 单人/主机模式下面板与建筑同进程，直接写是安全的；专用服务器的回写 RPC 留作后续。
 */
UCLASS()
class REMOTE_FACTORY_API UWTFluidSelectPanelWidget : public UFGInteractWidget
{
    GENERATED_BODY()
public:
    // Begin UUserWidget interface
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
    // End UUserWidget interface

protected:
    UFUNCTION()
    void OnRefreshClicked();

    UFUNCTION()
    void OnCloseClicked();

private:
    /** The downloader this panel was opened for (via the vanilla mInteractObject wiring). */
    AWTUpgradeCloudFluidDownloader* GetDownloader() const;

    /** Rebuild the fluid rows from the cloud pool (signature-guarded, so idle panels cost nothing). */
    void RebuildFluidList();

    /** A fluid row was clicked, carrying its index into CachedFluids. */
    void SelectFluid(int32 Index);

    // ---- widgets ----
    UPROPERTY()
    TObjectPtr<UCanvasPanel> RootCanvas = nullptr;

    UPROPERTY()
    TObjectPtr<UBorder> RootBorder = nullptr;

    UPROPERTY()
    TObjectPtr<UTextBlock> TitleText = nullptr;

    UPROPERTY()
    TObjectPtr<UTextBlock> CurrentText = nullptr;

    UPROPERTY()
    TObjectPtr<UScrollBox> FluidListBox = nullptr;

    // ---- state ----
    /** Fluid classes currently shown (row order = CachedFluids order).
     *  ★ CRASH GUARD: TSubclassOf holds a UClass*, so this must be reflected or the GC cannot
     *  see the reference and a descriptor class could be recycled out from under a live panel. */
    UPROPERTY()
    TArray<TSubclassOf<UFGItemDescriptor>> CachedFluids;

    /** Amounts snapshot for the signature check (parallel to CachedFluids). */
    TArray<int32> CachedAmounts;

    /** Signature of the last rebuild; avoids re-creating rows every tick. */
    uint32 CachedSig = 0;
};
