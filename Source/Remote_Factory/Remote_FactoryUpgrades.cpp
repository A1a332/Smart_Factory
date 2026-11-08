#include "Remote_FactoryUpgrades.h"

#include "WTPortKit.h"                        // ★1.0.8AY 圆口/机身几何工具箱（可移植）

#include "Remote_FactoryItemSelectPanel.h"   // ★1.0.7 固态下载站的输出物品选择面板
#include "Remote_FactoryFluidPanel.h"         // ★1.0.6 恢复：云端液态下载站的流体选型面板（我们自己的液态云）
#include "Remote_FactoryLoc.h"
#include "Remote_FactoryLog.h"
#include "Remote_FactoryCrashTrace.h"
#include "Remote_FactoryUpgradeResearch.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableFactory.h"
#include "Buildables/FGBuildablePipeReservoir.h"
#include "Buildables/FGCentralStorageContainer.h"
#include "FGCentralStorageSubsystem.h"
#include "FGCentralStorageSettings.h"      // ★1.0.7o 原版上传速度的基础值（子系统取不到时回落用）
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
#include "Math/RotationMatrix.h"   // 1.0.8AV 圆口朝向（MakeFromX/MakeFromZ）
#include "Engine/World.h"
#include "TimerManager.h"   // ★2026-11-01 建造特效修复：GetTimerManager().SetTimerForNextTick 重设发光材质
#include "EngineUtils.h"
#include "UObject/UnrealType.h"                // ★1.0.7l 反射读 mStorageInventoryHandler（FindFProperty/CastField）
#include "Engine/BlueprintGeneratedClass.h"    // ★0.2.48 SCS 枚举：蓝图组件模板的唯一正规入口
#include "Engine/SimpleConstructionScript.h"   // ★0.2.48 USimpleConstructionScript::GetAllNodes()
#include "Engine/SCS_Node.h"                   // ★0.2.48 USCS_Node::GetActualComponentTemplate()
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"            // CreateDynamicMaterialInstance 返回类型
#include "Components/DynamicMeshComponent.h"              // ★1.0.8AW 运行时程序化建模（圆口法兰）
#include "UDynamicMesh.h"                                 // UDynamicMesh::GetMeshPtr
#include "GeometryScript/MeshPrimitiveFunctions.h"        // ★1.0.8AW AppendRevolvePolygon（回转体）
#include "UObject/ConstructorHelpers.h"
#include "UObject/UObjectIterator.h"          // ★0.2.48 TObjectIterator（WS_FindVanillaClass / 描述符兜底）

/**
 * ★1.0.8AO 云端流体两台站的诊断开关。
 *
 * WT_FLUID_DIAG —— 每 2 秒一行 `[cloud-fluid] up/down diag`。
 *   ★必须用 WT_WARNF（WT_LOGF 被 WT_LOG_VERBOSE=false 编译剔除，Shipping 日志里看不到）。
 *   实测确认流体通了之后可以关掉，省日志体积。
 *
 * 节流步长**不在这里**：它是头文件里 `mUploadInterval` / `mDownloadInterval`（0.2s），
 * 那个值同时充当传给原版管道接口的 dt，属于行为参数而不是诊断参数，详见头文件注释。
 */
static constexpr bool WT_FLUID_DIAG = true;

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

/**
 * ★★★边界（一手实测，误判过一次，记下来避免重犯）：SML 的 `Source/FactoryGame/Private/*.cpp`
 * 是**编译期占位**，不是运行时实现。
 *
 * 那些看上去像空桩的函数（`Factory_PullPipeInput` 返回 false、
 * `GetFluidBox` 返回 nullptr、`GetAllItemsFromCentralStorage` 什么都不做）在运行时
 * **从游戏的 `FactoryGameSteam-FactoryGame-Win64-Shipping.dll` 导入真实实现**。
 *
 * 验证方法（不靠猜）：解析 mod DLL 的 PE 导入表，对**修饰名子串**匹配。
 *   实测已确认导入自 `FactoryGameSteam-FactoryGame-Win64-Shipping.dll`：
 *     ?GetFluidBox@AFGBuildablePipeReservoir@@UEAAPEAUFFluidBox@@XZ
 *     ?Factory_PullPipeInput@UFGPipeConnectionComponent@@QEAA_NMAEAUFInventoryStack@@V?$TSubclassOf@VUFGItemDescriptor@@@@H@Z
 *   † 判据自证：必须用**子串**匹配（导入名是修饰名）、不能拿
 *     `Private/*.cpp` 的空函数当运行时行为。只有**inline**函数（如 `SetFluidIntegrant`）
 *     不在导入表里（编译进 mod），那也正常。
 *   †† 高危推论：「空桩⇒原版函数不可用⇒手写 fluid box」。实测证实不成立，
 *     且手写 `mFluidBox` 会把原版 `BeginPlay`（真实执行）算好的几何/压强参数盖掉。
 */
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
 * 把 (物品类, 数量) 直接写进云端池的 mStoredItems。
 *
 * ★0.2.36b 时它是**兜底路径**（主路径为 `WT_UploadStackToPool()`：临时单格库存 →
 * 原版 `UploadItemFromInventoryToCentralStorage`）。★1.0.8AP 起它多了一个正式身份：
 * **液体上传站的唯一入池通道** —— 原版公开入口内部只接受固体，液体只能这样写。
 *
 * 因为 mStoredItems 是 AFGCentralStorageSubsystem 的 private SaveGame+Replicated
 * TArray<FItemAmount>，按物品类聚合。代价是绕过原版的物品上限检查与通知（OnRep/委托），
 * 所以调用处一律打日志，便于事后确认是否走了这条路（与 0.2.36 抄 mInteractWidgetSoftClass /
 * 写 mMaxPotential 同一反射惯例）。
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
 * ★★1.0.8AP（用户 2026-10-28 定案「和我们现在的云端存货一样」）
 * 把老存档里记在**单个流体上传站自己账本**上的液体，一次性迁进原版云端池。
 *
 * 为什么要有这一步：0.2.36b 的定案是「云端固液分开」—— 流体只记在各站自己的
 * `mFluidLedger`，从不进原版池。后果有两个，用户都撞上了：
 *   ① **拆站即丢货**：账本挂在建筑上，`AFGBuildable::GetDismantleRefund` 只退库存，
 *      账本里的云端液体随建筑一起消失；
 *   ② 与固体不一致：固体自 1.0.6 起就**直接进原版云端池**（`PumpBufferToCentralStorage`），
 *      于是「同一个云端」在固液两侧是两套账、两个总量。
 * 1.0.8AP 起统一：液体也写进 `AFGCentralStorageSubsystem::mStoredItems`（同固态的兜底写入器
 * `WT_AddToCentralStorage`）。账本从此**只用于迁移**，迁完即空。
 *
 * 条目级处理：迁移失败（反射不可用）的条目**留在账本里**，下个周期再试 —— 绝不静默丢货。
 *
 * @return 本轮迁进池的件数（0 = 没有货 / 还没迁成功）。
 */
