#include "Remote_FactoryUpgrades.h"

#include "Remote_FactoryFluidPanel.h"
#include "Remote_FactoryLoc.h"
#include "Remote_FactoryLog.h"
#include "Remote_FactoryCrashTrace.h"
#include "Remote_FactoryUpgradeResearch.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableFactory.h"
#include "Buildables/FGBuildablePipeReservoir.h"
#include "Buildables/FGCentralStorageContainer.h"
#include "FGCentralStorageSubsystem.h"
#include "FGFactoryConnectionComponent.h"     // ★0.2.42 下载站传送带接口
#include "FGInventoryComponent.h"
#include "FGInventoryLibrary.h"
#include "FGPipeConnectionComponent.h"
#include "FGPowerConnectionComponent.h"       // ★0.2.42 蓄电池电力接口
#include "FGPowerInfoComponent.h"
#include "FGColoredInstanceMeshProxy.h"          // ★0.2.48 J 修复：mMeshMesh 是 UFGColoredInstanceMeshProxy，需完整类型定义才能调用其 SetStaticMesh/SetUserDefinedData/SetNumCustomDataFloats 等方法（否则 C2027 未定义类型）
#include "Resources/FGItemDescriptor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"   // ★0.2.48 电池建模：原版 SCS 的 SK 内部电池体
#include "Containers/Ticker.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/BlueprintGeneratedClass.h"    // ★0.2.48 SCS 枚举：蓝图组件模板的唯一正规入口
#include "Engine/SimpleConstructionScript.h"   // ★0.2.48 USimpleConstructionScript::GetAllNodes()
#include "Engine/SCS_Node.h"                   // ★0.2.48 USCS_Node::GetActualComponentTemplate()
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"            // CreateDynamicMaterialInstance 返回类型
#include "UObject/ConstructorHelpers.h"
#include "UObject/UObjectIterator.h"          // ★0.2.48 TObjectIterator（WS_FindVanillaClass / 描述符兜底）

/**
 * Write a float property by name on `Object`, whichever access section it lives in.
 *
 * The properties this file needs to change are all `private:` in the shipped SDK headers, so there
 * is no C++ spelling of the assignment. Reflection does not care about access specifiers -- they are
 * a compile-time construct -- so this is the correct tool rather than a workaround. It is also the
 * route the rest of this mod already takes for mIsUseable, mInteractWidgetSoftClass and mStackSize.
 */
static bool WT_SetFloatProperty(UObject* Object, const TCHAR* PropertyName, float Value)
{
    const UClass* Class = Object ? Object->GetClass() : nullptr;
    if (!Class)
    {
        return false;
    }

    FFloatProperty* Prop = FindFProperty<FFloatProperty>(Class, PropertyName);
    if (!Prop)
    {
        WT_WARNF(TEXT("[upgrade] %s::%s not found — the upgrade will keep its vanilla value."),
                 *Class->GetName(), PropertyName);
        return false;
    }

    Prop->SetPropertyValue_InContainer(Object, Value);
    WT_LOGF(TEXT("[upgrade] %s.%s <= %.3f"), *Class->GetName(), PropertyName, Value);
    return true;
}

/** Same, for an int32 property (the storage grid is sized by two of these). */
static bool WT_SetIntProperty(UObject* Object, const TCHAR* PropertyName, int32 Value)
{
    const UClass* Class = Object ? Object->GetClass() : nullptr;
    if (!Class)
    {
        return false;
    }

    FIntProperty* Prop = FindFProperty<FIntProperty>(Class, PropertyName);
    if (!Prop)
    {
        WT_WARNF(TEXT("[upgrade] %s::%s not found."), *Class->GetName(), PropertyName);
        return false;
    }

    Prop->SetPropertyValue_InContainer(Object, Value);
    WT_LOGF(TEXT("[upgrade] %s.%s <= %d"), *Class->GetName(), PropertyName, Value);
    return true;
}

/**
 * Numeric property setter that survives any integer width.
 *
 * mIndicatorLevelMax is a uint8 on AFGBuildablePowerStorage; FIntProperty::FindFProperty misses
 * it. FNumericProperty::SetPropertyValue(void*, int64) dispatches by the property's real type, so
 * one helper covers uint8 / int32 / float alike.
 */
static bool WT_SetNumericProperty(UObject* Object, const TCHAR* PropertyName, double Value)
{
    const UClass* Class = Object ? Object->GetClass() : nullptr;
    if (!Class)
    {
        return false;
    }

    FNumericProperty* Prop = FindFProperty<FNumericProperty>(Class, PropertyName);
    if (!Prop)
    {
        WT_WARNF(TEXT("[upgrade] %s::%s (numeric) not found."), *Class->GetName(), PropertyName);
        return false;
    }

    // ★UE5.6：FNumericProperty::SetPropertyValue(void*,int64) 已移除，改用虚函数
    //   SetIntPropertyValue —— FByteProperty(uint8)/FIntProperty/FFloatProperty 各自的
    //   override 会按属性真实类型收敛（float 属性走 SetFloatPropertyValue 内部路径）。
    Prop->SetIntPropertyValue(Prop->ContainerPtrToValuePtr<void>(Object),
                              static_cast<int64>(Value));
    WT_LOGF(TEXT("[upgrade] %s.%s <= %.3f (numeric)"), *Class->GetName(), PropertyName, Value);
    return true;
}

/**
 * ★0.2.42 连接接口**可见性诊断**（用户反馈「云端的四个建筑都无法接传送带/管道」）。
 *
 * 为什么必须用游戏自己的访问器：
 *   * 传送带这边，全息吸附找的是 `AFGBuildableFactory::GetConnectionComponents()`；
 *   * 管道这边找的是 `AFGBuildablePipeReservoir::GetPipeConnections()`（我们 override 的那个）。
 *   直接问它们要一遍，就能把「我们的组件没被游戏注册」和「注册了但全息拒绝吸附」两件事
 *   在**同一条日志**里分开 —— 上一轮（0.2.41）我们只知道自己建了组件，没有这个证据。
 * 每类一次（WT_WARNF_ONCE），不会因为读档 N 栋刷屏。
 */
static void WT_LogConnectionPorts(AFGBuildable* Self, const TCHAR* Tag)
{
    if (!Self)
    {
        return;
    }
    FString FactoryDesc, PipeDesc;
    int32 NumFactory = 0, NumPipe = 0;

    if (AFGBuildableFactory* Factory = Cast<AFGBuildableFactory>(Self))
    {
        TArray<UFGFactoryConnectionComponent*> Conns = Factory->GetConnectionComponents();
        NumFactory = Conns.Num();
        for (UFGFactoryConnectionComponent* C : Conns)
        {
            if (!C)
            {
                continue;
            }
            FactoryDesc += FString::Printf(TEXT("[%s dir=%d rel=%s loc=%s conn=%d]"),
                *C->GetName(), static_cast<int32>(C->GetDirection()),
                *C->GetRelativeLocation().ToCompactString(),
                *C->GetComponentLocation().ToCompactString(), C->IsConnected() ? 1 : 0);
        }
    }
    if (AFGBuildablePipeReservoir* Res = Cast<AFGBuildablePipeReservoir>(Self))
    {
        TArray<UFGPipeConnectionComponent*> Pipes = Res->GetPipeConnections();
        NumPipe = Pipes.Num();
        for (UFGPipeConnectionComponent* P : Pipes)
        {
            if (!P)
            {
                continue;
            }
            PipeDesc += FString::Printf(TEXT("[%s type=%d rel=%s loc=%s conn=%d]"),
                *P->GetName(), static_cast<int32>(P->GetPipeConnectionType()),
                *P->GetRelativeLocation().ToCompactString(),
                *P->GetComponentLocation().ToCompactString(), P->IsConnected() ? 1 : 0);
        }
    }

    WT_WARNF_ONCE(Tag, Self,
        TEXT("[upgrade] %s ports: factory=%d %s| pipe=%d %s"),
        *Self->GetClass()->GetName(), NumFactory, *FactoryDesc, NumPipe, *PipeDesc);
}

/**
 * ★0.2.36 可配置 UI：C++ 子类不带原版蓝图的交互面板。
 *
 * `mInteractWidgetSoftClass`（TSoftClassPtr，反射类型 FSoftObjectProperty）是原版在
 * Build_* 蓝图里填的，原生 CDO 上是 None —— 玩家对准我们的建筑按 E 什么都打不开。
 * 修法：从原版对应建筑的蓝图 CDO 把面板软类抄过来，同时把 mIsUseable 置真。
 * 原版面板完整复用（电池面板/云端仓库面板/仓库面板），我们零自绘。
 */
static void WT_ReuseVanillaInteractWidget(UObject* Target, UClass* VanillaBuildableClass)
{
    if (!Target || !VanillaBuildableClass)
    {
        return;
    }

    UObject* SrcCDO = VanillaBuildableClass->GetDefaultObject();
    UClass* C = Target->GetClass();

    FSoftObjectProperty* Src = CastField<FSoftObjectProperty>(
        VanillaBuildableClass->FindPropertyByName(TEXT("mInteractWidgetSoftClass")));
    FSoftObjectProperty* Dst = CastField<FSoftObjectProperty>(
        C->FindPropertyByName(TEXT("mInteractWidgetSoftClass")));
    if (Src && Dst && SrcCDO)
    {
        // ★★0.2.42 内存布局修复（「五个建筑没有配置面板」的真正根因）：
        //   `mInteractWidgetSoftClass` = TSoftClassPtr ⇒ 反射类型 FSoftObjectProperty
        //   （= TFObjectPropertyBase<FSoftObjectPtr>）。而 FSoftObjectPtr 继承自
        //   TPersistentObjectPtr<FSoftObjectPath>，其成员顺序是：
        //       offset 0 : mutable FWeakObjectPtr WeakPtr     ← 引擎源码 PersistentObjectPtr.h:272
        //       其后     : TObjectID ObjectID (= FSoftObjectPath) ← 真正存路径的字段
        //   旧代码 `*Src->ContainerPtrToValuePtr<FSoftObjectPath>(SrcCDO)` 把**偏移 0** 当成
        //   FSoftObjectPath 读 —— 读出来的是 WeakPtr 的字节 ⇒ 路径永远 "无效"；写入同理只会
        //   把 WeakPtr 写坏、真正的路径字段一个字都没动。⇒ 全部原版面板抄不过来。
        //   （实证日志：`vanilla Build_CentralStorage_C has no interact widget` —— 而原版蓝图
        //     明明在 CDO 上配了 BPW_CentralStorage。）
        //   必须走 FSoftObjectPtr 的公开 API：ToSoftObjectPath() 读、拷贝赋值写。
        const FSoftObjectPtr& SrcPtr = *Src->ContainerPtrToValuePtr<FSoftObjectPtr>(SrcCDO);
        const FSoftObjectPath WidgetPath = SrcPtr.ToSoftObjectPath();
        if (WidgetPath.IsValid())
        {
            FSoftObjectPtr& DstPtr = *Dst->ContainerPtrToValuePtr<FSoftObjectPtr>(Target);
            DstPtr = FSoftObjectPtr(WidgetPath);   // 整体赋值：WeakPtr 与 ObjectID 一起重置
            // ★0.2.36d 每类一次：本函数由构造函数调用（每实例一遍），成功路径不该刷屏。
            WT_WARNF_ONCE(TEXT("reuse-widget"), Target,
                     TEXT("[upgrade] %s interact widget <= %s"),
                     *C->GetName(), *WidgetPath.ToString());
        }
        else
        {
            WT_WARNF_ONCE(TEXT("reuse-widget-none"), Target,
                     TEXT("[upgrade] vanilla %s has no interact widget -- UI stays missing."),
                     *VanillaBuildableClass->GetName());
        }
    }
    else
    {
        WT_WARNF_ONCE(TEXT("reuse-widget-missing"), Target,
                 TEXT("[upgrade] mInteractWidgetSoftClass not found (src=%d dst=%d)."),
                 Src ? 1 : 0, Dst ? 1 : 0);
    }

    // mIsUseable 是位域 bool（uint8 mIsUseable:1）——反射类型 FBoolProperty，ImportText 走得通。
    if (FBoolProperty* UseProp = CastField<FBoolProperty>(C->FindPropertyByName(TEXT("mIsUseable"))))
    {
        void* Mem = UseProp->ContainerPtrToValuePtr<void>(Target);
        UseProp->SetPropertyValue(Mem, true);
    }
}

/**
 * Resolve a vanilla class by asset name without betting on a folder path.
 *
 * Try well-known paths first, then sweep the class iterator for the exact authored name. The
 * sweep is only hit once per name (static cache), so the O(n) cost lands at first use, not per
 * placement.
 */
static UClass* WT_FindVanillaClass(const TCHAR* PrimaryPath, const TCHAR* AuthoredName,
                                   UClass* BaseClass)
{
    if (UClass* C = StaticLoadClass(BaseClass, nullptr, PrimaryPath))
    {
        return C;
    }
    for (TObjectIterator<UClass> It; It; ++It)
    {
        UClass* C = *It;
        if (C->IsChildOf(BaseClass) && C->GetName().Equals(AuthoredName))
        {
            return C;
        }
    }
    WT_WARNF(TEXT("[upgrade] vanilla class '%s' not found (primary=%s)."), AuthoredName, PrimaryPath);
    return nullptr;
}

/**
 * 把 (物品类, 数量) 直接写进云端池的 mStoredItems —— **兜底路径**（★0.2.36b）。
 *
 * ★用户定案「为什么不走原版云端接口调用」之后：入池的**主路径**改为
 * `WT_UploadStackToPool()`（临时单格库存 → 原版 `UploadItemFromInventoryToCentralStorage`，
 * 见下），本函数降级为「原版接口不可用时」的最后兜底。
 *
 * 之所以还需要它：mStoredItems 是 AFGCentralStorageSubsystem 的 private SaveGame+Replicated
 * TArray<FItemAmount>，按物品类聚合。原版公开入口需要一个**真实库存组件**，遇到无法构造
 * 库存组件的极端情形时只有反射直写能把货送进去 —— 代价是绕过原版的物品上限检查与通知
 * （OnRep/委托），所以调用处一律打 WARN 日志，便于事后确认是否真的走了兜底。
 * 与 0.2.36 抄 mInteractWidgetSoftClass / 写 mMaxPotential 同一反射惯例。
 * 流体类与固体类是不同的 ItemClass，条目天然分开。
 *
 * 条目限容量（GetCentralStorageItemLimit = 组数限 × 堆叠上限）对流体不适用：
 * 流体描述符没有堆叠组概念，这里不做配额，云端池对流体按件数无限累积。
 *
 * @return 记账后的该类总量（失败返回 -1）。
 */
static int32 WT_AddToCentralStorage(UWorld* World, UClass* ItemClass, int32 Amount)
{
    AFGCentralStorageSubsystem* Sub = World ? AFGCentralStorageSubsystem::Get(World) : nullptr;
    if (!Sub || !ItemClass || Amount <= 0)
    {
        return -1;
    }

    static FArrayProperty* ArrProp = nullptr;
    static FStructProperty* Inner = nullptr;
    static bool bLookedUp = false;
    if (!bLookedUp)
    {
        bLookedUp = true;
        ArrProp = CastField<FArrayProperty>(AFGCentralStorageSubsystem::StaticClass()
            ->FindPropertyByName(TEXT("mStoredItems")));
        if (ArrProp)
        {
            Inner = CastField<FStructProperty>(ArrProp->Inner);
        }
        if (!Inner)
        {
            WT_ERRF(TEXT("[cloud-fluid] mStoredItems reflection missing -- fluid upload disabled."));
        }
    }
    if (!ArrProp || !Inner)
    {
        return -1;
    }

    FObjectPropertyBase* ItemProp = CastField<FObjectPropertyBase>(
        Inner->Struct->FindPropertyByName(TEXT("ItemClass")));
    FIntProperty* AmountProp = CastField<FIntProperty>(
        Inner->Struct->FindPropertyByName(TEXT("Amount")));
    if (!ItemProp || !AmountProp)
    {
        WT_ERRF(TEXT("[cloud-fluid] FItemAmount fields missing -- fluid upload disabled."));
        return -1;
    }

    FScriptArrayHelper Helper(ArrProp, ArrProp->ContainerPtrToValuePtr<void>(Sub));
    for (int32 i = 0; i < Helper.Num(); ++i)
    {
        void* Elem = Helper.GetRawPtr(i);
        if (ItemProp->GetObjectPropertyValue(Elem) == ItemClass)
        {
            int32& Cur = *reinterpret_cast<int32*>(AmountProp->ContainerPtrToValuePtr<void>(Elem));
            Cur += Amount;
            return Cur;
        }
    }

    const int32 NewIdx = Helper.AddValue();
    void* Elem = Helper.GetRawPtr(NewIdx);
    ItemProp->SetObjectPropertyValue(Elem, ItemClass);
    *reinterpret_cast<int32*>(AmountProp->ContainerPtrToValuePtr<void>(Elem)) = Amount;
    return Amount;
}

