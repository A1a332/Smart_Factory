#pragma once

#include "CoreMinimal.h"
#include "UI/FGInteractWidget.h"
// ★1.0.7k 必须**完整包含**（而非前置声明）：UWTItemFilterComboBox 要继承它，且
// TObjectPtr<UWTItemFilterComboBox> 在 .generated.h 展开时需知道它是 UObject 派生
// （只前置声明会报 C2504「未定义基类」+ ObjectPtr.h 的 static_assert 失败，本轮实测过一次）。
#include "Components/ComboBoxString.h"
#include "Engine/TimerHandle.h"          // ★1.0.8BJ 刷新定时器句柄（FTimerHandle 是完整类型成员）
#include "Remote_FactoryItemSelectPanel.generated.h"

class UBorder;
class UButton;
class UCanvasPanel;
class UEditableTextBox;
class UHorizontalBox;
class UImage;
class UScrollBox;
class SWidget;
class UTextBlock;
class UVerticalBox;
class UFGItemDescriptor;
class AWTUpgradeCloudDownloader;
class AWTUpgradeStorage;

/**
 * ★★1.0.8AQ 本面板服务的**目标建筑种类**（由 `mInteractObject` 的实际类型判定，不靠开关变量）。
 *
 * 本面板服务的目标建筑：固体上传站（AWTUpgradeStorage）与固体下载站（AWTUpgradeCloudDownloader），
 * 两种站共用同一份代码，由 `PanelKind` 区分：
 *
 *   固体上传站  AWTUpgradeStorage               → SolidUploader   （无选择列）
 *   固体下载站  AWTUpgradeCloudDownloader       → SolidDownloader （选择/取消 + 速率）
 *
 * 为什么用枚举而不是两个 bool：两种组合里「上传侧/下载侧」维度要影响
 * 行构建与数据源，两个 bool 迟早会拼出第五种非法状态（如「液体上传」却去读固体白名单）。
 */
enum class EWTCloudPanelKind : uint8
{
    None,
    SolidUploader,
    SolidDownloader,
};

/**
 * ★1.0.7k 深色主题下可读的筛选下拉（UComboBoxString 子类）。
 *
 * 与 `Remote_FactoryPanelWidget.h` 的 `UWTFilterComboBox` **同源同因**，故意各自一份：
 * 那个类在上拉面板的头里，为了复用把它抽到公共头会把这个面板拖进上拉面板的整套 include 依赖，
 * 收益不抵风险（本模组 UI 层一贯「各文件自带 static 主题色」的同一取舍）。
 *
 * 为什么必须子类（一手读引擎源码的结论，非猜测）：
 *   ① `InitFont` / `InitForegroundColor` 是 **protected**
 *      （Runtime/UMG/Public/Components/ComboBoxString.h:257/:263）⇒ 外部调用直接 C2248；
 *   ② 默认项生成器 `HandleGenerateWidget` 只设 Font、**不设颜色**
 *      （UMG/Private/Components/ComboBoxString.cpp）⇒ 行文字色只来自构造期捕获的 ForegroundColor；
 *   ③ 关闭态与展开态共用同一生成器 ⇒ 重写它即可同时修好两处。
 */
UCLASS()
class UWTItemFilterComboBox : public UComboBoxString
{
    GENERATED_BODY()

public:
    /** 行文字色（关闭态 + 展开列表共用）。默认近白，与面板 ISEL_VALUE 一致。 */
    FSlateColor WT_ItemColor = FSlateColor(FLinearColor(0.870f, 0.940f, 0.970f, 1.f));

    /** 行字号。 */
    float WT_FontSize = 12.f;

protected:
    //~ Begin UComboBoxString Interface
    virtual TSharedRef<SWidget> HandleGenerateWidget(TSharedPtr<FString> Item) const override;
    //~ End UComboBoxString Interface
};

