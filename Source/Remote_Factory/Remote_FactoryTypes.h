#pragma once

#include "CoreMinimal.h"
#include "Remote_FactoryLoc.h"
// ★0.2.153m 图标字段需要 UTexture2D 完整类型（TSoftObjectPtr<UTexture2D> 要能取路径）；
// Actor.h 是 Target 放宽为 AActor 所需（载具不是 AFGBuildable）。
// ⚠UHT 硬性规定：.generated.h 必须是头文件里最后一个 #include，新增 include 一律放它之前。
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "Remote_FactoryTypes.generated.h"

class AFGBuildable;

/**
 * Category a machine is filed under in the terminal's left-hand list.
 *
 * The terminal scrolls the world once per refresh, classifies every buildable it finds into one
 * of these buckets, and shows the counts per bucket. Kept as a plain enum so the client and the
 * server agree on the index without shipping any UMG/animation assets.
 */
UENUM()
enum class EWT_MachineCategory : uint8
{
    /** AFGBuildableManufacturer & friends: constructor / assembler / smelter / foundry / refinery. */
    Producer = 0,

    /** ★本轮 容器：AFGBuildableStorage（箱子 / 工业储存容器 / 中央仓库）+ AFGBuildablePipeReservoir
     * （液体 / 气体储罐）。0.2.153m 曾按用户要求拆成液体 / 固体两类，本轮合并回此类
     * （值 1；原 LiquidContainer=16 / SolidContainer=17 废弃不再使用）。 */
    Container = 1,

    /** AFGBuildableResourceExtractorBase: 采矿机 / 抽水机 / 采油机. */
    Miner = 2,

    /** AFGBuildableGenerator: 生物质 / 煤 / 燃油 / 核电 / 地热. */
    Generator = 3,

    /**
     * AFGBuildableCircuitSwitch: 电源开关 / 优先级电源开关.
     *
     * Form 3's "A" feature. A switch is neither a generator nor a consumer -- it BRIDGES two
     * circuits -- so it gets its own bucket instead of hiding inside 发电机. That also gives the
     * detail pane a place to hang the on/off + priority controls without them appearing on a
     * machine that has neither.
     */
    PowerSwitch = 4,

    /** AFGBuildableResourceSink: AWESOME 资源回收站. */
    AwesomeSink = 5,

    /** AFGBuildableHubTerminal: 枢纽. */
    Hub = 6,

    /** AFGBuildableSpaceElevator: 太空电梯. */
    SpaceElevator = 7,

    /** AFGBuildableAutomatedWorkBench: 装备工作台. ★0.2.153l 类目下架（用户要求）：它派生自 Manufacturer，已并入 Production；枚举值 8 保留不复用。⚠本文件含 USTRUCT/GENERATED_BODY，改注释不得增减行数（UHT 宏按行号编号，UBT 坏时无法重生成）。 */
    WorkBench = 8,

    /** 分子分析机 MAM. */
    MAM = 9,

    /** 空投舱 / 坠机点 (AFGDropPod). */
    DropPod = 10,

    /** Anything else that still exposes an interact widget. */
    Other = 11,

    /**
     * ★0.2.146 任意第三方 MOD 建筑（WT_IsModdedBuildableClass）。
     *
     * ficsit 上任何 MOD 的建筑只要玩家加载了就该被识别：派生自引擎基类（Manufacturer/Storage/...）
     * 的早已落入上面各类目，但派生自通用 AFGBuildable 的（无线电力塔/中继器等自定义建筑）此前
     * 一律落进 Other 而被列表丢弃（92k 原版传送带/地基防洪闸）。Modded 单独成桶：
     * 面板按 MOD 友好名分子组展示（Entry.ModName），原版类绝不进此桶。
     */
    Modded = 12,

    /**
     * ★0.2.146 AWESOME 商店（AFGBuildableResourceSinkShop，FGBuildableResourceSinkShop.h:14，
     * 派生 AFGBuildableFactory 而非 Manufacturer/Sink → 此前落 Other 被丢）。
     */
    Shop = 13,

    /**
     * ★0.2.146 制作台（Build_WorkBench，编辑器探针实证父类=裸 /Script/FactoryGame.FGBuildable，
     * 纯 BP 无原生 Buildable 基类 → 按 BP 包路径 /Buildable/Factory/WorkBench/Build_WorkBench. 匹配）。
     */
    CraftBench = 14,

    /** ★0.2.146 装备工坊（Build_Workshop，同上纯 BP，路径 /Buildable/Factory/Workshop/Build_Workshop.）。 */
    EquipWorkshop = 15,