/**
 * ★0.2.36 液态下载站：把面板软类指到本模组**原生 C++** UFGInteractWidget 子类
 * （选型面板不带蓝图资产，路径就是类自身的 PathName）。与 WT_ReuseVanillaInteractWidget
 * 的差别只在来源：这里是我们自己的类，不是原版蓝图 CDO。
 */
static void WT_UseNativeInteractPanel(UObject* Target, UClass* PanelClass)
{
    if (!Target || !PanelClass)
    {
        return;
    }

    UClass* C = Target->GetClass();
    if (FSoftObjectProperty* P = CastField<FSoftObjectProperty>(
            C->FindPropertyByName(TEXT("mInteractWidgetSoftClass"))))
    {
        // ★★0.2.42 同 WT_ReuseVanillaInteractWidget 的布局修复：必须按 FSoftObjectPtr 写
        //   （偏移 0 是 WeakPtr，不是 FSoftObjectPath），否则原生面板同样挂不上。
        FSoftObjectPtr& DstPtr = *P->ContainerPtrToValuePtr<FSoftObjectPtr>(Target);
        DstPtr = FSoftObjectPtr(FSoftObjectPath(PanelClass));
        // ★0.2.36d 每类一次（同 WT_ReuseVanillaInteractWidget：构造函数里每实例一遍）。
        WT_WARNF_ONCE(TEXT("native-panel"), Target,
                 TEXT("[upgrade] %s interact widget <= native %s"),
                 *C->GetName(), *PanelClass->GetName());
    }
    else
    {
        WT_ERRF(TEXT("[upgrade] mInteractWidgetSoftClass not found on %s."), *C->GetName());
    }

    if (FBoolProperty* UseProp = CastField<FBoolProperty>(C->FindPropertyByName(TEXT("mIsUseable"))))
    {
        UseProp->SetPropertyValue(UseProp->ContainerPtrToValuePtr<void>(Target), true);
    }
}

/**
 * ★0.2.36b 本模组自己的云账本操作（用户定案：建造取料「原版云端优先，原版没有才用我们的」）。
 *
 * 为什么必须有账本：原版云端池 mStoredItems 是**按物品类聚合、没有来源字段**的一张表，
 * 原版上传站和我们的上传站往里记的是同一行。要让「原版优先」成立，我们的货就不能直接进池，
 * 必须先记在自己名下的账本里（上传站自持、SaveGame 持久化），只在原版池里该类为 0 时
 * 才由回退 ticker 注进去。
 */
static void WT_LedgerAdd(TArray<FItemAmount>& Ledger, UClass* ItemClass, int32 Amount)
{
    if (!ItemClass || Amount <= 0)
    {
        return;
    }
    for (FItemAmount& IA : Ledger)
    {
        if (IA.ItemClass.Get() == ItemClass)
        {
            IA.Amount += Amount;
            return;
        }
    }
    Ledger.Add(FItemAmount(TSubclassOf<UFGItemDescriptor>(ItemClass), Amount));
}

static int32 WT_LedgerTake(TArray<FItemAmount>& Ledger, UClass* ItemClass, int32 Want)
{
    if (!ItemClass || Want <= 0)
    {
        return 0;
    }
    for (FItemAmount& IA : Ledger)
    {
        if (IA.ItemClass.Get() == ItemClass)
        {
            const int32 Take = FMath::Min(IA.Amount, Want);
            IA.Amount -= Take;
            return Take;
        }
    }
    return 0;
}

static int32 WT_LedgerGet(const TArray<FItemAmount>& Ledger, UClass* ItemClass)
{
    if (!ItemClass)
    {
        return 0;
    }
    for (const FItemAmount& IA : Ledger)
    {
        if (IA.ItemClass.Get() == ItemClass)
        {
            return IA.Amount;
        }
    }
    return 0;
}

/** 原版云端池里该类物品现有多少（0 = 原版"没修建 / 没存货"）。 */
static int32 WT_PoolAmount(UWorld* World, UClass* ItemClass)
{
    AFGCentralStorageSubsystem* Sub = World ? AFGCentralStorageSubsystem::Get(World) : nullptr;
    if (!Sub || !ItemClass)
    {
        return 0;
    }
    return Sub->GetNumItemsFromCentralStorage(TSubclassOf<UFGItemDescriptor>(ItemClass));
}

/**
 * ★0.2.36b 往原版云端池"加货" —— **优先走原版公开接口**。
 *
 * 原版唯一公开的入口是 `UploadItemFromInventoryToCentralStorage(库存组件, 格子号)`，
 * 它要求货躺在一个真实库存组件里 —— 所以这里建一个**临时单格库存**把货装进去再交给原版：
 * 由原版自己写 mStoredItems，通知（OnRep/委托）、复制、物品上限检查全部归原版管。
 *
 * 反射直写 mStoredItems 只作兜底（原版接口不可用时），那条路会静默绕过上面这些。
 *
 * @return 实际入池数量（失败返回 -1）。
 */
static int32 WT_UploadStackToPool(UWorld* World, UClass* ItemClass, int32 Amount)
{
    AFGCentralStorageSubsystem* Sub = World ? AFGCentralStorageSubsystem::Get(World) : nullptr;
    if (!Sub || !ItemClass || Amount <= 0)
    {
        return -1;
    }

    if (UFGInventoryComponent* Temp = NewObject<UFGInventoryComponent>(Sub))
    {
        // ★自检加固（2026-10-07）：Temp 只被一个 C++ 栈指针持有 —— **GC 看不见它**。
        //   原版 UploadItemFromInventoryToCentralStorage 内部会走委托/网络复制等逻辑，
        //   期间若发生一次对象回收，Temp 就成悬垂，下一轮 AddStack 直接崩。
        //   临时钉住，离开本函数前摘掉（不能常驻 AddToRoot：回退 ticker 每 5s 调一次，
        //   常驻会让临时库存对象持续泄漏）。
        Temp->AddToRoot();
        Temp->Resize(1);
        int32 Remaining = Amount;
        int32 Uploaded = 0;

        // 原版每次调用上传多少不确定（原版容器是 1 件/秒地喂），循环到喂完或原版拒收为止。
        for (int32 Guard = 0; Guard < 200 && Remaining > 0; ++Guard)
        {
            const int32 Added = Temp->AddStack(
                FInventoryStack(Remaining, TSubclassOf<UFGItemDescriptor>(ItemClass)), true);
            if (Added <= 0)
            {
                break;
            }

            const int32 Before = Sub->GetNumItemsFromCentralStorage(TSubclassOf<UFGItemDescriptor>(ItemClass));
            if (!Sub->UploadItemFromInventoryToCentralStorage(Temp, 0))
            {
                break;   // 原版不收（上限满/类型不收）
            }
            const int32 After = Sub->GetNumItemsFromCentralStorage(TSubclassOf<UFGItemDescriptor>(ItemClass));
            const int32 Moved = FMath::Max(0, After - Before);
            if (Moved <= 0)
            {
                break;   // 调用了但池子没涨 = 原版没真的收下
            }
            Uploaded += Moved;
            Remaining = Amount - Uploaded;
        }

        if (Uploaded > 0)
        {
            WT_LOGF(TEXT("[cloud-pool] vanilla API accepted %d x %s into the cloud pool."),
                    Uploaded, *ItemClass->GetName());
            Temp->RemoveFromRoot();
            return Uploaded;
        }
        Temp->RemoveFromRoot();
    }

    // ---- 兜底：反射直写（原版接口不可用）----
    const int32 Fallback = WT_AddToCentralStorage(World, ItemClass, Amount);
    if (Fallback >= 0)
    {
        WT_WARNF(TEXT("[cloud-pool] vanilla API unavailable -- %d x %s added by reflection fallback."),
                 Amount, *ItemClass->GetName());
    }
    return Fallback;
}

/**
 * Name for the mesh this mod adds when the parent brought no reusable one.
 *
 * ★ It must CONTAIN "MainMesh" but must not EQUAL it.
 *
 *   * Contains, because AFGBuildable::GetMainMeshes() matches with
 *     `component->GetFName().ToString().Contains(MAIN_MESH_NAME)` -- a substring test. That is what
 *     AFGBuildableHologram uses to find the geometry to draw as the placement ghost, so any name
 *     carrying "MainMesh" is picked up.
 *   * Not equal, because several vanilla parents already created a subobject literally called
 *     "MainMesh". A second one with the same name trips UObjectGlobals' "Default subobject ...
 *     already exists" fatal assert during cook.
 *
 * "WTMainMesh" satisfies both.
 */
static const TCHAR* const WT_MAIN_MESH_NAME = TEXT("WTMainMesh");

/**
 * 目标占地边长（uu）—— 用户定案「储能建筑 2x2 地基」。
 *
 * 1 个地基 = 800 x 800 uu（编辑器实测原版 Build_Foundation_8x1_01 的清空盒 = ±400），
 * 所以 2x2 地基 = 1600 x 1600 uu = 16m x 16m。
 */
static const float WT_POWER_STORAGE_FOOTPRINT_XY = 1600.f;

/**
 * 网格装配说明：画哪个模型、铺多大、要不要强制材质。
 *
 * ★ 0.2.32 用户定案：本模组的建筑不再只用 /Engine/BasicShapes/Cube，而是直接用原版建筑模型 ——
 *   储能 = 原版大型蓄电池 SM_PowerStorage_01，容量 = 原版位面存储站上传器 SM_CentralStorage_01。
 *   两个模型路径都不是猜的：用 CSS 版 UE 编辑器读原版蓝图 CDO，`Build_PowerStorageMk1` 的
 *   MainMesh 组件直接指向 SM_PowerStorage_01（缩放 1:1），`Build_CentralStorage` 同理。
 *
 * ★ 尺寸口径（同样是编辑器实测，三重交叉验证过）：
 *   1 个地基 = 800 x 800 uu（原版 Build_Foundation_8x1_01 清空盒 = ±400）；
 *   原版 Build_PowerStorageMk1 清空盒 = 600 x 600 x 1200 uu（即 6m x 6m x 12m）；
 *   原版 Build_CentralStorage 清空盒 = 500 x 900 x 423 uu（即 5m x 9m x 4.23m）。
 */
struct WTMeshSpec
{
    /** 要用的模型。为空（或加载失败）时退回 /Engine/BasicShapes/Cube，至少不会变成隐形建筑。 */
    UStaticMesh* Mesh = nullptr;

    /** 目标占地边长（uu）。> 0 时按模型真实包围盒等比缩放到这个边长；0 = 保持模型原始比例。 */
    float FootprintXY = 0.f;

    /** 拿不到包围盒时假定的模型原生边长（uu），来自上面那组清空盒实测。 */
    float AssumedNativeXY = 600.f;

    /** 同样兜底用的原版建筑高度（从轴心量到顶面，uu）。 */
    float AssumedNativeTopZ = 1200.f;

    /** 退回立方体时用的缩放。 */
    FVector CubeFallbackScale = FVector(1.f);

    /** 可选的强制材质资产路径（当前无调用者使用；★0.2.36b 起材质统一走 WT_CopyVanillaMeshMaterials 抄原版）。 */
    const TCHAR* MaterialPath = nullptr;
};

/**
 * 把模型等比缩放到 TargetXY 的占地需要的倍率。
 *
 * 用模型真实包围盒算，所以「原版模型到底多大」这件事即使我们估错了也不会做错 —— 引擎会告诉我们。
 * 只有在包围盒不可用时（本地内容镜像是占位数据时会返回 128）才退回 AssumedNativeXY。
 */
static float WT_ScaleForFootprint(const UStaticMesh* Mesh, float TargetXY, float AssumedNativeXY)
{
    if (Mesh)
    {
        const FBoxSphereBounds Bounds = Mesh->GetBounds();
        const float Native = 2.f * FMath::Max(Bounds.BoxExtent.X, Bounds.BoxExtent.Y);
        if (Native > 1.f && Native < 100000.f)
        {
            return TargetXY / Native;
        }
    }
    return TargetXY / FMath::Max(1.f, AssumedNativeXY);
}

/** 模型缩放 Scale 倍之后，顶面到轴心的 Z（用来写清空盒高度）。 */
static float WT_ScaledTopZ(const UStaticMesh* Mesh, float Scale, float AssumedNativeTopZ)
{
    if (Mesh)
    {
        const FBoxSphereBounds Bounds = Mesh->GetBounds();
        const float Top = Bounds.Origin.Z + Bounds.BoxExtent.Z;
        if (FMath::Abs(Top) > 1.f && FMath::Abs(Top) < 100000.f)
        {
            return Top * Scale;
        }
    }
    return AssumedNativeTopZ * Scale;
}

/** 模型局部空间下底面相对轴心的 Z：用来把模型「放平在地面上」而不是半埋进去。 */
static float WT_MeshBaseZ(const UStaticMesh* Mesh, float Fallback)
{
    if (Mesh)
    {
        const FBoxSphereBounds Bounds = Mesh->GetBounds();
        const float Base = Bounds.Origin.Z - Bounds.BoxExtent.Z;
        if (FMath::Abs(Base) < 100000.f)
        {
            return Base;
        }
    }
    return Fallback;
}

/**
 * 把主网格装到组件上。
 *
 * AFGBuildableHologram looks the placement preview geometry up through AFGBuildable::GetMainMeshes,
 * which matches any component whose name contains "MainMesh" -- the same contract
 * ARemote_FactoryBuildable documents. Any name without that substring and the ghost is invisible
 * while the building itself works fine.
 */
static void WT_SetupMesh(AActor* Owner, UStaticMeshComponent* MeshComp, const WTMeshSpec& Spec)
{
    if (!Owner || !MeshComp)
    {
        return;
    }

    UStaticMesh* Mesh = Spec.Mesh;
    FVector Scale = Spec.CubeFallbackScale;

    if (Mesh)
    {
        Scale = (Spec.FootprintXY > 0.f)
                    ? FVector(WT_ScaleForFootprint(Mesh, Spec.FootprintXY, Spec.AssumedNativeXY))
                    : FVector(1.f);
    }
    else
    {
        Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    }

    MeshComp->SetStaticMesh(Mesh);
    MeshComp->SetRelativeScale3D(Scale);

    // 立方体的轴心在几何中心（下底面 -50 uu），原版建筑模型的轴心大多在底面中心。用包围盒算出来，
    // 两种模型都能正好坐在地面上 —— 而不是半埋进去。
    MeshComp->SetRelativeLocation(FVector(0.f, 0.f, -WT_MeshBaseZ(Mesh, -50.f) * Scale.Z));

    if (Spec.MaterialPath)
    {
        if (UMaterialInterface* Override = LoadObject<UMaterialInterface>(nullptr, Spec.MaterialPath))
        {
            MeshComp->SetMaterial(0, Override);
        }
        else
        {
            WT_WARNF(TEXT("[upgrade] material '%s' not found — keeping the model's own material."),
                     Spec.MaterialPath);
        }
    }

    MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    MeshComp->SetCollisionResponseToAllChannels(ECR_Block);
    MeshComp->SetGenerateOverlapEvents(false);
}

// ----------------------------------------------------------------------------
// ★0.2.48（用户「位面接口，用对应的接口，别放两侧跟后面」）—— 云端四站的连接口位置。
//
// 结论：四个站（上传/下载/液态上传/液态下载）的连接组件**全部摆到原版位面存储站上传器
// 自己的那个口上**，不再用「正面 ±1.5m 分开」的内置坐标。
//
// 这个口的变换不再靠运行时去问引擎 —— **直接从原版资产逐字节解出来了**（离线取证，
// 脚本 `_cs_ports.py`，链路见 skill §53）：
//     Build_CentralStorage.uasset
//       → 导出 `Input0_GEN_VARIABLE`：UFGFactoryConnectionComponent，flags=0x40029
//         （含 RF_DefaultSubObject）；父组件 = RootComponent（默认场景根，单位变换）
//         RelativeLocation = (0, 390, 100)   RelativeRotation = (0, 90, 0)  ← Yaw=90° ⇒ 口朝 +Y
//     自洽校验：同资产四把梯子在 (±250, ±370, 235) 围出主体轮廓 ⇒ 口正好贴在 +Y 面外侧 20uu。
//     该资产**只有一个** UFGFactoryConnectionComponent，**没有任何管道接口**。
//
// ★★★为什么 0.2.46 与 0.2.48 初版的「照抄原版 CDO」在实机上都没生效（同一个根因，两次都白改）：
//   现场日志（FactoryGame.log 2026.10.08-15.46）四个站全部打出 `vanilla connection anchor
//   NOT found` ⇒ 端口全部退回内置坐标。根因不是「取错了组件集合」，而是**取错了对象**：
//   蓝图里摆的组件模板（`Input0_GEN_VARIABLE`）**既不在 CDO 的 OwnedComponents 里、
//   也不以 CDO 为 Outer**。cooked uasset 导出表实锤（`_cs_ports.py scs` 于
//   Build_CentralStorage.uasset，ExportCount=68，outer 已按 1-based 换算）：
//       Input0_GEN_VARIABLE             outer = Build_CentralStorage_C  ← 蓝图类本身
//       DefaultSceneRoot_GEN_VARIABLE   outer = Build_CentralStorage_C
//       SCS_Node (×10)                  outer = SimpleConstructionScript
//   与引擎注释逐字一致（BlueprintGeneratedClass.cpp:1425）：
//       "component templates are not currently owned by the Blueprint Class default object.
//        Instead, they are owned by the Blueprint Class itself."
//   ⇒ ① `AActor::GetComponents()` 走 **OwnedComponents**（Actor.h:3857）——CDO 不在
//        PostActorConstruction 的重建路径上（`if (!HasAnyFlags(RF_ClassDefaultObject))
//        ResetOwnedComponents();`）⇒ 0 命中；
//     ② `UObject::GetDefaultSubobjects()`（Obj.cpp:198）走 `ForEachObjectWithOuter(CDO)`
//        —— 模板的 outer 是**类**不是 CDO ⇒ 同样 0 命中。
//   两版都错在同一个假设上：以为「蓝图 CDO 的默认子对象」包含蓝图摆的组件。它不包含。
//   ⇒ 正解 = 走 SCS：`BPGC->SimpleConstructionScript->GetAllNodes()`，模板从
//     `USCS_Node::GetActualComponentTemplate(BPGC)` 取。
//
// 放置策略：**内置坐标是以资产为准的正本**，运行时查到的只在与内置值一致（≤100uu / ≤30°）
//   时才采用；不一致就保留内置值并打一条 always-on 告警（说明查到了别的东西）。
// ----------------------------------------------------------------------------
static const FVector  WT_VANILLA_PORT_LOC(0.f, 390.f, 100.f);
static const FRotator WT_VANILLA_PORT_ROT(0.f, 90.f, 0.f);

