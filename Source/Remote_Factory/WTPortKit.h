// ----------------------------------------------------------------------------
//  WTPortKit.h —— 圆形管道接口 + 程序化机身的运行时几何工具箱（★1.0.8AZ，作者 Monsue）
//
//  单头文件、零项目内依赖（只用 UE + GeometryScript + Satisfactory 的管道连接组件），
//  拷到任何 Satisfactory SML C++ mod 的 Source/<Module>/ 下即可复用。
//
//  ## 为什么是"运行时生成"而不是 .uasset
//    编辑器保存的 StaticMesh .uasset 是 **editor-only 数据**（BuildSettings / AssetImportData），
//    Shipping 里加载不出 render data ⇒ 散装投放无效，必须 cook 进 pak。
//    而游戏自带 GeometryFramework / GeometryCore / GeometryScriptingCore 三个 Shipping DLL
//    （一手核实：Engine/Binaries/Win64/FactoryGameSteam-Geometry*-Win64-Shipping.dll），
//    所以几何可以运行时算 ⇒ 零 cook、零资产依赖，换规格只改几个 float。
//
//  ## 接入三步（新 mod）
//    1) <Module>.Build.cs 的 PublicDependencyModuleNames 追加：
//         "GeometryFramework", "GeometryCore", "GeometryScriptingCore"
//    2) #include "WTPortKit.h"
//    3) 构造函数里建组件 + BeginPlay 里生成几何：
//
//         // ctor
//         FWTPipePortSpec Spec;
//         Spec.ConnName   = TEXT("MyPortA");
//         Spec.VisualName = TEXT("MyPortVisualA");
//         Spec.Loc        = FVector(0.f, 390.f, 100.f);
//         Spec.OutDir     = FVector(0.f, 1.f, 0.f);
//         Spec.VisualPush = 80.f;
//         WT_AddRoundPipePort(this, RootComponent, Spec);      // 连接件 + 视觉件骨架
//
//         // BeginPlay（主网格材质就绪之后）
//         if (UDynamicMeshComponent* V = WT_FindDynamicMeshCompByName(this, TEXT("MyPortVisualA")))
//         {
//             WT_BuildRoundPortGeometry(V, Spec, MyMainMeshComponent);
//         }
//
//  ## 整栋机身（可选）
//    给一个 FWTStationBodySpec（中心/半尺寸/各层厚度）就能长出一栋"结构箱 + 混凝土基座 +
//    钢檐口 + 四角立柱 + 竖向壁板"的建筑；尺寸从原版网格包围盒派生 ⇒ 占地与碰撞完全不变：
//
//         FWTStationBodySpec Body;
//         Body.Center = ...; Body.Extent = ...;              // 通常取原版网格的 bounds
//         const FWTMaterialSet Mats = WT_LoadDefaultBodyMaterials(VanillaMesh->GetMaterial(0));
//         WT_BuildStationVisual(MyBodyComp, VanillaMesh, Body, Mats);   // 顺带隐藏原版网格
//
//  ## 参数单位
//    100uu = 1m。原版普通管道外径 ≈ 1m ⇒ 通孔 Ø110 刚好套上去。
//
//  ## 已知坑（都踩过，别重走）
//    · AppendRevolvePolygon 绕 **+Z** 回转，剖面 X 就是半径（实现里 Point.X + Radius，
//      所以调用处 Radius 传 0），剖面必须 **CCW** 否则里外翻转（本文件已加有向体积自愈）。
//    · GeometryScript 的 Append* 只改 UDynamicMesh 内部，**不会**触发组件重建渲染数据 ⇒
//      每次构建完必须 NotifyMeshUpdated()，否则"几何算出来了但一个像素都不画"。
//    · Append* 是**累加**的：BeginPlay 可能因读档重跑，必须先 EditMesh{ Clear() }。
//    · 取组件一律按名匹配，别用 FindComponentByClass（TSet 无序，会拿到别的小件）。
//    · ★材质别乱借：原版机器的 Material(0) 基本都是**一张图集盖全机**（金属外壳 + 面板 +
//      发光片在同一张 BC 上），程序化 UV 是每面 0..1 铺满 ⇒ 采到图集里随机一块，
//      于是整栋箱子变成蓝/橙/灰拼色（1.0.8AY 就翻在这里，用的是管道件的 MI_InPuts）。
//      要"看起来像建筑"，必须挑**本身就是单一表面**的材质：壁板/混凝土/钢梁这三类
//      （见 WT_LoadDefaultBodyMaterials，路径都是一手从游戏 pak 清单里查出来的）。
//    · 多材质靠 FGeometryScriptPrimitiveOptions::MaterialID + Comp->SetMaterial(槽位)：
//      SetMaterial 会自动把槽位数组撑大到 index+1（BaseDynamicMeshComponent.cpp:311），
//      渲染代理按 MaterialID 分 section（MeshRenderDecomposition.cpp:56）。
// ----------------------------------------------------------------------------
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "UDynamicMesh.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "GeometryScript/MeshBooleanFunctions.h"   // ApplyMeshBoolean（方板挖圆孔）
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"            // UMaterial::GetDefaultMaterial（环板中性材质兜底）
#include "Engine/StaticMesh.h"
#include "Math/RotationMatrix.h"          // FRotationMatrix::MakeFromX/MakeFromZ（朝向对齐）
#include "FGPipeConnectionComponent.h"

// 机身三槽材质：壁板 / 结构件 / 基座。
// 分槽的意义不是好看，是**可复用**——换一栋建筑只换这三个指针，几何一行不用改。
struct FWTMaterialSet
{
    UMaterialInterface* Panel = nullptr;   // slot 0：外壳竖向壁板
    UMaterialInterface* Trim  = nullptr;   // slot 1：结构箱/立柱/檐口/圆口法兰
    UMaterialInterface* Base  = nullptr;   // slot 2：混凝土基座

    bool IsValid() const { return Panel || Trim || Base; }
};

// 带缓存的材质加载（LoadObject 会走一次资产查找，几何是每栋一次，缓存纯属防浪费）。
inline UMaterialInterface* WT_LoadCachedMaterial(const TCHAR* Path)
{
    if (!Path)
    {
        return nullptr;
    }
    static TMap<FString, UMaterialInterface*> Cache;
    const FString Key(Path);
    if (UMaterialInterface** Found = Cache.Find(Key))
    {
        return *Found;
    }
    UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, Path);
    Cache.Add(Key, Mat);
    UE_LOG(LogTemp, Warning, TEXT("[bodymesh] material %s -> %s"),
             Mat ? TEXT("OK  ") : TEXT("MISS"), Path);
    return Mat;
}

// 默认机身材质（★路径全部来自游戏 pak 清单，一手：UnrealPak -List FactoryGame-Windows.utoc）：
//   壁板 = FICSIT 橙色墙板 MI_WallSetOrange_8x4（8m×4m 一格的原版墙板，UV 天然是"一板一贴图"）
//   结构 = 立柱钢 MI_Beam_01
//   基座 = 混凝土墙板 MI_WallSetConcrete_8x4
// 顺序回落：缺壁板用混凝土，缺钢用壁板，全缺才用调用方给的兜底（一般是主网格 Material(0)）。
inline FWTMaterialSet WT_LoadDefaultBodyMaterials(UMaterialInterface* Fallback = nullptr)
{
    static bool bTried = false;
    static FWTMaterialSet Cached;
    if (!bTried)
    {
        bTried = true;
        Cached.Panel = WT_LoadCachedMaterial(
            TEXT("/Game/FactoryGame/Buildable/Building/Wall/FicsitWallSet/MI_WallSetOrange_8x4.MI_WallSetOrange_8x4"));
        Cached.Trim = WT_LoadCachedMaterial(
            TEXT("/Game/FactoryGame/Buildable/Building/Pillars/Material/MI_Beam_01.MI_Beam_01"));
        Cached.Base = WT_LoadCachedMaterial(
            TEXT("/Game/FactoryGame/Buildable/Building/Wall/ConcreteWallSet/MI_WallSetConcrete_8x4.MI_WallSetConcrete_8x4"));
        if (!Cached.Panel) { Cached.Panel = Cached.Base; }
        if (!Cached.Trim)  { Cached.Trim = Cached.Panel; }
        if (!Cached.Base)  { Cached.Base = Cached.Panel; }
    }
    FWTMaterialSet Out = Cached;
    if (!Out.Panel && Fallback)
    {
        Out.Panel = Fallback;
    }
    if (!Out.Trim && Fallback)
    {
        Out.Trim = Fallback;
    }
    if (!Out.Base && Fallback)
    {
        Out.Base = Fallback;
    }
    return Out;
}

struct FWTPipePortSpec
{
    FName   ConnName = NAME_None;                    // 连接组件名（必填）
    FName   VisualName = NAME_None;                  // 视觉件组件名（bVisual=false 可留空）
    FVector Loc = FVector::ZeroVector;               // 接口中心（相对 Parent，actor 空间）
    FVector OutDir = FVector(0.f, 1.f, 0.f);         // 管口朝外方向（Parent 空间，单位向量）
    float   Clearance = 150.f;                       // 连接间隙
    float   VisualScale = 1.f;                       // 法兰整体缩放
    float   AxisScale = 1.f;                         // ★1.0.8CH 轴向（网格本地 Y）缩放：压短鼓身外伸；1=原长 134.28
    float   VisualPush = 0.f;                        // 视觉件沿 OutDir 额外外推（贴到模型表面之外）
    bool    bVisual = true;                          // 是否需要视觉法兰

