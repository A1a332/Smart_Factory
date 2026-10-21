#include "Remote_FactoryResearchTree.h"

#include "Remote_FactoryLog.h"
#include "Remote_FactoryUpgradeResearch.h"   // EWTUpgradeTrack, 阶段/路线查询
#include "FGResearchManager.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"   // FindFProperty / FScriptArrayHelper（按名写节点属性）

// ---------------------------------------------------------------------------
// Resolve the research node UClass once.
//
// UFGResearchTreeNode::GetNodeSchematic() is a non-virtual BlueprintImplementableEvent, so a
// plain C++ node instance returns null. The Blueprint Data asset "BP_WT_ResearchNode" (a child of
// UWTResearchTreeNode) implements GetNodeSchematic() to return its mNodeSchematic property, which
// C++ fills per-instance in ConfigureTree(). We therefore instantiate the BP class for every node.
// If the asset ever fails to cook/load we fall back to the native class (nodes then yield null
// schematics -- the tree still appears, but research is inert), so the game never crashes.
// ---------------------------------------------------------------------------
static UClass* GWTNodeClass = nullptr;
static bool    GWTNodeClassResolved = false;

static UClass* GetWTNodeClass()
{
    if (GWTNodeClassResolved)
    {
        return GWTNodeClass;
    }
    GWTNodeClassResolved = true;

    // ★v0.2.27 关键：原版**所有** MAM 节点共用一个蓝图数据资产 BPD_ResearchTreeNode
    // （2026-10-06 从游戏 pak 解包实锤：FactoryGame/Content/FactoryGame/Schematics/Research/
    //  BPD_ResearchTreeNode.uasset，11 棵原版树全部引用它）。它的蓝图变量是：
    //    Schematic(ClassProperty) / Coordinates(IntVector2D) / Parents / ChildrenAndRoads /
    //    NodesToUnhide / UnhiddenBy，且 GetNodeSchematic() 返回 Schematic。
    // MAM 的图区就是靠 Coordinates 布局、Parents 连线的 —— 我们纯 C++ 节点这些字段一个都没有，
    // 所以"树在、节点在、图标在，图区一片空白"。而且 MAM 的 UMG 蓝图多半直接
    // Cast<BPD_ResearchTreeNode_C>，用自定义类会被 Cast 掉。**必须直接用原版那个类。**
    // (Fallback: 本项目自带的 BP -> 纯 C++ 类，保证任何情况下都不崩。)
    const TCHAR* Candidates[] = {
        TEXT("/Game/FactoryGame/Schematics/Research/BPD_ResearchTreeNode.BPD_ResearchTreeNode_C"),
        // ★0.2.137 改名 Smart_Factory：新挂载根在最前，旧根保留兜底
        TEXT("/Smart_Factory/Research/BP_WT_ResearchNode.BP_WT_ResearchNode_C"),
        TEXT("/Game/Smart_Factory/Research/BP_WT_ResearchNode.BP_WT_ResearchNode_C"),
        TEXT("/Remote_Factory/Research/BP_WT_ResearchNode.BP_WT_ResearchNode_C"),
        TEXT("/Game/Remote_Factory/Research/BP_WT_ResearchNode.BP_WT_ResearchNode_C"),
        // ★ fallback（铁律：改名后保留旧根兜底）
        TEXT("/WirelessTerminal/Research/BP_WT_ResearchNode.BP_WT_ResearchNode_C"),
        TEXT("/Game/WirelessTerminal/Research/BP_WT_ResearchNode.BP_WT_ResearchNode_C"),
        TEXT("BP_WT_ResearchNode_C"),
    };
    for (const TCHAR* Path : Candidates)
    {
        if (UClass* BP = LoadClass<UFGResearchTreeNode>(nullptr, Path))
        {
            BP->AddToRoot(); // 0.2.7: 静态裸 UClass* 不是 GC 引用，BP 类必须钉死
            GWTNodeClass = BP;

            // 取证：把该节点类的反射属性表打出来（一次进游戏就能核对名字/类型对不对）
            FString Props;
            for (TFieldIterator<FProperty> It(BP); It; ++It)
            {
                Props += FString::Printf(TEXT("%s(%s) "), *It->GetName(), *It->GetClass()->GetName());
            }
            WT_WARNF(TEXT("[wt-node] using node class '%s' props: %s"), *BP->GetName(), *Props);
            return GWTNodeClass;
        }
    }

    GWTNodeClass = UWTResearchTreeNode::StaticClass();
    WT_WARNF(TEXT("[research-tree] no BP node class found; falling back to native class "
                  "(tree visible but MAM graph will be blank)."));
    return GWTNodeClass;
}

// ---------------------------------------------------------------------------
// ★v0.2.30 按前缀在结构体作用域里找属性。
// 关键坑（0.2.29 实锤）：MAMTree_NodeData_Struct 是**蓝图自定义结构体（UserDefinedStruct）**，
// 其成员的运行时 FName 带 GUID 后缀（pak 名称表里的 `Schematic_27_3663...` 就是本名）——
// 按精确名 FindFProperty("Schematic") 对**原版节点也查不到**（[wt-nd] VANILLA ... prop missing 实锤）。
// 只能前缀匹配。
// ---------------------------------------------------------------------------
static FProperty* WT_FindStructMember(UStruct* Scope, const TCHAR* BaseName)
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

