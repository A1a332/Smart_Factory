#pragma once

#include "Containers/Ticker.h"
#include "Equipment/FGObjectScanner.h"  // ★0.2.104 父类改 AFGObjectScanner（自带 FGEquipment.h）
#include "Resources/FGEquipmentDescriptor.h"   // NOT Equipment/... — the file lives under Resources
#include "FGRecipe.h"
#include "Remote_FactoryEquipment.generated.h"

class URemote_FactoryPanelWidget;
class USceneComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;

/**
 * Handheld Remote Factory -- form 2 of the mod.
 *
 * The fixed terminal (ARemote_FactoryBuildable) is a placeable console you walk up to and press
 * E on. This one is the same control panel in your pocket: craft it, put it in a hand slot, and
 * the panel opens the moment you take it out.
 *
 * ## Why AFGObjectScanner and not plain AFGEquipment (★0.2.104)
 *
 * Form 2 started on AFGEquipment, and 0.2.85~0.2.103 proved the 1P arm raise is gated on the
 * equipment's C++ identity, not on mArmAnimation: the vanilla scanner BP ships mArmAnimation
 * = AE_None (DumpedObjects/Equip_ObjectScanner_C_2147473355.txt:60) yet still raises the 1P
 * arm, while ours fed armSlot=10 + aipose=ObjectScannerIdle_01 every frame and the arm never
 * moved. The only lever left is the class itself: AFGObjectScanner is what UFGAnimPlayer1p
 * casts to. We inherit it and neuter the scan side effects (Tick scan/beep loop, scanner menu
 * and cycle inputs, scanner BeginPlay setup, the inherited mScannerSkeletalMesh) while keeping
 * every AFGEquipment behaviour we relied on -- see the explicit AFGEquipment:: calls in the .cpp.
 *
 * ## Why the panel opens from Equip()/UnEquip()
 *
 * AFGEquipment has no "press a key to open my UI" hook that is cheap to reach from a mod: the clean
 * route is the mDefaultEquipmentActions bitmask plus an Enhanced Input mapping context, and both
 * want authored UInputAction assets this mod deliberately does not ship (the mod builds every widget
 * in C++ and carries no .uasset of its own). Equip()/UnEquip() are public virtual on AFGEquipment and
 * fire exactly at the right moments -- the panel appearing as the terminal comes out of the holster
 * is also the more obvious behaviour for a player, so this is a design win and not just a shortcut.
 *
 * ## Networking boundary (★ read before touching this file)
 *
 * Equipment is a CLIENT-local object: it exists on the machine of the player holding it, and the
 * server has no interest in it. Every actual factory action (scan / open machine / pull items) is a
 * server RPC that already lives on URemote_FactoryRCO.
 *
 * So this class does exactly one thing: open and close a widget. It reads no game state, writes no
 * game state, and never talks to the server. That keeps the split clean -- the equipment is a new
 * entrance to the existing panel, and all the business logic stays where it already worked.
 */
UCLASS()
class REMOTE_FACTORY_API ARemote_FactoryEquipment : public AFGObjectScanner
{
    GENERATED_BODY()

public:
    ARemote_FactoryEquipment();

    /** ★0.2.104 跳过 AFGObjectScanner::BeginPlay（屏材质渲染目标/扫描计时器/默认扫描物），
     *  只走 AFGEquipment 初始化；顺带压掉继承来的 mScannerSkeletalMesh（防与 HandMesh 重复）。 */
    virtual void BeginPlay() override;

    /** ★0.2.104 跳过 AFGObjectScanner 的输入绑定（扫描菜单/左右循环），只保留基类默认绑定。 */
    virtual void AddEquipmentActionBindings() override;

    /** Equip the terminal and immediately show the control panel. */
    virtual void Equip(class AFGCharacterPlayer* Character) override;

    /** Put the terminal away and close the panel with it. */
    virtual void UnEquip() override;