    // ---- 几何参数（uu，100uu = 1m）----
    // 1.0.8AW：几何改由**运行时程序化建模**生成（WT_BuildRoundPortGeometry），
    //   所以这几个数字就是真正的「程序配置」—— 换规格只改这里，
    //   既不需要建模软件，也不需要 cook 任何资产。
    float   OuterR = 85.f;       // 法兰半径 ⇒ Ø1.7m（原版管道 Ø1m 的常规法兰比例）
    float   InnerR = 55.f;       // 通孔半径 ⇒ Ø1.1m，接原版普通管道
    float   FlangeT = 22.f;      // 法兰厚度（凸出面板的高度）
    float   NeckR = 70.f;        // 管颈半径
    float   NeckL = 60.f;        // 管颈长度
    int32   Segments = 32;       // 圆周细分
};

// 回转体剖面（r,h 平面；h=0 = 法兰前表面，向 -h 伸进机器内部）。
//
// ★绕向必须 CCW：GeometryScript 的 AppendRevolvePolygon 头文件原文
//   "Polygon should be oriented counter-clockwise to produce a correctly-oriented shape,
//    otherwise it will be inside-out"。下面 6 点的有向面积 = +3450（编辑器实测），CCW ✓
// ★多边形 X 就是「到轴的距离」：实现里做的是 `Point.X + Radius`（MeshPrimitiveFunctions.cpp
//   AppendRevolvePolygon），所以调用处 Radius 传 0，真实半径全写在这 6 个点里。
inline void WT_MakeRoundPortProfile(const FWTPipePortSpec& Spec, TArray<FVector2D>& Out)
{
    const double R0 = Spec.InnerR;                       // 通孔半径
    const double R1 = Spec.OuterR;                       // 法兰半径
    const double RN = Spec.NeckR;                        // 管颈半径
    const double H0 = 0.0;                               // 法兰前表面
    const double H1 = -Spec.FlangeT;                     // 法兰背面
    const double H2 = -(Spec.FlangeT + Spec.NeckL);      // 管颈后端

    Out.Reset(6);
    Out.Add(FVector2D(R0, H0));   // 前面·内孔缘
    Out.Add(FVector2D(R0, H2));   // 通孔内壁（一直到底）
    Out.Add(FVector2D(RN, H2));   // 管颈后端面
    Out.Add(FVector2D(RN, H1));   // 管颈外壁
    Out.Add(FVector2D(R1, H1));   // 法兰背面
    Out.Add(FVector2D(R1, H0));   // 法兰外缘（回到前面）
}

// ----------------------------------------------------------------------------
// ★1.0.8AY 自建机身（用户 2026-10-28 定案「我要的是不是覆盖，是完全新建筑」）
//
// 为什么要换掉原版网格：`SM_CentralStorage_01` 的方形门洞是**焊在网格本体里**的，
//   SCS 里既没有独立组件也没有可隐藏的槽（一手核实见 .h 的 WTBodyMesh 注释）。
//   任何"放大圆环/加面板盖住"都属于打补丁，用户已经连否三次。
//   ⇒ 原版网格**完全不显示**，整栋外观由本函数用 GeometryScript 现场拼出来。
//
// 形体（与原版同轮廓，参数化）：结构箱 + 混凝土基座 + 钢檐口 + 四角立柱 + 竖向壁板。
//   尺寸不写死，而是从原版网格的**相对包围盒**派生（见 WT_BuildStationVisual），
//   所以放置占地、碰撞体、传送带对接点都不受影响 —— 原版网格还在，只是不可见。
//
// ★1.0.8AZ 加"壁板"这一层：1.0.8AY 只有一个光箱子 + 图集材质 ⇒ 游戏里是一团拼色。
//   壁板每块是独立小盒（离面 0..1 UV = 一整张原版墙板贴图），板缝里露出结构箱的钢色 ⇒
//   靠**几何缝隙**而不是贴图来表现"这是一栋板拼起来的建筑"，材质再怎么变都不会花。
// ----------------------------------------------------------------------------
struct FWTStationBodySpec
{
    FVector Center = FVector::ZeroVector;        // 机身中心（actor 空间）
    FVector Extent = FVector(237.f, 489.f, 215.f); // 半尺寸（= 原版网格包围盒）
    float   ShrinkXZ = 0.94f;    // 左右相对原版外框的收进量（原版外框含腿与檐）
    float   ShrinkY = 0.97f;     // 前后收进量

    // ---- 基座 / 檐口 ----
    bool    bBase = true;
    float   BaseH = 34.f;        // 混凝土基座高度
    float   BaseOut = 16.f;      // 基座外扩（踢脚）
    bool    bRoof = true;
    float   RoofH = 26.f;        // 檐口顶盖厚度
    float   RoofOut = 14.f;      // 檐口外挑（撑出阴影线）

    // ---- 竖向壁板 ----
    bool    bPanels = true;
    float   PanelT = 12.f;       // 壁板厚度
    float   PanelOut = 6.f;      // 壁板外表面凸出结构箱的量（板缝里能看到结构箱 ⇒ 暗缝）
    float   PanelGap = 7.f;      // 板缝宽度（太小会 z-fight，太大像栅栏）
    float   PanelTargetW = 800.f;// 目标板宽：原版墙板一格 8m

    // ---- 四角立柱 ----
    bool    bPillars = true;
    float   PillarW = 44.f;      // 立柱截面宽
    float   PillarOut = 12.f;    // 立柱凸出结构箱的量（必须 > PanelOut 才看得见）

    // ---- ★1.0.8BL 前脸中段开口（"拆中间"）----
    //
    // 用户 2026-10-28 定案：把原版云端站"中间那块"拆掉，塞进液体货运站台的那个缓冲罐
    // （= 液体运货箱，一手资产 `/Game/FactoryGame/Buildable/Factory/Train/Station/Mesh/
    //   SM_DockingContainerLiquid_01`，独立 StaticMesh，所以能单独拎出来）。
    //
    // 实现方式**不是**改 mesh（我们没有 Blender 也不该动 cook 资产），而是把结构箱拆成
    // 左/右/上/下/后 五块，围出一个朝 +Y 敞开的矩形凹腔：
    //   · 腔体侧壁 = 左右两块的 ±X 面；顶/底 = 上块底面 / 下块顶面；后壁 = 后块 +Y 面；
    //   · 这五块都是**实心闭合盒**，所以腔体内壁天生有面、有法线、有材质，不需要补面；
    //   · 原来那一整块结构箱在 `bBay` 为假时照旧，液体两站以外一行没变。
    bool    bBay = false;
    FVector BayCenter = FVector(0.f, 0.f, 0.f);     // 开口中心（actor 空间；Y 分量忽略）
    FVector BayHalf   = FVector(160.f, 0.f, 130.f); // 开口半尺寸（X 半宽 / Z 半高）
    float   BayDepth  = 250.f;                      // 从前表面往机内挖多深

    // ★1.0.8BL 用户 2026-10-28 定案："罐子放在云端中间的位置（把原版的中间部分挖掉只保留
    //   外面四个框体）" ⇒ 开口就要**居中且尽量大**，四边只留结构梁。基准参数按原版中央
    //   存储的尺寸给：结构箱半宽 237*0.94≈223、壳高 -181..189 ⇒ 左右梁 ≈63uu，上下梁 ≈51/59uu。

    /** 机身前表面（朝 +Y）的 actor 空间 Y —— 结构箱表面，不含壁板外凸。 */
    float FrontY() const { return Center.Y + Extent.Y * ShrinkY; }
    /** 壁板外凸量：圆口要贴的是**壁板**外表面，不是结构箱表面。 */
    float FrontOffset() const { return bPanels ? PanelOut : 0.f; }

    /** 开口是否吃掉了这一列 +Y 面壁板（壁板是整根竖列，压到开口就整根不铺，否则悬在洞上）。 */
    bool BayBlocksFrontPanel(float PanelCenterX, float PanelLen) const
    {
        return bBay && FMath::Abs(PanelCenterX - (Center.X + BayCenter.X))
                       < (BayHalf.X + PanelLen * 0.5f);
    }
};

// 追加一个轴对齐盒子。Origin=Base ⇒ 盒体沿 +Z 从 transform 位置长 Size.Z，所以要把
// transform 从"盒中心"下移半个高度。
inline void WT_AppendBodyBox(UDynamicMesh* Mesh, const FGeometryScriptPrimitiveOptions& Opts,
                             const FVector& BoxCenter, const FVector& BoxSize)
{
    FTransform Xf;
    Xf.SetTranslation(FVector(BoxCenter.X, BoxCenter.Y, BoxCenter.Z - BoxSize.Z * 0.5f));
    UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(
        Mesh, Opts, Xf, BoxSize.X, BoxSize.Y, BoxSize.Z, 1, 1, 1,
        EGeometryScriptPrimitiveOriginMode::Base, nullptr);
}

