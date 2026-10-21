#pragma once

#include "CoreMinimal.h"
#include "Unlocks/FGUnlockInfoOnly.h"
#include "Remote_FactoryUnlockInfo.generated.h"

/**
 * ⚠️【0.2.43 起已停用 —— 保留仅为存档/对照，不要再拿去挂 mUnlocks】
 *
 * 停用原因（官方 Docs JSON 逐条取证 + skill §11.7）：MAM 奖励区的「文字行」其数据源在
 * **schematic 自己**身上（`mUnlockName`/`mUnlockDescription`），并由一条 **BP 类**的
 * `BP_UnlockInfoOnly_C` 条目渲染 —— 原版 **21/21 处**该条目全是裸的（零自定义字段）。
 * 而 `UFGUnlockInfoOnly` 是 **abstract**，自建原生 C++ 子类（本类）**即使把
 * mUnlockName/mUnlockDescription 填满也渲染不出来**：奖励面板的显示逻辑长在原版**蓝图**里，
 * 原生 unlock 类没有蓝图图 ⇒ spawn 不出 widget（与 §11.6 的 `BP_UnlockRecipe_C` 同一条教训）。
 * ⇒ 0.2.43 改成：`Remote_FactoryUpgradeResearch.cpp` 的 `WT_ApplyRewardText()` 反射写
 *    schematic 字段 + `NewObject<UFGUnlockInfoOnly>(this, BP_UnlockInfoOnly_C)`。
 *
 * ---- 以下为原始说明（0.2.36h）----
 *
 * ★0.2.36h（用户定案「奖励区，写说明和物品」）：MAM 奖励区里**只展示文字说明**的解锁项。
 *
 * 为什么要有这个类：原版 `UFGUnlockInfoOnly` 是 **abstract**（见 Unlocks/FGUnlockInfoOnly.h），
 * 不能 NewObject —— 必须自建一个非 abstract 的子类。它的 mUnlockName / mUnlockDescription /
 * mUnlockIconBig / mUnlockIconSmall 都是 protected，**派生类里可直接写**（C++ 规则），所以
 * 不需要 AccessTransformers。
 *
 * 用法：与 `UFGUnlockRecipe`（物品）**并列**挂进 `UFGSchematic::mUnlocks`：
 *   mUnlocks[0] = UWTUnlockInfo （说明，排在前面）
 *   mUnlocks[1] = UFGUnlockRecipe（配方/物品）
 * 原版 MAM 的奖励面板会逐个渲染 mUnlocks，注释原文：
 *   "Simple class to be used to add unlock data into the UI without unlocking anything"
 *   ⇒ 它只进 UI、不产生任何解锁副作用，正合"写说明"的用途。
 */
UCLASS()
class REMOTE_FACTORY_API UWTUnlockInfo : public UFGUnlockInfoOnly
{
	GENERATED_BODY()

public:
	/** 一次性把名字/说明/图标填好（都是 protected 成员，本派生类可写）。 */
	void WT_Setup(const FText& Name, const FText& Description,
	              UTexture2D* BigIcon, UTexture2D* SmallIcon);
};
