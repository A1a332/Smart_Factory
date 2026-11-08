#include "Remote_FactoryUpgradeRegistry.h"

#include "Remote_FactoryLoc.h"
#include "Remote_FactoryLog.h"
#include "Remote_FactoryCrashTrace.h"
#include "Remote_FactoryUpgrades.h"
#include "Remote_FactoryUpgradeHolograms.h"
#include "Remote_FactoryUpgradeResearch.h"
#include "Remote_FactoryRegistry.h"
#include "Remote_FactoryEquipment.h"   // ★0.2.37 配方补发判据要用 URemote_FactoryEquipRecipe

#include "ItemAmount.h"
#include "FGRecipeManager.h"
#include "FGSchematicManager.h"   // ★0.2.48 配方补发门槛：IsSchematicPurchased（读档安全判据）
#include "Resources/FGItemDescriptor.h"
#include "Engine/StaticMesh.h"   // ★0.2.59 ApplyVanillaMirror 拷贝 mConveyorMesh
#include "FGSchematicCategory.h"
#include "Unlocks/FGUnlockRecipe.h"
#include "Unlocks/FGUnlockGiveItem.h"   // ★0.2.36h 商店发货解锁（UFGUnlockGiveItem，abstract）
#include "FGSchematic.h"
#include "FGResearchTree.h"            // ★2026-11-07 #45：读原版研究树首节点 schematic 的 mSchematicIcon.ImageSize 作基准
#include "FGResearchTreeNode.h"        // ★2026-11-07 #45：调 UFGResearchTreeNode::GetNodeSchematic() 需完整声明
#include "Equipment/FGBuildGun.h"
#include "Buildables/FGBuildable.h"          // ★0.2.37 WT_FindConstructorMk1Class 的基类过滤
#include "Buildables/FGBuildableFactory.h"
// ★1.0.7 制作台（手工台）绑定：UFGWorkBench 是**组件**（FGWorkBench.h:16
// `class UFGWorkBench : public USceneComponent, public IFGRecipeProducerInterface`），
// 手工配方的 producer 判据挂在它身上（GetRecipeProducerItemDescriptor 返回 ItemDescriptor，
// FGWorkBench.h:28），与机器（Buildable 类）不是同一套 ⇒ 必须 include 才能按基类解析。
#include "FGWorkBench.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
// ★2026-11-05：直接操作 FSlateBrush（mSchematicIcon）。本模块 Build.cs 已依赖 SlateCore（:48-49）。
//   用于补 UVRegion —— 凭空 NewObject 的 brush 该字段是 (0,0)-(0,0) 但 bIsValid=true，
//   Slate 会据此采样单点 ⇒ 渲染成纯色块（MAM 节点「空方块」的最后一环）。
#include "Styling/SlateBrush.h"
#include "EngineUtils.h"
#include "Containers/Ticker.h"

// ----------------------------------------------------------------------------
// 0.2.7 GC 钉死台账 + 复查探针
//
// 所有经 StaticLoadClass 解析、被本模组长期持有的 UClass 都记进 GWT_PinnedClasses
// 并 AddToRoot。90s/180s 后各跑一次复查并写 Display 日志——首次 GC 在世界加载后
// ~60s 发生，这两条日志能直接证明钉死是否挡住了回收（新存档 80s 崩溃的验证手段）。
// ----------------------------------------------------------------------------
static TArray<UClass*> GWT_PinnedClasses;

static void WT_QueuePinnedClassProbe()
{
    // 注：这里的函数级 static 是**进程级一次性诊断**（只排一个 90s 后的探针），
    // 与被守卫对象的世界无关，所以它不构成"换档后守卫恒 true"那类跨世界隐患。
    static bool bQueued = false;
    if (bQueued)
    {
        return;
    }
    bQueued = true;

    FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateLambda([](float) -> bool
        {
            WT_ZONE_COARSE(TEXT("Upgrade.PinnedClassProbe"));
            int32 Bad = 0;
            for (UClass* C : GWT_PinnedClasses)
            {
                if (!C || !IsValid(C))   // 本引擎无 UObject::IsValid() 成员，用全局 IsValid(const UObject*)
                {
                    ++Bad;
                }
            }
            UE_LOG(LogRemote_Factory, Display,
                   TEXT("[upgrade-probe] GC 复查: %d 个钉住类, %d 个失效."),
                   GWT_PinnedClasses.Num(), Bad);
            return false; // one-shot
        }), 90.f);
}

// ----------------------------------------------------------------------------
// Shared descriptor base
// ----------------------------------------------------------------------------

UWTUpgradeDescriptorBase::UWTUpgradeDescriptorBase()
{
    // The name and description are set per subclass, so the descriptor must be told to use them
    // rather than derive them from the buildable class.
    mUseDisplayNameAndDescription = true;

    // One unit per slot: these are buildings, and a stack of five batteries in one slot would let a
    // player place five without the build-gun cost being paid five times.
    //
    // ★v0.2.6 加固（与便携终端同款）：ImportText 对字节枚举可能静默失败 → 回读 + 字节强写。
    if (FProperty* StackProp = FindFProperty<FProperty>(
            UFGItemDescriptor::StaticClass(), TEXT("mStackSize")))
    {
        uint8* Raw = StackProp->ContainerPtrToValuePtr<uint8>(this);
        const uint8 Before = *Raw;
        StackProp->ImportText_Direct(TEXT("SS_ONE"), Raw, this, 0, nullptr);
        if (*Raw != static_cast<uint8>(EStackSize::SS_ONE))
        {
            WT_WARNF(TEXT("[upgrade] %s mStackSize import silently failed (default=%d); forcing SS_ONE."),
                     *GetClass()->GetName(), static_cast<int32>(Before));
            *Raw = static_cast<uint8>(EStackSize::SS_ONE);
        }
        UE_LOG(LogRemote_Factory, Display,
               TEXT("[upgrade] %s mStackSize: final=%d, mCachedStackSize=%d."),
               *GetClass()->GetName(), static_cast<int32>(*Raw), mCachedStackSize);
        // ★v0.2.8 真根因：GetStackSize() 先读 mCachedStackSize（原生 CDO 不走 PostLoad，
        // 缓存停在 0 → GetStackSize 恒返 0 → 物品永远"塞不进背包"）。缓存写 1（SS_ONE）。
        mCachedStackSize = 1;
    }
}

TSubclassOf<AFGHologram> UWTUpgradeDescriptorBase::GetHologramClassInternal() const { return HologramClass; }
FText UWTUpgradeDescriptorBase::GetItemNameInternal() const { return DisplayName; }
FText UWTUpgradeDescriptorBase::GetItemDescriptionInternal() const { return Desc; }

void UWTUpgradeDescriptorBase::InitCommon()
{
    // mBuildableClass is a protected member of UFGBuildingDescriptor, and this class derives from
    // it, so this is an ordinary assignment -- no reflection needed.
    mBuildableClass = BuildableClass;

    // mDisplayName / mDescription are public, again an ordinary write.
    mDisplayName = DisplayName;
    mDescription = Desc;

    // ---- build-gun category ----
    // mBuildCategory / mSubcategories are NOT declared on the shipped UFGBuildingDescriptor stub, so
    // they are written by name. The terminal's own descriptor does exactly this and its build-gun
    // entry works, so the property names are known-good.
    //
    // ★v0.2.24：这里原来只试 "mBuildCategory"，而 1.0 的真实属性名是 "mCategory"（Registry.cpp 的
    // SetBuildCategoryProp 一直带双名回退、终端就没报过警；本文件只试一个名字 → 三个升级建筑
    // 全部命中 "no mBuildCategory" 告警，建造枪里不出现）。补上同名回退，与终端完全一致。
    //
    // Reusing the TERMINAL's category rather than creating a new tab is deliberate: the three
    // upgrades are parts of the terminal system, and a fourth build-gun tab holding three items
    // would be a worse inventory than one tab holding all four.
    FProperty* CategoryProp = FindFProperty<FProperty>(GetClass(), TEXT("mBuildCategory"));
    if (!CategoryProp)
    {
        CategoryProp = FindFProperty<FProperty>(GetClass(), TEXT("mCategory"));
    }
    if (CategoryProp)
    {
        const FString Path = URemote_FactoryBuildCategory::StaticClass()->GetPathName();
        const TCHAR* Result = CategoryProp->ImportText_Direct(
            *Path, CategoryProp->ContainerPtrToValuePtr<void>(this), this, 0, nullptr);
        WT_LOGF(TEXT("[upgrade] %s.%s <= %s (imported=%s)"),
                *GetClass()->GetName(), *CategoryProp->GetName(), *Path, Result ? TEXT("ok") : TEXT("FAILED"));
    }
    else
    {
        WT_WARNF(TEXT("[upgrade] no build-category property (mBuildCategory/mCategory) on %s; "
                      "the build gun may not list it."),
                 *GetClass()->GetName());
    }

    if (FArrayProperty* ArrProp = FindFProperty<FArrayProperty>(GetClass(), TEXT("mSubcategories")))
    {
        FScriptArrayHelper Helper(ArrProp, ArrProp->ContainerPtrToValuePtr<void>(this));
        if (Helper.Num() == 0)
        {
            Helper.AddValue();
        }
        const FString Path = URemote_FactoryBuildSubCategory::StaticClass()->GetPathName();
        ArrProp->Inner->ImportText_Direct(*Path, Helper.GetRawPtr(0), this, 0, nullptr);
    }
}

UWTUpgradePowerStorageDescriptor::UWTUpgradePowerStorageDescriptor()
{
    BuildableClass = AWTUpgradePowerStorage::StaticClass();
    HologramClass = AWTUpgradePowerStorageHologram::StaticClass();
    DisplayName = AWTUpgradePowerStorage::GetDisplayName();
    // ★1.0.12 中文化 + 数字纠偏（旧文案 1000 MWh 是 0.2.48 提速前的旧数值，实际 1600/400）。
    Desc = WT_LOCTEXT("Desc_UpgradePowerStorage",
        "一座超大容量电池组（1600 MWh）。存储电网盈余、在缺电时自动释放；"
        "建成后整座工厂的蓄电总量会显示在智能工厂终端面板顶部。");
    InitCommon();
}

UWTUpgradeStorageDescriptor::UWTUpgradeStorageDescriptor()
{
    BuildableClass = AWTUpgradeStorage::StaticClass();
    HologramClass = AWTUpgradeStorageHologram::StaticClass();
    DisplayName = AWTUpgradeStorage::GetDisplayName();
    Desc = WT_LOCTEXT("Desc_UpgradeStorage",
        "An 8 x 12 = 96 slot container for stockpiling a whole production line's intermediates. "
        "The Smart Factory picks it up in its scan, so you can check the contents from there.");
    InitCommon();
}

UWTUpgradeCloudDownloaderDescriptor::UWTUpgradeCloudDownloaderDescriptor()
{
    BuildableClass = AWTUpgradeCloudDownloader::StaticClass();
    HologramClass = AWTUpgradeCloudDownloaderHologram::StaticClass();
    DisplayName = AWTUpgradeCloudDownloader::GetDisplayName();
    Desc = WT_LOCTEXT("Desc_UpgradeCloudDownloader",
        "The other half of the cloud storage system: pulls items back OUT of the central pool "
        "into its own buffer, one stack at a time. Open it like any storage container and grab "
        "what you need.");
    InitCommon();
}

UWTUpgradeCloudFluidUploaderDescriptor::UWTUpgradeCloudFluidUploaderDescriptor()
{
    BuildableClass = AWTUpgradeCloudFluidUploader::StaticClass();
    HologramClass = AWTUpgradeCloudFluidUploaderHologram::StaticClass();
    DisplayName = AWTUpgradeCloudFluidUploader::GetDisplayName();
    Desc = WT_LOCTEXT("Desc_UpgradeCloudFluidUploader",
        "云端流体上传站：把本地管网的流体吸入，记入我们自己的液态账本云（跨存档/跨地图共享）。");
    InitCommon();
}

UWTUpgradeCloudFluidDownloaderDescriptor::UWTUpgradeCloudFluidDownloaderDescriptor()
{
    BuildableClass = AWTUpgradeCloudFluidDownloader::StaticClass();
    HologramClass = AWTUpgradeCloudFluidDownloaderHologram::StaticClass();
    DisplayName = AWTUpgradeCloudFluidDownloader::GetDisplayName();
    Desc = WT_LOCTEXT("Desc_UpgradeCloudFluidDownloader",
        "云端流体下载站：从液态账本云把流体排进本地管网。按 E 打开流体选型面板，选好要抽的流体后持续输出。");
    InitCommon();
}


// ----------------------------------------------------------------------------
// 强化能量碎片（★0.2.36 落地）
// ----------------------------------------------------------------------------

UWTUpgradePowerShardDescriptor::UWTUpgradePowerShardDescriptor()
{
    mDisplayName = GetDisplayName();
    mDescription = GetItemDescription();

    // 走原版超频档：插入原版机器碎片插槽后完全按原版逻辑结算（含耗电指数曲线）。
    // 每片增量恒为 +100%（mExtraPotential=1.0；ApplyShardStage 也已统一为 1.0，不再随研究阶段缩放）。
    mPowerShardType = EPowerShardType::PST_Overclock;
    mExtraPotential = 1.0f;   // ★用户定案（2026-10-17）：每片强化碎片 = +100% 超频（原版 0.5=+50%；ApplyShardStage 也已统一为 1.0）
    mExtraProductionBoost = 1.0f;   // 非本类型用不到，给个无害值

    // 图标：★0.2.36h（用户定案「新版的能量碎片颜色改成彩虹」）优先用模组自带的**彩虹碎片贴图**
    // （真资产，editor 命令行导入 —— 运行时 CreateTransient 会被 GC，见 create_wt_icons.py 头注释）；
    // 资产缺失时回落原版紫色碎片贴图，保证图标永不为空（图标为空会被 UI 整块藏掉）。
    // ★0.2.137 改名：先试新挂载根 /Smart_Factory/，旧根兜底
    UTexture2D* Icon = LoadObject<UTexture2D>(nullptr,
        TEXT("/Smart_Factory/Icon/icon_rainbow_shard.icon_rainbow_shard"));
    if (!Icon)
    {
        Icon = LoadObject<UTexture2D>(nullptr,
            TEXT("/Remote_Factory/Icon/icon_rainbow_shard.icon_rainbow_shard"));
    }
    if (!Icon)
    {
        WT_WARNF(TEXT("[shard] rainbow icon asset missing -- falling back to vanilla shard icon."));
        Icon = LoadObject<UTexture2D>(nullptr,
            TEXT("/Game/FactoryGame/Resource/Environment/Crystal/UI/PowerShard_256.PowerShard_256"));
    }
    if (Icon)
    {
        for (const TCHAR* PropName : { TEXT("mSmallIcon"), TEXT("mPersistentBigIcon") })
        {
            if (FObjectProperty* OP = CastField<FObjectProperty>(
                    UFGItemDescriptor::StaticClass()->FindPropertyByName(PropName)))
            {
                OP->SetObjectPropertyValue(OP->ContainerPtrToValuePtr<void>(this), Icon);
            }
        }
    }

    // 组数：照抄原版能量碎片（SS_MEDIUM = 100 组）。两者都是本类可直写的保护成员。
    mStackSize = EStackSize::SS_MEDIUM;
    mCachedStackSize = 100;

    // ★修复（用户实测「强化碎片放进机器是灰色、拿不下来、拆机后消失」）：机器超频槽/入库按
    // mForm 校验，C++ 原生类构造函数从不设 mForm ⇒ RF_INVALID(0) 被直接拒绝，碎片掉进通用
    // 库存才出现上述现象（代码 0.2.59 注释已自陈此根因）。原版蓝色碎片 CDO 的权威值就是
    // RF_SOLID，这里在构造函数里直接兜底设好，不再依赖 ApplyVanillaMirror 能否在
    // POST_INITIALIZATION 跑到——万一它没跑，mForm 也正确。
    mForm = EResourceForm::RF_SOLID;
}

FText UWTUpgradePowerShardDescriptor::GetDisplayName()
{
    return WT_LOCTEXT("Shard_Name", "Enhanced Power Shard");
}

// ★0.2.59 从原版碎片 CDO 镜像 form/建模/图标（声明处注释有完整动机）。
void UWTUpgradePowerShardDescriptor::ApplyVanillaMirror(UClass* VanillaShardClass)
{
    if (!VanillaShardClass || VanillaShardClass == StaticClass())
    {
        WT_WARNF(TEXT("[shard] vanilla shard class unresolved -- mirror skipped (form/mesh/icon keep old values)."));
        return;
    }

    // ① mForm：原版蓝图类的权威值（RF_SOLID）。我们的 C++ 类从不设此成员 ⇒ RF_INVALID(0)，
    //    入库/碎片槽按 Form 校验直接拒绝 —— 用户实测「放不进机器/不能超频」的根因。
    mForm = UFGItemDescriptor::GetForm(VanillaShardClass);

    // ② mConveyorMesh：原版红色水晶建模（传送带/掉落/手持展示）。跨类读原版 CDO 保护成员只能反射。
    UObject* Src = VanillaShardClass->GetDefaultObject();
    if (FObjectProperty* OP = CastField<FObjectProperty>(
            UFGItemDescriptor::StaticClass()->FindPropertyByName(TEXT("mConveyorMesh"))))
    {
        if (UStaticMesh* Mesh = Cast<UStaticMesh>(OP->GetObjectPropertyValue(OP->ContainerPtrToValuePtr<void>(Src))))
        {
            mConveyorMesh = Mesh;
        }
    }

    // ③ 图标：原版红色碎片贴图，同名属性反射拷贝（与构造函数同一套反射例程）。
    int32 CopiedIcons = 0;
    for (const TCHAR* PropName : { TEXT("mSmallIcon"), TEXT("mPersistentBigIcon") })
    {
        if (FObjectProperty* OP = CastField<FObjectProperty>(
                UFGItemDescriptor::StaticClass()->FindPropertyByName(PropName)))
        {
            if (UObject* V = OP->GetObjectPropertyValue(OP->ContainerPtrToValuePtr<void>(Src)))
            {
                OP->SetObjectPropertyValue(OP->ContainerPtrToValuePtr<void>(this), V);
                ++CopiedIcons;
            }
        }
    }

    WT_WARNF(TEXT("[shard] vanilla mirror: form=%d conveyorMesh=%s icons=%d (src=%s)."),
             static_cast<int32>(mForm),
             mConveyorMesh ? *mConveyorMesh->GetName() : TEXT("<null>"),
             CopiedIcons,
             *VanillaShardClass->GetName());
}

FText UWTUpgradePowerShardDescriptor::GetItemDescription()
{
    return WT_LOCTEXT("Shard_Desc",
        "A refined shard for the vanilla shard slots. It overclocks exactly like a vanilla shard, "
        "but packs more potential per slot -- and the machines' potential ceiling rises with your "
        "MAM research (up to 400%: 3 shards = 1 + 3 x 1.0). Power draw follows the vanilla curve, "
        "so high overclocks burn coal fast.");
}

UWTUpgradeAcceleratorDescriptor::UWTUpgradeAcceleratorDescriptor()
{
    BuildableClass = AWTUpgradeAccelerator::StaticClass();
    HologramClass = AWTUpgradeAcceleratorHologram::StaticClass();
    DisplayName = AWTUpgradeAccelerator::GetDisplayName();
    Desc = WT_LOCTEXT("Desc_UpgradeAccelerator",
        "Needs no power shards: once powered it applies a long-lasting production multiplier to "
        "itself. Capped at 2.5x -- beyond that the power draw outgrows the output and you are "
        "better off building another machine.");
    InitCommon();
}

// ----------------------------------------------------------------------------
// Cost tables + helpers
// ----------------------------------------------------------------------------

namespace
{
    struct FWTCostLine
    {
        const TCHAR* ItemPath;
        int32 Amount;
    };

    // ------------------------------------------------------------------------
    // ★★★ v0.2.2 重排：三个扩展建筑不再共用一个 T4 里程碑，而是各自挂在
    //     T1 / T3 / T5（见文件末尾的三个里程碑类）。所以每个建筑的**建造造价**
    //     必须 ≤ 它自己的解锁层级，否则就是本会话一路在修的「越级」老毛病。
    //
    //     ★0.2.36（用户定案）：云端储存上传站/云端存储下载站的建造材料 =
    //     原版 Recipe_CentralStorage 的用料**同顺序**（SAM波动器×10 → 多功能框架×10 →
    //     电线×100），再各追加 1 个强化能量碎片（0.2.36 新物品）。
    //     ★0.2.45（用户定案）：**去掉梅瑟尔球（Desc_WAT2）** —— 它是限量的世界资源，
    //     不该当"每建一座都要付"的常耗材料；其余三项照原版同序保留，**不补替代品**。
    //     净成本 = 原版中央存储站 − 梅瑟尔球×1 + 强化能量碎片×1。
    //     强化碎片类是本模组原生类（UWTUpgradePowerShardDescriptor::StaticClass()），
    //     不走 FillIngredients 的路径解析 —— 原生类天然 GC 安全，无需钉死。
    // ------------------------------------------------------------------------

    // 云端储存上传站 —— T1 解锁（跟着「无线终端」那一个里程碑一起给）。
    const FWTCostLine GUploadStationCost[] = {
        { TEXT("/Game/FactoryGame/Resource/Parts/SAMFluctuator/Desc_SAMFluctuator.Desc_SAMFluctuator_C"), 10 },
        { TEXT("/Game/FactoryGame/Resource/Parts/ModularFrame/Desc_ModularFrame.Desc_ModularFrame_C"), 10 },
        { TEXT("/Game/FactoryGame/Resource/Parts/Wire/Desc_Wire.Desc_Wire_C"), 100 },
    };

    // 云端存储下载站 —— 与上传站同套用料（用户定案：云端站都消耗 1 个强化碎片；
    // 0.2.45 起两表都不含梅瑟尔球）。
    const FWTCostLine GDownloaderCost[] = {
        { TEXT("/Game/FactoryGame/Resource/Parts/SAMFluctuator/Desc_SAMFluctuator.Desc_SAMFluctuator_C"), 10 },
        { TEXT("/Game/FactoryGame/Resource/Parts/ModularFrame/Desc_ModularFrame.Desc_ModularFrame_C"), 10 },
        { TEXT("/Game/FactoryGame/Resource/Parts/Wire/Desc_Wire.Desc_Wire_C"), 100 },
    };