/**
 * 云端储存下载站 · 输出物品选择面板（★1.0.7 用户定案「云端下载站的 UI 重构，采用新面板，
 * 全物品中选择需要输出的物品；液体排除」）。
 *
 * 与 0.2.36 起一直复用的**原版云端面板**（位面存储站 Build_CentralStorage_C 的
 * mInteractWidgetSoftClass）分道扬镳：原版面板只能看池子、不能指定「我要它吐哪种货」，
 * 所以这里换成自建面板（纯 C++ 构建 —— 本模组不发行任何 UMG 资产，与
 * Remote_FactoryPanelWidget 同一路子）。
 *
 * ★1.0.7f（2026-10-26）用户定案的四条交互规则：
 *   ① 全中文；
 *   ② 文本全部**对齐**（名称左对齐、数量右对齐、按钮文字居中），不留默认散排；
 *   ③ 下载只允许 1 种（白名单单选）—— 但 ★1.0.7s 起**选中不再隐藏列表里的其它物品**
 *      （用户 2026-10-27「全部显示出来，别隐藏部分文字，所有类目的全部都是」）；
 *   ④ 换材料直接点另一行的【选择】；要停下载就**再点一下当前那一行**（同一颗按钮已变成【取消】），
 *      不必先"放开"再去挑；
 *   ⑤ 搜索框必须能看见输入的文本（自定义样式，前景/背景/提示三色显式给足对比）。
 *
 * 行布局（★1.0.7y 用户 2026-10-27 定案，1.0.8 补状态列/按钮双态）：
 *
 *   【图标】【文本】【云端数量】【——弹性空柱——】【正在下载/未下载】【下载速度】【选择/取消】
 *
 *   +----------------------------------------------------------------------------------+
 *   | [icon] 铁锭 云端 12,480　　　　　正在下载　　1,200/分　　[选择] |
 *   +----------------------------------------------------------------------------------+
 *   | [icon] 铜锭 云端  3,200　　　　　未下载　　　　 -- 　　[取消] |
 *   +----------------------------------------------------------------------------------+
 *
 *   * 列①图标 26×26（显式 SetImageSize，不靠贴图原生尺寸）；
 *   * 列②文本：**自然宽**（Automatic，左对齐）。★不许给它 Fill —— 名称一旦吃满剩余宽度，
 *     后面的数量必被顶到行最右（1.0.7y「数量往前挪」要治的就是这个）；
 *   * 列③数量：自然宽、左对齐、紧贴名称；
 *   * 列④弹性空柱：一根**空文本**吃 Fill（desired size 0，视觉上不存在），
 *     唯一作用 = 把它右边的三列顶到行尾并保持对齐；
 *   * 列⑤状态：固定最小宽（96px，右对齐）。「正在下载」= 该物品近 5 秒真在搬，否则「未下载」；
 *   * 列⑥速度：固定最小宽（78px，右对齐）；= **该物品自己**近 5 秒的搬运速率，
 *     没在搬这一类时显示「--」（下载站同一时刻只搬一种，这是准确表达而非故障）；
 *   * 列⑦按钮：**同一个按钮**双态。未选中【选择】（亮青底）、选中【取消】（暗红底）——
 *     再点一下 = 解除选择 = 停止搬运。★恒可见恒可点（用户 2026-10-27「为什么不能在点一下取消？」）；
 *     「已选」由整行底色高亮表达，不再另设 ✔ 文字标记。宽度恒定（64），切换不跳位。
 *
 * 数据口径（★液体排除 = 用户明确要求，但「生产」栏建筑产出的固体全收）：
 *   * 行集 = 「生产」栏全部建筑（AFGBuildableFactory 支系）能产出的物品；
 *   * 只保留 `RF_SOLID`：RF_LIQUID / RF_GAS / RF_INVALID 一律不列（流体走云端流体下载站）；
 *   * 桶装流体是灌装站（AFGBuildableManufacturer）的产物、本身是 Solid ⇒ 自然收录；
 *   * 建筑描述符（UFGBuildingDescriptor 支系，含本模组 UWTUpgradeDescriptorBase）排除；
 *   * 每种物品显示「云端合计」= 原版云端池 + 本模组各上传站账本。
 *
 * 进度解锁（★1.0.7e）：只收录「产出它的某个配方，被某个已解锁的里程碑 / MAM 研究 /
 * 替代配方 / 硬盘 发放」的物品。玩家升阶段或研究 MAM 后，进度签名变化 ⇒ 目录自动重建。
 *
 * 语义（★1.0.7x 用户 2026-10-27 定案「列表并未选择任何材料，但仍在下载」）：
 *   **空名单 = 一件都不搬**（显式选择制）。必须先点行内【选择】指定物品才开始下载；
 *   再点一下同一行（按钮已变【取消】）= 停止下载。旧的「空名单 = 搬全部」口径作废。
 *   选择写进建筑的 `mOutputFilter`，`UPROPERTY(SaveGame)` 持久化，重进存档仍在。
 *
 * ★1.0.7h（2026-10-27 用户定案「这个列表隐藏未输入的，只显示当下已存在的」）：
 *   列表里只留**云端当前真的有存货**的物品（显示数量 > 0），0 的全部折叠（Collapsed）。
 *   实现方式是**折叠行、不重建行集** —— 云端数量每 0.5s 在变，若按「有货集合」重建
 *   RebuildRows()，上传/下载期间每次跨 0 都要清空重加上百个 UMG 控件（闪烁 + 白烧 CPU）。
 *   折叠只改 Slate 可见性，行索引仍与 `Visible` 一一对应 ⇒ 点击回调的下标语义不变。
 *   ★唯一例外：**当前选中的那一种永远显示**（哪怕已经搬空）—— 否则搬空后那一行消失，
 *   玩家就点不到【取消】、也看不到「云端剩余 0」这个事实。
 *   ★1.0.7s 曾把这条改成「一律显示」，用户看到「满屏云端 0」后当场否决，1.0.7t 已恢复本条。
 *   1.0.7s 真正修掉的是「选中一种 ⇒ RebuildVisible 砍成 1 项」那处（保留）。
 *
 * 单人/主机模式下面板与建筑同进程，直接写属性是安全的（与流体选型面板同一条约定）。
 */
UCLASS()
class REMOTE_FACTORY_API UWTItemSelectPanelWidget : public UFGInteractWidget
{
    GENERATED_BODY()
public:
    // Begin UUserWidget interface
    UWTItemSelectPanelWidget(const FObjectInitializer& ObjectInitializer);
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
    virtual void NativeDestruct() override;
    // End UUserWidget interface