static FTransform WT_ComponentToActorSpace(const USceneComponent* Comp, const AActor* Owner)
{
    FTransform Acc = Comp ? Comp->GetRelativeTransform() : FTransform::Identity;
    const USceneComponent* Cur = Comp ? Comp->GetAttachParent() : nullptr;
    const USceneComponent* Root = (Owner && Owner != Owner->GetClass()->GetDefaultObject())
                                      ? Owner->GetRootComponent() : nullptr;
    for (int32 Guard = 0; Cur && Cur != Root && Guard < 16; ++Guard)
    {
        Acc = Cur->GetRelativeTransform() * Acc;
        Cur = Cur->GetAttachParent();
    }
    return Acc;
}

/**
 * 把一个 SCS 组件模板换算到「Actor 空间」。
 *
 * ★为什么不能直接用模板自己的 `GetAttachParent()`：引擎只在**实例化**时才给组件挂父
 *   （SCS_Node.cpp:166 `NewSceneComp->SetupAttachment(...)`，在 `ExecuteNodeOnActor` 里），
 *   CDO 上的模板对象根本没有 AttachParent。
 * ★为什么也不能用 `USCS_Node::GetParentComponentTemplate()`：整个函数在 `#if WITH_EDITOR`
 *   里（SCS_Node.h）⇒ **Shipping 下不存在**；而且它只查**父蓝图**的 SCS 树（SCS_Node.cpp:604
 *   循环 `StackIndex > 0` 起、跳过本类），同树内的父节点它返回 nullptr。
 * ⇒ 父链只能自己走：`Node->ParentComponentOrVariableName` ⇄ 同树内某个 `Node->GetVariableName()`
 *   （正是引擎 FindArchetype 用的口径）；父不在树里时按 `bIsParentComponentNative` 判为原生 C++ 组件。
 */
static FTransform WT_SCSNodeToActorSpace(USCS_Node* Node, UBlueprintGeneratedClass* BPGC,
                                         const TArray<USCS_Node*>& AllNodes)
{
    FTransform Acc = FTransform::Identity;
    USCS_Node* Cur = Node;
    for (int32 Guard = 0; Cur && Guard < 32; ++Guard)
    {
        if (USceneComponent* SC = Cast<USceneComponent>(Cur->ComponentTemplate))
        {
            Acc = SC->GetRelativeTransform() * Acc;
        }
        if (Cur->ParentComponentOrVariableName.IsNone())
        {
            break;
        }
        USCS_Node* Parent = nullptr;
        for (USCS_Node* N : AllNodes)
        {
            if (N && N->GetVariableName() == Cur->ParentComponentOrVariableName)
            {
                Parent = N;
                break;
            }
        }
        if (Parent)
        {
            Cur = Parent;
            continue;
        }
        // 父不在 SCS 树里 ⇒ 它是**原生 C++ 组件**（节点上的 bIsParentComponentNative 就是这个意思，
        // 原版 Build_CentralStorage 的 Input0 父名就是基类的 "RootComponent"）。
        // 原生默认子对象的 Outer 是 CDO，所以走 GetDefaultSubobjects 按名取 ——
        // ★绝不能对 CDO 调 GetComponents（见文件头的根因分析：CDO 的 OwnedComponents 是空的）。
        if (Cur->bIsParentComponentNative)
        {
            if (AActor* CDO = BPGC ? Cast<AActor>(BPGC->GetDefaultObject()) : nullptr)
            {
                TArray<UObject*> Subs;
                CDO->GetDefaultSubobjects(Subs);
                for (UObject* O : Subs)
                {
                    if (USceneComponent* SC = Cast<USceneComponent>(O))
                    {
                        if (SC->GetFName() == Cur->ParentComponentOrVariableName)
                        {
                            Acc = SC->GetRelativeTransform() * Acc;
                            break;
                        }
                    }
                }
            }
        }
        break;   // 链走到原生父（或父名无从解析）为止
    }
    return Acc;
}

/**
 * 取原版建筑 CDO 上第一个传送带接口的「Actor 空间」变换。
 *
 * ★★0.2.48 真·修法（第三版，前两版见上方注释的根因分析）：**必须走 SCS 节点**。
 *   四条路径按可靠性排序，命中即止：
 *     ① 蓝图 SCS 组件模板 —— 原版建筑（`Build_CentralStorage_C` 等）走这条；
 *     ② 蓝图 AddComponent 节点模板（`BPGC->ComponentTemplates`）；
 *     ③ 原生 C++ 默认子对象（`GetDefaultSubobjects`，纯 C++ 类走这条）；
 *     ④ 实例组件表（`GetComponents`，兜底）。
 *
 * @param OutCount 可选出口：本次枚举到的接口个数（四条路径合计）。
 */
static bool WT_VanillaConnectionAnchor(UClass* VanillaClass, FTransform& Out, int32* OutCount = nullptr)
{
    if (OutCount)
    {
        *OutCount = 0;
    }
    if (!VanillaClass)
    {
        return false;
    }
    AActor* CDO = Cast<AActor>(VanillaClass->GetDefaultObject());
    if (!CDO)
    {
        return false;
    }
    // ★不要把 `!CDO->GetRootComponent()` 写成硬性早退：SCS 路径（下面 ①）不依赖 root，
    //   而「CDO 的 root 一定非空」只是推断、不是证据。写成硬 guard 的话，一旦推断错了，
    //   整条锚定链会在查 SCS 之前就静默返回 false —— 那正是 0.2.46 那种「全 NOT found」的形态。
    UBlueprintGeneratedClass* BPGC = Cast<UBlueprintGeneratedClass>(VanillaClass);

    TArray<UFGFactoryConnectionComponent*> Conns;
    FTransform First = FTransform::Identity;
    bool bHaveFirst = false;

    // ---- ① 主路径：蓝图 SCS 组件模板。cooked 构建的组件实例化本来就以 SCS 为主干
    //      （USCS_Node::ExecuteNodeOnActor），且这些数据都不是 editor-only。
    if (BPGC && BPGC->SimpleConstructionScript)
    {
        const TArray<USCS_Node*>& Nodes = BPGC->SimpleConstructionScript->GetAllNodes();
        for (USCS_Node* Node : Nodes)
        {
            if (!Node)
            {
                continue;
            }
            UActorComponent* Tmpl = Node->GetActualComponentTemplate(BPGC);
            if (UFGFactoryConnectionComponent* C = Cast<UFGFactoryConnectionComponent>(Tmpl))
            {
                if (!bHaveFirst)
                {
                    First = WT_SCSNodeToActorSpace(Node, BPGC, Nodes);
                    bHaveFirst = true;
                }
                Conns.Add(C);
            }
        }
    }
    // ---- ② AddComponent 节点模板（写在蓝图构造函数图里的，不进 SCS）。
    if (Conns.Num() == 0 && BPGC)
    {
        for (UActorComponent* Tmpl : BPGC->ComponentTemplates)
        {
            if (UFGFactoryConnectionComponent* C = Cast<UFGFactoryConnectionComponent>(Tmpl))
            {
                if (!bHaveFirst)
                {
                    First = WT_ComponentToActorSpace(Cast<USceneComponent>(Tmpl), CDO);
                    bHaveFirst = true;
                }
                Conns.Add(C);
            }
        }
    }
    // ---- ③ 原生 C++ 默认子对象。
    if (Conns.Num() == 0)
    {
        TArray<UObject*> Subs;
        CDO->GetDefaultSubobjects(Subs);
        for (UObject* O : Subs)
        {
            if (UFGFactoryConnectionComponent* C = Cast<UFGFactoryConnectionComponent>(O))
            {
                if (!bHaveFirst)
                {
                    First = WT_ComponentToActorSpace(C, CDO);
                    bHaveFirst = true;
                }
                Conns.Add(C);
            }
        }
    }
    // ---- ④ 兜底：实例组件表。
    //    ★ UE5（Satisfactory 所用引擎）的 AActor::GetComponents<T>() 返回 void，按引用填充数组，
    //      绝不可写 `... > 0`。这是 0.2.48 初版编译失败的真正根因（误用 UE4 的 int 返回假设）。
    if (Conns.Num() == 0)
    {
        CDO->GetComponents<UFGFactoryConnectionComponent>(Conns);
        if (Conns.Num() > 0)
        {
            First = WT_ComponentToActorSpace(Conns[0], CDO);
            bHaveFirst = true;
        }
    }

    if (OutCount)
    {
        *OutCount = Conns.Num();
    }
    if (Conns.Num() == 0)
    {
        return false;
    }
    Out = bHaveFirst ? First : FTransform::Identity;
    return true;
}

/**
 * 把一个连接组件摆到「原版那个口」上（0.2.48 用户指令）。四个云端站唯一的摆放入口，
 * 避免各个站各写一套 ±1.5m 展开（正是用户否掉的那种形态）。
 *
 * @param OnceTag  每类只报一次的告警 tag
 * @param Who      日志里的人话主语（"depot conveyor input" 之类）
 */
static void WT_PlaceOnVanillaPort(USceneComponent* Comp, UClass* VanillaClass,
                                  const TCHAR* OnceTag, const TCHAR* Who)
{
    if (!Comp)
    {
        return;
    }
    FTransform T(WT_VANILLA_PORT_ROT, WT_VANILLA_PORT_LOC);
    int32 Count = 0;
    FTransform Live;
    if (WT_VanillaConnectionAnchor(VanillaClass, Live, &Count))
    {
        const float Delta = static_cast<float>(
            (Live.GetTranslation() - WT_VANILLA_PORT_LOC).Size());
        const bool bAgree = Delta <= 100.f && Live.Rotator().Equals(WT_VANILLA_PORT_ROT, 30.f);
        if (bAgree)
        {
            T = Live;      // 与资产解出的坐标一致 ⇒ 用运行时的（含父链累乘的真实结果）
        }
        else
        {
            WT_WARNF_ONCE(OnceTag, Comp,
                TEXT("[upgrade] %s: live vanilla port %s/%s disagrees with the asset-derived port ")
                TEXT("%s/%s (delta %.0f uu) -- keeping the asset-derived one."),
                Who, *Live.GetTranslation().ToString(), *Live.Rotator().ToString(),
                *WT_VANILLA_PORT_LOC.ToString(), *WT_VANILLA_PORT_ROT.ToString(), Delta);
        }
    }
    else
    {
        WT_WARNF_ONCE(OnceTag, Comp,
            TEXT("[upgrade] %s: vanilla CDO has no enumerable factory connection (found=%d) -- ")
            TEXT("using the asset-derived port %s/%s."),
            Who, Count, *WT_VANILLA_PORT_LOC.ToString(), *WT_VANILLA_PORT_ROT.ToString());
    }
    Comp->SetRelativeLocationAndRotation(T.GetTranslation(), T.GetRotation());
    WT_WARNF_ONCE(OnceTag, Comp,
        TEXT("[upgrade] %s placed at vanilla port: rel=%s rot=%s"),
        Who, *Comp->GetRelativeLocation().ToCompactString(), *Comp->GetRelativeRotation().ToString());
}

/**
 * 写一条原版风格的清空盒 —— 它决定「这个建筑占几个地基」和周围的不可建造区。
 *
 * 原版建筑就是这么定的（见 WTMeshSpec 上面的实测值）。在构造函数里先写好，原版的
 * AFGBuildable::TrySetupClearanceData() 看到已经有数据就不会再自作主张。
 *
 * ★ `mClearanceData` 本身是 **protected**（FGBuildable.h:1022，此前误判成 public，编译报
 *   error C2248），所以必须走公开 API：`ResetClearanceData()` + `AddClearanceData()`（两者都在
 *   FGBuildable.h:221~224 的 public 区）。
 */
static void WT_SetClearance(AActor* Owner, float HalfX, float HalfY, float TopZ)
{
    AFGBuildable* Buildable = Cast<AFGBuildable>(Owner);
    if (!Buildable)
    {
        return;
    }

    Buildable->ResetClearanceData();

    FFGClearanceData Data;
    Data.Type = EClearanceType::CT_Default;
    Data.ClearanceBox = FBox(FVector(-HalfX, -HalfY, 0.f), FVector(HalfX, HalfY, TopZ));
    Data.RelativeTransform = FTransform::Identity;
    Data.SoftClearanceResponse = ESoftClearanceOverlapResponse::SCOR_Default;
    Data.ExcludeForSnapping = false;
    Buildable->AddClearanceData(Data);
}

/**
 * Create this mod's own main mesh on `Owner`.
 *
 * ★ Deliberately does NOT reuse an inherited "MainMesh", even when one exists. Two vanilla parents
 *   in play here have their own, and reusing them is worse than making a new one:
 *
 *   * AFGBuildablePowerStorage's "MainMesh" is a UFGColoredInstanceMeshProxy, not a plain
 *     UStaticMeshComponent. Its ShouldCreateRenderState() override starts with
 *     `GetWorld()->WorldType`, and GetWorld() is NULL while a CDO is under construction -- so ANY
 *     call that asks for a render state (SetStaticMesh, SetRelativeScale3D, SetMobility) null-derefs
 *     and kills the whole cook. Verified: the crash callstack ran
 *     WT_SetupMesh -> UFGColoredInstanceMeshProxy::ShouldCreateRenderState.
 *   * The vanilla parent itself only ever calls SetMobility / SetupAttachment on that proxy inside
 *     its constructor; the visual mesh is assigned later from the Blueprint asset.
 *
 *   So the safe, uniform rule for all three upgrades is: add our own mesh, touch nothing inherited.
 *   The parent's proxy stays inert, exactly as vanilla leaves it.
 */
static UStaticMeshComponent* WT_CreateMainMesh(AActor* Owner, USceneComponent* AttachTo, const WTMeshSpec& Spec)
{
    if (!Owner)
    {
        return nullptr;
    }

    if (UStaticMeshComponent* NewMesh = Owner->CreateDefaultSubobject<UStaticMeshComponent>(WT_MAIN_MESH_NAME))
    {
        NewMesh->SetupAttachment(AttachTo);
        WT_SetupMesh(Owner, NewMesh, Spec);
        return NewMesh;
    }
    return nullptr;
}

/**
 * ★0.2.36b 贴图修复（用户实测：五栋建筑全白模）：
 *
 * 原版工厂建筑的材质**不在静态网格资产上**，而是蓝图在网格组件上做的 override ——
 * 所以裸用 SM_* 模型（0.2.36 删掉自建淡蓝材质后）就是一块白模。
 *
 * 修法与抄面板同思路、零路径猜测：加载原版对应建筑的蓝图 CDO，找到它的网格组件，
 * 把组件上的材质逐槽位抄到我们的 WTMainMesh 上（= 玩家看到的原版实物贴图）。
 */