/** 原版 BP 节点的数据载体是 mNodeDataStruct 结构体；找不到就退回节点对象本身。 */
static void WT_GetNodeDataScope(UFGResearchTreeNode* Node, void*& OutData, UStruct*& OutScope)
{
    OutData  = Node;
    OutScope = Node->GetClass();
    if (FStructProperty* NodeDataProp = FindFProperty<FStructProperty>(Node->GetClass(), TEXT("mNodeDataStruct")))
    {
        OutData  = NodeDataProp->ContainerPtrToValuePtr<void>(Node);
        OutScope = NodeDataProp->Struct;
    }
}

// ===========================================================================
// ★★0.2.44 MAM 连线修复 —— **「树在、方块在、就是没有连线」的真正根因**
//
// 实证链（全部来自游戏日志与本机 utoc，无一处猜测）：
//   ① 0.2.31 反射表实锤（日志 [wt-nd-prop]）：
//        Schematic(ClassProperty) / Coordinates(StructProperty) /
//        Parents(ArrayProperty, inner=**StructProperty**) /
//        ChildrenAndRoads(MapProperty, key=Struct, value=Struct) /
//        UnhiddenBy(ArrayProperty, inner=Struct) / NodesToUnhide(ArrayProperty, inner=Struct)
//   ② 0.2.32 起我们把 Parents 当「对象引用数组」写 ⇒ CastField<FObjectPropertyBase>(Inner)
//      恒为 null ⇒ 走「not an object ref, skipped」分支**静默跳过**。日志连续多版都在打：
//        [wt-node] member 'Parents' inner is StructProperty -- not an object ref, skipped.
//        [wt-nd] WT[0] ... Parents=0 NodesToUnhide=0 UnhiddenBy=0   ← 一个都没写进去
//   ③ 同一次 dump 的原版节点：Parents=1 / UnhiddenBy=4 ⇒ 原版是**有值**的，机制没错、写法错。
//   ④ 社区权威说明（AniMouse《Understanding path drawing between Research Tree's nodes》）：
//        「基础连线走 Parents；ChildrenAndRoads 只是"更花哨的多检查点路径"的可选覆盖」
//      实证吻合：原版 AOrganisms_2 / ACarapace_3 的 ChildrenAndRoads=0 但 Parents=1，照样连线。
//   ⑤ 本机 utoc 资产名录实锤 MAM 图区的结构体家族：MAMTree_NodeData_Struct / MAMTree_RoadPoints /
//        MAMTree_RoadStartEnd / MAMTree_RoadOccupiedTile / MAMTree_Vector2D_Array / Widget_MAMTree_Road
//      ⇒ 图区里「节点身份」用 2D 坐标点这类小结构体表达，**不是对象指针**。
//
// 元素结构体是蓝图自定义结构体（UDS），成员 FName 带 GUID 后缀（如 X_12_3F2A…），
// 既不能硬编码、也不该猜名字。本版做法 = **活模板 + 身份字段覆盖**，全程反射：
//   ① 从一个**原版**节点的 Parents[0] 取一份"活模板"（字段名/类型/常量值全是真的）；
//   ② 新元素先按字段 CopyCompleteValue 复制模板（保留颜色/标志/检查点等常量）；
//   ③ 再覆盖身份字段：X/Y 数值（含嵌套点结构体 IntVector2D / FVector2D / MAMTree_RoadPoints）
//      → 目标父节点坐标；模板里指向研究树节点的对象引用 → 指向目标父节点。
//      名字按**前缀**匹配（X_12_AB… → "X"），绕开 GUID 后缀。
//   ④ 首次写入把「元素结构体成员表 + 回读值」打进日志；树建完再回读两条代表节点自证。
//      —— 日志里的 [wt-link] 段落就是这次改动的验收依据。
// ===========================================================================

/** 一条连线目标：父节点 + 它的图区坐标（坐标才是 MAM 图区里节点的身份）。 */
struct FWTLinkTarget
{
    UFGResearchTreeNode* Node = nullptr;
    int32 X = 0;
    int32 Y = 0;
};

/** 读节点图区坐标（与写入走同一条反射路径）。 */
static bool WT_ReadNodeCoord(UFGResearchTreeNode* Node, int32& OutX, int32& OutY)
{
    if (!Node)
    {
        return false;
    }
    void*   Data  = nullptr;
    UStruct* Scope = nullptr;
    WT_GetNodeDataScope(Node, Data, Scope);

    FStructProperty* CoordProp = CastField<FStructProperty>(WT_FindStructMember(Scope, TEXT("Coordinates")));
    if (!CoordProp)
    {
        return false;
    }
    void* CoordData = CoordProp->ContainerPtrToValuePtr<void>(Data);
    bool bAny = false;
    if (FIntProperty* XP = CastField<FIntProperty>(WT_FindStructMember(CoordProp->Struct, TEXT("X"))))
    {
        OutX = *XP->ContainerPtrToValuePtr<int32>(CoordData);
        bAny = true;
    }
    if (FIntProperty* YP = CastField<FIntProperty>(WT_FindStructMember(CoordProp->Struct, TEXT("Y"))))
    {
        OutY = *YP->ContainerPtrToValuePtr<int32>(CoordData);
        bAny = true;
    }
    return bAny;
}

static FWTLinkTarget WT_MakeLink(UFGResearchTreeNode* Node)
{
    FWTLinkTarget T;
    T.Node = Node;
    if (!WT_ReadNodeCoord(Node, T.X, T.Y))
    {
        T.X = 0;
        T.Y = 0;
    }
    return T;
}