    /**
     * ★1.0.7c 修复「面板打开后鼠标转视角、键盘动角色」的**真正入口**：
     * UFGInteractWidget::SetInputMode 是 BlueprintNativeEvent（FGInteractWidget.h:43-44），
     * 原版交互面板是**蓝图**，在图里实现了它（GameAndUI + 光标 + 锁视角/移动）——所以按 E 打开
     * 原版面板一切正常。本模组面板是**纯 C++ 类**，不覆写这个事件它就是空实现（SML 桩里
     * FGInteractWidget.cpp:35 即 `{}`），游戏调用它时什么也没做 ⇒ 输入完全没被接管。
     * 日志实锤：`activeWidget=1`（面板确实进了 FGGameUI 交互栈）但键鼠仍驱动角色，正是此因。
     */
    virtual void SetInputMode_Implementation() override;

protected:
    UFUNCTION()
    void OnSearchChanged(const FText& Text);

    UFUNCTION()
    void OnClearSelectionClicked();

    UFUNCTION()
    void OnCloseClicked();

    /**
     * ★1.0.8BG 行尾【转移】/【清空】按钮的唯一入口（**按行**，一次只动这一行的物品）。
     *
     * 用户 2026-10-28 定案原话：「不要选择清空全部，选择单一的」「每一行不是一个物品吗，
     * 在后面增加一个清空/转移等等，选择转移的时候，背包 UI 或者其他 UI 要打开，方便转移」。
     *
     *   · 作用范围 = **这一行的物品类**（不是整个列表、也不是全目录）—— 标题行的批量按钮已删；
     *   · 固体 → `TakeCloudItemsToBackpack`（整类存量进背包，货还在）；
     *   · 固体搬完后关闭本面板、下一拍再打开原版背包 UI（`OpenInventoryAfterClosingSelf`），
     *     让玩家当场看到并整理（顺序不可换，否则背包没有光标）。
     *
     * 为什么不复用 `OnRowAction`：那颗按钮是【选择/取消】输出物（写建筑白名单），
     * 与"动云端存货"是两件不同的事，不能混进同一个入口。
     */
    void OnRowTakeClicked(int32 VisibleIndex);

    /**
     * ★1.0.8BJ 面板数据的**唯一刷新驱动**（世界定时器，0.30s 循环）。
     *
     * ## 为什么不再靠 `NativeTick`
     * 用户 2026-10-28 实测「这里的上传下载需要实时更新」：面板打开后云端数量 / 净变化 / 速度
     * 全部静止，只有打开那一拍的值。原实现把刷新挂在 `NativeTick` 的 0.5s 累加块里 ——
     * 那条路径是否执行取决于 UMG 的 tick 策略（`EWidgetTickFrequency::Auto` +
     * `SObjectWidget::CanRouteEvent`），一旦那一环没走到，**没有任何报错**，界面就变成静态快照。
     * 定时器由世界 TimerManager 驱动，与 Slate 的 tick 无关，`SetTimer(..., this, &Method)`
     * 绑定的是 UObject 成员函数 ⇒ 面板被销毁后定时器自动失效，不会打到野指针。
     *
     * ## 与 NativeTick 的分工
     *   · 本函数：进度签名检测 + RefreshAmounts + RefreshRowStyles + RefreshSpeedText（数据）；
     *   · NativeTick：只做「每帧把光标拉回来」（Satisfactory 每次 SetInputMode 都会重写
     *     bShowMouseCursor，必须持续保活），并打一条 tick 是否真的活着的取证日志。
     *
     * 刻意**不**加 `IsInViewport()` 门禁：NativeConstruct 时 inViewport 还是 0（日志实测），
     * 若用视口做判据，面板一旦被放进子视口就再也不刷新 —— 正是本函数要消灭的失效模式。
     */
    void OnPanelRefreshTick();

private:
    /** The downloader this panel was opened for (via the vanilla mInteractObject wiring). */
    AWTUpgradeCloudDownloader* GetDownloader() const;








    /**
     * ★1.0.8AQ 这次打开面板的建筑属于哪一种站（见 EWTCloudPanelKind 的注释）。
     *
     * NativeConstruct 里解析一次存进 `PanelKind`；解析只读 `mInteractObject`，不碰世界。
     */
    EWTCloudPanelKind ResolvePanelKind() const;


    /**
     * ★1.0.8AQ 上传侧判定（原 1.0.7z 的 `GetIsUploaderStation`，现已由 `PanelKind` 派生）。
     *
     * 历史定案（用户 2026-10-27）：「我们的云端上传站也采用此UI即可，输出就不显示【更换/选择】」
     * —— 上传站没有「输出物品」这个概念（它只管把本站缓冲推上云），所以：
     *   · 不建行尾按钮/标记（整列不存在，不是禁用）；
     *   · 状态列/速度列也不显示（那是下载侧的读数）；
     *   · 标题换成上传站口径。
     * 判定走 mInteractObject 的实际类型（`ResolvePanelKind`），不靠开关变量：同一个面板类
     * 被四种建筑共用，面板可能被不同建筑先后打开，任何「谁打开的」成员变量都会脏。
     */
    bool IsUploaderKind() const;


    /** ★1.0.8AQ 每类物品的下载速度（固体/液体同接口，内部按 PanelKind 分派）。 */
    void WT_GetItemRates(TMap<UClass*, float>& OutRates) const;

    /** ★1.0.8AQ 本站的下载速度上限（件/分钟），过滤行右侧读数用；取不到返回 0。 */
    float WT_GetRateLimitPerMinute() const;

