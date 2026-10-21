#include "Remote_FactoryUpgradeHolograms.h"

#include "Engine/World.h"
#include "EngineUtils.h"   // TActorIterator<>
#include "GameFramework/Actor.h"
#include "Buildables/FGBuildable.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/UObjectGlobals.h"   // LoadObject / FindObject

AWTUpgradePowerStorageHologram::AWTUpgradePowerStorageHologram()
{
    mNeedsValidFloor = false;
    mMaxPlacementFloorAngle = 90.f;
}

UStaticMeshComponent* AWTUpgradePowerStorageHologram::FindMainMesh() const
{
    // 在被全息镜像出来的组件树里找主网格 SM_PowerStorage_01（即 mMeshMesh 的镜像）。
    TArray<UStaticMeshComponent*> MainMeshes;
    GetComponents<UStaticMeshComponent>(MainMeshes, true);
    for (UStaticMeshComponent* SMC : MainMeshes)
    {
        if (UStaticMesh* M = SMC->GetStaticMesh())
        {
            if (M->GetPathName().Contains(TEXT("SM_PowerStorage_01")))
            {
                return SMC;
            }
        }
    }
    return nullptr;
}

void AWTUpgradePowerStorageHologram::SetPlacementMaterialState(EHologramMaterialState state)
{
    Super::SetPlacementMaterialState(state);

    // ★0.2.50 幽灵投影缺件修复：骨骼电池体（SK_PowerStorage_01）不在引擎默认全息镜像范围
    // （默认只镜像 UStaticMeshComponent），故幽灵预览里看不到它。Super 跑完后：
    //   1) 抓取 Super 刚给主网格贴上的全息材质（自动跟随 OK/WARNING/ERROR 三态）；
    //   2) 若引擎已把骨骼体镜像进预览树 → 直接补材质；
    //   3) 若引擎没镜像 → 显式补一个 USkeletalMeshComponent，挂在已镜像的主网格下
    //      （继承其 2.667× 等比缩放与位置，无需手算变换），再贴全息材质。
    UMaterialInterface* HoloMat = nullptr;
    if (UStaticMeshComponent* Main = FindMainMesh())
    {
        HoloMat = Main->GetMaterial(0);
    }

    bool bHasSK = false;
    TArray<USkeletalMeshComponent*> SKComps;
    GetComponents<USkeletalMeshComponent>(SKComps, true);
    for (USkeletalMeshComponent* SK : SKComps)
    {
        bHasSK = true;
        for (int32 i = 0; i < SK->GetNumMaterials(); ++i)
        {
            if (HoloMat) SK->SetMaterial(i, HoloMat);
        }
    }

    if (!bHasSK && !FindObject<USkeletalMeshComponent>(this, TEXT("HoloInnerBattery")))
    {
        UStaticMeshComponent* Main = FindMainMesh();
        if (Main)
        {
            USkeletalMeshComponent* SK = NewObject<USkeletalMeshComponent>(this, TEXT("HoloInnerBattery"));
            SK->SetupAttachment(Main);
            if (USkeletalMesh* Inner = LoadObject<USkeletalMesh>(nullptr,
                    TEXT("/Game/FactoryGame/Buildable/Factory/PowerStorage/Mesh/SK_PowerStorage_01.SK_PowerStorage_01")))
            {
                SK->SetSkeletalMesh(Inner);
            }
            SK->RegisterComponent();
            for (int32 i = 0; i < SK->GetNumMaterials(); ++i)
            {
                if (HoloMat) SK->SetMaterial(i, HoloMat);
            }
        }
    }
}

AWTUpgradeStorageHologram::AWTUpgradeStorageHologram()
{
    mNeedsValidFloor = false;
    mMaxPlacementFloorAngle = 90.f;
}

AWTUpgradeCloudDownloaderHologram::AWTUpgradeCloudDownloaderHologram()
{
    mNeedsValidFloor = false;
    mMaxPlacementFloorAngle = 90.f;
}

AWTUpgradeCloudFluidUploaderHologram::AWTUpgradeCloudFluidUploaderHologram()
{
    mNeedsValidFloor = false;
    mMaxPlacementFloorAngle = 90.f;
}

AWTUpgradeCloudFluidDownloaderHologram::AWTUpgradeCloudFluidDownloaderHologram()
{
    mNeedsValidFloor = false;
    mMaxPlacementFloorAngle = 90.f;
}

AWTUpgradeAcceleratorHologram::AWTUpgradeAcceleratorHologram()
{
    mNeedsValidFloor = false;
    mMaxPlacementFloorAngle = 90.f;
}

// ─────────────────────────────────────────────────────────────────────────────
// ★0.2.48 云端机无限向上堆叠
//
// 机制（查原版 FGHologram / AFGBuildableHologram 头文件确认，非猜测）：
//   - 建造枪命中某 Actor 后先问 IsValidHitResult；默认把「其他建筑」当无效命中，
//     会把全息隐藏、且不会进入 TrySnapToActor。故必须重写 IsValidHitResult，
//     让「同类云端机」成为有效命中。
//   - TrySnapToActor 返回 true 即表示已自行吸附，本帧不再做网格/地面对齐。
//     这正是原版堆叠架吸附到顶部所用的钩子，我们照抄同一约定。
//   - 叠放时新机的净空盒会和下方机重叠 → 默认会被净空冲突拒放；故重写
//     GetIgnoredClearanceActors，忽略同列同类机。
//
// 高度处理：用运行时实测的渲染包围盒半高（2*半高 = 一台净高），绝不手写尺寸；
// 同列多台取 Z 最高一台，避免瞄准到半空中的机器时叠错位置。
// ─────────────────────────────────────────────────────────────────────────────