    // ★本轮 液体 / 固体容器已合并回 Container=1（见上方注释）；原 LiquidContainer=16 /
    // SolidContainer=17 枚举值废弃不再使用，切勿再引用（否则分类静默落入 Other 被列表丢弃）。



    /**
     * ★0.2.153m 运输工具。AFGVehicle 及其派生：轮式载具（拖拉机/卡车/探险车，FGVehicle.h:126）、
     * 火车机车与货车厢（AFGRailroadVehicle : AFGVehicle，FGRailroadVehicle.h:36）、
     * 无人机（AFGDroneVehicle : AFGVehicle，FGDroneVehicle.h:121）。
     * 载具**不是** AFGBuildable，不在 AFGBuildableSubsystem 里 ⇒ 扫描必须另跑一遍世界 actor 迭代。
     */
    Vehicle = 18,

    /**
     * ★0.2.153m 外星传送门（AFGBuildablePortalBase：传送门 Build_Portal / 传送门卫星
     * Build_PortalSatellite，FGBuildablePortalBase.h:74）。父类是 AFGBuildableFactory，
     * 不与 Manufacturer 冲突，但仍放在 Manufacturer 判定之前以防未来父类调整。
     */
    Portal = 19,

    /**
     * ★0.2.153z 车站（AFGBuildableRailroadStation 铁路车站 / AFGBuildableDockingStation 车辆停靠站）。
     * 用户要求「车站类显示装卸货状态+物品」：车辆停靠站有 GetIsInLoadMode() 装载/卸载模式 + GetInventory()
     * 实际货品（FGBuildableDockingStation.h:110/134，已一手核实）；铁路车站货物在车厢（不在车站库存），
     * 状态显示真实站名（AFGTrainStationIdentifier::GetStationName，FGTrainStationIdentifier.h:88）+「铁路车站」。
     */
    Station = 20,

    /**
     * ★本轮 资源节点（原版矿点 + 模组矿点）提成【建筑】上方的独立顶层类目，组内再分两个子目：
     *   ResourceNode_Vanilla = 原版矿点；ResourceNode_Mod = 模组矿点。
     * 不是建筑（AFGResourceNode 不派生 AFGBuildable），不进实时扫描；数据来自
     * SmartFactoryLib/resourcenodes.json（wt.DumpResourceNodes 生成），面板在收到扫描回包后并入。
     * 模组矿点只要派生自 AFGResourceNode 即归入 ResourceNode_Mod，绝不进 MOD建筑
     * （MOD建筑只在 AFGBuildable 循环判定）。传送走坐标（Target==nullptr）。
     */
    ResourceNode_Vanilla = 21,   // ★本轮 资源节点·原版矿点（建筑上方独立顶层类目）
    ResourceNode_Mod = 22,       // ★本轮 资源节点·模组矿点

    /**
     * ★1.0.9 电网（用户定案：「新增一个电网类目，打开这个类目直接看到原版那块电力曲线」）。
     *
     * 一类 = 一个 UFGPowerCircuit（世界里可能有多条独立电网），**不是**一根电线杆：
     * 玩家要的是「我这张图现在峰值多少、上限多少」，而不是逐杆去看同一个数字。
     * 电线杆在这里只是聚合与定位的把手（按 GetPowerCircuit() 归并、拿最近一根的位置）。
     *
     * 内容形态与其它类目完全不同（曲线图 + 图例 + 电路行），所以面板走独立渲染路径
     * RebuildGridView()，数据来自 FWT_MachineListReply::Grids 而不是 Entries。
     *
     * 为什么不用其它类目顶替：其它类目的行必须对应一个可传送的 AActor，
     * 而「电网」是 UObject（UFGPowerCircuit），没有 actor 可言。
     */
    PowerGrid = 23,              // ★1.0.9 电网（电力曲线 + 五项图例）

    Count = 24
};

// ★本轮 枢纽建筑子类型（顶部快捷图标栏用）：区分 Hub 类目下的 6 种具体建筑 + 回收站。
// 存为 uint8（FWT_MachineEntry::HubKind），无需 UENUM 反射；扫描侧按类精确赋值。
enum class EWT_HubKind : uint8
{
    Unknown = 0,
    HubTerminal,       // 枢纽终端
    HubWorkbench,      // 枢纽制作台
    SpaceElevator,     // 太空电梯
    MAM,               // 分子分析仪
    EquipmentWorkshop, // 装备工坊
    AwesomeShop,       // AWESOME商店
    ResourceSink,      // 回收站（共享 Hub 桶，挪到顶部栏避免无处安放）
    Count
};