    // ★1.0.12（用户定案）：超级蓄电池建造材料 = 原版储能（Power Storage）用料 ×4：
    //   原版 100×线缆 + 10×多功能框架 + 5×装壳工业梁 ⇒ 400 / 40 / 20。
    //   路径全部用本 mod 已实机验证过的（装壳工业梁的描述符类名历史上就是 Desc_SteelPlateReinforced，
    //   不存在 Desc_EncasedIndustrialBeam —— WP v1.0.49 注释与官方 wiki className 双重实锤）。
    const FWTCostLine GStorageUnitCost[] = {
        { TEXT("/Game/FactoryGame/Resource/Parts/Wire/Desc_Wire.Desc_Wire_C"), 400 },                       // T0 线缆
        { TEXT("/Game/FactoryGame/Resource/Parts/ModularFrame/Desc_ModularFrame.Desc_ModularFrame_C"), 40 },// T2 多功能框架
        { TEXT("/Game/FactoryGame/Resource/Parts/SteelPlateReinforced/Desc_SteelPlateReinforced.Desc_SteelPlateReinforced_C"), 20 }, // T4 装壳工业梁
    };

    // 产能加速器 —— T5 解锁，最贵的一件：它替代的是能量碎片（有限的世界资源），
    // 所以造价应当高于「再造一台机器」。
    // 钢筋混凝土梁(T4) ≤ T5 合法；定子按用户要求换成智能嵌板 / Smart Plating（太空电梯件）。
    const FWTCostLine GAcceleratorCost[] = {
        { TEXT("/Game/FactoryGame/Resource/Parts/SteelPlateReinforced/Desc_SteelPlateReinforced.Desc_SteelPlateReinforced_C"), 30 }, // T4
        { TEXT("/Game/FactoryGame/Resource/Parts/ModularFrame/Desc_ModularFrame.Desc_ModularFrame_C"), 20 },                        // T2
        { TEXT("/Game/FactoryGame/Resource/Parts/SpaceElevatorParts/Desc_SpaceElevatorPart_1.Desc_SpaceElevatorPart_1_C"), 40 },     // T2 智能嵌板
        { TEXT("/Game/FactoryGame/Resource/Parts/Rotor/Desc_Rotor.Desc_Rotor_C"), 15 },                                             // T2
    };

    /**
     * Fill an ingredient array from a cost table.
     *
     * ★ This takes the array BY REFERENCE and never touches "this". That is the whole point:
     * TArray<FItemAmount>& needs no access to any protected member, so it can live as a free
     * helper. The protected member is passed in by a caller that already has the right to name it
     * (see the three ApplyIngredients() below, which are members of UFGRecipe subclasses).
     *
     * mIngredients is protected on UFGRecipe. A free helper naming "Recipe->mIngredients" directly
     * fails C2248, which is exactly the error this file was originally built with -- the existing
     * URemote_FactoryRecipe::ApplyIngredients works only because it writes the member from inside
     * a derived class's own member function.
     */
    void FillIngredients(TArray<FItemAmount>& OutIngredients, const FWTCostLine* Lines, int32 Count,
                         const TCHAR* Who)
    {
        OutIngredients.Empty();
        for (int32 i = 0; i < Count; ++i)
        {
            UClass* ItemClass = StaticLoadClass(UFGItemDescriptor::StaticClass(), nullptr, Lines[i].ItemPath);
            if (!ItemClass)
            {
                WT_WARNF(TEXT("[upgrade] %s cost could not resolve item '%s'."), Who, Lines[i].ItemPath);
                continue;
            }
            // 0.2.7 GC 钉死：StaticLoadClass 解析出的类若仅靠我们的引用存活，首次 GC（世界加载后
            // 约 60s）会把它回收——之后 MAM 研究管理器才触发食谱注册，SML 遍历食谱材料时撞到
            // 失效类 → "Attempt to add a Reference to an invalid object from WTUpgradePowerStorageRecipe"
            // （2026-10-06 新存档 80s 必崩）。裸 UClass* 不是 GC 引用，必须 AddToRoot。
            ItemClass->AddToRoot();
            GWT_PinnedClasses.Add(ItemClass);
            UE_LOG(LogRemote_Factory, Display,
                   TEXT("[upgrade-fill] %s item '%s' -> %s (valid=%d, pinned)."),
                   Who, Lines[i].ItemPath, *ItemClass->GetName(),
                   IsValid(ItemClass) ? 1 : 0);
            OutIngredients.Add(FItemAmount(TSubclassOf<UFGItemDescriptor>(ItemClass), Lines[i].Amount));
        }
        WT_LOGF(TEXT("[upgrade] %s build cost: %d ingredient lines."), Who, OutIngredients.Num());
    }

    /** Same trick for mProducedIn: take the array, not the object. */
    void FillProducedIn(TArray<TSoftClassPtr<UObject>>& OutProducedIn, UClass* BuildGunClass)
    {
        // ★1.0.7w 硬门禁（同 WT_FindCraftBenchProducers 的长注释）：mProducedIn 的元素必须实现
        //   IFGRecipeProducerInterface。非法类在这里被当成"没解析到"，走下面的软路径兜底 ——
        //   宁可配方挂不上去（玩家看不到，但不崩），也绝不让鼠标掠过产物就 Assert 崩游戏。
        if (BuildGunClass
            && !BuildGunClass->ImplementsInterface(UFGRecipeProducerInterface::StaticClass()))
        {
            WT_ERRF(TEXT("[shard] FillProducedIn: REFUSED %s (no IFGRecipeProducerInterface)."),
                    *BuildGunClass->GetPathName());
            BuildGunClass = nullptr;
        }
        if (!BuildGunClass)
        {
            // ★修复（用户实测「构造机/量子编码器里找不到强化碎片配方」）：调用方（WT_Find*Class）若在
            // 首帧解析失败返回 null，原实现直接 return，导致配方只剩构造函数里的裸软路径；软路径运行时
            // 若也解析不出，配方就不属于任何机器、机器 UI 永远不显示。这里兜底：把现有软路径里能解析出的
            // 类用上，确保配方至少挂到正确机器。
            for (const TSoftClassPtr<UObject>& Soft : OutProducedIn)
            {
                if (UClass* Resolved = Soft.Get())
                {
                    if (!Resolved->ImplementsInterface(UFGRecipeProducerInterface::StaticClass()))
                    {
                        WT_ERRF(TEXT("[shard] FillProducedIn: skipped soft path %s "
                                     "(no IFGRecipeProducerInterface)."), *Resolved->GetPathName());
                        continue;
                    }
                    BuildGunClass = Resolved;
                    break;
                }
            }
            if (!BuildGunClass)
            {
                WT_WARNF(TEXT("[shard] FillProducedIn: producer unresolved and no resolvable soft path -- recipe has no producer."));
                return;
            }
        }
        OutProducedIn.Empty();
        OutProducedIn.Add(TSoftClassPtr<UObject>(BuildGunClass));
    }

    /**
     * 原版能量碎片（Desc_CrystalShard）的类解析：先试常见路径，再按类名扫迭代器兜底。
     * 配方一/配方二共用；结果钉死 + 进 GC 台账（0.2.7 惯例）。
     */
    UClass* WT_FindVanillaShardClass()
    {
        // ★0.2.37 修复：守卫改为「**成功才锁定**」，与 UWTUpgradePowerShardDescriptor::ApplySinkPoints、
        // UWTShopPowerShardSchematic::ApplyShopSchematicData 同一条纪律。
        // 原实现在**尝试之前**就置 bTried=true —— 只要第一次调用时原版类还没加载好（时序/资产未就绪），
        // 本函数就会在整个进程内永久返回 nullptr，碎片配方永远停在 2:1 兜底，之后再怎么换世界也不重试。
        static UClass* VanillaShard = nullptr;
        static bool bTried = false;
        if (bTried)
        {
            return VanillaShard;
        }

        UClass* Found = StaticLoadClass(UFGItemDescriptor::StaticClass(), nullptr,
            TEXT("/Game/FactoryGame/Resource/Environment/Crystal/Desc_CrystalShard.Desc_CrystalShard_C"));
        if (!Found)
        {
            for (TObjectIterator<UClass> It; It; ++It)
            {
                if (It->IsChildOf(UFGItemDescriptor::StaticClass())
                    && It->GetName().Equals(TEXT("Desc_CrystalShard_C")))
                {
                    Found = *It;
                    break;
                }
            }
        }

        if (Found)
        {
            Found->AddToRoot();   // 0.2.7 台账：裸 UClass* 不是 GC 引用
            VanillaShard = Found;
            bTried = true;        // ★只有真解析到才算解决（失败留给下一次世界加载重试）
        }
        else
        {
            WT_WARNF(TEXT("[shard] vanilla Desc_CrystalShard not found -- shard recipes stay partial."));
        }
        return VanillaShard;
    }

    /**
     * ★0.2.37 构造机（Build_ConstructorMk1_C）类解析。
     *
     * 为什么必须显式解析：强化碎片配方 ①（构造机）的「在哪台机器里造」以前只在构造函数里塞一条裸软路径
     * `TSoftClassPtr<UObject>(FSoftObjectPath(".../Build_ConstructorMk1.Build_ConstructorMk1_C"))`。
     * 软路径在 CDO 构造期不解析，万一写错/资产改名，`mProducedIn` 就指向一个解析不出来的东西 ——
     * 配方不属于任何机器，机器 UI 里永远不显示它（玩家视角就是「无法制作」），而且**不报错**。
     * 这里与 WT_FindVanillaShardClass / WT_ResolveBuildGunClassForUpgrades 同一套路：
     * 先试权威路径，再按类名扫迭代器兜底，命中后钉死 + 进 GC 台账。
     */
    UClass* WT_FindConstructorMk1Class()
    {
        // ★0.2.37 修复：守卫改为「成功才锁定」（同 WT_FindVanillaShardClass）。
        // ① 号碎片配方（构造机）在 RegisterUpgradeRecipes（POST_INITIALIZATION）里靠这个类显式填 mProducedIn；
        // 若首次尝试失败就永久锁定，配方将只能依赖构造函数里的裸软路径 —— 软路径解析不出来时
        // 配方不属于任何机器（机器 UI 里永远不显示），而且一声不响。
        static UClass* Cached = nullptr;
        static bool bTried = false;
        if (bTried)
        {
            return Cached;
        }

        UClass* Found = StaticLoadClass(AFGBuildable::StaticClass(), nullptr,
            TEXT("/Game/FactoryGame/Buildable/Factory/ConstructorMk1/Build_ConstructorMk1.Build_ConstructorMk1_C"));
        if (!Found)
        {
            for (TObjectIterator<UClass> It; It; ++It)
            {
                if (It->IsChildOf(AFGBuildable::StaticClass())
                    && It->GetName().Equals(TEXT("Build_ConstructorMk1_C")))
                {
                    Found = *It;
                    break;
                }
            }
        }

        if (Found)
        {
            Found->AddToRoot();   // 0.2.7 台账：裸 UClass* 不是 GC 引用
            Cached = Found;
            bTried = true;        // ★只有真解析到才算解决（失败留给下一次世界加载重试）
        }
        else
        {
            WT_WARNF(TEXT("[shard] Build_ConstructorMk1_C not found -- shard recipes have no producer."));
        }
        return Cached;
    }
}

// ----------------------------------------------------------------------------
// Recipes
// ----------------------------------------------------------------------------

UWTUpgradePowerStorageRecipe::UWTUpgradePowerStorageRecipe()
{
    // Buildings are placed, not manufactured: zero duration is what every vanilla building recipe
    // uses, and a non-zero one would make the build gun show a progress bar for an instant action.
    mManufactoringDuration = 0.0f;
    mProduct.Add(FItemAmount(UWTUpgradePowerStorageDescriptor::StaticClass(), 1));
}

void UWTUpgradePowerStorageRecipe::ApplyProducedIn(UClass* BuildGunClass)
{
    // mProducedIn is protected: naming it here is legal because this is a member of a UFGRecipe
    // subclass. The shared logic lives in FillProducedIn, which is handed the array.
    FillProducedIn(mProducedIn, BuildGunClass);
}

void UWTUpgradePowerStorageRecipe::ApplyIngredients()
{
    FillIngredients(mIngredients, GStorageUnitCost, UE_ARRAY_COUNT(GStorageUnitCost), TEXT("storage unit"));
}

UWTUpgradeStorageRecipe::UWTUpgradeStorageRecipe()
{
    mManufactoringDuration = 0.0f;
    mProduct.Add(FItemAmount(UWTUpgradeStorageDescriptor::StaticClass(), 1));
}

void UWTUpgradeStorageRecipe::ApplyProducedIn(UClass* BuildGunClass)
{
    FillProducedIn(mProducedIn, BuildGunClass);
}

void UWTUpgradeStorageRecipe::ApplyIngredients()
{
    // ★0.2.36：原版位面存储站上传器用料同顺序（0.2.45 起去掉梅瑟尔球）+ 1 强化能量碎片（用户定案）。
    FillIngredients(mIngredients, GUploadStationCost, UE_ARRAY_COUNT(GUploadStationCost),
                    TEXT("upload station"));
    mIngredients.Add(FItemAmount(UWTUpgradePowerShardDescriptor::StaticClass(), 1));
}

UWTUpgradeCloudDownloaderRecipe::UWTUpgradeCloudDownloaderRecipe()
{
    mManufactoringDuration = 0.0f;
    mProduct.Add(FItemAmount(UWTUpgradeCloudDownloaderDescriptor::StaticClass(), 1));
}

void UWTUpgradeCloudDownloaderRecipe::ApplyProducedIn(UClass* BuildGunClass)
{
    FillProducedIn(mProducedIn, BuildGunClass);
}

void UWTUpgradeCloudDownloaderRecipe::ApplyIngredients()
{
    FillIngredients(mIngredients, GDownloaderCost, UE_ARRAY_COUNT(GDownloaderCost),
                    TEXT("cloud downloader"));
    mIngredients.Add(FItemAmount(UWTUpgradePowerShardDescriptor::StaticClass(), 1));
}

UWTUpgradeCloudFluidUploaderRecipe::UWTUpgradeCloudFluidUploaderRecipe()
{
    mManufactoringDuration = 0.0f;
    mProduct.Add(FItemAmount(UWTUpgradeCloudFluidUploaderDescriptor::StaticClass(), 1));
}

void UWTUpgradeCloudFluidUploaderRecipe::ApplyProducedIn(UClass* BuildGunClass)
{
    FillProducedIn(mProducedIn, BuildGunClass);
}

void UWTUpgradeCloudFluidUploaderRecipe::ApplyIngredients()
{
    FillIngredients(mIngredients, GUploadStationCost, UE_ARRAY_COUNT(GUploadStationCost),
                    TEXT("cloud fluid uploader"));
    mIngredients.Add(FItemAmount(UWTUpgradePowerShardDescriptor::StaticClass(), 1));
}

UWTUpgradeCloudFluidDownloaderRecipe::UWTUpgradeCloudFluidDownloaderRecipe()
{
    mManufactoringDuration = 0.0f;
    mProduct.Add(FItemAmount(UWTUpgradeCloudFluidDownloaderDescriptor::StaticClass(), 1));
}

void UWTUpgradeCloudFluidDownloaderRecipe::ApplyProducedIn(UClass* BuildGunClass)
{
    FillProducedIn(mProducedIn, BuildGunClass);
}

void UWTUpgradeCloudFluidDownloaderRecipe::ApplyIngredients()
{
    FillIngredients(mIngredients, GDownloaderCost, UE_ARRAY_COUNT(GDownloaderCost),
                    TEXT("cloud fluid downloader"));
    mIngredients.Add(FItemAmount(UWTUpgradePowerShardDescriptor::StaticClass(), 1));
}



// ----------------------------------------------------------------------------
// ★0.2.39 强化能量碎片配方（用户最终定案：**两条**）。
//   ① UWTUpgradePowerShardRecipe      —— 构造机：**2 × 原版能量碎片 → 1 × 强化碎片**（6s）。
//   ② UWTUpgradePowerShardSynthRecipe —— 量子编码器：镜像原版 `Recipe_SyntheticPowerShard`
//      （原版「合成能量碎片」）的**全部材料 ×2**（含气体）→ 强化碎片 ×1，**无副产**（12s）。
//   * 原版三条构造机碎片配方（Recipe_PowerCrystalShard_1/2/3，蓝/黄/紫 Slug）×2 镜像
//     曾是 0.2.36h~0.2.38 的做法，已退役 —— 两个类保留成空壳（**不要删**，见头文件）。
//   * 「制造机合成配方」在 0.2.36h 就已移除。
// ----------------------------------------------------------------------------

namespace
{
    /**
     * 原版碎片配方解析（先试路径，再按类名扫迭代器兜底；模式与 WT_FindVanillaShardClass 相同）。
     * Index 1/2/3 = 蓝/黄/紫 Slug 三条配方。
     *
     * 注：0.2.39 起只有 Index 1（蓝 Slug）仍在使用 —— ApplySinkPoints 读原版碎片售价时用它
     * 取产物描述符。三条能力保留（多档原版配方共用同一形状，删索引分支只会徒增差异）。
     */
    UClass* WT_FindVanillaShardRecipeClass(int32 Index)
    {
        static UClass* Cached[3] = { nullptr, nullptr, nullptr };
        static bool     bTried[3] = { false, false, false };
        if (Index < 1 || Index > 3)
        {
            return nullptr;
        }
        UClass*& CachedRef = Cached[Index - 1];
        if (bTried[Index - 1])
        {
            return CachedRef;
        }

        // ★0.2.37 修复：守卫改为「成功才锁定」（同 WT_FindVanillaShardClass）。
        // 本函数被 ApplySinkPoints（CONSTRUCTION 阶段，最早的阶段 —— 那时原版资产**可能还没加载好**）
        // 直接调用；入口预置锁会把「那一次失败」永久化，连带把镜像配方与回收点数一起钉死在兜底值。
        const FString Name = FString::Printf(TEXT("Recipe_PowerCrystalShard_%d"), Index);
        const FString Path = FString::Printf(
            TEXT("/Game/FactoryGame/Recipes/Constructor/%s.%s_C"), *Name, *Name);
        UClass* Found = StaticLoadClass(UFGRecipe::StaticClass(), nullptr, *Path);
        if (!Found)
        {
            for (TObjectIterator<UClass> It; It; ++It)
            {
                if (It->IsChildOf(UFGRecipe::StaticClass()) && It->GetName().Equals(Name))
                {
                    Found = *It;
                    break;
                }
            }
        }

        if (Found)
        {
            Found->AddToRoot();   // 0.2.7 台账：裸 UClass* 不是 GC 引用
            CachedRef = Found;
            bTried[Index - 1] = true;   // ★只有真解析到才算解决（失败留给下一次世界加载重试）
        }
        else
        {
            WT_WARNF(TEXT("[shard] vanilla recipe '%s' not found -- shard recipe falls back to 2:1."), *Name);
        }
        return CachedRef;
    }

    /**
     * ★0.2.39 原版「合成能量碎片」配方（量子编码器用的 Recipe_SyntheticPowerShard）的类解析。
     * ② 号配方要镜像它的全部材料。finder 家族纪律：**成功才锁定** + GC 钉死（见铁律）。
     */
    UClass* WT_FindVanillaSynthRecipeClass()
    {
        static UClass* Cached = nullptr;
        static bool bTried = false;
        if (bTried)
        {
            return Cached;
        }

        UClass* Found = StaticLoadClass(UFGRecipe::StaticClass(), nullptr,
            TEXT("/Game/FactoryGame/Recipes/QuantumEncoder/Recipe_SyntheticPowerShard"
                 ".Recipe_SyntheticPowerShard_C"));
        if (!Found)
        {
            for (TObjectIterator<UClass> It; It; ++It)
            {
                if (It->IsChildOf(UFGRecipe::StaticClass())
                    && It->GetName().Equals(TEXT("Recipe_SyntheticPowerShard_C")))
                {
                    Found = *It;
                    break;
                }
            }
        }

        if (Found)
        {
            Found->AddToRoot();   // 0.2.7 台账：裸 UClass* 不是 GC 引用
            Cached = Found;
            bTried = true;        // ★只有真解析到才算解决
        }
        else
        {
            WT_WARNF(TEXT("[shard] vanilla Recipe_SyntheticPowerShard not found -- synth mirror unavailable."));
        }
        return Cached;
    }

    /**
     * ★0.2.39 量子编码器（Build_QuantumEncoder_C）类解析 —— ② 号配方的生产机器。
     * 与 WT_FindConstructorMk1Class 同一套路：先权威路径，再按类名扫迭代器兜底，命中后钉死。
     */
    UClass* WT_FindQuantumEncoderClass()
    {
        static UClass* Cached = nullptr;
        static bool bTried = false;
        if (bTried)
        {
            return Cached;
        }

        UClass* Found = StaticLoadClass(AFGBuildable::StaticClass(), nullptr,
            TEXT("/Game/FactoryGame/Buildable/Factory/QuantumEncoder/Build_QuantumEncoder"
                 ".Build_QuantumEncoder_C"));
        if (!Found)
        {
            for (TObjectIterator<UClass> It; It; ++It)
            {
                if (It->IsChildOf(AFGBuildable::StaticClass())
                    && It->GetName().Equals(TEXT("Build_QuantumEncoder_C")))
                {
                    Found = *It;
                    break;
                }
            }
        }

        if (Found)
        {
            Found->AddToRoot();   // 0.2.7 台账：裸 UClass* 不是 GC 引用
            Cached = Found;
            bTried = true;        // ★只有真解析到才算解决
        }
        else
        {
            WT_WARNF(TEXT("[shard] quantum encoder class unresolved -- synth recipe keeps soft path only."));
        }
        return Cached;
    }

