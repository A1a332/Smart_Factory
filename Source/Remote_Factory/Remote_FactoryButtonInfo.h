#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Remote_FactoryButtonInfo.generated.h"

/** Which list a generated button belongs to. */
UENUM()
enum class EWT_ButtonKind : uint8
{
    Category,
    MachineRow,
    /** A row in the alien-energy collectible board. */
    CollectibleRow,
    /** ★0.2.62: a top-level group toggle (建筑 / 容器 / 外星科技). Index = group id (0..2). */
    TopToggle,
    /** ★0.2.62: an alien-energy KIND selector (Power Slug / Mercer Sphere / ...). Index = EWT_CollectibleKind. */
    CollectibleKind,
    // ★1.0.8 移除 NodePurity / ProdStateFilter 两个 kind：
    // 它们是为「横排互斥按钮」形态准备的，用户定案改「下选箭头」后筛选一律走 UComboBoxString，
    // 再也没有任何代码会生成这两种按钮 ⇒ 留着就是「点了没反应」的死枚举，直接删。
    // 枚举值 5/6 保留空号不复用（无存档/网络序列化依赖，纯 UI 瞬时结构）。

    /** ★1.0.9 【电网】类目里的一条电路行（点它切换上方曲线）。
     *  Index = FWT_MachineListReply::Grids 的下标 —— 那是服务端扫描时按 UFGPowerCircuit
     *  去重生成的数组，不是 Entries 的下标，别混用。 */
    GridRow
};

/**
 * Describes one generated list row: what clicking it means.
 *
 * UMG's UButton::OnClicked is a parameterless dynamic delegate, so a clicked button cannot tell
 * the handler which row it stands for. The panel side-steps this by registering the click through
 * the underlying Slate SButton instead (see Remote_FactoryPanelWidget::AddListButton), where a
 * lambda CAN capture this struct — giving each row its own closure with no lookup at click time.
 *
 * It is a plain USTRUCT (not a UObject) because the lambda captures a copy by value, so there is
 * nothing for the GC to track and no lifetime to get wrong.
 */
USTRUCT()
struct FWT_ButtonInfo
{
    GENERATED_BODY()

    /** Category buttons carry an EWT_MachineCategory; row buttons carry an entry index. */
    UPROPERTY()
    uint8 Kind = 0;

    /** Payload: category id for Category buttons, entry index for MachineRow buttons. */
    UPROPERTY()
    int32 Index = INDEX_NONE;
};

