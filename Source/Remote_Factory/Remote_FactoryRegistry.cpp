#include "Remote_FactoryRegistry.h"

#include "Remote_FactoryLoc.h"
#include "Remote_FactoryLog.h"
#include "Remote_FactoryBuildable.h"
#include "Remote_FactoryEquipment.h"
#include "Remote_FactoryHologram.h"
// v0.2.2: the tier-1 milestone now also unlocks the High-Capacity Storage Unit, whose recipe class
// lives in the upgrade registry. Only the UClass is needed here.
#include "Remote_FactoryUpgradeRegistry.h"

#include "ItemAmount.h"
#include "FGRecipeManager.h"
#include "Resources/FGItemDescriptor.h"
#include "FGSchematicCategory.h"
#include "Unlocks/FGUnlockRecipe.h"
#include "Equipment/FGBuildGun.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UObjectGlobals.h"
#include "Engine/World.h"

// ----------------------------------------------------------------------------
// Reflection helpers.
//
// UFGBuildingDescriptor's build category / sub-category members are not declared in the shipped
// SDK stub, so they are written by name at runtime (the same approach WirelessPower uses). This
// compiles against the real header set and writes the real game properties.
// ----------------------------------------------------------------------------
static void SetBuildCategoryProp(UFGBuildingDescriptor* Desc, UClass* CategoryClass)
{
    if (!Desc || !CategoryClass) return;

    // The property has been spelled both ways across SDK snapshots; accept either.
    FProperty* Prop = FindFProperty<FProperty>(Desc->GetClass(), TEXT("mBuildCategory"));
    if (!Prop)
    {
        Prop = FindFProperty<FProperty>(Desc->GetClass(), TEXT("mCategory"));
    }
    if (!Prop)
    {
        WT_WARNF(TEXT("[registry] no build-category property on %s; the build gun may not list the terminal."),
                 *Desc->GetClass()->GetName());
        return;
    }

    const FString ClassPath = CategoryClass->GetPathName();
    const TCHAR* ImportResult = Prop->ImportText_Direct(*ClassPath, Prop->ContainerPtrToValuePtr<void>(Desc), Desc, 0, nullptr);
    WT_LOGF(TEXT("[registry] %s.%s <= %s (imported=%s)"),
            *Desc->GetClass()->GetName(), *Prop->GetName(), *CategoryClass->GetName(),
            ImportResult ? TEXT("ok") : TEXT("FAILED"));
}

static void SetSubCategoriesProp(UFGBuildingDescriptor* Desc, UClass* SubCategoryClass)
{
    if (!Desc || !SubCategoryClass) return;

    FArrayProperty* ArrProp = FindFProperty<FArrayProperty>(Desc->GetClass(), TEXT("mSubcategories"));
    if (!ArrProp)
    {
        WT_WARNF(TEXT("[registry] no mSubcategories array on %s; the build-gun grid may be empty."),
                 *Desc->GetClass()->GetName());
        return;
    }

    void* ArrayPtr = ArrProp->ContainerPtrToValuePtr<void>(Desc);
    FScriptArrayHelper Helper(ArrProp, ArrayPtr);
    if (Helper.Num() == 0) Helper.AddValue();
    const FString Path = SubCategoryClass->GetPathName();
    const TCHAR* ImportResult = ArrProp->Inner->ImportText_Direct(*Path, Helper.GetRawPtr(0), Desc, 0, nullptr);
    WT_LOGF(TEXT("[registry] %s.mSubcategories[0] <= %s (imported=%s, num=%d)"),
            *Desc->GetClass()->GetName(), *SubCategoryClass->GetName(),
            ImportResult ? TEXT("ok") : TEXT("FAILED"), Helper.Num());
}