    /** ★v0.2.72 切 1P/3P 时把手持件重挂到新可见网格（修复"手持无建模"）。 */
    virtual void OnPostCameraModeChanged_Implementation(ECameraMode newCameraMode) override;

    /** ★v0.2.72 把整只装备 actor 重挂到当前可见网格（1P/3P 切换时调用，单一来源）。 */
    void ReattachHandheldToView();
    /** ★v0.2.72 相机模式看门狗：轮询 IsFirstPerson，变了就重挂（兜底钩子不触发的情况）。 */
    void StartCameraWatchdog();
    /** ★v0.2.72 停止相机模式看门狗。 */
    void StopCameraWatchdog();

    /** ★v0.2.83 静默可见性守护：原生 UpdateEquipmentVisibility 只认 actor 级 bHidden +
     *  专属 mScannerSkeletalMesh 组件（见 DumpedObjects/Equip_ObjectScanner_C：actor bHidden=true，
     *  可见网格是 mScannerSkeletalMesh 而非 actor 通用网格）。我们的自定义 WTHandMesh 原生完全
     *  不认识，会被原生通过 actor 级隐藏盖成不可见——这是 0.2.80~0.2.82「建模彻底没了」的根因
     *  （0.2.82 只点了 HandMesh 可见，没顶住 actor 级隐藏）。故每 0.3s 静默把 actor + HandMesh
     *  拉回可见（0.2.79 同款可见性口径，用户已认可；本版去掉 [vigil] 刷屏，且孤儿已由原生
     *  Super::UnEquip 根除，不再需要自愈重挂父链）。无日志，不打扰玩家。 */
    void StartVisibilityWatchdog();
    void StopVisibilityWatchdog();

    /** ★0.2.142 终端屏幕显示 Smart Factory logo（白底）：把 HandMesh 的屏幕材质槽（槽名含
     *  "Screen"，兜底索引 2，DumpedObjects 实锤 OverrideMaterials[2]=ScreenMaterialInstance）
     *  克隆为 MID，其全部纹理参数指向运行时从 mod 目录加载的 Content/ScreenLogo.png。
     *  ★必须在 HandMesh 网格就绪后调用（Equip 赋网格之后 / vigil 材质重置之后）；BeginPlay 时
     *  实例 HandMesh 还没网格（GetNumMaterials()==0，0.2.142 首版教训），不能在 BeginPlay 调。 */
    void ApplyHandheldScreenLogo();

    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    /** ★修复 ①（2026-10-18 接手）：每帧把"相机跟随锚点"摆到玩家相机前，手持件随之恒在视野内。
     *  绕开原 socket 挂接在 1P 下被重定位到 94m 外的根因。仅第一人称需要更新。 */
    virtual void Tick(float DeltaSeconds) override;

    /** Human readable label, shared with the descriptor and the milestone. */
    static FText GetEquipmentDisplayName();

    /**
     * ★v0.2.10 运行时加载原版 SK_ObjectScanner_01 骨骼网格并写入 CDO 的 HandMesh 组件模板。
     * 内容挂载后调用（RegisterHandheld 内）；多候选路径 + 日志，全部失败则手上无模型（不崩）。
     * ★v0.2.11 起改为整包拷贝原版 Equip_ObjectScanner_C 的组件配置（网格+动画+相对变换+挂点）。
     */
    static void ApplyHandheldHandMesh();

protected:
    /** Create + add the control panel to the viewport, if it is not already up. */
    void OpenPanel();

    /** Remove the control panel from the viewport and drop the reference. */
    void ClosePanel();

    /**
     * ★v0.2.15 面板看门狗（0.5s 周期 ticker，面板关闭即自停）：
     * ① 发现暂停菜单开着 → 立即 ClosePanel()（暂停菜单永远可点，面板永不卡死在底下）；
     * ② GameUI 就绪且面板未注册 → 注册进交互栈（ESC 替补关生效）。
     * 开局早段 GetGameUI() 为 null（0.2.12/0.2.13 日志实锤），所以靠 ticker 等它就绪。
     */
    void StartPanelWatchdog();
    void StopPanelWatchdog();