inline bool WT_BuildStationBody(UDynamicMeshComponent* Comp, const FWTStationBodySpec& Spec,
                                const FWTMaterialSet& Mats)
{
    if (!Comp)
    {
        return false;
    }
    UDynamicMesh* Mesh = Comp->GetDynamicMesh();
    if (!Mesh)
    {
        UE_LOG(LogTemp, Error, TEXT("[bodymesh] dynamic mesh component '%s' has no UDynamicMesh"), *Comp->GetName());
        return false;
    }

    // ★先清空：BeginPlay 可能因读档/重载再跑一次，Append* 是**累加**的，
    //   不清就会在一次会话里叠出两层三层几何（越玩越卡、Z-fighting）。
    Mesh->EditMesh([](UE::Geometry::FDynamicMesh3& M) { M.Clear(); });

    // 每面 0..1 铺满：壁板这一层正好等于"一整张原版墙板贴图"。
    //
    // ★槽位编号一律 >0：GeometryScript 的 primitive 实现里是
    //   `if (PrimitiveOptions.MaterialID > 0) { …SetValue(TID, MaterialID) }`
    //   （MeshPrimitiveFunctions.cpp:72）—— 传 0 时**根本不写**材质 ID 属性，只能吃默认值。
    //   默认值虽然是 0（正确），但"靠默认值"属于不可见契约：真出偏差的结果是那批三角形
    //   被当成无效材质直接丢掉 ⇒ 壁板整层消失且日志一声不响。这里全给显式 ID，
    //   槽 0 再垫一份壁板材质做兜底，把这条路彻底堵死。
    //
    //   槽 0 = 兜底（未打标的三角形）  槽 1 = 壁板  槽 2 = 结构钢  槽 3 = 基座
    auto MakeOpts = [](int32 MaterialID)
    {
        FGeometryScriptPrimitiveOptions Opts;
        Opts.UVMode = EGeometryScriptPrimitiveUVMode::ScaleToFill;
        Opts.MaterialID = MaterialID;
        return Opts;
    };

    const float HalfW = Spec.Extent.X * Spec.ShrinkXZ;          // 结构箱半宽（X）
    const float HalfD = Spec.Extent.Y * Spec.ShrinkY;           // 结构箱半深（Y）
    const float ZBottom = Spec.Center.Z - Spec.Extent.Z;
    const float ZTop    = Spec.Center.Z + Spec.Extent.Z;
    const float BaseH   = Spec.bBase ? Spec.BaseH : 0.f;
    const float RoofH   = Spec.bRoof ? Spec.RoofH : 0.f;
    const float ShellZ0 = ZBottom + BaseH;
    const float ShellZ1 = ZTop - RoofH;
    const float ShellH  = FMath::Max(ShellZ1 - ShellZ0, 10.f);
    const float ShellZC = (ShellZ0 + ShellZ1) * 0.5f;

    // ① 结构箱（Trim 钢色）：壁板缝里透出来的就是它 ⇒ 板缝自动是暗钢线
    if (!Spec.bBay)
    {
        WT_AppendBodyBox(Mesh, MakeOpts(2),
            FVector(Spec.Center.X, Spec.Center.Y, ShellZC),
            FVector(HalfW * 2.f, HalfD * 2.f, ShellH));
    }
    else
    {
        // ★1.0.8BL 带开口：拆成五块围出凹腔（几何关系见 FWTStationBodySpec::bBay 注释）。
        const float BX0 = Spec.Center.X + Spec.BayCenter.X - Spec.BayHalf.X;
        const float BX1 = Spec.Center.X + Spec.BayCenter.X + Spec.BayHalf.X;
        const float BZ0 = Spec.Center.Z + Spec.BayCenter.Z - Spec.BayHalf.Z;
        const float BZ1 = Spec.Center.Z + Spec.BayCenter.Z + Spec.BayHalf.Z;
        const float X0 = Spec.Center.X - HalfW;
        const float X1 = Spec.Center.X + HalfW;
        // 后壁位置：至少留 40uu 料，别把后壁推出机体（BayDepth 给太大时自动收）。
        const float BackY = FMath::Max(Spec.FrontY() - FMath::Max(Spec.BayDepth, 10.f),
                                       Spec.Center.Y - HalfD + 40.f);
        const float FullD = HalfD * 2.f;
        const FVector C(Spec.Center.X, Spec.Center.Y, 0.f);

        // 左右两块：夹住开口，全高全深
        const float WLeft  = FMath::Max(BX0 - X0, 0.f);
        const float WRight = FMath::Max(X1 - BX1, 0.f);
        if (WLeft > 0.f)
        {
            WT_AppendBodyBox(Mesh, MakeOpts(2), FVector(X0 + WLeft * 0.5f, C.Y, ShellZC),
                             FVector(WLeft, FullD, ShellH));
        }
        if (WRight > 0.f)
        {
            WT_AppendBodyBox(Mesh, MakeOpts(2), FVector(X1 - WRight * 0.5f, C.Y, ShellZC),
                             FVector(WRight, FullD, ShellH));
        }
        // 上块 / 下块：只占开口的 X 区间，把开口上下封成"框"
        const float HTop    = FMath::Max(ShellZ1 - BZ1, 0.f);
        const float HBottom = FMath::Max(BZ0 - ShellZ0, 0.f);
        if (HTop > 0.f)
        {
            WT_AppendBodyBox(Mesh, MakeOpts(2), FVector((BX0 + BX1) * 0.5f, C.Y, ShellZ1 - HTop * 0.5f),
                             FVector(BX1 - BX0, FullD, HTop));
        }
        if (HBottom > 0.f)
        {
            WT_AppendBodyBox(Mesh, MakeOpts(2), FVector((BX0 + BX1) * 0.5f, C.Y, ShellZ0 + HBottom * 0.5f),
                             FVector(BX1 - BX0, FullD, HBottom));
        }
        // 后块：从机体后表面一直填到 BackY ⇒ 它的 +Y 面就是腔体后壁（朝玩家）
        const float BackD = FMath::Max(BackY - (Spec.Center.Y - HalfD), 1.f);
        WT_AppendBodyBox(Mesh, MakeOpts(2),
                         FVector((BX0 + BX1) * 0.5f, (Spec.Center.Y - HalfD) + BackD * 0.5f,
                                 (BZ0 + BZ1) * 0.5f),
                         FVector(BX1 - BX0, BackD, BZ1 - BZ0));
    }

    // ② 竖向壁板：四个侧面各排一列网格（板长按 PanelTargetW 分格，板缝 PanelGap）
    if (Spec.bPanels && Spec.PanelOut > 0.f)
    {
        // 每张面：法线轴向 + 面长 + 该面的贴板方向
        struct FFace { FVector N; float Len; };
        const FFace Faces[4] = {
            { FVector(1.f, 0.f, 0.f),  HalfD * 2.f },   // +X 面：沿 Y 铺板
            { FVector(-1.f, 0.f, 0.f), HalfD * 2.f },   // -X 面
            { FVector(0.f, 1.f, 0.f),  HalfW * 2.f },   // +Y 面（正面）：沿 X 铺板
            { FVector(0.f, -1.f, 0.f), HalfW * 2.f },   // -Y 面
        };
        const float Gap = FMath::Max(Spec.PanelGap, 0.f);
        const float TargetW = FMath::Max(Spec.PanelTargetW, 100.f);
        const float PanelH = FMath::Max(ShellH - Gap * 2.f, 20.f);   // 上下各留一条缝 ⇒ 对称

        for (const FFace& F : Faces)
        {
            const float Len = F.Len;
            const int32 nCols = FMath::Max(1, FMath::CeilToInt((Len - Gap) / TargetW));
            const float PanelLen = (Len - Gap * (nCols + 1)) / float(nCols);
            if (PanelLen < 50.f)
            {
                continue;   // 太窄就别铺了（极端参数保护）
            }
            // 面所在平面 + 壁板中心到面的偏移（外表面 = 结构箱面 + PanelOut）
            const float PlaneOff = (F.N.X > 0.f ? HalfW : (F.N.X < 0.f ? -HalfW :
                                  (F.N.Y > 0.f ? HalfD : -HalfD)));
            const float CenterOff = PlaneOff + F.N.X * (Spec.PanelOut - Spec.PanelT * 0.5f)
                                             + F.N.Y * (Spec.PanelOut - Spec.PanelT * 0.5f);
            for (int32 i = 0; i < nCols; ++i)
            {
                const float Along = -Len * 0.5f + Gap * (i + 1) + PanelLen * (i + 0.5f);

                // ★1.0.8BL 前脸开了凹腔：压到开口上的整列壁板不铺（否则悬在洞口上方）。
                if (F.N.Y > 0.5f && Spec.BayBlocksFrontPanel(Spec.Center.X + Along, PanelLen))
                {
                    continue;
                }

                FVector C = FVector(Spec.Center.X, Spec.Center.Y, Spec.Center.Z);
                FVector S;
                if (FMath::Abs(F.N.X) > 0.5f)
                {
                    C.X = Spec.Center.X + CenterOff;
                    C.Y = Spec.Center.Y + Along;
                    S = FVector(Spec.PanelT, PanelLen, PanelH);
                }
                else
                {
                    C.Y = Spec.Center.Y + CenterOff;
                    C.X = Spec.Center.X + Along;
                    S = FVector(PanelLen, Spec.PanelT, PanelH);
                }
                C.Z = ShellZC;
                WT_AppendBodyBox(Mesh, MakeOpts(1), C, S);
            }
        }
    }

    // ③ 混凝土基座（略外扩，像原版机器的踢脚）
    if (Spec.bBase)
    {
        WT_AppendBodyBox(Mesh, MakeOpts(3),
            FVector(Spec.Center.X, Spec.Center.Y, ZBottom + BaseH * 0.5f),
            FVector(HalfW * 2.f + Spec.BaseOut * 2.f, HalfD * 2.f + Spec.BaseOut * 2.f, BaseH));
    }

    // ④ 钢檐口顶盖（外挑 RoofOut，形成阴影线）
    if (Spec.bRoof)
    {
        WT_AppendBodyBox(Mesh, MakeOpts(2),
            FVector(Spec.Center.X, Spec.Center.Y, ZTop - RoofH * 0.5f),
            FVector(HalfW * 2.f + Spec.RoofOut * 2.f, HalfD * 2.f + Spec.RoofOut * 2.f, RoofH));
    }

    // ⑤ 四角立柱（必须比壁板更凸才看得见，否则整根被壁板吃掉）
    if (Spec.bPillars)
    {
        const float PX = HalfW - Spec.PillarW * 0.5f + Spec.PillarOut;
        const float PY = HalfD - Spec.PillarW * 0.5f + Spec.PillarOut;
        for (int32 sx = -1; sx <= 1; sx += 2)
        {
            for (int32 sy = -1; sy <= 1; sy += 2)
            {
                WT_AppendBodyBox(Mesh, MakeOpts(2),
                    FVector(Spec.Center.X + sx * PX, Spec.Center.Y + sy * PY, ShellZC),
                    FVector(Spec.PillarW, Spec.PillarW, ShellH));
            }
        }
    }

    const UE::Geometry::FDynamicMesh3* DM = Mesh->GetMeshPtr();
    if (!DM || DM->TriangleCount() == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("[bodymesh] station body build FAILED (triangles=0)"));
        return false;
    }

    // ★多材质：SetMaterial 会自动把槽位撑大到 index+1（BaseDynamicMeshComponent.cpp:311），
    //   渲染代理按 MaterialID 分 section（MeshRenderDecomposition.cpp:56）⇒ 不必先建空槽。
    if (Mats.Panel) { Comp->SetMaterial(0, Mats.Panel); }   // 兜底（未打标三角形）
    if (Mats.Panel) { Comp->SetMaterial(1, Mats.Panel); }
    if (Mats.Trim)  { Comp->SetMaterial(2, Mats.Trim); }
    if (Mats.Base)  { Comp->SetMaterial(3, Mats.Base); }

    Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Comp->NotifyMeshUpdated();
    Comp->UpdateBounds();
    Comp->MarkRenderStateDirty();

    UE_LOG(LogTemp, Warning, TEXT("[bodymesh] station body '%s' tris=%d dims=%.0fx%.0fx%.0f frontY=%.1f panels=%d"),
             *Comp->GetName(), DM->TriangleCount(), HalfW * 2.f, HalfD * 2.f, Spec.Extent.Z * 2.f,
             Spec.FrontY() + Spec.FrontOffset(), Spec.bPanels ? 1 : 0);
    UE_LOG(LogTemp, Warning, TEXT("[bodymesh] slots=%d panel=%s trim=%s base=%s"),
             Comp->GetNumMaterials(),
             Mats.Panel ? *Mats.Panel->GetName() : TEXT("none"),
             Mats.Trim ? *Mats.Trim->GetName() : TEXT("none"),
             Mats.Base ? *Mats.Base->GetName() : TEXT("none"));
    return true;
}

