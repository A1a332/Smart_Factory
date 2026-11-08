// Smart Warehouse (智能仓储) — 建造/制作缺料时，直接从原版存储集装箱取用（物品始终留在集装箱，不进任何池）。
//
// ★2026-11-02 简化（用户定案「无列表，直接从集装箱中取用」）：
//   彻底移除上一版的「独立池 G_SmartWarehousePool」与「把货抽进池」的逻辑。物品**始终留在集装箱里**，
//   只在建造/制作缺料、原版中央存储(mStoredItems=云端池)不够时，由扣料 hook **直接从集装箱补足差额**。
//   这是与云端完全解耦的实现：永不写 mStoredItems，云端池零污染、云端终端不显示集装箱货。
//
// 机制（集装箱直取 + 两个 hook，均不碰 mStoredItems）：
//   · 可用量缓存 G_ContainerAvailability：服务端/客户端各自每 ~1s 聚合一次全部原版存储集装箱的库存
//     （物品留在原处，仅统计；排除本 mod 云站）。供下面两个 hook 与成本格染蓝共用。
//   · 计数 hook：SUBSCRIBE_METHOD(GetNumItemsFromCentralStorage) 在智能仓储开启时**无条件**在云端返回值上加集装箱可用量
//     —— 建造 / 制作台 / MAM / 任意成本校验界面都能看到并使用集装箱货（物品始终留在集装箱，云端池不污染）。
//     说明：智能仓储语义上云端保持为空，位面存储站面板列表为空、不会显示被抬高的计数（仅当云端与集装箱同时存有同物品时
//     才可能偶发面板数字偏高，属无害显示）；专用服务器恒加（服务端校验需要集装箱量）。
//   · 扣料 hook：SUBSCRIBE_METHOD(TryRemoveItemsFromCentralStorage) 先让原版从 mStoredItems(云端) 扣，
//     不足部分由 WT_PullFromContainers **直接从集装箱移除**(target=nullptr 即不转存、被建造消耗) 补足，返回总量。
//   · 蓝色染色：成本格 widget 读集装箱可用量把该物品格染蓝（见 Remote_FactoryCostSlotTint），与云端紫区分。
//
// 取物优先级（用户 2026-11-02）：背包 → 云端(mStoredItems) → 智能仓储(集装箱直取)。前两者属原版不改；
// 智能仓储是独立第三源，靠上面两个 hook 接入原版建造/制作扣料路径。语义 = 直取（物品留在集装箱，按需消耗）。
//
// 一手依据（全部来自 SDK 头文件核验，非凭记忆）：
//   · FGCentralStorageSubsystem.h  GetNumItemsFromCentralStorage(Cls) const -> int32（非虚，读 mStoredItems）
//     / TryRemoveItemsFromCentralStorage(Cls,numToRemove) -> int32（非虚，返回实际移除数）/ mStoredItems
//     （TArray<FItemAmount>，SaveGame+Replicated，本模块**不写**它）。
//   · NativeHookManager.h  SUBSCRIBE_METHOD(方法指针, TFunction右值)；TCallScope::GetResult()/Override()——
//     before 处理器内先 Scope(Args...) 跑原版取 Base，再 Scope.Override(Base+额外) 覆盖返回值。
//   · FGInventoryComponent.h:347  RemoveFromIndex(idx,num,targetInventory=nullptr) -> void（直接移出，不转存）。
//   · FGBuildableSubsystem.h  GetAllBuildablesRef() / AFGBuildableStorage 遍历集装箱。

#include "Remote_FactorySmartWarehouse.h"

#include "Remote_FactoryLog.h"
#include "Remote_FactoryUpgrades.h"   // AWTUpgradeCloudDownloader / AWTUpgradeStorage（排除本 mod 云站）

#include "Containers/Ticker.h"        // FTSTicker::GetCoreTicker
#include "Engine/World.h"             // UWorld / ENetMode / GetNetMode

#include "FGBuildableSubsystem.h"
#include "Buildables/FGBuildableStorage.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGCentralStorageContainer.h"  // ★2026-11-05 排除枢纽自带储物盒（AFGCentralStorageContainer 支系）
#include "FGInventoryComponent.h"
#include "FGCentralStorageSubsystem.h"

#include "FGPlayerController.h"        // AFGPlayerController（下发遍历玩家用，本版保留兼容 include）
#include "Patching/NativeHookManager.h"  // ★2026-11-02 SUBSCRIBE_METHOD / TCallScope（hook 原版中央存储计数+扣料）

#include "FGCharacterPlayer.h"        // AFGCharacterPlayer::GetBuildGun()（建造态判定，使位面存储站面板不显示集装箱货）
#include "Equipment/FGBuildGun.h"     // AFGBuildGun::IsInState(EBuildGunState::BGS_BUILD)
#include "GameFramework/PlayerController.h"  // APlayerController::GetCharacter()

#include "ItemAmount.h"               // FItemAmount

#include "Blueprint/UserWidget.h"              // ★2026-11-04 位面面板开合判定：枚举 UUserWidget
#include "Blueprint/WidgetBlueprintLibrary.h"  // ★2026-11-04 UWidgetBlueprintLibrary::GetAllWidgetsOfClass

#include "Equipment/FGEquipment.h"     // AFGEquipment（IsEquipped / Cast）
#include "Equipment/FGChainsaw.h"      // AFGChainsaw（mFuelClass 反射读取）
#include "Equipment/FGWeapon.h"        // ★AFGWeapon（mCurrentAmmunitionClass 反射读取：步枪/钢筋枪弹药）
#include "Equipment/FGJetPack.h"       // AFGJetPack::GetSelectedConsumableType()
#include "Kismet/GameplayStatics.h"    // UGameplayStatics::GetAllActorsOfClass（遍历装备，避开 TActorIterator 头缺失）
#include "UObject/UnrealType.h"        // FindField / FProperty（反射读 mFuelClass）
#include "UObject/Class.h"             // FClassProperty / CastField

// ★2026-11-05 存入方向「建造材料」判据需要的物品分类基类（核验过的继承关系见 IsStorableMaterial 注释）
#include "Resources/FGEquipmentDescriptor.h"   // UFGEquipmentDescriptor（含子类 UFGConsumableDescriptor）
#include "Equipment/FGAmmoType.h"              // UFGAmmoType（独立支系，非 Equipment 子类）
#include "Resources/FGNoneDescriptor.h"        // UFGNoneDescriptor
#include "Resources/FGOverflowDescriptor.h"    // UFGOverflowDescriptor
#include "Resources/FGWildCardDescriptor.h"    // UFGWildCardDescriptor

// ★2026-11-02 智能仓储【集装箱可用量缓存】（服务端/客户端各自刷新；物品留在集装箱，不进池）。
// 不写原版 AFGCentralStorageSubsystem::mStoredItems（云端池），与云端完全解耦。
// 游戏线程内由 ticker 写、hook/染蓝读，单线程无并发，无需加锁。
static TMap<TSubclassOf<UFGItemDescriptor>, int32> G_ContainerAvailability;

// ★2026-11-03 新增：原版云端(mStoredItems)真实存量缓存（不含集装箱量）。
// 用途：成本格着色要按「背包 / 位面云端 / 集装箱」三源比数量，谁多显谁的色；
// 而 GetNumItemsFromCentralStorage 已被本模块 hook 成「云端+集装箱」，拿不到纯云端值，
// 故在 hook 里把原版 Base 顺手记下来，供 RemoteFactoryCostSlotTint 读取。
static TMap<TSubclassOf<UFGItemDescriptor>, int32> G_CentralStorageRealCount;

// ---------------------------------------------------------------------------------------------
// ★2026-11-04 修「集装箱的货跑到位面存储站去了」（用户实测：往原版集装箱里存货 → 位面面板里
//   数量跟着涨/列表里出现）。
//
// 根因：位面存储站面板（原版中央存储 UI）逐物品显示的数量就是 `GetNumItemsFromCentralStorage`，
//   而本模块为让建造/制作判得到集装箱量，把这个函数 hook 成「云端 + 集装箱」。于是面板一问，
//   集装箱的量就被算进「云端」，看起来像被上传了 —— 判定层与显示层共用同一个入口，光靠
//   「显示走 GetAllItemsFromCentralStorage」的说法挡不住。
//
// 修法（不改判定能力，只切显示):面板开着时**不加**集装箱量，返回纯云端值 ⇒ 面板只显示云端存货；
//   建造/制作/MAM 时（这些时候位面面板没开）照旧并入集装箱量，取料能力一点不减。
//   同一份判据也用于 TryRemove 的补料：面板上点「取出」只从云端扣，不会顺手把集装箱的货拉走。
//
// 判据 = 视口里是否有「Visible 且类名含 CentralStorage」的 UserWidget（位面存储站面板本体）。
//   用 0.1s 节流缓存，避免热点函数（建造时每帧多次调用）反复全量枚举控件。
// ---------------------------------------------------------------------------------------------
static bool G_CentralStoragePanelOpen = false;
static double G_LastPanelProbeTime = -1000.0;