/** 取节点数据里某个「结构体数组」成员（Parents / NodesToUnhide / UnhiddenBy 通用）。 */
static FArrayProperty* WT_FindLinkArray(UFGResearchTreeNode* Node, const TCHAR* MemberName, void*& OutData)
{
    OutData = nullptr;
    if (!Node)
    {
        return nullptr;
    }
    void*   Data  = nullptr;
    UStruct* Scope = nullptr;
    WT_GetNodeDataScope(Node, Data, Scope);
    OutData = Data;

    FArrayProperty* Arr = CastField<FArrayProperty>(WT_FindStructMember(Scope, MemberName));
    if (!Arr || !CastField<FStructProperty>(Arr->Inner))
    {
        return nullptr;
    }
    return Arr;
}

/** 属性值 → 可读文本（只用于一次性自证 dump，不进热路径）。 */
static FString WT_PropValueText(FProperty* Prop, void* Container, int32 Depth)
{
    if (!Prop || !Container)
    {
        return TEXT("<null>");
    }
    void* V = Prop->ContainerPtrToValuePtr<void>(Container);

    if (FObjectPropertyBase* OP = CastField<FObjectPropertyBase>(Prop))
    {
        UObject* O = OP->GetObjectPropertyValue(V);
        return O ? O->GetName() : TEXT("null");
    }
    if (FBoolProperty* BP = CastField<FBoolProperty>(Prop))
    {
        return BP->GetPropertyValue(V) ? TEXT("true") : TEXT("false");
    }
    if (FNumericProperty* NP = CastField<FNumericProperty>(Prop))
    {
        if (NP->IsInteger())
        {
            return FString::Printf(TEXT("%lld"), NP->GetSignedIntPropertyValue(V));
        }
        return FString::Printf(TEXT("%.3f"), NP->GetFloatingPointPropertyValue(V));
    }
    if (FStructProperty* SP = CastField<FStructProperty>(Prop))
    {
        if (Depth <= 0)
        {
            return FString::Printf(TEXT("{%s}"), *SP->Struct->GetName());
        }
        FString S(TEXT("{"));
        int32 n = 0;
        for (TFieldIterator<FProperty> It(SP->Struct); It && n < 8; ++It, ++n)
        {
            S += FString::Printf(TEXT("%s=%s "), *It->GetName(), *WT_PropValueText(*It, V, Depth - 1));
        }
        return S + TEXT("}");
    }
    if (FStrProperty* StrP = CastField<FStrProperty>(Prop))
    {
        return StrP->GetPropertyValue(V);
    }
    if (FNameProperty* NameP = CastField<FNameProperty>(Prop))
    {
        return NameP->GetPropertyValue(V).ToString();
    }
    if (FArrayProperty* AP = CastField<FArrayProperty>(Prop))
    {
        FScriptArrayHelper H(AP, V);
        return FString::Printf(TEXT("array[%d]"), H.Num());
    }
    if (FMapProperty* MP = CastField<FMapProperty>(Prop))
    {
        FScriptMapHelper H(MP, V);
        return FString::Printf(TEXT("map[%d]"), H.Num());
    }
    return Prop->GetClass()->GetName();
}

/** 结构体实例 → 可读文本。 */
static FString WT_StructValueText(UScriptStruct* S, void* Data, int32 Depth)
{
    if (!S || !Data)
    {
        return TEXT("<null>");
    }
    FString Out(TEXT("{"));
    int32 n = 0;
    for (TFieldIterator<FProperty> It(S); It && n < 12; ++It, ++n)
    {
        Out += FString::Printf(TEXT("%s(%s)=%s "), *It->GetName(), *It->GetClass()->GetName(),
                               *WT_PropValueText(*It, Data, Depth));
    }
    return Out + TEXT("}");
}

// ---------------------------------------------------------------------------
// 活模板：从原版节点取一份真实连线元素。
// 资产路径来自本机 utoc 目录索引实锤的 10 棵原版 MAM 树（BPD_ResearchTree_*）。
// ---------------------------------------------------------------------------
static UFGResearchTreeNode* GWT_LinkTplSrc   = nullptr;   // 模板所在的原版节点
static UClass*              GWT_LinkTplTree  = nullptr;   // 它所在的树（schema dump 用）
static bool                 GWT_LinkTplTried = false;

static void WT_AcquireLinkTemplate()
{
    if (GWT_LinkTplTried)
    {
        return;
    }
    GWT_LinkTplTried = true;

    static const TCHAR* kVanillaTreePaths[] = {
        TEXT("/Game/FactoryGame/Schematics/Research/BPD_ResearchTree_AlienOrganisms.BPD_ResearchTree_AlienOrganisms_C"),
        TEXT("/Game/FactoryGame/Schematics/Research/BPD_ResearchTree_AlienTech.BPD_ResearchTree_AlienTech_C"),
        TEXT("/Game/FactoryGame/Schematics/Research/BPD_ResearchTree_Caterium.BPD_ResearchTree_Caterium_C"),
        TEXT("/Game/FactoryGame/Schematics/Research/BPD_ResearchTree_HardDrive.BPD_ResearchTree_HardDrive_C"),
        TEXT("/Game/FactoryGame/Schematics/Research/BPD_ResearchTree_Mycelia.BPD_ResearchTree_Mycelia_C"),
        TEXT("/Game/FactoryGame/Schematics/Research/BPD_ResearchTree_Nutrients.BPD_ResearchTree_Nutrients_C"),
        TEXT("/Game/FactoryGame/Schematics/Research/BPD_ResearchTree_PowerSlugs.BPD_ResearchTree_PowerSlugs_C"),
        TEXT("/Game/FactoryGame/Schematics/Research/BPD_ResearchTree_Quartz.BPD_ResearchTree_Quartz_C"),
        TEXT("/Game/FactoryGame/Schematics/Research/BPD_ResearchTree_Sulfur.BPD_ResearchTree_Sulfur_C"),
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
            void* D = nullptr;
            FArrayProperty* Arr = WT_FindLinkArray(N, TEXT("Parents"), D);
            if (!Arr)
            {
                continue;
            }
            FScriptArrayHelper H(Arr, Arr->ContainerPtrToValuePtr<void>(D));
            if (H.Num() <= 0)
            {
                continue;
            }
            GWT_LinkTplSrc  = N;
            GWT_LinkTplTree = TreeCls;
            WT_WARNF(TEXT("[wt-link] element template acquired from vanilla node '%s' (Parents[0])."), *N->GetName());
            return;
        }
    }
    WT_WARNF(TEXT("[wt-link] no vanilla Parents element found; writing default-constructed link elements."));
}