    /** ★1.0.8AQ 本站整体下载速度（件/分钟），过滤行右侧读数用。 */
    float WT_GetTotalRatePerMinute() const;

    /** ★1.0.7z 上传站模式（NativeConstruct 里按 mInteractObject 判一次，行构建要用）。 */
    bool bUploaderMode = false;

    // ---- ★1.0.8BI 云端净变化采样（纯运行期，不入存档）---------------------------------
    /** 上一次的"每类云端总量"快照（池 + 各站账本，与 RefreshAmounts 的口径一致）。 */
    TMap<UClass*, int32> PrevPoolTotals;

    /** 上次快照的游戏时间（秒）。 */
    double PrevPoolSampleTime = 0.0;

    /**
     * ★是否已经有第一拍基线（**不能**用 `PrevPoolSampleTime <= 0.0` 代替）。
     *
     * 用户可能在世界时间还不到 1 秒时就打开了面板（新开档、或点得极快），此时
     * `GetTimeSeconds()` 可能返回 0 或极小值 ⇒ "时间戳为 0 = 没采过"这个判据会永久成立
     * ⇒ 净值列永远显示「净 0」，而且没有任何报错（静默失效，本工程踩过多次）。
     */
    bool bPoolBaselineReady = false;

    /** 每类的净变化（件/分钟），RefreshRowStyles 查表用；正 = 池子在涨。 */
    TMap<UClass*, float> NetRateCache;

    /**
     * ★1.0.8BJ 「至少做过一次差分」的标记（净值列据此区分「还没测」与「真的没变」）。
     *
     * 为什么需要它：面板刚打开的第一拍只建基线、不出读数。此时 `NetRateCache` 是空的，
     * 而空表在渲染侧读出来也是 0 —— 于是「还没测到」被显示成「净 0」，看起来像池子稳住了。
     * 有了这个标记，首拍显示「净 --」，测过之后才可能显示「净 0」。
     */
    bool bNetMeasured = false;

    /**
     * ★1.0.8BI 上一拍「本机每类速度」的快照（`ShouldShowRow` 只读用它）。
     *
     * 为什么需要缓存：`ShouldShowRow` 是 const 且在 `RefreshRowStyles` **之前**被调用，
     * 拿不到那次算好的局部 `ItemRates`。而"本机正在搬的行不许折叠"这条判据必须能在
     * 那里读到速率 —— 一台上传站把缓冲泵空的那一瞬间、或下载站刚把货取到 0，
     * 只看"云端 > 0"会让**正在工作**的行闪一下消失。
     */
    TMap<UClass*, float> LastItemRates;

    /**
     * ★1.0.8AQ 本次打开面板的建筑种类（NativeConstruct 里解析一次）。
     *
     * 刻意不复用 `bUploaderMode` 一个 bool 打天下：液体下载站与固体下载站同为「下载侧」，
     * 但数据源（云端池里的液体 vs 固体）、选择写回（mSelectedFluidClass vs mOutputFilter）
     * 、目录（液/气全集 vs 生产栏产物）全都不同。四种站 = 四份分支，必须是枚举。
     */
    EWTCloudPanelKind PanelKind = EWTCloudPanelKind::None;

    /**
     * ★1.0.7j 交互栈摘除幂等标记。
     *
     * 本面板由游戏经 `mInteractWidgetSoftClass` 创建并压进 `UFGGameUI::mInteractWidgetStack`，
     * 关闭时必须走 `RemoveInteractWidget` 摘栈（否则 HasActiveInteractWidget() 恒真 ⇒ E 全锁死、
     * 面板再也开不出来）。OnCloseClicked 与 NativeDestruct 都可能触发摘栈，此标记保证只摘一次。
     * 不放在 UPROPERTY 里：纯运行期状态，不进存档。
     */
    bool bInteractStackReleased = false;

    /**
     * ★1.0.8BH 输入模式是否**已经**由本类主动还原过（消除"晚到的还原覆盖背包"竞态）。
     *
     * 背景（用户实测「选择转移后弹出背包丢失光标」的真实成因链）：
     *   `OnCloseClicked()` 已经做了 GameOnly + 隐光标 + Reset*，随后 `RemoveFromParent()`。
     *   但 `RemoveFromParent` 只是把 SObjectWidget 从视口摘掉 —— 当它正处在 Slate 的输入回调里
     *   （我们就是这种情况：点按钮 → 回调里关自己），SObjectWidget 还有额外引用，
     *   `NativeDestruct` 会被**延后**到 Slate 清理时才执行。若那时原版背包已经打开，
     *   `NativeDestruct` 里那份"对称还原"就会把背包刚设好的 GameAndUI + 光标**按回 GameOnly**。
     *
     * ⇒ 主动关闭走过的路径，`NativeDestruct` 不再重复还原（还原本身是幂等的，重复只有害处：
     *   它会晚到并覆盖别的界面刚设好的输入模式）。只由 `OnCloseClicked` 置位，`NativeConstruct` 复位。
     */
    bool bInputRestoredBySelf = false;

    /**
     * ★1.0.7e 一次性的目录构建（含进度解锁判定）。管理器未就绪时**不置 bCatalogBuilt**，
     * 以便下次重试。
     */
    void EnsureCatalog();

    // ---- ★1.0.7k 里程碑阶段过滤 + 按阶段排序（用户诉求「云端加上面板过滤筛选条件和里程碑顺序排列」）----