    /**
     * ★1.0.7 制作台（手工台）生产者类解析 —— ③ 号配方的 mProducedIn 候选集合。
     *
     * ## 为什么不能只解析「一个类」（一手核实 FGRecipe.h / FGWorkBench.h）
     * `mProducedIn` 的元素类型是 **TSoftClassPtr<UObject>**（FGRecipe.h:181，
     * `Meta = (MustImplement = "FGRecipeProducerInterface")`），原版允许两种写法：
     *   ① **建筑类** `Build_WorkBench_C` / `Build_WorkBenchIntegrated_C`
     *      —— 构造机就是走这条（已实测可用：碎片配方在构造机里能看到）。
     *   ② **工作台组件 BP 类**（`UFGWorkBench` 自身实现 IFGRecipeProducerInterface，FGWorkBench.h:16；
     *      utoc 名表里实证有 `BP_WorkBenchComponent.uasset`）。
     *
     * ★★1.0.7w 已**删除**原第 ② 项「建筑物品描述符 Desc_WorkBench_C」—— 那是错的：
     *   它只是 UFGItemDescriptor 子类，不实现 IFGRecipeProducerInterface，而元数据要求实现。
     *   写进 mProducedIn 后原版 tooltip 会 Execute_ 崩游戏（2026-10-27 10:52 实测崩溃，
     *   [shard-probe] 日志实证当时 6 条 producer，其中 2 条就是描述符）。
     *
     * 到底哪一项匹配，只能靠运行时 `AFGRecipeManager::GetAvailableRecipesForProducer` 反查
     * （探针在 RegisterUpgradeRecipes 里，日志前缀 `[shard-probe]`）。
     * ⇒ 三套全挂：多挂的项不会被 RecipeManager 报错（它只是遍历比较），只要一项命中配方就出现。
     *
     * 解析顺序：权威资产路径 → 按类名扫迭代器兜底（四个 uasset 名均已在 utoc 名表实锤）。
     * 命中即 AddToRoot（0.2.7 台账：裸 UClass* 不是 GC 引用）。全失败返回空数组，调用方保留软路径。
     */
    void WT_FindCraftBenchProducers(TArray<UClass*>& OutClasses)
    {
        OutClasses.Reset();

        // ★★1.0.7w 硬门禁：mProducedIn 的元素**必须实现 IFGRecipeProducerInterface**。
        //
        // 一手依据：`UFGRecipe::mProducedIn` 的声明是
        //   `UPROPERTY( Meta = (MustImplement = "FGRecipeProducerInterface") )`
        //   （FGRecipe.h:181，SML SDK 里逐字可查）。
        // 这个 meta 不只是编辑器提示 —— 原版运行时**到处**会直接
        //   `IFGRecipeProducerInterface::Execute_GetRecipeProducerItemDescriptor(Producer->GetDefaultObject(), ...)`
        // 而不预先判空/判接口，`Execute_` 内部第一行就是
        //   `checkf(O->GetClass()->ImplementsInterface(UFGRecipeProducerInterface::StaticClass()), ...)`
        //   （FGRecipeProducerInterface.gen.cpp:46）。
        //
        // 实测代价（2026-10-27 10:52 崩溃包 UECC-Windows-16C0CD1A...）：
        //   我们曾把 **`Desc_WorkBench_C` / `Desc_WorkBenchIntegrated_C`（UFGItemDescriptor 子类）**
        //   塞进 recipe3 的 mProducedIn（[shard-probe] 日志实证 6 条），它们不实现该接口。
        //   原先以为「RecipeManager 反查只是比较指针、不崩」是对的，但**原版物品 tooltip**
        //   会遍历产物的所有配方的 mProducedIn 逐个 Execute_ 取产出图标 ⇒
        //   玩家鼠标掠过强化碎片的那一刻直接 Assert 崩游戏。功能"能用"≠"合法"。
        auto AddCandidate = [&OutClasses](UClass* C)
        {
            if (!C)
            {
                return;
            }
            if (!C->ImplementsInterface(UFGRecipeProducerInterface::StaticClass()))
            {
                // 宁可少一个候选（配方挂不上去 = 玩家看不到，但不崩），
                // 也绝不把非法类写进 mProducedIn（= 鼠标一靠近就崩游戏）。
                WT_WARNF(TEXT("[shard] REFUSED producer candidate %s: does not implement "
                              "IFGRecipeProducerInterface (would crash the vanilla tooltip)."),
                         *C->GetPathName());
                return;
            }
            C->AddToRoot();
            OutClasses.AddUnique(C);
        };

        // ① 建筑类（可建造制作台 / 枢纽内置制作台）。
        AddCandidate(StaticLoadClass(AFGBuildable::StaticClass(), nullptr,
            TEXT("/Game/FactoryGame/Buildable/Factory/WorkBench/Build_WorkBench.Build_WorkBench_C")));
        AddCandidate(StaticLoadClass(AFGBuildable::StaticClass(), nullptr,
            TEXT("/Game/FactoryGame/Buildable/Factory/WorkBench/Build_WorkBenchIntegrated"
                 ".Build_WorkBenchIntegrated_C")));

        // ② 建筑物品描述符 —— ★1.0.7w 已**删除**（原来此处解析 Desc_WorkBench_C /
        //    Desc_WorkBenchIntegrated_C 并挂进 mProducedIn）。它们只是物品描述符，
        //    不实现 IFGRecipeProducerInterface，是 10:52 那次 tooltip 崩溃的直接来源。
        //    当时"线索"（`UFGWorkBench::GetRecipeProducerItemDescriptor_Implementation` 返回
        //    ItemDescriptor）被误读成"producer 判据走描述符"：那个返回值只是「我这个生产者对应
        //    哪个物品图标」，与 mProducedIn 的类型约束毫无关系。

        // ③ 工作台组件 BP 类（路径为推断；解析不到由下面的名字扫描兜住）。
        AddCandidate(StaticLoadClass(UFGWorkBench::StaticClass(), nullptr,
            TEXT("/Game/FactoryGame/Buildable/Factory/WorkBench/BP_WorkBenchComponent"
                 ".BP_WorkBenchComponent_C")));

        // ---- 名字兜底：补齐上面没解析到的（按类名精确匹配，绝不宽泛 Contains 收错类）----
        for (TObjectIterator<UClass> It; It; ++It)
        {
            UClass* C = *It;
            if (C->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
            {
                continue;
            }
            const FString Name = C->GetName();
            if (Name.Contains(TEXT("Automated")))   // 自动化装备工作台是另一套机器，不能挂进来
            {
                continue;
            }

            const bool bBenchBuildable =
                C->IsChildOf(AFGBuildable::StaticClass())
                && (Name.Equals(TEXT("Build_WorkBench_C"))
                    || Name.Equals(TEXT("Build_WorkBenchIntegrated_C")));

            // ★1.0.7w 名字兜底里**也**不再收物品描述符（Desc_WorkBench_C 一类）：
            //   与上面 ② 同一个理由 —— 它们不实现 IFGRecipeProducerInterface。
            const bool bBenchComponent =
                C->IsChildOf(UFGWorkBench::StaticClass())
                && Name.Contains(TEXT("WorkBench"));

            if (bBenchBuildable || bBenchComponent)
            {
                AddCandidate(C);
            }
        }

        for (UClass* C : OutClasses)
        {
            WT_LOGF(TEXT("[shard] craft bench producer candidate: %s"), *C->GetPathName());
        }
        if (OutClasses.Num() == 0)
        {
            WT_WARNF(TEXT("[shard] no craft bench producer class resolved -- recipe3 keeps soft paths only."));
        }
    }

    /**
     * ★0.2.39 镜像原版「合成能量碎片」配方：**全部材料 ×2**、时长沿用原版。
     *
     * 产物**刻意不镜像**：我们的产物只有强化碎片 ×1，而且**丢掉**原版那条配方的
     * 暗物质残渣副产（用户定案「编码器里的强化版能量碎片不再有残渣」）。
     * 返回 false = 原版配方没解析到（调用方走「2 × 原版碎片」兜底 + 保持构造机之外的软路径）。
     */
    bool WT_BuildMirroredSynthRecipe(TArray<FItemAmount>& OutIngredients,
                                     float& OutDuration, FString& OutDbg)
    {
        OutIngredients.Empty();
        OutDuration = 12.0f;
        OutDbg.Empty();

        UClass* Vanilla = WT_FindVanillaSynthRecipeClass();
        const UFGRecipe* R = Vanilla ? Cast<UFGRecipe>(Vanilla->GetDefaultObject()) : nullptr;
        if (!R)
        {
            return false;
        }

        for (const FItemAmount& A : R->GetIngredients())
        {
            if (!A.ItemClass || A.Amount <= 0)
            {
                continue;
            }
            OutIngredients.Add(FItemAmount(A.ItemClass, A.Amount * 2));   // ★材料双倍（含气体）
        }
        OutDuration = R->GetManufacturingDuration();

        for (const FItemAmount& A : OutIngredients)
        {
            OutDbg += FString::Printf(TEXT("%dx%s "), A.Amount, *A.ItemClass->GetName());
        }
        return OutIngredients.Num() > 0;
    }
}

UWTUpgradePowerShardRecipe::UWTUpgradePowerShardRecipe()
{
    // ① 构造机生产：**2 × 原版能量碎片 → 1 × 强化碎片**（材料在 ApplyIngredients 里从原版碎片类填）。
    // ★0.2.37：这里只留一条软路径**兜底**，权威来源是 RegisterUpgradeRecipes 里对
    //          ApplyProducedIn(WT_FindConstructorMk1Class()) 的显式调用。
    mManufactoringDuration = 6.0f;
    mProduct.Add(FItemAmount(UWTUpgradePowerShardDescriptor::StaticClass(), 1));
    mProducedIn.Add(TSoftClassPtr<UObject>(FSoftObjectPath(TEXT(
        "/Game/FactoryGame/Buildable/Factory/ConstructorMk1/Build_ConstructorMk1.Build_ConstructorMk1_C"))));
}

void UWTUpgradePowerShardRecipe::ApplyProducedIn(UClass* ProducerClass)
{
    // mProducedIn is protected; naming it here is legal because this is a member of a UFGRecipe
    // subclass. Shared logic lives in FillProducedIn.
    FillProducedIn(mProducedIn, ProducerClass);
}

void UWTUpgradePowerShardRecipe::ApplyIngredients()
{
    // ★0.2.39 用户最终定案（两条配方之 ①）：**构造机里 2 × 原版能量碎片 → 1 × 强化碎片**。
    // 不再镜像原版蓝/黄/紫 Slug 配方（那是 0.2.36h~0.2.38 的三配方，已退役）。
    const float Duration = 6.0f;

    if (UClass* VanillaShard = WT_FindVanillaShardClass())
    {
        mIngredients.Empty();
        mIngredients.Add(FItemAmount(TSubclassOf<UFGItemDescriptor>(VanillaShard), 2));   // ★原版碎片 ×2
        mManufactoringDuration = Duration;
        mProduct.Empty();
        mProduct.Add(FItemAmount(UWTUpgradePowerShardDescriptor::StaticClass(), 1));
        WT_LOGF(TEXT("[shard] recipe1 (constructor): 2 x %s -> 1 enhanced shard."),
                *VanillaShard->GetName());
    }
    else
    {
        // 原版碎片类解析不到 ⇒ 材料无从可得。**必须停用整条配方**：
        // 只清 mIngredients 会留下「零材料无限产出」的配方（比解析失败本身更糟）。
        mIngredients.Empty();
        mProduct.Empty();
        mProducedIn.Empty();
        WT_WARNF(TEXT("[shard] recipe1 DISABLED: vanilla shard class unresolved "
                      "-- no ingredients can be expressed."));
    }
}

// ----------------------------------------------------------------------------
// ★0.2.39 退役空壳（详见头文件注释 —— **这两个类不能删**）。
//
// 0.2.36h~0.2.38 期间它们是「原版黄/紫 Slug 构造机配方材料×2 镜像」，一直被写进 T1 里程碑的
// 解锁链，于是老存档的 AFGRecipeManager::mAvailableRecipes（UPROPERTY(SaveGame)，只增不减）
// 里存着它们的类路径。用户 2026-10-08 定案配方回到**两条**（构造机 + 编码器）后它们退役，
// 但类必须留成空壳：删掉就会重演 0.2.37 那次「读档 / 开机器配方列表必崩」（TSubclassOf 落
// 成 nullptr，引擎遍历到空元素即 Access Violation）。
//
// 处置（与当年 SynthRecipe 完全一样）：
//   * 构造体**留空** —— 不填 mProduct / mIngredients / mProducedIn / 时长；
//   * 不进任何解锁链（Remote_FactoryRegistry.cpp::AddUnlockRecipes 里已无它们）；
//   * 由 WT_RepairRecipeAvailability 主动从 mAvailableRecipes 摘除，让老存档彻底干净。
// ----------------------------------------------------------------------------
UWTUpgradePowerShardRecipe2::UWTUpgradePowerShardRecipe2()
{
    // 故意留空。什么都不要填！
}

UWTUpgradePowerShardRecipe3::UWTUpgradePowerShardRecipe3()
{
    // 故意留空。什么都不要填！
}

// ----------------------------------------------------------------------------
// ★0.2.39 复活为「② 编码器配方」（0.2.37~0.2.38 期间它是一具空壳 —— 见上面的同类说明）。
//
// 在量子编码器（Build_QuantumEncoder_C）里镜像原版 `Recipe_SyntheticPowerShard`
// （原版「合成能量碎片」= 时间水晶×2 + 暗物质×2 + 石英×12 + 光子物质 12m³，12s）的
// **全部材料 ×2**（含气体：光子物质 24m³）。产物只有强化碎片 ×1，
// **不带**原版那条配方的暗物质残渣副产（用户定案「编码器里的强化版能量碎片不再有残渣」）。
//
// 材料在 ApplyIngredients 里**运行时**从原版配方解析（原版改数值我们自动跟随）；
// 构造函数只留软路径兜底，权威来源是 RegisterUpgradeRecipes 里对
// ApplyProducedIn(WT_FindQuantumEncoderClass()) 的显式调用。
// ----------------------------------------------------------------------------
UWTUpgradePowerShardSynthRecipe::UWTUpgradePowerShardSynthRecipe()
{
    mManufactoringDuration = 12.0f;
    mProduct.Add(FItemAmount(UWTUpgradePowerShardDescriptor::StaticClass(), 1));
    mProducedIn.Add(TSoftClassPtr<UObject>(FSoftObjectPath(TEXT(
        "/Game/FactoryGame/Buildable/Factory/QuantumEncoder/Build_QuantumEncoder.Build_QuantumEncoder_C"))));
}

void UWTUpgradePowerShardSynthRecipe::ApplyProducedIn(UClass* ProducerClass)
{
    FillProducedIn(mProducedIn, ProducerClass);
}

void UWTUpgradePowerShardSynthRecipe::ApplyIngredients()
{
    TArray<FItemAmount> Ingredients;
    float Duration = 12.0f;
    FString Dbg;

    if (WT_BuildMirroredSynthRecipe(Ingredients, Duration, Dbg))
    {
        mIngredients           = MoveTemp(Ingredients);
        mManufactoringDuration = Duration;
        mProduct.Empty();
        // ★产物刻意不镜像：只有强化碎片 ×1，没有暗物质残渣（用户定案）。
        mProduct.Add(FItemAmount(UWTUpgradePowerShardDescriptor::StaticClass(), 1));
        WT_LOGF(TEXT("[shard] recipe2 (quantum encoder, x2 materials): %s-> 1 enhanced shard "
                     "(no residue)."), *Dbg);
    }
    else
    {
        // 兜底：原版合成配方解析不到 ⇒ 退回与 ① 同构的「2 × 原版碎片 → 1 强化碎片」
        // （配方仍可造，且绝不出现零材料无限产出）。
        if (UClass* VanillaShard = WT_FindVanillaShardClass())
        {
            mIngredients.Empty();
            mIngredients.Add(FItemAmount(TSubclassOf<UFGItemDescriptor>(VanillaShard), 2));
            WT_WARNF(TEXT("[shard] recipe2 fallback: 2 x vanilla shard -> 1 enhanced shard."));
        }
        else
        {
            mIngredients.Empty();
            mProduct.Empty();
            mProducedIn.Empty();
            WT_WARNF(TEXT("[shard] recipe2 DISABLED: neither synth recipe nor vanilla shard resolved."));
        }
    }
}

// ----------------------------------------------------------------------------
// ★1.0.7 强化能量碎片 · ③ 制作台（手工台）配方 —— 用户 2026-10-26 定案新增。
//   2 × 原版能量碎片 → 1 × 强化碎片，手搓 6s（与构造机同比例、同耗时；区别只是不用建机器）。
//   材料表**不镜像**任何原版配方：只需要解析到原版碎片类（WT_FindVanillaShardClass）。
//   mProducedIn 挂两个制作台类（独立制作台 / 枢纽内置制作台），任一解析成功即可用。
// ----------------------------------------------------------------------------
UWTUpgradePowerShardBenchRecipe::UWTUpgradePowerShardBenchRecipe()
{
    mManufactoringDuration = 6.0f;
    // ★1.0.7 手搓倍率显式设 1.0：手搓时间 = mManufactoringDuration × mManualManufacturingMultiplier
    //   （FGRecipe.h:130）。基类 CDO 的这个默认值在 SML 里读不到实现（Private 是空桩），
    //   若它是 0 就会「一点即成」——碎片变成无限刷，经济性直接崩。这里钉死 1.0 = 手搓 6 秒。
    mManualManufacturingMultiplier = 1.0f;
    // 菜单顺序靠前（负值更靠前，FGRecipe.h:166 注释）。
    mManufacturingMenuPriority = -10.0f;
    mProduct.Add(FItemAmount(UWTUpgradePowerShardDescriptor::StaticClass(), 1));

    // 软路径兜底（**只写两条建筑类**）。
    // 权威来源 = RegisterUpgradeRecipes 里对 ApplyProducedIn(WT_FindCraftBenchProducers(...)) 的显式调用。
    //
    // ★1.0.7w 删掉原第 3/4 条 `Desc_WorkBench_C` / `Desc_WorkBenchIntegrated_C`：
    //   它们是 UFGItemDescriptor 子类，**不实现 IFGRecipeProducerInterface**，而
    //   `mProducedIn` 声明带 `Meta=(MustImplement="FGRecipeProducerInterface")`（FGRecipe.h:181）。
    //   原版物品 tooltip 遍历产物的 mProducedIn 时直接 Execute_ 取产出图标 ⇒ 这类"软路径"
    //   一旦被解析就必崩（2026-10-27 10:52 实测崩溃）。既然非法，就不能留在兜底里。
    mProducedIn.Add(TSoftClassPtr<UObject>(FSoftObjectPath(TEXT(
        "/Game/FactoryGame/Buildable/Factory/WorkBench/Build_WorkBench.Build_WorkBench_C"))));
    mProducedIn.Add(TSoftClassPtr<UObject>(FSoftObjectPath(TEXT(
        "/Game/FactoryGame/Buildable/Factory/WorkBench/Build_WorkBenchIntegrated.Build_WorkBenchIntegrated_C"))));
}

void UWTUpgradePowerShardBenchRecipe::ApplyProducedIn(const TArray<UClass*>& ProducerClasses)
{
    // ★1.0.7w 最后一道门：这里再过滤一次接口实现，不依赖调用方（WT_FindCraftBenchProducers）
    //   已经筛过。理由见该函数里的长注释 —— 非法类写进 mProducedIn 会让原版 tooltip
    //   在 `Execute_GetRecipeProducerItemDescriptor` 里 Assert 崩游戏，属于"必须双保险"的坑。
    TArray<TSoftClassPtr<UObject>> Resolved;
    for (UClass* C : ProducerClasses)
    {
        if (!C)
        {
            continue;
        }
        if (!C->ImplementsInterface(UFGRecipeProducerInterface::StaticClass()))
        {
            WT_ERRF(TEXT("[shard] recipe3: DROPPED producer %s (no IFGRecipeProducerInterface) "
                         "-- writing it would crash the vanilla tooltip."), *C->GetPathName());
            continue;
        }
        Resolved.Add(TSoftClassPtr<UObject>(C));
    }

    if (Resolved.Num() == 0)
    {
        // ★纪律（同 FillProducedIn）：解析失败时**保留**构造函数里的两条软路径，不清空 ——
        // 清空 mProducedIn = 配方不属于任何机器，玩家在制作台里永远看不到，而且不报错。
        WT_WARNF(TEXT("[shard] bench class unresolved -- recipe3 keeps the two soft paths only."));
        return;
    }

    mProducedIn = MoveTemp(Resolved);
    WT_LOGF(TEXT("[shard] recipe3 producer set: %d bench class(es)."), mProducedIn.Num());
}

void UWTUpgradePowerShardBenchRecipe::ApplyIngredients()
{
    if (UClass* VanillaShard = WT_FindVanillaShardClass())
    {
        mIngredients.Empty();
        mIngredients.Add(FItemAmount(TSubclassOf<UFGItemDescriptor>(VanillaShard), 2));   // 原版碎片 ×2
        mManufactoringDuration = 6.0f;
        mManualManufacturingMultiplier = 1.0f;   // 手搓 6 秒（见构造函数注释）
        mProduct.Empty();
        mProduct.Add(FItemAmount(UWTUpgradePowerShardDescriptor::StaticClass(), 1));
        WT_LOGF(TEXT("[shard] recipe3 (craft bench): 2 x %s -> 1 enhanced shard."),
                *VanillaShard->GetName());
    }
    else
    {
        // 原版碎片类解析不到 ⇒ 停用整条配方（绝不能留下「零材料无限产出」的配方）。
        mIngredients.Empty();
        mProduct.Empty();
        mProducedIn.Empty();
        WT_WARNF(TEXT("[shard] recipe3 DISABLED: vanilla shard class unresolved."));
    }
}

// ----------------------------------------------------------------------------
// ★0.2.36h（用户定案「它可以被回收」）：回收点数 = 原版能量碎片 ×2。
// 原版数值在运行时从原版碎片配方 1 的**产物描述符** CDO 读（反射读 —— mResourceSinkPoints 是
// 基类保护成员，对本类之外的 CDO 只能走反射）；原版改数值我们自动跟随。读不到兜底 150000。
// 幂等（static 守卫）；由 GameWorldModule 的 CONSTRUCTION 阶段调用（那时加载原版类是安全的）。
// ----------------------------------------------------------------------------
void UWTUpgradePowerShardDescriptor::ApplySinkPoints()
{
    // ★0.2.37：守卫改为「**成功才锁定**」。
    // 原实现进门就 bDone=true —— 如果第一次调用时原版类还没加载（时序/资产未就绪导致
    // WT_FindVanillaShardRecipeClass 返回 null），就会永久停留在 fallback 值，再也不重试。
    // 现在只有真读到原版数值才置位，读不到就留着下一次世界加载再算（幂等，代价一次反射读）。
    static bool bDone = false;
    if (bDone)
    {
        return;
    }

    int32 Vanilla = 0;
    if (UClass* Recipe = WT_FindVanillaShardRecipeClass(1))
    {
        if (const UFGRecipe* R = Cast<UFGRecipe>(Recipe->GetDefaultObject()))
        {
            for (const FItemAmount& A : R->GetProducts())
            {
                if (A.ItemClass && A.Amount > 0)
                {
                    if (UObject* CDO = A.ItemClass->GetDefaultObject())
                    {
                        if (FIntProperty* IP = CastField<FIntProperty>(
                                UFGItemDescriptor::StaticClass()->FindPropertyByName(TEXT("mResourceSinkPoints"))))
                        {
                            Vanilla = *IP->ContainerPtrToValuePtr<int32>(CDO);
                        }
                    }
                    break;
                }
            }
        }
    }

    mResourceSinkPoints = (Vanilla > 0) ? Vanilla * 2 : 150000;
    bDone = (Vanilla > 0);   // ★只有读到真实原版数值才算成功
    if (Vanilla <= 0)
    {
        WT_WARNF(TEXT("[shard] vanilla sink points unreadable -- using fallback 150000 (will retry next world)."));
    }
    else
    {
        WT_WARNF(TEXT("[shard] sink points = %d (vanilla shard = %d; recycle enabled when > 0)."),
                 mResourceSinkPoints, Vanilla);
    }
}

// ----------------------------------------------------------------------------
// ★0.2.36h（用户定案「可以出现AMS商店」）：AWESOME 商店条目。
//   * 类型 EST_ResourceSink：商店按类型枚举 schematic（原版样例
//     ResourceSink_SyntheticPowerShard）；把本类注册进 GameWorldModule::mSchematics 即上架。
//   * 售价 = 50,000 代价券 / 块（用户 2026-10-07 定案），每次购买 1 块、可重复购买。
//   * 发货走 UWTUnlockGiveItemShard（UFGUnlockGiveItem 是 abstract，必须子类 —— 引擎头实测）。
//   * 字段全部在 ApplyShopSchematicData()（CONSTRUCTION 阶段）里填 —— 构造函数不动资产，
//     与里程碑 ApplyMilestoneData 同一套时序纪律。
// ----------------------------------------------------------------------------
void UWTShopPowerShardSchematic::ApplyShopSchematicData()
{
    // ★0.2.37：守卫改为「**成功才锁定**」（同 ApplySinkPoints）。首次调用时原版资产可能还没
    // 加载好，旧实现会在失败后永久放弃；现在失败了下一次世界加载会重算。
    // 重跑前必须摘掉上一次挂上去的 Give-Item unlock，否则 mUnlocks 会累积重复实例。
    static bool bDone = false;
    if (bDone)
    {
        return;
    }

    mUnlocks.RemoveAll([](const TObjectPtr<UFGUnlock>& U)
    {
        return !U || U->IsA(UWTUnlockGiveItemShard::StaticClass());
    });

    mType             = ESchematicType::EST_ResourceSink;
    mDisplayName      = WT_LOCTEXT("ShopPowerShard_Name", "Enhanced Power Shard");
    mDescription      = WT_LOCTEXT("ShopPowerShard_Desc",
        "Buy an Enhanced Power Shard with FICSIT coupons. One purchase = one shard; "
        "purchase as many as you need.");
    mTechTier         = 8;         // Tier 9（与原版合成能量碎片同档，纯分组用）
    mMenuPriority     = 2000.f;
    mTimeToComplete   = 1.0f;
    mIsPlayerSpecific = false;
    mDependenciesBlocksSchematicAccess  = false;
    mHiddenUntilDependenciesMet         = false;

    bool bConfigured = true;

    // 售价：25,000 代价券（★1.0.7 用户 2026-10-26 定案，原 50,000 半价）。
    if (UClass* Coupon = StaticLoadClass(UFGItemDescriptor::StaticClass(), nullptr,
            TEXT("/Game/FactoryGame/Resource/Parts/ResourceSinkCoupon/Desc_ResourceSinkCoupon.Desc_ResourceSinkCoupon_C")))
    {
        Coupon->AddToRoot();   // 0.2.7 台账：裸 UClass* 不是 GC 引用
        mCost.Empty();
        mCost.Add(FItemAmount(TSubclassOf<UFGItemDescriptor>(Coupon), 25000));
    }
    else
    {
        bConfigured = false;
        WT_WARNF(TEXT("[shop] coupon descriptor not found -- shop entry price stays empty."));
    }

    // 购买发货：1 块强化碎片。
    if (UWTUnlockGiveItemShard* Give = NewObject<UWTUnlockGiveItemShard>(this))
    {
        TArray<FItemAmount> Items;
        Items.Add(FItemAmount(UWTUpgradePowerShardDescriptor::StaticClass(), 1));
        Give->AddItemsToGive(Items);

        // ★0.2.40 ROOT-PIN（真·崩溃修复，2026-10-08 01:34 实测抓到）：
        //   本 schematic 是 CDO（GetMutableDefault，进程级不朽），它的 mUnlocks 跨世界持久；
        //   而这个 Give 是运行时 NewObject 出来的实例 —— **不钉 root，第一次世界 GC 就回收它**，
        //   mUnlocks 里留下悬垂指针 ⇒ 同进程第二次读档时 SML 重新注册 schematic：
        //     RegisterSchematic → AutoRegisterSchematicReferences → AutoRegisterUnlockReferences
        //     → `Cast<UFGUnlockRecipe>(Unlock)` 解引用垃圾
        //     ⇒ EXCEPTION_ACCESS_VIOLATION reading 0xffffffffffffffff（ModContentRegistry.cpp:932）。
        //   诊断链：进程 17:31 首载 save 3123 正常 → 17:34 二次读档即崩；本类商店 unlock 是
        //   全工程**唯一**没钉 root 的运行时 unlock（其余 8 处 NewObject 全部已 AddToRoot，
        //   WirelessPower 的 unlock 也已钉）。
        //   与下方里程碑 unlock 的 ROOT-PIN（1584 行注释，WirelessPower v1.0.45/46 同款崩溃）
        //   完全同一副药。
        Give->AddToRoot();
        mUnlocks.Add(Give);
    }
    else
    {
        bConfigured = false;
        WT_WARNF(TEXT("[shop] NewObject<UWTUnlockGiveItemShard> failed -- purchase gives nothing."));
    }

    // 商店分组类目（★0.2.37 修正）：原先写的 `/…/SchematicCategories/SC_Parts.SC_Parts_C`
    // **在游戏里根本不存在**（实测 utoc 目录索引：只有 SC_Logistics、SC_RSS_* 系列），
    // StaticLoadClass 静默失败后回落 SC_Logistics —— 没崩但分组归错。
    // 现在首选原版 AWESOME 商店自己的类目 SC_RSS_Parts（已实证存在于同一目录），
    // 退路依次为 SC_RSS_StandardParts → SC_Logistics。
    UClass* Cat = StaticLoadClass(UFGSchematicCategory::StaticClass(), nullptr,
        TEXT("/Game/FactoryGame/Schematics/SchematicCategories/SC_RSS_Parts.SC_RSS_Parts_C"));
    if (!Cat)
    {
        Cat = StaticLoadClass(UFGSchematicCategory::StaticClass(), nullptr,
            TEXT("/Game/FactoryGame/Schematics/SchematicCategories/SC_RSS_StandardParts.SC_RSS_StandardParts_C"));
    }
    if (!Cat)
    {
        Cat = StaticLoadClass(UFGSchematicCategory::StaticClass(), nullptr,
            TEXT("/Game/FactoryGame/Schematics/SchematicCategories/SC_Logistics.SC_Logistics_C"));
    }
    if (Cat)
    {
        Cat->AddToRoot();
        mSchematicCategory = TSubclassOf<UFGSchematicCategory>(Cat);
    }
    else
    {
        bConfigured = false;
        WT_WARNF(TEXT("[shop] no schematic category resolved -- entry falls back to 'misc' grouping."));
    }

    // 图标：彩虹碎片贴图（与物品本体同一张，商店里一眼认出）；缺资产时函数内部回落，不会写空。
    WT_ApplySchematicIconStyled(this, EWTSchematicIconStyle::ShardRainbow);

    bDone = bConfigured;
    WT_WARNF(TEXT("[shop] shard entry configured=%d (type=%d, cost=%d coupons -> 1 shard, cat=%s)."),
             bConfigured ? 1 : 0, static_cast<int32>(mType),
             mCost.Num() > 0 ? mCost[0].Amount : 0,
             mSchematicCategory ? *mSchematicCategory->GetName() : TEXT("<none>"));
}

UWTUpgradeAcceleratorRecipe::UWTUpgradeAcceleratorRecipe()
{
    mManufactoringDuration = 0.0f;
    mProduct.Add(FItemAmount(UWTUpgradeAcceleratorDescriptor::StaticClass(), 1));
}

void UWTUpgradeAcceleratorRecipe::ApplyProducedIn(UClass* BuildGunClass)
{
    FillProducedIn(mProducedIn, BuildGunClass);
}

void UWTUpgradeAcceleratorRecipe::ApplyIngredients()
{
    FillIngredients(mIngredients, GAcceleratorCost, UE_ARRAY_COUNT(GAcceleratorCost), TEXT("accelerator"));
}

// ----------------------------------------------------------------------------
// Milestone
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// Milestones
//
// ★★★ v0.2.2 -- 结构变更：原来是一个 T4「升级套件」里程碑一次解锁三个建筑，
//     现在改成两个，各自挂在建筑本身该出现的层级上：
//
//         T3  UWTUpgradeStorageMilestone      -> 储能单元
//         T5  UWTUpgradeAcceleratorMilestone  -> 产能加速器
//         T1  （固定终端 + 便携终端 + 大容量仓库 由 URemote_FactoryMilestone 一起给）
//
//     为什么非拆不可：一个里程碑只能有一个 mTechTier，而三个建筑的"合理解锁点"
//     差了两档。挤在 T4 就意味着 T1/T3 该拿到的建筑被强行推后，或者反过来
//     里程碑索要玩家当时造不出的件 —— 本会话一路在修的就是后者。
//
//     ★ 写入动作全在基类 UWTUpgradeMilestoneBase 里，派生类只交数据：
//       mCost / mSchematicCategory / mUnlocks / UFGUnlockRecipe::mRecipes 都是 protected，
//       AccessTransformers.ini 的 friend 是**按类**声明的，自由函数不享受（C2248）。
// ----------------------------------------------------------------------------

// 里程碑造价表。行尾 (T?) 是该物品的解锁层级 —— 改数字时照着这条线看，别越级。
// ★1.0.12（用户定案）：里程碑费用不放大 4 倍，维持原量纲；但旧表第三项「马达(T4)」越级，
//   与 T3 存储里程碑不符 —— 改为钢管(T3)，与钢板/多功能框架同为 T3 钢铁线，量纲不变。
//   （建造材料的 ×4 是电池「放置配方」GStorageUnitCost 的事，不在本表。）
static const FWTCostLine GMilestoneCostStorage[] = {          // 里程碑 T3
    { TEXT("/Game/FactoryGame/Resource/Parts/SteelPlate/Desc_SteelPlate.Desc_SteelPlate_C"), 50 },                              // T3 钢板
    { TEXT("/Game/FactoryGame/Resource/Parts/SpaceElevatorParts/Desc_SpaceElevatorPart_2.Desc_SpaceElevatorPart_2_C"), 50 },    // T3 多功能框架
    { TEXT("/Game/FactoryGame/Resource/Parts/SteelPipe/Desc_SteelPipe.Desc_SteelPipe_C"), 30 },                               // T3 钢管（★1.0.12 替换原 马达 T4）
};

static const FWTCostLine GMilestoneCostAccelerator[] = {      // 里程碑 T5
    // 三样都是太空电梯件（用户指定）。层级：多功能框架 T3 / 自动化线缆 T4 / 模块化引擎 T6。
    // 模块化引擎是 T6，但 T5 与 T6 由同一次太空电梯阶段（Phase 2）一起开放，所以不算越级
    // —— 这一点和「重型模块化框架」是同一个道理，用户已确认接受。
    { TEXT("/Game/FactoryGame/Resource/Parts/SpaceElevatorParts/Desc_SpaceElevatorPart_2.Desc_SpaceElevatorPart_2_C"), 25 },  // T3 多功能框架
    { TEXT("/Game/FactoryGame/Resource/Parts/SpaceElevatorParts/Desc_SpaceElevatorPart_3.Desc_SpaceElevatorPart_3_C"), 100 }, // T4 自动化线缆
    { TEXT("/Game/FactoryGame/Resource/Parts/SpaceElevatorParts/Desc_SpaceElevatorPart_4.Desc_SpaceElevatorPart_4_C"), 50 },  // T6 模块化引擎
};

UWTUpgradeMilestoneBase::UWTUpgradeMilestoneBase()
{
    mType = ESchematicType::EST_Milestone;
    mMenuPriority = 0.0f;
    mTimeToComplete = 120.0f;
    mIsPlayerSpecific = false;
    mDependenciesBlocksSchematicAccess = false;
    mHiddenUntilDependenciesMet = false;
}

// ----------------------------------------------------------------------------
// ★v0.2.24 schematic 图标。HUB 把"图标无效"当作"schematic 不完整"处理：费用行与奖励面板
// 一起不显示（FGSchematic.cpp IsDataValid 的告警原文见头文件注释）。WP v1.0.43 同款定案。
// 写法完全走反射（brush 布局随引擎版本变，直接 include SlateCore 不值当）。
// ----------------------------------------------------------------------------
static UTexture2D* WT_LoadModIcon()
{
    static UTexture2D* Cached = nullptr;
    if (Cached)
    {
        return Cached;
    }
    static const TCHAR* Roots[] =
    {
        // ★0.2.137 改名 Smart_Factory：新根在最前，旧根保留兜底（fallback 铁律）
        TEXT("/Smart_Factory/"),
        TEXT("/Game/Smart_Factory/"),
        TEXT("/smart_factory/"),
        TEXT("/Game/smart_factory/"),
        TEXT("/Remote_Factory/"),
        TEXT("/Game/Remote_Factory/"),
        TEXT("/remote_factory/"),
        TEXT("/Game/remote_factory/"),
        // ★ fallback（铁律：改名后保留旧根兜底）
        TEXT("/WirelessTerminal/"),
        TEXT("/Game/WirelessTerminal/"),
        TEXT("/wirelessterminal/"),
        TEXT("/Game/wirelessterminal/"),
    };
    for (const TCHAR* Root : Roots)
    {
        const FString Full = FString(Root) + TEXT("Icon/icon_handheld_terminal.icon_handheld_terminal");
        if (UTexture2D* Icon = LoadObject<UTexture2D>(nullptr, *Full))
        {
            // 双保险之一：包内资产本就 GC 安全，仍钉进 root set（AddToRoot 幂等）。
            if (!Icon->IsRooted())
            {
                Icon->AddToRoot();
                WT_WARNF(TEXT("[icon] milestone icon %s pinned to root set."), *Icon->GetName());
            }
            Cached = Icon;
            return Cached;
        }
    }
    WT_WARNF(TEXT("[icon] mod icon texture not found -- HUB milestone panel stays blank."));
    return nullptr;
}

// ----------------------------------------------------------------------------
// ★0.2.36g 分档图标：候选贴图表 + 染色。见头文件 EWTSchematicIconStyle 的完整说明。
//   候选里既可以直接给「纹理资产路径」，也可以给「Desc_* 描述符类路径」——后者会去读它的
//   mPersistentBigIcon（原版建筑图标多数挂在描述符上）。两种都试，能中哪个算哪个；
//   一个都中不了就回落到模组自带图标（绝不写空，否则 MAM 会把费用+奖励面板整块藏掉）。
// ----------------------------------------------------------------------------
struct FWTSchematicIconSpec
{
    const TCHAR* const* TextureCandidates;   // 以 nullptr 结尾
    FLinearColor        Tint;
    bool                bTint;
};

// 轨 0 · 云端储存上传站 —— 优先位面存储站（我们上传站的基类），退大容量仓库。
static const TCHAR* const GTexTrack0[] = {
    TEXT("/Game/FactoryGame/Buildable/Factory/CentralStorage/Desc_CentralStorage.Desc_CentralStorage_C"),
    TEXT("/Game/FactoryGame/Buildable/Factory/StorageContainerMk2/Desc_StorageContainerMk2.Desc_StorageContainerMk2_C"),
    TEXT("/Game/FactoryGame/Buildable/Factory/StorageContainerMk1/Desc_StorageContainerMk1.Desc_StorageContainerMk1_C"),
    nullptr
};

// 轨 1 · 超级蓄电池 —— 优先原版储能单元，退电池（电池路径已在里程碑造价表里实证可加载）。
static const TCHAR* const GTexTrack1[] = {
    TEXT("/Game/FactoryGame/Buildable/Factory/PowerStorage/Desc_PowerStorageMk1.Desc_PowerStorageMk1_C"),
    TEXT("/Game/FactoryGame/Resource/Parts/Battery/Desc_Battery.Desc_Battery_C"),
    nullptr
};

// 轨 2 · 强化能量碎片 + 末段两档 —— 原版能量碎片贴图（即「超频」那张，已实证可加载）。
static const TCHAR* const GTexPowerShard[] = {
    TEXT("/Game/FactoryGame/Resource/Environment/Crystal/UI/PowerShard_256.PowerShard_256"),
    TEXT("/Game/FactoryGame/Resource/Parts/PowerShard/Desc_PowerShard.Desc_PowerShard_C"),
    nullptr
};

// ★0.2.36h 强化碎片本体 / 商店条目 —— 模组彩虹贴图（editor 命令行导入的真资产），
//   缺资产时回落原版紫色碎片贴图；不染色（任何 Tint 都会把彩虹压成单色）。
static const TCHAR* const GTexShardRainbow[] = {
    TEXT("/Smart_Factory/Icon/icon_rainbow_shard.icon_rainbow_shard"),
    TEXT("/Remote_Factory/Icon/icon_rainbow_shard.icon_rainbow_shard"),
    TEXT("/Game/FactoryGame/Resource/Environment/Crystal/UI/PowerShard_256.PowerShard_256"),
    nullptr
};

/** 档位 -> 贴图候选 + 染色。染色与贴图各自独立生效，哪种在游戏里显示得出来就用哪种。 */
static FWTSchematicIconSpec WT_IconSpecFor(EWTSchematicIconStyle Style)
{
    switch (Style)
    {
    case EWTSchematicIconStyle::Track0:      // 青蓝，与 MI_WTDepot_LightBlue 同色系
        return { GTexTrack0,     FLinearColor(0.20f, 0.72f, 1.00f, 1.0f), true };
    case EWTSchematicIconStyle::Track1:      // 橙
        return { GTexTrack1,     FLinearColor(1.00f, 0.55f, 0.10f, 1.0f), true };
    case EWTSchematicIconStyle::Track2:      // 紫（能量碎片本色）
        return { GTexPowerShard, FLinearColor(0.80f, 0.45f, 1.00f, 1.0f), true };
    case EWTSchematicIconStyle::FinalPlain:  // ★末段 · 常规路线（不消耗碎片）= 黄
        return { GTexPowerShard, FLinearColor(1.00f, 0.88f, 0.10f, 1.0f), true };
    case EWTSchematicIconStyle::FinalBoost:  // ★0.2.42 末段 · 碎片路线 = 彩虹碎片不染色（与常规路线的黄染紫碎片一眼区分；此前同为紫碎片仅染红，用户分不出来）
        return { GTexShardRainbow, FLinearColor::White, false };
    case EWTSchematicIconStyle::ShardRainbow:// ★0.2.36h 强化碎片本体 / 商店条目 = 彩虹，不染色
        return { GTexShardRainbow, FLinearColor::White, false };
    default:
        return { nullptr,        FLinearColor::White, false };
    }
}

/** 从描述符 CDO 上读 mPersistentBigIcon（原版建筑/物品的图标都挂在这）。 */
static UTexture2D* WT_IconOfDescriptor(const TCHAR* DescriptorPath)
{
    UClass* DescCls = StaticLoadClass(UFGItemDescriptor::StaticClass(), nullptr, DescriptorPath);
    if (!DescCls)
    {
        return nullptr;
    }
    DescCls->AddToRoot();
    UObject* CDO = DescCls->GetDefaultObject();
    if (!CDO)
    {
        return nullptr;
    }
    FObjectProperty* OP = CastField<FObjectProperty>(
        UFGItemDescriptor::StaticClass()->FindPropertyByName(TEXT("mPersistentBigIcon")));
    if (!OP)
    {
        return nullptr;
    }
    return Cast<UTexture2D>(OP->GetObjectPropertyValue(OP->ContainerPtrToValuePtr<void>(CDO)));
}

// ----------------------------------------------------------------------------
// ★0.2.41 我们的物品描述符图标（用户实测：MAM 奖励面板里我们的建筑/碎片条目 = **一块纯白方块**，
//   截图 2026-10-07 22:39「强化能量碎片·LV4」/ 22:41「云端储存上传站·LV4」实锤）。
//   根因：这些描述符是**原生 C++ CDO**，mSmallIcon / mPersistentBigIcon 默认为空 ——
//   MAM 奖励面板 / HUB 里程碑 / 物品栏全从这两个字段取图，取不到就是白块。
//   （对比：HUB 里程碑界面里 5 栋建筑显示正常，因为那走的是 schematic 图标 WT_ApplySchematicIcon。）
//   修法 = 与 WT_LoadStyleIcon 同一套候选机制：优先**原版同族建筑的官方图标纹理**
//   （路径取自官方 Docs JSON，权威；256/512 双档），退原版描述符 mPersistentBigIcon，
//   最后退模组自带图标（WT_LoadModIcon）。★绝不写空 —— 图标为空时 MAM 会把费用+奖励面板整块藏掉。
// ----------------------------------------------------------------------------
static void WT_ApplyItemIcon(UClass* Target, const TCHAR* const* Candidates, const TCHAR* Label)
{
    if (!Target || !Candidates)
    {
        return;
    }
    UObject* CDO = Target->GetDefaultObject();
    if (!CDO)
    {
        return;
    }

    UTexture2D* Icon = nullptr;
    const TCHAR* Chosen = TEXT("<mod-icon fallback>");
    for (int32 i = 0; Candidates[i]; ++i)
    {
        UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, Candidates[i]);
        if (!Tex)
        {
            // 不是纹理资产 —— 再当描述符类试一次（建筑图标的常见挂法）。
            Tex = WT_IconOfDescriptor(Candidates[i]);
        }
        if (Tex)
        {
            Icon = Tex;
            Chosen = Candidates[i];
            break;
        }
    }
    if (!Icon)
    {
        Icon = WT_LoadModIcon();
    }
    if (!Icon)
    {
        WT_WARNF(TEXT("[icon] %s: no candidate and no mod icon -- reward panel stays white."), Label);
        return;
    }
    if (!Icon->IsRooted())
    {
        Icon->AddToRoot();   // GC 台账惯例（AddToRoot 幂等）
    }