// 一次性把「自建机身 + 隐藏原版网格」做完，返回机身前表面（含壁板外凸）的 actor 空间 Y，
// 圆口按它定位才不会埋进去或悬空。
// ★为什么保留并隐藏原版网格：放置占地判定、拆除框、传送带/管道对接、碰撞体全都挂在它上面。
//   删掉 = 建筑放不下去；改小 = 对接点漂移。所以只动"渲染可见性"，几何数据一个字节不改。
inline float WT_BuildStationVisual(UDynamicMeshComponent* BodyComp,
                                  UStaticMeshComponent* VanillaComp,
                                  const FWTStationBodySpec& Spec,
                                  const FWTMaterialSet& Mats)
{
    WT_BuildStationBody(BodyComp, Spec, Mats);
    if (VanillaComp)
    {
        // SetVisibility 只管渲染，不影响碰撞（碰撞看 SetCollisionEnabled）；
        // 两个开关都拨一遍：不同组件的隐藏路径不一致（proxy 与普通 SM 行为有差异）。
        VanillaComp->SetVisibility(false, true);
        VanillaComp->SetHiddenInGame(true);
    }
    return Spec.FrontY() + Spec.FrontOffset();
}

// ----------------------------------------------------------------------------
// ★1.0.8BL 液体缓冲罐：塞进前脸凹腔的那个模块
//
// 用户 2026-10-28 原话："把红框部分（差不多相当于液体缓冲罐）拆出来并横过来，塞入
// 原版云端站拆掉的部分"。大肥鱼也建议把罐做成独立组件挂 Socket —— 方向一致，但
// **挂不上去**：那个罐不是我们机身子件的资产，Socket 是原版 mesh 上的概念，
// 而我们的机身是运行时生成的（1.0.8AY 起原版 mesh 只留碰撞、不显示）。
// ⇒ 改成"按机身几何算位置"：罐挂在 Root，位置 = 凹腔中心，朝向 = 窗口法线朝 +Y。
//
// 朝向不猜：用**网格自身包围盒**判三轴角色（最薄=窗口法线 / 最长=横放长边 / 余下=竖向），
// 再用 FRotationMatrix::MakeFromXY(长边, 法线) 直接建旋转 ⇒ 换任何罐都不会躺倒或背对玩家。
// 唯一猜不了的是"法线该朝 +thin 还是 -thin"（贴图只在一面），所以日志把轴序打出来，
// 万一背对只需把 WT_TANK_FLIP_FACE 改成 true。
// ----------------------------------------------------------------------------

/** 实测后如果发现罐的窗口朝着机内（背对玩家），把这个改成 true 即可（同一行，不用改结构）。 */
#ifndef WT_TANK_FLIP_FACE
    #define WT_TANK_FLIP_FACE 0
#endif

/** 创建罐体组件（必须在构造函数里调，与现有端口小件同口径）。默认隐藏，摆好再显形。 */
inline UStaticMeshComponent* WT_AddBufferTank(AActor* Self, USceneComponent* Parent, FName CompName)
{
    if (!Self || !Parent || CompName.IsNone())
    {
        return nullptr;
    }
    UStaticMeshComponent* Comp = Self->CreateDefaultSubobject<UStaticMeshComponent>(CompName);
    if (!Comp)
    {
        return nullptr;
    }
    Comp->SetupAttachment(Parent);
    Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Comp->SetMobility(EComponentMobility::Movable);
    Comp->SetVisibility(false);
    return Comp;
}

/**
 * 把罐体怼到机身前脸（滚筒位置）（BeginPlay 里调：网格资产这时才该 Load，ctor 里 LoadObject
 * 会拖慢 CDO 构造）。
 *
 * ★1.0.8BM 朝向定案（runtime 实测 `[baytank] ext=(762,336,316)` = 15.2m×6.7m×6.3m 的**长罐**，
 *   窗口在长轴的**端面**上，不是薄轴的侧面 —— 1.0.8BL 按"薄轴=窗口法线"猜错，游戏里窗口朝侧面）：
 *   · 长轴（X）→ 世界 +Y（罐身**伸进机体**，像把液罐推进站里）；
 *   · 窗口端（±X 端面）→ 朝玩家（+Y）；
 *   · 横截面（Y×Z）→ 世界 X×Z（前脸的高宽）；
 *   · 就是一个 `FRotator(0, ±90, 0)`（绕 Z 转 90°：+X→+Y，Z 保持向上），不用包围盒猜轴。
 *
 * ★居中必须用**包围盒原点**：网格原点不一定在几何中心（1.0.8BL 没用原点，罐整个探出墙面
 *   就是它的锅）。`CompLoc = 目标包围盒中心 - Rot*(Origin*Scale)`。
 *
 * @param FaceCenter 罐面前脸的中心（actor 空间）；Z 取滚筒中心（原版雾面片在 Z≈100）
 * @param FaceHalfW  罐面目标半宽：按"盖住滚筒（Ø3.0m）"给，不要按机身宽给
 * @param Protrude   罐面凸出机面前表面的量（uu）
 * @return 罐面前表面的 actor 空间 Y（失败时回 FaceCenter.Y）
 */
inline float WT_FitBufferTank(UStaticMeshComponent* Comp,
                              const FVector& FaceCenter, float FaceHalfW, float Protrude)
{
    if (!Comp)
    {
        return FaceCenter.Y;
    }

    // 一手资产路径（游戏 pak 清单里实测存在，含 BC/N/REFL 贴图，是独立 StaticMesh 不是子件）：
    //   ../../../FactoryGame/Content/FactoryGame/Buildable/Factory/Train/Station/Mesh/SM_DockingContainerLiquid_01.uasset
    UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr,
        TEXT("/Game/FactoryGame/Buildable/Factory/Train/Station/Mesh/SM_DockingContainerLiquid_01.SM_DockingContainerLiquid_01"));
    if (!Mesh)
    {
        UE_LOG(LogTemp, Error, TEXT("[baytank] buffer tank mesh MISSING -> tank not shown"));
        return FaceCenter.Y;
    }
    if (Comp->GetStaticMesh() != Mesh)
    {
        Comp->SetStaticMesh(Mesh);
    }

    const FBoxSphereBounds B = Mesh->GetBounds();
    const FVector E = B.BoxExtent;
    if (!(E.X >= E.Y && E.X >= E.Z))
    {
        // 换网格后长轴不在 X ⇒ 朝向逻辑要重推（不要静默转错）。
        UE_LOG(LogTemp, Error,
            TEXT("[baytank] mesh long axis is NOT +X (ext=%.0f,%.0f,%.0f) -- orientation needs revisit"),
            E.X, E.Y, E.Z);
    }

    // 绕 Z 转 90°：+X→+Y（窗口端朝外）；窗口在 +X 端还是 -X 端猜不了 ⇒ 一个开关翻面。
    const FRotator Rot(0.f, WT_TANK_FLIP_FACE ? -90.f : 90.f, 0.f);

    // 缩放：把横截面（Y×Z）压进 FaceHalfW 见方（罐身长度不参与缩放决策，反正伸进机体）。
    const float Scale = FaceHalfW / FMath::Max(1.f, FMath::Max(E.Y, E.Z));

    // 目标：包围盒**前表面**（+Y 端）在 FaceCenter.Y + Protrude，中心高 FaceCenter.Z。
    const FVector RelCenter = Rot.RotateVector(B.Origin) * Scale;   // 盒心相对组件原点
    const float HalfLen = E.X * Scale;                              // 罐身半长（沿 +Y）
    const FVector CompLoc = FVector(FaceCenter.X, FaceCenter.Y + Protrude, FaceCenter.Z)
                            - RelCenter - FVector(0.f, HalfLen, 0.f);

    Comp->SetRelativeRotation(Rot);
    Comp->SetRelativeScale3D(FVector(Scale));
    Comp->SetRelativeLocation(CompLoc);
    Comp->SetVisibility(true, true);
    Comp->SetHiddenInGame(false);

    const float TankFrontY = FaceCenter.Y + Protrude;
    UE_LOG(LogTemp, Warning,
        TEXT("[baytank] '%s' mesh=%s ext=(%.0f,%.0f,%.0f) origin=(%.0f,%.0f,%.0f) scale=%.3f rot=%s loc=%s frontY=%.0f face=%.0fx%.0f mats=%d"),
        *Comp->GetName(), *Mesh->GetName(), E.X, E.Y, E.Z,
        B.Origin.X, B.Origin.Y, B.Origin.Z, Scale, *Rot.ToCompactString(),
        *CompLoc.ToCompactString(), TankFrontY,
        2.f * E.Y * Scale, 2.f * E.Z * Scale, Mesh->GetStaticMaterials().Num());
    for (int32 i = 0; i < Mesh->GetStaticMaterials().Num(); ++i)
    {
        UE_LOG(LogTemp, Warning, TEXT("[baytank]   slot%d = %s"), i,
               Mesh->GetMaterial(i) ? *Mesh->GetMaterial(i)->GetName() : TEXT("none"));
    }
    return TankFrontY;
}