    /**
     * 物品类 → 里程碑阶段号（0..10 = 阶段；99 = 未归类；-1 = 不在目录里）。
     *
     * 口径与面板的「目录」同源，不另起一套：
     *   • 配方产物：产出它的配方，被哪个已解锁蓝图（里程碑/MAM/替代）发放过，取**最小** TechTier；
     *   • 原始资源：没有配方，按 `Remote_FactoryResourceNodes.cpp:WT_LookupNodeTier` 的资源阶段表
     *     （铁/铜/石灰石 T0、煤 T3、硫 T5、SAM T6、铝土/石英 T7、铀 T8）取值。
     */
    int32 GetItemTier(UClass* ItemClass) const;

    /** 当前目录里**实际出现过**的阶段键（升序，99 沉底）—— 下拉只列这些，不列全游戏表。 */
    TArray<int32> CollectCatalogTiers() const;

    /** 重建「阶段」下拉的选项与选中态（目录重建后必须调，否则选项与数据脱节）。 */
    void RebuildMilestoneFilter();

    /** 某个物品是否通过当前的阶段过滤（SelectedMilestone < 0 = 不过滤）。 */
    bool PassesMilestoneFilter(UClass* ItemClass) const;

    /** ★1.0.7k 阶段下拉回调。 */
    UFUNCTION()
    void OnMilestoneFilterChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

    // ---- ★1.0.7p 「物品」过滤（用户诉求「在阶段后面加个物品」）----------------------
    //
    // 与阶段下拉并排的第二个筛选维度，键 = 物品类名（FString，空 = 全部）。
    // 为什么键用类名字符串而不是裸 UClass*：选项表是**视图层**数据（不进存档、不进 GC），
    // 用字符串可以避开 UPROPERTY 反射要求，比较时按 GetName()（类名唯一且稳定）。

    /** 重建「物品」下拉的选项与选中态（目录或阶段变化后必须调，否则选项与过滤结果脱节）。 */
    void RebuildItemFilter();

    /** 某个物品是否通过当前的物品过滤（SelectedItemKey 为空 = 不过滤）。 */
    bool PassesItemFilter(UClass* ItemClass) const;

    /** ★1.0.7p 物品下拉回调。 */
    UFUNCTION()
    void OnItemFilterChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

    /**
     * ★1.0.7e 计算当前进度签名=(已解锁里程碑/MAM/替代来源数<<8)|最高阶段号。
     * 每 0.5s 与 ProgressSig 比对，变了就重建目录（升阶段/研究 MAM 后新产物自动出现）。
     * 管理器未就绪返回 -1。
     */
    int32 ComputeProgressSig() const;

    /** ★1.0.7f 当前选中的那一种（来自建筑 mOutputFilter 的第一个元素）；未选返回 nullptr。 */
    UClass* GetSelectedClass() const;

    /**
     * ★1.0.8BB 把 Cls 的云端存量全部搬进玩家背包。
     *
     * @return 实际搬入件数；-1 = 没给物品类；-2 = 云端子系统/玩家背包不可用；-3 = 云端本来就没货。
     *         ≥0 时**包括 0** 都是「尝试过了」：0 表示背包放不下（原版 API 会尽量塞，塞不进就返回 0）。
     */
    int32 TakeCloudItemsToBackpack(UClass* Cls);

    /**
     * ★1.0.8BD 上传站：把「本站正在输入的物品」收集进 `InboundCache`。
     *
     * 用户 2026-10-28：「UI 中不再显示当前列表，输入什么就显示什么」「不是收缩框，是输入的物品」
     *   ⇒ 上传站列表显示的是**这台机器实际在收的东西**，不是云端总库存、也不是全目录。
     *
     *   · 固体上传站 = 站内库存里实际存在的物品类（`AFGBuildableStorage::GetStorageInventory`
     *     的栈列表；同站多格同类只留一个类）；
     *
     * 结果进 `InboundCache` 供 `ShouldShowRow` 使用 —— ShouldShowRow 是 const 且每行都会问，
     * 不能在里面读库存（那是 TArray 拷贝 + 遍历）。
     */
    void RefreshInboundCache();

    /** 固体上传站（AWTUpgradeStorage）—— 其它模式下恒 nullptr。 */
    AWTUpgradeStorage* GetUploaderStation() const;



