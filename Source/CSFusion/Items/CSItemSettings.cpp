// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Items/CSItemSettings.h"

#include "Items/CSItemDefinition.h"

int32 UCSItemSettings::FindItemIndex(FName ItemId) const
{
	if (ItemId.IsNone())
	{
		return INDEX_NONE;
	}

	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		if (const UCSItemDefinition* Item = Items[Index].LoadSynchronous())
		{
			if (Item->ItemId == ItemId)
			{
				return Index;
			}
		}
	}
	return INDEX_NONE;
}

const UCSItemDefinition* UCSItemSettings::GetItem(int32 Index) const
{
	return Items.IsValidIndex(Index) ? Items[Index].LoadSynchronous() : nullptr;
}
