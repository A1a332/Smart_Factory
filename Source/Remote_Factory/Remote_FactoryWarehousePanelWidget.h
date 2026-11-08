#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Remote_FactoryWarehousePanelWidget.generated.h"

class SWidget;
class UVerticalBox;
class UScrollBox;
class UEditableTextBox;
class UTextBlock;
class UButton;
class UHorizontalBox;
class UWidget;
class UFGItemDescriptor;
class URemote_FactoryWarehousePanel;   // ★前向声明：行控件类在面板类之前声明，但其成员/形参用到面板类指针

// ★2026-11-03 集装箱面板的一行：单个集装箱物品（图标 + 名称 + 三源数量 + 取出按钮）。
// 与面板同属一个 .generated.h（避免触发首个 UCLASS 的 UHT 竞态）。
// 点按钮经面板的 RequestTakeFromContainer → RCO 服务端 RPC，从集装箱把该行货拉进背包。
//
// 【数据来源】Setup 的四个参数全部由面板 RefreshContents 从智能仓储的聚合表里取：
//   InNumContainer = Remote_FactorySmartWarehouse::GetContainerAvailability()[Cls]（全局集装箱总量）
//   InNumCloud     = Remote_FactorySmartWarehouse::GetCentralStorageRealCount()[Cls]（纯原版云端，不含集装箱）
//   InNumBackpack  = 本玩家背包 GetNumItems(Cls)
// 本行不自己查数据、不持有库存引用 —— 数据只存快照，用于显示 + 夹取请求数量。
//
// 【数量语义】取出按钮是「一颗按钮三档数量」，靠修饰键区分（见 OnTakeClicked）：
//   左键 = 1；Shift+左键 = 10；Ctrl+左键 = 一组（GetStackSize，铁矿石 66 / 电线 500）。
//   ★Ctrl 早期是「全部」，曾一次搬空 3559 个电线、整行消失，用户以为物品丢了，故改成「一组」。
UCLASS()
class URemote_FactoryWarehouseRow : public UUserWidget
{
    GENERATED_BODY()
public:
    // 由面板在 CreateWidget 后、加入列表前调用：填入本行数据并回指面板。
    // ★顺序敏感：数据必须在 AddChild 之前设好，因为 RebuildWidget 建树时直接读这些成员填文字/图标。
    // ★2026-11-05 新增 InContainer：本行属于哪个容器（「一个容器一行」展开后的物品行）。
    //   取/存都限定在这个容器里，避免"同一个物品散在三个箱子，点哪行都从全部箱子拿"的困惑。
    //   传 nullptr 表示全局（不带容器限定的调用方仍可用）。
    void Setup(TSubclassOf<UFGItemDescriptor> Cls, int32 InNumBackpack, int32 InNumCloud,
               int32 InNumContainer, URemote_FactoryWarehousePanel* Owner, AActor* InContainer = nullptr);
    // ★建控件树必须在 RebuildWidget（Slate 按树建完后 NativeConstruct 再改不生效，会空白）。
    virtual TSharedRef<SWidget> RebuildWidget() override;

private:
    // 取出按钮的三档语义（★2026-11-04 改）：
    //   左键 = 1 个；Shift+左键 = 10 个；Ctrl+左键 = 一组（按该物品单格堆叠上限，不是「搬空整个集装箱」）。
    //   历史教训：早期 Ctrl=全部，把 3559 个电线一次搬空、列表整行消失，用户以为物品丢了。
    //   修饰键读自 FSlateApplication::Get().GetModifierKeys()；Ctrl 优先于 Shift（同按时按一组）。
    UFUNCTION()
    void OnTakeClicked();

    // 统一取货入口：Want 会按 NumContainer 夹取（请求量不超过本行显示的集装箱存量），≤0 直接忽略。
    // 真正的服务端转移在 RCO，这里只发请求；面板会在请求后清签名，强制下次刷新重建行。
    void TakeAmount(int32 Want);

    // ★2026-11-05【存入】按钮，与取出完全对称的三档（方向相反）：
    //   左键        = 100 个
    //   Shift+左键  = 一组（该物品单格堆叠上限）
    //   Ctrl+左键   = 背包里该物品的全部（NumBackpack 快照量）
    // ★为什么"全部"取 NumBackpack 而不是别的：存入的源是背包，能存的上限就是背包持有量。
    UFUNCTION()
    void OnStoreClicked();

    // 统一存入入口：Want 先按 NumBackpack 夹，再由服务端按实际持有量与箱子容量二次夹。
    void StoreAmount(int32 Want);

    UPROPERTY()
    TSubclassOf<UFGItemDescriptor> ItemCls = nullptr;
    int32 NumContainer = 0;   // 本行显示时的集装箱快照（只用于显示与夹取，真实扣减在服务端）
    int32 NumBackpack = 0;
    int32 NumCloud = 0;
    UPROPERTY()
    URemote_FactoryWarehousePanel* OwnerPanel = nullptr;   // 回指面板，取出时经它转发 RPC

    // ★2026-11-05 本行所属的容器（「一个容器一行」展开后的物品行才有值）。
    //   取/存时带给 RPC，让服务端只操作这一个箱子。
    //   ★用弱引用：箱子被拆掉后这里自动失效，不会留下悬空指针。
    TWeakObjectPtr<AActor> ContainerActor;
};

/**
 * ★2026-11-05 容器行 —— 「一个容器一行」的外层行。
 * 显示：展开箭头 + 容器名 + 概览（几种 / 共多少个）。点击整行切换展开/收起。
 *
 * ★为什么单独一个类而不是复用 URemote_FactoryWarehouseRow：那个行的数据模型是「物品类 + 三源数量」，
 *   这里是「容器名 + 种类数/总件数」，字段没有一个是共用的，硬塞会让两边都变脏。
 *
 * ★展开状态不存在这个控件里：控件每次刷新都会重建（RefreshContents 是 0.5s 节流的），
 *   状态存在面板的 ExpandedContainers 里，按容器名做 key。
 */