    /**
     * ★1.0.8BH 转移完成后「先关本面板、下一拍再开原版背包」（用户 2026-10-28 实测
     * 「选择转移后弹出背包丢失光标」）。
     *
     * ## 为什么会丢光标（1.0.8BG 的缺陷，根因在本类自己的还原逻辑里）
     *   1. 点【转移】→ `NotifyOnToggleInventory()` → 原版蓝图打开背包；
     *   2. 游戏在打开新交互界面时会把本面板从 `mInteractWidgetStack` 顶掉 ⇒ 本类 `NativeDestruct` 执行；
     *   3. `NativeDestruct` **无条件** `SetInputMode(GameOnly)` + `bShowMouseCursor = false`
     *      （那是"关面板即回自由视角"的对称还原，单独用时是对的）；
     *   4. 于是刚打开的背包被我们按成 GameOnly + 无光标 ⇒ **界面在、点不动**。
     *
     * ## 修法的先后顺序（顺序本身就是修复点，不能换）
     *   ① `OnCloseClicked()` 先把本面板**彻底**退出（摘交互栈 → GameOnly + Reset* → RemoveFromParent）；
     *   ② **下一拍**（0.12s 定时器，不是同帧）才发原版开背包事件 —— 此时 Slate 与交互栈里
     *      已经没有本面板，原版背包自己设 GameAndUI + 光标，没有任何东西能反向覆盖它。
     *
     * 为什么不"跳过还原、直接把背包开着"：万一原版这条链没接上（蓝图没绑），玩家就会卡在
     * UIOnly + 光标可见但游戏不动 —— 比丢光标更糟。先关面板这条路上，**无论背包开不开，
     * 玩家都处于可正常游玩的 GameOnly 状态**，失败也只损失"界面临时没弹出来"。
     *
     * 定时器 lambda **只捕获玩家控制器的弱引用**，不捕获 `this`：本函数返回前面板已经移除，
     * 下一拍时它可能已被 GC。
     */
    void OpenInventoryAfterClosingSelf();

    /**
     * 按当前搜索词 + 阶段过滤 + 物品过滤重算 Visible（结构重建的唯一入口）。
     * ★1.0.7s：**不再**因「已选中某一种」而砍成 1 项（见文件头 1.0.7h 段的撤销说明）。
     */
    void RebuildVisible();

    /** 清空并重建全部行 widget（只在搜索词变化 / 选中变化 / 首次构建时调用）。 */
    void RebuildRows();

    /** 只刷新已存在行的选中色、数量文字与按钮可用态（点击/定时刷新都走这里，不重建行）。 */
    void RefreshRowStyles();

    /**
     * ★1.0.7n 刷新过滤行最右侧的「下载速度 N/分」。
     *
     * 只读 `GetDownloadRatePerMinute()`（O(窗口样本数) ≈ 3），由 NativeTick 的 0.5s 节流块调用，
     * 不碰世界、不重建任何控件。
     */
    void RefreshSpeedText();

    /**
     * ★1.0.7t 该行是否显示：云端合计 > 0，或者它就是当前选中的那一种（选中行永不被藏），
     * 或者玩家用「物品」下拉明确筛了某一类。判定只读 AmountCache（由 RefreshAmounts 每 0.5s
     * 刷一次），不碰世界。
     *
     * ★1.0.7s 曾短暂改成「一律显示」，被用户当场否决（截图「满屏云端 0」）：
     *   列表要的是「云端现在有什么」，不是「目录里有多少种」。
     * ★1.0.7s 真正该修的是另一处：`RebuildVisible()` 不能因「已选中一种」把行集砍成 1 项
     *   （那才是「下拉写全部（126）、列表只 1 行」的根因，已修且保留）。
     */
    bool ShouldShowRow(int32 VisibleIndex, UClass* Picked) const;

    /** 重算 AmountCache（云端合计），一次遍历原版池 + 一次遍历上传站，不按行重复遍历。 */
    void RefreshAmounts();

    /** 【选择】：挑中该行物品（单选，替换现有选择）。 */
    void SelectItem(int32 VisibleIndex);

    /**
     * ★1.0.8 【取消】：撤销输出物品选择（白名单清空 ⇒ 机器一件都不搬）。
     *
     * 不动搜索词与物品过滤 —— 取消是"这一类不搬了"，不是"把界面清空"。
     * 行集不依赖选择，所以只刷样式、不重建行（重建会把滚动位置弹回顶部）。
     */
    void ClearSelection();

    /**
     * ★1.0.8 行尾按钮 / 整行的唯一入口（用户 2026-10-27：「为什么不能在点一下取消？」）：
     *   不是它 ⇒ 选中（整体覆盖为单选）；就是它 ⇒ 取消（白名单清空 = 不下载）。
     *
     * 行与按钮共用同一条逻辑，`bFromActionButton` 这个区分参数已删除 ——
     *   它当年存在只是为了"禁止点行取消"，而那个禁令的前提（空名单 = 搬全部）已在 1.0.7x 消失。
     * 例外：老存档多项残留时点第 0 项 → 先收敛成单选，而不是清空。
     */
    void OnRowAction(int32 VisibleIndex);

    /** True when this item passes the current search box text. */
    bool MatchesSearch(const FText& ItemName) const;

    // ---- widgets ----
    UPROPERTY()
    TObjectPtr<UCanvasPanel> RootCanvas = nullptr;

    UPROPERTY()
    TObjectPtr<UBorder> RootBorder = nullptr;

    UPROPERTY()
    TObjectPtr<UTextBlock> TitleText = nullptr;

    UPROPERTY()
    TObjectPtr<UTextBlock> SelectionText = nullptr;

    UPROPERTY()
    TObjectPtr<UEditableTextBox> SearchBox = nullptr;

    /** ★1.0.7k 阶段过滤下拉（过滤行第一个控件）。 */
    UPROPERTY()
    TObjectPtr<UWTItemFilterComboBox> MilestoneCombo = nullptr;

    /**
     * ★1.0.7p 「物品」过滤下拉（★紧跟在阶段下拉**后面**，用户 2026-10-27 诉求
     * 「在阶段后面加个物品」）。选项 = 当前阶段过滤后仍存在的物品，按阶段→显示名排序。
     */
    UPROPERTY()
    TObjectPtr<UWTItemFilterComboBox> ItemFilterCombo = nullptr;