static void WT_CopyVanillaMeshMaterials(UClass* VanillaBuildableClass, UStaticMeshComponent* Dst)
{
    if (!VanillaBuildableClass || !Dst)
    {
        return;
    }

    const AActor* SrcCDO = Cast<AActor>(VanillaBuildableClass->GetDefaultObject());
    if (!SrcCDO)
    {
        return;
    }
    const UStaticMeshComponent* Src = SrcCDO->FindComponentByClass<UStaticMeshComponent>();
    if (!Src)
    {
        WT_WARNF_ONCE(TEXT("mesh-copy-nocomp"), Dst,
                 TEXT("[upgrade] %s CDO has no static mesh component -- texture copy skipped."),
                 *VanillaBuildableClass->GetName());
        return;
    }

    const int32 SrcNum = Src->GetNumMaterials();
    const int32 DstNum = Dst->GetNumMaterials();

    // ★自检加固（2026-10-07）：GetNumMaterials 在「组件没设网格」时返回 0。
    //   此时 FMath::Clamp(i, 0, SrcNum-1) 的上界是 -1，会退化成 GetMaterial(-1) ——
    //   本引擎的 UMeshComponent::GetMaterial 走 IsValidIndex 判假返回 nullptr，所以不越界；
    //   但那是「依赖引擎实现细节不崩」，而且整段抄写会静默变成空操作。显式挡住更清楚。
    if (SrcNum <= 0)
    {
        WT_WARNF_ONCE(TEXT("mesh-copy-noslot"), Dst,
                 TEXT("[upgrade] %s mesh has 0 material slots -- texture copy skipped."),
                 *VanillaBuildableClass->GetName());
        return;
    }

    // ★0.2.48 修「顶部往下看无材质」（电池顶槽空白）：原版组件对应槽位若为 null，旧逻辑会跳过，
    //   而 SM_* 模型资产自身不带材质（白模，见本函数头注释），该槽就永远留白。改为「槽位 null 时
    //   回落到最后一个非空原版材质」，保证没有一个槽是白模。i<SrcNum 走一一对应，i>=SrcNum 回落
    //   LastValid，与旧 FMath::Clamp(i,0,SrcNum-1) 在 Src[SrcNum-1] 非空时完全一致（不影响 depot 等正常站）。
    int32 LastValid = SrcNum - 1;
    while (LastValid >= 0 && !Src->GetMaterial(LastValid))
    {
        --LastValid;
    }
    const bool bHaveFallback = LastValid >= 0;

    int32 FilledByFallback = 0;
    for (int32 i = 0; i < DstNum; ++i)
    {
        const int32 Idx = (i < SrcNum) ? i : (SrcNum - 1);
        UMaterialInterface* MI = Src->GetMaterial(Idx);
        if (!MI && bHaveFallback)
        {
            MI = Src->GetMaterial(LastValid);
            ++FilledByFallback;
        }
        if (MI)
        {
            Dst->SetMaterial(i, MI);
        }
    }
    // ★0.2.36d 每类一次：本函数由构造函数调用（每实例一遍），成功路径不该刷屏。
    WT_WARNF_ONCE(TEXT("mesh-materials"), Dst,
             TEXT("[upgrade] %s materials <= %s (dst=%d src=%d fallback=%d)."),
             *Dst->GetName(), *VanillaBuildableClass->GetName(), DstNum, SrcNum, FilledByFallback);
}

// ----------------------------------------------------------------------------
// ★0.2.48 圆口视觉件（用户定案 2026-10-10「建模不应该是云端建模吗」）：
//
// 主体保持云端建模（SM_CentralStorage_01，同固态两站），**不做任何裁剪/替换**；
// 圆形管道口的视觉用原版「端口小件」SM_Pipe_InPut_01 叠加在管道锚点外侧。
//
// 一手资料（`_cs_ports.py` 解析 Build_HadronCollider.uasset 实锤，编辑器格式 SCS）：
//   原版机器的圆口 = 独立 StaticMeshComponent（FactoryInputs/Mesh/SM_Pipe_InPut_01），
//   与管道连接组件成对摆放：
//     Input0 连接件 (200, 990, 175) Yaw 90  ↔  SM_Pipe_InPut_01 (200, 1000, 175) Yaw 0
//   规律：网格在连接点**向外 ~10cm**、Yaw = 连接Yaw − 90（方口件 Input1/SM_Input_01 同规律）。
//   照抄该相对关系 ⇒ 视觉与原版一致，零坐标猜测。
// ----------------------------------------------------------------------------
static void WT_AddRoundPortVisual(AActor* Self, const TCHAR* CompName, const UFGPipeConnectionComponent* Pipe)
{
    if (!Self || !Pipe || !Self->GetRootComponent())
    {
        return;
    }
    UStaticMesh* PortMesh = LoadObject<UStaticMesh>(nullptr,
        TEXT("/Game/FactoryGame/Buildable/Factory/FactoryInputs/Mesh/SM_Pipe_InPut_01.SM_Pipe_InPut_01"));
    if (!PortMesh)
    {
        return; // 视觉件加载失败只影响观感，管道功能不受影响（构造期不刷日志，见日志纪律）。
    }
    UStaticMeshComponent* Port = Self->CreateDefaultSubobject<UStaticMeshComponent>(CompName);
    if (!Port)
    {
        return;
    }
    Port->SetupAttachment(Self->GetRootComponent());
    Port->SetStaticMesh(PortMesh);
    Port->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    // 照抄 HadronCollider 一手相对关系：向外 10cm、Yaw = 连接Yaw − 90。
    const FRotator ConnRot = Pipe->GetRelativeRotation();
    const float YawRad = FMath::DegreesToRadians(ConnRot.Yaw);
    const FVector Outward(FMath::Cos(YawRad), FMath::Sin(YawRad), 0.f);
    Port->SetRelativeLocation(Pipe->GetRelativeLocation() + Outward * 10.f);
    Port->SetRelativeRotation(FRotator(0.f, ConnRot.Yaw - 90.f, 0.f));
}

// ----------------------------------------------------------------------------
// 储能单元
// ----------------------------------------------------------------------------

AWTUpgradePowerStorage::AWTUpgradePowerStorage()
{
    // ★0.2.48 充电动画：打开 Tick，把充电状态/电量百分比写成网格自定义图元数据（节流 4Hz 足够顺滑）。
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.25f;
    PrimaryActorTick.bStartWithTickEnabled = true;

    // ★0.2.50 重构（查原版 SCS 一手证据 Build_PowerStorageMk1.uasset）：
    //   原版只有一棵组件树 DefaultSceneRoot → MainMesh(=继承的 mMeshMesh，根可视)
    //   → {FGFactoryLegs, PowerConnection, SK_PowerStorage_01}。发光窗材质 MI_PowerStorage_Inst 原版
    //   就铺在 mMeshMesh 的窗口槽上、由原生代码写 CPD(13/14) 驱动；骨骼电池体 SK 是 mMeshMesh 的子组件。
    //   上一代自建并列 WTMainMesh 代理造成：① 叠了两个电池壳 → 布局错乱；② 原生发光只写 mMeshMesh
    //   （我们没给它窗材质）→ 窗不发光；③ 手写 CPD 错位 → 只显 3 格（原版 5 格）。
    //   本代直接复用继承的 mMeshMesh 作为唯一可视网格，完全复刻原版：设 SM_PowerStorage_01 + 等比缩放
    //   (2x2 地基≈2.667×) + 逐槽抄原版材质(窗口槽自动拿到 MI_PowerStorage_Inst) + CPD 缓冲 20 + 挂 SK
    //   子组件；发光交给原生 CPD 驱动。
    //   （早期「碰 mMeshMesh 会 cook 崩」是 WTMainMesh 时期的推测；同类型 proxy 上 WT_SetupMesh 已在
    //    WTMainMesh 上 cook 通过，故直接复用 mMeshMesh 等价且安全。）
    // ★0.2.32 用户定案：储能建筑改用原版大型蓄电池模型，等比放大到 2x2 地基。
    //   原版 Build_PowerStorageMk1 实测只有 6m x 6m x 12m（清空盒 600 x 600 x 1200 uu，模型放在
    //   MainMesh 上且缩放 1:1），而 2x2 地基 = 1600 x 1600 uu，所以要等比放大 1600/600 ≈ 2.667 倍。
    //   高度因此变成约 32m —— 用户已明确选择「等比放大」而不是压扁或保持原版。
    static ConstructorHelpers::FObjectFinder<UStaticMesh> BatteryMesh(
        TEXT("/Game/FactoryGame/Buildable/Factory/PowerStorage/Mesh/SM_PowerStorage_01.SM_PowerStorage_01"));

    WTMeshSpec Spec;
    Spec.Mesh = BatteryMesh.Object;
    Spec.FootprintXY = WT_POWER_STORAGE_FOOTPRINT_XY;
    Spec.AssumedNativeXY = 600.f;
    Spec.AssumedNativeTopZ = 1200.f;
    Spec.CubeFallbackScale = FVector(16.f, 16.f, 32.f);   // 立方体兜底：1600 x 1600 x 3200 uu

    // ★复用继承的原生主网格 mMeshMesh（单一可视），不再自建并列代理。极小概率为空时兜底自建。
    UFGColoredInstanceMeshProxy* MeshComp = mMeshMesh;
    if (!MeshComp)
    {
        MeshComp = CreateDefaultSubobject<UFGColoredInstanceMeshProxy>(WT_MAIN_MESH_NAME);
        MeshComp->SetupAttachment(RootComponent);
    }
    // ★0.2.48 J1：mNumCustomDataFloats 必须在 SetupMesh/组件注册前设定 —— 实例管理器据此分配
    //   20 浮点的每实例缓冲；若实例创建后才改，缓冲大小已定，写 13/14 越界被静默丢弃 ⇒ 窗不发光。
    MeshComp->SetNumCustomDataFloats(20);   // 匹配原版 proxy：每实例缓冲 20 浮点（含 13/14 发光）
    WT_SetupMesh(this, MeshComp, Spec);
    MeshComp->SetMobility(EComponentMobility::Movable);

    // 清空盒跟着模型走：2x2 地基，高度按实际缩放后的模型顶面算。
    {
        const float MeshScale = WT_ScaleForFootprint(Spec.Mesh, Spec.FootprintXY, Spec.AssumedNativeXY);
        const float TopZ = WT_ScaledTopZ(Spec.Mesh, MeshScale, Spec.AssumedNativeTopZ);
        WT_SetClearance(this, Spec.FootprintXY * 0.5f, Spec.FootprintXY * 0.5f, TopZ);
        WT_LOGF(TEXT("[upgrade] power storage mesh='%s' scale=x%.3f footprint=%.0f uu height=%.0f uu"),
                Spec.Mesh ? *Spec.Mesh->GetName() : TEXT("<cube fallback>"),
                MeshScale, Spec.FootprintXY, TopZ);
    }

    // ★ The numbers. Both properties are private on AFGBuildablePowerStorage, so reflection is the
    // only way in, and the constructor is the only correct moment: these are EditDefaultsOnly and
    // the values are captured from the CDO into every instance the game later spawns.
    //
    // ★0.2.48 用户定案：**初始储电量与充放速率都取原版大型蓄电池的 4 倍**。
    //   原版数值的出处（都在原版蓝图 Build_PowerStorageMk1 里设，C++ CDO 全是 0）：
    //     - mPowerStoreCapacity = 100 MWh  (FGBuildablePowerStorage.h:143 "maximum ... in MWh")
    //     - mPowerInputCapacity = 100 MW   (FGBuildablePowerStorage.h:155 "The maximum INPUT power
    //       to this battery, in MW. @note there is no cap on output power.")
    //   ★上游契约（已对原版头文件核验）：原版明文 **放电功率没有上限**，所以"放电倍率"
    //     落在**容量**上；并不存在"放电速率"字段需要改。唯一要乘的就是下面这两个基础值。
    //   ★铁律：只能写「基础值 × 倍率」，绝不能写「当前值 × 倍率」。基础值只在这一处定义，
    //     实际部署值 = 基础值 × 玩家研究等级倍率，在 BeginPlay 里算（见下）。
    //   ★0.2.48 用户定案：基础**蓄电容量 400 → 1600** MWh（原版 100 的 16×；充放速率仍 400 MW = 4×）。
    WT_SetFloatProperty(this, TEXT("mPowerStoreCapacity"), 1600.f);
    // 最大输入（充电）功率：原版 100 MW × 4 = 400 MW（本回合未改）。
    WT_SetFloatProperty(this, TEXT("mPowerInputCapacity"), 400.f);

    // ★0.2.36 崩溃修复：原版把 mIndicatorLevelMax（uint8，侧面的电量格数）写在蓝图
    //   Build_PowerStorageMk1 里，C++ CDO 默认 0 —— 而 AFGBuildablePowerStorage::BeginPlay 的
    //   check(mIndicatorLevelMax > 0 && mPowerStoreCapacity > 0) 会当场崩掉整个游戏
    //   （2026-10-07 实测：一放置储能单元就崩）。这里补上 5 格，对齐原版储能单元观感（用户实测原版为 5 格）。
    WT_SetNumericProperty(this, TEXT("mIndicatorLevelMax"), 5.0);

    // ★0.2.48 D 修复（用户：原版电池有 2 个接线柱，这个电池做 4 个接口）：
    //   原版 Build_PowerStorageMk1 功能上只有 1 个 FGPowerConnectionComponent（前脸 Y-、近顶部，
    //   原版模型空间坐标 (-0.037,-321.483,1149.996)）。WT 这个更大的电池做 4 个接口。
    //   ★用户最终定案（2026-10-09）：4 个接口要放在**同一个坐标**上，像电线杆那样一个接点并多根线，
    //     不要分散到前/后/左/右四面。故 4 个 UFGPowerConnectionComponent 全部 SetupAttachment(RootComponent)、
    //     坐标全部 = 同一个合成 Root 空间点 MeshLoc + PortPos*MeshScale（PortPos 为原版前脸近顶单点）。
    //   —— 为什么挂 RootComponent 并手工合成坐标：① 电力连接系统只对「挂 RootComponent 的端口」取位正常；
    //        挂非 Root 子组件会让取位异常；② 合成坐标 = MeshLoc(含落地 Z 偏移) + PortPos*MeshScale(等比≈2.667)
    //        等价于「挂在主网格下的世界位」，既让连接系统正确取位、又保留缩放与落地偏移
    //        （避免首轮只乘 1600/600 导致偏低/埋地、超出可接距离）。
    //   —— 为什么 4 口同坐标而不是各占一面：用户明确要「像电线杆」——一个物理接点并多根线；
    //        且 Satisfactory 原版机制「一个位置可接 2 根线」，同坐标多组件 = 可并更多线。
    //   注：建组件用 CreateDefaultSubobject 拿 CDO 期稳定指针；MeshComp 在 WT_CreateMainMesh 时已建好
    //   且 SetupAttachment(RootComponent)，这里用其相对位置/缩放做坐标合成。
    if (MeshComp)
    {
        WTMainMesh = MeshComp;   // 供 WT_UpdateBatteryGlow 写发光窗 CPD（材质已抄到 WTMainMesh 上）
        // ★0.2.48 D 接口（用户 2026-10-09 最终定案：像电线杆，一个接点并多根线，
        //   不要分散到前/后/左/右四面）。坐标沿用原版前脸近顶模型空间单点
        //   (-0.037,-321.483,1149.996)，经 MeshLoc + PortPos*MeshScale 合成到 Root 空间
        //   （MeshLoc 含落地 Z 偏移、MeshScale≈2.667 等比），等价于挂在主网格下的同一世界位。
        //   ★ 用单个 UWT_PowerConnectionComponent（mMaxNumConnectionLinks=4）落该点，见下方 PortName。
        const float MeshScale = MeshComp->GetRelativeScale3D().X;   // 等比，取 X 即可
        const FVector MeshLoc = MeshComp->GetRelativeLocation();    // (0,0,-WT_MeshBaseZ*Scale.Z)
        const FVector PortPos = FVector(-0.037f, -321.483f, 1149.996f);   // 单点（前脸近顶，原版坐标）
        // ★0.2.48 D(4线，2026-10-09)：用**单个** UWT_PowerConnectionComponent（子类把
        //   mMaxNumConnectionLinks 从默认 1 提到 4）落在同一坐标点，像电线杆一个接点并 4 根线。
        //   不采用「4 个独立口同坐标、每口 mMax=1」：电线工具的就近取口会一直选到同一个最近口，
        //   导致实际只能并 1 根线；单口 mMax=4 才稳定并 4 根。坐标 = MeshLoc + PortPos*MeshScale。
        static const TCHAR* PortName = TEXT("PowerConnection_WirePole");
        mPowerConnections.Reset();
        mPowerConnections.Reserve(1);
        {
            UWT_PowerConnectionComponent* C =
                CreateDefaultSubobject<UWT_PowerConnectionComponent>(PortName);
            C->SetupAttachment(RootComponent);
            // 单口单点：MeshLoc(含落地 Z 偏移) + PortPos*MeshScale(等比≈2.667)，等价于挂主网格下同一世界位。
            C->SetRelativeLocation(MeshLoc + PortPos * MeshScale);
            C->SetPowerInfo(GetPowerInfo());
            mPowerConnections.Add(C);
        }
        WT_WARNF_ONCE(TEXT("power-conn"), this,
                TEXT("[upgrade] power storage wire-pole port bound on RootComponent (cap=4) info=%s"),
                GetPowerInfo() ? TEXT("yes") : TEXT("NO"));
    }

    // ★0.2.36 可配置UI（用户定案）：抄原版蓄电池面板，按 E 显示原版电池 UI
    //   （蓄电进度/充放功率/时间预估）。★0.2.36b 顺带抄组件材质修白模。
    if (UClass* VanillaBattery = WT_FindVanillaClass(
            TEXT("/Game/FactoryGame/Buildable/Factory/PowerStorage/Build_PowerStorageMk1.Build_PowerStorageMk1_C"),
            TEXT("Build_PowerStorageMk1_C"), AFGBuildable::StaticClass()))
    {
        VanillaBattery->AddToRoot();   // 裸 UClass* 不是 GC 引用（0.2.7 台账惯例）
        WT_CopyVanillaMeshMaterials(VanillaBattery, MeshComp);
        WT_ReuseVanillaInteractWidget(this, VanillaBattery);
    }

    // ★0.2.48 电池建模第三轮（前两轮只动材质抄写 ⇒ 无效：日志实证 mesh-materials
    //   dst=6 src=6 fallback=0，六槽全非空，问题根本不在「槽空没空」）。根因是**模型组装缺件**：
    //   离线解析 Build_PowerStorageMk1.uasset（_cs_ports 解码链）实锤 ——
    //     ① 蓝图对 MainMesh **零 OverrideMaterials** ⇒ 我们从 CDO 抄到的 6 槽全是
    //        SM_PowerStorage_01 资产自带材质，其中混着 "Fbx Default Material" 导入占位槽；
    //     ② 原版 SCS 还摆了 SK_PowerStorage_01 **骨骼网格组件**（parent=MainMesh、相对变换
    //        单位，见 _cs_ports.py scs 输出 num=6 var=SK_PowerStorage_01 parent=MainMesh），
    //        真正的电池体（闭合顶盖 + Materialize/FinalizeGlow/DayEmis 充电发光材质参数）在它身上。
    //   ⇒ 原版视觉 = SM 底座 + SK 电池体（+ 腿部）；我们只摆了 SM，顶部自然是占位材质/镂空
    //     （用户截图：俯视看到环形外框 + 内部机械结构）。
    //   修法与原版同构：把 SK 也挂到 WTMainMesh 下（继承等比放大），材质用 SK 资产自带
    //   （原版组件同样零 override ⇒ 资产材质即原版观感）。
    //   ★上游契约（引擎源码已核）：SetSkeletalMesh 为 ENGINE_API public
    //     （SkeletalMeshComponent.h:2048）；VisibilityBasedAnimTickOption 为 public UPROPERTY
    //     （SkinnedMeshComponent.h:733，默认仅渲染时 tick 动画）；bComponentUseFixedSkelBounds
    //     是 protected（:809）不设，LDMaxDrawDistance 仅影响裁剪距离不设。
    if (MeshComp)
    {
        static ConstructorHelpers::FObjectFinder<USkeletalMesh> BatteryInnerMesh(
            TEXT("/Game/FactoryGame/Buildable/Factory/PowerStorage/Mesh/SK_PowerStorage_01.SK_PowerStorage_01"));
        WTInnerBatteryMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WTInnerBatteryMesh"));
        WTInnerBatteryMesh->SetupAttachment(MeshComp);   // ★原版同款：SK 挂 MainMesh 下，继承缩放
        WTInnerBatteryMesh->SetSkeletalMesh(BatteryInnerMesh.Object);
        WTInnerBatteryMesh->VisibilityBasedAnimTickOption =
            EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
        // 原版 SK 组件序列化同款（Build_PowerStorageMk1.uasset 实测 bComponentUseFixedSkelBounds=true）：
        // 用骨骼网格固定包围盒，不依赖物理资产。★引擎头 SkinnedMeshComponent.h:809 为 public 位域
        // （756 行 public: 段；自检初版误判 protected，检查器失败纠正了我——保留判据防回退）。
        WTInnerBatteryMesh->bComponentUseFixedSkelBounds = true;
        WT_WARNF_ONCE(TEXT("battery-inner"), this,
                TEXT("[upgrade] power storage inner SK mesh: %s (vanilla SCS parity)."),
                BatteryInnerMesh.Object ? TEXT("loaded") : TEXT("NULL"));
    }

    // ★0.2.50：发光窗材质不再手动铺到「所有」槽位——那样会把电池壳体也铺成发光材质、且只显 3 格
    //   （旧实现根因之一）。改为由上面的 WT_CopyVanillaMeshMaterials 逐槽抄原版 mMeshMesh 材质，其中
    //   窗口槽天然就是 MI_PowerStorage_Inst（读 CPD 13=状态/14=电量，与原版完全一致）。原生代码 +
    //   本类 WT_UpdateBatteryGlow 用 SetCustomPrimitiveDataFloat 写 13/14 驱动 5 格发光窗。
    //   CPD 缓冲已在上面 SetNumCustomDataFloats(20) 预分配。
    if (WTMainMesh)
    {
        WT_WARNF_ONCE(TEXT("battery-glow-mat"), this,
                TEXT("[upgrade] J1: window-slot glow inherited from vanilla per-slot copy (MI_PowerStorage_Inst)."));
    }
}