UCLASS()
class URemote_FactoryWarehouseContainerRow : public UUserWidget
{
    GENERATED_BODY()
public:
    // 由面板在 CreateWidget 后、AddChild 前调用（顺序敏感：RebuildWidget 直接读这些成员）。
    //   InKey      = 展开状态的 key（容器显示名，全局唯一）
    //   InLabel    = 这一行**显示**的文字（可能是「集装箱 5 · 铁板」这种带物品名的组合）
    void Setup(const FString& InKey, const FString& InLabel, int32 InNumKinds, int32 InNumItems,
               bool bInExpanded, URemote_FactoryWarehousePanel* Owner);

    virtual TSharedRef<SWidget> RebuildWidget() override;

private:
    // 整行点击 = 切换展开。用 OnPressed 不用 OnClicked：面板跟着背包滑动，
    // 按下与抬起落在不同位置时 OnClicked 会被丢弃（2026-11-05 已踩过这个坑）。
    UFUNCTION()
    void OnToggleClicked();

    FString Key;            // 展开状态的 key（同时也是容器显示名）
    FString Label;          // 行上显示的文字
    int32 NumKinds = 0;     // 该容器里有几种物品
    int32 NumItems = 0;     // 该容器里总共有多少件
    bool bExpanded = false;

    UPROPERTY()
    URemote_FactoryWarehousePanel* OwnerPanel = nullptr;
};

/**
 * ★2026-11-04 智能仓储左侧面板（停靠在【物品栏】左侧，与原版【位面】同一条左侧带）。
 * 纯 C++ UUserWidget，不依赖原版背包蓝图的子节点。
 * 结构：深色底板 → 标题栏(含关闭) + 搜索框 + 滚动物品列表（每行可「取出」到背包）。无底部手动上传。
 *
 * 【生命期（三处坑，改之前先读）】
 *   ① 创建：RegisterWorld 在 GameWorldModule::POST_INITIALIZATION 调用，那时 GetFirstPlayerController()
 *      常为 null ⇒ EnsurePanelCreated 静默 return。故失败时挂 0.1s 重试 ticker（上限 600 次 = 60s），
 *      控制器一到位就创建 + 注册监听 + 自我注销。
 *   ② 注册：RegisterToggleListener 绑 AFGPlayerController::OnToggleInventory（动态 multicast，必须
 *      AddDynamic + UFUNCTION），同时起【每帧】ticker（间隔 0，滑入动画要逐帧）。
 *   ③ 保活：RemoveFromParent 会让 Slate 拆掉、UObject 可能进 pending kill ⇒ 委托回调会静默失效。
 *      故【不把显隐押在委托上】，TickRefresh 每帧自观测原版背包 widget 可见性作为真相；
 *      委托只当「立即响应」的加速通道。GPanel 是 weak ptr，但 NativeDestruct 里【不要】Reset 它
 *      （背包关闭也会走 NativeDestruct，Reset 了下次开背包面板就没了）。
 *
 * 【显隐唯一入口】ShowPanel / HidePanel，用显式状态位 bAttached 记账。
 *   绝不拿 IsInViewport() 当闸门：RemoveFromParent 后它的几何缓存仍是旧值 ⇒ 误判"已在视口"
 *   ⇒ 跳过 AddToViewport ⇒ 面板再也不出现（这个坑用户复现过多次）。
 *
 * 【暂停菜单（ESC）】暂停时原版背包 widget 仍在但不可见 ⇒ 旧逻辑误判"背包关了"→ 收面板，
 *   回游戏后挂不回来（用户原话「我就按了一下 ESC 就没了」）。故暂停期间冻结状态，详见 TickRefresh。
 *
 * 【滑入动画】照原版背包手感，从下往上浮出（0.22s / EaseOutQuint / 位移 42px）。
 *   绝不读当前 Slot Y 再叠加偏移（会逐帧累加把面板推出屏幕），只用 Layout 写好的 SlideTargetY。
 */
UCLASS()
class URemote_FactoryWarehousePanel : public UUserWidget
{
    GENERATED_BODY()
public:
    // 由 Remote_FactoryWarehousePanel_Init 调用：单例创建面板并注册背包开关监听 + 刷新 ticker。
    // 失败（控制器未就绪）会自动挂重试 ticker，见函数内注释。
    static void RegisterWorld(UWorld* World);

    // ★2026-11-04 延迟创建用：自由函数 lambda 不能调 private 的 RegisterToggleListener，
    //   故暴露一个静态入口（内部校验 GPanel 有效性后转调）。
    static void RegisterToggleListenerForCurrentPanel();

    // ★面板与行都在 RebuildWidget 里建控件树（NativeConstruct 里建会空白：Slate 已按空树建完）。
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;


    // ★2026-11-03 手动取出：经 RCO 服务端 RPC 把 Want 个 Cls 从集装箱拉进本玩家背包。
    //   集装箱是 actor 库存，必须服务端改；面板是客户端 widget，不能本地直接调智能仓储的转移函数。
    //   Want 由行控件按 NumContainer 夹好再进来。
    void RequestTakeFromContainer(TSubclassOf<UFGItemDescriptor> Cls, int32 Want, AActor* OnlyContainer = nullptr);

    // ★2026-11-05【存入】两个入口（都是客户端 → RCO 服务端 RPC）：
    //   RequestStoreToContainer      —— 行内【存入】按钮用，只存指定物品 Want 个。
    //   RequestStoreAllToContainers  —— 顶部【全部存入】用，把背包里所有建造材料一次存完。
    //   ★两者都在 RPC 后作废签名，让界面立刻跟着变（服务端是异步的，不等 0.5s 节流）。
    void RequestStoreToContainer(TSubclassOf<UFGItemDescriptor> Cls, int32 Want, AActor* OnlyContainer = nullptr);
    void RequestStoreAllToContainers();

    // 顶部【全部存入】按钮回调。用 OnPressed（不是 OnClicked）：本面板跟着背包滑动，
    // 按下与抬起可能落在不同位置，OnClicked 会被丢弃 —— 这是 2026-11-05 踩过的坑。
    UFUNCTION()
    void OnStoreAllClicked();

