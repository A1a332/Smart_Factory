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

    /** AFGBuildableStorage: 所有箱子 / 工业容器. */
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

    /**
     * ★0.2.153m 液体容器（AFGBuildablePipeReservoir：液体缓冲罐 Build_PipeStorageTank /
     * 工业储罐 Build_IndustrialTank —— utoc 索引实证）。原「容器」按用户要求拆成液体/固体两类，
     * 原 Container=1 停止使用（值保留不复用，避免打乱既有编号语义）。
     */
    LiquidContainer = 16,

    /** ★0.2.153m 固体容器（AFGBuildableStorage：箱子 / 工业储存容器 / 中央仓库）。 */
    SolidContainer = 17,

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

    Count = 20
};

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

    /** Which bucket this entry belongs to (see EWT_MachineCategory). */
    UPROPERTY()
    uint8 Category = 0;

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
