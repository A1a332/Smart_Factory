#pragma once

#include "UI/FGInteractWidget.h"
#include "Containers/Set.h"   // ★v0.2.76 TSet ExpandedMachineGroups（显式包含，不赌传递包含）
#include "Remote_FactoryTypes.h"
#include "Remote_FactoryButtonInfo.h"
#include "Remote_FactoryResourceNodes.h"   // ★2026-10-25 FWT_PurityAudit 值成员（纯度来源提示）
#include "Components/ComboBoxString.h"     // ★1.0.7i 筛选下拉基类（子类化以修深色主题文字对比度）
#include "Remote_FactoryPanelWidget.generated.h"

class UBorder;
class UCanvasPanel;
class UButton;
class UProgressBar;
class UTextBlock;
class UVerticalBox;
class UScrollBox;
class UEditableTextBox;
class UComboBoxString;   // ★1.0.8 子类目筛选下拉（点箭头选条件）
class SWidget;           // ★1.0.7i UWTFilterComboBox::HandleGenerateWidget 返回类型
class UWTPowerGraphWidget;   // ★1.0.9 电网曲线控件（Remote_FactoryPowerGraph.h）
class UHorizontalBox;
class AFGBuildable;
class URemote_FactoryRCO;

/** ★v0.2.15 用户按下面板上的 ✕ 关闭按钮。持有方（手持装备）监听并做完整收尾（摘栈+清指针）。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FWTOnPanelCloseRequested);

/**
 * ★1.0.11 单个子类目的筛选快照 —— 切类目时存一份、回来时恢复一份。
 *
 * 非 USTRUCT：它只在面板内部当纯值用，不进反射、不复制、不存盘（关面板就没了），
 * 加 USTRUCT 反而要 GENERATED_BODY 与 UPROPERTY 全套，收益为零。
 */
struct FWT_CatFilterSnapshot
{
    int32 ProdState = -1;
    int32 DistBand = -1;
    int32 NodePurity = -1;
    int32 AmountBand = -1;
    int32 Issue = -1;
    int32 RatioBand = -1;
    int32 Milestone = -1;      // ★1.0.7i 里程碑阶段
    int32 Sort = 0;
    FString ItemKey;
    FString TypeKey;
};

/**
 * ★1.0.7i 深色主题下可读的筛选下拉（UComboBoxString 子类）。
 *
 * 为什么要子类（一手核对引擎源码后的结论，不是猜）：
 *   ① 原版 `InitFont` / `InitForegroundColor` 是 **protected**
 *     （Engine/Source/Runtime/UMG/Public/Components/ComboBoxString.h:257 / :263）
 *      ⇒ 面板外部**调用不了**，编译期直接 C2248（本会话已实测一次）。
 *   ② 它生成下拉项时只设字体、**不设颜色**
 *     （UMG/Private/Components/ComboBoxString.cpp 的 HandleGenerateWidget）
 *      ⇒ 每一行的文字色完全来自构造期捕获的 ForegroundColor；
 *   ③ ForegroundColor 只在 RebuildWidget 里 `.ForegroundColor(ForegroundColor)` 应用一次
 *      ⇒ UMG 皮肤默认浅灰 + 深字，压在本 mod 的近黑面板上就是「看不清」（用户实测）。
 *
 * 所以这里重写 HandleGenerateWidget，**显式**给每一项 STextBlock 上色：
 * 关闭态（选中项也走这个生成器）与展开列表共用同一颜色，不依赖任何皮肤默认值。
 */
UCLASS()
class UWTFilterComboBox : public UComboBoxString
{
    GENERATED_BODY()

public:
    /** 行文字色（关闭态 + 展开列表共用）。默认近白，与面板 C_VALUE 一致。 */
    FSlateColor WT_ItemColor = FSlateColor(FLinearColor(0.870f, 0.940f, 0.970f, 1.f));