static bool WT_IsCentralStoragePanelOpen(UWorld* World)
{
    if (!World)
    {
        return false;
    }
    const double Now = static_cast<double>(World->GetTimeSeconds());
    if (Now - G_LastPanelProbeTime < 0.1)
    {
        return G_CentralStoragePanelOpen;   // 节流：0.1s 内复用上次结果
    }
    G_LastPanelProbeTime = Now;

    bool bOpen = false;
    TArray<UUserWidget*> All;
    UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, All, UUserWidget::StaticClass(), false);
    for (UUserWidget* Wdg : All)
    {
        if (!Wdg || !Wdg->GetClass())
        {
            continue;
        }
        if (!Wdg->GetClass()->GetName().Contains(TEXT("CentralStorage")))
        {
            continue;
        }
        if (Wdg->IsVisible())
        {
            bOpen = true;
            break;
        }
    }
    G_CentralStoragePanelOpen = bOpen;
    return bOpen;
}

namespace
{
    // ---------------------------------------------------------------------------------------------
    // 取存储建筑**真正生效**的库存组件（同 Remote_FactoryUpgrades.cpp 的口径：优先 mStorageInventoryHandler
    // -> GetActiveInventoryComponent/GetActiveInventory，退回 GetStorageInventory()）。
    // ---------------------------------------------------------------------------------------------
    // ★2026-11-03 修复（工业集装箱/Mk.2 读取不到）：解析时**同时**把
    //   ① mStorageInventoryHandler 的 GetActiveInventoryComponent/GetActiveInventory（原版推荐口径）
    //   ② 直接 mStorageInventory（GetStorageInventory；部分存储类型 handler 返回空壳、真实物品在此）
    //   纳入候选，取【含物品最多】者作为生效库存——兼容 Mk.1（handler 即真实）与 Mk.2（handler 空壳、mStorageInventory 真实）。
    UFGInventoryComponent* WT_ResolveActiveContainerInv(AFGBuildableStorage* Storage, FString& OutHow)
    {
        OutHow = TEXT("none");
        if (!Storage)
        {
            OutHow = TEXT("null");
            return nullptr;
        }

        TArray<UFGInventoryComponent*> Cands;

        static FObjectPropertyBase* HandlerProp = nullptr;
        static bool bResolved = false;
        if (!bResolved)
        {
            bResolved = true;
            HandlerProp = CastField<FObjectPropertyBase>(FindFProperty<FProperty>(
                AFGBuildableStorage::StaticClass(), TEXT("mStorageInventoryHandler")));
        }

        if (HandlerProp)
        {
            if (UObject* Handler = HandlerProp->GetObjectPropertyValue_InContainer(Storage))
            {
                static const TCHAR* CandidateFns[] = {
                    TEXT("GetActiveInventoryComponent"),
                    TEXT("GetActiveInventory"),
                };
                for (const TCHAR* FnName : CandidateFns)
                {
                    if (UFunction* Fn = Handler->FindFunction(FnName))
                    {
                        struct FRetParams { UFGInventoryComponent* Ret; };
                        FRetParams Parms{};
                        Handler->ProcessEvent(Fn, &Parms);
                        if (UFGInventoryComponent* Active = Parms.Ret)
                        {
                            Cands.AddUnique(Active);
                        }
                    }
                }
            }
        }
        // 直接库存：始终纳入候选（即便 handler 已给出，也一并对比按物品数择优）。
        if (UFGInventoryComponent* Direct = Storage->GetStorageInventory())
        {
            Cands.AddUnique(Direct);
        }

        // 按含物品数择优（tie 时保留首个候选；通常 handler 与 direct 为同一对象）。
        UFGInventoryComponent* Best = nullptr;
        int32 BestN = -1;
        for (UFGInventoryComponent* C : Cands)
        {
            if (!C)
            {
                continue;
            }
            int32 N = 0;
            const int32 Sz = C->GetSizeLinear();
            for (int32 i = 0; i < Sz; ++i)
            {
                FInventoryStack S;
                if (C->GetStackFromIndex(i, S) && S.HasItems())
                {
                    N += S.NumItems;
                }
            }
            if (N > BestN)
            {
                BestN = N;
                Best = C;
            }
        }
        if (Best)
        {
            OutHow = FString::Printf(TEXT("%s items=%d"), *Best->GetName(), BestN);
            return Best;
        }
        OutHow = TEXT("no-inventory-component");
        return nullptr;
    }

    // =============================================================================================
    // ★★★2026-11-05【「枢纽自带储物盒」单独一组】—— 用户先报「背包存入直接进了枢纽储物盒」，
    //   给定三个选项后追加指令：「储物盒单独开一个吧」。
    //
    //   一手事实（用户本机日志，2026-11-05 10:15）：
    //     [smart-warehouse] container found: class=Build_StorageIntegrated_C  resolve=StorageInventory items=80
    //     [smart-warehouse] container found: class=Build_StorageContainerMk1_C resolve=StorageInventory items=7660
    //     [warehouse-panel] store-to-container 'Desc_Fuel_C' want=100 stored=100 scope=ALL
    //   ⇒ 枢纽自带的那个储物盒（蓝图类 Build_StorageIntegrated_C）和玩家放的集装箱一样继承
    //     AFGBuildableStorage，被 Cast<AFGBuildableStorage> 一起收了进来；而存入是「按 GetAllBuildablesRef
    //     顺序往有容量的箱子里填」，它排在玩家集装箱前面 ⇒ 东西全进了它。
    //
    //   ★最终口径（用户 2026-11-05 定案，2026-11-08 推翻）：原定「不排除、单独分一组」。
    //     2026-11-08 用户改主意「完全踢出智能仓储」——不显示、不可读、不写不存，内容对建造也不可见。
    //     ⇒ 见下方 WT_IsExcludedStorageActor，枢纽自带储物盒现已与云站同等排除（WT_IsHubStorageActor 删除）。
    //
    //   ★2026-11-08 反转：原「不排除、单独分一组」已推翻，改为**完全踢出智能仓储**
    //     （不显示、不可读、不写不存，内容对建造也不可见）。枢纽自带储物盒现与云站同等排除，
    //     判据合并进 WT_IsExcludedStorageActor（WT_IsHubStorageActor 已删除）。
    // =============================================================================================
    // 排除「不属于玩家自己放的集装箱」的存储体：本 mod 云站 + 枢纽自带储物盒。
    // 两张口径（WT_CollectContainerInventories / GetContainerSnapshots）都只调这一个函数，保证一致。
    static bool WT_IsExcludedStorageActor(const AActor* B)
    {
        if (!B)
        {
            return true;
        }
        // 本 mod 自己的云站：下载站走 AFGBuildableStorage 支系，上传站走 AFGCentralStorageContainer。
        // 流体上传/下载站走 AFGBuildablePipeReservoir，本来就不是 Storage，自然不会进这个列表。
        if (B->IsA(AWTUpgradeCloudDownloader::StaticClass()))
        {
            return true;
        }
        if (B->IsA(AWTUpgradeStorage::StaticClass()))
        {
            return true;
        }
        // ★2026-11-08【完全踢出枢纽自带储物盒】：蓝图类 Build_StorageIntegrated_C，随 Hub 自带，
        // 与玩家集装箱同走 AFGBuildableStorage 支系，不排掉就会被收进智能仓储、背包存入可能进它、
        // 面板还会置顶显示。用户定案「完全踢出」⇒ 与云站同等排除（不显示/不可读/不写不存）。
        // 类名兜底：SDK 头文件查不到它继承关系，两条判据都写上。
        if (B->IsA(AFGCentralStorageContainer::StaticClass()))
        {
            return true;
        }
        if (B->GetClass() && B->GetClass()->GetName().Contains(TEXT("StorageIntegrated")))
        {
            return true;
        }
        return false;
    }