static int32 WT_MigrateLedgerToPool(UWorld* World, TArray<FItemAmount>& Ledger)
{
    if (!World || Ledger.Num() == 0)
    {
        return 0;
    }
    int32 Moved = 0;
    // 倒序遍历：RemoveAt 不会影响还没看的下标。
    for (int32 i = Ledger.Num() - 1; i >= 0; --i)
    {
        FItemAmount& IA = Ledger[i];
        UClass* Cls = IA.ItemClass.Get();
        if (!Cls || IA.Amount <= 0)
        {
            Ledger.RemoveAt(i);   // 空条目直接清掉，别在存档里留垃圾行
            continue;
        }
        if (WT_AddToCentralStorage(World, Cls, IA.Amount) >= 0)
        {
            Moved += IA.Amount;
            Ledger.RemoveAt(i);
        }
    }
    return Moved;
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
 * 本模组自己的云账本操作（★0.2.36b 引入，★1.0.8AP 已退役为新货不入账）。
 *
 * 历史：0.2.36b 为了「建造取料原版优先」把货先记在自己名下的账本里（上传站自持、SaveGame），
 * 只在原版池里该类为 0 时才由回退 ticker 注进池。1.0.6 起**固体**改回直接进池，1.0.8AP 起
 * **液体**也统一进池（用户定案「和我们现在的云端存货一样」）。
 *
 * ⇒ 现在这些 helper 的用途只剩三处：
 *   ① 老存档迁移（`WT_MigrateLedgerToPool` 读旧账本 → 写池）；
 *   ② 池写入不可用（mStoredItems 反射失败）时的兜底容器；
 *   ③ 固体站的历史账本回退 ticker（1.0.6 之前留下的存货）。
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

/**
 * ★★★1.0.7l 取存储建筑**真正生效**的库存组件。
 *
 * 一手依据（Public/Buildables/FGBuildableStorage.h:54，UPROPERTY 上方的官方注释）：
 *     "The inventory to store everything in.
 *      Don't use this directly, use mStorageInventoryHandler->GetActiveInventoryComponent()"
 * 也就是说 `GetStorageInventory()` 返回的 `mStorageInventory` 是**底层那个**，官方明确要求
 * 不要直接用它。而 SDK dump 里**没有** `mStorageInventoryHandler` 的成员声明 ——
 * 同款注释也出现在 `FGBuildableTrainPlatformCargo.h:317`（`mCargoInventoryHandler`）上，
 * 且同样没有声明 ⇒ 结论是「handler 存在、只是 dump 漏了」，只能靠**反射按名字**拿。
 *
 * 拿到 handler 之后再调它的 `GetActiveInventoryComponent()`（BlueprintPure）。
 * 函数名在不同版本里不一样（TrainPlatformCargo 那条注释写的是 `GetActiveInventory()`），
 * 所以两个名字都试；都不行就退回 `GetStorageInventory()`，行为与旧版一致（不会更差）。
 *
 * ★为什么这一步是「选择后不下载」的关键：往**非活跃**的底层库存 AddStack，货要么进不去
 * （Added=0 ⇒ 每周期取到货又退回账本 ⇒ 永远不出货），要么进了玩家看不见的地方。
 *
 * @param OutHow 回填实际走的路径，用于诊断日志（一眼看出是反射命中还是兜底）。
 */
static UFGInventoryComponent* WT_ResolveActiveStorageInventory(AFGBuildableStorage* Storage, FString& OutHow)
{
    OutHow = TEXT("storage-inventory(fallback)");
    if (!Storage)
    {
        OutHow = TEXT("null-storage");
        return nullptr;
    }

    // 反射结果只解析一次（类固定，不会变）。
    static FObjectPropertyBase* HandlerProp = nullptr;
    static bool bResolved = false;
    if (!bResolved)
    {
        bResolved = true;
        HandlerProp = CastField<FObjectPropertyBase>(FindFProperty<FProperty>(
            AFGBuildableStorage::StaticClass(), TEXT("mStorageInventoryHandler")));
    }

    if (HandlerProp)
    {
        UObject* Handler = HandlerProp->GetObjectPropertyValue_InContainer(Storage);
        if (Handler)
        {
            static const TCHAR* CandidateFns[] = {
                TEXT("GetActiveInventoryComponent"),
                TEXT("GetActiveInventory"),
            };
            for (const TCHAR* FnName : CandidateFns)
            {
                if (UFunction* Fn = Handler->FindFunction(FnName))
                {
                    // UFunction 的返回值是第一个 out 参数（BlueprintPure 也一样）。
                    struct FRetParams { UFGInventoryComponent* Ret; };
                    FRetParams Parms{};
                    Handler->ProcessEvent(Fn, &Parms);
                    if (UFGInventoryComponent* ActiveInv = Parms.Ret)
                    {
                        OutHow = FString::Printf(TEXT("%s::%s"),
                                                 *Handler->GetClass()->GetName(), FnName);
                        return ActiveInv;
                    }
                }
            }
            OutHow = TEXT("handler-found-but-no-active-inventory");
        }
        else
        {
            OutHow = TEXT("handler-prop-null");
        }
    }
    return Storage->GetStorageInventory();
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
 * ★1.0.8BK 一次性 dump 原版建筑蓝图的组件树（外观移植的唯一一手依据）。
 *
 * ## 为什么必须 dump 而不是看资产名猜
 * 用户 2026-10-28 提了两件纯外观的事：
 *   ① 「原版云端站中间那个动态视觉效果，我们这里没有」；
 *   ② 「把原版云端站中间掏空，把液体货运站台红框那个罐横过来塞进去」。
 * 这两件事的答案都不在类名里，而在**蓝图 SCS 里每个组件模板的 mesh / 材质 / 变换**：
 *   · 哪个 mesh 是"中间那块"，它挂的材质有哪些标量参数（流速/时间缩放之类的动态效果常由
 *     材质参数或 Panner 驱动，抄材质时父材质/参数丢了就不动）；
 *   · 液体货运站台那个罐叫什么 mesh、尺寸多大，才能算出"横过来"的旋转与缩放。
 *
 * 用 `USCS_Node::GetActualComponentTemplate()` 读**蓝图里定义的模板**（而不是 CDO 的
 * 运行时组件集合）—— 前者带原始相对变换、且不受构造期逻辑改写，正是"原版长什么样"的答案。
 *
 * 每个组件打印：节点变量名 / 组件类 / 网格资产 / 包围盒 / 材质链（含标量参数）。
 * 只打一次（调用方用 static 守卫），日志量约几十行。
 */
static void WT_DumpVanillaBuildableSCS(const TCHAR* ClassPath)
{
    UClass* Cls = LoadClass<AActor>(nullptr, ClassPath);
    if (!Cls)
    {
        WT_WARNF(TEXT("[vanilla-dump] FAILED to load '%s'"), ClassPath);
        return;
    }

    WT_WARNF(TEXT("[vanilla-dump] ===== %s ====="), ClassPath);

    UBlueprintGeneratedClass* BPC = Cast<UBlueprintGeneratedClass>(Cls);
    USimpleConstructionScript* SCS = BPC ? BPC->SimpleConstructionScript : nullptr;
    if (!SCS)
    {
        WT_WARNF(TEXT("[vanilla-dump]   no SimpleConstructionScript (not a blueprint?)"));
        return;
    }

    const TArray<USCS_Node*>& Nodes = SCS->GetAllNodes();
    WT_WARNF(TEXT("[vanilla-dump]   nodes=%d"), Nodes.Num());
    for (USCS_Node* Node : Nodes)
    {
        if (!Node)
        {
            continue;
        }
        UActorComponent* Tpl = Node->GetActualComponentTemplate(BPC);
        if (!Tpl)
        {
            WT_WARNF(TEXT("[vanilla-dump]   node='%s' template=null"),
                     *Node->GetVariableName().ToString());
            continue;
        }

        UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Tpl);
        UStaticMesh* Mesh = SMC ? SMC->GetStaticMesh() : nullptr;
        FString MeshDesc = TEXT("-");
        if (Mesh)
        {
            const FBoxSphereBounds B = Mesh->GetBounds();
            MeshDesc = FString::Printf(TEXT("%s | origin=%s extent=%s"),
                                       *Mesh->GetPathName(),
                                       *B.Origin.ToCompactString(),
                                       *B.BoxExtent.ToCompactString());
        }
        else if (SMC)
        {
            MeshDesc = TEXT("<no static mesh>");
        }

        // 相对变换只有 USceneComponent 才有（UActorComponent 是纯逻辑组件）——
        //   `GetRelativeLocation` / `GetRelativeScale3D` 都在 USceneComponent 上，
        //   对 UActorComponent* 直接调会 C2039（本轮实锤一次）。
        USceneComponent* TplScene = Cast<USceneComponent>(Tpl);
        const FString RelLoc = TplScene ? TplScene->GetRelativeLocation().ToCompactString() : TEXT("-");
        const FString RelScl = TplScene ? TplScene->GetRelativeScale3D().ToCompactString() : TEXT("-");

        WT_WARNF(TEXT("[vanilla-dump]   node='%s' comp=%s(%s) rel=%s scl=%s | mesh=%s"),
                 *Node->GetVariableName().ToString(),
                 *Tpl->GetName(),
                 *Tpl->GetClass()->GetName(),
                 *RelLoc, *RelScl,
                 *MeshDesc);

        // 材质链：父材质 + 标量参数（材质里有没有"流速/时间"这类可动参数，一眼看得出来）。
        if (SMC && SMC->GetNumMaterials() > 0)
        {
            for (int32 MatIdx = 0; MatIdx < SMC->GetNumMaterials(); ++MatIdx)
            {
                UMaterialInterface* MI = SMC->GetMaterial(MatIdx);
                if (!MI)
                {
                    continue;
                }
                UMaterialInterface* Parent = MI->GetMaterial();
                WT_WARNF(TEXT("[vanilla-dump]     mat[%d]=%s | parent=%s | dynamic=%d"),
                         MatIdx, *MI->GetPathName(),
                         Parent ? *Parent->GetPathName() : TEXT("none"),
                         MI->IsA(UMaterialInstanceDynamic::StaticClass()) ? 1 : 0);

                TArray<FMaterialParameterInfo> ScalarInfos;
                TArray<FGuid> ScalarIds;
                // ★UE5 的入口是 `GetAllScalarParameterInfo`（`GetAllScalarParameters` 在 5.x 已不存在，
                //   本轮实锤 C2039）；取值要传 `FHashedMaterialParameterInfo`。
                MI->GetAllScalarParameterInfo(ScalarInfos, ScalarIds);
                for (const FMaterialParameterInfo& Info : ScalarInfos)
                {
                    float Value = 0.f;
                    MI->GetScalarParameterValue(FHashedMaterialParameterInfo(Info), Value);
                    WT_WARNF(TEXT("[vanilla-dump]       scalar '%s'=%.3f"),
                             *Info.Name.ToString(), Value);
                }
            }
        }
    }
}

/**
 * ★1.0.8BK 从原版蓝图 SCS 里克隆一个静态网格组件到我们的站体上。
 *
 * 为什么用「读模板 + 抄」而不是硬编码 mesh/材质/变换三个常量：
 *   ① 原版组件的相对变换（位置/旋转/缩放）是设计师在蓝图里摆好的，凭包围盒反推一定会偏；
 *   ② 材质可能是 MID（动态实例）或 MI，抄组件自带的材质数组才 1:1 保留半透明/发光参数；
 *   ③ 以后游戏版本挪了位置，这段代码自动跟着走，不需要我们再改一遍常量。
 */
static UStaticMeshComponent* WT_CloneVanillaSCSComponent(
    AActor* Owner, const TCHAR* BlueprintPath, const TCHAR* NodeName,
    USceneComponent* AttachParent, FName CompName)
{
    if (!Owner || !AttachParent)
    {
        return nullptr;
    }

    UClass* Cls = LoadClass<AActor>(nullptr, BlueprintPath);
    UBlueprintGeneratedClass* BPC = Cast<UBlueprintGeneratedClass>(Cls);
    USimpleConstructionScript* SCS = BPC ? BPC->SimpleConstructionScript : nullptr;
    if (!SCS)
    {
        return nullptr;
    }

    UStaticMeshComponent* Tpl = nullptr;
    for (USCS_Node* Node : SCS->GetAllNodes())
    {
        if (Node && Node->GetVariableName().ToString().Equals(NodeName, ESearchCase::IgnoreCase))
        {
            Tpl = Cast<UStaticMeshComponent>(Node->GetActualComponentTemplate(BPC));
            break;
        }
    }
    if (!Tpl || !Tpl->GetStaticMesh())
    {
        return nullptr;
    }

    UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(Owner, CompName);
    if (!Comp)
    {
        return nullptr;
    }

    // 运行时 NewObject 的组件必须是 Movable（静态移动性在注册时会 checkf）。
    Comp->SetMobility(EComponentMobility::Movable);
    Comp->SetStaticMesh(Tpl->GetStaticMesh());
    const int32 NumMats = FMath::Max(Tpl->GetNumMaterials(), Tpl->GetStaticMesh()->GetStaticMaterials().Num());
    for (int32 i = 0; i < NumMats; ++i)
    {
        if (UMaterialInterface* M = Tpl->GetMaterial(i))
        {
            Comp->SetMaterial(i, M);
        }
    }
    Comp->SetRelativeTransform(Tpl->GetRelativeTransform());

    // 纯装饰件：不碰撞、不投影、不参与导航。
    //   投影必须关掉 —— 半透明发光面片投影是一块纯黑剪影，比没有效果更难看。
    Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Comp->SetGenerateOverlapEvents(false);
    Comp->SetCastShadow(false);
    Comp->bReceivesDecals = false;
    Comp->SetCanEverAffectNavigation(false);

    // 先挂到父组件、再注册进世界（规范顺序：先 Attach 再 Register，避免注册时变换层级未就绪）。
    Comp->AttachToComponent(AttachParent, FAttachmentTransformRules::KeepRelativeTransform);
    Comp->RegisterComponent();
    return Comp;
}

/**
 * ★2026-11-01 建造特效修复：把发光 MID 设到光膜组件上。抽成独立函数以便「建造特效结束后」再次调用。
 * 方向：材质标量 Speed = 流光速率/方向，具体正负约定由调用点按站显式传入（见 WT_EnsureGlowFogPlane）。
 * 不再用单一全局符号反两站——两站底座朝向不同，同一符号在两站视觉方向相反，全局反号会把下载站也翻错。
 */
static void WT_ApplyGlowMaterial(UStaticMeshComponent* Fog, float FlowSpeed)
{
    if (!Fog)
    {
        return;
    }
    if (UMaterialInterface* Base = Fog->GetMaterial(0))
    {
        if (UMaterialInstanceDynamic* MID = Fog->CreateDynamicMaterialInstance(0, Base))
        {
            MID->SetScalarParameterValue(
                TEXT("Speed"),
                FlowSpeed);
            Fog->SetMaterial(0, MID);
        }
    }
}

/**
 * ★2026-11-01 建造特效修复：建造特效结束后再把发光 MID 设回去。
 * 原版 SM_GlowFogPlane_01 没有「建造特效材质交换数据」（UFGBuildEffectMaterialSwapAssetData），
 * 建造特效对它的材质 remap 会落到 mBuildEffectFallback（黑），导致中央纯黑（游戏日志可见
 * `[BuildEffect] Failed to find remap material ... using fallback!`）。
 * 本函数在建造特效结束（mBuildEffectIsPlaying=false）后重设发光 MID 并强制可见，消除黑屏。
 * 自身用下一帧定时器自我重挂，直到建造特效结束才落定，避免与特效动画期间的状态互相打架。
 */
static void WT_GlowBuildEffectFixup(AActor* Owner, UStaticMeshComponent* Fog, bool bFlowOutward, float FlowSpeed)
{
    if (!IsValid(Owner) || !IsValid(Fog))
    {
        return;
    }
    AFGBuildable* BB = Cast<AFGBuildable>(Owner);
    const bool bPlaying = BB && BB->IsPlayingBuildEffect();
    if (bPlaying)
    {
        // ★施工期间保持隐藏（光膜是运行时加的组件，不参与建造特效材质交换，会以原始材质露出
        //   一块暗色外星有机纹理方框）；下一帧再查，不落定。
        Fog->SetVisibility(false, true);
        Fog->SetHiddenInGame(true);
        if (Owner->GetWorld())
        {
            FTimerDelegate Del;
            Del.BindStatic(&WT_GlowBuildEffectFixup, Owner, Fog, bFlowOutward, FlowSpeed);
            Owner->GetWorld()->GetTimerManager().SetTimerForNextTick(Del);
        }
        return;
    }

    // 建造特效已结束（或无特效）：重设发光 MID 并强制可见。
    WT_ApplyGlowMaterial(Fog, FlowSpeed);
    Fog->SetVisibility(true, true);
    Fog->SetHiddenInGame(false);

    // ★一次性运行时诊断（供用户进游戏核对修复是否生效）：确认最终材质是发光 MID 且组件可见。
    {
        static bool bFixupDiagDone = false;
        if (!bFixupDiagDone)
        {
            bFixupDiagDone = true;
            const float WTDirSpeed = FlowSpeed;
            UMaterialInterface* GMat = Fog->GetMaterial(0);
            WT_WARNF(TEXT("[glow] fixup applied mat=%s visible=%d hiddenInGame=%d Speed=%.2f %s"),
                     GMat ? *GMat->GetName() : TEXT("<null>"),
                     Fog->IsVisible() ? 1 : 0,
                     Fog->bHiddenInGame ? 1 : 0,
                     WTDirSpeed,
                     bFlowOutward ? TEXT("OUTWARD") : TEXT("INWARD"));
        }
    }
}

/**
 * ★恢复（用户 2026-10-31「现在先把云端固体站的光膜加入」+「光膜要符合上传下载的方向」）：
 * 1:1 克隆原版位面存储站中央那块动态发光雾面片（网格 SM_GlowFogPlane_01 + 材质
 * MM_AlienGlowPlane_01，均来自原版蓝图 SCS 节点 SM_GlowFogPlane_01），保留设计师摆好的
 * 相对变换，让固体站也有原版那个中央蓝雾面片。
 * 气流方向此前反复调不对，2026-11-02 用户决定直接移除气流：Speed=0 ⇒ 材质 UV 不滚动 ⇒
 * 光膜为静止发光面（无所谓朝外/朝里）。两站统一设 0，不再逐站区分方向，也不再几何翻转。
 * 仅固体上传/下载站调用；液体站已整体移除，不进此函数。
 */

/**
 * ★2026-11-02：销毁一个建筑上所有「光膜相关」组件——当前名 WTCloudGlowPlane，以及 1.0.8CA 旧名
 * WTGlowFogPlane / WTGlowFogPlaneB。供 EndPlay 与 BeginDestroy 双重兜底调用，确保无论拆解走哪条路径，
 * 运行时光膜都不会残留（用户实测 EndPlay 单独不足以消除残留）。销毁时打一次诊断日志，便于进游戏核对。
 */
static void WT_DestroyGlowComponents(AActor* Owner, TObjectPtr<UStaticMeshComponent>& GlowPlane, const TCHAR* Reason)
{
    if (!Owner)
    {
        return;
    }
    int32 Found = 0, Destroyed = 0;
    const bool bPointerHit = (GlowPlane != nullptr);
    // ① 精确销毁持有指针指向的发光面片（最可靠：不依赖 GetFName 按名匹配，规避
    //    NewObject 重名后缀 / GetFName 比较失效导致拆除后残留）。
    if (GlowPlane)
    {
        ++Found;
        GlowPlane->DestroyComponent();
        GlowPlane = nullptr;
        ++Destroyed;
    }
    // ② 兜底：任何名字以 WTCloudGlow / WTGlow 开头的残留光膜组件（兼容旧存档
    //    WTGlowFogPlane / WTGlowFogPlaneB，以及重复创建被引擎加后缀的 WTCloudGlowPlane_N）。
    //    不碰 WTCloudPortArrow（液体站专属，固体两站无此组件）。
    TArray<UActorComponent*> Comps;
    Owner->GetComponents(Comps);
    for (UActorComponent* C : Comps)
    {
        if (C)
        {
            const FString Name = C->GetFName().ToString();
            if (Name.StartsWith(TEXT("WTCloudGlow")) || Name.StartsWith(TEXT("WTGlow")))
            {
                ++Found;
                C->DestroyComponent();
                ++Destroyed;
            }
        }
    }
    if (Found > 0)
    {
        WT_WARNF(TEXT("[glow] %s glow-cleanup found=%d destroyed=%d pointerHit=%d"), Reason, Found, Destroyed, bPointerHit ? 1 : 0);
    }
}

static UStaticMeshComponent* WT_EnsureGlowFogPlane(
    AActor* Owner, const FVector& PortLoc, const FVector& OutDir,
    float TargetSize, float Inset, bool bFlowOutward)
{
    if (!Owner)
    {
        return nullptr;
    }

    // 这几个参数在「1:1 克隆原版中央面片」路线下不再用于定位（原生变换已在 SCS 里摆好），
    // 但保留签名以兼容两处调用点。显式 void 掉，避免某些引擎配置把 unused-parameter 当错。
    (void)PortLoc; (void)OutDir; (void)TargetSize; (void)Inset;

    // 旧存档可能残留 1.0.8CA 建的 WTGlowFogPlane / WTGlowFogPlaneB 组件 ⇒ **销毁**（之前只隐藏会永久残留，
    // 且用户实测拆除后仍可见）。同时销毁任何已有 WTCloudGlowPlane，避免 BeginPlay 被多次调用时叠加多个光膜。
    {
        TArray<UActorComponent*> OldComps;
        Owner->GetComponents(OldComps);
        for (UActorComponent* C : OldComps)
        {
            if (C && (C->GetFName() == TEXT("WTGlowFogPlane") || C->GetFName() == TEXT("WTGlowFogPlaneB") || C->GetFName() == TEXT("WTCloudGlowPlane")))
            {
                C->DestroyComponent();
                WT_WARNF(TEXT("[glow] BeginPlay destroyed legacy/pre-existing glow component %s"), *C->GetName());
            }
        }
    }

    // 1:1 克隆原版中央发光面片（网格 + 材质 + 相对变换全抄），挂到 RootComponent。
    UStaticMeshComponent* Fog = WT_CloneVanillaSCSComponent(
        Owner,
        TEXT("/Game/FactoryGame/Buildable/Factory/CentralStorage/Build_CentralStorage.Build_CentralStorage_C"),
        TEXT("SM_GlowFogPlane_01"),
        Owner->GetRootComponent(),
        TEXT("WTCloudGlowPlane"));
    if (!Fog)
    {
        return nullptr;
    }

    // 位置修正（用户实测：光膜贴在背面/屁股，要求移到正面开口处）：
    //   原版 SCS 节点 SM_GlowFogPlane_01 的相对变换是 identity（relLoc=0,relRot=0，见运行时 [glow] diag），
    //   但其网格几何中心实测烘焙在 local(0,-465,+178)，所以 1:1 克隆后光膜落在 -Y（建筑背面/屁股）。
    //   绕世界 Up(Z) 轴 Yaw 180° 把它镜像到 +Y（正面开口侧），同时让原本朝 -Y 的可见面转向 +Y（玩家侧）。
    //   两站一致，不再逐站翻转（气流方向已由 Speed=0 移除）。
    {
        // 位置修正：原版 SCS 节点 SM_GlowFogPlane_01 几何中心烘焙在 local(0,-465,+178)，
        // 1:1 克隆后落在 -Y（建筑背面）。绕世界 Up 轴 Yaw 180° 把它镜像到 +Y（正面开口侧），
        // 同时让原本朝 -Y 的可见面转向 +Y（玩家侧）。
        FTransform WT = Fog->GetRelativeTransform();
        WT.SetRotation(FRotator(0.f, 180.f, 0.f).Quaternion());
        // ★2026-11-02 用户实测：镜像翻转（X 缩放取负）对「同心环」图案**无视觉效果**——
        //   同心圆角方框是 4 向对称的，镜像/旋转/翻面都不改其外观 ⇒ 已撤回该几何 hack（无效且
        //   负缩放可能带来法线/光照副作用）。方向改由材质标量 Speed 的符号控制（见下方 FlowSpeed）。
        Fog->SetRelativeTransform(WT);
    }

    // ★气流移除（用户最终决定）：此前反复调 Speed 正负（±1）想控制光膜朝外/朝里流动，
    //   但同心环图案无几何手性，Speed 符号对可见方向无效；用户决定**直接移除气流** ⇒
    //   Speed=0，光膜为静止发光面（无所谓朝外/朝里）。两站统一 0，bFlowOutward 不再驱动
    //   光膜流动，仅保留给端口方向箭头（WT_AddPortDirectionArrow）。
    const float FlowSpeed = 0.f;
    WT_ApplyGlowMaterial(Fog, FlowSpeed);

    // 取证日志（每个站实例打一次）：确认两站 Speed 与朝向。气流已移除 ⇒ 两站均 STATIC。
    {
        WT_WARNF(TEXT("[glow] dir type=%s bFlowOutward=%d Speed=%.2f => %s"),
                 bFlowOutward ? TEXT("download") : TEXT("upload"),
                 bFlowOutward ? 1 : 0,
                 FlowSpeed,
                 TEXT("STATIC(气流已移除/无流动)"));
    }

    // ★一次性运行时诊断（用户 2026-10-31「光膜不对」→「走原版效果」）：
    //   离线 mesh 解析不可靠（该 mesh 的 uexp 是压缩的），改为让游戏把**真实**网格/包围盒/
    //   相对变换/材质打出来，用于和原版对齐。每进程只打一条，避免刷屏。
    {
        static bool bGlowDiagDone = false;
        if (!bGlowDiagDone)
        {
            bGlowDiagDone = true;
            const FBoxSphereBounds GB = Fog->Bounds;
            const FTransform GT = Fog->GetRelativeTransform();
            UStaticMesh* GM = Fog->GetStaticMesh();
            UMaterialInterface* GMat = Fog->GetMaterial(0);
            // ★父级 RootComponent 的世界缩放：若它是非均匀缩放（自定义模型常被缩放对齐），
            //   用 KeepRelativeTransform 挂上去的平面会被拉成「竖条」——这是「竖条」首要嫌疑。
            const FVector RootScl = (Owner->GetRootComponent())
                ? Owner->GetRootComponent()->GetComponentScale()
                : FVector::ZeroVector;
            WT_WARNF(TEXT("[glow] diag mesh=%s bounds(orig=%s ext=%s r=%.1f) relLoc=%s relRot=%s relScl=%s rootScl=%s mat=%s actorLoc=%s"),
                     GM ? *GM->GetName() : TEXT("<null>"),
                     *GB.Origin.ToString(), *GB.BoxExtent.ToString(), GB.SphereRadius,
                     *GT.GetLocation().ToString(),
                     *GT.GetRotation().Rotator().ToString(),
                     *GT.GetScale3D().ToString(),
                     *RootScl.ToString(),
                     GMat ? *GMat->GetName() : TEXT("<null>"),
                     *Owner->GetActorLocation().ToString());
        }
    }

    // ★建造特效修复（2026-11-01）：原版 SM_GlowFogPlane_01 没有「建造特效材质交换数据」
    // （UFGBuildEffectMaterialSwapAssetData），建造特效对它做材质 remap 会落到 mBuildEffectFallback
    // （黑）⇒ 中央纯黑。若此刻建造特效正在播放，挂一个「特效结束后再重设发光 MID + 强制可见」
    // 的自重挂定时器，确保光膜最终呈现原版整面发光，而不是被黑材质覆盖。
    {
        AFGBuildable* BB = Cast<AFGBuildable>(Owner);
        if (BB && BB->IsPlayingBuildEffect() && Owner->GetWorld())
        {
            // 施工中：先隐藏（否则光膜以原始材质露出暗色外星纹理方框），等特效结束由 fixup
            // 重设发光 MID 后再显示。
            Fog->SetVisibility(false, true);
            Fog->SetHiddenInGame(true);
            FTimerDelegate FixupDel;
            FixupDel.BindStatic(&WT_GlowBuildEffectFixup, Owner, Fog, bFlowOutward, FlowSpeed);
            Owner->GetWorld()->GetTimerManager().SetTimerForNextTick(FixupDel);
        }
        else
        {
            // 无建造特效（存档载入 / 已建成）：光膜直接可见。
            Fog->SetVisibility(true, true);
            Fog->SetHiddenInGame(false);
        }
    }
    return Fog;
}

/**
 * ★圆孔（管道接口）方向标注（用户 2026-10-31「圆形孔【管道接口】要标注出方向」→ 选定「圆孔加静态箭头」）：
 * 在管道口生成一个静态箭头，标出流向 —— 上传站货进云端（指向内）、下载站货出云端（指向外）。
 * 复用原版 BuildGun 的 Arrow 网格 + 原版发光材质 MM_AlienGlowPlane_01（均既有资产，不新增材质），
 * 与中央光膜视觉一致。
 * 注意：Arrow 原生网格朝向未知（默认按 +X 为箭尖）。若游戏内箭头方向反了，把
 * WT_ARROW_DIR_SIGN 翻成 -1.f（或把 MakeFromX 换成 MakeFromY）即可。
 */
static UStaticMeshComponent* WT_AddPortDirectionArrow(
    AActor* Owner, const FVector& PortLoc, const FVector& OutDir, bool bFlowOutward)
{
    if (!Owner)
    {
        return nullptr;
    }

    // 防重复：已建过就跳过（BeginPlay 偶发多次时不叠箭头）。
    {
        TArray<UActorComponent*> Comps;
        Owner->GetComponents(Comps);
        for (UActorComponent* C : Comps)
        {
            if (C && C->GetFName() == TEXT("WTCloudPortArrow"))
            {
                return nullptr;
            }
        }
    }

    UStaticMesh* ArrowMesh = LoadObject<UStaticMesh>(
        nullptr, TEXT("/Game/FactoryGame/Equipment/Buildgun/Mesh/Arrow.Arrow"));
    if (!ArrowMesh)
    {
        return nullptr;
    }
    UMaterialInterface* GlowMat = LoadObject<UMaterialInterface>(
        nullptr, TEXT("/Game/FactoryGame/Buildable/Factory/CentralStorage/Material/MM_AlienGlowPlane_01.MM_AlienGlowPlane_01"));

    UStaticMeshComponent* Arrow = NewObject<UStaticMeshComponent>(Owner, TEXT("WTCloudPortArrow"));
    if (!Arrow)
    {
        return nullptr;
    }
    Arrow->SetMobility(EComponentMobility::Movable);
    Arrow->SetStaticMesh(ArrowMesh);
    if (GlowMat)
    {
        const int32 N = ArrowMesh->GetStaticMaterials().Num();
        for (int32 i = 0; i < N; ++i)
        {
            Arrow->SetMaterial(i, GlowMat);
        }
    }

    // 箭头指向：下载站=外流（沿 +OutDir）、上传站=内流（沿 -OutDir）。
    const float WT_ARROW_DIR_SIGN = 1.f;   // 若反向改成 -1.f
    FVector ArrowDir = WT_ARROW_DIR_SIGN * (bFlowOutward ? OutDir : -OutDir);
    ArrowDir.Normalize();
    // 原生 Arrow 网格默认箭尖朝 +X ⇒ 用 X 轴对齐流向。
    FRotator Rot = FRotationMatrix::MakeFromX(ArrowDir).Rotator();
    // 从端口沿流向稍推出，避免埋在机身里。
    FVector Pos = PortLoc + OutDir.GetSafeNormal() * 20.f;

    const float WT_ARROW_SCALE = 2.f;       // Arrow 原生较小，放大到可见；按观感调
    Arrow->SetRelativeLocationAndRotation(Pos, Rot);
    Arrow->SetRelativeScale3D(FVector(WT_ARROW_SCALE));

    Arrow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Arrow->SetGenerateOverlapEvents(false);
    Arrow->SetCastShadow(false);
    Arrow->bReceivesDecals = false;
    Arrow->SetCanEverAffectNavigation(false);

    Arrow->RegisterComponent();
    Arrow->AttachToComponent(Owner->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
    return Arrow;
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
// ★2026-11-02：流体站圆口抬到门洞中心 Z=178（与中央光膜同高，门洞约 2.5m 见方 ⇒ 中心≈1.78m）。
//   固体站传送带口仍用 WT_VANILLA_PORT_LOC（离地 1m），两者不可混用。
static const FVector  WT_FLUID_PORT_LOC(0.f, 390.f, 178.f);

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
// ★★★1.0.8AT 已废弃「读包围盒算游口锚点」的方案（保留结论，不留废代码）：
//
// 1.0.8AS 曾用 `UStaticMesh::GetBounds()` 把两个管道口推到包围盒表面外 10cm。
//   游戏内实测取证（本局日志，`fluid up port anchor`）：
//     mesh=SM_CentralStorage_01 extent=V(X=236.95, Y=488.82, Z=214.64)（原点 Y=-23.6）
//   即实尺寸 473.9 x 977.6 x 429.3 uu。而这个 extent 是**四条腿 + 顶檐的外框**，
//   主体墙面远在里面（原版方形口 390 的那个门洞一层）⇒ 游口悬在墙外约 1.1m，
//   高度 1m 又埋在草里（用户截图实锤）。包围盒在这类「框架式建筑」上不代表墙面。
//
// 用户 2026-10-28 定案：就放在原版方形接口的位置（`WT_VANILLA_PORT_LOC/ROT`，
//   = `_cs_ports.py` 解析 Build_CentralStorage.uasset 的 SCS 一手数据），并用原尺寸圆口
//   法兰盖住那个方形门洞 —— 「移除方形接口，使用圆形接口」。
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// ★1.0.8ASb BeginPlay 取证用：**确定性**拿主网格组件。
//   FindComponentByClass 内部遍历的是组件集合（无序），actor 上除了 WTMainMesh 还有
//   两个圆口视觉件（WTRoundPortInA/B 也是 UStaticMeshComponent）—— 运气差会拿到
//   SM_Pipe_InPut_01 的小包围盒，诊断行里的 mesh extent 就被污染了。
//   修法：先按组件名 WTMainMesh 精确匹配；名字 miss 再退化为「静态网格资产包围盒
//   最大者」（主网格必然比圆口小件大几个量级）。
// ----------------------------------------------------------------------------
static UStaticMeshComponent* WT_FindMainMeshComp(const AActor* Self)
{
    if (!Self)
    {
        return nullptr;
    }
    UStaticMeshComponent* Best = nullptr;
    float BestExtent = -1.f;
    TInlineComponentArray<UStaticMeshComponent*> Comps(Self);
    for (UStaticMeshComponent* C : Comps)
    {
        if (!C)
        {
            continue;
        }
        if (C->GetName() == TEXT("WTMainMesh"))
        {
            return C;   // 名字精确命中，直接用
        }
        // 注：UStaticMeshComponent 没有 GetBounds 成员（上一版 C2039 实锤），
        //   包围盒一律从 UStaticMesh 资产上取 —— 与 BeginPlay 诊断行的口径一致。
        const UStaticMesh* SM = C->GetStaticMesh();
        const float E = SM ? SM->GetBounds().BoxExtent.GetAbsMax() : -1.f;
        if (E > BestExtent)
        {
            BestExtent = E;
            Best = C;
        }
    }
    return Best;
}

// ----------------------------------------------------------------------------
// 1.0.8AV 可复用「圆形管道接口」：自建建模 + 一体化程序配置
//
// 一手依据（为什么必须自己做模型、不能拿原版件硬盖）：
//   · 方形门洞**焊在 MainMesh 本体里**：`_cs_ports.py` 解 Build_CentralStorage.uasset，
//     SCS 只有 Input0(传送带口) + FGFactoryLegs + 3 mesh + 4 梯子，没有独立方口组件，删不掉。
//   · 原版 `SM_Pipe_InPut_01` 是粒子加速器的粗管法兰（运行时实测 extent 直径 199.6uu≈2m、
//     内孔约 1.5m），套在普通管道(~1m)上既太大又盖不住门洞。用户 2026-10-28 定案
//     「为什么要去盖住？直接制作新的建模不就行了？」
//
// 自建网格：`/Smart_Factory/PortMesh/SM_PipePort_01`
//   由编辑器内 GeometryScript 程序化生成（脚本 C:/SMLDev/make_port_mesh.py，11s 跑完）
//     = 法兰圆盘(Ø290) ∪ 管颈(Ø150) − 内孔(Ø110)
//   尺寸：法兰 Ø290uu(2.9m) / 通孔 Ø110uu(1.1m，适配原版管道 ~1m) / 法兰厚 25 / 管颈 60 / 32 段
//   约定：**网格原点 = 法兰前表面中心，管口朝 +Z**（装配器就按这个约定摆位）
//
// 程序配置（大世界复用的入口）：
//   `FWTPipePortSpec` + `WT_AddRoundPipePort` 一次调用同时建好
//     · 管道连接组件（位置/朝向/类型/间隙）
//     · 视觉法兰（位置/朝向/缩放/外推量，材质继承主网格）
//   任何新建筑只要给一个 Spec 就能长出合规圆口；换规格只改 Spec 里的数。
// ----------------------------------------------------------------------------

// 视觉件沿管口方向的外推量（uu）：接口点 (0,390,100) 贴着墙面，
// 不外推会整块埋进 MainMesh 里看不见。1.0.8AU 那版法兰前表面落到 Y=473 时用户能看清整个口，
// 现在取 80 ⇒ 前表面 Y=470，与那版齐平（用户已验证过这个深度不会显得悬空）。
// **外观要微调就只改这一个数**（BeginPlay 会把 portLoc / 世界包围盒 / 材质名打进日志，一次对准）。
static constexpr float WT_PORT_VISUAL_PUSH = 80.f;

// ★1.0.8AY 圆口与机身的几何实现已抽到 `WTPortKit.h`
//   （单头文件工具箱，可整个文件拷到其它 Satisfactory mod 复用，接入说明见该文件头注释）。

// ----------------------------------------------------------------------------
// ★1.0.8BQ 原版方形口实测量（用户 2026-10-29 定案原话：「先找到原版的方形口的四条边的
//   中心点，然后以四条边中心点为初始位置形成一个圆形，则就是液体输入口，且适配原版
//   管道横截面大小」）。
//
// 原理（全部本局世界坐标实测，不再信任何硬编码 —— 1.0.8BP「Ø2.9m 法兰盖不严、漏出
//   Mercer 球」的教训：bounding 推出来 wallY=512、旧口径门面 470，跟真实开口对不上）：
//   BeginPlay（游戏线程）从站体正前方对前脸打**网格射线**（bTraceComplex）：
//   ★1.0.8BR 修正：1.0.8BQ 用 World->LineTraceSingleByChannel + AddIgnoredActor(Self)
//   —— 实测 864 格全线打空（bin hits=0/864）：AddIgnoredActor 忽略的是自身 actor 的
//   **全部组件**，恰恰包括要测的原版主网格 ⇒ 测量永远失败回落兜底（用户截图实锤）。
//   改用 `UPrimitiveComponent::LineTraceComponent`（PrimitiveComponent.h:3001）**只对
//   VanillaMesh 一个组件打**：雾面片/Mercer 球/其它建筑天然不参与，无需忽略任何人。
//   · 命中点 Y 聚在同一平面 ⇒ 实体墙面；出现次数最多的 10uu 档 = 门面平面（众数比均值
//     抗凸起细节，门面是前脸最大的一块平面）；
//   · 命中点明显更靠里（或打空）⇒ 该格落在开口里。
// 开口格的 min/max X/Z = 方形口四条边；四条边中点 → 内切圆：
//   圆心 = ((minX+maxX)/2, (minZ+maxZ)/2)，半径 = min(HalfX, HalfZ)。
// ----------------------------------------------------------------------------
struct FWTSquareOpeningMeasure
{
    bool  bFound  = false;
    float CenterX = 0.f;    // 开口中心 X（actor 空间）
    float CenterZ = 0.f;    // 开口中心 Z（actor 空间；actor 原点在建筑底部，Z 0..429）
    float HalfX   = 0.f;    // 开口半宽
    float HalfZ   = 0.f;    // 开口半高
    float WallY   = 0.f;    // 门面平面 Y（actor 空间，开口四周实体面的众数档）
    // ★1.0.8CG：圆口圆盘足迹（Ø200）内最浅腔内面（背板/机内壁）——「往里缩」的硬边界。
    float BackY   = 0.f;    // 足迹内开口格的最浅命中 Y（actor 空间）；仅 bBack 时有效
    bool  bBack   = false;  // 足迹内测到过开口格才 true（bFound 且圆盘压到开口）
    float RecessDepth = 0.f; // 开口最大进深（门面到腔体最背壁，actor 空间；through 封顶 300）
};

static FWTSquareOpeningMeasure WT_MeasureSquareOpening(AActor* Self,
                                                       UStaticMeshComponent* VanillaMesh,
                                                       float HintWallY)
{
    FWTSquareOpeningMeasure Out;
    UWorld* World = Self ? Self->GetWorld() : nullptr;
    if (!World || !Self || !VanillaMesh)
    {
        return Out;
    }

    const FTransform& ActorXf = Self->GetActorTransform();
    const float YStart = HintWallY + 200.f;   // 起点：墙前 2m（actor 空间）
    const float YEnd   = HintWallY - 500.f;   // 终点：穿墙深入机内 5m

    struct FSample { float X, Z, HitY; };
    TArray<FSample> Samples;
    Samples.Reserve(1024);
    TMap<int32, int32> Hist;                  // 10uu 档 → 命中数

    // ★1.0.8BR：只对原版主网格组件打射线（bTraceComplex 走渲染网格精度）。
    //   不再用 World trace + AddIgnoredActor —— 那会把要测的墙自己也忽略掉，
    //   实测 864 格全线打空（1.0.8BQ 用户截图+日志实锤）。
    //   LineTraceComponent 对组件 BodyInstance 查询：Mercer 球/雾面片/别的建筑天然不参与。
    FCollisionQueryParams Params(TEXT("WTMeasureOpening"), /*bTraceComplex=*/true);

    const float Step = 15.f;
    int32 Total = 0;
    for (float Z = 15.f; Z <= 415.f; Z += Step)
    {
        for (float X = -235.f; X <= 235.f; X += Step)
        {
            ++Total;
            const FVector SWorld = ActorXf.TransformPosition(FVector(X, YStart, Z));
            const FVector EWorld = ActorXf.TransformPosition(FVector(X, YEnd, Z));
            FHitResult Hit;
            if (VanillaMesh->LineTraceComponent(Hit, SWorld, EWorld, Params))
            {
                const FVector HitLocal = ActorXf.InverseTransformPosition(Hit.ImpactPoint);
                Samples.Add({ X, Z, (float)HitLocal.Y });
                ++Hist.FindOrAdd(FMath::RoundToInt((float)HitLocal.Y / 10.f));
            }
            else
            {
                // 完全打空 = 开口正后方也没东西，按开口处理（Y 记为终点，绝不进墙面档）。
                Samples.Add({ X, Z, YEnd - 100.f });
            }
        }
    }
    if (Samples.Num() < 32)
    {
        WT_WARNF(TEXT("[portmeasure] only %d/%d samples hit-eligible -> measure FAILED"),
                 Samples.Num(), Total);
        return Out;
    }

    // 门面平面 = 命中最多的 10uu 档（众数）。
    int32 BestBin = 0, BestCount = 0;
    for (const TPair<int32, int32>& KV : Hist)
    {
        if (KV.Value > BestCount)
        {
            BestCount = KV.Value;
            BestBin = KV.Key;
        }
    }
    Out.WallY = BestBin * 10.f;

    // 开口格：命中比门面平面明显靠里（阈值 60uu，吃掉墙面浮雕噪声）。
    constexpr float kOpenDepth = 60.f;
    constexpr float kBig = UE_BIG_NUMBER;
    float MinX = kBig, MaxX = -kBig;
    float MinZ = kBig, MaxZ = -kBig;
    int32 OpenPts = 0;
    for (const FSample& S : Samples)
    {
        if (S.HitY < Out.WallY - kOpenDepth)
        {
            ++OpenPts;
            MinX = FMath::Min(MinX, S.X);  MaxX = FMath::Max(MaxX, S.X);
            MinZ = FMath::Min(MinZ, S.Z);  MaxZ = FMath::Max(MaxZ, S.Z);
        }
    }
    Out.CenterX = (MinX + MaxX) * 0.5f;
    Out.CenterZ = (MinZ + MaxZ) * 0.5f;
    Out.HalfX   = (MaxX - MinX) * 0.5f;
    Out.HalfZ   = (MaxZ - MinZ) * 0.5f;

    // ★1.0.8CU：开口最大进深（门面到腔体最背壁），through 封顶 300。
    {
        float MinOpenY = kBig;
        for (const FSample& S : Samples)
        {
            if (S.HitY < Out.WallY - kOpenDepth) MinOpenY = FMath::Min(MinOpenY, S.HitY);
        }
        Out.RecessDepth = (MinOpenY < kBig) ? FMath::Min(Out.WallY - MinOpenY, 300.f) : 0.f;
    }

    // 尺寸合理性门禁：开口必须存在且在"一个门洞"量级（格距 15 ⇒ 半尺寸至少吃 4 格）；
    //   众数档至少 20 格命中 —— 防"全线打空（碰撞未注册/被忽略）⇒ WallY=0、全网格被当
    //   开口"的假绿路径：没有可信的墙面平面就一律回落硬编码兜底。
    //   上限 210/180（行为推演反例3 实锤：235/210 放得过宽，"整面墙都空"会溜进去）。
    Out.bFound = BestCount >= 20 &&
                 OpenPts >= 12 && Out.HalfX >= 60.f && Out.HalfZ >= 60.f &&
                 Out.HalfX <= 210.f && Out.HalfZ <= 180.f;

    // ★1.0.8CG：圆口圆盘足迹内最浅腔内面。移植网格 Ø200（半径 100，BU bounds 实锤），
    //   中心 = (CenterX, CenterZ-46)（46 = 1.0.8CE 眼调下移量；改那个数这里同步改）。
    //   足迹内开口格（比门面深 60+）的最浅命中 = 鼓身往里收会顶到的第一道面 ⇒ 退距硬边界。
    if (Out.bFound)
    {
        constexpr float kMeshR = 105.f;   // 网格半径 100 + 5 余量
        const float PortCx = Out.CenterX;
        const float PortCz = Out.CenterZ - 46.f;
        float BackY = -kBig;
        int32 BackCells = 0;
        for (const FSample& S : Samples)
        {
            if (S.HitY >= Out.WallY - kOpenDepth) continue;   // 只看开口格（打空的极深格不影响 max）
            const float Dx = S.X - PortCx;
            const float Dz = S.Z - PortCz;
            if (Dx * Dx + Dz * Dz > kMeshR * kMeshR) continue;
            BackY = FMath::Max(BackY, S.HitY);
            ++BackCells;
        }
        if (BackCells > 0)
        {
            Out.BackY = BackY;
            Out.bBack = true;
            WT_WARNF(TEXT("[portmeasure] 1.0.8CG disc backY=%.1f (depth %.1f from wall, cells=%d)"),
                     Out.BackY, Out.WallY - Out.BackY, BackCells);
        }
    }

    // ★1.0.8CU：[portmap] 逐行开口深度地图 —— 一口实锤「方形开口」真实形状：
    //   逐 Z 行扫描开口格（HitY < WallY - kOpenDepth），输出该行的 void X 段与进深，
    //   用于区分「连通框形(每行都有 void)」vs「四角孤立洞(仅极端 Z 行有 void)」，
    //   从而决定遮板是填整框还是只填四角。绝不靠截图猜。
    if (Out.bFound)
    {
        constexpr float kRow = 15.f;
        int32 VoidRows = 0;
        for (float Z = 15.f; Z <= 415.f; Z += kRow)
        {
            float RowMinX = kBig, RowMaxX = -kBig, RowDepth = 0.f;
            bool bRowVoid = false;
            for (const FSample& S : Samples)
            {
                if (FMath::Abs(S.Z - Z) > kRow * 0.5f) continue;
                if (S.HitY < Out.WallY - kOpenDepth)
                {
                    bRowVoid = true;
                    RowMinX = FMath::Min(RowMinX, S.X);
                    RowMaxX = FMath::Max(RowMaxX, S.X);
                    RowDepth = FMath::Max(RowDepth, Out.WallY - S.HitY);
                }
            }
            if (bRowVoid)
            {
                ++VoidRows;
                WT_WARNF(TEXT("[portmap] Z=%3.0f voidX[%6.0f..%6.0f] depth=%4.0f"), Z, RowMinX, RowMaxX, RowDepth);
            }
        }
        WT_WARNF(TEXT("[portmap] 1.0.8CU summary wallY=%.0f portCenter=(%.0f,%.0f) rows=%d"),
                 Out.WallY, Out.CenterX, Out.CenterZ - 46.f, VoidRows);
    }

    WT_WARNF(TEXT("[portmeasure] wallY=%.0f (bin hits=%d/%d) opening pts=%d/%d "
                  "edges X[%.0f..%.0f] Z[%.0f..%.0f] center=(%.0f,%.0f) half=(%.0f,%.0f) found=%d"),
             Out.WallY, BestCount, Total, OpenPts, Total,
             MinX, MaxX, MinZ, MaxZ, Out.CenterX, Out.CenterZ, Out.HalfX, Out.HalfZ,
             Out.bFound ? 1 : 0);
    WT_WARNF(TEXT("[portmeasure] 1.0.8BR trace=component(LineTraceComponent vs vanilla mesh)"));
    return Out;
}






// ----------------------------------------------------------------------------
// 储能单元
// ----------------------------------------------------------------------------

AWTUpgradePowerStorage::AWTUpgradePowerStorage()
{
    // ★★★1.0.7 面板标题修复（同 AWTUpgradeStorage ctor 的详细说明）：纯 C++ 建筑必须在构造函数里
    //   写 AFGBuildable::mDisplayName，否则复用原版面板时标题栏是空的。
    mDisplayName = GetDisplayName();

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
    //   ★0.2.48 用户定案：基础**蓄电容量 400 → 1600** MWh（原版 100 的 16×）。
    //   充电速率（mPowerInputCapacity）见下方 2026-11-07 定案，改按里程碑档位固定上限（不再 400×等级倍率）。
    WT_SetFloatProperty(this, TEXT("mPowerStoreCapacity"), 1600.f);
    // 最大输入（充电）功率：2026-11-07 定案改为「按里程碑档位固定上限」，默认值取档位 0（Lv1~4）= 200 MW；
    // 实际值由 BeginPlay 的 ApplyResearchedCapacity 按当前研究档位经 WT_PowerStorageChargeCapMW 覆盖写入。
    WT_SetFloatProperty(this, TEXT("mPowerInputCapacity"), 200.f);

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

void AWTUpgradePowerStorage::ApplyResearchedCapacity()
{
    // 研究等级可能已变（先放后研究）。电力系统实时读 mPowerStoreCapacity / mPowerInputCapacity，
    // 运行时改写即生效 —— 解决「研究后空提升」。
    const int32 L = WT_GetTrackLevel(GetWorld(), EWTUpgradeTrack::PowerStorage);
    // ★2026-11-07 修「研究前依旧是 2000」：`WT_GetTrackLevel` 在**什么都没研究**时兜底返回 1
    //   （不是真的 Lv1），而 `WT_TrackScale` 又把 Level<1 钳成 1（1.25×）⇒ 未研究电池被算成
    //   1600×1.25 = 2000。电池建筑由 T3 里程碑解锁、与 MAM 等级轨是两套 ⇒「有建筑但没研究」
    //   是常态。这里显式判定本轨是否**真的研究过**：没研究 → 基础倍率 1.0（1600）；
    //   研究过才乘等级倍率（Lv1 = 1600×1.25 = 2000，与设计「前移一级」一致）。
    const bool bResearched = WT_IsTrackResearched(GetWorld(), EWTUpgradeTrack::PowerStorage);
    // ★2026-11-08 百位取整：实际储电量与展示名共用同一数据源 WT_PowerStorageMWh（已按百位四舍五入）。
    //   未研究 → 基础 1600 MWh；研究过 → 该等级取整后储电量。
    const float Cap = bResearched
        ? (float)WT_PowerStorageMWh(L, WT_GetFinalRoute(GetWorld()))
        : 1600.f;
    // ★2026-11-07 充电速率定案：充电上限改「按里程碑档位固定」(Lv1~4=200 … Lv17~20=5000 MW)，
    // 不再随等级 ×1.25 暴涨（Lv20≈34.7GW 任何电网都供不上）；储电量 Cap 仍 = 1600 × 等级倍率（已取整）。
    const float Inp = WT_PowerStorageChargeCapMW(L);
    WT_SetFloatProperty(this, TEXT("mPowerStoreCapacity"), Cap);
    WT_SetFloatProperty(this, TEXT("mPowerInputCapacity"), Inp);
    // ★诊断：未研究（bResearched=false）与 L>1 都打一条，便于验收「研究前=1600 / Lv1=2000」。
    if (L > 1 || !bResearched)
    {
        WT_WARNF(TEXT("[wt-upg-batt] %s Lv%d researched=%d -> mPowerStoreCapacity=%.0f MWh"),
                 *GetName(), L, bResearched ? 1 : 0, Cap);
    }
}

void AWTUpgradePowerStorage::BeginPlay()
{
    Super::BeginPlay();

    // 按当前研究等级套用电池容量（基础 1600 MWh / 400 MW × 等级倍率）。封装成 ApplyResearchedCapacity，
    // 使「先放后研究」时研究推进脉冲也能调它重新套用 —— 解决研究后空提升。
    ApplyResearchedCapacity();

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
    // ★★★1.0.7（用户 2026-10-27 截图对比「原版面板左上角有『位面存储站上传器』，我们只有图标」）：
    //   原版面板标题 / 瞄准提示 / 拆解提示读的都是 **AFGBuildable::mDisplayName**
    //   （FGBuildable.h:824，`UPROPERTY(BlueprintReadOnly, EditDefaultsOnly)`）。原版建筑在蓝图 CDO 里
    //   配了它；我们是纯 C++ 类，从没赋过值 ⇒ 空 FText ⇒ 复用同一块原版面板时标题栏只剩图标。
    //   在构造函数里赋一次 = 写 CDO 默认值，EditDefaultsOnly 属性会复制给每个实例，不必每实例重设。
    //   中文/英文都走 GetDisplayName() 里那条 WT_LOCTEXT，和建造菜单用的是同一个 key。
    mDisplayName = GetDisplayName();

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
        // ★1.0.7z 上传站改用自己的面板（用户 2026-10-27 定案：「我们的云端上传站也采用此UI即可，
        //   输出就不显示【更换/选择】」）。仍走上面这条 vanilla-depot 分支拿材质/端口，
        //   只把「按 E 打开哪个面板」换成 UWTItemSelectPanelWidget —— 它靠 mInteractObject
        //   的实际类型判断是上传站模式（不建行尾按钮列、标题换口径，见 GetIsUploaderStation）。
        WT_UseNativeInteractPanel(this, UWTItemSelectPanelWidget::StaticClass());

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

void AWTUpgradeStorage::ApplyResearchedCapacity()
{
    // 容量 = 本地缓冲格网。ApplyStorageGrid 把 mInventorySizeX/Y 设成当前研究等级对应的尺寸，
    // 但已放置建筑的库存是在 BeginPlay 按当时尺寸建的，改字段不会自动重建 —— 必须 Resize 现有库存。
    // 只扩不缩（研究只升不降），Resize 保留已有货物（同下载站自愈逻辑）。
    ApplyStorageGrid();
    if (UFGInventoryComponent* Inv = GetStorageInventory())
    {
        UFGInventoryComponent* Target = Inv;
        FString How;
        if (UFGInventoryComponent* Active = WT_ResolveActiveStorageInventory(this, How))
        {
            Target = Active;
        }
        const int32 Need = mInventorySizeX * mInventorySizeY;
        if (Target->GetSizeLinear() < Need)
        {
            Target->Resize(Need);
            WT_WARNF(TEXT("[upgrade] storage '%s' capacity grown to %dx%d=%d slots (was %d)."),
                     *GetName(), mInventorySizeX, mInventorySizeY, Need, Target->GetSizeLinear());
        }
    }
}

void AWTUpgradeStorage::BeginPlay()
{
    // ★★★1.0.7 顺序修复（用户 2026-10-27 定案「缓冲改成 1×1 起步」时一并处理）：
    //   `ApplyStorageGrid()` 必须排在 `Super::BeginPlay()` **之前** ——
    //   原版 `AFGBuildableStorage::BeginPlay()` 就是在那里面用 `mInventorySizeX/Y`
    //   把 `mStorageInventory` 建出来的（FGBuildableStorage.h:56）。放在 Super 之后只能改字段、
    //   改不动已经建好的库存 ⇒ 高研究等级也会退化成 CDO 那 1×1（旧注释把这当成"已知限制"，
    //   实际只要挪个顺序就好了）。★对**已存在的老存档建筑**仍然无效（库存随存档尺寸走，
    //   要吃到新网格得拆了重建），这条限制无法绕开。
    ApplyStorageGrid();
    Super::BeginPlay();
    WT_LogConnectionPorts(this, TEXT("ports-depot"));   // ★0.2.42 接口可见性取证

    // ★1.0.8BK 补上原版云端站中间那块动态发光雾面片（用户 2026-10-28「这里原版有个动态
    //   视觉效果，我们没有」）。四座云端站统一补，逻辑见 WT_EnsureGlowFogPlane。
    //   本类 = 固体上传站：口在 WT_VANILLA_PORT_LOC、朝 +Y；门洞约 2.5m 见方（原版面片 1:1 大小）。
    mGlowPlane = WT_EnsureGlowFogPlane(this, WT_VANILLA_PORT_LOC, FVector(0.f, 1.f, 0.f),
                                       /*TargetSize=*/250.f, /*Inset=*/8.f, /*bFlowOutward=*/false);

    // ★1.0.8（2026-10-31 用户修正）：箭头是**液体站专属**，固体站不加箭头。
    //   （WT_AddPortDirectionArrow 保留定义，供日后液体上传/下载站复用；此处不调用。）

    // ★1.0.6（「传送带输入不进去」修复）：ctor 里调过一次 SetInventory，但那是在**构造期**——
    //   此时 `GetStorageInventory()` 拿到的子对象尚未注册，而 SetInventory 是游戏侧的非 inline
    //   实现（BlueprintCallable），不同版本对「构造期传入未注册库存」的处理不一定可靠。
    //   实测症状正是：口在、方向对、传送带也连上了，但货死活进不来（= 连接组件的
    //   mConnectionInventory 为 null）。这里在 BeginPlay 再绑一次——幂等操作，
    //   已经绑好的情况下重复设置没有任何副作用。
    if (mConveyorIn)
    {
        mConveyorIn->SetInventory(GetStorageInventory());
        mConveyorIn->SetDirection(EFactoryConnectionDirection::FCD_INPUT);

        // 绑定结果必须能在 Shipping 日志里核对：dir 必须是 0(FCD_INPUT)，
        // bound 必须是 yes。若 bound=NO，说明 SetInventory 本身失败，
        // 那就要走「自建库存 + 每帧搬运」的兜底路线，不能再靠这个端口。
        WT_WARNF(TEXT("[upgrade] depot '%s' conveyor input rebind: dir=%d bound=%s (storageInv=%s)"),
                 *GetName(),
                 static_cast<int32>(mConveyorIn->GetDirection()),
                 mConveyorIn->GetInventory() ? TEXT("yes") : TEXT("NO"),
                 GetStorageInventory() ? TEXT("yes") : TEXT("NO"));
    }
    else
    {
        WT_ERRF(TEXT("[upgrade] depot '%s' has NO conveyor input component at BeginPlay."), *GetName());
    }

    // ★1.0.6 取证：注册由基类 BeginPlay 完成（我们仍是"合法云端节点"）。
    //   这里补一条**地基级**诊断：云端子系统能否拿到。拿不到 ⇒ 上传整条链都是空转，
    //   而这条链一旦断，症状恰好又是「货进去就没了」，必须能在 Shipping 日志里一眼看出来。
    AFGCentralStorageSubsystem* CloudSub = GetWorld() ? AFGCentralStorageSubsystem::Get(GetWorld()) : nullptr;
    WT_WARNF(TEXT("[upgrade] depot '%s' centralStorageSubsystem=%s centralStorageBuilt=%s"),
             *GetName(),
             CloudSub ? TEXT("ok") : TEXT("NULL"),
             (CloudSub && CloudSub->IsCentralStorageBuilt()) ? TEXT("yes") : TEXT("no"));

    int32 LedgerTotal = 0;
    for (const FItemAmount& IA : mLedger)
    {
        LedgerTotal += IA.Amount;
    }
    // 老存档迁移量必须可见（旧版把货黑洞在账本里，玩家要能看到还剩多少没迁出去）。
    // ★1.0.7：加 liveSlots —— 声明值(mInventorySizeX*Y)和**实际建出来的库存格数**可能不一致
    //   （取决于原版是否照我们在 Super 之前设的值建库）。实测就盯这一项：期望 1x1 时 liveSlots=1。
    const int32 LiveSlots = GetStorageInventory() ? GetStorageInventory()->GetSizeLinear() : -1;
    WT_WARNF(TEXT("[upgrade] depot '%s' grid %dx%d (%d slots, liveSlots=%d) legacyLedger=%d entries / %d items."),
             *GetName(), mInventorySizeX, mInventorySizeY, mInventorySizeX * mInventorySizeY, LiveSlots,
             mLedger.Num(), LedgerTotal);
}

void AWTUpgradeStorage::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // ★2026-11-02（用户截图反馈：拆除固体上传站后残留一块带外星纹理方块）：
    // WT_EnsureGlowFogPlane() 在 BeginPlay 经 NewObject 加的发光面片组件（WTCloudGlowPlane）
    // 实测拆除后会残留。EndPlay + BeginDestroy 双重兜底销毁；诊断日志见 WT_DestroyGlowComponents。
    WT_DestroyGlowComponents(this, mGlowPlane, TEXT("storage EndPlay"));
    Super::EndPlay(EndPlayReason);
}

void AWTUpgradeStorage::BeginDestroy()
{
    // ★2026-11-02：EndPlay 兜底——若拆解路径未触发 EndPlay，BeginDestroy 必然触发，确保光膜组件被销毁。
    WT_DestroyGlowComponents(this, mGlowPlane, TEXT("storage BeginDestroy"));
    Super::BeginDestroy();
}

void AWTUpgradeStorage::Dismantle_Implementation()
{
    // ★2026-11-02（用户截图：拆除过程中露原始外星纹理方块）：拆除走**独立拆除特效**
    //   （Dismantle_Implementation → NetMulticast_Dismantle → PlayDismantleEffect → UFGMaterialEffect_Build），
    //   会把建筑所有网格套上拆除材质；我们的运行时发光面片不参与建造/拆除材质交换，于是以原始
    //   外星纹理露出（=截图那块暗色细胞纹理方块）。在拆除真正开始（本函数入口）先销毁光膜，
    //   让随后的拆除特效不再把它纳入网格集合 ⇒ 拆除全程无残留。之后 EndPlay/BeginDestroy 再清一次（幂等）。
    WT_DestroyGlowComponents(this, mGlowPlane, TEXT("storage Dismantle"));
    Super::Dismantle_Implementation();
}

void AWTUpgradeStorage::Tick(float dt)
{
    WT_ZONE(TEXT("Upgrade.Storage.Tick"));   // 0.5s 一次：原版上传 + 自己的加速推进

    // ★1.0.6（消失 bug 修复）：**恢复**调 Super —— 原版 AFGCentralStorageContainer 的 Tick
    //   负责累积 mUploadTimer、跑 CanUploadItem/TryUploadItem、驱动面板的上传进度条。
    //   旧版这里刻意不调 Super（为了把货截进自己的账本），代价是货进了玩家看不见的地方。
    Super::Tick(dt);

    PumpBufferToCentralStorage();
}

void AWTUpgradeStorage::Factory_Tick(float dt)
{
    // ★★★1.0.7 崩溃修复（2026-10-27 实测 Crash 的根因，一手 callstack 见下）：
    //   **绝对不能在这里调 PumpBufferToCentralStorage()！**
    //
    //   原版 `AFGBuildableSubsystem::TickFactoryActors` 是用 `ParallelFor` **并行**回调各建筑的
    //   `Factory_Tick`（FGBuildableSubsystem.cpp:644 那个 ParallelForInternal），也就是跑在
    //   LowLevelTasks 的工作线程上、**不是游戏线程**。而
    //   `AFGCentralStorageSubsystem::UploadItemFromInventoryToCentralStorage()`
    //   （FGCentralStorageSubsystem.cpp:97）内部会立刻
    //     → `Client_ItemAmountUpdatedInCentralStorage()`（同文件 :321）
    //       → 广播 `mOnCentralStorageItemAmountUpdated` 多播委托
    //         → `AFGCharacterPlayer::OnCentralStorageItemAmountUpdated`
    //           → `UFGShoppingListComponent::UpdateShoppingList()`（FGShoppingListComponent.cpp:493）
    //             → `FTimerManager::FindTimer()` → `check(IsInGameThread())` ⇒ **引擎断言崩溃**
    //
    //   崩溃栈（2026-10-27 04:50 UECC-Windows-9EA759F0…，32 帧）：
    //     FRunnableThreadWin → LowLevelTasks::FScheduler::WorkerLoop
    //       → ParallelForImpl::ParallelForInternal
    //         → AFGBuildableSubsystem::TickFactoryActors()::<lambda_1>   [:644]
    //           → Remote_Factory.dll（本函数）→ UploadItemFromInventoryToCentralStorage [:97]
    //             → TimerManager.cpp:524  Assertion failed: IsInGameThread()
    //
    //   ⇒ 上传动作**只能**留在 `Tick()`（AActor 的 PrimaryActorTick = 游戏线程，
    //      本类构造函数已设 TickInterval=0.5s / bStartWithTickEnabled=true）。
    //      这里硬性拦住 `Super::Factory_Tick(dt)` + 自带守卫，别再往并行路径上加任何
    //      涉及「原版云端子系统 / 委托广播 / UI 状态」的调用。
    Super::Factory_Tick(dt);
}

void AWTUpgradeStorage::PumpBufferToCentralStorage()
{
    // ★★★1.0.7 线程守卫（崩溃修复的**第二道保险**，第一道见 Factory_Tick 顶部的完整崩溃栈）：
    //   本函数内部会调 `UploadItemFromInventoryToCentralStorage()`，原版实现会立刻广播
    //   `mOnCentralStorageItemAmountUpdated` ⇒ 购物车 `UpdateShoppingList()` ⇒ `FTimerManager::FindTimer()`
    //   ⇒ `check(IsInGameThread())`。只要不是在游戏线程调用，就必然是引擎断言崩溃（不是偶发）。
    //   调用点现在只剩 `Tick()`（游戏线程）；这里再加一道守卫，防止将来有人把它塞回
    //   Factory_Tick / 任何 ParallelFor 或异步任务里 —— 那种回归是「直接崩游戏」，代价太高。
    if (!IsInGameThread())
    {
        WT_WARNF_ONCE(TEXT("depot-pump-offgamethread"), this,
                TEXT("[upgrade] depot '%s' PumpBufferToCentralStorage() called OFF the game thread "
                     "-- skipped (vanilla upload broadcasts a delegate that asserts IsInGameThread)."),
                *GetName());
        return;
    }

    // ★1.0.6 内部节流（见头文件 mLastBufferPumpTime 的完整说明）。
    //   调用路径自 1.0.7 起**只剩 `Tick()`** 一条（`Factory_Tick` 是生产线程，已禁止调用）。
    //   TickInterval = 0.5s，所以阈值取 0.4s 而不是 0.5s：引擎的 Tick 间隔是浮点累加，
    //   经常出现「0.4999 < 0.5」⇒ 每隔一次 Tick 才跑一轮，上传速率白白减半。
    //   保留节流而不是删掉，是为了将来万一有第二处（游戏线程）调用点时仍能去重。
    if (const UWorld* W = GetWorld())
    {
        const double Now = static_cast<double>(W->GetTimeSeconds());
        if (mLastBufferPumpTime >= 0.0 && (Now - mLastBufferPumpTime) < 0.4)
        {
            return;
        }
        mLastBufferPumpTime = Now;
    }

    WT_ZONE(TEXT("Upgrade.Storage.PumpToPool"));

    UWorld* World = GetWorld();
    UFGInventoryComponent* Inv = GetStorageInventory();
    if (!World || !Inv)
    {
        return;
    }

    AFGCentralStorageSubsystem* Sub = AFGCentralStorageSubsystem::Get(World);
    if (!Sub)
    {
        // 拿不到云端子系统 = 上传链根本不存在，货只会堆在本地缓冲（不会再"消失"，但也上不了云）。
        // 一次性 Warn，避免每 0.5s 刷屏。
        WT_WARNF_ONCE(TEXT("depot-no-cloudsub"), this,
                TEXT("[upgrade] depot '%s' cannot reach AFGCentralStorageSubsystem — upload is a no-op."),
                *GetName());
        return;
    }

    // 遍历整格库存；格子会随研究阶段放到 10x10，规模化后这步有成本，值得单独计时。
    const int32 Size = Inv->GetSizeLinear();
    int32 Uploaded = 0;
    int32 Calls = 0;

    // ★1.0.8BI 按类累加本拍推进池子的量 —— 面板「正在上传 ↑X/分」要按物品行读，
    //   只有总数是算不出每一类的（与下载站 mPullSamples 带 Item 的理由完全相同）。
    TMap<UClass*, int32> MovedByClass;

    for (int32 i = 0; i < Size && Calls < WT_MAX_UPLOADS_PER_PUMP; ++i)
    {
        FInventoryStack S;
        if (!Inv->GetStackFromIndex(i, S) || !S.HasItems())
        {
            continue;
        }

        const TSubclassOf<UFGItemDescriptor> Cls = S.Item.GetItemClass();

        // ★同一格要**连续提交**：原版这个入口一次只消化一件（见 WT_UploadStackToPool 实测注释），
        //   若每格只提交一次，一格 500 个就得等 500 轮 ≈ 4 分钟，玩家会再次觉得「送不进去」。
        //   循环到「原版拒收 / 配额满 / 这一格空了」为止，总次数仍受 WT_MAX_UPLOADS_PER_PUMP 约束。
        while (Calls < WT_MAX_UPLOADS_PER_PUMP)
        {
            // 原版的「能不能收」判据（物品过滤 + 该类配额是否已满）。不收就留在缓冲里，
            // 绝不搬到别处 —— 这一条是「不丢料」的底线。
            if (!Sub->CanUploadInventoryItemToCentralStorage(S.Item))
            {
                break;
            }

            const int32 Before = Sub->GetNumItemsFromCentralStorage(Cls);
            ++Calls;
            if (!Sub->UploadItemFromInventoryToCentralStorage(Inv, i))
            {
                break;   // 原版拒收：这一格保持原样
            }

            const int32 Moved = FMath::Max(0, Sub->GetNumItemsFromCentralStorage(Cls) - Before);
            Uploaded += Moved;
            if (Moved > 0)
            {
                MovedByClass.FindOrAdd(Cls.Get()) += Moved;
            }

            // 原版说收了、池子却没涨 ⇒ 别再空转（避免刷日志 / 白扫整格）。
            if (Moved <= 0)
            {
                break;
            }

            // 这一格搬完了就换下一格。
            if (!Inv->GetStackFromIndex(i, S) || !S.HasItems())
            {
                break;
            }
        }
    }

    if (Uploaded > 0)
    {
        // ★低频但关键（玩家最关心的就是「东西到底进云了没有」），用 Warn 保证 Shipping 下可见：
        //   WT_LOGF 被 WT_LOG_VERBOSE=false 整段编译剔除，旧版正是因此看不到任何上传痕迹。
        WT_WARNF(TEXT("[upgrade] depot '%s' pushed %d items into vanilla central storage (%d calls)."),
                 *GetName(), Uploaded, Calls);

        // ★1.0.8BI 每类各记一个样本（一拍一次，不是每件一次 —— 见 RecordUploadSample 注释）。
        for (const TPair<UClass*, int32>& Pair : MovedByClass)
        {
            RecordUploadSample(Pair.Key, Pair.Value);
        }
    }
}

void AWTUpgradeStorage::RecordUploadSample(UClass* Item, int32 Amount)
{
    if (!Item || Amount <= 0)
    {
        return;
    }
    const UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }
    const float Now = World->GetTimeSeconds();

    // 淘汰窗口外的头部样本（数组按时间升序，过期的一定在最前面）。
    int32 NumStale = 0;
    while (NumStale < mUploadSamples.Num() && Now - mUploadSamples[NumStale].Time > WT_RATE_WINDOW)
    {
        ++NumStale;
    }
    if (NumStale > 0)
    {
        mUploadSamples.RemoveAt(0, NumStale);
    }
    mUploadSamples.Emplace(Now, Amount, Item);
}

float AWTUpgradeStorage::GetUploadRatePerMinute() const
{
    const UWorld* World = GetWorld();
    if (!World || mUploadSamples.Num() < 2)
    {
        return 0.f;
    }
    return WT_SampleRatePerMinute(mUploadSamples, World->GetTimeSeconds(),
                                  /*OnlyItem=*/nullptr, WT_RATE_WINDOW);
}

void AWTUpgradeStorage::GetItemRatesPerMinute(TMap<UClass*, float>& OutRates) const
{
    OutRates.Reset();
    const UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }
    WT_CollectItemRates(mUploadSamples, World->GetTimeSeconds(), WT_RATE_WINDOW, OutRates);
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
    // instance the level reflects what the player has researched.
    // ★1.0.7：调用时机改成「BeginPlay 里、Super::BeginPlay() 之前」（见 BeginPlay 的说明）——
    //   原版是在 Super::BeginPlay 里按这两个字段建库存的，晚一步就只是改了个没人读的字段。
    //   已存在的旧建筑仍按存档里那份库存走，改网格需要拆了重建（原版同样如此）。
    // ★2026-10-07 定案（见设计表 §6.1）：本地缓冲格网**不再是主维度**（主维度是云端配额）。
    // ★★★1.0.7 改口径（用户 2026-10-27 截图对比后定案「改成 1×1 起步（贴原版）」）：
    //   原版上传器（AFGCentralStorageContainer）的本地缓冲就是 **1 格**，面板据此把那一格
    //   画成大图标 + 数量角标。我们原来从 2x2 起步 ⇒ 同一个原版面板会画成 2x2 的小格子网格，
    //   物品挤在左上角、数量角标被缩没、还多出 3 个空格子 —— 截图里「我们的比原版多格子」就是它。
    //   现在：阶段 1~5 -> 1x1 / 2x2 / 3x3 / 4x4 / 5x5（1 → 25 格）。
    //   缓冲放得越大反而把物品堵在本地（云端才是主仓），所以这一维只做温和放大，起点与原版一致。
    const int32 L = (GetWorld() ? WT_GetTrackLevel(GetWorld(), EWTUpgradeTrack::Storage) : 1);
    const int32 Stage = WT_StageForLevel(L);
    const int32 Side = FMath::Clamp(Stage, 1, 10);
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
        // 账本历来只收固体（旧版 `DrainInventoryToLedger` 只搬 UFGInventoryComponent 里的货，
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
    // ★★★1.0.7 面板标题修复（同 AWTUpgradeStorage ctor 的详细说明）：纯 C++ 建筑必须自己写
    //   AFGBuildable::mDisplayName，否则复用原版面板时标题栏是空的。
    mDisplayName = GetDisplayName();

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
    // ★1.0.7 面板改为**自建输出选型面板**（用户定案「云端下载站的 UI 重构，采用新面板，
    //   全物品中选择需要输出的物品」）—— 不再复用原版云端面板：原版面板只能看池子，
    //   没法指定「我要它吐哪种货」，而下载站的核心诉求恰恰是这个。
    if (UClass* VanillaDepot = WT_FindVanillaClass(
            TEXT("/Game/FactoryGame/Buildable/Factory/CentralStorage/Build_CentralStorage.Build_CentralStorage_C"),
            TEXT("Build_CentralStorage_C"), AFGBuildable::StaticClass()))
    {
        VanillaDepot->AddToRoot();
        WT_CopyVanillaMeshMaterials(VanillaDepot, MeshComp);

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

    // ★1.0.7 面板：放在 vanilla-depot 分支**之外** —— 材质/端口依赖原版蓝图类，但面板不依赖，
    //   原版蓝图类万一改路径也不会把面板一起带下水。
    WT_UseNativeInteractPanel(this, UWTItemSelectPanelWidget::StaticClass());

    ApplyStorageGrid();

    WT_LOGF(TEXT("[upgrade] downloader mesh='%s' footprint=500x900 uu height=423 uu"),
            Spec.Mesh ? *Spec.Mesh->GetName() : TEXT("<cube fallback>"));
}

void AWTUpgradeCloudDownloader::ApplyResearchedCapacity()
{
    // 与上传站同一套：改 mInventorySizeX/Y 不会自动重建库存，必须 Resize 现有库存（只扩不缩，保留货物）。
    ApplyStorageGrid();
    if (UFGInventoryComponent* Inv = GetStorageInventory())
    {
        UFGInventoryComponent* Target = Inv;
        FString How;
        if (UFGInventoryComponent* Active = WT_ResolveActiveStorageInventory(this, How))
        {
            Target = Active;
        }
        const int32 Need = mInventorySizeX * mInventorySizeY;
        if (Target->GetSizeLinear() < Need)
        {
            Target->Resize(Need);
            WT_WARNF(TEXT("[upgrade] downloader '%s' capacity grown to %dx%d=%d slots (was %d)."),
                     *GetName(), mInventorySizeX, mInventorySizeY, Need, Target->GetSizeLinear());
        }
    }
}

void AWTUpgradeCloudDownloader::BeginPlay()
{
    // ★1.0.7 顺序修复（与上传站同款）：库存尺寸必须在 Super::BeginPlay() 之前定好，
    //   原版是在基类 BeginPlay 里按 mInventorySizeX/Y 建库存的 —— 详见 AWTUpgradeStorage::BeginPlay。
    ApplyStorageGrid();
    Super::BeginPlay();
    WT_LogConnectionPorts(this, TEXT("ports-downloader"));   // ★0.2.42 接口可见性取证

    // ★1.0.8BK 中间动态雾面片：本类 = 固体下载站（货从云端吐出去）⇒ 流动方向反向（flow=out）。
    mGlowPlane = WT_EnsureGlowFogPlane(this, WT_VANILLA_PORT_LOC, FVector(0.f, 1.f, 0.f),
                                       /*TargetSize=*/250.f, /*Inset=*/8.f, /*bFlowOutward=*/true);

    // ★1.0.8（2026-10-31 用户修正）：箭头是**液体站专属**，固体站不加箭头。
    //   （WT_AddPortDirectionArrow 保留定义，供日后液体上传/下载站复用；此处不调用。）

    // ★★★1.0.7l 既有建筑扩容 + 取「真正生效」的库存组件：
    //   ApplyStorageGrid() 只对**新建**建筑有效 —— 原版是在 AFGBuildableStorage::BeginPlay 里
    //   按 mInventorySizeX/Y 建库的。已经放在存档里的下载站就是 1 格（1.0.7 的回归），
    //   而 1 格意味着「格子里只要有任意物品，另一种货就永远 AddStack 失败」⇒ 永久死锁。
    //   另外官方注释明确要求不要直接用 mStorageInventory（见 WT_ResolveActiveStorageInventory
    //   的完整依据），所以这里也换成活跃库。
    //     · GetSizeLinear() 是 inline 读 mInventoryStacks，是唯一可信的格数来源；
    //     · Resize() 在 SML Private/FGInventoryComponent.cpp:54 是空桩，但**运行时链接的是
    //       游戏本体的真实实现**（桩只用于让 mod 编译通过）；
    //     · 只在「想要 > 实际」时调，天然幂等，对新建建筑是 no-op。
    FString InvHow;
    UFGInventoryComponent* StoreInv = WT_ResolveActiveStorageInventory(this, InvHow);
    const int32 WantedSlots = FMath::Max(4, mInventorySizeX * mInventorySizeY);
    const int32 CurrentSlots = StoreInv ? StoreInv->GetSizeLinear() : -1;
    if (StoreInv && WantedSlots > CurrentSlots)
    {
        StoreInv->Resize(WantedSlots);
    }
    // ★这条日志是「库存到底怎么解析的」的唯一证据：inv= 后面能直接看出是反射命中
    //   （`...Handler::GetActiveInventoryComponent`）还是兜底（`storage-inventory(fallback)`）。
    WT_WARNF(TEXT("[upgrade] downloader '%s' inventory resolved via '%s': %d -> %d slots"),
             *GetName(), *InvHow, CurrentSlots, StoreInv ? StoreInv->GetSizeLinear() : -1);

    // ★1.0.6 与上传站同款修复：ctor 期 SetInventory 未必可靠（见 AWTUpgradeStorage::BeginPlay），
    //   输出口在 BeginPlay 再绑一次——幂等，绑好了重复设置无副作用。
    if (mConveyorOut)
    {
        // ★1.0.7l 绑的是同一个活跃库（此前绑的是 mStorageInventory，可能是非活跃的底层库）。
        mConveyorOut->SetInventory(StoreInv);
        mConveyorOut->SetDirection(EFactoryConnectionDirection::FCD_OUTPUT);
        WT_WARNF(TEXT("[upgrade] downloader '%s' conveyor output rebind: dir=%d bound=%s (activeInv=%s)"),
                 *GetName(),
                 static_cast<int32>(mConveyorOut->GetDirection()),
                 mConveyorOut->GetInventory() ? TEXT("yes") : TEXT("NO"),
                 StoreInv ? TEXT("yes") : TEXT("NO"));
    }
}

void AWTUpgradeCloudDownloader::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // ★2026-11-02（用户截图反馈：拆除固体下载站后残留一块带外星纹理方块）：
    // 与上传站同款修复——EndPlay + BeginDestroy 双重兜底销毁 BeginPlay 运行时加的发光面片组件。
    WT_DestroyGlowComponents(this, mGlowPlane, TEXT("downloader EndPlay"));
    Super::EndPlay(EndPlayReason);
}

void AWTUpgradeCloudDownloader::BeginDestroy()
{
    // ★2026-11-02：EndPlay 兜底——若拆解路径未触发 EndPlay，BeginDestroy 必然触发，确保光膜组件被销毁。
    WT_DestroyGlowComponents(this, mGlowPlane, TEXT("downloader BeginDestroy"));
    Super::BeginDestroy();
}

void AWTUpgradeCloudDownloader::Dismantle_Implementation()
{
    // ★2026-11-02：与上传站同款——拆除特效会把运行时发光面片套上拆除材质露出原始外星纹理方块；
    //   拆除真正开始时先销毁光膜，让随后的拆除特效不再包含它。
    WT_DestroyGlowComponents(this, mGlowPlane, TEXT("downloader Dismantle"));
    Super::Dismantle_Implementation();
}

void AWTUpgradeCloudDownloader::ApplyStorageGrid()
{
    // 与上传站同一套阶段放大（见 AWTUpgradeStorage::ApplyStorageGrid 的定案注释）。
    //
    // ★★★1.0.7l 致命 bug 修复：「下载站选好物品也不出货」的真正根因是**缓冲只有 1 格**。
    //   1.0.7 时按上传站的「贴原版 1 格」定案一起改成了 `Clamp(Stage, 1, 10)` ⇒ 1x1。
    //   但那条定案只对**上传站**成立（它复用原版 AFGCentralStorageContainer 面板，原版面板按
    //   1 格画大图标 + 数量角标）。下载站用的是本 mod 自建面板（UWTItemSelectPanelWidget），
    //   根本没有「贴原版格子」这个约束 —— 套用过来只有害处：
    //     1 格缓冲下，只要那格里已经有任何物品（上一次搬进来的货、玩家手动放的、或者传送带
    //     出口堵着没抽走），`Inv->AddStack()` 对另一种物品就**必然返回 0** ⇒ TryPullFromCloud
    //     每次都「取到货 → 放不下 → 退回账本」⇒ 面板显示云端有货、机器却永远不出货。
    //   而且这个失败**完全不可见**：诊断用的 `GetInventoryStacks()` / `GetNumItems()` 在 SML
    //   里都是空桩（Private/FGInventoryComponent.cpp:79 `{ }` 与 :74 `return int32()`），
    //   打出来的 `buffer=0/1(has 0)` 是假象，把「格子其实已占满」伪装成「缓冲是空的」。
    //   ⇒ 下载站缓冲最小 2x2（4 格）：任何单格占用都不再能造成永久死锁。
    const int32 L = (GetWorld() ? WT_GetTrackLevel(GetWorld(), EWTUpgradeTrack::Storage) : 1);
    const int32 Stage = WT_StageForLevel(L);
    const int32 Side = FMath::Clamp(Stage, 2, 10);
    mInventorySizeX = Side;
    mInventorySizeY = Side;
    WT_WARNF(TEXT("[upgrade] downloader grid Lv%d (stage %d) -> %dx%d = %d slots"),
             L, Stage, Side, Side, Side * Side);
}

void AWTUpgradeCloudDownloader::Tick(float dt)
{
    WT_ZONE(TEXT("Upgrade.Downloader.Tick"));   // 每帧（TickInterval 未设 = 0），但额度不够时立刻 return
    Super::Tick(dt);

    // ★★★1.0.7o 用户定案（2026-10-27）：「把原版的上传速度搬过来，改成下载速度」。
    //
    //   旧节拍：`mPullInterval = 2s` 固定，每 2 秒搬**一组**（最多 100 件）
    //          ⇒ 稳态实测约 1200 件/分，而原版云端上传是 1 件/秒（60 件/分）——
    //            下载比上传快 20 倍，云端只进不出/只出不进两种极端都不对称。
    //   新节拍：按原版上传口径累积**额度**，每 T 秒才允许搬 1 件
    //          （T = AFGCentralStorageSubsystem::GetCentralStorageTimeToUpload()，含解锁倍率）。
    //
    //   额度制而不是「固定间隔搬 1 件」的理由：TickInterval 是 0.5 秒，而 T 可能小于 0.5 秒
    //   （解锁加速后），也可能远大于 0.5 秒。用额度累积能把任意 T 精确折算成「这一拍该搬几件」，
    //   不必改 Tick 频率，也不会出现「T=0.1 秒却只能搬 1 件/0.5 秒」的硬上限。
    const float SecondsPerItem = FMath::Max(0.01f, GetVanillaUploadSecondsPerItem());
    // 本拍应得的额度（件）。T=1s、TickInterval=0.5s ⇒ 0.5 件/拍 ⇒ 两拍搬 1 件 = 1 件/秒。
    const float PerTick = dt / SecondsPerItem;
    mPullCredit += PerTick;
    // 额度上限 = 两拍的份额（且不少于 1 件）。
    //   ★这是「不囤积」的关键：缓冲长时间堵塞时额度不会攒到几十件，解堵后也就不会一次性爆量
    //   ——否则玩家会看到「堵了 5 分钟，突然甩出 300 件」，既不像原版速度也会瞬时卡顿。
    //   同时它也保证了「一拍没搬成（Moved=0）⇒ 下一拍最多补 1~2 拍的量」，长期平均仍等于限速。
    const float CreditCap = FMath::Max(1.f, PerTick * 2.f);
    mPullCredit = FMath::Min(mPullCredit, CreditCap);
    if (mPullCredit < 1.f)
    {
        return;
    }

    const int32 Budget = FMath::FloorToInt(mPullCredit);
    const int32 Moved = TryPullFromCloud(Budget);
    if (Moved > 0)
    {
        // 只按**真的搬到的件数**扣额度：搬不满（缓冲空间不够）时额度自然被上面的 cap 压住，
        // 不会无限堆积，也不会因为「搬不满」而永久欠速。
        mPullCredit -= static_cast<float>(Moved);
    }

    // ★限速口径自证：只在口径变化（或每 60 秒）打一条，方便玩家/我核对「下载是不是真的按原版速度」。
    const UWorld* RateWorld = GetWorld();
    if (RateWorld)
    {
        const float NowRate = RateWorld->GetTimeSeconds();
        if (NowRate - mLastRateLimitLogTime >= 60.f)
        {
            mLastRateLimitLogTime = NowRate;
            WT_WARNF(TEXT("[downloader] '%s' rate limit: %.2fs/item (= %.0f items/min, from vanilla ")
                          TEXT("central-storage upload speed) credit=%.1f"),
                     *GetName(), SecondsPerItem, 60.f / SecondsPerItem, mPullCredit);
        }
    }
}

float AWTUpgradeCloudDownloader::GetVanillaUploadSecondsPerItem() const
{
    // ① 权威口径：子系统返回的**已经乘过解锁倍率**的秒数
    //    （UFGCentralStorageSettings 的 tooltip 明确写着 mDefaultTimeToUpload 会被
    //      AFGUnlockSubsystem::mUnlockedCentralStorageUploadSpeedMultiplier 影响）。
    //    SML 桩里它是 `return float()`（0），但运行时链接游戏本体真实现 —— 与
    //    GetSizeLinear()/AddStack() 同一类「桩是空的、运行时是真的」情况（本项目已踩过）。
    if (const UWorld* World = GetWorld())
    {
        if (AFGCentralStorageSubsystem* Sub = AFGCentralStorageSubsystem::Get(const_cast<UWorld*>(World)))
        {
            const float T = Sub->GetCentralStorageTimeToUpload();
            if (T > 0.f)
            {
                return T;
            }
        }
    }

    // ② 退化：读设置里的基础值（没有解锁倍率，但至少是个合理数）
    if (const UFGCentralStorageSettings* Settings = UFGCentralStorageSettings::Get())
    {
        if (Settings->mDefaultTimeToUpload > 0.f)
        {
            return Settings->mDefaultTimeToUpload;
        }
    }

    // ③ 最后兜底：1 件/秒（原版容器的 mTimeToUpload 默认值就是 1.f）。
    //    ★绝不返回 0 —— Tick 里会除它，0 会让额度瞬间爆到 64 件上限。
    return 1.f;
}

float AWTUpgradeCloudDownloader::GetDownloadRateLimitPerMinute() const
{
    return 60.f / FMath::Max(0.01f, GetVanillaUploadSecondsPerItem());
}

int32 AWTUpgradeCloudDownloader::TryPullFromCloud(int32 MaxItems)
{
    // ★1.0.7o `MaxItems` = 本拍由「原版上传速度」累积出的取货额度（≥1）。
    //   以前这里是「一周期一组」，现在必须**按额度**取，否则限速形同虚设。
    if (MaxItems <= 0)
    {
        return 0;
    }
    // 每次拉货都要 TActorIterator 遍历全部上传站（O(站数)），站多了这步会变贵。
    WT_ZONE_COARSE(TEXT("Upgrade.Downloader.TryPull"));
    UWorld* World = GetWorld();
    if (!World)
    {
        return 0;
    }

    UFGInventoryComponent* Inv = GetStorageInventory();
    if (!Inv)
    {
        return 0;
    }

    // ★★★1.0.7l 换成**真正生效**的库存组件（官方注释：别直接用 mStorageInventory）。
    FString InvHow;
    UFGInventoryComponent* ActiveInv = WT_ResolveActiveStorageInventory(this, InvHow);
    if (ActiveInv)
    {
        Inv = ActiveInv;
    }
    // ★自愈：格数不足当场扩容。BeginPlay 里那次 Resize 可能因存档加载顺序或反射失配没跑到，
    //   这里每周期兜一次 —— GetSizeLinear() 是 inline 真值，已经够大时 Resize 不会被调用。
    if (Inv->GetSizeLinear() < 4)
    {
        Inv->Resize(4);
        WT_WARNF(TEXT("[downloader] '%s' self-heal: buffer resized to %d slots (inv=%s)"),
                 *GetName(), Inv->GetSizeLinear(), *InvHow);
    }

    // ---- 第一优先：原版云端池（用户定案「先看原版云端是否修建+内容」）----
    //
    // ★★★1.0.7m 修复「面板显示云端剩 1,400 铁矿石，下载站一件都不搬」（2026-10-27 实测）。
    //
    //   旧代码把 `IsCentralStorageBuilt()` 当**硬门槛**。可它的实现是
    //   `return mCentralStorages.Num() > 0;`（SDK `Public/FGCentralStorageSubsystem.h:57`，
    //   头文件 inline）—— 读的是**上传器注册表**，不是货架。实测存档里原版池中确有
    //   1,400 铁矿石（面板「云端剩余」读 GetAllItemsFromCentralStorage 得到），而这张
    //   注册表是空的（cloudBuilt=0）⇒ 有货的池被整段跳过，诊断随即打出
    //   `ledger=0 taken=0`，看上去像「云端没货」，其实货一直躺在原版池里没人去取。
    //   ⇒ 判据改成**按池内容判定**：池里有这一类的存货就试搬一次。这与面板
    //     「云端剩余」完全同源（两边都只读 GetAllItemsFromCentralStorage），
    //     不会再出现「面板说 1,400、下载站认为 0」的口径分裂。
    //
    // 两层搬运，前一层成功就完全不会进下一层：
    //   ① MoveItemFromCentralStorage —— 原版标准路径（池扣数 + 自动选槽 + 广播通知）；
    //   ② GrabItemFromCentralStorage —— 指定目标格，绕开自动选槽（缓冲里有半格时更灵）。
    //   ★刻意**没有**「先扣后放」的第三层，理由见下面 ② 失败处那段注释（会真丢货）。
    int32 DiagCloudEntries    = 0;
    int32 DiagCloudContainers = -1;
    int32 DiagCloudTried      = 0;
    int32 DiagCloudGot        = 0;
    if (AFGCentralStorageSubsystem* Sub = AFGCentralStorageSubsystem::Get(World))
    {
        // 纯诊断：注册表大小（原版「是否已修建」就看这个数）。它**不参与**搬运判定。
        DiagCloudContainers = Sub->GetCentralStorageContainers().Num();

        TArray<FItemAmount> CloudItems;
        Sub->GetAllItemsFromCentralStorage(CloudItems);
        DiagCloudEntries = CloudItems.Num();
        // 每个周期只搬一种、最多一组 —— 温和搬运，不抢锁不清仓。
        for (const FItemAmount& IA : CloudItems)
        {
            if (IA.Amount <= 0 || IA.ItemClass.Get() == nullptr)
            {
                continue;
            }
            // ★1.0.7x 面板白名单：**必须显式选过**才搬（空名单 = 一件都不搬）。
            if (!IsOutputAllowed(IA.ItemClass))
            {
                continue;
            }
            const int32 StackSize = UFGItemDescriptor::GetStackSize(IA.ItemClass);
            FItemAmount Want;
            Want.ItemClass = IA.ItemClass;
            // ★1.0.7o 三重上限：池里的量 / 一组（StackSize）/ **本拍额度**（原版上传速度算出）。
            //   额度是最后加的，也是真正让「下载速度 = 上传速度」生效的那一层。
            Want.Amount = FMath::Min(IA.Amount, FMath::Min(FMath::Max(1, StackSize), MaxItems));
            ++DiagCloudTried;

            // ① 原版标准路径
            int32 Moved = UFGInventoryLibrary::MoveItemFromCentralStorage(Want, Inv);

            // ② 指定格（原版自动选槽失败时，往具体格子放往往能成）
            if (Moved <= 0)
            {
                const int32 Slots = Inv->GetSizeLinear();
                for (int32 Slot = 0; Slot < Slots && Moved <= 0; ++Slot)
                {
                    Moved = UFGInventoryLibrary::GrabItemFromCentralStorage(Want, Inv, Slot);
                }
            }

            // ★★★1.0.7m 深度自检抓到的坑：**这里不能有「先扣后放」的第三层**。
            //   原本写了 `TryRemoveItemsFromCentralStorage` + `AddStack` 兜底：先把它从池里
            //   扣出来，再往缓冲里塞，塞不下就把剩余报 Error。问题在于 —— 能走到这一步就
            //   说明 ① ② 都搬不进去，而 ②（GrabItemFromCentralStorage）本身就是「往指定格搬、
            //   放得下多少搬多少」，它返回 0 已经等于「缓冲此刻放不下这类货」。
            //   此时再先扣后放，扣出来的东西必然也塞不进去 ⇒ **每次死循环都真丢一组货**，
            //   比"搬不动"严重得多（1.0.6「塞进去的东西凭空消失」的下一个版本）。
            //   ⇒ 兜底层已删除。①② 都失败时保持池不动，由下面的 idle 诊断把真相打出来：
            //     `pool[entries>0 tried>0 got=0]` = 池里有货但缓冲放不下，去清缓冲/接传送带。

            DiagCloudGot = FMath::Max(DiagCloudGot, Moved);
            if (Moved > 0)
            {
                // ★1.0.7n 喂下载速度（只算真正进缓冲的件数）；1.0.7p 起带上物品类，
                //   面板行内「下载速度」列要按类取读数。
                RecordDownloadSample(Moved, IA.ItemClass.Get());
                // ★成功也要打：旧版是 WT_LOGF，被 WT_LOG_VERBOSE=false 整句编译剔除，
                //   于是「搬了」和「没搬」在 Shipping 日志里长得一样。限流 10 秒。
                const float NowLog = World->GetTimeSeconds();
                if (NowLog - mLastPullLogTime >= 10.f)
                {
                    mLastPullLogTime = NowLog;
                    WT_WARNF(TEXT("[downloader] '%s' pulled %d x %s from the vanilla cloud pool ")
                                  TEXT("(entries=%d containers=%d)."),
                             *GetName(), Moved, *Want.ItemClass->GetName(),
                             DiagCloudEntries, DiagCloudContainers);
                }
                return Moved;   // 本拍只搬一笔，额度由 Tick 按实际件数扣减
            }
        }
    }

    // ---- 第二优先：我们自己的云（各上传站的账本）----
    // 走到这里 = 原版云端没修建或这类货为 0 —— 正好是用户定的「才使用我们的云端」。
    //
    // ★★★1.0.7l 诊断累加器：只采集**真实函数的返回值**。
    //   旧诊断用的 `GetInventoryStacks()`（SML 桩 `{ }`）与 `GetNumItems()`（桩 `return 0`）
    //   恒为空/0 ⇒ 打出的 `buffer=0/1(has 0)` 是假象，把「缓冲其实已占满」显示成「缓冲空着」，
    //   直接导致上一轮误判成「云端没货」。现在看 TakeFromLedger / AddStack 的真实返回，
    //   一眼能分出「账本没货(taken=0)」与「缓冲放不下(added=0 且 taken>0)」。
    int32 DiagStations      = 0;
    int32 DiagLedgerEntries = 0;
    int32 DiagMatched       = 0;
    int32 DiagTaken         = 0;
    int32 DiagAdded         = 0;

    for (TActorIterator<AWTUpgradeStorage> It(World); It; ++It)
    {
        AWTUpgradeStorage* Station = *It;
        if (!Station)
        {
            continue;
        }
        ++DiagStations;
        for (const FItemAmount& IA : Station->GetLedger())
        {
            ++DiagLedgerEntries;
            UClass* Cls = IA.ItemClass.Get();
            if (IA.Amount <= 0 || !Cls)
            {
                continue;
            }
            // ★1.0.7 面板白名单（同原版池分支）。
            if (!IsOutputAllowed(IA.ItemClass))
            {
                continue;
            }
            ++DiagMatched;
            const int32 StackSize = UFGItemDescriptor::GetStackSize(IA.ItemClass);
            // ★1.0.7o 同原版池分支：额度也算进上限（下载速度 = 原版上传速度）。
            const int32 Want = FMath::Min(IA.Amount, FMath::Min(FMath::Max(1, StackSize), MaxItems));
            const int32 Taken = Station->TakeFromLedger(IA.ItemClass, Want);
            DiagTaken = FMath::Max(DiagTaken, Taken);
            if (Taken <= 0)
            {
                continue;
            }
            FInventoryStack S(Taken, IA.ItemClass);
            int32 Added = Inv->AddStack(S, true);

            // ★1.0.7l 二级投放：`AddStack()` 走的是组件自己的「自动选槽」逻辑，在
            //   「活跃库与底层库不是同一个组件」或组件内部状态异常时会**整体返回 0**，
            //   而这时逐格调用 `AddStackToIndex()` 往往还能成功（它绕开自动选槽）。
            //   两段式是纯增量优化：第一段成功就完全不会进这个分支。
            if (Added <= 0 && Taken > 0)
            {
                const int32 Slots = Inv->GetSizeLinear();
                for (int32 Slot = 0; Slot < Slots && Added < Taken; ++Slot)
                {
                    const int32 Put = Inv->AddStackToIndex(
                        Slot, FInventoryStack(Taken - Added, IA.ItemClass), /*allowPartialAdd=*/true,
                        /*sourceInventory=*/nullptr);
                    if (Put > 0)
                    {
                        Added += Put;
                    }
                }
            }

            DiagAdded = FMath::Max(DiagAdded, Added);
            if (Added < Taken)
            {
                Station->AddToLedger(IA.ItemClass, Taken - Added);   // 没放下的退回账本
            }
            if (Added > 0)
            {
                // ★1.0.7n 同原版池分支：只把真正进缓冲的量计入下载速度
                //   （`Added` 已扣掉「没放下又退回账本」的那部分）。
                RecordDownloadSample(Added, Cls);
                WT_LOGF(TEXT("[downloader] '%s' pulled %d x %s from our own cloud ledger."),
                        *GetName(), Added, *Cls->GetName());
                return Added;   // 本拍只搬一笔，额度由 Tick 按实际件数扣减
            }
        }
    }

    // ---- ★1.0.7l 诊断快照：走到这里 = 两路都没搬成 ----
    // 「面板选了但不下载」这只报障无法自证，因为真正的搬运成功日志是 WT_LOGF（被
    // WT_LOG_VERBOSE=false 编译剔除，Shipping 里不存在）。这里补一条**限流**的现状快照，
    // 只在白名单非空（= 玩家明确指定了物品）且连续 10 秒没搬动时打一次，开销可忽略。
    //   slots    —— 缓冲总格数。★只有 GetSizeLinear() 可信（:443 inline 读 mInventoryStacks）；
    //               GetInventoryStacks()/GetNumItems() 在 SML 里是空桩，读数全是假的，已弃用。
    // ✓ ★1.0.7m 口径修正：旧行的 `cloudBuilt=0` 被误读成「玩家没修云端」或「云端没货」，
    //   其实它只是注册表 mCentralStorages.Num()。现在拆成三段：
    //   pool[...] —— 原版云池：entries=池内条目数（== 面板「云端剩余」的来源）、
    //               containers=上传器注册表大小（built 的原义）、tried=本轮试搬次数、
    //               got=本轮实际搬出量（0 而 entries>0 ⇒ 三层搬运全失败，去查池/缓冲）、
    //               built=IsCentralStorageBuilt() 原值（保留以便对照，**不是**搬运判据）。
    //   ours[...] —— 我们自己的云：见下方 stations/ledger/matched/taken/added 说明。
    if (mOutputFilter.Num() > 0)
    {
        const float Now = World->GetTimeSeconds();
        if (Now - mLastIdleDiagTime >= 10.f)
        {
            mLastIdleDiagTime = Now;
            // ★不能写 const UClass*：TSubclassOf<UFGItemDescriptor> 的构造函数只吃非 const UClass*
            //   （实测 C2440「无法从 const UClass* 转换」）。诊断用不影响语义。
            UClass* First = mOutputFilter[0].Get();
            AFGCentralStorageSubsystem* DiagSub = AFGCentralStorageSubsystem::Get(World);
            WT_WARNF(TEXT("[downloader] '%s' idle: filter=%d first=%s budget=%d | slots=%d inv=%s | ")
                          TEXT("pool[entries=%d containers=%d tried=%d got=%d built=%d] | ")
                          TEXT("ours[stations=%d ledger=%d matched=%d taken=%d added=%d]"),
                     *GetName(), mOutputFilter.Num(),
                     First ? *First->GetName() : TEXT("<null>"), MaxItems,
                     Inv->GetSizeLinear(), *InvHow,
                     DiagCloudEntries, DiagCloudContainers, DiagCloudTried, DiagCloudGot,
                     (DiagSub && DiagSub->IsCentralStorageBuilt()) ? 1 : 0,
                     DiagStations, DiagLedgerEntries, DiagMatched, DiagTaken, DiagAdded);
        }
    }

    // 两路都没搬成：返回 0，Tick 据此「不囤额度」。
    return 0;
}

void AWTUpgradeCloudDownloader::RecordDownloadSample(int32 Amount, UClass* Item)
{
    // 只有真的进了缓冲的件数才算「下载速度」——「取到货但放不下」不是速度，是缓冲堵塞。
    if (Amount <= 0)
    {
        return;
    }
    const UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }
    const float Now = World->GetTimeSeconds();

    // 先淘汰窗口外的样本。样本数组按时间升序，过期的永远在头部 ⇒ 数出前缀长度批量删，
    // 不逐条 RemoveAt（每条都会搬动内存）。
    int32 NumStale = 0;
    while (NumStale < mPullSamples.Num() && Now - mPullSamples[NumStale].Time > WT_RATE_WINDOW)
    {
        ++NumStale;
    }
    if (NumStale > 0)
    {
        mPullSamples.RemoveAt(0, NumStale);
    }

    // ★1.0.7p 样本带物品类：面板行内「下载速度」列要按类读（整体读数仍只用 Amount）。
    mPullSamples.Emplace(Now, Amount, Item);
}

float AWTUpgradeCloudDownloader::WT_RatePerMinute(const TArray<FWT_PullSample>& Samples,
                                                  float Now, UClass* OnlyItem)
{
    // 窗口内、且（如指定了）属于这一类的样本下标。
    TArray<int32, TInlineAllocator<8>> Window;
    for (int32 i = 0; i < Samples.Num(); ++i)
    {
        if (Now - Samples[i].Time > AWTUpgradeCloudDownloader::WT_RATE_WINDOW)
        {
            continue;
        }
        if (OnlyItem && Samples[i].Item.Get() != OnlyItem)
        {
            continue;
        }
        Window.Add(i);
    }

    // 只有 1 个（或 0 个）样本时**算不出速率**：那一批货是「从无到有」攒的，
    // 不知道它代表了多长时间。返回 0 比瞎猜一个虚高值好。
    if (Window.Num() < 2)
    {
        return 0.f;
    }

    // ★口径（精确版）：**丢掉最老样本的量**，只算它之后的量 ÷ 它到最新样本的时间跨度。
    //
    //   为什么不用「窗口内总量 ÷ 窗口长度」：搬运是离散的，5 秒窗口里会落下多个样本
    //   ⇒ 分子变成 N 组货而分母是 5 秒 ⇒ 系统性高估（实测 3×40 = 120 件 / 5s = 1440/分，
    //   真值是 40 件/2s = 1200/分）。而「3 个样本」的真实含义是：最老那一组代表它**之前**
    //   的产量，不属于 [t-4, t] 这段。⇒ 分子只取之后几组，分母取时间跨度（离散积分近似）。
    int32 Sum = 0;
    for (int32 k = 1; k < Window.Num(); ++k)
    {
        Sum += Samples[Window[k]].Amount;
    }
    const float Span = Samples[Window.Last()].Time - Samples[Window[0]].Time;
    if (Sum <= 0 || Span <= 0.f)
    {
        return 0.f;
    }
    return static_cast<float>(Sum) * 60.f / Span;
}

float AWTUpgradeCloudDownloader::GetDownloadRatePerMinute() const
{
    const UWorld* World = GetWorld();
    if (!World || mPullSamples.Num() < 2)
    {
        return 0.f;
    }
    return WT_RatePerMinute(mPullSamples, World->GetTimeSeconds(), /*OnlyItem=*/nullptr);
}

void AWTUpgradeCloudDownloader::GetItemRatesPerMinute(TMap<UClass*, float>& OutRates) const
{
    OutRates.Reset();

    const UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }
    const float Now = World->GetTimeSeconds();

    // 只对「窗口内真的出现过」的类各算一次 —— 窗口里最多几个样本，
    // 所以这个循环最多跑几次，面板上百行查表即可，不会每行一次 O(样本)。
    TSet<UClass*> Seen;
    for (const FWT_PullSample& Sample : mPullSamples)
    {
        UClass* Cls = Sample.Item.Get();
        if (!Cls || Now - Sample.Time > WT_RATE_WINDOW)
        {
            continue;
        }
        bool bAlready = false;
        Seen.Add(Cls, &bAlready);
        if (bAlready)
        {
            continue;
        }
        const float Rate = WT_RatePerMinute(mPullSamples, Now, Cls);
        if (Rate > 0.f)
        {
            OutRates.Add(Cls, Rate);
        }
    }
}

