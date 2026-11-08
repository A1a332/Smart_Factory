#pragma once

#include "CoreMinimal.h"

/**
 * ★1.0.7 云端（位面存储）配额口径 —— 用户 2026-10-27 两次定案：
 *   ①「当前初始是 1000（铁板）… 刚好是翻 10 倍」  ⇒ 未研究 = 原版 1 组 × 10 = 10 组/类
 *   ②「MAM 研究 2 级 ×1.1 倍、3 级 ×1.2 倍如此」  ⇒ 研究加成走**等级系数**，不是原版那套每级 +N 组
 *
 * 原版口径（全部一手核实）：
 *   · `UFGCentralStorageSettings::mDefaultItemStackLimit = 1` 组/类
 *     —— SMLStarterProject/Config/DefaultGame.ini 的 [FGCentralStorageSettings] 段。
 *   · `AFGUnlockSubsystem::mUnlockedCentralStorageItemStackLimit` 逐级累加，
 *     头文件注释原文：*"Added on top of UFGCentralStorageSettings::mDefaultItemLimit"*。
 *   · `AFGCentralStorageSubsystem::UpdateItemStackLimit()` 因此算出
 *     `mItemStackLimit = 默认值 + 解锁累计`，`GetCentralStorageItemLimit(class)` 再 × 该物品堆叠上限。
 *   · 堆叠上限同段配置：SS_SMALL=50 / SS_MEDIUM=100 / SS_BIG=200 / SS_HUGE=500（铁板 = MEDIUM = 100）。
 *
 * 本 mod 的换算（**基线只在这里定义一次**）：
 *   TargetStacks(Lv) = WT_InitialStacks × Factor(Lv)，Factor(Lv) = 1.0 + 0.1 × (Lv - 1)
 *
 *   | 等级 | 系数 | 组数/类 | 铁板（100/组） |
 *   |------|------|---------|----------------|
 *   | 未研究 | 1.0  | 10      | 1,000          |
 *   | Lv1  | 1.1  | 11      | 1,100          |
 *   | Lv2  | 1.1  | 11      | 1,100          |
 *   | Lv3  | 1.2  | 12      | 1,200          |
 *   | Lv16 | 2.5  | 25      | 2,500          |
 *   | Lv17~20（常规）| 2.6 / 2.7 / 2.8 / 2.9 | 26 / 27 / 28 / 29 | 2,600 ~ 2,900 |
 *   | Lv17~20（碎片路线）| 2.7 / 2.8 / 2.9 / 3.0 | 27 / 28 / 29 / 30 | 2,700 ~ 3,000 |
 *
 * ★末段碎片路线为什么 +0.1：原版里碎片路线每级比常规多 +1 组（常规 Lv17~20 = 34/36/38/40，
 *   碎片 = 35/38/41/44）。在「固定 10 组 × 系数」口径下，0.1 系数正好等于 1 组 ⇒ 语义原样保留。
 *
 * ★★★1.0.7 追加（用户 2026-10-27 三次定案：「MAM 现在不加上去」）：
 *   本版**不发放任何 MAM 容量加成** —— 云端配额恒为开局那 10 组/类（铁板 1,000 个），
 *   上面那张等级系数表只是**代码里备好的口径**，由 `WT_FEATURE_CLOUD_QUOTA_MAM` 总闸关着。
 *   关闸的实现方式 = 让 `FactorPercent` 恒返回 100% ⇒ `TargetStacks` 恒等于 `WT_InitialStacks`
 *   ⇒ `Remote_FactoryUpgradeResearch` 里逐级 unlock 增量恒为 0（=不发 unlock），
 *   三条引用点（GameWorldModule 基线 / 研究 unlock 增量 / 奖励文案）**自动一致**，不必各写一份判断。
 *   ★为什么不直接删掉系数代码：用户口径是「现在不加」，不是「永久不要」——
 *     翻回 true 即恢复整张表，且不需要重新对拍三个引用点。
 *
 * ★为什么把系数烘进「默认值」和「unlock 增量」这两个输入，而不是乘在 `mItemStackLimit` 的结果上：
 *   `mItemStackLimit` 是 Transient 字段，原版在读档 / 每次解锁时都会重算并覆盖；
 *   只有让每次重算都得出同一个值，才不用 hook 原版的重算时机，也不会被读档打回原形。
 *   另：`mUnlockedCentralStorageItemStackLimit` 是**裸 int32（无 UPROPERTY ⇒ 不存档）**，
 *   读档后由已购研究重新 Apply 累加 ⇒ 改增量数值对老存档自动追溯，不需要迁移脚本。
 */