    // 收集世界上全部「原版」存储容器库存（排除本 mod 的云站；枢纽自带储物盒排在最后），按稳定顺序以便可复现。
    // ★2026-11-03 修复：工业集装箱(Mk.2) 经 WT_ResolveActiveContainerInv 按含物品数择优，确保被纳入可用量统计/扣料/染蓝。
    void WT_CollectContainerInventories(UWorld* World, TArray<UFGInventoryComponent*>& Out)
    {
        Out.Reset();
        if (!World)
        {
            return;
        }
        AFGBuildableSubsystem* BS = AFGBuildableSubsystem::Get(World);
        if (!BS)
        {
            return;
        }
        // ★一次性诊断：每种容器类(Class)首次出现时打印其解析方式与物品数，
        //   用于确认工业集装箱(Mk.2)等被正确纳入（开智能仓储后搜 [smart-warehouse] container found）。
        static TSet<FString> SeenClasses;
        // ★2026-11-08：枢纽自带储物盒已在 WT_IsExcludedStorageActor 内完全排除，下面只收玩家自己放的集装箱。
        const TArray<AFGBuildable*>& All = BS->GetAllBuildablesRef();
        for (AFGBuildable* B : All)
        {
            AFGBuildableStorage* Storage = Cast<AFGBuildableStorage>(B);
            if (!Storage)
            {
                continue;
            }
            // 排除本 mod 云站 + 枢纽自带储物盒（用户 2026-11-08 定案：完全踢出）。
            if (WT_IsExcludedStorageActor(B))
            {
                continue;
            }
            FString How;
            UFGInventoryComponent* Inv = WT_ResolveActiveContainerInv(Storage, How);
            if (Inv)
            {
                Out.Add(Inv);
            }
            const FString ClsName = B->GetClass()->GetName();
            if (!SeenClasses.Contains(ClsName))
            {
                SeenClasses.Add(ClsName);
                int32 N = 0;
                if (Inv)
                {
                    const int32 Sz = Inv->GetSizeLinear();
                    for (int32 i = 0; i < Sz; ++i)
                    {
                        FInventoryStack S;
                        if (Inv->GetStackFromIndex(i, S) && S.HasItems())
                        {
                            N += S.NumItems;
                        }
                    }
                }
                WT_WARNF(TEXT("[smart-warehouse] container found: class=%s resolve=%s items=%d"),
                         *ClsName, *How, N);
            }
        }
        // 注：枢纽自带储物盒已在上方 WT_IsExcludedStorageActor 排除，不再单独追加。
    }

    // 聚合世界上全部原版存储集装箱的可用量（物品留在原处，仅统计）。
    // 结果写入 G_ContainerAvailability，供 GetNumItems hook（客户端材料检查）与成本格染蓝共用。
    void WT_RefreshContainerAvailability(UWorld* World)
    {
        TMap<TSubclassOf<UFGItemDescriptor>, int32> Avail;
        if (!World)
        {
            return;
        }
        TArray<UFGInventoryComponent*> Containers;
        WT_CollectContainerInventories(World, Containers);
        for (UFGInventoryComponent* Inv : Containers)
        {
            if (!Inv)
            {
                continue;
            }
            const int32 Size = Inv->GetSizeLinear();
            for (int32 i = 0; i < Size; ++i)
            {
                FInventoryStack S;
                if (!Inv->GetStackFromIndex(i, S) || !S.HasItems())
                {
                    continue;
                }
                const TSubclassOf<UFGItemDescriptor> Cls = S.Item.GetItemClass();
                Avail.Add(Cls, Avail.FindRef(Cls) + S.NumItems);
            }
        }
        G_ContainerAvailability = MoveTemp(Avail);
    }

    // 直接从集装箱补足差额：从各容器里移除匹配 Cls 的物品（target=nullptr => 不转存，由建造消耗），返回实际移除量。
    // 仅在服务端权威世界调用。物品留在集装箱，按需直取，不进任何池。
    int32 WT_PullFromContainers(UWorld* World, TSubclassOf<UFGItemDescriptor> Cls, int32 Want)
    {
        if (!World || Want <= 0)
        {
            return 0;
        }
        TArray<UFGInventoryComponent*> Containers;
        WT_CollectContainerInventories(World, Containers);
        int32 Got = 0;
        for (UFGInventoryComponent* Inv : Containers)
        {
            if (Got >= Want)
            {
                break;
            }
            if (!Inv)
            {
                continue;
            }
            const int32 Size = Inv->GetSizeLinear();
            for (int32 i = 0; i < Size && Got < Want; ++i)
            {
                FInventoryStack S;
                if (!Inv->GetStackFromIndex(i, S) || !S.HasItems())
                {
                    continue;
                }
                if (S.Item.GetItemClass() != Cls)
                {
                    continue;   // 只取匹配的物品类
                }
                const int32 Have = S.NumItems;
                const int32 Take = FMath::Min(Have, Want - Got);
                // 直接从集装箱移除（targetInventory=nullptr => 不转存到任何库存，由建造消耗）。
                // RemoveFromIndex 返回 void（FGInventoryComponent.h:347 一手核实），故移除后再读确认实际移除量。
                Inv->RemoveFromIndex(i, Take, nullptr);
                FInventoryStack SAfter;
                const int32 After = (Inv->GetStackFromIndex(i, SAfter) && SAfter.HasItems()) ? SAfter.NumItems : 0;
                const int32 Actually = FMath::Max(0, Have - After);
                Got += Actually;
            }
        }
        return Got;
    }
}

// ---------------------------------------------------------------------------------------------
// ★2026-11-03 反馈B：装备消耗品从智能仓储(集装箱)取用（覆盖原版所有从背包取用消耗品的装备，不止电锯/喷气背包）
//   装备（电锯/喷气背包/扫描枪/电击器/炸药 detonator 等）的消耗品走玩家背包，不走中央存储 API，
//   故智能仓储建造钩子覆盖不到它们。修法：智能仓储开启 + 服务端权威世界，给装备持有人的背包补「消耗品缓冲」
//   （从集装箱转入背包），装备照常从背包消耗 => 等效从仓库取用。
//   取用范围（每种装备）：① 使用成本 mCostToUse（GetCostToUse）② 声明消耗品类型 GetSupportedConsumableTypes
//            ③ 燃料：电锯 mFuelClass（反射读，GetFuelClass 为 protected）/ 喷气背包 当前所选燃料类型。
//   一手依据：AFGEquipment.h:390 GetCostToUse() / :354 GetSupportedConsumableTypes() / :251 IsEquipped() / :258 GetInstigatorCharacter()；
//            AFGChainsaw.h mFuelClass(UPROPERTY) / AFGJetPack.h:52 GetSelectedConsumableType() / AFGCharacterPlayer.h:722 GetInventory() /
//            FGInventoryComponent.h:415 GetNumItems() / :347 RemoveFromIndex(idx,num,targetInv) 转移语义。
// ---------------------------------------------------------------------------------------------

// ★2026-11-04 物品丢失事故修复（第二版：彻底绕开 RemoveFromIndex 的 targetInventory 转移）。
//   现象：取 1 个铁矿石，日志 got=1（容器确实少了），但背包里没有 ⇒ 物品蒸发。
//   第一版只夹「背包容量」，仍丢 ⇒ 说明根因不止「放不下」，而是
//   `RemoveFromIndex(idx, num, targetInventory)` 在这个调用场景下根本没有把物品塞进 targetInventory
//   （它是「先删后塞」，删是必然发生，塞是否成功不反馈给调用方）。
//   ★最终修法：不再依赖这个二合一接口，改成「先加后删」：
//     ① `TargetInv->AddStack(stack, /*allowPartialAdd=*/true)` —— 返回【实际加入背包的数量】；
//     ② 只有真正加进去了 N 个，才从集装箱删掉 N 个。
//   这样即使背包满、或物品类型被拒收，集装箱一个都不会少，物理上不可能再丢物品。
static int32 WT_MaxAddableTo(TSubclassOf<UFGItemDescriptor> Cls, int32 Want, UFGInventoryComponent* TargetInv)
{
    if (!TargetInv || !Cls || Want <= 0)
    {
        return 0;
    }
    if (TargetInv->HasEnoughSpaceForStack(FInventoryStack(Want, Cls)))
    {
        return Want;
    }
    // 整批放不下：二分找最大可放量（1 个都放不下则返回 0，调用方据此不取货）。
    int32 Lo = 0;
    int32 Hi = Want;
    while (Lo < Hi)
    {
        const int32 Mid = (Lo + Hi + 1) / 2;
        if (TargetInv->HasEnoughSpaceForStack(FInventoryStack(Mid, Cls)))
        {
            Lo = Mid;
        }
        else
        {
            Hi = Mid - 1;
        }
    }
    return Lo;
}