/**
 * 把发光液位窗（SM_Freight_Fuel）作为**子件**挂到缓冲罐容器下，沿用货运站台的真实装配：
 * 相对容器局部 (0,0,-238)（一手探针 `_wt_probe_platform*.py` 实锤的"罐 + 窗"相对变换），
 * 继承容器的旋转/缩放 ⇒ 不另算朝向/缩放，天然对齐。
 *
 * ★1.0.8BM 漏了这一步 ⇒ 用户实测「我要的是液体缓冲罐」—— 只有容器、缺窗。
 * 窗是子件，所以只需给局部变换；窗自带 MI_Freight_Fuel（发光液位指示），不用我们补材质。
 *
 * @param Win  液位窗组件（ctor 建，挂在 Tank 下）
 * @param Tank 容器组件（已 WT_FitBufferTank 摆好）
 */
inline void WT_FitBufferWindow(UStaticMeshComponent* Win, UStaticMeshComponent* Tank)
{
    if (!Win || !Tank || !Tank->GetStaticMesh())
    {
        return;
    }
    // 一手资产路径（游戏 pak 清单实测存在：Buildable/Vehicle/Train/Wagon/Mesh/SM_Freight_Fuel）：
    UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr,
        TEXT("/Game/FactoryGame/Buildable/Vehicle/Train/Wagon/Mesh/SM_Freight_Fuel.SM_Freight_Fuel"));
    if (!Mesh)
    {
        UE_LOG(LogTemp, Error, TEXT("[baytank] buffer window mesh MISSING -> window not shown"));
        return;
    }
    if (Win->GetStaticMesh() != Mesh)
    {
        Win->SetStaticMesh(Mesh);
    }
    // 子件局部变换：相对容器 (0,0,-238)，继承容器旋转/缩放（不另给）。
    Win->SetRelativeLocation(FVector(0.f, 0.f, -238.f));
    Win->SetRelativeRotation(FRotator::ZeroRotator);
    Win->SetRelativeScale3D(FVector(1.f));
    Win->SetVisibility(true, true);
    Win->SetHiddenInGame(false);
    UE_LOG(LogTemp, Warning,
        TEXT("[baytank] window '%s' mesh=%s (child of tank, localZ=-238, inherits tank rot/scale)"),
        *Win->GetName(), *Mesh->GetName());
}

// 圆口法兰的材质：优先用调用方给的（自建机身就用机身 Trim 钢色，天生一套），
// 否则退到原版管道接口网格的材质，最后才退到主网格 Material(0)。
//
// 为什么不用主网格的 Material(0)：中央存储的 MainMesh 是「一张图集盖全机」的材质
// （金属外壳 + 正面深色面板 + 金条都在同一张图上），我们程序化生成的 UV 是 0..1 铺满，
// 采到的是图集里那块**深色面板**区域 ⇒ 法兰整块发黑、没有光影，看起来"是张平面贴图"
// （用户 2026-10-28 原话："新塞的接口材质变成平面了"）。
inline UMaterialInterface* WT_GetRoundPortMaterial(const UMeshComponent* MaterialSource,
                                                   UMaterialInterface* Preferred = nullptr)
{
    if (Preferred)
    {
        return Preferred;
    }
    static bool bTried = false;
    static UMaterialInterface* Cached = nullptr;
    if (!bTried)
    {
        bTried = true;
        if (UStaticMesh* PipeMesh = LoadObject<UStaticMesh>(nullptr,
                TEXT("/Game/FactoryGame/Buildable/Factory/FactoryInputs/Mesh/SM_Pipe_InPut_01.SM_Pipe_InPut_01")))
        {
            Cached = PipeMesh->GetMaterial(0);
        }
        UE_LOG(LogTemp, Warning, TEXT("[portmesh] pipe-port material = %s"),
                 Cached ? *Cached->GetName() : TEXT("<not found, will fall back to main mesh>"));
    }
    if (Cached)
    {
        return Cached;
    }
    if (MaterialSource && MaterialSource->GetNumMaterials() > 0)
    {
        return MaterialSource->GetMaterial(0);
    }
    return nullptr;
}

// 运行时生成圆口几何（**只能在 BeginPlay 之后调**，ctor 期不碰网格）。
//
// 几何（局部空间：原点 = 法兰前表面中心，+Z 朝外）：
//   AppendRevolvePolygon 回转体，Z ∈ [-(FlangeT+NeckL), 0]
//   法兰外缘 = OuterR（Ø1.7m），通孔 = InnerR（Ø1.1m，接原版普通管道），
//   后面接一段 NeckR 管颈伸进机体，这样从侧面任何角度都看不到"贴片"。
//
// 一手依据（编辑器实测，脚本 C:/SMLDev/probe_revolve.py）：
//   AppendRevolvePolygon 绕 **+Z** 回转，CCW 剖面 [(55,0)…(145,0)] ⇒ 包围盒 (±R,±R,[-h,0])，
//   与「+Z 朝外、原点在法兰前面」的约定一致，所以装配时用 MakeFromZ(OutDir) 直接对齐。
inline bool WT_BuildRoundPortGeometry(UDynamicMeshComponent* Comp, const FWTPipePortSpec& Spec,
                                     const UMeshComponent* MaterialSource,
                                     UMaterialInterface* MatOverride = nullptr)
{
    if (!Comp)
    {
        return false;
    }
    UDynamicMesh* Mesh = Comp->GetDynamicMesh();
    if (!Mesh)
    {
        UE_LOG(LogTemp, Error, TEXT("[portmesh] dynamic mesh component '%s' has no UDynamicMesh"), *Comp->GetName());
        return false;
    }

    // ★读档/重载会重跑 BeginPlay，Append* 是累加的 ⇒ 先清空
    Mesh->EditMesh([](UE::Geometry::FDynamicMesh3& M) { M.Clear(); });

    TArray<FVector2D> Profile;
    WT_MakeRoundPortProfile(Spec, Profile);

    FGeometryScriptPrimitiveOptions PrimOpts;      // 默认 UV/分组
    FGeometryScriptRevolveOptions RevolveOpts;     // 默认 360°、无硬边
    UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendRevolvePolygon(
        Mesh, PrimOpts, FTransform::Identity, Profile, RevolveOpts,
        0.f /* Radius：真实半径已写在剖面里 */, FMath::Max(Spec.Segments, 8), nullptr);

    const UE::Geometry::FDynamicMesh3* DM = Mesh->GetMeshPtr();
    if (!DM || DM->TriangleCount() == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("[portmesh] round port geometry build FAILED (triangles=0)"));
        return false;
    }
    // ★法线朝向自愈：回转体是否「外翻」只取决于剖面绕向（文档要求 CCW，但约定不该被信任）。
    //   算一遍有向体积（标准约定：正 = 外向法线），为负就翻转 ——
    //   否则法兰会因背面剔除而"看不见"，这种 bug 在游戏里极难判读。
    double SignedVolume = 0.0;
    for (int32 Tid : DM->TriangleIndicesItr())
    {
        const auto Tri = DM->GetTriangle(Tid);
        const auto PA = DM->GetVertex(Tri.A);
        const auto PB = DM->GetVertex(Tri.B);
        const auto PC = DM->GetVertex(Tri.C);
        SignedVolume += PA.Dot(PB.Cross(PC)) / 6.0;
    }
    if (SignedVolume < 0.0)
    {
        Mesh->EditMesh([](UE::Geometry::FDynamicMesh3& M) { M.ReverseOrientation(); });
        DM = Mesh->GetMeshPtr();
        UE_LOG(LogTemp, Warning, TEXT("[portmesh] round port was inside-out (vol=%.0f) -> orientation reversed"),
                 SignedVolume);
        SignedVolume = -SignedVolume;
    }

    const auto Bounds = DM->GetBounds();   // FAxisAlignedBox3d（在 UE::Geometry 内，故用 auto）

    UMaterialInterface* Mat = WT_GetRoundPortMaterial(MaterialSource, MatOverride);
    if (Mat)
    {
        Comp->SetMaterial(0, Mat);
    }
    Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    // ★无条件通知：GeometryScript 的 Append* 直接改 UDynamicMesh 内部，不会自己触发
    //   组件的 render-data 重建；不通知就可能"几何算出来了但一个像素都不画"。
    Comp->NotifyMeshUpdated();
    Comp->UpdateBounds();
    Comp->MarkRenderStateDirty();

    UE_LOG(LogTemp, Warning, TEXT("[portmesh] runtime round port '%s' tris=%d vol=%.0f bounds=%s..%s"),
             *Comp->GetName(), DM->TriangleCount(), SignedVolume,
             *Bounds.Min.ToString(), *Bounds.Max.ToString());
    UE_LOG(LogTemp, Warning, TEXT("[portmesh] visual '%s' mat=%s slots=%d world=%s extent=%s"),
             *Comp->GetName(), Mat ? *Mat->GetName() : TEXT("none"), Comp->GetNumMaterials(),
             *Comp->GetComponentLocation().ToString(),
             *Comp->Bounds.BoxExtent.ToString());
    return true;
}

