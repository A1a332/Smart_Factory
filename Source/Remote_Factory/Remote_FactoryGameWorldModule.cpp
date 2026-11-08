#include "Remote_FactoryGameWorldModule.h"

#include "Remote_FactoryLog.h"
#include "Remote_FactoryCrashTrace.h"
#include "Remote_FactoryVersion.h"   // ★1.0.26 服务器闸门日志用 REMOTE_FACTORY_VERSION_TEXT
// ★1.0.26 专用服务器禁用闸门：IsRunningDedicatedServer()（CoreMisc.h:152）。
#include "Misc/CoreMisc.h"
#include "Remote_FactoryRCO.h"
#include "Remote_FactoryRegistry.h"
#include "Remote_FactoryUpgradeRegistry.h"
#include "Remote_FactoryUpgradeResearch.h"
#include "Remote_FactoryResearchTree.h"
#include "Remote_FactoryUpgrades.h"   // ★2026-11-xx WT_RefreshResearchedUpgrades（研究后让已放置升级建筑实际扩容）
#include "FGResearchManager.h"
#include "Patching/NativeHookManager.h"   // ★2026-11-06 SUBSCRIBE_METHOD / TCallScope（IsTreeFullyResearched 崩溃闸门）
#include "FGSchematicManager.h"
#include "FGResearchTree.h"
#include "Remote_FactorySharedSession.h"
#include "Remote_FactoryResourceNodes.h"
#include "Remote_FactoryCloudQuota.h"   // ★1.0.7 云端配额口径（初始倍数 + MAM 等级系数）
#include "Remote_FactorySmartWarehouse.h"   // ★智能仓储：建造/换带自动从原版容器补足差额
#include "Remote_FactoryMilestoneCargoReturn.h"   // ★2026-11-02 里程碑货舱即刻返回开关（LoadMilestoneCargoReturnConfig）
#include "Remote_FactoryWarehousePanel.h"            // ★2026-11-03 智能仓储右侧常驻列表面板
#include "Remote_FactorySave.h"                     // ★2026-11-03 按存档持久化（智能仓储/里程碑货舱开关）
#include "Remote_FactoryCostSlotTint.h"      // ★2026-11-02 成本格蓝色染色（智能仓储来源指示）
#include "FGGameMode.h"
// ★1.0.7 云端初始容量 10 倍：改 FGCentralStorageSettings 的 CDO 并让子系统重算生效值。
#include "FGCentralStorageSettings.h"
#include "FGCentralStorageSubsystem.h"
#include "FGUnlockSubsystem.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/UnrealType.h"
#include "UObject/EnumProperty.h"        // ★2026-11-05 [ms-dump] 读 ESchematicType mType：FEnumProperty 是独立头，
                                          //   UnrealType.h 不含它（已核 UnrealType.h 的 include 列表）
// ★1.0.7x 超频上限 400%：改 AFGBuildableFactory::mMaxPotential（protected，反射读写）+ 巡检世界实例。
#include "Buildables/FGBuildableFactory.h"
#include "Resources/FGPowerShardDescriptor.h"
#include "FGInventoryComponent.h"        // ★1.0.7z 碎片槽探针：GetNumItems()（FGInventoryComponent.h:415）
#include "EngineUtils.h"
#include "UObject/UObjectIterator.h"   // TObjectIterator（遍历全部 UClass 的 CDO）
#include "Math/NumericLimits.h"   // TNumericLimits<float>::Max()（最低值统计）
#include "Blueprint/UserWidget.h"        // ★1.0.8 原版超频面板补丁：遍历在屏的 UMG 实例
#include "Blueprint/WidgetTree.h"        // GetAllWidgets（WidgetTree.h:61）
#include "Blueprint/WidgetBlueprintLibrary.h"  // ★2026-11-02 GetAllWidgetsOfClass（wt.DumpBuildMenu 枚举在屏 UMG）
#include "Components/TextBlock.h"        // 刻度文字 150%/200%/250% → 200%/300%/400%
#include "Components/ProgressBar.h"      // 滑条轨道底色（GetWidgetStyle/SetWidgetStyle）
#include "Styling/SlateTypes.h"          // FProgressBarStyle.BackgroundImage.TintColor（SlateTypes.h，不是 ProgressBarStyle.h）
#include "Components/Image.h"            // 槽位图标（brush 资源名 dump / 后续精确替换）
#include "Components/OverlaySlot.h"      // ★1.0.8T 成本格 "1/1" 文字要横跨橙条（HAlign_Fill）
#include "Components/Overlay.h"        // ★1.0.8V 取回按钮的宿主判定（UOverlay 默认 slot 即 Fill）
#include "Components/Border.h"           // ★1.0.8T 格底色 dump（原版 214/232 两档的宿主控件）
#include "Styling/SlateBrush.h"          // FSlateBrush::GetResourceObject / ImageSize
// ★1.0.8d 实测：原版超频面板里**没有 UProgressBar**（日志 `bar=none`）。滑条 = `Image mProgressBar`
//   （画刷是动态材质 MID_MM_UI_OverclockingSlider，橙/灰两段都由材质自己画）+ `Slider MainSlider`。
//   所以要改"未填充区颜色"只能改材质参数，改不到 UProgressBar 的 WidgetStyle。
#include "Components/Slider.h"           // USlider（MainSlider，真实手柄）
#include "Components/EditableText.h"    // mPercentText：面板自己显示的百分比（要跟我们对齐）
#include "Components/CanvasPanel.h"      // 垫在滑条下方的纯黑底（CanvasPanel_0）
#include "Components/CanvasPanelSlot.h"  // 复制 mProgressBar 的 FAnchorData 给黑底
#include "Materials/MaterialInterface.h" // GetAllVectorParameterInfo / GetVectorParameterValue
#include "Materials/MaterialInstance.h"  // ★1.0.8X 槽位图标材质参数 dump（Parent / *ParameterValues）
#include "Materials/MaterialInstanceDynamic.h"  // K2_GetVectorParameterValue
#include "UI/FGInteractWidget.h"         // ★面板 → 机器：反射读 mInteractObject（判"槽里是不是我们的碎片"）
#include "Remote_FactoryShardTakeButton.h"  // ★1.0.8V 碎片取回按钮（原版按写死的原版碎片类取出，我们的取不动）
#include "Resources/FGItemDescriptor.h"   // ★1.0.8AA UFGItemDescriptor::GetSmallIcon（图标 SlotIcon 参数兜底）
#include "Engine/Texture2D.h"             // ★1.0.8AA SetTextureParameterValue 需要 UTexture2D 的完整类型

// The LogRemote_Factory category is defined in Remote_FactoryModule.cpp (the module's own
// translation unit) so it is live before any other object in the mod is constructed.

// ----------------------------------------------------------------------------
// ★v0.2.25 里程碑字段 dump（HUB 里卡片在、点开既无费用也无奖励的取证）。
//
// 背景：0.2.24 已经给里程碑写上了图标（日志 [icon] ... OK），用户实测 HUB 详情区**依然空白** ——
// 说明图标不是（唯一）原因，我此前的判断不完整。与其继续猜，不如把里程碑上跟 HUB 显示有关的字段
// 全部导出来，并且**同时导一个原版里程碑做对照**，一次进游戏就能 diff 出差在哪个字段。
//
// 不用 ExportText（宏/签名随版本变），改为按字段类型精确读值。
// ----------------------------------------------------------------------------
static void WT_DumpMilestone(UClass* Cls, const TCHAR* Tag)
{
    if (!Cls)
    {
        return;
    }
    UObject* CDO = Cls->GetDefaultObject();
    if (!CDO)
    {
        return;
    }

    int32 CostNum = -1;
    if (FArrayProperty* AP = CastField<FArrayProperty>(Cls->FindPropertyByName(TEXT("mCost"))))
    {
        FScriptArrayHelper H(AP, AP->ContainerPtrToValuePtr<void>(CDO));
        CostNum = H.Num();
    }

    int32 UnlockNum = -1;
    FString UnlockList;
    if (FArrayProperty* AP = CastField<FArrayProperty>(Cls->FindPropertyByName(TEXT("mUnlocks"))))
    {
        FScriptArrayHelper H(AP, AP->ContainerPtrToValuePtr<void>(CDO));
        UnlockNum = H.Num();
        FObjectProperty* Inner = CastField<FObjectProperty>(AP->Inner);
        for (int32 i = 0; i < UnlockNum && Inner; ++i)
        {
            UObject* U = Inner->GetObjectPropertyValue(H.GetRawPtr(i));
            UnlockList += FString::Printf(TEXT("[%s]"), U ? *U->GetClass()->GetName() : TEXT("null"));
        }
    }

    FString CatName = TEXT("?");
    if (FObjectProperty* OP = CastField<FObjectProperty>(Cls->FindPropertyByName(TEXT("mSchematicCategory"))))
    {
        UObject* C = OP->GetObjectPropertyValue(OP->ContainerPtrToValuePtr<void>(CDO));
        CatName = C ? C->GetName() : TEXT("NULL");
    }

    FString SmallIcon = TEXT("?");
    if (FObjectProperty* OP = CastField<FObjectProperty>(Cls->FindPropertyByName(TEXT("mSmallSchematicIcon"))))
    {
        UObject* V = OP->GetObjectPropertyValue(OP->ContainerPtrToValuePtr<void>(CDO));
        SmallIcon = V ? V->GetName() : TEXT("NULL");
    }

    int64 Tier = -1;
    if (FNumericProperty* NP = CastField<FNumericProperty>(Cls->FindPropertyByName(TEXT("mTechTier"))))
    {
        if (NP->IsInteger())
        {
            Tier = NP->GetSignedIntPropertyValue(NP->ContainerPtrToValuePtr<void>(CDO));
        }
    }

    int64 TypeVal = -1;
    if (FProperty* TypeProp = Cls->FindPropertyByName(TEXT("mType")))
    {
        // ★★★2026-11-05 修：`ESchematicType mType` 是**普通 UENUM**（FGSchematic.h:201-202），
        //   反射出来是 **FEnumProperty**，它继承 FProperty 而**不是** FNumericProperty
        //   （EnumProperty.h:28 `class FEnumProperty : public FProperty`）⇒ 旧代码
        //   `CastField<FNumericProperty>` 恒为 nullptr ⇒ 这条 dump 永远打 `type=-1`，
        //   让我们一直以为里程碑的 mType 没设置。改成先按 FEnumProperty 取底层整型。
        if (FEnumProperty* EP = CastField<FEnumProperty>(TypeProp))
        {
            if (FNumericProperty* Under = EP->GetUnderlyingProperty())
            {
                TypeVal = Under->GetUnsignedIntPropertyValue(EP->ContainerPtrToValuePtr<void>(CDO));
            }
        }
        else if (FNumericProperty* NP = CastField<FNumericProperty>(TypeProp))
        {
            if (NP->IsInteger())
            {
                TypeVal = NP->GetSignedIntPropertyValue(NP->ContainerPtrToValuePtr<void>(CDO));
            }
        }
    }

    WT_WARNF(TEXT("[ms-dump] %s cls=%s type=%d tier=%d cost=%d unlocks=%d%s cat=%s smallIcon=%s"),
             Tag, *Cls->GetName(), (int32)TypeVal, (int32)Tier, CostNum, UnlockNum, *UnlockList,
             *CatName, *SmallIcon);
}

// ----------------------------------------------------------------------------
// ★v0.2.29/30 MAM 节点数据 A/B dump。
//
// 0.2.29 实锤（[wt-nd] VANILLA ... Schematic=<prop missing>）：MAMTree_NodeData_Struct 是
// **UserDefinedStruct**，成员 FName 带 GUID 后缀（pak 名称表里的 `Schematic_27_...` 就是本名），
// 按精确名找不到 —— 必须**前缀匹配**。这也正是 withSchematic=0 → 树被标"已完究"→ MAM
// 图区空白的直接原因（树里一个"可研究节点"都没有）。
// ----------------------------------------------------------------------------
static FProperty* WT_FindNodeMember(UStruct* Scope, const TCHAR* BaseName)
{
    for (TFieldIterator<FProperty> It(Scope); It; ++It)
    {
        if (It->GetName().StartsWith(BaseName))
        {
            return *It;
        }
    }
    return nullptr;
}

/**
 * ★★★2026-11-05 决定性发现：Parents / NodesToUnhide / UnhiddenBy 三个数组的元素类型
 * 是 **IntVector2D（一个整数格点 X,Y）**，不是"节点对象引用"。
 *   · `[wt-link] elemStruct=IntVector2D members: X_2_... (IntProperty) Y_4_... (IntProperty)`
 *   · `ChildrenAndRoads` = Map<IntVector2D, MAMTree_RoadPoints>（键也是格点）
 * ⇒ MAM 的整张节点图是**按格点坐标索引**的：两个节点写同一个坐标，在游戏眼里就是同一个点。
 *   这也解释了"一个方块放 4 个节点"为什么必然失败（详见 Remote_FactoryResearchTree.cpp 里
 *   坐标唯一化的长注释）：4 个节点共坐标 ⇒ 图上只剩一个点 ⇒ 上层节点把自己挡住（自环）。
 *
 * 本函数把数组里每个元素的**实际 (X,Y)** 打出来，用于对照原版布局。
 */
static FString WT_PointListText(FProperty* Inner, FScriptArrayHelper& H, int32 MaxShow)
{
    if (!Inner)
    {
        return FString();
    }
    FStructProperty* SP = CastField<FStructProperty>(Inner);
    if (!SP)
    {
        return FString();
    }
    FProperty* XP = WT_FindNodeMember(SP->Struct, TEXT("X"));
    FProperty* YP = WT_FindNodeMember(SP->Struct, TEXT("Y"));
    FNumericProperty* XN = CastField<FNumericProperty>(XP);
    FNumericProperty* YN = CastField<FNumericProperty>(YP);
    if (!XN || !YN)
    {
        return FString();
    }

    FString Out;
    for (int32 Ei = 0; Ei < H.Num() && Ei < MaxShow; ++Ei)
    {
        void* Elem = H.GetRawPtr(Ei);
        const int32 X = static_cast<int32>(XN->GetSignedIntPropertyValue(XP->ContainerPtrToValuePtr<void>(Elem)));
        const int32 Y = static_cast<int32>(YN->GetSignedIntPropertyValue(YP->ContainerPtrToValuePtr<void>(Elem)));
        Out += FString::Printf(TEXT("(%d,%d)"), X, Y);
    }
    if (H.Num() > MaxShow)
    {
        Out += FString::Printf(TEXT("+%d"), H.Num() - MaxShow);
    }
    return Out;
}

/** 读节点的格点坐标（Coordinates.X/Y）；取不到返回 false。 */
static bool WT_NodeCoord(UFGResearchTreeNode* Node, int32& OutX, int32& OutY)
{
    if (!Node)
    {
        return false;
    }
    void*    Data  = Node;
    UStruct* Scope = Node->GetClass();
    if (FStructProperty* NodeDataProp = FindFProperty<FStructProperty>(Node->GetClass(), TEXT("mNodeDataStruct")))
    {
        Data  = NodeDataProp->ContainerPtrToValuePtr<void>(Node);
        Scope = NodeDataProp->Struct;
    }
    FStructProperty* CoordProp = CastField<FStructProperty>(WT_FindNodeMember(Scope, TEXT("Coordinates")));
    if (!CoordProp)
    {
        return false;
    }
    void* CoordData = CoordProp->ContainerPtrToValuePtr<void>(Data);
    FNumericProperty* XP = CastField<FNumericProperty>(WT_FindNodeMember(CoordProp->Struct, TEXT("X")));
    FNumericProperty* YP = CastField<FNumericProperty>(WT_FindNodeMember(CoordProp->Struct, TEXT("Y")));
    if (!XP || !YP)
    {
        return false;
    }
    OutX = static_cast<int32>(XP->GetSignedIntPropertyValue(XP->ContainerPtrToValuePtr<void>(CoordData)));
    OutY = static_cast<int32>(YP->GetSignedIntPropertyValue(YP->ContainerPtrToValuePtr<void>(CoordData)));
    return true;
}

static void WT_DumpNodeData(UFGResearchTreeNode* Node, const TCHAR* Tag)
{
    if (!Node)
    {
        WT_WARNF(TEXT("[wt-nd] %s: <null>"), Tag);
        return;
    }
    UClass* Cls = Node->GetClass();

    void* Data = Node;
    UStruct* Scope = Cls;
    if (FStructProperty* NodeDataProp = FindFProperty<FStructProperty>(Cls, TEXT("mNodeDataStruct")))
    {
        Data = NodeDataProp->ContainerPtrToValuePtr<void>(Node);
        Scope = NodeDataProp->Struct;
    }

    // ★v0.2.31 一次性 dump 节点数据结构体的**完整反射表（真名 + 类型）**。
    // 起因：0.2.30 实测 Parents / ChildrenAndRoads / NodesToUnhide 前缀匹配**仍然查不到**
    // （连原版节点也一样）—— 说明从 pak 名称表"相邻字符串"推断类型/名字不可靠。
    // 按铁律"猜属性名必错"：一次进游戏把真名全打出来，别再猜。
    static bool GWT_NodeDataScopeDumped = false;
    if (!GWT_NodeDataScopeDumped)
    {
        GWT_NodeDataScopeDumped = true;
        int32 MemberCount = 0;
        for (TFieldIterator<FProperty> It(Scope); It; ++It) { ++MemberCount; }
        WT_WARNF(TEXT("[wt-nd-prop] scope=%s members=%d"), *Scope->GetName(), MemberCount);
        for (TFieldIterator<FProperty> It(Scope); It; ++It)
        {
            WT_WARNF(TEXT("[wt-nd-prop]   %s (%s)"), *It->GetName(), *It->GetClass()->GetName());
        }
    }

    FString S;
    if (FClassProperty* P = CastField<FClassProperty>(WT_FindNodeMember(Scope, TEXT("Schematic"))))
    {
        UClass* V = Cast<UClass>(P->GetObjectPropertyValue_InContainer(Data));
        S += FString::Printf(TEXT("Schematic=%s "), V ? *V->GetName() : TEXT("<null>"));
    }
    else
    {
        S += TEXT("Schematic=<prop missing> ");
    }

    if (FStructProperty* P = CastField<FStructProperty>(WT_FindNodeMember(Scope, TEXT("Coordinates"))))
    {
        void* CoordData = P->ContainerPtrToValuePtr<void>(Data);
        int32 X = INDEX_NONE;
        int32 Y = INDEX_NONE;
        if (FIntProperty* XP = CastField<FIntProperty>(WT_FindNodeMember(P->Struct, TEXT("X"))))
        {
            X = *XP->ContainerPtrToValuePtr<int32>(CoordData);
        }
        if (FIntProperty* YP = CastField<FIntProperty>(WT_FindNodeMember(P->Struct, TEXT("Y"))))
        {
            Y = *YP->ContainerPtrToValuePtr<int32>(CoordData);
        }
        S += FString::Printf(TEXT("Coords=(%d,%d) "), X, Y);
    }
    else
    {
        S += TEXT("Coords=<prop missing> ");
    }

    // ---- 内层类型一次性 dump（每个属性只打一次，避免刷屏） ----
    auto DumpInnerOnce = [](const TCHAR* Tag, FProperty* Inner, FProperty* Value)
    {
        static TSet<FString> Dumped;
        const FString Key(Tag);
        if (Dumped.Contains(Key)) { return; }
        Dumped.Add(Key);
        WT_WARNF(TEXT("[wt-nd-inner] %s inner=%s value=%s"), Tag,
                 Inner ? *Inner->GetClass()->GetName() : TEXT("<none>"),
                 Value ? *Value->GetClass()->GetName() : TEXT("<none>"));
    };

    // ★★0.2.32 按 [wt-nd-prop] 反射表**实测真类型**读取。
    //   上一版这四处全按错类型读（Parents 当对象、ChildrenAndRoads 当数组、NodesToUnhide 当 Map、
    //   UnhiddenBy 当对象），结果连**原版节点**也一律报 <prop missing> —— 这正是"MAM 有方块、
    //   没有连线"的根因。真类型（0.2.31 [wt-nd-prop] 实锤）：
    //     Parents          = ArrayProperty
    //     ChildrenAndRoads = MapProperty
    //     NodesToUnhide    = ArrayProperty
    //     UnhiddenBy       = ArrayProperty
    //   内层 Key/Value 类型也一并 dump，避免继续猜。

    // Parents = Array<对象引用>
    if (FArrayProperty* P = CastField<FArrayProperty>(WT_FindNodeMember(Scope, TEXT("Parents"))))
    {
        FScriptArrayHelper H(P, P->ContainerPtrToValuePtr<void>(Data));
        S += FString::Printf(TEXT("Parents=%d "), H.Num());
        if (FObjectPropertyBase* Inner = CastField<FObjectPropertyBase>(P->Inner))
        {
            for (int32 Ei = 0; Ei < H.Num() && Ei < 4; ++Ei)
            {
                const UObject* V = Inner->GetObjectPropertyValue(H.GetRawPtr(Ei));
                S += FString::Printf(TEXT("[%d]=%s "), Ei, V ? *V->GetName() : TEXT("<null>"));
            }
        }
        else
        {
            S += FString::Printf(TEXT("(inner=%s) pts=%s "),
                                 *P->Inner->GetClass()->GetName(), *WT_PointListText(P->Inner, H, 4));
        }
        DumpInnerOnce(TEXT("Parents"), P->Inner, nullptr);
    }
    else
    {
        S += TEXT("Parents=<prop missing> ");
    }

    // ChildrenAndRoads = Map<对象引用, 结构体…>
    if (FMapProperty* P = CastField<FMapProperty>(WT_FindNodeMember(Scope, TEXT("ChildrenAndRoads"))))
    {
        FScriptMapHelper H(P, P->ContainerPtrToValuePtr<void>(Data));
        S += FString::Printf(TEXT("ChildrenAndRoads=%d "), H.Num());
        DumpInnerOnce(TEXT("ChildrenAndRoads"), P->KeyProp, P->ValueProp);
    }
    else
    {
        S += TEXT("ChildrenAndRoads=<prop missing> ");
    }

    // NodesToUnhide = Array（不是 Map）
    if (FArrayProperty* P = CastField<FArrayProperty>(WT_FindNodeMember(Scope, TEXT("NodesToUnhide"))))
    {
        FScriptArrayHelper H(P, P->ContainerPtrToValuePtr<void>(Data));
        S += FString::Printf(TEXT("NodesToUnhide=%d pts=%s "), H.Num(), *WT_PointListText(P->Inner, H, 4));
        DumpInnerOnce(TEXT("NodesToUnhide"), P->Inner, nullptr);
    }
    else
    {
        S += TEXT("NodesToUnhide=<prop missing> ");
    }

    // UnhiddenBy = Array（不是单个对象）
    if (FArrayProperty* P = CastField<FArrayProperty>(WT_FindNodeMember(Scope, TEXT("UnhiddenBy"))))
    {
        FScriptArrayHelper H(P, P->ContainerPtrToValuePtr<void>(Data));
        S += FString::Printf(TEXT("UnhiddenBy=%d "), H.Num());
        if (FObjectPropertyBase* Inner = CastField<FObjectPropertyBase>(P->Inner))
        {
            for (int32 Ei = 0; Ei < H.Num() && Ei < 4; ++Ei)
            {
                const UObject* V = Inner->GetObjectPropertyValue(H.GetRawPtr(Ei));
                S += FString::Printf(TEXT("[%d]=%s "), Ei, V ? *V->GetName() : TEXT("<null>"));
            }
        }
        else
        {
            S += FString::Printf(TEXT("pts=%s "), *WT_PointListText(P->Inner, H, 4));
        }
        DumpInnerOnce(TEXT("UnhiddenBy"), P->Inner, nullptr);
    }
    else
    {
        S += TEXT("UnhiddenBy=<prop missing> ");
    }

    WT_WARNF(TEXT("[wt-nd] %s cls=%s %s"), Tag, *Cls->GetName(), *S);
}


URemote_FactoryGameWorldModule::URemote_FactoryGameWorldModule()
{
    // Mark as the root module for this mod so SML discovers and loads it.
    bRootModule = true;

    // Register the HUB milestones. SML's base DispatchLifecycleEvent pushes this list into
    // UModContentRegistry from its INITIALIZATION branch, which is what makes them appear in the
    // HUB. Leaving this out means the terminal has no unlock path at all.
    //
    // ★ v0.2.2..0.2.5 -- the Remote Factory's unlocks:
    //   * URemote_FactoryMilestone  (T1 HUB)  -> the terminal + handheld. High-Capacity Storage
    //     was moved OUT of this milestone and into the MAM research tree (see below) so all three
    //     upgrade buildings unlock "Dyson-sphere style" through the MAM, not the HUB.
    //   * The 60 research nodes (3 tracks x 20 levels) below ARE the unlock + scaling path for the
    //     three upgrade buildings: each track chains L1 -> L20; L1 costs only the vanilla milestone
    //     raw materials and unlocks the building, every further level multiplies its numbers. The
    //     level is the highest PURCHASED node, saved natively by the research system.
    mSchematics.Add(URemote_FactoryMilestone::StaticClass());

    // ★v0.2.25 用户定案：三个"插件"（大容量仓库 / 储能单元 / 产能加速器）**不再走 HUB 里程碑**，
    // 一律只在 MAM 研究树里逐级解锁。原先 v0.2.22 把储能单元(T3)、产能加速器(T5)也注册进 HUB，
    // 是为了修"两条路都断"；现在 MAM 能用了，就按最初的设计收回去：
    //   HUB 里程碑  -> 只负责无线终端本身（T1，且按 v0.2.3 定案只给手持便携版）
    //   MAM 研究树  -> 三个插件建筑的解锁 + 逐级强化
    // 两个里程碑类保留（存档/兼容）。★1.0.11 例外：超级蓄电池（储能单元）恢复由 T3 HUB 里程碑发放，
    // 故重新注册该里程碑（加速器仍保留注释、不注册）。MAM 研究轨仍不恢复（用户定案）。
    mSchematics.Add(UWTUpgradeStorageMilestone::StaticClass());
    // mSchematics.Add(UWTUpgradeAcceleratorMilestone::StaticClass());

    // ★2026-11-05 用户定案分档（原话「云端固体放到 T5，云端液体放到 T7，强化能量碎片放到 T9」）：
    //   三组东西各自独立成卡，T1「智能工厂」卡不再发放建筑，只留手持终端。
    //   ⚠ 加卡必须三处齐动：① 这里注册 ② ApplyUpgradeMilestoneData 装成本/解锁
    //     ③ WT_RepairRecipeAvailability 的 Live / Wanted 门槛 —— 缺一处就会出现
    //     「买了卡却没有」或「读档后消失」。
    mSchematics.Add(UWTUpgradeCloudSolidMilestone::StaticClass());   // T5  云端固体（上传 + 下载）
    mSchematics.Add(UWTUpgradeCloudFluidMilestone::StaticClass());   // T7  云端液体（上传 + 下载）

    for (TSubclassOf<UFGSchematic> C : WT_GetAllUpgradeResearch())
    {
        mSchematics.Add(C);
    }

    // ★0.2.36h（用户定案「可以出现AMS商店」）：强化能量碎片上 AWESOME 商店。
    // EST_ResourceSink 条目随 mSchematics 走 SML 官方注册链；售价/发货在 CONSTRUCTION 阶段
    // 的 ApplyShopSchematicData() 里填（见 DispatchLifecycleEvent）。
    // ★0.2.134 闸门（WT_FEATURE_POWER_SHARD，用户定案「一起下架」）：关=不注册该商店条目
    // （售价/发货填充同样跳过，见 DispatchLifecycleEvent 里的同开关判断）。
    if (WT_FEATURE_POWER_SHARD)
    {
        mSchematics.Add(UWTShopPowerShardSchematic::StaticClass());
        // ★2026-11-05 用户定案：碎片科技线重新独立成卡，且**改挂 T9**（原 T5 已废弃）。
        // 三条配方不再由 T1「智能工厂」卡发放 —— T1 现在只给手持终端。
        mSchematics.Add(UWTUpgradePowerShardMilestone::StaticClass());
    }

    // v0.2.5 MAM 里程碑修复：研究树必须用 SML 官方的 mResearchTrees 数组注册 —— SML 在加载阶段
    // 自动把它们加进 AFGResearchManager 的可用列表。之前用 PopulateResearchTreeListDelegate 运行时
    // 绑定，时机晚于管理器的广播，导致树永远加不进去（表现为 MAM 里看不到 Remote_Factory 研究树）。
    for (TSubclassOf<UFGResearchTree> T : WT_GetAllResearchTrees())
    {
        mResearchTrees.Add(T);
    }
}

/**
 * ★1.0.7 云端（位面存储）**初始容量** = 原版未研究状态的 10 倍（用户 2026-10-27 定案）。
 *
 * 原版口径（一手：SMLStarterProject/Config/DefaultGame.ini 的 [FGCentralStorageSettings] 段）：
 *   `mDefaultItemStackLimit=1` —— 没做任何存储研究时，**每个物品类**只能囤 1 组；
 *   MAM 容量轨再按 UFGUnlockCentralStorageItemLimit::mItemStackLimitIncrease 逐级累加
 *   （`AFGUnlockSubsystem::mUnlockedCentralStorageItemStackLimit`，头文件注释原文
 *    *"Added on top of UFGCentralStorageSettings::mDefaultItemLimit"*）。
 *
 * 本 mod 的完整口径集中在 Remote_FactoryCloudQuota.h：
 *   未研究 10 组/类；MAM 每级按等级系数放大（Lv2 = ×1.1、Lv3 = ×1.2 … Lv16 = ×2.5、末段到 ×2.9）。
 *   本函数只负责**把基线写进 CDO**；等级那一半在 Remote_FactoryUpgradeResearch 里算 unlock 增量。
 *   ★★★用户 2026-10-27「MAM 现在不加上去」⇒ 总闸 `WT_FEATURE_CLOUD_QUOTA_MAM=false`：
 *     等级那一半整段失效，云端配额就是下面这个 10 组/类（铁板 1,000 个），恒定不变。
 *
 * 为什么改 settings 的 CDO，而不是发一个 unlock：
 *   · CDO 是「默认值」这一个输入，改它幂等、不落盘、卸载即还原；
 *   · 发 unlock 需要往玩家已购研究里塞一条记录，而这个量本来就能由 `mDefaultItemStackLimit`
 *     单独表达 —— 能用配置表达的就不动玩家解锁列表。
 *   · 而 `AFGCentralStorageSubsystem::UpdateItemStackLimit()` 每次重算都会读到新的默认值，
 *     所以不需要 hook 原版的重算时机（读档 / 每次解锁）也不会被打回 1 组。
 *
 * 只在「当前默认值更小」时写：别把玩家人为调大的 Game.ini 覆盖回 10。
 */
static void WT_ApplyCloudInitialQuota(UWorld* World)
{
    constexpr int32 WT_VanillaUnresearchedStacks = Remote_FactoryCloudQuota::WT_VanillaUnresearchedStacks;
    constexpr int32 WT_CloudInitialMultiplier    = Remote_FactoryCloudQuota::WT_Multiplier;
    constexpr int32 WT_TargetStacks              = Remote_FactoryCloudQuota::WT_InitialStacks;

    // Get() 返回 const 指针（开发者设置单例），但我们改的是运行时 CDO，不写回 ini。
    UFGCentralStorageSettings* Settings =
        const_cast<UFGCentralStorageSettings*>(UFGCentralStorageSettings::Get());
    if (!Settings)
    {
        WT_ERRF(TEXT("[cloud-quota] UFGCentralStorageSettings::Get() returned null -- initial quota untouched."));
        return;
    }

    const int32 Before = Settings->mDefaultItemStackLimit;
    if (Before < WT_TargetStacks)
    {
        Settings->mDefaultItemStackLimit = WT_TargetStacks;
    }

    // 立刻让「当前生效值」按新默认值重算：mItemStackLimit 是 Transient，读档时机在
    // POST_INITIALIZATION 之前或之后都可能，主动算一次两条路都能覆盖。
    // UnlockSubsystem 是 UpdateItemStackLimit() 的数据来源之一，没起来就别硬调。
    int32 LiveLimit = -1;
    if (AFGUnlockSubsystem* Unlock = AFGUnlockSubsystem::Get(World))
    {
        if (AFGCentralStorageSubsystem* Sub = AFGCentralStorageSubsystem::Get(World))
        {
            Sub->UpdateItemStackLimit();
            LiveLimit = Sub->GetCentralStorageItemStackLimit();
        }
    }

    // 诊断锚点（WT_LOGF 被 WT_LOG_VERBOSE=false 编译剔除，必须用 WT_WARNF）
    // 期望：vanilla-unresearched=1 target=10 (settings was 1, now 10) | live limit=10 | mam-scaling=off
    WT_WARNF(TEXT("[cloud-quota] initial stacks: vanilla-unresearched=%d x%d = %d "
                  "(settings was %d, now %d) | live limit per class=%d | mam-scaling=%s"),
             WT_VanillaUnresearchedStacks, WT_CloudInitialMultiplier, WT_TargetStacks, Before,
             Settings->mDefaultItemStackLimit, LiveLimit,
             Remote_FactoryCloudQuota::WT_FEATURE_CLOUD_QUOTA_MAM ? TEXT("on") : TEXT("off"));
}

/**
 * ★1.0.7x 超频上限 250% → 400%
 *   用户 2026-10-27 实测：「（强化碎片）放进去后超频最大 250%，到不了 400%，且加成和原版一样」。
 *
 * ## 一手依据（F:/Workbuddy/SMLDev/SMLStarterProject/Source/FactoryGame）
 *   · `AFGBuildableFactory::mMaxPotential` 注释原文：
 *       "Default maximum potential on the buildable, **not accounting for the installed power shards**"
 *       （Public/Buildables/FGBuildableFactory.h:621-623，`UPROPERTY(EditDefaultsOnly)`）；
 *   · `GetMaxPotential()` 直接返回它（同文件 :241）；
 *   · `GetCurrentMaxPotential()`(:245) / `GetCurrentMaxPotentialForType(type,min,max,mult)`(:364)
 *     算完「min + 槽内碎片数 × GetBoostValue(碎片类) × mult」后**必然 clamp 到 maxValue**，
 *     而调用方传的 maxValue 就是 `GetMaxPotential()`。
 *   ⇒ 原版蓝图把机器 mMaxPotential 设成 2.5：3 片 × (+50%) = 250%，**正好顶死**。
 *     强化碎片每片 `mExtraPotential = 1.0`（+100%），3 片本该 1 + 3×1.0 = 400%，
 *     却因上限仍 2.5 被夹成 250% —— 这就是「到不了 400%」的全部原因。
 *     原版碎片只有 +50%（3 槽最多 1 + 1.5 = 250%），所以把上限抬到 4.0 对原版玩家零影响。
 *
 * ## ★1.0.8g 废弃：「改 CDO 让新实例自动拿到 4.0」这个做法会污染原版归一化分母（见函数内新口径），
 * ## 现在只按实例、且只对「槽里有我们碎片」的机器设上限。下面这段仅作历史记录。
 * ## 为什么曾经改 CDO，而不是只改场景里的实例
 *   `mMaxPotential` 是 EditDefaultsOnly 且**不是 SaveGame**：读档时不从存档恢复，
 *   而是 Actor 构造时从 CDO 拷贝。⇒ 只要在 Actor 被 spawn 之前把 CDO 改好，新放的机器
 *   和读档载入的旧机器都会自动拿到 4.0。CONSTRUCTION 阶段（模块加载时、世界还没载入 Actor）
 *   正是那个时机；POST_INITIALIZATION 后再跑两遍是为了兜住「蓝图类晚于第一次扫描才加载」
 *   以及「已经 spawn 出来的实例」这两种情况。
 *
 * ## 诊断口径
 *   同时打印「我们自己碎片 CDO 的 mExtraPotential」与「原版 Desc_CrystalShard 的 mExtraPotential」。
 *   用户说「加成和原版一样」有两种可能：① 每片增量没生效（读到的是 0.5）② 上限没抬。
 *   只报上限无法区分，必须把两个增量并排打出来。
 */
/**
 * ★1.0.7z 属性解析：**每次现查**，不再 static 缓存。
 *
 * 1.0.7x 用 static lambda 缓存了 FFloatProperty*，而第一次调用发生在 CONSTRUCTION
 * （模块加载最早的时机）。实测日志自相矛盾 —— 同一轮里 `class CDOs ... lowest was 1.00`
 * 与 `live factory actors ... lowest was 5.00`，而原版机器蓝图 mMaxPotential 只有 2.5：
 * 这两个数说明 ContainerPtrToValuePtr 落到的偏移和运行时对象对不上（早期解析出的属性
 * 对象可能属于「当时已注册的另一个类版本」）。FindPropertyByName 只是一次 TMap 查表，
 * 每次重查的成本可以忽略，换来的是「偏移一定属于当前类」。
 */
// ★1.0.8g 上限常量（巡检、面板补丁共用一处，避免两个 4.0f 各自漂移）。
static constexpr float WT_OVERCLOCK_CEILING = 4.0f;
// 目标分母：滑条填充 = current / 分母，刻度搬到 0/25/50/75/100% ⇒ 分母必须是 4.0，
// 100% 才会正好落在 25% 的位置上。
//
// ★1.0.8k 原版面板自己的换算分母（蓝图常量，写死在蓝图图表里，不是 UPROPERTY：
//   1.0.8j 把面板类链上所有 float 变量 dump 了一遍，只捞到 `Widget.RenderOpacity`）。
//   实测三点：pos 0.40→pending 1.00、0.50→1.25、1.00→2.50 ⇒ **pending = 2.5 × pos**，
//   而且这个分母恒定，与 mMaxPotential 毫无关系（max 从 1.00 抬到 5.00，同一个 pos
//   仍然换算出同一个 pending）。
//   ⇒ 既然改不动 2.5，就改**滑条量程**：ours 时把 USlider 的 MaxValue 开到 4.0/2.5 = 1.6，
//     面板自己算出来就是 0..400%，滑块位置/百分比文字/材质填充全部原生自洽，
//     我们不再需要「接管 pending」那条会把滑块顶出轨道（val=1.6 溢出）的歪路。
static constexpr float WT_PANEL_POTENTIAL_DENOM = 2.5f;

// ★1.0.8N 超频上限 = 1.0 + 碎片数（每片 +100%），3 片顶到 4.0。
//   0 片 = 1.0（原版 100%，不接管）／1 片 = 2.0／2 片 = 3.0／3 片 = 4.0。
//
//   ★为什么必须按片数算（用户 2026-10-27 15:00「放置 1 个，为什么能拉满超频」）：
//   1.0.8k～8M 把上限写死成 WT_OVERCLOCK_CEILING(4.0)、量程写死 1.6，于是**只要槽里有
//   一片**面板就被开成 0..400% —— 1 片也能拖到 400%，等于凭 1 片白拿 3 片的效果。
//   原版是「3 片 × +50% = 250%」，片数决定上限，我们只是把每片 +50% 改成 +100%。
static float WT_CeilingForShards(int32 ShardCount)
{
    return 1.0f + static_cast<float>(FMath::Clamp(ShardCount, 0, 3));
}

// 前置声明：碎片计数的实现在文件后段（碎片槽那节），巡检/面板函数在它之前，
// 而「这台机器槽里有几片我们的碎片」正是上限口径的唯一判据。
static int32 WT_CountOurShards(AFGBuildableFactory* F);

static FFloatProperty* WT_MaxPotentialProperty()
{
    for (UClass* C = AFGBuildableFactory::StaticClass(); C; C = C->GetSuperClass())
    {
        if (FFloatProperty* P = CastField<FFloatProperty>(C->FindPropertyByName(TEXT("mMaxPotential"))))
        {
            return P;
        }
    }
    return nullptr;
}

/**
 * ★1.0.7z `mCanChangePotential`（FGBuildableFactory.h:600-602，
 * "Set this to true if we want this building to be able to change the production rate
 * potential with the Slider of Potential"）。
 *
 * 为什么改用它做判据：1.0.7x 用「mMaxPotential >= 2.0」判断「这台机器本来就能超频」，
 * 但 C++ 里 mMaxPotential 的默认值是 1.0，2.5 是**蓝图 CDO 覆盖**出来的 ⇒ 判据把真正
 * 能超频的机器也当成「刻意锁死」跳过了（实测 raised=0/103）。mCanChangePotential 才是
 * 原版自己用来表示「这台能超频」的语义位。
 */
static FBoolProperty* WT_CanChangePotentialProperty()
{
    for (UClass* C = AFGBuildableFactory::StaticClass(); C; C = C->GetSuperClass())
    {
        if (FBoolProperty* P = CastField<FBoolProperty>(C->FindPropertyByName(TEXT("mCanChangePotential"))))
        {
            return P;
        }
    }
    return nullptr;
}

/**
 * ★1.0.8g 撤掉「改原版碎片每片增量」：`Desc_CrystalShard::mExtraPotential` 的改写已删除。
 *
 * 1.0.7z～1.0.8f 曾把原版碎片从 +50% 抬到 +100%，理由是「玩家手里插的往往是原版碎片」。
 * 用户 2026-10-27 13:35「这原版超频条都不对」实测证明这个改动在污染原版行为：
 *   · 原版 1 片应该是 150%，被改成 200%；2 片 200% → 250%（整体偏移一档）；
 *   · 我们的碎片 CDO 本来就是 1.00（日志 `boost ours=1.00`），根本不需要「对齐」。
 * ⇒ 还原：原版碎片 +50%、我们的碎片 +100%，两条口径各走各的，原版一个数都不动。
 */

/**
 * ★1.0.8g 「这台机器的超频上限应该是多少」的**唯一实现**（巡检与面板补丁共用）。
 *
 *   槽里有 N 片我们碎片 → 抬到 1.0 + N（每片 +100%：1 片 200% / 2 片 300% / 3 片 400%）
 *   其余一律 → 一个字节都不动（原版锁死 / 研究阶段抬过的值都保持原样）
 *
 * 返回 true = 值真的被改了；OutBase < 0 = 这台不该碰（原版锁死 / 属性拿不到），调用方跳过。
 */
static bool WT_ApplyCeiling(AFGBuildableFactory* F, bool& bOutOurs,
                            float& OutBefore, float& OutAfter, float& OutBase)
{
    bOutOurs = false;
    OutBefore = OutAfter = OutBase = -1.f;
    if (!F)
    {
        return false;
    }
    FFloatProperty* Prop = WT_MaxPotentialProperty();
    if (!Prop)
    {
        return false;
    }
    if (FBoolProperty* CanProp = WT_CanChangePotentialProperty())
    {
        if (!CanProp->GetPropertyValue_InContainer(F))
        {
            return false;   // 原版刻意锁死的机器，一个字节都别碰
        }
    }
    float* V = Prop->ContainerPtrToValuePtr<float>(F);
    if (!V)
    {
        return false;
    }

    // ★1.0.8j 口径第三次修正：**只抬不降**。
    //
    //   前两版的错都在「基线」上，而本次日志证明基线的存在本身就有害：
    //     · 1.0.7x～8f：拿 CDO 当基线 ⇒ 蓝图 CDO(5.0) ≠ 实例原值(1.0)，写出 5.00；
    //     · 1.0.8g～8i：拿「首见值」当基线 ⇒ 把 Upgrade.ApplyShardStage 刚抬到 5.00 的机器
    //       又按回 1.00（日志 `ceiling RESTORED ... 5.00 -> 1.00`）。
    //   而 ApplyShardStage 是**本 mod 既有设计**（研究阶段把全场上限抬到 cap，日志
    //   `[pot] stage 1 applied: cap 500.0%, CDOs raised=103`）。按回 1.00 的后果不是观感问题，
    //   而是**功能性回归**：mMaxPotential=1.0 时任何超频（含原版碎片）都会被夹回 100%。
    //   ⇒ 现在：有我们碎片 → max(当前值, Target)；没有 → 一个字节都不动（谁也不许降）。
    const float Base = *V;      // 只读当前值用于日志，不再当"基线"写回

    OutBase = Base;
    OutBefore = *V;
    const int32 ShardN = WT_CountOurShards(F);
    bOutOurs = (ShardN > 0);
    if (!bOutOurs)
    {
        OutAfter = *V;
        return false;           // ★绝不下降：原版碎片 / 研究阶段抬过的值都保持原样
    }
    // ★1.0.8N 上限跟着**碎片数**走：1 片 200% / 2 片 300% / 3 片 400%。
    const float Want = FMath::Max(Base, WT_CeilingForShards(ShardN));
    OutAfter = Want;
    if (FMath::IsNearlyEqual(*V, Want, 0.001f))
    {
        return false;
    }
    *V = Want;
    return true;
}

static void WT_RaiseOverclockCeiling(const TCHAR* Tag, UWorld* World)
{
    FFloatProperty* Prop = WT_MaxPotentialProperty();
    if (!Prop)
    {
        WT_ERRF(TEXT("[shard] overclock ceiling (%s): AFGBuildableFactory::mMaxPotential not found -- "
                     "ceiling stays vanilla (250%%)."), Tag);
        return;
    }
    const bool bQuietTag = (FCString::Strcmp(Tag, TEXT("tick")) == 0);

    // ★1.0.8g 口径重写（用户 2026-10-27 13:35「这原版超频条都不对 / 你看看 100% 的滑条搁哪在」）：
    //   **不改 CDO，也不抬没有我们碎片的机器**。
    //   1.0.7x～1.0.8f 把所有机器（CDO + 实例）无脑抬到 4.0，可原版滑条的填充宽度是拿
    //   `current / GetMaxPotential()` 归一化的 ⇒ 没放碎片的机器也变成 1.0/4.0 = 0.25，
    //   橙色只填全宽的 1/4（实测截图 x=37→160，条宽 494），而 100% 刻度在第一段末
    //   （40% 宽，x≈235）—— 两者对不上，整条原版超频条失真。
    //   现在：基线 = 该类 CDO 的值（CDO 我们一个字节都不写 ⇒ 恒为原版 2.5），
    //   只有「槽里真的插了我们碎片」的机器才抬到 Target(4.0)，其余一律恢复基线。
    //   ⇒ 放进去 = 400% 上限；取出来 = 立刻回到原版 250%，一个像素都不差。
    if (!World || World->WorldType != EWorldType::Game)
    {
        return;   // CONSTRUCTION 阶段没有世界：以前正是在这里改 CDO，那才是污染源
    }

    UClass* VanillaShardClass = StaticLoadClass(UFGItemDescriptor::StaticClass(), nullptr,
        TEXT("/Game/FactoryGame/Resource/Environment/Crystal/Desc_CrystalShard.Desc_CrystalShard_C"));

    FFloatProperty* BoostProp = nullptr;
    for (UClass* C = UFGPowerShardDescriptor::StaticClass(); C; C = C->GetSuperClass())
    {
        if (FFloatProperty* P = CastField<FFloatProperty>(C->FindPropertyByName(TEXT("mExtraPotential"))))
        {
            BoostProp = P;
            break;
        }
    }
    auto ReadBoost = [BoostProp](UObject* CDO) -> float
    {
        return (BoostProp && CDO) ? *BoostProp->ContainerPtrToValuePtr<float>(CDO) : -1.f;
    };
    const float OurBoost = ReadBoost(UWTUpgradePowerShardDescriptor::StaticClass()->GetDefaultObject(false));
    const float VanillaBoost = ReadBoost(VanillaShardClass ? VanillaShardClass->GetDefaultObject(false) : nullptr);

    int32 ActorsScanned = 0;
    int32 ActorsOverclockable = 0;
    int32 ActorsRaised = 0;
    int32 ActorsRestored = 0;
    int32 ActorsWithOurs = 0;
    float LowestActor = TNumericLimits<float>::Max();
    int32 DetailBudget = 6;                        // 一次扫描最多 6 条明细，避免大存档刷屏
    int32 ProbeBudget = bQuietTag ? 0 : 2;         // tick 扫描不打探针（每 5s 两条同款会刷屏）

    for (TActorIterator<AFGBuildableFactory> It(World); It; ++It)
    {
        AFGBuildableFactory* F = *It;
        if (!F)
        {
            continue;
        }
        ++ActorsScanned;
        float* V = Prop->ContainerPtrToValuePtr<float>(F);
        if (!V)
        {
            continue;
        }
        bool bOurs = false;
        float Before = -1.f, After = -1.f, Base = -1.f;
        const bool bChanged = WT_ApplyCeiling(F, bOurs, Before, After, Base);
        if (Base < 0.f)
        {
            continue;   // 原版锁死 / 属性拿不到 —— 完全没碰
        }
        ++ActorsOverclockable;
        LowestActor = FMath::Min(LowestActor, After);
        if (bOurs)
        {
            ++ActorsWithOurs;
        }
        if (bChanged)
        {
            if (After > Before) { ++ActorsRaised; } else { ++ActorsRestored; }
            if (DetailBudget-- > 0)
            {
                WT_WARNF(TEXT("[shard] ceiling %s %s: %.2f -> %.2f (ours=%d base=%.2f)"),
                         (After > Before) ? TEXT("UP") : TEXT("RESTORED"),
                         *F->GetClass()->GetName(), Before, After, bOurs ? 1 : 0, Base);
            }
        }

        // 碎片槽探针：玩家报「超频无效」有三种完全不同的原因（① 放的是原版碎片 ② boost 没被采纳
        // ③ 被 mMaxPotential 夹住），日志里必须一眼能分开。
        if (ProbeBudget-- > 0)
        {
            UFGInventoryComponent* PotInv = F->GetPotentialInventory();
            const int32 NOurs = PotInv ? PotInv->GetNumItems(UWTUpgradePowerShardDescriptor::StaticClass()) : -1;
            const int32 NVanilla = (PotInv && VanillaShardClass) ? PotInv->GetNumItems(VanillaShardClass) : -1;
            float CurPot = -1.f;
            if (FFloatProperty* CurProp = CastField<FFloatProperty>(
                    AFGBuildableFactory::StaticClass()->FindPropertyByName(TEXT("mCurrentPotential"))))
            {
                CurPot = *CurProp->ContainerPtrToValuePtr<float>(F);
            }
            WT_WARNF(TEXT("[shard-probe] %s: maxPotential=%.2f base=%.2f current=%.2f | "
                          "slot ours=%d vanilla=%d | boost ours=%.2f vanilla=%.2f"),
                     *F->GetClass()->GetName(), After, Base, CurPot,
                     NOurs, NVanilla, OurBoost, VanillaBoost);
        }
    }
    if (LowestActor == TNumericLimits<float>::Max()) { LowestActor = -1.f; }

    // 汇总行节流：tick 扫描且无改动时不打（否则每 5s 一条刷屏）。
    if (!bQuietTag || ActorsRaised > 0 || ActorsRestored > 0)
    {
        WT_WARNF(TEXT("[shard] overclock ceiling (%s): target=1+N shards (max %.0f%%) | actors scanned=%d overclockable=%d "
                      "raised=%d restored=%d withOurShard=%d lowest=%.2f | extraPotential ours=%.2f vanilla=%.2f"),
                 Tag, WT_OVERCLOCK_CEILING * 100.f, ActorsScanned, ActorsOverclockable,
                 ActorsRaised, ActorsRestored, ActorsWithOurs, LowestActor, OurBoost, VanillaBoost);
    }
}

// ★1.0.8 碎片槽只读诊断：把"机器 potential 槽里到底有什么"打到日志。
//   为什么需要：玩家报「放入三个，三格仍然只显示一个碎片」。可能的原因只有两类 ——
//     ① 数据侧真的只进了 1 片（槽按某种规则拒收）；
//     ② 3 片都在，但原版 UMG 槽位只给第一格画了图标。
//   这两类的修法完全不同，不能猜。这里读的是 `mInventoryStacks` 的**原始数组**
//   （FScriptArrayHelper 直接看内存），绕开 SML 里可能被桩化的 GetNumItems/GetStackFromIndex。
static void WT_DumpShardSlots(UWorld* World)
{
    if (!World)
    {
        return;
    }
    FObjectProperty* InvProp = CastField<FObjectProperty>(
        AFGBuildableFactory::StaticClass()->FindPropertyByName(TEXT("mInventoryPotential")));
    FArrayProperty* StacksProp = CastField<FArrayProperty>(
        UFGInventoryComponent::StaticClass()->FindPropertyByName(TEXT("mInventoryStacks")));
    if (!InvProp || !StacksProp)
    {
        WT_WARNF(TEXT("[shard-ui] slot dump skipped: invProp=%d stacksProp=%d"),
                 InvProp ? 1 : 0, StacksProp ? 1 : 0);
        return;
    }

    int32 Dumped = 0;
    for (TActorIterator<AFGBuildableFactory> It(World); It && Dumped < 6; ++It)
    {
        AFGBuildableFactory* F = *It;
        if (!F)
        {
            continue;
        }
        UObject* InvObj = InvProp->GetObjectPropertyValue(InvProp->ContainerPtrToValuePtr<void>(F));
        UFGInventoryComponent* Inv = Cast<UFGInventoryComponent>(InvObj);
        if (!Inv)
        {
            continue;
        }

        FScriptArrayHelper Helper(StacksProp, StacksProp->ContainerPtrToValuePtr<void>(Inv));
        FString Line;
        for (int32 i = 0; i < Helper.Num(); ++i)
        {
            const FInventoryStack* S = reinterpret_cast<const FInventoryStack*>(Helper.GetRawPtr(i));
            if (!S || S->NumItems <= 0)
            {
                continue;
            }
            UClass* ItemClass = S->Item.GetItemClass();
            Line += FString::Printf(TEXT(" [%d]%s x%d"), i,
                                    ItemClass ? *ItemClass->GetName() : TEXT("<null>"), S->NumItems);
        }
        if (Line.IsEmpty())
        {
            continue;
        }
        WT_WARNF(TEXT("[shard-ui] factory %s potential:%s"), *F->GetClass()->GetName(), *Line);
        ++Dumped;
    }
    if (Dumped == 0)
    {
        WT_WARNF(TEXT("[shard-ui] slot dump: no factory has anything in its potential inventory."));
    }
}

// ★1.0.8d 「刻度要不要改」的唯一可靠判据：**这台机器的潜在库存里有没有我们的强化碎片**。
// ★1.0.8N 升级成**计数**（返回片数）：上限 = 1.0 + 片数，1 片 200% / 2 片 300% / 3 片 400%。
//
//   1.0.8b 用的是"面板里的 UImage 画刷名含 rainbow"—— 实测**永远为假**：
//   碎片图标在 `Widget_CostSlotWrapper_C` 里面，而那是**嵌套 UserWidget**，
//   外层 WidgetTree->GetAllWidgets() 看不到它（日志里整棵树只有 Background 与 mProgressBar
//   两个 UImage）。⇒ 刻度一直保持原版的 150/200/250，用户看到的「150 该是 200」没被修掉。
//   改为直接读机器数据：槽里是 UWTUpgradePowerShardDescriptor ⇒ 改；原版碎片/空槽 ⇒ 不动。
static int32 WT_CountOurShards(AFGBuildableFactory* F)
{
    if (!F)
    {
        return 0;
    }
    // 属性每次现查（缓存 FProperty 在早期生命周期解析会拿到错偏移，1.0.7x 已踩过这个坑）。
    FObjectProperty* InvProp = CastField<FObjectProperty>(
        AFGBuildableFactory::StaticClass()->FindPropertyByName(TEXT("mInventoryPotential")));
    FArrayProperty* StacksProp = CastField<FArrayProperty>(
        UFGInventoryComponent::StaticClass()->FindPropertyByName(TEXT("mInventoryStacks")));
    if (!InvProp || !StacksProp)
    {
        return false;
    }
    UObject* InvObj = InvProp->GetObjectPropertyValue(InvProp->ContainerPtrToValuePtr<void>(F));
    UFGInventoryComponent* Inv = Cast<UFGInventoryComponent>(InvObj);
    if (!Inv)
    {
        return false;
    }

    // 用 FScriptArrayHelper 直接读原始数组（GetNumItems 等在 SML 桩里不可靠，见记忆铁律）。
    int32 Total = 0;
    FScriptArrayHelper Helper(StacksProp, StacksProp->ContainerPtrToValuePtr<void>(Inv));
    for (int32 i = 0; i < Helper.Num(); ++i)
    {
        const FInventoryStack* S = reinterpret_cast<const FInventoryStack*>(Helper.GetRawPtr(i));
        if (!S || S->NumItems <= 0)
        {
            continue;
        }
        UClass* ItemClass = S->Item.GetItemClass();
        if (ItemClass && ItemClass->IsChildOf(UWTUpgradePowerShardDescriptor::StaticClass()))
        {
            Total += S->NumItems;
        }
    }
    return Total;
}

/**
 * ★1.0.8AK 潜在库存总格数（= `mInventoryStacks` 数组长度，实测 4）。
 *   只在「算显示补位映射」时用：需要遍历全部索引才能找出「哪几格有碎片」和「哪一格是空的」。
 */
static int32 WT_PotSlotCap(UFGInventoryComponent* Inv)
{
    if (!Inv)
    {
        return 0;
    }
    FArrayProperty* StacksProp = CastField<FArrayProperty>(
        UFGInventoryComponent::StaticClass()->FindPropertyByName(TEXT("mInventoryStacks")));
    if (!StacksProp)
    {
        return 0;
    }
    FScriptArrayHelper Helper(StacksProp, StacksProp->ContainerPtrToValuePtr<void>(Inv));
    return Helper.Num();
}

/**
 * ★1.0.8AJ 潜在库存**第 Idx 格**里有几片我们的碎片（0 = 空）。
 *
 * 为什么必须逐格读（用户 2026-10-27 23:59 实测铁证）：
 *   超频面板的 3 个 CostSlot 是**按索引**对应 `mInventoryPotential` 的前三格的。
 *   用户取走第 1 格的碎片后，库存**不会前移**：日志 `pot inv: [0]x1 [1]x1 [2]x1 -> [1]x1 [2]x1`
 *   —— idx0 留了个空洞，idx1/idx2 原地不动。而旧逻辑拿「碎片总数 2」当槽数，
 *   把真正装着碎片的 idx2 判成空闲格、写回原版成本类 ⇒ 那一格立刻变灰、且再也点不动。
 *   ⇒ 占用判定必须逐格：`Slot_i` 有碎片 ⇔ `PotInv[i]` 里有我们的碎片。
 */
static int32 WT_PotSlotShardCount(UFGInventoryComponent* Inv, int32 Idx)
{
    if (!Inv || Idx < 0)
    {
        return 0;
    }
    FArrayProperty* StacksProp = CastField<FArrayProperty>(
        UFGInventoryComponent::StaticClass()->FindPropertyByName(TEXT("mInventoryStacks")));
    if (!StacksProp)
    {
        return 0;
    }
    FScriptArrayHelper Helper(StacksProp, StacksProp->ContainerPtrToValuePtr<void>(Inv));
    if (Idx >= Helper.Num())
    {
        return 0;
    }
    const FInventoryStack* S = reinterpret_cast<const FInventoryStack*>(Helper.GetRawPtr(Idx));
    if (!S || S->NumItems <= 0)
    {
        return 0;
    }
    UClass* ItemClass = S->Item.GetItemClass();
    if (ItemClass && ItemClass->IsChildOf(UWTUpgradePowerShardDescriptor::StaticClass()))
    {
        return S->NumItems;
    }
    return 0;
}

/**
 * 潜在库存的「签名」：每格类名 + 数量，用来做逐拍差分。
 *
 * ★为什么需要它（1.0.8W 取证）：用户报「槽 2/3 取不下来」，但日志实锤潜在库存是
 *   `[0]x1 [1]x1 [2]x1` 三个独立格 —— 库存侧完全正常。剩下两种可能必须分开：
 *   (a) 原版点击根本没走到移除逻辑（格被当空槽）→ 签名不变；
 *   (b) 原版移除了、我们每拍又把显示/库存改回去 → 签名变了。
 *   两者修法完全不同，靠这行日志一次分清，不再猜。
 */
static FString WT_PotInvSignature(AFGBuildableFactory* F)
{
    if (!F)
    {
        return TEXT("<no-factory>");
    }
    FObjectProperty* InvProp = CastField<FObjectProperty>(
        AFGBuildableFactory::StaticClass()->FindPropertyByName(TEXT("mInventoryPotential")));
    FArrayProperty* StacksProp = CastField<FArrayProperty>(
        UFGInventoryComponent::StaticClass()->FindPropertyByName(TEXT("mInventoryStacks")));
    if (!InvProp || !StacksProp)
    {
        return TEXT("<no-props>");
    }
    UObject* InvObj = InvProp->GetObjectPropertyValue(InvProp->ContainerPtrToValuePtr<void>(F));
    UFGInventoryComponent* Inv = Cast<UFGInventoryComponent>(InvObj);
    if (!Inv)
    {
        return TEXT("<no-inv>");
    }

    FScriptArrayHelper Helper(StacksProp, StacksProp->ContainerPtrToValuePtr<void>(Inv));
    FString Out = FString::Printf(TEXT("cap=%d"), Helper.Num());
    for (int32 i = 0; i < Helper.Num(); ++i)
    {
        const FInventoryStack* S = reinterpret_cast<const FInventoryStack*>(Helper.GetRawPtr(i));
        if (!S || S->NumItems <= 0)
        {
            continue;
        }
        UClass* ItemClass = S->Item.GetItemClass();
        Out += FString::Printf(TEXT(" [%d]%s x%d"), i,
                               ItemClass ? *ItemClass->GetName() : TEXT("<null>"), S->NumItems);
    }
    return Out;
}


// ===================== 碎片槽「浅灰」现象（1.0.8S 诊断 → 1.0.8T 定案） =====================
//
// 现象（用户 2026-10-27 15:53「后面两个插槽位置，显示浅灰色」）：
//   插满 3 片、跑在 400% 时，第 1 个槽是彩色图标，第 2、3 个槽却是**发白的浅灰图标**。
//
// 像素量化（1080p 真实坐标，槽图标框内平均饱和度 |max-min| 通道差）：
//   0 片   槽1/2/3 = 1.0 / 0.7 / 0.7        （空槽：几乎无彩色）
//   1 片   槽1/2/3 = 10.2 / 0.7 / 0.6        （第 1 槽彩色，其余空）
//   3 片   槽1/2/3 = 10.2 / 4.2 / 4.4        （第 2、3 槽**有**图标，但只有四成饱和度）
//   机器侧数据同步确认过：WT_CountOurShards 数到 3 片、NumShards=3、pending=4.0。
//
// ★1.0.8T 定案（推翻 1.0.8S 的「被压暗」假设）：**根本没有被压暗**。
//   原版 0/1/2/3 片四态实测的格底色是 214/232 两档，规则 = {N-1, N} 这两格亮(214)、其余暗(232)；
//   而与「原版 1 片」对拍时，我们已插格的**图标区域逐像素完全相同**（4428 像素 0 差）。
//   发白的真正原因是「原版把它按 0/1 渲染」⇒ 高亮窗永远停在 {0}，第 2、3 格一直是 232 暗底。
//   修法见 WT_SyncShardSlots（B 路改成本类 / A 路亲手翻成 1/1）。
//
// 为什么不能靠「画刷名」判定：图标生在**嵌套两层**的 UserWidget 里
//   Panel → Widget_CostSlotWrapper_C(CostSlot_0/1/2) → Widget_InventorySlot_C → Image(图标)，
//   面板外层 WidgetTree->GetAllWidgets() 只到第一层（1.0.8b 就死在这上面）。
static void WT_CollectWidgetsDeep(UUserWidget* Root, TArray<UWidget*>& Out, int32 Depth)
{
    if (!Root || !Root->WidgetTree || Depth <= 0)
    {
        return;
    }
    TArray<UWidget*> Inner;
    Root->WidgetTree->GetAllWidgets(Inner);
    for (UWidget* X : Inner)
    {
        if (!X)
        {
            continue;
        }
        Out.Add(X);
        if (UUserWidget* Sub = Cast<UUserWidget>(X))
        {
            WT_CollectWidgetsDeep(Sub, Out, Depth - 1);
        }
    }
}

// 把一个控件抬回「完全不透明」。只动 alpha 通道，不动 RGB —— 万一蓝图用颜色表达
// 「物品不足」的语义，抬 alpha 也不会把该有的色调改掉。
static bool WT_ForceOpaque(UWidget* X)
{
    if (!X)
    {
        return false;
    }
    bool bChanged = false;
    if (!FMath::IsNearlyEqual(X->GetRenderOpacity(), 1.f, 0.01f))
    {
        X->SetRenderOpacity(1.f);
        bChanged = true;
    }
    if (UImage* Img = Cast<UImage>(X))
    {
        FLinearColor C = Img->GetColorAndOpacity();
        if (!FMath::IsNearlyEqual(C.A, 1.f, 0.01f))
        {
            C.A = 1.f;
            Img->SetColorAndOpacity(C);
            bChanged = true;
        }
    }
    return bChanged;
}

// ===================== 碎片槽「成本满足」态接管（1.0.8T · A+B 双路） =====================
//
// 【现象与根因】槽位是原版通用「成本格」`Widget_CostSlotWrapper_C`：它绑**一件固定的成本物品**，
//   再数机器潜在库存里有几件**那个类**，然后按 have/need 渲染（底部橙条 + "1/1" + 槽位高亮窗）。
//   成本物品是写死在面板里的（证据：0 片时我们面板与原版**像素级相同** ⇒ 成本格认得这台机器；
//   而空槽的幽灵图标恒为**原版碎片**，说明成本类不是从槽里读的）。于是它数不到我们的碎片类 ⇒
//   永远按 0/1 渲染：
//     · 已插格底部没有橙色 "1/1" 条（我们那格只剩深色角标底 + "1"）
//     · 高亮窗永远停在 [0]（原版是 {N-1, N}，N=片数），插到第 2、3 片也不推进
//       ⇒ 用户 2026-10-27 15:53「后面两个插槽位置，显示浅灰色」的真正来源
//   像素实测（1080p，两次会话几何一致）：已插格与空槽在**橙条以上区域逐像素完全相同**
//   （4428 个像素里 0 个差值 >20 灰阶）⇒ 图标尺寸/位置/底色都不用动，缺的只是底部 47px 橙条与文字。
//
// 【B 路（首选，让原版自己画）】把面板上的「成本类」变量改写成我们的碎片类：
//   面板把成本类存在某个 TSubclassOf<UFGPowerShardDescriptor> 变量里。只要在我们的碎片插入期间
//   它指向 UWTUpgradePowerShardDescriptor，原版就自己数到、自己画 1/1、自己推进高亮窗 ——
//   零像素抄写、零布局猜测。取走碎片后写回**首次扫到的原版类**，原版碎片照旧。
//   ★判据收紧（两个都要满足才碰）：① 属性允许的类型 ⊇ 原版碎片基类（我们的类装得进去）；
//     ② 当前值确实是碎片类。写之前还要幂等比较 ⇒ 绝不改坏面板上无关的类引用。
//
// 【A 路（兜底）】B 找不到那个变量（成本类被烤进蓝图函数/资产，不是变量）时，亲手翻成「已满足」：
//   `ProgressBar_0` 显示 + 100%、`mTextBG`（深色角标底）收起、`mStackSizeLbl` 写 "1/1" 并居中。
//   只处理**已占用**槽（0..ShardN-1）；空槽一个字节都不碰（原版空槽本来就该是幽灵态）。
//   ★尚未复刻的只剩一格底色（原版 214=当前格/下一格、232=更远格；宿主控件本轮 dump 出来，
//     见本文件顶部「碎片槽浅灰现象」段的实测表），下一版按真名写。

namespace
{
    /** 「成本类」属性的定位结果：属性本身 + 持有它的对象（面板实例 / 某个槽控件实例）。 */
    struct WTShardClassRef
    {
        FProperty* Prop = nullptr;
        UObject*   Holder = nullptr;
    };
}

static UObject* WT_ReadClassProp(FProperty* P, UObject* Holder)
{
    if (!P || !Holder)
    {
        return nullptr;
    }
    if (FClassProperty* CP = CastField<FClassProperty>(P))
    {
        return CP->GetObjectPropertyValue_InContainer(Holder);
    }
    if (FObjectProperty* OP = CastField<FObjectProperty>(P))
    {
        return OP->GetObjectPropertyValue_InContainer(Holder);
    }
    return nullptr;
}

/** 写类引用属性；值已经一样就返回 false（幂等，不脏控件/不刷日志）。 */
static bool WT_WriteClassProp(FProperty* P, UObject* Holder, UClass* NewClass)
{
    if (!P || !Holder || !NewClass)
    {
        return false;
    }
    if (WT_ReadClassProp(P, Holder) == NewClass)
    {
        return false;
    }
    if (FClassProperty* CP = CastField<FClassProperty>(P))
    {
        CP->SetObjectPropertyValue_InContainer(Holder, NewClass);
        return true;
    }
    if (FObjectProperty* OP = CastField<FObjectProperty>(P))
    {
        OP->SetObjectPropertyValue_InContainer(Holder, NewClass);
        return true;
    }
    return false;
}

/**
 * 在对象的类链上找「值是能量碎片类」的类属性（面板的成本类变量就在其中）。
 * 只收：① 当前值是 UFGPowerShardDescriptor 支系的类；② 该属性允许的类型是碎片基类的父类
 *       （否则我们的类写进去 = 类型不符，蓝图再 Cast 就是垃圾）。BP 变量只在自己那一层，
 *       所以 ExcludeSuper 逐层遍历，native 父类的同名属性不会被漏掉。
 */
static void WT_ScanShardClassProps(UObject* Obj, TArray<WTShardClassRef>& Out)
{
    UClass* ShardBase = UFGPowerShardDescriptor::StaticClass();
    if (!Obj || !ShardBase)
    {
        return;
    }
    for (UClass* C = Obj->GetClass(); C; C = C->GetSuperClass())
    {
        for (TFieldIterator<FProperty> It(C, EFieldIteratorFlags::ExcludeSuper); It; ++It)
        {
            FProperty* P = *It;
            UClass* Allowed = nullptr;
            if (FClassProperty* CP = CastField<FClassProperty>(P))
            {
                Allowed = CP->MetaClass;
            }
            else if (FObjectProperty* OP = CastField<FObjectProperty>(P))
            {
                Allowed = OP->PropertyClass;
            }
            if (!Allowed || !ShardBase->IsChildOf(Allowed))
            {
                continue;
            }
            UClass* Cur = Cast<UClass>(WT_ReadClassProp(P, Obj));
            if (!Cur || !Cur->IsChildOf(ShardBase))
            {
                continue;
            }
            WTShardClassRef R;
            R.Prop = P;
            R.Holder = Obj;
            Out.Add(R);
            WT_WARNF(TEXT("[shard-ui] cost class candidate: %s.%s = %s (allowed %s)"),
                     *Obj->GetClass()->GetName(), *P->GetName(), *Cur->GetName(), *Allowed->GetName());
        }
    }
}

// ===================== 原版槽位接口运行时反射 dump（1.0.8X） =====================
//
// 为什么必须 dump（2026-10-27 19:47 用户测完 1.0.8W 后的定论）：
//   三格角标已被我们手写成橙色 "1/1"，但图标仍是**灰剪影** = 原版「未满足」的图标。
//   一手取证三条：
//     ① 原版四态对照图（C:/SMLDev/shots/RF_原版超频_四态.png）：
//          已满足 = 彩色碎片图标 + 橙 "1/1"；未满足 = 灰剪影 + 深色 "1"。
//     ② `[shard-ui] invslot inner` 实锤图标控件 = `Image(mSlotBG)`，brush 是**材质实例**：
//          19:47:38 那次 = `MID_MM_UI_InventorySlot_Filtered_*`（灰化版）
//          19:46:37 那次 = `MID_MI_UI_InventorySlot_Part_*`（正常版）
//        ⇒ 灰度由材质决定，改 tint / RenderOpacity 一律无效（08T/08U/08W 三版已验证）。
//     ③ `TextBlock(mAmountText)` 原版写的是 '0' ⇒ 原版确实按 0 片渲染。
//   ⇒ 唯一正解是让原版自己走「满足」分支。刷新逻辑在 UMG 蓝图层，而蓝图函数也是
//     UFunction（可 ProcessEvent 调用），名字只存在于游戏的 UClass 里 —— pdb 只有 C++
//     符号，查不到 ⇒ 只能运行时反射 dump。这一版不改画面，只把靶子摆出来。
static FString WT_ReadPropBrief(FProperty* P, void* Container)
{
    if (!P || !Container)
    {
        return TEXT("?");
    }
    void* VP = P->ContainerPtrToValuePtr<void>(Container);
    if (FObjectProperty* OP = CastField<FObjectProperty>(P))
    {
        UObject* O = OP->GetObjectPropertyValue(VP);
        return O ? O->GetName() : TEXT("null");
    }
    if (FBoolProperty* BP = CastField<FBoolProperty>(P))
    {
        return BP->GetPropertyValue(VP) ? TEXT("true") : TEXT("false");
    }
    if (FNumericProperty* NP = CastField<FNumericProperty>(P))
    {
        if (NP->IsInteger())
        {
            return FString::Printf(TEXT("%lld"), static_cast<long long>(NP->GetSignedIntPropertyValue(VP)));
        }
        return FString::Printf(TEXT("%.4f"), NP->GetFloatingPointPropertyValue(VP));
    }
    if (FNameProperty* NmP = CastField<FNameProperty>(P))
    {
        return NmP->GetPropertyValue(VP).ToString();
    }
    if (FStrProperty* SP = CastField<FStrProperty>(P))
    {
        return SP->GetPropertyValue(VP);
    }
    if (FTextProperty* TP = CastField<FTextProperty>(P))
    {
        return TP->GetPropertyValue(VP).ToString();
    }
    return TEXT("<complex>");
}

/** 打一个 UMG 实例的属性（关键字过滤 + 当前值）与**全部** UFunction 签名。 */
static void WT_DumpObjectSurface(UObject* Obj, const TCHAR* Tag)
{
    if (!Obj)
    {
        return;
    }
    UClass* C = Obj->GetClass();
    WT_WARNF(TEXT("[shard-dump] ===== %s : %s ====="), Tag, *C->GetName());

    static const TCHAR* Keys[] = {
        TEXT("Icon"), TEXT("Item"), TEXT("Desc"), TEXT("Filter"), TEXT("Satisf"),
        TEXT("Amount"), TEXT("Brush"), TEXT("Color"), TEXT("Material"), TEXT("Cost"), TEXT("Stack")
    };
    for (TFieldIterator<FProperty> It(C); It; ++It)
    {
        FProperty* P = *It;
        if (P->HasAnyPropertyFlags(CPF_Parm))
        {
            continue;
        }
        const FString PN = P->GetName();
        bool bHit = false;
        for (const TCHAR* K : Keys)
        {
            if (PN.Contains(K))
            {
                bHit = true;
                break;
            }
        }
        if (!bHit)
        {
            continue;
        }
        WT_WARNF(TEXT("[shard-dump]   prop %s [%s] = %s"),
                 *PN, *P->GetClass()->GetName(), *WT_ReadPropBrief(P, Obj).Left(140));
    }

    int32 NFn = 0;
    for (TFieldIterator<UFunction> It(C); It; ++It)
    {
        UFunction* Fn = *It;
        FString Sig = Fn->GetName();
        Sig += TEXT("(");
        bool bFirst = true;
        for (TFieldIterator<FProperty> PIt(Fn); PIt && PIt->HasAnyPropertyFlags(CPF_Parm); ++PIt)
        {
            if (!bFirst)
            {
                Sig += TEXT(", ");
            }
            bFirst = false;
            if (PIt->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                Sig += TEXT("ret ");
            }
            Sig += FString::Printf(TEXT("%s %s"), *PIt->GetClass()->GetName(), *PIt->GetName());
        }
        Sig += TEXT(")");
        if (Fn->HasAnyFunctionFlags(FUNC_BlueprintCallable))
        {
            Sig += TEXT("  <BP-callable>");
        }
        if (Fn->HasAnyFunctionFlags(FUNC_Native))
        {
            Sig += TEXT("  <native>");
        }
        ++NFn;
        WT_WARNF(TEXT("[shard-dump]   fn %s"), *Sig);
    }
    WT_WARNF(TEXT("[shard-dump]   -> %s: %d funcs"), Tag, NFn);
}

/** 打材质实例的参数表（反射读，不依赖 UMaterialInstance 成员可见性）。 */
static void WT_DumpParamTable(UMaterialInstance* MInst, const TCHAR* ArrayName,
                              const TCHAR* Kind, int32 Limit)
{
    if (!MInst)
    {
        return;
    }
    FArrayProperty* AP = CastField<FArrayProperty>(
        UMaterialInstance::StaticClass()->FindPropertyByName(ArrayName));
    if (!AP)
    {
        return;
    }
    FStructProperty* EP = CastField<FStructProperty>(AP->Inner);
    if (!EP)
    {
        return;
    }
    FStructProperty* InfoProp = CastField<FStructProperty>(
        EP->Struct->FindPropertyByName(TEXT("ParameterInfo")));
    FNameProperty* NameProp = InfoProp
        ? CastField<FNameProperty>(InfoProp->Struct->FindPropertyByName(TEXT("Name"))) : nullptr;
    FProperty* ValProp = EP->Struct->FindPropertyByName(TEXT("ParameterValue"));

    FScriptArrayHelper H(AP, AP->ContainerPtrToValuePtr<void>(MInst));
    const int32 Shown = FMath::Min(H.Num(), Limit);
    for (int32 i = 0; i < Shown; ++i)
    {
        void* Elem = H.GetRawPtr(i);
        FString PName = TEXT("?");
        if (InfoProp && NameProp)
        {
            PName = NameProp->GetPropertyValue(InfoProp->ContainerPtrToValuePtr<void>(Elem)).ToString();
        }
        FString Val = TEXT("?");
        if (ValProp)
        {
            void* VP = ValProp->ContainerPtrToValuePtr<void>(Elem);
            if (FObjectProperty* OP = CastField<FObjectProperty>(ValProp))
            {
                UObject* O = OP->GetObjectPropertyValue(VP);
                Val = O ? O->GetName() : TEXT("null");
            }
            else if (FStructProperty* SP = CastField<FStructProperty>(ValProp))
            {
                if (SP->Struct == TBaseStructure<FLinearColor>::Get())
                {
                    const FLinearColor* LC = static_cast<const FLinearColor*>(VP);
                    Val = FString::Printf(TEXT("(%.2f,%.2f,%.2f,%.2f)"), LC->R, LC->G, LC->B, LC->A);
                }
                else
                {
                    Val = TEXT("<struct>");
                }
            }
            else if (FNumericProperty* NP = CastField<FNumericProperty>(ValProp))
            {
                Val = NP->IsInteger()
                    ? FString::Printf(TEXT("%lld"), static_cast<long long>(NP->GetSignedIntPropertyValue(VP)))
                    : FString::Printf(TEXT("%.4f"), NP->GetFloatingPointPropertyValue(VP));
            }
        }
        WT_WARNF(TEXT("[shard-dump]   mat.%s %s = %s"), Kind, *PName, *Val);
    }
    WT_WARNF(TEXT("[shard-dump]   mat.%s  (%d total, shown %d)"), Kind, H.Num(), Shown);
}

/** 图标的 brush 资源 + 父材质 + 全部材质参数。 */
static void WT_DumpIconMaterial(UWidget* W, const TCHAR* Tag)
{
    UImage* Img = Cast<UImage>(W);
    if (!Img)
    {
        return;
    }
    FStructProperty* ISP = CastField<FStructProperty>(
        UImage::StaticClass()->FindPropertyByName(TEXT("Brush")));
    if (!ISP)
    {
        return;
    }
    FSlateBrush* B = ISP->ContainerPtrToValuePtr<FSlateBrush>(Img);
    UObject* Res = B ? B->GetResourceObject() : nullptr;
    const FLinearColor IC = Img->GetColorAndOpacity();
    WT_WARNF(TEXT("[shard-dump] %s brush=%s (%s) size=%.0fx%.0f tint=(%.2f,%.2f,%.2f,%.2f)"),
             Tag, Res ? *Res->GetName() : TEXT("<null>"),
             Res ? *Res->GetClass()->GetName() : TEXT("-"),
             B ? B->ImageSize.X : 0.f, B ? B->ImageSize.Y : 0.f,
             IC.R, IC.G, IC.B, IC.A);

    UMaterialInstance* MInst = Cast<UMaterialInstance>(Res);
    if (!MInst)
    {
        return;
    }
    UMaterialInterface* Parent = nullptr;
    if (FObjectProperty* PP = CastField<FObjectProperty>(
            UMaterialInstance::StaticClass()->FindPropertyByName(TEXT("Parent"))))
    {
        Parent = Cast<UMaterialInterface>(
            PP->GetObjectPropertyValue(PP->ContainerPtrToValuePtr<void>(MInst)));
    }
    WT_WARNF(TEXT("[shard-dump] %s parent-material = %s"), Tag,
             Parent ? *Parent->GetName() : TEXT("<null>"));
    WT_DumpParamTable(MInst, TEXT("TextureParameterValues"), TEXT("tex"), 12);
    WT_DumpParamTable(MInst, TEXT("ScalarParameterValues"), TEXT("scalar"), 16);
    WT_DumpParamTable(MInst, TEXT("VectorParameterValues"), TEXT("vec"), 12);
}

/** 一块面板只跑一次：把 0 号槽的原版接口全部摊开。 */
static void WT_DumpSlotSurface(UUserWidget* SlotW)
{
    if (!SlotW)
    {
        return;
    }
    WT_DumpObjectSurface(SlotW, TEXT("CostSlotWrapper"));

    TArray<UWidget*> Deep;
    WT_CollectWidgetsDeep(SlotW, Deep, 4);
    UUserWidget* InvSlot = nullptr;
    for (UWidget* X : Deep)
    {
        UUserWidget* UW = Cast<UUserWidget>(X);
        if (UW && UW->GetName().StartsWith(TEXT("Widget_InventorySlot")))
        {
            InvSlot = UW;
            break;
        }
    }
    if (!InvSlot)
    {
        WT_WARNF(TEXT("[shard-dump] no Widget_InventorySlot inside %s"), *SlotW->GetName());
        return;
    }
    WT_DumpObjectSurface(InvSlot, TEXT("InventorySlot"));
    if (!InvSlot->WidgetTree)
    {
        return;
    }
    TArray<UWidget*> Inner;
    InvSlot->WidgetTree->GetAllWidgets(Inner);
    for (UWidget* Y : Inner)
    {
        if (UImage* II = Cast<UImage>(Y))
        {
            WT_DumpIconMaterial(II, *FString::Printf(TEXT("icon.%s"), *Y->GetName()));
        }
    }
}

/**
 * 每格一次：把这一格（wrapper 层 + InventorySlot 层）**所有** Image 摊开 —— 名字 / brush 资源 /
 * 可见性 / tint / 不透明度 / 尺寸。
 *
 * 1.0.8AF 为什么要这个：08AE 的日志已经证明 `mSlotBgMaterial` 和 `mSlotBG` 的 brush 都已经是
 * `MID_MI_UI_InventorySlot_Part_*`（原版彩色材质）、tint 全白、参数齐全，可屏幕还是灰剪影
 * ⇒ 灰**不是** `mSlotBG` 这一张图造成的。要么同一格上还叠着别的 Image（例如独立的图标层，
 * 它被 `SetSlotColor()` 上了深灰），要么 Slate 侧拿的还是旧 brush（属性改了但没通知重绘）。
 * 这一行 dump 就是为了把这两种可能一次分开。
 */
static void WT_DumpSlotImagesAll(UUserWidget* SlotW, int32 Idx)
{
    if (!SlotW)
    {
        return;
    }
    TArray<UWidget*> Deep;
    WT_CollectWidgetsDeep(SlotW, Deep, 6);
    int32 n = 0;
    for (UWidget* X : Deep)
    {
        UImage* II = Cast<UImage>(X);
        if (!II)
        {
            continue;
        }
        ++n;
        FStructProperty* ISP = CastField<FStructProperty>(
            UImage::StaticClass()->FindPropertyByName(TEXT("Brush")));
        FSlateBrush* B   = ISP ? ISP->ContainerPtrToValuePtr<FSlateBrush>(II) : nullptr;
        UObject*     Res = B ? B->GetResourceObject() : nullptr;
        const FLinearColor IC = II->GetColorAndOpacity();
        WT_WARNF(TEXT("[shard-img] slot%d #%d %s res=%s(%s) vis=%d tint=(%.2f,%.2f,%.2f,%.2f) op=%.2f size=%.0fx%.0f"),
                 Idx, n, *X->GetName(),
                 Res ? *Res->GetName() : TEXT("<null>"),
                 Res ? *Res->GetClass()->GetName() : TEXT("-"),
                 static_cast<int32>(X->GetVisibility()),
                 IC.R, IC.G, IC.B, IC.A, X->GetRenderOpacity(),
                 B ? B->ImageSize.X : 0.f, B ? B->ImageSize.Y : 0.f);
    }
        WT_WARNF(TEXT("[shard-img] slot%d total Images in subtree = %d"), Idx, n);
    }

    // ★2026-11-02 wt.DumpBuildMenu：枚举当前在屏全部 UMG，找出建造/制作台成本格
    // （Widget_CostSlotWrapper_C）并摊开其图标/底色/tint/可见性，用于锁定「来源色」究竟存在
    // 哪个属性里（不猜控件名）。开着建造菜单或制作台界面时执行，把日志贴回。
    static void WT_DumpBuildMenu(UWorld* World)
    {
        if (!World)
        {
            WT_WARNF(TEXT("[dump-buildmenu] no world."));
            return;
        }
        TArray<UUserWidget*> All;
        UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, All, UUserWidget::StaticClass(), false);
        WT_WARNF(TEXT("[dump-buildmenu] total user widgets = %d"), All.Num());

        // 第一遍：列出疑似「建造菜单 / 配方 / 成本 / 制作」的根控件类名，给用户看真实词汇。
        int32 nRoot = 0;
        for (UUserWidget* W : All)
        {
            if (!W) { continue; }
            const FString CN = W->GetClass()->GetName();
            if (CN.Contains(TEXT("Build")) || CN.Contains(TEXT("Recipe")) ||
                CN.Contains(TEXT("Cost")) || CN.Contains(TEXT("Craft")) ||
                CN.Contains(TEXT("HUD")) || CN.Contains(TEXT("Menu")))
            {
                WT_WARNF(TEXT("[dump-buildmenu] ROOT candidate: %s (instance '%s') vis=%d"),
                         *CN, *W->GetName(), static_cast<int32>(W->GetVisibility()));
                ++nRoot;
            }
        }

        // 第二遍：从每个根深搜 Widget_CostSlotWrapper_C，摊开其全部 Image 的 tint/brush/可见性。
        int32 nSlot = 0;
        for (UUserWidget* W : All)
        {
            if (!W || !W->WidgetTree) { continue; }
            TArray<UWidget*> Deep;
            WT_CollectWidgetsDeep(W, Deep, 6);
            for (UWidget* D : Deep)
            {
                UUserWidget* UW = Cast<UUserWidget>(D);
                if (UW && UW->GetClass()->GetName() == TEXT("Widget_CostSlotWrapper_C"))
                {
                    WT_WARNF(TEXT("[dump-buildmenu] COST-SLOT inside root '%s'"), *W->GetClass()->GetName());
                    WT_DumpSlotImagesAll(UW, nSlot);
                    WT_DumpObjectSurface(UW, TEXT("CostSlotWrapper"));
                    ++nSlot;
                }
            }
        }
        WT_WARNF(TEXT("[dump-buildmenu] root candidates=%d, cost-slot wrappers dumped=%d"), nRoot, nSlot);
    }

    static FAutoConsoleCommand GDumpBuildMenuCmd(
        TEXT("wt.DumpBuildMenu"),
        TEXT("Remote_Factory: enumerate on-screen UMG and dump build/craft cost-slot widget structure (tint/brush/visibility) to locate the source-color property."),
        FConsoleCommandDelegate::CreateLambda([]()
        {
            UWorld* World = nullptr;
            if (GEngine)
            {
                for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
                {
                    if (UWorld* W = Ctx.World())
                    {
                        if (W->WorldType == EWorldType::Game || W->WorldType == EWorldType::PIE)
                        {
                            World = W;
                            break;
                        }
                    }
                }
                if (!World)
                {
                    for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
                    {
                        if (UWorld* W = Ctx.World()) { World = W; break; }
                    }
                }
            }
            WT_DumpBuildMenu(World);
        }));

// ===================== 原版成本类 CDO 接管（1.0.8X 正解） =====================
//
// ★真正的病根不是 UI，是**类的继承关系**：
//   原版超频面板的成本格把物品类写死成 `Desc_CrystalShard_C`（日志实锤
//   `cost class candidate: Widget_CostSlotWrapper_C.itemDescriptor = Desc_CrystalShard_C`）。
//   原版画图标、数数量都拿这个类去 `GetNumItems(Class)` / `HasItems(Class)`，语义是
//   `ItemClass->IsA(Class)`：
//     · 原版碎片 `Desc_CrystalShard_C` → IsA 成立 → 数得到 1 → 彩色图标 + 橙 "1/1"
//     · 我们的 `UWTUpgradePowerShardDescriptor` 与它是**兄弟**（父类同为 UFGPowerShardDescriptor）
//       → IsA 不成立 → 数到 0 → 灰剪影 + 深色 "1"（原版「未满足」态）
//   这解释了为什么 08T/08U/08W 改 tint / RenderOpacity / 手写角标全都没用：
//   我们改的是**结果**，原版每一帧按「0 个」重画的才是**原因**。
//
// ⇒ 正解：把成本格的物品类换成**我们的碎片类**，让原版按我们的类去数。
//   原版会自己画出彩色图标 + 橙 "1/1"，插入/取出的判定也同时变成我们的碎片 ——
//   一个改动，渲染 / 插入 / 取出三件事一起接上，不再需要任何界面补丁。
//
// ★为什么必须改在 **CDO** 而不是实例：
//   面板每次打开都是新建实例，实例上的 BP 变量在 `NativeConstruct` 阶段就被 DropArea
//   拿去初始化槽了（我们 tick 到的时候已经是快照）。08T 在实例上改得到
//   「写进去了但画面不变」，正是因为改得太晚，而且关面板就丢。
//   改 CDO ⇒ 新实例出生时默认值就是我们的类 ⇒ 初始化那一刻原版就拿对了。
//
// ★安全阀：只改 `Widget_CostSlotWrapper_C` 这一个默认值，不碰任何库存/存档数据。
//   `WT_SHARD_COST_CDO = false` 即完全还原（CDO 只活在内存，重启游戏必然复位）。
static constexpr bool WT_SHARD_COST_CDO = true;

static void WT_InstallCostClassCDO()
{
    static bool bDone = false;
    if (bDone || !WT_SHARD_COST_CDO)
    {
        return;
    }
    bDone = true;

    UClass* CostSlotCls = nullptr;
    for (TObjectIterator<UClass> It; It; ++It)
    {
        if (It->GetName() == TEXT("Widget_CostSlotWrapper_C"))
        {
            CostSlotCls = *It;
            break;
        }
    }
    if (!CostSlotCls)
    {
        static bool bWarned = false;
        if (!bWarned)
        {
            bWarned = true;
            WT_WARNF(TEXT("[shard-cdo] Widget_CostSlotWrapper_C not loaded yet -- retry on next panel open"));
        }
        bDone = false;                     // 下次面板打开再试（首次可能在类加载前）
        return;
    }
    FProperty* P = CostSlotCls->FindPropertyByName(TEXT("itemDescriptor"));
    if (!P)
    {
        WT_ERRF(TEXT("[shard-cdo] %s has no itemDescriptor -- CDO override skipped"),
                *CostSlotCls->GetName());
        return;
    }
    UObject* CDO  = CostSlotCls->GetDefaultObject();
    UClass*  Ours = UWTUpgradePowerShardDescriptor::StaticClass();
    const FString Before = WT_ReadPropBrief(P, CDO);
    const bool bWrote = WT_WriteClassProp(P, CDO, Ours);
    WT_WARNF(TEXT("[shard-cdo] %s.itemDescriptor: %s -> %s (wrote=%d)"),
             *CostSlotCls->GetName(), *Before, *WT_ReadPropBrief(P, CDO), bWrote ? 1 : 0);
}

/**
 * 1.0.8AH：08AF~08AG 那批**深度诊断**（控件树全景 / 每格所有 Image 摊开 / 内存材质清单）
 * 已经把该问的都问完了（唯一可见 Image = mSlotBG；灰不是材质/叠加层/Slate 刷新），
 * 默认关掉降噪提速。万一以后又出画面问题，把它翻成 true 就能一次拿回全套现场。
 */
static constexpr bool WT_SHARD_DIAG_DEEP = false;

/**
 * 1.0.8AI：「碎片取不下来」专项诊断（交互委托绑定 + 悬停格）。
 * 显示已经修好，但取出这条链路一次都没被取证过 —— 这个开关把原版给每格装了什么处理器
 * 直接打出来。定位完成后关掉。
 */
static constexpr bool WT_SHARD_DIAG_CLICK = true;

/**
 * 原版是否**已经自己**把这一格画成「成本已满足」（1.0.8X，1.0.8AH 判据修正）。
 *
 * 判据：这一格里任一文本块 == "1/1" 即算满足。
 *   · 原版满足 → `mStackSizeLbl` 写 "1/1" + 整条橙条 + 彩色晶体（四态对照图实锤）
 *   · 原版没满足 → `mStackSizeLbl` 写 "1"、橙条只画 62%（日志实锤 pct=0.62）
 * ★1.0.8AH 修：原来只认 `mAmountText`，可那个文本块**永远**是 '1'（快照实锤 '1'v4），
 *   拿它判会一直 false ⇒ 08AG 每格都多调 SetTextboxFormating/SetSlotColor/SetIconBrush
 *   三次（日志 `tb=1 sc=1 ib=1`），且没法用它做「已满足就撒手」的门闩。
 *   真正的满足标志在 `mStackSizeLbl`（快照里 '1/1'v3 = 满足 / '1'v0 = 未满足）。
 * ★CDO 接管成功后原版会自己画对，此时我们**一个字节都不该再写**，否则双角标打架。
 *   谁对听谁的：原版对了就交还原版。
 */
static bool WT_VanillaSatisfied(UUserWidget* SlotW)
{
    if (!SlotW || !SlotW->WidgetTree)
    {
        return false;
    }
    TArray<UWidget*> Deep;
    WT_CollectWidgetsDeep(SlotW, Deep, 4);
    bool bText1of1 = false;
    bool bBarFull  = false;
    bool bTextBg   = false;
    for (UWidget* X : Deep)
    {
        if (!X)
        {
            continue;
        }
        const FString N = X->GetName();
        if (N == TEXT("mStackSizeLbl") || N == TEXT("mAmountText"))
        {
            if (UTextBlock* T = Cast<UTextBlock>(X))
            {
                if (T->GetText().ToString().TrimStartAndEnd().Equals(TEXT("1/1")))
                {
                    bText1of1 = true;
                }
            }
        }
        else if (N == TEXT("mTextBG"))
        {
            // 08AG 实测：满足态的橙条底是显示的（`mTextBG=显示 '1/1'`），未满足是隐藏的。
            const ESlateVisibility V = X->GetVisibility();
            bTextBg = (V != ESlateVisibility::Collapsed && V != ESlateVisibility::Hidden);
        }
        else if (UProgressBar* B = Cast<UProgressBar>(X))
        {
            if (B->GetPercent() >= 0.99f)
            {
                bBarFull = true;
            }
        }
    }
    // ★1.0.8AL 判据必须「文本 + 视觉」双条件（用户 00:24 实测「插入碎片后前两个位置变灰」）：
    //   日志实锤 `slot0 afterSetup: bar[pct=0.62] imgs{mTextBG=v0} txt{mStackSizeLbl='1/1'}` ——
    //   原版正门把**文本**改成了 '1/1'，可橙条还停在 62%、橙条底还是隐藏。
    //   旧判据只看文本 ⇒ 认为「已满足」⇒ 撒手 ⇒ 那一格永久停在灰剪影，只有重开面板
    //   （额度清零、重新走一遍）才好。现在必须橙条满或橙条底可见，才算真满足。
    return bText1of1 && (bBarFull || bTextBg);
}

/**
 * 改完成本类之后，**主动触发原版自己的重算**（1.0.8X）。
 *
 * 为什么必须有这一步：槽的渲染状态是面板 `NativeConstruct` 时按当时的 `itemDescriptor`
 * 算完存下的快照。08T 只改 BP 变量得到「写进去了但画面不变」，就是因为没人叫原版重算。
 *
 * ★只调用**无入参**且名字是 Update/Refresh/Setup/Init/Build 前缀的 UFunction：
 *   无参 ⇒ 不需要构造参数结构，`ProcessEvent` 传 nullptr 即可，零崩溃风险；
 *   前缀白名单 ⇒ 不会误调 `SetXxx(…)` / `RemoveItem(…)` 这类会改数据或要参数的函数。
 * ★每个控件实例只调一次（TSet 去重），防 `Update → Update` 自我递归。
 */
static constexpr bool WT_SHARD_VANILLA_REFRESH = true;

static void WT_TryVanillaRefresh(UUserWidget* Root, const TCHAR* Tag)
{
    if (!Root || !WT_SHARD_VANILLA_REFRESH)
    {
        return;
    }
    static TSet<TWeakObjectPtr<UUserWidget>> Called;
    if (Called.Contains(Root))
    {
        return;
    }
    Called.Add(Root);

    TArray<UWidget*> Deep;
    WT_CollectWidgetsDeep(Root, Deep, 4);
    Deep.Add(Root);

    // ★只留「刷新」语义的前缀：Init/Build/Reconstruct 这类会重建控件树，
    //   在面板已经构造完之后再调等于拆了重装，风险远大于收益 —— 一律不碰。
    static const TCHAR* Verbs[] = {
        TEXT("Update"), TEXT("Refresh"), TEXT("Notify"), TEXT("Setup")
    };

    for (UWidget* X : Deep)
    {
        UUserWidget* UW = Cast<UUserWidget>(X);
        if (!UW)
        {
            continue;
        }
        UClass* C = UW->GetClass();
        // ★只碰这两个控件类：面板里其他 UMG 子控件的无参 Update* 语义未知，不冒险。
        const FString CN = C->GetName();
        if (!CN.StartsWith(TEXT("Widget_InventorySlot")) &&
            !CN.StartsWith(TEXT("Widget_CostSlotWrapper")))
        {
            continue;
        }
        int32 Invoked = 0;
        for (TFieldIterator<UFunction> It(C); It; ++It)
        {
            UFunction* Fn = *It;
            if (!Fn || Fn->ParmsSize != 0)
            {
                continue;                    // 有参数结构的一律不碰
            }
            const FString FN = Fn->GetName();
            bool bHit = false;
            for (const TCHAR* V : Verbs)
            {
                if (FN.StartsWith(V))
                {
                    bHit = true;
                    break;
                }
            }
            if (!bHit)
            {
                continue;
            }
            // ★必须给一个**有效**的参数缓冲：蓝图函数走脚本 VM，传 nullptr 时部分路径
            //   会解引用 Parms（即使 ParmsSize==0 也不安全）。分配 Max(ParmsSize,1) 并清零，
            //   对零参函数等价于一个空结构体，绝对安全。
            const int32 BufSize = FMath::Max<int32>(static_cast<int32>(Fn->ParmsSize), 1);
            uint8* Parms = static_cast<uint8*>(FMemory::Malloc(BufSize, 16));
            if (!Parms)
            {
                continue;
            }
            FMemory::Memzero(Parms, BufSize);
            UW->ProcessEvent(Fn, Parms);
            FMemory::Free(Parms);
            ++Invoked;
            WT_WARNF(TEXT("[shard-call] %s -> %s::%s()"), Tag, *C->GetName(), *FN);
        }
        if (Invoked > 0)
        {
            WT_WARNF(TEXT("[shard-call] %s -> %s: %d func(s) invoked"), Tag, *C->GetName(), Invoked);
        }
    }
}

/** 把一个已占用槽翻成「成本已满足」态（A 路）。bDump=true 时先采样再改并打一行。 */
static void WT_MarkCostSlotSatisfied(UUserWidget* SlotW, int32 Idx, int32 Want, bool bDump)
{
    if (!SlotW)
    {
        return;
    }
    // ★1.0.8X 自愈：原版已经自己画出 "1/1" ⇒ 交还原版，一格都不碰。
    if (WT_VanillaSatisfied(SlotW))
    {
        static TSet<FString> Logged;
        const FString Key = SlotW->GetName();
        if (!Logged.Contains(Key))
        {
            Logged.Add(Key);
            WT_WARNF(TEXT("[shard-ui] %s already vanilla-satisfied (mAmountText='1/1') -- leaving it to vanilla"),
                     *Key);
        }
        return;
    }
    TArray<UWidget*> Deep;
    WT_CollectWidgetsDeep(SlotW, Deep, 4);

    UTextBlock*   Label     = nullptr;   // 角标数字 / 成本 "1/1" 文字
    UProgressBar* Bar       = nullptr;   // ProgressBar_0：原版成本条
    UImage*       TextBg    = nullptr;   // mTextBG：角标圆角深底（成本态由橙条取代）
    UImage*       TextboxBg = nullptr;   // TextBackground：背包数字底（成本态原版是收起的）
    UUserWidget*  InvSlot   = nullptr;   // Widget_InventorySlot_C：图标宿主（底色 214/232 在它里面）
    UWidget*      IconImg   = nullptr;
    for (UWidget* X : Deep)
    {
        const FString N = X->GetName();
        if (UTextBlock* T = Cast<UTextBlock>(X))
        {
            if (!Label && N == TEXT("mStackSizeLbl")) { Label = T; }
        }
        else if (UProgressBar* B = Cast<UProgressBar>(X))
        {
            if (!Bar) { Bar = B; }
        }
        else if (UImage* I = Cast<UImage>(X))
        {
            if (N == TEXT("mTextBG"))           { TextBg = I; }
            else if (N == TEXT("TextBackground")) { TextboxBg = I; }
        }
        else if (UUserWidget* UW = Cast<UUserWidget>(X))
        {
            if (!InvSlot && UW->GetName().StartsWith(TEXT("Widget_InventorySlot"))) { InvSlot = UW; }
        }
    }

    // ★先采样、后修改：日志里必须是「改之前」的值，否则永远看不出哪一格没被满足。
    FString Before;
    if (bDump)
    {
        Before = FString::Printf(
            TEXT("bar=%s vis=%d pct=%.2f | lbl='%s' vis=%d | mTextBG vis=%d | textboxBg vis=%d | invSlot=%s"),
            Bar ? *Bar->GetName() : TEXT("<none>"),
            Bar ? static_cast<int32>(Bar->GetVisibility()) : -1,
            Bar ? Bar->GetPercent() : -1.f,
            Label ? *Label->GetText().ToString() : TEXT("<none>"),
            Label ? static_cast<int32>(Label->GetVisibility()) : -1,
            TextBg ? static_cast<int32>(TextBg->GetVisibility()) : -1,
            TextboxBg ? static_cast<int32>(TextboxBg->GetVisibility()) : -1,
            InvSlot ? *InvSlot->GetName() : TEXT("<none>"));
    }

    if (Bar)
    {
        Bar->SetPercent(1.f);
        Bar->SetVisibility(ESlateVisibility::HitTestInvisible);
    }
    if (TextBg)
    {
        TextBg->SetVisibility(ESlateVisibility::Collapsed);   // 角标深底让位给橙条
    }
    if (Label)
    {
        Label->SetText(FText::FromString(TEXT("1/1")));
        Label->SetJustification(ETextJustify::Center);
        Label->SetVisibility(ESlateVisibility::HitTestInvisible);
        if (UOverlaySlot* OS = Cast<UOverlaySlot>(Label->Slot))
        {
            OS->SetHorizontalAlignment(HAlign_Fill);          // 原来贴右下角，改成横跨橙条
        }
    }
    // 只抬 alpha（不动 RGB），空槽不进来 ⇒ 幽灵槽保持原版。
    WT_ForceOpaque(SlotW);
    for (UWidget* X : Deep)
    {
        WT_ForceOpaque(X);
    }

    if (bDump)
    {
        WT_WARNF(TEXT("[shard-ui] cost satisfied %d/%d %s | before: %s"),
                 Idx, Want, *SlotW->GetName(), *Before);

        // ★1.0.8T 下一版的靶子：图标宿主里那层「格底色」到底是哪个控件（原版 214/232 两档）。
        //   ★1.0.8X：改成**每格都打**——三格的图标材质可能不同（Filtered / Part），
        //   只有三行并排才能看出「为什么这一格灰、那一格彩」。
        if (InvSlot && InvSlot->WidgetTree)
        {
            TArray<UWidget*> Inner;
            InvSlot->WidgetTree->GetAllWidgets(Inner);
            FString Desc;
            int32 Budget = 16;
            for (UWidget* In : Inner)
            {
                if (!In || Budget-- <= 0)
                {
                    continue;
                }
                Desc += FString::Printf(TEXT(" %s(%s)vis=%d"), *In->GetClass()->GetName(),
                                        *In->GetName(), static_cast<int32>(In->GetVisibility()));
                if (UImage* II = Cast<UImage>(In))
                {
                    if (FStructProperty* ISP = CastField<FStructProperty>(
                            UImage::StaticClass()->FindPropertyByName(TEXT("Brush"))))
                    {
                        FSlateBrush* IB = ISP->ContainerPtrToValuePtr<FSlateBrush>(II);
                        UObject* IRes = IB ? IB->GetResourceObject() : nullptr;
                        const FLinearColor IC = II->GetColorAndOpacity();
                        Desc += FString::Printf(TEXT("[%s]size=%.0fx%.0f tint=(%.2f,%.2f,%.2f,%.2f)"),
                                                IRes ? *IRes->GetName() : TEXT("<null>"),
                                                IB ? IB->ImageSize.X : 0.f, IB ? IB->ImageSize.Y : 0.f,
                                                IC.R, IC.G, IC.B, IC.A);
                    }
                }
                else if (UTextBlock* IT = Cast<UTextBlock>(In))
                {
                    Desc += FString::Printf(TEXT("'%s'"), *IT->GetText().ToString());
                }
                else if (UBorder* IBd = Cast<UBorder>(In))
                {
                    const FLinearColor BC = IBd->GetBrushColor();
                    Desc += FString::Printf(TEXT("brushColor=(%.2f,%.2f,%.2f,%.2f)"),
                                            BC.R, BC.G, BC.B, BC.A);
                }
            }
            WT_WARNF(TEXT("[shard-ui] invslot inner(%d):%s"), Inner.Num(), *Desc);
        }
    }
}

// ===================== 格底色「高亮窗」（1.0.8U） =====================
//
// 现象（用户 2026-10-27 17:23 截图，3 片 400%）：第 1 格底板偏深、第 2、3 格偏浅灰。
//
// 原版四态实测（1080p 真实像素，格内左上角 10x50 区域均值；静态、不呼吸）：
//   N=0 → [214,232,232]   N=1 → [214,214,232]
//   N=2 → [232,214,214]   N=3 → [232,232,214]
//   ⇒ 深色 214 的集合 = { clamp(N-1,0,2), clamp(N,0,2) }（最后插满那格 + 下一个空格）
//   我方恒为 [214,232,232]，因为原版把片数当 0（成本类数不到我们的碎片）。
//
// ★不猜控件名：同一时刻「第 1 格（深）」与「最后一格（浅）」里**同名控件**的
//   不透明度 / 颜色之差，就是这两档的取值本身。抽出来，再按上面的集合逐格写。
struct FWTVisProbe
{
    FName        Name;
    float        Opacity = 1.f;
    FLinearColor Tint    = FLinearColor::White;
    bool         bTint   = false;      // 只有 UImage / UBorder 有可写颜色
};

struct FWTVisDiff
{
    FName        Name;
    float        OpHi = 1.f;
    float        OpLo = 1.f;
    FLinearColor THi  = FLinearColor::White;
    FLinearColor TLo  = FLinearColor::White;
    bool         bTint = false;
};

[[maybe_unused]] static void WT_CollectVis(UUserWidget* SlotW, TArray<FWTVisProbe>& Out)
{
    TArray<UWidget*> Deep;
    WT_CollectWidgetsDeep(SlotW, Deep, 4);
    for (UWidget* X : Deep)
    {
        if (!X)
        {
            continue;
        }
        FWTVisProbe P;
        P.Name    = X->GetFName();
        P.Opacity = X->GetRenderOpacity();
        if (UImage* I = Cast<UImage>(X))
        {
            P.Tint  = I->GetColorAndOpacity();
            P.bTint = true;
        }
        else if (UBorder* Bd = Cast<UBorder>(X))
        {
            P.Tint  = Bd->GetBrushColor();
            P.bTint = true;
        }
        Out.Add(P);
    }
}

// ★1.0.8W 起**不再调用**：它把「格底色深浅」误抓成 Widget_InventorySlot 的 RenderOpacity，
//   会把已占用格压回 0.40（= 原版空槽态）⇒ 图标发灰 + 那格点不动。
//   结论保留作考古，已占用格改由 WT_SyncShardSlots 末尾统一强制不透明。
[[maybe_unused]] static void WT_ApplySlotHighlight(const TMap<int32, UUserWidget*>& Slots, int32 Want, bool bDump)
{
    UUserWidget* const* HiP = Slots.Find(0);
    if (!HiP || !*HiP)
    {
        return;
    }
    UUserWidget* LoW = nullptr;
    for (int32 i = 2; i >= 1; --i)
    {
        UUserWidget* const* F = Slots.Find(i);
        if (F && *F)
        {
            LoW = *F;
            break;
        }
    }
    if (!LoW)
    {
        return;
    }

    TArray<FWTVisProbe> Hi, Lo;
    WT_CollectVis(*HiP, Hi);
    WT_CollectVis(LoW, Lo);

    TArray<FWTVisDiff> Diffs;
    for (const FWTVisProbe& A : Hi)
    {
        const FWTVisProbe* B = Lo.FindByPredicate([&A](const FWTVisProbe& Q) { return Q.Name == A.Name; });
        if (!B || B->bTint != A.bTint)
        {
            continue;
        }
        const bool bOp  = FMath::Abs(A.Opacity - B->Opacity) > 0.02f;
        const bool bTin = A.bTint && (FMath::Abs(A.Tint.R - B->Tint.R) > 0.02f
                                   || FMath::Abs(A.Tint.G - B->Tint.G) > 0.02f
                                   || FMath::Abs(A.Tint.B - B->Tint.B) > 0.02f);
        if (!bOp && !bTin)
        {
            continue;
        }
        FWTVisDiff D;
        D.Name  = A.Name;
        D.OpHi  = A.Opacity;
        D.OpLo  = B->Opacity;
        D.THi   = A.Tint;
        D.TLo   = B->Tint;
        D.bTint = A.bTint;
        Diffs.Add(D);
    }

    if (bDump)
    {
        FString S;
        for (const FWTVisDiff& D : Diffs)
        {
            S += FString::Printf(TEXT(" %s[op %.2f/%.2f"), *D.Name.ToString(), D.OpHi, D.OpLo);
            if (D.bTint)
            {
                S += FString::Printf(TEXT(" tint(%.2f,%.2f,%.2f)/(%.2f,%.2f,%.2f)"),
                                     D.THi.R, D.THi.G, D.THi.B, D.TLo.R, D.TLo.G, D.TLo.B);
            }
            S += TEXT("]");
        }
        WT_WARNF(TEXT("[shard-ui] hl diff(%d) %s vs %s:%s"),
                 Diffs.Num(), *(*HiP)->GetName(), *LoW->GetName(), *S);
    }
    if (Diffs.Num() == 0)
    {
        return;
    }

    for (int32 i = 0; i < Want; ++i)
    {
        UUserWidget* const* Found = Slots.Find(i);
        if (!Found || !*Found)
        {
            continue;
        }
        const bool bHi = (i == FMath::Clamp(Want - 1, 0, 2)) || (i == FMath::Clamp(Want, 0, 2));
        TArray<UWidget*> Deep;
        WT_CollectWidgetsDeep(*Found, Deep, 4);
        for (UWidget* X : Deep)
        {
            if (!X)
            {
                continue;
            }
            const FWTVisDiff* D = Diffs.FindByPredicate(
                [X](const FWTVisDiff& Q) { return Q.Name == X->GetFName(); });
            if (!D)
            {
                continue;
            }
            X->SetRenderOpacity(bHi ? D->OpHi : D->OpLo);
            if (!D->bTint)
            {
                continue;
            }
            if (UImage* I = Cast<UImage>(X))
            {
                I->SetColorAndOpacity(bHi ? D->THi : D->TLo);
            }
            else if (UBorder* Bd = Cast<UBorder>(X))
            {
                Bd->SetBrushColor(bHi ? D->THi : D->TLo);
            }
        }
    }
}

// ===================== 碎片取回按钮（1.0.8V） =====================
//
// 【为什么必须自己接管】原版超频面板的槽 `Widget_CostSlotWrapper_C` 把成本物品写死成原版碎片
//   `Desc_CrystalShard_C`（日志实锤：`cost class candidate: ...itemDescriptor = Desc_CrystalShard_C`）。
//   点击槽位取出碎片时原版按**那个具体类**去潜在库存里数/移；我们的碎片是它的子类，
//   按具体类永远数不到 ⇒ 插得进去、取不下来（用户 2026-10-27 实测「取不下来」）。
//   08T 改 BP 变量（B 路）无效：画面不变 + 依旧取不下来 ⇒ 取出链里另有写死的类引用。
//
// 【做法】在「被我们碎片占用的格」上叠一枚**全透明**按钮，宿主 = 图标宿主
//   `Widget_InventorySlot_C` 内部的 CanvasPanel `mContainer`（该控件名由 08T 的
//   `invslot inner(7)` 日志实锤）。按钮自己把碎片从潜在库存转进玩家背包（见
//   Remote_FactoryShardTakeButton.cpp）。
//   · 原版碎片 / 空槽**不挂按钮**（Want 之外的格一律 RemoveFromParent）⇒ 原版取出链零影响。
//   · SButton 不实现 OnDrop ⇒ 玩家从背包**拖入**碎片仍由原版槽处理，插入不受影响。
//   · 「从槽拖出」会被按钮吃掉 —— 那正是原版本来做不到的动作，已由点击替代。

// ★懒初始化：全局 static FName 会在静态初始化阶段构造，那时 FNamePool 未必就绪。
//   函数内 static 保证第一次真正用到（进游戏、有面板）时才建立。
static const FName& WT_TakeBtnName()
{
    static const FName N(TEXT("WT_ShardTakeBtn"));
    return N;
}

/** 挂载宿主：必须是**自己的 WidgetTree** 里的 UPanelWidget（跨 tree 挂控件会不渲染/崩）。 */
struct FWTMountHost
{
    UPanelWidget* Panel = nullptr;
    UWidgetTree*  Tree  = nullptr;
};

static FWTMountHost WT_FindMountHost(UUserWidget* SlotW)
{
    FWTMountHost R;
    if (!SlotW)
    {
        return R;
    }

    // ① 槽自己的控件树：根面板优先（同 tree、生命周期最稳、命中区最大）
    //   ★只认 CanvasPanel / Overlay：VerticalBox 之类的子项按内容撑高，全屏锚点会塌成
    //     0 尺寸 ⇒ 按钮点不到。这类根一律走 ②。
    if (SlotW->WidgetTree)
    {
        UPanelWidget* RootP = Cast<UPanelWidget>(SlotW->WidgetTree->RootWidget);
        if (RootP && (Cast<UCanvasPanel>(RootP) || Cast<UOverlay>(RootP)))
        {
            R.Panel = RootP;
            R.Tree  = SlotW->WidgetTree;
            return R;
        }
    }

    // ② 退到图标宿主 `Widget_InventorySlot_C` 的 `mContainer`（08T 日志实锤：CanvasPanel(mContainer)）
    TArray<UWidget*> Deep;
    WT_CollectWidgetsDeep(SlotW, Deep, 4);
    for (UWidget* X : Deep)
    {
        UUserWidget* UW = Cast<UUserWidget>(X);
        if (!UW || !UW->WidgetTree || !UW->GetName().StartsWith(TEXT("Widget_InventorySlot")))
        {
            continue;
        }
        TArray<UWidget*> Inner;
        UW->WidgetTree->GetAllWidgets(Inner);
        for (UWidget* Y : Inner)
        {
            UPanelWidget* P = Cast<UPanelWidget>(Y);
            if (P && Y->GetFName() == TEXT("mContainer"))
            {
                R.Panel = P;
                R.Tree  = UW->WidgetTree;
                return R;
            }
        }
        UPanelWidget* RootP = Cast<UPanelWidget>(UW->WidgetTree->RootWidget);
        if (RootP && (Cast<UCanvasPanel>(RootP) || Cast<UOverlay>(RootP)))
        {
            R.Panel = RootP;
            R.Tree  = UW->WidgetTree;
            return R;
        }
    }
    return R;
}

/** 摘掉所有同名按钮（槽自己 + 所有嵌套 UserWidget 的 tree 各查一次）。 */
static void WT_RemoveTakeButtons(UUserWidget* SlotW)
{
    if (!SlotW)
    {
        return;
    }
    auto KillOne = [](UWidgetTree* T) {
        if (!T)
        {
            return;
        }
        if (UWidget* W = T->FindWidget(WT_TakeBtnName()))
        {
            W->RemoveFromParent();
        }
    };
    KillOne(SlotW->WidgetTree);
    TArray<UWidget*> Deep;
    WT_CollectWidgetsDeep(SlotW, Deep, 4);
    for (UWidget* X : Deep)
    {
        if (UUserWidget* UW = Cast<UUserWidget>(X))
        {
            KillOne(UW->WidgetTree);
        }
    }
}

/** 幂等：需要就挂、不需要就摘。每拍调用，成本 = 一次 FindWidget。 */
static void WT_EnsureTakeButton(UUserWidget* SlotW, AFGBuildableFactory* Factory, int32 SlotIdx, bool bMount)
{
    if (!SlotW || !SlotW->WidgetTree)
    {
        return;
    }

    const FWTMountHost Host = WT_FindMountHost(SlotW);

    // 找现有按钮：可能挂在槽自己的 tree，也可能挂在图标宿主的 tree（宿主选择可能随面板重建变化）。
    UWTShardTakeButton* Btn = nullptr;
    if (Host.Tree)
    {
        Btn = Cast<UWTShardTakeButton>(Host.Tree->FindWidget(WT_TakeBtnName()));
    }
    if (!Btn)
    {
        Btn = Cast<UWTShardTakeButton>(SlotW->WidgetTree->FindWidget(WT_TakeBtnName()));
    }

    if (!bMount)
    {
        // 槽不再归我们 ⇒ 前面挂过的按钮必须摘掉，否则那格会继续吞点击。
        if (Btn)
        {
            WT_RemoveTakeButtons(SlotW);
            WT_WARNF(TEXT("[shard-take] unmount btn %s slot=%d (slot no longer ours)"),
                     *SlotW->GetName(), SlotIdx);
        }
        return;
    }

    if (Btn)
    {
        Btn->TargetFactory = Factory;      // 面板换机器后仍指向正确工厂
        Btn->SlotIndex     = SlotIdx;
        return;
    }

    if (!Host.Panel || !Host.Tree)
    {
        static TSet<FString> Warned;
        const FString Key = SlotW->GetName();
        if (!Warned.Contains(Key))
        {
            Warned.Add(Key);
            WT_ERRF(TEXT("[shard-take] no panel host inside %s -- take button NOT mounted "
                         "(shards of this slot can not be taken back)"), *SlotW->GetName());
        }
        return;
    }

    UWTShardTakeButton* NewBtn = Host.Tree->ConstructWidget<UWTShardTakeButton>(
        UWTShardTakeButton::StaticClass(), WT_TakeBtnName());
    if (!NewBtn)
    {
        return;
    }
    NewBtn->TargetFactory = Factory;
    NewBtn->SlotIndex     = SlotIdx;
    NewBtn->SetBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.f));   // 全透明：只当命中区，不改外观
    NewBtn->SetVisibility(ESlateVisibility::Visible);

    if (UCanvasPanel* CP = Cast<UCanvasPanel>(Host.Panel))
    {
        if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(CP->AddChild(NewBtn)))
        {
            CS->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));    // 铺满整格
            CS->SetOffsets(FMargin(0.f));
            CS->SetAlignment(FVector2D(0.f, 0.f));
        }
    }
    else
    {
        Host.Panel->AddChild(NewBtn);   // Overlay / VerticalBox 等：默认 slot 已 Fill
    }
    WT_WARNF(TEXT("[shard-take] mount btn on %s slot=%d factory=%s host=%s/%s"),
             *SlotW->GetName(), SlotIdx,
             Factory ? *Factory->GetClass()->GetName() : TEXT("<none>"),
             *Host.Tree->GetName(), *Host.Panel->GetName());
}

/**
 * 每拍同步碎片槽（1.0.8T）。ShardN = 潜在库存里**我们的**碎片片数（0 = 纯原版/空槽）。
 *   B 路：面板上找得到「成本类」变量 ⇒ 按 bOurs 写我们的类 / 写回原版类（每块面板只扫一次）。
 *   A 路：找不到（或还没有可用目标类）⇒ 亲手把已占用槽翻成 1/1。
 *   V 路（1.0.8V）：给已占用格挂「取回」透明按钮（原版取出链数不到我们的碎片）。
 */
// ===================== B2 路：把「成本过滤器类」也对齐（1.0.8Y） =====================
//
// 为什么还差这一步（2026-10-27 20:05 用户截图 + 反射 dump 实锤）：
//   B 路只改了 `Widget_CostSlotWrapper_C.itemDescriptor`（日志 changed=3/3 确实成功），
//   但嵌套的 `Widget_InventorySlot_C` 里是**两个**类属性各管一半：
//     · `mItemClass`            = 格里「实际」物品类（原版被 itemDescriptor 同步成了我们的，已对）
//     · `mFilterItemDescriptor` = 这格「要什么」的过滤器（**仍是 Desc_CrystalShard_C**）
//   dump 实锤：`mItemClass = WTUpgradePowerShardDescriptor` 但
//              `mFilterItemDescriptor = Desc_CrystalShard_C` ⇒ 两者不等。
//   两者不等时原版按「过滤器幽灵」渲染：`MM_UI_InventorySlot_Filtered`（灰剪影）+ 深色数字，
//   这正是「三格灰剪影 + 橙 1/1」混合态的来历 —— 我们的 A 路只补了角标文字，图标仍是幽灵。
//   ⇒ 把过滤器也写成本类，两者相等，原版自己走「格里就是它」的正常分支：
//     `MI_UI_InventorySlot_Part`（彩色图标）+ 橙色 "1/1"。绘制权完全交还原版。
//   ★幂等：每拍比较，只有真的不相等才写 + 才叫原版重算；碎片数减少时写回原版类复原。

/** 在槽包装里找嵌套的 `Widget_InventorySlot_C`（真正的图标/边框宿主）。 */
static UUserWidget* WT_FindInvSlot(UUserWidget* SlotW)
{
    if (!SlotW)
    {
        return nullptr;
    }
    TArray<UWidget*> Deep;
    WT_CollectWidgetsDeep(SlotW, Deep, 4);
    for (UWidget* X : Deep)
    {
        UUserWidget* UW = Cast<UUserWidget>(X);
        if (UW && UW->GetName().StartsWith(TEXT("Widget_InventorySlot")))
        {
            return UW;
        }
    }
    return nullptr;
}

/** 按名字调一个**零参**蓝图函数（必须给有效参数缓冲，见 WT_TryVanillaRefresh 注释）。 */
static bool WT_CallNoArgFn(UObject* Obj, const TCHAR* FnName)
{
    if (!Obj || !FnName)
    {
        return false;
    }
    UFunction* Fn = Obj->FindFunction(FName(FnName));
    if (!Fn || Fn->ParmsSize != 0)
    {
        return false;
    }
    const int32 BufSize = FMath::Max<int32>(static_cast<int32>(Fn->ParmsSize), 1);
    uint8* Parms = static_cast<uint8*>(FMemory::Malloc(BufSize, 16));
    FMemory::Memzero(Parms, BufSize);
    Obj->ProcessEvent(Fn, Parms);
    FMemory::Free(Parms);
    return true;
}

/** 按名字调一个「只有一个 bool 入参」的蓝图函数（如 SetTextboxFormating(HasItems)）。 */
static bool WT_CallFnWithBool(UObject* Obj, const TCHAR* FnName, bool Value)
{
    if (!Obj || !FnName)
    {
        return false;
    }
    UFunction* Fn = Obj->FindFunction(FName(FnName));
    if (!Fn || Fn->ParmsSize == 0)
    {
        return false;
    }
    const int32 BufSize = static_cast<int32>(Fn->ParmsSize);
    uint8* Parms = static_cast<uint8*>(FMemory::Malloc(BufSize, 16));
    FMemory::Memzero(Parms, BufSize);

    // 只在**确实找到**那个 bool 入参时才调用：找不到（签名与预期不符）就放弃，
    // 宁可原版不重画，也不拿全零缓冲去调一个需要真实入参的蓝图函数。
    bool bWrote = false;
    for (TFieldIterator<FProperty> It(Fn); It; ++It)
    {
        FProperty* Child = *It;
        if (!Child->HasAnyPropertyFlags(CPF_Parm) || Child->HasAnyPropertyFlags(CPF_ReturnParm))
        {
            continue;
        }
        if (FBoolProperty* BP = CastField<FBoolProperty>(Child))
        {
            BP->SetPropertyValue_InContainer(Parms, Value);
            bWrote = true;
            break;
        }
    }
    if (!bWrote)
    {
        FMemory::Free(Parms);
        return false;
    }
    Obj->ProcessEvent(Fn, Parms);
    FMemory::Free(Parms);
    return bWrote;
}

/**
 * 机器的潜在（碎片）库存 —— `AFGBuildableFactory::mInventoryPotential`。
 *
 * ★1.0.8AC 改走 **SDK 的 inline 访问器**（`FGBuildableFactory.h:221`
 *   `FORCEINLINE UFGInventoryComponent* GetPotentialInventory() const { return mInventoryPotential; }`）。
 *   理由：inline 是直接读成员的**真实现**（和 GetResourcePurity 同一个套路），编译期展开、
 *   不经过 SML 的桩函数；而反射版（FindPropertyByName）在 08AB 那一版里**没打出任何日志**，
 *   怀疑要么属性没取到、要么整段被跳过 —— 换成 inline 顺带排除这一层不确定性。
 */
static UFGInventoryComponent* WT_PotentialInventory(AFGBuildableFactory* F)
{
    return F ? F->GetPotentialInventory() : nullptr;
}

/**
 * 按名字调一个「(ObjectProperty, IntProperty)」签名的蓝图函数
 * —— 专门为 `Widget_InventorySlot_C::SetupInventorySlot(inventoryComponent, slotIdx)` 而写。
 *
 * ★参数按**类型**定位而不是按名字：ChildProperties 的声明顺序在蓝图编译后不一定等于
 *   源码顺序，但「唯一的那个 object 入参」「唯一的那个 int 入参」是稳定的。
 *   两个都找不到就不调 —— 宁可原版不重设，也不拿全零缓冲去调一个要真实入参的函数。
 */
static bool WT_CallFnObjectInt(UObject* Obj, const TCHAR* FnName, UObject* O, int32 I)
{
    if (!Obj || !FnName)
    {
        return false;
    }
    UFunction* Fn = Obj->FindFunction(FName(FnName));
    if (!Fn || Fn->ParmsSize == 0)
    {
        return false;
    }
    const int32 BufSize = static_cast<int32>(Fn->ParmsSize);
    uint8* Parms = static_cast<uint8*>(FMemory::Malloc(BufSize, 16));
    FMemory::Memzero(Parms, BufSize);

    bool bObj = false;
    bool bInt = false;
    for (TFieldIterator<FProperty> It(Fn); It; ++It)
    {
        FProperty* Child = *It;
        if (!Child->HasAnyPropertyFlags(CPF_Parm) || Child->HasAnyPropertyFlags(CPF_ReturnParm))
        {
            continue;
        }
        if (!bObj)
        {
            if (FObjectProperty* OP = CastField<FObjectProperty>(Child))
            {
                OP->SetObjectPropertyValue_InContainer(Parms, O);
                bObj = true;
                continue;
            }
        }
        if (!bInt)
        {
            if (FIntProperty* IP = CastField<FIntProperty>(Child))
            {
                IP->SetPropertyValue_InContainer(Parms, I);
                bInt = true;
                continue;
            }
        }
    }
    if (!bObj || !bInt)
    {
        FMemory::Free(Parms);
        return false;
    }
    Obj->ProcessEvent(Fn, Parms);
    FMemory::Free(Parms);
    return true;
}

/** 读一个 int32 UPROPERTY（读不到返回 -1）。 */
static int32 WT_ReadInt32PropOf(UObject* Obj, const TCHAR* PropName)
{
    if (!Obj || !PropName)
    {
        return -1;
    }
    FIntProperty* P = CastField<FIntProperty>(Obj->GetClass()->FindPropertyByName(FName(PropName)));
    if (!P)
    {
        return -1;
    }
    return P->GetPropertyValue_InContainer(Obj);
}

/**
 * 写成本格的「成本物品类」——`Widget_CostSlotWrapper_C.mItemCost`（FItemAmount）。
 *
 * ★为什么非改它不可（1.0.8Z 定案，一条根因解释两个症状）：
 *   原版判定「这一格的成本满足了没有」和「点它该取出什么」，用的都是 **mItemCost.ItemClass**：
 *     · 数数量：拿这个类去潜在库存 `GetNumItems/HasItems` —— 语义 `ItemClass->IsA(这个类)`；
 *     · 取物品：点格取出时同样按这个类去库存里找；
 *     · 上颜色：`SetSlotColor()` 按结果在 `GetOrange()` / `GetDarkGray()` 之间选。
 *   我们的 `UWTUpgradePowerShardDescriptor` 与 `Desc_CrystalShard_C` 是**兄弟类**
 *   （父类同为 `UFGPowerShardDescriptor`）⇒ IsA 不成立 ⇒ 数到 0 ⇒
 *     ① 图标被画成灰剪影（DarkGray 分支）
 *     ② 点它取不出我们的碎片
 *   前几版改的 `itemDescriptor` / `mFilterItemDescriptor` / `mItemClass` 都只是**外围副本**，
 *   dump 实锤 mItemCost 从未被碰过（StructProperty 一直没展开）。本函数改的才是正主。
 *
 * 改完类必须叫原版自己重算（否则状态还是旧快照）：SetSlotColor / SetTextboxFormating(true) /
 * Update Progress / SetProgressBarPercent —— 都是 dump 里查到的真名。幂等：相等就不写不调。
 */
static bool WT_AlignCostAmount(UUserWidget* SlotW, UClass* Target, int32 Idx, bool bLog)
{
    if (!SlotW || !Target)
    {
        return false;
    }
    FStructProperty* CostProp = CastField<FStructProperty>(
        SlotW->GetClass()->FindPropertyByName(TEXT("mItemCost")));
    if (!CostProp || !CostProp->Struct)
    {
        return false;
    }
    void* CostAddr = CostProp->ContainerPtrToValuePtr<void>(SlotW);
    if (!CostAddr)
    {
        return false;
    }

    int32 Changed = 0;
    FString Seen;
    for (TFieldIterator<FProperty> It(CostProp->Struct); It; ++It)
    {
        FClassProperty* CP = CastField<FClassProperty>(*It);
        if (!CP)
        {
            continue;
        }
        UClass* Cur = Cast<UClass>(CP->GetObjectPropertyValue_InContainer(CostAddr));
        Seen += FString::Printf(TEXT("%s=%s "), *CP->GetName(),
                                Cur ? *Cur->GetName() : TEXT("<null>"));
        if (Cur != Target)
        {
            CP->SetObjectPropertyValue_InContainer(CostAddr, Target);
            ++Changed;
        }
    }
    if (Changed == 0)
    {
        return false;
    }

    WT_CallNoArgFn(SlotW, TEXT("SetSlotColor"));
    WT_CallFnWithBool(SlotW, TEXT("SetTextboxFormating"), true);
    WT_CallNoArgFn(SlotW, TEXT("Update Progress"));
    WT_CallNoArgFn(SlotW, TEXT("SetProgressBarPercent"));

    if (bLog)
    {
        // 诊断：改完立刻回报图标材质名 + 角标文字，判断原版是不是自己走对了分支。
        FString IconMat = TEXT("<none>");
        FString AmountTxt = TEXT("<?>");
        if (UUserWidget* Inv = WT_FindInvSlot(SlotW))
        {
            if (Inv->WidgetTree)
            {
                TArray<UWidget*> Inner;
                Inv->WidgetTree->GetAllWidgets(Inner);
                for (UWidget* In : Inner)
                {
                    if (UImage* II = Cast<UImage>(In))
                    {
                        if (II->GetName() != TEXT("mSlotBG"))
                        {
                            continue;
                        }
                        if (FStructProperty* ISP = CastField<FStructProperty>(
                                UImage::StaticClass()->FindPropertyByName(TEXT("Brush"))))
                        {
                            FSlateBrush* IB = ISP->ContainerPtrToValuePtr<FSlateBrush>(II);
                            UObject* Res = IB ? IB->GetResourceObject() : nullptr;
                            IconMat = Res ? Res->GetName() : TEXT("<null>");
                        }
                    }
                    else if (UTextBlock* TT = Cast<UTextBlock>(In))
                    {
                        if (TT->GetName() == TEXT("mAmountText"))
                        {
                            AmountTxt = TT->GetText().ToString();
                        }
                    }
                }
            }
        }
        WT_WARNF(TEXT("[shard-cost] slot%d mItemCost was[%s]-> %s | icon=%s amt='%s'"),
                 Idx, *Seen, *Target->GetName(), *IconMat, *AmountTxt);
    }
    return true;
}

// ===================== 08AA：槽位 tint 链（父级 UserWidget 的 ColorAndOpacity） =====================
//
// 08Z 实测（13:06 日志）已经证明 mItemCost 是**对的靶子**：
//   `[shard-cost] slot0 mItemCost was[ItemClass=Desc_CrystalShard_C ]-> WTUpgradePowerShardDescriptor
//    | icon=MID_MI_UI_InventorySlot_Part_* amt='1'`
//   ⇒ ① 元凶坐实（原来就是 Desc_CrystalShard_C）
//     ② 改完后原版重算出的图标材质是 **Part（彩色档）**，角标从 '0' 变成 '1'
//     ③ **但屏幕仍是灰剪影**，而 `Image(mSlotBG)` 自身 tint 恒 (1,1,1,1)
// ⇒ 灰不在材质对象、不在 Image tint、不在过滤器类。剩下唯一没展开看过的是
//   **父级 UserWidget 的 tint**：`ColorAndOpacity` / `ForegroundColor`（dump 里永远是 `<complex>`）。
//   原版 `SetSlotColor()` 正是拿 `GetOrange()` / `GetDarkGray()` 上色的地方 —— 深灰会乘到
//   整棵子树，彩色材质也会被压成灰。本段先 dump 真值（一次），再对已占用格强制白色（幂等）。

/** 读一个 tint 属性的当前值（支持 FLinearColor / FSlateColor；其它结构只回类型名）。 */
static FString WT_ReadTintOf(UObject* Obj, const TCHAR* PropName)
{
    if (!Obj || !PropName)
    {
        return TEXT("-");
    }
    FStructProperty* SP = CastField<FStructProperty>(
        Obj->GetClass()->FindPropertyByName(FName(PropName)));
    if (!SP || !SP->Struct)
    {
        return TEXT("-");
    }
    void* Addr = SP->ContainerPtrToValuePtr<void>(Obj);
    const FString SN = SP->Struct->GetName();
    if (SN == TEXT("LinearColor"))
    {
        const FLinearColor* LC = static_cast<const FLinearColor*>(Addr);
        return FString::Printf(TEXT("(%.2f,%.2f,%.2f,%.2f)"), LC->R, LC->G, LC->B, LC->A);
    }
    if (SN == TEXT("SlateColor"))
    {
        const FLinearColor LC = static_cast<const FSlateColor*>(Addr)->GetSpecifiedColor();
        return FString::Printf(TEXT("s(%.2f,%.2f,%.2f,%.2f)"), LC.R, LC.G, LC.B, LC.A);
    }
    return FString::Printf(TEXT("<%s>"), *SN);
}

/** 把 tint 属性拨成白色。返回 true = 真的改了（已经是白就不写）。 */
static bool WT_ForceTintWhite(UObject* Obj, const TCHAR* PropName)
{
    if (!Obj || !PropName)
    {
        return false;
    }
    FStructProperty* SP = CastField<FStructProperty>(
        Obj->GetClass()->FindPropertyByName(FName(PropName)));
    if (!SP || !SP->Struct)
    {
        return false;
    }
    void* Addr = SP->ContainerPtrToValuePtr<void>(Obj);
    const FString SN = SP->Struct->GetName();
    if (SN == TEXT("LinearColor"))
    {
        FLinearColor* LC = static_cast<FLinearColor*>(Addr);
        if (LC->Equals(FLinearColor::White, 0.02f))
        {
            return false;
        }
        *LC = FLinearColor::White;
        return true;
    }
    if (SN == TEXT("SlateColor"))
    {
        FSlateColor* SC = static_cast<FSlateColor*>(Addr);
        if (SC->GetSpecifiedColor().Equals(FLinearColor::White, 0.02f))
        {
            return false;
        }
        // ★SpecifiedColor 是 protected（实测 C2248）⇒ 只能用公开构造函数整体赋值。
        //   构造出的 ColorUseRule = UseColor_Specified，正是"就按这个颜色画"的语义。
        *SC = FSlateColor(FLinearColor::White);
        return true;
    }
    return false;
}

/** 找槽里的图标控件 `Image(mSlotBG)`。 */
static UImage* WT_FindSlotIcon(UUserWidget* InvSlot)
{
    if (!InvSlot || !InvSlot->WidgetTree)
    {
        return nullptr;
    }
    TArray<UWidget*> Inner;
    InvSlot->WidgetTree->GetAllWidgets(Inner);
    for (UWidget* X : Inner)
    {
        UImage* II = Cast<UImage>(X);
        if (II && II->GetName() == TEXT("mSlotBG"))
        {
            return II;
        }
    }
    return nullptr;
}

/** 取图标 brush 的 FSlateBrush（`UImage::Brush`）。 */
static FSlateBrush* WT_IconBrush(UImage* Icon)
{
    if (!Icon)
    {
        return nullptr;
    }
    FStructProperty* ISP = CastField<FStructProperty>(
        UImage::StaticClass()->FindPropertyByName(TEXT("Brush")));
    return ISP ? ISP->ContainerPtrToValuePtr<FSlateBrush>(Icon) : nullptr;
}

/**
 * 把图标材质的 `SlotIcon` 纹理参数换成本 mod 碎片的贴图。
 * 为什么需要：`MI_UI_InventorySlot_Part` / `MM_UI_InventorySlot_Filtered` 两个材质共用同一个
 * `SlotIcon` 参数决定画哪张图；原版把"未知物品"画成**白色 mask** 正是靠这个参数。
 * 换了它，即使原版把材质翻回 Filtered，图标至少是我们的彩虹碎片（彩色），不会退成灰剪影。
 */
static bool WT_ForceIconParam(UImage* Icon, UTexture2D* Tex, bool bLog)
{
    if (!Icon || !Tex)
    {
        return false;
    }
    FSlateBrush* B = WT_IconBrush(Icon);
    if (!B)
    {
        return false;
    }
    UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(B->GetResourceObject());
    if (!MID)
    {
        return false;
    }
    static const FName SlotIconName(TEXT("SlotIcon"));
    UObject* Cur = nullptr;
    for (const FTextureParameterValue& TV : MID->TextureParameterValues)
    {
        if (TV.ParameterInfo.Name == SlotIconName)
        {
            Cur = TV.ParameterValue;
            break;
        }
    }
    if (Cur == Tex)
    {
        return false;
    }
    MID->SetTextureParameterValue(SlotIconName, Tex);
    if (bLog)
    {
        WT_WARNF(TEXT("[shard-tint] icon SlotIcon param: %s -> %s"),
                 Cur ? *Cur->GetName() : TEXT("<null>"), *Tex->GetName());
    }
    return true;
}

/**
 * 把一格的整条 tint 链拉回白色：包装层 / InventorySlot / 图标自身 / 图标 brush 的 Tint。
 * bLog=true 时先打一行原始值（每格只打一次由调用方保证）。
 * 返回 true = 本拍真的改了什么。
 */
static bool WT_ForceSlotTint(UUserWidget* SlotW, int32 Idx, bool bLog)
{
    if (!SlotW)
    {
        return false;
    }
    UUserWidget* Inv = WT_FindInvSlot(SlotW);
    UImage* Icon = WT_FindSlotIcon(Inv);

    FSlateBrush* Brush = nullptr;
    if (Icon)
    {
        if (FStructProperty* ISP = CastField<FStructProperty>(
                UImage::StaticClass()->FindPropertyByName(TEXT("Brush"))))
        {
            Brush = ISP->ContainerPtrToValuePtr<FSlateBrush>(Icon);
        }
    }

    if (bLog)
    {
        WT_WARNF(TEXT("[shard-tint] slot%d BEFORE wrapper(CA=%s FG=%s op=%.2f) inv(CA=%s FG=%s) "
                      "img(CA=%s op=%.2f) brushTint=%s"),
                 Idx,
                 *WT_ReadTintOf(SlotW, TEXT("ColorAndOpacity")),
                 *WT_ReadTintOf(SlotW, TEXT("ForegroundColor")),
                 SlotW->GetRenderOpacity(),
                 Inv ? *WT_ReadTintOf(Inv, TEXT("ColorAndOpacity")) : TEXT("-"),
                 Inv ? *WT_ReadTintOf(Inv, TEXT("ForegroundColor")) : TEXT("-"),
                 Icon ? *WT_ReadTintOf(Icon, TEXT("ColorAndOpacity")) : TEXT("-"),
                 Icon ? Icon->GetRenderOpacity() : -1.f,
                 Brush ? *FString::Printf(TEXT("(%.2f,%.2f,%.2f,%.2f)"),
                          Brush->TintColor.GetSpecifiedColor().R,
                          Brush->TintColor.GetSpecifiedColor().G,
                          Brush->TintColor.GetSpecifiedColor().B,
                          Brush->TintColor.GetSpecifiedColor().A)
                       : TEXT("-"));
    }

    bool bChanged = false;
    if (WT_ForceTintWhite(SlotW, TEXT("ColorAndOpacity")))  { bChanged = true; }
    if (WT_ForceTintWhite(SlotW, TEXT("ForegroundColor")))  { bChanged = true; }
    if (Inv)
    {
        if (WT_ForceTintWhite(Inv, TEXT("ColorAndOpacity"))) { bChanged = true; }
        if (WT_ForceTintWhite(Inv, TEXT("ForegroundColor"))) { bChanged = true; }
    }
    if (Icon)
    {
        if (WT_ForceTintWhite(Icon, TEXT("ColorAndOpacity"))) { bChanged = true; }
    }
    if (Brush)
    {
        const FLinearColor Cur = Brush->TintColor.GetSpecifiedColor();
        if (!Cur.Equals(FLinearColor::White, 0.02f))
        {
            Brush->TintColor = FSlateColor(FLinearColor::White);
            bChanged = true;
        }
    }
    if (bLog && Icon)
    {
        // 一次性把图标的 brush/父材质/全部材质参数摊开（SlotIcon 是否被换成白 mask 一眼可辨）。
        WT_DumpIconMaterial(Icon, TEXT("icon(mSlotBG)"));
    }
    if (bChanged)
    {
        WT_WARNF(TEXT("[shard-tint] slot%d AFTER wrapper(CA=%s) inv(CA=%s) img(CA=%s) brushTint=%s"),
                 Idx,
                 *WT_ReadTintOf(SlotW, TEXT("ColorAndOpacity")),
                 Inv ? *WT_ReadTintOf(Inv, TEXT("ColorAndOpacity")) : TEXT("-"),
                 Icon ? *WT_ReadTintOf(Icon, TEXT("ColorAndOpacity")) : TEXT("-"),
                 Brush ? *FString::Printf(TEXT("(%.2f,%.2f,%.2f,%.2f)"),
                          Brush->TintColor.GetSpecifiedColor().R,
                          Brush->TintColor.GetSpecifiedColor().G,
                          Brush->TintColor.GetSpecifiedColor().B,
                          Brush->TintColor.GetSpecifiedColor().A)
                       : TEXT("-"));
    }

    // ★最后一道：把图标材质画哪张图的 `SlotIcon` 参数换成我们碎片的贴图。
    //   原版在「未满足 / 未知物品」分支会把这个参数设成白色 mask ⇒ 无论如何上色都是灰剪影。
    if (UTexture2D* OurIcon = UFGItemDescriptor::GetSmallIcon(
            UWTUpgradePowerShardDescriptor::StaticClass()))
    {
        WT_ForceIconParam(Icon, OurIcon, bLog);
    }
    return bChanged;
}

/** 读一个 UObjectProperty 指向的对象（反射；属性名不对返回 null）。 */
static UObject* WT_ReadObjPropOf(UObject* Holder, const TCHAR* PropName)
{
    if (!Holder || !PropName)
    {
        return nullptr;
    }
    FObjectProperty* P = CastField<FObjectProperty>(
        Holder->GetClass()->FindPropertyByName(FName(PropName)));
    return P ? P->GetObjectPropertyValue(P->ContainerPtrToValuePtr<void>(Holder)) : nullptr;
}

/** 材质名里带 `InventorySlot_Part` = 原版「已满足」的彩色材质；`..._Filtered` = 灰剪影。 */
static bool WT_IsPartMaterial(UObject* Mat)
{
    return Mat && Mat->GetName().Contains(TEXT("InventorySlot_Part"));
}

/**
 * 找原版「已满足」母材质 `MI_UI_InventorySlot_Part`（MaterialInstance 资产，不是 MID）。
 *
 * 1.0.8AD：不硬编码 `/Game/...` 路径、也不引资产注册表（会多一个模块依赖），
 * 直接 `TObjectIterator<UMaterialInterface>` 扫内存 —— 只要游戏里任何一格显示过彩色，
 * 这个材质就已经加载，扫到就缓存（同 WT_CollectVehicleBoxes 的套路）。
 */
static UMaterialInterface* WT_FindPartParentMaterial()
{
    static TWeakObjectPtr<UMaterialInterface> Cached;
    static bool bSearched = false;
    if (Cached.IsValid())
    {
        return Cached.Get();
    }
    if (bSearched)
    {
        return nullptr;
    }
    bSearched = true;
    for (TObjectIterator<UMaterialInterface> It; It; ++It)
    {
        UMaterialInterface* M = *It;
        if (M && !Cast<UMaterialInstanceDynamic>(M)
            && M->GetName().Contains(TEXT("InventorySlot_Part")))
        {
            Cached = M;
            return M;
        }
    }
    return nullptr;
}

/** 原版碎片图标纹理 `PowerShard_256`（槽位 `mCostSlotBrush` 用的就是它）—— 扫内存，不硬编码路径。 */
static UTexture2D* WT_VanillaShardIconTex()
{
    static TWeakObjectPtr<UTexture2D> Cached;
    if (Cached.IsValid())
    {
        return Cached.Get();
    }
    for (TObjectIterator<UTexture2D> It; It; ++It)
    {
        UTexture2D* T = *It;
        if (T && T->GetName() == TEXT("PowerShard_256"))
        {
            Cached = T;
            return T;
        }
    }
    return nullptr;
}

/**
 * 给本 mod 的碎片描述符补图标（幂等：CDO 上已有值就撒手）。
 *
 * 为什么单独做：`UFGItemDescriptor::GetSmallIcon(Class)` 读的是 CDO 的 `mSmallIcon`
 * （FGItemDescriptor.h:425 + 215，SML 桩里是真实现）。我们派生的碎片 CDO 这个字段是空的
 * ⇒ 08AA 的 `WT_ForceIconParam` 那段 `if (UTexture2D* OurIcon = ...)` 一直进不去（日志一行都没打），
 * 槽位画出来的就只能是原版兜底纹理/白 mask。补上之后物品栏、tooltip、槽位才都有图。
 */
static bool WT_EnsureShardIcon(bool bLog)
{
    UClass* Cls = UWTUpgradePowerShardDescriptor::StaticClass();
    if (!Cls)
    {
        return false;
    }
    UObject* CDO = Cls->GetDefaultObject();
    FObjectProperty* IP = CDO
        ? CastField<FObjectProperty>(Cls->FindPropertyByName(TEXT("mSmallIcon")))
        : nullptr;
    if (!CDO || !IP)
    {
        return false;
    }
    void* Slot = IP->ContainerPtrToValuePtr<void>(CDO);
    if (IP->GetObjectPropertyValue(Slot))
    {
        return false;   // 已经有图标，不动
    }
    UTexture2D* Tex = WT_VanillaShardIconTex();
    if (!Tex)
    {
        return false;
    }
    IP->SetObjectPropertyValue(Slot, Tex);
    if (bLog)
    {
        WT_WARNF(TEXT("[shard-mat] shard descriptor mSmallIcon: <null> -> %s"), *Tex->GetName());
    }
    return true;
}

/**
 * 把「原版已经选好的」`mSlotBgMaterial` 真正推给 Slate。
 *
 * 1.0.8AF 关键假设：`UImage::Brush` 是 UPROPERTY，如果某个刷新只写了 `Brush.ResourceObject`
 * 而**没走** `UImage::SetBrush`，Slate 的 SImage 拿到的仍是旧 brush ⇒ 属性全对、屏幕永远是
 * 上一帧的灰剪影（08AE 日志：mSlotBgMaterial 与 mSlotBG.brush 都已是 `..._Part_*`，画面仍灰）。
 * 这里用 `SetBrushResourceObject`（Image.h:89，内部 SetBrush → SImage->SetImage）重推一次。
 * 材质**仍然是原版选的那个**，我们只是让它生效 —— 不换材质、不自绘。
 */
static bool WT_PushSlotMaterial(UUserWidget* InvSlot)
{
    if (!InvSlot)
    {
        return false;
    }
    UImage*  Icon = WT_FindSlotIcon(InvSlot);
    UObject* Mat  = WT_ReadObjPropOf(InvSlot, TEXT("mSlotBgMaterial"));
    if (!Icon || !Mat)
    {
        return false;
    }
    FSlateBrush* B = WT_IconBrush(Icon);
    if (B && B->GetResourceObject() == Mat)
    {
        // 属性层面已经一致：Slate 侧可能还拿着旧的，用"自我重设"强制重绘一次。
        Icon->SetBrush(*B);
        return true;
    }
    Icon->SetBrushResourceObject(Mat);
    return true;
}

/**
 * 把一格槽的底色材质换回原版「已满足」的 Part 材质（**用原版资产 + 原版参数**，不是自绘）。
 *
 * 1.0.8AC 日志已经把根因钉死：原版**自己数到了**我们的碎片
 * （`[shard-ab] slotN num=1 ALREADY-OK`），说明"够不够"的判定其实成立；
 * 但 `mSlotBgMaterial` 仍是 `MID_MM_UI_InventorySlot_Filtered` —— 原版只在「物品变化」事件里
 * 重选材质，面板打开时按 CDO 算过一次之后就再也不重算 ⇒ 属性全对、画面永远灰剪影。
 *
 * 做法：从原版 `MI_UI_InventorySlot_Part` 新建一个 MID，把当前 Filtered MID 的
 * 纹理/向量参数原样搬过去（SlotTexture、SlotIcon=PowerShard_256、AccentColor 橙色都不变），
 * 只去掉"去饱和"这一层 ⇒ 外观就是原版「已满足」态：彩色晶体 + 橙 "1/1"。
 * 幂等：当前已经是 Part 材质立刻返回 false，不会每帧造 MID。
 */
static bool WT_ForcePartMaterial(UUserWidget* InvSlot, int32 Idx, bool bLog)
{
    if (!InvSlot)
    {
        return false;
    }
    UImage* Icon = WT_FindSlotIcon(InvSlot);
    if (!Icon)
    {
        return false;
    }
    UObject* Cur = WT_ReadObjPropOf(InvSlot, TEXT("mSlotBgMaterial"));
    if (WT_IsPartMaterial(Cur))
    {
        return false;
    }
    UMaterialInterface* Parent = WT_FindPartParentMaterial();
    if (!Parent)
    {
        if (bLog)
        {
            WT_WARNF(TEXT("[shard-mat] slot%d Part 母材质未加载（MI_UI_InventorySlot_Part 扫不到）"), Idx);
        }
        return false;
    }
    UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Parent, InvSlot);
    if (!MID)
    {
        return false;
    }
    if (UMaterialInstanceDynamic* OldMID = Cast<UMaterialInstanceDynamic>(Cur))
    {
        for (const FTextureParameterValue& TV : OldMID->TextureParameterValues)
        {
            if (UTexture2D* Tex = Cast<UTexture2D>(TV.ParameterValue))
            {
                MID->SetTextureParameterValue(TV.ParameterInfo.Name, Tex);
            }
        }
        for (const FVectorParameterValue& VV : OldMID->VectorParameterValues)
        {
            MID->SetVectorParameterValue(VV.ParameterInfo.Name, VV.ParameterValue);
        }
    }
    // 原版「已满足」态的标量（1.0.8X 从真实 Part MID dump 出来的那组，见日志 mat.scalar 段）
    MID->SetScalarParameterValue(TEXT("ShowIcon"), 1.f);
    MID->SetScalarParameterValue(TEXT("IgnoreNineSlice"), 0.f);
    MID->SetScalarParameterValue(TEXT("ShouldGlitch"), 0.f);
    MID->SetScalarParameterValue(TEXT("BlinkIcon"), 1.f);

    if (FObjectProperty* MP = CastField<FObjectProperty>(
            InvSlot->GetClass()->FindPropertyByName(TEXT("mSlotBgMaterial"))))
    {
        MP->SetObjectPropertyValue(MP->ContainerPtrToValuePtr<void>(InvSlot), MID);
    }
    // ★必须走 UImage::SetBrushResourceObject（Image.h:89）而不是直接改内部 FSlateBrush：
    //   后者只改到 UPROPERTY 里的那份副本，Slate 收不到通知 ⇒ 屏幕不会变。
    Icon->SetBrushResourceObject(MID);
    if (bLog)
    {
        WT_WARNF(TEXT("[shard-mat] slot%d forced Part MID=%s parent=%s (was %s)"),
                 Idx, *MID->GetName(), *Parent->GetName(),
                 Cur ? *Cur->GetName() : TEXT("<null>"));
    }
    return true;
}

/**
 * 调一个「单对象入参」的蓝图函数（`SetIconBrush(IconTexture)` 这类）。
 * 参数按**类型**定位（第一个 object 入参），找不到就放弃 —— 绝不拿半空缓冲去打脚本 VM。
 */
static bool WT_CallFnObject(UObject* Obj, const TCHAR* FnName, UObject* Arg)
{
    if (!Obj || !FnName)
    {
        return false;
    }
    UFunction* Fn = Obj->FindFunction(FName(FnName));
    if (!Fn)
    {
        return false;
    }
    const int32 BufSize = FMath::Max<int32>(static_cast<int32>(Fn->ParmsSize), 1);
    uint8* Parms = static_cast<uint8*>(FMemory::Malloc(BufSize, 16));
    if (!Parms)
    {
        return false;
    }
    FMemory::Memzero(Parms, BufSize);
    bool bWrote = false;
    for (TFieldIterator<FProperty> It(Fn); It; ++It)
    {
        FProperty* P = *It;
        if (!P->HasAnyPropertyFlags(CPF_Parm) || P->HasAnyPropertyFlags(CPF_ReturnParm))
        {
            continue;
        }
        if (FObjectProperty* OP = CastField<FObjectProperty>(P))
        {
            OP->SetObjectPropertyValue(OP->ContainerPtrToValuePtr<void>(Parms), Arg);
            bWrote = true;
            break;
        }
    }
    if (bWrote)
    {
        Obj->ProcessEvent(Fn, Parms);
    }
    FMemory::Free(Parms);
    return bWrote;
}

/**
 * 一格的**外观状态快照**：底色材质 / 成本条 / 各 Image 可见性 / 各文本与数量。
 * 1.0.8AG 用它对比「原版正门调之前 → 调之后」，一行就能看出原版到底改了哪一项。
 */
/**
 * 把一格内层槽上**三个交互委托**的绑定明细打出来（1.0.8AI 诊断「碎片取不下来」）。
 *
 * `Widget_InventorySlot_C` 上没有任何「我是第几格 / 我的库存是谁」的字段
 * （属性 dump 实锤），所以「点这一格把碎片取回背包」这件事只能由绑定在
 * `OnMoveStack` / `OnStackDroppedOnSlot` / `OnSlotItemNumberChanged` 上的原版蓝图处理器完成。
 * ⇒ 只要把每格的绑定内容摊开，就能一次分清两种情况：
 *     ① 第 2/3 格的绑定为空   → 原版压根没给它们装交互（这才是「只有第 1 格能取」的真相）；
 *     ② 三格绑定完全一样       → 处理器装了，但内部判据（多半是过滤器类）没通过。
 * 用引擎自带的 `TMulticastScriptDelegate::ToString<UObject>()`（ScriptDelegates.h:825），
 * 它按 `[对象路径.函数名, ...]` 格式化整条调用链，比自己拆 InvocationList 更稳。
 */
static FString WT_SlotDelegateInfo(UUserWidget* InvSlot)
{
    if (!InvSlot)
    {
        return FString(TEXT("<null>"));
    }
    UClass*     C = InvSlot->GetClass();
    const TCHAR* Names[3] = { TEXT("OnMoveStack"),
                              TEXT("OnStackDroppedOnSlot"),
                              TEXT("OnSlotItemNumberChanged") };
    FString Out;
    for (int32 i = 0; i < 3; ++i)
    {
        FMulticastDelegateProperty* MP =
            CastField<FMulticastDelegateProperty>(C->FindPropertyByName(Names[i]));
        if (!MP)
        {
            Out += FString::Printf(TEXT(" %s=<no-prop>"), Names[i]);
            continue;
        }
        const FMulticastScriptDelegate* Del =
            MP->GetMulticastDelegate(MP->ContainerPtrToValuePtr<void>(InvSlot));
        Out += FString::Printf(TEXT(" %s=%s"), Names[i],
                               Del ? *Del->ToString<UObject>() : TEXT("<null>"));
    }
    return Out;
}

static FString WT_SlotSnapshot(UUserWidget* SlotW)
{
    if (!SlotW || !SlotW->WidgetTree)
    {
        return FString(TEXT("<null>"));
    }
    TArray<UWidget*> Deep;
    WT_CollectWidgetsDeep(SlotW, Deep, 4);
    UUserWidget* InvSlot = nullptr;
    FString Bar;
    FString Imgs;
    FString Texts;
    for (UWidget* X : Deep)
    {
        if (!X)
        {
            continue;
        }
        if (UUserWidget* UW = Cast<UUserWidget>(X))
        {
            if (!InvSlot && UW->GetName().StartsWith(TEXT("Widget_InventorySlot")))
            {
                InvSlot = UW;
            }
            continue;
        }
        if (UTextBlock* T = Cast<UTextBlock>(X))
        {
            Texts += FString::Printf(TEXT("%s='%s'v%d "), *X->GetName(),
                                     *T->GetText().ToString(), static_cast<int32>(X->GetVisibility()));
        }
        else if (UProgressBar* B = Cast<UProgressBar>(X))
        {
            Bar = FString::Printf(TEXT("pct=%.2f vis=%d"), B->GetPercent(),
                                  static_cast<int32>(B->GetVisibility()));
        }
        else if (UImage* I = Cast<UImage>(X))
        {
            Imgs += FString::Printf(TEXT("%s=v%d "), *X->GetName(),
                                    static_cast<int32>(X->GetVisibility()));
            (void)I;
        }
    }
    FString Mat = TEXT("<noInvSlot>");
    if (InvSlot)
    {
        UObject* M = WT_ReadObjPropOf(InvSlot, TEXT("mSlotBgMaterial"));
        Mat = FString::Printf(TEXT("%s num=%d"), M ? *M->GetName() : TEXT("<null>"),
                              WT_ReadInt32PropOf(InvSlot, TEXT("mNumItems")));
    }
    return FString::Printf(TEXT("mat=%s | bar[%s] | imgs{%s} | txt{%s}"),
                           *Mat, *Bar, *Imgs, *Texts);
}

/** 一次把这一格的控件树摊开（类名 + 名字 + 可见性 + 不透明度）—— 确认灰剪影到底画在谁身上。 */
static void WT_DumpSlotTree(UUserWidget* SlotW, int32 Idx)
{
    if (!SlotW || !SlotW->WidgetTree)
    {
        return;
    }
    TArray<UWidget*> Deep;
    WT_CollectWidgetsDeep(SlotW, Deep, 6);
    int32 n = 0;
    for (UWidget* X : Deep)
    {
        if (!X || ++n > 40)
        {
            break;
        }
        WT_WARNF(TEXT("[shard-tree] slot%d #%d %s '%s' vis=%d op=%.2f"),
                 Idx, n, *X->GetClass()->GetName(), *X->GetName(),
                 static_cast<int32>(X->GetVisibility()), X->GetRenderOpacity());
    }
    WT_WARNF(TEXT("[shard-tree] slot%d total widgets = %d"), Idx, Deep.Num());
}

/** 内存里所有名字带 InventorySlot 的**材质资产**清单（全局一次）—— 找原版「已满足」到底用哪个。 */
static void WT_LogSlotMaterialList()
{
    static bool bDone = false;
    if (bDone)
    {
        return;
    }
    bDone = true;
    int32 n = 0;
    for (TObjectIterator<UMaterialInterface> It; It; ++It)
    {
        UMaterialInterface* M = *It;
        if (!M || Cast<UMaterialInstanceDynamic>(M))
        {
            continue;   // 只看资产，不看每格的临时 MID
        }
        const FString N = M->GetName();
        if (!N.Contains(TEXT("InventorySlot")))
        {
            continue;
        }
        if (++n > 24)
        {
            break;
        }
        WT_WARNF(TEXT("[shard-mats] %s (%s)"), *N, *M->GetClass()->GetName());
    }
    WT_WARNF(TEXT("[shard-mats] total material assets with 'InventorySlot' = %d"), n);
}

/**
 * 调原版 `Widget_CostSlotWrapper_C::Setup CostIcon(...)` —— 超频面板「成本格」渲染总入口。
 *
 * ★1.0.8AE：用户明确要求「强制换，不如走原版」。这个函数就是原版的正门，反射 dump 的完整签名
 *   （函数名里**带空格**，勿改）：
 *     Setup CostIcon(IconTexture, ItemAmount, CachedInventoryComponent, slotIdx,
 *                    CurrentNumInSlot, SmallSlot, BigSlot, ForceOrangeTextbox,
 *                    GrabFromStockpile, OverwriteComponentNumInSlot)
 *   原版在这一个函数里决定：画哪张图、底色用 `MI_UI_InventorySlot_Part`（彩色）还是
 *   `MM_UI_InventorySlot_Filtered`（灰剪影）、文本写 "1/1" 还是 "1"、点一下取几个。
 *   ⇒ 只要把参数喂对，材质/图标/文本/交互就全部回到原版手里，不需要我们硬替换任何东西。
 *
 * 参数按**名字**定位（蓝图编译后 ChildProperties 顺序可能变，但参数名是符号表里的真名）。
 * 必需项（IconTexture / ItemAmount / slotIdx）缺一个就不调用，避免拿半空缓冲去打蓝图函数。
 */
static bool WT_CallSetupCostIcon(UUserWidget* Wrapper, UTexture2D* IconTex, UClass* ItemCls,
                                 int32 Amount, UFGInventoryComponent* Inv, int32 Idx, int32 CurNum,
                                 bool bOverwriteNum)
{
    if (!Wrapper || !ItemCls)
    {
        return false;
    }
    UFunction* Fn = Wrapper->FindFunction(FName(TEXT("Setup CostIcon")));
    if (!Fn || Fn->ParmsSize == 0)
    {
        return false;
    }
    const int32 BufSize = static_cast<int32>(Fn->ParmsSize);
    uint8* Parms = static_cast<uint8*>(FMemory::Malloc(BufSize, 16));
    FMemory::Memzero(Parms, BufSize);

    int32 nTex = 0;
    int32 nAmt = 0;
    int32 nIdx = 0;
    int32 nCls = 0;
    // ★1.0.8AG：把真实参数表打一次。若「调了没反应」，第一件事就是核对参数名/类型是否如我们所猜。
    static bool bParmsLogged = false;
    const bool  bLogNow      = !bParmsLogged;
    FString     ParmList;
    for (TFieldIterator<FProperty> It(Fn); It; ++It)
    {
        FProperty* P = *It;
        if (!P->HasAnyPropertyFlags(CPF_Parm) || P->HasAnyPropertyFlags(CPF_ReturnParm))
        {
            continue;
        }
        const FString N   = P->GetName();
        void*         Dst = P->ContainerPtrToValuePtr<void>(Parms);
        if (bLogNow)
        {
            ParmList += FString::Printf(TEXT("%s(%s) "), *N, *P->GetClass()->GetName());
        }
        if (FObjectProperty* OP = CastField<FObjectProperty>(P))
        {
            if (N == TEXT("IconTexture"))
            {
                OP->SetObjectPropertyValue(Dst, IconTex);
                ++nTex;
            }
            else if (N == TEXT("CachedInventoryComponent"))
            {
                OP->SetObjectPropertyValue(Dst, Inv);
            }
        }
        else if (FIntProperty* IP = CastField<FIntProperty>(P))
        {
            if (N == TEXT("slotIdx"))
            {
                IP->SetPropertyValue(Dst, Idx);
                ++nIdx;
            }
            else if (N == TEXT("CurrentNumInSlot"))
            {
                IP->SetPropertyValue(Dst, CurNum);
            }
        }
        else if (FBoolProperty* BP = CastField<FBoolProperty>(P))
        {
            // 面板上是小槽；不强制橙字（让原版按数量自己决定）；不触发"从存量抓取"；
            // OverwriteComponentNumInSlot 由调用方给 —— 1.0.8AH 起**第一轮就是 true**：
            // false 时原版会去共享库存 `mInventoryPotential` 数总数（3 片全在里面）
            // ⇒ 每格都被算成 "3/1" + 橙条 62%，1.5 秒后才被第二轮修正（08AG 日志实锤的跳变）。
            bool bVal = false;
            if (N == TEXT("SmallSlot"))
            {
                bVal = true;
            }
            else if (N == TEXT("OverwriteComponentNumInSlot"))
            {
                bVal = bOverwriteNum;
            }
            BP->SetPropertyValue(Dst, bVal);
        }
        else if (FStructProperty* SP = CastField<FStructProperty>(P))
        {
            if (N == TEXT("ItemAmount"))
            {
                // FItemAmount{ ItemClass; Amount } —— 按成员**类型**填，不写死字段名
                // （同 WT_AlignCostAmount 的做法，蓝图结构体字段名可能改）。
                for (TFieldIterator<FProperty> Sub(SP->Struct); Sub; ++Sub)
                {
                    FProperty* CP   = *Sub;
                    void*      CDst = CP->ContainerPtrToValuePtr<void>(Dst);
                    if (FClassProperty* CLP = CastField<FClassProperty>(CP))
                    {
                        CLP->SetObjectPropertyValue(CDst, ItemCls);
                        ++nCls;
                    }
                    else if (FIntProperty* CIP = CastField<FIntProperty>(CP))
                    {
                        CIP->SetPropertyValue(CDst, Amount);
                    }
                }
                ++nAmt;
            }
        }
    }
    if (bLogNow)
    {
        bParmsLogged = true;
        WT_WARNF(TEXT("[shard-parms] Setup CostIcon: %s| wrote tex=%d amount=%d class=%d idx=%d"),
                 *ParmList, nTex, nAmt, nCls, nIdx);
    }
    if (nTex == 0 || nAmt == 0 || nIdx == 0 || nCls == 0)
    {
        FMemory::Free(Parms);
        return false;
    }
    Wrapper->ProcessEvent(Fn, Parms);
    FMemory::Free(Parms);
    return true;
}

/**
 * 把一格的「过滤器类 + 物品类」都拨到 Target。类真的变了才叫原版重算（幂等）。
 * 返回 true = 本拍做过修改。
 */
static bool WT_AlignCostFilter(UUserWidget* SlotW, UClass* Target, int32 Idx, bool bLog)
{
    if (!SlotW || !Target)
    {
        return false;
    }
    UUserWidget* InvSlot = WT_FindInvSlot(SlotW);
    if (!InvSlot)
    {
        return false;
    }
    UClass* C = InvSlot->GetClass();
    FProperty* FilterProp = C->FindPropertyByName(TEXT("mFilterItemDescriptor"));
    FProperty* ItemProp   = C->FindPropertyByName(TEXT("mItemClass"));
    if (!FilterProp && !ItemProp)
    {
        static TSet<FString> Warned;
        const FString Key = C->GetName();
        if (!Warned.Contains(Key))
        {
            Warned.Add(Key);
            WT_ERRF(TEXT("[shard-filter] %s has neither mFilterItemDescriptor nor mItemClass "
                         "-- cost filter NOT aligned"), *Key);
        }
        return false;
    }

    bool bChanged = false;
    if (FilterProp
        && Cast<UClass>(WT_ReadClassProp(FilterProp, InvSlot)) != Target
        && WT_WriteClassProp(FilterProp, InvSlot, Target))
    {
        bChanged = true;
    }
    if (ItemProp
        && Cast<UClass>(WT_ReadClassProp(ItemProp, InvSlot)) != Target
        && WT_WriteClassProp(ItemProp, InvSlot, Target))
    {
        bChanged = true;
    }

    if (bChanged)
    {
        // 类变了 ⇒ 叫原版按新类重画这一格（三支都是零参、只重算显示，不动控件树）。
        WT_CallNoArgFn(InvSlot, TEXT("UpdateSlotInformation"));
        WT_CallNoArgFn(InvSlot, TEXT("UpdateResourceFormVisuals"));
        WT_CallNoArgFn(InvSlot, TEXT("UpdateAmountBGVisbility"));
        if (bLog)
        {
            // 诊断：把重画后的图标材质名一起打出来。
            //   `MI_UI_InventorySlot_Part`     = 彩色（原版「已满足」）
            //   `MM_UI_InventorySlot_Filtered` = 灰剪影（原版「未满足」/ 过滤器幽灵）
            FString IconMat = TEXT("<none>");
            if (InvSlot->WidgetTree)
            {
                TArray<UWidget*> Inner;
                InvSlot->WidgetTree->GetAllWidgets(Inner);
                for (UWidget* In : Inner)
                {
                    UImage* II = Cast<UImage>(In);
                    if (!II || II->GetName() != TEXT("mSlotBG"))
                    {
                        continue;
                    }
                    if (FStructProperty* ISP = CastField<FStructProperty>(
                            UImage::StaticClass()->FindPropertyByName(TEXT("Brush"))))
                    {
                        FSlateBrush* IB = ISP->ContainerPtrToValuePtr<FSlateBrush>(II);
                        UObject* Res = IB ? IB->GetResourceObject() : nullptr;
                        IconMat = Res ? Res->GetName() : TEXT("<null>");
                    }
                    break;
                }
            }
            WT_WARNF(TEXT("[shard-filter] slot%d: mFilter/mItem -> %s | icon=%s"),
                     Idx, *Target->GetName(), *IconMat);
        }
    }
    return bChanged;
}

static void WT_SyncShardSlots(UUserWidget* Panel, int32 ShardN, AFGBuildableFactory* Factory)
{
    if (!Panel || !Panel->WidgetTree)
    {
        return;
    }
    // ★1.0.8X：首次进入时接管原版成本格默认物品类（见 WT_InstallCostClassCDO 的长注释）。
    WT_InstallCostClassCDO();
    TArray<UWidget*> Outer;
    Panel->WidgetTree->GetAllWidgets(Outer);

    TMap<int32, UUserWidget*> Slots;
    for (UWidget* X : Outer)
    {
        UUserWidget* UW = Cast<UUserWidget>(X);
        if (!UW)
        {
            continue;
        }
        const FString N = UW->GetName();            // CostSlot_0 / _1 / _2（数字后缀稳定）
        if (!N.StartsWith(TEXT("CostSlot_")))
        {
            continue;
        }
        Slots.Add(FCString::Atoi(*N.RightChop(9)), UW);
    }

    const int32 Want  = FMath::Clamp(ShardN, 0, 3);
    const bool  bOurs = (Want > 0);

    static TWeakObjectPtr<UUserWidget> ScannedPanel;
    static TArray<UWidget*>            ScannedSlotW;
    static TArray<WTShardClassRef>     CostRefs;
    static UClass*                     VanillaCostClass = nullptr;
    static int32                       LastCostMode = -1;
    static TWeakObjectPtr<UUserWidget> LastDumpPanel;
    static int32                       LastDumpWant = -1;

    // ★1.0.8AN：槽包裹对象被原版**池化重建**过（同一块面板、但 CostSlot_i 换成了新控件）时也必须重扫。
    //   旧判据只看 `ScannedPanel != Panel`，而面板是复用的（关掉只是 Collapsed）⇒ 重开时还是
    //   同一个 Panel 对象，`CostRefs` 里存的是**上一代控件**的指针，B 路写进去毫无效果
    //   —— 这正是「改了不进画面、重开面板才好」的另一半原因。这里按控件指针集合判定。
    bool bSlotsRebuilt = (ScannedSlotW.Num() != Slots.Num());
    if (!bSlotsRebuilt)
    {
        for (int32 i = 0; i < Slots.Num(); ++i)
        {
            if (ScannedSlotW[i] != Slots.FindRef(i))
            {
                bSlotsRebuilt = true;
                break;
            }
        }
    }

    // 一块面板只扫一次（BP 变量不会中途改类），新面板 / 新槽控件重新扫。
    const bool bNewPanel = (ScannedPanel.Get() != Panel);
    if (bNewPanel || bSlotsRebuilt)
    {
        ScannedPanel    = Panel;
        ScannedSlotW.Reset();
        for (int32 i = 0; i < Slots.Num(); ++i)
        {
            ScannedSlotW.Add(Slots.FindRef(i));
        }
        LastDumpPanel.Reset();
        LastDumpWant    = -1;
        LastCostMode    = -1;
        CostRefs.Reset();
        VanillaCostClass = nullptr;

        WT_ScanShardClassProps(Panel, CostRefs);
        for (const TPair<int32, UUserWidget*>& KV : Slots)
        {
            WT_ScanShardClassProps(KV.Value, CostRefs);
        }
        // 记下原版类：扫到的第一个**不是我们的**碎片类 = 面板原本的成本物品。
        for (const WTShardClassRef& R : CostRefs)
        {
            if (UClass* Cur = Cast<UClass>(WT_ReadClassProp(R.Prop, R.Holder)))
            {
                if (!Cur->IsChildOf(UWTUpgradePowerShardDescriptor::StaticClass()))
                {
                    VanillaCostClass = Cur;
                    break;
                }
            }
        }
        WT_WARNF(TEXT("[shard-ui] cost class scan: panel=%s slots=%d refs=%d vanilla=%s%s"),
                 *Panel->GetClass()->GetName(), Slots.Num(), CostRefs.Num(),
                 VanillaCostClass ? *VanillaCostClass->GetName() : TEXT("<none>"),
                 bNewPanel ? TEXT("") : TEXT(" (slots rebuilt)"));

        // ★1.0.8X：把原版槽位对外的接口一次性摊开（属性当前值 + 全部 UFunction + 图标材质参数）。
        //   ★1.0.8AN：只在**换了一块新面板**时 dump；槽控件被池化重建时只重扫属性，不重复打印
        //   （这两段一次几百行，40Hz 下重扫时每插一片都打一遍会把日志淹掉）。
        if (bNewPanel)
        {
            if (UUserWidget** S0 = Slots.Find(0))
            {
                WT_DumpSlotSurface(*S0);
            }
            // 面板类本身也 dump：真正「一键重算全部槽」的入口大概在 DropArea 上，不在单格上。
            WT_DumpObjectSurface(Panel, TEXT("DropArea"));
        }
    }

    // ---- B 路（★1.0.8X 重新打开：这是「让原版认我们的碎片」的唯一入口） ----
    //   08U 关它的理由是「碎片取不下来」，但 08W 已用日志查清真因是 WT_ApplySlotHighlight
    //   把 RenderOpacity 写成 0.40（原版「空槽」语义），与 B 路无关 ⇒ 现在恢复。
    //   改成我们的类之后，原版 GetNumItems/HasItems 按 IsA 就能数到 1 片，
    //   渲染、插入、取出三件事同时回到原版手里（不再需要 A 路手写角标）。
    static constexpr bool WT_SHARD_COST_CLASS = true;
    bool bBApplied = false;
    UClass* CostTarget = bOurs ? UWTUpgradePowerShardDescriptor::StaticClass() : VanillaCostClass;
    if (WT_SHARD_COST_CLASS && CostRefs.Num() > 0 && CostTarget)
    {
        int32 Changed = 0;
        for (const WTShardClassRef& R : CostRefs)
        {
            if (WT_WriteClassProp(R.Prop, R.Holder, CostTarget))
            {
                ++Changed;
            }
        }
        bBApplied = true;
        const int32 Mode = bOurs ? 1 : 0;
        if (Changed > 0 || LastCostMode != Mode)
        {
            LastCostMode = Mode;
            WT_WARNF(TEXT("[shard-ui] cost class -> %s (changed=%d/%d ours=%d)"),
                     *CostTarget->GetName(), Changed, CostRefs.Num(), bOurs ? 1 : 0);
        }
    }

    // ---- AB 路（★1.0.8AB）：把已占用格交回原版 `SetupInventorySlot` ----
    //
    // ★本轮日志实锤的根因链（不再靠猜）：
    //   ① 面板打开瞬间原版按 CDO 的 `Desc_CrystalShard_C` 初始化槽 ⇒ `mNumItems = 0`
    //      ⇒ `mSlotBgMaterial = MID_MM_UI_InventorySlot_Filtered`（灰剪影 = 原版「未满足」态）；
    //   ② 08X~08AA 把 itemDescriptor / mFilterItemDescriptor / mItemClass / mItemCost 全换成我们的类，
    //      但原版**不会因为属性变了就重选材质** —— 它只在「物品变化」事件里重算。
    //      反证有两处：插第 3 片那一刻（13:30:15 日志）材质确实变成 Part（原版重算过一次）；
    //      同一块面板静止 78 秒后用户截图仍是 Filtered。
    //   ③ 所以缺的从来不是属性，是「让原版按我们的碎片重设这一格」的那一次调用。
    //
    // 正解 = 原版蓝图函数 `Widget_InventorySlot_C::SetupInventorySlot(inventoryComponent, slotIdx)`
    //   （运行时反射 dump 拿到的真名，pdb 里查不到）。这就是 DropArea 在物品变化时走的正门：
    //   绑定「真实库存 + 格号」之后，图标、数量、底色材质、点击取出全部回到原版手里
    //   —— 一次调用同时修「灰剪影」和「插槽 2/3 取不下来」。
    //
    // 安全阀：
    //   · 只对**已占用格**（idx < Want）调，空槽保持原版成本槽外观；
    //   · 原版已经数到（`mNumItems > 0`）就撒手，说明这一格已经接通；
    //   · 同一面板同一格 1 秒最多调一次 —— 这个函数会注册多播委托，不能每拍重复绑；
    //   · 面板换了就清表（槽被原版池化重建）。
    static TWeakObjectPtr<UUserWidget> VanillaSetupPanel;
    static TMap<FString, double>       VanillaSetupStamp;
    static TSet<FString>               VanillaOwned;
    // ★1.0.8AJ：`SetupInventorySlot` 的绑定账本。ab-seen 之前把 AB 路关死的教训写在调用点。
    static constexpr bool              WT_SHARD_FORCE_SETUP_INV = true;
    static TSet<FString>               SetupInvDone;     // 真正绑成功过的「槽名#格号」
    static TMap<FString, int32>        SetupInvIdx;      // 那个格子当时绑的是库存第几格（补位后要跟着变）
    static TMap<FString, int32>        SetupBindTries;   // 每格最多试 3 次（失败也别刷屏）
    static TSet<FString>               AbFirstSeen;      // 每格只解释一次（1.0.8AC 诊断）
    static TMap<FString, double>       MatStamp;         // ★1.0.8AD 材质路限流
    static TMap<FString, FString>      MatSeen;          // ★1.0.8AF 上一次看到的材质名（变化才 dump/推送）
    static TMap<FString, int32>        V2Tries;          // ★1.0.8AG 原版正门每格试了几次
    static TMap<FString, double>       V2Stamp;          // ★1.0.8AG 原版正门限流
    static TMap<FString, FString>      DelSeen;          // ★1.0.8AI 委托绑定明细（变化才打）
    static bool                        bShardIconDone = false;
    if (VanillaSetupPanel.Get() != Panel)
    {
        VanillaSetupPanel = Panel;
        VanillaSetupStamp.Reset();
        VanillaOwned.Reset();
        SetupInvDone.Reset();
        SetupInvIdx.Reset();
        SetupBindTries.Reset();
        AbFirstSeen.Reset();
        MatStamp.Reset();
        MatSeen.Reset();
        V2Tries.Reset();
        V2Stamp.Reset();
        DelSeen.Reset();
    }
    // 碎片描述符补图标（全局一次；空了就补，补上之后物品栏/tooltip/槽位都有图）
    if (!bShardIconDone)
    {
        bShardIconDone = true;
        WT_EnsureShardIcon(true);
    }

    UFGInventoryComponent* PotInv = WT_PotentialInventory(Factory);

    // ★1.0.8AJ：槽位与潜在库存是**一一对应**的（Slot_i ↔ PotInv[i]），占用与否只能看
    //   「这一格自己」有几个碎片，不能拿「碎片总数」当槽数。
    //   用户实测（15:59:41 日志）：「取走第 1 片 → 剩下两片仍在 idx1/idx2，idx0 变空洞」，
    //   而旧逻辑 `Want = 碎片总数 = 2` ⇒ 把真正装着碎片的 idx2 那格当空闲格写回原版类 ⇒ 变灰。
    //   这就是「后面两个补到前面、直接变灰锁死」的全部原因。
    TArray<int32> PotIdxOfSlot;      // 槽 i -> 实际绑的库存索引（-1 = 没有可用索引）
    PotIdxOfSlot.Init(-1, 3);
    TArray<int32> SlotShardN;        // 槽 i 该不该显示成「装着碎片」（0/1）
    SlotShardN.SetNumZeroed(3);
    if (PotInv)
    {
        // ★1.0.8AK 显示补位（用户 2026-10-28 00:09 截图定案）：
        //   AJ 让「槽 i ↔ 库存 idx i」一一对应，结果取走第 1 片后 idx0 留空洞，
        //   左边第 1 格变成灰剪影 + 数字 1，右边两格彩色 —— 看着像坏掉（用户原话「666」）。
        //   原版这 3 个成本格表达的其实是「装了几片」：装了 2 片就该是**前 2 格**彩色。
        //   ⇒ 槽 i 绑「第 i 个有碎片的库存索引」，碎片永远从第 1 格起连续排满；
        //     装不满的槽绑到「第一个空的库存索引」上，交回原版画需求态（灰格）。
        //   库存本身一个字节都不动（不搬物品、不压实、不触发复制），只改「每格看哪一格」。
        const int32 PotCap = WT_PotSlotCap(PotInv);
        TArray<int32> Occ;
        for (int32 i = 0; i < PotCap; ++i)
        {
            if (WT_PotSlotShardCount(PotInv, i) > 0)
            {
                Occ.Add(i);
            }
        }
        int32 EmptyIdx = -1;
        for (int32 i = 0; i < PotCap; ++i)
        {
            if (!Occ.Contains(i))
            {
                EmptyIdx = i;
                break;
            }
        }
        for (int32 i = 0; i < 3; ++i)
        {
            if (Occ.IsValidIndex(i))
            {
                PotIdxOfSlot[i] = Occ[i];
                SlotShardN[i]   = 1;
            }
            else
            {
                PotIdxOfSlot[i] = EmptyIdx;
            }
        }
    }
    const int32 SlotN = Slots.Num();   // 面板实际槽数（原版是 3）

    // ★1.0.8AN **实时刷新**（用户 05:15「我需要在面板插入/取出时进行实时更新，而不是开关面板刷新」）。
    //
    //   现象：在面板里插/拔一片碎片，格子的彩色/灰态要等到**关掉面板再打开**才对。
    //   根因不在「有没有每拍同步」，而在「每拍同步被自己的额度挡住了」：
    //     · 插入/取出会改变库存内容，但**槽包裹对象、槽名、绑定的库存索引都可能不变**
    //       ⇒ 走不到 remap 那条清账路（remap 只在 `PotIdxOfSlot` 变了才触发），
    //     · 而 V 路的 `V2Tries`（每格额度）与 `V2Stamp`（限流）是**按槽名记账、跨内容变化保留**的
    //       ⇒ 额度一旦用完，这一格就再也不重画，只有重开面板（新对象/新 Key）才会清零重来。
    //   修法：把「三格各装了几片」当成库存签名，**签名一变就当场清空视觉侧的额度/限流**
    //     （V2Tries / V2Stamp / MatSeen），让 AB 重绑与 V 重画在**同一拍**重做。
    //   为什么不清 `SetupInvDone`：绑定本身仍然有效（索引搬家由 remap 负责），
    //     重复调 `SetupInventorySlot` 会重复注册多播委托，没必要。
    {
        FString Sig;
        for (int32 i = 0; i < 3; ++i)
        {
            Sig += FString::Printf(TEXT("%d;"), SlotShardN.IsValidIndex(i) ? SlotShardN[i] : 0);
        }
        static FString LastShardSig;
        if (Sig != LastShardSig)
        {
            const bool bSilent = LastShardSig.IsEmpty();   // 第一次进面板：别当成「玩家改了库存」
            LastShardSig = Sig;
            if (!bSilent)
            {
                V2Tries.Reset();
                V2Stamp.Reset();
                MatSeen.Reset();
                MatStamp.Reset();
                VanillaSetupStamp.Reset();   // 让 AB 路可以立刻重绑一次（不再等 0.1s 节流）
                WT_WARNF(TEXT("[shard-live] inventory changed perSlot=%s -> quotas reset, redraw now"),
                         *Sig);
            }
        }
    }

    // ★1.0.8AC 哨兵：每块面板一行，把 AB 路**所有**判据一次打全。
    //   08AB 那一版 `[shard-vanilla]` 一行都没进日志，说明这段被某个条件挡住却无从判断是哪个
    //   ⇒ 这一行不打出来就能立刻区分「代码没加载 / 面板没进 Sync / 判据不成立」三种情况。
    {
        static TWeakObjectPtr<UUserWidget> AbSentinelPanel;
        if (AbSentinelPanel.Get() != Panel)
        {
            AbSentinelPanel = Panel;
            WT_WARNF(TEXT("[shard-ab] VER=AN built=%s %s panel=%s ours=%d want=%d factory=%s potInv=%s slots=%d perSlot=%d/%d/%d potIdx=%d/%d/%d"),
                     TEXT(__DATE__), TEXT(__TIME__),
                     Panel ? *Panel->GetClass()->GetName() : TEXT("<null>"),
                     bOurs ? 1 : 0, Want,
                     Factory ? *Factory->GetClass()->GetName() : TEXT("<none>"),
                     PotInv ? *PotInv->GetName() : TEXT("<null>"), SlotN,
                     SlotShardN[0], SlotShardN[1], SlotShardN[2],
                     PotIdxOfSlot[0], PotIdxOfSlot[1], PotIdxOfSlot[2]);
        }
    }

    // ★1.0.8AM UI 刷新提速（用户「UI刷新频率加一下」）。两道路的限流从「秒级」压到「一两拍」：
    //   · 面板补丁定时器 0.05s(20Hz) → 0.025s(40Hz)；
    //   · AB（绑库存）1.5s → 0.10s，额度 3 → 10；
    //   · V（原版正门重画）0.6s → 0.05s（≈每拍），额度 4 → 12。
    //   为什么敢压到这么狠：两道都是**满足即停** —— AB 命中 `SetupInvDone` 就 continue、
    //   V 命中 `WT_VanillaSatisfied` 就 continue，额度只防异常时无限空转，不是常态路径。
    //   正常一次插入 = 绑 1 次 + 重画 1~2 次，40Hz 下 50~75ms 内就变彩色（旧版要 600ms+）。
    static constexpr double WT_AB_RETRY_SEC  = 0.10;
    static constexpr double WT_V_RETRY_SEC   = 0.05;
    static constexpr int32  WT_AB_MAX_TRIES  = 10;
    static constexpr int32  WT_V_MAX_TRIES   = 12;

    if (bOurs && PotInv)
    {
        const double NowS = FPlatformTime::Seconds();
        for (const TPair<int32, UUserWidget*>& KV : Slots)
        {
            // ★1.0.8AJ：**所有槽都绑**，不再按 Want 裁剪。空格也要绑（绑到空的库存索引），
            //   否则原版不知道这一格对应哪个库存格，取走/放入都会错位。
            // ★1.0.8AK：绑定目标不再是「槽号」而是补位映射 `PotIdxOfSlot` —— 碎片永远排在前 n 格。
            UUserWidget* InvSlot   = WT_FindInvSlot(KV.Value);
            const FString SlotName = KV.Value ? KV.Value->GetName() : FString();
            const int32   Num      = InvSlot ? WT_ReadInt32PropOf(InvSlot, TEXT("mNumItems")) : -2;
            const int32   Shards   = SlotShardN.IsValidIndex(KV.Key) ? SlotShardN[KV.Key] : 0;
            const int32   TgtIdx   = PotIdxOfSlot.IsValidIndex(KV.Key) ? PotIdxOfSlot[KV.Key] : -1;
            const FString ExKey    = FString::Printf(TEXT("%s/%d"), *SlotName, KV.Key);
            const bool bFirstSeen  = !AbFirstSeen.Contains(ExKey);
            if (bFirstSeen)
            {
                AbFirstSeen.Add(ExKey);
            }
            const FString Key = FString::Printf(TEXT("%s#%d"), *SlotName, KV.Key);
            // ★1.0.8AK：补位映射变了（取走/放入一片后碎片前移）⇒ 这一格必须重新绑到新的库存索引，
            //   否则格子里显示的仍是上一轮那一片（视觉上「取下来的那片还赖在那儿」）。
            // ★1.0.8AL：remap 还要**清掉两道限流**，否则重开面板前会一直灰（用户 00:24 实测
            //   「插入碎片后前两个位置变灰，重新打开面板才正常」）：
            //     ① `VanillaSetupStamp`（1.5s 节流）—— 不清理的话新索引要等 1.5 秒才绑，
            //        这段时间里格子还挂着旧索引的视觉（灰剪影 + 1）；
            //     ② `V2Tries`/`V2Stamp`（V 路每格 2 次额度）—— 额度用完后那句 `Tries >= 2 continue`
            //        会让这一格永久停在绑定时被重置成的原版态 = 灰，只有重开面板（额度清零）才好。
            if (SetupInvDone.Contains(Key) && SetupInvIdx.FindRef(Key) != TgtIdx)
            {
                const int32 Was = SetupInvIdx.FindRef(Key);
                SetupInvDone.Remove(Key);
                SetupBindTries.Remove(Key);
                VanillaSetupStamp.Remove(Key);
                const FString VK = FString::Printf(TEXT("%s/%d"), *SlotName, KV.Key);
                V2Tries.Remove(VK);
                V2Stamp.Remove(VK);
                WT_WARNF(TEXT("[shard-ab] slot%d remap %d -> %d (rebind, quota reset)"), KV.Key, Was, TgtIdx);
            }
            // ★1.0.8AJ 自愈：库存这一格明明有碎片、槽却没数到（Num < Shards）⇒ 绑定丢了
            //   （面板重开、槽被池化重建），清账让它重新绑一次。
            if (SetupInvDone.Contains(Key) && Num >= 0 && Num < Shards)
            {
                SetupInvDone.Remove(Key);
                SetupBindTries.Remove(Key);
                WT_WARNF(TEXT("[shard-ab] slot%d bind lost (num=%d < shards=%d) -> rebind"), KV.Key, Num, Shards);
            }
            // ★1.0.8AJ 定案（08AI 日志实锤）：`mNumItems > 0` 只能证明「原版按类数到了」，
            //   **不能**证明「这一格绑上了库存」。08AC 起这里见到 num>0 就撒手，结果
            //   面板第一次出现时三格全是 ALREADY-OK ⇒ 只有后来变空的 slot0 碰巧被绑上
            //   （`[shard-ab] slot0 num=0 -> SetupInventorySlot(InventoryPotential, 0) ok=1`），
            //   而 slot1/slot2 一次都没绑 ⇒ **点它们原版不知道从哪格取** = 用户报的「取不下来」。
            //   ⇒ 现在按 Key 记账，每格**必须真绑成功一次**才撒手。
            const bool bNeedBind = WT_SHARD_FORCE_SETUP_INV && !SetupInvDone.Contains(Key);
            if (InvSlot && !bNeedBind && TgtIdx >= 0)
            {
                // ★1.0.8AK：**空格不登记 VanillaOwned** —— 空格要留给 B2 路写回原版成本类，
                //   由原版画成「还差 1 片」的需求灰格（这就是原版超频面板没装满时的样子）。
                if (Shards > 0)
                {
                    VanillaOwned.Add(SlotName);
                }
                if (bFirstSeen)
                {
                    WT_WARNF(TEXT("[shard-ab] slot%d bound shards=%d num=%d idx=%d (skip, bind done)"),
                             KV.Key, Shards, Num, TgtIdx);
                }
                continue;
            }
            if (TgtIdx < 0)
            {
                continue;
            }
            int32& Binds = SetupBindTries.FindOrAdd(Key);
            // ★1.0.8AM：与 V 路同款 —— 额度用完后退化成 2 秒一次低频重试，绝不彻底撒手。
            //   旧版在这里 `Binds >= 3` 就 continue 并且登记 VanillaOwned（等于让 B2 路
            //   也不许再碰这格的过滤器），一旦前 3 次绑定没成功，这一格就再也绑不上库存
            //   ⇒ 点它取不出碎片，且只有重开面板才会清零额度。
            const bool   bBindSlow = (Binds >= WT_AB_MAX_TRIES);
            const double BindRetry = bBindSlow ? 2.0 : WT_AB_RETRY_SEC;
            double& Last = VanillaSetupStamp.FindOrAdd(Key);
            if (Last > 0.0 && NowS - Last < BindRetry)
            {
                continue;
            }
            Last = NowS;
            if (!bBindSlow)
            {
                ++Binds;
            }
            const bool bOk = InvSlot
                ? WT_CallFnObjectInt(InvSlot, TEXT("SetupInventorySlot"), PotInv, TgtIdx)
                : false;
            if (bOk)
            {
                // ★接管成功就立刻登记：后面的 B2 路（写 mFilterItemDescriptor + UpdateSlotInformation）
                //   会把原版刚选好的材质又刷回 Filtered，必须在同一拍就让开。
                if (Shards > 0)
                {
                    VanillaOwned.Add(SlotName);
                }
                SetupInvDone.Add(Key);
                SetupInvIdx.Add(Key, TgtIdx);
                // ★1.0.8AJ 第二手（只对**有碎片**的格）：面板关闭时 B2 路会把
                //   `mItemCost.ItemClass` 写回原版 `Desc_CrystalShard_C`
                //   （日志 `[shard-cost] slot0 mItemCost was[...]-> Desc_CrystalShard_C`），
                //   而槽里实际装的是我们的碎片类 ⇒ 若取出逻辑拿 mItemCost 去
                //   `RemoveAmountFromInventory` 就会找不到物品、点了没反应。
                //   空格不动它 —— 空格就该保持原版成本槽的类与外观。
                UClass* OurCls = UWTUpgradePowerShardDescriptor::StaticClass();
                if (OurCls && Shards > 0)
                {
                    WT_AlignCostAmount(KV.Value, OurCls, TgtIdx, false);
                    UTexture2D* ITex = UFGItemDescriptor::GetSmallIcon(OurCls);
                    if (!ITex)
                    {
                        ITex = WT_VanillaShardIconTex();
                    }
                    WT_CallSetupCostIcon(KV.Value, ITex, OurCls, 1, PotInv, TgtIdx, 1, true);
                }
                // 绑定会重算这一格的视觉；之前 V 路可能已经用满 2 次额度，这里清账让它还能再修一次。
                V2Tries.Remove(FString::Printf(TEXT("%s/%d"), *SlotName, KV.Key));
                V2Stamp.Remove(FString::Printf(TEXT("%s/%d"), *SlotName, KV.Key));
            }
            WT_WARNF(TEXT("[shard-ab] slot%d bind try=%d shards=%d num=%d idx=%d invSlot=%s -> SetupInventorySlot(%s, %d) ok=%d"),
                     KV.Key, Binds, Shards, Num, TgtIdx,
                     InvSlot ? *InvSlot->GetName() : TEXT("<none>"),
                     *PotInv->GetName(), TgtIdx, bOk ? 1 : 0);
        }
    }

    // ---- B2 路（1.0.8Y）：嵌套的 InventorySlot 有**自己**的成本过滤器类，也要对齐 ----
    //   B 路只管了外层 CostSlotWrapper.itemDescriptor；不把这一层的 mFilterItemDescriptor
    //   一起换掉，原版就会按「过滤器幽灵」画灰剪影（详见本函数上方长注释）。
    //   空闲格的判据是**这一格自己**有没有碎片（SlotShardN），不是「碎片总数 Want」——
    //   用户取走第 1 片后库存留着 idx0 空洞、idx1/idx2 原地不动，按总数裁会把 idx2 误判成空闲。
    //   ★1.0.8AB：已经交给原版接管的格（VanillaOwned）一律不再动 —— 否则我们又把它改回
    //     「过滤器幽灵」态，和原版抢同一个属性，灰剪影会来回闪。
    if (WT_SHARD_COST_CLASS)
    {
        for (const TPair<int32, UUserWidget*>& KV : Slots)
        {
            const bool bOccupied = bOurs && SlotShardN.IsValidIndex(KV.Key) && SlotShardN[KV.Key] > 0;
            if (bOccupied && KV.Value && VanillaOwned.Contains(KV.Value->GetName()))
            {
                continue;
            }
            UClass* WantCls = bOccupied
                ? UWTUpgradePowerShardDescriptor::StaticClass()
                : VanillaCostClass;
            if (WantCls)
            {
                WT_AlignCostFilter(KV.Value, WantCls, KV.Key, true);
                // ★1.0.8Z：真正决定「数到几片 / 点它取什么」的是 mItemCost，上面那两类属性只是外围副本。
                WT_AlignCostAmount(KV.Value, WantCls, KV.Key, true);
                // ★1.0.8AA：已占用格把整条 tint 链拉回白色 —— 原版 `SetSlotColor()` 用
                //   `GetDarkGray()` 上色时会乘到整棵子树，彩色材质也会被压成灰剪影。
                if (bOccupied)
                {
                    static TSet<FString> TintLogged;
                    const FString TKey = KV.Value ? KV.Value->GetName() : FString();
                    const bool bFirst = !TintLogged.Contains(TKey);
                    if (bFirst)
                    {
                        TintLogged.Add(TKey);
                    }
                    WT_ForceSlotTint(KV.Value, KV.Key, bFirst);
                }
            }
        }
    }

    // ---- V 路（1.0.8AG）：把「满足态」整条交回原版，我们只记录它自己造成的结果 ----
    //   ★08AF 的定论（日志实锤）：`mSlotBG` 是这一格**唯一**可见的 Image，材质确实是 `..._Part...`、
    //     `SlotIcon=PowerShard_256`、`AccentColor` 橙、`ShowIcon=1`、tint 全白 —— 参数一个不缺，
    //     可屏幕画的还是「未满足」的灰剪影。原因只有一个：**原版那次重算从来没被调用过**。
    //     08AE/08AF 都被开头那句「材质已是 Part 就撒手」挡在门外（08AE 日志 `[shard-setup]` 0 行、
    //     08AF 的 `[shard-push]` 每秒重复打，说明根本没进去过判断体）。
    //   ⇒ 这一版不再猜参数、也不再自己画：把原版那套入口按顺序叫一遍，只把它造成的结果打出来：
    //     ① `Setup CostIcon(...)`        原版成本格总入口（画图 / 底色 / 文本 / 点击数量都在这里）
    //     ② `SetTextboxFormating(true)`  文本底由「小角标」切成「整条橙条」
    //     ③ `SetSlotColor()`             取 `GetOrange()` 上色
    //     ④ `SetIconBrush(IconTexture)`  图标笔刷再确认一次
    //   每格额度 WT_V_MAX_TRIES(12) 次、限流 WT_V_RETRY_SEC(0.05s ≈ 每拍)；
    //   满足（文本 '1/1' + 视觉到位）就当场清零撒手，所以正常路径只调 1~2 次。
    //   ★1.0.8AM：旧版 2 次/1.5s 会让「插入后」肉眼可见地等半秒多才变彩色（用户「UI刷新频率加一下」）。
    //   ★`WT_SHARD_FORCE_MAT` 是最后兜底总闸，默认关：只有实测发现原版这条路也不给彩色时才翻。
    static constexpr bool WT_SHARD_FORCE_MAT = false;
    if (bOurs && PotInv)
    {
        UClass*     OurCls = UWTUpgradePowerShardDescriptor::StaticClass();
        UTexture2D* IconTex = OurCls ? UFGItemDescriptor::GetSmallIcon(OurCls) : nullptr;
        if (!IconTex)
        {
            IconTex = WT_VanillaShardIconTex();   // CDO 没图标就用原版碎片图标（视觉一致）
        }
        if (WT_SHARD_DIAG_DEEP)
        {
            WT_LogSlotMaterialList();
        }
        // 图标候选对照（每块面板一次）：原版成本类用的图标 vs 我们的 vs 扫到的 PowerShard_256
        static TWeakObjectPtr<UUserWidget> IconDiagPanel;
        if (IconDiagPanel.Get() != Panel)
        {
            IconDiagPanel = Panel;
            UTexture2D* VTex = VanillaCostClass
                ? UFGItemDescriptor::GetSmallIcon(VanillaCostClass) : nullptr;
            UTexture2D* PTex = WT_VanillaShardIconTex();
            WT_WARNF(TEXT("[shard-img] icon candidates: vanillaCls=%s -> %s | ours=%s -> %s | scanPowerShard256=%s"),
                     VanillaCostClass ? *VanillaCostClass->GetName() : TEXT("<null>"),
                     VTex ? *VTex->GetName() : TEXT("<null>"),
                     OurCls ? *OurCls->GetName() : TEXT("<null>"),
                     IconTex ? *IconTex->GetName() : TEXT("<null>"),
                     PTex ? *PTex->GetName() : TEXT("<null>"));
        }
        const double NowV = FPlatformTime::Seconds();
        for (const TPair<int32, UUserWidget*>& KV : Slots)
        {
            if (!KV.Value)
            {
                continue;
            }
            UUserWidget* Wrapper = KV.Value;
            UUserWidget* InvSlot = WT_FindInvSlot(Wrapper);
            if (!InvSlot)
            {
                continue;
            }
            const FString VKey = FString::Printf(TEXT("%s/%d"), *Wrapper->GetName(), KV.Key);

            // ★1.0.8AJ：**空格（该格库存没有碎片）一律交回原版**画空槽。
            //   旧版按「碎片总数」当槽数，取走第 1 片后仍把有碎片的 idx2 那格当空闲、
            //   又把真正变空的 idx0 硬画成「1 片」⇒ 整排锁死。现在一格一格看。
            if (!SlotShardN.IsValidIndex(KV.Key) || SlotShardN[KV.Key] <= 0)
            {
                V2Tries.Remove(VKey);
                V2Stamp.Remove(VKey);
                continue;
            }
            // ★1.0.8AK：这一格实际绑的库存索引（补位后 ≠ 槽号），原版正门要用它去数片数。
            const int32 VTgtIdx = PotIdxOfSlot.IsValidIndex(KV.Key) ? PotIdxOfSlot[KV.Key] : -1;
            if (VTgtIdx < 0)
            {
                continue;
            }

            // 材质名一变就把这一格**所有** Image 摊开，并把「原版选定的材质」真正推给 Slate。
            //   （这一步是纯观测，不参与判定；08AF 靠它拿到了「唯一可见 Image = mSlotBG」。）
            UObject*      CurMat = WT_ReadObjPropOf(InvSlot, TEXT("mSlotBgMaterial"));
            const FString MatNow = CurMat ? CurMat->GetName() : FString(TEXT("<null>"));
            FString& Seen = MatSeen.FindOrAdd(VKey);
            if (Seen != MatNow)
            {
                Seen = MatNow;
                // ★1.0.8AJ：原版重算过这一格（材质变了）⇒ 把我们的修复额度清零，
                //   否则「每格最多 2 次」用完之后这一格就永久停在灰色（用户报的「变灰锁死」）。
                V2Tries.Remove(VKey);
                if (WT_SHARD_DIAG_DEEP)
                {
                    WT_DumpSlotImagesAll(KV.Value, KV.Key);
                }
                const bool bPushed = WT_PushSlotMaterial(InvSlot);
                WT_WARNF(TEXT("[shard-push] slot%d material=%s pushed=%d"), KV.Key, *MatNow, bPushed ? 1 : 0);
            }

            // ① 原版正门（每格最多 2 次）
            //   ★1.0.8AH 定案（08AG 日志逐行对拍）：真正起效的是 `overwrite=1` 那一轮。
            //     overwrite=0 时原版会去**共享库存** `mInventoryPotential` 数总数（3 片全在里面），
            //     于是每格都算成 "3/1" + 橙条 62% + 未满足底色，1.5 秒后才被第二轮 overwrite=1 修正
            //     ⇒ 用户看到的是「先错 1.5 秒再跳成对」。现在第一轮就 overwrite=1、
            //       CurrentNumInSlot 传 1（碎片槽每格 1 片），一次到位，没有中间态。
            //   已经显示 "1/1"（原版满足态）⇒ 零调用撒手，让原版自己管。
            if (WT_VanillaSatisfied(Wrapper))
            {
                V2Tries.Remove(VKey);
                continue;
            }
            int32& Tries = V2Tries.FindOrAdd(VKey);
            // ★1.0.8AM：额度用完后**不再彻底撒手**，退化成 2 秒一次的低频重试。
            //   旧版 `Tries >= N` 直接 continue ⇒ 一旦用满额度又没修好，这一格就永久灰，
            //   只能靠「换材质 / remap / 重开面板」清账（就是用户反复遇到的「变灰锁死」）。
            //   低频重试的成本 = 每 2 秒一次 `Setup CostIcon`，异常态下也能自愈。
            const bool   bSlowRetry = (Tries >= WT_V_MAX_TRIES);
            const double RetrySec   = bSlowRetry ? 2.0 : WT_V_RETRY_SEC;
            double& LastV = V2Stamp.FindOrAdd(VKey);
            if (LastV > 0.0 && NowV - LastV < RetrySec)
            {
                continue;
            }
            LastV = NowV;
            if (!bSlowRetry)
            {
                ++Tries;
            }

            const bool bOverwrite = true;   // 固定 true：不让原版去共享库存数总数（否则 "3/1"）
            const FString Snap0 = WT_SlotSnapshot(Wrapper);
            const bool bSetup = WT_CallSetupCostIcon(Wrapper, IconTex, OurCls, 1, PotInv,
                                                     VTgtIdx, 1, bOverwrite);
            const FString Snap1 = WT_SlotSnapshot(Wrapper);

            bool bTb = false;
            bool bSc = false;
            bool bIb = false;
            if (!WT_VanillaSatisfied(Wrapper))
            {
                bTb = WT_CallFnWithBool(Wrapper, TEXT("SetTextboxFormating"), true);
                bSc = WT_CallNoArgFn(Wrapper, TEXT("SetSlotColor"));
                bIb = WT_CallFnObject(Wrapper, TEXT("SetIconBrush"), IconTex);
            }
            const FString Snap2 = WT_SlotSnapshot(Wrapper);

            WT_WARNF(TEXT("[shard-v2] slot%d try=%d overwrite=%d setup=%d tb=%d sc=%d ib=%d sat=%d"),
                     KV.Key, Tries, bOverwrite ? 1 : 0, bSetup ? 1 : 0, bTb ? 1 : 0, bSc ? 1 : 0, bIb ? 1 : 0,
                     WT_VanillaSatisfied(Wrapper) ? 1 : 0);
            if (Tries == 1)
            {
                WT_WARNF(TEXT("[shard-v2] slot%d before    : %s"), KV.Key, *Snap0);
            }
            WT_WARNF(TEXT("[shard-v2] slot%d afterSetup: %s"), KV.Key, *Snap1);
            WT_WARNF(TEXT("[shard-v2] slot%d afterAll  : %s"), KV.Key, *Snap2);

            if (WT_SHARD_DIAG_DEEP && Tries == 1 && KV.Key == 0)
            {
                WT_DumpSlotTree(Wrapper, KV.Key);
            }

            UObject* AfterMat = WT_ReadObjPropOf(InvSlot, TEXT("mSlotBgMaterial"));
            if (!WT_IsPartMaterial(AfterMat) && WT_SHARD_FORCE_MAT)
            {
                WT_ForcePartMaterial(InvSlot, KV.Key, true);
            }
        }
    }

    // ---- F 路（1.0.8AI）：内层槽的「过滤器类」对齐成我们的碎片 ----
    //   ★1.0.8AJ 更正：08AI 依据的「mFilterItemDescriptor 还是原版类」是**dump 时序造成的误读** ——
    //     dump 在 ScannedPanel 分支里、B 路之前执行，那时当然还是 Desc_CrystalShard_C；
    //     B 路（`[shard-ui] cost class -> ... changed=3/3`）扫到的 refs 里就含它，跑完已经是我们的类。
    //     ⇒ 08AI 日志里 `[shard-flt]` 一行都没有，恰是「Cur == FCls 直接 continue」的证据，
    //       而不是「代码没跑」。所以「取不下来」与过滤器无关，别再往这条路上改。
    //   这段保留为**只读诊断**：把每格此刻的真值打一次，防止以后再被时序骗。
    static constexpr bool WT_SHARD_FILTER_ALIGN = true;
    if (WT_SHARD_FILTER_ALIGN && bOurs && Want > 0)
    {
        static TSet<FString> FltSeen;
        UClass* FCls = UWTUpgradePowerShardDescriptor::StaticClass();
        for (const TPair<int32, UUserWidget*>& KV : Slots)
        {
            if (!SlotShardN.IsValidIndex(KV.Key) || SlotShardN[KV.Key] <= 0 || !KV.Value || !FCls)
            {
                continue;
            }
            UUserWidget* InvSlot = WT_FindInvSlot(KV.Value);
            if (!InvSlot)
            {
                continue;
            }
            FProperty* FP = InvSlot->GetClass()->FindPropertyByName(TEXT("mFilterItemDescriptor"));
            if (!FP)
            {
                const FString NK = FString::Printf(TEXT("%s/%d"), *InvSlot->GetName(), KV.Key);
                FString& S = FltSeen.FindOrAdd(NK);
                if (S != TEXT("noprop"))
                {
                    S = TEXT("noprop");
                    WT_WARNF(TEXT("[shard-flt] slot%d %s has NO mFilterItemDescriptor"), KV.Key, *InvSlot->GetClass()->GetName());
                }
                continue;
            }
            UClass* Cur = Cast<UClass>(WT_ReadClassProp(FP, InvSlot));
            bool bWrote = false;
            if (Cur != FCls)
            {
                bWrote = WT_WriteClassProp(FP, InvSlot, FCls);
            }
            const FString Now = FString::Printf(TEXT("%s/w%d"), Cur ? *Cur->GetName() : TEXT("<null>"), bWrote ? 1 : 0);
            FString& S = FltSeen.FindOrAdd(FString::Printf(TEXT("%s/%d"), *InvSlot->GetName(), KV.Key));
            if (S != Now)
            {
                S = Now;
                WT_WARNF(TEXT("[shard-flt] slot%d mFilterItemDescriptor %s -> %s wrote=%d"),
                         KV.Key, Cur ? *Cur->GetName() : TEXT("<null>"),
                         *FCls->GetName(), bWrote ? 1 : 0);
            }
        }
    }

    // ---- 1.0.8AI 诊断：原版给每格装了哪些交互处理器 + 当前悬停在哪一格 ----
    //   前者回答「取出到底是被拒绝、还是根本没装」；后者回答「点击确实落在这格吗」。
    if (WT_SHARD_DIAG_CLICK)
    {
        // ★1.0.8AM：面板补丁升到 40Hz 后，这段「每格摊开委托链」的反射开销不能每拍都跑
        //   （WT_SlotDelegateInfo 会拼字符串 + 遍历 InvocationList）。改成 1Hz 采样，
        //   诊断能力不变（绑定变化是低频事件），但每拍开销降到几乎为零。
        static double LastDelScan = -1000.0;
        const double  NowDel      = FPlatformTime::Seconds();
        const bool    bDoDel      = (NowDel - LastDelScan) >= 1.0;
        if (bDoDel)
        {
            LastDelScan = NowDel;
        }
        for (const TPair<int32, UUserWidget*>& KV : Slots)
        {
            if (!bDoDel)
            {
                break;   // 1Hz 采样：本拍不做委托链诊断（悬停检测仍在下面照常跑）
            }
            if (!KV.Value)
            {
                continue;
            }
            UUserWidget* InvSlot = WT_FindInvSlot(KV.Value);
            if (!InvSlot)
            {
                continue;
            }
            const FString DKey = FString::Printf(TEXT("%s/%d"), *InvSlot->GetName(), KV.Key);
            const FString Info = WT_SlotDelegateInfo(InvSlot);
            FString& SeenD = DelSeen.FindOrAdd(DKey);
            if (SeenD != Info)
            {
                SeenD = Info;
                WT_WARNF(TEXT("[shard-del] slot%d%s"), KV.Key, *Info);
            }
        }
        static TWeakObjectPtr<UUserWidget> LastHover;
        UUserWidget* NowHover = nullptr;
        int32        NowHoverIdx = -1;
        for (const TPair<int32, UUserWidget*>& KV : Slots)
        {
            if (!KV.Value)
            {
                continue;
            }
            // ★1.0.8AJ：鼠标实际落在**内层** Widget_InventorySlot_C 上，外层 Wrapper 的
            //   IsHovered() 常年为 false（08AI 的 `[shard-hover]` 一行都没有就是这个原因）。
            UUserWidget* Inner = WT_FindInvSlot(KV.Value);
            if (KV.Value->IsHovered() || (Inner && Inner->IsHovered()))
            {
                NowHover    = KV.Value;
                NowHoverIdx = KV.Key;
                break;
            }
        }
        if (NowHover && NowHover != LastHover.Get())
        {
            LastHover = NowHover;
            WT_WARNF(TEXT("[shard-hover] slot%d %s"), NowHoverIdx, *NowHover->GetName());
        }
    }

    // ---- A 路（1.0.8U：无条件执行，不再被 B 挡住） ----
    //   08T 把 A 路挂在 `!bBApplied` 后面，而 B 路只要扫到引用就置 bBApplied=true
    //   （日志 changed=3/3）⇒ A 路一次都没跑（`cost satisfied` 计数 = 0），
    //   所以橙条和 "1/1" 根本没画出来。现在 A 路是唯一执行路径。
    const bool bDump = (LastDumpPanel.Get() != Panel) || (LastDumpWant != Want);
    if (bDump)
    {
        LastDumpPanel = Panel;
        LastDumpWant  = Want;
    }
    if (bDump && Slots.Num() > 0)
    {
        // B 路生效时也要留一条状态行，否则下一轮没法判断「B 到底有没有画出来」。
        UUserWidget** First = Slots.Find(0);
        if (First && *First)
        {
            TArray<UWidget*> Deep;
            WT_CollectWidgetsDeep(*First, Deep, 4);
            FString State;
            for (UWidget* X : Deep)
            {
                if (UProgressBar* B = Cast<UProgressBar>(X))
                {
                    State += FString::Printf(TEXT("bar vis=%d pct=%.2f "),
                                             static_cast<int32>(B->GetVisibility()), B->GetPercent());
                }
                else if (UTextBlock* T = Cast<UTextBlock>(X))
                {
                    State += FString::Printf(TEXT("lbl '%s' "), *T->GetText().ToString());
                }
            }
            WT_WARNF(TEXT("[shard-ui] slot state (B=%d ours=%d want=%d) slot0: %s"),
                     bBApplied ? 1 : 0, bOurs ? 1 : 0, Want, *State);
        }
    }

    // ---- A 路执行：已占用槽翻成「成本已满足」 ----
    //   ★1.0.8X 顺序：先叫原版自己重算（B 路已把成本类换成我们的类），
    //     原版算对了就不用 A 路动手（WT_MarkCostSlotSatisfied 里有自愈判据）。
    if (bOurs && Factory)
    {
        // ★1.0.8AJ：只处理**真的有碎片**的格（逐格判，不按碎片总数裁前 N 格）。
        for (int32 i = 0; i < 3; ++i)
        {
            if (!SlotShardN.IsValidIndex(i) || SlotShardN[i] <= 0)
            {
                continue;
            }
            if (UUserWidget** Found = Slots.Find(i))
            {
                WT_TryVanillaRefresh(*Found, *FString::Printf(TEXT("slot%d"), i));
            }
        }
    }
    if (bOurs)
    {
        for (int32 i = 0; i < 3; ++i)
        {
            if (!SlotShardN.IsValidIndex(i) || SlotShardN[i] <= 0)
            {
                continue;
            }
            UUserWidget** Found = Slots.Find(i);
            if (!Found || !*Found)
            {
                continue;
            }
            WT_MarkCostSlotSatisfied(*Found, i, Want, bDump);
        }

        // ---- ★1.0.8W：已占用格**必须**不透明，任何「压暗」都不许写到已占用的格上 ----
        //   原版用 `Widget_InventorySlot` 的 RenderOpacity 表达「这格里有碎片(1.00) / 空槽(0.40)」。
        //   08U 的 WT_ApplySlotHighlight 想抄「格底色深浅两档」，结果差分抓到的偏偏是这个
        //   opacity（日志实锤 `Widget_InventorySlot[op 1.00/0.40]`），把已占用格写回 0.40 ⇒
        //     ① 图标只剩四成亮度 ⇒ 用户 15:53「后面两个插槽显示浅灰色」；
        //     ② 那格在外观上等于空槽，原版按「空格」处理 ⇒ 点不动 ⇒
        //        用户 2026-10-27「只有插槽1能取下来，后面插槽2和3取不下来」。
        //   （08T 时 A 路被 B 路门控掉、一格都没抬过 opacity ⇒ 三格全取不下来，同源。）
        //   ⇒ 已占用格统一 1.00，且放在所有其它处理之后，保证是本拍最后一次写。
        //   ★1.0.8AJ：按**这一格有没有碎片**逐个设，不再按「碎片总数」裁前 N 格
        //   （否则取走第 1 片后，真正装着碎片的 idx2 那格不会被设回 1.00 ⇒ 半透明变灰）。
        for (int32 i = 0; i < 3; ++i)
        {
            if (!SlotShardN.IsValidIndex(i) || SlotShardN[i] <= 0)
            {
                continue;
            }
            UUserWidget** Found = Slots.Find(i);
            if (!Found || !*Found)
            {
                continue;
            }
            TArray<UWidget*> Deep;
            WT_CollectWidgetsDeep(*Found, Deep, 4);
            for (UWidget* X : Deep)
            {
                WT_ForceOpaque(X);
            }
            WT_ForceOpaque(*Found);
        }
        if (bDump)
        {
            FString Ops;
            for (int32 i = 0; i < 3; ++i)
            {
                UUserWidget** Found = Slots.Find(i);
                if (Found && *Found)
                {
                    Ops += FString::Printf(TEXT(" slot%d=%.2f(has=%d)"), i,
                                           (*Found)->GetRenderOpacity(),
                                           SlotShardN.IsValidIndex(i) ? SlotShardN[i] : 0);
                }
            }
            WT_WARNF(TEXT("[shard-ui] opaque forced:%s (want=%d)"), *Ops, Want);
        }
    }

    // ---- V 路（1.0.8V 透明取回按钮）：★1.0.8W 起关闭 ----
    //   用户否决（「加按钮干什么」）：叠透明命中区是界面层补丁，且按钮吞掉拖拽。
    //   真正的修复在上面「已占用格强制不透明」。代码保留待用，开关默认 false。
    static constexpr bool WT_SHARD_TAKE_BTN = false;
    if (WT_SHARD_TAKE_BTN)
    {
        for (int32 i = 0; i < 3; ++i)
        {
            UUserWidget** Found = Slots.Find(i);
            if (!Found || !*Found)
            {
                continue;
            }
            const bool bMount = bOurs && (i < Want) && (Factory != nullptr);
            WT_EnsureTakeButton(*Found, Factory, i, bMount);
        }
    }

    // ---- 1.0.8W 取证：潜在库存变化监视（只在本拍签名变化时打一行） ----
    //   玩家点一次槽 → 若这行出现 `-> [0]... x2` 说明原版确实取出来了（问题在我们的显示）；
    //   若点完毫无新行，说明原版压根没触到移除逻辑（格被当成空槽）。
    if (Factory)
    {
        static TWeakObjectPtr<AFGBuildableFactory> WatchF;
        static FString LastSig;
        const FString Sig = WT_PotInvSignature(Factory);
        if (WatchF.Get() != Factory || Sig != LastSig)
        {
            WT_WARNF(TEXT("[shard-ui] pot inv: %s -> %s | shardsN=%d want=%d"),
                     LastSig.IsEmpty() ? TEXT("<init>") : *LastSig, *Sig, ShardN, Want);
            WatchF    = Factory;
            LastSig   = Sig;
        }
    }
}


// 把一个材质（动态材质）当前的全部参数打成一行，供下一版精确改色用。
//   ★为什么不能直接猜参数名（如 "EmptyColor"）：原版这个 MID 是蓝图材质，
//   参数名只有运行时枚举得出来；猜错 = 静默无效，还要再烧一轮编译+重启。
static FString WT_DescribeMaterialParams(UMaterialInterface* MI)
{
    if (!MI)
    {
        return FString();
    }
    UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(MI);
    FString Out;

    TArray<FMaterialParameterInfo> Info;
    TArray<FGuid> Ids;

    MI->GetAllVectorParameterInfo(Info, Ids);
    for (const FMaterialParameterInfo& P : Info)
    {
        FLinearColor V = FLinearColor::Black;
        if (MID)
        {
            V = MID->K2_GetVectorParameterValue(P.Name);
        }
        else
        {
            MI->GetVectorParameterValue(FHashedMaterialParameterInfo(P), V);
        }
        Out += FString::Printf(TEXT(" [V]%s=(%.2f,%.2f,%.2f,%.2f)"),
                               *P.Name.ToString(), V.R, V.G, V.B, V.A);
    }

    Info.Reset();
    Ids.Reset();
    MI->GetAllScalarParameterInfo(Info, Ids);
    for (const FMaterialParameterInfo& P : Info)
    {
        float V = 0.f;
        if (MID)
        {
            V = MID->K2_GetScalarParameterValue(P.Name);
        }
        else
        {
            MI->GetScalarParameterValue(FHashedMaterialParameterInfo(P), V);
        }
        Out += FString::Printf(TEXT(" [S]%s=%.3f"), *P.Name.ToString(), V);
    }
    return Out;
}

// ============================================================================
// ★1.0.8 原版超频面板的三处口径对齐（用户 2026-10-27 三张截图）
//
//   ① 刻度文字还是「1 片 = +50%」时代的 150% / 200% / 250%，而我们的碎片是 1 片 = +100%
//      ⇒ 滑条拖到写着 150% 的位置，机器真实值就是 200%（用户原话「150 对应 200，以此类推」）。
//      刻度是按档位等距画的、值 = 位置 × max，所以**只改文字**就能完全对齐：
//      150%→200%、200%→300%、250%→400%。
//   ② 「不拉动滑条，滑条背景为黑」——★1.0.8d 实测更正：面板里**没有 UProgressBar**，
//      滑条 = `Image mProgressBar`（动态材质自己画橙/灰两段）+ `Slider MainSlider`。
//      ⇒ 改 UProgressBar WidgetStyle 是死代码（上一版就是这样，从没生效过）。
//      现在的做法是在 mProgressBar 下一层垫一张纯黑 Image（布局复刻），只有插我们的碎片才露出。
//   ③ 三格碎片槽只有第一格显示碎片图标（见下面的槽位 dump）。
//
// 为什么只能运行时改：原版超频面板是**纯 UMG 蓝图**（SDK 的 Public/ 里没有任何
//   Overclock/Potential 的 UUserWidget 子类），没有 C++ 类可继承、也没有配置项。
// 识别特征 = 同一棵控件树里同时有 "0%"/"150%"/"250%" 三个刻度文本（只有超频面板有）。
// 开销：命中后只碰缓存的那 4 个控件；未命中时 1.5s 才扫一次（TObjectIterator 是 O(全部 UObject)）。
//
// ---------------------------------------------------------------------------
// ★1.0.8e 崩溃修复（用户 2026-10-27 12:48 Factory Crash Reporter）：
//
//   Assertion failed: ((UObject*)ContainerPtr)->IsA(GetOwner<UClass>())
//     'Widget_InventorySlot_DropArea' is of class 'Widget_InventorySlot_DropArea_C'
//     however property 'mInteractObject' belongs to class 'FGInteractWidget'
//
//   根因：滑条所在的 `Widget_InventorySlot_DropArea_C` 是**纯 UMG 蓝图 UUserWidget**，
//   不是 UFGInteractWidget。把 FGInteractWidget 的属性套到别的类实例上，引擎在
//   FProperty::ContainerPtrToValuePtr 里 checkf(Container->IsA(GetOwner<UClass>()))
//   （UnrealType.h:700/704，Shipping 保留，不是 ensure）⇒ 直接崩客户端。
//   1.0.8d 的写法 `InteractProp->ContainerPtrToValuePtr<void>(W)` 就是这一处。
//
//   ⇒ 读 mInteractObject 之前必须先拿到**真正的 UFGInteractWidget**，并且用 IsA 再验一次
//     容器类（双保险：任何路径都不许把属性套到别的类实例上）。
//     机器怎么找：① 滑条面板的 Outer 链；② 在屏的交互面板里"控件树包含本面板"的那个；
//     ③ 全屏只有一台可见机器面板时直接用它。三条都失败 ⇒ 返回 nullptr，
//     此时 bOurs=false、面板一个像素都不碰（等价原版），但**绝不崩**。
// ---------------------------------------------------------------------------

/** 安全读 mInteractObject：容器类与属性所属类不符时返回 nullptr（引擎 checkf 会崩客户端）。 */
static UObject* WT_ReadInteractObject(UObject* Container, FObjectProperty* Prop)
{
    if (!Container || !Prop)
    {
        return nullptr;
    }
    UClass* OwnerCls = Prop->GetOwnerClass();   // FGInteractWidget（FField::GetOwnerClass，Field.h:421）
    if (!OwnerCls || !Container->IsA(OwnerCls))
    {
        return nullptr;   // ★防线：类不匹配一律不读
    }
    return Prop->GetObjectPropertyValue(Prop->ContainerPtrToValuePtr<void>(Container));
}

/** Root 的控件树里（**含嵌套 UserWidget 内部**）有没有 Target。
 *  GetAllWidgets 只到本层（WidgetTree.cpp:ForEachWidget），所以这里用 ForEachWidgetAndDescendants。 */
static bool WT_TreeHasWidget(UUserWidget* Root, UWidget* Target)
{
    if (!Root || !Root->WidgetTree || !Target)
    {
        return false;
    }
    bool bFound = false;
    Root->WidgetTree->ForEachWidgetAndDescendants([&bFound, Target](UWidget* X)
    {
        if (X == Target)
        {
            bFound = true;
        }
    });
    return bFound;
}

/** 超频滑条面板（可能是嵌套的纯 UUserWidget）→ 它属于哪台机器。全失败返回 nullptr。 */
static AFGBuildableFactory* WT_PanelToFactory(UUserWidget* Panel, FString& OutWhy)
{
    OutWhy = TEXT("<unresolved>");
    if (!Panel)
    {
        OutWhy = TEXT("<no panel>");
        return nullptr;
    }
    FObjectProperty* Prop = CastField<FObjectProperty>(
        UFGInteractWidget::StaticClass()->FindPropertyByName(TEXT("mInteractObject")));
    if (!Prop)
    {
        OutWhy = TEXT("<mInteractObject missing>");
        return nullptr;
    }

    // ① Outer 链：控件树里的子控件 Outer=WidgetTree，WidgetTree 的 Outer=所属面板
    if (UFGInteractWidget* IW = Panel->GetTypedOuter<UFGInteractWidget>())
    {
        if (AFGBuildableFactory* F = Cast<AFGBuildableFactory>(WT_ReadInteractObject(IW, Prop)))
        {
            OutWhy = FString::Printf(TEXT("outer -> %s"), *F->GetClass()->GetName());
            return F;
        }
    }

    // ② / ③ 扫在屏的交互面板
    UWorld* PanelWorld = Panel->GetWorld();
    AFGBuildableFactory* OnlyCandidate = nullptr;
    int32 CandidateCount = 0;
    for (TObjectIterator<UFGInteractWidget> It; It; ++It)
    {
        UFGInteractWidget* IW = *It;
        if (!IW || IW->HasAnyFlags(RF_ClassDefaultObject) || IW->GetWorld() != PanelWorld || !IW->IsVisible())
        {
            continue;
        }
        AFGBuildableFactory* F = Cast<AFGBuildableFactory>(WT_ReadInteractObject(IW, Prop));
        if (!F)
        {
            continue;   // 不是机器面板（终端/商店/工作台……）
        }
        if (WT_TreeHasWidget(IW, Panel))
        {
            OutWhy = FString::Printf(TEXT("tree -> %s"), *F->GetClass()->GetName());
            return F;
        }
        OnlyCandidate = F;
        ++CandidateCount;
    }
    if (CandidateCount == 1)
    {
        OutWhy = FString::Printf(TEXT("single -> %s"), *OnlyCandidate->GetClass()->GetName());
        return OnlyCandidate;
    }
    OutWhy = FString::Printf(TEXT("<unresolved: %d visible machine panels>"), CandidateCount);
    return nullptr;
}

// ★1.0.8g 从 UImage 上取它正在用的动态材质（原版滑条 = `Image mProgressBar` + `MID_MM_UI_OverclockingSlider`）。
//   `Brush` 是 UPROPERTY(protected)，SML 桩里没有可靠的公开 getter，统一走反射读结构体 ——
//   读法与下面 dump 段完全一致（同源安全：Brush 属于 UImage，Img 就是 UImage 实例）。
static UMaterialInstanceDynamic* WT_ImageMID(UImage* Img)
{
    if (!Img)
    {
        return nullptr;
    }
    FStructProperty* SP = CastField<FStructProperty>(
        UImage::StaticClass()->FindPropertyByName(TEXT("Brush")));
    if (!SP)
    {
        return nullptr;
    }
    FSlateBrush* B = SP->ContainerPtrToValuePtr<FSlateBrush>(Img);
    return B ? Cast<UMaterialInstanceDynamic>(B->GetResourceObject()) : nullptr;
}

// ★1.0.8h 刻度标签守卫（用户 2026-10-27 13:46「位置不对」→ 14:4x「和原版一样只是修改上限和位置」）。
//
//   原版标签就摆在 0/40/60/80/100% —— **正好是材质分段边界**（Big:Small = 2:1:1:1）。
//   1.0.8h 曾把中间三档搬到 25/50/75%（当时把 Big 也改成 1.0 做四等分），
//   1.0.8L 起条恢复原版形态、刻度数字改成 0/100/200/300/400，**位置一个都不动**。
//
//   这个函数现在只做「守卫」：记下原版 FAnchorData，一旦发现被谁挪过就写回原样。
//   返回空串 = 位置已是原版；非空 = 无法校对的原因（一个像素都不动）。
static FString WT_LayoutBarLabels(TWeakObjectPtr<UTextBlock>* Labels, int32 Num, bool bOurs)
{
    static FAnchorData GOrigLayout[5];
    static bool        GOrigValid = false;

    if (Num < 5)
    {
        return TEXT("need 5 labels");
    }

    UTextBlock*       L[5] = { nullptr };
    UCanvasPanelSlot* S[5] = { nullptr };
    for (int32 i = 0; i < 5; ++i)
    {
        L[i] = Labels[i].Get();
        if (!L[i])
        {
            return TEXT("label missing");
        }
        // 只处理 canvas 摆放的标签：其它容器（HBox/Grid）里改 offset 语义完全不同，
        // 一旦遇到就把控件类打进日志，等看到真实结构再写对应实现。
        S[i] = Cast<UCanvasPanelSlot>(L[i]->Slot);
        if (!S[i])
        {
            return FString::Printf(TEXT("label %d slot=%s (not canvas)"), i,
                                   L[i]->Slot ? *L[i]->Slot->GetClass()->GetName() : TEXT("<null>"));
        }
    }

    if (!GOrigValid)
    {
        for (int32 i = 0; i < 5; ++i)
        {
            const FGeometry& G = L[i]->GetCachedGeometry();
            if (G.GetAbsoluteSize().X <= 0.f || G.GetAbsolutePosition().IsZero())
            {
                return TEXT("geometry not ready");   // 还没画出来，下一拍再试
            }
        }
        for (int32 i = 0; i < 5; ++i)
        {
            GOrigLayout[i] = S[i]->GetLayout();
        }
        GOrigValid = true;
    }

    // ★1.0.8L 用户口径「和原版一样，只是修改上限和位置」：标签**不挪位**。
    //   原版 0/40/60/80/100% 正好就是材质分段边界（Big:Small = 2:1:1:1），
    //   1.0.8h 把中间三档搬到 25/50/75% 反而离开了边界（用户「位置不对」）。
    //   这里只做一件事：槽位一旦和原版不一致（旧逻辑或蓝图挪过），按原样写回。
    (void)bOurs;
    for (int32 i = 0; i < 5; ++i)
    {
        const float Was = S[i]->GetLayout().Offsets.Left;
        if (!(S[i]->GetLayout() == GOrigLayout[i]))
        {
            S[i]->SetLayout(GOrigLayout[i]);
            WT_WARNF(TEXT("[shard-ui] label %d restored to vanilla x=%.1f (was %.1f)"),
                     i, GOrigLayout[i].Offsets.Left, Was);
        }
    }
    return FString();
}

// ★1.0.8h 读 AFGBuildableFactory 自己的 float 属性。属性由本类查出、容器就是本类实例 ⇒ 同源安全。
static float WT_FactoryFloat(AFGBuildableFactory* F, const TCHAR* PropName)
{
    if (!F)
    {
        return -1.f;
    }
    FFloatProperty* P = CastField<FFloatProperty>(
        AFGBuildableFactory::StaticClass()->FindPropertyByName(PropName));
    return P ? *P->ContainerPtrToValuePtr<float>(F) : -1.f;
}

// ★1.0.8k 面板判据的第二种「在用我们的超频」证据。
//
//   1.0.8d～8j 只认「mInventoryPotential 槽里有我们的碎片」，而 1.0.8j 的日志里出现过
//   38.019 ARMED → 42.022 `potential has our shard=0` 的翻转：玩家把悬停值拖到 400% 并
//   确认后，槽里的碎片可能被消耗/锁定，判据立刻变 false ⇒ 面板当场退回原版 250% 刻度、
//   滑块量程也被写回 1.0，看起来就是「白忙一场」。
//   原版任何碎片组合都封顶 250%（3 片 × +50%），所以「正在以 >255% 运行」只可能是我们
//   带来的 —— 拿它做补充判据，取走碎片后 pending 会自己掉回 250% 以下，状态可自愈。
static bool WT_FactoryRunsAboveVanillaCap(AFGBuildableFactory* F)
{
    if (!F)
    {
        return false;
    }
    const float Cur  = WT_FactoryFloat(F, TEXT("mCurrentPotential"));
    const float Pend = WT_FactoryFloat(F, TEXT("mPendingPotential"));
    return Cur > 2.55f || Pend > 2.55f;
}

/**
 * ★1.0.8i 超频面板一次性探针（用户「超频上限滑动到 400% 了依旧是给的 250%」）。
 *
 * 要回答的是同一个问题：**滑条填充的分母到底是哪个值**。
 *   · 机器上：inst max / current / pending / min，以及该类 CDO 的 max（对比实例是不是被谁改过）
 *   · 面板上：MainSlider 的 min / max / value（UI 自己认为的上限）
 *   · 材质上：Value / NumShards / BigBarRatio / SmallBarRatio，并反算 denom = current / Value
 *     —— denom 就是填充真正的分母。denom == instMax ⇒ 我们的公式成立；
 *        denom != instMax ⇒ 分母是「instMax + 碎片增量」之类，得换算法（下一版照数据改）。
 */
static FString WT_ProbeOverclock(AFGBuildableFactory* F, UMaterialInstanceDynamic* MID,
                                USlider* Slider, UEditableText* PctText)
{
    if (!F)
    {
        return TEXT("<no factory>");
    }
    const float InstMax  = WT_FactoryFloat(F, TEXT("mMaxPotential"));
    const float InstCur  = WT_FactoryFloat(F, TEXT("mCurrentPotential"));
    const float InstPend = WT_FactoryFloat(F, TEXT("mPendingPotential"));

    float CdoMax = -1.f;
    if (UClass* C = F->GetClass())
    {
        if (AFGBuildableFactory* Cdo = Cast<AFGBuildableFactory>(C->GetDefaultObject(/*bCreateIfNeeded=*/false)))
        {
            CdoMax = WT_FactoryFloat(Cdo, TEXT("mMaxPotential"));
        }
    }
    bool bCan = false;
    if (FBoolProperty* CanProp = WT_CanChangePotentialProperty())
    {
        bCan = CanProp->GetPropertyValue_InContainer(F);
    }

    // ★1.0.8j 三件「谁是分母」的官方读数一次打全（virtual getter，SDK 里有声明，直接问游戏）：
    //   gmax = GetMaxPotential()（= mMaxPotential）、curmax = GetCurrentMaxPotential()（含碎片、
    //   滑条真正该用的上限）、gmin = GetCurrentMinPotential()。反射读的值与之对比即可确认
    //   我们的反射有没有读错对象。
    FString Out = FString::Printf(
        TEXT("inst(max=%.2f cur=%.2f pend=%.2f) cdo(max=%.2f) can=%d | get(max=%.2f curmax=%.2f min=%.2f)"),
        InstMax, InstCur, InstPend, CdoMax, bCan ? 1 : 0,
        F->GetMaxPotential(), F->GetCurrentMaxPotential(), F->GetCurrentMinPotential());

    float Pos = -1.f;
    if (Slider)
    {
        Pos = Slider->GetValue();
        Out += FString::Printf(TEXT(" | slider min=%.2f max=%.2f val=%.3f"),
                               Slider->GetMinValue(), Slider->GetMaxValue(), Pos);
    }
    if (MID)
    {
        const float Fill    = MID->K2_GetScalarParameterValue(TEXT("Value"));
        const float Shards  = MID->K2_GetScalarParameterValue(TEXT("NumShards"));
        const float Big     = MID->K2_GetScalarParameterValue(TEXT("BigBarRatio"));
        const float Small   = MID->K2_GetScalarParameterValue(TEXT("SmallBarRatio"));
        // 反算「面板自己用的分母」：pending / 填充比例。实测原版 = 2.5，且**不随 mMaxPotential 变**。
        const float Denom   = (Fill > 0.001f) ? (InstPend / Fill) : -1.f;
        Out += FString::Printf(TEXT(" | mid Value=%.3f Shards=%.0f Big=%.2f Small=%.2f | panelDenom=%.2f"),
                               Fill, Shards, Big, Small, Denom);
    }
    if (Pos > 0.001f)
    {
        Out += FString::Printf(TEXT(" | pend/pos=%.2f (must stay 2.50: panel换算恒定; pos 0..1.6 => pend 0..4.00)"),
                               InstPend / Pos);
    }
    if (PctText)
    {
        Out += FString::Printf(TEXT(" | pctText='%s'"), *PctText->GetText().ToString());
    }
    return Out;
}

/**
 * ★1.0.8Q 滑条量程 = **恒 1.0（原版）**，别再按 pending 反算了。
 *
 * 面板的换算钉死：`Value(滑条值) = pending / 2.5`，它自己写。SSlider 手柄位置 =
 * `(Value - Min) / (Max - Min)`。量程恒 1.0 时 ⇒ 手柄位置 = Value = pending/2.5，
 * 而材质填充也正好是 pending/2.5 的宽度 ⇒ **手柄永远压在橙色末端**，这是原版天然成立的关系，
 * 我们什么都不用算。
 *
 * 1.0.8k~8P 走过的三种量程都会被这个反算破坏：
 *   · 写死 1.6（4.0/2.5）：100% 时手柄在 0.4/1.6 = 25%（用户上一张截图：插 1 片手柄跳到 26%）
 *   · 写死 ceiling/2.5（1 片 = 0.8）：100% 时手柄在 50%
 *   · 按 pending 反算 Max = (pending/2.5)/填充占比：数值自洽，但会让 `Value` 一物二用
 *     （我们为了画填充要写 0.6，面板为了算 pending 要写 0.8），每拍拉锯（rewrites/s=11）
 * 结论：量程一律不碰。上限靠 `mMaxPotential`（按片数 1+N）+ ceiling 夹 pending 来管。
 */
static float WT_BarFillForPending(float Pend, float Ceiling);   // 实现在本文件后段
static float WT_PendingForBarFill(float Pos);                   // ★1.0.8R 上面那个的反函数，实现在后段

/**
 * ★1.0.8Q 每拍确认滑条量程是 1.0 / 0.0（原版）。面板若把它改成别的值，这里改回来并打日志。
 */
static FString WT_SetSliderRange(USlider* Slider, AFGBuildableFactory* F, bool bOurs, float Ceiling)
{
    if (!Slider)
    {
        return TEXT("no slider");
    }
    const float Pend    = F ? F->GetPendingPotential() : 1.f;
    // ★1.0.8Q 量程恒为 1.0（原版）：手柄位置 = Value = pending/2.5 = 橙色末端，天然重合。
    //   1.0.8k 写死 1.6、1.0.8N 写死 ceiling/2.5、1.0.8P 按 pending 反算 —— 三种都会让手柄
    //   离开橙色末端（用户 15:08 那两张截图：填充 40%、手柄 26%）。原版之所以永不错位，
    //   就是因为量程恒 1.0、值恒 pending/2.5。别再动了。
    const float WantMax = 1.0f;
    const float WantMin = 0.0f;

    static TWeakObjectPtr<USlider> LastLogged;
    static float LastLoggedMax = -1.f;
    static int32 FixCount = 0;          // 本轮重新写入量程的次数（诊断「谁在反复重置」）
    static double FixWindow = 0.0;

    bool bChanged = false;
    if (!FMath::IsNearlyEqual(Slider->GetMinValue(), WantMin, 0.001f))
    {
        Slider->SetMinValue(WantMin);
        bChanged = true;
    }
    if (!FMath::IsNearlyEqual(Slider->GetMaxValue(), WantMax, 0.001f))
    {
        // 只改量程、不动 Value：UMG 的 SetMaxValue 不回写 Value（引擎 `Slider.cpp:196`），
        // 手柄画到哪儿由 SSlider 按 (Value-Min)/(Max-Min) 自己算。
        Slider->SetMaxValue(WantMax);
        bChanged = true;
    }
    if (bChanged && (LastLogged.Get() != Slider
                     || !FMath::IsNearlyEqual(LastLoggedMax, WantMax, 0.001f)))
    {
        LastLogged = Slider;
        LastLoggedMax = WantMax;
        FixCount = 0;
        FixWindow = FPlatformTime::Seconds();
        WT_WARNF(TEXT("[shard-ui] slider max -> %.3f (ours=%d | pend=%.2f frac=%.3f knob=%.1f%% | ceiling=%.0f%% | val=%.3f)"),
                 Slider->GetMaxValue(), bOurs ? 1 : 0, Pend,
                 WT_BarFillForPending(Pend, Ceiling),
                 (Slider->GetMaxValue() > 0.001f ? Slider->GetValue() / Slider->GetMaxValue() * 100.f : 0.f),
                 Ceiling * 100.f, Slider->GetValue());
    }
    else if (bChanged)
    {
        // 同一个量程被反复写回 ⇒ 面板那边每帧都在重置它（我们改一次它改一次）。
        ++FixCount;
        const double Now = FPlatformTime::Seconds();
        if (Now - FixWindow > 2.0)
        {
            if (FixCount > 20)
            {
                WT_WARNF(TEXT("[shard-ui] slider max THRASHED %d times in %.1fs (panel resets it) | want=%.3f got=%.3f"),
                         FixCount, Now - FixWindow, WantMax, Slider->GetMaxValue());
            }
            FixCount = 0;
            FixWindow = Now;
        }
    }
    return FString();
}

/**
 * ★1.0.8k 数值兜底：**跟随**面板口径（2.5 × pos），只在面板没算对时补一笔。
 *
 * 1.0.8j 的教训：抢方向盘（自己按 4.0 算）会让面板把 pending 反算回滑块 ⇒ 位置溢出。
 * 量程一改，面板自己算出来就是 4.0，本函数绝大多数帧什么都不做；留着是为了兜住
 * 「某些机器子类在蓝图里改写了 OnValueChanged」这种情况，触发时打 `drift` 便于定位。
 */
static void WT_VerifyOverclock(AFGBuildableFactory* F, USlider* Slider, bool bOurs, float Ceiling)
{
    if (!F || !Slider)
    {
        return;
    }
    // ★1.0.8AL：**碎片清零 ⇒ 超频必须降回 100%**（用户 2026-10-28 00:18「三个取出来后，没有降频」）。
    //   实测日志（16:19:43~48）三片全取走后：`inst(max=5.00 cur=1.00 pend=2.00)` + `pctText='200%'`
    //   ⇒ 机器的 `mPendingPotential` 一直停在取走前最后一次的 2.00，没有任何人去动它：
    //     · 原版面板只在**拖动滑条**时改 pending，取走碎片它不管；
    //     · `WT_ApplyCeiling` 的纪律是「只抬不降」（研究阶段抬过的 5.00 不许按回 1.00），
    //       而且它管的是 `mMaxPotential`，不是 pending；
    //     · `WT_VerifyOverclock` 原来开头 `!bOurs` 直接 return ⇒ 无碎片时整段撒手。
    //   ⇒ 这里补一条收尾：**曾经被我们接管过**的机器（EverOurs），碎片清零那一拍把 pending 拉回 1.0，
    //     并把滑条摆到原版 100% 的位置（val = 1.0/2.5 = 0.40，面板据此自己把文字刷回 100%）。
    //   只认「我们自己带起来的超频」：从没被我们碰过的机器（玩家用原版碎片拖到 250%）一个字节都不动。
    static TMap<TWeakObjectPtr<AFGBuildableFactory>, uint8> EverOurs;
    if (EverOurs.Num() > 64)
    {
        EverOurs.Reset();   // 极简防泄漏：面板来来去去，清空重来没有副作用（只会漏掉一次收尾）
    }
    if (!bOurs)
    {
        if (EverOurs.Remove(F) <= 0)
        {
            return;   // 从没被我们接管过 ⇒ 不碰
        }
        const float Pend = F->GetPendingPotential();
        if (Pend > 1.01f)
        {
            F->SetPendingPotential(1.0f);
            if (!FMath::IsNearlyEqual(F->GetPendingPotential(), 1.0f, 0.02f))
            {
                if (FFloatProperty* PP = CastField<FFloatProperty>(
                        AFGBuildableFactory::StaticClass()->FindPropertyByName(TEXT("mPendingPotential"))))
                {
                    PP->SetPropertyValue_InContainer(F, 1.0f);
                }
            }
            // 原版换算恒定 = 2.5 × pos ⇒ 100% 的滑条位置是 0.40（实测 probe：val=0.400 ↔ pctText='100%'）
            if (Slider->GetMaxValue() > 0.001f)
            {
                Slider->SetValue(FMath::Clamp(1.0f / WT_PANEL_POTENTIAL_DENOM, 0.f, 1.f));
            }
            WT_WARNF(TEXT("[shard-ui] shards cleared -> pending %.2f -> %.2f (overclock removed) | %s"),
                     Pend, F->GetPendingPotential(), *F->GetClass()->GetName());
        }
        return;
    }
    EverOurs.Add(F, 1);
    // ★1.0.8R 位置语义改了：滑条的 Value **不再是面板的 pending/2.5，而是「条内比例」**
    //   （= 橙色填充末端 = 手柄位置）。因为填充分段（0.4/0.6/0.8/1.0），
    //   面板那套 `2.5 × pos` 会把 60% 的位置算成 150% —— 手柄与橙色末端同时错。
    //   换算统一走 WT_PendingForBarFill / WT_BarFillForPending 这一对互逆函数。
    float Pos = FMath::Clamp(Slider->GetValue(), 0.f, 1.f);

    float Want = WT_PendingForBarFill(Pos);
    const float MinP = F->GetCurrentMinPotential();
    const float Clamped = FMath::Clamp(Want, MinP, Ceiling);
    if (!FMath::IsNearlyEqual(Clamped, Want, 0.02f))
    {
        // 拖过上限（或低于下限）⇒ 把手柄按回边界，别让它停在条外／停在空档里
        Want = Clamped;
        const float EdgePos = WT_BarFillForPending(Want, Ceiling);
        if (FMath::Abs(Pos - EdgePos) > 0.005f)
        {
            Slider->SetValue(EdgePos);
            Pos = EdgePos;
            static TWeakObjectPtr<AFGBuildableFactory> LastEdgeF;
            static float LastEdgeAt = -1.f;
            if (LastEdgeF.Get() != F || !FMath::IsNearlyEqual(LastEdgeAt, EdgePos, 0.01f))
            {
                LastEdgeF = F;
                LastEdgeAt = EdgePos;
                WT_WARNF(TEXT("[shard-ui] handle pulled to ceiling: pos -> %.3f (=%.0f%% of bar, pending %.2f / ceiling %.2f)"),
                         EdgePos, WT_BarFillForPending(Want, Ceiling) * 100.f, Want, Ceiling);
            }
        }
    }

    const float Cur  = F->GetPendingPotential();
    if (FMath::IsNearlyEqual(Cur, Want, 0.02f))
    {
        // pending 已经对了：这一拍只剩「保证手柄压在橙色末端」这一件事。
        // 面板若按自己的 `pending/2.5` 摆过手柄（1 片 200% 会被摆到 80%，而我们要求 60%），
        // 这里写回；日志里的 fight 计数就是「抢不抢得过面板」的判据。
        const float WantPos = WT_BarFillForPending(Cur, Ceiling);
        if (FMath::Abs(Pos - WantPos) > 0.005f)
        {
            Slider->SetValue(WantPos);
            static double LastFightS = 0.0;
            static int32  Fights = 0;
            ++Fights;
            const double NowS = FPlatformTime::Seconds();
            if (NowS - LastFightS > 1.0)
            {
                WT_WARNF(TEXT("[shard-ui] handle fight pos=%.3f -> %.3f (pend=%.2f) | rewrites/s=%d"),
                         Pos, WantPos, Cur, Fights);
                Fights = 0;
                LastFightS = NowS;
            }
        }
        return;   // 面板自己算的就是对的：这一帧不写 pending
    }

    F->SetPendingPotential(Want);
    bool bByProp = false;
    if (!FMath::IsNearlyEqual(F->GetPendingPotential(), Want, 0.02f))
    {
        // 内部 clamp 掉了（或虚函数在蓝图子类里被改写）⇒ 直接写属性兜住。
        if (FFloatProperty* PP = CastField<FFloatProperty>(
                AFGBuildableFactory::StaticClass()->FindPropertyByName(TEXT("mPendingPotential"))))
        {
            PP->SetPropertyValue_InContainer(F, Want);
            bByProp = true;
        }
    }
    // 写完 pending 立刻把手柄摆到新位置（面板那边下一拍才会跟，这里先到位，避免抖一格）
    const float NewPos = WT_BarFillForPending(F->GetPendingPotential(), Ceiling);
    if (FMath::Abs(Slider->GetValue() - NewPos) > 0.005f)
    {
        Slider->SetValue(NewPos);
    }
    // 限频：同一台机器同一个位置只打一行（拖动时值是连续的，每帧打会刷屏）。
    static TWeakObjectPtr<AFGBuildableFactory> LastF;
    static float LastPos = -1.f;
    if (LastF.Get() != F || !FMath::IsNearlyEqual(LastPos, Pos, 0.002f))
    {
        LastF = F;
        LastPos = Pos;
        WT_WARNF(TEXT("[shard-ui] pending drift pos=%.3f panel=%.2f want=%.2f got=%.2f (%s) | %s"),
                 Pos, Cur, Want, F->GetPendingPotential(),
                 bByProp ? TEXT("via property") : TEXT("via SetPendingPotential"),
                 *F->GetClass()->GetName());
    }
}

/**
 * ★1.0.8L 材质填充（取代 1.0.8k 的 `pending / 4.0` 线性写法）。
 *
 * 条宽固定，材质参数 `Value` 就是「填充占整条的比例」。原版量程=1.0 时面板直接把滑块值
 * 当比例传（0.4 处正好 40% 宽，也正是原版 100% 的位置）。量程开到 1.6 后，面板要么仍传
 * 原始值（→ 填充画到 160%，溢出条外），要么传归一化值（→ 本来就对），所以这里必须自己接管。
 * 只在差 > 0.005 时写，面板本来就传对的话这里永远不触发。
 *
 * ★1.0.8L 用户口径（2026-10-27 14:4x「和原版一样只是修改上限和位置」）：
 *   条的形态**一个像素都不改** —— 大格 + 三小格（BigBarRatio=2.0 / SmallBarRatio=1.0），
 *   1.0.8k 把 Big 改成 1.0（四等分）反而把条变成「原版没有的样子」，撤回。
 *
 *   段宽是 2:1:1:1 ⇒ 刻度只能落在 0/40/60/80/100% 这四道边界上，于是：
 *       0-100%   → 第一道（大格，占全宽 40%）
 *       100-200% / 200-300% / 300-400% → 三个小格（各占全宽 20%）
 *   因此 `Value`（= 填充占整条的比例）必须**分段**算，不能像原版那样线性（pend/2.5）：
 *       pend 0 → 0.00 ；1.00 → 0.40 ；2.00 → 0.60 ；3.00 → 0.80 ；4.00 → 1.00
 *   这样每一档整百刻度都正好压在段边界上，填充与刻度不会错位。
 */
static float WT_BarFillForPending(float Pend, float Ceiling)
{
    // ★1.0.8R 回到**分段映射**（1.0.8Q 退回线性是错的，录屏实锤）：
    //   用户 15:37 录屏：插 1 片拖满 → 面板显示「运行频率 201%」，条却填到 **79%**，
    //   橙色末端压在「250%」刻度上。同一张图两套坐标 —— 因为线性 pend/2.5 是**原版**
    //   「每片 +50%」的刻度（0/100/150/200/250 在 0/40/60/80/100%）。
    //   我们的碎片是**每片 +100%**，段边界必须重挂：
    //       pend 1.00 → 0.40（大格末）  pend 2.00 → 0.60  pend 3.00 → 0.80  pend 4.00 → 1.00
    //   段宽仍是原版 2:1:1:1、刻度位置仍是 0/40/60/80/100%，变的只有刻度**文字**
    //   （0/100/200/300/400，见 ①）⇒ 200% 才会压在 60% 那道边界上。
    (void)Ceiling;
    const float P = FMath::Clamp(Pend, 0.f, WT_OVERCLOCK_CEILING);
    const float V = (P <= 1.f) ? (P * 0.4f) : (0.4f + (P - 1.f) * 0.2f);
    return FMath::Clamp(V, 0.f, 1.f);
}

// ★1.0.8R WT_BarFillForPending 的**反函数**：把手柄位置（= 条内比例）还原成 pending。
//   1.0.8Q 一直用面板口径 `pending = 2.5 × pos`，那是因为当时填充是线性的；
//   现在填充分段，再用 2.5 × pos 就会「拖到 60% 却是 150%」——手柄和橙色末端同时错。
//   pos 0.40 以下对应 0..100%（大格），之上每 0.20 对应 +100%。
static float WT_PendingForBarFill(float Pos)
{
    const float P = FMath::Clamp(Pos, 0.f, 1.f);
    return (P <= 0.4f) ? (P / 0.4f) : (1.f + (P - 0.4f) / 0.2f);
}

static void WT_SetBarFill(UMaterialInstanceDynamic* MID, AFGBuildableFactory* F, bool bOurs, float Ceiling)
{
    if (!MID || !F)
    {
        return;
    }
    const float Cur = MID->K2_GetScalarParameterValue(TEXT("Value"));
    if (!bOurs)
    {
        // 原版口径下我们一个字节都不碰，只兜一个溢出：玩家用我们的碎片把 pending 拖到
        // 4.00 之后又取走碎片（bOurs 变 false、量程回 1.0），面板会把 Value 传成 1.6，
        // 橙色填充直接画出条外。这时把比例压回 1.0（= 满格），等面板自己刷新再接管。
        if (Cur > 1.0f)
        {
            MID->SetScalarParameterValue(TEXT("Value"), 1.0f);
        }
        return;
    }
    const float Want = WT_BarFillForPending(F->GetPendingPotential(), Ceiling);
    if (FMath::Abs(Cur - Want) < 0.005f)
    {
        return;
    }
    MID->SetScalarParameterValue(TEXT("Value"), Want);

    // ★1.0.8M 被覆盖计数：同一个 1 秒窗口里如果反复要重写，说明有别人（面板）在按它自己的
    //   公式回写 Value —— 那就是「我们写 1.000、屏幕停在 40%」的真凶，改法必须换方向。
    static double LastLogS = 0.0;
    static int32  Rewrites = 0;
    ++Rewrites;
    const double NowS = FPlatformTime::Seconds();
    if (NowS - LastLogS > 1.0)
    {
        WT_WARNF(TEXT("[shard-ui] bar fill %.3f -> %.3f | rewrites/s=%d | mid=%s"),
                 Cur, Want, Rewrites, *MID->GetName());
        Rewrites = 0;
        LastLogS = NowS;
    }
}

// ============================================================================
static void WT_PatchOverclockPanel()
{
    static TWeakObjectPtr<UTextBlock>   Cached150;      // mBarText_2（原版 150% 那一格）
    static TWeakObjectPtr<UTextBlock>   Cached200;      // mBarText_3
    static TWeakObjectPtr<UTextBlock>   Cached250;      // mBarText_4
    // ★1.0.8h 五档刻度全都要（mBarText_0..4）：改段宽后标签位置也得跟着走，
    //   否则「100%」还停在原版 40% 处，跟橙色填充（25% 处）对不上（用户「位置不对」）。
    static TWeakObjectPtr<UTextBlock>   CachedBarLabels[5];
    static int32 LastLabelMode = -1;   // -1 未设 / 0 原版 / 1 等分（避免每拍重排）
    static TWeakObjectPtr<UProgressBar> CachedBar;      // ★实测恒为空：原版面板根本没有 UProgressBar
    static TWeakObjectPtr<UImage>       CachedBarImg;   // ⭐真正的滑条 = Image mProgressBar（动态材质）
    static TWeakObjectPtr<UMaterialInstanceDynamic> CachedBarMID;   // ★1.0.8g 滑条的动态材质（改段宽用）
    static TWeakObjectPtr<UMaterialInstanceDynamic> GeomDone;       // ★1.0.8M 已经写过原版几何的那块 MID
    static int32 LastGeomMode = -1;   // -1=未设 / 0=原版 2:1:1:1 / 1=ours 四段等宽（避免每拍重复 Set）
    static TWeakObjectPtr<USlider>      CachedSlider;   // MainSlider（真手柄）
    static TWeakObjectPtr<UEditableText> CachedPctText;   // ★1.0.8j mPercentText（面板自己的百分比显示）
    // ★1.0.8f：原来这里还缓存我们自己垫的黑底 Image，已撤（见下面「撤掉黑底垫图」）。
    static TWeakObjectPtr<UUserWidget> CachedPanel;    // ★1.0.8e 命中滑条的那个面板（解机器要用它）
    static TWeakObjectPtr<AFGBuildableFactory> CachedFactory;   // 面板对应的机器（判"槽里是不是我们的碎片"）
    static bool   bDumpedTree = false;
    static double LastScanSeconds = -1000.0;

    // ★1.0.8e 面板关掉后缓存必须作废：原版交互面板可能是「复用 + Collapsed」而不是销毁，
    //   缓存不清的话，玩家换一台机器再开面板，新的那棵控件树永远轮不到补丁（表现为
    //   「换机器后刻度又变回 150%」）。这里一旦发现缓存的画板不在屏上，全清、下一拍重扫。
    if (Cached250.IsValid() && CachedPanel.IsValid() && !CachedPanel->IsVisible())
    {
        Cached150.Reset();
        Cached200.Reset();
        Cached250.Reset();
        for (int32 i = 0; i < 5; ++i) { CachedBarLabels[i].Reset(); }
        LastLabelMode = -1;
        CachedBar.Reset();
        CachedBarImg.Reset();
        CachedBarMID.Reset();
        CachedPctText.Reset();
        LastGeomMode = -1;
        CachedSlider.Reset();
        CachedPanel.Reset();
        CachedFactory.Reset();
        LastScanSeconds = -1000.0;   // 立即重扫，不用等 1.5s 的节流
    }

    if (!Cached250.IsValid())
    {
        const double Now = FPlatformTime::Seconds();
        // ★1.0.8AM 面板发现节流 1.5s → 0.4s：这一层决定「面板刚打开到被接管」的延迟，
        //   旧版最坏要等 1.5 秒才认到面板（表现为「刚打开时滑条/刻度还是原版样」）。
        //   成本 = 一次 TObjectIterator<UUserWidget>；只在**没命中面板**时跑，命中后整段跳过。
        if (Now - LastScanSeconds < 0.4)
        {
            return;
        }
        LastScanSeconds = Now;

        for (TObjectIterator<UUserWidget> It; It; ++It)
        {
            UUserWidget* W = *It;
            if (!W || !W->WidgetTree || !W->GetCachedWidget().IsValid())
            {
                continue;
            }
            // 关掉的面板仍留在内存里（Collapsed 而不是销毁）——只处理真的画在屏幕上的那棵。
            if (!W->IsVisible())
            {
                continue;
            }

            TArray<UWidget*> All;
            W->WidgetTree->GetAllWidgets(All);

            UTextBlock*   T150 = nullptr;
            UTextBlock*   T200 = nullptr;
            UTextBlock*   T250 = nullptr;
            UTextBlock*   Lbl[5] = { nullptr, nullptr, nullptr, nullptr, nullptr };
            UImage*       BarImg = nullptr;
            USlider*      Slider = nullptr;
            UEditableText* PctText = nullptr;
            UProgressBar* Bar  = nullptr;
            for (UWidget* X : All)
            {
                if (!X)
                {
                    continue;
                }
                // ★1.0.8d 按**控件名**取刻度（dump 实测名字就是 mBarText_0..4，稳定且与语言无关），
                //   文本匹配只作兜底 —— 上一版只按文本匹配，客户端换语言或蓝图重排就会失手。
                const FString N = X->GetName();
                if (UTextBlock* T = Cast<UTextBlock>(X))
                {
                    if (N == TEXT("mBarText_0"))      { Lbl[0] = T; }
                    else if (N == TEXT("mBarText_1")) { Lbl[1] = T; }
                    else if (N == TEXT("mBarText_2")) { T150 = T; Lbl[2] = T; }
                    else if (N == TEXT("mBarText_3")) { T200 = T; Lbl[3] = T; }
                    else if (N == TEXT("mBarText_4")) { T250 = T; Lbl[4] = T; }
                    else
                    {
                        const FString S = T->GetText().ToString();
                        if (S == TEXT("150%") && !T150)      { T150 = T; }
                        else if (S == TEXT("200%") && !T200) { T200 = T; }
                        else if (S == TEXT("250%") && !T250) { T250 = T; }
                    }
                }
                else if (UImage* Img = Cast<UImage>(X))
                {
                    if (N == TEXT("mProgressBar")) { BarImg = Img; }
                }
                else if (USlider* Sl = Cast<USlider>(X))
                {
                    if (!Slider) { Slider = Sl; }
                }
                else if (UEditableText* ET = Cast<UEditableText>(X))
                {
                    if (N == TEXT("mPercentText")) { PctText = ET; }
                }
                else if (!Bar)
                {
                    Bar = Cast<UProgressBar>(X);
                }
            }
            if (!T250)
            {
                continue;   // 不是超频面板
            }

            Cached150 = T150;
            Cached200 = T200;
            Cached250 = T250;
            for (int32 i = 0; i < 5; ++i) { CachedBarLabels[i] = Lbl[i]; }
            LastLabelMode = -1;   // 新面板 = 原版布局，重排状态从头来
            CachedBar = Bar;
            CachedBarImg = BarImg;
            CachedBarMID = WT_ImageMID(BarImg);
            LastGeomMode = -1;   // 新面板 = 新 MID，几何必须重设（否则沿用上一块面板的判断）
            if (BarImg && !CachedBarMID.IsValid())
            {
                WT_WARNF(TEXT("[shard-ui] bar material: mProgressBar brush is not a MaterialInstanceDynamic "
                              "-- segment geometry cannot be patched."));
            }
            CachedSlider = Slider;
            CachedPctText = PctText;
            CachedPanel = W;          // ★1.0.8e：机器靠它反解（下面 WT_PanelToFactory）
            CachedFactory = nullptr;  // 新面板 = 新机器，就地重解

            // 面板 → 机器：★1.0.8e 崩溃点已在这里删掉（原写法把 FGInteractWidget 的
            //   mInteractObject 套到纯 UUserWidget `Widget_InventorySlot_DropArea_C` 身上，
            //   引擎 checkf 直接崩）。现在统一走 WT_PanelToFactory，见本函数末尾的解析段。

            // ★1.0.8f 撤掉 1.0.8d 的「轨道黑底」垫图（用户 2026-10-27 13:19 截图「这又成啥了」）。
            //
            //   1.0.8d 以为 mProgressBar 只是那条细轨道，于是复制它的 slot 布局垫一张纯黑 Image。
            //   本次日志证明这个前提错了：
            //     Image mProgressBar brush=MID_MM_UI_OverclockingSlider size=32x32
            //       params: [S]BigBarRatio=2.000 [S]SmallBarRatio=1.000 [S]Spacing=1.000
            //               [S]NumShards=0.000 [S]Value=0.250      ← 只有 5 个 scalar，**零 vector**
            //   ⇒ 那张图的 slot 覆盖**整个面板内区**，材质自己在下部画出分段刻度条 + 橙色填充；
            //     垫在它下面的黑图就成了「顶着整个面板左半边的大黑块」（截图正是如此）。
            //   而且材质参数里没有任何颜色项（vector 一个都没有），说明橙/灰两色是材质里写死的
            //   ⇒ 「未填充段改黑」没法靠参数改，只能靠我们自己盖图。
            //   但现在没有轨道条的精确子矩形（材质内部比例，猜不得），所以先**不做**：
            //   宁可保持原版灰，也不能再画出一个大黑块。

            if (!bDumpedTree)
            {
                bDumpedTree = true;
                WT_WARNF(TEXT("[shard-ui] overclock panel: %s | widgets=%d | labels 150=%d 200=%d 250=%d | bar=%s"),
                         *W->GetClass()->GetName(), All.Num(),
                         T150 ? 1 : 0, T200 ? 1 : 0, T250 ? 1 : 0,
                         Bar ? *Bar->GetClass()->GetName() : TEXT("none"));

                // ★1.0.8h 刻度标签到底怎么摆的（锚点式还是偏移式）必须知道，否则重排只能瞎挪。
                for (int32 i = 0; i < 5; ++i)
                {
                    UTextBlock* T = Lbl[i];
                    if (!T)
                    {
                        continue;
                    }
                    if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(T->Slot))
                    {
                        const FAnchorData D = CS->GetLayout();
                        WT_WARNF(TEXT("[shard-ui] label %d slot=Canvas anchors=(%.2f,%.2f)-(%.2f,%.2f) "
                                      "align=(%.2f,%.2f) offsets=(%.1f,%.1f,%.1f,%.1f)"),
                                 i, D.Anchors.Minimum.X, D.Anchors.Minimum.Y,
                                 D.Anchors.Maximum.X, D.Anchors.Maximum.Y,
                                 D.Alignment.X, D.Alignment.Y,
                                 D.Offsets.Left, D.Offsets.Top, D.Offsets.Right, D.Offsets.Bottom);
                    }
                    else
                    {
                        WT_WARNF(TEXT("[shard-ui] label %d slot=%s parent=%s"), i,
                                 T->Slot ? *T->Slot->GetClass()->GetName() : TEXT("<null>"),
                                 T->GetParent() ? *T->GetParent()->GetClass()->GetName() : TEXT("<null>"));
                    }
                }

                // ★1.0.8j 面板自己缓存的上限藏在哪个变量里：滑条分母实测恒为 2.5、与 mMaxPotential
                //   无关，那 2.5 只可能是蓝图自己存的（BP 变量 / 常量）。把类链上所有非零 float 打一遍，
                //   一次就能定位；若它是个 UPROPERTY 变量，下一版直接写 4.0，滑条/进度/百分比
                //   就全部原生自洽，连 pending 接管都可以撤掉。
                for (UClass* C = W->GetClass(); C; C = C->GetSuperClass())
                {
                    for (TFieldIterator<FFloatProperty> FPIt(C, EFieldIteratorFlags::ExcludeSuper); FPIt; ++FPIt)
                    {
                        FFloatProperty* FP = *FPIt;
                        const float Val = FP->GetPropertyValue_InContainer(W);
                        if (FMath::Abs(Val) > 0.001f && FMath::Abs(Val) < 100000.f)
                        {
                            WT_WARNF(TEXT("[shard-ui] panel float %s.%s = %.3f"),
                                     *C->GetName(), *FP->GetName(), Val);
                        }
                    }
                }

                // 一次性 dump 控件树（类名 / 控件名 / 文本 / 图片画刷）。
                //   ★为什么需要：第三格槽位图标要按**真实控件名 + 画刷资源**改，猜不得。
                //   一句话 60 条足够覆盖这个面板（实测它只有二十来个控件）。
                int32 Budget = 60;
                for (UWidget* X : All)
                {
                    if (!X || Budget-- <= 0)
                    {
                        break;
                    }
                    FString Extra;
                    if (UTextBlock* T = Cast<UTextBlock>(X))
                    {
                        Extra = FString::Printf(TEXT(" text='%s'"), *T->GetText().ToString());
                    }
                    else if (UImage* Img = Cast<UImage>(X))
                    {
                        if (FStructProperty* SP = CastField<FStructProperty>(
                                UImage::StaticClass()->FindPropertyByName(TEXT("Brush"))))
                        {
                            FSlateBrush* B = SP->ContainerPtrToValuePtr<FSlateBrush>(Img);
                            UObject* Res = B ? B->GetResourceObject() : nullptr;
                            Extra = FString::Printf(TEXT(" brush=%s cls=%s size=%.0fx%.0f tint=(%.2f,%.2f,%.2f,%.2f)"),
                                                    Res ? *Res->GetName() : TEXT("<null>"),
                                                    Res ? *Res->GetClass()->GetName() : TEXT("-"),
                                                    B ? B->ImageSize.X : 0.f,
                                                    B ? B->ImageSize.Y : 0.f,
                                                    Img->GetColorAndOpacity().R, Img->GetColorAndOpacity().G,
                                                    Img->GetColorAndOpacity().B, Img->GetColorAndOpacity().A);
                            // ★动态材质的全部参数（橙/灰就是它自己画的）——
                            //   下一版"未填充区改黑"必须用这里的真实参数名，猜不得。
                            if (UMaterialInterface* MI = Cast<UMaterialInterface>(Res))
                            {
                                Extra += FString::Printf(TEXT(" | parent=%s | params:%s"),
                                    MI->GetMaterial() ? *MI->GetMaterial()->GetName() : TEXT("<null>"),
                                    *WT_DescribeMaterialParams(MI));
                            }
                        }
                    }
                    else if (UUserWidget* Child = Cast<UUserWidget>(X))
                    {
                        // ★嵌套 UserWidget（Widget_CostSlotWrapper_C 等）内部控件**不在外层
                        //   GetAllWidgets 里** —— 上一版"看图标画刷判是不是我们的碎片"就死在这里。
                        //   这里按需展开一层，看清"空槽为什么显示碎片图标 + 「1」"。
                        if (Child->WidgetTree)
                        {
                            TArray<UWidget*> Inner;
                            Child->WidgetTree->GetAllWidgets(Inner);
                            Extra = FString::Printf(TEXT(" inner=%d:"), Inner.Num());
                            int32 InnerBudget = 12;
                            for (UWidget* In : Inner)
                            {
                                if (!In || InnerBudget-- <= 0)
                                {
                                    continue;
                                }
                                Extra += FString::Printf(TEXT(" %s(%s)"),
                                                         *In->GetClass()->GetName(), *In->GetName());
                                if (UTextBlock* IT = Cast<UTextBlock>(In))
                                {
                                    Extra += FString::Printf(TEXT("'%s'"), *IT->GetText().ToString());
                                }
                                else if (UImage* II = Cast<UImage>(In))
                                {
                                    if (FStructProperty* ISP = CastField<FStructProperty>(
                                            UImage::StaticClass()->FindPropertyByName(TEXT("Brush"))))
                                    {
                                        FSlateBrush* IB = ISP->ContainerPtrToValuePtr<FSlateBrush>(II);
                                        UObject* IRes = IB ? IB->GetResourceObject() : nullptr;
                                        Extra += FString::Printf(TEXT("[%s]"),
                                                                 IRes ? *IRes->GetName() : TEXT("<null>"));
                                    }
                                }
                                Extra += FString::Printf(TEXT("vis=%d"), static_cast<int32>(In->GetVisibility()));
                            }
                        }
                    }
                    WT_WARNF(TEXT("[shard-ui]   %s name=%s%s"),
                             *X->GetClass()->GetName(), *X->GetName(), *Extra);
                }
            }

            // 槽位真实内容：每次新开面板打一次（最多 6 次），判定"3 片到底进没进槽"。
            static int32 SlotDumpCount = 0;
            if (SlotDumpCount < 6)
            {
                ++SlotDumpCount;
                WT_DumpShardSlots(W->GetWorld());
            }
            break;
        }
    }

    // ★1.0.8AM：补丁提到 40Hz 后，**没有面板就直接返回**。后面每一段都挂在
    //   CachedPanel/CachedBarMID/Cached250 上（这三个只在上面的扫描里一起赋值、一起清空），
    //   所以这里返回不会漏掉任何工作，只是省掉「空面板」时每拍几十次 IsValid 与
    //   `WT_SyncShardSlots` 的反射查找。
    if (!Cached250.IsValid() && !CachedPanel.IsValid() && !CachedFactory.IsValid())
    {
        return;
    }

    // ★1.0.8d 判据改成**机器真实数据**：潜在库存里有没有 UWTUpgradePowerShardDescriptor。
    //
    //   上一版走"面板里的 UImage 画刷名含 rainbow"——实测这条路永远为假：
    //   碎片图标生在 `Widget_CostSlotWrapper_C` 里面，而那是**嵌套 UserWidget**，
    //   外层 WidgetTree->GetAllWidgets() 看不到（dump 证明外层只有 Background 与 mProgressBar
    //   两个 UImage）⇒ bOurs 恒 false ⇒ 刻度永远保持原版的 150/200/250。
    //   语义与用户 1.0.8b 定案一致：原版碎片 / 空槽 ⇒ 这个面板一个像素都不碰。
    // ★1.0.8e 面板 → 机器（0.5s 一拍；解出来就缓存，没解出来才重试——面板先建控件、
    //   后绑 mInteractObject 的时序也能兜住）。失败不崩：bOurs=false ⇒ 面板保持原版。
    static FString LastResolveWhy = TEXT("<init>");
    if (CachedPanel.IsValid() && !CachedFactory.IsValid())
    {
        FString Why;
        if (AFGBuildableFactory* Found = WT_PanelToFactory(CachedPanel.Get(), Why))
        {
            CachedFactory = Found;
        }
        if (Why != LastResolveWhy)
        {
            LastResolveWhy = Why;
            WT_WARNF(TEXT("[shard-ui] panel -> factory: %s"), *Why);
        }
    }

    // ★1.0.8k 判据 = 「槽里有我们的碎片」OR「这台机器正在以 >250% 运行」。
    //   后者兜住「拖到 400% 并确认后碎片被消耗/锁定」那一段（1.0.8j 日志 42.022 的翻转），
    //   否则面板会在玩家刚确认完就退回原版刻度，看起来像什么都没做。
    // ★1.0.8N 上限由**片数**决定（用户「放置 1 个，为什么能拉满超频」）：
    //    0 片 → 1.0（原版，不接管）／1 片 → 2.0／2 片 → 3.0／3 片 → 4.0
    //   1.0.8k～8M 把上限写死 4.0 ⇒ 1 片也开成 0..400%，等于凭 1 片白拿 3 片的效果。
    const int32 ShardN = WT_CountOurShards(CachedFactory.Get());
    const bool bHasShard = (ShardN > 0);
    const bool bAboveCap = WT_FactoryRunsAboveVanillaCap(CachedFactory.Get());
    const bool bOurs = bHasShard || bAboveCap;
    // ★1.0.8N 上限口径：**片数决定能拖多高**，与原版语义一一对应
    //   （原版 0/1/2/3 片 = 100/150/200/250%，我们每片 +100% ⇒ 100/200/300/400%）。
    //
    //   ★为什么不拿 mMaxPotential 当上限：研究阶段 ApplyShardStage 的奖励**已经体现在
    //   「每片 +100%」上**（原版碎片是 +50%），它的 Cap=5.0 只是引擎层的允许余量。
    //   若把 5.0 拿来当玩家可见量程，1 片就会被算成 600% —— 正是用户这次报的「放置 1 个
    //   就能拉满」的同一个毛病，只是数字更大。
    // ★1.0.8P 显示上限不能低于机器**实际**在跑的值：玩家拖到 400% 确认后槽里的碎片会被
    //   消耗（1.0.8j 日志 42.022 `potential has our shard=0`），此时 ShardN=0、
    //   CeilingForShards(0)=1.0，直接拿它当上限会把填充夹回 40% —— 面板看着像白忙一场。
    // ★★1.0.8S 棘轮 bug（1.0.8R 录屏实锤）：floor **绝不能算 mPendingPotential**。
    //   1.0.8R 日志 15:52:33 → 15:52:37（全程 shardsN=1，只放了一片）：
    //       pend=2.02 ceiling=202%  →  pend=2.50 ceiling=250%
    //   每拍拿 pending 抬 ceiling，于是「拖高一格 → 上限跟着涨一格 → 还能再拖」自我放大，
    //   1 片就被开成 0..250%（正是用户 15:00 报的「放置 1 个，为什么能拉满超频」，
    //   1.0.8N 关掉了硬编码 4.0 这个口子，棘轮又从另一个口子把它还回来了）。
    //   mCurrentPotential 不跟着拖动走（三次录屏全程 cur=1.00），拿它当 floor 才不棘轮；
    //   而「确认后碎片被消耗、ShardN 归 0」那条路仍然兜得住（那时 cur 已经是 4.0）。
    float Ceiling = WT_CeilingForShards(ShardN);
    if (bOurs)
    {
        const float InstCur = WT_FactoryFloat(CachedFactory.Get(), TEXT("mCurrentPotential"));
        Ceiling = FMath::Max(Ceiling, InstCur);
    }

    // 状态翻转时打一行（每拍都打会刷屏；这个值只在换碎片/换机器时变）。
    static int32 LastOursLogged = -1;
    if (LastOursLogged != (bOurs ? 1 : 0))
    {
        LastOursLogged = bOurs ? 1 : 0;
        WT_WARNF(TEXT("[shard-ui] scale patch %s | factory=%s | hasShard=%d aboveCap=%d shards=%d ceiling=%.0f%%"),
                 bOurs ? TEXT("ARMED (vanilla look, ceiling = 1+N shards)")
                       : TEXT("idle (vanilla, untouched)"),
                 CachedFactory.IsValid() ? *CachedFactory->GetClass()->GetName() : TEXT("<none>"),
                 bHasShard ? 1 : 0, bAboveCap ? 1 : 0, ShardN, Ceiling * 100.f);
    }

    // ⓪ ★1.0.8M 每拍重解滑条的动态材质。
    //   1.0.8L 实测：日志里 `mid Value=1.000`（写完回读就是我们写的值），屏幕上橙色却只到 40%
    //   ⇒ 只有两种可能：① 蓝图刷新时重新 `CreateDynamicMaterialInstance` 了，我们写在一块
    //   已经不在屏幕上的旧材质上；② 面板自己每帧按 `current` 重写 Value，我们被覆盖。
    //   这一拍先把 ① 排掉：brush 里的资源对象每拍重读一次，换了就重设几何并打日志。
    if (CachedBarImg.IsValid())
    {
        UMaterialInstanceDynamic* Live = WT_ImageMID(CachedBarImg.Get());
        if (Live != CachedBarMID.Get())
        {
            WT_WARNF(TEXT("[shard-ui] bar MID swapped: %s -> %s"),
                     CachedBarMID.IsValid() ? *CachedBarMID->GetName() : TEXT("<null>"),
                     Live ? *Live->GetName() : TEXT("<null>"));
            CachedBarMID = Live;
            GeomDone.Reset();   // 新材质参数回默认，几何要重设
        }
    }

    // ① ★1.0.8R 刻度文字按 bOurs 分岔（1.0.8Q 一律退回原版 150/200/250，录屏实锤是错的）。
    //   用户 15:37 录屏：插 1 片拖满时面板写着「运行频率 201%」，条上的刻度却还是 150/200/250 ——
    //   同一张图两套坐标，橙色末端压在「250%」上，所以他说「数值变回原版了 / 和没插一样」。
    //   我们的碎片是每片 +100%，段边界（原版 0/40/60/80/100% 位置不动）对应的值就是
    //   0/100/200/300/400；位置一个都不改，只改字。
    //   ★未插我们的碎片（bOurs=false）时**保持原版 150/200/250**，这是用户 15:24 的第一条要求：
    //   「未插入我们碎片，超频不变走原版」。
    const TCHAR* Want150 = bOurs ? TEXT("200%") : TEXT("150%");
    const TCHAR* Want200 = bOurs ? TEXT("300%") : TEXT("200%");
    const TCHAR* Want250 = bOurs ? TEXT("400%") : TEXT("250%");
    if (Cached150.IsValid() && Cached150->GetText().ToString() != Want150)
    {
        Cached150->SetText(FText::FromString(Want150));
    }
    if (Cached200.IsValid() && Cached200->GetText().ToString() != Want200)
    {
        Cached200->SetText(FText::FromString(Want200));
    }
    if (Cached250.IsValid() && Cached250->GetText().ToString() != Want250)
    {
        Cached250->SetText(FText::FromString(Want250));
    }

    // ①.x ★1.0.8L 刻度**位置保持原版**（0/40/60/80/100%）——那正好是材质的分段边界，
    //     刻度数字在 ① 里已经改成 0/100/200/300/400，位置一个都不动。
    //     函数本身只做守卫（被挪过就写回），每拍调用是幂等的。
    {
        const FString Why = WT_LayoutBarLabels(CachedBarLabels, 5, bOurs);
        const int32 Mode = Why.IsEmpty() ? (bOurs ? 1 : 0) : -1;
        static FString LastLabelWhy;
        if (Mode != LastLabelMode || (!Why.IsEmpty() && Why != LastLabelWhy))
        {
            LastLabelWhy = Why;
            WT_WARNF(TEXT("[shard-ui] label layout -> %s | %s"),
                     bOurs ? TEXT("OURS: vanilla 0/40/60/80/100% positions + vanilla texts")
                           : TEXT("VANILLA 0/40/60/80/100%"),
                     Why.IsEmpty() ? TEXT("ok") : *Why);
        }
        LastLabelMode = Mode;
    }

    // ② 轨道底色：1.0.8f 撤掉了（见上面「撤掉黑底垫图」那段；材质没有颜色参数，
    //   而 mProgressBar 的 slot 是整个面板内区，垫黑图必成大黑块）。原版灰保留。

    // ③ ★1.0.8L 段宽**保持原版 2:1:1:1**（用户「和原版一样」）。
    //   1.0.8g 曾把 BigBarRatio 改成 1.0 做四等分，条的样子就跟原版不一样了，撤回。
    //   原版口径：0-100% 段 2 个单位（全宽 40%）+ 3 个碎片格各 1 个单位（各 20%），
    //   我们把「每格 +50%」改成「每格 +100%」，配合 WT_BarFillForPending 的分段填充，
    //   0/100/200/300/400 就正好压在 0/40/60/80/100% 四道边界上。
    if (CachedBarMID.IsValid())
    {
        if (GeomDone.Get() != CachedBarMID.Get())
        {
            GeomDone = CachedBarMID;
            CachedBarMID->SetScalarParameterValue(TEXT("BigBarRatio"), 2.0f);
            CachedBarMID->SetScalarParameterValue(TEXT("SmallBarRatio"), 1.0f);
            WT_WARNF(TEXT("[shard-ui] bar geometry -> VANILLA kept 2:1:1:1 (BigBarRatio=%.1f SmallBarRatio=%.1f) | factory=%s"),
                     CachedBarMID->K2_GetScalarParameterValue(TEXT("BigBarRatio")),
                     CachedBarMID->K2_GetScalarParameterValue(TEXT("SmallBarRatio")),
                     CachedFactory.IsValid() ? *CachedFactory->GetClass()->GetName() : TEXT("<none>"));
        }
    }

    // ③.x ★1.0.8Q 材质碎片格数 —— 「后面插入的显示不出来 / 拖动后没有颜色」的真凶。
    //
    //   原版材质 `MM_UI_OverclockingSlider` 只有 5 个 scalar，其中 `NumShards` 决定**能画到第几格**：
    //       0 = 只有大格（0~40% 全宽）    1/2/3 = 再点亮 1/2/3 个小格
    //   原版面板按 pending 推它：Value 0.4→0 / 0.6→1 / 0.8→2 / 1.0→3
    //     ⇔ NumShards = ceil(pending × 2 − 2)，夹在 0..3
    //   （原版 0/1/2/3 片拖满时 pending 正好 1.0/1.5/2.0/2.5，代入就得 0/1/2/3，一一对应。）
    //
    //   我们的碎片是自定义物品，原版统计不到 ⇒ 日志里 `[S]NumShards=0.000 [S]Value=1.600`
    //   ⇒ Value 超过 0.4 的部分**一律画不出来**，橙色永远停在大格末端。
    //   这里照原版公式替它算好（按 pending，不是按片数：1 片拖到 200% 时 pending=2.0 ⇒ 2 格）。
    //
    //   ★必须是 **ceil（向上取整）+ 0.001 容差**，不能四舍五入：
    //     pending 1.25 ⇒ Value 0.50，橙色已经进第 1 格半格了；四舍五入得 0 格 ⇒ 那半格被裁掉，
    //     玩家会看到「拖到 125% 橙色还停在大格末端」。ceil 给 1 格，正好够画。
    //     容差 0.001 是为了让「恰好 1.00 / 1.50 / 2.00 / 2.50」落回整数格（否则浮点误差会多点亮一格）。
    if (CachedBarMID.IsValid() && bOurs)
    {
        const float PendNow = CachedFactory.IsValid() ? CachedFactory->GetPendingPotential() : 1.f;
        // ★1.0.8R 取**并集**：`片数` 与 `pending 需要的格数` 谁大用谁。
        //   原版那边 NumShards 就是片数（插 1 片 ⇒ 1，哪怕没拖动），保证「插片后条上多出一格」；
        //   而我们每片 +100%，拖到 200% 时需要 2 格才画得出来（ceil(2.0×2−2) = 2）。
        //   1.0.8Q 只用后者 ⇒ 插 1 片没拖动时是 0 格，条上一点变化都没有（用户
        //   「插入 1 个后，滑条依旧是和未插入一样的颜色」）。
        const float NeedForFill = FMath::CeilToFloat(PendNow * 2.f - 2.f - 0.001f);
        const float WantShards  = FMath::Clamp(FMath::Max((float)ShardN, NeedForFill), 0.f, 3.f);
        const float CurShards  = CachedBarMID->K2_GetScalarParameterValue(TEXT("NumShards"));
        if (FMath::Abs(CurShards - WantShards) > 0.01f)
        {
            CachedBarMID->SetScalarParameterValue(TEXT("NumShards"), WantShards);
            WT_WARNF(TEXT("[shard-ui] shards param -> %.0f (was %.0f) | pend=%.2f shardsN=%d ceiling=%.0f%%"),
                     WantShards, CurShards, PendNow, ShardN, Ceiling * 100.f);
        }
    }

    // ③.y ★1.0.8T 碎片槽「成本满足」态（A+B 双路）。用户 15:53「后面两个插槽位置，显示浅灰色」
    //   的真正根因是原版成本格数不到我们的碎片类 ⇒ 永远按 0/1 渲染（没有橙条、高亮窗不推进）。
    //   ★**每拍都要调，不能只在 bOurs 时调**：B 路要把成本类写回原版，玩家取走碎片后
    //     面板必须立刻恢复原版渲染，否则会拿我们的类去数原版碎片（永远 0/1）。
    if (CachedPanel.IsValid())
    {
        WT_SyncShardSlots(CachedPanel.Get(), bOurs ? ShardN : 0, CachedFactory.Get());
    }

    // ④ ★1.0.8g 上限**就地**生效：巡检 5s 才轮一圈，玩家放完碎片马上就要拖滑条，
    //   等 5 秒会看到「拖不过 250%」。面板这 0.5s 一拍直接对这台机器应用同一套口径
    //   （WT_ApplyCeiling 是唯一实现，规则与巡检完全一致，重复设置是幂等的）。
    if (AFGBuildableFactory* F = CachedFactory.Get())
    {
        bool bOursNow = false;
        float Before = -1.f, After = -1.f, Base = -1.f;
        if (WT_ApplyCeiling(F, bOursNow, Before, After, Base))
        {
            WT_WARNF(TEXT("[shard-ui] ceiling %s %s: %.2f -> %.2f (ours=%d)"),
                     (After > Before) ? TEXT("UP") : TEXT("RESTORED"),
                     *F->GetClass()->GetName(), Before, After, bOursNow ? 1 : 0);
        }
    }

    // ⑤ ★1.0.8h 超频值追踪（用户「超频上限滑动到 400% 了依旧是给的 250%」）。
    //   滑条能拖到 400% 只证明 UI 读到了 mMaxPotential=4.0；真正决定产出的是 mCurrentPotential。
    //   这里在它变化时打一行，把 max / current / pending 钉在同一时刻，用来区分三种完全不同的病因：
    //     · current 没到 4.0        → 值没写进去（或写入时被 clamp，看 pending）
    //     · pending 到 4.0 但 current 没跟 → 工厂 tick 没采纳，或被别处夹回
    //     · 三个都到 4.0 但产出没涨 → 生产公式侧另有上限，要换方向查
    // ⑤.0 ★1.0.8k 三件套（取代 1.0.8j 的「接管 pending」，那条路会被面板反算回滑块、把
    //   滑块顶到量程外 60%，材质 Value=1.6 把橙色填充画到条外 —— 玩家说「没用」就是这个）：
    //     ① 滑条量程：ours 时 MaxValue = (pending/2.5) / 填充占比（1.0.8P 口径），
    //        让手柄正好压在填充末端 = 刻度所在的段边界（1.0.8N 的 ceiling/2.5 会错开）
    //     ② 数值兜底：跟随面板口径（2.5 × pos），面板算对时什么都不写
    //     ③ 材质填充：分段映射（0→0、1.0→0.40、2.0→0.60、3.0→0.80、4.0→1.00），
    //        正好压在原版 2:1:1:1 的四道段边界上（1.0.8L）
    //   顺序不能换：量程先到位，②③ 才有正确的 pos / pending 可用。
    if (AFGBuildableFactory* F = CachedFactory.Get())
    {
        const FString RangeWhy = WT_SetSliderRange(CachedSlider.Get(), F, bOurs, Ceiling);
        static FString LastRangeWhy;
        if (!RangeWhy.IsEmpty() && RangeWhy != LastRangeWhy)
        {
            LastRangeWhy = RangeWhy;
            WT_WARNF(TEXT("[shard-ui] slider range skipped | %s"), *RangeWhy);
        }
        WT_VerifyOverclock(F, CachedSlider.Get(), bOurs, Ceiling);
        WT_SetBarFill(CachedBarMID.Get(), F, bOurs, Ceiling);

        // ★1.0.8M 材质看门狗：每 0.3s 打一行「屏幕上那块材质现在是多少 / 我们希望是多少 /
        //   current / pending」，用来区分两种完全不同的病因：
        //     V 一直是 0.400 而我们 want=1.000 ⇒ 面板每帧回写（改法要换：不能让面板当唯一数据源）
        //     V 跟着 want 走 ⇒ 我们赢了，屏幕上还不对就是渲染/另一个控件的事
        static double LastWatchS = 0.0;
        const double NowW = FPlatformTime::Seconds();
        if (CachedBarMID.IsValid() && NowW - LastWatchS > 0.3)
        {
            LastWatchS = NowW;
            WT_WARNF(TEXT("[shard-ui] bar watch %s V=%.3f want=%.3f pos=%.3f | cur=%.2f pend=%.2f | ceiling=%.0f%% shardsN=%d midShards=%.0f Big=%.1f | mid=%s"),
                     bOurs ? TEXT("OURS") : TEXT("VANILLA"),
                     CachedBarMID->K2_GetScalarParameterValue(TEXT("Value")),
                     WT_BarFillForPending(F->GetPendingPotential(), Ceiling),
                     CachedSlider.IsValid() ? CachedSlider->GetValue() : -1.f,
                     WT_FactoryFloat(F, TEXT("mCurrentPotential")),
                     F->GetPendingPotential(),
                     Ceiling * 100.f, ShardN,
                     CachedBarMID->K2_GetScalarParameterValue(TEXT("NumShards")),
                     CachedBarMID->K2_GetScalarParameterValue(TEXT("BigBarRatio")),
                     *CachedBarMID->GetName());
        }
    }

    if (AFGBuildableFactory* F = CachedFactory.Get())
    {
        static TWeakObjectPtr<AFGBuildableFactory> LastPotF;
        static float LastCur = -999.f, LastPend = -999.f, LastMax = -999.f;
        const float Cur  = WT_FactoryFloat(F, TEXT("mCurrentPotential"));
        const float Pend = WT_FactoryFloat(F, TEXT("mPendingPotential"));
        const float Max  = WT_FactoryFloat(F, TEXT("mMaxPotential"));
        if (LastPotF.Get() != F
            || !FMath::IsNearlyEqual(Cur, LastCur, 0.001f)
            || !FMath::IsNearlyEqual(Pend, LastPend, 0.001f)
            || !FMath::IsNearlyEqual(Max, LastMax, 0.001f))
        {
            LastPotF = F;
            LastCur  = Cur;
            LastPend = Pend;
            LastMax  = Max;
            WT_WARNF(TEXT("[shard-ui] probe ours=%d | %s | factory=%s"),
                     bOurs ? 1 : 0,
                     *WT_ProbeOverclock(F, CachedBarMID.Get(), CachedSlider.Get(), CachedPctText.Get()),
                     *F->GetClass()->GetName());
        }
    }
}

void URemote_FactoryGameWorldModule::DispatchLifecycleEvent(ELifecyclePhase Phase)
{
    // ★2026-10-07 崩溃/卡顿链定位：世界加载 = 注册配方 + 配 60 个研究 CDO + 建 3 棵研究树
    //   + 注册建筑/升级配方，全在这一个函数里串起来。进世界时的「卡一下 / 崩在这里」
    //   可以直接从日志的 `> Lifecycle` / `< Lifecycle (xxx ms)` 与 CHAIN DUMP 判定。
    WT_ZONE_COARSE(TEXT("World.Lifecycle"));
    WT_TRACE_MARK(*FString::Printf(TEXT("DispatchLifecycleEvent phase=%d"), static_cast<int32>(Phase)));

    // ★1.0.26 专用服务器禁用闸门（用户 2026-10-25 定案）：本 mod 单机向，专用服务器上
    // 完全空挂。此处直接 return：不调 Super（不把 mSchematics 接入 SML content registry）、
    // 不填里程碑/研究树、不注册建筑/升级配方/传送命令/自动扫描定时器——服务器上装了这个
    // mod 等于没装。Steam/EGS 客户端 IsRunningDedicatedServer() 为 false，不受影响。
    if (IsRunningDedicatedServer())
    {
        WT_WARNF(TEXT("[wt-server-guard] Remote Factory %s DISABLED on dedicated server (single-player mod)."),
                 REMOTE_FACTORY_VERSION_TEXT);
        return;
    }

    // ★v0.2.28 注册阶段定位标记：0.2.27 首测在 INITIALIZATION 派发别的模块（WP）注册配方时崩
    // （SML AutoRegisterRecipeReferences 读到野指针 ItemClass），没有分阶段标记没法判断崩在
    // 谁家。begin/done 成对打出来，崩在谁家一翻日志便知。
    WT_WARNF(TEXT("[wt-reg] phase %d begin."), static_cast<int32>(Phase));

    // ★★★2026-11-06 崩溃根治（注册期遍历 mNodes AV @0x1e30b0328）：
    //   SML 的 UGameWorldModule::DispatchLifecycleEvent 在 INITIALIZATION 阶段调用
    //   RegisterDefaultContent() → RegisterResearchTree() → AutoRegisterResearchTreeReferences()，
    //   后者**无任何指针保护地**遍历 UFGResearchTree::GetNodes() 并对每个节点调
    //   GetNodeSchematic()（gen.cpp: FindFunctionChecked → GetClass，野节点直接 AV）。
    //   而树 CDO 的 mNodes 会被引擎在处理树时重置（运行时节点不在资产里 ⇒ 变空/残留旧节点）。
    //   故必须在 Super 触发注册遍历**之前**，把 mNodes 修复成"整树新鲜有效节点"。
    //   （WT_ValidateAndRepairTree 用 WT_IsNodeAlive=IsValidLowLevelFast 判活，不解引用野指针，
    //    本身安全；命中损坏即整树重建，见 Remote_FactoryResearchTree.cpp。）
    if (Phase == ELifecyclePhase::INITIALIZATION)
    {
        WT_ZONE_COARSE(TEXT("World.PreRegistrationTreeRepair"));
        WT_ValidateAndRepairTree(GetWorld());
    }

    // CRITICAL: the SML base implementation is what wires the module (and mSchematics) into the
    // content registry. Always let the base class run first.
    Super::DispatchLifecycleEvent(Phase);

    // CONSTRUCTION runs before INITIALIZATION, and INITIALIZATION is when SML reads mSchematics.
    // Every milestone's cost + category must therefore be populated here so the HUB's own
    // "available schematics" query sees complete entries.
    if (Phase == ELifecyclePhase::CONSTRUCTION)
    {
        // T1 HUB milestone (terminal + handheld). High-Capacity Storage is no longer unlocked here;
        // it is the L1 node of the Storage MAM research track (see Remote_FactoryUpgradeResearch).
        WT_ZONE_COARSE(TEXT("World.ApplyMilestoneData"));
        Remote_Factory::ApplyMilestoneData();

        // Populate the 60 MAM research CDOs (cost / category / unlock / dependency) at this safe
        // time. Must NOT happen in the research classes' constructors — Apply() loads assets and
        // NewObject's subobjects, which is illegal during CDO construction and crashes the game.
        {
            WT_ZONE_COARSE(TEXT("World.ConfigureAllResearch"));
            WT_ConfigureAllResearch();
        }

        // ★0.2.36h 强化碎片两条新能力（都在 CONSTRUCTION 填，SML INITIALIZATION 读 mSchematics 前就绪）：
        //   ① 回收：mResourceSinkPoints = 原版碎片 ×2（运行时镜像，见 UpgradeRegistry.cpp）。
        //   ② 商店：EST_ResourceSink 条目 CDO 的售价（50,000 券）+ 发货解锁 + 图标。
        if (UWTUpgradePowerShardDescriptor* Shard = Cast<UWTUpgradePowerShardDescriptor>(
                UWTUpgradePowerShardDescriptor::StaticClass()->GetDefaultObject()))
        {
            Shard->ApplySinkPoints();
        }
        // ★0.2.134 闸门：商店条目已下架时，售价/发货数据也不必填（与注册处同一开关）。
        if (WT_FEATURE_POWER_SHARD)
        {
            if (UWTShopPowerShardSchematic* Shop = Cast<UWTShopPowerShardSchematic>(
                    UWTShopPowerShardSchematic::StaticClass()->GetDefaultObject()))
            {
                Shop->ApplyShopSchematicData();
            }

            // ★1.0.8g 这里不再碰 CDO。1.0.7x～1.0.8f 在这一步把所有机器 CDO 抬到 4.0，
            //   连带把「没放碎片的机器」滑条归一化分母从 2.5 变成 4.0 ⇒ 原版超频条填充错位
            //   （用户 2026-10-27 13:35 截图）。上限现在完全由 POST_INITIALIZATION 的巡检按
            //   「这台机器槽里有没有我们的碎片」逐实例设置：有 → 4.0，没有 → 该类 CDO 原值。
            //   CONSTRUCTION 阶段没有世界，什么也不用做。
        }

        // 构建 3 棵 MAM 研究树的节点（每棵 20 个，指向对应研究 schematic）。必须在 SML 于加载阶段
        // 读取 mResearchTrees 之前完成，否则树会是空的。注册交给构造函数的 mResearchTrees（SML 官方
        // 机制），不再用运行时 delegate 绑定。
        {
            WT_ZONE_COARSE(TEXT("World.BuildResearchTrees"));
            WT_BuildResearchTrees();
        }
    }

    // POST_INITIALIZATION: world + FactoryGame subsystems are fully up. Safe to register.
    if (Phase == ELifecyclePhase::POST_INITIALIZATION)
    {
        UWorld* World = GetWorld();
        if (!World)
        {
            WT_WARNF(TEXT("Remote Factory: no world at POST_INITIALIZATION, skip registration."));
            return;
        }

        // A fresh world means any shared-session roster from the previous one is meaningless. The
        // roster is keyed by world so this is belt-and-braces, but it costs one line and removes a
        // whole class of "ghost viewer" bug after a map reload on a host.
        Remote_FactoryShared::Reset(World);

        // ★2026-10-07「崩溃/卡顿链快速定位」：武装逐帧卡顿看门狗。
        //   200 ms/帧 ≈ 5 FPS 的停顿，是「明显卡了」与「正常加载抖动」的分界。
        //   看门狗与 World 绑定（TWeakObjectPtr 判重），换档/回主菜单时自动卸载。
        WTTrace::StartWatchdog(World, 200.f);

        // ★1.0.7 云端初始容量（用户 2026-10-27 定案）：原版未研究 1 组 ⇒ 本 mod 开局即 10 组/类。
        //   放在所有注册之前：越早生效，越不会有存档在旧默认值下重算过一轮。
        WT_ApplyCloudInitialQuota(World);

        WT_LOGF(TEXT("Remote Factory: registering buildables…"));
        {
            WT_ZONE_COARSE(TEXT("World.RegisterBuildables"));
            Remote_Factory::RegisterBuildables(World);
        }

        // Form 3 "C": the three upgrade buildings. Registered on the same beat as the terminal
        // because they share the build gun, the build category and the milestone machinery.
        WT_LOGF(TEXT("Remote Factory: registering Form 3 upgrades…"));
        {
            WT_ZONE_COARSE(TEXT("World.RegisterUpgradeRecipes"));
            Remote_FactoryUpgrades::RegisterUpgradeRecipes(World);
        }

        // Register the terminal RCO on the game mode. This spawns an instance on every current and
        // future player controller so panel clicks can reach the server (buildables are
        // server-owned and cannot take client RPCs directly).
        if (AFGGameMode* GM = Cast<AFGGameMode>(World->GetAuthGameMode()))
        {
            GM->RegisterRemoteCallObjectClass(URemote_FactoryRCO::StaticClass());
            WT_LOGF(TEXT("Remote Factory: RCO registered on game mode."));
        }

        else
        {
            WT_WARNF(TEXT("Remote Factory: no AFGGameMode; RCO not registered."));
        }

        // ★智能仓储（2026-11-02 重做）：与云端功能同源——把玩家全部原版存储集装箱经原版中央存储
        // 上传 API 抽进云池，建造便像云端物品一样自然取货。在权威世界启动自动抽取 ticker，幂等。
        Remote_FactorySmartWarehouse::RegisterHook(World);

        // ★2026-11-03 智能仓储右侧面板：背包打开时停靠屏幕右侧，列出全局集装箱货物（聚合）。
        // 经拆分头后的自由函数入口（GameWorldModule 不直接依赖 UCLASS 生成头，避免 UHT 竞态 C2653）。
        Remote_FactoryWarehousePanel_Init(World);

        // ★2026-11-02 里程碑货舱即刻返回：模块初始化时载入持久化开关状态（面板/行为接线读取）。
        Remote_FactoryMilestoneCargoReturn::LoadMilestoneCargoReturnConfig();
        // ★2026-11-03 行为接线：安装“返回时间缩短至 10 秒”的状态 ticker（全局一次）。
        Remote_FactoryMilestoneCargoReturn::InstallMilestoneCargoReturnHooks(World);

        // ★2026-11-03 按存档持久化：把两个面板开关随存档保存/载入（新存档默认关、开过的存档记住）。
        Remote_FactorySave::InstallPerSaveHooks();

        // ★2026-11-02 成本格蓝色染色：客户端 ticker，把「智能仓储补足」的成本格叠蓝覆盖层。
        RemoteFactoryCostSlotTint::Register(World);

        // ★v0.2.22 进度链诊断（用户实测反馈"HUB 里程碑 T1/T3/T5 均无 + MAM 无研究树"后加）。
        //
        // 为什么需要它：里程碑与研究树是两条完全独立、都可能静默失败的注册链——
        //   HUB：GameWorldModule::mSchematics → SML INITIALIZATION 的 RegisterDefaultContent()
        //        → ModContentRegistry → 绑 AFGSchematicManager::PopulateSchematicListDelegate
        //   MAM：GameWorldModule::mResearchTrees → 同一处注册 → 绑 PopulateResearchTreeListDelegate
        // 任何一环断掉都只表现为"游戏里看不到"，没有任何报错。这一段直接在运行时问两个管理器
        // "你到底拿到我们的类没有"，一次进游戏就能定位断点在哪一环。
        // 全部走 WT_WARNF（Shipping 下 LOGF 会被整体编译剔除，必须用 Warn 级取证）。
        {
            if (AFGSchematicManager* SchematicManager = AFGSchematicManager::Get(World))
            {
                TArray<TSubclassOf<UFGSchematic>> AllSchematics;
                SchematicManager->GetAllSchematics(AllSchematics);
                int32 HitT1 = 0;
                int32 HitStorage = 0;
                int32 HitAccel = 0;
                int32 HitShard = 0;
                int32 HitCloudSolid = 0;
                int32 HitCloudFluid = 0;
                for (const TSubclassOf<UFGSchematic>& S : AllSchematics)
                {
                    UClass* SClass = S.Get();
                    if (!SClass) continue;
                    if (SClass == URemote_FactoryMilestone::StaticClass())            ++HitT1;
                    else if (SClass == UWTUpgradeStorageMilestone::StaticClass())       ++HitStorage;
                    else if (SClass == UWTUpgradeAcceleratorMilestone::StaticClass())   ++HitAccel;
                    // ★2026-11-05 分档后：碎片卡**重新注册**了（改挂 T9），另加两张云端卡。
                    // 期望（本轮）：T1=1 T3storage=1 T5accel=0 T9shard=1 T5solid=1 T7fluid=1。
                    // 哪个是 0 就是那张卡没进 mSchematics —— 注意 T5accel 恒为 0，加速器建筑已退役。
                    else if (SClass == UWTUpgradePowerShardMilestone::StaticClass())    ++HitShard;
                    else if (SClass == UWTUpgradeCloudSolidMilestone::StaticClass())    ++HitCloudSolid;
                    else if (SClass == UWTUpgradeCloudFluidMilestone::StaticClass())    ++HitCloudFluid;
                }
                WT_WARNF(TEXT("[wt-diag] schematics: total=%d | T1=%d T3storage=%d T5accel=%d T9shard=%d T5solid=%d T7fluid=%d"),
                         AllSchematics.Num(), HitT1, HitStorage, HitAccel, HitShard, HitCloudSolid, HitCloudFluid);

                // ★v0.2.25 里程碑字段 dump + 原版对照（见 WT_DumpMilestone 上方注释）。
                WT_DumpMilestone(URemote_FactoryMilestone::StaticClass(), TEXT("WT_T1"));
                WT_DumpMilestone(UWTUpgradeStorageMilestone::StaticClass(), TEXT("WT_T3storage"));
                WT_DumpMilestone(UWTUpgradeAcceleratorMilestone::StaticClass(), TEXT("WT_T5accel"));
                for (const TSubclassOf<UFGSchematic>& S : AllSchematics)
                {
                    UClass* SClass = S.Get();
                    if (!SClass)
                    {
                        continue;
                    }
                    const FString Path = SClass->GetPathName();
                    if (Path.Contains(TEXT("Smart_Factory")) || Path.Contains(TEXT("Remote_Factory")) || Path.Contains(TEXT("WirelessPower")))
                    {
                        continue;
                    }
                    if (!SClass->GetName().Contains(TEXT("Milestone")))
                    {
                        continue;
                    }
                    WT_DumpMilestone(SClass, TEXT("VANILLA"));
                    break;
                }
            }
            else
            {
                WT_WARNF(TEXT("[wt-diag] AFGSchematicManager unavailable."));
            }

            if (AFGResearchManager* ResearchManager = AFGResearchManager::Get(World))
            {
                TArray<TSubclassOf<UFGResearchTree>> AllTrees;
                ResearchManager->GetAllResearchTrees(AllTrees);
                int32 HitOurs = 0;
                for (const TSubclassOf<UFGResearchTree>& T : AllTrees)
                {
                    UClass* TClass = T.Get();
                    if (!TClass) continue;
                    if (WT_IsOurResearchTree(TClass))
                    {
                        ++HitOurs;
                    }
                }
                WT_WARNF(TEXT("[wt-diag] research trees: total=%d | ours=%d"), AllTrees.Num(), HitOurs);

                // ★★★2026-11-05 **量原版图区到底能放多大**。
                // 起因：用户实测「拆成 4 个后只有云端那条轨能研究，另外两条根本点不了」。
                // 我们因为每格铺 4 级，把整张图撑到 X 0..13 / Y 0..8；
                // 而原版树（前面 dump 的 AlienOrganisms）只用 X 1..5 / Y 0..5。
                // MAM 图区若是固定画布，超出的节点会落在可点击范围之外 ⇒ 看得见点不着。
                // 这里把**所有原版树**的坐标包络统计出来，给出真实可用尺寸，别再凭感觉布点。
                int32 EnvMinX = 9999, EnvMaxX = -9999, EnvMinY = 9999, EnvMaxY = -9999;
                FString EnvDetail;
                for (const TSubclassOf<UFGResearchTree>& T : AllTrees)
                {
                    UClass* TClass = T.Get();
                    if (!TClass || WT_IsOurResearchTree(TClass))
                    {
                        continue;   // 只统计原版
                    }
                    const TArray<UFGResearchTreeNode*> VN = UFGResearchTree::GetNodes(T);
                    int32 MnX = 9999, MxX = -9999, MnY = 9999, MxY = -9999;
                    for (UFGResearchTreeNode* N : VN)
                    {
                        int32 X = 0, Y = 0;
                        if (!N || !WT_NodeCoord(N, X, Y))
                        {
                            continue;
                        }
                        MnX = FMath::Min(MnX, X); MxX = FMath::Max(MxX, X);
                        MnY = FMath::Min(MnY, Y); MxY = FMath::Max(MxY, Y);
                    }
                    if (VN.Num() == 0 || MnX > MxX)
                    {
                        continue;
                    }
                    EnvMinX = FMath::Min(EnvMinX, MnX); EnvMaxX = FMath::Max(EnvMaxX, MxX);
                    EnvMinY = FMath::Min(EnvMinY, MnY); EnvMaxY = FMath::Max(EnvMaxY, MxY);
                    EnvDetail += FString::Printf(TEXT("%s[n=%d X%d..%d Y%d..%d] "),
                                                 *TClass->GetName(), VN.Num(), MnX, MxX, MnY, MxY);
                }
                if (EnvMinX <= EnvMaxX)
                {
                    WT_WARNF(TEXT("[wt-vtree] vanilla envelope: X %d..%d (%d wide) | Y %d..%d (%d tall)"),
                             EnvMinX, EnvMaxX, EnvMaxX - EnvMinX + 1,
                             EnvMinY, EnvMaxY, EnvMaxY - EnvMinY + 1);
                    WT_WARNF(TEXT("[wt-vtree] vanilla trees: %s"), *EnvDetail);
                }

                // ★★★2026-11-05 方块推进定时器（用户定稿「买完 Lv1 → Lv2 覆盖 Lv1」的驱动）。
                //   MAM 图按坐标索引，同一坐标任何时刻只能有 1 个节点 ⇒ 买完一级必须把该坐标的
                //   节点换成下一级。已购状态由原版复制，客户端也读得到，故两侧都跑；
                //   只有真的发生推进时才重写树 CDO 的 mNodes（见 WT_AdvanceResearchBlocks，幂等）。
                // ★★★2026-11-06 修复「多次加载」：原 static bool bBlockTimerArmed 是**进程级**，
                //   二次进图（重载存档 / 切存档）INITIALIZATION 再跑时它已为 true ⇒ 定时器不再重挂，
                //   WT_RefreshResearchedUpgrades 与 WT_AdvanceResearchBlocks 全部停摆 ⇒
                //   重载后研究等级读不到、容量锁死基数。改为按 World 跟踪：世界一变（新世界/旧世界已销毁）
                //   就重新挂定时器，覆盖「同一存档不同进度反复加载」场景。
                static TWeakObjectPtr<UWorld> sArmedWorld;
                if (World && sArmedWorld.Get() != World)
                {
                    sArmedWorld = World;
                    TWeakObjectPtr<UWorld> WeakWorld(World);
                    FTimerHandle BlockTimer;
                    World->GetTimerManager().SetTimer(
                        BlockTimer,
                        FTimerDelegate::CreateLambda([WeakWorld]()
                        {
                            if (UWorld* W = WeakWorld.Get())
                            {
                                WT_AdvanceResearchBlocks(W);
                                // ★2026-11-xx 研究推进后同步刷新已放置升级建筑的实际容量（电池/云端库存/加速倍率），解决「研究后空提升」。
                                WT_RefreshResearchedUpgrades(W);
                                // 同一次 tick 里刷新「前置条件 / 已达成」文案（幂等）
                                UWTUpgradeResearchBase::WT_RefreshPrereqText(AFGSchematicManager::Get(W));
                            }
                        }),
                        1.0f, /*bLoop=*/true);
                    WT_WARNF(TEXT("[wt-block] advance timer armed (1s loop) for world %s."), *World->GetName());

                    // ★★★2026-11-06 #44（落地）：订阅研究状态/完成委托，研究完成时**预先**推进合并树 +
                    //   刷新已放置升级建筑，使 MAM 重读树时拿到已推进的 mNodes（无需重开面板）。
                    //   处理器实例常驻（AddToRoot），动态委托按 UFUNCTION 绑定（AddUniqueDynamic）。
                    //   委托挂在 AFGResearchManager 上、随世界销毁而解绑，新世界进入时本块重新绑定，
                    //   天然覆盖「同一存档不同进度反复加载」场景（铁律）。
                    if (AFGResearchManager* RM = AFGResearchManager::Get(World))
                    {
                        static TWeakObjectPtr<URemoteFactoryResearchRefresh> sRefresh;
                        if (!sRefresh.IsValid())
                        {
                            sRefresh = NewObject<URemoteFactoryResearchRefresh>();
                            sRefresh->AddToRoot();   // 防 GC；模块生命周期内常驻一个实例
                        }
                        RM->ResearchStateChangedDelegate.AddUniqueDynamic(
                            sRefresh.Get(), &URemoteFactoryResearchRefresh::HandleResearchStateChanged);
                        RM->ResearchCompletedDelegate.AddUniqueDynamic(
                            sRefresh.Get(), &URemoteFactoryResearchRefresh::HandleResearchCompleted);
                        WT_WARNF(TEXT("[wt-refresh-44] bound OnResearchStateChanged/Completed for world %s."), *World->GetName());
                    }
                }

                // ★★★2026-11-06 #47 崩溃根治 + #44 即时推进（同源合并，同一道钩子）：
                //   在崩溃发生的那一帧（AFGResearchManager::IsTreeFullyResearched 遍历 mNodes）之前插强校验+自愈，
                //   并顺手把合并树推进到下一级。该函数被 MAM 打开/刷新树时调用、逐节点 GetNodeSchematic，
                //   一旦 mNodes 里混进悬空节点就 EXCEPTION_ACCESS_VIOLATION；且 MAM 研究完成刷新树也会走它，
                //   故在此推进⇒研究完 LV1 立刻出 LV2，无需重开 MAM（#44 目标）。
                //   只拦截我们自己的合并树，原版树零开销。
                //   SUBSCRIBE_METHOD 挂的是游戏运行时的真实静态函数（SDK 里的 IsTreeFullyResearched
                //   只是链接桩 return bool();），静态函数指针 NTTP 不会触发 MSVC 对成员指针 NTTP 的 C1001。
                {
                    static bool bHooked = false;
                    if (!bHooked)
                    {
                        bHooked = true;
                        using FScope = TCallScope<bool(*)(UObject*, TSubclassOf<UFGResearchTree>, TSubclassOf<UFGSchematic>)>;
                        TFunction<void(FScope&, UObject*, TSubclassOf<UFGResearchTree>, TSubclassOf<UFGSchematic>)> Handler =
                            [](FScope& Scope, UObject* WorldCtx, TSubclassOf<UFGResearchTree> Tree, TSubclassOf<UFGSchematic> Ignore)
                            {
                                if (WT_IsOurResearchTree(Tree.Get()))
                                {
                                    UWorld* W = WorldCtx ? WorldCtx->GetWorld() : nullptr;
                                    if (!W && GEngine)
                                    {
                                        for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
                                        {
                                            if (Ctx.World())
                                            {
                                                W = Ctx.World();
                                                break;
                                            }
                                        }
                                    }
                                    // 先自愈（#47 崩溃闸门），再推进下一级（#44 即时显示）。
                                    // 两者幂等；此处推进与 1s 定时器双重保险，研究完成即见 LV2。
                                    // ★2026-11-07 回归修复：刷新容量 / 发放碎片两件「带副作用的重操作」移出本钩子，
                                    //   本钩子随 MAM 打开/刷新树被反复调用，里面跑写操作会卡死/重入（实测：研究完碎片开分析机卡屏）。
                                    //   二者改由真正的研究完成事件 HandleResearchStateChanged/Completed + 1s 定时器触发（见 ResearchTree.cpp），
                                    //   功能不丢，且不在只读查询路径上。
                                    WT_ValidateAndRepairTree(W);
                                    WT_AdvanceResearchBlocks(W);
                                }
                                Scope(WorldCtx, Tree, Ignore);   // 跑原版遍历（此时 mNodes 已保证有效且为下一级）
                            };
                        SUBSCRIBE_METHOD(AFGResearchManager::IsTreeFullyResearched, MoveTemp(Handler));
                        WT_WARNF(TEXT("[wt-block] IsTreeFullyResearched self-heal + advance hook installed (#47 crash guard + #44 live advance)."));
                    }
                }
            }
            else
            {
                WT_WARNF(TEXT("[wt-diag] AFGResearchManager unavailable."));
            }

            // ★v0.2.26 MAM 节点图空白取证（续）：对照原版树的节点。
            // 我们的节点是纯 C++（FGResearchTreeNode 基类反射零属性，实锤）；
            // 原版节点是 BP 子类，布局/图标/连接等属性多半定义在 BP 里。
            // 把第一棵原版树的节点类全反射表 + 对象属性值 dump 出来对照，一次进游戏看全。
            if (AFGResearchManager* ResearchManager2 = AFGResearchManager::Get(World))
            {
                TArray<TSubclassOf<UFGResearchTree>> AllTrees2;
                ResearchManager2->GetAllResearchTrees(AllTrees2);
                for (const TSubclassOf<UFGResearchTree>& T : AllTrees2)
                {
                    UClass* TC = T.Get();
                    if (!TC)
                    {
                        continue;
                    }
                    const FString TreePath = TC->GetPathName();
                    if (TreePath.Contains(TEXT("Smart_Factory")) || TreePath.Contains(TEXT("Remote_Factory")) || TreePath.Contains(TEXT("WirelessPower")))
                    {
                        continue;
                    }
                    const TArray<UFGResearchTreeNode*> VNodes = UFGResearchTree::GetNodes(T);
                    WT_WARNF(TEXT("[wt-vtree] tree '%s' path=%s nodes=%d"),
                             *TC->GetName(), *TreePath, VNodes.Num());

                    // 原版树类自身的属性链（BP 可能给 FGResearchTree 加了布局相关字段）
                    for (UStruct* S = TC; S; S = S->GetSuperStruct())
                    {
                        FString TreeProps;
                        for (TFieldIterator<FProperty> It(S); It; ++It)
                        {
                            TreeProps += FString::Printf(TEXT("%s(%s) "), *It->GetName(), *It->GetClass()->GetName());
                        }
                        WT_WARNF(TEXT("[wt-vtree] tree-class '%s' props: %s"), *S->GetName(), *TreeProps);
                    }

                    // ★★★2026-11-05 全量 dump + **坐标重复统计**。
                    // 目的：一次进游戏定论「原版是否真的存在多个节点共用同一格点」。
                    // 已知 Parents / UnhiddenBy / NodesToUnhide 的元素是 IntVector2D（整数格点），
                    // ChildrenAndRoads 的键也是格点 ⇒ 整张图按坐标索引。若原版从无重复坐标，
                    // 则「一个方块塞 4 个节点」在设计上就不成立（第 2~4 级会覆盖第 1 级、
                    // 上层节点的 UnhiddenBy 指向自己 ⇒ 永远不显形 ⇒ 空方块且点不动）。
                    int32 DupGroups = 0;
                    int32 DupNodes  = 0;
                    {
                        TMap<FIntPoint, int32> CoordCount;
                        for (UFGResearchTreeNode* N : VNodes)
                        {
                            int32 X = 0, Y = 0;
                            if (N && WT_NodeCoord(N, X, Y))
                            {
                                ++CoordCount.FindOrAdd(FIntPoint(X, Y));
                            }
                        }
                        for (const auto& KV : CoordCount)
                        {
                            if (KV.Value > 1)
                            {
                                ++DupGroups;
                                DupNodes += KV.Value;
                            }
                        }
                        WT_WARNF(TEXT("[wt-vtree] coord audit: %d nodes / %d unique coords / %d dup groups (%d nodes)"),
                                 VNodes.Num(), CoordCount.Num(), DupGroups, DupNodes);
                    }

                    for (UFGResearchTreeNode* N : VNodes)
                    {
                        if (!N)
                        {
                            continue;
                        }
                        // 只打印数据行（[wt-nd] VANILLA），不再重复打印每节点的 props（省日志）
                        WT_DumpNodeData(N, TEXT("VANILLA"));
                    }
                    break; // 只对照第一棵原版树
                }
            }

            // 逐棵树：节点数 + 节点能否解出 schematic（BP 节点类没烤进去的话这里会是 0）
            // + 可用状态（0=Locked 1=Unlocked 2=Started 3=FinishedAll）。
            for (const TSubclassOf<UFGResearchTree>& TreeClass : WT_GetAllResearchTrees())
            {
                if (!TreeClass.Get()) continue;

                const TArray<UFGResearchTreeNode*> Nodes = UFGResearchTree::GetNodes(TreeClass);
                int32 WithSchematic = 0;
                for (UFGResearchTreeNode* Node : Nodes)
                {
                    if (Node && Node->GetNodeSchematic()) ++WithSchematic;
                }

                const int32 Status = static_cast<int32>(
                    UFGResearchTree::GetResearchTreeStatus(TreeClass, World));

                // ★2026-11-05 坐标唯一性硬校验（图按格点索引，重复坐标 = 节点互相遮盖）
                TSet<FIntPoint> Uniq;
                for (UFGResearchTreeNode* Node : Nodes)
                {
                    int32 X = 0, Y = 0;
                    if (Node && WT_NodeCoord(Node, X, Y))
                    {
                        Uniq.Add(FIntPoint(X, Y));
                    }
                }
                WT_WARNF(TEXT("[wt-diag] tree '%s': nodes=%d withSchematic=%d status=%d coords=%d/%d %s"),
                         *TreeClass->GetName(), Nodes.Num(), WithSchematic, Status,
                         Uniq.Num(), Nodes.Num(),
                         (Uniq.Num() == Nodes.Num()) ? TEXT("(unique OK)") : TEXT("(OVERLAP!)"));

                // ★★★2026-11-05 决定性判定位：**这棵树到底解锁了没**。
                //   一手依据（FGResearchManager.h:236 / :350）：
                //     `IsResesearchTreeUnlocked(Tree)` 读的是 `mUnlockedResearchTrees`
                //     （UPROPERTY(SaveGame, Replicated)，见 :401）；
                //     写入只有两条路：`UnlockResearchTree()`（由 UpdateUnlockedResearchTrees 调）。
                //   我们的树是运行时 NewObject/CDO 造出来的，注册时机晚于管理器 PopulateResearchTreeList
                //   ⇒ 如果这里 false，MAM 会把整棵树按「未解锁」渲染：节点全灰、一个都点不动。
                //   这与用户截图「14 个灰方块 + 不可研究」完全吻合（14 = 14 个 tile 的首级节点数）。
                if (AFGResearchManager* RM = AFGResearchManager::Get(World))
                {
                    WT_WARNF(TEXT("[wt-diag] tree '%s' unlocked=%d (mUnlockedResearchTrees)"),
                             *TreeClass->GetName(),
                             RM->IsResesearchTreeUnlocked(TreeClass) ? 1 : 0);

                    // 未解锁就主动让管理器重跑一次它自己的解锁判定（幂等：已解锁时无副作用）。
                    // 只在有权威的一侧做 —— mUnlockedResearchTrees 是 SaveGame+Replicated，
                    // 客户端写它没有意义，还会和服务器打架。
                    if (World->GetNetMode() != NM_Client)
                    {
                        RM->UpdateUnlockedResearchTrees();
                        WT_WARNF(TEXT("[wt-diag] tree '%s' unlocked after refresh=%d"),
                                 *TreeClass->GetName(),
                                 RM->IsResesearchTreeUnlocked(TreeClass) ? 1 : 0);
                    }
                }

                // ★v0.2.29 我们的节点数据 A/B：每条轨道抽第 1 个 + 第 2 个（看坐标/父节点/连线）
                if (Nodes.Num() > 0)
                {
                    const int32 Pick[4] = { 0, 1, WT_MaxUpgradeLevel, WT_MaxUpgradeLevel + 1 };
                    for (int32 Pi = 0; Pi < 4; ++Pi)
                    {
                        const int32 Idx = Pick[Pi];
                        if (Nodes.IsValidIndex(Idx))
                        {
                            WT_DumpNodeData(Nodes[Idx],
                                *FString::Printf(TEXT("WT[%d]"), Idx));
                        }
                    }
                }
            }

            // ★v0.2.24 MAM 节点图空白取证：树在、节点在（withSchematic=20），但 MAM 中间一片空。
            // 社区头的 UFGResearchTreeNode 只有 GetNodeSchematic()，真实引擎类多半还有我们没设的
            // 字段（位置/图标/半径…）。"猜属性名必错" —— 直接把反射表 dump 出来，一次进游戏看全。
            {
                for (UStruct* S = UWTResearchTreeNode::StaticClass(); S; S = S->GetSuperStruct())
                {
                    FString Props;
                    for (TFieldIterator<FProperty> It(S); It; ++It)
                    {
                        Props += FString::Printf(TEXT("%s(%s) "), *It->GetName(), *It->GetClass()->GetName());
                    }
                    WT_WARNF(TEXT("[wt-diag] node-class '%s' props: %s"), *S->GetName(), *Props);
                }
                for (UStruct* S = WT_GetTreeClass(); S; S = S->GetSuperStruct())
                {
                    FString Props;
                    for (TFieldIterator<FProperty> It(S); It; ++It)
                    {
                        Props += FString::Printf(TEXT("%s(%s) "), *It->GetName(), *It->GetClass()->GetName());
                    }
                    WT_WARNF(TEXT("[wt-diag] tree-class '%s' props: %s"), *S->GetName(), *Props);
                }
            }
        }

        // ★资源节点「加载存档自动扫描覆盖」（2026-10-25 用户需求）：
        //   进档后自动遍历全图资源节点并去重合并进 SmartFactoryLib/resourcenodes.json，
        //   供终端资源节点类目取用。覆盖 14 种：铁矿石/石灰石/铜矿石/钦金矿石/煤/原油/硫磺/
        //   铝土矿/粗石英/铀/SAM物质/水/氮气/间歇泉；资源井只收【主节点】、不收卫星子节点。
        //
        //   为什么用递增延时多次扫：World Partition 按格子流式驻留，单次扫描只能看到已驻留的那部分，
        //   一次进游戏采不全。MergeSpawns 按「资源名 + 坐标(取整到米)」去重 ⇒ 重复扫描零新增时
        //   根本不会重写文件（幂等、无日志抖动、无 I/O 浪费）。
        //
        //   为什么只在 Game 世界武装：POST_INITIALIZATION 在主菜单等非游戏世界也会派发，
        //   那里没有任何资源节点，扫了是纯浪费。
        if (World && World->WorldType == EWorldType::Game)
        {
            TWeakObjectPtr<UWorld> WeakWorld(World);
            const float ScanDelays[] = { 3.f, 10.f, 25.f, 60.f };
            for (const float Delay : ScanDelays)
            {
                FTimerHandle ScanTimer;
                World->GetTimerManager().SetTimer(ScanTimer,
                    [WeakWorld]()
                    {
                        if (UWorld* W = WeakWorld.Get())
                        {
                            FWTResourceNodes::AutoScanMergeIntoFile(W);
                        }
                    },
                    Delay, /*bLoop=*/false);
            }
            WT_WARNF(TEXT("[wt-autoscan] resource node auto-scan armed (4 passes @3/10/25/60s)."));

            // ★1.0.7z 超频上限 250% → 400%：世界起来后**持续**补扫。
            //   1.0.7x 只扫了 3s/12s 两次，实测日志里 `class CDOs raised=0/103` 与
            //   `live factory actors raised=0/10` 同时为 0 —— 那两次扫描时原版机器蓝图类
            //   还没加载（TObjectIterator 只看已加载的类），玩家的机器也都还没放下来。
            //   现在改成：0.5s 先扫一遍，然后每 5s 循环扫一次（幂等，只往上抬），
            //   这样「玩家放下/读档载入一台机器」最迟 5 秒后就能拿到 400% 上限。
            //   开销 = 一次 TActorIterator（几毫秒），无改动时不打日志。
            if (WT_FEATURE_POWER_SHARD)
            {
                FTimerHandle CeilingTimer;
                World->GetTimerManager().SetTimer(CeilingTimer,
                    [WeakWorld]()
                    {
                        if (UWorld* W = WeakWorld.Get())
                        {
                            WT_RaiseOverclockCeiling(TEXT("world"), W);
                        }
                    },
                    0.5f, /*bLoop=*/false);

                FTimerHandle CeilingLoopTimer;
                World->GetTimerManager().SetTimer(CeilingLoopTimer,
                    [WeakWorld]()
                    {
                        if (UWorld* W = WeakWorld.Get())
                        {
                            WT_RaiseOverclockCeiling(TEXT("tick"), W);
                        }
                    },
                    5.0f, /*bLoop=*/true);

                // ★1.0.8 原版超频面板补丁（刻度文字 / 段宽 / 滑条量程 / 填充归一化）。
                //   ★1.0.8k 0.5s → 0.1s：材质 `Value` 的归一化必须跟得上玩家拖动，0.5s 一拍
                //   会出现「拖到中途就满格」的可见抖动。命中面板缓存后每拍只做几次比较与
                //   SetLayout/SetScalarParameter（都有差值守卫），开销可以忽略。
                //   ★1.0.8P 0.1s → 0.05s：滑条量程现在**跟着 pending 走**（手柄位置 = 填充占比），
                //   20Hz 才能让手柄在拖动过程中贴着鼠标，10Hz 会明显落后半格。
                //   ★1.0.8AM 0.05s → 0.025s（20Hz → 40Hz，用户「UI刷新频率加一下」）：
                //     碎片槽的重画与滑条量程都挂在这一拍里，20Hz 时「插入碎片 → 变彩色」最坏要
                //     等 50ms(排程) + 600ms(V 路限流) ；现在限流压到 0.05s，整链 ≈ 50~75ms。
                //     补丁函数本身有差值守卫（布局/参数只在真变时 Set），空闲时不产生任何写入，
                //     加班只多几次 GetText/GetPercent 比较，可忽略。
                FTimerHandle PanelPatchTimer;
                World->GetTimerManager().SetTimer(PanelPatchTimer,
                    []()
                    {
                        WT_PatchOverclockPanel();
                    },
                    0.025f, /*bLoop=*/true);
            }
        }
    }

    WT_WARNF(TEXT("[wt-reg] phase %d done."), static_cast<int32>(Phase));
}