void AWTUpgradePowerStorage::BeginPlay()
{
    Super::BeginPlay();

    // Scale by the player's researched PowerStorage level (Dyson-sphere style progression). The base
    // (level 1) values above are the floor; each researched level multiplies capacity + charge rate.
    const int32 L = WT_GetTrackLevel(GetWorld(), EWTUpgradeTrack::PowerStorage);
    // ★末段（Lv17~20）碎片路线是 1.5 倍增量外推，必须把玩家实际走的路线一起传进来。
    const float s = WT_TrackScale(EWTUpgradeTrack::PowerStorage, L, WT_GetFinalRoute(GetWorld()));
    // ★0.2.48：基础**蓄电容量 1600** MWh（原版 100 的 16×，用户 400→1600 定案）/ 充放速率 400 MW（原版 100 的 4×），
    //   此处**只乘等级倍率**。写「基础值 × 倍率」而不是「当前值 × 倍率」，否则同一实例被 BeginPlay 跑两次就会平方放大。
    WT_SetFloatProperty(this, TEXT("mPowerStoreCapacity"), 1600.f * s);
    WT_SetFloatProperty(this, TEXT("mPowerInputCapacity"), 400.f * s);
    WT_LOGF(TEXT("[upgrade] power storage Lv%d -> %.0f MWh / %.0f MW charge (base 1600/400)"),
            L, 1600.f * s, 400.f * s);

    // ★0.2.48 J 修复（v2，用户实测「无充电动画」+「重载丢失材质」）：
    //   不再依赖原版 proxy mMeshMesh 渲染 —— 实例管理器在组件注册期就接管其渲染，运行时改网格/
    //   材质在「退出重进」重载后被实例重置，导致发光窗消失 + 材质丢失（正是用户两次实机反馈的现象）。
    //   改为：发光直接驱动**自建且稳定渲染**的 WTMainMesh(SM 底座) + WTInnerBatteryMesh(SK 电池体，
    //   发光窗材质在其身上)，二者都用原版同名资产/材质，读同一套 CPD 索引（13=状态 / 14=电量，
    //   见原版 FGBuildablePowerStorage.h:95「11 + StatusPrimitiveID」）。
    //   ★关键修复：用 UPrimitiveComponent::SetCustomPrimitiveDataFloat(13/14, ...) 驱动发光 ——
    //   该方法即原版 FGBuildable.cpp:1453 同款机制；其内部 SetPrimitiveData 自动把 CPD 缓冲扩容到
    //   所需索引，并 World->Scene->UpdateCustomPrimitiveData 上传 GPU。基类没有也不需 SetNumCustomDataFloats
    //   （那是原版 proxy 子类 UFGColoredInstanceMeshProxy 的私有成员）。每次 Tick 重写 CPD，重载后也会
    //   重新点亮，不再丢材质。proxy mMeshMesh 仅作副通道兜底。
    WT_WARNF_ONCE(TEXT("power-glow-setup"), this,
            TEXT("[upgrade] battery glow CPD armed (main=%s inner=%s)."),
            WTMainMesh ? TEXT("yes") : TEXT("NO"),
            WTInnerBatteryMesh ? TEXT("yes") : TEXT("NO"));


    // ★0.2.48 充电动画：首帧就把状态/电量写到可见网格，避免出生瞬间发光窗停在默认态。
    WT_UpdateBatteryGlow();

    // ★0.2.48 J1 加固：
    //   1) 显式确保 WTMainMesh 被实例管理器接管（实例句柄有效，SetUserDefinedData 才能上传 GPU）。
    //      游戏正常流程（OnBuildEffectFinished）也会调 SetInstanced(true)，这里双保险。
    if (WTMainMesh)
    {
        WTMainMesh->SetNumCustomDataFloats(20);   // 预分配发光缓冲（ctor 已设，重保险防实例化重置）
        WTMainMesh->SetInstanced(true);
    }
    //   2) 为 SK 电池体各槽位建 MID，供每帧驱动其**真实发光材质参数**（见 WT_UpdateBatteryGlow）。
    if (WTInnerBatteryMesh)
    {
        const int32 N = WTInnerBatteryMesh->GetNumMaterials();
        for (int32 i = 0; i < N; ++i)
        {
            if (UMaterialInterface* Base = WTInnerBatteryMesh->GetMaterial(i))
            {
                if (UMaterialInstanceDynamic* MID = WTInnerBatteryMesh->CreateDynamicMaterialInstance(i, Base))
                {
                    mGlowMIDs.AddUnique(MID);
                }
            }
        }
    }
}

void AWTUpgradePowerStorage::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    WT_UpdateBatteryGlow();
}

void AWTUpgradePowerStorage::OnBuildEffectFinished()
{
    Super::OnBuildEffectFinished();

    // ★0.2.48 J1 修复（按原版 FGBuildable.cpp 链路）：基类 OnBuildEffectFinished 会对本 actor 上
    // 所有 UFGColoredInstanceMeshProxy 组件调 SetVisibility(true)+SetInstanced(true) 重新实例化。
    // 重新实例化会重建实例缓冲，清掉我们在 BeginPlay/Tick 写进 WTMainMesh 的每实例自定义数据
    // （索引 13=状态 / 14=电量，见 FGBuildablePowerStorage.h 的 11+PrimitiveID 注释），导致发光窗熄灭。
    // 故在 Super 之后立即把 WTMainMesh 重新实例并再写一次发光数据，保证建完特效后窗仍亮。
    if (WTMainMesh)
    {
        WTMainMesh->SetNumCustomDataFloats(20);   // 重新实例化后缓冲可能被重置，再预分配一次
        WTMainMesh->SetInstanced(true);
        WT_UpdateBatteryGlow();
    }
}

void AWTUpgradePowerStorage::WT_UpdateBatteryGlow()
{
    // 把充电状态 / 电量百分比写成可见网格的自定义图元数据，驱动原版充电动画（发光窗填充 + 状态变色）。
    // 主通道：WTMainMesh 现在复用继承的 mMeshMesh（单一可视网格，窗口槽材质 = 原版 MI_PowerStorage_Inst，
    //   读 CPD 13=状态 / 14=电量）。用与原生 AFGBuildablePowerStorage 同款的 SetCustomPrimitiveDataFloat
    //   直接写组件级 CPD —— 实例管理器会把它作为每实例自定义数据喂给窗口材质，故发光窗正确填充 5 格。
    // 副通道：WTInnerBatteryMesh(SK 电池体) 走它自己的真实标量参数发光（见下）。
    const int32 StatusID = mStatusPrimitiveID;   // 2
    const int32 ChargeID = mChargePrimitiveID;   // 3
    const int32 SIdx = 11 + StatusID;            // 13
    const int32 CIdx = 11 + ChargeID;            // 14
    const float StatusVal = static_cast<float>(static_cast<uint8>(GetBatteryStatus()));
    const float ChargeVal = FMath::Clamp(GetPowerStorePercent(), 0.f, 1.f);

    // ★0.2.50：主通道改为组件级 CPD（与原生同款）。旧实现用 SetUserDefinedData(20 元素) 与实例缓冲
    //   尺寸/原生写入错位（只显 3 格的根因之一），现弃用。
    if (WTMainMesh)
    {
        WTMainMesh->SetCustomPrimitiveDataFloat(SIdx, StatusVal);   // 13 = 充电状态（BS_PowerIn=3 ⇒ 充电中）
        WTMainMesh->SetCustomPrimitiveDataFloat(CIdx, ChargeVal);   // 14 = 电量百分比(0..1)
    }
    if (WTInnerBatteryMesh)
    {
        WTInnerBatteryMesh->SetCustomPrimitiveDataFloat(SIdx, StatusVal);
        WTInnerBatteryMesh->SetCustomPrimitiveDataFloat(CIdx, ChargeVal);
    }

    // ★0.2.48 J1（修正）：SK 电池体发光窗走「它自己的真实材质参数」驱动（一手数据：SK_PowerStorage_01
    //   材质暴露 GlowIntensity / DayEmisIntenity / NightEmisIntenity / FinalizeGlow / RaisedIntensity /
    //   GlowEffectBoost / HeatGlow 等标量参数。原 v5 写的 ChargeAmount/Glow/EmissivePower 在真实材质里
    //   根本不存在=no-op，仅 Materialize/FinalizeGlow 恰好存在被强设成 Clamp(charge,0.15,1) 的假青光）。
    //   现用真实参数名 + 真实电量值（不夹最低亮度）：
    //     充电中(PowerIn/PowerOut) ⇒ 发光强度随电量 0→1 线性增长；空闲(Idle) ⇒ 归零，只留已建成外观。
    const bool bActive = (StatusVal >= 3.f);   // BS_PowerIn(3)/BS_PowerOut(4)
    const float Glow = bActive ? ChargeVal : 0.f;   // 真实电量 0..1，不夹底
    for (UMaterialInstanceDynamic* MID : mGlowMIDs)
    {
        if (!MID) continue;
        MID->SetScalarParameterValue(FName("GlowIntensity"), Glow);
        MID->SetScalarParameterValue(FName("DayEmisIntenity"), Glow);
        MID->SetScalarParameterValue(FName("NightEmisIntenity"), Glow);
        MID->SetScalarParameterValue(FName("FinalizeGlow"), Glow);
        MID->SetScalarParameterValue(FName("RaisedIntensity"), Glow);
        MID->SetScalarParameterValue(FName("GlowEffectBoost"), Glow);
        MID->SetScalarParameterValue(FName("HeatGlow"), Glow * 0.5f);
        MID->SetScalarParameterValue(FName("Materialize"), 1.f);   // 始终呈已建成外观
    }
}

FText AWTUpgradePowerStorage::GetDisplayName()
{
    return WT_LOCTEXT("Upgrade_PowerStorage", "Power Storage Unit");
}

// ----------------------------------------------------------------------------
// 大容量仓库
// ----------------------------------------------------------------------------

