#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"   // ★2026-11-02 UWorld* 形参需要完整类型（CoreMinimal 不带入 UWorld）
#include "Resources/FGItemDescriptor.h"   // UFGItemDescriptor（TSubclassOf 键，需要完整类型；本文件用到 TSubclassOf<UFGItemDescriptor>）

class UFGInventoryComponent;   // ★2026-11-03 前向声明：TransferFromContainersToInventory 形参用到指针，本头不引入完整类型

/**
 * 智能仓储（Smart Warehouse）
 * ========================
 * ★2026-11-02 简化（用户定案「无列表，直接从集装箱中取用」）：物品**始终留在原版存储集装箱**，
 * 不抽进任何池、不写原版中央存储池 AFGCentralStorageSubsystem::mStoredItems（云端池零污染）。
 *
 * 机制 = 集装箱直取 + hook 原版中央存储计数/扣料 API：
 *   · 可用量缓存：客户端/服务端各自每 ~1s 聚合一次全部原版存储集装箱库存（物品留在原处，仅统计），
 *     供计数 hook 与成本格染蓝共用（见 Remote_FactorySmartWarehouse.cpp 的 G_ContainerAvailability）。
 *   · 计数 hook：SUBSCRIBE_METHOD(GetNumItemsFromCentralStorage) 在云端返回值上加集装箱可用量，
 *     使客户端材料检查与成本格 UI 看到「云端 + 集装箱」总量 —— 客户端才不拦建造。
 *   · 扣料 hook：SUBSCRIBE_METHOD(TryRemoveItemsFromCentralStorage) 先让原版从 mStoredItems(云端) 扣，
 *     不足部分由集装箱直取补足（物品原地移除、被建造消耗），返回总量 —— 建造/制作便从集装箱真实取料。
 *   · 蓝色染色：成本格 widget 读集装箱可用量把该物品格染蓝（见 CostSlotTint），与云端紫区分。
 *
 * 取物优先级（用户 2026-11-02）：背包 → 云端(mStoredItems) → 智能仓储(集装箱直取)。前两者属原版不改；
 * 智能仓储是独立第三源，靠上面两个 hook 接入原版建造/制作扣料路径。语义 = 直取（物品留在集装箱，按需消耗）。
 *
 * 默认【关闭】：建造终端后玩家在面板手动开启（见终端面板 SmartWarehouseButton）。
 *
 * 一手依据（SDK 头文件核验，非凭记忆）：FGCentralStorageSubsystem.h（GetNumItemsFromCentralStorage /
 * TryRemoveItemsFromCentralStorage 均非虚、mStoredItems=TArray<FItemAmount> 本模块不写）；
 * NativeHookManager.h（SUBSCRIBE_METHOD / TCallScope::GetResult / Override）；
 * FGInventoryComponent.h:347 RemoveFromIndex(idx,num,target=nullptr)->void（直接移出，不转存）；
 * FGBuildableSubsystem.h GetAllBuildablesRef；FGBuildableStorage.h GetStorageInventory。
 */
namespace Remote_FactorySmartWarehouse
{
    /**
     * ★2026-11-02 在权威世界上启动「集装箱可用量刷新」核心 ticker（游戏线程），并安装原版中央存储 hook。
     * 客户端世界也安装 hook + 刷新（客户端需要可用量做材料检查与染蓝）。同一世界幂等（弱引用判重）。
     * World 必须来自 POST_INITIALIZATION。
     */
    void RegisterHook(UWorld* World);

    /**
     * ★2026-11-02 智能仓储开关（终端面板可切换）。默认【关】；状态持久化到 Smart_Factory.ini（[SmartWarehouse] bEnabled）。
     * 关闭后 ticker 清空可用量，hook 加 0，行为等同原版（集装箱内容保持原位）。
     */
    bool IsSmartWarehouseEnabled();
    void SetSmartWarehouseEnabled(bool bEnable);
    /** 模块初始化（RegisterHook 内）调用一次：从 ini 读取持久化状态，找不到 key 时保持默认关。 */
    void LoadSmartWarehouseConfig();

    /**
     * ★2026-11-02 返回当前集装箱可用量缓存（客户端/服务端各自刷新）。成本格染蓝与材料检查共用。
     * 物品始终留在集装箱，此 map 仅统计、不持有。
     */
    const TMap<TSubclassOf<UFGItemDescriptor>, int32>& GetContainerAvailability();

    /**
     * ★2026-11-03 新增：返回【纯原版云端】存量（mStoredItems，不含集装箱量）。
     * 成因：GetNumItemsFromCentralStorage 已被本模块 hook 成「云端+集装箱」，成本格要按
     * 「背包 / 位面云端 / 集装箱」三源比数量选颜色，必须能单独拿到纯云端值。
     */
    const TMap<TSubclassOf<UFGItemDescriptor>, int32>& GetCentralStorageRealCount();

    /**
     * ★2026-11-03 修（客户端可用量不刷新）：刷新/清空 G_ContainerAvailability（自带「关→清空」门控）。
     * 专用服务器下服务端的 ticker 不跑在客户端进程，故改由确定跑在客户端的成本格 ticker 兜底调用，
     * 保证客户端成本格染蓝与计数 hook 能看到集装箱可用量（否则 sw=0→不染蓝露原版紫；制作可用量不随库存变）。
     */
    void RefreshContainerAvailability(UWorld* World);

