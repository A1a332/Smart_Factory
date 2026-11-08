// Copyright Coffee Stain Studios. All Rights Reserved.

#include "Remote_FactoryPowerGraph.h"

#include "Styling/CoreStyle.h"          // FCoreStyle::Get().GetBrush("WhiteBrush") —— 1×1 白刷子，本 mod 不发贴图
#include "Rendering/DrawElements.h"     // FSlateDrawElement::MakeBox / MakeLines
#include "Widgets/InvalidateWidgetReason.h"  // EInvalidateWidgetReason::Paint

// ----------------------------------------------------------------------------
// SWTPowerGraph
// ----------------------------------------------------------------------------

int32 SWTPowerGraph::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
                             const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
                             int32 LayerId, const FWidgetStyle& InWidgetStyle,
                             bool bParentEnabled) const
{
    // 局部尺寸。GetLocalSize() 返回的是 FDeprecateSlateVector2D（派生自 FVector2f）；
    // 这里直接用 FVector2f 接 —— 切片拷贝，不走它那个标了 deprecated 的
    // operator FVector2d()，也免得跟着引擎的 double/float 迁移来回改。
    const FVector2f LocalSize = AllottedGeometry.GetLocalSize();
    const float W = FMath::Max(1.f, LocalSize.X);
    const float H = FMath::Max(1.f, LocalSize.Y);

    // 无参 ToPaintGeometry() = 以控件局部尺寸为绘制空间、原点在左上。
    // 下面所有点都按 [0,W]×[0,H] 的局部坐标给，所以不需要再自己算缩放。
    const FPaintGeometry PaintGeo = AllottedGeometry.ToPaintGeometry();

    // ---- 底 ----
    // 用引擎 WhiteBrush 染色（与面板其它分隔线同一手法）。绝不 new 一个没资源的
    // FSlateBrush —— 那会在 OnPaint 里崩（WirelessPower 踩过：FName::ToString 访问冲突）。
    FSlateDrawElement::MakeBox(OutDrawElements, LayerId, PaintGeo,
        FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")), ESlateDrawEffect::None, BgColor);

    int32 Layer = LayerId + 1;

    // ---- 横向网格 ----
    // 画 GridRows-1 条内部线（边界由底色边缘本身表达，不再额外描边）。
    if (GridRows > 1)
    {
        for (int32 i = 1; i < GridRows; ++i)
        {
            const float Y = H * static_cast<float>(i) / static_cast<float>(GridRows);
            TArray<FVector2f> Seg;
            Seg.Reserve(2);
            Seg.Add(FVector2f(0.f, Y));
            Seg.Add(FVector2f(W, Y));
            FSlateDrawElement::MakeLines(OutDrawElements, Layer, PaintGeo,
                MoveTemp(Seg), ESlateDrawEffect::None, GridColor, /*bAntialias=*/true, 1.f);
        }
    }

    // ---- 折线 ----
    ++Layer;
    const float SafeMax = FMath::Max(MaxY, 1.f);
    for (const FWTGraphSeriesRaw& S : Series)
    {
        const int32 N = S.Values.Num();
        if (N < 2)
        {
            continue;   // 单点画不出线；N==1 时下面 1-(V/Max) 也只有一像素假线
        }

        TArray<FVector2f> Pts;
        Pts.Reserve(N);
        for (int32 i = 0; i < N; ++i)
        {
            // X 按索引均分到整宽：不同线的采样点数可能不同（刚建的电网点数少），
            // 各自拉伸到满宽比「按点数截断」更符合「这段时间的走势」这个语义。
            const float X = W * static_cast<float>(i) / static_cast<float>(N - 1);
            // Slate 的 Y 向下 ⇒ 值越大越靠上。
            const float V = FMath::Clamp(S.Values[i], 0.f, SafeMax);
            const float Y = H * (1.f - V / SafeMax);
            Pts.Add(FVector2f(X, Y));
        }
        FSlateDrawElement::MakeLines(OutDrawElements, Layer, PaintGeo,
            MoveTemp(Pts), ESlateDrawEffect::None, S.Color, /*bAntialias=*/true, S.Thickness);
    }

    return Layer + 1;
}

FVector2D SWTPowerGraph::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
    // 默认高度 150：与原版电线杆那块图的观感接近，且能塞进右列/中列而不挤压列表。
    // 实际高度由外层 USizeBox 覆盖（面板里会显式设 150）。
    return FVector2D(320.f, 150.f);
}

// ----------------------------------------------------------------------------
// UWTPowerGraphWidget
// ----------------------------------------------------------------------------

void UWTPowerGraphWidget::SetSeries(const TArray<FWTGraphSeriesRaw>& InSeries, float InMaxY)
{
    PendingSeries = InSeries;
    PendingMaxY = (InMaxY > 0.f) ? InMaxY : 1.f;
    bPendingValid = true;

    if (Graph.IsValid())
    {
        ApplyPendingToSlate();
        // 只要重画，不需要重新布局（尺寸没变）。
        Graph->Invalidate(EInvalidateWidgetReason::Paint);
    }
}

void UWTPowerGraphWidget::ClearSeries()
{
    PendingSeries.Reset();
    PendingMaxY = 1.f;
    bPendingValid = true;

    if (Graph.IsValid())
    {
        ApplyPendingToSlate();
        Graph->Invalidate(EInvalidateWidgetReason::Paint);
    }
}

void UWTPowerGraphWidget::ApplyPendingToSlate()
{
    if (!Graph.IsValid())
    {
        return;
    }

    Graph->Series = PendingSeries;
    Graph->MaxY   = PendingMaxY;

    // InMaxY <= 0 的「自动定上限」在 SetSeries 里已经折算成 1 了，
    // 这里再兜一次底：空数据时上限至少 1，避免 0/0 在 OnPaint 里出 NaN。
    if (Graph->MaxY <= 0.f)
    {
        Graph->MaxY = 1.f;
    }
}

TSharedRef<SWidget> UWTPowerGraphWidget::RebuildWidget()
{
    SAssignNew(Graph, SWTPowerGraph);

    // ★关键：面板构造期（SetSeries 早于这里）填进来的数据必须在此刻补上，
    // 否则第一次显示永远是空白，得等下一帧扫描回包才会出图。
    if (bPendingValid)
    {
        ApplyPendingToSlate();
    }
    return Graph.ToSharedRef();
}

void UWTPowerGraphWidget::ReleaseSlateResources(bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);
    Graph.Reset();
}