/**
 * 覆盖元素里的「身份字段」：X/Y 数值 + 指向研究树节点的对象引用。
 * 返回写进去的字段数（0 = 没认出身份字段 —— 日志会告警，[wt-link] dump 会给出真实成员表）。
 */
static int32 WT_WriteLinkIdentity(UScriptStruct* S, uint8* Elem, const uint8* Src, const FWTLinkTarget& T, int32 Depth)
{
    int32 Written = 0;

    for (TFieldIterator<FProperty> It(S); It; ++It)
    {
        FProperty* P  = *It;
        void*      D  = P->ContainerPtrToValuePtr<void>(Elem);
        void*      Sv = Src ? P->ContainerPtrToValuePtr<void>(const_cast<uint8*>(Src)) : nullptr;
        const FString Name = P->GetName();

        if (FObjectPropertyBase* OP = CastField<FObjectPropertyBase>(P))
        {
            // 身份判定：模板里这个字段指向一个研究树节点（没有模板时看字段声明的类）
            bool bIdentity = false;
            if (Sv)
            {
                UObject* Cur = OP->GetObjectPropertyValue(Sv);
                bIdentity = Cur && Cur->IsA(UFGResearchTreeNode::StaticClass());
            }
            else if (OP->PropertyClass)
            {
                bIdentity = OP->PropertyClass->IsChildOf(UFGResearchTreeNode::StaticClass());
            }
            if (bIdentity && T.Node)
            {
                OP->SetObjectPropertyValue(D, T.Node);
                ++Written;
            }
            continue;
        }

        if (FStructProperty* SP = CastField<FStructProperty>(P))
        {
            // 坐标点 / 嵌套包装结构体（IntVector2D / FVector2D / MAMTree_RoadPoints…）
            Written += WT_WriteLinkIdentity(SP->Struct, static_cast<uint8*>(D),
                                            Sv ? static_cast<const uint8*>(Sv) : nullptr, T, Depth + 1);
            continue;
        }

        if (FNumericProperty* NP = CastField<FNumericProperty>(P))
        {
            const bool bX = Name.StartsWith(TEXT("X"));
            const bool bY = Name.StartsWith(TEXT("Y"));
            if (bX || bY)
            {
                const int32 Val = bX ? T.X : T.Y;
                if (NP->IsInteger())
                {
                    NP->SetIntPropertyValue(D, static_cast<int64>(Val));
                }
                else
                {
                    NP->SetFloatingPointPropertyValue(D, static_cast<double>(Val));
                }
                ++Written;
            }
            continue;
        }
    }

    // 兜底：整份结构体一个身份字段都没认出来，但有数值成员 ⇒ 按声明顺序当 (X, Y)。
    if (Depth == 0 && Written == 0)
    {
        int32 k = 0;
        for (TFieldIterator<FProperty> It(S); It; ++It)
        {
            FNumericProperty* NP = CastField<FNumericProperty>(*It);
            if (!NP)
            {
                continue;
            }
            void* D = NP->ContainerPtrToValuePtr<void>(Elem);
            const int32 Val = (k == 0) ? T.X : T.Y;
            if (NP->IsInteger())
            {
                NP->SetIntPropertyValue(D, static_cast<int64>(Val));
            }
            else
            {
                NP->SetFloatingPointPropertyValue(D, static_cast<double>(Val));
            }
            ++k;
            ++Written;
            if (k >= 2)
            {
                break;
            }
        }
        if (Written > 0)
        {
            WT_WARNF(TEXT("[wt-link] element '%s' has no X/Y-named numeric; wrote %d positional as (X=%d, Y=%d)."),
                     *S->GetName(), Written, T.X, T.Y);
        }
    }
    return Written;
}