void AWTUpgradeCloudDownloader::SetOutputFilter(const TArray<TSubclassOf<UFGItemDescriptor>>& InFilter)
{
    mOutputFilter.Reset();
    for (const TSubclassOf<UFGItemDescriptor>& C : InFilter)
    {
        // ★判空必须走 Get()：TSubclassOf 没有 operator bool（写了会 C2676）。
        if (C.Get())
        {
            mOutputFilter.AddUnique(C);
        }
    }

    // 按类名排 —— 同一份选择无论怎么点出来，存档字节都一致（diff / 比对友好）。
    mOutputFilter.Sort([](const TSubclassOf<UFGItemDescriptor>& A, const TSubclassOf<UFGItemDescriptor>& B)
    {
        const FString AN = A.Get() ? A->GetName() : FString();
        const FString BN = B.Get() ? B->GetName() : FString();
        return AN < BN;
    });

    // ★Warning 级：WT_LOGF 被 WT_LOG_VERBOSE=false 整句编译剔除，Shipping 日志里看不到；
    //   而「面板选了但机器不按选的搬」正是需要一眼可查的事。
    // ★1.0.7v 补上前 4 个类名：只报数量时，玩家报「无论选不选都下载」根本分不清是
    //   「名单为空（=搬全部）」还是「名单里残留多项（老存档多选）」，这两种的修法完全不同。
    {
        FString Names;
        for (int32 i = 0; i < mOutputFilter.Num() && i < 4; ++i)
        {
            if (i > 0)
            {
                Names += TEXT(", ");
            }
            Names += mOutputFilter[i].Get() ? mOutputFilter[i]->GetName() : TEXT("<null>");
        }
        if (mOutputFilter.Num() > 4)
        {
            Names += TEXT(", ...");
        }
        WT_WARNF(TEXT("[cloud-downloader] '%s' output filter <= %d item type(s) [%s]%s"),
                 *GetName(), mOutputFilter.Num(), *Names,
                 mOutputFilter.Num() == 0 ? TEXT(" (empty = pull NOTHING; pick an item in the panel)") : TEXT(""));
    }
}