// 按 Spec 摆放圆口视觉件（**ctor 与 BeginPlay 各调一次**）。
//
// ★为什么必须单独抽出来：组件位置在 ctor 里就定死了，而"外推多少"往往要等 BeginPlay
//   量到主网格/自建机身的真实前表面才知道。只在 BeginPlay 改 Spec.VisualPush 是**完全无效**的
//   —— 那只改了一个局部变量，组件位置一个字节都没动，结果就是"算出来的口在 A 处、实际画在 B 处"。
//   本轮（1.0.8AY）就踩过：机身正面 Y≈498，口还停在 470，整圈埋进机身看不见。
inline void WT_PlaceRoundPipePort(UMeshComponent* Comp, const FWTPipePortSpec& Spec)
{
    if (!Comp)
    {
        return;
    }
    // 几何约定：+Z 朝外、原点在法兰前面 ⇒ 让 +Z 对齐 OutDir，再沿 OutDir 外推。
    Comp->SetRelativeLocation(Spec.Loc + Spec.OutDir * Spec.VisualPush);
    Comp->SetRelativeRotation(FRotationMatrix::MakeFromZ(Spec.OutDir).Rotator());
    Comp->SetRelativeScale3D(FVector(Spec.VisualScale, Spec.VisualScale, Spec.VisualScale));
}

// ---------------------------------------------------------------------------
//  cooked StaticMesh 路线（1.0.8BA 起）
//
//  为什么现在能做：cook 通道已打通（脚本 cook_sf_assets.py），产出的
//  SM_PipePort_01.uasset/.uexp/.ubulk 带**真正的 cooked render data**，Shipping 能渲染。
//
//  ★包名根是插件名 SmartFactory（uplugin 文件名），不是目录名 Smart_Factory
//    —— 一手取证：DevelopmentAssetRegistry.bin 里逐字是
//    `/SmartFactory/PortMesh/SM_PipePort_01`。目录名只是 Mods/ 下的文件夹名。
// ---------------------------------------------------------------------------

// 尝试加载 cook 好的圆口静态网格；找不到就返回 nullptr（调用方回退程序化几何）。
inline UStaticMesh* WT_TryLoadCookedPortMesh(const TCHAR** OutUsedPath = nullptr)
{
    // 新根在前、旧根兜底（改名后保留旧根的既有铁律）
    static const TCHAR* Candidates[] =
    {
        TEXT("/SmartFactory/PortMesh/SM_PipePort_01.SM_PipePort_01"),
        TEXT("/Smart_Factory/PortMesh/SM_PipePort_01.SM_PipePort_01"),
        TEXT("/Remote_Factory/PortMesh/SM_PipePort_01.SM_PipePort_01"),
    };
    for (const TCHAR* Path : Candidates)
    {
        if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Path))
        {
            if (OutUsedPath)
            {
                *OutUsedPath = Path;
            }
            return Mesh;
        }
    }
    return nullptr;
}

// 用 cook 好的静态网格顶替程序化几何（BeginPlay 里调）。
//
// ★只在 BeginPlay 调：ctor 阶段资产系统未必就绪，且 CreateDefaultSubobject 之后
//   再想"换个组件类型"是不可能的。所以策略是——ctor 照旧建 DynamicMeshComponent，
//   BeginPlay 里若 cooked 网格可用，另建一个 UStaticMeshComponent 顶上去、
//   把程序化件隐藏掉。这样两条路互不干扰，cooked 失败时画面照常。
//
// 返回 true = 已用 cooked 网格；false = 保持程序化几何。
inline bool WT_UseCookedPortMesh(AActor* Self, UMeshComponent* Programmatic,
                                 const FWTPipePortSpec& Spec, UMaterialInterface* MatOverride = nullptr)
{
    if (!Self || !Programmatic)
    {
        return false;
    }
    const TCHAR* UsedPath = nullptr;
    UStaticMesh* Cooked = WT_TryLoadCookedPortMesh(&UsedPath);
    if (!Cooked)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[portmesh] cooked mesh NOT found -- keeping procedural geometry (paths tried: /SmartFactory/ /Smart_Factory/ /Remote_Factory/)"));
        return false;
    }

    UStaticMeshComponent* SMC = NewObject<UStaticMeshComponent>(
        Self, MakeUniqueObjectName(Self, UStaticMeshComponent::StaticClass(), TEXT("WTCookedPort")));
    if (!SMC)
    {
        // ★顺序铁律（1.0.8BA 深度自检抓到的）：隐藏程序化件**必须放在替身建出来之后**。
        //   先隐藏再 NewObject，一旦 NewObject 失败就是「画面空窗 + 日志一声不响」，
        //   比"没换成 cooked"严重得多。这里直接返回，程序化几何照常显示。
        return false;
    }
    SMC->SetMobility(EComponentMobility::Movable);
    SMC->SetStaticMesh(Cooked);
    SMC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SMC->RegisterComponent();

    // 挂到程序化件的父级上，再按同一套 Spec 摆放（位置口径与 1.0.8AY 完全一致）
    if (USceneComponent* Parent = Programmatic->GetAttachParent())
    {
        if (SMC->GetAttachParent() != Parent)
        {
            SMC->AttachToComponent(Parent, FAttachmentTransformRules::KeepRelativeTransform);
        }
    }
    WT_PlaceRoundPipePort(SMC, Spec);

    // 替身已就位 —— 现在才让程序化件退居幕后（仅实例级，CDO 不动）
    Programmatic->SetVisibility(false, true);
    Programmatic->SetHiddenInGame(true);

    // 材质：cooked 网格自带的若为空，就用机身那套结构钢（游戏内置 MI，mod pak 不带 shader）
    UMaterialInterface* Mat = MatOverride;
    if (!Mat)
    {
        Mat = WT_GetRoundPortMaterial(Programmatic, nullptr);
    }
    if (Mat)
    {
        SMC->SetMaterial(0, Mat);
    }

    // ★日志三个量合起来才能判断"位置对不对"：
    //   origin = 网格本地包围盒中心（判断网格原点是否落在法兰面 —— 程序化几何的约定是
    //            原点在法兰前面、+Z 朝外；cooked 网格若不同，就得在 Spec 里补偏移）
    //   extent = 半尺寸（判断尺度是否 1:1 对上，差多少就乘多少）
    //   loc    = 摆好之后的相对位置（判据：应与程序化件同值，即 Y=390+push）
    const FBoxSphereBounds B = Cooked->GetBounds();
    UE_LOG(LogTemp, Warning,
        TEXT("[portmesh] cooked mesh '%s' IN USE from %s | tris=%d mat=%s origin=%s extent=%s loc=%s"),
        *Cooked->GetName(), UsedPath ? UsedPath : TEXT("?"),
        Cooked->GetNumTriangles(0),
        Mat ? *Mat->GetName() : TEXT("none"),
        *B.Origin.ToCompactString(), *B.BoxExtent.ToCompactString(),
        *SMC->GetRelativeLocation().ToCompactString());
    return true;
}