    /** 行字号（11：一行能塞下 7~8 个下拉还不糊）。 */
    float WT_FontSize = 11.f;

protected:
    //~ Begin UComboBoxString Interface
    virtual TSharedRef<SWidget> HandleGenerateWidget(TSharedPtr<FString> Item) const override;
    //~ End UComboBoxString Interface
};

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

    /** ★2026-11-02 智能仓储开关：切换 Remote_FactorySmartWarehouse 的开关并持久化到 ini。 */
    UFUNCTION()
    void OnSmartWarehouseToggleClicked();

    /** ★2026-11-02 里程碑货舱即刻返回开关：切换 Remote_FactoryMilestoneCargoReturn 的开关并持久化到 ini。 */
    UFUNCTION()
    void OnMilestoneCargoReturnToggleClicked();

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

    /** ★本轮 顶部枢纽快捷图标栏：把 Hub 类目下的 6 种枢纽建筑 + 回收站 以固定图标单元格展示在搜索框上方，
     *  每个单元格含图标 + 名称 + 【传送】【打开】两个操作；建筑未建造时图标灰显、操作禁用。 */
    void RebuildHubShortcuts();

    /**
     * Create a list button and wire its click to OnClicked with this row's payload captured.
     *
     * We reach through UButton's underlying Slate SButton because the UMG-level OnClicked delegate
     * cannot carry an argument. The lambda captures a plain FWT_ButtonInfo by value, so each row
     * gets its own closure — no lookup table, no guessing which button fired.
     */
    UButton* AddListButton(const FText& Label, bool bSelected, const FWT_ButtonInfo& Info, bool bEnabled,
                           UTextBlock** OutLabel = nullptr, const FText* ArrowGlyph = nullptr, bool bExpanded = false);

    /**
     * ★1.0.8 富内容行按钮：与 AddListButton 完全同一套点击派发，但按钮内容由调用方提供。
     *
     * 为什么需要它：机器行现在要放 [名称+距离][状态灯][状态文字] 三个控件，纯文本 Label 装不下
     * 一个彩色灯。把「按钮壳 + 点击绑定」和「内容」解耦后，行内可以塞任意 HorizontalBox，
     * 而点击语义（选中该行）与 AddListButton 生成的普通行**完全一致**，不会出现两种行两种行为。
     */
    UButton* AddRichButton(UWidget* Content, bool bSelected, const FWT_ButtonInfo& Info, bool bEnabled);

    /**
     * ★1.0.8 把 FWT_ButtonInfo 派发到对应的选择函数。
     *
     * 抽出来是因为 AddListButton / AddRichButton 两处都要同一份 switch —— 各写一份迟早会漂
     * （新加一种 EWT_ButtonKind 只改一处 ⇒ 另一种按钮点了没反应）。
     */
    void DispatchRowClick(const FWT_ButtonInfo& Info);

    /** Fill the right-hand detail pane for the selected machine. */
    void RebuildMachineList();

    /**
     * ★1.0.9 【电网】类目的专属渲染（用户定案：点开类目直接看到原版那块电力曲线）。
     *
     * 由 RebuildMachineList 在 SelectedCategory == PowerGrid 时分流进来，与 Entries
     * 渲染互斥。做三件事：
     *   ① 把选中电网的五条曲线喂给 GridGraph（Y 轴按本组实际数据定标，不按上限）；
     *   ② 刷新五行图例数值；
     *   ③ 重建下方「每条电路一行」的列表（点行切换上面的图）。
     *
     * 没有电网时把 GridHeaderBox 整个收起并给一句空态说明 —— 留一张空网格更像坏了。
     */
    void RebuildGridView();

    /** ★1.0.9 点电网行：切换上方曲线显示的是哪条电路。Index 指向 CurrentList.Grids。 */
    void SelectGrid(int32 GridIndex);

    /**
     * ★1.0.9 类目计数（类目按钮上的 `(N)` 与是否可点都靠它）。
     *
     * 为什么要单独一个函数：电网的条目**不在** Entries 里 —— 服务端把电线杆聚合成
     * 电路记录（Reply.Grids），Entries 里一条都没有，所以 CategoryCounts[PowerGrid]
     * 恒为 0。若各处直接读 CategoryCounts，电网按钮会永远显示灰色不可点。
     */
    int32 CategoryCount(int32 CategoryIndex) const;

    /**
     * ★1.0.11 类目问题数（用户方案稿「问题徽标：红/橙小圆点 + 数字，别用灰色」）。
     *
     * 口径 = 该类目里 IssueFlags != 0 的条目数，与「问题」下拉选「任意问题 (N)」的 N 同源，
     * 保证左侧徽标与下拉里的数字永远一致（两处各算一份必然漂）。
     * 电网类目恒 0：它的条目是电路聚合，没有单个建筑的问题可言。
     */
    int32 CategoryIssueCount(int32 CategoryIndex) const;

    /**
     * ★1.0.8 中列顶部筛选条（用户定案：横列 N 个筛选条件，点下选箭头选择）。
     * ★1.0.10 改为「按类目配置维度」：每个子类目自己一组，见下表的维度分配。
     *
     *   生产设备            : [状态 ▼] [物品 ▼] [距离 ▼]
     *   采矿设备            : [资源节点 ▼] [纯度 ▼] [距离 ▼]
     *   发电机              : [状态 ▼] [燃料 ▼] [距离 ▼]
     *   容器（含死亡箱子）  : [物品 ▼] [数量 ▼] [距离 ▼]
     *   车站 / 运输工具     : [物品 ▼] [距离 ▼]
     *   MOD 建筑            : [物品 ▼] [距离 ▼]
     *   枢纽 / 传送门 / 电网: [距离 ▼]
     *   资源节点（原版/MOD）: [纯度 ▼] [距离 ▼]
     *   收集品              : [距离 ▼]
     *
     * 「物品 / 资源节点 / 燃料」这三类下拉的选项**不是写死的档位**，而是这份列表里
     * 实际出现过的物品（ItemTags 去重，按显示名排序），也就是用户要的「已存在物品」；
     * 同时搜索框会把 ItemTags 一起匹配，所以那个下拉也可以直接打字搜。
     *
     * 选项文本自带计数（如「运行中 (12)」），所以下拉本身就是计数条，不需要额外角标行。
     * 多个下拉是**叠加（AND）**关系，由 RebuildMachineList 统一按交集过滤。
     */
    void RebuildFilterBar();

    /**
     * ★1.0.10 筛选维度标识（取代原先裸 int32 DimIndex）。
     * 顺序只影响 FilterCombos 的键，不决定界面顺序 —— 界面顺序由 RebuildFilterBar
     * 里每个类目的维度表决定。
     */
    enum class EWT_FilterDim : uint8
    {
        ProdState = 0,   // 生产状态（-1 全部 / 0 空闲 / 1 运行 / 2 待机 / 3 阻塞）
        Item      = 1,   // 物品（字符串键，见 SelectedItemKey）
        Amount    = 2,   // 库存件数档（-1 全部 / 0 空 / 1 少量 / 2 中量 / 3 大量）
        Purity    = 3,   // 矿点纯度（-1 全部 / 0 贫瘠 / 1 普通 / 2 纯）
        Distance  = 4,   // 距离档（-1 全部 / 0 100m / 1 500m / 2 1km）
        Type      = 5,   // ★1.0.11 建筑类型（字符串键 = DisplayName，见 SelectedTypeKey）
        Issue     = 6,   // ★1.0.11 问题（-1 全部 / 0 任意问题 / 1..8 → EWT_Issue）
        Ratio     = 7,   // ★1.0.11 比例档（效率 / 填充率共用：-1 全部 / 0 0-25 / 1 25-50 / 2 50-75 / 3 75-100）
        Sort      = 8,   // ★1.0.11 排序（不是过滤维度，只影响行顺序；不进 PassesCategoryFilters）
        Milestone = 9,   // ★1.0.7i 里程碑阶段（-1 全部 / 0..N = 蓝图 TechTier / 99 未归类）
        Picked    = 10,  // ★1.0.8AN 收集品拾取状态（-1 全部 / 0 未拾取 / 1 已拾取）
    };

    /**
     * ★1.0.10 某一维度取某值后还剩几个条目（供下拉选项文本里的计数，如「运行中 (12)」）。
     * 计数时会叠加**其余**已选维度，所以「选它还剩几个」的数字与点下去后的实际行数一致。
     * 物品维度不走这里（键是字符串），见 CountEntriesWithItem。
     */
    int32 CountEntriesForDim(EWT_FilterDim Dim, int32 Value) const;

    /** ★1.0.10 物品维度计数：当前类目下含某物品（Key 为空 = 全部物品）的行数。 */
    int32 CountEntriesWithItem(const FString& ItemKey) const;

    /** ★1.0.10 当前类目里实际出现过的物品（去重、按显示名排序），供「物品」下拉。 */
    TArray<FWT_ItemTag> CollectCategoryItems() const;

    /**
     * ★1.0.11 建筑类型维度计数（键 = DisplayName 字符串，空 = 全部类型）。
     * 与物品维度同一套思路：选项来自「这一列里实际出现过的类型」，不列全游戏建筑表。
     */
    int32 CountEntriesWithType(const FString& TypeKey) const;

    /** ★1.0.11 当前类目里实际出现过的建筑类型（按显示名去重排序），供「类型」下拉。 */
    TArray<FWT_ItemTag> CollectCategoryTypes() const;

    /**
     * ★1.0.7i 里程碑阶段维度（用户诉求「过滤条件中新增里程碑阶段」）。
     *
     * 键 = FWT_MachineEntry::MilestoneTier（扫描侧填：建筑按解锁它的蓝图 TechTier，
     * 资源节点按矿种阶段表；都查不到 = 99 未归类）。选项只列当前类目里**实际出现过**的阶段，
     * 与其它维度同一套「已存在值」口径 —— 空阶段不会出现在下拉里。
     */
    int32 CountEntriesWithMilestone(int32 Tier) const;
    TArray<int32> CollectCategoryMilestones() const;

    UFUNCTION()
    void OnMilestoneComboChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

    /**
     * ★1.0.8AN 收集品的「拾取」改成**下拉**（用户 05:15：「收集品中【拾取】我需要和后面的
     * 【距离】【排序】要一样」）。原先的【未拾取】【已拾取】两个按钮换成与距离/排序同款
     * `UWTFilterComboBox`：同样的高度、同样的「标题: 值 (N)」文案、同样的展开方式。
     * 状态仍由 SelectedCollectiblePicked 驱动（-1 全部 / 0 未拾取 / 1 已拾取）。
     */
    void SetCollectiblePickedFilter(int32 Picked);

    /** ★1.0.8AN 拾取下拉变更（-1 全部 / 0 未拾取 / 1 已拾取）。 */
    UFUNCTION()
    void OnPickedComboChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

    // ---- ★1.0.8 下拉选择回调（UComboBoxString::OnSelectionChanged 是 dynamic 委托，
    //      只能绑 UFUNCTION，所以每个维度一个回调，而不是一个 lambda 里猜是谁触发的）----
    UFUNCTION()
    void OnProdStateComboChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

    UFUNCTION()
    void OnPurityComboChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

    UFUNCTION()
    void OnDistComboChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

    /** ★1.0.10 物品下拉变更：按下标反查 CurrentItemOptionKeys 拿到物品 Key（第 0 项 = 全部）。 */
    UFUNCTION()
    void OnItemComboChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

    /** ★1.0.10 数量档下拉变更。 */
    UFUNCTION()
    void OnAmountComboChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

    /** ★1.0.11 建筑类型下拉变更：按下标反查 CurrentTypeOptionKeys（第 0 项 = 全部类型）。 */
    UFUNCTION()
    void OnTypeComboChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

    /** ★1.0.11 问题下拉变更（-1 全部 / 0 任意问题 / 1..8 对应 EWT_Issue）。 */
    UFUNCTION()
    void OnIssueComboChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

    /** ★1.0.11 比例档下拉变更（效率 / 填充率共用）。 */
    UFUNCTION()
    void OnRatioComboChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

    /** ★1.0.11 排序下拉变更（只改行顺序，不改变过滤结果）。 */
    UFUNCTION()
    void OnSortComboChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

    /**
     * ★1.0.8 筛选变更后的统一收尾：先刷列表，再**延迟一帧**重建筛选条。
     *
     * 为什么必须延迟：下拉回调是在 ComboBox 自己的 OnSelectionChanged 里跑的，
     * 此刻 RebuildFilterBar() 会 ClearChildren 把正在派发事件的 ComboBox 销毁掉 ——
     * 属于「在自己的回调里删自己」。下一帧重建既能避开这个坑，又能让选项上的计数
     * 跟着新的筛选结果刷新。
     */
    void ApplyFilterChange();

    /** ★1.0.8 按维度索引取当前筛选条里的下拉控件（0=状态 1=库存 2=纯度 3=距离）。
     *  回调只能拿到选项文本，拿不到是哪个控件触发，所以建的时候按维度存一份引用。 */
    UPROPERTY()
    TMap<int32, TObjectPtr<UComboBoxString>> FilterCombos;

    /**
     * ★1.0.8 「本次重画列表时别碰筛选条」的临时标记。
     *
     * 由来：RebuildMachineList() 开头会重建筛选条（扫描/切类目后下拉里的计数要跟着变），
     * 但下拉回调里调 ApplyFilterChange() 时，重建筛选条等于在 ComboBox 自己的事件栈里
     * 销毁它自己。ApplyFilterChange 会在调 RebuildMachineList 前置位本标记，
     * 让这次重画跳过筛选条，改由下一帧统一重建。
     */
    bool bDeferFilterBarRebuild = false;

    /** ★1.0.8 切类目时复位所有筛选维度（避免「切到容器后还留着上类目的状态筛选」）。 */
    void ResetCategoryFilters();

    /** ★1.0.8 判断一条 Entries 记录是否通过当前全部筛选维度（AND）。 */
    bool PassesCategoryFilters(const struct FWT_MachineEntry& Entry) const;

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

    /** ★本轮 从 SmartFactoryLib/resourcenodes.json 读取全图资源节点，并入 CurrentList（建筑组-资源节点子目）。
     *  与收集品同源：数据在 Lib，不在实时扫描。资源节点不是建筑，Target 恒 null，传送走坐标。 */
    void MergeResourceNodesIntoList();

    /** ★1.0.8 内嵌在中列列表顶部的搜索框文本变化回调（原顶部统一搜索框已搬到列表里）：
     *  更新 SearchText / bSearchActive、把关键词记到**当前类目**名下，然后只重画当前类目列表。 */
    UFUNCTION()
    void OnSearchTextChanged(const FText& Text);

    /** ★本轮 清空搜索（用户点类目/顶层组/外星科技种类时退出搜索模式）。★1.0.8 只清当前类目那一份。 */
    void ClearSearch();

    /** ★0.2.69 Official localized map-area name at a world position (client PC's map-area texture).
     *  Returns an empty text when no map-area volume contains the point or the PC is unavailable. */
    FText ComputeRegionForLocation(const FVector& Loc) const;

    // ---- RCO callbacks ----
    void HandleMachineListReceived(const FWT_MachineListReply& Reply);
    void HandleInteractUIRequested(AActor* Target);
    /** ★1.0.8 放宽为 AActor：死亡箱子 AFGCrate 不是建筑，但远程取物是它的主要用途。 */
    void HandlePullResult(AActor* Target, int32 ItemsMoved, bool bSuccess);
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

    /** ★2026-11-02 智能仓储开关按钮（标题栏，紧跟 Refresh 之后）。点击切换并持久化开关状态。 */
    UPROPERTY()
    TObjectPtr<UButton> SmartWarehouseButton = nullptr;

    /** 智能仓储按钮内文字（「智能仓储: 开 / 关」）。MakeButton 的 OutLabel 形参是裸 UTextBlock**，故用裸指针。 */
    UPROPERTY()
    UTextBlock* SmartWarehouseLabel = nullptr;

    /** ★2026-11-02 里程碑货舱即刻返回开关按钮（标题栏，紧跟智能仓储之后）。点击切换并持久化开关状态。 */
    UPROPERTY()
    TObjectPtr<UButton> MilestoneCargoReturnButton = nullptr;

    /** 里程碑货舱即刻返回按钮内文字（「里程碑货舱即刻返回: 开 / 关」）。MakeButton 的 OutLabel 形参是裸 UTextBlock*，故用裸指针。 */
    UPROPERTY()
    UTextBlock* MilestoneCargoReturnLabel = nullptr;

    /** Left column: one button per category, rebuilt whenever counts change. v0.2.55 redesign:
     * 分类标签移到左侧一列 (vertical stack, per user's layout). */
    UPROPERTY()
    TObjectPtr<class UVerticalBox> CategoryBox = nullptr;

    /** Middle column: the machine rows for the selected category. */
    UPROPERTY()
    TObjectPtr<UScrollBox> MachineListBox = nullptr;

    /** ★1.0.28i 中列顶部纯度过滤条容器（仅资源节点类目显示，RebuildPurityBar 重建）。
     *  置于 MachineListBox 之上、滚出区之外，不随列表滚动消失。 */
    UPROPERTY()
    TObjectPtr<UVerticalBox> PurityBarBox = nullptr;

    /** ★1.0.11 筛选条固定高度外壳（用户方案稿「控件区高度固定：切类目时列表不能跳动」）。
     *  两行高度常驻；但当某类目一个维度都不建（电网）时，RebuildFilterBar 把它压成 0，
     *  否则会在曲线图上顶出一块无意义的空白。 */
    UPROPERTY()
    TObjectPtr<class USizeBox> FilterHeightBox = nullptr;

    // ---- ★1.0.9 【电网】类目：曲线图 + 五项图例（常驻控件，仅该类别可见）----
    /** 中列里承载「曲线图 + 图例」的容器，仅在 SelectedCategory == PowerGrid 时 Visible。
     *  刻意不销毁重建：里面的曲线是自绘 Slate 控件，销毁要重建 Slate 资源，而切类目很频繁。 */
    UPROPERTY()
    TObjectPtr<UVerticalBox> GridHeaderBox = nullptr;

    /** 曲线控件（自绘 SLeafWidget 的 UMG 包装，见 Remote_FactoryPowerGraph.h）。 */
    UPROPERTY()
    TObjectPtr<UWTPowerGraphWidget> GridGraph = nullptr;

    /** 五行图例的数值文本，下标 = FWT_GridSeries::Kind（0消耗 1最大消耗 2发电 3上限 4能源增幅）。 */
    UPROPERTY()
    TArray<TObjectPtr<UTextBlock>> GridLegendValueTexts;

    /** 当前选中的电网，指向 FWT_MachineListReply::Grids 的下标（不是 Entries 的下标）。 */
    int32 SelectedGridIndex = 0;

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

    /** ★1.0.8 内嵌在中列列表顶部的搜索框（原「顶部统一搜索框」已搬进列表）。
     *  输入即过滤**当前子类目**的列表：名称 / 产物名 / 状态描述 / MOD 名。 */
    UPROPERTY()
    TObjectPtr<UEditableTextBox> SearchBox = nullptr;

    /** ★本轮 顶部枢纽快捷图标栏（搜索框上方）：6 个枢纽建筑 + 回收站 的图标单元格容器（HorizontalBox 单行均分，永不换行）。 */
    UPROPERTY()
    TObjectPtr<UHorizontalBox> HubShortcutBox = nullptr;

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

    /** ★1.0.24 true once resourcenodes.json parsed successfully (even with an empty nodes array).
     *  Distinguishes「文件不存在 ⇒ 提示先跑 wt.DumpResourceNodes」from「文件存在但本图无矿点」。 */
    bool bResourceNodeFileLoaded = false;

    /** ★2026-10-25 资源节点纯度来源审计（resourcenodes.json 顶层 purityAudit 块）。
     *  Source != Vanilla（MOD 改造 / 游戏模式强制）时，类目树渲染禁用提示行告知用户
     *  「这批纯度不是原版值」，避免把改造值当原版参考。 */
    FWT_PurityAudit ResourceNodePurityAudit;

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
    bool bTopExpanded[3] = { true, true, false };   // ★本轮 资源节点(0) 与 建筑(1) 默认展开，收集品(2) 折叠

    // ---- ★0.2.67 收集品种类折叠：★1.0.7i 已废弃 ----
    // 种类行下原本挂【未拾取】/【已拾取】两个子行，靠 bKindExpanded 折叠；用户定案把这两项
    // 上移成筛选条上的**两个独立过滤条件**后，种类行不再有任何子行，折叠状态与三角一并删除
    //（`SelectedCollectiblePicked` 仍然存在，只是改由筛选条驱动）。

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

    /** ★2026-10-25 资源节点纯度筛选（用户诉求）：-1 = 全部，0 = 贫瘠，1 = 普通，2 = 纯。
     *  只在 SelectedCategory 为 ResourceNode_Vanilla / ResourceNode_Mod 时有意义；
     *  切到其他类目时由 SelectCategory 重置为 -1。 */
    int32 SelectedNodePurity = -1;

    // ---- ★1.0.8 子类目筛选（用户定案：横列 N 个下拉，每个子类目贴合自身）----
    /** 生产状态筛选：-1 = 全部，0 = 空闲，1 = 运行中，2 = 待机，3 = 阻塞。
     *  只对 生产设备 / 采矿设备 / 发电机 三类有意义（其余类目条目 ProdState 恒 255）。 */
    int32 SelectedProdState = -1;

    /** 距离档位筛选：-1 = 全部，0 = 100m 内，1 = 500m 内，2 = 1km 内。所有建筑子类目都有。 */
    int32 SelectedDistBand = -1;

    /**
     * ★1.0.10 物品筛选（用户诉求「下拉选择已存在物品」）：空 = 全部物品，否则是物品描述符类名。
     *
     * 存类名不存显示名：显示名是本地化文本，换语言后这个筛选状态就失效了；类名跨语言稳定。
     * 选项来源 = 当前列表条目里实际出现过的物品（CollectCategoryItems），不是全游戏物品表 ——
     * 玩家问的是「这份列表里有哪些东西」，列出整个物品库既慢又没用。
     */
    FString SelectedItemKey;

    /** ★1.0.10 数量档筛选：-1 全部 / 0 空 / 1 少量(1~999) / 2 中量(1k~9999) / 3 大量(≥10000)。
     *  只对有件数概念的条目有效（ContentStackCount < 0 的行不参与该维，见字段注释）。 */
    int32 SelectedAmountBand = -1;

    /**
     * ★1.0.11 建筑类型筛选（键 = 该建筑 DisplayName）。
     *
     * 为什么用 DisplayName 当键而不是 UClass 名：类型维度要显示给玩家看，
     * 而列表行分组头本来就用 DisplayName 分组（MOD 建筑还可能是 MOD 名），
     * 两处口径必须一致，否则「下拉写组装机、列表分组头写 Assembler」这种错位一定出现。
     * 换语言的代价只是这个临时筛选状态失效（下拉回落「全部类型」），不影响任何数据。
     */
    FString SelectedTypeKey;

    /** ★1.0.11 类型下拉的选项键，下标与选项一一对应（第 0 项 = 空 = 全部类型）。 */
    TArray<FString> CurrentTypeOptionKeys;

    /** ★1.0.11 问题筛选：-1 全部 / 0 任意问题（IssueFlags != 0） / 1..8 = EWT_Issue 的位（见 Types.h）。 */
    int32 SelectedIssue = -1;

    /** ★1.0.11 比例档：-1 全部 / 0 (0,25] / 1 (25,50] / 2 (50,75] / 3 (75,100]。
     *  生产设备读 Efficiency01、容器读 FillPercent，两者都是 0~100 的百分比口径。 */
    int32 SelectedRatioBand = -1;

    /** ★1.0.11 排序：0 距离近→远（默认）/ 1 距离远→近 / 2 类型名 / 3 比例低→高 / 4 比例高→低 / 5 数量多→少。 */
    int32 SelectedSort = 0;

    /** ★1.0.7i 里程碑阶段筛选：-1 = 全部，>=0 = 蓝图阶段号，99 = 未归类（见 WTBuildingMilestoneTier）。 */
    int32 SelectedMilestone = -1;

    /** ★1.0.7i 里程碑下拉的选项键，下标与选项一一对应（第 0 项 = -1 = 全部阶段）。 */
    TArray<int32> CurrentMilestoneOptionKeys;

    /**
     * ★1.0.11 按类目记忆的筛选状态（用户方案稿「切换类目保持子列表筛选状态」）。
     *
     * 原来切类目一律 ResetCategoryFilters() 全清，玩家去容器筛完「快满的箱子」再回生产设备，
     * 回来时筛选全没了，得重选一次。这里在离开类目时存一份、进入时恢复一份，
     * 缺省（第一次进入）仍是全清 —— 不改变首访行为。
     */
    void SaveCategoryFilters(uint8 Cat);
    void LoadCategoryFilters(uint8 Cat);
    TMap<uint8, FWT_CatFilterSnapshot> CategoryFilterStates;

    /**
     * ★1.0.10 物品下拉的选项键，下标与下拉选项一一对应（第 0 项 = 空字符串 = 全部物品）。
     *
     * 为什么另存一份而不是解析下拉文本：选项文本是「物品: 铁板 (12)」这种拼装串，
     * 解析它等于把 UI 文案当协议用，文案一改筛选就静默失效。
     */
    TArray<FString> CurrentItemOptionKeys;

    /**
     * ★1.0.8 按类目隔离的列表搜索词（用户定案：「每个子类目列表各一个，每个搜索对应当前列表」）。
     *
     * 物理上只有一个 SearchBox（嵌在中列列表顶部），但它记住每个类目各自的输入：
     * 切到采矿设备时框里恢复采矿设备的搜索词、列表也只按它过滤，完全不影响生产设备。
     * key = EWT_MachineCategory 的整数值。
     */
    TMap<uint8, FString> CategorySearchTexts;

    /** True until the first reply arrives, so the panel can show "Scanning…". */
    bool bAwaitingFirstScan = true;

    /** Accumulator for the periodic rescan (the terminal is an always-live overview). */
    float RescanAccum = 0.f;

    /** ★1.0.8 搜索状态：SearchText 非空即按关键词过滤**当前类目**的列表
     *  （不再有跨类目汇总视图 —— 原 RebuildSearchResults() 已随之删除）。 */
    FString SearchText;
    bool bSearchActive = false;

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

    // ---- ★本轮 容器存量实时刷新（0.1s）----
    /** 每个固体容器行登记：文本块 + 对应建筑 Actor（客户端复制的库存组件），供 NativeTick
     *  直读刷新总量。液体罐无数量 API 不登记。Reset at start of RebuildMachineList。 */
    struct FWT_LiveContainerRow
    {
        TWeakObjectPtr<UTextBlock> Text;
        TWeakObjectPtr<AActor>     Actor;
        int32 LastTotal = -1;
    };
    TMap<int32, FWT_LiveContainerRow> WT_LiveContainerRows;

    /** Throttle accumulator for UpdateCollectibleDistances. */
    float DistRefreshAccum = 0.f;
};