    /** ★v0.2.14 开面板时立即尝试注册一次（GameUI 已就绪就不用等看门狗）。 */
    void TryRegisterWithGameUI();

    /** ★v0.2.15 面板上 ✕ 关闭按钮的回调 → 走 ClosePanel() 完整收尾。 */
    UFUNCTION()
    void HandlePanelCloseRequested();

    /**
     * ★v0.2.12 右键开面板：运行时自建 UInputAction+UInputMappingContext（右键），
     * 装备时应用 IMC 并绑定，卸下时解绑。ESC 关闭走 FGGameUI 交互栈（OpenPanel 里注册）。
     */
    void ApplyToggleInput(class AFGCharacterPlayer* Character);
    void RemoveToggleInput();
    void HandleToggleInput(const struct FInputActionValue& Value);

    /** Handle to the currently open panel. Cleared in ClosePanel. */
    UPROPERTY(Transient)
    TObjectPtr<URemote_FactoryPanelWidget> PanelWidget;

    /** ★v0.2.51 自有输入组件：装备时 NewObject + BindAction + PushInputComponent 推上玩家
     *  控制器输入栈，卸下时 PopInputComponent 摘除（引擎同时清绑定值），随后置空强引用让
     *  旧组件可被 GC（栈内是弱引用，靠本成员保活）。从机制上根除两类病灶（0.2.50 实测）：
     *  ① 绑定跨装备累积（旧实现从不解绑，一按右键触发全部历史绑定 → 面板闪烁后净「关」，
     *     日志实锤：6 次装备后一按 6 连触发）；
     *  ② 绑定落到 PC->InputComponent 这个随装备/建造枪推栈漂移的临时组件上，栈一弹绑定即死
     *     （日志实锤：09:49:18 后新装备重新绑定也零触发）。 */
    UPROPERTY(Transient)
    TObjectPtr<class UEnhancedInputComponent> ToggleInputComponent;

    /** ★v0.2.12 EnhancedInput 绑定记账（0=未绑），同一装备周期内防止重复绑定。 */
    uint32 ToggleBindHandle = 0;

    /** ★v0.2.51 绑定/Push 时的玩家控制器（弱引用；Pop 时若 Instigator 已清则用它兜底）。 */
    TWeakObjectPtr<class APlayerController> ToggleBindPC;

    /** ★v0.2.14 面板是否已注册进 FGGameUI 交互栈（含重试状态）。 */
    bool bRegisteredWithGameUI = false;

    /** ★v0.2.15 面板看门狗 ticker 句柄（面板关闭时 RemoveTicker 主动停表）。 */
    FTSTicker::FDelegateHandle PanelWatchdogHandle;

    /** ★v0.2.72 相机模式看门狗 ticker 句柄（切 1P/3P 重挂手持的兜底，不依赖 OnPostCameraModeChanged 钩子）。 */
    FTSTicker::FDelegateHandle CameraWatchdogHandle;
    /** ★v0.2.83 静默可见性守护 ticker 句柄（Equip 起、UnEquip/EndPlay 摘，且随 actor 销毁自停）。 */
    FTSTicker::FDelegateHandle VisibilityWatchdogHandle;
    /** ★v0.2.72 上次挂接时的第一人称状态，看门狗据此判变。 */
    bool bLastAttachedFirstPerson = false;