    /** ★1.0.7k 阶段过滤下拉所在的那一行（便于将来增删同一行的其它筛选控件）。 */
    UPROPERTY()
    TObjectPtr<UHorizontalBox> FilterRow = nullptr;

    /**
     * ★1.0.7n 下载速度读数（过滤行最右侧，与阶段下拉同一行）。
     *
     * 用户诉求（2026-10-27）「加上一个下载速度」：显示「下载速度 N/分」，数据来自
     * `AWTUpgradeCloudDownloader::GetDownloadRatePerMinute()`（最近 5 秒真实进缓冲的件数）。
     * 为什么放这一行：过滤器行高度已被 USizeBox 锁成 28px、右侧本来就空着，
     * 塞在这里既不新增行、也不会让下面的列表起点跳动（与 UI 对齐铁律一致）。
     */
    UPROPERTY()
    TObjectPtr<UTextBlock> SpeedText = nullptr;

    UPROPERTY()
    TObjectPtr<UScrollBox> ItemListBox = nullptr;

    // ---- row widgets (parallel to Visible / AmountCache) ----
    /**
     * ★1.0.7h 行的**根**控件（= 包住整行的那个 UButton）。折叠 0 数量行只能拿根控件改可见性，
     * 拿行尾按钮不顶用（它折叠了整行还在）。与 Visible / AmountCache 同下标。
     */
    UPROPERTY()
    TArray<TObjectPtr<UButton>> RowRoots;

    /**
     * 行尾唯一操作按钮：未选中文案【选择】，选中文案【取消】（同一控件、宽度不变、恒可见）。
     * 与 `RowActionLabels` 同下标；上传站模式整列不建 ⇒ 该数组元素为 nullptr。
     */
    UPROPERTY()
    TArray<TObjectPtr<UButton>> RowActionButtons;

    UPROPERTY()
    TArray<TObjectPtr<UTextBlock>> RowActionLabels;

    UPROPERTY()
    TArray<TObjectPtr<UTextBlock>> RowLabels;

    UPROPERTY()
    TArray<TObjectPtr<UTextBlock>> RowAmounts;

    /**
     * ★1.0.8BI 行内「云端净变化」列（紧跟数量列）。
     *
     * 解决的正是用户 2026-10-28 问的那个场景：「同一个物品，一边上传，一边下载」——
     * 池子被同时灌与抽，数量列那个数字看起来**不动**，玩家会以为机器没工作。
     * 这一列直接写出净速度：`+60/分`（涨，红）/ `−60/分`（跌，绿）/ `0`（灰，真的平衡）。
     *
     * ★口径是整个云端池的变化（**不是本机**）：池是全局共享的，含别的站、含原版维度仓库
     *   的手动存取。所以文本带「净」字，本机自己的流量由 RowRates（↑/↓ 箭头）回答。
     * ★数据不是站体读数，而是 RefreshAmounts 里对池总量的差分采样（≥0.4s 节流）——
     *   只有全局差分才能得到真实净值。
     */
    UPROPERTY()
    TArray<TObjectPtr<UTextBlock>> RowNets;

    /**
     * ★1.0.7y 行内【正在下载 / 未下载】状态列（用户 2026-10-27 定案：「数量往前挪，
     * 这个位置放【正在下载/未下载】」）。
     *
     * 位置 = 原来「云端数量」列占的那块（行右侧、速度列之前）；数量列本身已前移到物品名
     * 右侧。判定与 RowRates **同源**（`GetItemRatesPerMinute()` 的 5 秒窗口 > 0 即在搬），
     * 只换措辞，不引入第二条数据源 —— 避免出现「正在下载」配「--」的自相矛盾。
     */
    UPROPERTY()
    TArray<TObjectPtr<UTextBlock>> RowStates;

    /**
     * ★1.0.7p 行内「下载速度」列（【状态】与【选择】之间）。
     *
     * 读数来自 `AWTUpgradeCloudDownloader::GetItemRatesPerMinute()`（按物品类分组的 5 秒窗口），
     * 没在搬这一类的行显示「--」（暗色）—— 下载站同一时刻只搬一种，其余行本就该是空的。
     */
    UPROPERTY()
    TArray<TObjectPtr<UTextBlock>> RowRates;

    // ---- data ----
    /** ★ CRASH GUARD: TSubclassOf holds a UClass*, so this must be reflected or the GC cannot see
     *  the reference and a descriptor class could be recycled out from under a live panel
     *  （与 Remote_FactoryPanelWidget 里同样的 GC 理由）。 */
    UPROPERTY()
    TArray<TSubclassOf<UFGItemDescriptor>> Catalog;

    /** Catalog 的搜索过滤结果（行序号 = 该数组下标）。 */
    UPROPERTY()
    TArray<TSubclassOf<UFGItemDescriptor>> Visible;

    /**
     * ★1.0.7k 物品类 → 里程碑阶段（0..10 / 99 未归类），与 Catalog 同步构建与清空。
     *
     * 必须 UPROPERTY：键是裸 UClass*，不反射就是 GC 看不见的引用（与 Catalog 同一条 CRASH GUARD）。
     */
    UPROPERTY()
    TMap<UClass*, int32> TierByItem;