/**
 * ★1.0.11 行级「问题」标签（用户方案稿里的【堵塞】【缺料】【未接入电网】【未连接物流】
 * 【已超频】【燃料不足】【已满】【已空】这些开关）。
 *
 * 为什么要位掩码而不是一堆 bool：一条真实建筑常常同时有多个问题（堵塞 + 没接电），
 * 玩家要能一次筛出「所有有问题的」，也要能只看某一种。位掩码让「任一问题」这条档位
 * 退化成 `Flags != 0`，不必为它在扫描侧多写一遍判定。
 *
 * 存 uint8（FWT_MachineEntry::IssueFlags），不单独做 UENUM 反射字段 —— 位运算不需要 UHT。
 */
enum class EWT_Issue : uint8
{
    OutputBlocked = 0,   // 输出堵塞（生产状态 IS_ERROR：下游满了 / 无处放产物）
    InputStarved  = 1,   // 输入缺料（生产状态 IS_STANDBY：上游没货 / 没接上）
    NoPower       = 2,   // 未接入电网（建筑有 UFGPowerInfoComponent 但 IsConnected() == false）
    NoLogistics   = 3,   // 未连接物流（有传送带/管道接口但一个都没接上）
    Overclocked   = 4,   // 已超频（GetPendingPotential() > 1.0）
    FuelStarved   = 5,   // 燃料不足（发电机当前没有可用燃料类）
    Full          = 6,   // 已满（容器填充率 ≥ 99%）
    Empty         = 7,   // 已空（容器填充率 == 0）
};

/** 某一位的掩码值。写 `WT_IssueBit(EWT_Issue::NoPower)` 比 `1u << 2` 更难写错。 */
constexpr uint8 WT_IssueBit(EWT_Issue I)
{
    return static_cast<uint8>(1u << static_cast<uint8>(I));
}

/**
 * Collectibles that can be routed to from the terminal: the alien-energy pickups AND the
 * foragable flora (scattered food ingredients).
 *
 * They are deliberately NOT part of EWT_MachineCategory: those categories describe BUILDINGS
 * (things with an interact panel), while these are world pickups the player walks into. Keeping
 * them in their own enum keeps the two lists — and their two scan passes — from tangling.
 *
 * Kinds 0..5 are the four alien-energy categories (Power Slugs split into three colours).
 * Kinds 6..8 are the foragable flora harvested from bushes / plants on the map
 * (Beryl Nut, Pale Berry, Bacon Agaric). The data for every kind lives in collectibles.json,
 * shipped inside the SmartFactoryLib dependency mod; the panel reads counts and coordinates
 * straight from that file and sorts nearest-first, then by map area (region).
 */
UENUM()
enum class EWT_CollectibleKind : uint8
{
    /** 蓝能量蛞蝓 (Blue Power Slug). */
    BlueSlug = 0,

    /** 黄能量蛞蝓 (Yellow Power Slug). */
    YellowSlug = 1,

    /** 紫能量蛞蝓 (Purple Power Slug). */
    PurpleSlug = 2,

    /** 梅瑟尔球 (Mercer Sphere). */
    MercerSphere = 3,

    /** 索莫晶体 / 索莫斯鲁普 (Somersloop). */
    Somersloop = 4,

    /** 空投舱坠机点 (Crash Site / Drop Pod). */
    DropPod = 5,

    /** 绿石果 (Beryl Nut — foragable flora). */
    BerylNut = 6,

    /** 古莓 (Pale Berry — foragable flora). */
    PaleBerry = 7,

    /** 培根伞菌 (Bacon Agaric — foragable flora). */
    BaconAgaric = 8,

    Count = 9
};

/**
 * ★1.0.10 「已存在物品」筛选用的物品标签。
 *
 * 用户诉求（2026-10-27）：「放一排，然后下拉选择已存在物品」「每个列表，单独做个已存在筛选」。
 * 即：每个子类目列表顶部的筛选条里多一个下拉，选项不是写死的几个档位，而是**这份列表里
 * 真正出现过的物品**（箱子里装的东西 / 机器在产的东西 / 矿点的矿种），选了就只留含该物品的行。
 *
 * 为什么按类名（Key）当过滤键而不是显示名：
 *   ① 显示名是本地化文本，换语言后玩家存的筛选状态就失效了；
 *   ② mod 物品可能重名，按名字比对会把两个不同物品算成一个。
 * Label 只用于下拉里显示给人看。
 */
USTRUCT()
struct FWT_ItemTag
{
    GENERATED_BODY()

    /** 物品描述符类名（UClass::GetName，跨端稳定、与语言无关）。 */
    UPROPERTY()
    FString Key;