// ★2026-11-05【限定容器】收集要用到的集装箱 inventory。
//   OnlyContainer 为空 ⇒ 全部容器（旧行为，建造扣料/全局取存都走这条）。
//   OnlyContainer 非空 ⇒ **只**返回它那一个（面板「一个容器一行」时，点某行的取/存只作用于那个箱子）。
//   ★为什么用 actor 而不是直接传 inventory：RPC 只能传 UObject*，而且服务端必须重新解析一次
//     （客户端给的 inventory 指针在服务端不一定对应同一个对象），用 actor 让服务端自己走同一套
//     WT_ResolveActiveContainerInv 解析，口径才和统计完全一致。
static void WT_CollectContainersFiltered(UWorld* World, AActor* OnlyContainer, TArray<UFGInventoryComponent*>& Out)
{
    if (!OnlyContainer)
    {
        WT_CollectContainerInventories(World, Out);
        return;
    }
    Out.Reset();
    // ★防御：即使客户端送来一个已被排除的对象（如枢纽自带储物盒），也一律拒绝，
    //   保证「面板不显示的东西」不可能成为取/存的作用目标。
    if (WT_IsExcludedStorageActor(OnlyContainer))
    {
        return;
    }
    AFGBuildableStorage* Storage = Cast<AFGBuildableStorage>(OnlyContainer);
    if (!Storage)
    {
        return;
    }
    FString How;
    if (UFGInventoryComponent* Inv = WT_ResolveActiveContainerInv(Storage, How))
    {
        Out.Add(Inv);
    }
}

// 把 Want 个 Cls 从集装箱转入 TargetInv（★先加后删，失败则不删，杜绝物品蒸发）。
static int32 WT_TransferFromContainersToInventory(UWorld* World, TSubclassOf<UFGItemDescriptor> Cls, int32 Want, UFGInventoryComponent* TargetInv, AActor* OnlyContainer = nullptr)
{
    if (!World || !TargetInv || Want <= 0)
    {
        return 0;
    }

    // 先探一次总容量：一个都放不下就直接不取货（省得白扫一遍容器）。
    const int32 Capacity = WT_MaxAddableTo(Cls, Want, TargetInv);
    if (Capacity <= 0)
    {
        WT_WARNF(TEXT("[warehouse-panel] take aborted: backpack has no room for any %s (want=%d)."),
                 (Cls ? *Cls->GetName() : TEXT("?")), Want);
        return 0;
    }

    // ★关键：先加后删，绝不让"删了但没塞进去"发生。这是物品蒸发问题的唯一正解，别改回二合一接口。
    //   · WT_MaxAddableTo 先算出背包最多能接多少（整批放得下就直接返回 Want，否则二分），
    //     一个都放不下 ⇒ Capacity=0 ⇒ 直接返回，集装箱一个都不动（玩家看不到变化，但东西没丢）。
    //   · 每处理一堆：先 AddStack(allowPartialAdd=true) 拿到**真实加入数** Added，再只删 Added。
    //   · Added<=0 立刻 break：背包满了，继续扫只会白扫。
    //   · 删的时候 target=nullptr（纯移除，不再往背包塞），否则会重复入账。
    //   · Got = 实际给到玩家几个，RCO 会把它打进日志；取不满时打 take partial。
    TArray<UFGInventoryComponent*> Containers;
    WT_CollectContainersFiltered(World, OnlyContainer, Containers);
    int32 Got = 0;
    for (UFGInventoryComponent* Inv : Containers)
    {
        if (Got >= Capacity)
        {
            break;
        }
        if (!Inv)
        {
            continue;
        }
        const int32 Size = Inv->GetSizeLinear();
        for (int32 i = 0; i < Size && Got < Capacity; ++i)
        {
            FInventoryStack S;
            if (!Inv->GetStackFromIndex(i, S) || !S.HasItems())
            {
                continue;
            }
            if (S.Item.GetItemClass() != Cls)
            {
                continue;
            }
            const int32 Have = S.NumItems;
            const int32 Try = FMath::Min(Have, Capacity - Got);
            if (Try <= 0)
            {
                break;
            }

            // ① 先往背包加（allowPartialAdd=true ⇒ 能加多少加多少，返回实际加入数）。
            const int32 BpBefore = TargetInv->GetNumItems(Cls);
            const int32 Added = TargetInv->AddStack(FInventoryStack(Try, Cls), /*allowPartialAdd=*/true);
            const int32 BpAfter = TargetInv->GetNumItems(Cls);
            WT_WARNF(TEXT("[warehouse-panel] add-probe: want=%d AddStack=%d bp %d->%d (net %d)"),
                     Try, Added, BpBefore, BpAfter, BpAfter - BpBefore);
            if (Added <= 0)
            {
                break;   // 背包满了：一个都没进去，集装箱一个都不删
            }

            // ② 只删「确实进了背包」的那部分（target=nullptr ⇒ 纯移除，不再往背包塞，避免重复入账）。
            Inv->RemoveFromIndex(i, Added, nullptr);

            Got += Added;
            if (Cls)
            {
                WT_WARNF(TEXT("[equip-fuel] transferred %d %s from containers to inventory (smart-warehouse)."), Added, *Cls->GetName());
            }
        }
    }
    if (Got < Want)
    {
        WT_WARNF(TEXT("[warehouse-panel] take partial: %d of %d %s (backpack full)."),
                 Got, Want, (Cls ? *Cls->GetName() : TEXT("?")));
    }
    return Got;
}

// ★2026-11-03 右侧面板「手动取出」公开入口：直接转发到上面的文件私有实现（保持最小改动，不动既有调用点）。
// 注意：本 .cpp 未整体包裹 namespace Remote_FactorySmartWarehouse，故定义必须加命名空间限定，
// 否则会与头文件声明（namespace 内自由函数）分属两个符号，导致 LNK2019 未解析外部符号。
int32 Remote_FactorySmartWarehouse::TransferFromContainersToInventory(UWorld* World, TSubclassOf<UFGItemDescriptor> Cls, int32 Want, UFGInventoryComponent* TargetInv, AActor* OnlyContainer)
{
    return WT_TransferFromContainersToInventory(World, Cls, Want, TargetInv, OnlyContainer);
}

// =============================================================================================
// ★2026-11-05 存入方向（背包 → 集装箱）
//
//   为什么必须新写而不是复用取出的函数：取出的循环结构是「扫集装箱的格子」，
//   存入要「扫背包的格子」，容器与迭代方向正好相反，强行合只会写出一堆 if 分支。
//   但**安全模型完全照抄取出**：先加后删，绝不先删后加。理由见 WT_TransferFromContainersToInventory
//   上方那段 2026-11-04 物品蒸发事故的注释 —— 原版 RemoveFromIndex(idx,num,targetInv) 是
//   「先删后塞、塞失败不回报」的二合一接口，在这个调用场景下会把物品直接吞掉。
// =============================================================================================

// 判「建造材料类」。排除清单的继承关系全部核过 SDK 头文件，见 Remote_FactorySmartWarehouse.h 的注释。
bool Remote_FactorySmartWarehouse::IsStorableMaterial(TSubclassOf<UFGItemDescriptor> Cls)
{
    const UClass* C = Cls.Get();
    if (!C)
    {
        return false;
    }
    // ① 装备：含武器/工具/护甲/喷气背包，**并同时覆盖全部消耗品**
    //    （UFGConsumableDescriptor : public UFGEquipmentDescriptor，SDK 里就是这么继承的）。
    if (C->IsChildOf(UFGEquipmentDescriptor::StaticClass()))
    {
        return false;
    }
    // ② 弹药：★2026-11-05 用户报「子弹在面板中为什么读取不到」⇒ **不再排除**。
    //    上一版按"只存建造材料"把 UFGAmmoType（独立支系，非装备子类）排除了，结果：
    //      · 「背包（可存入）」行看不到子弹 ⇒ 没法把子弹放进集装箱；
    //      · 存入请求也被拒 ⇒ 装备补料想从集装箱取子弹时，箱子里根本没有子弹可拿。
    //    现在放开：子弹当作普通物资，能存能取，补料链条才闭合。
    // ③ 内部占位类：背包遍历偶尔会扫到，存进去只会制造脏数据。
    if (C->IsChildOf(UFGNoneDescriptor::StaticClass())
        || C->IsChildOf(UFGOverflowDescriptor::StaticClass())
        || C->IsChildOf(UFGWildCardDescriptor::StaticClass()))
    {
        return false;
    }
    return true;
}