    // ★2026-11-04 对照探针运行时开关（控制台命令 wt.PanelCmp 调用）。
    //   把「原版物品栏」和「本面板」的坐标逐帧打进同一行日志，用于判断两者是否同步。
    //   做成 public 是因为控制台 lambda 拿不到 private 成员；它只写三个诊断字段，不碰游戏状态。
    void SetCompareProbe(bool bEnable, int32 Budget);

private:
    // 注册背包开关监听（AFGPlayerController::OnToggleInventory）+ 启动【每帧】刷新 ticker（间隔 0）。
    // ★间隔必须是 0：滑入动画要逐帧插值，早期用 0.5s，0.18s 的动画只插到一个点，观感是"跳一下"（用户反馈"不够平滑"）。
    //   重活（布局/内容刷新）改由 TickRefresh 内部按 RefreshAccum 节流 0.5s，故每帧并不重。
    // 由 RegisterWorld / RegisterToggleListenerForCurrentPanel 调用，bRegistered 守卫保证只装一次。
    void RegisterToggleListener();

    // 背包开关回调（动态委托，必须 UFUNCTION）：开则停靠显示，关则移除。
    // ★暂停菜单开着时【必须直接 return】：按 ESC 时原版也广播 open=0，但那不是玩家关背包，
    //   是暂停菜单把背包顶掉了（日志里 open=0 后紧跟 Widget_PauseMenu_* 加载）。响应了就会收面板且挂不回来。
    UFUNCTION()
    void OnInventoryToggled(bool bIsOpen);

    // ★2026-11-04 修「点 ❌ 后再开背包看不到」：显隐改用显式状态位 bAttached，
    //   不再用 IsInViewport() 做闸门（RemoveFromParent 后它的几何缓存仍是旧值 ⇒ 误判"已在视口"
    //   ⇒ 跳过 AddToViewport ⇒ 面板再也不出现）。ShowPanel/HidePanel 是唯一的显隐入口，幂等。
    //   ShowPanel 顺序敏感：先 Layout（算出目标 Y 写进 SlideTargetY）→ 再 StartSlideIn → 最后 RefreshContents。
    //   HidePanel 会顺带 bSliding=false，下次开重新播动画。
    void ShowPanel();
    void HidePanel();

    // 搜索框文本变化（动态委托，必须 UFUNCTION）：更新过滤词并重建列表。
    UFUNCTION()
    void OnSearchChanged(const FText& Text);

    // 标题栏关闭按钮：★2026-11-05【第二十九次修】改为「收起成一条窄标签」，
    //   不再整块隐藏 —— 用户要的是原版位面面板那种「收起来留一个向左的小箭头，点它再展开」。
    UFUNCTION()
    void OnCloseClicked();

    // 收起/展开（成对）。CollapsePanel 把 RootBorder 的内容换成只有一个 `‹` 的窄条，
    //   并让布局把整块压到 26x46；ExpandPanel 换回完整内容并重算布局。
    //   ★换的是 UBorder 的 Content，不是改可见性 —— 那些行控件与搜索框都还在内存里，展开时不用重建。
    void CollapsePanel();
    void ExpandPanel(bool bSilent = false);

    // 折叠条上那个 `‹` 按钮（动态委托，必须 UFUNCTION）。
    UFUNCTION()
    void OnTabClicked();

    // 每帧 ticker 入口：整个显隐状态机的唯一驱动（返回 true = 继续 tick）。
    //   每帧只做三件轻活：① 观测背包开/关（暂停菜单期间冻结）② 按状态 Show/Hide ③ 推进滑入动画。
    //   重量级的 LayoutLeftOfInventory + RefreshContents 按 RefreshAccum 节流到 0.5s 一次。
    //   ★不把显隐真相押在 OnToggleInventory 委托上（它会静默失效），这里自观测 widget 可见性才是真相，
    //     委托只当"立即响应"的加速通道。
    bool TickRefresh(float DeltaSeconds);

    // 周期性刷新内容（读三类可用量，按搜索词过滤后重建列表，保留滚动位置）。
    // 有内容签名 LastSig 短路：签名没变就直接返回，避免每次刷新都把行按钮拆掉重建造成点击丢失。
    void RefreshContents();

    // ★2026-11-04 左移布局（第三版）：把面板停靠到【物品栏】左侧那条空白带，顶底与紧邻的原版面板齐平。
    //   做法：枚举全部 UUserWidget，只认"一整块面板"级控件，取最靠左的竖边 LeftMost 及其 Top/Bottom，
    //   于是 空白带 = [SideGap, LeftMost - SideGap]（左右间距对称），纵向照抄参照面板 Y=Top、H=Bottom-Top。
    //   读不到有效几何则回退"贴左 SideGap + 纵向居中"，保证始终可见。
    //   末尾必须写 SlideTargetY = Y，入场动画才能落到正确位置。
    //   ⚠ 锚点必须 (0,0) + Alignment(0,0)（Position 即左上角），居中锚点无法直接指定绝对 Y（曾导致顶底不齐）。
    void LayoutLeftOfInventory();

    UPROPERTY()
    UVerticalBox* RowBox = nullptr;

    UPROPERTY()
    UScrollBox* ScrollBox = nullptr;

    UPROPERTY()
    UEditableTextBox* SearchBox = nullptr;

    // ★★★2026-11-05【第三十一次修·清场】收起态与折叠条控件。
    //   bCollapsed：面板处于「收成一条」的状态（不是隐藏），布局见 LayoutLeftOfInventory 末尾的强制段。
    //   PanelZOrder：挂载层级。★必须是 Game 层（AddToViewport）+ 明确 ZOrder —— 实测 100 就够：
    //     日志 `OCCLUSION SOLVED at z=100`，原版购物清单那块满屏屏障再也吃不到我们的点击。
    bool bCollapsed = false;
    int32 PanelZOrder = 100;

    UPROPERTY()
    class UBorder* RootBorderRef = nullptr;

    UPROPERTY()
    class UVerticalBox* RootVRef = nullptr;

    UPROPERTY()
    class USizeBox* CollapsedBoxRef = nullptr;

    // 搜索过滤词（空 = 全部显示）。
    FString SearchText;

    // 上次重建时的内容签名（过滤词 + 各显示项三源数量）。相同则跳过重建，
    // 避免 0.5s 刷新把行按钮拆掉重建造成点击丢失。
    FString LastSig;

public:
    // ★2026-11-05【一个容器一行】每个容器的展开状态（key = 容器显示名，如 "集装箱 1"）。
    //   ★为什么存在面板而不是行控件里：RefreshContents 是 0.5s 节流重建的，
    //     行控件每次都会被销毁重建，状态放控件里会一秒闪回两次。
    //   ★为什么用容器名而不是索引：索引会随箱子增删漂移，名字（带编号）相对稳定。
    //     箱子被拆掉后对应条目会自然失效（最多留一点垃圾，无副作用）。
    TMap<FString, bool> ExpandedContainers;