/** 往 Parents / NodesToUnhide / UnhiddenBy 追加一条连线（元素 = 蓝图结构体，全程反射）。 */
static int32 WT_AppendLinkElem(UFGResearchTreeNode* Node, const TCHAR* MemberName, const FWTLinkTarget& T)
{
    if (!Node || !T.Node)
    {
        return 0;
    }
    WT_AcquireLinkTemplate();

    void* D = nullptr;
    FArrayProperty* Arr = WT_FindLinkArray(Node, MemberName, D);
    if (!Arr)
    {
        static TSet<FName> Warned;
        const FName Key(MemberName);
        if (!Warned.Contains(Key))
        {
            Warned.Add(Key);
            WT_WARNF(TEXT("[wt-link] member '%s' is not a struct array on node data -- 连线跳过（不崩）。"), MemberName);
        }
        return 0;
    }

    UScriptStruct* S = CastField<FStructProperty>(Arr->Inner)->Struct;

    // 活模板：同一个成员 + 同一个结构体类型才可用（Parents 与 UnhiddenBy 结构体不一定相同）
    const uint8* Src = nullptr;
    if (GWT_LinkTplSrc)
    {
        void* TD = nullptr;
        FArrayProperty* TArr = WT_FindLinkArray(GWT_LinkTplSrc, MemberName, TD);
        if (TArr && CastField<FStructProperty>(TArr->Inner)->Struct == S)
        {
            FScriptArrayHelper TH(TArr, TArr->ContainerPtrToValuePtr<void>(TD));
            if (TH.Num() > 0)
            {
                Src = static_cast<const uint8*>(TH.GetRawPtr(0));
            }
        }
    }

    FScriptArrayHelper H(Arr, Arr->ContainerPtrToValuePtr<void>(D));
    const int32 Idx = H.AddValue();                       // 默认构造（对象引用置空）
    uint8* Dst = static_cast<uint8*>(H.GetRawPtr(Idx));

    if (Src)
    {
        for (TFieldIterator<FProperty> It(S); It; ++It)   // 先抄模板常量，再覆盖身份
        {
            It->CopyCompleteValue_InContainer(Dst, const_cast<uint8*>(Src));
        }
    }
    const int32 Written = WT_WriteLinkIdentity(S, Dst, Src, T, 0);

    static bool bDumpOnce = false;
    if (!bDumpOnce)
    {
        bDumpOnce = true;
        FString Members;
        for (TFieldIterator<FProperty> It(S); It; ++It)
        {
            Members += FString::Printf(TEXT("%s(%s) "), *It->GetName(), *It->GetClass()->GetName());
        }
        WT_WARNF(TEXT("[wt-link] elemStruct=%s members: %s"), *S->GetName(), *Members);
        WT_WARNF(TEXT("[wt-link] first write member=%s template=%s wroteFields=%d target=%s(%d,%d) readback=%s"),
                 MemberName, Src ? TEXT("yes") : TEXT("no"), Written,
                 *T.Node->GetName(), T.X, T.Y, *WT_StructValueText(S, Dst, 1));
    }
    return Written;
}

/**
 * 一次性留档：原版 ChildrenAndRoads（Map<结构体,结构体>）的真实成员表。
 * 本版不写它（原版节点普遍为 0 也照样连线）；它代表"多检查点的弯曲路径"，
 * 先把布局抓进日志，万一将来要画弯线就不必再进一次游戏取证。
 */
static void WT_DumpVanillaRoadSchemaOnce()
{
    static bool bDone = false;
    if (bDone || !GWT_LinkTplTree)
    {
        return;
    }
    bDone = true;

    TArray<UFGResearchTreeNode*> Nodes = UFGResearchTree::GetNodes(GWT_LinkTplTree);
    for (UFGResearchTreeNode* N : Nodes)
    {
        void*   D     = nullptr;
        UStruct* Scope = nullptr;
        WT_GetNodeDataScope(N, D, Scope);
        FMapProperty* MP = CastField<FMapProperty>(WT_FindStructMember(Scope, TEXT("ChildrenAndRoads")));
        if (!MP)
        {
            continue;
        }
        FScriptMapHelper H(MP, MP->ContainerPtrToValuePtr<void>(D));
        if (H.Num() <= 0)
        {
            continue;
        }
        FStructProperty* KP = CastField<FStructProperty>(MP->KeyProp);
        FStructProperty* VP = CastField<FStructProperty>(MP->ValueProp);
        WT_WARNF(TEXT("[wt-road] node '%s' ChildrenAndRoads=%d keyStruct=%s valueStruct=%s"),
                 *N->GetName(), H.Num(),
                 KP ? *KP->Struct->GetName() : TEXT("<not a struct>"),
                 VP ? *VP->Struct->GetName() : TEXT("<not a struct>"));
        if (KP)
        {
            WT_WARNF(TEXT("[wt-road]   key   %s"), *WT_StructValueText(KP->Struct, H.GetKeyPtr(0), 1));
        }
        if (VP)
        {
            WT_WARNF(TEXT("[wt-road]   value %s"), *WT_StructValueText(VP->Struct, H.GetValuePtr(0), 1));
        }
        return;
    }
    WT_WARNF(TEXT("[wt-road] no vanilla ChildrenAndRoads entry found for schema dump."));
}

/**
 * ★「同一个方块买 4 次」的核心：A 研究完成后揭示 B。
 *   A.NodesToUnhide += B；B.UnhiddenBy += A。两个方向都写，MAM 读哪个都不会空。
 */
static void WT_LinkUnhide(UFGResearchTreeNode* From, UFGResearchTreeNode* To)
{
    if (!From || !To)
    {
        return;
    }
    WT_AppendLinkElem(From, TEXT("NodesToUnhide"), WT_MakeLink(To));
    WT_AppendLinkElem(To,   TEXT("UnhiddenBy"),    WT_MakeLink(From));
}

