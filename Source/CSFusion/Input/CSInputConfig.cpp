// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Input/CSInputConfig.h"

#include "Core/CSLog.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

UInputMappingContext* UCSInputConfig::BuildRuntimeMappingContext(UObject* Outer) const
{
	UInputMappingContext* Context = NewObject<UInputMappingContext>(
		Outer ? Outer : GetTransientPackage(), NAME_None, RF_Transient);

	int32 Count = 0;

	auto Map = [&Context, &Count](UInputAction* Action, const FKey& Key,
		std::initializer_list<UInputModifier*> Modifiers) -> void
	{
		if (!Action || !Key.IsValid())
		{
			return;
		}

		FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, Key);
		for (UInputModifier* Modifier : Modifiers)
		{
			if (Modifier)
			{
				Mapping.Modifiers.Add(Modifier);
			}
		}
		++Count;
	};

	auto MakeNegate = [Context]() -> UInputModifier*
	{
		return NewObject<UInputModifierNegate>(Context);
	};

	auto MakeSwizzleYXZ = [Context]() -> UInputModifier*
	{
		UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(Context);
		Swizzle->Order = EInputAxisSwizzle::YXZ;
		return Swizzle;
	};

	// Movement is one Axis2D action: X is strafe (right positive), Y is
	// forward. ACSCharacter::Input_Move reads them that way, so W and S are
	// swizzled onto Y and S and A are negated.
	Map(IA_Move, Key_MoveForward, { MakeSwizzleYXZ() });
	Map(IA_Move, Key_MoveBack,    { MakeSwizzleYXZ(), MakeNegate() });
	Map(IA_Move, Key_MoveRight,   {});
	Map(IA_Move, Key_MoveLeft,    { MakeNegate() });

	Map(IA_Look,            Key_Look,            {});
	Map(IA_Jump,            Key_Jump,            {});
	Map(IA_Sprint,          Key_Sprint,          {});
	Map(IA_Crouch,          Key_Crouch,          {});
	Map(IA_Fire,            Key_Fire,            {});
	Map(IA_Aim,             Key_Aim,             {});
	Map(IA_Reload,          Key_Reload,          {});
	Map(IA_Interact,        Key_Interact,        {});
	Map(IA_ToggleInventory, Key_ToggleInventory, {});
	Map(IA_PauseMenu,       Key_PauseMenu,       {});
	Map(IA_Scoreboard,      Key_Scoreboard,      {});

	UE_LOG(LogCS, Log, TEXT("Built runtime mapping context with %d mappings."), Count);
	return Context;
}