    void ToggleContainerExpanded(const FString& Key);
    bool IsContainerExpanded(const FString& Key) const;

private:

    FDelegateHandle ToggleHandle;
    bool bRegistered = false;      // 委托（OnToggleInventory）是否已绑上
    // ★2026-11-04：ticker 与委托必须分开守。旧版共用一个 bRegistered，当 GetFirstPlayerController
    //   此刻为 null 时会"ticker 已挂但守卫仍为 false" ⇒ 再调一次就多挂一个 ticker ⇒ 动画翻倍速。
    bool bTickerStarted = false;   // 每帧 ticker 是否已启动（只启动一次）

    // ★2026-11-03 面板常驻：跟踪背包开关态与用户手动关闭态。
    //   bInventoryOpen = 「背包是否开着」的当前结论（每帧自观测，暂停期间冻结）。没直接对应 UI 状态，
    //                    只是状态机的输入，故可与 bAttached 不同步（如背包开着但用户点了 ×）。
    //   bUserClosed    = 用户点了 × 的"本次会话手动关闭"标记：置位后不再自动补挂，
    //                    直到背包被关掉（bInventoryOpen=false 时复位）或重新打开背包（OnInventoryToggled 复位）。
    //   TickRefresh 兜底：背包开着 + 未手动关 + 未挂载 → ShowPanel，确保"每次开背包都看得见"。
    bool bInventoryOpen = false;
    bool bUserClosed = false;

    // ★★★2026-11-05【第二十六次修「面板彻底不出现」的根治】
    //   历史病根：`AFGPlayerController::OnToggleInventory`（FGPlayerController.h:29/299）是游戏自己发的
    //   开关事件，本该是权威；但旧代码在回调里写进 bInventoryOpen 之后，**每一帧又被
    //   `WT_IsInventoryWidgetOpen()` 的"猜形状"结果覆盖掉**。于是：
    //     · 形状判据（宽200~700/高400~1000/比例1.15~3.2）只要不匹配玩家的 UI 缩放或原版改版，
    //       面板就**永远不出现**，且日志里连一条 ShowPanel 都没有（用户 2026-11-05 实测）；
    //     · 反过来它还会**间歇误命中**：2026-11-05 20:11 那场日志里面板每 ~2.5 秒被
    //       唤起又收起（`layout WRITE` / `HidePanel` 交替），同期探针一直报 no-shape-match。
    //   ⇒ 两处一起改（见 .cpp 同名注释）：
    //     ① 「委托优先、轮询兜底」：收到过委托 ⇒ 以委托为准；从没收到过 ⇒ 用轮询；
    //        轮询与委托**持续不一致**且方向可信 ⇒ 采纳轮询并打日志自愈（防委托静默失效后卡死）。
    //     ② 「把背包控件钉成固定身份」：形状只用来**发现一次**，之后按 TWeakObjectPtr 认人
    //        （全局 G_PinnedSidebar，委托 open=1 时开 1.5s 窗口去钉/改钉）。
    bool  bDelegateOpen = false;          // 委托报告的状态
    bool  bDelegateEverFired = false;     // 本面板是否收到过委托
    float LastDelegateAt = -1000.f;       // 最近一次委托时刻（秒）
    float PollDisagreeAcc = 0.f;          // 轮询与委托"持续不一致"的累计时长（仅可信方向累计）

    // ★2026-11-04：本面板当前是否已挂进视口（唯一真相；IsInViewport() 在 RemoveFromParent 后
    //   仍可能返回 true，不可作为显隐判据）。ShowPanel/HidePanel 与 NativeDestruct 三处维护它。
    bool bAttached = false;

    // ★★★2026-11-05【第十四次修 —— 自激振荡的真发动机】
    //   实测铁证：面板每 0.73s「AddToPlayerScreen → RemoveFromParent → 再 Add」一个循环，
    //   而 0.73s ≈ 原版背包滑入(≈0.35s) + 滑出(≈0.38s) 两段动画。
    //   机理：原版背包的开关状态机在**蓝图**里（OnToggleInventory 的 Broadcast 在源码中不存在，
    //   只有 BlueprintImplementableEvent 的 NotifyOnToggleInventory，见 FGCharacterPlayer.h:1010），
    //   它靠动画时长推进状态；我们反复 Add/Remove 会不断打断这套动画 ⇒ 状态机在两态之间来回翻
    //   ⇒ 每翻一次就广播一个 open=0/open=1 ⇒ 我们再跟着摘/挂 ⇒ 闭合成振荡。
    //   ⇒ 修法：**首次挂载后永不 RemoveFromParent**，显隐只切 ESlateVisibility。
    //     bEverAttached = Slate 侧"已经挂过一次"，此后永久为 true（除非 widget 被销毁）。
    //   ★为什么用 Hidden 而不是 Collapsed：Collapsed 不参与布局 ⇒ GetCachedGeometry 失效 ⇒
    //     下次打开又得重新等几何（回到 canvasW=0 / y=0 那个闪）。Hidden 仍参与布局，
    //     几何一直有效，重新显示时第一帧就能摆对位置。
    bool bEverAttached = false;

    // ★2026-11-05【第十四次修「点 × 点不掉」】
    //   振荡期间 open=1 会不断到来，而 OnInventoryToggled(open=1) 里有一句
    //   `bUserClosed = false`（设计意图是"重新开背包要再出现"），于是玩家刚点的 × 当场被冲掉。
    //   ⇒ 记下最后一次点 × 的时刻，1 秒内的 open=1 不当作"玩家重新开背包"，只当振荡回声。
    float LastUserCloseAt = -1000.f;

    // ★2026-11-04 左移：缓存原版物品栏控件 + 重试计数（避免每帧枚举全部 widget）。
    TWeakObjectPtr<UWidget> CachedInventory;
    int32 InvFindAttempts = 0;
    float PanelW = 340.f;   // 视口自适应算出的【自然】面板宽（不被布局缩窄结果覆盖，便于空间变大时还原）
    float PanelH = 560.f;   // 视口自适应算出的面板高（布局只改 X/宽，高保持不变）

