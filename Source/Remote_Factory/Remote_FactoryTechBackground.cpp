#include "Remote_FactoryTechBackground.h"

#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"
#include "Rendering/DrawElementTypes.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SWidget.h"
#include "Layout/Geometry.h"

// ----------------------------------------------------------------------------
// SWTTechDeck -- a leaf Slate widget that paints the modern-tech deck.
//
// Paint order (later elements sit on top of earlier ones because each group uses
// its own layer id):
//   0  base vertical gradient
//   1  technical grid (vertical + horizontal hairlines)
//   2  horizontal scan lines
//   3  outline rectangle
//   4  four corner brackets
//   5  sheen: an accent-tinted wash over the top band that fades out downwards
// Nothing here references a texture or a style asset, so there is no way for this
// widget to paint an unresolvable resource (the exact failure mode that used to
// crash the old panel). All colours come from the owning UWTTechBackground's
// UPROPERTYs, so both panels can be re-tinted from Blueprint if we ever want to.
// ----------------------------------------------------------------------------
class SWTTechDeck : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SWTTechDeck) {}
        SLATE_ARGUMENT(FLinearColor, TopColor)
        SLATE_ARGUMENT(FLinearColor, MidColor)
        SLATE_ARGUMENT(FLinearColor, BottomColor)
        SLATE_ARGUMENT(FLinearColor, GridColor)
        SLATE_ARGUMENT(FLinearColor, ScanColor)
        SLATE_ARGUMENT(FLinearColor, FrameColor)
        SLATE_ARGUMENT(FLinearColor, CornerColor)
        SLATE_ARGUMENT(float, GridStep)
        SLATE_ARGUMENT(float, ScanStep)
        SLATE_ARGUMENT(float, FrameThickness)
        SLATE_ARGUMENT(float, CornerLength)
        SLATE_ARGUMENT(bool, bDrawScanlines)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs)
    {
        TopColor         = InArgs._TopColor;
        MidColor         = InArgs._MidColor;
        BottomColor      = InArgs._BottomColor;
        GridColor        = InArgs._GridColor;
        ScanColor        = InArgs._ScanColor;
        FrameColor       = InArgs._FrameColor;
        CornerColor      = InArgs._CornerColor;
        GridStep         = InArgs._GridStep;
        ScanStep         = InArgs._ScanStep;
        FrameThickness   = InArgs._FrameThickness;
        CornerLength     = InArgs._CornerLength;
        bDrawScanlines   = InArgs._bDrawScanlines;

        // Purely decorative: never take part in hit testing, so clicks always reach the
        // widgets stacked above us in the overlay.
        SetVisibility(EVisibility::HitTestInvisible);
    }

    void ApplyStyle(const FLinearColor& InTop, const FLinearColor& InMid, const FLinearColor& InBottom,
                    const FLinearColor& InGrid, const FLinearColor& InScan, const FLinearColor& InFrame,
                    const FLinearColor& InCorner, float InGridStep, float InScanStep,
                    float InFrameThickness, float InCornerLength, bool bInScanlines)
    {
        TopColor       = InTop;
        MidColor       = InMid;
        BottomColor    = InBottom;
        GridColor      = InGrid;
        ScanColor      = InScan;
        FrameColor     = InFrame;
        CornerColor    = InCorner;
        GridStep       = InGridStep;
        ScanStep       = InScanStep;
        FrameThickness = InFrameThickness;
        CornerLength   = InCornerLength;
        bDrawScanlines = bInScanlines;
    }

    /** The deck never asks for room; it fills whatever its slot gives it. */
    virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override
    {
        return FVector2D::ZeroVector;
    }

    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
                          const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
                          int32 LayerId, const FWidgetStyle& InWidgetStyle,
                          bool bParentEnabled) const override;

private:
    FLinearColor TopColor       = FLinearColor::White;
    FLinearColor MidColor       = FLinearColor::White;
    FLinearColor BottomColor    = FLinearColor::Black;
    FLinearColor GridColor      = FLinearColor::Transparent;
    FLinearColor ScanColor      = FLinearColor::Transparent;
    FLinearColor FrameColor     = FLinearColor::White;
    FLinearColor CornerColor    = FLinearColor::White;
    float        GridStep       = 34.f;
    float        ScanStep       = 6.f;
    float        FrameThickness = 1.4f;
    float        CornerLength   = 26.f;
    bool         bDrawScanlines = true;
};

