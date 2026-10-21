#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Remote_FactoryTechBackground.generated.h"

class SWTTechDeck;

/**
 * ★0.2.65 Modern-tech background deck shared by the two terminal windows
 *         (the factory panel and the production calculator).
 *
 * Why a custom Slate widget instead of a few nested UBorder / UImage layers:
 *   - UMG has no primitive for a gradient, for scan lines, or for corner brackets. Painting
 *     those as "real" widgets would need dozens of tiny borders with faked sizes.
 *   - Drawing them directly with Slate draw elements keeps the widget count at ONE per panel,
 *     is resolution independent (no texture, no tiling seams) and needs no texture resource
 *     at all -- which matters here, because a brush without a real texture is exactly what
 *     crashed the old WirelessPower panel (access violation while painting).
 *
 * Everything this deck paints is either
 *   FSlateDrawElement::MakeGradient(...)   -- gradient stops are interpreted in LOCAL space
 *   FSlateDrawElement::MakeLines(...)      -- TArray<FVector2f>, thickness in local units
 * Both are verified against the engine headers:
 *   Engine/Source/Runtime/SlateCore/Public/Rendering/DrawElementTypes.h:88  (MakeBox)
 *   Engine/Source/Runtime/SlateCore/Public/Rendering/DrawElementTypes.h:166 (MakeGradient)
 *   Engine/Source/Runtime/SlateCore/Public/Rendering/DrawElementTypes.h:216 (MakeLines, FVector2f points)
 * and against the batcher note "Gradient stops are interpreted in local space"
 *   (Engine/Source/Runtime/SlateCore/Private/Rendering/ElementBatcher.cpp:1792).
 */
UCLASS()
class REMOTE_FACTORY_API UWTTechBackground : public UWidget
{
    GENERATED_BODY()

public:
    UWTTechBackground(const FObjectInitializer& ObjectInitializer);

    /** Top stop of the vertical base gradient (slightly lifted, reads as a lit surface). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
    FLinearColor TopColor;

    /** Middle stop -- the body colour of the deck. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
    FLinearColor MidColor;

    /** Bottom stop (darkest) -- gives the deck depth. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
    FLinearColor BottomColor;

    /** Faint technical grid (both directions). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
    FLinearColor GridColor;

    /** Horizontal scan lines laid over the whole deck. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
    FLinearColor ScanColor;

    /** Thin outline of the whole deck. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
    FLinearColor FrameColor;

    /** Brighter L-shaped brackets in the four corners. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
    FLinearColor CornerColor;

    /** Grid spacing, local units. Values below 12 are clamped so huge panels stay cheap. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance", meta = (ClampMin = "12.0"))
    float GridStep;

    /** Scan-line spacing, local units. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance", meta = (ClampMin = "3.0"))
    float ScanStep;

    /** Outline thickness, local units. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance", meta = (ClampMin = "0.5"))
    float FrameThickness;

    /** Length of one arm of a corner bracket, local units (clamped to 18% of the short side). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance", meta = (ClampMin = "4.0"))
    float CornerLength;

    /** Scan lines are cosmetic; turn them off for a cleaner look. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
    bool bDrawScanlines;

    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void SynchronizeProperties() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
    TSharedPtr<SWTTechDeck> Deck;
};
