#pragma once

#include "CoreMinimal.h"
#include "Components/Button.h"
#include "Remote_FactoryShardTakeButton.generated.h"

class AFGBuildableFactory;

/**
 * 碎片取回按钮（★1.0.8V）。
 *
 * 【为什么需要它】原版超频面板的碎片槽 `Widget_CostSlotWrapper_C` 把「成本物品」写死成
 *   原版碎片 `Desc_CrystalShard_C`（日志实锤：`cost class candidate: Widget_CostSlotWrapper_C
 *   .itemDescriptor = Desc_CrystalShard_C`）。点击槽位取出碎片时，原版按**那个具体类**
 *   去潜在库存里找/移除；而我们的碎片是 `UWTUpgradePowerShardDescriptor` 子类，按具体类
 *   永远数不到 ⇒ **插得进去、取不下来**（用户 2026-10-27 实测）。
 *
 * 【为什么不改原版变量】08T 把 `itemDescriptor` 改写成本类后：画面不变（原版槽是打开面板
 *   时算一次的快照），且取出依旧失败 ⇒ 说明取出链里还有一份写死的类引用（不在 BP 变量上），
 *   改变量这条路走不通。
 *
 * 【本方案】在槽位的图标宿主 `Widget_InventorySlot_C / mContainer`（CanvasPanel，日志实锤存在）
 *   上叠一个**完全透明**的按钮，anchor 铺满整格：
 *     · 左键点击 = 由我们自己把该槽对应的**我们的碎片**从潜在库存转移到玩家背包；
 *     · 只在「该槽确实是我们碎片占用的格」时挂载，原版碎片/空槽一格字节都不碰 ⇒ 原版
 *       碎片照旧能取；
 *     · SButton 不实现 OnDrop ⇒ 玩家从背包**拖入**碎片的操作仍然穿透到原版槽（插入不受影响）；
 *       只有「从槽拖出」被按钮吃掉 —— 那正是原版本来就做不到的事，已由点击替代。
 */
UCLASS()
class UWTShardTakeButton : public UButton
{
    GENERATED_BODY()

public:
    UWTShardTakeButton();

    /** 这块面板指向的机器（弱引用，面板关了不影响）。 */
    UPROPERTY()
    TWeakObjectPtr<AFGBuildableFactory> TargetFactory;

    /** 槽位序号（只用于日志；实际按「第一件我们的碎片」取，索引坍缩也不影响正确性）。 */
    int32 SlotIndex = 0;

    UFUNCTION()
    void HandleClicked();
};