void AWTUpgradeCloudDownloader::ToggleOutputItem(TSubclassOf<UFGItemDescriptor> ItemClass)
{
    if (!ItemClass.Get())
    {
        return;
    }

    // ★从当前白名单派生新名单再交给 SetOutputFilter —— 保证「增删 + 排序 + 日志」只有一套逻辑。
    TArray<TSubclassOf<UFGItemDescriptor>> Next = mOutputFilter;
    if (Next.Remove(ItemClass) == 0)
    {
        Next.Add(ItemClass);
    }
    SetOutputFilter(Next);
}

bool AWTUpgradeCloudDownloader::IsOutputAllowed(TSubclassOf<UFGItemDescriptor> ItemClass) const
{
    // ★★★1.0.7x 用户 2026-10-27 定案：「列表并未选择任何材料，但仍在下载」—— **未选择 = 一件都不搬**。
    //
    // 旧口径（空名单 = 不过滤 = 搬全部）是当初为「老存档不许静默停摆」加的向后兼容，
    // 但它和面板上「选择」这个动作的语义直接冲突：玩家看到一行都没高亮，机器却在搬货，
    // 第一反应是 bug（用户原话就是「无论选不选都下载」/「列表并未选择任何材料，但仍在下载」）。
    // 现在改成显式选择制：必须点行内【选择】指定物品才开搬；点【更换】放开 = 停止搬运。
    // 发现下载站不动时先看面板标题：显示「未选择（不会下载）」就是没选，不是故障。
    if (mOutputFilter.Num() == 0)
    {
        return false;
    }
    return mOutputFilter.Contains(ItemClass);
}

