#include "Remote_FactoryBuildable.h"

#include "Remote_FactoryLoc.h"
#include "Remote_FactoryLog.h"
#include "Remote_FactoryPanelWidget.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

ARemote_FactoryBuildable::ARemote_FactoryBuildable()
{
    PrimaryActorTick.bCanEverTick = false;

    // The mesh component MUST be named "MainMesh": AFGBuildableHologram looks the preview geometry
    // up by that exact name when the player is placing the building. Any other name and the
    // placement ghost is invisible even though the building itself works.
    UStaticMeshComponent* MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MainMesh"));
    if (MeshComp)
    {
        MeshComp->SetupAttachment(RootComponent);

        // A vanilla, always-cooked mesh keeps this mod asset-free. The console reads as a squat
        // pedestal; the scale below makes it about chest height so it looks like something you
        // walk up to and interact with.
        static ConstructorHelpers::FObjectFinder<UStaticMesh> ConsoleMeshFinder(
            TEXT("/Engine/BasicShapes/Cube.Cube"));
        if (ConsoleMeshFinder.Succeeded())
        {
            MeshComp->SetStaticMesh(ConsoleMeshFinder.Object);
        }
        // Relative (not world) transforms: this runs in the constructor, where the actor has no
        // world transform yet, and the parent root is Static mobility.
        MeshComp->SetRelativeScale3D(FVector(0.6f, 0.6f, 1.2f));
        MeshComp->SetRelativeLocation(FVector(0.f, 0.f, 60.f));
        MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        MeshComp->SetCollisionResponseToAllChannels(ECR_Block);
        MeshComp->SetGenerateOverlapEvents(false);
        MeshComp->SetCanEverAffectNavigation(false);
    }

    MakeUseableWithPanel();
}

FText ARemote_FactoryBuildable::GetTerminalDisplayName()
{
    return WT_LOCTEXT("Building_Remote_Factory", "智能工厂");
}

void ARemote_FactoryBuildable::MakeUseableWithPanel()
{
    // mIsUseable and mInteractWidgetSoftClass are private on AFGBuildable, and the shipped SDK
    // header does not expose them at all. Reflection is the only way in, and it is also the
    // documented pair the game's interaction check reads:
    //   * mIsUseable            -> "pressing E on this building does something"
    //   * mInteractWidgetSoftClass -> which UFGInteractWidget to spawn for it
    const UClass* PanelClass = URemote_FactoryPanelWidget::StaticClass();

    if (FBoolProperty* UseableProp = FindFProperty<FBoolProperty>(AFGBuildable::StaticClass(), TEXT("mIsUseable")))
    {
        UseableProp->SetPropertyValue(UseableProp->ContainerPtrToValuePtr<void>(this), true);
        WT_LOGF(TEXT("[buildable] mIsUseable set on %s"), *GetClass()->GetName());
    }
    else
    {
        WT_ERRF(TEXT("[buildable] AFGBuildable::mIsUseable not found — the terminal will not open."));
    }

    if (FSoftClassProperty* WidgetProp = FindFProperty<FSoftClassProperty>(AFGBuildable::StaticClass(), TEXT("mInteractWidgetSoftClass")))
    {
        if (PanelClass)
        {
            const FString Path = PanelClass->GetPathName();
            const TCHAR* ImportResult = WidgetProp->ImportText_Direct(
                *Path, WidgetProp->ContainerPtrToValuePtr<void>(this), this, 0, nullptr);
            WT_LOGF(TEXT("[buildable] mInteractWidgetSoftClass <= %s (imported=%s)"),
                *Path, ImportResult ? TEXT("ok") : TEXT("FAILED"));
        }
    }
    else
    {
        WT_ERRF(TEXT("[buildable] AFGBuildable::mInteractWidgetSoftClass not found — the terminal will not open."));
    }
}

void ARemote_FactoryBuildable::BeginPlay()
{
    Super::BeginPlay();

    // Re-assert the interaction wiring at runtime. A building spawned from a save or from the
    // build gun goes through the CDO copy, and if the soft class ever failed to resolve during
    // construction this is the second chance to get it right.
    MakeUseableWithPanel();

    WT_LOGF(TEXT("[buildable] Remote Factory placed at %s"), *GetActorLocation().ToString());
}
