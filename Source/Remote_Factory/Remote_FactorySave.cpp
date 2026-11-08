// 按存档持久化（per-save persistence）
// 把「智能仓储」「里程碑货舱即刻返回」两个开关状态随存档保存 / 载入。

#include "Remote_FactorySave.h"

#include "Remote_FactoryLog.h"
#include "Remote_FactorySmartWarehouse.h"
#include "Remote_FactoryMilestoneCargoReturn.h"
#include "FGGameState.h"                  // AFGGameState::Serialize / BeginPlay
#include "Engine/World.h"                 // ENetMode (NM_Client) / GetNetMode
#include "Patching/NativeHookManager.h"   // SUBSCRIBE_METHOD / TCallScope

// 魔数（'W','F','M','S'）+ 版本，用于区分本 mod 写入的存档数据段；旧档无此段则默认关。
static const int32 WT_SAVE_MAGIC   = 0x57464D53;
static const int32 WT_SAVE_VERSION = 1;

// ★2026-11-03：记录「本次会话里刚从存档恢复过设置」的那个 GameState。
//   用于区分「读档进来」与「新开一局」：后者没有存档数据段，必须把两个开关复位成关。
//   用弱引用，GameState 销毁后自动失效，不会悬空。
static TWeakObjectPtr<AFGGameState> G_LastRestoredGameState;

void Remote_FactorySave::InstallPerSaveHooks()
{
    static bool bInstalled = false;
    if (bInstalled)
    {
        return;
    }
    bInstalled = true;

    using FSaveScope = TCallScope<void(*)(AFGGameState*, FArchive&)>;
    auto SaveHandler = TFunction<void(FSaveScope&, AFGGameState*, FArchive&)>(
        [](FSaveScope& Scope, AFGGameState* Self, FArchive& Ar)
        {
            (void)Self;
            Scope(Self, Ar);   // 原版序列化先做（存档 / 读档都走它）

            if (Ar.IsSaving())
            {
                int32 Magic = WT_SAVE_MAGIC;
                int32 Ver   = WT_SAVE_VERSION;
                uint8 bSW   = Remote_FactorySmartWarehouse::IsSmartWarehouseEnabled() ? 1 : 0;
                uint8 bMCR  = Remote_FactoryMilestoneCargoReturn::IsMilestoneCargoReturnEnabled() ? 1 : 0;
                Ar << Magic;
                Ar << Ver;
                Ar << bSW;
                Ar << bMCR;
            }
            else if (Ar.IsLoading())
            {
                int32 Magic = 0;
                int32 Ver   = 0;
                const int64 Pos = Ar.Tell();
                Ar << Magic;
                if (Magic == WT_SAVE_MAGIC)
                {
                    Ar << Ver;
                    (void)Ver;   // v1 仅含两个 bool，版本仅作前向兼容占位
                    uint8 bSW = 0;
                    uint8 bMCR = 0;
                    Ar << bSW;
                    Ar << bMCR;
                    Remote_FactorySmartWarehouse::SetSmartWarehouseEnabled(bSW != 0);
                    Remote_FactoryMilestoneCargoReturn::SetMilestoneCargoReturnEnabled(bMCR != 0);
                    G_LastRestoredGameState = Self;   // 标记：这个 GameState 已从存档恢复过（BeginPlay 时据此不复位）
                    WT_WARNF(TEXT("[wt-save] per-save settings loaded: SmartWarehouse=%d MilestoneCargo=%d"),
                             bSW, bMCR);
                }
                else
                {
                    // ★2026-11-03 补强：旧档 / 不含本 mod 数据段的档 → 必须【显式复位为关】。
                    //   原因：GSmartWarehouseEnabled / GMilestoneCargoReturnEnabled 是进程级静态量，
                    //   若本次会话先读过「开着」的存档，再读一个没有本数据段的档（或开新游戏），
                    //   不复位就会残留成「开」，违反「只有新建存档才默认关」的要求。
                    Remote_FactorySmartWarehouse::SetSmartWarehouseEnabled(false);
                    Remote_FactoryMilestoneCargoReturn::SetMilestoneCargoReturnEnabled(false);
                    Ar.Seek(Pos);   // 回退读取位置，不破坏存档
                    WT_WARNF(TEXT("[wt-save] no per-save settings in this save -> both toggles default OFF"));
                }
            }
        });

    // ★2026-11-04 修正（两次）：
    //   ① AFGGameState::Serialize 与基类 AActor::Serialize 同名 → 需显式签名消歧（否则 decltype 报 overloaded）。
    //   ② 它是【虚函数 override】→ SML 钩虚函数必须提供样本对象实例解析实现地址，否则运行时断言崩溃
    //      ("Attempt to hook virtual function override without providing object instance for implementation resolution")。
    //   故用 UOBJECT_EXPLICIT：显式签名 + GetDefault<AFGGameState>()（CDO，随时可用）作样本实例。
    SUBSCRIBE_UOBJECT_METHOD_EXPLICIT(void(AFGGameState::*)(FArchive&), AFGGameState, Serialize, MoveTemp(SaveHandler));

    // ★2026-11-03 新开一局的复位：AFGGameState::BeginPlay 时，若这个 GameState 不是「刚从存档恢复过」的那个，
    //   说明是新开的存档（没有本 mod 数据段）→ 两个开关复位为「关」，满足「只有新建存档才默认关」。
    //   用弱引用比对，故与 Serialize(IsLoading) / BeginPlay 的先后顺序无关：
    //     · 恢复发生在 BeginPlay 之前 → 弱引用已指向本实例 → 跳过复位，保留存档值；
    //     · 恢复发生在 BeginPlay 之后 → 先复位成关，随后 Serialize 再恢复成存档值。
    //   客户端跳过：这些功能都在服务端权威生效，客户端不要自行复位以免与服务端状态不一致。
    using FBeginPlayScope = TCallScope<void(*)(AFGGameState*)>;
    auto BeginPlayHandler = TFunction<void(FBeginPlayScope&, AFGGameState*)>(
        [](FBeginPlayScope& Scope, AFGGameState* Self)
        {
            Scope(Self);   // 原版逻辑先做
            if (!Self) return;
            if (Self->GetNetMode() == NM_Client) return;
            if (G_LastRestoredGameState.Get() == Self) return;   // 读档恢复过 → 保留存档值
            Remote_FactorySmartWarehouse::SetSmartWarehouseEnabled(false);
            Remote_FactoryMilestoneCargoReturn::SetMilestoneCargoReturnEnabled(false);
            WT_WARNF(TEXT("[wt-save] new game (no saved settings) -> both toggles default OFF"));
        });
    SUBSCRIBE_UOBJECT_METHOD_EXPLICIT(void(AFGGameState::*)(), AFGGameState, BeginPlay, MoveTemp(BeginPlayHandler));

    WT_WARNF(TEXT("[wt-save] per-save persistence hooks installed (SmartWarehouse + MilestoneCargoReturn)."));
}