namespace
{
    /** One straight segment pushed as a single draw element. */
    void DrawSegment(FSlateWindowElementList& Out, uint32 Layer, const FPaintGeometry& Geo,
                     const FVector2f& A, const FVector2f& B, const FLinearColor& Tint, float Thickness)
    {
        TArray<FVector2f> Points;
        Points.Reserve(2);
        Points.Add(A);
        Points.Add(B);
        FSlateDrawElement::MakeLines(Out, Layer, Geo, MoveTemp(Points), ESlateDrawEffect::None, Tint, true, Thickness);
    }
}

int32 SWTTechDeck::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
                           const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
                           int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
    const FVector2D LocalSizeD = AllottedGeometry.GetLocalSize();
    if (LocalSizeD.X < 8.0 || LocalSizeD.Y < 8.0)
    {
        return LayerId;
    }

    const float W = static_cast<float>(LocalSizeD.X);
    const float H = static_cast<float>(LocalSizeD.Y);
    const FPaintGeometry Geo = AllottedGeometry.ToPaintGeometry();

    int32 Layer = LayerId;

    // ---- 0) base vertical gradient -------------------------------------------
    {
        TArray<FSlateGradientStop> Stops;
        Stops.Reserve(3);
        Stops.Add(FSlateGradientStop(FVector2f(0.f, 0.f), TopColor));
        Stops.Add(FSlateGradientStop(FVector2f(0.f, H * 0.55f), MidColor));
        Stops.Add(FSlateGradientStop(FVector2f(0.f, H), BottomColor));
        FSlateDrawElement::MakeGradient(OutDrawElements, Layer, Geo, MoveTemp(Stops),
                                        EOrientation::Orient_Vertical, ESlateDrawEffect::None);
    }
    ++Layer;

    // ---- 1) technical grid ----------------------------------------------------
    // Kept out of the last pixel column / row so it never doubles up with the outline.
    {
        const float Step = FMath::Max(12.f, GridStep);
        for (float X = Step; X < W - 1.f; X += Step)
        {
            DrawSegment(OutDrawElements, Layer, Geo, FVector2f(X, 0.f), FVector2f(X, H), GridColor, 1.f);
        }
        for (float Y = Step; Y < H - 1.f; Y += Step)
        {
            DrawSegment(OutDrawElements, Layer, Geo, FVector2f(0.f, Y), FVector2f(W, Y), GridColor, 1.f);
        }
    }
    ++Layer;

    // ---- 2) scan lines --------------------------------------------------------
    if (bDrawScanlines)
    {
        const float Step = FMath::Max(3.f, ScanStep);
        for (float Y = 0.f; Y < H; Y += Step)
        {
            DrawSegment(OutDrawElements, Layer, Geo, FVector2f(0.f, Y), FVector2f(W, Y), ScanColor, 1.f);
        }
    }
    ++Layer;

    // ---- 3) outline -----------------------------------------------------------
    {
        TArray<FVector2f> Rect;
        Rect.Reserve(5);
        Rect.Add(FVector2f(0.f, 0.f));
        Rect.Add(FVector2f(W,  0.f));
        Rect.Add(FVector2f(W,  H));
        Rect.Add(FVector2f(0.f, H));
        Rect.Add(FVector2f(0.f, 0.f));
        FSlateDrawElement::MakeLines(OutDrawElements, Layer, Geo, MoveTemp(Rect),
                                     ESlateDrawEffect::None, FrameColor, true, FrameThickness);
    }
    ++Layer;

    // ---- 4) corner brackets ---------------------------------------------------
    {
        const float L = FMath::Min(CornerLength, FMath::Min(W, H) * 0.18f);
        const float T = FMath::Max(1.5f, FrameThickness * 1.6f);

        TArray<FVector2f> TL;                       // top-left: down the left, then right
        TL.Add(FVector2f(0.f, L));
        TL.Add(FVector2f(0.f, 0.f));
        TL.Add(FVector2f(L,   0.f));
        FSlateDrawElement::MakeLines(OutDrawElements, Layer, Geo, MoveTemp(TL),
                                     ESlateDrawEffect::None, CornerColor, true, T);

        TArray<FVector2f> TR;                       // top-right
        TR.Add(FVector2f(W - L, 0.f));
        TR.Add(FVector2f(W,     0.f));
        TR.Add(FVector2f(W,     L));
        FSlateDrawElement::MakeLines(OutDrawElements, Layer, Geo, MoveTemp(TR),
                                     ESlateDrawEffect::None, CornerColor, true, T);

        TArray<FVector2f> BR;                       // bottom-right
        BR.Add(FVector2f(W,     H - L));
        BR.Add(FVector2f(W,     H));
        BR.Add(FVector2f(W - L, H));
        FSlateDrawElement::MakeLines(OutDrawElements, Layer, Geo, MoveTemp(BR),
                                     ESlateDrawEffect::None, CornerColor, true, T);

        TArray<FVector2f> BL;                       // bottom-left
        BL.Add(FVector2f(L,     H));
        BL.Add(FVector2f(0.f,   H));
        BL.Add(FVector2f(0.f,   H - L));
        FSlateDrawElement::MakeLines(OutDrawElements, Layer, Geo, MoveTemp(BL),
                                     ESlateDrawEffect::None, CornerColor, true, T);
    }
    ++Layer;

    // ---- 5) sheen over the top band -------------------------------------------
    {
        FLinearColor SheenTop = FrameColor;
        SheenTop.A = FrameColor.A * 0.22f;

        FLinearColor FadeOut = SheenTop;
        FadeOut.A = 0.f;

        TArray<FSlateGradientStop> Stops;
        Stops.Reserve(3);
        Stops.Add(FSlateGradientStop(FVector2f(0.f, 0.f),             SheenTop));
        Stops.Add(FSlateGradientStop(FVector2f(0.f, H * 0.26f),       FadeOut));
        Stops.Add(FSlateGradientStop(FVector2f(0.f, H),               FadeOut));
        FSlateDrawElement::MakeGradient(OutDrawElements, Layer, Geo, MoveTemp(Stops),
                                        EOrientation::Orient_Vertical, ESlateDrawEffect::None);
    }

    // Keep the next sibling (the real panel content) strictly above the whole deck.
    return Layer + 1;
}

