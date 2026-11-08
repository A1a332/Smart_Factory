#pragma once

#include "CoreMinimal.h"

class UWorld;

/**
 * ★2026-11-02 成本格蓝色染色（智能仓储来源指示）。
 *
 * 为什么需要：智能仓储把容器货抽进了和原版中央存储【同一个】池（mStoredItems），
 * 所以原版成本格 Widget_CostSlotWrapper 从云端取货时只会显示成原版的「中央存储色」，
 * 无法区分「这货是真·中央存储建筑里的」还是「智能仓储容器抽上来的」。原版只认两色
 * （背包橙 GetSatisfactoryOrange / 中央存储 GetSatisfactoryDarkGray），没有第三色。
 *
 * 做法：不跟原版材质参数较劲（参数名/映射未知，盲改必错），而是运行时给成本格根面板
 * 叠一个半透明蓝 UBorder——这一定可见、不依赖原版内部材质。判定用我们自己的贡献量
 * 账本 GetClientSWContribution()：当某物品「中央存储可供应量里全部来自智能仓储容器」
 * （realCloud==0 且 sw>0），即背包+真云端都不够、靠智能仓储补足时，把该格染蓝。
 *
 * 扩展点：颜色逻辑在蓝图里（SetSlotColor/SelectColor 不在 C++ SDK），C++ 不能直改图，
 * 故用运行时叠加覆盖层方案，最稳。
 */
namespace RemoteFactoryCostSlotTint
{
    /** 注册客户端成本格蓝染 ticker（UI 仅客户端）。幂等。 */
    void Register(UWorld* World);
}