    /** 显示名（UFGItemDescriptor::GetItemName 的结果，已本地化）。 */
    UPROPERTY()
    FString Label;
};

/**
 * One row in the terminal's machine list.
 *
 * Target is a live pointer to the buildable. Unreal replicates UObject pointer parameters by
 * NetGUID and resolves them on the far side, which is exactly how the vanilla game ships an actor
 * list to a client (see FGBuildGunDismantle::Client_NotifyActorsFailedDismantle). There is
 * deliberately no location- or index-based identity here: stacking makes locations ambiguous, and
 * an index would not exist on a dedicated client.
 */
USTRUCT()
struct FWT_MachineEntry
{
    GENERATED_BODY()

    /**
     * ★1.0.10 这一行「实际与哪些物品有关」，供筛选条里的「物品」下拉使用。
     * 填充口径按类目语义（扫描侧 RCO 负责，其它类目留空数组 = 不参与该维筛选）：
     *   容器   = 箱内每一格有货的物品（固体格全部，不是只取首格）/ 液体罐的液体
     *   死亡箱 = 箱内每一格有货的物品
     *   生产   = 当前配方产物（多产物全收）
     *   采矿   = 所连资源节点的资源类
     *   发电机 = 当前燃料
     * 空数组 = 该行没有「物品」概念，物品下拉永远不会把它筛出来（但「全部物品」仍显示它，
     * 与「未选中即不过滤」的既有语义一致）。
     */
    UPROPERTY()
    TArray<FWT_ItemTag> ItemTags;

    /**
     * ★1.0.10 库存总件数（容器 / 车站 / 运输工具 / 死亡箱的货量），供「数量」维度分档。
     * 液体罐按 m³ 折算为件数（原版库存同样以 NumItems 记账）。
     *
     * ★-1 = **不可计量**，不是 0：生产机器 / 矿点 / 电网这类根本没有库存概念的行，
     * 以及液体罐（SDK 只暴露 GetFluidDescriptor，没有量接口，实测 FGBuildablePipeReservoir.h
     * 全文无 GetFluidInventory/GetFluidAmount）都留 -1。用 0 会把它们混进「空」档，
     * 让玩家筛「空箱子」时冒出一堆机器。
     * 数量维度对这些行的语义是「不参与筛选」（无论选哪一档都不出现）。
     *
     * 与 bHasContents 的关系：那是布尔「有没有货」，只能筛两档；这是真实件数，
     * 能筛「快装满了的箱子」这类需求，两者并存（旧维度不动，避免动既有行为）。
     */
    UPROPERTY()
    int32 ContentStackCount = -1;

    /**
     * The machine itself. Travels as a replicated reference, NOT as a serialized pointer value.
     * ★0.2.153m 放宽为 AActor：运输工具（AFGVehicle 派生）不是 AFGBuildable，要进同一张列表。
     * 需要建筑的 RPC（打开面板/取料/开关）在调用点用 Cast<AFGBuildable> 收敛，见 PanelWidget。
     */
    UPROPERTY()
    TObjectPtr<AActor> Target = nullptr;

    /** Human readable label shown in the list ("Assembler", "Storage Container", ...). */
    UPROPERTY()
    FText DisplayName;

    /**
     * ★0.2.153m 列表行图标（生产机器/采矿设备显示对应建筑物品图标）。
     * 用软引用而非硬 UTexture2D*：硬引用走 NetGUID，客户端未加载该贴图资产时会解析失败；
     * 软引用只传路径字符串，由客户端 UImage::SetBrushFromSoftTexture 异步加载。
     * 仅服务端为需要的类目填充，其余恒空（空路径几乎零开销）。
     */
    UPROPERTY()
    TSoftObjectPtr<UTexture2D> Icon;

    /**
     * ★0.2.153p 产出物图标（用户要求：行内「传送」按钮左边显示这台机器正在产什么的图标）。
     * 生产机器 = 当前配方的主要产物（UFGRecipe::GetProducts 第 0 项）；
     * 采矿设备 = 所连资源节点的资源类（AFGResourceNodeBase::GetResourceClass）。
     * 与 Icon 同为软引用（只传路径，客户端异步加载），其余类目恒空、零开销。
     */
    UPROPERTY()
    TSoftObjectPtr<UTexture2D> ProductIcon;

    /**
     * ★本轮 产出物名称（物品/材料名），用于面板搜索。生产机器 = 当前配方主产物描述名；
     * 采矿设备 = 所连资源节点的资源描述名；其余类目恒空。搜「铁板/电缆/铁矿」这类物品/材料名时命中。
     */
    UPROPERTY()
    FText ProductName;

