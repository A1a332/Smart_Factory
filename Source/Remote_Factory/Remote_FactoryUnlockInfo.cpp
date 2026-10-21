#include "Remote_FactoryUnlockInfo.h"

void UWTUnlockInfo::WT_Setup(const FText& Name, const FText& Description,
                             UTexture2D* BigIcon, UTexture2D* SmallIcon)
{
	// 基类字段是 protected + EditDefaultsOnly（纯数据、无逻辑），派生类直接赋值即可。
	// 不需要 AccessTransformers：C++ 的 protected 成员对派生类可见。
	mUnlockName        = Name;
	mUnlockDescription = Description;
	mUnlockIconBig     = BigIcon;
	mUnlockIconSmall   = SmallIcon;
}