    // ===== 对照探针（★临时诊断，定位「面板与原版物品栏的位置/出现时机是否一致」） =====
    // 逐帧把「原版背包」与「我们面板」的坐标放进**同一行**日志，直接看差多少。
    //   对比不了的两件事：原版背包可能压根没找到（原版没开/被别的面板顶掉），
    //   以及我们面板可能没挂（bAttached=false）—— 两种情况都要如实打出来，不能只打有值的那个。
    // ★为什么要有独立开关：WT_WARNF 的契约是"不高频"，而这里天生逐帧。
    //   默认 false ⇒ 不刷日志；要诊断时改 true 重编，或运行时用控制台命令打开（见 SetCompareProbe）。
    bool  bCompareProbe = false;
    int32 CompareProbeFrame = 0;      // 已输出的帧数（用于编号，方便和原版日志对齐时间轴）
    int32 CompareProbeBudget = 240;   // 本次最多输出多少帧（4 秒 @60fps，足够看清入场全过程）

    // ★2026-11-04 入场动画 —— 照原版做法重写（查证结论见下）。
    //
    // 【原版怎么做的 · 已解包核实】
    //   ① Widget_PlayerInventory.uasset / Widget_PlayerInventoryAddon.uasset 里**没有**入场动画，
    //      只有 AnimateNewInventorySlots（新物品格子高亮）。
    //   ② 真正的入场动画在 HUD 外壳 BP_GameUI.uasset 上，名字 **FadeAnim**，
    //      轨道 = RenderOpacity + Translation + EaseIn/EaseOut（UMG 资产里这四类字符串全部命中）。
    //   ⇒ 原版观感是「整体淡入为主 + 小幅位移为辅的缓动」，不是「一块面板滑 42px 进来」。
    //
    // 所以本面板也照这个来：入场**跟随**原版背包自己的位移（实测 0.22s 上浮 49.2px，见 .cpp 注释）。
    //   ★坑一：不能读当前 Slot Y 再叠加偏移 —— Layout 与动画共用同一个 Slot，读当前值会逐帧累加、
    //          把面板一路推出屏幕。故 Layout 每次都把真正的目标写进 SlideTargetY，动画只写 SlideTargetY+剩余偏移。
    //   ★坑二：ticker 间隔必须是 0（每帧），0.5s 一帧只插值一个点 = 跳变（用户反馈"不够平滑"）。
    //   ★坑三：DeltaSeconds 要钳到 1/30s，否则切场景/掉帧时 dt 突然很大，整段动画一帧跳完。
    // ★2026-11-04【修「从上往下弹出来」】两段式入场（结构性保证只会单调上浮一次）：
    //   ① ShowPanel 首次进入：只测位置 + **把面板摆到起点（目标 Y + SlideDist）+ 透明度清零**，不播动画；
    //   ② 下一帧 TickRefresh：bSlideArmed && !bSlidePlayed ⇒ StartSlideIn 从起点走到目标。
    //   这样"面板出现在最终位置"与"动一下"永远不会同时发生，也就不存在观感上的反向位移。
    //   ★SlideDist 必须是【屏幕坐标 Y 向下为正】下的**正值**（+SlideDist = 起点在下方 = 起点更低 = 往上浮）。
    //   ★2026-11-04 解包查证后的最终结论（第二轮修正）：
    //     - Widget_PlayerInventory / Widget_PlayerInventoryAddon 里**没有任何入场动画**；
    //     - 唯一的 FadeAnim 在 HUD 根 BP_GameUI 上，它的 Translation 轨道是 **-128**，
    //       那是「整个 HUD 进场时从上方落 128px」的全局动画，不是物品栏面板的。
    //   ⇒ **原版打开物品栏时，面板本身不位移，只有整体淡入。**
    //   故本面板照抄这个事实：只做 RenderOpacity 0→1 淡入，位移量 = 0。
    //   （之前 42px / 10px 的滑入都是我自己发明的，原版没有 —— 用户三次反馈"弹一下"就是它。）
    //   ★位移量保留为常量而非删掉：万一以后想要轻微浮出，改一个数即可，且 0 值的数学路径
    //     与原先完全一致（Offset 恒 0），不会引入新的分支风险。
    //   ★2026-11-04【第五次修·用户截图定案】原版确实是**从下往上浮**：
    //     截图红线框 = 原版打开 0.01s 时面板所在位置，在最终位置**下方约 50px**，随后向上滑入。
    //     （之前"原版不动"的结论是被"几何未就绪 y=0→177 跳变"误导了，那是另一个 bug，已单独修掉。）
    //   ★SlideDist 是【屏幕坐标 Y 向下为正】下的**正值**（+SlideDist = 起点更低 = 往上浮）。
    //   50px 是按截图比例估的（1080p 下红框与最终位置的高度差）。
    static constexpr float SlideDist = 50.f;
    // 入场时长：0.14s，与截图"0.1s 时已接近但未到位"吻合。
    static constexpr float SlideDur = 0.14f;
    bool  bSlideArmed = false;   // 已摆到起点、等待开播（第一段已完成）

    void StartSlideIn();
    void TickSlide(float DeltaSeconds);
    float SlideTargetY = 0.f;   // 本次动画的目标 Y（★由 LayoutLeftOfInventory 每帧写入，动画只读不写）
    float SlideTargetH = 0.f;   // 本次动画的目标高度（动画期间冻结，避免中途尺寸变化导致视觉跳动）
    float SlideT = 0.f;         // 已用时间（秒）
    bool  bSliding = false;     // 是否正在播放入场动画（HidePanel 会清掉，下次开重新播）