// 收集与 ColumnLoc 同 X/Y（同列）、同建造类的所有云端机。供「取最高一台」与净空忽略共用。
static void WT_CollectSameClassColumn(
    const AFGBuildableHologram* Self, const FVector& ColumnLoc, TArray<AFGBuildable*>& Out)
{
    Out.Reset();
    if (!Self)
    {
        return;
    }
    UClass* BC = Self->GetBuildClass().Get();
    UWorld* W = Self->GetWorld();
    if (!BC || !W)
    {
        return;
    }
    for (TActorIterator<AFGBuildable> It(W); It; ++It)
    {
        AFGBuildable* B = *It;
        if (!B || !B->IsA(BC))
        {
            continue;
        }
        const FVector BL = B->GetActorLocation();
        if (FMath::Abs(BL.X - ColumnLoc.X) < 100.f && FMath::Abs(BL.Y - ColumnLoc.Y) < 100.f)
        {
            Out.Add(B);
        }
    }
}

// 命中同类机时，把全息吸附到同列最高一台的正上方。返回 true = 已吸附。
static bool WT_CloudStackSnapToTop(AFGBuildableHologram* Self, const FHitResult& Hit)
{
    AActor* A = Hit.GetActor();
    if (!A || !Self)
    {
        return false;
    }
    UClass* BC = Self->GetBuildClass().Get();
    if (!BC || !A->IsA(BC))
    {
        return false;   // 只吸附同类机
    }
    if (A == Self)
    {
        return false;
    }

    // 同列取 Z 最高一台，叠在它上面（避免瞄准半空中的机器时叠错）。
    TArray<AFGBuildable*> Column;
    WT_CollectSameClassColumn(Self, A->GetActorLocation(), Column);
    AFGBuildable* Top = Cast<AFGBuildable>(A);
    for (AFGBuildable* B : Column)
    {
        if (B->GetActorLocation().Z > Top->GetActorLocation().Z)
        {
            Top = B;
        }
    }

    // 实测半高（渲染包围盒，必然存在）。兜底：一台净高约 4.23m → 半高 ~2.115m。
    FVector BOrigin;
    FVector BExtent;
    Top->GetActorBounds(false, BOrigin, BExtent);
    float HalfZ = BExtent.Z;
    if (HalfZ < 1.f)
    {
        HalfZ = 211.5f;
    }

    FVector NewLoc = Top->GetActorLocation();
    NewLoc.Z += 2.f * HalfZ;            // 一台净高 = 2*半高，叠在正上方、严丝合缝
    const FRotator NewRot = Top->GetActorRotation();

    Self->SetActorLocation(NewLoc);
    Self->SetActorRotation(NewRot);
    return true;
}

// 四个云端机全息共用同一套堆叠逻辑，仅类名不同。
#define WT_CLOUD_STACK_HOLO_IMPL(ClassName)                                                       \
    bool ClassName::IsValidHitResult(const FHitResult& hitResult) const                           \
    {                                                                                              \
        if (AActor* A = hitResult.GetActor())                                                      \
        {                                                                                          \
            if (A->IsA(GetBuildClass().Get()))                                                     \
            {                                                                                      \
                return true;   /* 允许瞄准同类机（用于堆叠） */                                     \
            }                                                                                      \
        }                                                                                          \
        return Super::IsValidHitResult(hitResult);                                                 \
    }                                                                                              \
    bool ClassName::TrySnapToActor(const FHitResult& hitResult)                                    \
    {                                                                                              \
        if (WT_CloudStackSnapToTop(this, hitResult))                                               \
        {                                                                                          \
            return true;                                                                           \
        }                                                                                          \
        return Super::TrySnapToActor(hitResult);                                                   \
    }                                                                                              \
    void ClassName::GetIgnoredClearanceActors(TSet<AActor*>& ignoredActors) const                  \
    {                                                                                              \
        Super::GetIgnoredClearanceActors(ignoredActors);                                           \
        TArray<AFGBuildable*> Column;                                                              \
        WT_CollectSameClassColumn(this, GetActorLocation(), Column);                               \
        for (AFGBuildable* B : Column)                                                            \
        {                                                                                          \
            ignoredActors.Add(B);                                                                  \
        }                                                                                          \
    }

WT_CLOUD_STACK_HOLO_IMPL(AWTUpgradeStorageHologram)
WT_CLOUD_STACK_HOLO_IMPL(AWTUpgradeCloudDownloaderHologram)
WT_CLOUD_STACK_HOLO_IMPL(AWTUpgradeCloudFluidUploaderHologram)
WT_CLOUD_STACK_HOLO_IMPL(AWTUpgradeCloudFluidDownloaderHologram)

// ─────────────────────────────────────────────────────────────────────────────
// ★0.2.48 §A 圆口（用户定案 2026-10-10「建模不应该是云端建模吗」）：
//
// 流体两站主体保持云端建模（SM_CentralStorage_01），圆形管道口 = 独立「端口小件」
// SM_Pipe_InPut_01 组件（见 Upgrades.cpp 的 WT_AddRoundPortVisual 与其一手依据）。
// 这些组件都在建造类 CDO 上，全息 SetBuildClass「copying the actors content」时
// 一并复制 ⇒ 预览自动就是「云端建模 + 圆口」，本文件**不需要任何特殊处理**。
// （此前的储罐换网格/手动 SetMaterialState 补丁已全部删除。）
// ─────────────────────────────────────────────────────────────────────────────