// ---------------------------------------------------------------------------
// ★v0.2.28/30 把一份节点数据写进原版节点类。
// 结构：BPD_ResearchTreeNode_C 反射只有一个属性 mNodeDataStruct(StructProperty)，
// 其类型 MAMTree_NodeData_Struct（UserDefinedStruct）装着
// Schematic / Coordinates / Parents / ChildrenAndRoads / NodesToUnhide / UnhiddenBy。
// 写法：定位 mNodeDataStruct → 在其 Struct 里前缀匹配成员 → 按类型写；缺就跳过不崩。
// ---------------------------------------------------------------------------
static void WT_WriteNodeData(UFGResearchTreeNode* Node, UClass* SchematicClass,
                             int32 CoordX, int32 CoordY,
                             const TArray<FWTLinkTarget>& Parents)
{
    if (!Node)
    {
        return;
    }

    if (UWTResearchTreeNode* WTNode = Cast<UWTResearchTreeNode>(Node))
    {
        WTNode->mNodeSchematic = SchematicClass;   // 纯 C++ 回退路径用
    }

    void*   Data  = nullptr;
    UStruct* Scope = nullptr;
    WT_GetNodeDataScope(Node, Data, Scope);

    // ① Schematic：原版 GetNodeSchematic() 的返回值来源
    if (FClassProperty* SchematicProp = CastField<FClassProperty>(WT_FindStructMember(Scope, TEXT("Schematic"))))
    {
        SchematicProp->SetPropertyValue_InContainer(Data, SchematicClass);
    }

    // ② Coordinates（IntVector2D）：MAM 图区的网格坐标
    //    ★2026-10-07 布局改成「3 列 x 4 层」：X = 轨列位、Y = 阶段序号(0~3)；
    //      ★0.2.36h 列位 0/1/2 → 2/4/6（列距 2）；
    //      ★0.2.41（用户实测「MAM 研究偏右了」）：再左移 1 个坐标单位 → **1/3/5**，末段中轴 X=3。
    //      依据 = 实测截图量测：0.2.39（2/4/6）三列在屏上的中心比 MAM 图区中心**偏右 69px**，
    //      而列距 2 单位 ≈ 141px（1080p）⇒ 1 单位 ≈ 70px —— 左移 1 单位恰好回正。
    //      （0/1/2 时代用户观感「挤在左半边」、2/4/6 时代「偏右」，本值取两者之间。）
    //      每个方块内 4 个 schematic 共用同一坐标。
    if (FStructProperty* CoordProp = CastField<FStructProperty>(WT_FindStructMember(Scope, TEXT("Coordinates"))))
    {
        void* CoordData = CoordProp->ContainerPtrToValuePtr<void>(Data);
        if (FIntProperty* XP = CastField<FIntProperty>(WT_FindStructMember(CoordProp->Struct, TEXT("X"))))
        {
            *XP->ContainerPtrToValuePtr<int32>(CoordData) = CoordX;
        }
        if (FIntProperty* YP = CastField<FIntProperty>(WT_FindStructMember(CoordProp->Struct, TEXT("Y"))))
        {
            *YP->ContainerPtrToValuePtr<int32>(CoordData) = CoordY;
        }
    }

    // ③ Parents：★★0.2.44 元素是**蓝图结构体**（0.2.31 反射表实锤 inner=StructProperty），
    //    不是对象引用。0.2.32 当成对象引用写 ⇒ 每次追加都在类型转换处静默跳过 ⇒ **一路没有连线**。
    //    现在交给 WT_AppendLinkElem（活模板 + 身份字段覆盖，见文件上方长注释）。
    for (const FWTLinkTarget& P : Parents)
    {
        WT_AppendLinkElem(Node, TEXT("Parents"), P);
    }
}

// ---------------------------------------------------------------------------
// Tile（方块）构造：
//   一个「方块」= 同一坐标上的 4 个 schematic（Lv1~Lv4），靠 NodesToUnhide 逐级揭示，
//   肉眼看到的就是同一个方块被研究了 4 次 —— 见 WT_MAM_研究树结构图.svg。
// 返回该方块的最后一个节点（供下一阶段 / 末段挂 UnhiddenBy）。
// ---------------------------------------------------------------------------
static UFGResearchTreeNode* WT_MakeTile(
    UObject* Outer,
    const EWTUpgradeTrack Track,
    int32 Stage, int32 CoordX, int32 CoordY,
    UFGResearchTreeNode* ParentOfFirst,
    TArray<TObjectPtr<UFGResearchTreeNode>>& OutNodes)
{
    UFGResearchTreeNode* FirstOfTile = nullptr;
    UFGResearchTreeNode* PrevLevel   = nullptr;

    for (int32 k = 1; k <= 4; ++k)
    {
        const int32 L = (Stage - 1) * 4 + k;
        UFGResearchTreeNode* Node = NewObject<UFGResearchTreeNode>(Outer, GetWTNodeClass());
        if (!Node)
        {
            // ★自检加固（2026-10-07）：GetWTNodeClass() 保证非空（末位回退到 UWTResearchTreeNode
            // 原生类），所以这里实际不会触发；但下一行就是 AddToRoot() 解引用，补一道守卫让
            // 「分配失败」表现为少一个节点而不是进程崩溃。
            WT_WARNF(TEXT("[research-tree] node allocation failed for L=%d; stage incomplete."), L);
            continue;
        }
        // ROOT-PIN: MAM 在 populate 时从树 CDO 的 mNodes 读节点；不钉住可能被 GC 掉。
        Node->AddToRoot();

        TArray<FWTLinkTarget> Parents;
        if (k == 1)
        {
            if (ParentOfFirst)
            {
                Parents.Add(WT_MakeLink(ParentOfFirst));
            }
        }
        else
        {
            Parents.Add(WT_MakeLink(PrevLevel));       // 同坐标内上一级
            WT_LinkUnhide(PrevLevel, Node);            // 买完上一级才露出本级
        }

        WT_WriteNodeData(Node, WT_ResearchClassFor(Track, L), CoordX, CoordY, Parents);
        OutNodes.Add(Node);

        if (k == 1)
        {
            FirstOfTile = Node;
        }
        PrevLevel = Node;
    }
    return PrevLevel;
}