    // ═══════════════════════════════════════════════════════════════════════════════
    // ★★2026-11-05【第十二次修】布局目标的平滑跟随（解决「打开有一瞬间闪」「点×后重开连闪 2~3 次」）
    //
    //   病根：面板 Y 来自"参照面板"的顶边，而参照面板本身会变（原版背包自带上浮动画、
    //   位面面板显隐换人、首帧回退居中）⇒ 每次布局算出的目标 Y 会在几个值之间跳。
    //   旧实现把跳变**一次性写进 Slot** ⇒ 0.1 秒硬位移 ⇒ 玩家看到"闪"。
    //   修法：① 目标与当前 Slot 值差得少（≤ YSnapEps）⇒ 沿用旧值（吸收抖动，根本不写）；
    //        ② 差得多（真要动）⇒ 记终点，交给 TickFollow 用 0.2s EaseOutCubic 平滑过去。
    //   ★为什么用"差值"而不是"只许向上"：跳变是**双向**的，单向棘轮挡不住。
    //   ★LastWrittenPanelY 的语义从此严格是"Slot 里此刻的 Y（观众看到的位置）"，
    //     过渡期间也要逐帧同步，否则吸附判据会基于一个没人见过的数工作。
    void TickFollow(float DeltaSeconds);
    // 吸附阈值（像素）。为什么取 6：1080p 下 6px 不到面板高度的 1%，肉眼看不出；
    //   而"闪一下"的跳变量通常是几十像素起步，一定超过它。调大更稳但会更迟钝，调小则挡不住抖动。
    static constexpr float YSnapEps = 6.f;
    // 跟随过渡时长（秒）。与 SlideDur 同量级：够平滑，又不会让"位面面板刚关"这种场合显得拖沓。
    static constexpr float FollowDur = 0.20f;
    // ★GoalY/GoalH = "面板应该待在的 Y/高"，是布局层唯一的真值。
    //   为什么要把"目标"和"Slot 实际值"分成两个量：
    //     吸附要比"目标 vs 目标"（判断这次算的稳不稳），过渡要比"目标 vs Slot"（判断要不要动）。
    //     合成一个量就会出现"过渡期间 Slot 一直在变 ⇒ 每次布局都以为目标在变 ⇒ 永远收敛不了"
    //     —— 这是本轮自检仿真实测出来的坑（末值停在 259.3，目标 250）。
    //   哨兵 -99999 表示"本次打开还没有任何目标"，此时吸附与棘轮都放行（首帧必须能自由摆放）。
    float GoalY = -99999.f;
    float GoalH = 0.f;
    bool  HasGoalY() const { return GoalY > -9000.f; }
    float FollowStartY = 0.f;    // 过渡起点（= 过渡开始那一刻的 Slot 真实值）
    float FollowGoalY = 0.f;     // 过渡终点（= 布局这次认可的 GoalY）
    float FollowGoalH = 0.f;     // 过渡终点高度（高度不插值，只在起点写一次）
    float FollowTweenT = 0.f;    // 过渡已用时间（★不叫 FollowT：那个名字已被上面的跟随窗口计时占用）
    bool  bFollowing = false;    // 是否正在做布局跟随过渡（★与 bFollowWindowStarted 无关，两套机制）
    // "Slot 里此刻的 Y"的哨兵判据。★必须用函数而不是裸比较：
    //   哨兵值出现过 -99999、-99999.f 等多种写法，直接比较容易写错，统一入口才可靠。
    bool  HasLastPanelY() const { return LastWrittenPanelY > -9000.f; }
    // ═══════════════════════════════════════════════════════════════════════════════

    // ★2026-11-04【修「弹出来后往下弹一点」】动画期间锁定的终点与高度。
    //   病根：LayoutLeftOfInventory 每 0.5s 会重算 Y —— 面板刚挂上时原版背包几何还没算好，
    //   先走"纵向居中"回退，0.5s 后几何到位又改成"顶底齐平" ⇒ 动画中途 Y 被改 ⇒ 跳一下。
    //   修法：StartSlideIn 时把当时的目标记进 SlideLockedY/H，整段动画只认这一对值；
    //        Layout 在 bSliding 期间不写 Y/H（只更新 X 与宽），动画结束才恢复跟随。
    float SlideLockedY = 0.f;
    float SlideLockedH = 0.f;

    // 布局算出的原始值（供 ShowPanel 摆起点用；不受动画冻结影响）。
    float LayoutX = 0.f;        // 上一次 LayoutLeftOfInventory 算出的真实 X
    float LayoutY = 0.f;        // 上一次 LayoutLeftOfInventory 算出的真实 Y
    float LayoutH = 0.f;        // 同上，高度

    // ★2026-11-04【第四次修「从上往下弹」—— 日志实证的根因】
    //   实测日志（1080 视口）：
    //     13:23:56.653  canvasW=0    -> y=0     ← AddToViewport 同帧，画布几何还没算好
    //     13:23:57.059  canvasW=1920 -> y=177   ← 0.4s 后才拿到真实画布尺寸
    //   ⇒ 面板先在 y=0（屏幕最上沿），0.4s 后落到 y=177 ⇒ 玩家看到「从上往下掉」，
    //     而且这下落被当成"动画"来感知。**和 SlideDist 完全无关**（它已经是 0）。
    //
    //   修法：几何无效（canvasW<=200）时**不许显示**，一直保持透明；拿到真实几何后才开始淡入
    //   并把面板摆到真实位置。两者在**同一帧**完成 ⇒ 玩家看到的第一眼就在最终位置。
    bool  bGeometryReady = false;   // 是否已拿到有效画布几何（Canvas.X > 200）
    // ★2026-11-05【第十五次修】几何"长时间读不到"的超时起点（游戏时间秒；-1 = 当前就绪）。
    //   用途：几何未就绪时面板被摆到屏幕外（y = -PanelH-2000）避免首帧在 y=0 被看见；
    //   但若某种界面下几何永远读不到，1.5s 后必须放弃躲避、回退 y=0，否则面板永远不出现。
    //   ★这里替掉了原来声明了却**从未被使用**的 `GeomRetryT`（死符号，顺手清掉）。
    float GeoNotReadySince = -1.f;
    float CanvasOriginY = 0.f;      // 画布绝对原点 Y（绝对坐标→局部坐标的换算基准）

    // ★2026-11-05【第十次修「从上往下闪一下再落下来」】透明度遮挡的"欠账"账本。
    //   病根：ShowPanel 在几何未就绪时把 RenderOpacity 压到 0 遮挡 y=0 的错位闪现，
    //   但同一函数末尾**无条件**又把它设回 1 ⇒ 遮挡当场被自己撤掉 ⇒ 面板带着 y=0 不透明地出现，
    //   几帧后才被摆到正确 Y ⇒ 每次开背包都看到"从上面掉下来"。
    //   语义：true = "我压过一个 0，还没在合法时机收回"。
    //   置位点：ShowPanel 的 !bGeometryReady 分支（与 SetRenderOpacity(0) 严格成对）。
    //   清位点（三处，缺一不可）：
    //     ① ShowPanel 里 bGeometryReady 为真的收尾（位置是真值的正常路径）；
    //     ② TickRefresh 的 watchdog（ShowPanel 幂等、可能不再被调用，必须有一条按帧走的兜底）；
    //     ③ HidePanel（收起时状态归零，否则残留账本会让下次打开从错误状态起步）。
    bool  bHoldHidden = false;