FText AWTUpgradeCloudDownloader::GetDisplayName()
{
    return WT_LOCTEXT("Upgrade_CloudDownloader", "Cloud Storage Download Station");
}

// ----------------------------------------------------------------------------
// 云端液态上传站 / 云端液态下载站（★0.2.36 新增，用户定案）
// ----------------------------------------------------------------------------


// ----------------------------------------------------------------------------
// 产能加速器
// ----------------------------------------------------------------------------





    

AWTUpgradeAccelerator::AWTUpgradeAccelerator()
{
    // ★★★1.0.7 面板标题修复：纯 C++ 建筑必须自己写 AFGBuildable::mDisplayName（详见 AWTUpgradeStorage ctor）。
    //   本类已退役（WT_FEATURE_* 相关），但旧存档里还可能有实例，名字照样得补。
    mDisplayName = GetDisplayName();

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

void AWTUpgradeAccelerator::ApplyResearchedCapacity()
{
    // 生产加速倍率：SetPendingProductionBoost 实时生效，运行时重设即可。
    const int32 L = WT_GetTrackLevel(GetWorld(), EWTUpgradeTrack::Accelerator);
    const float Boost = WT_AcceleratorBoostAtLevel(L, WT_GetFinalRoute(GetWorld()));
    mStandingBoost = Boost;   // 持久化解析值（存档/读档）
    SetPendingProductionBoost(Boost);
    WT_LOGF(TEXT("[upgrade] accelerator '%s' Lv%d applied standing boost x%.2f"),
            *GetName(), L, Boost);
}

void WT_RefreshResearchedUpgrades(UWorld* World)
{
    if (!World)
    {
        return;
    }
    int32 nB = 0, nS = 0, nD = 0, nA = 0;
    // ① 电池容量（PowerStorage）：mPowerStoreCapacity / mPowerInputCapacity 被电力系统实时读，改写即生效。
    for (TActorIterator<AWTUpgradePowerStorage> It(World); It; ++It)
    {
        AWTUpgradePowerStorage* B = *It;
        if (IsValid(B))
        {
            B->ApplyResearchedCapacity();
            ++nB;
        }
    }
    // ② 云端上传站 / ③ 下载站：容量 = 本地缓冲格网，已放置建筑须 Resize 现有库存（只扩不缩，保留货物）。
    for (TActorIterator<AWTUpgradeStorage> It(World); It; ++It)
    {
        AWTUpgradeStorage* S = *It;
        if (IsValid(S))
        {
            S->ApplyResearchedCapacity();
            ++nS;
        }
    }
    for (TActorIterator<AWTUpgradeCloudDownloader> It(World); It; ++It)
    {
        AWTUpgradeCloudDownloader* D = *It;
        if (IsValid(D))
        {
            D->ApplyResearchedCapacity();
            ++nD;
        }
    }
    // ④ 强化能量碎片（Accelerator）：生产加速倍率，运行时重设即生效。
    for (TActorIterator<AWTUpgradeAccelerator> It(World); It; ++It)
    {
        AWTUpgradeAccelerator* A = *It;
        if (IsValid(A))
        {
            A->ApplyResearchedCapacity();
            ++nA;
        }
    }
    // ★诊断（临时，确认刷新确实跑到且研究等级被正确读取）：只统计数量 + 当前电池轨等级。
    WT_WARNF(TEXT("[wt-refresh] called: batteries=%d storage=%d download=%d accel=%d | batteryTrackLv=%d"),
             nB, nS, nD, nA, WT_GetTrackLevel(World, EWTUpgradeTrack::PowerStorage));
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
    // 封装成 ApplyResearchedCapacity，使「先放后研究」时研究推进脉冲也能重设倍率。
    ApplyResearchedCapacity();
}

FText AWTUpgradeAccelerator::GetDisplayName()
{
    return WT_LOCTEXT("Upgrade_Accelerator", "Production Accelerator");
}

// ----------------------------------------------------------------------------
// ★1.0.6 恢复：云端液态上传/下载站（方孔处接原版缓冲罐的管道接口）
// ----------------------------------------------------------------------------

// 圆口视觉件：云端建模不动，把原版圆形管道口小件 SM_Pipe_InPut_01 叠在管道锚点外侧。
// 一手依据（HadronCollider 同款相对关系）：向外 10cm、Yaw = 连接Yaw − 90。
static void WT_AddRoundPortVisual(AActor* Self, const TCHAR* CompName, const UFGPipeConnectionComponent* Pipe,
                                   UStaticMeshComponent* SourceMesh = nullptr)
{
    if (!Self || !Pipe || !Self->GetRootComponent())
    {
        return;
    }
    UStaticMesh* PortMesh = LoadObject<UStaticMesh>(nullptr,
        TEXT("/Game/FactoryGame/Buildable/Factory/FactoryInputs/Mesh/SM_Pipe_InPut_01.SM_Pipe_InPut_01"));
    if (!PortMesh)
    {
        return; // 视觉件加载失败只影响观感，管道功能不受影响（构造期不刷日志）。
    }
    UStaticMeshComponent* Port = Self->CreateDefaultSubobject<UStaticMeshComponent>(CompName);
    if (!Port)
    {
        return;
    }
    Port->SetupAttachment(Self->GetRootComponent());
    Port->SetStaticMesh(PortMesh);
    Port->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    // ★1.0.8AU 白圈修复（用户实拍：蓝图投影里多一个白圈）：SM_Pipe_InPut_01 资产本身不带材质
    //   （白模），且全息预览不会给这个额外子组件套幽灵材质，于是预览/实机里都渲染成默认白。
    //   把已抄好原版仓库材质的主网格材质补到圆口件的空槽位上（原版自带材质若存在则保留），
    //   保证这个法兰圈永远不是白模。
    if (SourceMesh)
    {
        const int32 NumSlots = Port->GetNumMaterials();
        const int32 SrcSlots = SourceMesh->GetNumMaterials();
        for (int32 i = 0; i < NumSlots; ++i)
        {
            if (!Port->GetMaterial(i) && SrcSlots > 0)
            {
                if (UMaterialInterface* MI = SourceMesh->GetMaterial(FMath::Min(i, SrcSlots - 1)))
                {
                    Port->SetMaterial(i, MI);
                }
            }
        }
    }

    // 照抄一手相对关系：向外 40cm（★2026-11-08 用户定：10→20→25→30→35→再外推 5 = 40）、Yaw = 连接Yaw − 90。
    const FRotator ConnRot = Pipe->GetRelativeRotation();
    const float YawRad = FMath::DegreesToRadians(ConnRot.Yaw);
    const FVector Outward(FMath::Cos(YawRad), FMath::Sin(YawRad), 0.f);
    Port->SetRelativeLocation(Pipe->GetRelativeLocation() + Outward * 40.f);
    Port->SetRelativeRotation(FRotator(0.f, ConnRot.Yaw - 90.f, 0.f));
}

AWTUpgradeCloudFluidUploader::AWTUpgradeCloudFluidUploader()
{
    PrimaryActorTick.bCanEverTick = true;

    // 识别色（原生喷漆槽，槽4=青）：流体机冷色组。
    mColorSlot = 4;

    // 主体保持云端建模（SM_CentralStorage_01，与固态两站同款、同 180° 朝向），圆口视觉件另行叠加。
    static ConstructorHelpers::FObjectFinder<UStaticMesh> DepotMesh(
        TEXT("/Game/FactoryGame/Buildable/Factory/CentralStorage/Mesh/SM_CentralStorage_01.SM_CentralStorage_01"));

    WTMeshSpec Spec;
    Spec.Mesh = DepotMesh.Object;
    Spec.FootprintXY = 0.f;                                // 0 = 保持模型原始比例（原版尺寸）
    Spec.CubeFallbackScale = FVector(5.f, 9.f, 4.23f);     // 立方体兜底：500 x 900 x 423 uu
    UStaticMeshComponent* MeshComp = WT_CreateMainMesh(this, RootComponent, Spec);
    if (MeshComp)
    {
        MeshComp->SetRelativeRotation(FRotator(0.f, 180.f, 0.f));
    }

    WT_SetClearance(this, 250.f, 450.f, 423.f);

    // 管道接口 ×2（原版缓存罐 BeginPlay 索引第 2 个接口，1 个必崩）。类型 PCT_ANY，余量 1.5m。
    // 固定摆原版仓库口坐标 (0,390,100)/Yaw90（与固态两站一致，方向一致：从本地管网吸入进云端）。
    mPipeInA = CreateDefaultSubobject<UFGPipeConnectionComponent>(TEXT("WTFluidInA"));
    mPipeInA->SetupAttachment(RootComponent);
    mPipeInA->SetRelativeLocation(WT_FLUID_PORT_LOC);
    mPipeInA->SetRelativeRotation(WT_VANILLA_PORT_ROT);
    mPipeInA->SetPipeConnectionType(EPipeConnectionType::PCT_ANY);
    mPipeInA->SetConnectorClearance(150.f);

    mPipeInB = CreateDefaultSubobject<UFGPipeConnectionComponent>(TEXT("WTFluidInB"));
    mPipeInB->SetupAttachment(RootComponent);
    mPipeInB->SetRelativeLocation(WT_FLUID_PORT_LOC);
    mPipeInB->SetRelativeRotation(WT_VANILLA_PORT_ROT);
    mPipeInB->SetPipeConnectionType(EPipeConnectionType::PCT_ANY);
    mPipeInB->SetConnectorClearance(150.f);

    // 上传站：原版云端站 CDO 只用来抄材质（白模修复），面板不再复用原版位面存储站 UMG
    // （它绑定的是存储库存，上传站是管道缓冲罐、无该库存 → 复用会弹空/不弹）。
    // ★1.0.8AU 改用我们的原生只读状态面板（看云端液态云库存）。
    UClass* VanillaDepot = WT_FindVanillaClass(
        TEXT("/Game/FactoryGame/Buildable/Factory/CentralStorage/Build_CentralStorage.Build_CentralStorage_C"),
        TEXT("Build_CentralStorage_C"), AFGBuildable::StaticClass());
    if (VanillaDepot)
    {
        VanillaDepot->AddToRoot();
        WT_CopyVanillaMeshMaterials(VanillaDepot, MeshComp);
    }
    WT_UseNativeInteractPanel(this, UWTFluidStatusPanelWidget::StaticClass());

    // 圆口视觉件：管道锚点外侧叠原版圆形管道口小件。两根管道口同坐标，视觉件只需一个。
    // ★1.0.8AU 传入主网格（已抄原版仓库材质）用于给圆口件补材质，消除蓝图投影里的白圈。
    WT_AddRoundPortVisual(this, TEXT("WTRoundPortIn"), mPipeInA.Get(), MeshComp);
}

void AWTUpgradeCloudFluidUploader::BeginPlay()
{
    Super::BeginPlay();
    WT_LogConnectionPorts(this, TEXT("ports-fluid-up"));

    // ★1.0.8AU 与下载站同理：父类 AFGBuildablePipeReservoir 可能在 BeginPlay 复位交互状态，
    //   这里重挂我们的原生只读状态面板兜底（构造函数里那次可能被覆盖）。
    WT_UseNativeInteractPanel(this, UWTFluidStatusPanelWidget::StaticClass());

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
    // 流体记进我们自己的液态账本，不进原版池（用户定案「云端固液分开」）。
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

    // 识别色（原生喷漆槽，槽5=蓝）：流体机冷色组。
    mColorSlot = 5;

    // 主体保持云端建模（同上传站/固态两站）。
    static ConstructorHelpers::FObjectFinder<UStaticMesh> DepotMesh(
        TEXT("/Game/FactoryGame/Buildable/Factory/CentralStorage/Mesh/SM_CentralStorage_01.SM_CentralStorage_01"));

    WTMeshSpec Spec;
    Spec.Mesh = DepotMesh.Object;
    Spec.FootprintXY = 0.f;
    Spec.CubeFallbackScale = FVector(5.f, 9.f, 4.23f);
    UStaticMeshComponent* MeshComp = WT_CreateMainMesh(this, RootComponent, Spec);
    if (MeshComp)
    {
        MeshComp->SetRelativeRotation(FRotator(0.f, 180.f, 0.f));
    }

    WT_SetClearance(this, 250.f, 450.f, 423.f);

    // 双接口（原版缓存罐 BeginPlay 索引第 2 个接口，1 个必崩）。下载站 = 生产端（向管网供货）。
    // 类型 PCT_ANY、余量 1.5m。固定摆原版仓库口坐标 (0,390,100)/Yaw90，方向一致（从云端排进本地管网）。
    mPipeOutA = CreateDefaultSubobject<UFGPipeConnectionComponent>(TEXT("WTFluidOutA"));
    mPipeOutA->SetupAttachment(RootComponent);
    mPipeOutA->SetRelativeLocation(WT_FLUID_PORT_LOC);
    mPipeOutA->SetRelativeRotation(WT_VANILLA_PORT_ROT);
    mPipeOutA->SetPipeConnectionType(EPipeConnectionType::PCT_ANY);
    mPipeOutA->SetConnectorClearance(150.f);

    mPipeOutB = CreateDefaultSubobject<UFGPipeConnectionComponent>(TEXT("WTFluidOutB"));
    mPipeOutB->SetupAttachment(RootComponent);
    mPipeOutB->SetRelativeLocation(WT_FLUID_PORT_LOC);
    mPipeOutB->SetRelativeRotation(WT_VANILLA_PORT_ROT);
    mPipeOutB->SetPipeConnectionType(EPipeConnectionType::PCT_ANY);
    mPipeOutB->SetConnectorClearance(150.f);

    // 白模修复：同固态两站一样从原版云端站 CDO 抄材质。
    if (UClass* VanillaDepot = WT_FindVanillaClass(
            TEXT("/Game/FactoryGame/Buildable/Factory/CentralStorage/Build_CentralStorage.Build_CentralStorage_C"),
            TEXT("Build_CentralStorage_C"), AFGBuildable::StaticClass()))
    {
        VanillaDepot->AddToRoot();
        WT_CopyVanillaMeshMaterials(VanillaDepot, MeshComp);
    }

    // 圆口视觉件：管道锚点外侧叠原版圆形管道口小件。
    // ★1.0.8AU 传入主网格（已抄原版仓库材质）用于给圆口件补材质，消除蓝图投影里的白圈。
    WT_AddRoundPortVisual(this, TEXT("WTRoundPortOut"), mPipeOutA.Get(), MeshComp);

    // 面板：流体选型面板（我们自己的原生选型面板，枚举我们自己的液态云）。
    WT_UseNativeInteractPanel(this, UWTFluidSelectPanelWidget::StaticClass());
}

void AWTUpgradeCloudFluidDownloader::BeginPlay()
{
    Super::BeginPlay();
    WT_LogConnectionPorts(this, TEXT("ports-fluid-down"));

    // ★1.0.8AU 面板修复（用户实测按 E 弹不出流体选型面板）：AFGBuildablePipeReservoir 在 BeginPlay
    //   阶段可能把交互可用性 / 面板类复位。这里在 BeginPlay 里重挂我们的原生流体选型面板，确保
    //   mIsUseable + mInteractWidgetSoftClass 在任何父类初始化之后都生效（构造函数里的那次可能被覆盖）。
    WT_UseNativeInteractPanel(this, UWTFluidSelectPanelWidget::StaticClass());

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


    bool IsUpgradeAccelerator(const UClass* Class)
    {
        return Class && Class->IsChildOf(AWTUpgradeAccelerator::StaticClass());
    }

    bool IsUpgradeCloudFluidUploader(const UClass* Class)
    {
        return Class && Class->IsChildOf(AWTUpgradeCloudFluidUploader::StaticClass());
    }

    bool IsUpgradeCloudFluidDownloader(const UClass* Class)
    {
        return Class && Class->IsChildOf(AWTUpgradeCloudFluidDownloader::StaticClass());
    }

    bool IsUpgradeBuilding(const UClass* Class)
    {
        return IsUpgradePowerStorage(Class) || IsUpgradeStorage(Class)
            || IsUpgradeAccelerator(Class)
            || IsUpgradeCloudFluidUploader(Class)
            || IsUpgradeCloudFluidDownloader(Class);
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
                        if (!Raw)
                        {
                            continue;
                        }

                        // ★1.0.6（消失 bug 修复）：这里**不再**用「原版池里这一类为 0」做前置条件。
                        //   旧条件是「原版有就不动账本」，实际效果是：只要原版云里存过一块铁板，
                        //   之后所有铁板就永远锁死在账本里、面板看不见 ⇒ 玩家眼里的「凭空消失」。
                        //   现在无条件尝试迁移，收不收由原版自己判（配额满时返回 0，货原样退回账本）。
                        const int32 Take = Station->TakeFromLedger(Cls, 100);
                        if (Take > 0)
                        {
                            const int32 Injected = WT_UploadStackToPool(W, Raw, Take);
                            if (Injected < Take)
                            {
                                // 原版没收下的部分原样退回账本（防丢料）。
                                Station->AddToLedger(Cls, Take - FMath::Max(0, Injected));
                            }
                            WT_WARNF(TEXT("[cloud-fallback] migrated %d/%d x %s from '%s' ledger "
                                          "into vanilla central storage."),
                                     FMath::Max(0, Injected), Take, *Raw->GetName(), *Station->GetName());
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