// ---------------------------------------------------------------------------
// ★1.0.8BU：直接移植原版管道接口网格 SM_Pipe_InPut_01（彻底删掉手搓回转体 + 方形背板）。
//
// 一手依据（用户 2026-10-29 定案「法线没找对 / 别自制，移植原版的」）：
//   · SM_Pipe_InPut_01 = 粒子加速器的粗管法兰（Ø2m、内孔≈1.5m）：自带正确法线 + 原版材质，
//     Shipping 直接渲染，永不再出现「法线翻转 / 朝向错 / 借图集拼色」这类手搓翻车。
//   · ★★建模轴向（1.0.8BU 实机 bounds 实锤，推翻 BS 的「+X」与 BT 的「+Z」两次猜测）：
//       bounds = X ±99.82 / Z ±99.82 / Y [-83.79, +50.49] ⇒ X/Z 构成 Ø2m 圆截面、Y 只 134 长
//       ⇒ 回转轴 = **本地 +Y**；管颈/鼓身在 +Y 端，法兰安装面 = 本地 -Y 端（Y=-83.79）。
//       （BS 的 MakeFromX 把 +Y 送向侧方、BT 的 MakeFromZ 把 +Y 送向上方 —— 两次截图里
//       「鼓身不朝外」全是同一个根因：轴是 +Y，谁都没把它指向门外。）
//   · 摆放：MakeFromY(OutDir)（本地 +Y 管颈 → 门外）+ loc 沿 OutDir 前移 |FaceMinY|=83.79，
//     让法兰安装面正好贴住开口面（wallY 平面）、管颈向门外伸出 134。无需 VisualPush。
//   · 若实机再看反（法兰面朝外、管颈埋墙）= 颈其实在 -Y 端：改 MakeFromY(-Spec.OutDir) 且
//     loc 偏移取 +(B.Origin.Y + B.BoxExtent.Y)（各一行）。
//
// 返回创建好的 UStaticMeshComponent*（"WTVanillaPipePort"）= 已用原版网格（隐藏了手搓程序化件）；
// 返回 nullptr = 原版网格缺失，保底显示程序化件。
// ★★调用方约定（1.0.8BX）：**直接拿返回的指针**读 bounds / 摆连接件锚点，绝不要再按名
//   WT_FindMeshCompByName 去捞刚创建的组件 —— 该函数用 MakeUniqueObjectName 建名，读档重载后
//   名字可能带数字后缀（如 WTVanillaPipePort1），同作用域立刻按名查找会 miss，1.0.8BW 因此踩过
//   dist=0（锚点没挪、管子够不到法兰口面）。返回值非空即成功，null 即回退路径。
inline UStaticMeshComponent* WT_UseVanillaPipePortMesh(AActor* Self, UMeshComponent* Programmatic,
                                                     const FWTPipePortSpec& Spec)
{
    if (!Self || !Programmatic)
    {
        return nullptr;
    }
    UStaticMesh* Vanilla = LoadObject<UStaticMesh>(nullptr,
        TEXT("/Game/FactoryGame/Buildable/Factory/FactoryInputs/Mesh/SM_Pipe_InPut_01.SM_Pipe_InPut_01"));
    if (!Vanilla)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[portmesh] 1.0.8BS SM_Pipe_InPut_01 NOT found -- keeping procedural geometry as fallback"));
        return nullptr;
    }

    UStaticMeshComponent* SMC = NewObject<UStaticMeshComponent>(
        Self, MakeUniqueObjectName(Self, UStaticMeshComponent::StaticClass(), TEXT("WTVanillaPipePort")));
    if (!SMC)
    {
        return nullptr;
    }
    SMC->SetMobility(EComponentMobility::Movable);
    SMC->SetStaticMesh(Vanilla);
    SMC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SMC->RegisterComponent();

    // 挂到程序化件的父级（= RootComponent），再按开口中心摆放。
    if (USceneComponent* Parent = Programmatic->GetAttachParent())
    {
        if (SMC->GetAttachParent() != Parent)
        {
            SMC->AttachToComponent(Parent, FAttachmentTransformRules::KeepRelativeTransform);
        }
    }
    // ★1.0.8BU：轴 = 本地 +Y。管颈/鼓身在 +Y 端，法兰安装面 = 本地 -Y 端（bounds Y min）。
    //   MakeFromY(OutDir)：本地 +Y(管颈) → 门外；loc 沿 OutDir 前移 |FaceMinY| 让安装面贴住开口面。
    const FBoxSphereBounds B = Vanilla->GetBounds();
    const float FaceMinY = B.Origin.Y - B.BoxExtent.Y;              // 法兰安装面（本地 -Y 端 ≈ -83.79）
    // ★1.0.8CH：轴向（本地 Y）缩放 —— 鼓身外伸长度 ×AxisScale。CG 圆盘探深实锤：门洞中带
    //   （圆口 Ø200 足迹内）背后是 ≤60uu 的原版机芯浅面（bBack=false、足迹内零深格）⇒ 退距到头
    //   （face=385 已贴住，再深必 clip 机芯），口面要再往里缩只能压短鼓身。
    //   SetRelativeScale3D 的分量对应**网格本地轴**（旋转前生效）⇒ 本地 Y 分量缩放恒沿鼓身轴，
    //   与 MakeFromY 摆放姿态无关。★安装面不变量：loc 偏移同步乘 AxisScale ⇒ 法兰面仍精确落
    //   在 Spec.Loc.Y（分量数学：origin = Loc - OutDir*(FaceMinY*s)，面 = origin + OutDir*FaceMinY*s = Loc）。
    SMC->SetRelativeScale3D(FVector(1.f, Spec.AxisScale, 1.f));
    SMC->SetRelativeLocation(Spec.Loc - Spec.OutDir * (FaceMinY * Spec.AxisScale));
    SMC->SetRelativeRotation(FRotationMatrix::MakeFromY(Spec.OutDir).Rotator());

    // 材质用网格自带的（原版，法线/光影正确）—— 不覆盖、不借主网格图集。
    Programmatic->SetVisibility(false, true);
    Programmatic->SetHiddenInGame(true);

    UE_LOG(LogTemp, Warning,
        TEXT("[portmesh] 1.0.8BS vanilla SM_Pipe_InPut_01 IN USE | 1.0.8BU axis=local+Y | 1.0.8CH axisScale=%.2f faceY=%.2f origin=%s extent=%s loc=%s rot=%s"),
        Spec.AxisScale, FaceMinY,
        *B.Origin.ToCompactString(), *B.BoxExtent.ToCompactString(),
        *SMC->GetRelativeLocation().ToCompactString(), *SMC->GetRelativeRotation().ToCompactString());
    return SMC;
}

// 建圆口视觉件的组件骨架（ctor 里调用，只摆位置，不建几何）。
// 改用 UDynamicMeshComponent 的原因见文件顶部 1.0.8AW 说明：
//   编辑器保存的 .uasset 在 Shipping 里加载不出 render data，只能 cook；
//   而几何在运行时算，就彻底不需要 cook，也让「换规格」变成改几个 float。
inline UDynamicMeshComponent* WT_CreateRoundPipePortVisual(
    AActor* Self, USceneComponent* Parent, const FWTPipePortSpec& Spec)
{
    if (!Self || !Parent || Spec.VisualName.IsNone())
    {
        return nullptr;
    }
    UDynamicMeshComponent* Comp =
        Self->CreateDefaultSubobject<UDynamicMeshComponent>(Spec.VisualName);
    if (!Comp)
    {
        return nullptr;
    }
    Comp->SetupAttachment(Parent);
    Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    WT_PlaceRoundPipePort(Comp, Spec);
    return Comp;
}

// 按名字找 UDynamicMeshComponent（同 WT_FindMeshCompByName 的理由：组件集合无序）。
inline UDynamicMeshComponent* WT_FindDynamicMeshCompByName(const AActor* Self, const TCHAR* CompName)
{
    if (!Self || !CompName)
    {
        return nullptr;
    }
    TInlineComponentArray<UDynamicMeshComponent*> Comps(Self);
    for (UDynamicMeshComponent* C : Comps)
    {
        if (C && C->GetName() == CompName)
        {
            return C;
        }
    }
    return nullptr;
}

// ----------------------------------------------------------------------------
// ★1.0.8BQ 方形背板：堵死原版方形开口里「内切圆盖不住的四角」。
//
// 用户 2026-10-29 定案两句话合起来的必然推论：
//   「以方形口四条边中心点定一个圆 = 液体输入口」＋「我要看不见这个方形口」
//   ⇒ 内切圆与方口之间天然剩四个角洞，必须有一块**比开口大一圈的方形背板**垫在圆后面。
//
// 为什么不把背板 AppendBox 进圆口的回转体网格里：圆口视觉件的局部轴是
//   `FRotationMatrix::MakeFromZ(OutDir)` 生成的，其 X/Y 轴对 (0,1,0) 朝向的映射
//   不可靠（引擎按"最不对齐轴"挑正交基）⇒ 长方形板会转 90°。背板独立成组件、
//   **恒等旋转**（actor 轴对齐），长宽语义就再也不含糊。
//
// 背板前表面落在 Center.Y - 2（比门面平面缩 2uu）：既不与墙面 z-fight，也不悬进开口里。
// 读档重跑 BeginPlay 幂等：先按名找旧组件复用（网格会重建，Append 前已 Clear）。
//
// @param Center 开口中心（actor 空间；Y = 门面平面）
// @param HalfX/HalfZ 背板半宽/半高（调用方给"开口半尺寸 + 余量"）
// @param Thick 板厚（沿 -Y 从门面往机内长）
inline UDynamicMeshComponent* WT_AddOpeningBackPlate(AActor* Self, USceneComponent* Parent,
    const TCHAR* CompName, const FVector& Center, float HalfX, float HalfZ,
    float Thick, UMaterialInterface* Mat)
{
    if (!Self || !Parent || !CompName)
    {
        return nullptr;
    }
    // 幂等：读档重跑 BeginPlay 时按名复用旧组件，避免 NewObject 越积越多。
    UDynamicMeshComponent* Comp = WT_FindDynamicMeshCompByName(Self, CompName);
    if (!Comp)
    {
        Comp = NewObject<UDynamicMeshComponent>(Self, CompName);
        if (!Comp)
        {
            return nullptr;
        }
        Comp->SetMobility(EComponentMobility::Movable);
        Comp->SetupAttachment(Parent);
        Comp->RegisterComponent();
    }
    Comp->SetRelativeRotation(FRotator::ZeroRotator);
    // ★Y 定位：AppendBox 的 Base 模式只对 Z 生效，Y 是以组件原点为中心 ⇒
    //   要让板前表面 = Center.Y - 2，组件原点须在 Center.Y - Thick*0.5 - 2。
    //   （自检抓出的 bug：直接写 Center.Y - 2 会让板凸出门面 Thick*0.5 - 2。）
    Comp->SetRelativeLocation(FVector(Center.X, Center.Y - Thick * 0.5f - 2.f,
                                      Center.Z - HalfZ - 4.f));
    Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    UDynamicMesh* Mesh = Comp->GetDynamicMesh();
    if (!Mesh)
    {
        UE_LOG(LogTemp, Error, TEXT("[portmesh] back plate '%s' has no UDynamicMesh"), CompName);
        return nullptr;
    }
    Mesh->EditMesh([](UE::Geometry::FDynamicMesh3& M) { M.Clear(); });

    // AppendBox OriginMode::Base = 盒底在 transform 原点沿 +Z 长（引擎一手已核：
    // MeshPrimitiveFunctions.cpp:246）⇒ transform 放在盒底：Z = Center.Z - HalfZ - 4（见上）。
    // X/Z 方向各比开口大 4uu 余量；槽位 MaterialID 必须 >0（=0 引擎不写入，见 1.0.8AY 教训）。
    FGeometryScriptPrimitiveOptions Opts;
    Opts.UVMode = EGeometryScriptPrimitiveUVMode::ScaleToFill;
    Opts.MaterialID = 1;
    UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(
        Mesh, Opts, FTransform::Identity, HalfX * 2.f, Thick, HalfZ * 2.f + 8.f,
        1, 1, 1, EGeometryScriptPrimitiveOriginMode::Base, nullptr);

    if (Mat)
    {
        Comp->SetMaterial(0, Mat);   // 兜底槽（未打标三角形）
        Comp->SetMaterial(1, Mat);
    }
    Comp->NotifyMeshUpdated();
    Comp->UpdateBounds();
    Comp->MarkRenderStateDirty();

    UE_LOG(LogTemp, Warning,
        TEXT("[portmesh] back plate '%s' half=(%.0f,%.0f) thick=%.0f center=(%.0f,%.0f,%.0f) mat=%s"),
        CompName, HalfX, HalfZ, Thick, Center.X, Center.Y, Center.Z,
        Mat ? *Mat->GetName() : TEXT("none"));
    return Comp;
}

