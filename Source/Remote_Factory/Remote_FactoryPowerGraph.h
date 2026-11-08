// Copyright Coffee Stain Studios. All Rights Reserved.
//
// ★1.0.9 电力曲线控件（用户定案：新增【电网】类目，点开直接看到原版那块电力曲线）。
//
// 为什么必须自绘：本 mod 不发任何 UMG/贴图资产（面板全部用 ConstructWidget 搭），
// 而 UMG 自带控件里没有折线图。用一排 UImage 拼柱子画不出「阶梯折线」的原版观感，
// 所以直接落到 Slate 层：SLeafWidget + FSlateDrawElement::MakeLines。
//
// 为什么不 CreateWidget 复用原版蓝图 UFGPowerCircuitWidget：
// ① 它要在蓝图里绑 mGetPowerCircuitEvent（DECLARE_DYNAMIC_DELEGATE_RetVal），
//    纯 C++ 面板只能靠 BindUFunction 硬注入，且它内部结构依赖具体蓝图资源；
// ② 原版那块是浅色背景 + 原版字体，塞进这个暗色面板会格格不入；
// ③ 我们要的数据（FWT_GridEntry::Series）已经是服务端扫描时打包好的，不需要 UFGPowerCircuit。

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Remote_FactoryPowerGraph.generated.h"

/**
 * 一条要画的曲线：原始 MW 采样值 + 颜色。
 *
 * 只带「原始值」不带归一化结果：Y 轴上限取决于同组所有线里的最大值，
 * 而那个上限只有在拿到全部线之后才知道，所以归一化留到 OnPaint 里做。
 */
struct FWTGraphSeriesRaw
{
    /** 采样值，单位 MW。索引 0 = 最旧（画在左边）。 */
    TArray<float> Values;

    FLinearColor Color = FLinearColor::White;

    /** 线宽（px）。消耗/发电那几条主线上调粗一点，观感更接近原版。 */
    float Thickness = 1.6f;
};

/**
 * 纯 Slate 绘制层：深色底 + 横向网格 + N 条折线。
 *
 * 所有点都用**控件局部坐标**（原点在左上、Y 向下），配合
 * AllottedGeometry.ToPaintGeometry() 直接画，不需要手算 DPI/缩放。
 */
class SWTPowerGraph : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SWTPowerGraph) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs) {}

    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
                          const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
                          int32 LayerId, const FWidgetStyle& InWidgetStyle,
                          bool bParentEnabled) const override;

    virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

    /** 要画的线。空 = 只画底和网格（该电网还没有历史点）。 */
    TArray<FWTGraphSeriesRaw> Series;

    /** Y 轴上限（MW）。由调用方按「本组所有线的最大值」给，至少 1。 */
    float MaxY = 1.f;

    /** 横向网格条数（含顶底两条边界线以外的内部线）。 */
    int32 GridRows = 4;

    FLinearColor BgColor   = FLinearColor(0.020f, 0.028f, 0.042f, 1.00f);
    FLinearColor GridColor = FLinearColor(0.290f, 0.780f, 0.900f, 0.16f);
};

/**
 * UMG 侧包装，让面板能把它当成普通控件塞进 VerticalBox。
 *
 * ★必须先缓存再应用：面板是「先 ConstructWidget 搭完整棵树、最后才被挂到屏幕上」，
 * 而 Slate 控件要到 RebuildWidget 才真正存在。若 SetSeries 在此之前被调用，
 * 直接写 Graph->Series 就是空指针。所以这里把数据留在 Pending*，RebuildWidget 时补一次。
 */
UCLASS()
class UWTPowerGraphWidget : public UWidget
{
    GENERATED_BODY()

public:
    /**
     * 换一组曲线。
     * @param InSeries 每条线的原始 MW 值（长度可以不一致，OnPaint 按各自长度拉伸到满宽）
     * @param InMaxY   Y 轴上限，传 0 或负数表示「按本组最大值自动定」
     */
    void SetSeries(const TArray<FWTGraphSeriesRaw>& InSeries, float InMaxY);

    /** 只清空曲线（保留底与网格），用于「该电网刚建好、还没有历史点」。 */
    void ClearSeries();

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
    void ApplyPendingToSlate();

    TSharedPtr<SWTPowerGraph> Graph;

    // Slate 控件尚未创建时的暂存区。
    TArray<FWTGraphSeriesRaw> PendingSeries;
    float PendingMaxY = 1.f;
    bool  bPendingValid = false;
};