AWTUpgradeStorage::AWTUpgradeStorage()
{
    // ★0.2.36b 需要自己的 Tick 把本地缓冲搬进账本（原版上传链路全部拦死，见 Tick/Factory_Tick）。
    //
    // ★★0.2.36c 加载期修复：**必须**同时打开 bStartWithTickEnabled。
    //   继承链上 AFGBuildable(201-202) 与 AFGBuildableStorage(21-22) 的 ctor 都把
    //   `PrimaryActorTick.bStartWithTickEnabled = false`；引擎的 AActor::RegisterAllActorTickFunctions
    //   用 `bStartWithTickEnabled` 决定 tick 函数初始是否 enable，而 AFGBuildable::EnablePrimaryTickFunctions
    //   在整个 SDK 里**没有任何调用点**。⇒ 只设 bCanEverTick=true 会「注册但不启用」，
    //   Tick() 永不执行（静默不工作：放进去的货永远不上云）。原版 AFGBuildableFactory(64-65)
    //   特意设 bStartWithTickEnabled=true 就是为子类开 tick 留的开关，这里照抄同一约定。
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;
    PrimaryActorTick.TickInterval = 0.5f;   // 0.5s 足够快，省一点空转

    // ★0.2.48 识别色（原生喷漆槽，见 DefaultGame.ini mDefaultColorSlots：槽2=黄=暖色组）。
    //   建筑出生即带色，玩家仍可用喷漆枪覆盖。四机配色：仓库=黄(2)、下载器=红(1)、流体上传=青(4)、流体下载=蓝(5)。
    mColorSlot = 2;   // 暖色组（固态机）

    // ★0.2.32 用户定案：容量建筑改用原版「位面存储站上传器」模型（Build_CentralStorage 用的
    //   SM_CentralStorage_01），保持原版 1:1 尺寸（清空盒实测 500 x 900 x 423 uu = 5m x 9m x 4.23m）。
    //   ★0.2.36b：材质不再自建，改为从原版蓝图 CDO 抄（白模修复）。
    //
    //   基类 AFGCentralStorageContainer 自身不建任何网格代理（只有 AFGBuildablePowerStorage 有
    //   UFGColoredInstanceMeshProxy），所以这里自建 WTMainMesh 是安全的。
    static ConstructorHelpers::FObjectFinder<UStaticMesh> DepotMesh(
        TEXT("/Game/FactoryGame/Buildable/Factory/CentralStorage/Mesh/SM_CentralStorage_01.SM_CentralStorage_01"));

    WTMeshSpec Spec;
    Spec.Mesh = DepotMesh.Object;
    Spec.FootprintXY = 0.f;                                // 0 = 保持模型原始比例（原版尺寸）
    Spec.CubeFallbackScale = FVector(5.f, 9.f, 4.23f);     // 立方体兜底：500 x 900 x 423 uu
    // ★0.2.36（用户定案）：不再套自建淡蓝材质 —— 材质在下面从原版蓝图 CDO 抄（白模修复）。
    UStaticMeshComponent* MeshComp = WT_CreateMainMesh(this, RootComponent, Spec);
    // ★0.2.48 A 修正（一手数据 `_cs_ports.py` 解析 Build_CentralStorage.uasset 实锤）：
    //   原版 MainMesh_GEN_VARIABLE 自带 RelativeRotation (0,180,0) —— 整模型绕 Z 轴转 180°；
    //   而我们的传送带/管道口都挂在 RootComponent 上、不跟随该旋转。原版视觉正面朝 -Y、入口在 +Y（背面）。
    //   之前漏了这 180° ⇒ 同样「朝前放」时我们的口在正面、原版在背面，观感即「位置相反」。
    //   这里**只转 mesh、不动端口**（端口保持 (0,390,100) = 原版口世界坐标），使视觉朝向与原版一致。
    MeshComp->SetRelativeRotation(FRotator(0.f, 180.f, 0.f));

    // 清空盒照抄原版 Build_CentralStorage 的实测值。
    WT_SetClearance(this, 250.f, 450.f, 423.f);

    // ★0.2.42 传送带入口（与下载站的传送带修复同一根因）：原版的连接组件是蓝图里摆的，
    //   C++ CDO 一个都没有 ⇒ 玩家没法用传送带往上传站送货。这里补一个纯输入口
    //   （上传站只进不出 —— 出口会绕过「只上传」的设计意图）。
    // ★0.2.48：坐标不再手猜 —— 摆到原版上传器那个口（(0,390,100) / Yaw 90°，见 WT_VANILLA_PORT_LOC）。
    mConveyorIn = CreateDefaultSubobject<UFGFactoryConnectionComponent>(TEXT("WTConveyorIn"));
    mConveyorIn->SetupAttachment(RootComponent);
    mConveyorIn->SetRelativeLocation(WT_VANILLA_PORT_LOC);
    mConveyorIn->SetRelativeRotation(WT_VANILLA_PORT_ROT);
    mConveyorIn->SetDirection(EFactoryConnectionDirection::FCD_INPUT);
    mConveyorIn->SetInventory(GetStorageInventory());
    WT_WARNF_ONCE(TEXT("conveyor-depot"), this,
            TEXT("[upgrade] depot conveyor input bound: comp=%s dir=%d inv=%s"),
            *mConveyorIn->GetName(),
            static_cast<int32>(mConveyorIn->GetDirection()),
            GetStorageInventory() ? TEXT("yes") : TEXT("NO"));

    // ★0.2.36 可配置UI（用户定案）：抄原版位面存储站上传器面板，按 E = 原版完整 UI
    //   （本地缓冲 + 上传进度 + 云端统计）。★0.2.36b 顺带抄组件材质修白模。
    if (UClass* VanillaDepot = WT_FindVanillaClass(
            TEXT("/Game/FactoryGame/Buildable/Factory/CentralStorage/Build_CentralStorage.Build_CentralStorage_C"),
            TEXT("Build_CentralStorage_C"), AFGBuildable::StaticClass()))
    {
        VanillaDepot->AddToRoot();
        WT_CopyVanillaMeshMaterials(VanillaDepot, MeshComp);
        WT_ReuseVanillaInteractWidget(this, VanillaDepot);

        // ★0.2.48 接口位置 = 原版那个口（唯一入口，见 WT_PlaceOnVanillaPort 的说明）。
        WT_PlaceOnVanillaPort(mConveyorIn, VanillaDepot,
                              TEXT("port-depot"), TEXT("depot conveyor input"));
    }
    else
    {
        WT_WARNF_ONCE(TEXT("vanilla-depot-missing"), this,
                TEXT("[upgrade] vanilla depot class missing - ports stay at the asset-derived ")
                TEXT("location %s/%s (already applied in the ctor)."),
                *WT_VANILLA_PORT_LOC.ToString(), *WT_VANILLA_PORT_ROT.ToString());
    }

    WT_LOGF(TEXT("[upgrade] depot mesh='%s' scale=1:1 footprint=500x900 uu height=423 uu"),
            Spec.Mesh ? *Spec.Mesh->GetName() : TEXT("<cube fallback>"));

    // Unlike the other two, mInventorySizeX / mInventorySizeY are PUBLIC on AFGBuildableStorage
    // (which AFGCentralStorageContainer derives from), so these are ordinary assignments rather
    // than reflection. The parent's constructor uses them to size mStorageInventory, and the safe
    // direction of change is UP: growing a grid keeps every existing slot index valid, whereas
    // shrinking one would orphan anything stored past the new edge. Base is 8 x 12 = 96 slots;
    // scaled up by the researched Storage level (see BeginPlay).
    ApplyStorageGrid();
}

void AWTUpgradeStorage::BeginPlay()
{
    Super::BeginPlay();
    ApplyStorageGrid();
    WT_LogConnectionPorts(this, TEXT("ports-depot"));   // ★0.2.42 接口可见性取证

    // ★0.2.32 取证：基类换成原版 AFGCentralStorageContainer（位面存储站上传器）；
    //   ★0.2.36b：注册照旧由基类完成（我们仍是"合法云端节点"，让建造取料有云可用），
    //   但上传路径改为自己的账本 —— 见 Factory_Tick。
    int32 LedgerTotal = 0;
    for (const FItemAmount& IA : mLedger)
    {
        LedgerTotal += IA.Amount;
    }
    WT_LOGF(TEXT("[upgrade] depot '%s' grid %dx%d (%d slots) ownLedger=%d entries / %d items."),
            *GetName(), mInventorySizeX, mInventorySizeY, mInventorySizeX * mInventorySizeY,
            mLedger.Num(), LedgerTotal);
}

void AWTUpgradeStorage::Tick(float dt)
{
    WT_ZONE(TEXT("Upgrade.Storage.Tick"));   // 0.5s 一次；本地缓冲 -> 账本
    // ★0.2.36b：故意不调 Super —— 原版 AFGBuildableStorage/Container 的 Tick/上传链路
    // 会把库存直接送进原版云端池，绕过我们的账本（"原版优先"就无从谈起）。
    DrainInventoryToLedger();
}

void AWTUpgradeStorage::Factory_Tick(float dt)
{
    // 同上：原版的上传如果走 Factory_Tick 这条路，这里一并拦死。
    DrainInventoryToLedger();
}

void AWTUpgradeStorage::DrainInventoryToLedger()
{
    // ★0.2.36f 内部节流（见头文件 mLastLedgerDrainTime 的完整说明）。
    //   本函数有**两条**触发路径：① 自己的 PrimaryActorTick（TickInterval = 0.5s）；
    //   ② 基类链 AFGBuildableStorage : AFGBuildableFactory 带来的 Factory_Tick（TickInterval = 0.0
    //   ⇒ 每帧）。节流放在**函数内部**而不是调用点：无论哪条路径先到、另一条会不会到，
    //   整格库存的扫描频率恒为 0.5s；也避免"删掉某一条 drain 调用"导致上传静默失效的回归风险。
    //   GetWorld() 为空（CDO 期）时不节流，沿用原有行为（此时库存组件也为空，等价于直接 return）。
    if (const UWorld* W = GetWorld())
    {
        const double Now = static_cast<double>(W->GetTimeSeconds());
        if (mLastLedgerDrainTime >= 0.0 && (Now - mLastLedgerDrainTime) < 0.5)
        {
            return;
        }
        mLastLedgerDrainTime = Now;
    }

    // 遍历整格库存；格子会随研究阶段放到 10x10，规模化后这步有成本，值得单独计时。
    WT_ZONE(TEXT("Upgrade.Storage.DrainLedger"));
    UFGInventoryComponent* Inv = GetStorageInventory();
    if (!Inv)
    {
        return;
    }

    const int32 Size = Inv->GetSizeLinear();
    int32 Moved = 0;
    for (int32 i = 0; i < Size; ++i)
    {
        FInventoryStack S;
        if (!Inv->GetStackFromIndex(i, S) || !S.HasItems())
        {
            continue;
        }
        UClass* Cls = S.Item.GetItemClass().Get();
        if (!Cls)
        {
            continue;
        }
        WT_LedgerAdd(mLedger, Cls, S.NumItems);
        Moved += S.NumItems;
        Inv->RemoveFromIndex(i, S.NumItems);   // 清空这一格
    }

    if (Moved > 0)
    {
        WT_LOGF(TEXT("[upgrade] depot '%s' took %d items into its own cloud ledger."), *GetName(), Moved);
    }
}

int32 AWTUpgradeStorage::AddToLedger(TSubclassOf<UFGItemDescriptor> ItemClass, int32 Amount)
{
    WT_LedgerAdd(mLedger, ItemClass.Get(), Amount);
    return WT_LedgerGet(mLedger, ItemClass.Get());
}

int32 AWTUpgradeStorage::TakeFromLedger(TSubclassOf<UFGItemDescriptor> ItemClass, int32 Want)
{
    return WT_LedgerTake(mLedger, ItemClass.Get(), Want);
}

int32 AWTUpgradeStorage::GetLedgerAmount(TSubclassOf<UFGItemDescriptor> ItemClass) const
{
    return WT_LedgerGet(mLedger, ItemClass.Get());
}

void AWTUpgradeStorage::ApplyStorageGrid()
{
    // Best-effort level read: in the CDO GetWorld() is null (falls back to level 1); for a placed
    // instance the level reflects what the player has researched. NOTE: changing the grid after the
    // inventory component was already created (BeginPlay) does not auto-resize the live inventory in
    // vanilla -- newly placed buildings pick up the grid at construction, existing buildings keep
    // their build-time grid until rebuilt. Tracked as a known follow-up.
    // ★2026-10-07 定案（见设计表 §6.1）：本地缓冲格网**不再是主维度**（主维度是云端配额）。
    //   这里改成「按阶段跳」：阶段 1~5 -> 2x2 / 4x4 / 6x6 / 8x8 / 10x10（4 → 100 格）。
    //   原版上传器的本地缓冲本来就只有 1 组（wiki："internal buffer of one stack"），
    //   而云端是按配额收件 —— 缓冲放得越大反而把物品堵在本地，所以这一维只做温和放大。
    const int32 L = (GetWorld() ? WT_GetTrackLevel(GetWorld(), EWTUpgradeTrack::Storage) : 1);
    const int32 Stage = WT_StageForLevel(L);
    const int32 Side = FMath::Clamp(2 * Stage, 2, 10);
    mInventorySizeX = Side;
    mInventorySizeY = Side;
    WT_LOGF(TEXT("[upgrade] storage grid Lv%d (stage %d) -> %dx%d = %d slots"),
            L, Stage, Side, Side, Side * Side);
}

FText AWTUpgradeStorage::GetDisplayName()
{
    // ★0.2.36 改名（用户定案）：大容量仓库 -> 云端储存上传站
    return WT_LOCTEXT("Upgrade_Storage", "Cloud Storage Upload Station");
}

void AWTUpgradeStorage::GetDismantleRefund_Implementation(TArray<FInventoryStack>& out_refund,
                                                          bool noBuildCostEnabled) const
{
    WT_ZONE_COARSE(TEXT("Upgrade.Storage.DismantleRefund"));   // 拆除期：账本退还

    // 形态照抄原版 AFGBuildable::GetDismantleRefund_Implementation：
    //   ① Super 负责本地缓冲（GetDismantleInventoryReturns）+ 建造材料 + 连线 + 涂装；
    //   ② 自己再并进账本条目（原版看不见这部分 —— 它不在任何 UFGInventoryComponent 里）；
    //   ③ 最后统一 ConsolidateInventoryItems（原版也是这个收尾，所以 MergeInventoryItem 只管追加）。
    Super::GetDismantleRefund_Implementation(out_refund, noBuildCostEnabled);

    int32 Refunded = 0;
    for (const FItemAmount& IA : mLedger)
    {
        if (IA.Amount <= 0 || !IA.ItemClass)
        {
            continue;
        }
        // 账本只收固体（DrainInventoryToLedger 只搬 UFGInventoryComponent 里的货，
        // 与 GetDismantleInventoryReturns 的 RF_SOLID 口径一致），所以这里不做形态过滤。
        UFGInventoryLibrary::MergeInventoryItem(out_refund, FInventoryStack(IA.Amount, IA.ItemClass));
        Refunded += IA.Amount;
    }

    UFGInventoryLibrary::ConsolidateInventoryItems(out_refund);

    if (Refunded > 0)
    {
        // ★用 Warn 而不是 LOGF：LOGF 在 Shipping 下被整段编译剔除（连字面量都不进 .rdata），
        //   而这行是「拆除一座有货的云端站」的**低频但重要**事件 —— 它既要在实测日志里看得到，
        //   也要能被产物校验脚本证明存在（校验清单铁律）。数量级只有「拆站」时一次，不吵。
        WT_WARNF(TEXT("[upgrade] depot '%s' dismantle refund: %d items returned from own cloud ledger."),
                 *GetName(), Refunded);
    }
}

// ----------------------------------------------------------------------------
// 云端存储下载站
// ----------------------------------------------------------------------------

AWTUpgradeCloudDownloader::AWTUpgradeCloudDownloader()
{
    // 拉货是主动行为，需要自己的 Tick（仓库基类默认不 tick）。
    // ★★0.2.36c 加载期修复：bStartWithTickEnabled 也必须打开（基类 AFGBuildableStorage 设为 false，
    //   不打开则 Tick() 永不跑 ⇒ 下载站完全不下货）。理由详见 AWTUpgradeStorage ctor。
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;

    // ★0.2.48 识别色（原生喷漆槽，见 DefaultGame.ini mDefaultColorSlots：槽1=红=暖色组）。
    //   四机配色：仓库=黄(2)、下载器=红(1)、流体上传=青(4)、流体下载=蓝(5)。
    mColorSlot = 1;   // 暖色组（固态机）

    // ★0.2.36 外观：与上传站同款原版位面存储站模型；材质从原版蓝图 CDO 抄（白模修复）。
    static ConstructorHelpers::FObjectFinder<UStaticMesh> DepotMesh(
        TEXT("/Game/FactoryGame/Buildable/Factory/CentralStorage/Mesh/SM_CentralStorage_01.SM_CentralStorage_01"));

    WTMeshSpec Spec;
    Spec.Mesh = DepotMesh.Object;
    Spec.FootprintXY = 0.f;                                // 0 = 保持模型原始比例（原版尺寸）
    Spec.CubeFallbackScale = FVector(5.f, 9.f, 4.23f);     // 立方体兜底：500 x 900 x 423 uu
    // ★0.2.36（用户定案）：与上传站同款原版位面存储站模型；材质从其蓝图 CDO 抄（白模修复）。
    UStaticMeshComponent* MeshComp = WT_CreateMainMesh(this, RootComponent, Spec);
    // ★0.2.48 A 修正（一手数据 `_cs_ports.py` 解析 Build_CentralStorage.uasset 实锤）：
    //   原版 MainMesh_GEN_VARIABLE 自带 RelativeRotation (0,180,0) —— 整模型绕 Z 轴转 180°；
    //   而我们的传送带/管道口都挂在 RootComponent 上、不跟随该旋转。原版视觉正面朝 -Y、入口在 +Y（背面）。
    //   之前漏了这 180° ⇒ 同样「朝前放」时我们的口在正面、原版在背面，观感即「位置相反」。
    //   这里**只转 mesh、不动端口**（端口保持 (0,390,100) = 原版口世界坐标），使视觉朝向与原版一致。
    MeshComp->SetRelativeRotation(FRotator(0.f, 180.f, 0.f));

    WT_SetClearance(this, 250.f, 450.f, 423.f);

    // ★★0.2.42 传送带接口（用户反馈「云端固体接不了传送带」）：
    //   基类 AFGBuildableStorage 的 ctor 只建 StorageInventory，**没有任何连接组件**
    //   （原版 Mk1/Mk2 集装箱的进出带接口都是蓝图里摆的）⇒ 传送带没有可接的点，自然也接不上。
    //   这里自建一个**输出口**，把本站库存绑上去，与原版集装箱口径一致
    //   （SetDirection = FCD_OUTPUT，SetInventory = GetStorageInventory()）。
    // ★0.2.48：下载站=纯下载（只出不出）。输出口摆到原版上传器那个口（(0,390,100)/Yaw90，见
    //   WT_VANILLA_PORT_LOC）—— 不再像 0.2.46 那样沿 Y 展开，也不再叠一个输入口
    //   （之前进/出同坐标 ⇒ 「一个口同时显输入+输出箭头」且连接全息吸错口）。上传站对称只留输入口。
    mConveyorOut = CreateDefaultSubobject<UFGFactoryConnectionComponent>(TEXT("WTConveyorOut"));
    mConveyorOut->SetupAttachment(RootComponent);
    mConveyorOut->SetRelativeLocation(WT_VANILLA_PORT_LOC);
    mConveyorOut->SetRelativeRotation(WT_VANILLA_PORT_ROT);
    mConveyorOut->SetDirection(EFactoryConnectionDirection::FCD_OUTPUT);
    mConveyorOut->SetInventory(GetStorageInventory());
    WT_WARNF_ONCE(TEXT("conveyor-downloader"), this,
            TEXT("[upgrade] downloader conveyor output bound: out=%s inv=%s"),
            mConveyorOut ? TEXT("yes") : TEXT("NO"),
            GetStorageInventory() ? TEXT("yes") : TEXT("NO"));

    // ★0.2.36b 材质：模型是位面存储站的，材质也抄它的（不能抄 Mk2 集装箱的涂装）。
    // ★0.2.36b 用户定案：**云端站用云端面板**（位面存储站上传器），不用工业集装箱面板 ——
    //   面板与材质都出自同一台原版建筑，打开看到的就是原版云端界面。
    if (UClass* VanillaDepot = WT_FindVanillaClass(
            TEXT("/Game/FactoryGame/Buildable/Factory/CentralStorage/Build_CentralStorage.Build_CentralStorage_C"),
            TEXT("Build_CentralStorage_C"), AFGBuildable::StaticClass()))
    {
        VanillaDepot->AddToRoot();
        WT_CopyVanillaMeshMaterials(VanillaDepot, MeshComp);
        WT_ReuseVanillaInteractWidget(this, VanillaDepot);

        // ★0.2.48 输出口摆到原版那个口（(0,390,100)/Yaw90）。下载站=纯下载，只留这一个输出口
        // （不再叠输入口，避免「一个口同时显输入+输出」与连接全息吸错口）。
        WT_PlaceOnVanillaPort(mConveyorOut, VanillaDepot,
                              TEXT("port-downloader-out"), TEXT("downloader conveyor output"));
    }
    else
    {
        WT_WARNF_ONCE(TEXT("vanilla-depot-missing"), this,
                TEXT("[upgrade] vanilla depot class missing - downloader ports stay at the asset-derived ")
                TEXT("location %s/%s (already applied in the ctor)."),
                *WT_VANILLA_PORT_LOC.ToString(), *WT_VANILLA_PORT_ROT.ToString());
    }

    ApplyStorageGrid();

    WT_LOGF(TEXT("[upgrade] downloader mesh='%s' footprint=500x900 uu height=423 uu"),
            Spec.Mesh ? *Spec.Mesh->GetName() : TEXT("<cube fallback>"));
}