    /**
     * ★2026-11-03 集装箱面板「取出」：把 Want 个 Cls 从全部原版存储集装箱移除并转入 TargetInv（玩家背包）。
     * ★仅服务端权威世界调用★（集装箱是 actor 库存，客户端改了不会同步、等于没拿）。返回实际转移量。
     * 面板是客户端 widget，须经 RCO 的 Server_TakeFromContainer RPC 进来，不要直接调。
     *
     * 为什么要单独开这个公开入口：内部实现走的是"先加后删"（AddStack 拿真实加入数 → 只删那么多），
     * 而不是原版 RemoveFromIndex(idx,num,targetInventory) 的二合一语义 —— 后者先删后塞、塞失败不回报，
     * 在这个调用场景下会把物品直接吞掉（用户实测"取出来背包里没有"）。见 .cpp 内 WT_TransferFromContainersToInventory。
     *
     * 玩家会遇到的情况：
     *   · 背包满       → 返回 0，集装箱一个都不减
     *   · 只放得下部分 → 返回部分量，剩下留在箱里
     *   · 集装箱里没有 → 返回 0
     * 调用方（RCO）只需记日志，不需要额外补偿逻辑。
     */
    int32 TransferFromContainersToInventory(UWorld* World, TSubclassOf<UFGItemDescriptor> Cls, int32 Want, UFGInventoryComponent* TargetInv, AActor* OnlyContainer = nullptr);

    /**
     * ★2026-11-05 集装箱面板「存入」：把 SourceInv（玩家背包）里的 Want 个 Cls 搬进全部原版存储集装箱。
     * ★仅服务端权威世界调用★（集装箱是 actor 库存，客户端改了不同步，等于白存）。返回实际存入量。
     * 面板是客户端 widget，须经 RCO 的 Server_StoreToContainer / Server_StoreAllToContainers RPC 进来。
     *
     * ★与取出镜像，同样是"先加后删"：先 AddStack 拿真实入箱数，再从背包只删那么多
     *   ⇒ 背包满/箱子满/物品被拒收时，两边都不会凭空少东西。绝不倒过来写（先删后加会把物品吞掉）。
     *
     * 玩家会遇到的情况：
     *   · 没有集装箱 / 全都满了 → 返回 0，背包一个不减
     *   · 只塞得下一部分       → 返回部分量，其余留在背包
     *   · 背包里没有           → 返回 0
     *   · 装备/消耗品（如喷气背包、医疗包） → 返回 0，见 IsStorableMaterial
     *     ★弹药不算：2026-11-05 起子弹可以正常存/取。
     */
    int32 TransferFromInventoryToContainers(UWorld* World, TSubclassOf<UFGItemDescriptor> Cls, int32 Want, UFGInventoryComponent* SourceInv, AActor* OnlyContainer = nullptr);

    /**
     * ★2026-11-05 判「建造材料类」—— 存入只处理这类，其余一律不存（返回 false）。
     * 用户定案：不要把身上在用的东西莫名其妙存走。
     *
     * 排除清单与一手依据：
     *   · UFGEquipmentDescriptor 及其子类 —— 武器/工具/护甲/喷气背包…
     *     ★FGConsumableDescriptor.h:13 `UFGConsumableDescriptor : public UFGEquipmentDescriptor`
     *       ⇒ 排除 Equipment 就**同时**排除了全部消耗品（药、燃料罐等），不必单独判。
     *   · UFGAmmoType 及其子类（各类弹药）—— FGAmmoType.h:55 `UFGAmmoType : public UFGItemDescriptor`，
     *     是**独立支系**、不是 Equipment 子类，所以必须单独排除。
     *   · UFGNoneDescriptor / UFGOverflowDescriptor / UFGWildCardDescriptor —— 引擎/原版内部占位类，
     *     玩家背包里本不该出现，但背包遍历可能扫到，存进去只会制造脏数据。
     */
    bool IsStorableMaterial(TSubclassOf<UFGItemDescriptor> Cls);
}

namespace Remote_FactorySmartWarehouse
{
    /**
     * ★2026-11-05 单个存储容器的快照（面板「一个容器一行」用）。
     * ★为什么不塞 inventory 指针进 UI：面板只需要"看"和"点取/存"，取/存走 RPC 时
     *   只带**容器 actor 指针**（服务端自己重新解析 inventory），这样箱子被拆掉也不会留下悬空引用。
     */
    struct FContainerSnapshot
    {
        /** 容器本体（用于「只从这个容器取 / 只存进这个容器」）。弱引用，箱子拆了就自动失效。 */
        TWeakObjectPtr<AActor> Storage;
        /** 容器显示名（"集装箱 1" / "工业集装箱 2" 这种，按遍历顺序编号）。 */
        FString Label;
        /** 分组键 = 集装箱型号友好名（"存储集装箱" 等）。同型号多个箱子在面板合并成一行。 */
        FString BoxType;
        /** 该容器内每种物品的实际数量。★这是**单个容器**的量，不是全部容器的总和。 */
        TMap<TSubclassOf<UFGItemDescriptor>, int32> Items;
        /** 物品种类数 / 物品总件数（容器行收起时显示的概览）。 */
        int32 NumKinds = 0;
        int32 NumItems = 0;

        /** 弱引用是否仍然有效（箱子还在世界上）。 */
        bool IsValid() const { return Storage.IsValid(); }
    };

    /**
     * ★2026-11-05 采集世界上每个存储容器各自的库存快照（排除本 mod 云站）。
     * 与 GetContainerAvailability 的区别：那个是**所有容器合并后**的每物品总量，
     * 这个是**按容器分开**的。面板「一个容器一行」用这个 —— 同一物品存在多个箱子时，
     * 每个箱子各显示自己的数量，而不是被合并成一个看不出出处的总数。
     */
    void GetContainerSnapshots(UWorld* World, TArray<FContainerSnapshot>& Out);
}