namespace Remote_FactoryCloudQuota
{
    /**
     * ★★★1.0.7 云端配额「按 MAM 等级放大」总闸（用户 2026-10-27 定案：「MAM 现在不加上去」）。
     *
     * false（当前）= 云端配额恒为 `WT_InitialStacks` 组/类（= 原版未研究的 10 倍，铁板 1,000 个）；
     *                 MAM 容量轨的每一级都不再发放 `mItemStackLimitIncrease`（Increase 恒 0 ⇒
     *                 `mUnlocks` 里不会出现配额 unlock 条目）。
     * true        = 按下面 `FactorPercent` 的等级系数表生效（Lv1 ×1.1、Lv2 ×1.2 … 末段 ×2.9/×3.0）。
     *
     * ★三处引用点全部走本开关（缺一处就会「界面写 11 组、实际只有 10 组」这种谎）：
     *   ① 本文件 FactorPercent / TargetStacks —— 数值唯一来源；
     *   ② Remote_FactoryUpgradeResearch.cpp 配额 unlock 段（增量 = 本级目标 − 上一级目标）；
     *   ③ 同上 WTRewardDescription(ti=0) —— 文案（关闸时改说「固定 N 组、不随研究提升」）。
     * 另：Remote_FactoryGameWorldModule.cpp 的 `[cloud-quota]` 诊断日志会打 `mam-scaling=off/on`。
     *
     * 注意：MAM 研究另有总闸 `WT_FEATURE_MAM_RESEARCH`（Remote_FactoryRegistry.h，2026-11-05 已翻 true）。
     *
     * ★★★2026-11-05 翻闸（用户原话「云端配额的也做了，我说的是云端的四个建造最后修」）：
     *   本闸 = **true**。⇒ 云端配额按下面等级系数表随 MAM 容量轨逐级放大：
     *     未研究 = 10 组/类（铁板 1,000）→ Lv1 = 11 → Lv2 = 12 → Lv16 = 26 → 末段常规 27~30 / 碎片 28~31。
     *   ★「云端最后修」指的是**四个云端建造站**（上传/下载/流体上传/流体下载）的毛病，
     *     与配额无关 —— 那四台的解锁已改到 T5 / T7 两张卡上，功能问题留到最后单独一轮。
     */
    constexpr bool WT_FEATURE_CLOUD_QUOTA_MAM = true;

    /** 原版未研究时每类可囤的组数（DefaultGame.ini 的 mDefaultItemStackLimit）。 */
    constexpr int32 WT_VanillaUnresearchedStacks = 1;

    /** ★用户定案的倍数：未研究 = 原版 × 这个数。 */
    constexpr int32 WT_Multiplier = 10;

    /** 开局生效的组数/类（`WT_ApplyCloudInitialQuota` 写进 CDO 的值）。 */
    constexpr int32 WT_InitialStacks = WT_VanillaUnresearchedStacks * WT_Multiplier;

    /** 等级系数的步长（百分点）：Lv1 = 100%，每升一级 +10 个百分点。 */
    constexpr int32 WT_FactorStepPercent = 10;

    /** Lv1 的系数（百分点）= 100；配合下方 FactorPercent 的 `*(Lv)` 公式 ⇒ Lv1 实际 = 110%（×1.1，前移一级）。
     *  容量 Lv1 = 11 组（原版基线 10 组 +1），Lv16 = 26 组，Lv20常规 = 30 组 / 碎片路线 31 组。详见桌面设计表 §6.2。 */
    constexpr int32 WT_FactorLevel1Percent = 100;

    /** 末段碎片路线额外加多少百分点（原版「每级再多 1 组」⇒ 10 组基线下 = +0.1）。 */
    constexpr int32 WT_FinalShardBonusPercent = 10;

    /** MAM 末段（常规 / 碎片两条路线二选一）的首级与末级。 */
    constexpr int32 WT_FinalFirstLevel = 17;
    constexpr int32 WT_FinalLastLevel  = 20;

    /**
     * 等级系数（百分点，前移）：Lv1 = 110，Lv2 = 120，Lv3 = 130 … Lv16 = 260，Lv20常规 = 300（碎片路线 Lv17~20 再 +10/级）。
     * ★闸门关着时恒返回 100（= 不放大），调用点不必自己判闸。
     * @param Level   1-based 等级
     * @param bShard  末段是否走强化碎片路线（只有 Lv17~20 才可能为 true）
     */
    FORCEINLINE int32 FactorPercent(int32 Level, bool bShard)
    {
        if (!WT_FEATURE_CLOUD_QUOTA_MAM)
        {
            return 100;   // 100% = 原样，不放大
        }
        const int32 Lv = FMath::Max(1, Level);
        int32 Percent = WT_FactorLevel1Percent + WT_FactorStepPercent * Lv;
        if (bShard)
        {
            Percent += WT_FinalShardBonusPercent;
        }
        return Percent;
    }

    /** 该等级的目标组数/类（四舍五入到整组）。闸门关着时恒等于开局基线。 */
    FORCEINLINE int32 TargetStacks(int32 Level, bool bShard)
    {
        if (!WT_FEATURE_CLOUD_QUOTA_MAM)
        {
            return WT_InitialStacks;   // 任意等级都是 10 组/类
        }
        return (WT_InitialStacks * FactorPercent(Level, bShard) + 50) / 100;
    }
}