    /**
     * ★v0.2.10 手持模型：运行时从原版 1.0 物品扫描仪拷来的 SK_ObjectScanner_01。
     * 组件本身在 ctor 里创建（★v0.2.21 起是根 SceneComponent 的子，与原版装备结构一致），
     * 网格资产在 RegisterHandheld（内容挂载后）由 ApplyHandheldHandMesh() 写进 CDO 组件模板
     * ——生成的实例自动继承。
     * 手臂姿势：mArmAnimation = AE_ObjectScanner（0.2.85 起；0.2.67~0.2.84 的 AE_None 是对"实例
     * 转储"的误判，导致 3P 角色空手垂臂、设备掉到胯部——AE_ObjectScanner 才是原版扫描仪的
     * 双手举持姿势，经 UFGAnimPlayer::CacheArmSlotData 驱动 1P/3P）。挂接 socket 运行时探测。
     */
    UPROPERTY(Transient)
    TObjectPtr<USkeletalMeshComponent> HandMesh;

    /**
     * ★修复 ①（2026-10-18 接手）：相机跟随锚点。原方案把装备挂到角色网格 hand socket，
     * 而 1P 下该 socket 世界坐标被重定位到距相机 ~94m 处（装备漂在世界里 → "手持无建模"）。
     * 改用此锚点：每帧用**公开**的 GetCameraComponentWorldLocation/ForwardVector 把锚点摆到
     * 相机前，HandMesh 作为其子组件，相对变换取 GWT_HandViewModel*，恒在视野内、第一人称即在手上。
     */
    UPROPERTY(Transient)
    TObjectPtr<USceneComponent> ViewModelAnchor;

    /** 把 ViewModelAnchor 摆到正确位置（1P=手部 socket+相机距离守卫，失真回落相机前方持握位 / 3P=角色手部 socket）。Tick 每帧调用。 */
    void UpdateViewModelTransform();

    /**
     * ★0.2.87 3P 举持姿势诊断+兜底：读取 3P 网格动画实例（UFGAnimPlayer）的 mArmSlotType，
     * 首次把「槽位装备/动画缓存」实况写日志；若槽位确为本装备而缓存未跟上（!= AE_ObjectScanner）
     * 则每帧直写修正。用于定位「3P 手臂不举」到底是注册缺失还是缓存失效。
     */
    void EnforceArmPoseCache(class AFGCharacterPlayer* Character);

    /** ★0.2.87 首次姿势诊断日志是否已打（每个装备实例一次）。 */
    UPROPERTY(Transient)
    bool bArmPoseDiagLogged = false;
    /** ★0.2.93 语义变更：动态蒙太奇**创建失败**时置位（放弃每帧重试防刷日志）；正常播放不再用它门控
     *  （0.2.93 起 idle 播放由 IsSlotActive 状态驱动，飞行/切视角丢姿势自动重播）。UnEquip 复位。 */
    bool bIdle3pMontagePlayed = false;
    /** ★0.2.95 1P 手臂网格举持蒙太奇创建失败的放弃标记（与 3P 独立，防 1P 每帧重建刷日志）。UnEquip 复位。 */
    bool bIdle1pMontageFailed = false;
    /** ★0.2.90 实例侧 idle3p 为 null 的一次性诊断标记（避免每帧刷日志），UnEquip 复位。 */
    bool bIdle3pNullLogged = false;
    /** ★0.2.91 1P 锚点一次性定位诊断是否已打（每个装备实例一次）：记录所选网格/socket 距离/落点路径。 */
    bool bFirstPersonDiagLogged = false;
    /** ★0.2.96 1P 锚点上一帧模式（-1 未初始化 / 0 socket / 1 camera-fallback）：模式翻转时补打诊断，消除静默盲区。 */
    int8 bLast1pMode = -1;
    /** ★0.2.97 举持蒙太奇重播冷却（FPlatformTime::Seconds() 墙钟秒）。0.2.96 实测 1P 下 slot 每 ~114ms
     *  被原生侧清一次，IsSlotActive 纯状态门控退化为「每 114ms 重建蒙太奇」→ 手臂姿势在举起/休息间抖、
     *  sockFrontDot 在 0.9/-0.03 间闪、设备路径 socket/fallback 每帧互换（用户截图「卡手上/飘」交替）。
     *  现加冷却：被清后最多 0.75s 恢复，不再每帧重建。1P/3P 共用（切视角重播本就预期，无碍）。 */
    double LastPoseMontagePlaySec = -1e9;
    /** ★0.2.92 看门狗重建 HandMesh 渲染代理的一次性日志标记（1P 不可见根因=代理未建，MarkRenderStateDirty 无效）。 */
    bool b1pProxyFixLogged = false;
    /** ★0.2.102 实例 mIdlePoseAnimation 补写的一次性日志标记：0.2.91 实锤首装备 ctor 早于资产加载，
     *  实例姿势永久 null 且从无补写；补写成功后 native 每帧读 GetIdlePoseAnimation() 即非空。 */
    bool bIdle1pInstPoseEnsured = false;