/** 末段一个方块（Lv17~20）：坐标为 (2,4) 常规 / (4,4) 碎片（★0.2.42 两块横向并排、同排 Y=4，图标区分见 WT_IconSpecFor），父节点 = 阶段 4 三个末级。 */
static UFGResearchTreeNode* WT_MakeFinalTile(
    UObject* Outer, EWTFinalRoute Route, int32 CoordY,
    const TArray<FWTLinkTarget>& ConvergingParents,
    TArray<TObjectPtr<UFGResearchTreeNode>>& OutNodes,
    UFGResearchTreeNode** OutFirstLevel = nullptr,
    int32 CoordX = 4)
{
    UFGResearchTreeNode* PrevLevel  = nullptr;
    UFGResearchTreeNode* FirstLevel = nullptr;

    for (int32 L = WT_FinalMinLevel; L <= WT_MaxUpgradeLevel; ++L)
    {
        UFGResearchTreeNode* Node = NewObject<UFGResearchTreeNode>(Outer, GetWTNodeClass());
        if (!Node)
        {
            // ★自检加固（2026-10-07）：同上一处，补判空避免 AddToRoot() 裸解引用。
            WT_WARNF(TEXT("[research-tree] final-route node allocation failed for L=%d."), L);
            continue;
        }
        Node->AddToRoot();

        TArray<FWTLinkTarget> Parents;
        if (L == WT_FinalMinLevel)
        {
            Parents = ConvergingParents;
            FirstLevel = Node;
        }
        else
        {
            Parents.Add(WT_MakeLink(PrevLevel));
            WT_LinkUnhide(PrevLevel, Node);
        }

        WT_WriteNodeData(Node, WT_FinalResearchClassFor(Route, L), CoordX, CoordY, Parents);
        OutNodes.Add(Node);
        PrevLevel = Node;
    }
    if (OutFirstLevel)
    {
        *OutFirstLevel = FirstLevel;
    }
    return PrevLevel;
}

// ---------------------------------------------------------------------------
// ★v0.2.34 三轨合并 + 14 方块布局（2026-10-07 用户定案，见 WT_MAM_研究树结构图.svg）
//   ★0.2.36h 列位 0/1/2 → 2/4/6（列距 2）；
//   ★0.2.41（用户实测「MAM 研究偏右了」）：列位 X = **1/3/5**（列距 2 不变），末段 X=3 ——
//   三列沿 X=3 中轴对称。实测截图量测：2/4/6 的树中心比 MAM 图区中心偏右 69px ≈ 1 个
//   坐标单位（列距 2 ≈ 141px@1080p），左移 1 单位恰好回正；0/1/2 时代观感「挤在左半边」。
//
//   3 列（大容量仓库 / 储能单元 / 强化能量碎片）
// x  4 层（阶段 1~4，坐标 Y = 0~3）= 12 个方块，每块买 4 次
//   + 末段 2 个方块（常规 (3,4) / 碎片 (3,5)），各自买 4 次
//   = 56 个 schematic，但图上只有 14 个方块。
// ---------------------------------------------------------------------------
void UWTResearchTreeBase::ConfigureMergedTree(const FText& DisplayName, const FText& Description)
{
    mDisplayName           = DisplayName;
    mPreUnlockDisplayName  = DisplayName;
    mPreUnlockDescription  = Description;
    mPostUnlockDescription = Description;

    const EWTUpgradeTrack Tracks[3] = {
        EWTUpgradeTrack::Storage,
        EWTUpgradeTrack::PowerStorage,
        EWTUpgradeTrack::Accelerator,
    };

    TArray<TObjectPtr<UFGResearchTreeNode>> Nodes;
    Nodes.Reserve(14 * 4);

    UFGResearchTreeNode* Stage4Last[3] = { nullptr, nullptr, nullptr };

    for (int32 ti = 0; ti < 3; ++ti)
    {
        UFGResearchTreeNode* PrevTileLast = nullptr;   // 上一阶段的末级（跨阶段连线）
        for (int32 Stage = 1; Stage <= 4; ++Stage)
        {
            UFGResearchTreeNode* TileLast =
                WT_MakeTile(this, Tracks[ti], Stage, /*CoordX=*/1 + ti * 2, /*CoordY=*/Stage - 1,
                            PrevTileLast, Nodes);
            PrevTileLast = TileLast;
        }
        Stage4Last[ti] = PrevTileLast;
    }

    // 末段：阶段 4 的三个末级一起汇入两个最终方块
    TArray<FWTLinkTarget> Converging;
    for (int32 ti = 0; ti < 3; ++ti)
    {
        if (Stage4Last[ti])
        {
            Converging.Add(WT_MakeLink(Stage4Last[ti]));
        }
    }

    UFGResearchTreeNode* PlainFirst = nullptr;
    UFGResearchTreeNode* BoostFirst = nullptr;
    // ★0.2.42 末段两块横向并排：常规 (2,4) / 碎片 (4,4)，同排一眼区分。
    WT_MakeFinalTile(this, EWTFinalRoute::Plain, /*CoordY=*/4, Converging, Nodes, &PlainFirst, /*CoordX=*/2);
    WT_MakeFinalTile(this, EWTFinalRoute::Boost, /*CoordY=*/4, Converging, Nodes, &BoostFirst, /*CoordX=*/4);

    // 阶段 4 的三个末级一起揭示末段两个方块的首级（Parents 已经在上面连好，这里补 UnhiddenBy）
    for (int32 ti = 0; ti < 3; ++ti)
    {
        if (Stage4Last[ti])
        {
            WT_LinkUnhide(Stage4Last[ti], PlainFirst);
            WT_LinkUnhide(Stage4Last[ti], BoostFirst);
        }
    }

    mNodes = Nodes;

    WT_WARNF(TEXT("[wt-node] merged tree '%s': %d nodes (= 14 tiles x 4 levels)."),
             *DisplayName.ToString(), Nodes.Num());

    // ★0.2.44 连线自证：把两条代表节点的 Parents 回读进日志 + 原版 ChildrenAndRoads 布局留档。
    // 判读法：node[1] = 第 1 格 L2（父 = 同坐标 L1，即 (1,0)）；node[4] = 第 2 格 L1（父 = L4，(1,0)）。
    // 元素里的数值字段应等于父节点坐标；若元素里有对象引用，应等于父节点对象名。
    WT_DumpVanillaRoadSchemaOnce();
    static const int32 kProbes[2] = { 1, 4 };
    for (int32 i = 0; i < 2; ++i)
    {
        UFGResearchTreeNode* N = Nodes.IsValidIndex(kProbes[i]) ? Nodes[kProbes[i]].Get() : nullptr;
        if (!N)
        {
            continue;
        }
        void* D = nullptr;
        FArrayProperty* Arr = WT_FindLinkArray(N, TEXT("Parents"), D);
        if (!Arr)
        {
            WT_WARNF(TEXT("[wt-link] selfcheck node[%d]: Parents array missing."), kProbes[i]);
            continue;
        }
        UScriptStruct* S = CastField<FStructProperty>(Arr->Inner)->Struct;
        FScriptArrayHelper H(Arr, Arr->ContainerPtrToValuePtr<void>(D));
        int32 nx = -1;
        int32 ny = -1;
        WT_ReadNodeCoord(N, nx, ny);
        FString Body;
        if (H.Num() > 0)
        {
            Body = WT_StructValueText(S, H.GetRawPtr(0), 1);
        }
        WT_WARNF(TEXT("[wt-link] selfcheck node[%d] '%s' coord=(%d,%d) Parents=%d elem=%s"),
                 kProbes[i], *N->GetName(), nx, ny, H.Num(), *Body);
    }
}

