// 里程碑货舱即刻返回（Milestone Cargo Immediate Return）
// 控制面：默认关、随存档持久化（见 Remote_FactorySave）。
//
// ★2026-11-03 v5（真机制，全链一手核实）：
//   v3（盯电梯状态 ticker）/ v4（掐 StartSpaceElevatorUpgradeTimer）全部无效，因为货舱返回时长
//   根本不在电梯状态机里。真实链路（SDK 头逐条核对）：
//
//     UFGSchematic::mTimeToComplete             ← 每个里程碑资产上的「完成后耗时」，默认 600 秒
//       ↑ 读作
//     UFGSchematic::GetTimeToComplete(Class)    ← static，FGSchematic.cpp:50 真实现（非空桩），
//                                                  return inClass.GetDefaultObject()->mTimeToComplete
//       ↑ 被用于计算
//     AFGSchematicManager::mShipLandTimeStamp   ← FGSchematicManager.h:414，Replicated，
//                                                  「飞船将要降落在贸易站的时刻」
//       ↑ 读作
//     AFGSchematicManager::GetTimeUntilShipReturn()  ← 返回「剩余秒数」，UI 倒计时读它
//
//   ⇒ hook GetTimeUntilShipReturn 只是改「读」的显示，改不动已定死的 mShipLandTimeStamp，
//     倒计时归零了船也不会回来（v5 初版即踩此坑，已改）。
//   ⇒ 必须改赋值源：hook UFGSchematic::GetTimeToComplete，让里程碑类型的返回 10 秒，
//     原版据此算出的降落时刻就是「现在 + 10 秒」，船真的 10 秒后返航，UI 同步显示 10。
//
//   安全边界：GetTimeToComplete 也被研究/掉落舱等使用（见 FGLocalSettings.h:129 注释），
//   因此**只在 UFGSchematic::GetType()==EST_Milestone 时**才钳，其它类型原样返回。

#include "Remote_FactoryMilestoneCargoReturn.h"

#include "Remote_FactoryLog.h"

#include "FGSchematic.h"                         // UFGSchematic / GetType / ESchematicType / GetTimeToComplete
#include "Engine/World.h"                        // UWorld（函数参数类型）
#include "Patching/NativeHookManager.h"          // SUBSCRIBE_METHOD / TCallScope（含 static 函数重载）

static bool GMilestoneCargoReturnEnabled = false;

bool Remote_FactoryMilestoneCargoReturn::IsMilestoneCargoReturnEnabled()
{
    return GMilestoneCargoReturnEnabled;
}

void Remote_FactoryMilestoneCargoReturn::SetMilestoneCargoReturnEnabled(bool bEnable)
{
    GMilestoneCargoReturnEnabled = bEnable;
    WT_WARNF(TEXT("[milestone-cargo-return] %s."), bEnable ? TEXT("enabled") : TEXT("disabled"));
}

void Remote_FactoryMilestoneCargoReturn::LoadMilestoneCargoReturnConfig()
{
    // 随存档持久化（Remote_FactorySave 模块 hook AFGGameState::Serialize 写入）；本函数保持空。
}

// ---------------------------------------------------------------------------------------------
// 安装 hook（全局一次；仅依赖 IsMilestoneCargoReturnEnabled 运行时读开关）。
// ---------------------------------------------------------------------------------------------
void Remote_FactoryMilestoneCargoReturn::InstallMilestoneCargoReturnHooks(UWorld* World)
{
    static bool bInstalled = false;
    if (bInstalled)
    {
        WT_WARNF(TEXT("[milestone-cargo-return] Install skipped (already installed once)."));
        return;
    }
    bInstalled = true;

    WT_WARNF(TEXT("[milestone-cargo-return] InstallMilestoneCargoReturnHooks entered (World valid=%s)."),
             World ? TEXT("Y") : TEXT("N"));

    // 诊断：只记前几笔，防刷屏（每帧/每 UI 刷新都可能调）。
    static int32 GDiagLogged = 0;

    // Hook —— 钳里程碑完成耗时：开启且类型为 EST_Milestone 时返回 10 秒。
    // GetTimeToComplete 是 **static** 成员函数（FGSchematic.h:124）；SML 的
    // HookInvoker<R(*)(A...), Callable> 特化（NativeHookManager.h:569）支持全局/静态函数 hook，
    // 因此 SUBSCRIBE_METHOD 可直接用，TCallScope 无 this 参数。
    using FScope = TCallScope<float(*)(TSubclassOf<UFGSchematic>)>;
    TFunction<void(FScope&, TSubclassOf<UFGSchematic>)> Handler =
        [](FScope& Scope, TSubclassOf<UFGSchematic> InClass)
        {
            const float Original = Scope(InClass);   // 先取原版返回值（读资产 mTimeToComplete）
            const bool bEnabled = Remote_FactoryMilestoneCargoReturn::IsMilestoneCargoReturnEnabled();
            if (!bEnabled || Original <= 10.f)
            {
                return;   // 关闭 / 原值已 ≤10：保留原版
            }
            // 只对里程碑生效，避免影响研究 / 掉落舱 / 其它蓝图计时。
            // 判据① 类型 == EST_Milestone（原版自己也这么判，见 FGSchematic.cpp:78）；
            // 判据② 兜底：资产路径含 "Milestone"（原版里程碑资产在 .../Schematic/Milestone/ 下），
            //        防某些资产漏设 mType（其默认值是 EST_Custom）。
            const ESchematicType Type = UFGSchematic::GetType(InClass);
            bool bIsMilestone = (Type == ESchematicType::EST_Milestone);
            if (!bIsMilestone && InClass.Get())
            {
                bIsMilestone = InClass->GetPathName().Contains(TEXT("Milestone"));
            }
            if (!bIsMilestone)
            {
                return;
            }
            if (GDiagLogged < 8)
            {
                ++GDiagLogged;
                WT_WARNF(TEXT("[milestone-cargo-return] DIAG GetTimeToComplete(milestone): original=%.1f -> 10.0 (enabled)."), Original);
            }
            Scope.Override(10.f);
        };
    SUBSCRIBE_METHOD(UFGSchematic::GetTimeToComplete, MoveTemp(Handler));

    WT_WARNF(TEXT("[milestone-cargo-return] hooks installed (clamp UFGSchematic::GetTimeToComplete for EST_Milestone to 10s)."));
}
