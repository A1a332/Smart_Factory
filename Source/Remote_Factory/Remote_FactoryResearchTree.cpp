#include "Remote_FactoryResearchTree.h"

#include "Remote_FactoryLog.h"
#include "Remote_FactoryRegistry.h"          // WT_FEATURE_MAM_RESEARCH（研究树上架总闸）
#include "Remote_FactoryUpgradeResearch.h"   // EWTUpgradeTrack, 阶段/路线查询
#include "Remote_FactoryUpgrades.h"           // WT_RefreshResearchedUpgrades（研究完成即时刷新已放置升级建筑）
#include "FGResearchManager.h"
#include "AvailabilityDependencies/FGAvailabilityDependency.h"   // ★探针：逐条依赖 AreDependenciesMet
#include "FGSchematicManager.h"   // ★2026-11-05 诊断：IsSchematicPurchased（T1 到底买了没）
#include "ItemAmount.h"           // ★2026-11-07 成本诊断：UFGSchematic::GetCost 返回 TArray<FItemAmount>
#include "Engine/World.h"         // AFGSchematicManager::Get(World) 需要完整类型
#include "FGPlayerController.h"   // ★2026-11-08 LV16/LV17 提示：GetGameUI()->ShowTextNotification 给玩家弹一行提示
#include "UI/FGGameUI.h"         // UFGGameUI::ShowTextNotification(const FText&)
#include "Engine/Engine.h"       // GEngine / FWorldContext（CDO 上 GetWorld() 为 null，必须绕道）
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"   // FindFProperty / FScriptArrayHelper（按名写节点属性）
#include <excpt.h>                // SEH：GetExceptionCode / EXCEPTION_ACCESS_VIOLATION（安全探测坏节点）

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

static int32 WT_RawNodeAlive(UFGResearchTreeNode* N);   // 前向声明（SEH 探针，定义见下方）

/** 原版 BP 节点的数据载体是 mNodeDataStruct 结构体；找不到就退回节点对象本身。 */
static void WT_GetNodeDataScope(UFGResearchTreeNode* Node, void*& OutData, UStruct*& OutScope)
{
    // ★★★2026-11-08 同型崩溃第三次根治（用户实测崩溃报告：UnrealType.h:695
    //   `check(((UObject*)ContainerPtr)->IsValidLowLevel())`，栈顶 6 个 Remote_Factory 帧，
    //   上面紧挨 `AFGResearchManager::execIsTreeFullyResearched`，日志先打 "Other object in slot"）。
    //
    //   关键认知：下面 `NodeDataProp->ContainerPtrToValuePtr<void>(Node)` 走的是**UObject 容器重载**，
    //   它内部第一件事就是上面那条 `check()` —— 那是**断言、直接 Fatal 崩游戏，不是 AV**，
    //   所以既有 SEH 探针（WT_RawNodeAlive/WT_ProbeNodeSchematic）**根本拦不住它**：
    //   先被 SEH 判活放行 → 再碰属性 → 撞断言带走进程。
    //
    //   故这里把「强有效」提为**前置条件**：不通过就让调用方拿到 null Data/Scope 并安全跳过，
    //   绝不再对可疑指针做任何属性访问。调用方必须判空（已在各点补齐）。
    OutData  = nullptr;
    OutScope = nullptr;
    if (!Node || WT_RawNodeAlive(Node) != 1)
    {
        return;
    }
    OutData  = Node;
    OutScope = Node->GetClass();
    if (FStructProperty* NodeDataProp = FindFProperty<FStructProperty>(Node->GetClass(), TEXT("mNodeDataStruct")))
    {
        OutData  = NodeDataProp->ContainerPtrToValuePtr<void>(Node);
        OutScope = NodeDataProp->Struct;
    }
}