// ----------------------------------------------------------------------------
// ★1.0.8CV v5 整凹槽全覆盖·外凸15（用户最终定案「彻底覆盖方形孔，留下圆形孔」）。
//
// 尺寸依据（portmap 一手实测，不靠猜）：
//   · 板外边界 = 下部浅凹槽矩形：X[-145..140]（半宽 142.5）、Z[45..315]
//     （凹槽中心 Z = 开口bbox中心-45）——恰好盖满方形孔、绝不多盖机身面（CT 教训，
//     ★半宽必须用 portmap 锚定常量，绝不可 Open.HalfX=202.5：bbox 被上部穿透槽行撑大）；
//   · 中央圆孔 r=112（圆口网格半径≈100 + 12uu 间隙环），孔心 Z = 开口bbox中心-46；
//   · 上部穿透槽 Z[330..405] 不碰（板顶 315 < 330）。
// 形态：整块方板 减 中央圆孔 = 前视图只见圆口；板前表面在门面内 15、厚 94，
//   背 331 伸入原版机身壳内 17uu 不可见；接管后管子填满圆孔（原版行为）。
// 材质必须不透明实体（MI_Beam_01）：引擎默认 WorldGridMaterial 半透明会整板隐形（1.0.8CT 教训）。
// 读档重跑 BeginPlay 幂等：按名复用旧组件、Append 前 Clear；工具网格挂瞬态包（不随重跑累积）。
// ----------------------------------------------------------------------------
inline UDynamicMeshComponent* WT_AddRecessCornerFill(AActor* Self, USceneComponent* Parent,
    const TCHAR* CompName,
    float OpeningCenterX, float OpeningCenterZ, float WallY,
    UMaterialInterface* Mat)
{
    if (!Self || !Parent || !CompName)
    {
        return nullptr;
    }
    // 实测常量（改这里必须先改一手实测依据：[portmap] 日志）。
    constexpr float kRecessHalfX  = 142.5f; // 凹槽半宽（portmap 下部19行一致 X[-145..140]；★绝不用 Open.HalfX=202.5）
    constexpr float kRecessCtrZOff = 45.f;  // 凹槽中心 Z = 开口bbox中心 - 45（凹槽 Z[45..315]）
    constexpr float kRecessHalfZ  = 135.f;  // 凹槽半高（(315-45)/2=135）
    constexpr float kHoleR     = 112.f;  // 圆口网格半径≈100 + 12 间隙
    constexpr float kHoleZOff  = 46.f;   // 圆口中心 Z = 开口bbox中心 - 46
    constexpr float kFrontGap  = 15.f;   // 板前表面在门面内 15uu
    constexpr float kThick     = 94.f;   // 背=331：伸入原版机身壳内17uu，藏于壳体不可见

    const float HoleCenterZ = OpeningCenterZ - kHoleZOff;
    const float RecessCtrZ  = OpeningCenterZ - kRecessCtrZOff;
    const float FillBottomZ = RecessCtrZ - kRecessHalfZ;   // 板底 45 = 凹槽底
    const float FillHeightZ = 2.f * kRecessHalfZ;

    // 幂等：读档重跑 BeginPlay 按名复用旧组件（网格重建，Append 前已 Clear）。
    UDynamicMeshComponent* Comp = WT_FindDynamicMeshCompByName(Self, CompName);
    if (!Comp)
    {
        Comp = NewObject<UDynamicMeshComponent>(Self, CompName);
        if (!Comp)
        {
            return nullptr;
        }
        Comp->SetMobility(EComponentMobility::Movable);
        Comp->SetupAttachment(Parent);
        Comp->RegisterComponent();
    }
    Comp->SetRelativeRotation(FRotator::ZeroRotator);
    // AppendBox Base 模式只对 Z 生效、Y 以组件原点为中心 ⇒ 原点 = (盒心X, 前面Y-半厚, 盒底Z)。
    Comp->SetRelativeLocation(FVector(OpeningCenterX,
                                      WallY - kFrontGap - kThick * 0.5f,
                                      FillBottomZ));
    Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    UDynamicMesh* Mesh = Comp->GetDynamicMesh();
    if (!Mesh)
    {
        UE_LOG(LogTemp, Error, TEXT("[portmesh] recess corner fill '%s' has no UDynamicMesh"), CompName);
        return nullptr;
    }
    Mesh->EditMesh([](UE::Geometry::FDynamicMesh3& M) { M.Clear(); });

    // ① 整凹槽方形板：局部 X ±kRecessHalfX、Y ±半厚、Z 0..FillHeightZ（Base=盒底在原点沿 +Z）。
    FGeometryScriptPrimitiveOptions Opts;
    Opts.UVMode = EGeometryScriptPrimitiveUVMode::ScaleToFill;
    Opts.MaterialID = 1;   // 槽号一律 >0（=0 引擎不写入，1.0.8AY 教训）
    UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(
        Mesh, Opts, FTransform::Identity, kRecessHalfX * 2.f, kThick, FillHeightZ,
        1, 1, 1, EGeometryScriptPrimitiveOriginMode::Base, nullptr);

    // ② 工具圆柱（瞬态网格，Identity 建在工具原点、轴沿工具 +Z、Center=中点在原点），
    //    再由 ToolTransform 把轴掰到 +Y（MakeFromZ）并平移到孔心 —— 只变换一次，禁双变换。
    UDynamicMesh* ToolMesh = NewObject<UDynamicMesh>(GetTransientPackage(), NAME_None);
    if (ToolMesh)
    {
        FGeometryScriptPrimitiveOptions ToolOpts;
        ToolOpts.MaterialID = 1;
        UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCylinder(
            ToolMesh, ToolOpts, FTransform::Identity, kHoleR, kThick + 20.f, 32, 0, true,
            EGeometryScriptPrimitiveOriginMode::Center, nullptr);

        // ③ 布尔减孔：TargetTransform=Identity ⇒ 结果仍在组件局部空间（OutputSpace 默认 Target）。
        const FTransform ToolXf(FRotationMatrix::MakeFromZ(FVector(0.f, 1.f, 0.f)).Rotator(),
                                FVector(0.f, 0.f, HoleCenterZ - FillBottomZ));
        FGeometryScriptMeshBooleanOptions BoolOpts;
        UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(
            Mesh, FTransform::Identity, ToolMesh, ToolXf,
            EGeometryScriptBooleanOperation::Subtract, BoolOpts, nullptr);
    }

    if (Mat)
    {
        Comp->SetMaterial(0, Mat);   // 兜底槽（未打标三角形）
        Comp->SetMaterial(1, Mat);
    }
    Comp->NotifyMeshUpdated();
    Comp->UpdateBounds();
    Comp->MarkRenderStateDirty();

    UE_LOG(LogTemp, Warning,
        TEXT("[portmesh] 1.0.8CV recess corner fill '%s' halfX=%.1f halfZ=%.0f proud=%.0f holeR=%.0f holeZ=%.0f wallY=%.0f mat=%s"),
        CompName, kRecessHalfX, kRecessHalfZ, kFrontGap, kHoleR, HoleCenterZ, WallY,
        Mat ? *Mat->GetName() : TEXT("none"));
    return Comp;
}

// 装配一个圆形管道接口：连接件（朝向/类型/间隙）+ 视觉件骨架，一次建好。
// 视觉几何在 BeginPlay 里由 WT_BuildRoundPortGeometry 生成（那时主网格材质已就绪）。
inline UFGPipeConnectionComponent* WT_AddRoundPipePort(
    AActor* Self, USceneComponent* Parent, const FWTPipePortSpec& Spec)
{
    if (!Self || !Parent || Spec.ConnName.IsNone())
    {
        return nullptr;
    }
    UFGPipeConnectionComponent* Conn =
        Self->CreateDefaultSubobject<UFGPipeConnectionComponent>(Spec.ConnName);
    if (!Conn)
    {
        return nullptr;
    }
    Conn->SetupAttachment(Parent);
    Conn->SetRelativeLocation(Spec.Loc);
    // 连接件的「朝外」= 本地 +X（与工程既有口径一致：Yaw90 ⇒ 朝 +Y）
    Conn->SetRelativeRotation(FRotationMatrix::MakeFromX(Spec.OutDir).Rotator());
    Conn->SetPipeConnectionType(EPipeConnectionType::PCT_ANY);
    Conn->SetConnectorClearance(Spec.Clearance);

    if (!Spec.bVisual || Spec.VisualName.IsNone())
    {
        return Conn;   // 辅助口：只要连接件（满足原版 BeginPlay「必须两个接口」的契约）
    }
    WT_CreateRoundPipePortVisual(Self, Parent, Spec);
    return Conn;
}