// ----------------------------------------------------------------------------

UWTTechBackground::UWTTechBackground(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    // Shared palette: same cool cyan/near-black family as the panel's existing colours,
    // but with a real gradient so the surface reads as a lit panel instead of a flat card.
    TopColor       = FLinearColor(0.070f, 0.115f, 0.150f, 1.f);
    MidColor       = FLinearColor(0.040f, 0.065f, 0.098f, 1.f);
    BottomColor    = FLinearColor(0.020f, 0.030f, 0.048f, 1.f);
    GridColor      = FLinearColor(0.290f, 0.780f, 0.900f, 0.055f);
    ScanColor      = FLinearColor(0.290f, 0.780f, 0.900f, 0.030f);
    FrameColor     = FLinearColor(0.290f, 0.780f, 0.900f, 0.55f);
    CornerColor    = FLinearColor(0.420f, 0.930f, 1.000f, 0.90f);
    GridStep       = 34.f;
    ScanStep       = 6.f;
    FrameThickness = 1.4f;
    CornerLength   = 26.f;
    bDrawScanlines = true;
}

TSharedRef<SWidget> UWTTechBackground::RebuildWidget()
{
    Deck = SNew(SWTTechDeck)
        .TopColor(TopColor)
        .MidColor(MidColor)
        .BottomColor(BottomColor)
        .GridColor(GridColor)
        .ScanColor(ScanColor)
        .FrameColor(FrameColor)
        .CornerColor(CornerColor)
        .GridStep(GridStep)
        .ScanStep(ScanStep)
        .FrameThickness(FrameThickness)
        .CornerLength(CornerLength)
        .bDrawScanlines(bDrawScanlines);

    return Deck.ToSharedRef();
}

void UWTTechBackground::SynchronizeProperties()
{
    Super::SynchronizeProperties();

    if (Deck.IsValid())
    {
        Deck->ApplyStyle(TopColor, MidColor, BottomColor, GridColor, ScanColor, FrameColor,
                         CornerColor, GridStep, ScanStep, FrameThickness, CornerLength, bDrawScanlines);
    }
}

void UWTTechBackground::ReleaseSlateResources(bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);
    Deck.Reset();
}
