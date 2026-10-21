#pragma once

#include "Hologram/FGBuildableHologram.h"
#include "Remote_FactoryUpgradeHolograms.generated.h"

/**
 * Placement holograms for the three Form 3 "C" upgrade buildings.
 *
 * Each is a one-line subclass: the base AFGBuildableHologram copies the target building's "MainMesh"
 * component automatically, so the preview geometry is already correct and the only thing worth
 * overriding is the floor rule.
 *
 * ## Why the floor rule is relaxed
 *
 * The vanilla default rejects placement unless the spawn lands on a valid floor with a shallow
 * angle, which is right for a foundation-snapped building and wrong for a box the player wants to
 * drop on uneven terrain next to a mine. Setting mNeedsValidFloor=false and
 * mMaxPlacementFloorAngle=90 keeps placement permissive in exactly the way the base terminal's own
 * hologram already is, so the four buildings behave consistently.
 *
 * They are separate classes rather than one shared hologram because the descriptor names its
 * hologram by class, and a future per-building tweak (a snap rule, an extra validation) belongs on
 * the one it applies to.
 */
UCLASS()
class REMOTE_FACTORY_API AWTUpgradePowerStorageHologram : public AFGBuildableHologram
{
    GENERATED_BODY()
public:
    AWTUpgradePowerStorageHologram();

    // ★0.2.50：骨骼电池体（SK_PowerStorage_01）不在引擎默认全息镜像范围（默认只镜像
    // UStaticMeshComponent），导致幽灵投影缺件。重写以在 Super 之后确保骨骼体存在于预览树
    // 并贴上全息材质（自动跟随 OK/WARNING/ERROR 三态）。
    virtual void SetPlacementMaterialState(EHologramMaterialState state) override;
private:
    // 找被镜像的主网格 SM_PowerStorage_01，骨骼体挂其下以继承 2.667× 缩放与位置。
    class UStaticMeshComponent* FindMainMesh() const;
};

UCLASS()
class REMOTE_FACTORY_API AWTUpgradeStorageHologram : public AFGBuildableHologram
{
    GENERATED_BODY()
public:
    AWTUpgradeStorageHologram();

    // ★0.2.48 云端机无限向上堆叠：同类机可叠在彼此正上方。
    virtual bool IsValidHitResult(const FHitResult& hitResult) const override;
    virtual bool TrySnapToActor(const FHitResult& hitResult) override;
    virtual void GetIgnoredClearanceActors(TSet<AActor*>& ignoredActors) const override;
};

UCLASS()
class REMOTE_FACTORY_API AWTUpgradeCloudDownloaderHologram : public AFGBuildableHologram
{
    GENERATED_BODY()
public:
    AWTUpgradeCloudDownloaderHologram();

    // ★0.2.48 云端机无限向上堆叠：同类机可叠在彼此正上方。
    virtual bool IsValidHitResult(const FHitResult& hitResult) const override;
    virtual bool TrySnapToActor(const FHitResult& hitResult) override;
    virtual void GetIgnoredClearanceActors(TSet<AActor*>& ignoredActors) const override;
};

UCLASS()
class REMOTE_FACTORY_API AWTUpgradeCloudFluidUploaderHologram : public AFGBuildableHologram
{
    GENERATED_BODY()
public:
    AWTUpgradeCloudFluidUploaderHologram();

    // ★0.2.48 云端机无限向上堆叠：同类机可叠在彼此正上方。
    virtual bool IsValidHitResult(const FHitResult& hitResult) const override;
    virtual bool TrySnapToActor(const FHitResult& hitResult) override;
    virtual void GetIgnoredClearanceActors(TSet<AActor*>& ignoredActors) const override;
};

UCLASS()
class REMOTE_FACTORY_API AWTUpgradeCloudFluidDownloaderHologram : public AFGBuildableHologram
{
    GENERATED_BODY()
public:
    AWTUpgradeCloudFluidDownloaderHologram();

    // ★0.2.48 云端机无限向上堆叠：同类机可叠在彼此正上方。
    virtual bool IsValidHitResult(const FHitResult& hitResult) const override;
    virtual bool TrySnapToActor(const FHitResult& hitResult) override;
    virtual void GetIgnoredClearanceActors(TSet<AActor*>& ignoredActors) const override;
};

UCLASS()
class REMOTE_FACTORY_API AWTUpgradeAcceleratorHologram : public AFGBuildableHologram
{
    GENERATED_BODY()
public:
    AWTUpgradeAcceleratorHologram();
};