    // ★2026-11-05【第十一次修「关闭的时候会闪一下」】关闭流程标记（位置冻结）。
    //   病根：原版背包按下关闭键后**先播下滑离场动画**，这一两百毫秒里它仍然 IsVisible()，
    //   而本面板的 WT_IsInventoryWidgetOpen 有 0.1s 节流、判关还要连续 2 拍 ⇒ 面板仍 bAttached
    //   ⇒ 每帧照跑 LayoutLeftOfInventory，参考的正是那个**正在下滑**的背包 ⇒ 面板跟着掉。
    //   更刺眼的是：背包滑出后被判不可见 ⇒ Layout 找不到参照 ⇒ 走回退分支**跳到屏幕正中**。
    //   语义：true = "背包已经要关了，别再跟随它的位置"。
    //   置位点：TickRefresh ③ 段，采样到 !bInventoryOpen 的那一拍（不等 2 拍确认）。
    //   清位点（两处）：
    //     ① TickRefresh ③ 段回到"开"的分支（背包又开了）；
    //     ② HidePanel / ShowPanel（面板重新出现时必须是干净状态）。
    bool  bClosing = false;

    // ★2026-11-05【第十三次修】"持续判关"的累计时长（秒），用于强制收起上限。
    //   为什么需要：判关现在有一道"UI 安静期"门控（防自己重建行时把背包 IsVisible 的假 false 当真）。
    //   但若签名持续变化（工厂一直生产 ⇒ 物品数一直变 ⇒ 每次刷新都真重建），安静期永远等不到
    //   ⇒ 玩家关背包后面板**永远收不起来**（自检仿真实测到的死锁）。
    //   ⇒ 用本变量兜底：判关持续超过 0.8s 就无条件收。真关时它一路上涨必触发；
    //     误判时背包很快恢复可见（走 bInventoryOpen 分支）⇒ 它被清零 ⇒ 永远涨不到上限。
    float CloseWaitAcc = 0.f;

    // ★★★2026-11-05【第十八次修「关闭比原版慢约 1 秒」】
    //   日志实证：`HidePanel: -> Hidden` 在 `open=0` 之后 **17~65ms** 就执行了，一点也不慢。
    //   那 1 秒延迟是**之后的重新显示**造成的，而且因为第十四次修改成"永不摘除"，
    //   重新显示只走 SetVisibility（不再 AddToPlayerScreen）⇒ **日志里一条都不留**，极难发现。
    //   机理：
    //     t0  open=0 ⇒ bWantClose ⇒ HidePanel ⇒ bAttached=false
    //     t0+16ms  下一拍 TickRefresh 采样：原版背包**还在播滑出动画（约 0.38s）**，
    //              `IsVisible()` 仍为真 ⇒ bInventoryOpen=true
    //              ⇒ 满足 `bInventoryOpen && !bUserClosed && !bAttached` ⇒ **ShowPanel 又显示**
    //     如此反复，直到背包彻底不可见、且轮询不再翻回 true ⇒ 玩家看到"关了 1 秒才真的没了"。
    //   ⇒ 修法：记住最后一次隐藏的时刻，**关闭后 0.6s 内**不接受"轮询说背包可见"作为重开依据
    //     （那 0.6s 里的"可见"必然是滑出动画的残影）。
    //     真正的重开由委托 `OnInventoryToggled(open=1)` 直接驱动，不受本冷却限制。
    float LastHideAt = -1000.f;

    // ★★★2026-11-05【第十八次修 v2 —— 1 秒延迟的真正机理，仿真证实，别再猜】
    //   `bWantClose` 只能活 1.5 秒：委托 open=0 置位时记下时刻，超时自动作废。
    //   【为什么不能再用轮询去清它】（第十四次修写的 `if (bProbeFresh) bWantClose = false;`）
    //     关闭瞬间原版背包要播 0.38s 的滑出动画，这段时间它**确实还 IsVisible()** ——
    //     那是真值，不是误报。用"新鲜采样说可见"去清 bWantClose，等于把最可靠的关闭信号丢了。
    //     丢掉之后只能等 CloseWaitAcc 从"背包彻底不可见"起累计 0.6s ⇒ 总延迟约 1 秒。
    //     仿真复现：旧逻辑 HidePanel 落在 3.984s（关背包发生在 3.0s）⇒ 0.98s，与用户体感一致。
    //   【为什么还要保留 1.5s 超时】万一 open=0 是误发（ESC/暂停菜单等），
    //     没有超时就会一直挂着，下次进关闭分支时立刻误收面板。
    float WantCloseAt = -1000.f;

    // ★★★2026-11-05【第二十次修「打开时面板发飘 / 跟不上物品栏」】
    //   面板永不摘除后，关闭时它停在上次的位置（例如 216），但重新打开时物品栏
    //   又从下方 226 开始播上浮动画 ⇒ 目标变成 226，面板需要**往下走 10px** 去追，
    //   追的过程是 0.2s 过渡，而物品栏的上浮只有 0.22s —— 两条曲线时间尺度接近、方向相反，
    //   叠在一起就是"面板飘一下才归位"。
    //   ★数据佐证：23 次开关里 `layout WRITE` 只有 **1** 条（首次），后面 22 次全走了过渡分支，
    //     所以玩家每次看到的都不是"直接就位"，而是"从旧位置挪过去"。
    //   ⇒ 修法：每次打开后的**第一次有效布局**（几何已就绪那次）直接写位置，不走进渡。
    //     这与原版观感一致：面板一出现就在该在的地方，然后跟着物品栏一起上浮。
    bool bFreshOpen = false;