void AWTUpgradeCloudDownloader::BeginPlay()
{
    Super::BeginPlay();
    ApplyStorageGrid();
    WT_LogConnectionPorts(this, TEXT("ports-downloader"));   // ★0.2.42 接口可见性取证
    WT_LOGF(TEXT("[upgrade] downloader '%s' grid %dx%d (%d slots) ready."),
            *GetName(), mInventorySizeX, mInventorySizeY, mInventorySizeX * mInventorySizeY);
}

void AWTUpgradeCloudDownloader::ApplyStorageGrid()
{
    // 与上传站同一套阶段放大（见 AWTUpgradeStorage::ApplyStorageGrid 的定案注释）。
    const int32 L = (GetWorld() ? WT_GetTrackLevel(GetWorld(), EWTUpgradeTrack::Storage) : 1);
    const int32 Stage = WT_StageForLevel(L);
    const int32 Side = FMath::Clamp(2 * Stage, 2, 10);
    mInventorySizeX = Side;
    mInventorySizeY = Side;
    WT_LOGF(TEXT("[upgrade] downloader grid Lv%d (stage %d) -> %dx%d = %d slots"),
            L, Stage, Side, Side, Side * Side);
}

void AWTUpgradeCloudDownloader::Tick(float dt)
{
    WT_ZONE(TEXT("Upgrade.Downloader.Tick"));   // 2s 一次
    Super::Tick(dt);

    mPullCooldown -= dt;
    if (mPullCooldown > 0.f)
    {
        return;
    }
    mPullCooldown = mPullInterval;
    TryPullFromCloud();
}

void AWTUpgradeCloudDownloader::TryPullFromCloud()
{
    // 每次拉货都要 TActorIterator 遍历全部上传站（O(站数)），站多了这步会变贵。
    WT_ZONE_COARSE(TEXT("Upgrade.Downloader.TryPull"));
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    UFGInventoryComponent* Inv = GetStorageInventory();
    if (!Inv)
    {
        return;
    }

    // ---- 第一优先：原版云端池（用户定案「先看原版云端是否修建+内容」）----
    if (AFGCentralStorageSubsystem* Sub = AFGCentralStorageSubsystem::Get(World))
    {
        if (Sub->IsCentralStorageBuilt())
        {
            TArray<FItemAmount> CloudItems;
            Sub->GetAllItemsFromCentralStorage(CloudItems);
            // 每个周期只搬一种、最多一组 —— 温和搬运，不抢锁不清仓。
            for (const FItemAmount& IA : CloudItems)
            {
                if (IA.Amount <= 0 || IA.ItemClass.Get() == nullptr)
                {
                    continue;
                }
                const int32 StackSize = UFGItemDescriptor::GetStackSize(IA.ItemClass);
                FItemAmount Want;
                Want.ItemClass = IA.ItemClass;
                Want.Amount = FMath::Min(IA.Amount, FMath::Max(1, StackSize));
                const int32 Moved = UFGInventoryLibrary::MoveItemFromCentralStorage(Want, Inv);
                if (Moved > 0)
                {
                    WT_LOGF(TEXT("[downloader] '%s' pulled %d x %s from the vanilla cloud."),
                            *GetName(), Moved, *IA.ItemClass->GetName());
                    return;   // 一周期一种
                }
            }
        }
    }

    // ---- 第二优先：我们自己的云（各上传站的账本）----
    // 走到这里 = 原版云端没修建或这类货为 0 —— 正好是用户定的「才使用我们的云端」。
    for (TActorIterator<AWTUpgradeStorage> It(World); It; ++It)
    {
        AWTUpgradeStorage* Station = *It;
        if (!Station)
        {
            continue;
        }
        for (const FItemAmount& IA : Station->GetLedger())
        {
            UClass* Cls = IA.ItemClass.Get();
            if (IA.Amount <= 0 || !Cls)
            {
                continue;
            }
            const int32 StackSize = UFGItemDescriptor::GetStackSize(IA.ItemClass);
            const int32 Want = FMath::Min(IA.Amount, FMath::Max(1, StackSize));
            const int32 Taken = Station->TakeFromLedger(IA.ItemClass, Want);
            if (Taken <= 0)
            {
                continue;
            }
            FInventoryStack S(Taken, IA.ItemClass);
            const int32 Added = Inv->AddStack(S, true);
            if (Added < Taken)
            {
                Station->AddToLedger(IA.ItemClass, Taken - Added);   // 没放下的退回账本
            }
            if (Added > 0)
            {
                WT_LOGF(TEXT("[downloader] '%s' pulled %d x %s from our own cloud ledger."),
                        *GetName(), Added, *Cls->GetName());
                return;   // 一周期一种
            }
        }
    }
}

FText AWTUpgradeCloudDownloader::GetDisplayName()
{
    return WT_LOCTEXT("Upgrade_CloudDownloader", "Cloud Storage Download Station");
}

// ----------------------------------------------------------------------------
// 云端液态上传站 / 云端液态下载站（★0.2.36 新增，用户定案）
// ----------------------------------------------------------------------------

AWTUpgradeCloudFluidUploader::AWTUpgradeCloudFluidUploader()
{
    PrimaryActorTick.bCanEverTick = true;

    // ★0.2.48 识别色（原生喷漆槽，见 DefaultGame.ini mDefaultColorSlots：槽4=青=冷色组）。
    //   四机配色：仓库=黄(2)、下载器=红(1)、流体上传=青(4)、流体下载=蓝(5)。
    mColorSlot = 4;   // 冷色组（流体机）

    // ★0.2.48 外观（用户定案 2026-10-10「建模不应该是云端建模吗」）：主体保持**云端建模**
    //   （SM_CentralStorage_01，与固态两站同款、同 180° 朝向修正），整栋不再换成储罐网格。
    //   圆口视觉由构造器末尾的端口小件叠加（WT_AddRoundPortVisual，一手依据见其注释）。
    static ConstructorHelpers::FObjectFinder<UStaticMesh> DepotMesh(
        TEXT("/Game/FactoryGame/Buildable/Factory/CentralStorage/Mesh/SM_CentralStorage_01.SM_CentralStorage_01"));

    WTMeshSpec Spec;
    Spec.Mesh = DepotMesh.Object;
    Spec.FootprintXY = 0.f;                                // 0 = 保持模型原始比例（原版尺寸）
    Spec.CubeFallbackScale = FVector(5.f, 9.f, 4.23f);     // 立方体兜底：500 x 900 x 423 uu
    UStaticMeshComponent* MeshComp = WT_CreateMainMesh(this, RootComponent, Spec);
    // ★0.2.48 A 修正（一手数据 `_cs_ports.py` 解析 Build_CentralStorage.uasset 实锤）：
    //   原版 MainMesh_GEN_VARIABLE 自带 RelativeRotation (0,180,0)；只转 mesh、不动端口（同固态两站）。
    if (MeshComp)
    {
        MeshComp->SetRelativeRotation(FRotator(0.f, 180.f, 0.f));
    }

    WT_SetClearance(this, 250.f, 450.f, 423.f);

    // ★0.2.36b 管道接口 ×2（原版缓存罐 BeginPlay 索引第 2 个接口，1 个必崩；两个口必须都存在，见 §36）。
    //   类型 PCT_ANY（取货走 Factory_PullPipeInput，与类型无关），余量 1.5m。
    //   固定摆原版仓库口坐标 (0,390,100)/Yaw90（与固态两站一致；圆口视觉件也钉在这）。
    mPipeInA = CreateDefaultSubobject<UFGPipeConnectionComponent>(TEXT("WTFluidInA"));
    mPipeInA->SetupAttachment(RootComponent);
    mPipeInA->SetRelativeLocation(WT_VANILLA_PORT_LOC);
    mPipeInA->SetRelativeRotation(WT_VANILLA_PORT_ROT);
    mPipeInA->SetPipeConnectionType(EPipeConnectionType::PCT_ANY);
    mPipeInA->SetConnectorClearance(150.f);

    mPipeInB = CreateDefaultSubobject<UFGPipeConnectionComponent>(TEXT("WTFluidInB"));
    mPipeInB->SetupAttachment(RootComponent);
    mPipeInB->SetRelativeLocation(WT_VANILLA_PORT_LOC);
    mPipeInB->SetRelativeRotation(WT_VANILLA_PORT_ROT);
    mPipeInB->SetPipeConnectionType(EPipeConnectionType::PCT_ANY);
    mPipeInB->SetConnectorClearance(150.f);

    // 面板：★0.2.36b 用户定案「云端用云端的」—— 上传站用原版**云端面板**（位面存储站上传器）。
    UClass* VanillaDepot = WT_FindVanillaClass(
        TEXT("/Game/FactoryGame/Buildable/Factory/CentralStorage/Build_CentralStorage.Build_CentralStorage_C"),
        TEXT("Build_CentralStorage_C"), AFGBuildable::StaticClass());
    if (VanillaDepot)
    {
        VanillaDepot->AddToRoot();
        // ★0.2.48 白模修复：恢复云端建模后，同固态两站一样从原版云端站 CDO 抄材质。
        WT_CopyVanillaMeshMaterials(VanillaDepot, MeshComp);
        WT_ReuseVanillaInteractWidget(this, VanillaDepot);
    }

    // ★0.2.48 圆口视觉件：云端建模不动，把原版圆形管道口小件叠在管道锚点外侧。
    //   两根管道口同坐标（(0,390,100) 叠放），视觉件只需一个。
    WT_AddRoundPortVisual(this, TEXT("WTRoundPortIn"), mPipeInA.Get());
}

void AWTUpgradeCloudFluidUploader::BeginPlay()
{
    Super::BeginPlay();
    WT_LogConnectionPorts(this, TEXT("ports-fluid-up"));   // ★0.2.42 接口可见性取证
    WT_LOGF(TEXT("[upgrade] fluid uploader '%s' online."), *GetName());
}

TArray<class UFGPipeConnectionComponent*> AWTUpgradeCloudFluidUploader::GetPipeConnections()
{
    TArray<class UFGPipeConnectionComponent*> Out;
    if (mPipeInA)
    {
        Out.Add(mPipeInA);
    }
    if (mPipeInB)
    {
        Out.Add(mPipeInB);
    }
    return Out;
}

void AWTUpgradeCloudFluidUploader::Tick(float dt)
{
    WT_ZONE(TEXT("Upgrade.FluidUploader.Tick"));
    Super::Tick(dt);

    mUploadCooldown -= dt;
    if (mUploadCooldown > 0.f)
    {
        return;
    }
    mUploadCooldown = mUploadInterval;
    TryUploadToCloud(dt);
}

void AWTUpgradeCloudFluidUploader::TryUploadToCloud(float dt)
{
    WT_ZONE(TEXT("Upgrade.FluidUploader.TryUpload"));
    // ★0.2.36b（用户定案）：固液分开 —— 流体记进**我们自己的液态账本**，不进原版池；
    // 下载站「原版池优先、池空才动我们的账本」。
    UFGPipeConnectionComponent* Conns[] = { mPipeInA.Get(), mPipeInB.Get() };
    for (UFGPipeConnectionComponent* Conn : Conns)
    {
        if (!Conn)
        {
            continue;
        }

        // 流体类型由管网下发到连接组件（原版机制）；管网没定型前不动。
        const TSubclassOf<UFGItemDescriptor> Fluid = Conn->GetFluidDescriptor();
        if (!Fluid)
        {
            continue;
        }

        // 原版消费端入口（精炼厂拉输入同款）：dt 进原版时间债限速，必须给真实帧时长。
        FInventoryStack Out;
        if (!Conn->Factory_PullPipeInput(dt, Out, Fluid))
        {
            continue;
        }
        if (!Out.HasItems())
        {
            continue;
        }

        UClass* Cls = Out.Item.GetItemClass().Get();
        WT_LedgerAdd(mFluidLedger, Cls, Out.NumItems);
        const int32 Total = WT_LedgerGet(mFluidLedger, Cls);
        WT_LOGF(TEXT("[cloud-fluid] '%s' uploaded %d x %s (own ledger total %d)."),
                *GetName(), Out.NumItems, *Cls->GetName(), Total);
        return;   // 一周期拉一口
    }
}

int32 AWTUpgradeCloudFluidUploader::AddToLedger(TSubclassOf<UFGItemDescriptor> FluidClass, int32 Amount)
{
    WT_LedgerAdd(mFluidLedger, FluidClass.Get(), Amount);
    return WT_LedgerGet(mFluidLedger, FluidClass.Get());
}

int32 AWTUpgradeCloudFluidUploader::TakeFromLedger(TSubclassOf<UFGItemDescriptor> FluidClass, int32 Want)
{
    return WT_LedgerTake(mFluidLedger, FluidClass.Get(), Want);
}

int32 AWTUpgradeCloudFluidUploader::GetLedgerAmount(TSubclassOf<UFGItemDescriptor> FluidClass) const
{
    return WT_LedgerGet(mFluidLedger, FluidClass.Get());
}

FText AWTUpgradeCloudFluidUploader::GetDisplayName()
{
    return WT_LOCTEXT("Upgrade_CloudFluidUploader", "Cloud Fluid Upload Station");
}