/** Resolve /Game/.../BP_BuildGun.BP_BuildGun_C once per process. */
static UClass* WT_ResolveBuildGunClass()
{
    static UClass* Cached = nullptr;
    if (Cached) return Cached;

    if (UClass* BG = FindObject<UClass>(nullptr, TEXT("/Game/FactoryGame/Equipment/BuildGun/BP_BuildGun.BP_BuildGun_C")))
    {
        BG->AddToRoot(); // 0.2.7: 裸静态 UClass* 不是 GC 引用
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

// ----------------------------------------------------------------------------
// Build costs. Vanilla parts only, resolved at world-construction time.
//
// v0.2.2 -- 终端下移到 tech tier 1，材料改为 铁棒 / 螺丝 / 铜板。
//
// (v0.2.1 的历史留着，因为同一个坑很容易再踩：这张表曾经写着 Desc_SteelPlateReinforced
//  —— 钢筋混凝土梁 / Encased Industrial Beam，一个 TIER 4 的件 —— 而旁边的注释却说是
//  强化铁板 / Reinforced Iron Plate，另外还混了 Desc_CircuitBoard（电路板，TIER 5）。
//  于是一个 tier-2 的 HUB 里程碑在索要玩家根本还没解锁的东西。
//  ★ 物品中文名一律以游戏自带的 CommunityResources/Docs 为准，别信任何注释。)
//
// ★（0.2.140 后失效的历史取舍）铜板 / Copper Sheet 曾因里程碑停在 tier 1 而高一档；
//   0.2.140 起里程碑挪到 tier 2，铜板（tier 2「部件装配」解锁）与里程碑同档，无解锁差。
//   铁棒（HUB 升级 1）和螺丝（HUB 升级 3）则是开局就有。
// ----------------------------------------------------------------------------
struct FWTCostLine
{
    const TCHAR* ItemPath;
    int32 Amount;
};

static const FWTCostLine GCostTerminal[] = {
    { TEXT("/Game/FactoryGame/Resource/Parts/IronRod/Desc_IronRod.Desc_IronRod_C"), 30 },
    { TEXT("/Game/FactoryGame/Resource/Parts/IronScrew/Desc_IronScrew.Desc_IronScrew_C"), 50 },
    { TEXT("/Game/FactoryGame/Resource/Parts/CopperSheet/Desc_CopperSheet.Desc_CopperSheet_C"), 15 },
};

// The HUB milestone. ★0.2.140（用户 2026-10-20 定案）: 挪到 tier 2，成本改为
//   10×原版对象扫描仪 + 10×铁棒 + 300×螺丝 + 100×铜板（原 200/300/100）。
// 扫描仪描述符路径经两路一手核实（运行时转储类名 + 社区游戏数据转储交叉一致）：
//   /Game/FactoryGame/Resource/Equipment/GemstoneScanner/BP_EquipmentDescriptorObjectScanner
//   ★0.2.141（用户 2026-10-20 定案）-- 里程碑成本与车间（手持终端）制作配方对调：
//   里程碑沿用原手持终端料单（铁棒/螺丝/铜板），扫描仪改到车间配方。
//   —— 注意扫描仪路径在 Resource/Equipment/GemstoneScanner 下；Equipment/ObjectScanner 只有
//   装备 actor/网格/材质，不是物品描述符。扫描仪本体由 T1「Field Research」解锁。
static const TCHAR* GMilestoneCostNames[] = {
    TEXT("/Game/FactoryGame/Resource/Parts/IronRod/Desc_IronRod.Desc_IronRod_C"),
    TEXT("/Game/FactoryGame/Resource/Parts/IronScrew/Desc_IronScrew.Desc_IronScrew_C"),
    TEXT("/Game/FactoryGame/Resource/Parts/CopperSheet/Desc_CopperSheet.Desc_CopperSheet_C"),
};
static const int32 GMilestoneCostAmounts[] = { 20, 30, 10 };

// ----------------------------------------------------------------------------
// Descriptor
// ----------------------------------------------------------------------------
URemote_FactoryDescriptor::URemote_FactoryDescriptor()
{
    mUseDisplayNameAndDescription = true;
    BuildableClass = ARemote_FactoryBuildable::StaticClass();
    HologramClass = ARemote_FactoryHologram::StaticClass();
    DisplayName = WT_LOCTEXT("Building_Remote_Factory", "智能工厂");
    Desc = WT_LOCTEXT("Desc_Remote_Factory",
        "A factory-wide console. Build it and press E: from your HUB you can see the output of "
        "every machine plus total factory generation and draw, open any machine's own panel "
        "remotely, and pull its output straight out.");
    InitCommon();
}

TSubclassOf<AFGHologram> URemote_FactoryDescriptor::GetHologramClassInternal() const { return HologramClass; }
FText URemote_FactoryDescriptor::GetItemNameInternal() const { return DisplayName; }
FText URemote_FactoryDescriptor::GetItemDescriptionInternal() const { return Desc; }

void URemote_FactoryDescriptor::InitCommon()
{
    mBuildableClass = BuildableClass;
    SetBuildCategoryProp(this, URemote_FactoryBuildCategory::StaticClass());
    SetSubCategoriesProp(this, URemote_FactoryBuildSubCategory::StaticClass());
}

// ----------------------------------------------------------------------------
// Recipe
// ----------------------------------------------------------------------------
URemote_FactoryRecipe::URemote_FactoryRecipe()
{
    mManufactoringDuration = 0.0f;
    mProduct.Add(FItemAmount(URemote_FactoryDescriptor::StaticClass(), 1));
}

void URemote_FactoryRecipe::ApplyProducedIn(UClass* BuildGunClass)
{
    if (!BuildGunClass) return;
    mProducedIn.Empty();
    mProducedIn.Add(TSoftClassPtr<UObject>(BuildGunClass));
}

void URemote_FactoryRecipe::ApplyIngredients()
{
    // Idempotent: clear what an earlier call may have written.
    mIngredients.Empty();
    for (const FWTCostLine& Line : GCostTerminal)
    {
        UClass* ItemClass = StaticLoadClass(UFGItemDescriptor::StaticClass(), nullptr, Line.ItemPath);
        if (!ItemClass)
        {
            WT_WARNF(TEXT("[registry] build cost could not resolve item '%s'."), Line.ItemPath);
            continue;
        }
        ItemClass->AddToRoot(); // 0.2.7: 裸 UClass* 不是 GC 引用，钉死防首次 GC 回收
        mIngredients.Add(FItemAmount(TSubclassOf<UFGItemDescriptor>(ItemClass), Line.Amount));
    }
    WT_LOGF(TEXT("[registry] terminal build cost: %d ingredient lines."), mIngredients.Num());
}

// ----------------------------------------------------------------------------
// Build category / sub-category
// ----------------------------------------------------------------------------
// ★1.0.12：建造分类图标填充 helper。参数是 FSlateBrush 本体引用 —— 自由函数只碰 brush，
//   不碰任何 protected 成员；赋值 `mCategoryIcon = ...` 由两个派生类构造函数自己做
//   （protected 成员自由函数直写会 C2248，本文件 SetBuildCategoryProp 注释记着同一坑）。
static FSlateBrush WT_MakeCategoryIconBrush()
{
    FSlateBrush Brush;
    UTexture2D* Icon = nullptr;
    static const TCHAR* const IconRoots[] = {
        TEXT("/Smart_Factory/"),
        TEXT("/Game/Smart_Factory/"),
        TEXT("/Remote_Factory/"),
        TEXT("/Game/Remote_Factory/"),
        TEXT("/WirelessTerminal/"),          // 改名前祖根（BP 资产内部旧路径兜底，铁律保留）
        TEXT("/Game/WirelessTerminal/"),
    };
    for (const TCHAR* Root : IconRoots)
    {
        Icon = LoadObject<UTexture2D>(nullptr,
            *(FString(Root) + TEXT("Icon/icon_handheld_terminal.icon_handheld_terminal")));
        if (Icon)
        {
            break;
        }
    }
    if (!Icon)
    {
        // 无图标时返回默认刷子（什么都不画）= 维持旧行为，但明确告警。
        WT_WARNF(TEXT("[registry] build-category icon texture not found -- category stays blank."));
        return Brush;
    }
    Brush.SetResourceObject(Icon);
    Brush.ImageSize = FVector2D(Icon->GetSizeX(), Icon->GetSizeY());
    Brush.DrawAs = ESlateBrushDrawType::Image;
    return Brush;
}

URemote_FactoryBuildCategory::URemote_FactoryBuildCategory()
{
    mDisplayName = WT_LOCTEXT("Building_Remote_Factory", "智能工厂");
    // ★1.0.12：UFGCategory::mCategoryIcon 从未被设过 ⇒ 建造枪左栏「智能工厂」一直空白
    //   （1.0.11 用户截图实锤）。贴图复用里程碑同款 icon_handheld_terminal。
    mCategoryIcon = WT_MakeCategoryIconBrush();
}

URemote_FactoryBuildSubCategory::URemote_FactoryBuildSubCategory()
{
    mDisplayName = WT_LOCTEXT("Building_Remote_Factory", "智能工厂");
    mCategoryIcon = WT_MakeCategoryIconBrush();
}

// ----------------------------------------------------------------------------
// Milestone
// ----------------------------------------------------------------------------
URemote_FactoryMilestone::URemote_FactoryMilestone()
{
    // Content-free fields only. mCost / mSchematicCategory need /Game/... classes, which
    // ApplyMilestoneData() resolves once the game content is mounted.
    mType = ESchematicType::EST_Milestone;
    // ★0.2.141（用户 2026-10-20 定案）-- 里程碑成本与车间配方对调：里程碑改采原手持终端料单
    // （20×铁棒 + 30×螺丝 + 10×铜板），扫描仪不再出现在里程碑；tier 维持 2（T1 扫描仪即可做出，无卡点）。
    // （v0.2.2 的 tier-1 定案与「两个升级建筑拆其它 tier」的旧注释随 0.2.56 建筑移除一并过时。）
    mTechTier = 2;
    mDisplayName = WT_LOCTEXT("Building_Remote_Factory", "智能工厂");
    mDescription = WT_LOCTEXT("Milestone_Remote_Factory",
        "Unlocks the handheld Smart Factory and the High-Capacity Storage Unit. Equip the "
        "terminal from your inventory to browse every machine, operate them remotely, pull their "
        "output and teleport -- from anywhere in the world.");
    mMenuPriority = 0.0f;
    mTimeToComplete = 60.0f;
    mIsPlayerSpecific = false;
    mDependenciesBlocksSchematicAccess = false;
    mHiddenUntilDependenciesMet = false;
}

// ----------------------------------------------------------------------------
// Milestone population.
//
// All of the writes below touch protected members of UFGSchematic / UFGUnlockRecipe. They live in
// URemote_FactoryMilestone member functions on purpose: AccessTransformers.ini opens those members
// only to this class (`Friend=(..., FriendClass="URemote_FactoryMilestone")`). A free function --
// even one in the same translation unit -- is not the friend and will not compile.
// ----------------------------------------------------------------------------

FString URemote_FactoryMilestone::InstallCostCategoryAndUnlock()
{
    // ★v0.2.24 图标三件套之一（见 Remote_FactoryUpgradeRegistry.h 的 WT_ApplySchematicIcon 注释）。
    // T1 里程碑此前同样没写图标 → HUB 把费用行和奖励面板一起藏掉。
    WT_ApplySchematicIcon(this);

    // ---- cost ----
    mCost.Empty();
    const int32 Count = UE_ARRAY_COUNT(GMilestoneCostNames);
    for (int32 i = 0; i < Count; ++i)
    {
        UClass* ItemClass = StaticLoadClass(UFGItemDescriptor::StaticClass(), nullptr, GMilestoneCostNames[i]);
        if (!ItemClass)
        {
            WT_WARNF(TEXT("[registry] milestone cost item '%s' did not resolve."), GMilestoneCostNames[i]);
            continue;
        }
        ItemClass->AddToRoot(); // 0.2.7: 裸 UClass* 不是 GC 引用，钉死防首次 GC 回收
        mCost.Add(FItemAmount(TSubclassOf<UFGItemDescriptor>(ItemClass), GMilestoneCostAmounts[i]));
    }

    // ---- category ----
    // The vanilla Logistics category, where Feyko's own milestones also sit, so the terminal
    // shows up next to the rest of the progression instead of inventing a new tab.
    // (Path verified against WirelessPower's working registration.)
    static const TCHAR* const GLogisticsCategory =
        TEXT("/Game/FactoryGame/Schematics/SchematicCategories/SC_Logistics.SC_Logistics_C");
    if (UClass* Category = StaticLoadClass(UFGSchematicCategory::StaticClass(), nullptr, GLogisticsCategory))
    {
        mSchematicCategory = TSubclassOf<UFGSchematicCategory>(Category);
    }
    else
    {
        WT_WARNF(TEXT("[registry] could not resolve the vanilla Logistics schematic category."));
        return TEXT("category missing");
    }

    // ---- unlock ----
    // mUnlocks must hold a BP_UnlockRecipe_C instance: the HUB's reward panel is drawn by
    // blueprint graphs that only that class carries, so a native UFGUnlockRecipe renders an
    // empty panel (WirelessPower v1.0.44 recorded this; same fix here).
    static UClass* BPUnlockClass = nullptr;
    if (!BPUnlockClass)
    {
        BPUnlockClass = StaticLoadClass(UFGUnlockRecipe::StaticClass(), nullptr,
            TEXT("/Game/FactoryGame/Unlocks/BP_UnlockRecipe.BP_UnlockRecipe_C"));
        if (BPUnlockClass)
        {
            BPUnlockClass->AddToRoot(); // 0.2.7: 静态裸 UClass* 不是 GC 引用
        }
    }

    if (!BPUnlockClass)
    {
        WT_WARNF(TEXT("[registry] could not load BP_UnlockRecipe — HUB rewards will stay blank."));
        return TEXT("BP_UnlockRecipe missing");
    }

    // ★跨世界重建（0.2.56）：每次都先把 CDO 上残留的 BP_UnlockRecipe_C 实例（若已 AddToRoot 则先
    // RemoveFromRoot）从 mUnlocks 摘掉，再建一个全新的、本世界专用的解锁实例。直接 NewObject +
    // AddToRoot 而不清理旧实例，会让旧的 process-persistent 实例在「同进程换档」时仍存活，越过它被
    // 注册时所依赖的 SML FGameObjectRegistryState，下一次重注册走到失效的 recipe→item ReferencedBy
    // （Data=NULL/Num>=2）而在 ModContentRegistry.cpp AddObjectReference 读 0x8 崩溃。
    // 这样保证任意时刻每个里程碑 CDO 上只有「一个」被 root 的解锁实例，且随每次重注册干净重建。
    // 注意：this（里程碑 CDO）是 process-immortal，其 mUnlocks 跨世界保留；下面这段清理正是为了让
    // 换档后第一个 InstallBlueprintUnlock 调用就把上一世界遗留的实例拔掉。
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
        // ★自检加固（2026-10-07）：NewObject 只在类无效/无法构造时返回 null，但紧接着就会
        // AddUnlockRecipes / AddToRoot / GetClass 三连解引用 —— 不判空就是把一次分配失败
        // 升级成必崩。同文件另一处（UpgradeRegistry.cpp）本来就有这道守卫，这里补齐。
        WT_WARNF(TEXT("[registry] NewObject<UFGUnlockRecipe> returned null; milestone unlock skipped."));
        return TEXT("unlock allocation failed");
    }
    AddUnlockRecipes(Unlock);

    // ROOT-PIN. AFGBuildable::BeginPlay walks every schematic's mUnlocks through
    // FixupSave_FindBuiltByRecipe; a runtime NewObject'ed, unrooted instance gets
    // reaped by GC and leaves a dangling pointer in that walk (an access violation
    // WirelessPower hit in v1.0.45/46). One tiny object pinned for process lifetime
    // -- but removed (RemoveFromRoot) on every rebuild above, so no cross-world accumulation.
    Unlock->AddToRoot();
    mUnlocks.Add(Unlock);

    WT_LOGF(TEXT("[registry] milestone unlock installed: %s recipes=%d (root-pinned, rebuilt this call)."),
            *Unlock->GetClass()->GetName(), Unlock->mRecipes.Num());

    return FString();
}

void URemote_FactoryMilestone::AddUnlockRecipes(UFGUnlockRecipe* Unlock) const
{
    if (!Unlock)
    {
        return;
    }
    // UFGUnlockRecipe::mRecipes is protected; this class is its declared friend.
    Unlock->mRecipes.Empty();
    // v0.2.3: HANDHELD-ONLY (user decision). The fixed terminal's build-gun recipe is retired
    // from progression: new saves can no longer build the placed form. The class, descriptor and
    // recipe stay compiled and registered so terminals placed by older saves keep loading,
    // saving and working (mAvailableRecipes is SaveGame -- already-unlocked saves keep theirs).
    //
    // ★0.2.32（用户定案）：
    //   * ★0.2.56 修订：升级建筑不再随 T1 里程碑解锁（见下方 AddUnlockRecipes 注释）；里程碑只给
    //     手持终端，升级建筑改由 MAM 研究树解锁 + 读档补发（WT_RepairRecipeAvailability）保证可获得性。
    //     MAM 研究树只研究「对云端的加成」。
    //   * ★【产能不做建筑】：`AWTUpgradeAccelerator` 退役 —— 不在这里解锁，也没有任何研究节点
    //     再发它。产能轨改走「纯物品」路线：原版能量碎片二次加工成新版能量碎片，插进原版机器的
    //     能量碎片插槽。类与配方**保留编译注册**（旧存档里已放置的实例要能继续加载/保存），
    //     只是玩家再也拿不到 —— 与 v0.2.3 退役固定式终端同一套做法。
    //
    //   ⚠️ SaveGame 粘滞提示：mAvailableRecipes 是存档字段且**只增不减**，而已购 schematic
    //   的 unlock 不会再执行一次 —— 所以**已经买过 T1 的旧存档不会补发**这两个配方。
    //   新存档、以及尚未买 T1 的存档都正常。
    // ★0.2.56（用户定案）：T1 里程碑只解锁「手持无线终端」+「强化碎片」两样。5 个升级建筑（大容量仓库 /
    // 电力存储 / 云端下载站 / 云端流体上传·下载站）从 mod 彻底移除（研究树已清空、Wanted 已去除、里程碑亦不列）。
    Unlock->mRecipes.AddUnique(URemote_FactoryEquipRecipe::StaticClass());
    // ★0.2.134：强化能量碎片改由 WT_FEATURE_POWER_SHARD 单开关控制（见 Remote_FactoryRegistry.h）。
    //   0.2.57 注：强化碎片是**两个**配方——构造机 + 量子编码器，少给编码器会导致新档买了 T1
    //   只拿到构造机配方，编码器要等读档补发。开关打开时两条一起发。
    if (WT_FEATURE_POWER_SHARD)
    {
        Unlock->mRecipes.AddUnique(UWTUpgradePowerShardRecipe::StaticClass());
        Unlock->mRecipes.AddUnique(UWTUpgradePowerShardSynthRecipe::StaticClass());
    }
}

namespace Remote_Factory
{
    void ApplyMilestoneData()
    {
        URemote_FactoryMilestone* Milestone = GetMutableDefault<URemote_FactoryMilestone>();
        if (!Milestone)
        {
            return;
        }

        const FString Status = Milestone->InstallCostCategoryAndUnlock();
        if (!Status.IsEmpty())
        {
            WT_WARNF(TEXT("[registry] milestone setup incomplete: %s"), *Status);
        }
    }

    void RegisterBuildables(UWorld* World)
    {
        if (!World) return;

        AFGRecipeManager* RM = AFGRecipeManager::Get(World);
        if (!RM)
        {
            WT_WARNF(TEXT("[registry] AFGRecipeManager not ready; skipping registration."));
            return;
        }

        // Fill in the milestone category/cost. Safe to repeat (idempotent).
        ApplyMilestoneData();

        UClass* BuildGunClass = WT_ResolveBuildGunClass();
        WT_LOGF(TEXT("[registry] Build Gun class = %s"),
                BuildGunClass ? *BuildGunClass->GetPathName() : TEXT("NULL"));

        // mProducedIn must point at the real Build Gun class, or
        // GetAvailableRecipesForProducer() never returns our recipe.
        if (URemote_FactoryRecipe* RecipeCDO =
                Cast<URemote_FactoryRecipe>(URemote_FactoryRecipe::StaticClass()->GetDefaultObject()))
        {
            RecipeCDO->ApplyProducedIn(BuildGunClass);
            RecipeCDO->ApplyIngredients();
        }

        // Handheld terminal (form 2): the same content is mounted and the workbench class is only
        // resolvable now, so it is registered on the same beat.
        RegisterHandheld();

        WT_LOGF(TEXT("[registry] terminal registered: descAvail=%d recipeAvail=%d"),
                RM->IsItemDescriptorAvailable(URemote_FactoryDescriptor::StaticClass()) ? 1 : 0,
                RM->IsRecipeAvailable(URemote_FactoryRecipe::StaticClass()) ? 1 : 0);
    }
}