    // ★2026-11-05【第十三次修】"委托说该关了"的标志。
    //   AFGPlayerController::OnToggleInventory(open=0) 置位；TickRefresh ③ 段看到就**立刻**收面板；
    //   背包重新可见时清零。
    //   ★为什么不能直接在回调里 HidePanel（第八次修踩过的坑）：WT_IsInventoryWidgetOpen 有 0.1s 节流，
    //     回调里摘掉后下一拍轮询可能还报"背包开着"⇒ TickRefresh 立刻又 ShowPanel ⇒ 摘了又挂地闪。
    //     ⇒ 只置标志，让 TickRefresh 在同一拍里"先判后动"，时序才自洽。
    //   ★为什么需要它：判关的另一路是轮询兜底，阈值必须慢到 1.0s（躲开 Slate 噪声）。
    //     只有委托这一路能让真关做到零延迟。
    bool  bWantClose = false;

    // ★2026-11-05【第十一次修·下半】棘轮用的"上一次真正写进 Slot 的 Y"。
    //   原先是 LayoutLeftOfInventory 里的函数级 static float，本轮提升为成员，理由有二：
    //     ① 棘轮逻辑要在写 Slot **之前**读它，而函数内 static 只能在其声明点之后使用；
    //     ② static 会跨实例共享 —— 换世界/重建面板时会读到上一个实例的值，
    //        导致新面板第一帧被误钳成旧位置。
    //   哨兵值 -99999：表示"还没有任何历史"，此时棘轮放行（首帧必须允许自由摆放）。
    // ★★2026-11-05【第十二次修 —— 语义收紧，必须按新语义理解】
    //   现在它不仅"记录写过什么"，而是**Slot 里此刻的 Y**（= 观众看到的位置）。三条铁律：
    //     ① 直接写 Slot 的分支：写多少记多少；
    //     ② 交给 TickFollow 做过渡的分支：过渡期间**逐帧**同步成当前插值，绝不能在起点就记成终点；
    //     ③ 几何没就绪时**不记**（此时 LayoutY 是回退值 0/居中，不是玩家看到的位置）。
    //   ★为什么差一帧就会出事：吸附判据（YSnapEps）和棘轮都靠它判断"现在在哪"。
    //     账本一旦领先于画面，下一次布局就会认为"已经差得很多、需要动"或"不该动"，
    //     两种都会重新制造跳变 —— 等于把本轮修的这个 bug 从另一个入口放回来。
    float LastWrittenPanelY = -99999.f;

    // ★2026-11-04【第六次修·日志实测定案】原版背包打开时**自己会从下往上浮约 50px**。
    //   实测（1080p，40 帧逐帧采样）：
    //     f1  absY=347.3  →  f10 324.6  →  f20 299.8  →  f23 298.1（停住，之后 17 帧不动）
    //   总位移 49.2px，用时约 0.22s，曲线是 EaseOut（前快后慢）。
    //   这是**原版自己**的运动，不是我们该模拟的东西。
    //   ⇒ 正确做法：每帧读原版背包的绝对 Y，本面板跟着走。完全不用自己插值，
    //     自然与原版同步，也不会出现方向/幅度/时长的任何偏差。
    float FollowT = 0.f;            // 打开后的跟随计时（前 0.6s 每帧重算布局，之后恢复节流）
    bool  bFollowWindowStarted = false;  // ★跟随窗口本次打开是否已启动（防 ShowPanel 反复重置计时）

    // ★2026-11-04 本轮是否已经播过入场动画。ShowPanel 若因几何未稳而跳过 StartSlideIn，
    //   由 TickRefresh 在布局稳定后补播一次；这个标志保证"只补播一次、绝不会每 0.5s 滑一下"。
    //   HidePanel（收起面板）时复位，下次打开重新播。
    bool  bSlidePlayed = false;

    // ★2026-11-04 ticker 改每帧后：布局/内容刷新按 0.5s 节流（动画仍逐帧），避免逐帧扫 widget 掉帧。
    //   （LayoutLeftOfInventory 要枚举全部 UserWidget 算坐标，RefreshContents 要重建行，都是重活。）
    float RefreshAccum = 0.f;

    // ★2026-11-04 暂停菜单（ESC）不再误收面板：
    //   bPauseFrozen = 暂停菜单开着期间冻结状态判定（暂停时原版背包 widget 仍在但不可见，
    //     旧逻辑会误判「背包关了」→ HidePanel，回来后挂不回去 ⇒ 用户看到「按 ESC 面板就没了」）。
    //     冻结期间只推进动画 tick，不采样、不 Show/Hide；退出暂停后先清标记再恢复采样。
    //   CloseTicks = 「判关」连续采样计数，连续 2 次才真收，防止可见性单帧抖动导致面板消失。
    //     （开背包时清零，避免旧计数把刚挂上的面板立刻收掉。）
    bool  bPauseFrozen = false;
    int32 CloseTicks = 0;

    // ★2026-11-04【面板彻底不出现】自愈链（用户实测"进游戏啥也没有"，日志里只有 RegisterWorld 第一条）：
    //   三个已知失败点，每个都必须有兜底，否则任意一个卡住 = 面板永不出现且无任何日志：
    //     f1 CreateWidget<URemote_FactoryWarehousePanel>(PC) 返回 null —— PlayerController 在 POST_INITIALIZATION
    //        阶段虽然非 null，但可能还没进 world、或是 PreLogin 阶段的过渡控制器。旧版这里静默丢，
    //        重试 ticker 会一直重试到 60s 后放弃 ⇒ 玩家永远看不到面板。现在失败会打日志（见 EnsurePanelCreated）。
    //     f2 重试 ticker 的 lambda 旧版裸捕获 World 指针，读档/切换宇宙后 World 被销毁而 lambda 仍持有它
    //        ⇒ 悬空指针，最坏表现是静默中断且无日志。已改捕获 TWeakObjectPtr<UWorld>。
    //     f3 ticker 起不来/中途被注销（AddTicker 无效）⇒ 永远不再尝试创建。故除 RegisterWorld 的一次性
    //        重试 ticker 之外，再加一条【全局自愈 ticker】（见 .cpp 的 self-heal），它挂在 FTSTicker 核心
    //        上、不依赖任何 UObject 生命期，每 0.25s 检查一次，建出来即自我注销。
    //   两个 ticker 的创建都走 EnsurePanelCreated，故只要游戏进程在跑、PlayerController 最终出现，
    //   面板就一定建得出来。
};