// 把 Want 个 Cls 从 SourceInv（背包）搬进各个集装箱。★先加后删，失败则一个都不减。
//   · 每个箱子先二分算「还能接多少」（WT_MaxAddableTo），接不下就换下一个箱子 —— 多个集装箱会自动分摊。
//   · 每处理一格：先 AddStack(allowPartialAdd=true) 拿**真实入箱数** Added，再从背包只删 Added。
//   · Added<=0 ⇒ 这个箱子拒收/已满 ⇒ Budget 置 0，跳出该箱子（继续下一个），背包不动。
//   · 背包格子索引在本函数内不会被前移（删的是当前格、且只在 Added>0 后删），故 i 单调递增是安全的。
static int32 WT_TransferFromInventoryToContainers(UWorld* World, TSubclassOf<UFGItemDescriptor> Cls, int32 Want, UFGInventoryComponent* SourceInv, AActor* OnlyContainer = nullptr)
{
    if (!World || !Cls || Want <= 0 || !SourceInv)
    {
        return 0;
    }
    if (!Remote_FactorySmartWarehouse::IsStorableMaterial(Cls))
    {
        WT_WARNF(TEXT("[warehouse-panel] store rejected: '%s' is not a storable material."), *Cls->GetName());
        return 0;
    }

    // 背包实际持有量就是硬上限（面板传上来的 Want 可能比实际多，界面是 0.5s 前的快照）。
    const int32 Have = SourceInv->GetNumItems(Cls);
    int32 Remaining = FMath::Min(Want, Have);
    if (Remaining <= 0)
    {
        return 0;
    }

    TArray<UFGInventoryComponent*> Containers;
    WT_CollectContainersFiltered(World, OnlyContainer, Containers);
    if (Containers.Num() == 0)
    {
        WT_WARNF(TEXT("[warehouse-panel] store aborted: no storage containers in world (want=%d %s)."),
                 Want, *Cls->GetName());
        return 0;
    }

    int32 Stored = 0;

    // ★★★2026-11-05【存入顺序：先填「已经在装这个物品」的箱子】—— 用户玩法「每个产物放一个集装箱」。
    //   若按遍历顺序填，100 个铁板会被塞进第一个有空间的箱子（可能是装铜矿的那个），
    //   玩家的分类布局一存就乱。所以分两轮：
    //     第一轮 只往「已经有 Cls」的容器里加  → 同类归位，布局不动
    //     第二轮 剩余的才进空箱/别的箱         → 保证东西存得下，不会因为"没有同类箱"而存不进去
    TArray<UFGInventoryComponent*> Ordered;
    Ordered.Reserve(Containers.Num());
    for (UFGInventoryComponent* CInv : Containers)
    {
        if (CInv && CInv != SourceInv && CInv->GetNumItems(Cls) > 0)
        {
            Ordered.Add(CInv);
        }
    }
    const int32 NumPreferred = Ordered.Num();
    for (UFGInventoryComponent* CInv : Containers)
    {
        if (CInv && CInv != SourceInv && CInv->GetNumItems(Cls) <= 0)
        {
            Ordered.Add(CInv);
        }
    }

    // 往**一个**容器里填。Remaining 归零即什么都不做（多个容器顺序调用时天然幂等）。
    auto FillOne = [&](UFGInventoryComponent* CInv)
    {
        if (Remaining <= 0 || !CInv || CInv == SourceInv)
        {
            return;   // 防御：把自己写进自己的列表（理论上不会）
        }

        // 这个箱子最多还能接多少。<=0 ⇒ 满了或拒收，换下一个箱子。
        int32 Budget = WT_MaxAddableTo(Cls, Remaining, CInv);
        if (Budget <= 0)
        {
            return;
        }

        const int32 Size = SourceInv->GetSizeLinear();
        for (int32 i = 0; i < Size && Budget > 0 && Remaining > 0; ++i)
        {
            FInventoryStack S;
            if (!SourceInv->GetStackFromIndex(i, S) || !S.HasItems())
            {
                continue;
            }
            if (S.Item.GetItemClass() != Cls)
            {
                continue;
            }
            const int32 Try = FMath::Min(S.NumItems, Budget);
            if (Try <= 0)
            {
                continue;
            }

            // ① 先放进集装箱（allowPartialAdd=true ⇒ 返回实际入箱数）。
            const int32 Added = CInv->AddStack(FInventoryStack(Try, Cls), /*allowPartialAdd=*/true);
            if (Added <= 0)
            {
                Budget = 0;   // 这个箱子满了，别再往里试
                continue;
            }

            // ② 只从背包扣掉「确实进了箱」的那部分（target=nullptr ⇒ 纯移除）。
            SourceInv->RemoveFromIndex(i, Added, nullptr);

            Stored += Added;
            Budget -= Added;
            Remaining -= Added;
        }
    };

    for (UFGInventoryComponent* CInv : Ordered)
    {
        if (Remaining <= 0)
        {
            break;
        }
        FillOne(CInv);
    }

    // ★★★2026-11-05【混装/满箱时自动溢出到别的箱子】—— 用户报「混装的情况下，存不进去」。
    //   面板上的物品行是从某个容器展开出来的，点【存入】会把 OnlyContainer 限定到那一个箱子。
    //   若那个箱子已经满了（混装箱很正常）⇒ 上面循环一个都加不进去 ⇒ 玩家看到"存不进去"，
    //   但世界里明明还有别的空箱。所以这里补一轮：指定箱子装不下的部分，自动落到其它容器。
    //   ★不丢货：仍然是先加后删，加多少删多少。
    int32 StoredIntoSpecified = Stored;
    if (OnlyContainer && Remaining > 0 && Stored >= 0)
    {
        TArray<UFGInventoryComponent*> All;
        WT_CollectContainersFiltered(World, nullptr, All);

        // 同样优先「已经在装这个物品」的箱子，其次才是空箱/别的箱。
        TArray<UFGInventoryComponent*> Over;
        Over.Reserve(All.Num());
        for (UFGInventoryComponent* CInv : All)
        {
            if (CInv && CInv != SourceInv && CInv->GetNumItems(Cls) > 0)
            {
                Over.Add(CInv);
            }
        }
        for (UFGInventoryComponent* CInv : All)
        {
            if (CInv && CInv != SourceInv && CInv->GetNumItems(Cls) <= 0)
            {
                Over.Add(CInv);
            }
        }
        for (UFGInventoryComponent* CInv : Over)
        {
            if (Remaining <= 0)
            {
                break;
            }
            FillOne(CInv);
        }
        if (Stored > StoredIntoSpecified)
        {
            WT_WARNF(TEXT("[warehouse-panel] store overflow: %d of %s did not fit in the specified container, spilled into others (%d total)."),
                     Stored - StoredIntoSpecified, *Cls->GetName(), Stored);
        }
    }

    if (Stored > 0)
    {
        WT_WARNF(TEXT("[warehouse-panel] store: %d %s moved from inventory into containers (want=%d have=%d, same-item-boxes=%d/%d)."),
                 Stored, *Cls->GetName(), Want, Have, NumPreferred, Ordered.Num());
    }
    else
    {
        WT_WARNF(TEXT("[warehouse-panel] store failed: no container room for '%s' (want=%d have=%d)."),
                 *Cls->GetName(), Want, Have);
    }
    return Stored;
}

int32 Remote_FactorySmartWarehouse::TransferFromInventoryToContainers(UWorld* World, TSubclassOf<UFGItemDescriptor> Cls, int32 Want, UFGInventoryComponent* SourceInv, AActor* OnlyContainer)
{
    return WT_TransferFromInventoryToContainers(World, Cls, Want, SourceInv, OnlyContainer);
}