    for (const TCHAR* PropName : { TEXT("mSmallIcon"), TEXT("mPersistentBigIcon") })
    {
        if (FObjectProperty* OP = CastField<FObjectProperty>(
                UFGItemDescriptor::StaticClass()->FindPropertyByName(PropName)))
        {
            OP->SetObjectPropertyValue(OP->ContainerPtrToValuePtr<void>(CDO), Icon);
        }
    }
    WT_WARNF(TEXT("[icon] %s <= %s"), Label, Chosen);
}

/** 给我们的全部物品描述符挂图标；幂等，随 RegisterUpgradeRecipes（POST_INITIALIZATION）每个世界跑一次。
 *  ★0.2.42 曾把四座云端站的图标**借别的原版建筑**来区分（下载站=Mk2容器、流体上传=工业缓冲罐、
 *    流体下载=阀门）。★0.2.48 用户实测截图后定案「贴图不对」：菜单图标必须与建筑放出来的样子一致 ——
 *    四座站在代码里本来就全是位面存储站模型（SM_CentralStorage_01 + 原版材质，见
 *    Remote_FactoryUpgrades.cpp 各站构造函数），所以**统一**用位面存储站官方图标，
 *    区分靠建筑名（0.2.42 想用图标区分的诉求让位于"图标必须真实"）。
 *    蓄电池仍用原版蓄电池图标（模型同族 SM_PowerStorage_01，本来就是对的）。
 *    路径均经官方 Docs JSON（mPersistentBigIcon 字段）核实；候选链最后由 WT_ApplyItemIcon 统一回落模组图标。 */