// ===========================================================================
// ★★★2026-11-09 崩溃根治（用户实测：FTimerManager::Tick → 我们 mod 的 1s 定时器回调 →
//   WT_TreeIsCorrupt / WT_FindLinkArray → N->IsValidLowLevelFast() 读 0x0000001000000120 AV）
//
// 根因：mNodes 与 WT_Blocks().Levels 里存的是**原始 UFGResearchTreeNode***。这些节点是世界内容，
//   地图重载/换图后其 UObject 被销毁，但裸指针仍留在数组里 ⇒ 悬空。当地址落在**未映射页**时，
//   连 IsValidLowLevelFast（内部读 this->InternalIndex）都会 AV —— 先前"UObject 内存走 free-list、
//   不会被 OS 回收，读 InternalIndex 不会 AV"的假设，在"分配器把整页归还 OS"时并不成立。
//
// 修法：把"解引用节点指针"整体包进 SEH。任何访问违例一律判"节点已死/不可读"，交由上层整树重建；
//   绝不让一次野指针读把整个游戏拖崩。与 WT_ProbeNodeSchematic 同款写法。
//   __try 内不得有带析构的局部对象（C2712），故本函数只做平凡操作、返回 int32。
//   返回码：1 = 活且类型正确；0 = Fast 校验不过（野指针/非活对象）；2 = 强校验不过
//   （槽位已被**别的**对象复用，即断言 IsValidLowLevel 的同一条判据）；3 = 类型不符；-1 = 访问违例。
//   ★2026-11-08 追加「强校验」：Fast 只回答「槽位里现在有没有活对象」，**不保证就是原来那个**。
//   跨世界重进档 / 资产 CDO 被引擎重初始化后，旧节点指针的槽位会被本世界新建的同类型节点复用 ⇒
//   Fast + IsA 双双通过，随后任何一次属性访问都撞 UnrealType.h:695 的 check() 直接崩（日志先打
//   "Other object in slot"）。IsValidLowLevel() 会核对「槽位里存的到底是不是 this」——正是该断言的
//   同一条判据，而它自己只返回 bool、不 assert，所以可以安全地当作探针用。
// ===========================================================================
static int32 WT_RawNodeAlive(UFGResearchTreeNode* N)
{
    __try
    {
        // 第一关：引擎级快速校验（拦野地址、错对齐、vtable 无效、Class/Outer 非法）。
        if (!N->IsValidLowLevelFast())
        {
            return 0;
        }
        // 第二关：强校验 —— 槽位归属。拦「指针已被复用成别的对象」这种 Fast 放行的假活。
        if (!N->IsValidLowLevel())
        {
            return 2;
        }
        // 第三关：类型正确（防槽位被非节点 UObject 复用）。
        if (!N->IsA(UFGResearchTreeNode::StaticClass()))
        {
            return 3;
        }
        return 1;
    }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    {
        return -1;
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
    if (!Data || !Scope)   // ★2026-11-08：节点不可强信时一律拒（详见 WT_GetNodeDataScope 注释）
    {
        return false;
    }

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
    // ★2026-11-07 重载崩溃兜底：悬空/已释放节点（如跨地图残留的原版节点实例）判活失败即拒，
    //   绝不拿它去 WT_GetNodeDataScope 解引用（否则 Node->GetClass() 遍历坏属性链 → AV 0xfff）。
    // ★2026-11-09 再收紧：直接用 SEH 探针 WT_RawNodeAlive —— 悬空指针落在未映射页时，
    //   连 IsValidLowLevelFast 本身都会读野地址 AV，必须 SEH 兜住，否则定时器路径直接崩游戏。
    if (!Node || WT_RawNodeAlive(Node) != 1)
    {
        return nullptr;
    }
    void*   Data  = nullptr;
    UStruct* Scope = nullptr;
    WT_GetNodeDataScope(Node, Data, Scope);
    OutData = Data;
    if (!Data || !Scope)   // ★2026-11-08：同上，拿不到有效作用域就当没这条链
    {
        return nullptr;
    }

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
static UFGResearchTreeNode* GWT_LinkTplSrc   = nullptr;   // 模板所在的原版节点（**仅本世界有效**）
static UClass*              GWT_LinkTplTree  = nullptr;   // 它所在的树（schema dump 用）
static bool                 GWT_LinkTplTried = false;

/**
 * ★★★2026-11-07 第二次换档崩溃根治（BuildResearchTrees 内 Assert IsValidLowLevel @ UnrealType.h:695）
 *
 * 现象：同一进程内第二次加载存档（换档/重进）时，崩在
 *   `[wt-trace] > World.BuildResearchTrees` 之后，日志先打 `LogUObjectArray: Warning: Other object in slot`
 *   再断言 `((UObject*)ContainerPtr)->IsValidLowLevel()`，且调用栈里有一串 Remote_Factory 帧。
 *
 * 根因（**上一轮 11-07 的修复不够**）：
 *   上一轮把守卫写成 `IsValidLowLevelFast() && IsA(UFGResearchTreeNode::StaticClass())`，以为能拦住悬空。
 *   但 `IsValidLowLevelFast()` 只回答「这个槽位里**现在**有没有一个活对象」，**它不保证该对象就是原来那个**。
 *   跨世界时：旧世界的原版节点被销毁 → 槽位被**本世界新建的同类型节点**（同样是 BPD_ResearchTreeNode_C）
 *   复用 → 两道检查**全部通过** → 继续用这个「已指向别人的旧指针」当模板去读 Parents 数组
 *   → 属性容器指针与对象不匹配 → 断言 IsValidLowLevel() 失败（`Other object in slot` 正是此意）。
 *   ⇒ 「判活 + 判类型」不足以识别跨世界复用，**UObject 实例一律不可跨世界缓存**（项目铁律再次应验）。
 *
 * 本版修法：**不跨世界缓存节点实例**。
 *   `WT_ResetLinkTemplateCache()` 在每次 `WT_BuildResearchTrees()`（每个世界 CONSTRUCTION 都会跑）
 *   开头强制清空缓存，于是模板永远在**当前世界**重新获取一次。
 *   建树本身每个世界只跑一次，重取成本可忽略；而 `GWT_LinkTplTree`（UClass）跨世界安全，保留不清。
 */
static void WT_ResetLinkTemplateCache()
{
    GWT_LinkTplSrc   = nullptr;   // 旧世界节点实例，世界一换即悬空，绝不能留
    GWT_LinkTplTried = false;     // 允许本世界重新获取
}

static void WT_AcquireLinkTemplate()
{
    // 缓存只在「本世界已经成功取过、且指针仍是本世界那个节点」时才复用。
    // ★注意顺序：这里**不再**用 IsValidLowLevelFast 作为唯一凭据（它对槽位复用会误判），
    //   而是依赖 WT_ResetLinkTemplateCache() 在每个世界开头强制失效 —— 二者配合才安全。
    if (GWT_LinkTplTried && GWT_LinkTplSrc)
    {
        return;
    }
    GWT_LinkTplTried = true;
    GWT_LinkTplSrc   = nullptr;   // 取失败也保持 null，走默认元素路径（不崩）

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

    // 两遍选择：
    //  ① 优先挑「真有 ChildrenAndRoads 连线」的节点 —— WT_AddChildRoad 需要它的连线形状当模板；
    //  ② 退而求其次挑「有 Parents」的节点 —— WT_AppendLinkElem 只需要成员类型匹配，空条目也能用。
    // 任一遍命中即返回；两遍都空才走默认构造路径（不崩）。
    auto NodeHasRoad = [](UFGResearchTreeNode* N) -> bool
    {
        void* ND = nullptr; UStruct* NS = nullptr;
        WT_GetNodeDataScope(N, ND, NS);
        if (!ND || !NS)   // ★2026-11-08：不可强信的节点（跨世界残留等）直接判「无连线」
        {
            return false;
        }
        if (FMapProperty* CMap = CastField<FMapProperty>(WT_FindStructMember(NS, TEXT("ChildrenAndRoads"))))
        {
            FScriptMapHelper CH(CMap, CMap->ContainerPtrToValuePtr<void>(ND));
            return CH.Num() > 0;
        }
        return false;
    };

    // 第一遍：ChildrenAndRoads 条目（连线形状模板）
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
            if (NodeHasRoad(N))
            {
                GWT_LinkTplSrc  = N;
                GWT_LinkTplTree = TreeCls;
                WT_WARNF(TEXT("[wt-link] road template acquired from vanilla node '%s' (ChildrenAndRoads[0])."), *N->GetName());
                return;
            }
        }
    }
    // 第二遍：Parents 条目兜底
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
    WT_WARNF(TEXT("[wt-link] no vanilla Parents/ChildrenAndRoads element found; writing default-constructed link elements."));
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

/** 读一个连线元素（IntVector2D 形状）的 X/Y；成功返回 true。用于去重比对。 */
static bool WT_TryReadElemCoords(UScriptStruct* S, const uint8* Elem, int32& OutX, int32& OutY)
{
    if (!S || !Elem)
    {
        return false;
    }
    bool bGotX = false, bGotY = false;
    for (TFieldIterator<FProperty> It(S); It; ++It)
    {
        FNumericProperty* NP = CastField<FNumericProperty>(*It);
        if (!NP || !NP->IsInteger())
        {
            continue;
        }
        const FString Name = It->GetName();
        const void* V = It->ContainerPtrToValuePtr<void>(const_cast<uint8*>(Elem));
        if (Name.StartsWith(TEXT("X"))) { OutX = static_cast<int32>(NP->GetSignedIntPropertyValue(V)); bGotX = true; }
        else if (Name.StartsWith(TEXT("Y"))) { OutY = static_cast<int32>(NP->GetSignedIntPropertyValue(V)); bGotY = true; }
    }
    return bGotX && bGotY;
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

    // ★★★2026-11-07 去重（连线不显示的真正原因之一）：
    //   曾出现 `Parents=2 pts=(1,0)(1,0)` —— 同一父坐标写了两条。原版 Parents 从不重复，
    //   MAM 按坐标索引连线，重复项会命中同一条而被判无效/互相覆盖 ⇒ 画不出线。
    //   这里写入前先扫已有元素：若已存在「同坐标」条目就直接返回（不再追加）。
    for (int32 i = 0; i < H.Num(); ++i)
    {
        const uint8* Ex = static_cast<const uint8*>(H.GetRawPtr(i));
        int32 ExX = 0, ExY = 0;
        if (WT_TryReadElemCoords(S, Ex, ExX, ExY) && ExX == T.X && ExY == T.Y)
        {
            return 0;   // 已有同坐标连线，跳过（幂等）
        }
    }

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
        if (!D || !Scope)   // ★2026-11-08：不可强信的节点跳过
        {
            continue;
        }
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

// ---------------------------------------------------------------------------
// ★2026-11-07 连线诊断：把「原版有连线节点」与「我们的父→子节点对」逐字段打印成文本，
//   用于对照到底哪个字段驱动连线、以及我们写的值是否正确。前缀 [wt-cmp]，一次性输出。
// ---------------------------------------------------------------------------
static UFGResearchTreeNode* WT_FindLevelNode(int32 Track, int32 Stage, int32 Level);   // 前向声明

static void WT_DumpNodeLinks(UFGResearchTreeNode* N, const FString& Tag)
{
    if (!N)
    {
        WT_WARNF(TEXT("[wt-cmp] %s = null"), *Tag);
        return;
    }
    void* D = nullptr; UStruct* Scope = nullptr;
    WT_GetNodeDataScope(N, D, Scope);
    if (!D || !Scope) { WT_WARNF(TEXT("[wt-cmp] %s = no scope"), *Tag); return; }

    auto FieldStructText = [&](const TCHAR* M, int32 Depth) -> FString
    {
        if (FProperty* P = WT_FindStructMember(Scope, M))
            if (FStructProperty* SP = CastField<FStructProperty>(P))
                return WT_StructValueText(SP->Struct, SP->ContainerPtrToValuePtr<void>(D), Depth);
        return TEXT("<none>");
    };
    auto DumpArr = [&](const TCHAR* M) -> FString
    {
        if (FArrayProperty* A = CastField<FArrayProperty>(WT_FindStructMember(Scope, M)))
        {
            FScriptArrayHelper H(A, A->ContainerPtrToValuePtr<void>(D));
            UScriptStruct* ES = CastField<FStructProperty>(A->Inner)->Struct;
            FString S = FString::Printf(TEXT("%s[%d]"), M, H.Num());
            for (int32 i = 0; i < H.Num() && i < 3; ++i)
                S += FString::Printf(TEXT("  [%d]=%s"), i, *WT_StructValueText(ES, H.GetRawPtr(i), 3));
            return S;
        }
        return FString::Printf(TEXT("%s=<missing>"), M);
    };
    auto DumpMap = [&](const TCHAR* M) -> FString
    {
        if (FMapProperty* MP = CastField<FMapProperty>(WT_FindStructMember(Scope, M)))
        {
            FScriptMapHelper H(MP, MP->ContainerPtrToValuePtr<void>(D));
            FString S = FString::Printf(TEXT("%s[%d]"), M, H.Num());
            const int32 NE = FMath::Min<int32>(H.Num(), 3);
            for (int32 e = 0; e < NE; ++e)
            {
                S += FString::Printf(TEXT("  key%d=%s"), e,
                                     *WT_StructValueText(CastField<FStructProperty>(MP->KeyProp)->Struct, H.GetKeyPtr(e), 2));
                uint8* VPtr = static_cast<uint8*>(H.GetValuePtr(e));
                S += TEXT(" pts=");
                if (FStructProperty* VP = CastField<FStructProperty>(MP->ValueProp))
                {
                    if (FArrayProperty* PtsArr = CastField<FArrayProperty>(WT_FindStructMember(VP->Struct, TEXT("Points"))))
                    {
                        FScriptArrayHelper PA(PtsArr, PtsArr->ContainerPtrToValuePtr<void>(VPtr));
                        S += FString::Printf(TEXT("array[%d]"), PA.Num());
                        UScriptStruct* PES = CastField<FStructProperty>(PtsArr->Inner)->Struct;
                        for (int32 i = 0; i < PA.Num() && i < 6; ++i)
                        {
                            int32 px = 0, py = 0;
                            if (WT_TryReadElemCoords(PES, static_cast<const uint8*>(PA.GetRawPtr(i)), px, py))
                                S += FString::Printf(TEXT(" p%d=(%d,%d)"), i, px, py);
                        }
                    }
                }
            }
            return S;
        }
        return FString::Printf(TEXT("%s=<missing>"), M);
    };

    WT_WARNF(TEXT("[wt-cmp] %s  Coords=%s"), *Tag, *FieldStructText(TEXT("Coordinates"), 3));
    WT_WARNF(TEXT("[wt-cmp]   %s"), *DumpArr(TEXT("Parents")));
    WT_WARNF(TEXT("[wt-cmp]   %s"), *DumpMap(TEXT("ChildrenAndRoads")));
    WT_WARNF(TEXT("[wt-cmp]   %s"), *DumpArr(TEXT("UnhiddenBy")));
    WT_WARNF(TEXT("[wt-cmp]   %s"), *DumpArr(TEXT("NodesToUnhide")));
}

static void WT_DumpConnectionComparisonOnce()
{
    static bool bDone = false;
    if (bDone) return;
    bDone = true;

    // 原版：找一个真有 Parents 的节点（即原版里会画连线的节点），看它 Parents 元素长啥样
    if (GWT_LinkTplTree)
    {
        TArray<UFGResearchTreeNode*> VNodes = UFGResearchTree::GetNodes(GWT_LinkTplTree);
        for (UFGResearchTreeNode* VN : VNodes)
        {
            void* VD = nullptr; UStruct* VS = nullptr;
            WT_GetNodeDataScope(VN, VD, VS);
            if (!VD || !VS)   // ★2026-11-08：不可强信的节点跳过
            {
                continue;
            }
            if (FArrayProperty* A = CastField<FArrayProperty>(WT_FindStructMember(VS, TEXT("Parents"))))
            {
                FScriptArrayHelper H(A, A->ContainerPtrToValuePtr<void>(VD));
                if (H.Num() > 0) { WT_DumpNodeLinks(VN, TEXT("VANILLA(hasParents)")); break; }
            }
        }
    }
    // 我们的：阶段1 LV4（父）→ 阶段2 LV1（子），抽一条轨道对照
    if (UFGResearchTreeNode* From = WT_FindLevelNode(0, 1, 4))
        WT_DumpNodeLinks(From, TEXT("OUR-t0-S1LV4(parent)"));
    if (UFGResearchTreeNode* To = WT_FindLevelNode(0, 2, 1))
        WT_DumpNodeLinks(To, TEXT("OUR-t0-S2LV1(child)"));
}

/**
 * ★「同一个方块买 4 次」的核心：A 研究完成后揭示 B。
 *   A.NodesToUnhide += B 的坐标；B.UnhiddenBy += A 的坐标。两个方向都写，MAM 读哪个都不会空。
 *
 * ★★★调用约束（2026-11-05 血泪）：**必须等 To 的坐标已经写完再调用**。
 *   元素是 IntVector2D 格点，`WT_MakeLink(To)` 是"立刻读当前坐标"的快照语义；
 *   在 WT_WriteNodeData(To, ...) 之前调用会拿到默认 (0,0)，实测日志：
 *     `[wt-nd] WT[0] ... Coords=(1,0) ... NodesToUnhide=1 pts=(0,0)`
 *   而图是按坐标索引的 ⇒ 等于凭空遮住 (0,0) 那个节点。
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
// ★2026-11-07 **MAM 连线**：原版画线读**父节点**的 `ChildrenAndRoads`
// （Map<子坐标 IntVector2D, MAMTree_RoadPoints{ Points:Array<坐标> }>）。只写 Parents 不够
// （Parents 只表从属/前置），必须给父节点写 ChildrenAndRoads 才会出线。
// 做法：用「活模板」整体拷贝原版节点的 ChildrenAndRoads 条目（key+value），再把 key 坐标与
// value 的 Points[0] 坐标覆写成子节点坐标 —— 与原版直线段同形。模板取自 GWT_LinkTplSrc
// （dump 实锤其 ChildrenAndRoads=1，如 AOrganisms_3）。
// ---------------------------------------------------------------------------

/** 在结构体（含数组）里深度优先找第一个「坐标结构」(IntVector2D/FVector2D) 并写入 X/Y，命中即止。 */
static bool WT_SetFirstCoordInStruct(UScriptStruct* S, uint8* Elem, const FWTLinkTarget& T)
{
    for (TFieldIterator<FProperty> It(S); It; ++It)
    {
        FProperty* P = *It;
        void*      D = P->ContainerPtrToValuePtr<void>(Elem);
        if (FStructProperty* SP = CastField<FStructProperty>(P))
        {
            if (WT_WriteLinkIdentity(SP->Struct, static_cast<uint8*>(D), nullptr, T, 0) > 0)
            {
                return true;
            }
            if (WT_SetFirstCoordInStruct(SP->Struct, static_cast<uint8*>(D), T))
            {
                return true;
            }
        }
        else if (FArrayProperty* AP = CastField<FArrayProperty>(P))
        {
            if (FStructProperty* IStr = CastField<FStructProperty>(AP->Inner))
            {
                FScriptArrayHelper AH(AP, D);
                if (AH.Num() > 0)
                {
                    uint8* E0 = static_cast<uint8*>(AH.GetRawPtr(0));
                    if (WT_WriteLinkIdentity(IStr->Struct, E0, nullptr, T, 0) > 0)
                    {
                        return true;
                    }
                    if (WT_SetFirstCoordInStruct(IStr->Struct, E0, T))
                    {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

/** 给父节点写一条 ChildrenAndRoads 入口（key=子坐标，value=路径点[子坐标]），让 MAM 画出连线。 */
static void WT_AddChildRoad(UFGResearchTreeNode* Parent, const FWTLinkTarget& Child)
{
    if (!Parent || !Child.Node)
    {
        return;
    }
    void*   D     = nullptr;
    UStruct* Scope = nullptr;
    WT_GetNodeDataScope(Parent, D, Scope);
    if (!D || !Scope)   // ★2026-11-08：父节点不可强信（跨世界残留等）→ 不写连线，避免碰属性撞断言
    {
        return;
    }
    FMapProperty* MP = CastField<FMapProperty>(WT_FindStructMember(Scope, TEXT("ChildrenAndRoads")));
    if (!MP)
    {
        return;
    }
    UScriptStruct* KeyStruct = CastField<FStructProperty>(MP->KeyProp)->Struct;
    UScriptStruct* ValStruct = CastField<FStructProperty>(MP->ValueProp)->Struct;

    // 确保本世界模板已就绪（幂等，重复使用安全）
    WT_AcquireLinkTemplate();

    // 模板：原版节点 ChildrenAndRoads[0] 的 key+value（整体拷贝，避免猜 Points 元素类型）
    uint8* KeyProto = nullptr;
    uint8* ValProto = nullptr;
    if (GWT_LinkTplSrc)
    {
        void* TD = nullptr; UStruct* TS = nullptr;
        WT_GetNodeDataScope(GWT_LinkTplSrc, TD, TS);
        if (TD && TS)   // ★2026-11-08：模板源跨世界残留时 TS 为空，直接跳过取模板
        {
            if (FMapProperty* TMP = CastField<FMapProperty>(WT_FindStructMember(TS, TEXT("ChildrenAndRoads"))))
            {
                FScriptMapHelper TH(TMP, TMP->ContainerPtrToValuePtr<void>(TD));
                if (TH.Num() > 0)
                {
                    KeyProto = static_cast<uint8*>(TH.GetKeyPtr(0));
                    ValProto = static_cast<uint8*>(TH.GetValuePtr(0));
                }
            }
        }
    }

    FScriptMapHelper H(MP, MP->ContainerPtrToValuePtr<void>(D));
    // ★ 正确 API：FScriptMapHelper::AddDefaultValue_Invalid_NeedsRehash() 返回新条目索引 int32；
    //   加完条目后必须 Rehash() 才成有效 map（见 UnrealType.h 注释）。
    const int32 Idx = H.AddDefaultValue_Invalid_NeedsRehash();
    uint8* K = static_cast<uint8*>(H.GetKeyPtr(Idx));
    uint8* V = static_cast<uint8*>(H.GetValuePtr(Idx));
    if (KeyProto) KeyStruct->CopyScriptStruct(K, KeyProto); else KeyStruct->InitializeStruct(K);
    if (ValProto) ValStruct->CopyScriptStruct(V, ValProto); else ValStruct->InitializeStruct(V);

    // 覆写 key = 子坐标（直接写顶层 X/Y）
    WT_WriteLinkIdentity(KeyStruct, K, KeyProto, Child, 0);

    // 父节点坐标（构造正交拐点用）。MAM 图区按格点索引，连线从父节点位置画向 key。
    int32 pcx = 0, pcy = 0;
    const bool bHaveParent = WT_ReadNodeCoord(Parent, pcx, pcy);
    const int32 ccx = Child.X, ccy = Child.Y;
    // ★★2026-11-08 实测更正（推翻旧假设）：MAM **不读 Points 画线**——它对父节点 ChildrenAndRoads 的
    //   每个 key 只画一条「父坐标 → key 坐标」直连直线（原版 pts 仅 1 个终点也印证），所以本函数写入的
    //   Points 路点对视觉**无作用**。「三轨下垂→水平公共干线→两处下落」的干线形状引擎画不出（除非在
    //   中间行摆真实方块）。下面仍按「竖直→水平→竖直」写路点（保留数据完整性、无副作用）；
    //   同列/相邻行自动退化为两点。
    const int32 MidY = pcy + (ccy > pcy ? 1 : (ccy < pcy ? -1 : 0));

    // ★ 覆写 value 的 Points 数组：清空模板原版的多余路点（弯/斜连线），
    //   只留我们给定的路点，否则会把线画到原版旧路点（屏外）位置。
    if (FArrayProperty* PtsArr = CastField<FArrayProperty>(WT_FindStructMember(ValStruct, TEXT("Points"))))
    {
        FScriptArrayHelper PA(PtsArr, PtsArr->ContainerPtrToValuePtr<void>(V));
        PA.EmptyValues();
        UScriptStruct* PES = CastField<FStructProperty>(PtsArr->Inner)->Struct;
        if (bHaveParent && MidY != pcy && MidY != ccy)
        {
            // 三段折线（跨行的边走这条：末段汇聚线）
            // ① 父列竖直下落到干线行 —— 三轨各自下垂的那一小段
            const int32 Pi1 = PA.AddValue();
            const FWTLinkTarget E1{ nullptr, pcx, MidY };
            WT_WriteLinkIdentity(PES, PA.GetRawPtr(Pi1), nullptr, E1, 0);
            // ② 干线行水平横移到子列 —— 所有兄弟边在这一行重叠成公共干线
            const int32 Pi2 = PA.AddValue();
            const FWTLinkTarget E2{ nullptr, ccx, MidY };
            WT_WriteLinkIdentity(PES, PA.GetRawPtr(Pi2), nullptr, E2, 0);
            // ③ 子列竖直下落到末段方块
            const int32 Pi3 = PA.AddValue();
            WT_WriteLinkIdentity(PES, PA.GetRawPtr(Pi3), nullptr, Child, 0);
        }
        else
        {
            // 相邻行/同列/同排：两点折线（先横后竖）即可，或直接落到子坐标
            const int32 Pi1 = PA.AddValue();
            const FWTLinkTarget Elbow1{ nullptr, ccx, pcy };
            WT_WriteLinkIdentity(PES, PA.GetRawPtr(Pi1), nullptr, Elbow1, 0);
            const int32 Pi2 = PA.AddValue();
            WT_WriteLinkIdentity(PES, PA.GetRawPtr(Pi2), nullptr, Child, 0);
        }
    }
    else
    {
        WT_SetFirstCoordInStruct(ValStruct, V, Child);
    }

    H.Rehash();   // 关键：AddDefaultValue_Invalid_NeedsRehash 后必须 Rehash 才生成有效 map
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
    if (!Data || !Scope)   // ★2026-11-08：新建节点理论上一定通过；不通过就只保留上面的 C++ 字段
    {
        return;
    }

    // ① Schematic：原版 GetNodeSchematic() 的返回值来源
    if (FClassProperty* SchematicProp = CastField<FClassProperty>(WT_FindStructMember(Scope, TEXT("Schematic"))))
    {
        SchematicProp->SetPropertyValue_InContainer(Data, SchematicClass);
    }

    // ② Coordinates（IntVector2D）：MAM 图区的网格坐标
    //    ★★★2026-11-05 定案（两次试错后的唯一正确解）：**一个方块 = 一个坐标**。
    //      · 试错 A（旧）：「4 级共用同一坐标 + 靠 UnhiddenBy 逐级揭示」⇒ 同坐标互相遮盖，
    //        且被盖者的 UnhiddenBy 指向自己 ⇒ 自环 ⇒ 永远不显形（用户截图「14 个空方块」）。
    //      · 试错 B（废案，用户明确否决）：「4 级拆成 4 个相邻坐标」⇒ 图被撑到 X 0..13，
    //        远超原版包络（X 0..6），用户实测「只能研究云端的，其他的根本点不了」。
    //      · 正解：4 级**登记**在同一个坐标，但任何时刻 `mNodes` 里只放「当前那一级」，
    //        买完一级由 `WT_AdvanceResearchBlocks` 把该坐标换成下一级 ⇒ 图上永远 14 个方块。
    //    所以本函数拿到的 CoordX / CoordY 就是该方块**唯一**的坐标，不再做任何偏移。
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
// ★★★2026-11-05 **一个方块 = 一个坐标 = 连买 4 次**（用户定稿的初衷，本轮终于落地）
//
// 用户设计原话：「节点 1 买完 LV1 后，LV2 **覆盖** LV1，同时保留 LV1 的研究效果。」
//
// 为什么必须"覆盖"而不是"并存"（本轮一手实证）：
//   `Parents` / `UnhiddenBy` / `NodesToUnhide` 的元素类型是 **IntVector2D（整数格点）**，
//   `ChildrenAndRoads` 的键也是格点 ⇒ **整张 MAM 图按坐标索引**。
//   原版 `[wt-vtree] coord audit: 11 nodes / 11 unique coords / 0 dup groups` —— 从不重复。
//   所以同一坐标上并存两个节点，在游戏眼里就是**同一个点**：后写入的覆盖前者，
//   且被覆盖者的 `UnhiddenBy` 指向自己那个坐标 ⇒ 自环 ⇒ 永远不显形。
//   （这就是之前「14 个空方块、点不动」的真正机理。）
//
// 本结构就是为此而生：一个方块登记 4 级，`mNodes` 里**永远只放 Current 那一个**；
// 买完一级就把该坐标的节点换成下一级 —— 图形上是同一个方块被研究了 4 次，
// 数据上任何时刻该坐标只有 1 个节点。
// 等级效果的"保留"不需要额外处理：升级数值取的是「该轨最高已购级」，Lv1 记录一直在存档里。
// ---------------------------------------------------------------------------
struct FWTResearchBlock
{
    int32 CoordX    = 0;
    int32 CoordY    = 0;
    int32 Track     = -1;                       // -1 = 末段
    bool  bFinal    = false;
    int32 BaseLevel = 1;                        // 本块第一级的等级号（1/5/9/13/17）
    EWTFinalRoute Route = EWTFinalRoute::None;
    TArray<UFGResearchTreeNode*> Levels;        // 4 个节点，全部在同一坐标
    TArray<UClass*>              Schematics;    // 与 Levels 一一对应
    int32 Current   = 0;                        // 当前占用该坐标的级别下标
};

static TArray<FWTResearchBlock>& WT_Blocks()
{
    static TArray<FWTResearchBlock> Blocks;
    return Blocks;
}

/**
 * ★2026-11-07（恢复揭示链配套）：按「轨道 + 等级」在 WT_Blocks() 里定位节点实例。
 *   · Track ∈ 0..2，Level ∈ 1..16；
 *   · 只有 `!bFinal` 的块参与（末段块 Track=-1，用 Route 区分，不走这里）；
 *   · 越界/未建树时返回 nullptr，调用方须判空（绝不裸解引用）。
 * 用途：ConfigureMergedTree 建完 14 个块后，按 Stage 边界把
 *   「上一阶段方块的 Lv4」→「下一阶段方块的 Lv1」串成揭示链。
 */
static UFGResearchTreeNode* WT_FindLevelNode(int32 Track, int32 Stage, int32 Level)
{
    if (Track < 0 || Track > 2 || Stage < 1 || Stage > 4)
    {
        return nullptr;
    }
    for (const FWTResearchBlock& B : WT_Blocks())
    {
        if (B.bFinal || B.Track != Track || B.BaseLevel != (Stage - 1) * 4 + 1)
        {
            continue;
        }
        // ★2026-11-07 修正（实测「只隐藏 LV5、LV9/LV13/末段没隐藏」根因）：
        //   传入的 Level 是**块内等级 1..4**（调用处一律传 1 或 4），而 B.BaseLevel 是**全局首级**
        //   （如 Stage2 块 = 5）。用 `Level - B.BaseLevel` 会得到负数（4-5=-1）⇒ IsValidIndex 失败
        //   ⇒ 只有「取 Stage1 块 Lv4」(4-1=3) 能命中，其余全部返回 nullptr ⇒ 揭示链漏写。
        //   正确应为块内下标 `Level - 1`（Lv1→0 … Lv4→3）。
        if (Level < 1 || Level > 4)
        {
            return nullptr;
        }
        const int32 Idx = Level - 1;   // 块内下标 0..3
        if (B.Levels.IsValidIndex(Idx))
        {
            return B.Levels[Idx];
        }
        return nullptr;
    }
    return nullptr;
}

/**
 * 依据「已购状态」推进每个方块当前显示的那一级。
 * 买完 Lv1 ⇒ 该坐标的节点换成 Lv2；买完 Lv2 ⇒ 换成 Lv3 … 直到 Lv4 停在最后一级。
 * 只在真的发生变化时才重写树 CDO 的 `mNodes`（避免每帧无谓重建）。
 */
// ---------------------------------------------------------------------------
// ★2026-11-06 崩溃根治：强存活校验 + 损坏即重建。
//
// 旧版用 `IsValid(N)` 过滤悬空节点，但 `IsValid` 读的是对象标志位（位于对象内存里）；
// 当节点 UObject 被释放、其内存又被复用给别的对象时，那段内存上的"新对象"标志位是正常的，
// `IsValid` 会误判成"有效"并放行 ⇒ 悬空指针写回 mNodes ⇒ 开 MAM 遍历 GetNodeSchematic 时
// 解引用已损坏的 vtable ⇒ EXCEPTION_ACCESS_VIOLATION。
//
// 正确做法是 `IsValidLowLevelFast()`：它不读对象内存里的标志，而是用对象内部的 InternalIndex
// 反查全局 UObject 表来确认"这个地址现在到底是不是一个活着的、类型为 research node 的对象"。
// 释放后再分配的内存上 InternalIndex 属于别的对象（或已失效），反查必然失败 ⇒ 准确识别悬空指针，
// 且整个判断过程**不触碰 vtable**，不会在判读阶段就崩。
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ★2026-11-06 安全探测（崩溃根治第二道闸门）
//
// 现象：合并树节点在 `IsTreeFullyResearched` 遍历里调 `GetNodeSchematic` 时读坏地址
// 0x1e30b0328 崩溃。节点对象本身"活着"（IsValidLowLevelFast 通过，WT_TreeIsCorrupt 漏判），
// 但 mNodeDataStruct 内部某指针字段已被写坏。普通存活校验拦不住这种"活着但字段损坏"的节点，
// 而直接读损坏字段又会立刻 AV —— 所以这里用 SEH 把 GetNodeSchematic 包起来：能正常返回的就
// 是好的；一旦 AV 就说明该节点数据已损坏，交由上层整树重建，绝不把坏节点交给原版遍历。
//
// ★SEH 约束：含 __try 的函数里不能有带析构的局部对象（否则 C2712）。故 GetNodeSchematic
// 的返回值用独立函数 WT_RawSchematic 取原始 UClass*，__try 块内只留平凡类型。
// ---------------------------------------------------------------------------
static bool WT_IsNodeAlive(UFGResearchTreeNode* N);   // 前向声明（下方才定义）

static UClass* WT_RawSchematic(UFGResearchTreeNode* Node)
{
    return Node ? Node->GetNodeSchematic().Get() : nullptr;
}

static bool WT_ProbeNodeSchematic(UFGResearchTreeNode* Node)
{
    // ★2026-11-08：GetNodeSchematic() 是虚调用，对「槽位已被复用」的旧指针会读到别人的 vtable。
    //   先用强判活挡掉，再进 SEH 探测（SEH 只能拦 AV，拦不住引擎里的 check()）。
    if (!Node || WT_RawNodeAlive(Node) != 1)
    {
        return false;
    }
    UClass* S   = nullptr;
    bool    bAV = false;
    __try
    {
        S = WT_RawSchematic(Node);
    }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    {
        bAV = true;
    }
    if (bAV)
    {
        return false;
    }
    return (S != nullptr);
}

// ★★★2026-11-07 MAM 连线根治：树身份改用**烘焙好的蓝图资产类** BP_WT_ResearchTree_C。
//
// 实证链（全部一手，无猜测）：
//   ① 我们的节点数据（Coordinates/Parents/ChildrenAndRoads/Points）经 [wt-cmp] 与原版逐字段对照
//      **完全一致**，且去重已生效 ⇒ 数据层无问题。
//   ② 但连线始终不画。代码历史（v0.2.41/0.2.44）实锤：**纯 C++ 树类 UWTResearchTreeMerged
//      时代就是「节点可见、没有连线」**。
//   ③ 原版 10 棵树**全部**是蓝图资产类 BPD_ResearchTree_*_C；MAM 图区按「资产树」处理
//      （UPackage outer / 资产身份 / 蓝图类 Cast）。纯 C++ 类的 outer 是代码模块包，
//      UFGResearchTree::GetPrimaryAssetId() 由 GetOutermost() 推导 ⇒ 拿不到可加载包身份。
//      （先试过 override 返回假 PrimaryAssetId，实测仍无线 ⇒ 门禁不止是那个 id，直接上资产类。）
//   ④ 资产其实**早就烘焙好了**：Mods/Smart_Factory/Content/Research/BP_WT_ResearchTree.uasset
//      （由 WTMakeTree commandlet 生成，父类 WTResearchTreeMerged，内含全部 56 个节点与
//       mNodeDataStruct 结构），只是注册从未指向它。
//
// ★v0.2.54 那次的失败原因（本次必须避开）：当时只把注册换成资产类，但节点仍写在
//   **C++ CDO** 上；资产类 CDO 的 mNodes 用的是烘焙瞬间的节点（运行时并不存在/未重建），
//   于是「依旧未显示节点」。本版修法 = **注册用资产类，且运行时把节点写进资产类 CDO**，
//   两者对齐。写不进去（资产未 cook 进包）才回退 C++ 类，保证最差等于旧行为、绝不更差。
static UClass* GWTTreeClass = nullptr;
static bool    GWTTreeClassResolved = false;

/** 当前生效的研究树 UClass：优先蓝图资产类，缺失则回退纯 C++ 类。 */
UClass* WT_GetTreeClass()
{
    if (GWTTreeClassResolved)
    {
        return GWTTreeClass;
    }
    GWTTreeClassResolved = true;

    // ★挂载根 = `/Smart_Factory/`（**不是** mod_reference "SmartFactory"）。
    //   一手依据：Remote_FactoryModule.cpp 第 42–48 行实测结论 —— cook 时 PackageStore 固化的
    //   包名就是 `/Smart_Factory/...`，LoadObject 直达 cooked 资产；IoStore 容器由引擎扫描
    //   Mods/<目录>/Content/Paks/ 挂载，与目录名无关。全项目其它资产全部用该根，保持一致。
    //   旧根兜底（改名历史：Remote_Factory / WirelessTerminal）。
    static const TCHAR* kTreeAssetPaths[] = {
        TEXT("/Smart_Factory/Research/BP_WT_ResearchTree.BP_WT_ResearchTree_C"),
        TEXT("/Game/Smart_Factory/Research/BP_WT_ResearchTree.BP_WT_ResearchTree_C"),
        TEXT("/Remote_Factory/Research/BP_WT_ResearchTree.BP_WT_ResearchTree_C"),
        TEXT("/Game/Remote_Factory/Research/BP_WT_ResearchTree.BP_WT_ResearchTree_C"),
        TEXT("/WirelessTerminal/Research/BP_WT_ResearchTree.BP_WT_ResearchTree_C"),
        TEXT("/Game/WirelessTerminal/Research/BP_WT_ResearchTree.BP_WT_ResearchTree_C"),
    };
    for (const TCHAR* Path : kTreeAssetPaths)
    {
        if (UClass* AssetCls = LoadClass<UFGResearchTree>(nullptr, Path))
        {
            AssetCls->AddToRoot();   // 静态裸 UClass* 不是 GC 引用，必须钉住
            GWTTreeClass = AssetCls;
            WT_WARNF(TEXT("[wt-tree] using BAKED ASSET tree class '%s' (roads need an asset-identity tree)."),
                     *AssetCls->GetName());
            return GWTTreeClass;
        }
    }
    GWTTreeClass = UWTResearchTreeMerged::StaticClass();
    WT_WARNF(TEXT("[wt-tree] baked tree asset not found; falling back to native UWTResearchTreeMerged "
                  "(nodes ok, roads likely missing)."));
    return GWTTreeClass;
}

// ★2026-11-07：树身份改为资产类后，凡「判断是不是我们这棵树」的地方都必须接受两套：
//   资产类（当前生效）+ C++ 类（回退/历史存档路径）。统一走该判定，避免各点各写一套漏判。
bool WT_IsOurResearchTree(UClass* TreeClass)
{
    if (!TreeClass)
    {
        return false;
    }
    if (TreeClass == UWTResearchTreeMerged::StaticClass() || TreeClass == WT_GetTreeClass())
    {
        return true;
    }
    // 资产类在本模块内的父类链依然指向 UWTResearchTreeMerged；上面的精确比对已覆盖，
    // 这里再补一条「子类也算」的兜底（例如将来另烘焙一版资产）。
    return TreeClass->IsChildOf(UWTResearchTreeMerged::StaticClass());
}

/** 整树 15 节点是否全部"存活且 GetNodeSchematic 可读"。任一节点损坏即返回 false（触发重建）。 */
static bool WT_MergedTreeNodesSafe()
{
    TArray<UFGResearchTreeNode*> Nodes = UFGResearchTree::GetNodes(WT_GetTreeClass());
    if (Nodes.Num() == 0)
    {
        return false;
    }
    for (UFGResearchTreeNode* N : Nodes)
    {
        // ★2026-11-08：这里改用探针返回码，失败时把「到底哪一关没过」打进日志
        //   （0=野指针/Fast 不过，2=槽位已被别的对象复用【断言同款判据】，3=类型不符，-1=访问违例）。
        const int32 Alive = WT_RawNodeAlive(N);
        if (Alive != 1)
        {
            WT_WARNF(TEXT("[wt-block] merged tree node ptr %p failed alive-check (code=%d; "
                          "0=fast-invalid 2=slot-reused 3=wrong-class -1=access-violation) -> rebuild needed."),
                     N, Alive);
            return false;
        }
        if (!WT_ProbeNodeSchematic(N))
        {
            // 坏节点诊断：节点对象本身仍存活（内存有效），故安全读取其 mNodeDataStruct 原始
            // 指针/坐标（只读 8 字节，不对其解引用），定位到底是哪个字段坏成野指针。
            void*    D     = nullptr;
            UStruct* Scope = nullptr;
            WT_GetNodeDataScope(N, D, Scope);
            FString Diag;
            if (D && Scope)
            {
                if (FClassProperty* SP = CastField<FClassProperty>(WT_FindStructMember(Scope, TEXT("Schematic"))))
                {
                    UClass* S = *reinterpret_cast<UClass* const*>(SP->ContainerPtrToValuePtr<void>(D));
                    Diag += FString::Printf(TEXT(" SchematicRaw=%p"), S);
                }
                if (FStructProperty* CP = CastField<FStructProperty>(WT_FindStructMember(Scope, TEXT("Coordinates"))))
                {
                    void* CD = CP->ContainerPtrToValuePtr<void>(D);
                    int32 X = -1, Y = -1;
                    if (FIntProperty* XP = CastField<FIntProperty>(WT_FindStructMember(CP->Struct, TEXT("X")))) X = *XP->ContainerPtrToValuePtr<int32>(CD);
                    if (FIntProperty* YP = CastField<FIntProperty>(WT_FindStructMember(CP->Struct, TEXT("Y")))) Y = *YP->ContainerPtrToValuePtr<int32>(CD);
                    Diag += FString::Printf(TEXT(" Coord=(%d,%d)"), X, Y);
                }
            }
            WT_WARNF(TEXT("[wt-block] merged tree node '%s' failed safe-probe (dead or mNodeDataStruct corrupt)%s -> rebuild needed."),
                     N ? *N->GetName() : TEXT("null"), *Diag);
            return false;
        }
    }
    return true;
}

static bool WT_IsNodeAlive(UFGResearchTreeNode* N)
{
    if (!N)
    {
        return false;
    }
    // ★2026-11-09：判活整体交给 SEH 探针 WT_RawNodeAlive。
    //   旧实现直接 N->IsValidLowLevelFast()，当 N 是"落在未映射页的悬空指针"时会 AV
    //   （用户实测 0x0000001000000120 崩溃）。现在 -1(访问违例)/0(非活对象) 一律判死。
    return WT_RawNodeAlive(N) == 1;
}

/** 合并树 mNodes 是否已损坏（含空树）。用强校验，不靠 IsValid。 */
static bool WT_TreeIsCorrupt()
{
    UWTResearchTreeBase* CDO = Cast<UWTResearchTreeBase>(WT_GetTreeClass()->GetDefaultObject());
    if (!CDO)
    {
        return false;
    }
    TArray<UFGResearchTreeNode*> Nodes = UFGResearchTree::GetNodes(WT_GetTreeClass());
    if (Nodes.Num() == 0)
    {
        return true;
    }
    for (UFGResearchTreeNode* N : Nodes)
    {
        // ★2026-11-08：失败时打出探针返回码，定位是「野指针」还是「槽位被复用」。
        const int32 Alive = WT_RawNodeAlive(N);
        if (Alive != 1)
        {
            WT_WARNF(TEXT("[wt-corrupt] tree node ptr %p rejected (code=%d; "
                          "0=fast-invalid 2=slot-reused 3=wrong-class -1=access-violation)."),
                     N, Alive);
            return true;
        }
    }
    return false;
}

/** 整树重建（节点 + 解锁依赖），并把 WT_Blocks 复位。推进等级由调用方随后重跑。 */
// 前向声明：节点集构建（定义见 WT_AdvanceResearchBlocks 之前）。
// ★可见性由揭示链（UnhiddenBy/NodesToUnhide）驱动，mNodes 永远含全部方块，故不再需要 WT_IsBlockUnlocked。
static TArray<UFGResearchTreeNode*> WT_BuildLiveNodes();

static void WT_RebuildMergedTree(UWorld* World)
{
    UWTResearchTreeBase* CDO = Cast<UWTResearchTreeBase>(WT_GetTreeClass()->GetDefaultObject());
    if (!CDO)
    {
        return;
    }
    WT_WARNF(TEXT("[wt-repair] rebuilding merged MAM tree (corrupt/empty nodes detected)."));
    CDO->ConfigureMergedTree(
        WT_LOCTEXT("Tree_Remote_Factory", "智能工厂"),
        WT_LOCTEXT("TreeDesc_Remote_Factory",
            "Research the Smart Factory's upgrade buildings level by level in the MAM. "
            "Each level costs more than the last; the matching building scales automatically."));
    // 注意：ConfigureMergedTree 内部已 WT_Blocks().Reset() 并用全新节点重建了 WT_Blocks，
    // 下面的推进循环会基于"已购状态"把每个方块恢复到正确等级。
}

/** 强校验 + 损坏即重建（对外，供 IsTreeFullyResearched 钩子调用）。 */
void WT_ValidateAndRepairTree(UWorld* World)
{
    // 第一道闸门：廉价存活校验（悬空/已释放节点，IsValidLowLevelFast 拦得住）。
    bool bCorrupt = WT_TreeIsCorrupt();
    // 第二道闸门：节点都"活着"但仍可能存在"mNodeDataStruct 字段损坏"的节点
    // （活着但字段野指针，普通校验拦不住，调 GetNodeSchematic 直接 AV）。
    // 用 SEH 安全探测，命中即视为需重建。
    bool bUnsafe = !bCorrupt && !WT_MergedTreeNodesSafe();

    if (bCorrupt || bUnsafe)
    {
        WT_WARNF(TEXT("[wt-repair] merged tree %s -> rebuilding to clear bad node."),
                 bCorrupt ? TEXT("corrupt/dead nodes detected") : TEXT("passed alive-check but SEH probe failed"));
        WT_RebuildMergedTree(World);
        // 重建后立刻按已购状态推进到正确等级（不递归进本函数，避免重复自检开销）。
        const AFGSchematicManager* SM = World ? AFGSchematicManager::Get(World) : nullptr;
        if (SM)
        {
            for (FWTResearchBlock& B : WT_Blocks())
            {
                while (B.Current + 1 < B.Levels.Num())
                {
                    UClass* Cur = B.Schematics.IsValidIndex(B.Current) ? B.Schematics[B.Current] : nullptr;
                    if (!Cur || !SM->IsSchematicPurchased(Cur))
                    {
                        break;
                    }
                    ++B.Current;
                }
            }
            TArray<UFGResearchTreeNode*> NewNodes = WT_BuildLiveNodes();
            UFGResearchTree::SetNodes(WT_GetTreeClass(), NewNodes);
            if (UWTResearchTreeBase* MCDO = Cast<UWTResearchTreeBase>(WT_GetTreeClass()->GetDefaultObject()))
            {
                MCDO->EnsureUnlockDependencies();
            }
            WT_WARNF(TEXT("[wt-repair] tree rebuilt + advanced -> %d live nodes."), NewNodes.Num());
        }
    }

    // ★2026-11-09 回归修复（用户实测：开 MAM 研究树显示空白 / "研究树没了"）：
    //   引擎在注册后会重初始化树 CDO，清空 mUnlockDependencies；原版 BPW_MAMTree_ListButtonInfo
    //   SetContent 第一行就 GetUnlockDependencies()[0]，数组空 ⇒ 蓝图抛越界、后续绑定全中断 ⇒
    //   方块只剩空壳、整棵看起来"没了"。
    //   旧逻辑只在节点损坏时才武装依赖；节点活着、依赖却被清空时漏掉了 ⇒ 依赖持续为空。
    //   依赖非空与否必须和节点存活解耦：无论节点是否损坏，只要依赖为空就重新武装。
    //   EnsureUnlockDependencies 自身幂等（非空即返回），每次调用零额外开销；本函数随
    // ★2026-11-10 二次修复（用户实测：MAM 树只剩 LV1、其余全消失）：
    //   第一版只在「树损坏/不安全」才重算 mNodes，健康树 mNodes 永远是 ConfigureMergedTree 用
    //   （可能为空）的 SchematicMgr 算出的结果 ⇒ 空管理器下只有 Stage1 进图、且永不再刷新。
    //   本段已改为**无条件**按真实已购状态重算 mNodes；但真正让「后续阶段 / 末段」出现的根因在
    //   WT_BuildLiveNodes：此前它用 WT_IsBlockUnlocked 预先过滤，导致未解锁的方块根本不进 mNodes，
    //   MAM 无从显示 ⇒ 整棵树只剩 Stage1 那一排。
    //   正确做法（与原版 MAM 一致）：mNodes 永远包含**全部 14 个方块**的当前级节点，可见性完全交给
    //   揭示链（UnhiddenBy / NodesToUnhide，ConfigureMergedTree 已按节点级写好）——根块初始可见，
    //   后续阶段须前置块购满 LV4 才由揭示链显形，末段须任一轨 Lv16。这同时保留「未购前置不露面」的约束，
    //   且不会复发 2026-11-09 前「坐标级揭示误判提前露面」的问题（揭示链已是节点级）。
    if (const AFGSchematicManager* SM = World ? AFGSchematicManager::Get(World) : nullptr)
    {
        TArray<UFGResearchTreeNode*> NewNodes = WT_BuildLiveNodes();
        UFGResearchTree::SetNodes(WT_GetTreeClass(), NewNodes);
    }

    //   IsTreeFullyResearched 钩子在「MAM 每次读树」时调用，故能卡在渲染前把依赖补回。
    if (UWTResearchTreeBase* CDO = Cast<UWTResearchTreeBase>(WT_GetTreeClass()->GetDefaultObject()))
    {
        CDO->EnsureUnlockDependencies();
    }
}

// ---------------------------------------------------------------------------
// ★2026-11-10 可见性架构定案（对照用户实测「只剩一排」）：
//   可见性**完全由揭示链驱动**（UnhiddenBy / NodesToUnhide，ConfigureMergedTree 已按节点级写好），
//   与原版 MAM 一致——mNodes 包含全部 14 个方块，根块（UnhiddenBy 空）初始可见，后续阶段须前置块
//   购满 LV4 才显形，末段须任一轨 Lv16。
//   因此 WT_BuildLiveNodes 不再做任何「解锁过滤」（旧版 WT_IsBlockUnlocked 过滤把未解锁方块挡在
//   mNodes 之外，正是「只剩 Stage1 一排」的根因），只做存活校验后把每个方块的当前级节点加入 mNodes。
//   2026-11-09 曾把可见性改成 mNodes 成员资格驱动，那是坐标级揭示误判的临时绕过；揭示链已修成节点级，
//   故恢复 vanilla 的「mNodes 全量 + 揭示链控制显隐」做法。研究顺序仍由 mSchematicDependencies 强制。
// ---------------------------------------------------------------------------

// 构建研究树 mNodes：包含**全部方块**的当前级节点（可见性由揭示链控制，不在此过滤）。
// 买完一级由调用方（WT_AdvanceResearchBlocks）先把 B.Current 推进到下一级，这里只负责
// 「成员资格（全部方块）+ 存活校验」。揭示链（UnhiddenBy/NodesToUnhide）决定每个节点何时显形。
static TArray<UFGResearchTreeNode*> WT_BuildLiveNodes()
{
    TArray<UFGResearchTreeNode*> Out;
    Out.Reserve(WT_Blocks().Num());
    for (const FWTResearchBlock& B : WT_Blocks())
    {
        if (B.Levels.IsValidIndex(B.Current) && WT_IsNodeAlive(B.Levels[B.Current]))
        {
            Out.Add(B.Levels[B.Current]);
        }
    }
    return Out;
}

// ---------------------------------------------------------------------------
// ★2026-11-06 #44：研究完成即自动显示下一级（无需手动关闭重开 MAM）。
//   机制见 URemoteFactoryResearchRefresh::HandleResearchStateChanged：
//   订阅 AFGResearchManager::OnResearchStateChanged（动态委托，必须用 UFUNCTION），
//   研究状态一变就同步调用 WT_AdvanceResearchBlocks 把 mNodes 推进到下一级；
//   由于我们的处理器在 MAM 自身处理器【之前】注册，MAM 重读树时拿到的已是更新后的 mNodes
//   ⇒ LV2 立刻出现，玩家无感知延迟。
// ---------------------------------------------------------------------------
void WT_AdvanceResearchBlocks(UWorld* World)
{
    // ★2026-11-06 崩溃根治：每次推进前先自愈。若 mNodes 里混进了悬空节点
    // （释放后复用、IsValid 漏判），先整树重建再推进，保证本函数写回 mNodes 的永远有效。
    // 用 bReentrant 防重入（本函数推进时不该再触发一次完整自检）。
    static bool bReentrant = false;
    if (!bReentrant)
    {
        bReentrant = true;
        if (WT_TreeIsCorrupt())
        {
            WT_RebuildMergedTree(World);
        }
        bReentrant = false;
    }

    const AFGSchematicManager* SM = World ? AFGSchematicManager::Get(World) : nullptr;
    if (!SM)
    {
        return;
    }

    // ★2026-11-09 回归修复：依赖非空与节点推进解耦，每次推进路径都确保武装（详见 WT_ValidateAndRepairTree 注释）。
    //   EnsureUnlockDependencies 幂等，非空即返回，此处常驻调用零额外开销。
    if (UWTResearchTreeBase* CDO = Cast<UWTResearchTreeBase>(WT_GetTreeClass()->GetDefaultObject()))
    {
        CDO->EnsureUnlockDependencies();
    }

    bool bDirty = false;
    for (FWTResearchBlock& B : WT_Blocks())
    {
        while (B.Current + 1 < B.Levels.Num())
        {
            UClass* Cur = B.Schematics.IsValidIndex(B.Current) ? B.Schematics[B.Current] : nullptr;
            if (!Cur || !SM->IsSchematicPurchased(Cur))
            {
                break;
            }
            ++B.Current;
            bDirty = true;
        }
    }
    if (!bDirty)
    {
        return;
    }

    TArray<UFGResearchTreeNode*> NewNodes = WT_BuildLiveNodes();
    UFGResearchTree::SetNodes(WT_GetTreeClass(), NewNodes);
    // ★崩溃修复：CDO 重初始化会清空 mUnlockDependencies，令整棵树被判 Locked / 列表读 [0] 越界。
    //   每次推进后重新武装恒真依赖，确保 MAM 列表按钮读 [0] 永不越界、树恒解锁。
    if (UWTResearchTreeBase* CDO = Cast<UWTResearchTreeBase>(WT_GetTreeClass()->GetDefaultObject()))
    {
        CDO->EnsureUnlockDependencies();
    }
    WT_WARNF(TEXT("[wt-block] advanced -> tree now exposes %d nodes (1 per block)."), NewNodes.Num());
}

// ---------------------------------------------------------------------------
// ★2026-11-06 #44：研究完成即时推进（落地实现，对应 Remote_FactoryResearchTree.h 的
//   URemoteFactoryResearchRefresh）。机制见头文件注释：订阅 OnResearchStateChanged /
//   OnResearchCompleted，在研究完成的那个 tick 预先推进合并树 + 刷新已放置升级建筑，
// 使 MAM 重读树时拿到已推进的 mNodes（无需重开面板）。
// ---------------------------------------------------------------------------
static UWorld* WT_GetGameWorldFromContexts()
{
    if (GEngine)
    {
        for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
        {
            if (UWorld* W = Ctx.World())
            {
                if (W->IsGameWorld())
                {
                    return W;
                }
            }
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// ★2026-11-08 LV16/LV17 研究里程碑提示：
//   在研究完成回调里给玩家弹一行文字通知（UFGGameUI::ShowTextNotification），告知：
//     · 三轨 Lv16 全完成 → 下一步 Lv17 强化路线需二选一；
//     · Lv17 选定了哪条路线（常规 / 强化碎片），另一条已永久锁定。
//   用文件静态 bool 保证每类提示整局只弹一次（研究不可逆，无需重复打扰）。
//   回调本身在客户端跑（直接改本地 MAM 树），故本地 GameUI 一定可用。
// ---------------------------------------------------------------------------
static void WT_NotifyResearchMilestone(UWorld* W)
{
    if (!W || !W->IsGameWorld())
    {
        return;
    }
    AFGSchematicManager* SM = AFGSchematicManager::Get(W);
    if (!SM)
    {
        return;
    }

    static bool bNotifiedLV16 = false;
    static bool bNotifiedLV17 = false;

    auto Show = [&](const FText& Msg)
    {
        if (APlayerController* PC = W->GetFirstPlayerController())
        {
            if (AFGPlayerController* FPC = Cast<AFGPlayerController>(PC))
            {
                if (UFGGameUI* UI = FPC->GetGameUI())
                {
                    UI->ShowTextNotification(Msg);
                }
            }
        }
    };

    // —— LV16 完成提示：三轨（0/1/2）Stage4 Lv4 全部已购 ——
    if (!bNotifiedLV16)
    {
        bool bAllLV16 = true;
        for (int32 ti = 0; ti < 3; ++ti)
        {
            UFGResearchTreeNode* N = WT_FindLevelNode(ti, 4, 4);
            if (!N)
            {
                bAllLV16 = false;
                break;
            }
            UClass* Sch = N->GetNodeSchematic();
            if (!Sch || !SM->IsSchematicPurchased(Sch))
            {
                bAllLV16 = false;
                break;
            }
        }
        if (bAllLV16)
        {
            Show(FText::FromString(
                TEXT("【智能仓储研究】三轨 Lv16 已全部研究完成！下一步进入 Lv17 强化路线，需二选一：常规路线 / 强化碎片路线。")));
            bNotifiedLV16 = true;
        }
    }

    // —— LV17 决策提示：某末段块（bFinal）的 Lv17 已购，报出所选路线、锁定另一条 ——
    if (!bNotifiedLV17)
    {
        for (const FWTResearchBlock& B : WT_Blocks())
        {
            if (!B.bFinal || B.Levels.Num() == 0)
            {
                continue;
            }
            UFGResearchTreeNode* N = B.Levels[0];   // Lv17 节点
            if (!N)
            {
                continue;
            }
            UClass* Sch = N->GetNodeSchematic();
            if (Sch && SM->IsSchematicPurchased(Sch))
            {
                const FString RouteName = (B.Route == EWTFinalRoute::Boost)
                    ? TEXT("强化碎片路线")
                    : TEXT("常规路线");
                const FString Msg = FString::Printf(
                    TEXT("【智能仓储研究】已选择 %s（Lv17）。另一条强化路线已永久锁定。"), *RouteName);
                Show(FText::FromString(Msg));
                bNotifiedLV17 = true;
                break;
            }
        }
    }
}

void URemoteFactoryResearchRefresh::HandleResearchStateChanged(EResearchState State)
{
    // 只在「研究结束」（完成/取消）时推进；研究进行中（ERS_Researching）推进无意义且徒增开销。
    if (State != EResearchState::ERS_NotResearching)
    {
        return;
    }
    UWorld* W = WT_GetGameWorldFromContexts();
    if (!W)
    {
        return;
    }
    // 先自愈（#47 崩溃闸门），再推进下一级（#44 即时显示），最后刷新已放置升级建筑的实际容量。
    WT_ValidateAndRepairTree(W);
    WT_AdvanceResearchBlocks(W);
    WT_RefreshResearchedUpgrades(W);
    WT_GivePendingShardCompensation(W, nullptr);   // ★2026-11-07 研究完成即把强化碎片显式发到背包（兜底补发）
    WT_NotifyResearchMilestone(W);                 // ★2026-11-08 LV16/LV17 里程碑提示
    WT_WARNF(TEXT("[wt-refresh-44] research completed -> advanced merged tree + refreshed placed upgrades."));
}

void URemoteFactoryResearchRefresh::HandleResearchCompleted(TSubclassOf<UFGSchematic> Schematic)
{
    UWorld* W = WT_GetGameWorldFromContexts();
    if (!W)
    {
        return;
    }
    WT_ValidateAndRepairTree(W);
    WT_AdvanceResearchBlocks(W);
    WT_RefreshResearchedUpgrades(W);
    // ★2026-11-07 修复 LV1 错位：把「刚研究完」的 schematic 直接传进去，绕过 IsSchematicPurchased 时序窗口。
    WT_GivePendingShardCompensation(W, Schematic ? Schematic.Get() : nullptr);
    WT_NotifyResearchMilestone(W);                 // ★2026-11-08 LV16/LV17 里程碑提示
    WT_WARNF(TEXT("[wt-refresh-44] research completed(%s) -> advanced merged tree + refreshed placed upgrades."),
             Schematic ? *Schematic->GetName() : TEXT("?"));
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

    FWTResearchBlock Block;
    Block.CoordX    = CoordX;
    Block.CoordY    = CoordY;
    Block.Track     = (int32)Track;
    Block.BaseLevel = (Stage - 1) * 4 + 1;

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

        // ★★★2026-11-05（第三次修正）：**4 级都挂同一份 Parents**。
        //   之前只有 Lv1 挂（怕"同坐标自环"），但覆盖式推进后 mNodes 里换成了 Lv2 ⇒
        //   Lv2 没有 Parents ⇒ 买完一级图上那条连线就断了，节点变成孤岛。
        //   自环恐惧其实不成立：Parents 指向的是**上一个方块的坐标**（Stage-1 那格），
        //   与本方块坐标不同；真正会自环的是 `UnhiddenBy`（指向下一个同级），我们压根没写它。
        TArray<FWTLinkTarget> Parents;
        if (ParentOfFirst)
        {
            Parents.Add(WT_MakeLink(ParentOfFirst));
        }

        // ★★★2026-11-05 回到定稿结构：**一个方块 = 一个坐标**，块内 4 级共用这一个坐标，
        //   但**不会同时进图**（见下面的 block 登记 + WT_AdvanceResearchBlocks）。
        //   硬约束（本轮实证）：`Parents`/`UnhiddenBy`/`NodesToUnhide` 的元素是 **IntVector2D 格点**，
        //   原版 11 节点 11 唯一坐标、零重复 ⇒ **整张图按坐标索引，同一坐标不能并存两个节点**。
        //   所以「同一坐标连买 4 次」只能靠「买完一级就把该坐标的节点换成下一级」来实现。
        WT_WriteNodeData(Node, WT_ResearchClassFor(Track, L), CoordX, CoordY, Parents);

        // ★块内不再建揭示链：本级与下一级坐标相同，`UnhiddenBy` 写进自己那个坐标就是**自环**
        //   （旧版实测症状：节点永远不显形）。块内推进改由 WT_AdvanceResearchBlocks 负责。

        Block.Levels.Add(Node);
        Block.Schematics.Add(WT_ResearchClassFor(Track, L));

        // ★只有本块**当前这一级**才进图的 mNodes（用户定稿：「买完 Lv1 后 Lv2 覆盖 Lv1」）。
        //   初始全部只暴露 Lv1；买完后由 WT_AdvanceResearchBlocks 把该坐标换成 Lv2，依此类推。
        if (k == 1)
        {
            OutNodes.Add(Node);
            FirstOfTile = Node;
        }
        PrevLevel = Node;
    }

    if (Block.Levels.Num() > 0)
    {
        WT_Blocks().Add(Block);
    }
    // 返回本级方块的代表节点（坐标即该方块坐标，供下一阶段挂 Parents）
    return FirstOfTile ? FirstOfTile : PrevLevel;
}

/** 末段一个方块（Lv17~20）：坐标为 (2,4) 常规 / (4,4) 碎片（★2026-11-08 由 Y=5 挪回原高度 Y=4；两块横向并排、图标区分见 WT_IconSpecFor），父节点 = 就近双轨末级。 */
static UFGResearchTreeNode* WT_MakeFinalTile(
    UObject* Outer, EWTFinalRoute Route, int32 CoordY,
    const TArray<FWTLinkTarget>& ConvergingParents,
    TArray<TObjectPtr<UFGResearchTreeNode>>& OutNodes,
    UFGResearchTreeNode** OutFirstLevel = nullptr,
    int32 CoordX = 4)
{
    UFGResearchTreeNode* PrevLevel  = nullptr;
    UFGResearchTreeNode* FirstLevel = nullptr;

    FWTResearchBlock Block;
    Block.CoordX    = CoordX;
    Block.CoordY    = CoordY;
    Block.Track     = -1;
    Block.bFinal    = true;
    Block.Route     = Route;
    Block.BaseLevel = WT_FinalMinLevel;

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

        // ★同 WT_MakeTile：末段 4 级也都挂同一份 ConvergingParents，
        //   否则 Lv18~20 覆盖上来后连线会断（自环不成立，理由见 WT_MakeTile 处注释）。
        WT_WriteNodeData(Node, WT_FinalResearchClassFor(Route, L), CoordX, CoordY, ConvergingParents);

        Block.Levels.Add(Node);
        Block.Schematics.Add(WT_FinalResearchClassFor(Route, L));

        if (L == WT_FinalMinLevel)
        {
            OutNodes.Add(Node);
            FirstLevel = Node;
        }
        PrevLevel = Node;
    }

    if (Block.Levels.Num() > 0)
    {
        WT_Blocks().Add(Block);
    }
    if (OutFirstLevel)
    {
        *OutFirstLevel = FirstLevel;
    }
    return FirstLevel ? FirstLevel : PrevLevel;
}

// ---------------------------------------------------------------------------
// ★v0.2.34 三轨合并 + 14 方块布局（2026-10-07 用户定案；按设计图回退为 2 条末段路线：
//   常规 / 碎片，无汇合；坐标口径见 WT_MAM_研究树结构图.svg）
//
// 坐标（列=X、行=Y，与结构图一致；三轨列间距 2，末段两块落在列间空隙、同排并排）：
//        X=1        X=2        X=3        X=4        X=5
//  Y=0  仓库 1              储能 1              碎片 1
//  Y=1  仓库 2              储能 2              碎片 2
//  Y=2  仓库 3              储能 3              碎片 3
//  Y=3  仓库 4              储能 4              碎片 4
//  Y=4            末段·常规            末段·碎片
//   ★2026-11-08：末段由 Y=5 挪回原高度 Y=4（引擎只画父→子直线，横向干线画不出，故不再留空行）。
//   包络 X 1..5（5 宽）x Y 0..4（5 高）—— 落在原版实测包络 X 0..6 / Y 0..8 之内。
//   两个末段节点同排 Y=4 并排（常规 X=2 / 碎片 X=4），Lv17 二选一（互斥），选定后 Lv18~20 整段跟随。
//
//   每个方块仍是「同一坐标连买 4 次」（Lv2 覆盖 Lv1，见 FWTResearchBlock）。
// ---------------------------------------------------------------------------
void UWTResearchTreeBase::ConfigureMergedTree(const FText& DisplayName, const FText& Description)
{
    mDisplayName           = DisplayName;
    mPreUnlockDisplayName  = DisplayName;
    mPreUnlockDescription  = Description;
    mPostUnlockDescription = Description;

    // ★重建前先清空方块登记表：ConfigureMergedTree 每次进世界都会对 CDO 重跑一遍，
    //   不清就会把上一轮的 block 累加进去 ⇒ 同一坐标登记两份 ⇒ 推进时错位。
    WT_Blocks().Reset();

    const EWTUpgradeTrack Tracks[3] = {
        EWTUpgradeTrack::Storage,
        EWTUpgradeTrack::PowerStorage,
        EWTUpgradeTrack::Accelerator,
    };

    TArray<TObjectPtr<UFGResearchTreeNode>> Nodes;
    Nodes.Reserve(14 * 4);   // 12 阶段方块 + 2 末段方块 = 14 个方块（初挂各 1 个 Lv1 节点）

    UFGResearchTreeNode* Stage4Last[3] = { nullptr, nullptr, nullptr };   // 保留登记（末段父已改由 ChildrenAndRoads 表达，此处仅备查）

    for (int32 ti = 0; ti < 3; ++ti)
    {
        UFGResearchTreeNode* PrevTileLast = nullptr;   // 上一节点层的同级（同一条横排里向左相邻）
        for (int32 Stage = 1; Stage <= 4; ++Stage)
        {
            // ★坐标口径（与结构图一致）：列 X = 研究种类（1/3/5 = 仓库/储能/碎片，间距 2 给末段留空隙），行 Y = 节点层（0/1/2/3）。
            UFGResearchTreeNode* TileLast =
                WT_MakeTile(GetTransientPackage(), Tracks[ti], Stage, /*CoordX=*/1 + ti * 2, /*CoordY=*/Stage - 1,
                            PrevTileLast, Nodes);
            PrevTileLast = TileLast;
        }
        Stage4Last[ti] = PrevTileLast;
        (void)Stage4Last[ti];   // 明确「仅为登记/调试用」，避免「赋值未使用」告警
    }

    // ★2026-11-08 末段连线整理（用户反馈：末段一团跨列斜线太乱，要求照设计图摆整齐）。
    //
    // 实测证据（本轮日志 [wt-nd] VANILLA dump）：
    //   · 原版节点绝大多数 `Parents=1` —— **一个父 = 一条线**；
    //   · 全树唯一的 `Parents=4` 是汇合枢纽，四条线是**扇形汇到同一个点**（(1,0)(2,0)(4,0)(5,0)→(3,3)），
    //     同源同汇 ⇒ 不交叉。
    //   ⇒ 旧版给两个末段各挂「三轨全部」父 = 每块 3 条线、共 6 条；而三轨列 X=1/3/5 与末段列 X=2/4
    //     **交错**，必然出现 (1,3)-(4,4)、(5,3)-(2,4) 这种跨两列的长斜线互相穿插 ⇒ 图上乱成一团。
    //
    // 设计图那种「三轨先汇入一条共享干线、再从中间分两根下来」MAM 画不出来（没有总线/干线拐点概念，
    // 它只画「父坐标→子坐标」的直连）。所以改成 **就近双父、零交叉**：
    //     常规(2,4) ← 仓库轨(1,3) + 储能轨(3,3)
    //     碎片(4,4) ← 储能轨(3,3) + 碎片轨(5,3)
    //   四条边 (1,3)-(2,4)、(3,3)-(2,4)、(3,3)-(4,4)、(5,3)-(4,4) 两两成「V」、互不交叉；
    //   三轨全部保持与末段的连线（中间储能轨同时喂两块，最像设计图里那条「中间分叉」）。
    //   ★功能完全不受影响：末段「任一轨 Lv16 即可解锁」由揭示链保证（下面把 NodesToUnhide 写在
    //     **三轨** 的 Lv16 上），与图上画几条线无关 —— 单轨模式下三轨只能走一条，必须 ANY-of 揭示。
    // ★★★2026-11-08 第二轮修正（用户截图：干线出来了，但混着斜线）：
    //   实测确认 MAM **同时**画两样东西：
    //     ① 子节点 `Parents` → 父坐标 的**直连斜线**（末段 Parents=2 就是那两条斜线）；
    //     ② 父节点 `ChildrenAndRoads` 的 Points 折线（我们写的三段正交路径）。
    //   两者叠在一起 ⇒ 干线 + 斜线 = 仍然乱。
    //   因此末段的 **Parents 清空**，连线只留父侧 `ChildrenAndRoads` 的正交折线。
    //   · 原版根节点 `Parents=0` 但 `ChildrenAndRoads=1`，图上照样有线 ⇒ 画线靠父侧折线，不靠 Parents；
    //   · 显示（何时出现）由 `UnhiddenBy` 控制（末段写了三轨 3 条），与 Parents 无关
    //     （原版父=0 的根是 UnhiddenBy=0 才常显；我们 UnhiddenBy≠0 ⇒ 仍按揭示链显隐）。
    TArray<FWTLinkTarget> PlainParents, BoostParents;   // 故意留空：不让 MAM 画父→子的斜直线

    UFGResearchTreeNode* PlainFirst = nullptr;
    UFGResearchTreeNode* BoostFirst = nullptr;
    // ★末段两块（常规 X=2 / 碎片 X=4，同排并排、落在三轨列间空隙），Lv17 二选一。
    //   ★2026-11-08 由 Y=5 挪回原高度 Y=4（引擎只画父→子直线，横向干线画不出，不再为干线留空行）。
    WT_MakeFinalTile(GetTransientPackage(), EWTFinalRoute::Plain, /*CoordY=*/4, PlainParents, Nodes, &PlainFirst, /*CoordX=*/2);
    WT_MakeFinalTile(GetTransientPackage(), EWTFinalRoute::Boost, /*CoordY=*/4, BoostParents, Nodes, &BoostFirst, /*CoordX=*/4);

    // ★★★2026-11-07 **恢复揭示链**（用户实测：只买 T1/T2 却看到 LV5~LV20 全部方块）。
    //
    // 为什么必须写 `UnhiddenBy` / `NodesToUnhide`（原版数据实证，见 [wt-nd] VANILLA dump）：
    //   · 根节点：`Parents=0 / UnhiddenBy=0 / NodesToUnhide=7`  ⇒ **一开始就显示**，买完后揭示 7 个；
    //   · 中间层：`Parents=1 / UnhiddenBy=4 / NodesToUnhide=0` ⇒ **初始不在显示集**，要靠别人的
    //     `NodesToUnhide` 点到自己的坐标才出现。
    //   ⇒ 「节点是否出现在图上」**由揭示链决定，与 `Parents` 无关**（Parents 只画线/表达从属）。
    //   我们此前只写 `Parents`、两条都不写 ⇒ 所有节点都带上「根节点」语义（UnhiddenBy=0）
    //   ⇒ 14 个方块全量常显。这正是用户看到 LV5 起也露出的原因。
    //
    // 与 11-05「不建揭示链」的关系（那次用户反馈「节点 2 有两个前置条件」）：
    //   MAM 面板里的「前置条件」列表读的是 schematic 的 `mSchematicDependencies`（见
    //   Remote_FactoryUpgradeResearch.cpp 的 AddPurchasedDep/AddNotPurchasedDep），
    //   **不是** UnhiddenBy/NodesToUnhide（后者只控制显隐）。故恢复揭示链不会多出前置条目。
    //
    // 写法约束（避免 11-05 踩过的自环）：
    //   · 块内 4 级同坐标 ⇒ **块内绝不写揭示**（写同坐标即自环，被揭示者永不显形）；
    //   · 揭示链只跨块（同轨上一阶段方块 → 本阶段方块），坐标必然不同。
    //   · 每个非首块节点**恰好一条** `UnhiddenBy`（指回上一阶段方块坐标），
    //     父节点 `NodesToUnhide` 追加本块坐标 ⇒ 与原版「中间层 UnhiddenBy=1 组」同形。
    for (int32 ti = 0; ti < 3; ++ti)
    {
        for (int32 Stage = 2; Stage <= 4; ++Stage)
        {
            const int32 PX = 1 + ti * 2;
            const int32 FromY = Stage - 2;   // 上一阶段方块所在行
            const int32 ToY   = Stage - 1;   // 本阶段方块所在行
            UFGResearchTreeNode* From = WT_FindLevelNode(ti, Stage - 1, /*Lv=*/4);
            UFGResearchTreeNode* To   = WT_FindLevelNode(ti, Stage,     /*Lv=*/1);
            if (From && To)
            {
                WT_LinkUnhide(From, To);     // 揭示链只挂 LV1（To.UnhiddenBy += From）
                // ★ ChildrenAndRoads 必须写在「父块当前可见的每个等级」上（LV1~4 共用同一坐标、
                //   升级会换成下一级节点对象）。旧版只写在 LV4（不在 mNodes、不可见）
                //   ⇒ 可见父节点永远没有 ChildrenAndRoads ⇒ MAM 画不出父→子连线。
                //   4 级都写，无论推进到哪一级，可见父节点都带 ChildrenAndRoads ⇒ 连线持续。
                for (int32 PLv = 1; PLv <= 4; ++PLv)
                {
                    if (UFGResearchTreeNode* FromLv = WT_FindLevelNode(ti, Stage - 1, PLv))
                    {
                        WT_AddChildRoad(FromLv, WT_MakeLink(To));
                    }
                }
                // ★ 基础连线来自子节点 Parents：给本阶段 4 个等级都挂父节点，
                //   否则升到 LV2~LV4 后当前节点换成无 Parents 的新对象，连线会闪没。
                for (int32 Lv = 1; Lv <= 4; ++Lv)
                {
                    if (UFGResearchTreeNode* ToLv = WT_FindLevelNode(ti, Stage, Lv))
                    {
                        WT_AppendLinkElem(ToLv, TEXT("Parents"), WT_MakeLink(From));
                    }
                }
            }
            else
            {
                WT_WARNF(TEXT("[wt-unhide] track %d stage %d: link nodes missing (from=%s to=%s)."),
                         ti, Stage, From ? TEXT("y") : TEXT("n"), To ? TEXT("y") : TEXT("n"));
            }
        }
    }
    // 末段揭示链 + 父侧连线补齐（2026-11-08 重写，配合上面「就近双父」）。
    //
    //   · 揭示（NodesToUnhide / UnhiddenBy）：仍写**三轨全部** —— 这些字段只控制「节点何时出现在图上」，
    //     不决定画几条线；写三轨才能保证单轨模式下「任一轨走到 Lv16」都能揭示末段（ANY-of，与 schematic 门禁一致）。
    //   · 图上的连线（Parents / ChildrenAndRoads）：**只补那两个就近父**，与 WT_MakeFinalTile 构造时写入的
    //     Parents 保持一致。旧版在这里对三轨全部追加 Parents 且正向/反向各写一遍，正是跨列斜线乱成一团的来源。
    //   · ★WT_FindLevelNode 的 Level 是**块内 1..4**（非全局等级）：阶段 4 的末级传 4（=全局 Lv16）。
    for (UFGResearchTreeNode* FinalFirst : { PlainFirst, BoostFirst })
    {
        if (!FinalFirst)
        {
            continue;
        }
        // 揭示：三轨 Lv16 → 本末段（任一轨到达即可见）
        for (int32 ti = 0; ti < 3; ++ti)
        {
            if (UFGResearchTreeNode* S4Last = WT_FindLevelNode(ti, 4, /*Lv=*/4))
            {
                WT_AppendLinkElem(S4Last, TEXT("NodesToUnhide"), WT_MakeLink(FinalFirst));
                WT_AppendLinkElem(FinalFirst, TEXT("UnhiddenBy"), WT_MakeLink(S4Last));
            }
        }
        // 画线：只给「就近双父」补 ChildrenAndRoads（父→子那份；子侧 Parents 已在 WT_MakeFinalTile 写好）。
        //   常规(2,4) ← 仓库(1,3)+储能(3,3)；碎片(4,4) ← 储能(3,3)+碎片(5,3)。（末段已挪回 Y=4）
        const bool bIsPlain = (FinalFirst == PlainFirst);
        const int32 ParentTracks[2] = { bIsPlain ? 0 : 1, bIsPlain ? 1 : 2 };
        for (int32 pi = 0; pi < 2; ++pi)
        {
            for (int32 PLv = 1; PLv <= 4; ++PLv)   // 父块 4 级都写：无论推进到哪一级，可见父节点都带线
            {
                if (UFGResearchTreeNode* S4Lv = WT_FindLevelNode(ParentTracks[pi], 4, PLv))
                {
                    WT_AddChildRoad(S4Lv, WT_MakeLink(FinalFirst));
                }
            }
        }
    }

    // ★2026-11-10 可见性由揭示链驱动：mNodes 含全部方块当前级，未解锁的由 UnhiddenBy/NodesToUnhide
    //   控制显隐（不在此过滤）。不再用 WT_IsBlockUnlocked 预先裁剪。
    mNodes = WT_BuildLiveNodes();

    // ★2026-11-07 定向探针（不改设计，只加日志）：用户实测三轨竖线已出，但
    //   「第4阶段(LV16) → 末段(LV17)」的汇聚连线没有。这里回读三条轨阶段4末级
    //   与两个末段块 Lv17 的 Parents / ChildrenAndRoads，判断是「没写进去」还是
    //   「写了但 MAM 不画」。
    for (UFGResearchTreeNode* F : { PlainFirst, BoostFirst })
    {
        if (F)
        {
            WT_DumpNodeLinks(F, TEXT("FINAL-Lv17"));
        }
    }
    for (int32 ti = 0; ti < 3; ++ti)
    {
        if (UFGResearchTreeNode* S4 = WT_FindLevelNode(ti, 4, /*Lv=*/4))
        {
            WT_DumpNodeLinks(S4, FString::Printf(TEXT("STAGE4-Lv16-track%d"), ti));
        }
    }

    // ★★★2026-11-05 **实测定案：必须非空**（这一版把上一轮的「清空验证」结论固化了）。
    //
    // 上一轮按排查清单做了「清空验证」，实测结果反向证明清空是**错**的：
    //   · 清空后每次开 MAM 仍然固定抛 —— `index 0 from 'GetUnlockDependencies_ReturnValue'
    //     of length 0 in BPW_MAMTree_ListButtonInfo:SetContent`；
    //   · 而且**用户点节点时**日志出现 `FGSchematic::GetType: class was nullpeter` +
    //     `FGSchematic::GetTimeToComplete: class was nullpeter`
    //     ⇒ UI 侧拿到的 schematic 是 **NULL**；
    //   · 可我们的 C++ 探针在**同一批节点**上拿到的是正常值（`GetNodeSchematic=WTUpg_Cap_S1_L01`）
    //     ⇒ 数据是好的，是 **UI 那条路提前中断了**。
    //   机理：`SetContent` 第一行就 `GetUnlockDependencies()[0]`，数组空 ⇒ 蓝图抛异常中断
    //   ⇒ 后面绑定 schematic / 图标 / 可研究状态的代码**一条都没跑** ⇒ 方块只剩空壳、点不动。
    //
    // 所以：依赖必须**非空**，但**不能承载真实门槛**。
    //
    // ★★★2026-11-05 二次修正（上一版挂 T1 里程碑 ⇒ 用户实测整棵树全灰、点不了）：
    //   上一版把「T1 里程碑已购」挂成树解锁依赖。结果在**没买 T1 的存档**里
    //   `[wt-diag] unlocked=0` ⇒ MAM 把整棵树判为 Locked ⇒ 图区全灰。
    //   而节点自身的入口门禁本来就是「T1 已购」（`mSchematicDependencies` 第 ① 层），
    //   该拦的地方一点没漏 —— 让整棵树背这个门槛纯属重复且致命。
    //   ⇒ 改用 `UWT_AlwaysMetDep`：非空（绕过 SetContent 的 [0] 越界）且恒为真（树恒解锁）。
    //
    // ⚠ 还有一个必须配套的点：本树是运行时注册的，注册时机晚于
    //   `AFGResearchManager::PopulateResearchTreeList` ⇒ 管理器可能压根没跑过解锁判定。
    //   配套修复在 `Remote_FactoryGameWorldModule.cpp` 的 `[wt-diag]` 段：
    //   在权威侧调用 `UpdateUnlockedResearchTrees()`（幂等），并打刷新前后的 unlocked 值。
    //
    // ★★★2026-11-05 崩溃修复（根因）：研究树 CDO 会被引擎在处理树时重新初始化，
    //   连带清空 mUnlockDependencies（表现为 MAM 列表按钮 SetContent 报 "GetDependecyData of length 0"）
    //   并销毁以 CDO 为 Outer 的子节点（节点悬空 → 开 MAM 调 GetNodeSchematic 读野指针崩溃）。
    //   节点已改为 GetTransientPackage() 为 Outer 并 AddToRoot()，不再随 CDO 重建被销毁；
    //   依赖则用下面的自愈方法补回，且 WT_AdvanceResearchBlocks 每次推进后也会重新武装。
    EnsureUnlockDependencies();

    // ★判定位：T1 里程碑到底买了没有 + 树在 MAM 眼里是 Locked(0) 还是 Unlocked(1)。
    //   期望（买了 T1）：purchased=1，status=1。若 purchased=0 ⇒ 一切「全灰」都只是还没买卡。
    {
        bool bT1 = false;
        int32 TreeStatus = -1;
        // ★注意：这里是**树 CDO**（ConfigureMergedTree 由 WT_BuildResearchTrees 对 CDO 调用），
        //   CDO 的 GetWorld() 恒为 null ⇒ 必须从 GEngine 的世界上下文里取当前世界。
        UWorld* W = nullptr;
        if (GEngine)
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
        if (W)
        {
            if (const AFGSchematicManager* SM = AFGSchematicManager::Get(W))
            {
                bT1 = SM->IsSchematicPurchased(URemote_FactoryMilestone::StaticClass());
            }
            TreeStatus = (int32)UFGResearchTree::GetResearchTreeStatus(GetClass(), W);
        }
        WT_WARNF(TEXT("[wt-probe] T1 milestone purchased=%d | tree status=%d (0=Locked 1=Unlocked) | unlockDeps=%d | world=%s"),
                 bT1 ? 1 : 0, TreeStatus, mUnlockDependencies.Num(),
                 W ? TEXT("ok") : TEXT("NULL"));
    }

    WT_WARNF(TEXT("[wt-node] merged tree '%s': %d nodes (= 14 tiles x 4 levels), unlockDeps=%d."),
             *DisplayName.ToString(), Nodes.Num(), mUnlockDependencies.Num());

    // ============================================================================================
    // ★★★2026-11-05 探针：MAM 节点「空方块」到底断在哪一环（用户实测两轮仍空白）
    //
    // 节点 icon 的链路假设：
    //   节点 widget → UFGResearchTreeNode::GetNodeSchematic()（蓝图实现，读 mNodeDataStruct.Schematic）
    //   → UFGSchematic CDO 的 mSchematicIcon(FSlateBrush) → 画出来
    //
    // 三个可能断点，本探针一次全查：
    //   ① mNodeDataStruct 里**以 Schematic 开头的字段有几个**（我们按前缀写、蓝图按精确名读 ⇒ 可能不是同一个）
    //   ② C++ 直接调 GetNodeSchematic() 是否非空 —— 这就是 MAM widget 取 schematic 的同一条路
    //      （BlueprintImplementableEvent 在 C++ 里调用会走蓝图 Event Graph，拿到的是真值，不是我们的 dump）
    //   ③ 拿到 schematic 后，CDO 的 mSchematicIcon 里 ResourceObject 是否非空
    // ============================================================================================
    {
        UFGResearchTreeNode* N0 = Nodes.Num() > 0 ? Nodes[0].Get() : nullptr;
        if (N0)
        {
            FString MemberDump;
            int32 SchematicLike = 0;
            if (FStructProperty* NP = FindFProperty<FStructProperty>(N0->GetClass(), TEXT("mNodeDataStruct")))
            {
                if (NP->Struct)
                {
                    for (TFieldIterator<FProperty> It(NP->Struct); It; ++It)
                    {
                        MemberDump += FString::Printf(TEXT("%s(%s) "), *It->GetName(), *It->GetClass()->GetName());
                        if (It->GetName().StartsWith(TEXT("Schematic")))
                        {
                            ++SchematicLike;
                        }
                    }
                }
            }
            WT_WARNF(TEXT("[wt-probe] mNodeDataStruct fields (Schematic* count=%d): %s"),
                     SchematicLike, *MemberDump);
        }

        // ★坐标唯一性自证：MAM 按坐标摆节点，重叠即互相遮盖（曾经 56 节点只显示 14 个方块）。
        //   期望：unique == nodes.Num()。若 unique 远小于节点数 ⇒ 布局又退回"多级共坐标"。
        {
            TSet<FIntPoint> Uniq;
            for (const TObjectPtr<UFGResearchTreeNode>& NP : Nodes)
            {
                UFGResearchTreeNode* N = NP.Get();
                if (!N)
                {
                    continue;
                }
                void*   Data  = nullptr;
                UStruct* Scope = nullptr;
                WT_GetNodeDataScope(N, Data, Scope);
                if (!Data || !Scope)
                {
                    continue;
                }
                if (FStructProperty* CoordProp =
                        CastField<FStructProperty>(WT_FindStructMember(Scope, TEXT("Coordinates"))))
                {
                    void* CoordData = CoordProp->ContainerPtrToValuePtr<void>(Data);
                    int32 X = -1, Y = -1;
                    if (FIntProperty* XP = CastField<FIntProperty>(
                            WT_FindStructMember(CoordProp->Struct, TEXT("X"))))
                    {
                        X = *XP->ContainerPtrToValuePtr<int32>(CoordData);
                    }
                    if (FIntProperty* YP = CastField<FIntProperty>(
                            WT_FindStructMember(CoordProp->Struct, TEXT("Y"))))
                    {
                        Y = *YP->ContainerPtrToValuePtr<int32>(CoordData);
                    }
                    Uniq.Add(FIntPoint(X, Y));
                }
            }
            WT_WARNF(TEXT("[wt-probe] coordinates: %d unique / %d nodes %s"),
                     Uniq.Num(), Nodes.Num(),
                     (Uniq.Num() == Nodes.Num()) ? TEXT("(OK, no overlap)")
                                                 : TEXT("(OVERLAP! nodes will hide each other)"));
        }

        for (int32 i = 0; i < 3 && i < Nodes.Num(); ++i)
        {
            UFGResearchTreeNode* N = Nodes[i].Get();
            if (!N)
            {
                continue;
            }
            // ★这一行是判定位：若 NULL，说明蓝图读的结构体字段与我们写的不是同一个
            //   ⇒ 节点 widget 拿不到 schematic ⇒ 方块里什么都不画（与「空方块」现象吻合）。
            const TSubclassOf<UFGSchematic> S = N->GetNodeSchematic();
            FString BrushRes = TEXT("<no schematic>");
            FString SizeTxt = TEXT("?");
            if (S)
            {
                if (UFGSchematic* CDO = S->GetDefaultObject<UFGSchematic>())
                {
                    if (FStructProperty* BP =
                            FindFProperty<FStructProperty>(CDO->GetClass(), TEXT("mSchematicIcon")))
                    {
                        if (BP->Struct)
                        {
                            void* Mem = BP->ContainerPtrToValuePtr<void>(CDO);
                            if (FObjectProperty* RO = CastField<FObjectProperty>(
                                    BP->Struct->FindPropertyByName(TEXT("ResourceObject"))))
                            {
                                UObject* Obj = RO->GetObjectPropertyValue(RO->ContainerPtrToValuePtr<void>(Mem));
                                BrushRes = Obj ? Obj->GetName() : TEXT("<brush resource NULL>");
                            }
                            // ★2026-11-07 #45 验证：回读 mSchematicIcon.ImageSize（FVector2D），
                            //   确认我们对齐原版基准的尺寸真的写进去了。
                            if (FProperty* ISP2 = BP->Struct->FindPropertyByName(TEXT("ImageSize")))
                            {
                                if (FStructProperty* VecProp = CastField<FStructProperty>(ISP2))
                                {
                                    void* VecMem = VecProp->ContainerPtrToValuePtr<void>(Mem);
                                    float vx = 0.f, vy = 0.f;
                                    if (FProperty* Xp = VecProp->Struct->FindPropertyByName(TEXT("X")))
                                    {
                                        if (FNumericProperty* Xn = CastField<FNumericProperty>(Xp))
                                        {
                                            vx = Xn->GetFloatingPointPropertyValue(Xn->ContainerPtrToValuePtr<void>(VecMem));
                                        }
                                    }
                                    if (FProperty* Yp = VecProp->Struct->FindPropertyByName(TEXT("Y")))
                                    {
                                        if (FNumericProperty* Yn = CastField<FNumericProperty>(Yp))
                                        {
                                            vy = Yn->GetFloatingPointPropertyValue(Yn->ContainerPtrToValuePtr<void>(VecMem));
                                        }
                                    }
                                    SizeTxt = FString::Printf(TEXT("(%.0f,%.0f)"), vx, vy);
                                }
                            }
                        }
                    }
                }
            }
            // ★2026-11-07 成本栏诊断：MAM「成本」区显示英文占位（No Unlock Cost Assure）时，
            //   先确认是「成本数组为空」还是「schematic 为 null」还是「玩家已有材料 ⇒ 剩余成本空」。
            //   GetCost / GetUnlocks 均为 public static，和 MAM 面板读的是同一份数据（CDO）。
            int32 CostNum = -1, UnlockNum = -1;
            FString CostTxt = TEXT("-");
            if (S)
            {
                const TArray<FItemAmount> Cost = UFGSchematic::GetCost(S);
                CostNum = Cost.Num();
                UnlockNum = UFGSchematic::GetUnlocks(S).Num();
                for (int32 c = 0; c < CostNum && c < 3; ++c)
                {
                    UClass* IC = Cost[c].ItemClass.Get();
                    CostTxt += FString::Printf(TEXT("%s x%d; "),
                                               IC ? *IC->GetName() : TEXT("NULL"), Cost[c].Amount);
                }
            }
            WT_WARNF(TEXT("[wt-probe] node[%d] GetNodeSchematic=%s | cost=%d lines [%s] unlocks=%d | icon=%s iconSize=%s"),
                     i, S ? *S->GetName() : TEXT("NULL"), CostNum, *CostTxt, UnlockNum, *BrushRes, *SizeTxt);
        }

        // ============================================================================================
        // ★★★2026-11-05 决定性探针：**让游戏自己回答「这个节点能不能研究」**。
        //
        // 一手入口（`FGResearchManager.h:252`）：
        //   `UFUNCTION(BlueprintCallable) bool CanResearchBeInitiated(TSubclassOf<UFGSchematic>)`
        // —— MAM 里节点按钮能不能点，最终收敛到它。逐个节点打三件事：
        //   ① canResearch  —— 游戏认不认这个节点可研究
        //   ② deps 条数 / 每条 `AreDependenciesMet(World)` 的真值
        //   ③ 哪条依赖返回 0，就是「全灰点不动」的真凶（其余环节已逐项排除：
        //      坐标 56/56 唯一、树 status=1 已解锁、56 个 schematic 全部可解、图标资源非空）
        //
        // 判读：
        //   · canResearch 全 1  ⇒ 后端没问题，剩下的是**渲染/UI**侧
        //   · canResearch 全 0 且某条依赖恒 0 ⇒ 锁定那条依赖（多半是我们自己的 Dep 子类）
        // ============================================================================================
        {
            UWorld* PW = nullptr;
            if (GEngine)
            {
                for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
                {
                    if (Ctx.World())
                    {
                        PW = Ctx.World();
                        break;
                    }
                }
            }
            AFGResearchManager* RM = PW ? AFGResearchManager::Get(PW) : nullptr;
            if (!RM)
            {
                WT_WARNF(TEXT("[wt-res] research manager not available at tree-build time -- probe skipped."));
            }
            else
            {
                int32 UnmetChecks = 0;
                int32 CanCount    = 0;
                for (int32 i = 0; i < Nodes.Num(); ++i)
                {
                    UFGResearchTreeNode* N = Nodes[i].Get();
                    const TSubclassOf<UFGSchematic> S = N ? N->GetNodeSchematic() : nullptr;
                    if (!S)
                    {
                        continue;
                    }

                    TArray<UFGAvailabilityDependency*> Deps;
                    UFGSchematic::GetSchematicDependencies(S, Deps);

                    FString DepText;
                    bool bAllMet = true;
                    for (UFGAvailabilityDependency* D : Deps)
                    {
                        const bool bMet = D && D->AreDependenciesMet(PW);
                        if (!bMet)
                        {
                            bAllMet = false;
                            ++UnmetChecks;
                        }
                        DepText += FString::Printf(TEXT("%s=%d "),
                                                   D ? *D->GetClass()->GetName() : TEXT("NULL"), bMet ? 1 : 0);
                    }

                    const bool bCan = RM->CanResearchBeInitiated(S);
                    if (bCan)
                    {
                        ++CanCount;
                    }
                    // 只打前 14 条 + 所有「不可研究」的，避免 56 行刷屏。
                    if (i < 14 || !bCan)
                    {
                        WT_WARNF(TEXT("[wt-res] node[%2d] %-22s canResearch=%d deps=%d allMet=%d %s"),
                                 i, *S->GetName(), bCan ? 1 : 0, Deps.Num(), bAllMet ? 1 : 0, *DepText);
                    }
                }
                WT_WARNF(TEXT("[wt-res] summary: %d/%d nodes canResearch, %d unmet dependency checks."),
                         CanCount, Nodes.Num(), UnmetChecks);
            }
        }
    }

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
// ★2026-11-05 崩溃修复：解锁依赖自愈。
//   研究树 CDO 被引擎重初始化时会清空 mUnlockDependencies（UI 列表按钮读 [0] 越界 ⇒ 报错），
//   故每次都自查：若为空则重建一个「恒真」依赖（UWT_AlwaysMetDep）。该依赖非空 ⇒ 绕过 [0] 越界，
//   恒为真 ⇒ 树恒解锁。依赖对象以 GetTransientPackage() 为 Outer + AddToRoot，不随 CDO 重建被销毁。
//   在 ConfigureMergedTree 末尾与 WT_AdvanceResearchBlocks 每次推进后都会调用，确保时刻非空。
// ---------------------------------------------------------------------------
void UWTResearchTreeBase::EnsureUnlockDependencies()
{
    // 已非空则不动（幂等），避免每 1s 定时器反复 NewObject 污染 transient package。
    if (mUnlockDependencies.Num() > 0)
    {
        return;
    }
    UWT_AlwaysMetDep* Dep = NewObject<UWT_AlwaysMetDep>(GetTransientPackage());
    if (Dep)
    {
        Dep->AddToRoot();   // 工程纪律：运行时 NewObject 必须钉住
        mUnlockDependencies.Add(Dep);
        WT_WARNF(TEXT("[wt-node] EnsureUnlockDependencies: re-armed tree unlock dep (num=%d)."),
                 mUnlockDependencies.Num());
    }
    else
    {
        WT_WARNF(TEXT("[wt-node] EnsureUnlockDependencies: NewObject<UWT_AlwaysMetDep> returned null; "
                      "MAM tree list button may keep the vanilla index-0 warning."));
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
        PrevTileLast = WT_MakeTile(GetTransientPackage(), Track, Stage, /*CoordX=*/0, /*CoordY=*/Stage - 1,
                                   PrevTileLast, Nodes);
    }
    mNodes = Nodes;

    WT_WARNF(TEXT("[wt-node] %s: attached %d nodes (4 tiles x 4 levels)."),
             *DisplayName.ToString(), Nodes.Num());
}

TArray<TSubclassOf<UFGResearchTree>> WT_GetAllResearchTrees()
{
    // ★2026-11-05 恢复上架（用户定案：「走第二条路」→「MAM 定案写入」）。
    //   0.2.56~0.2.139 期间这里硬返回空数组（MAM 里没有「智能工厂」树）。
    //   现在挂到 WT_FEATURE_MAM_RESEARCH 总闸上：闸开=注册这一棵三合一树（15 方块 / 60 节点），
    //   闸关=仍旧返回空。两处必须同开同关 —— 只翻闸门不返回树 = MAM 里依旧什么都没有；
    //   只返回树不开闸门 = 树在、节点点不了（schematic 没进 mSchematics）。
    TArray<TSubclassOf<UFGResearchTree>> Out;
    if (WT_FEATURE_MAM_RESEARCH)
    {
        // ★2026-11-07：返回资产类（而非 C++ 类）—— MAM 只给资产树画连线。
        Out.Add(WT_GetTreeClass());
    }
    return Out;
}

void WT_BuildResearchTrees()
{
    // ★★★2026-11-06 崩溃根治（注册期 AV @0x1e30b0328）：
    //   旧实现用函数内 `static bool bBuilt` 让整棵树在**整个进程内只建一次**。结果第二次进图
    //   （再加载存档/返回后重进）时这里直接 return，树 CDO 的 mNodes 仍指向上一张地图残留的节点；
    //   世界重载后这些指针已失效（内存被复用），而 SML 在 INITIALIZATION 的
    //   AutoRegisterResearchTreeReferences() 会**无任何保护地**遍历 mNodes 调 GetNodeSchematic()
    //   → FindFunctionChecked → GetClass() → EXCEPTION_ACCESS_VIOLATION（实测 0x1e30b0328）。
    //   同时 WT_Blocks() 也会残留在上一会话的悬空节点上，与 mNodes 失步。
    //   故：每次进图（CONSTRUCTION）都必须重建，绝不做进程级缓存。
    UWTResearchTreeBase* CDO = Cast<UWTResearchTreeBase>(WT_GetTreeClass()->GetDefaultObject());
    if (!CDO)
    {
        WT_WARNF(TEXT("[research-tree] merged tree CDO missing"));
        return;
    }

    // ★★★2026-11-07 第二次换档崩溃根治（必须在本世界取节点之前）：
    //   `GWT_LinkTplSrc` 指向上一个世界里的原版 MAM 节点实例，世界一换即悬空；
    //   而 `IsValidLowLevelFast()+IsA` 对「槽位被同类型对象复用」会误判通过（见函数上方长注释），
    //   导致用指向别人的旧指针读 Parents → Assert IsValidLowLevel（日志前置 Other object in slot）。
    //   这里在每个世界 CONSTRUCTION 建树前无条件让模板缓存失效，强制本世界重取。
    WT_ResetLinkTemplateCache();

    // ★★★2026-11-07 root 节点泄漏修复：上一世界 `WT_MakeTile/WT_MakeFinalTile` 对每个节点
    //   `AddToRoot()` 但从不 `RemoveFromRoot`，每进一次世界泄漏 14×4=56 个常驻 UObject（对象表只增不减）。
    //   进新世界 CONSTRUCTION 时，上一世界的节点因 AddToRoot 仍存活（防 GC），此处先 RemoveFromRoot 释放，
    //   再 Reset 让本世界重建；ConfigureMergedTree 同帧内重塞 mNodes，二者之间无 GC 间隙 ⇒ 不会悬空。
    //   （注意：旧树 CDO 的 mNodes 仍指向这些旧节点，但 ConfigureMergedTree 随后 `mNodes=Nodes` 整体覆盖，
    //    丢弃旧引用；Release 与覆盖在同一同步调用内完成，无中途 GC 风险。）
    for (FWTResearchBlock& B : WT_Blocks())
    {
        for (UFGResearchTreeNode* N : B.Levels)
        {
            if (N)
            {
                N->RemoveFromRoot();
            }
        }
    }
    WT_Blocks().Reset();

    CDO->ConfigureMergedTree(
        WT_LOCTEXT("Tree_Remote_Factory", "智能工厂"),
        WT_LOCTEXT("TreeDesc_Remote_Factory",
            "Research the Smart Factory's upgrade buildings level by level in the MAM. "
            "Each level costs more than the last; the matching building scales automatically."));

    WT_LOGF(TEXT("[research-tree] built the merged MAM tree (15 tiles x 4 levels = 60 schematics)."));

    // ★ 连线诊断：把原版 vs 我们的连线字段逐字段打印，前缀 [wt-cmp]，供对照定位。
    WT_DumpConnectionComparisonOnce();
}
