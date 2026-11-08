#pragma once

class UWorld;

/**
 * ★2026-11-03 智能仓储左侧面板（背包打开时停靠在【物品栏】左侧，列出全局集装箱货物）。
 *
 * 真正实现是一个内部 UCLASS（UUserWidget 子类），定义见 Remote_FactoryWarehousePanelWidget.h / .cpp。
 * 本头故意【不含 UCLASS / generated.h】，只向 GameWorldModule 暴露一个自由函数，
 * 避免外部 TU 在编译期依赖 UHT 生成头——此前因 UHT 竞态，GameWorldModule 编译时认不出面板类（C2653）。
 *
 * ─────────────────────────────────────────────────────────────────────────
 * 本功能 =「智能仓储集装箱面板」。它由三块拼起来，缺一不可：
 *
 *  【1】面板本体（本文件 + WarehousePanelWidget.h/.cpp）
 *     纯 C++ UUserWidget，不依赖原版背包蓝图的任何子节点。
 *     结构：深色底板 → 标题栏(含 ×) + 搜索框 + 滚动物品列表；**无底部手动上传**（用户定案）。
 *     每行 = 图标 + 名称 + 「背N/云N」+「箱N」+【取出】按钮。
 *     位置：屏幕左侧那条空白带，顶边/底边与紧邻的原版位面面板齐平（读原版控件几何对齐，不是写死像素）。
 *
 *  【2】取货链路（RCO RPC → 智能仓储转移）
 *     面板是客户端 widget，集装箱是 actor 库存（客户端改不同步）⇒ 必须走服务端：
 *       Row::OnTakeClicked（读 Shift/Ctrl 决定数量）
 *         → Panel::RequestTakeFromContainer
 *           → URemote_FactoryRCO::Server_TakeFromContainer（Server, Reliable）
 *             → Remote_FactorySmartWarehouse::TransferFromContainersToInventory（先加后删，杜绝蒸发）
 *
 *  【3】显示/隐藏状态机（TickRefresh + OnInventoryToggled）
 *     判据 = 自观测原版背包 widget 是否可见（不押在委托上），并在暂停菜单期间冻结状态。
 *
 * ─────────────────────────────────────────────────────────────────────────
 * 用户总纲（面板行为，改动前先读）：
 *   · 打开背包/位面时，左侧空白带出现这个列表，和原版【位面】面板并排。
 *   · 列表【只显示全局集装箱货物】（聚合总量，不分哪个集装箱），绝不进入原版位面列表。
 *   · 每行显示该物品的【背包 / 位面 / 集装箱】三类可用量。
 *   · 颜色规则：背包最多→橙、位面最多→紫、集装箱最多→蓝（严格大于，并列不染色）。
 *   · 智能仓储只管固体、不碰流体（流体另有「流体仓储」，不许混进来）。
 *   · ★UI 命名一律用原版叫法，不自创；下载做成最简单：点【取出】即可，不另造下载流程。
 *   · ★原版上传站 UI/逻辑一律不改，我们只在「取出/下载」方向做集装箱差异。
 *
 * 已踩过的坑（改动前务必看，每条都曾让用户实机复现过）：
 *   ① 面板不出现 —— RegisterWorld 早于 PlayerController 创建，EnsurePanelCreated 静默失败 ⇒ 加重试 ticker。
 *   ② 面板过一会儿消失 —— AddDynamic 委托会静默失效 ⇒ 改自观测 widget 可见性。
 *   ③ 按 ESC 面板消失 —— 暂停菜单把背包顶掉、广播 open=0，不是玩家关背包 ⇒ 暂停期间冻结状态。
 *   ④ 取出的物品凭空消失 —— RemoveFromIndex(idx,num,target) 先删后塞、塞失败不回报 ⇒ 改「先加后删」。
 *   ⑤ 滑入动画不平滑 / 把面板推出屏幕 —— ticker 0.5s 太粗；且不能读当前 Slot Y 叠加偏移。
 *   ⑥ 与位面面板顶底不齐 —— 锚点必须是 (0,0)+Alignment(0,0)，读参照面板 RefTop/RefBottom。
 */
void Remote_FactoryWarehousePanel_Init(UWorld* World);