// ---------------------------------------------------------------------------
// 单轨版本（v0.2.24 起不再注册，保留给三个旧树类用）。
// ---------------------------------------------------------------------------
void UWTResearchTreeBase::ConfigureTree(EWTUpgradeTrack Track, const FText& DisplayName)
{
    mDisplayName          = DisplayName;
    mPreUnlockDisplayName = DisplayName;
    mPreUnlockDescription = FText::FromString(
        TEXT("通过 MAM 逐级解锁智能工厂的升级建筑。每级成本递增，对应建筑按等级自动强化。"));
    mPostUnlockDescription = mPreUnlockDescription;

    TArray<TObjectPtr<UFGResearchTreeNode>> Nodes;
    Nodes.Reserve(4 * 4);

    UFGResearchTreeNode* PrevTileLast = nullptr;
    for (int32 Stage = 1; Stage <= 4; ++Stage)
    {
        PrevTileLast = WT_MakeTile(this, Track, Stage, /*CoordX=*/0, /*CoordY=*/Stage - 1,
                                   PrevTileLast, Nodes);
    }
    mNodes = Nodes;

    WT_WARNF(TEXT("[wt-node] %s: attached %d nodes (4 tiles x 4 levels)."),
             *DisplayName.ToString(), Nodes.Num());
}

// ★v0.2.54 MAM 修复（回退 v0.2.54 F1 资产类方案）：
//   实证（用户跨版本 A/B 实测，无猜测）：
//     · 纯 C++ 类 UWTResearchTreeMerged 时代（v0.2.41「偏右了」、v0.2.44「没有连线」）
//       —— 这些抱怨只可能发生在「节点已可见」的前提下，证明 C++ 类能正常渲染节点；
//     · v0.2.54 改用资产类 BP_WT_ResearchTree_C（F1「GetPrimaryAssetId 取不到」假说）后，
//       用户实测「依旧未显示节点」，假说被实测推翻。
//   二者唯一功能差异就是「树类身份」。故回到已验证可用的 C++ 类方案：
//   节点数据仍由 ConfigureMergedTree() 在运行时写入 C++ CDO 的 mNodes（逻辑不变）。
//   BP_WT_ResearchTree / BP_WT_ResearchNode 资产不再作为树身份使用（仅留作兼容、不参与注册）。
TArray<TSubclassOf<UFGResearchTree>> WT_GetAllResearchTrees()
{
    // ★0.2.56：不再向 MAM 注册任何 Remote_Factory 研究树（用户定案：MAM 里不应出现无线终端
    // 研究科技，含升级建筑）。升级建筑改由 WT_RepairRecipeAvailability 的 Wanted 清单在 T1 解锁后
    // 自动发放，不在 MAM 占节点。故返回空数组。
    return TArray<TSubclassOf<UFGResearchTree>>();
}

void WT_BuildResearchTrees()
{
    static bool bBuilt = false;
    if (bBuilt) return;

    UWTResearchTreeBase* CDO = Cast<UWTResearchTreeBase>(UWTResearchTreeMerged::StaticClass()->GetDefaultObject());
    if (!CDO)
    {
        WT_WARNF(TEXT("[research-tree] merged tree CDO missing"));
        bBuilt = true;
        return;
    }
    CDO->ConfigureMergedTree(
        WT_LOCTEXT("Tree_Remote_Factory", "智能工厂"),
        WT_LOCTEXT("TreeDesc_Remote_Factory",
            "Research the Smart Factory's upgrade buildings level by level in the MAM. "
            "Each level costs more than the last; the matching building scales automatically."));

    bBuilt = true;
    WT_LOGF(TEXT("[research-tree] built the merged MAM tree (14 tiles x 4 levels = 56 schematics)."));
}