static void WT_ApplyUpgradeItemIcons()
{
    // 四座云端站 + 产能加速器 —— 位面存储站本体（我们所有站的实际外观，★0.2.48 统一）。
    static const TCHAR* const GIconCentralStorage[] = {
        TEXT("/Game/FactoryGame/Buildable/Factory/CentralStorage/UI/IconDesc_CentralStorage_256.IconDesc_CentralStorage_256"),
        TEXT("/Game/FactoryGame/Buildable/Factory/CentralStorage/UI/IconDesc_CentralStorage_512.IconDesc_CentralStorage_512"),
        TEXT("/Game/FactoryGame/Buildable/Factory/CentralStorage/Desc_CentralStorage.Desc_CentralStorage_C"),
        nullptr
    };
    // 超级蓄电池 —— 原版储能单元同族（模型 = SM_PowerStorage_01）。
    static const TCHAR* const GIconPowerStorage[] = {
        TEXT("/Game/FactoryGame/Buildable/Factory/PowerStorage/UI/IconDesc_PowerStorage_512.IconDesc_PowerStorage_512"),
        TEXT("/Game/FactoryGame/Buildable/Factory/PowerStorage/Desc_PowerStorageMk1.Desc_PowerStorageMk1_C"),
        nullptr
    };

    struct FWTPair { UClass* Target; const TCHAR* const* Candidates; const TCHAR* Label; };
    const FWTPair Specs[] = {
        { UWTUpgradeStorageDescriptor::StaticClass(),              GIconCentralStorage,        TEXT("UWTUpgradeStorageDescriptor") },
        { UWTUpgradeCloudDownloaderDescriptor::StaticClass(),      GIconCentralStorage,        TEXT("UWTUpgradeCloudDownloaderDescriptor") },
        { UWTUpgradePowerStorageDescriptor::StaticClass(),         GIconPowerStorage,          TEXT("UWTUpgradePowerStorageDescriptor") },
        // ★1.0.8AU 补上两座云端流体站描述符（上传/下载）：它们与另三座站同款 SM_CentralStorage_01 模型，
        //   故同样借位面存储站官方图标。此前漏加 ⇒ 建造枪 / HUB 里程碑 / MAM 奖励里这两座站图标整块空白。
        { UWTUpgradeCloudFluidUploaderDescriptor::StaticClass(),   GIconCentralStorage,        TEXT("UWTUpgradeCloudFluidUploaderDescriptor") },
        { UWTUpgradeCloudFluidDownloaderDescriptor::StaticClass(), GIconCentralStorage,        TEXT("UWTUpgradeCloudFluidDownloaderDescriptor") },
        // 产能加速器 = 退役兼容类，但老存档的研究奖励/商店仍可能引用它的图标 —— 一并挂上，防止白块。
        { UWTUpgradeAcceleratorDescriptor::StaticClass(),          GIconCentralStorage,        TEXT("UWTUpgradeAcceleratorDescriptor") },
    };
    for (const FWTPair& S : Specs)
    {
        WT_ApplyItemIcon(S.Target, S.Candidates, S.Label);
    }
}

/** 按档位取贴图；每个档位只解析一次。任何失败都退回模组自带图标（保证非空）。 */
static UTexture2D* WT_LoadStyleIcon(EWTSchematicIconStyle Style, const FWTSchematicIconSpec& Spec)
{
    static TMap<uint8, UTexture2D*> Cache;
    if (UTexture2D** Hit = Cache.Find((uint8)Style))
    {
        return *Hit;
    }

    UTexture2D* Chosen = nullptr;
    const TCHAR* ChosenPath = TEXT("<mod-icon fallback>");
    if (Spec.TextureCandidates)
    {
        for (int32 i = 0; Spec.TextureCandidates[i]; ++i)
        {
            const TCHAR* Path = Spec.TextureCandidates[i];
            UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, Path);
            if (!Tex)
            {
                // 不是纹理资产 —— 再当描述符类试一次（建筑图标的常见挂法）。
                Tex = WT_IconOfDescriptor(Path);
            }
            if (Tex)
            {
                Chosen = Tex;
                ChosenPath = Path;
                break;
            }
        }
    }
    if (!Chosen)
    {
        Chosen = WT_LoadModIcon();
        if (!Chosen)
        {
            WT_WARNF(TEXT("[icon] style %d: no texture at all (candidates exhausted AND mod icon "
                          "missing) -- node icon stays blank."), (int32)Style);
            return nullptr;
        }
    }
    else if (!Chosen->IsRooted())
    {
        Chosen->AddToRoot();
    }

    // ★只在这里打一次（每档一行，不是每节点一行）—— 便于实测确认"到底挂上了哪张"。
    WT_WARNF(TEXT("[icon] style %d -> texture '%s' (tint=%s)."),
             (int32)Style, *Chosen->GetName(),
             Spec.bTint ? *Spec.Tint.ToString() : TEXT("none"));

    Cache.Add((uint8)Style, Chosen);
    return Chosen;
}

// ★★★2026-11-07 #45「图标太小」：用户定调「看原版大小，在等比例放置」。
//   本函数只读地取一座**原版**研究树首节点的 schematic 的 `mSchematicIcon.ImageSize` 作为基准，
//   把我们的 brush 对齐到它（MAM 节点 widget 直接画 schematic 的 mSchematicIcon brush，见
//   Remote_FactoryResearchTree.cpp:1350 的渲染链路，故 ImageSize 直接决定节点里图标画多大）。
//
//   只探测、不修改；带重试与缓存：成功一次即缓存，失败最多重试 6 次后放弃并回退原生尺寸。
//   读取方式与下方 WT_ApplySchematicIconStyled 写 brush 同一套反射，绝不猜字段类型。
static FVector2D GWT_VanillaMAMIconSize(-1.f, -1.f);
static int32    GWT_VanillaMAMIconAttempts = 0;

// 从某个持有 FSlateBrush 的对象上读出其 ImageSize（FVector2D）。
// 形参必须是 FStructProperty*：只有 FStructProperty 才有 ->Struct（指向它描述的 UScriptStruct），
// 基类 FProperty 没有该成员（这是上一轮编译 C2039 的真凶）。
static FVector2D WT_ReadBrushImageSize(UObject* Owner, FStructProperty* BrushStructProp)
{
    FVector2D V(0.f, 0.f);
    if (!Owner || !BrushStructProp)
    {
        return V;
    }
    void* SB = BrushStructProp->ContainerPtrToValuePtr<void>(Owner);
    if (FProperty* ISP = BrushStructProp->Struct->FindPropertyByName(TEXT("ImageSize")))
    {
        if (FStructProperty* VecProp = CastField<FStructProperty>(ISP))
        {
            void* VecMem = VecProp->ContainerPtrToValuePtr<void>(SB);
            if (FProperty* Xp = VecProp->Struct->FindPropertyByName(TEXT("X")))
            {
                if (FNumericProperty* Xn = CastField<FNumericProperty>(Xp))
                {
                    V.X = Xn->GetFloatingPointPropertyValue(Xn->ContainerPtrToValuePtr<void>(VecMem));
                }
            }
            if (FProperty* Yp = VecProp->Struct->FindPropertyByName(TEXT("Y")))
            {
                if (FNumericProperty* Yn = CastField<FNumericProperty>(Yp))
                {
                    V.Y = Yn->GetFloatingPointPropertyValue(Yn->ContainerPtrToValuePtr<void>(VecMem));
                }
            }
        }
    }
    return V;
}

static FVector2D WT_GetVanillaMAMIconSize()
{
    // 命中一次就缓存返回；没命中允许重试，但限 6 次避免反复 LoadClass 刷屏。
    if (GWT_VanillaMAMIconSize.X > 1.f && GWT_VanillaMAMIconSize.Y > 1.f)
    {
        return GWT_VanillaMAMIconSize;
    }
    if (GWT_VanillaMAMIconAttempts >= 6)
    {
        return GWT_VanillaMAMIconSize;
    }
    ++GWT_VanillaMAMIconAttempts;

    // 与 WT_AcquireLinkTemplate（Remote_FactoryResearchTree.cpp:312）同源的原版研究树路径。
    static const TCHAR* kVanillaTreePaths[] = {
        TEXT("/Game/FactoryGame/Schematics/Research/BPD_ResearchTree_Quartz.BPD_ResearchTree_Quartz_C"),
        TEXT("/Game/FactoryGame/Schematics/Research/BPD_ResearchTree_Caterium.BPD_ResearchTree_Caterium_C"),
        TEXT("/Game/FactoryGame/Schematics/Research/BPD_ResearchTree_PowerSlugs.BPD_ResearchTree_PowerSlugs_C"),
        TEXT("/Game/FactoryGame/Schematics/Research/BPD_ResearchTree_Sulfur.BPD_ResearchTree_Sulfur_C"),
        TEXT("/Game/FactoryGame/Schematics/Research/BPD_ResearchTree_Nutrients.BPD_ResearchTree_Nutrients_C"),
        TEXT("/Game/FactoryGame/Schematics/Research/BPD_ResearchTree_Mycelia.BPD_ResearchTree_Mycelia_C"),
        TEXT("/Game/FactoryGame/Schematics/Research/BPD_ResearchTree_AlienOrganisms.BPD_ResearchTree_AlienOrganisms_C"),
        TEXT("/Game/FactoryGame/Schematics/Research/BPD_ResearchTree_AlienTech.BPD_ResearchTree_AlienTech_C"),
        TEXT("/Game/FactoryGame/Schematics/Research/BPD_ResearchTree_HardDrive.BPD_ResearchTree_HardDrive_C"),
        TEXT("/Game/FactoryGame/Schematics/Research/BPD_ResearchTree_XMas.BPD_ResearchTree_XMas_C"),
    };

    for (const TCHAR* Path : kVanillaTreePaths)
    {
        UClass* TreeCls = LoadClass<UFGResearchTree>(nullptr, Path);
        if (!TreeCls)
        {
            continue;
        }
        TArray<UFGResearchTreeNode*> Nodes = UFGResearchTree::GetNodes(TreeCls);
        for (UFGResearchTreeNode* N : Nodes)
        {
            if (!N)
            {
                continue;
            }
            // GetNodeSchematic() 返回 TSubclassOf<UFGSchematic>（声明见 FGResearchTreeNode.h:21）；
            // 图标存在该 schematic 的 CDO 的 mSchematicIcon 里（FGSchematic.h:255）。
            TSubclassOf<UFGSchematic> SchCls = N->GetNodeSchematic();
            UClass* SC = SchCls.Get();
            if (!SC)
            {
                continue;
            }
            UFGSchematic* SchCDO = Cast<UFGSchematic>(SC->GetDefaultObject());
            if (!SchCDO)
            {
                continue;
            }
            if (FStructProperty* SP = CastField<FStructProperty>(
                    UFGSchematic::StaticClass()->FindPropertyByName(TEXT("mSchematicIcon"))))
            {
                if (SP->Struct && SP->Struct->GetName().Equals(TEXT("SlateBrush"), ESearchCase::IgnoreCase))
                {
                    const FVector2D V = WT_ReadBrushImageSize(SchCDO, SP);
                    if (V.X > 1.f && V.Y > 1.f)
                    {
                        GWT_VanillaMAMIconSize = V;
                        WT_WARNF(TEXT("[icon] vanilla MAM baseline mSchematicIcon.ImageSize=(%.0f,%.0f) from node '%s'"),
                                 V.X, V.Y, *N->GetName());
                        return GWT_VanillaMAMIconSize;
                    }
                }
            }
        }
    }
    WT_WARNF(TEXT("[icon] vanilla MAM baseline not available yet (attempt %d/6); will retry on next node."),
             GWT_VanillaMAMIconAttempts);
    return GWT_VanillaMAMIconSize; // (-1,-1)
}