    /** Which bucket this entry belongs to (see EWT_MachineCategory). */
    UPROPERTY()
    uint8 Category = 0;

    /**
     * ★本轮 枢纽建筑子类型（顶部快捷图标栏用）：当 Category == Hub 时，区分 6 种具体枢纽建筑 + 回收站。
     * 扫描侧按 Cast 精确赋值；面板据此把 6 个图标固定展示在搜索框上方，并复用 SelectedEntry 传送/打开。
     */
    UPROPERTY()
    uint8 HubKind = 0;

    /**
     * ★0.2.146 仅 Category == Modded 时有意义：该建筑所属 MOD 的友好名
     * （UModLoadingLibrary::GetLoadedModInfo → FModInfo::FriendlyName，如「WirelessPower-重制版」；
     * 查不到时回退 mod_reference 原串）。面板用它做 MOD 子分组头；其他类目恒为空。
     */
    UPROPERTY()
    FString ModName;

    /** Distance from the terminal, in metres. Used to sort nearest-first. */
    UPROPERTY()
    float DistanceMeters = 0.f;

    /** True if this buildable actually exposes an interact widget we can open remotely. */
    UPROPERTY()
    bool bHasInteractUI = false;

    /**
     * ★0.2.153e 坐标直传（第十轮定案，1.0.0 前完工）：机器世界坐标 + 落点用的水平半宽。
     * 远端机器（客户端网络 relevancy 未复制该 actor）时 Entry->Target 解析为 null，UI 拿不到
     * actor 引用就发不出 Server_TeleportTo；这两个字段让 UI 退化为「按坐标直传」，不依赖 actor
     * 引用，从而不受 relevancy 限制（扫描是服务端遍历全部建筑，坐标永远有）。
     */
    UPROPERTY()
    FVector Location = FVector::ZeroVector;

    UPROPERTY()
    float BaseRadius = 0.f;

    /** Short one-line status for the list ("Running", "Idle", "Iron Plate x12"). */
    UPROPERTY()
    FText StatusLine;

    /**
     * ★1.0.8 生产状态码（列表行彩色指示灯用），与 StatusLine 后半段的状态文字**同源同值**：
     *   255 = 不适用（非 AFGBuildableFactory 类目，如容器/矿点/收集品 —— 不画灯）
     *     0 = 空闲（IS_NONE）
     *     1 = 运行中（IS_PRODUCING / IS_PRODUCING_WITH_CRYSTAL）
     *     2 = 待机（IS_STANDBY）
     *     3 = 阻塞（IS_ERROR）
     *
     * 默认 255 而不是 0：uint8 的 0 是「空闲」，会造成「容器也显示空闲灯」的假状态。
     * 用 255 明确区分「无状态可言」，面板侧据此完全不建灯控件。
     *
     * 为什么单独存码而不让 UI 解析 StatusLine：状态文字是「矿种 · 状态」这种拼接串，
     * 拿它做字符串匹配去找颜色，一旦文案改了灯色就跟着错。这里存原始枚举，
     * 面板侧用 WT_ProdStateColor(Code) 取色、WT_ProdStateText(Code) 取字，
     * 两个函数共用同一份 switch —— 灯色与文字天然一一对应，不可能漂。
     */
    UPROPERTY()
    uint8 ProdState = 255;

    /**
     * ★1.0.8 状态行的「非状态部分」：生产机器 = 当前产物名；采矿设备 = 矿种（·纯度）；
     * 容器 = 箱内首格存货；其余类目留空（此时 StatusLine 就是完整内容）。
     *
     * 为什么要拆出来：用户要求「在状态文字前加彩色指示灯」，而 StatusLine 是把
     * 「铁板 · 运行中」拼成的一整串 —— 灯要插在中间就必须把两段分开。
     * 面板侧渲染规则：
     *    ProdState != 255 ⇒ [名称 距离] [StatusDetail] [灯][状态字]
     *    ProdState == 255 ⇒ [名称 距离] [StatusLine]        (无灯)
     * StatusLine 本身保持原样不动，其它读它的地方（详情面板/搜索）零影响。
     */
    UPROPERTY()
    FText StatusDetail;

    /** ★2026-10-25 纯度筛选键（资源节点类目用）：-1=非矿点/无纯度信息，0=贫瘠，1=普通，2=纯。
     *  与 StatusLine 同源（矿点的 StatusLine 就是纯度文本），供面板按纯度过滤。 */
    UPROPERTY()
    int32 PurityFilter = -1;

    // ---- Form 3 "A": power switch telemetry ------------------------------------
    // Only meaningful when Category == PowerSwitch; left at their defaults otherwise, which costs
    // three bytes per row and saves a second round-trip when the player clicks a switch.