    /** ★1.0.7k 当前阶段过滤：-1 = 全部；0..10 = 阶段号；99 = 未归类。 */
    int32 SelectedMilestone = -1;

    /** ★1.0.7k 阶段下拉每一行对应的阶段键（第 0 项固定 -1 = 全部）。 */
    TArray<int32> MilestoneOptionKeys;

    /** ★1.0.7p 物品下拉每一行对应的物品类名（第 0 项固定空串 = 全部）。 */
    TArray<FString> ItemOptionKeys;

    /** ★1.0.7p 当前物品过滤：空 = 全部；否则 = 某个物品的类名。 */
    FString SelectedItemKey;

    /**
     * ★1.0.7p 重建物品下拉期间的重入守卫（与 bRebuildingMilestoneFilter 同因：
     * ClearOptions / SetSelectedIndex 会广播 OnSelectionChanged）。
     */
    bool bRebuildingItemFilter = false;

    /**
     * ★1.0.7k 重建下拉选项期间的重入守卫。
     * `ClearOptions` / `SetSelectedIndex` 会广播 OnSelectionChanged（可能是 ESelectInfo::Direct，
     * 也可能不是），不挡住就会在「选项只填了一半」时先跑一次过滤，读到半成品状态。
     */
    bool bRebuildingMilestoneFilter = false;

    /** 云端合计，与 Visible 并行。 */
    TArray<int32> AmountCache;

    /** 当前搜索词（小写化前的原始输入，比较时按 IgnoreCase）。 */
    FString SearchText;

    bool bCatalogBuilt = false;

    /**
     * ★1.0.7e 进度签名 = (已解锁的里程碑/MAM/替代配方来源数 << 8) | 最高阶段号。
     * 玩家升阶段或研究出新的 MAM 后签名会变，面板据此重建目录（解锁的产物才出现在列表里）。
     */
    int32 ProgressSig = -1;

    /** 数量刷新的节流累加器（云端池是别人在写，逐帧扫没必要）。 */
    float AmountRefreshAccum = 0.f;

    // ---- ★1.0.8BJ 刷新驱动（世界定时器）----------------------------------------------
    /**
     * 0.30s 循环定时器句柄（`OnPanelRefreshTick`）。
     *
     * NativeConstruct 里起、NativeDestruct 里清。绑的是 UObject 成员函数 ⇒ 即使某条拆毁路径
     * 漏了 ClearTimer，TimerManager 也会因对象失效而自动丢弃这一次触发（不会访问野指针）。
     */
    FTimerHandle RefreshTimerHandle;

    /** 只在第一次 NativeTick 打一条「tick 活着」的日志（排查 tick 是否执行的取证点）。 */
    bool bLoggedNativeTick = false;

    /** 只在第一次定时器回调打一条日志（证明数据刷新驱动真的在跑）。 */
    bool bLoggedRefreshTimer = false;

    /**
     * 诊断日志节流：每 3 秒一条「这一拍实际读到了什么」。
     *
     * 用户 2026-10-28 报「上传下载不实时」，而面板刷新是"读别人的状态"，肉眼无法区分
     * 「没刷新」与「刷新了但源头没变」。这条日志把两者分开：`amount` 变了而屏幕没变 ⇒ UI 问题；
     * `amount` 一直同一个值 ⇒ 源头（云端池/站体速率）问题。留作长期取证点，代价每 3 秒一行。
     */
    double LastDiagLogTime = 0.0;

    /** ★1.0.7h 上一次刷完后「实际显示了几行」（= 云端有存货的行数 + 选中的那一行）。 */
    int32 NumShownRows = 0;

    /** ★1.0.7h 上次打日志时的显示行数（只在数字变化时打一条，避免每 0.5s 刷屏）。 */
    int32 LastLoggedShownRows = -1;

    // ---- ★1.0.8BG 行尾【转移】/【清空】按钮 ----------------------------------
    /**
     * 每行右侧第二颗按钮：固体【转移】（整类进背包）/ 液体【清空】（整类销毁）。
     *
     * ★1.0.8BG 起**上传站也建这一列**（1.0.7z 的"上传站整列不建"已作废 —— 那时上传站确实
     * 没有任何可执行动作；现在它有"把这一类取回背包"这个动作，玩家清理积压全靠它）。
     * 与 RowLabels 同下标；云端 0 的行该按钮禁用（点了也没货，别给假反馈）。
     */
    TArray<TObjectPtr<UButton>> RowTakeButtons;

    TArray<TObjectPtr<UTextBlock>> RowTakeLabels;

    /**
     * 上一次取货的结果文案 + 过期时刻（GetTimeSeconds）。
     *
     * 为什么不像异常态那样每帧算：取货结果是**一次性事件**，没有可重算的来源
     * （搬完云端就空了，"搬了多少"只有那一瞬间知道）。所以存下来显示 6 秒，过期自动回落。
     */
    FString TakeResultNote;

    /** 0 = 没有待显示的取货结果。 */
    double TakeResultUntil = 0.0;

    /**
     * ★1.0.8BD 上传站"正在输入的物品"缓存（`RefreshInboundCache` 定时重建）。
     *
     * 存 UClass* 不存 TObjectPtr：UClass 是引擎静态类对象、不会被 GC 回收
     * （与 TSubclassOf 持有的语义一致），所以不需要 UPROPERTY 参与引用追踪。
     */
    TSet<UClass*> InboundCache;
};