void WT_ApplySchematicIconStyled(UFGSchematic* Schematic, EWTSchematicIconStyle Style)
{
    if (!Schematic)
    {
        return;
    }
    const FWTSchematicIconSpec Spec = WT_IconSpecFor(Style);
    UTexture2D* Icon = (Style == EWTSchematicIconStyle::Default)
        ? WT_LoadModIcon()
        : WT_LoadStyleIcon(Style, Spec);
    if (!Icon)
    {
        return;
    }
    UClass* C = Schematic->GetClass();
    bool bAny = false;

    // (1) mSmallSchematicIcon -- 纯纹理指针，直接写。
    if (FObjectProperty* OP = CastField<FObjectProperty>(C->FindPropertyByName(TEXT("mSmallSchematicIcon"))))
    {
        OP->SetObjectPropertyValue(OP->ContainerPtrToValuePtr<void>(Schematic), Icon);
        WT_WARNF(TEXT("[icon] %s.mSmallSchematicIcon <= %s OK"), *C->GetName(), *Icon->GetName());
        bAny = true;
    }

    // (2) mSchematicIcon -- FSlateBrush 结构。先在临时缓冲上组好默认刷子再改三个关键字段，
    //     最后 CopyCompleteValue 拷回（保住对象引用），与 WP Registration.cpp 同一套路。
    if (FStructProperty* SP = CastField<FStructProperty>(C->FindPropertyByName(TEXT("mSchematicIcon"))))
    {
        UScriptStruct* S = SP->Struct;
        if (S && S->GetName().Equals(TEXT("SlateBrush"), ESearchCase::IgnoreCase))
        {
            TArray<uint8> Tmp;
            Tmp.SetNumZeroed(FMath::Max<int32>(SP->GetSize() + 16, 64));
            void* B = Tmp.GetData();
            S->InitializeStruct(B);

            if (FObjectProperty* RO = CastField<FObjectProperty>(S->FindPropertyByName(TEXT("ResourceObject"))))
            {
                RO->SetObjectPropertyValue(RO->ContainerPtrToValuePtr<void>(B), Icon);
            }

            // ★★★2026-11-05 「MAM 节点方块里是空白色块」的**真正根因**（用户截图实锤 + 引擎源码核实）
            //   FSlateBrush 从零 InitializeStruct 出来时 `ImageType` = **NoImage(0)**。
            //   只设 ResourceObject **不会**把它升级成 FullColor —— 引擎
            //   `FSlateBrush::SetResourceObject()`（SlateBrush.cpp:192-204）只改指针；
            //   只有**从资产加载**的 brush 才会在 PostLoad 里被补成 FullColor。
            //   ⇒ Slate 拿着 ImageType=NoImage 的 brush 走「纯色块」绘制路径，
            //     在 MAM 节点里就渲染成一个没有任何内容的灰方块（截图 14 个方块全空）。
            //   一手核实枚举（SlateBrush.h:90-108）：
            //     namespace ESlateBrushImageType { enum Type : int { NoImage=0, FullColor, Linear, Vector }; }
            //   存储字段（同文件 :264）：TEnumAsByte<enum ESlateBrushImageType::Type> ImageType;
            //   ★TEnumAsByte 反射出来是 FByteProperty ⇒ ImportText 认短名 "FullColor"，兜底写数字 1。
            if (FProperty* IT = S->FindPropertyByName(TEXT("ImageType")))
            {
                void* ITMem = IT->ContainerPtrToValuePtr<void>(B);
                bool bTypeSet = false;
                static const TCHAR* const ImageTypeTexts[] = {
                    TEXT("FullColor"), TEXT("ESlateBrushImageType::FullColor"), TEXT("1") };
                for (const TCHAR* T : ImageTypeTexts)
                {
                    if (IT->ImportText_Direct(T, ITMem, nullptr, 0, nullptr))
                    {
                        bTypeSet = true;
                        break;
                    }
                }
                if (!bTypeSet)
                {
                    // 最后兜底：按数值属性直接写 1（TEnumAsByte 的存储是 1 字节，数值语义等价）。
                    if (FNumericProperty* Num = CastField<FNumericProperty>(IT))
                    {
                        // ★必须显式 (int64)：SetIntPropertyValue 有 int64 / uint64 两个重载，
                        //   裸写字面量 1 会在两者之间二义（已在 2026-11-05 编译实测撞到）。
                        Num->SetIntPropertyValue(ITMem, static_cast<int64>(1));
                        bTypeSet = true;
                    }
                }
                if (!bTypeSet)
                {
                    WT_WARNF(TEXT("[icon] brush ImageType could not be set to FullColor on %s -- "
                                  "MAM nodes will render as blank colour blocks."), *C->GetName());
                }
            }
            else
            {
                WT_WARNF(TEXT("[icon] brush has no ImageType field on %s -- "
                              "MAM nodes may render as blank colour blocks."), *C->GetName());
            }
            if (FProperty* IS = S->FindPropertyByName(TEXT("ImageSize")))
            {
                // ★★★2026-11-07 #45「图标太小」：尺寸对齐原版 MAM 节点图标基准
                //   （用户定调「看原版大小，在等比例放置」）。优先用原版首节点 schematic 的
                //   mSchematicIcon.ImageSize 作基准；拿不到才回退原生贴图尺寸（既有行为）。
                float SzX = static_cast<float>(Icon->GetSizeX());
                float SzY = static_cast<float>(Icon->GetSizeY());
                const FVector2D VanillaSize = WT_GetVanillaMAMIconSize();
                const bool bUsedVanilla = (VanillaSize.X > 1.f && VanillaSize.Y > 1.f);
                if (bUsedVanilla)
                {
                    SzX = VanillaSize.X;
                    SzY = VanillaSize.Y;
                }
                const FString SizeText = FString::Printf(TEXT("(X=%.0f,Y=%.0f)"), SzX, SzY);
                if (!IS->ImportText_Direct(*SizeText, IS->ContainerPtrToValuePtr<void>(B), nullptr, 0, nullptr))
                {
                    WT_WARNF(TEXT("[icon] brush ImageSize=%s rejected by %s."),
                             *SizeText, *IS->GetClass()->GetName());
                }
                else
                {
                    WT_WARNF(TEXT("[icon] %s.mSchematicIcon ImageSize set (%.0f,%.0f)%s."),
                             *C->GetName(), SzX, SzY,
                             bUsedVanilla ? TEXT(" [vanilla baseline]") : TEXT(" [native fallback]"));
                }
            }
            // DrawAs 必须是 Image，否则默认刷子什么都不画。短名与全名各试一次。
            if (FProperty* DA = S->FindPropertyByName(TEXT("DrawAs")))
            {
                static const TCHAR* const DrawTexts[] = { TEXT("Image"), TEXT("ESlateBrushDrawType::Image") };
                bool bSet = false;
                for (const TCHAR* T : DrawTexts)
                {
                    if (DA->ImportText_Direct(T, DA->ContainerPtrToValuePtr<void>(B), nullptr, 0, nullptr))
                    {
                        bSet = true;
                        break;
                    }
                }
                if (!bSet)
                {
                    WT_WARNF(TEXT("[icon] brush DrawAs could not be set -- the brush may draw nothing."));
                }
            }

            // ★0.2.36g 染色（三轨各一色；末段 碎片路线=红 / 常规路线=黄）。
            //   FSlateBrush.TintColor 是 FSlateColor{ SpecifiedColor:FLinearColor, ColorUseRule:enum }。
            //   ★必须把 ColorUseRule 从默认的 UseColor_ColorTable(1) 改成 UseColor_Specified(0)，
            //     否则 SpecifiedColor 会被颜色表顶掉，等于白写（引擎 SlateColor.h 两个枚举值已核对）。
            //   这里**不**额外打成功日志：每节点一行会刷屏，且违反本项目的日志契约
            //   （WT_WARNF 只用于真问题）。染色结果并入下面既有的 [icon-verify] readback 行。
            if (Spec.bTint)
            {
                if (FStructProperty* TintProp = CastField<FStructProperty>(S->FindPropertyByName(TEXT("TintColor"))))
                {
                    if (TintProp->Struct)
                    {
                        void* TintMem = TintProp->ContainerPtrToValuePtr<void>(B);
                        if (FStructProperty* SpecColor = CastField<FStructProperty>(
                                TintProp->Struct->FindPropertyByName(TEXT("SpecifiedColor"))))
                        {
                            if (SpecColor->Struct &&
                                SpecColor->Struct->GetStructureSize() >= (int32)sizeof(FLinearColor))
                            {
                                const FLinearColor Col = Spec.Tint;
                                FMemory::Memcpy(SpecColor->ContainerPtrToValuePtr<void>(TintMem), &Col,
                                                sizeof(FLinearColor));
                            }
                        }
                        if (FProperty* Rule = TintProp->Struct->FindPropertyByName(TEXT("ColorUseRule")))
                        {
                            static const TCHAR* const RuleTexts[] = {
                                TEXT("UseColor_Specified"),
                                TEXT("ESlateColorStylingMode::UseColor_Specified"),
                                TEXT("0") };
                            for (const TCHAR* RT : RuleTexts)
                            {
                                if (Rule->ImportText_Direct(RT, Rule->ContainerPtrToValuePtr<void>(TintMem),
                                                            nullptr, 0, nullptr))
                                {
                                    break;
                                }
                            }
                        }
                    }
                }
                else
                {
                    WT_WARNF(TEXT("[icon] %s brush has no TintColor field -- tint for style %d skipped."),
                             *C->GetName(), (int32)Style);
                }
            }

            // ★★★2026-11-05 最后一块拼图：**UVRegion** —— 「画出来是纯色块」的真凶
            //
            // 一手证据（引擎 `SlateCore/Private/Styling/SlateBrush.cpp:10-28`，默认构造函数）：
            //     : TintColor(White)
            //     , DrawAs(ESlateBrushDrawType::Image)
            //     , Tiling(NoTile)
            //     , Mirroring(NoMirror)
            //     , ImageType(ESlateBrushImageType::NoImage)
            //     , ImageSize(32, 32)
            //     , ResourceObject(nullptr)
            //     , UVRegion(ForceInit)      // ← Min=(0,0) Max=(0,0)，而且 bIsValid=**true**
            //
            // `UVRegion(ForceInit)` = `FBox2f(EForceInit)` ⇒ Min=Max=(0,0) 但**标记为有效**。
            // Slate 绘制时见到「有效」就直接采信 ⇒ UV 范围是**零尺寸** ⇒ 整块只采到同一个像素
            // ⇒ 输出一块**纯色**（用户截图那 14 个灰方块就是这么来的）。
            // 从资产加载的 brush 会在 PostLoad 里被补成 (0,0)-(1,1)，凭空 NewObject 的不会。
            //
            // 类型链路（一手核实 `SlateBrush.h`）：
            //   · 字段本体 `:487 FBox2f UVRegion;` 是 **protected**（不能直接点）；
            //   · 但类里给了 public setter `:393 void SetUVRegion(const FBox2f&)` —— 用它。
            //   （`FDeprecateBox2D` 那层只出现在 getter 的返回类型上，赋值走 FBox2f 重载即可。）
            {
                FSlateBrush* Brush = static_cast<FSlateBrush*>(B);
                Brush->SetUVRegion(FBox2f(FVector2f(0.f, 0.f), FVector2f(1.f, 1.f)));
            }

            SP->CopyCompleteValue(SP->ContainerPtrToValuePtr<void>(Schematic), B);
            S->DestroyStruct(B);
            WT_WARNF(TEXT("[icon] %s.mSchematicIcon <= brush(%s) OK"), *C->GetName(), *Icon->GetName());
            bAny = true;
        }
        else
        {
            WT_WARNF(TEXT("[icon] %s.mSchematicIcon is a %s struct, not a SlateBrush -- skipped."),
                     *C->GetName(), S ? *S->GetName() : TEXT("NULL"));
        }
    }

    if (!bAny)
    {
        WT_WARNF(TEXT("[icon] neither mSmallSchematicIcon nor mSchematicIcon found on %s."), *C->GetName());
        return;
    }

    // ★v0.2.25 回读校验：写完必须能读回来。别自己给自己发合格证 —— 0.2.24 的日志只证明"写调用
    // 返回了"，没有证明值真的落在对象上（用户实测图标写完 HUB 依然空的，所以必须把读回来的值打出来）。
    if (FObjectProperty* ReadBack = CastField<FObjectProperty>(C->FindPropertyByName(TEXT("mSmallSchematicIcon"))))
    {
        UObject* Back = ReadBack->GetObjectPropertyValue(ReadBack->ContainerPtrToValuePtr<void>(Schematic));
        WT_WARNF(TEXT("[icon-verify] %s.mSmallSchematicIcon readback=%s"),
                 *C->GetName(), Back ? *Back->GetName() : TEXT("NULL"));
    }
    if (FStructProperty* BrushProp = CastField<FStructProperty>(C->FindPropertyByName(TEXT("mSchematicIcon"))))
    {
        if (BrushProp->Struct)
        {
            void* BrushMem = BrushProp->ContainerPtrToValuePtr<void>(Schematic);
            UObject* ResObj = nullptr;
            if (FObjectProperty* RO = CastField<FObjectProperty>(BrushProp->Struct->FindPropertyByName(TEXT("ResourceObject"))))
            {
                ResObj = RO->GetObjectPropertyValue(RO->ContainerPtrToValuePtr<void>(BrushMem));
            }
            FString DrawAsText;
            if (FProperty* DA = BrushProp->Struct->FindPropertyByName(TEXT("DrawAs")))
            {
                DA->ExportText_InContainer(0, DrawAsText, BrushMem, nullptr, nullptr, PPF_None, nullptr);
            }
            // ★0.2.36g 把染色也读回来（不新增日志行，并进这一行即可）—— 便于实测判定
            //   "染色到底落没落上去、ColorUseRule 有没有被改成 Specified"。
            FString TintText = TEXT("n/a");
            if (FStructProperty* TintProp = CastField<FStructProperty>(
                    BrushProp->Struct->FindPropertyByName(TEXT("TintColor"))))
            {
                if (TintProp->Struct)
                {
                    // ★★★2026-11-05 修：基址必须是**刷子结构体的地址**，不是 UObject 地址。
                    //   TintColor 是 mSchematicIcon(FSlateBrush) 的**子属性**，所以它的容器是
                    //   BrushMem（上面 1901 行算好的那个），偏移才落到 TintColor 上。
                    //   旧写法传 Schematic（UObject 基址）⇒ 偏移算到了对象内存的无关位置
                    //   ⇒ 日志一直打 `tint=(R=-3.38e+31,...) rule=`，染色到底有没有生效永远看不出来。
                    //   （实测值 -3.3846e31 在所有条目里完全相同，就是同一个错误地址的稳定垃圾值。）
                    void* TintMem = TintProp->ContainerPtrToValuePtr<void>(BrushMem);
                    FString ColorText, RuleText;
                    if (FProperty* SC = TintProp->Struct->FindPropertyByName(TEXT("SpecifiedColor")))
                    {
                        SC->ExportText_InContainer(0, ColorText, TintMem, nullptr, nullptr, PPF_None, nullptr);
                    }
                    if (FProperty* CR = TintProp->Struct->FindPropertyByName(TEXT("ColorUseRule")))
                    {
                        CR->ExportText_InContainer(0, RuleText, TintMem, nullptr, nullptr, PPF_None, nullptr);
                    }
                    TintText = FString::Printf(TEXT("%s rule=%s"), *ColorText, *RuleText);
                }
            }
            // ★2026-11-05 加 ImageType 回读：这是 MAM 节点「空白色块」的判定位。
            //   期望读回 FullColor。若读回 NoImage，节点必然画成纯色块。
            // ★★2026-11-05 二次修正：`ExportText_InContainer` 是**追加**语义，不是覆盖。
            //   上一版把初始值写成 "<no ImageType field>" 再让它追加 ⇒ 实测打出了
            //   `imageType=<no ImageType field>FullColor` 这种既像"没字段"又像"有值"的鬼话，
            //   违反「判据本身也要自证」。现在改成：**先导出到空串，导出后按结果分派**。
            FString ImageTypeText;
            if (FProperty* ITP = BrushProp->Struct->FindPropertyByName(TEXT("ImageType")))
            {
                ITP->ExportText_InContainer(0, ImageTypeText, BrushMem, nullptr, nullptr, PPF_None, nullptr);
                if (ImageTypeText.IsEmpty())
                {
                    // TEnumAsByte 的 ExportText 有时给空串 ⇒ 退回按数值读，保证日志一定有可判读的值。
                    if (FNumericProperty* Num = CastField<FNumericProperty>(ITP))
                    {
                        const int64 V = Num->GetSignedIntPropertyValue(ITP->ContainerPtrToValuePtr<void>(BrushMem));
                        ImageTypeText = FString::Printf(TEXT("<numeric %lld, 0=NoImage 1=FullColor>"), V);
                    }
                    else
                    {
                        ImageTypeText = TEXT("<empty export, not numeric>");
                    }
                }
            }
            else
            {
                ImageTypeText = TEXT("<NO ImageType FIELD>");
            }

            WT_WARNF(TEXT("[icon-verify] %s.mSchematicIcon readback: resource=%s drawAs=%s imageType=%s tint=%s"),
                     *C->GetName(), ResObj ? *ResObj->GetName() : TEXT("NULL"), *DrawAsText,
                     *ImageTypeText, *TintText);
        }
    }
}

void WT_ApplySchematicIcon(UFGSchematic* Schematic)
{
    WT_ApplySchematicIconStyled(Schematic, EWTSchematicIconStyle::Default);
}

FString UWTUpgradeMilestoneBase::InstallCostCategoryAndUnlock(const TArray<FItemAmount>& Costs,
                                                             const TArray<UClass*>& RecipeClasses)
{
    // ★v0.2.24 先把图标写上：HUB 在图标无效时把费用行+奖励面板一起藏掉（见函数上方注释）。
    WT_ApplySchematicIcon(this);

    // ---- cost ----
    // The research cost, kept separate from each recipe's ingredients. Charging the building
    // materials here as well would double-charge: the HUB pays this once, then the build gun pays
    // the per-building cost every time one is placed.
    mCost = Costs;
    if (mCost.Num() == 0)
    {
        WT_WARNF(TEXT("[upgrade] milestone cost table resolved to zero lines — the HUB row would be empty."));
        return TEXT("empty cost");
    }
    WT_LOGF(TEXT("[upgrade] milestone cost: %d lines."), mCost.Num());

    // ---- category ----
    // The vanilla Logistics category, same tab as the terminal milestone itself.
    static const TCHAR* const GLogisticsCategory =
        TEXT("/Game/FactoryGame/Schematics/SchematicCategories/SC_Logistics.SC_Logistics_C");
    if (UClass* Category = StaticLoadClass(UFGSchematicCategory::StaticClass(), nullptr, GLogisticsCategory))
    {
        mSchematicCategory = TSubclassOf<UFGSchematicCategory>(Category);
    }
    else
    {
        WT_WARNF(TEXT("[upgrade] could not resolve the vanilla Logistics schematic category."));
        return TEXT("category missing");
    }

    // ---- unlock ----
    // BP_UnlockRecipe_C, not the native class: the HUB's rewards panel is drawn by blueprint graphs
    // that only that class carries, so a native UFGUnlockRecipe renders an empty panel. The same
    // finding WirelessPower v1.0.44 recorded, and the terminal's own milestone uses it too.
    static UClass* BPUnlockClass = nullptr;
    if (!BPUnlockClass)
    {
        BPUnlockClass = StaticLoadClass(UFGUnlockRecipe::StaticClass(), nullptr,
            TEXT("/Game/FactoryGame/Unlocks/BP_UnlockRecipe.BP_UnlockRecipe_C"));
    }
    if (!BPUnlockClass)
    {
        WT_WARNF(TEXT("[upgrade] could not load BP_UnlockRecipe — HUB rewards will stay blank."));
        return TEXT("BP_UnlockRecipe missing");
    }

    // ★跨世界重建（0.2.56）：先把 CDO 上残留的 BP_UnlockRecipe_C 实例（已 root 则先 RemoveFromRoot）
    // 从 mUnlocks 摘掉，再建一个本世界专用实例。直接 NewObject+AddToRoot 不清理旧实例，会让
    // process-persistent 实例在「同进程换档」时越过其注册所依赖的 SML FGameObjectRegistryState，
    // 导致下次重注册读失效的 recipe→item ReferencedBy（Data=NULL/Num>=2）而在 AddObjectReference
    // 读 0x8 崩溃。保证每里程碑 CDO 任意时刻只有「一个」被 root 的解锁实例，随每次重注册干净重建。
    for (int32 i = mUnlocks.Num() - 1; i >= 0; --i)
    {
        if (UFGUnlock* U = mUnlocks[i])
        {
            if (U->IsA(UFGUnlockRecipe::StaticClass()))
            {
                if (U->IsRooted())
                {
                    U->RemoveFromRoot();
                }
                mUnlocks.RemoveAt(i);
            }
        }
        else
        {
            mUnlocks.RemoveAt(i);
        }
    }

    UFGUnlockRecipe* Unlock = NewObject<UFGUnlockRecipe>(this, BPUnlockClass);
    if (!Unlock)
    {
        WT_WARNF(TEXT("[upgrade] NewObject<UFGUnlockRecipe> returned null."));
        return TEXT("unlock allocation failed");
    }

    // UFGUnlockRecipe::mRecipes is protected; this class is its declared friend. Empty() first so a
    // re-run never accumulates a superset of the intended recipe set.
    Unlock->mRecipes.Empty();
    for (UClass* RecipeClass : RecipeClasses)
    {
        if (RecipeClass)
        {
            Unlock->mRecipes.AddUnique(RecipeClass);
        }
    }

    // ROOT-PIN. AFGBuildable::BeginPlay walks every schematic's mUnlocks through
    // FixupSave_FindBuiltByRecipe; a runtime NewObject'ed, unrooted instance gets reaped by GC and
    // leaves a dangling pointer in that walk (an access violation WirelessPower hit in v1.0.45/46).
    // Removed (RemoveFromRoot) on every rebuild above, so no cross-world accumulation.
    Unlock->AddToRoot();
    mUnlocks.Add(Unlock);

    WT_LOGF(TEXT("[upgrade] milestone unlock installed: %s recipes=%d (root-pinned, rebuilt this call)."),
            *Unlock->GetClass()->GetName(), Unlock->mRecipes.Num());

    return FString();
}

// ---- T3：储能单元 ----
UWTUpgradeStorageMilestone::UWTUpgradeStorageMilestone()
{
    // ★v0.2.22 用户定案：T4 → T3（回到 v0.2.2 注释里原本的档位）。
    // ★1.0.12 材料 = 原版储能用料×4（线缆 T0 / 多功能框架 T2 / 装壳工业梁 T4）。装壳工业梁是
    //   T4 件 —— 与旧表马达 T4 同属相邻档容忍，用户明确要求里程碑放 T3，档位不动。
    // ★1.0.12 文案中文化 + 数字纠偏：旧文案是英文且写的是 1000 MWh/100 MW（0.2.48 提速前的
    //   旧数值），实际 0.2.48 定案为 1600 MWh / 400 MW。
    mTechTier = 3;
    mDisplayName = WT_LOCTEXT("Milestone_UpgradeStorage", "智能工厂 · 超级蓄电池");
    mDescription = WT_LOCTEXT("MilestoneDesc_UpgradeStorage",
        "解锁超级蓄电池（1600 MWh，充/放 400 MW）。接入电网即可存储全网盈余、在缺电时自动释放；"
        "建成后整座工厂的蓄电总量会显示在智能工厂终端面板顶部。");
}

FString UWTUpgradeStorageMilestone::InstallCostAndUnlock()
{
    TArray<FItemAmount> Costs;
    FillIngredients(Costs, GMilestoneCostStorage, UE_ARRAY_COUNT(GMilestoneCostStorage),
                    TEXT("storage milestone"));

    // ★1.0.11 恢复：超级蓄电池配方重新由 T3 存储里程碑发放（撤销 0.2.59b 的"不再发放"）。
    // MAM 研究轨暂不恢复（用户定案），故电池恒为 1600 MWh / 400 MW 基础档，无等级加成。
    TArray<UClass*> Recipes;
    Recipes.Add(UWTUpgradePowerStorageRecipe::StaticClass());
    return InstallCostCategoryAndUnlock(Costs, Recipes);
}

// ---- T5：产能加速器 ----
UWTUpgradeAcceleratorMilestone::UWTUpgradeAcceleratorMilestone()
{
    // Tier 5 ("Oil Processing"). Tiers 5 and 6 open on the same Space Elevator phase, which is why
    // heavy modular frame is acceptable here.
    mTechTier = 5;
    mDisplayName = WT_LOCTEXT("Milestone_UpgradeAccelerator", "智能工厂 · Production Accelerator");
    mDescription = WT_LOCTEXT("MilestoneDesc_UpgradeAccelerator",
        "Unlocks the Production Accelerator (up to 2.5x output, no power shards needed). It links up "
        "with the terminal and shows up in the terminal's machine list once built.");
}

FString UWTUpgradeAcceleratorMilestone::InstallCostAndUnlock()
{
    TArray<FItemAmount> Costs;
    FillIngredients(Costs, GMilestoneCostAccelerator, UE_ARRAY_COUNT(GMilestoneCostAccelerator),
                    TEXT("accelerator milestone"));

    TArray<UClass*> Recipes;
    // *0.2.59b fully-remove buildings: Production Accelerator recipe no longer granted by milestone (mod = terminal + shard only).
    return InstallCostCategoryAndUnlock(Costs, Recipes);
}

// ---- T9：强化能量碎片（★2026-11-05 用户定案：从 T5 改档到 T9）----
// 成本：沿用同一组太空电梯件（多功能框架 T3 / 自动化线缆 T4 / 模块化引擎 T6），但**数量抬到 T9 档**
//   —— 原 T5 档是 25/50/25，现在统一搭上 T9 的量级增长（×2）。件名一个都没换，避免引入未核实的路径。
static const FWTCostLine GMilestoneCostPowerShard[] =
{
    { TEXT("/Game/FactoryGame/Resource/Parts/SpaceElevatorParts/Desc_SpaceElevatorPart_2.Desc_SpaceElevatorPart_2_C"), 50 },  // T3 多功能框架
    { TEXT("/Game/FactoryGame/Resource/Parts/SpaceElevatorParts/Desc_SpaceElevatorPart_3.Desc_SpaceElevatorPart_3_C"), 100 }, // T4 自动化线缆
    { TEXT("/Game/FactoryGame/Resource/Parts/SpaceElevatorParts/Desc_SpaceElevatorPart_4.Desc_SpaceElevatorPart_4_C"), 50 },  // T6 模块化引擎
};

// ---- T5：云端固体（★2026-11-05 新增卡）----
// 造价思路：T5 玩家手上必有钢板/钢管/多功能框架，另加 T4 自动化线缆体现「比 T3 蓄电池卡贵一档」。
static const FWTCostLine GMilestoneCostCloudSolid[] =
{
    { TEXT("/Game/FactoryGame/Resource/Parts/SteelPlate/Desc_SteelPlate.Desc_SteelPlate_C"), 100 },                            // T3 钢板
    { TEXT("/Game/FactoryGame/Resource/Parts/SpaceElevatorParts/Desc_SpaceElevatorPart_2.Desc_SpaceElevatorPart_2_C"), 50 },    // T3 多功能框架
    { TEXT("/Game/FactoryGame/Resource/Parts/SpaceElevatorParts/Desc_SpaceElevatorPart_3.Desc_SpaceElevatorPart_3_C"), 25 },    // T4 自动化线缆
};

// ---- T7：云端液体（★2026-11-05 新增卡）----
// 造价思路：与 T5 卡同构，主料换成 T4 自动化线缆 + T6 模块化引擎（T7 与 T6 同阶段开放）。
static const FWTCostLine GMilestoneCostCloudFluid[] =
{
    { TEXT("/Game/FactoryGame/Resource/Parts/SpaceElevatorParts/Desc_SpaceElevatorPart_3.Desc_SpaceElevatorPart_3_C"), 50 },   // T4 自动化线缆
    { TEXT("/Game/FactoryGame/Resource/Parts/SpaceElevatorParts/Desc_SpaceElevatorPart_4.Desc_SpaceElevatorPart_4_C"), 25 },   // T6 模块化引擎
    { TEXT("/Game/FactoryGame/Resource/Parts/SteelPipe/Desc_SteelPipe.Desc_SteelPipe_C"), 60 },                                 // T3 钢管
};

UWTUpgradePowerShardMilestone::UWTUpgradePowerShardMilestone()
{
    // ★2026-11-05 用户定案：「强化能量碎片放到 T9」。原为 T5（1.0.7 首版），现改到 T9。
    mTechTier = 9;
    mMenuPriority = 10.0f;    // 同档组内排前面
    mTimeToComplete = 60.0f;
    mDisplayName = WT_LOCTEXT("Milestone_UpgradePowerShard", "智能工厂 · 强化能量碎片");
    mDescription = WT_LOCTEXT("MilestoneDesc_UpgradePowerShard",
        "解锁强化能量碎片的三种制造方式：构造机（2 块能量碎片 → 1 块）、量子编码器（合成能量碎片"
        "的全部材料翻倍 → 1 块）、制作台（2 块能量碎片手搓 1 块，6 秒）。强化碎片插进机器碎片槽后"
        "每块 +100% 超频，机器上限 500%（受槽位数限制，1 块 200% / 2 块 300% / 3 块 400% / 4 块 500%），"
        "耗电按原版越级曲线自动上涨。");
}

FString UWTUpgradePowerShardMilestone::InstallCostAndUnlock()
{
    TArray<FItemAmount> Costs;
    FillIngredients(Costs, GMilestoneCostPowerShard, UE_ARRAY_COUNT(GMilestoneCostPowerShard),
                    TEXT("power shard milestone"));

    // 三条配方一起发：少发一条 = 新档买了卡却在某台机器里看不到配方（0.2.57 的编码器事故）。
    TArray<UClass*> Recipes;
    Recipes.Add(UWTUpgradePowerShardRecipe::StaticClass());        // ① 构造机
    Recipes.Add(UWTUpgradePowerShardSynthRecipe::StaticClass());   // ② 量子编码器
    Recipes.Add(UWTUpgradePowerShardBenchRecipe::StaticClass());   // ③ 制作台（★1.0.7 新增）
    return InstallCostCategoryAndUnlock(Costs, Recipes);
}

