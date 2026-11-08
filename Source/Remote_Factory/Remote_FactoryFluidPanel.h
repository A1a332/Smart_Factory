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
class AWTUpgradeCloudFluidUploader;

/**
 * 云端液态下载站 · 流体选型面板（★本版恢复：枚举**我们自己的液态账本云**）。
 *
 * 纯 C++ 构建（WidgetTree->ConstructWidget，参考 Remote_FactoryPanelWidget 先例，
 * 本模组不发行任何 UMG 资产）。布局：
 *
 *   +--------------------------------------------------+
 *   |  云端液态下载站 · 选择流体            [刷新] [关闭] |
 *   |  当前：<流体名 / 未选择>                           |
 *   |  ------------------------------------------------ |
 *   |  [ 水 · 12,000 ]                                  |
 *   |  [ 原油 · 3,500 ]        <- 云端里现有的流体        |
 *   |  [ 氮气 · 800 ]                                   |
 *   +--------------------------------------------------+
 *
 * 每行 = 云端里现有的一种流体，点击即写回下载站的 mSelectedFluidClass。
 * 流体来源 = **我们自己的液态账本云**（聚合所有云端流体上传站的 mFluidLedger）
 *           + 原版中央存储里的流体（下载站取货时先查原版池，故一并列出，合并计数）。
 * 单人/主机模式下面板与建筑同进程，直接写属性是安全的（与固体面板同一条约定）。
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

    /** Rebuild the fluid rows from our fluid ledger + the vanilla cloud (signature-guarded). */
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

    // ★1.0.8AV 输入接管：UFGInteractWidget::SetInputMode 是 BlueprintNativeEvent，原版面板在蓝图里
    //   实现了它（GameAndUI + 光标 + 锁视角/移动）；我们的纯 C++ 面板必须自己覆写，否则默认空实现
    //   ⇒ 开面板后鼠标仍转视角、键盘仍动角色。
    virtual void SetInputMode_Implementation() override;

    // ★1.0.8AV 对称还原兜底：ESC 关面板 / 游戏摘栈 / 看门狗拆毁走这里；未主动还原时才还原（防覆盖背包）。
    virtual void NativeDestruct() override;

    // 已主动还原输入模式的标记，避免 NativeDestruct（可能被 Slate 延后）把随后打开的原版背包
    //   刚设好的 GameAndUI + 光标按回 GameOnly（与 ItemSelectPanel 同款防御）。
    bool bInputRestoredBySelf = false;
};

/**
 * 云端液态上传站 · 云端流体状态面板（只读）。
 *
 * 上传站是「自动把管网流体吸入并上传到位面存储云」的机器，本身不需要选型；
 * 但用户按 E 时应有一个面板可看：当前云端液态云里有哪些流体、各多少。
 * 原版位面存储站的 UMG 面板绑定的是存储库存，上传站（管道缓冲罐）没有那种库存，
 * 复用它会弹空或干脆不弹 —— 故这里用我们自己的原生只读面板（与选型面板同套路，
 * 本模组不发行任何 UMG 资产）。
 */
UCLASS()
class REMOTE_FACTORY_API UWTFluidStatusPanelWidget : public UFGInteractWidget
{
    GENERATED_BODY()
public:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

protected:
    UFUNCTION()
    void OnCloseClicked();

    AWTUpgradeCloudFluidUploader* GetUploader() const;
    void RebuildFluidList();

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

    // 云端现有流体（行序 = CachedFluids 序）。必须反射，GC 才能看到描述符类引用。
    UPROPERTY()
    TArray<TSubclassOf<UFGItemDescriptor>> CachedFluids;

    TArray<int32> CachedAmounts;
    uint32 CachedSig = 0;

    // ★1.0.8AV 输入接管：UFGInteractWidget::SetInputMode 是 BlueprintNativeEvent，原版面板在蓝图里
    //   实现了它（GameAndUI + 光标 + 锁视角/移动）；我们的纯 C++ 面板必须自己覆写，否则默认空实现
    //   ⇒ 开面板后鼠标仍转视角、键盘仍动角色。
    virtual void SetInputMode_Implementation() override;

    // ★1.0.8AV 对称还原兜底：ESC 关面板 / 游戏摘栈 / 看门狗拆毁走这里；未主动还原时才还原（防覆盖背包）。
    virtual void NativeDestruct() override;

    // 已主动还原输入模式的标记，避免 NativeDestruct（可能被 Slate 延后）把随后打开的原版背包
    //   刚设好的 GameAndUI + 光标按回 GameOnly（与 ItemSelectPanel 同款防御）。
    bool bInputRestoredBySelf = false;
};
