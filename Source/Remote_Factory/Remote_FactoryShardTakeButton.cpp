#include "Remote_FactoryShardTakeButton.h"

#include "Buildables/FGBuildableFactory.h"
#include "FGCharacterPlayer.h"
#include "FGInventoryComponent.h"
#include "GameFramework/PlayerController.h"
#include "Remote_FactoryLog.h"
#include "Remote_FactoryUpgradeRegistry.h"

UWTShardTakeButton::UWTShardTakeButton()
{
    // 动态多播：UMG 的 UButton::OnClicked 无参数，所以每个槽一个按钮实例，各自记着自己的槽号。
    OnClicked.AddDynamic(this, &UWTShardTakeButton::HandleClicked);
}

void UWTShardTakeButton::HandleClicked()
{
    AFGBuildableFactory* Factory = TargetFactory.Get();
    if (!Factory)
    {
        WT_WARNF(TEXT("[shard-take] slot %d: factory gone -- click ignored"), SlotIndex);
        return;
    }

    UFGInventoryComponent* Pot = Factory->GetPotentialInventory();
    if (!Pot)
    {
        WT_WARNF(TEXT("[shard-take] slot %d: %s has no potential inventory -- click ignored"),
                 SlotIndex, *Factory->GetClass()->GetName());
        return;
    }

    // 找第一件「我们的碎片」。不按槽号取是刻意的：RemoveFromIndex 之后槽位会坍缩，
    // 而玩家眼里的 3 格内容物完全等价，取走任意一件的体验都和原版一致。
    UClass* Ours = UWTUpgradePowerShardDescriptor::StaticClass();
    int32 FoundIdx = INDEX_NONE;
    FInventoryStack FoundStack;
    for (int32 i = 0; i < Pot->GetSizeLinear(); ++i)
    {
        FInventoryStack S;
        if (!Pot->GetStackFromIndex(i, S) || !S.HasItems())
        {
            continue;
        }
        UClass* C = S.Item.GetItemClass();
        if (C && C->IsChildOf(Ours))
        {
            FoundIdx   = i;
            FoundStack = S;
            break;
        }
    }

    const int32 BeforePot  = Pot->GetNumItems(Ours);
    if (FoundIdx == INDEX_NONE || BeforePot <= 0)
    {
        WT_WARNF(TEXT("[shard-take] slot %d: no enhanced shard in potential (ours=%d) -- click ignored"),
                 SlotIndex, BeforePot);
        return;
    }

    AFGCharacterPlayer* Player = nullptr;
    if (UWorld* W = Factory->GetWorld())
    {
        if (APlayerController* PC = W->GetFirstPlayerController())
        {
            Player = Cast<AFGCharacterPlayer>(PC->GetPawn());
        }
    }
    UFGInventoryComponent* PlayerInv = Player ? Player->GetInventory() : nullptr;
    if (!PlayerInv)
    {
        WT_WARNF(TEXT("[shard-take] slot %d: player inventory unavailable -- click ignored"), SlotIndex);
        return;
    }

    // ★绝不丢物品：先确认背包放得下，再让库存把这一件直接转进去。
    const int32 BeforePlayer = PlayerInv->GetNumItems(Ours);
    FInventoryItem OneItem = FoundStack.Item;
    if (!PlayerInv->HasEnoughSpaceForItem(OneItem))
    {
        WT_WARNF(TEXT("[shard-take] slot %d: player inventory full -- click ignored (ours pot=%d player=%d)"),
                 SlotIndex, BeforePot, BeforePlayer);
        return;
    }

    Pot->RemoveFromIndex(FoundIdx, 1, PlayerInv);

    const int32 AfterPot    = Pot->GetNumItems(Ours);
    const int32 AfterPlayer = PlayerInv->GetNumItems(Ours);
    WT_WARNF(TEXT("[shard-take] slot %d: took 1 from potential idx=%d (%s) | pot %d->%d player %d->%d"),
             SlotIndex, FoundIdx, *Ours->GetName(),
             BeforePot, AfterPot, BeforePlayer, AfterPlayer);

    // 事后核对：正常情况下 pot 少 1、player 多 1。两者都不动 = 移除没生效；
    // pot 少 1 而 player 没多 = 物品蒸发（下一版改成手动 Add+Remove 两步）。
    if (AfterPot == BeforePot && AfterPlayer == BeforePlayer)
    {
        WT_ERRF(TEXT("[shard-take] slot %d: RemoveFromIndex had NO effect -- report this line"), SlotIndex);
    }
    else if (AfterPot < BeforePot && AfterPlayer <= BeforePlayer)
    {
        WT_ERRF(TEXT("[shard-take] slot %d: shard vanished instead of moving to player -- report this line"),
                SlotIndex);
    }
}