    /**
     * ★v0.2.21 诊断探针组件：与 HandMesh 挂同一个 socket 的一个静态网格实体。
     * 用途：二分"手上无建模"的病灶——探针可见而扫描仪网格不可见 ⇒ 挂接链路是好的，
     * 问题在 SK_ObjectScanner_01/动画/材质；两者都不可见 ⇒ 挂接链路本身坏了。
     * 诊断完成后把 Remote_FactoryEquipment.cpp 里的 kDebugHandMarker 改 false 即彻底关闭
     * （组件仍在但永不显示）。
     */
    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> HandMarker;
};

/**
 * Item descriptor for the handheld terminal.
 *
 * This is the piece the inventory, the crafting station output slot and the hotbar all reference --
 * the equipment actor above is spawned from it when the player equips the item. UFGEquipmentDescriptor
 * carries exactly one link we care about (mEquipmentClass); everything else comes from UFGItemDescriptor.
 */
UCLASS()
class REMOTE_FACTORY_API URemote_FactoryEquipDescriptor : public UFGEquipmentDescriptor
{
    GENERATED_BODY()

public:
    URemote_FactoryEquipDescriptor();

    virtual FText GetItemNameInternal() const override;
    virtual FText GetItemDescriptionInternal() const override;

protected:
    /** ★ 保持 UPROPERTY：与 URemote_FactoryDescriptor 同一套模式。FText 本身不受 GC 追踪，
     *  但统一标注避免以后往这里加 TSubclassOf / TObjectPtr 成员时漏掉反射。 */
    UPROPERTY()
    FText DisplayName;
    UPROPERTY()
    FText Desc;
};

/**
 * Crafting recipe for the handheld terminal.
 *
 * ★ Produced in the WORKBENCH, not the build gun -- that is the whole difference between this recipe
 * and URemote_FactoryRecipe. Equipment is an item; the build gun only makes buildings. The vanilla
 * Recipe_ObjectScanner uses /Game/FactoryGame/Buildable/-Shared/WorkBench/BP_WorkshopComponent as its
 * mProducedIn target, and that is the class this recipe points at too.
 *
 * Like the buildable recipe, mProducedIn cannot be resolved from a constructor: the workbench class
 * only exists once the game content is mounted, so it is applied from ApplyProducedIn() during
 * registration at POST_INITIALIZATION.
 */
UCLASS()
class REMOTE_FACTORY_API URemote_FactoryEquipRecipe : public UFGRecipe
{
    GENERATED_BODY()

public:
    URemote_FactoryEquipRecipe();

    /** Point mProducedIn at the resolved workbench class. Idempotent. */
    void ApplyProducedIn(UClass* WorkBenchClass);

    /** Resolve /Game/... item paths into mIngredients so the recipe is not free. */
    void ApplyIngredients();
};

namespace Remote_Factory
{
    /**
     * Resolve mProducedIn + mIngredients on the handheld recipe. Needs the game content mounted;
     * call at POST_INITIALIZATION alongside RegisterBuildables().
     */
    void RegisterHandheld();
}