// =============================================================================================
// ★2026-11-05【一个容器一行】：按容器（不是按物品）采集库存快照。
//
//   为什么要这个：用户提出「多个容器存放同一种物品」时，面板把所有容器的同种物品合成一个数字，
//   玩家无法知道"哪个箱子里还有多少"。改成每个容器一行、箭头展开看内容，数字就是该容器的真实数量。
//
//   ★遍历口径必须与 WT_CollectContainerInventories 完全一致（同一个过滤器、同一个解析函数），
//     否则"按容器分的数"加起来会跟"按物品聚合的总数"对不上，玩家一眼就能看出矛盾。
//     故这里直接复用同一套排除条件（云下载站 / 云上传站）与 WT_ResolveActiveContainerInv。
// =============================================================================================
void Remote_FactorySmartWarehouse::GetContainerSnapshots(UWorld* World, TArray<FContainerSnapshot>& Out)
{
    Out.Reset();
    if (!World)
    {
        return;
    }
    AFGBuildableSubsystem* BS = AFGBuildableSubsystem::Get(World);
    if (!BS)
    {
        return;
    }

    // mDisplayName 是 FText 的 UPROPERTY（FGBuildable.h:824 附近），直接成员访问可能受访问级别限制，
    // 故用反射读一次并缓存 —— 读不到就退回类名，绝不让面板因为拿到空名字而崩。
    static FTextProperty* DisplayNameProp = nullptr;
    static bool bPropResolved = false;
    if (!bPropResolved)
    {
        bPropResolved = true;
        DisplayNameProp = CastField<FTextProperty>(FindFProperty<FProperty>(AFGBuildable::StaticClass(), TEXT("mDisplayName")));
    }

    // ★2026-11-08：枢纽自带储物盒已在 WT_IsExcludedStorageActor 内完全排除，这里只收玩家自己放的集装箱，
    //   单一编号串（「集装箱 1/2/3…」），不再分 Normal/Hub 两组。
    int32 ContainerIndex = 0;   // 「集装箱 1/2/3…」独立编号
    for (AFGBuildable* B : BS->GetAllBuildablesRef())
    {
        AFGBuildableStorage* Storage = Cast<AFGBuildableStorage>(B);
        if (!Storage)
        {
            continue;
        }
        // 与 WT_CollectContainerInventories 同一套排除（云站 + 枢纽自带储物盒）。
        // ★必须调同一个函数（WT_IsExcludedStorageActor），否则两张口会对不上。
        if (WT_IsExcludedStorageActor(B))
        {
            continue;
        }
        FString How;
        UFGInventoryComponent* Inv = WT_ResolveActiveContainerInv(Storage, How);
        if (!Inv)
        {
            continue;
        }

        FContainerSnapshot Snap;
        Snap.Storage = B;   // 供面板把「只从这一箱取 / 只存进这一箱」带回 RPC

        // 名字：优先用建筑的本地化显示名（"集装箱"），拿不到就用类名去前后缀。
        FString BaseName;
        if (DisplayNameProp)
        {
            const FText* NamePtr = DisplayNameProp->ContainerPtrToValuePtr<FText>(B);
            if (NamePtr && !NamePtr->IsEmpty())
            {
                BaseName = NamePtr->ToString();
            }
        }
        if (BaseName.IsEmpty())
        {
            BaseName = B->GetClass()->GetName();
            BaseName.RemoveFromStart(TEXT("Build_"));
            BaseName.RemoveFromStart(TEXT("BP_"));
            BaseName.RemoveFromEnd(TEXT("_C"));
        }
        Snap.BoxType = BaseName;   // ★2026-11-08 同型号合并：用型号友好名做分组键
        Snap.Label = FString::Printf(TEXT("%s %d"), *BaseName, ++ContainerIndex);

        const int32 Size = Inv->GetSizeLinear();
        for (int32 i = 0; i < Size; ++i)
        {
            FInventoryStack S;
            if (!Inv->GetStackFromIndex(i, S) || !S.HasItems())
            {
                continue;
            }
            const TSubclassOf<UFGItemDescriptor> Cls = S.Item.GetItemClass();
            if (!Cls)
            {
                continue;
            }
            Snap.Items.FindOrAdd(Cls) += S.NumItems;
            Snap.NumItems += S.NumItems;
        }
        Snap.NumKinds = Snap.Items.Num();
        Out.Add(MoveTemp(Snap));
    }
}

// 收集一件装备「从背包取用」的全部消耗品类（不止电锯/喷气背包，覆盖原版所有从背包取消耗品的装备）。
// 来源：① mCostToUse（装备使用时消耗的物品）② GetSupportedConsumableTypes（支持的消耗品类型）
//       ③ 燃料（电锯 mFuelClass 反射读取，因 GetFuelClass 为 protected；喷气背包当前所选燃料类型）。全部 AddUnique 去重。
static void WT_GetEquipmentConsumableClasses(AFGEquipment* Eq, TArray<TSubclassOf<UFGItemDescriptor>>& Out)
{
    if (!Eq)
    {
        return;
    }

    // ★★★2026-11-05 修【补料抓错对象】：建造枪必须是第一个排除项。
    //   一手事实：AFGBuildGun : public AFGEquipment（FGBuildGun.h:244），所以它会走进本函数。
    //   而它的 GetCostToUse() 返回的是「建造这一栋楼要哪些料」——日志实测搬的是
    //     Desc_IronPlate_C / Desc_Wire_C / Desc_IronRod_C
    //   ⇒ 之前每 1s 都在把建造材料往玩家背包里塞，既不是燃料也不是弹药，
    //     玩家在物品栏里看到的自然全是建材，真正想要的燃料/弹药一个都没补上。
    //   ★建造扣料本来就由 TryRemoveItemsFromCentralStorage 的 hook 覆盖，不需要在这里再补一遍。
    if (Eq->IsA(AFGBuildGun::StaticClass()))
    {
        return;
    }

    // ① 使用成本里列出的消耗品
    const TArray<FItemAmount>& Cost = Eq->GetCostToUse();
    for (const FItemAmount& A : Cost)
    {
        if (A.ItemClass)
        {
            Out.AddUnique(A.ItemClass);
        }
    }

    // ② 装备声明的可消耗类型
    TArray<TSubclassOf<UFGItemDescriptor>> Supported;
    Eq->GetSupportedConsumableTypes(Supported);
    for (const TSubclassOf<UFGItemDescriptor>& C : Supported)
    {
        if (C)
        {
            Out.AddUnique(C);
        }
    }

    // ③ 燃料类（protected 成员用反射读取，避开编译错误）
    if (AFGChainsaw* CS = Cast<AFGChainsaw>(Eq))
    {
        FProperty* Prop = FindFProperty<FProperty>(AFGChainsaw::StaticClass(), TEXT("mFuelClass"));
        FClassProperty* ClassProp = CastField<FClassProperty>(Prop);
        if (ClassProp)
        {
            void* ValPtr = ClassProp->ContainerPtrToValuePtr<void>(CS);
            UClass* Val = ValPtr ? *reinterpret_cast<UClass**>(ValPtr) : nullptr;
            Out.AddUnique(TSubclassOf<UFGItemDescriptor>(Val));
        }
    }
    if (AFGJetPack* JP = Cast<AFGJetPack>(Eq))
    {
        TSubclassOf<UFGItemDescriptor> Sel = JP->GetSelectedConsumableType();
        if (Sel)
        {
            Out.AddUnique(Sel);
        }
    }

    // ★★★2026-11-05【弹药：步枪子弹 / 钢筋枪等】—— 用户报「步枪子弹、钢筋枪…不会自动补充」。
    //   一手事实：`AFGWeapon::mCurrentAmmunitionClass`（FGWeapon.h:308，protected UPROPERTY，
    //   类型 TSubclassOf<UFGAmmoType>）；`GetSpareAmmunition()`（:122）读的就是背包里的备用弹。
    //   ★为什么前面 ①②③ 都收不到它：弹药不在 mCostToUse 里（那不是"使用成本"），
    //     也不在 GetSupportedConsumableTypes 里（那是消耗品不是弹药），武器更没有燃料成员。
    //   ★为什么只读"当前弹药类型"：玩家换弹种时该成员会随之改变（OnCurrentAmmoTypeChanged），
    //     下一拍补料自然跟上；把所有弹种都补一遍会让背包塞满用不到的子弹。
    if (AFGWeapon* Wp = Cast<AFGWeapon>(Eq))
    {
        FProperty* AmmoProp = FindFProperty<FProperty>(AFGWeapon::StaticClass(), TEXT("mCurrentAmmunitionClass"));
        FClassProperty* AmmoClassProp = CastField<FClassProperty>(AmmoProp);
        if (AmmoClassProp)
        {
            void* ValPtr = AmmoClassProp->ContainerPtrToValuePtr<void>(Wp);
            UClass* Val = ValPtr ? *reinterpret_cast<UClass**>(ValPtr) : nullptr;
            if (Val)
            {
                Out.AddUnique(TSubclassOf<UFGItemDescriptor>(Val));
            }
        }
    }
}