    /** AFGBuildableCircuitSwitch::IsSwitchOn() at scan time. */
    UPROPERTY()
    bool bSwitchOn = false;

    /**
     * AFGBuildableCircuitSwitch::IsSwitchConnected() -- true only when BOTH ends are wired to a
     * circuit. An unwired switch silently does nothing when toggled, so the panel warns instead.
     */
    UPROPERTY()
    bool bSwitchConnected = false;

    /** AFGBuildablePriorityPowerSwitch::GetPriority(), or -1 when this is a plain switch. */
    UPROPERTY()
    int32 SwitchPriority = -1;

    // ---- Form 3 "C": upgrade-building telemetry --------------------------------
    // Populated only for the mod's own power-storage / storage / accelerator buildings.

    /** AFGBuildablePowerStorage::GetPowerStore() / GetPowerStoreCapacity(), in MWh. */
    UPROPERTY()
    float BatteryStoredMWh = 0.f;

    UPROPERTY()
    float BatteryCapacityMWh = 0.f;

    /** AFGBuildableStorage: linear slot count, so the pane can show "48 slots". */
    UPROPERTY()
    int32 StorageSlotCount = 0;

    /** AFGBuildableFactory::GetCurrentProductionBoost() -- 1.0 means no boost. */
    UPROPERTY()
    float ProductionBoost = 1.f;

    /** Set when this row is one of the mod's own Form 3 upgrade buildings. */
    UPROPERTY()
    bool bIsUpgradeBuilding = false;

    /**
     * ★本轮 里程碑排序序位（资源节点：按里程碑/太空电梯阶段从上往下排）。
     * 0=最早；其他类目恒 99。仅面板渲染排序用，不参与 RPC 语义。
     */
    UPROPERTY()
    int32 MilestoneTier = 99;

    /** ★本轮 里程碑 / 太空电梯阶段标签（资源节点分组头展示），其他类目恒空。 */
    UPROPERTY()
    FString MilestoneLabel;

    // ---- ★1.0.11 筛选扩展（用户方案稿：问题开关 / 效率区间 / 填充率 / 耗电）--------------
    /**
     * 问题位掩码，位序见 EWT_Issue（OutputBlocked=bit0 … Empty=bit7）。
     * 0 = 没检测到任何问题（**不是**「没检测」——不适用该判定的类目同样留 0，
     * 面板侧靠 Category 决定要不要显示「问题」下拉，而不是靠这个字段区分）。
     */
    UPROPERTY()
    uint8 IssueFlags = 0;

    /**
     * 生产效率 0~1（AFGBuildableFactory::GetProductivity，与原版 UI 的效率百分比同源）。
     * **-1 = 不适用**：非 AFGBuildableFactory 类目（容器/矿点/枢纽）、以及不产出的工厂。
     * 用负值而不是 0：0 是「效率 0%」，会把一台正常机器说成完全停摆。
     */
    UPROPERTY()
    float Efficiency01 = -1.f;

    /**
     * 容器填充率 0~100（已占用格数 / 总格数）。**-1 = 不适用**（不是容器类目）。
     * 口径按「格」而不是按件数：不同物品堆叠上限不同（铁板 100 / 电缆 200），
     * 按件数算会得出「装电缆的箱子比装铁板的箱子满」这种反直觉结论。
     */
    UPROPERTY()
    float FillPercent = -1.f;

    /**
     * 当前实际耗电 MW（UFGPowerInfoComponent::GetActualConsumption）。
     * **-1 = 不适用**（没挂电力组件）。0 = 挂着但当前不耗电（停机）。
     */
    UPROPERTY()
    float PowerMW = -1.f;
};

/**
 * ★1.0.9 电网曲线里的一条线（对应原版电线杆 UI 图例里的一格）。
 *
 * 为什么要把点序列搬过来：原版那块图是客户端按 FPowerCircuitStats 的环形缓冲画的，
 * 而我们是在服务端扫描时把数据打包成一次回包 —— 客户端读不到 UFGPowerCircuit 的
 * 私有 mPowerStats，只能靠这里带过去。
 *
 * 降采样到 ~120 点、每段取**最大值**：曲线上第 3 个峰尖被 4 个采样点包围时，取平均会把
 * 峰值抹平（用户点名要看的正是峰值），取最大则保真。数据量也从 300+ 点降到 120 点。
 */
USTRUCT()
struct FWT_GridSeries
{
    GENERATED_BODY()

