#pragma once

#include "CoreMinimal.h"

class UWorld;   // 仅作指针参数前向声明（CoreMinimal 不含 Engine 的 UWorld）

/**
 * 里程碑货舱即刻返回（Milestone Cargo Immediate Return）
 * =====================================================
 * ★2026-11-02 新增功能（用户定案：默认关闭，写在终端面板中，与「智能仓储」开关并列）。
 * 开启后把「枢纽自带飞行载具」的【返回时间】缩短至 10 秒。
 *
 * 控制面（本文件 + 面板按钮 MilestoneCargoReturnButton）：
 *   · 默认【关闭】；状态持久化到 Smart_Factory.ini（[MilestoneCargoReturn] bEnabled）。
 *   · 钥匙不存在时保持关闭（与智能仓储同一口径）。
 *
 * 行为接线（见 Remote_FactoryMilestoneCargoReturn.cpp，v4）：
 *   直接掐【返回计时器】本身——一手核实 FGBuildableSpaceElevator.h 的
 *   StartSpaceElevatorUpgradeTimer(time) 注释明写「启动计时器，到期把状态设为 ESES_Load」，
 *   即货舱返回的官方计时器。开启时把 time 钳到 10 秒（并兜底在进 ESES_Send 时主动启动 10 秒计时）。
 */
namespace Remote_FactoryMilestoneCargoReturn
{
    /** 默认【关】：钥匙不存在时保持关闭。 */
    bool IsMilestoneCargoReturnEnabled();

    /** 切换并持久化到 Smart_Factory.ini。bEnable=true 时（行为接线后）缩短返回时间至 10 秒。 */
    void SetMilestoneCargoReturnEnabled(bool bEnable);

    /** 模块初始化（GameWorldModule POST_INITIALIZATION）调用一次：从 ini 读取持久化状态。 */
    void LoadMilestoneCargoReturnConfig();

    /** 安装「返回时间缩短至 10 秒」的状态 ticker（全局一次；依赖 LoadMilestoneCargoReturnConfig 已设好开关）。 */
    void InstallMilestoneCargoReturnHooks(UWorld* World);
}
