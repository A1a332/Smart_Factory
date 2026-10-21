#pragma once

#include "UI/FGInteractWidget.h"
#include "Containers/Set.h"   // ★v0.2.76 TSet ExpandedMachineGroups（显式包含，不赌传递包含）
#include "Remote_FactoryTypes.h"
#include "Remote_FactoryButtonInfo.h"
#include "Remote_FactoryPanelWidget.generated.h"

class UBorder;
class UCanvasPanel;
class UButton;
class UProgressBar;
class UTextBlock;
class UVerticalBox;
class UScrollBox;
class UHorizontalBox;
class AFGBuildable;
class URemote_FactoryRCO;

/** ★v0.2.15 用户按下面板上的 ✕ 关闭按钮。持有方（手持装备）监听并做完整收尾（摘栈+清指针）。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FWTOnPanelCloseRequested);

/**
 * The Remote Factory control panel.
 *
 * Layout:
 *
 *   +-----------------------------------------------------------------+
 *   |  无线终端 · 全厂控制台                     [ 刷新 ]  [ 范围 ]      |
 *   +------------------+----------------------------------------------+
 *   |  分类            |  机器详情                                     |
 *   |  [生产设备  128] |  名称 / 距离 / 状态                           |
 *   |  [容器       64] |  ------------------------------------------   |
 *   |  [采矿设备   12] |  产物列表（品名 x 数量）                       |
 *   |  [发电机     24] |  ------------------------------------------   |
 *   |  [资源回收站  1] |  [ 打开机器面板 ]   [ 取出产物 ]               |
 *   |  [枢纽        1] |                                              |
 *   |  [太空电梯    1] |                                              |
 *   |  [工作台      2] |                                              |
 *   |  [分子分析机  1] |                                              |
 *   |  [其他        N] |                                              |
 *   +------------------+----------------------------------------------+
 *
 * The whole UI is built in C++ (WidgetTree->ConstructWidget) because the mod ships without UMG
 * assets — same approach as WirelessPowerPanelWidget. Every action goes to the server through
 * URemote_FactoryRCO; the machine list comes back as a replicated reply.
 */
UCLASS()
class REMOTE_FACTORY_API URemote_FactoryPanelWidget : public UFGInteractWidget
{
    GENERATED_BODY()
public:
    URemote_FactoryPanelWidget(const FObjectInitializer& ObjectInitializer);

    // Begin UUserWidget interface
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
    // End UUserWidget interface

    /** ESC closes the panel. FGGameUI stack registration is disabled (v0.2.20, third-party mod
     *  crash), so ESC can no longer rely on the game's interact-widget path -- capture it here. */
    virtual FReply NativeOnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

protected:
    /** 刷新: ask the server for a new scan. */
    UFUNCTION()
    void OnRefreshClicked();

    /** 打开机器面板: ask the server to open the selected machine's native UI. */
    UFUNCTION()
    void OnOpenMachineClicked();

    /** 取出产物: ask the server to drain the selected machine's output into the player. */
    UFUNCTION()
    void OnPullOutputClicked();

    /** 传送到机器: fly the player to the selected machine. */
    UFUNCTION()
    void OnTeleportToMachineClicked();

    /** 传送到目标: fly the player to the selected alien-energy collectible. */
    UFUNCTION()
    void OnTeleportToCollectibleClicked();

    /** ★v0.2.15 ✕ 关闭按钮：广播关闭请求（无人监听时兜底自移除）。 */
    UFUNCTION()
    void OnCloseClicked();

public:
    /** ★v0.2.15 关闭按钮事件。手持装备绑定它走 ClosePanel() 完整收尾（摘 GameUI 栈+清指针）。 */
    UPROPERTY(BlueprintAssignable)
    FWTOnPanelCloseRequested PanelCloseRequested;

protected:
    // ---- Form 3 "A": power switch controls ------------------------------------
    /** 合闸 / 断开 the selected switch. Sends the OPPOSITE of its current state. */
    UFUNCTION()
    void OnToggleSwitchClicked();

    /** Step the selected priority switch's shed priority up by one. */
    UFUNCTION()
    void OnPriorityUpClicked();

    /** Step the selected priority switch's shed priority down by one. */
    UFUNCTION()
    void OnPriorityDownClicked();

    /** 不自动限电: set the priority to 0, which exempts the switch from automatic shedding. */
    UFUNCTION()
    void OnPriorityClearClicked();

    // ---- list row handlers (called from the Slate lambdas installed in AddListButton) ----
    /** A category button was clicked, carrying its EWT_MachineCategory value. */
    void SelectCategory(int32 CategoryIndex);