// ---- T5：云端固体（上传站 + 下载站）★2026-11-05 新增 ----
UWTUpgradeCloudSolidMilestone::UWTUpgradeCloudSolidMilestone()
{
    mTechTier = 5;
    mMenuPriority = 5.0f;
    mTimeToComplete = 60.0f;
    mDisplayName = WT_LOCTEXT("Milestone_CloudSolid", "智能工厂 · 云端储存（固体）");
    mDescription = WT_LOCTEXT("MilestoneDesc_CloudSolid",
        "解锁云端储存上传站与云端储存下载站：把固体物品上传进位面存储的云端池，"
        "或在任意一台下载站按需取回。两台站都要接电，上传/下载速率与云端可囤积的组数由"
        "MAM「智能工厂」研究树的容量轨决定。");
}

FString UWTUpgradeCloudSolidMilestone::InstallCostAndUnlock()
{
    TArray<FItemAmount> Costs;
    FillIngredients(Costs, GMilestoneCostCloudSolid, UE_ARRAY_COUNT(GMilestoneCostCloudSolid),
                    TEXT("cloud solid milestone"));

    TArray<UClass*> Recipes;
    Recipes.Add(UWTUpgradeStorageRecipe::StaticClass());         // ① 云端储存上传站（固体）
    Recipes.Add(UWTUpgradeCloudDownloaderRecipe::StaticClass()); // ② 云端储存下载站（固体）
    return InstallCostCategoryAndUnlock(Costs, Recipes);
}

// ---- T7：云端液体（流体上传 + 流体下载）★2026-11-05 新增 ----
UWTUpgradeCloudFluidMilestone::UWTUpgradeCloudFluidMilestone()
{
    mTechTier = 7;
    mMenuPriority = 5.0f;
    mTimeToComplete = 60.0f;
    mDisplayName = WT_LOCTEXT("Milestone_CloudFluid", "智能工厂 · 云端储存（液体）");
    mDescription = WT_LOCTEXT("MilestoneDesc_CloudFluid",
        "解锁云端流体上传站与云端流体下载站：液体同样能上传进位面存储并在别处取回。"
        "两台站都按原版管道流量结算，接上管道即可工作。");
}

FString UWTUpgradeCloudFluidMilestone::InstallCostAndUnlock()
{
    TArray<FItemAmount> Costs;
    FillIngredients(Costs, GMilestoneCostCloudFluid, UE_ARRAY_COUNT(GMilestoneCostCloudFluid),
                    TEXT("cloud fluid milestone"));

    TArray<UClass*> Recipes;
    Recipes.Add(UWTUpgradeCloudFluidUploaderRecipe::StaticClass());   // ③ 云端流体上传站
    Recipes.Add(UWTUpgradeCloudFluidDownloaderRecipe::StaticClass()); // ④ 云端流体下载站
    return InstallCostCategoryAndUnlock(Costs, Recipes);
}

// ----------------------------------------------------------------------------
// ★0.2.36 新版插槽系统（用户定案 10-07）
//
// 产能研究阶段 s（1~4）原本分别抬两处，自 2026-10-17 起用户定案改为**全阶段统一**：
//   * 强化碎片每块潜能增量：恒 1.0（+100%/片）
//   * 全场可超频机器（原版 CDO mMaxPotential==2.5）的潜能上限：★1.0.7 起恒 5.0（500%，原 4.0）
// ★1.0.7（用户 2026-10-26 定案）：增量维持 +100%/片，潜能上限 400% → **500%**
//   ⇒ 1/2/3/4 片强化碎片 = 200/300/400/500%；第 4 片终于有意义（此前 3 片就顶满 400%）。
//   原版碎片每块 +0.5、3 槽物理极限 250%，与本上限互不干扰（本上限只抬 mod 碎片可用的天花板）。
//   耗电走原版指数曲线（潜能^指数）自动放大，越往上电费越狠 —— 原版自平衡。
// ----------------------------------------------------------------------------

namespace
{
    struct FWTPotStage
    {
        float Inc;   // 强化碎片每块潜能增量
        float Cap;   // 机器潜能上限
    };

    // ★1.0.7（用户 2026-10-26 定案）：每片强化碎片仍是 +100%，但上限 400% → 500%，
    //   使「第 4 片」终于有收益（旧表 3 片就顶满）。四档统一，不随研究阶段缩放。
    // Inc = 每片潜能增量（1.0 = +100%），Cap = 机器潜能上限（5.0 = 500%）。1/2/3/4 片 → 200/300/400/500%。
    const FWTPotStage GPotStages[5] = {
        { 0.f,      0.f   },
        { 1.0f,     5.0f  },
        { 1.0f,     5.0f  },
        { 1.0f,     5.0f  },
        { 1.0f,     5.0f  },
    };

    int32 GAppliedPotStage = 0;   // 0 = 尚未应用

    /**
     * ★崩溃/功能修复（2026-10-07 全代码自检）：ticker 判重必须按**世界**，不能用一次性 bool。
     *
     * 原实现用 `static bool bStarted`：回主菜单再进新存档（新 UWorld）时，旧 ticker 会因为
     * WeakWorld 失效而自行停表（返回 false），而一次性 bool 已经置位 ⇒ **新世界永远没有 ticker**，
     * 强化碎片阶段不再自动应用、云端回退也不再注入。RegisterUpgradeRecipes 本来就是
     * 每个世界在 POST_INITIALIZATION 跑一次的（同文件已有 Remote_FactoryShared::Reset(World)
     * 为"新世界"清状态），所以这里也必须按世界判重。
     */
    static TWeakObjectPtr<UWorld> GShardTickerWorld;

    /**
     * 写一台/一个类的 mMaxPotential：只抬「低于新上限」的对象。
     * ★0.2.74 修复（0.2.73 用户实测 raised=0 根因）：旧判据带下界（先 >=2.49 后 >1.01），
     * 但本版游戏 AFGBuildableFactory CDO 的 mMaxPotential 默认就是 1.0——任何按当前值的
     * 「可超频」下界都会把 CDO 与新建实例全部排除（两版实测均 raised=0 铁证）。
     * 可超频性已由调用方限定（只遍历 AFGBuildableFactory 子类，抽碎片槽的机器全在其中），
     * 这里只需保证不把已 >= 上限的重复抬。
     */
    void WT_RaiseMaxPotential(UObject* Target, FFloatProperty* Prop, float NewCap, int32& OutRaised)
    {
        const float Cur = *Prop->ContainerPtrToValuePtr<float>(Target);
        if (Cur < NewCap - 0.01f)
        {
            Prop->SetPropertyValue_InContainer(Target, NewCap);
            ++OutRaised;
        }
    }
}

void Remote_FactoryUpgrades::ApplyShardStage(UWorld* World, int32 Stage)
{
    // ★卡顿链定位：这一步会跑 TObjectIterator<UClass>（遍历**全部** UClass）再遍历全场机器，
    //   是运行期最容易造成一次性掉帧的路径（每 10s 复查 + 每次阶段变化）。必须单独计时。
    WT_ZONE_COARSE(TEXT("Upgrade.ApplyShardStage"));

    Stage = FMath::Clamp(Stage, 1, 4);
    const FWTPotStage& S = GPotStages[Stage];

    // 1) 强化碎片 CDO：每块增量
    UClass* ShardClass = UWTUpgradePowerShardDescriptor::StaticClass();
    if (FFloatProperty* IncProp = CastField<FFloatProperty>(
            ShardClass->FindPropertyByName(TEXT("mExtraPotential"))))
    {
        IncProp->SetPropertyValue_InContainer(ShardClass->GetDefaultObject(), S.Inc);
        WT_WARNF(TEXT("[pot] stage %d: enhanced shard potential +%.3f/block."), Stage, S.Inc);
    }

    // 2) 全场可超频机器：CDO + 已放置实例一起抬
    int32 RaisedCDOs = 0;
    int32 RaisedActors = 0;
    for (TObjectIterator<UClass> It; It; ++It)
    {
        UClass* C = *It;
        if (!C->IsChildOf(AFGBuildableFactory::StaticClass()))
        {
            continue;
        }
        if (C->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
        {
            continue;
        }
        if (FFloatProperty* CapProp = CastField<FFloatProperty>(
                C->FindPropertyByName(TEXT("mMaxPotential"))))
        {
            const int32 Before = RaisedCDOs;
            WT_RaiseMaxPotential(C->GetDefaultObject(), CapProp, S.Cap, RaisedCDOs);
            if (RaisedCDOs != Before)
            {
                C->AddToRoot();   // 裸 UClass* 不是 GC 引用（0.2.7 台账）
            }
        }
    }
    if (World)
    {
        for (TActorIterator<AFGBuildableFactory> It(World); It; ++It)
        {
            AFGBuildableFactory* F = *It;
            if (!F)
            {
                continue;
            }
            if (FFloatProperty* CapProp = CastField<FFloatProperty>(
                    F->GetClass()->FindPropertyByName(TEXT("mMaxPotential"))))
            {
                WT_RaiseMaxPotential(F, CapProp, S.Cap, RaisedActors);
            }
        }
    }
    // ★0.2.75 决定性探针（SDK 铁证：超频滑条读 virtual GetCurrentMaxPotential()，
    //   mMaxPotential 只是上限之一；碎片加成走 UFGPowerShardDescriptor::mExtraPotential）。
    //   抽两台在场机器把 UI 链路读数一次打出：再报"碎片没用"时可据此直接定位是哪一环
    //   （CanChange=滑条是否存在 / Min=滑条下限 / Max=mMaxPotential / CurMax=滑条实际读值）。
    if (World)
    {
        int32 Probed = 0;
        for (TActorIterator<AFGBuildableFactory> It(World); It && Probed < 2; ++It)
        {
            AFGBuildableFactory* F = *It;
            if (!F)
            {
                continue;
            }
            WT_WARNF(TEXT("[pot-probe] %s: CanChange=%d Min=%.2f Max=%.2f CurMax=%.2f"),
                     *F->GetClass()->GetName(),
                     F->GetCanChangePotential() ? 1 : 0,
                     F->GetCurrentMinPotential(),
                     F->GetMaxPotential(),
                     F->GetCurrentMaxPotential());
            ++Probed;
        }
    }
    WT_WARNF(TEXT("[pot] stage %d applied: cap %.1f%%, CDOs raised=%d, live actors raised=%d."),
             Stage, S.Cap * 100.f, RaisedCDOs, RaisedActors);
}

void Remote_FactoryUpgrades::StartShardStageTicker(UWorld* World)
{
    if (!World)
    {
        return;
    }
    if (GShardTickerWorld.Get() == World)
    {
        return;   // 本世界已在跑（重复注册同一世界时只留一个 ticker）
    }
    GShardTickerWorld = World;

    // ★新世界必须重新应用一次：GAppliedPotStage 是**进程级**全局，跨世界会粘住 ——
    //   若不归零，新存档里若阶段恰好与上一世界相同，首帧就不会跑 ApplyShardStage，
    //   新世界里已放置的机器就拿不到抬升后的上限。
    GAppliedPotStage = 0;

    TWeakObjectPtr<UWorld> WeakWorld(World);
    FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateLambda([WeakWorld](float) -> bool
        {
            UWorld* W = WeakWorld.Get();
            if (!W)
            {
                return false;   // 世界没了, 停表
            }
            const int32 S = WT_StageForLevel(WT_GetTrackLevel(W, EWTUpgradeTrack::Accelerator));
            if (S != GAppliedPotStage)
            {
                ApplyShardStage(W, S);
                GAppliedPotStage = S;
            }
            return true;   // 继续, 每 10s 复查
        }), 10.f);
}

// ----------------------------------------------------------------------------
// Registration
// ----------------------------------------------------------------------------

namespace
{
    /**
     * Resolve /Game/.../BP_BuildGun_C once per process.
     *
     * Duplicated from Remote_FactoryRegistry.cpp on purpose. Sharing it would mean either a header
     * for one function or exporting it across translation units, and the two copies are three lines
     * of identical logic whose failure mode (the build gun is not listed) is immediately visible.
     */
    UClass* WT_ResolveBuildGunClassForUpgrades()
    {
        static UClass* Cached = nullptr;
        if (Cached)
        {
            return Cached;
        }

        if (UClass* BG = FindObject<UClass>(nullptr,
                TEXT("/Game/FactoryGame/Equipment/BuildGun/BP_BuildGun.BP_BuildGun_C")))
        {
            BG->AddToRoot(); // 0.2.7: 裸静态 UClass* 不是 GC 引用，BP 类必须钉死
            Cached = BG;
            return Cached;
        }

        for (TObjectIterator<UClass> It; It; ++It)
        {
            UClass* C = *It;
            if (C->IsChildOf(AFGBuildGun::StaticClass()) && C != AFGBuildGun::StaticClass()
                && C->HasAnyClassFlags(CLASS_CompiledFromBlueprint))
            {
                C->AddToRoot(); // 0.2.7 同上
                Cached = C;
                return Cached;
            }
        }

        Cached = AFGBuildGun::StaticClass();
        return Cached;
    }
}

namespace Remote_FactoryUpgrades
{
    /**
     * ★0.2.37 配方可用性自愈。
     *
     * 用户报「强化碎片无法制作 / 一读档或开机器配方列表就崩」，全代码自检 + 老存档二进制取证
     * 得到两条**独立**的根因，这里一次收口（取证见 0.2.37 实测清单）：
     *
     * 【根因 A · 崩溃】老存档 `AFGRecipeManager::mAvailableRecipes`（UPROPERTY(SaveGame)，
     *   只增不减）里存着 0.2.36 的制造机配方 `/Script/Remote_Factory.WTUpgradePowerShardSynthRecipe`
     *   以及 0.2.36h~0.2.38 的两条 Slug 镜像配方。**只要对应的 UCLASS 还编译存在，反序列化就能
     *   落到合法类上**（这正是「空壳类不能删」的原因）；这里的摘除是为了让玩家侧列表干净，
     *   同时也是「万一将来再次删类」前的护栏 —— 遍历到 nullptr 就是 Access Violation。
     *   处置：类一律保留成空壳（见头文件），这里再把条目从数组里主动摘掉。
     *
     * 【根因 B · 无法制作】schematic 的 unlock 只在**购买那一刻**执行一次。老存档早就买过 T1，
     *   永远不会再跑一遍 unlock，于是新版本新增的配方永远进不了 mAvailableRecipes ——
     *   玩家在机器里只能看到一条（甚至一条都没有）。
     *   处置：以「装备配方已可用」为判据（= 玩家确实解锁过 T1），幂等补齐整组配方。
     *   新存档没买 T1 时装备配方也不可用 ⇒ 不会提前解锁，玩法门槛不变。
     */
    void WT_RepairRecipeAvailability(AFGRecipeManager* RM)
    {
        if (!RM)
        {
            return;
        }

        // ── 1) 摘掉已退役配方（老存档僵尸条目）─────────────────────────────────
        // 注意：先把待删项收集完再调用 RemoveAvailableRecipes —— 那个调用会改写
        //       mAvailableRecipes，而 GetAllAvailableRecipes() 返回的是它的 const 引用，
        //       边遍历边删就是自己踩自己的迭代器。
        //
        // ★0.2.39：Stale 清单 = { SynthRecipe, Recipe2, Recipe3 } 中**当前不属于解锁链**的那些。
        //  - SynthRecipe 在 0.2.39 复活为 ② 号配方，是 Wanted 成员 ⇒ 不应该被摘（下面按 Wanted 判定）。
        //  - Recipe2 / Recipe3 已退役 ⇒ 老存档里的条目必须摘掉。
        // 实现上不写死「谁退役」，而是「谁不在 Wanted 里就摘」—— 将来再退役一条配方只改 Wanted 一处。
        {
            // *0.2.59b fully-remove buildings (terminal + shard only). Placed buildings remain in old
            // saves but their recipes are pruned from the available list (see Retired) => cannot re-craft.
            TArray<TSubclassOf<UFGRecipe>> Live;
            Live.Add(URemote_FactoryEquipRecipe::StaticClass());
            // ★0.2.134 闸门（WT_FEATURE_POWER_SHARD）：开=碎片三条留在 Live（不 prune）；
            // 关=移入下面的 Retired ⇒ 老存档里已有的碎片配方条目被 prune，机器里不再出现。
            if (WT_FEATURE_POWER_SHARD)
            {
                Live.Add(UWTUpgradePowerShardRecipe::StaticClass());
                Live.Add(UWTUpgradePowerShardSynthRecipe::StaticClass());
                Live.Add(UWTUpgradePowerShardBenchRecipe::StaticClass());   // ★1.0.7 ③ 制作台
            }
            // ★★★2026-11-05 关键改动：云端四站改为**无条件**进 Live（不再看 WT_FEATURE_CLOUD_STATIONS，
            //   也不再看「有没有买对应里程碑卡」）。
            //   为什么必须无条件：Live 的作用是「别把这些配方判成 Stale 摘掉」。它们现在由 T5（固体）/
            //   T7（液体）两张卡发放，**未买卡的新档本来就没有这些条目**，多列进 Live 不会凭空发放；
            //   但**已买过旧 T1 卡的老档**手里本来就有 —— 若按里程碑判 Live，老档一读档就被 prune，
            //   玩家平白丢掉已经能造的建筑（0.2.48 那类事故）。⇒「发放」看里程碑，「回收」永远不看。
            Live.Add(UWTUpgradeStorageRecipe::StaticClass());              // ① 云端储存上传站（固体）
            Live.Add(UWTUpgradeCloudDownloaderRecipe::StaticClass());      // ② 云端储存下载站（固体）
            Live.Add(UWTUpgradeCloudFluidUploaderRecipe::StaticClass());   // ③ 云端流体上传站
            Live.Add(UWTUpgradeCloudFluidDownloaderRecipe::StaticClass()); // ④ 云端流体下载站

            // *0.2.59b: upgrade-building recipes retired too (see Live comment). Old saves lose them from
            // the available list (cannot re-craft) but keep already-placed buildings.
            // ★1.0.11 例外：UWTUpgradePowerStorageRecipe（超级蓄电池）不再 retired —— 改由 T3 存储里程碑
            // 发放，若仍在此清单会被 prune 掉，故移除（见下方）。
            TArray<TSubclassOf<UFGRecipe>> Retired;
            if (!WT_FEATURE_POWER_SHARD)
            {
                Retired.Add(UWTUpgradePowerShardRecipe::StaticClass());
                Retired.Add(UWTUpgradePowerShardSynthRecipe::StaticClass());
                Retired.Add(UWTUpgradePowerShardBenchRecipe::StaticClass());   // ★1.0.7 ③
            }
            Retired.Add(UWTUpgradePowerShardRecipe2::StaticClass());
            Retired.Add(UWTUpgradePowerShardRecipe3::StaticClass());
            // ★1.0.7：云端站恢复发放（见上方 Live）。仍写进 Retired 是**故意**的 ——
            // Stale 判据是 `Retired.Contains(R) && !Live.Contains(R)`：
            //   · 固体两台（闸门开）同时在 Live ⇒ 不会被 prune；
            //   · 云端流体两台站已于本版恢复（配方/描述符类重新加回，并登记进 Live + Wanted），
            //     故此处不再 prune 它们；已放置的旧液体站实例建筑类仍在，不受影响。
            Retired.Add(UWTUpgradeStorageRecipe::StaticClass());
            Retired.Add(UWTUpgradeCloudDownloaderRecipe::StaticClass());
            Retired.Add(UWTUpgradeAcceleratorRecipe::StaticClass());

            TArray<TSubclassOf<UFGRecipe>> Stale;
            for (const TSubclassOf<UFGRecipe>& R : RM->GetAllAvailableRecipes())
            {
                // 只摘「明确退役」的类 —— 绝不动原版配方或其它 mod 的配方。
                if (R && Retired.Contains(R) && !Live.Contains(R))
                {
                    Stale.Add(R);
                }
            }
            if (Stale.Num() > 0)
            {
                RM->RemoveAvailableRecipes(Stale);
                WT_WARNF(TEXT("[wt-fix] pruned %d retired recipe(s) from mAvailableRecipes "
                              "(stale save entries; class kept as a shell for save compat)."),
                         Stale.Num());
            }
        }

        // ── 2) 幂等补发 ────────────────────────────────────────────────────────
        // ★0.2.48 门槛修正（用户实测「里程碑已研究，二次进存档后建筑全消失」）：
        //   原判据「装备配方已可用 = T1 已解锁」在**读档场景必然失效**——存档里的
        //   mAvailableRecipes 会在 AFGRecipeManager::PostLoadGame 被引擎整批丢弃
        //   （0.2.48 实测日志 6 条 "Recipe '...Recipe' can not be added to available
        //   recipes anymore."，装备配方 URemote_FactoryEquipRecipe 也在其中），
        //   门槛恒 false ⇒ 补发被静默跳过 ⇒ 读档后建造菜单里 WT 建筑全部消失。
        //   新判据 = 「T1 里程碑是否已购买」（AFGSchematicManager::IsSchematicPurchased，
        //   存档原生持久化、读档后立即可查；与 WT_GetTrackLevel 同款查询路径）。
        //   装备配方可用性保留为 OR 兜底（schematic manager 拿不到时仍按旧判据走）。
        // ★0.2.59b 修复（用户实测「解锁 T1 后仍无强化碎片配方」）：
        //   原门槛只看「WT T1 里程碑是否被购买」。实测该判定在老存档 / 读档时序 / 类匹配场景下
        //   不稳定地返回 false，导致强化碎片两条配方永远进不了 mAvailableRecipes。
        //   强化碎片是 mod 核心物品，其合成配方只要对应机器（构造机 / 量子编码器）解锁后即可制作，
        //   故**不再依赖里程碑门槛**——机器未解锁时本来就做不了，不会破坏前期节奏：
        //     · 装备配方（手持终端）仍按设计由 WT T1 里程碑解锁（保留 bT1Unlocked 门槛）；
        //     · 强化碎片两条配方**无条件补发**，保证「解锁 T1 / 有构造机」即能在机器里看到。
        // ★1.0.7（2026-10-26 最终定案「算了，跟终端放一起吧」）：强化碎片不再单开 T5 卡，三条
        //   配方回到「跟手持终端同一张卡」——门槛就是 bT1Unlocked（T1 里程碑已购，或配方已在）。
        //   取消耗 0.2.59b 的「无条件补发」（当时是为绕开 T1 判定不稳），也取消本轮首版的 T5 门槛。
        bool bT1Unlocked = RM->IsRecipeAvailable(URemote_FactoryEquipRecipe::StaticClass());
        if (!bT1Unlocked)
        {
            UWorld* World = RM->GetWorld();
            const AFGSchematicManager* SM = World ? AFGSchematicManager::Get(World) : nullptr;
            if (SM && SM->IsSchematicPurchased(URemote_FactoryMilestone::StaticClass()))
            {
                bT1Unlocked = true;
                // 取证行（每会话至多一次）：证明读档丢配方后走的是「里程碑购买」这条新门槛。
                WT_WARNF_ONCE(TEXT("wt-fix-gate"), RM,
                              TEXT("[wt-fix] equip recipe was dropped by PostLoadGame; "
                                   "gate re-opened via purchased WT milestone schematic."));
            }
        }
        // 注意：此处不再因 !bT1Unlocked 直接 return —— 每条配方在下方 Missing 循环里各按自己的门槛
        // 判定（碎片三条 / 云端两条 = T1，超级蓄电池 = T3），一处 return 会把整批补发一起掐掉。
        // ★旧文案「强化碎片无论门槛如何都补发」是 0.2.59b 的临时放宽，1.0.7 已取消，别再照抄。

        // ★1.0.11 恢复超级蓄电池：T3 存储里程碑（UWTUpgradeStorageMilestone）已重新发放该配方；
        // 此处加 T3 门槛，保证「已购 T3 的老存档」升级后也能经读档补发拿到，且不提前到 T1 解锁。
        bool bStorageMilestoneUnlocked = false;
        {
            UWorld* World = RM->GetWorld();
            const AFGSchematicManager* SM = World ? AFGSchematicManager::Get(World) : nullptr;
            if (SM && SM->IsSchematicPurchased(UWTUpgradeStorageMilestone::StaticClass()))
            {
                bStorageMilestoneUnlocked = true;
            }
        }

        // ★2026-11-05 三张新卡的「已购」判定。一次查清，供下面 Wanted 循环按配方分派。
        //   T5 云端固体 / T7 云端液体 / T9 强化碎片（原 T5 卡改档）。
        bool bCloudSolidUnlocked = false;
        bool bCloudFluidUnlocked = false;
        bool bShardMilestoneUnlocked = false;
        {
            UWorld* World = RM->GetWorld();
            const AFGSchematicManager* SM = World ? AFGSchematicManager::Get(World) : nullptr;
            if (SM)
            {
                bCloudSolidUnlocked     = SM->IsSchematicPurchased(UWTUpgradeCloudSolidMilestone::StaticClass());
                bCloudFluidUnlocked     = SM->IsSchematicPurchased(UWTUpgradeCloudFluidMilestone::StaticClass());
                bShardMilestoneUnlocked = SM->IsSchematicPurchased(UWTUpgradePowerShardMilestone::StaticClass());
            }
        }

        TArray<TSubclassOf<UFGRecipe>> Wanted;
        Wanted.Add(URemote_FactoryEquipRecipe::StaticClass());        // 终端：T1 门槛
        // ★1.0.11：超级蓄电池配方经 T3 存储里程碑发放；此处纳入读档补发（门槛=已购 T3，不提前到 T1）。
        Wanted.Add(UWTUpgradePowerStorageRecipe::StaticClass());
        // ★2026-11-05：碎片三条配方改由 T9 卡发放（原 T5 卡改档；不再跟 T1 一起给）。
        if (WT_FEATURE_POWER_SHARD)
        {
            Wanted.Add(UWTUpgradePowerShardRecipe::StaticClass());        // 强化碎片① 构造机
            Wanted.Add(UWTUpgradePowerShardSynthRecipe::StaticClass());   // 强化碎片② 量子编码器（0.2.39）
            Wanted.Add(UWTUpgradePowerShardBenchRecipe::StaticClass());   // 强化碎片③ 制作台（★1.0.7）
        }
        // ★2026-11-05：云端四站不再随 T1 发放 —— 固体两站看 T5 卡、液体两站看 T7 卡。
        if (WT_FEATURE_CLOUD_STATIONS)
        {
            Wanted.Add(UWTUpgradeStorageRecipe::StaticClass());              // ① 云端储存上传站（固体）→ T5
            Wanted.Add(UWTUpgradeCloudDownloaderRecipe::StaticClass());      // ② 云端储存下载站（固体）→ T5
            Wanted.Add(UWTUpgradeCloudFluidUploaderRecipe::StaticClass());   // ③ 云端流体上传站 → T7
            Wanted.Add(UWTUpgradeCloudFluidDownloaderRecipe::StaticClass()); // ④ 云端流体下载站 → T7
        }

        TArray<TSubclassOf<UFGRecipe>> Missing;
        for (const TSubclassOf<UFGRecipe>& C : Wanted)
        {
            if (!C || RM->IsRecipeAvailable(C))
            {
                continue;
            }
            // 每条配方按**各自的卡**判门槛（2026-11-05 分档后）：
            //   终端                   → T1「智能工厂」
            //   超级蓄电池             → T3「超级蓄电池」
            //   云端固体两站           → T5「云端储存（固体）」
            //   云端液体两站           → T7「云端储存（液体）」
            //   强化碎片三条           → T9「强化能量碎片」
            // ★别把 UWTUpgradeStorageRecipe（云端上传站）和 UWTUpgradePowerStorageRecipe（超级蓄电池）
            //   搞混 —— 名字只差一个 Power，走的是完全不同的两张卡。
            const bool bIsShard = (C == UWTUpgradePowerShardRecipe::StaticClass())
                               || (C == UWTUpgradePowerShardSynthRecipe::StaticClass())
                               || (C == UWTUpgradePowerShardBenchRecipe::StaticClass());
            const bool bIsPowerStorage = (C == UWTUpgradePowerStorageRecipe::StaticClass());
            const bool bIsCloudSolid = (C == UWTUpgradeStorageRecipe::StaticClass())
                                    || (C == UWTUpgradeCloudDownloaderRecipe::StaticClass());
            const bool bIsCloudFluid = (C == UWTUpgradeCloudFluidUploaderRecipe::StaticClass())
                                    || (C == UWTUpgradeCloudFluidDownloaderRecipe::StaticClass());
            if (bIsPowerStorage)
            {
                if (!bStorageMilestoneUnlocked) continue;
            }
            else if (bIsShard)
            {
                if (!bShardMilestoneUnlocked) continue;
            }
            else if (bIsCloudSolid)
            {
                if (!bCloudSolidUnlocked) continue;
            }
            else if (bIsCloudFluid)
            {
                if (!bCloudFluidUnlocked) continue;
            }
            else if (!bT1Unlocked)
            {
                continue;
            }
            Missing.Add(C);
        }

        if (Missing.Num() > 0)
        {
            RM->AddAvailableRecipes(Missing);
            for (const TSubclassOf<UFGRecipe>& C : Missing)
            {
                WT_WARNF(TEXT("[wt-fix] recipe top-up (was missing): %s"), *C->GetName());
            }
        }
        else
        {
            WT_LOGF(TEXT("[wt-fix] all %d WT recipes already available."), Wanted.Num());
        }
    }