    /**
     * 通道编号，与面板侧 WT_GridSeriesColor/Label 共用同一套 switch：
     *   0 = 消耗（橙）      1 = 最大消耗（青）  2 = 发电（灰白）
     *   3 = 上限（浅灰）    4 = 能源增幅（紫）
     * ★顺序与原版图例一致，改这里必须同步改面板那两个函数。
     */
    UPROPERTY()
    uint8 Kind = 0;

    /** 降采样后的采样值，单位 MW。索引 0 = 最旧，末项 = 最新。 */
    UPROPERTY()
    TArray<float> Points;
};

/**
 * ★1.0.9 一条电网（= 一个 UFGPowerCircuit）的快照。
 *
 * 数值口径与原版电线杆 UI 的图例完全一致（FPowerCircuitStats，FGPowerCircuit.h:57）：
 *   消耗 = PowerConsumed / 最大消耗 = MaximumPowerConsumption（= 峰值）
 *   发电 = PowerProduced / 上限 = PowerProductionCapacity / 能源增幅 = BoostProduced
 */
USTRUCT()
struct FWT_GridEntry
{
    GENERATED_BODY()

    /** UFGPowerCircuit::GetCircuitID()。同一局内稳定，用作「选中哪条电网」的键。 */
    UPROPERTY()
    int32 CircuitId = -1;

    /** 这条电网上有多少根电线杆（立杆/壁插/电力塔）。纯展示，说明这条线铺了多大。 */
    UPROPERTY()
    int32 PoleCount = 0;

    /** 这些杆上已接线 / 可接线的插口总数。 */
    UPROPERTY()
    int32 LinkUsed = 0;

    UPROPERTY()
    int32 LinkMax = 0;

    /** 代表位置：离终端最近的那根杆（没有杆时取第一个成员建筑），用于「传送过去」。 */
    UPROPERTY()
    FVector Location = FVector::ZeroVector;

    UPROPERTY()
    float DistanceMeters = 0.f;

    // ---- 五项实时值（单位 MW，与列表/图例同源）----
    UPROPERTY()
    float ConsumedMW = 0.f;

    /** 最大消耗 —— 用户说的「电力峰值」。 */
    UPROPERTY()
    float PeakConsumedMW = 0.f;

    UPROPERTY()
    float ProducedMW = 0.f;

    UPROPERTY()
    float CapacityMW = 0.f;

    UPROPERTY()
    float BoostMW = 0.f;

    UPROPERTY()
    bool bHasPower = false;

    UPROPERTY()
    bool bFuseBlown = false;

    /** 曲线覆盖的时间跨度（秒）= FPowerCircuitStats::StatHistoryTime，图上横轴标注用。 */
    UPROPERTY()
    float HistorySeconds = 0.f;

    /** 五条线，按 Kind 顺序装。空数组 = 该电路还没有历史点（刚建好/没通电）。 */
    UPROPERTY()
    TArray<FWT_GridSeries> Series;
};

/**
 * One alien-energy collectible found on the map.
 *
 * Only the location and a label are carried back: the terminal's job here is "show me where they
 * are and fly me there", not "let me operate them". The actor pointer is included so the teleport
 * request can pass it straight back through the same NetGUID mechanism the machine list uses.
 */
USTRUCT()
struct FWT_CollectibleEntry
{
    GENERATED_BODY()

    /** The pickup / drop-pod actor. Replicated by NetGUID, same as FWT_MachineEntry::Target. */
    UPROPERTY()
    TObjectPtr<AActor> Actor = nullptr;

    /** Which of the four kinds this is (see EWT_CollectibleKind). */
    UPROPERTY()
    uint8 Kind = 0;

    /** Human readable label ("Power Slug (Purple)", "Mercer Sphere", ...). */
    UPROPERTY()
    FText DisplayName;

    /** World position, for the coordinate readout. */
    UPROPERTY()
    FVector Location = FVector::ZeroVector;

    /** Distance from the terminal, in metres. */
    UPROPERTY()
    float DistanceMeters = 0.f;

    // ---- v0.2.3: full-map listing (user request: show everything, with state + region) ----

    /** True when the slug/sphere/loop is already picked up, or the drop pod already opened.
     *  Collected rows stay in the list (the coordinates are still useful) but render greyed. */
    UPROPERTY()
    bool bCollected = false;

    /** Official map-area name the entry sits in ("北部森林" ... from UFGMapArea::GetAreaDisplayName,
     *  so it is localized by the game itself). Empty when no map-area volume contains the point. */
    UPROPERTY()
    FText RegionName;