    /** A machine row was clicked, carrying its index into CurrentList.Entries. */
    void SelectEntry(int32 EntryIndex);

    /** A collectible row was clicked, carrying its index into CurrentList.Collectibles. */
    void SelectCollectible(int32 CollectibleIndex);

    /** ★0.2.62 A top-level group toggle button was clicked (建筑 / 容器 / 外星科技). TopIndex = group id. */
    void ToggleTopCategory(int32 TopIndex);

    /** ★0.2.62 An alien-energy KIND selector was clicked. Encoded as kind*3 + (state+1):
     *  state -1 = the kind row itself (全部), 0 = 未拾取, 1 = 已拾取. Clears machine selection. */
    void SelectCollectibleKind(int32 Encoded);

    /** ★0.2.64 How many of one alien-energy kind are in the list. Picked: -1 = both,
     *  0 = only 未拾取, 1 = only 已拾取. */
    int32 CountCollectibles(int32 Kind, int32 Picked) const;

    /** ★0.2.62 Total entry count under a top-level group (0=建筑, 1=容器, 2=外星科技). */
    int32 TopCount(int32 TopIndex) const;

private:
    /** Route a server change through the player controller's RCO. */
    URemote_FactoryRCO* GetRCO() const;

    /**
     * The terminal this panel was opened for, or null (the handheld form has none).
     *
     * Used only by Form 3 "D" to scope a shared session to one terminal, so two consoles in a big
     * base get independent owners instead of fighting over a single global one.
     */
    AFGBuildable* GetInteractBuildable() const;

    /** Bind to RCO events / unbind on teardown. */
    void BindRCO();
    void UnbindRCO();

    /** Build one row button for the left-hand category list. */
    void RebuildCategoryList();

    /**
     * Create a list button and wire its click to OnClicked with this row's payload captured.
     *
     * We reach through UButton's underlying Slate SButton because the UMG-level OnClicked delegate
     * cannot carry an argument. The lambda captures a plain FWT_ButtonInfo by value, so each row
     * gets its own closure — no lookup table, no guessing which button fired.
     */
    UButton* AddListButton(const FText& Label, bool bSelected, const FWT_ButtonInfo& Info, bool bEnabled,
                           UTextBlock** OutLabel = nullptr);

    /** Fill the right-hand detail pane for the selected machine. */
    void RebuildMachineList();

    /** ★0.2.70 refresh the distance text of the currently drawn collectible rows from the live player
     *  location, without rebuilding the (potentially hundreds of) widgets. Called throttled from
     *  NativeTick. */
    void UpdateCollectibleDistances();


    /** Redraw the detail pane (selected machine's name, status, output contents). */
    void RebuildDetailPane();

    /**
     * Show, hide and label the Form 3 "A" switch controls for the currently selected row.
     *
     * Kept separate from RebuildDetailPane so the switch block's visibility rules live in one place:
     * a plain machine hides the whole block, a switch shows it, and an unwired switch shows it in a
     * disabled state with the reason spelled out.
     */
    void RefreshSwitchControls();

    /** Update the factory-wide power line and its bar from CurrentList. */
    void RefreshPowerReadout();

    /** Form 3 "D": show / hide / reword the shared-session banner for this player. */
    void RefreshSharedSessionBanner();

    /**
     * Draw the alien-energy board: one row per kind (能量蛞蝓 / 梅瑟尔球 / 索莫晶体 / 空投舱)
     * followed by that kind's individual locations with their world coordinates.
     */
    void RebuildCollectibleList();

    /** ★0.2.69 Rebuild DisplayCollectibles from collectibles.json (full map) overlaid with the live
     *  scan's picked/loaded state. Called on open and after every scan reply. */
    void RebuildDisplayCollectibles();

    /** ★0.2.69 Official localized map-area name at a world position (client PC's map-area texture).
     *  Returns an empty text when no map-area volume contains the point or the PC is unavailable. */
    FText ComputeRegionForLocation(const FVector& Loc) const;

    // ---- RCO callbacks ----
    void HandleMachineListReceived(const FWT_MachineListReply& Reply);
    void HandleInteractUIRequested(AFGBuildable* Target);
    void HandlePullResult(AFGBuildable* Target, int32 ItemsMoved, bool bSuccess);
    /** Reason is a WTTeleportFail::Type (0.2.135): distinguishes "target gone" from "refused, no safe landing". */
    void HandleTeleportResult(AActor* Target, bool bSuccess, int32 Reason);

    /** Form 3 "A": the server's answer to a switch write. */
    void HandleSwitchResult(AFGBuildable* Target, bool bSuccess, const FText& Message);

