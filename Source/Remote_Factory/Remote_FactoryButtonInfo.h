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
    CollectibleKind
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