    /**
     * ★0.2.136 TRUE when this spawn is inside a cave (溶洞). Set from collectibles.json's "cave"
     * field or the altitude heuristic (WT_CAVE_Z_THRESHOLD). The panel tags these rows: a cave
     * spawn cannot be teleported to from outside (0.2.135 refuses instead of dropping the player
     * into the void) — the player has to walk into that cave first.
     */
    UPROPERTY()
    bool bCave = false;

    /** Drop pods only: what it takes to open it ("开启材料：转子 × 5" / "开启电力：20 MW").
     *  Empty for the other three kinds. */
    UPROPERTY()
    FText CostText;
};

/** Reply payload for a "list machines" request. */
USTRUCT()
struct FWT_MachineListReply
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FWT_MachineEntry> Entries;

    /** Per-category totals so the client can draw the category buttons without recounting. */
    UPROPERTY()
    TArray<int32> CategoryCounts;

    /** True when the scan bailed out early because the world has more buildables than the cap. */
    UPROPERTY()
    bool bTruncated = false;

    /**
     * ★1.0.28 世界中实际存在的资源节点键集合（FWTResourceNodes::NodeKey = 资源名+坐标米）。
     * 服务端扫描时对每个驻留的 AFGResourceNodeBase 记账（含静态库已收录的，在任何
     * 去重过滤之前）。客户端 MergeResourceNodesIntoList 用它把「玩家附近且不在本集合」
     * 的静态库条目判为已拆除——修「拆除/放置后 MOD 节点不同步」（静态库是全图快照，
     * 拆除的库条目会被每次合并重新加回列表）。井卫星不记账（库里本就没有卫星条目）。
     */
    UPROPERTY()
    TArray<FString> LiveNodeKeys;

    // ---- factory-wide power overview, summed over every power circuit ----
    // Sourced from FPowerCircuitStats (FGPowerCircuit::GetStats), the same numbers the vanilla
    // power graph shows, so the terminal agrees with the in-game power panel.

    /** MW currently being generated (actual output, not capacity). */
    UPROPERTY()
    float PowerProducedMW = 0.f;

    /** MW currently being drawn by machines. */
    UPROPERTY()
    float PowerConsumedMW = 0.f;

    /** MW of generation capacity installed (the ceiling PowerProduced is capped by). */
    UPROPERTY()
    float PowerCapacityMW = 0.f;

    /** MW that *could* be drawn if every machine ran flat out. */
    UPROPERTY()
    float PowerMaxConsumptionMW = 0.f;

    /** Total energy stored in all power storage buildings, in MWh. */
    UPROPERTY()
    float BatteryStoredMWh = 0.f;

    /** Total energy capacity of all power storage buildings, in MWh. */
    UPROPERTY()
    float BatteryCapacityMWh = 0.f;

    /** Number of power circuits folded into the totals above. */
    UPROPERTY()
    int32 CircuitCount = 0;

    /**
     * ★1.0.9 每条电网一条（面板「电网」类目的全部内容）。
     *
     * 与上面 PowerProducedMW 那几个「全厂合计」并存不冲突：合计是给概览条用的一个数，
     * 这里是逐条电路的明细 + 曲线点。二者同源（同一个 FPowerCircuitStats::GetStats），
     * 所以概览条上的数字与电网类目里各条之和永远对得上。
     */
    UPROPERTY()
    TArray<FWT_GridEntry> Grids;

    // ---- map-wide alien-energy collectibles ----
    // Scanned unfiltered (the whole map, not just the terminal's surroundings) because finding
    // them is the point: they are scattered across the world and are the reason to fly out.

    /** Every 能量蛞蝓 / 梅瑟尔球 / 索莫晶体 / 空投舱 found on the map. */
    UPROPERTY()
    TArray<FWT_CollectibleEntry> Collectibles;

    /** Per-kind totals, indexed by EWT_CollectibleKind. */
    UPROPERTY()
    TArray<int32> CollectibleCounts;

    // ---- Form 3 "D": shared-session state --------------------------------------
    // "Weak sharing": every player still opens their OWN panel, but the server tells each of them
    // who else is looking at the same terminal and whether they are allowed to press the buttons.
    // Nothing about the widget itself is replicated -- only these few fields -- which is what keeps
    // D cheap enough to sit on top of the existing single-cast RPCs.

    /** Display name of the player who owns the shared session (the one who opened it first). */
    UPROPERTY()
    FString SessionOwnerName;

    /** True when the REQUESTING player is the session owner and may perform write actions. */
    UPROPERTY()
    bool bIsSessionOwner = true;

    /** Display names of every player currently viewing the same terminal. */
    UPROPERTY()
    TArray<FString> SessionViewers;

    /** True when more than one player is viewing, so the panel can show the sharing banner. */
    UPROPERTY()
    bool bSharedSession = false;
};