    // ---- widgets ----
    /** ★v0.2.24 根画布：把面板按视口比例居中限宽（原来根是 RootBorder，会铺满全屏）。 */
    UPROPERTY()
    TObjectPtr<class UCanvasPanel> RootCanvas = nullptr;

    UPROPERTY()
    TObjectPtr<UBorder> RootBorder = nullptr;

    UPROPERTY()
    TObjectPtr<UTextBlock> TitleText = nullptr;

    UPROPERTY()
    TObjectPtr<UTextBlock> StatusText = nullptr;

    /** Factory-wide power overview line (发电 / 耗电 / 容量). */
    UPROPERTY()
    TObjectPtr<UTextBlock> PowerText = nullptr;

    /** Thin bar under the power line; its fill width tracks consumed/capacity. */
    UPROPERTY()
    TObjectPtr<UProgressBar> PowerBar = nullptr;

    UPROPERTY()
    TObjectPtr<UButton> RefreshButton = nullptr;

    /** Left column: one button per category, rebuilt whenever counts change. v0.2.55 redesign:
     * 分类标签移到左侧一列 (vertical stack, per user's layout). */
    UPROPERTY()
    TObjectPtr<class UVerticalBox> CategoryBox = nullptr;

    /** Middle column: the machine rows for the selected category. */
    UPROPERTY()
    TObjectPtr<UScrollBox> MachineListBox = nullptr;

    /** Right column: details + action buttons for the selected machine. */
    UPROPERTY()
    TObjectPtr<UVerticalBox> DetailBox = nullptr;

    UPROPERTY()
    TObjectPtr<UTextBlock> DetailNameText = nullptr;

    UPROPERTY()
    TObjectPtr<UTextBlock> DetailStatusText = nullptr;

    UPROPERTY()
    TObjectPtr<UTextBlock> DetailOutputText = nullptr;

    UPROPERTY()
    TObjectPtr<UButton> OpenMachineButton = nullptr;

    UPROPERTY()
    TObjectPtr<UButton> PullOutputButton = nullptr;

    UPROPERTY()
    TObjectPtr<UButton> TeleportToMachineButton = nullptr;

    // ---- Form 3 "A": the switch operation block ----
    // Hidden entirely unless the selected row is a power switch; see RefreshSwitchControls().

    /** Owns every control below, so the whole block can be shown/hidden with one SetVisibility. */
    UPROPERTY()
    TObjectPtr<UVerticalBox> SwitchBox = nullptr;

    UPROPERTY()
    TObjectPtr<UTextBlock> SwitchHeaderText = nullptr;

    /** One-line state readout ("On · Priority 3" / "Off" / "Not wired"). */
    UPROPERTY()
    TObjectPtr<UTextBlock> SwitchStateText = nullptr;

    /** 合闸 / 断开 -- toggles whatever the switch is currently not. */
    UPROPERTY()
    TObjectPtr<UButton> SwitchToggleButton = nullptr;

    /** Label inside SwitchToggleButton, retargeted on every rebuild ("Turn on" vs "Turn off"). */
    UPROPERTY()
    TObjectPtr<UTextBlock> SwitchToggleLabel = nullptr;

    UPROPERTY()
    TObjectPtr<UButton> PriorityDownButton = nullptr;

    UPROPERTY()
    TObjectPtr<UButton> PriorityUpButton = nullptr;

    /** 不自动限电 -- sets priority to 0, the "never shed me" state. */
    UPROPERTY()
    TObjectPtr<UButton> PriorityClearButton = nullptr;

    // ---- Form 3 "D": shared-session banner ----

    /** "Host: Alice  (2 others watching)" -- hidden in single-player and when nobody else watches. */
    UPROPERTY()
    TObjectPtr<UTextBlock> SharedSessionText = nullptr;

    /** Right column, lower half: the alien-energy collectible board. */
    UPROPERTY()
    TObjectPtr<UTextBlock> CollectibleHeaderText = nullptr;

    UPROPERTY()
    TObjectPtr<UScrollBox> CollectibleListBox = nullptr;

    UPROPERTY()
    TObjectPtr<UTextBlock> CollectibleDetailText = nullptr;

    UPROPERTY()
    TObjectPtr<UButton> TeleportToCollectibleButton = nullptr;

    // ---- state ----
    /** Latest scan result from the server.
     *
     *  ★ CRASH GUARD: this MUST stay a UPROPERTY. The struct carries TObjectPtr / FText /
     *  TArray members (FWT_MachineEntry::Target, FWT_CollectibleEntry::Actor, the many
     *  FText labels). GC only walks *into* a USTRUCT if the holding property is itself
     *  reflected -- without UPROPERTY here the whole tree is invisible to the collector,
     *  so a destroyed buildable or a picked-up collectible can leave a dangling pointer
     *  behind this widget and crash the panel at a random later tick. */
    UPROPERTY()
    FWT_MachineListReply CurrentList;

