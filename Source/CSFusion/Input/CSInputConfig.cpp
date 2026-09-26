// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Input/CSInputConfig.h"

#include "Core/CSLog.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

#define LOCTEXT_NAMESPACE "CSInputConfig"

TArray<FCSRebindableBinding> UCSInputConfig::GetRebindableBindings() const
{
	// Ids are persisted in the settings save - never rename one, only add.
	return {
		{ TEXT("MoveForward"),     LOCTEXT("MoveForward", "Move forward"),    Key_MoveForward },
		{ TEXT("MoveBack"),        LOCTEXT("MoveBack", "Move back"),          Key_MoveBack },
		{ TEXT("MoveLeft"),        LOCTEXT("MoveLeft", "Move left"),          Key_MoveLeft },
		{ TEXT("MoveRight"),       LOCTEXT("MoveRight", "Move right"),        Key_MoveRight },
		{ TEXT("Jump"),            LOCTEXT("Jump", "Jump"),                   Key_Jump },
		{ TEXT("Sprint"),          LOCTEXT("Sprint", "Sprint"),               Key_Sprint },
		{ TEXT("Crouch"),          LOCTEXT("Crouch", "Crouch"),               Key_Crouch },
		{ TEXT("Reload"),          LOCTEXT("Reload", "Reload"),               Key_Reload },
		{ TEXT("Interact"),        LOCTEXT("Interact", "Pick up / ammo machine"), Key_Interact },
		{ TEXT("Drop"),            LOCTEXT("Drop", "Drop weapon"),            Key_Drop },
		{ TEXT("Scoreboard"),      LOCTEXT("Scoreboard", "Scoreboard (hold)"), Key_Scoreboard },
		{ TEXT("BuyMenu"),         LOCTEXT("BuyMenu", "Shop"),                Key_BuyMenu },
		{ TEXT("Inspect"),         LOCTEXT("Inspect", "Inspect weapon"),      Key_Inspect },
	};
}

UInputMappingContext* UCSInputConfig::BuildRuntimeMappingContext(UObject* Outer) const
{
	return BuildRuntimeMappingContext(Outer, [](FName, const FKey& Default) { return Default; });
}

UInputMappingContext* UCSInputConfig::BuildRuntimeMappingContext(UObject* Outer,
	TFunctionRef<FKey(FName, const FKey&)> Resolve) const
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
	Map(IA_Move, Resolve(TEXT("MoveForward"), Key_MoveForward), { MakeSwizzleYXZ() });
	Map(IA_Move, Resolve(TEXT("MoveBack"),    Key_MoveBack),    { MakeSwizzleYXZ(), MakeNegate() });
	Map(IA_Move, Resolve(TEXT("MoveRight"),   Key_MoveRight),   {});
	Map(IA_Move, Resolve(TEXT("MoveLeft"),    Key_MoveLeft),    { MakeNegate() });

	Map(IA_Look,            Key_Look,                                          {});
	Map(IA_Jump,            Resolve(TEXT("Jump"), Key_Jump),                   {});
	Map(IA_Sprint,          Resolve(TEXT("Sprint"), Key_Sprint),               {});
	Map(IA_Crouch,          Resolve(TEXT("Crouch"), Key_Crouch),               {});
	Map(IA_Fire,            Key_Fire,                                          {});
	Map(IA_Aim,             Key_Aim,                                           {});
	Map(IA_Reload,          Resolve(TEXT("Reload"), Key_Reload),               {});
	Map(IA_Interact,        Resolve(TEXT("Interact"), Key_Interact),           {});
	Map(IA_PauseMenu,       Key_PauseMenu,                                     {});
	Map(IA_Scoreboard,      Resolve(TEXT("Scoreboard"), Key_Scoreboard),       {});
	Map(IA_Drop,            Resolve(TEXT("Drop"), Key_Drop),                   {});
	Map(GetBuyMenuAction(), Resolve(TEXT("BuyMenu"), Key_BuyMenu),        {});
	Map(GetInspectAction(), Resolve(TEXT("Inspect"), Key_Inspect),        {});

	// Every number key drives the same action; a Scalar modifier turns the
	// digital 1.0 into the key's own number, which Input_EquipSlot decodes.
	for (int32 i = 0; i < SlotKeys.Num(); ++i)
	{
		UInputModifierScalar* Scalar = NewObject<UInputModifierScalar>(Context);
		Scalar->Scalar = FVector(static_cast<double>(i + 1));
		Map(IA_EquipSlot, SlotKeys[i], { Scalar });
	}

	UE_LOG(LogCS, Log, TEXT("Built runtime mapping context with %d mappings."), Count);
	return Context;
}

#undef LOCTEXT_NAMESPACE

UInputAction* UCSInputConfig::GetBuyMenuAction() const
{
	if (!IA_BuyMenuRuntime)
	{
		IA_BuyMenuRuntime = NewObject<UInputAction>(const_cast<UCSInputConfig*>(this), TEXT("IA_BuyMenuRuntime"), RF_Transient);
		IA_BuyMenuRuntime->ValueType = EInputActionValueType::Boolean;
	}
	return IA_BuyMenuRuntime;
}

UInputAction* UCSInputConfig::GetInspectAction() const
{
	if (!IA_InspectRuntime)
	{
		IA_InspectRuntime = NewObject<UInputAction>(const_cast<UCSInputConfig*>(this), TEXT("IA_InspectRuntime"), RF_Transient);
		IA_InspectRuntime->ValueType = EInputActionValueType::Boolean;
	}
	return IA_InspectRuntime;
}