// 智能仓储开启 + 服务端权威世界：给已装备装备的持有人背包补消耗品缓冲（从集装箱转入）。
// 覆盖原版所有从背包取用消耗品的装备（电锯/喷气背包/扫描枪/电击器/炸药引爆器等），不仅限于两种。
static void WT_TopUpEquippedFuel(UWorld* W)
{
    static bool bLogged = false;
    if (!bLogged)
    {
        bLogged = true;
        WT_WARNF(TEXT("[smart-warehouse] equipment-consumables-from-warehouse support installed (all equipment drawing consumables from backpack)."));
    }
    if (!W)
    {
        return;
    }
    if (W->GetNetMode() == NM_Client)
    {
        return;   // 仅服务端权威世界补料（背包库存权威在服）
    }
    if (!Remote_FactorySmartWarehouse::IsSmartWarehouseEnabled())
    {
        return;   // 智能仓储关则不动玩家背包
    }

    TArray<AActor*> Actors;
    UGameplayStatics::GetAllActorsOfClass(W, AFGEquipment::StaticClass(), Actors);
    for (AActor* A : Actors)
    {
        AFGEquipment* Eq = Cast<AFGEquipment>(A);
        if (!Eq || !Eq->IsEquipped())
        {
            continue;
        }
        AFGCharacterPlayer* Owner = Eq->GetInstigatorCharacter();
        if (!Owner)
        {
            continue;
        }
        UFGInventoryComponent* Inv = Owner->GetInventory();
        if (!Inv)
        {
            continue;
        }
        TArray<TSubclassOf<UFGItemDescriptor>> Classes;
        WT_GetEquipmentConsumableClasses(Eq, Classes);

        // ★一次性诊断：每种装备类第一次被扫描时，把它认到的消耗品类全打出来。
        //   用户报「防毒面具/防化服不会自动补充」这类问题时，看这行就知道是"没认到类"还是"认到了但仓库没货"。
        {
            static TSet<FString> SeenEquipClasses;
            const FString EqCls = Eq->GetClass()->GetName();
            if (!SeenEquipClasses.Contains(EqCls))
            {
                SeenEquipClasses.Add(EqCls);
                FString Joined;
                for (const TSubclassOf<UFGItemDescriptor>& C : Classes)
                {
                    Joined += C ? (C->GetName() + TEXT(",")) : TEXT("None,");
                }
                WT_WARNF(TEXT("[equip-consumable] scan %s equipped=%d consumables=%d (%s)"),
                         *EqCls, Eq->IsEquipped() ? 1 : 0, Classes.Num(), *Joined);
            }
        }
        for (const TSubclassOf<UFGItemDescriptor>& Cls : Classes)
        {
            if (!Cls)
            {
                continue;
            }
            const int32 Have = Inv->GetNumItems(Cls);

            // ★★★2026-11-05【按组补给：用完了才补，一次补满一组】—— 用户定案。
            //   演进史：补满一格(200) → 20 → 3 → 1 → **按组**。
            //   为什么最终选这个：
            //     · 平时背包里**一个都没有**（`Have > 0` 直接跳过）⇒ 不会出现"东西自己跑进背包"的观感；
            //     · 一旦耗尽，下一拍（1 秒内）一次补满**一整组**（`GetStackSize`）⇒ 之后长时间不用再补，
            //       不会像补 1 个那样每消耗一次就找一次仓库、也不会因为补得太少而卡顿。
            //   ★子弹（步枪/钢筋枪）尤其不能只补 1 个：换弹是一次要一个弹匣的量，
            //     1 发连一次装填都不够，必须按组补。
            //   ★为什么不能"完全不补、让装备直接扣集装箱"：原版装备的扣料函数
            //     AFGJetPack::ConsumeFuel / AFGChainsaw::ConsumeFuel 都是 **protected 且非虚、还不是 UFUNCTION**
            //     （FGJetPack.h:81 / FGChainsaw.h:130 一手核实），SML 拿不到可 hook 的地址；
            //     而它们只认玩家背包（喷气背包靠 PickPreferredFuelTypeFromInventory 从背包挑燃料）。
            //     ⇒「把货提前放进背包」是实现「装备能用集装箱货」的唯一路径，别的地方插不进去。
            if (Have > 0)
            {
                continue;   // 还有货 ⇒ 不补（背包平时保持干净）
            }
            int32 Buffer = UFGItemDescriptor::GetStackSize(Cls);
            if (Buffer <= 0)
            {
                Buffer = 100;   // 取不到堆叠上限时的兜底
            }
            if (Have >= Buffer)
            {
                continue;
            }
            int32 Need = Buffer - Have;

            // ★★★2026-11-05【三级来源：背包 → 云端 → 智能仓储】用户定案。
            //   用 TryRemoveItemsFromCentralStorage 而不是直接扫集装箱，一次就拿到两级：
            //     · 该 API 在智能仓储关闭时 = 纯原版云端（mStoredItems），即「云端」这一级；
            //     · 智能仓储开启时，本模块的 hook 会先让原版扣云端，不够的再从集装箱补足，
            //       并把实际补到的量一并返回 ⇒ 天然等于「云端 + 智能仓储」。
            //   这样就不必在这里写两条来源分支，也不会漏掉任何一个来源。
            //   背包（第一级）已经体现在 Have 里 —— 不够才补，够就 continue。
            const int32 Room = WT_MaxAddableTo(Cls, Need, Inv);
            if (Room <= 0)
            {
                continue;   // 背包已满，补不进去，别白扣云端/集装箱的量
            }

            AFGCentralStorageSubsystem* Sub = AFGCentralStorageSubsystem::Get(W);
            if (!Sub)
            {
                continue;
            }
            const int32 Got = Sub->TryRemoveItemsFromCentralStorage(Cls, Room);
            if (Got <= 0)
            {
                continue;   // 云端和集装箱都没有这件东西
            }

            // ★先扣后加的风险点：TryRemove 是「先删」，删完不还回去。
            //   故上面先用 WT_MaxAddableTo 二分算出 Room（保证 HasEnoughSpaceForStack 成立），
            //   只请求 Room 个，AddStack 就能全加进去 —— 不会出现「扣了 5 个只进 3 个」。
            const int32 Added = Inv->AddStack(FInventoryStack(Got, Cls), /*allowPartialAdd=*/true);
            if (Added < Got)
            {
                // 理论上到不了（Room 由二分保证放得下）。真出现了说明云端的量已经先被删掉了，
                // 差额找不回来 —— 把数打出来，别让它静默发生。
                WT_WARNF(TEXT("[equip-consumable] WARNING lost %d of %s while topping up (room=%d got=%d added=%d)."),
                         Got - Added, *Cls->GetName(), Room, Got, Added);
            }
            WT_WARNF(TEXT("[equip-consumable] %s topped up from central-storage/warehouse: removed=%d added=%d have %d->%d (buffer=%d)."),
                     *Cls->GetName(), Got, Added, Have, Inv->GetNumItems(Cls), Buffer);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// ★2026-11-02 hook 安装：把集装箱直取接入原版中央存储计数/扣料 API。
// 两个原生函数均非虚（FGCentralStorageSubsystem.h 核验），用 SUBSCRIBE_METHOD（非虚成员函数 hook）。
// 全局 patch（与进程/世界无关）；用静态守卫保证只装一次。
// ---------------------------------------------------------------------------------------------
static void InstallSmartWarehouseHooks()
{
    static bool bInstalled = false;
    if (bInstalled)
    {
        return;
    }
    bInstalled = true;

    // Hook 1 —— 计数：记录纯云端真实存量 + 智能仓储开启时把集装箱可用量并入返回值。
    // ★用户铁律（云端在左、集装箱在右，显示严格分离）的落实方式：
    //   · 「显示层」分离：左侧位面列表由 GetAllItemsFromCentralStorage 直接枚举 mStoredItems，
    //     不读本返回值；集装箱货只在右侧智能仓储列表显示 —— 所以两列表天然不混。
    //   · 「判断/扣料层」合并：建造 / 制作台 / MAM / 成本校验靠 GetNumItemsFromCentralStorage
    //     判断「够不够」，**必须**含集装箱量，否则会判不足、用不了集装箱货（2026-11-03 实测回归）。
    //   故这里：始终记 Base 到 G_CentralStorageRealCount（供成本格纯云端着色）；
    //   开启时再 Scope.Override(Base + 集装箱可用量)，让判断层看到集装箱。
    //   物品始终留在集装箱，云端池 mStoredItems 零污染（本模块永不写入）。
    // 非虚 const 成员函数 => this 为 const AFGCentralStorageSubsystem*。
    using FGetNumItemsScope = TCallScope<int32(*)(const AFGCentralStorageSubsystem*, TSubclassOf<UFGItemDescriptor>)>;
    TFunction<void(FGetNumItemsScope&, const AFGCentralStorageSubsystem*, TSubclassOf<UFGItemDescriptor>)> GetNumItemsHandler =
        [](FGetNumItemsScope& Scope, const AFGCentralStorageSubsystem* Self, TSubclassOf<UFGItemDescriptor> Cls)
        {
            const int32 Base = Scope(Self, Cls);   // 跑原版（读 mStoredItems=云端池，纯云端值）
            if (Cls)
            {
                // 记录纯云端值，供成本格三源着色 / 任何需要「纯云端」的场合。
                G_CentralStorageRealCount.Add(Cls, Base);

                // ★2026-11-03 晚 回归修法：智能仓储开启时，把集装箱可用量**并入返回值**，
                //   让「建造 / 制作台 / MAM / 任意成本校验」看到并使用集装箱货。
                //   历史事故：为守「左右严格分离」把这里改成只记录不覆盖 → 建造判够不够时
                //   看不到集装箱量 → 判数量不足 → 开启开关反而用不了集装箱货（用户实测回归）。
                //   本处只并入「够不够」判断这一层；左侧位面列表的**显示**走
                //   GetAllItemsFromCentralStorage（直接枚举 mStoredItems），不读本返回值，
                //   故左右分离在显示层仍严格成立。开关关时 G_ContainerAvailability 已被清空
                //   （RefreshContainerAvailability / ticker 门控），加 0 等同原版。
                // ★2026-11-04 只在「位面存储站面板没开」时并入集装箱量：
                //   面板开着 = 玩家在看云端存货，必须给纯云端值（否则集装箱货看着像被上传进位面）。
                //   建造/制作/MAM 时面板没开，照旧并入，取料能力不变。
                if (Remote_FactorySmartWarehouse::IsSmartWarehouseEnabled() &&
                    !WT_IsCentralStoragePanelOpen(Self ? Self->GetWorld() : nullptr))
                {
                    const int32 ContainerN = G_ContainerAvailability.FindRef(Cls);
                    if (ContainerN > 0)
                    {
                        Scope.Override(Base + ContainerN);
                    }
                }
            }
        };
    SUBSCRIBE_METHOD(AFGCentralStorageSubsystem::GetNumItemsFromCentralStorage, MoveTemp(GetNumItemsHandler));

    // Hook 2 —— 扣料补足：原版先从 mStoredItems(云端) 扣，不足部分由集装箱直取补足，返回总量。
    // 非虚非 const 成员函数 => this 为 AFGCentralStorageSubsystem*。
    using FTryRemoveScope = TCallScope<int32(*)(AFGCentralStorageSubsystem*, TSubclassOf<UFGItemDescriptor>, int32)>;
    TFunction<void(FTryRemoveScope&, AFGCentralStorageSubsystem*, TSubclassOf<UFGItemDescriptor>, int32)> TryRemoveHandler =
        [](FTryRemoveScope& Scope, AFGCentralStorageSubsystem* Self, TSubclassOf<UFGItemDescriptor> Cls, int32 numToRemove)
        {
            const int32 RemovedFromCloud = Scope(Self, Cls, numToRemove);   // 原版从 mStoredItems 扣
            // 仅服务端权威世界真正从集装箱直取；客户端不抽（其量只是显示/检查副本）。
            // ★2026-11-04：位面存储站面板开着时不从集装箱补 —— 面板上点「取出」只应拿到云端货，
            //   否则等于从云端界面把集装箱的货拉走（用户明确要求两者严格分离）。
            bool bClient = (Self && Self->GetWorld() && Self->GetWorld()->GetNetMode() == NM_Client);
            int32 FromSW = 0;
            if (!bClient &&
                Remote_FactorySmartWarehouse::IsSmartWarehouseEnabled() &&      // ★关闭时行为必须等同原版
                !WT_IsCentralStoragePanelOpen(Self ? Self->GetWorld() : nullptr))
            {
                const int32 Deficit = numToRemove - RemovedFromCloud;
                if (Deficit > 0)
                {
                    FromSW = WT_PullFromContainers(Self ? Self->GetWorld() : nullptr, Cls, Deficit);
                }
            }
            Scope.Override(RemovedFromCloud + FromSW);
        };
    SUBSCRIBE_METHOD(AFGCentralStorageSubsystem::TryRemoveItemsFromCentralStorage, MoveTemp(TryRemoveHandler));

    WT_WARNF(TEXT("[smart-warehouse] hooks installed (direct-from-containers; GetNumItemsFromCentralStorage + TryRemoveItemsFromCentralStorage)."));
}

// ---------------------------------------------------------------------------------------------
// 智能仓储开关。
//
// 默认值：关（用户定案 —— 建造终端后由玩家自己在面板开启）。
// 持久化：**按存档**存，不是全局 ini。读写点在 Remote_FactorySave（hook AFGGameState::Serialize
//   的魔数段），所以它本质是「服务端存档状态」。
//
// ★谁可以改这个值（2026-11-04 明确，改错会导致联机行为不一致）：
//   · 服务端：Remote_FactoryRCO::Server_SetSmartWarehouse（权威写入，写完刷聚合表）
//   · 客户端：只有 Remote_FactoryRCO::Client_SetSmartWarehouse（收到服务端下发才写）
//   · 读档：Remote_FactorySave 在 Serialize(IsLoading) 里写（服务端读档）
//   · ★玩家点面板按钮【不再直接写本地】，只发 Server RPC。否则联机时本地和服务端会分叉：
//     客户端开着、服务端关着 ⇒ 面板显示有货但取货点了没反应（看起来像坏掉）。
//   · 本函数本身不做任何同步，纯粹是"设一下静态量"。需要同步时走 RCO，别在这里加网络逻辑。
// ---------------------------------------------------------------------------------------------
static bool GSmartWarehouseEnabled = false;   // ★默认关闭（用户要求）

bool Remote_FactorySmartWarehouse::IsSmartWarehouseEnabled()
{
    return GSmartWarehouseEnabled;
}

// 纯赋值 + 日志。★调用方须自己保证"改的是权威端"或是"收到服务端下发"，见上方清单。
void Remote_FactorySmartWarehouse::SetSmartWarehouseEnabled(bool bEnable)
{
    GSmartWarehouseEnabled = bEnable;
    WT_WARNF(TEXT("[smart-warehouse] %s."),
             bEnable ? TEXT("enabled") : TEXT("disabled"));
}

void Remote_FactorySmartWarehouse::LoadSmartWarehouseConfig()
{
    // ★2026-11-03 改为按存档持久化（见 Remote_FactorySave）：不再读全局 ini，默认值（关）由静态初始化提供，
    // 实际状态在载入存档时由 AFGGameState::Serialize hook 写入。保留空函数避免改动调用点。
}

const TMap<TSubclassOf<UFGItemDescriptor>, int32>& Remote_FactorySmartWarehouse::GetContainerAvailability()
{
    return G_ContainerAvailability;
}

const TMap<TSubclassOf<UFGItemDescriptor>, int32>& Remote_FactorySmartWarehouse::GetCentralStorageRealCount()
{
    return G_CentralStorageRealCount;
}

void Remote_FactorySmartWarehouse::RefreshContainerAvailability(UWorld* World)
{
    if (!IsSmartWarehouseEnabled())
    {
        G_ContainerAvailability.Reset();   // 关掉则清空可用量，hook 加 0，等同原版
        return;
    }
    WT_RefreshContainerAvailability(World);
}

// ---------------------------------------------------------------------------------------------
// 注册：安装 hook（全局一次），并在世界上启动集装箱可用量刷新 ticker（客户端+服务端各自刷新）。
// 开关关时不刷新（集装箱可用量保持空，hook 加 0，行为等同原版）。
// ---------------------------------------------------------------------------------------------
void Remote_FactorySmartWarehouse::RegisterHook(UWorld* World)
{
    LoadSmartWarehouseConfig();
    InstallSmartWarehouseHooks();

    if (!World)
    {
        WT_WARNF(TEXT("[smart-warehouse] no world at register -- availability ticker not started."));
        return;
    }

    // 每权威世界只启动一次（弱引用判重：换世界/重载后旧弱引用失效，新世界会重新启动）。
    static TWeakObjectPtr<UWorld> GSmartWarehouseWorld;
    if (GSmartWarehouseWorld.Get() == World)
    {
        return;
    }
    GSmartWarehouseWorld = World;

    TWeakObjectPtr<UWorld> WeakWorld(World);
    FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateLambda([WeakWorld](float) -> bool
        {
            UWorld* W = WeakWorld.Get();
            if (!W)
            {
                return false;   // 世界销毁，停表
            }
            if (!IsSmartWarehouseEnabled())
            {
                G_ContainerAvailability.Reset();   // 关掉则清空可用量，hook 加 0，等同原版
                return true;    // 保持 ticker，随时可重新开启
            }
            WT_RefreshContainerAvailability(W);
            WT_TopUpEquippedFuel(W);   // ★2026-11-03 反馈B：装备燃料从集装箱补进背包
            return true;
        }), 1.f);

    WT_WARNF(TEXT("[smart-warehouse] container-availability ticker started (1s); takes directly from vanilla storage containers when building (enabled)."));
}