AWTUpgradeCloudFluidDownloader::AWTUpgradeCloudFluidDownloader()
{
    PrimaryActorTick.bCanEverTick = true;

    // ★0.2.48 识别色（原生喷漆槽，见 DefaultGame.ini mDefaultColorSlots：槽5=蓝=冷色组）。
    //   四机配色：仓库=黄(2)、下载器=红(1)、流体上传=青(4)、流体下载=蓝(5)。
    mColorSlot = 5;   // 冷色组（流体机）

    // ★0.2.48 外观（用户定案 2026-10-10）：主体保持**云端建模**（同上传站/固态两站），整栋不再换成储罐网格。
    //   圆口视觉由构造器末尾的端口小件叠加（WT_AddRoundPortVisual，一手依据见其注释）。
    static ConstructorHelpers::FObjectFinder<UStaticMesh> DepotMesh(
        TEXT("/Game/FactoryGame/Buildable/Factory/CentralStorage/Mesh/SM_CentralStorage_01.SM_CentralStorage_01"));

    WTMeshSpec Spec;
    Spec.Mesh = DepotMesh.Object;
    Spec.FootprintXY = 0.f;                                // 0 = 保持模型原始比例（原版尺寸）
    Spec.CubeFallbackScale = FVector(5.f, 9.f, 4.23f);     // 立方体兜底：500 x 900 x 423 uu
    UStaticMeshComponent* MeshComp = WT_CreateMainMesh(this, RootComponent, Spec);
    // ★0.2.48 A 修正：原版 MainMesh 自带 (0,180,0)；只转 mesh、不动端口（同固态两站）。
    if (MeshComp)
    {
        MeshComp->SetRelativeRotation(FRotator(0.f, 180.f, 0.f));
    }

    WT_SetClearance(this, 250.f, 450.f, 423.f);

    // ★0.2.36b 双接口（原版缓存罐 BeginPlay 索引第 2 个接口，1 个必崩；两个口必须都存在，见 §36）。
    //   下载站 = 生产端（向管网供货）。类型 PCT_ANY、余量 1.5m。固定摆原版仓库口坐标 (0,390,100)/Yaw90。
    mPipeOutA = CreateDefaultSubobject<UFGPipeConnectionComponent>(TEXT("WTFluidOutA"));
    mPipeOutA->SetupAttachment(RootComponent);
    mPipeOutA->SetRelativeLocation(WT_VANILLA_PORT_LOC);
    mPipeOutA->SetRelativeRotation(WT_VANILLA_PORT_ROT);
    mPipeOutA->SetPipeConnectionType(EPipeConnectionType::PCT_ANY);
    mPipeOutA->SetConnectorClearance(150.f);

    mPipeOutB = CreateDefaultSubobject<UFGPipeConnectionComponent>(TEXT("WTFluidOutB"));
    mPipeOutB->SetupAttachment(RootComponent);
    mPipeOutB->SetRelativeLocation(WT_VANILLA_PORT_LOC);
    mPipeOutB->SetRelativeRotation(WT_VANILLA_PORT_ROT);
    mPipeOutB->SetPipeConnectionType(EPipeConnectionType::PCT_ANY);
    mPipeOutB->SetConnectorClearance(150.f);

    // ★0.2.36b 白模修复：恢复云端建模后，同固态两站一样从原版云端站 CDO 抄材质。
    if (UClass* VanillaDepot = WT_FindVanillaClass(
            TEXT("/Game/FactoryGame/Buildable/Factory/CentralStorage/Build_CentralStorage.Build_CentralStorage_C"),
            TEXT("Build_CentralStorage_C"), AFGBuildable::StaticClass()))
    {
        VanillaDepot->AddToRoot();
        WT_CopyVanillaMeshMaterials(VanillaDepot, MeshComp);
    }

    // ★0.2.48 圆口视觉件：云端建模不动，把原版圆形管道口小件叠在管道锚点外侧。
    WT_AddRoundPortVisual(this, TEXT("WTRoundPortOut"), mPipeOutA.Get());

    // 面板：★0.2.36b 选型面板（流体类型），保持我们自己的原生选型面板。
    WT_UseNativeInteractPanel(this, UWTFluidSelectPanelWidget::StaticClass());
}

void AWTUpgradeCloudFluidDownloader::BeginPlay()
{
    Super::BeginPlay();
    WT_LogConnectionPorts(this, TEXT("ports-fluid-down"));   // ★0.2.42 接口可见性取证
    WT_LOGF(TEXT("[upgrade] fluid downloader '%s' online (selected=%s)."),
            *GetName(),
            mSelectedFluidClass ? *mSelectedFluidClass->GetName() : TEXT("<none>"));
}

TArray<class UFGPipeConnectionComponent*> AWTUpgradeCloudFluidDownloader::GetPipeConnections()
{
    TArray<class UFGPipeConnectionComponent*> Out;
    if (mPipeOutA)
    {
        Out.Add(mPipeOutA);
    }
    if (mPipeOutB)
    {
        Out.Add(mPipeOutB);
    }
    return Out;
}

void AWTUpgradeCloudFluidDownloader::SetSelectedFluid(TSubclassOf<UFGItemDescriptor> FluidClass)
{
    mSelectedFluidClass = FluidClass;
    WT_LOGF(TEXT("[cloud-fluid] '%s' selection <= %s."),
            *GetName(), FluidClass ? *FluidClass->GetName() : TEXT("<none>"));
}

void AWTUpgradeCloudFluidDownloader::Tick(float dt)
{
    WT_ZONE(TEXT("Upgrade.FluidDownloader.Tick"));
    Super::Tick(dt);

    mDownloadCooldown -= dt;
    if (mDownloadCooldown > 0.f)
    {
        return;
    }
    mDownloadCooldown = mDownloadInterval;
    TryDownloadFromCloud(dt);
}

void AWTUpgradeCloudFluidDownloader::TryDownloadFromCloud(float dt)
{
    UWorld* World = GetWorld();
    if (!World || !mSelectedFluidClass)
    {
        return;
    }
    // 防丢料第一道闸：没接管子不扣货。
    if ((!mPipeOutA || !mPipeOutA->IsConnected()) && (!mPipeOutB || !mPipeOutB->IsConnected()))
    {
        return;
    }

    // ---- 第一优先：原版云端池 ----
    int32 Removed = 0;
    AFGCentralStorageSubsystem* Sub = AFGCentralStorageSubsystem::Get(World);
    if (Sub)
    {
        const int32 Available = Sub->GetNumItemsFromCentralStorage(mSelectedFluidClass);
        if (Available > 0)
        {
            // 每周期最多 50 件（温和下载，与固态站"一周期一组"同一节奏哲学）。
            Removed = Sub->TryRemoveItemsFromCentralStorage(mSelectedFluidClass,
                                                            FMath::Min(Available, 50));
            if (Removed > 0)
            {
                WT_LOGF(TEXT("[cloud-fluid] '%s' took %d x %s from the vanilla cloud."),
                        *GetName(), Removed, *mSelectedFluidClass->GetName());
            }
        }
    }

    // ---- 第二优先：我们自己的液态云（各液态上传站的账本）----
    if (Removed <= 0)
    {
        int32 Want = 50;
        for (TActorIterator<AWTUpgradeCloudFluidUploader> It(World); It && Want > 0; ++It)
        {
            AWTUpgradeCloudFluidUploader* Station = *It;
            if (!Station)
            {
                continue;
            }
            const int32 Taken = Station->TakeFromLedger(mSelectedFluidClass, Want);
            if (Taken > 0)
            {
                Removed += Taken;
                Want -= Taken;
            }
        }
        if (Removed > 0)
        {
            WT_LOGF(TEXT("[cloud-fluid] '%s' took %d x %s from our own fluid ledger."),
                    *GetName(), Removed, *mSelectedFluidClass->GetName());
        }
    }
    if (Removed <= 0)
    {
        return;   // 原版云没有 + 我们的账本也空
    }

    // 原版生产端入口（原油萃取机出油同款）：把库存栈按原版换算率推回管网。
    FInventoryStack Stack(Removed, mSelectedFluidClass);
    int32 Pushed = 0;
    UFGPipeConnectionComponent* Conns[] = { mPipeOutA.Get(), mPipeOutB.Get() };
    for (UFGPipeConnectionComponent* Conn : Conns)
    {
        if (!Conn || !Conn->IsConnected())
        {
            continue;
        }
        FInventoryStack Remaining(Removed - Pushed, mSelectedFluidClass);
        Pushed += Conn->Factory_PushPipeOutput(dt, Remaining);
        if (Pushed >= Removed)
        {
            break;
        }
    }

    if (Pushed < Removed)
    {
        // 防丢料第二道闸：没推出去的差额，退回我们的液态账本（管网满/类型冲突时）。
        for (TActorIterator<AWTUpgradeCloudFluidUploader> It(World); It && Pushed < Removed; ++It)
        {
            AWTUpgradeCloudFluidUploader* Station = *It;
            if (!Station)
            {
                continue;
            }
            const int32 Given = FMath::Min(Removed - Pushed, 50);
            if (Given <= 0)
            {
                break;
            }
            const int32 Total = Station->AddToLedger(mSelectedFluidClass, Given);
            Pushed += Given;
            WT_LOGF(TEXT("[cloud-fluid] refund %d x %s -> '%s' (ledger %d)."),
                    Given, *mSelectedFluidClass->GetName(), *Station->GetName(), Total);
        }
    }
    if (Pushed > 0)
    {
        WT_LOGF(TEXT("[cloud-fluid] '%s' downloaded %d x %s into the pipe network."),
                *GetName(), Pushed, *mSelectedFluidClass->GetName());
    }
}

FText AWTUpgradeCloudFluidDownloader::GetDisplayName()
{
    return WT_LOCTEXT("Upgrade_CloudFluidDownloader", "Cloud Fluid Download Station");
}

// ----------------------------------------------------------------------------
// 产能加速器
// ----------------------------------------------------------------------------

AWTUpgradeAccelerator::AWTUpgradeAccelerator()
{
    PrimaryActorTick.bCanEverTick = false;

    // 退役建筑保持原样的立方体外观：旧存档里可能还有实例，改模型只会让它们更奇怪。
    WTMeshSpec Spec;
    Spec.CubeFallbackScale = FVector(0.9f, 0.9f, 1.4f);
    WT_CreateMainMesh(this, RootComponent, Spec);

    // The accelerator is a factory building because that is where the boost machinery lives, but it
    // does no crafting of its own and holds no inventory. Suppressing the build-gun's "needs
    // ingredients" expectations keeps it from showing an empty recipe slot.
    mStandingBoost = 2.0f;
}

void AWTUpgradeAccelerator::BeginPlay()
{
    Super::BeginPlay();

    // Apply the standing boost here rather than only in the constructor. SetPendingProductionBoost
    // is a virtual that routes through the factory's boost bookkeeping (and recalculates the power
    // draw that comes with it), so it wants a live, initialised object -- which is exactly what the
    // constructor does not give us.
    //
    // The boost now scales with the player's researched Accelerator level (Dyson-sphere style):
    // level 1 = 2.0x, rising geometrically and soft-capped at 30x.
    const int32 L = WT_GetTrackLevel(GetWorld(), EWTUpgradeTrack::Accelerator);
    const float Boost = WT_AcceleratorBoostAtLevel(L, WT_GetFinalRoute(GetWorld()));
    mStandingBoost = Boost;   // persist the resolved value for save/load
    SetPendingProductionBoost(Boost);

    WT_LOGF(TEXT("[upgrade] accelerator '%s' Lv%d applied standing boost x%.2f"),
            *GetName(), L, Boost);
}

FText AWTUpgradeAccelerator::GetDisplayName()
{
    return WT_LOCTEXT("Upgrade_Accelerator", "Production Accelerator");
}

// ----------------------------------------------------------------------------
// Predicates + registration
// ----------------------------------------------------------------------------

namespace Remote_FactoryUpgrades
{
    bool IsUpgradePowerStorage(const UClass* Class)
    {
        return Class && Class->IsChildOf(AWTUpgradePowerStorage::StaticClass());
    }

    bool IsUpgradeStorage(const UClass* Class)
    {
        return Class && Class->IsChildOf(AWTUpgradeStorage::StaticClass());
    }

    bool IsUpgradeCloudDownloader(const UClass* Class)
    {
        return Class && Class->IsChildOf(AWTUpgradeCloudDownloader::StaticClass());
    }

    bool IsUpgradeCloudFluidUploader(const UClass* Class)
    {
        return Class && Class->IsChildOf(AWTUpgradeCloudFluidUploader::StaticClass());
    }

    bool IsUpgradeCloudFluidDownloader(const UClass* Class)
    {
        return Class && Class->IsChildOf(AWTUpgradeCloudFluidDownloader::StaticClass());
    }

    bool IsUpgradeAccelerator(const UClass* Class)
    {
        return Class && Class->IsChildOf(AWTUpgradeAccelerator::StaticClass());
    }

    bool IsUpgradeBuilding(const UClass* Class)
    {
        return IsUpgradePowerStorage(Class) || IsUpgradeStorage(Class)
            || IsUpgradeCloudDownloader(Class) || IsUpgradeCloudFluidUploader(Class)
            || IsUpgradeCloudFluidDownloader(Class) || IsUpgradeAccelerator(Class);
    }

    float MaxProductionBoost()
    {
        // The accelerator's boost is now driven by the researched Accelerator level (see
        // WT_AcceleratorBoostAtLevel), which scales geometrically from 2.0x and is soft-capped at 30x.
        // This constant is the documented ceiling for any direct boost assignment (e.g. the panel
        // slider) and must stay in sync with WT_AcceleratorBoostAtLevel's clamp.
        return 30.0f;
    }

    /**
     * ★功能修复（2026-10-07 全代码自检）：同 StartShardStageTicker —— ticker 判重必须按**世界**。
     * 一次性 `static bool` 会让「回主菜单再进新存档」之后的每个世界都拿不到回退 ticker
     * （旧 ticker 随旧世界自停，新世界因 bool 已置位而不再注册）⇒ 建造取料的「原版没有才吃我们的」
     * 静默失效。弱引用本身就能区分世界：旧世界销毁后 .Get() 为 null ≠ 新世界。
     */
    static TWeakObjectPtr<UWorld> GCloudFallbackWorld;

    void StartCloudFallbackTicker(UWorld* World)
    {
        if (!World)
        {
            return;
        }
        if (GCloudFallbackWorld.Get() == World)
        {
            return;   // 本世界已在跑
        }
        GCloudFallbackWorld = World;

        TWeakObjectPtr<UWorld> WeakWorld(World);
        FTSTicker::GetCoreTicker().AddTicker(
            FTickerDelegate::CreateLambda([WeakWorld](float) -> bool
            {
                UWorld* W = WeakWorld.Get();
                if (!W)
                {
                    return false;   // 世界没了，停表
                }

                // ★0.2.36b 建造取料回退（用户定案）：原版云端池里某类为 0（没修建/没存货）
                // 时，把**我们账本**里的该类货以 100/次 注入池子。原版池有货时一律不动，
                // 「建造先吃原版云端、原版没有才吃我们的」因此自然成立。
                // 只处理固体（建造不用流体；流体留在液态账本里由液态下载站取）。
                for (TActorIterator<AWTUpgradeStorage> It(W); It; ++It)
                {
                    AWTUpgradeStorage* Station = *It;
                    if (!Station || Station->GetLedger().Num() == 0)
                    {
                        continue;
                    }

                    // 先快照类列表再动手（注入会改账本数组，边遍历边改会踩悬空）。
                    TArray<TSubclassOf<UFGItemDescriptor>> Classes;
                    for (const FItemAmount& IA : Station->GetLedger())
                    {
                        if (IA.Amount > 0 && IA.ItemClass.Get())
                        {
                            Classes.Add(IA.ItemClass);
                        }
                    }

                    for (const TSubclassOf<UFGItemDescriptor>& Cls : Classes)
                    {
                        UClass* Raw = Cls.Get();
                        if (!Raw || WT_PoolAmount(W, Raw) > 0)
                        {
                            continue;   // 原版云端有这类货 —— 我们的账本按兵不动
                        }
                        const int32 Take = Station->TakeFromLedger(Cls, 100);
                        if (Take > 0)
                        {
                            const int32 Injected = WT_UploadStackToPool(W, Raw, Take);
                            if (Injected < Take)
                            {
                                // 原版没收下的部分原样退回账本（防丢料）。
                                Station->AddToLedger(Cls, Take - FMath::Max(0, Injected));
                            }
                            WT_LOGF(TEXT("[cloud-fallback] vanilla cloud empty of %s -- injected %d "
                                         "from '%s' (accepted=%d)."),
                                    *Raw->GetName(), Take, *Station->GetName(), Injected);
                        }
                    }
                }
                return true;   // 继续，每 5s 复查
            }), 5.f);

        WT_LOGF(TEXT("[upgrade] cloud fallback ticker started (5s)."));
    }
}

// ★0.2.48 D(4线，2026-10-09)：电线杆式电力连接组件。
// 基类 UFGCircuitConnectionComponent 的 mMaxNumConnectionLinks 是 protected 且无公开 setter，
// 桩 ctor 里写死默认 = 1（一个口只能接 1 根线）。用户要「一个接点并 4 根线」（像电线杆），
// 故在子类构造里把上限提到 4。单口 mMax=4 比「4 个独立口同坐标、每口 mMax=1」可靠：
// 电线工具的就近取口会一直选到同一个最近口，分散方案实际只能并 1 根线。
UWT_PowerConnectionComponent::UWT_PowerConnectionComponent()
{
    // 基类 UFGCircuitConnectionComponent ctor 已隐式运行（桩里写死 mMaxNumConnectionLinks=1）。
    // 这里把每口可接线数提到 4（protected 字段，无公开 setter，只能子类 ctor 设定）。
    mMaxNumConnectionLinks = 4;
}