    /** ★0.2.69 file-driven collectible board: the merged list (collectibles.json full-map spawns
     *  overlaid with the live scan's picked/loaded state) that RebuildCollectibleList / CountCollectibles
     *  / RebuildMachineList's collectible branch all read from. Rebuilt in RebuildDisplayCollectibles.
     *
     *  UPROPERTY for the same GC-visibility reason as CurrentList: it holds FWT_CollectibleEntry
     *  structs with TObjectPtr<AActor> Actor + FText fields. */
    UPROPERTY()
    TArray<FWT_CollectibleEntry> DisplayCollectibles;

    /** ★0.2.69 true once collectibles.json parsed successfully (even with an empty spawns array).
     *  Used to decide whether to show file-based counts or fall back to the wiki whole-map totals. */
    bool bCollectibleFileLoaded = false;

    // ---- ★0.2.127: per-pickup "already collected" memory (see Remote_FactoryCollectibles.h) ----
    /** Reloaded from RF_collected.json on EVERY rebuild (the pickup monitor owns the file and may
     *  have recorded new pickups while the panel was closed). */
    TSet<FString> CollectedSpawnKeys;

    /** Which category tab is showing. */
    int32 SelectedCategory = static_cast<int32>(EWT_MachineCategory::Producer);

    /** Which entry in the filtered list is selected (-1 = none). */
    int32 SelectedEntry = -1;

    /** Which collectible row is selected (-1 = none). */
    int32 SelectedCollectible = -1;

    // ---- ★0.2.62: three-group folding ----
    /** Collapse state of the three top-level groups: 0=建筑, 1=容器, 2=外星科技. true = expanded. */
    bool bTopExpanded[3] = { true, false, false };

    // ---- ★0.2.67: per-kind folding for 外星科技 ----
    /** Collapse state of each alien-energy kind (indexed by EWT_CollectibleKind). true = expanded. */
    bool bKindExpanded[static_cast<int32>(EWT_CollectibleKind::Count)] = { false, false, false, false, false, false, false, false, false };

    // ---- ★v0.2.76: machine-type folding（同类型机器折叠）----
    /** Machine-type groups the user expanded. Key = "<Category>|<DisplayName>".
     *  Deliberately NOT cleared on the 15s periodic rescan (expansion must survive rebuilds). */
    UPROPERTY(Transient)
    TSet<FString> ExpandedMachineGroups;

    /** Selected alien-energy kind (-1 = none; 0..3 = EWT_CollectibleKind). Mutually exclusive with SelectedCategory. */
    int32 SelectedCollectibleKind = -1;

    /** ★0.2.64 Picked-up state filter that goes with SelectedCollectibleKind:
     *  -1 = 全部 (the kind row itself), 0 = 未拾取, 1 = 已拾取. Meaningless when SelectedCollectibleKind < 0. */
    int32 SelectedCollectiblePicked = -1;

    /** True until the first reply arrives, so the panel can show "Scanning…". */
    bool bAwaitingFirstScan = true;

    /** Accumulator for the periodic rescan (the terminal is an always-live overview). */
    float RescanAccum = 0.f;

    /** ★用户诉求：传送成功后下一帧自动关面板（延迟到 NativeTick 执行，避免在 RCO 委托广播中同步销毁面板）。 */
    bool bPendingAutoClose = false;

    /** v0.2.4: signature of the last collectible set we rebuilt, so the (potentially
     *  hundreds-of-widgets) collectible board is only rebuilt when a scan reply actually changed it. */
    uint32 CachedCollectibleSig = 0;

    /** vX: signature of the last machine-list content we rebuilt into widgets, so a 10k+ row list is
     *  only re-created when the set actually changed (not on every 15s periodic rescan). */
    uint32 CachedMachineSig = 0;

    // ---- ★0.2.70 real-time collectible distance ----
    /** Text block + source index for each collectible row currently drawn in MachineListBox, so
     *  NativeTick can refresh distances live without rebuilding the widgets. Reset at the start of
     *  RebuildMachineList. */
    UPROPERTY()
    TArray<TObjectPtr<class UTextBlock>> CollectibleRowTexts;
    TArray<int32> CollectibleRowSrcIdx;
    /** Throttle accumulator for UpdateCollectibleDistances. */
    float DistRefreshAccum = 0.f;
};