    void ApplyUpgradeMilestoneData()
    {
        // Two milestones since v0.2.2 (T3 storage unit, T5 production accelerator). Each one is
        // filled independently -- a failure in one must not leave the other half-configured.
        if (UWTUpgradeStorageMilestone* M = GetMutableDefault<UWTUpgradeStorageMilestone>())
        {
            const FString Status = M->InstallCostAndUnlock();
            if (!Status.IsEmpty())
            {
                WT_WARNF(TEXT("[upgrade] storage milestone setup incomplete: %s"), *Status);
            }
        }
        else
        {
            WT_WARNF(TEXT("[upgrade] UWTUpgradeStorageMilestone CDO missing."));
        }

        if (UWTUpgradeAcceleratorMilestone* M = GetMutableDefault<UWTUpgradeAcceleratorMilestone>())
        {
            const FString Status = M->InstallCostAndUnlock();
            if (!Status.IsEmpty())
            {
                WT_WARNF(TEXT("[upgrade] accelerator milestone setup incomplete: %s"), *Status);
            }
        }
        else
        {
            WT_WARNF(TEXT("[upgrade] UWTUpgradeAcceleratorMilestone CDO missing."));
        }

        // ★1.0.7 强化碎片里程碑（★2026-11-05 改档 T5 → T9）：成本/类目/解锁链在这里填。
        // 与上面两张卡同一套写法 —— 独立 try，一张失败不影响其它几张。
        if (UWTUpgradePowerShardMilestone* M = GetMutableDefault<UWTUpgradePowerShardMilestone>())
        {
            const FString Status = M->InstallCostAndUnlock();
            if (!Status.IsEmpty())
            {
                WT_WARNF(TEXT("[upgrade] power shard milestone setup incomplete: %s"), *Status);
            }
        }
        else
        {
            WT_WARNF(TEXT("[upgrade] UWTUpgradePowerShardMilestone CDO missing."));
        }

        // ★2026-11-05 新增两张云端卡（T5 固体 / T7 液体）。同上一套写法，各自独立判空。
        if (UWTUpgradeCloudSolidMilestone* M = GetMutableDefault<UWTUpgradeCloudSolidMilestone>())
        {
            const FString Status = M->InstallCostAndUnlock();
            if (!Status.IsEmpty())
            {
                WT_WARNF(TEXT("[upgrade] cloud solid milestone setup incomplete: %s"), *Status);
            }
        }
        else
        {
            WT_WARNF(TEXT("[upgrade] UWTUpgradeCloudSolidMilestone CDO missing."));
        }

        if (UWTUpgradeCloudFluidMilestone* M = GetMutableDefault<UWTUpgradeCloudFluidMilestone>())
        {
            const FString Status = M->InstallCostAndUnlock();
            if (!Status.IsEmpty())
            {
                WT_WARNF(TEXT("[upgrade] cloud fluid milestone setup incomplete: %s"), *Status);
            }
        }
        else
        {
            WT_WARNF(TEXT("[upgrade] UWTUpgradeCloudFluidMilestone CDO missing."));
        }
    }

    void RegisterUpgradeRecipes(UWorld* World)
    {
        if (!World)
        {
            return;
        }

        AFGRecipeManager* RM = AFGRecipeManager::Get(World);
        if (!RM)
        {
            WT_WARNF(TEXT("[upgrade] AFGRecipeManager not ready; skipping upgrade registration."));
            return;
        }

        UClass* BuildGunClass = WT_ResolveBuildGunClassForUpgrades();
        WT_LOGF(TEXT("[upgrade] build gun resolved: %s"),
                BuildGunClass ? *BuildGunClass->GetPathName() : TEXT("NULL"));

        ApplyUpgradeMilestoneData();

        // ★0.2.41 物品图标（MAM 奖励面板/HUB 白方块修复）—— 幂等，见 WT_ApplyUpgradeItemIcons 注释。
        WT_ApplyUpgradeItemIcons();

        if (UWTUpgradePowerStorageRecipe* R = Cast<UWTUpgradePowerStorageRecipe>(
                UWTUpgradePowerStorageRecipe::StaticClass()->GetDefaultObject()))
        {
            R->ApplyProducedIn(BuildGunClass);
            R->ApplyIngredients();
        }
        if (UWTUpgradeStorageRecipe* R = Cast<UWTUpgradeStorageRecipe>(
                UWTUpgradeStorageRecipe::StaticClass()->GetDefaultObject()))
        {
            R->ApplyProducedIn(BuildGunClass);
            R->ApplyIngredients();
        }
        if (UWTUpgradeCloudDownloaderRecipe* R = Cast<UWTUpgradeCloudDownloaderRecipe>(
                UWTUpgradeCloudDownloaderRecipe::StaticClass()->GetDefaultObject()))
        {
            R->ApplyProducedIn(BuildGunClass);
            R->ApplyIngredients();
        }
        if (UWTUpgradeCloudFluidUploaderRecipe* R = Cast<UWTUpgradeCloudFluidUploaderRecipe>(
                UWTUpgradeCloudFluidUploaderRecipe::StaticClass()->GetDefaultObject()))
        {
            R->ApplyProducedIn(BuildGunClass);
            R->ApplyIngredients();
        }
        if (UWTUpgradeCloudFluidDownloaderRecipe* R = Cast<UWTUpgradeCloudFluidDownloaderRecipe>(
                UWTUpgradeCloudFluidDownloaderRecipe::StaticClass()->GetDefaultObject()))
        {
            R->ApplyProducedIn(BuildGunClass);
            R->ApplyIngredients();
        }
        if (UWTUpgradeAcceleratorRecipe* R = Cast<UWTUpgradeAcceleratorRecipe>(
                UWTUpgradeAcceleratorRecipe::StaticClass()->GetDefaultObject()))
        {
            R->ApplyProducedIn(BuildGunClass);
            R->ApplyIngredients();
        }
        // ★0.2.39 强化碎片 = 两条配方，各自属于不同机器：
        //   ① UWTUpgradePowerShardRecipe      → 构造机（Build_ConstructorMk1_C）：
        //                                        2 × 原版能量碎片 → 1 × 强化碎片
        //   ② UWTUpgradePowerShardSynthRecipe → 量子编码器（Build_QuantumEncoder_C）：
        //                                        原版 Recipe_SyntheticPowerShard 全部材料 ×2 → 1 × 强化碎片（无残渣）
        // 两台机器的类都在运行时**显式解析**（★0.2.37 起）—— 不再只靠构造函数里的裸软路径：
        // 软路径解析不出来时配方不属于任何机器，玩家在机器 UI 里永远看不到，而且一声不响 = 「无法制作」。
        {
            UClass* Constructor = WT_FindConstructorMk1Class();
            if (!Constructor)
            {
                WT_WARNF(TEXT("[shard] constructor class unresolved -- recipe1 keeps soft path only."));
            }
            if (UWTUpgradePowerShardRecipe* R = Cast<UWTUpgradePowerShardRecipe>(
                    UWTUpgradePowerShardRecipe::StaticClass()->GetDefaultObject()))
            {
                R->ApplyProducedIn(Constructor);
                R->ApplyIngredients();
            }

            UClass* Encoder = WT_FindQuantumEncoderClass();
            if (!Encoder)
            {
                WT_WARNF(TEXT("[shard] quantum encoder class unresolved -- recipe2 keeps soft path only."));
            }
            if (UWTUpgradePowerShardSynthRecipe* R = Cast<UWTUpgradePowerShardSynthRecipe>(
                    UWTUpgradePowerShardSynthRecipe::StaticClass()->GetDefaultObject()))
            {
                R->ApplyProducedIn(Encoder);
                R->ApplyIngredients();
            }
            // Recipe2 / Recipe3 是退役空壳：**不要**调用它们（它们没有 Apply* 接口，构造体也刻意留空）。

            // ★1.0.7 ③ 制作台（手工台）：2 × 原版碎片 → 1 × 强化碎片。
            //   制作台侧 producer 判据挂在建筑类与工作台组件上，不是普通机器那一套 ⇒
            //   由 WT_FindCraftBenchProducers 给候选人（建筑类 / 工作台组件；★1.0.7w 起
            //   物品描述符已被拒绝，见该函数注释 —— 它不实现 IFGRecipeProducerInterface，写进去会崩）。
            //   全失败时保留构造函数里的两条软路径 —— 清空 mProducedIn 会让配方无声消失。
            {
                TArray<UClass*> Benches;
                WT_FindCraftBenchProducers(Benches);
                if (Benches.Num() == 0)
                {
                    WT_WARNF(TEXT("[shard] craft bench producers unresolved -- recipe3 keeps soft paths only."));
                }
                if (UWTUpgradePowerShardBenchRecipe* R = Cast<UWTUpgradePowerShardBenchRecipe>(
                        UWTUpgradePowerShardBenchRecipe::StaticClass()->GetDefaultObject()))
                {
                    R->ApplyProducedIn(Benches);
                    R->ApplyIngredients();
                }

                // ★1.0.7 绑定探针（每次进档一次，Warning 级不会被编译剔除）：
                // 对每个候选 producer 反查「它能生产多少配方」。原版手工配方全挂在制作台上，
                // 所以命中那一类的数量必然 > 0 —— 哪一行不为 0，就说明 mProducedIn 该写哪一类。
                // 同一行里再打出我们配方自己的 producer 列表，一眼看出填没填对。
                for (UClass* C : Benches)
                {
                    TArray<TSubclassOf<UFGRecipe>> Producible;
                    RM->GetAvailableRecipesForProducer(C, Producible);
                    // ★1.0.7w 同时打「是否实现接口」：只打命中数是看不出来的 —— Desc_WorkBench_C
                    //   当年也报 1 条（因为它就在我们自己的配方列表里，反查必然命中自己），
                    //   假阳性让「描述符可用」这个错判活了好几版。
                    WT_WARNF(TEXT("[shard-probe] producer %s -> %d producible recipes | implements=%d"),
                             *C->GetName(), Producible.Num(),
                             C->ImplementsInterface(UFGRecipeProducerInterface::StaticClass()) ? 1 : 0);
                }
                if (UWTUpgradePowerShardBenchRecipe* R = Cast<UWTUpgradePowerShardBenchRecipe>(
                        UWTUpgradePowerShardBenchRecipe::StaticClass()->GetDefaultObject()))
                {
                    TArray<TSubclassOf<UObject>> OurProducers;
                    R->GetProducedIn(OurProducers);
                    WT_WARNF(TEXT("[shard-probe] recipe3 mProducedIn = %d entries (available=%d)"),
                             OurProducers.Num(),
                             RM->IsRecipeAvailable(UWTUpgradePowerShardBenchRecipe::StaticClass()) ? 1 : 0);
                    for (const TSubclassOf<UObject>& P : OurProducers)
                    {
                        // TSubclassOf 没有 operator bool，用 Get() 判空（否则 MSVC C2676）。
                        WT_WARNF(TEXT("[shard-probe]   producer entry: %s"),
                                 P.Get() ? *P->GetPathName() : TEXT("NULL"));
                    }
                }
            }
        }

        // ★0.2.59b 修复：原版能量碎片镜像源错误。
        // 旧实现遍历 GetAllItemDescriptors 取「第一个 UFGPowerShardDescriptor 子类且非 WT 类」——
        // 但索莫晶体 Desc_WAT1 也是 UFGPowerShardDescriptor 子类，会抢在真实能量碎片(Desc_CrystalShard)
        // 前被选中（日志实锤 src=Desc_WAT1_C ⇒ 强化碎片误用索莫水晶建模 SM_SomerSloop_01）。
        // 改用专用解析器 WT_FindVanillaShardClass()（按权威路径加载 Desc_CrystalShard，失败才按名兜底），
        // 与配方材料/合成解析同一条正确链路。
        {
            UClass* VanillaShard = WT_FindVanillaShardClass();
            if (UWTUpgradePowerShardDescriptor* Shard = Cast<UWTUpgradePowerShardDescriptor>(
                    UWTUpgradePowerShardDescriptor::StaticClass()->GetDefaultObject()))
            {
                Shard->ApplyVanillaMirror(VanillaShard);
            }
        }

        // ★0.2.37 配方可用性自愈（摘僵尸条目 + 幂等补发；详见函数注释）。
        WT_RepairRecipeAvailability(RM);

        WT_LOGF(TEXT("[upgrade] recipes registered (storage=%d downloader=%d accel=%d shard=%d/%d)."),
                RM->IsRecipeAvailable(UWTUpgradeStorageRecipe::StaticClass()) ? 1 : 0,
                RM->IsRecipeAvailable(UWTUpgradeCloudDownloaderRecipe::StaticClass()) ? 1 : 0,
                RM->IsRecipeAvailable(UWTUpgradeAcceleratorRecipe::StaticClass()) ? 1 : 0,
                RM->IsRecipeAvailable(UWTUpgradePowerShardRecipe::StaticClass()) ? 1 : 0,
                RM->IsRecipeAvailable(UWTUpgradePowerShardSynthRecipe::StaticClass()) ? 1 : 0);

        // 只有真出问题才 WARN —— WT_LOGF 默认编译期删除（WT_LOG_VERBOSE=false），
        // 上面那条汇总在 Shipping 日志里根本不存在，出问题时必须有一条看得见的。
        {
            const bool bR1 = RM->IsRecipeAvailable(UWTUpgradePowerShardRecipe::StaticClass());
            const bool bR2 = RM->IsRecipeAvailable(UWTUpgradePowerShardSynthRecipe::StaticClass());
            const bool bR3 = RM->IsRecipeAvailable(UWTUpgradePowerShardBenchRecipe::StaticClass());
            if (!(bR1 && bR2 && bR3))
            {
                // ★★2026-11-05 改档纠偏：碎片三条配方已从 T1 搬到 **T9**（用户定案「强化能量碎片放到 T9」）。
                // 买 T9 卡前本来就该缺，属正常；买 T9 后仍缺才是问题。
                //（旧文案写「expected until the T1 milestone」是搬档前的，已被实测日志误导过一次，必须改掉。）
                WT_WARNF(TEXT("[shard] shard recipes not all available yet (bench=%d constructor=%d encoder=%d) -- "
                              "expected until the T9 milestone (Enhanced Power Shard, Tier 9) is purchased at the HUB."),
                         bR3 ? 1 : 0, bR1 ? 1 : 0, bR2 ? 1 : 0);
            }
        }

        // ★0.2.36 新版插槽系统：进场先查一次产能研究阶段，之后每 10s 复查（研究买完 ≤10s 生效）。
        StartShardStageTicker(World);

        // ★0.2.36b 建造取料回退（用户定案）：原版云没货时把我们的账本注入池子。
        StartCloudFallbackTicker(World);

        // 0.2.7: 90s 后 GC 复查探针（Display 级，Shipping 可见）。
        WT_QueuePinnedClassProbe();
    }
}
