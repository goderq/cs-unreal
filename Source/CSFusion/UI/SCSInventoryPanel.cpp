// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "UI/SCSInventoryPanel.h"

#include "Characters/CSCharacter.h"
#include "Combat/CSMatchDirector.h"
#include "Core/CSCombatSettings.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Items/CSItemDefinition.h"
#include "Items/CSItemSettings.h"
#include "UI/CSUIStyle.h"
#include "Weapons/CSWeaponDefinition.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CSInventory"

namespace
{
	FLinearColor RarityColor(ECSItemRarity Rarity)
	{
		switch (Rarity)
		{
		case ECSItemRarity::Uncommon:	return FLinearColor(0.35f, 0.85f, 0.40f);
		case ECSItemRarity::Rare:		return FLinearColor(0.35f, 0.60f, 1.00f);
		case ECSItemRarity::Epic:		return FLinearColor(0.75f, 0.45f, 1.00f);
		default:						return CSUI::TextDim;
		}
	}

	FText TypeName(ECSItemType Type)
	{
		switch (Type)
		{
		case ECSItemType::Weapon:	return LOCTEXT("Weapon", "WEAPON");
		case ECSItemType::Ammo:		return LOCTEXT("Ammo", "AMMO");
		case ECSItemType::Grenade:	return LOCTEXT("Grenade", "GRENADE");
		case ECSItemType::Armor:	return LOCTEXT("Armor", "ARMOR");
		case ECSItemType::Medkit:	return LOCTEXT("Medkit", "MEDKIT");
		default:					return LOCTEXT("Misc", "ITEM");
		}
	}

	FText RarityName(ECSItemRarity Rarity)
	{
		return StaticEnum<ECSItemRarity>()->GetDisplayNameTextByValue(static_cast<int64>(Rarity));
	}

	const UCSWeaponDefinition* StarterWeapon()
	{
		return UCSCombatSettings::Get()->StarterWeapon.LoadSynchronous();
	}
}

void SCSInventoryPanel::Construct(const FArguments& InArgs)
{
	WorldContext = InArgs._WorldContext;
	OnClose = InArgs._OnClose;
	CloseKey = InArgs._CloseKey;
	bClosable = InArgs._ShowCloseButton;

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				CSUI::MakeHeader(LOCTEXT("Title", "INVENTORY"),
					LOCTEXT("Subtitle", "Held by the match authority. Dropped items appear in the world; the starter pistol always stays with you."))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
			[
				SNew(SBox).Visibility(InArgs._ShowCloseButton ? EVisibility::Visible : EVisibility::Collapsed)
				[
					CSUI::MakeButton(LOCTEXT("Close", "CLOSE  [TAB]"), FOnClicked::CreateLambda([this]() { OnClose.ExecuteIfBound(); return FReply::Handled(); }))
				]
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.52f).Padding(0.f, 0.f, 18.f, 0.f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(ListBox, SVerticalBox)
				]
			]
			+ SHorizontalBox::Slot().FillWidth(0.48f)
			[
				SNew(SBorder)
				.BorderImage(CSUI::WhiteBrush())
				.BorderBackgroundColor(CSUI::PanelRaised)
				.Padding(FMargin(24.f))
				[
					SAssignNew(DetailsBox, SBox)
				]
			]
		]
	];

	RebuildList();
}

ACSCharacter* SCSInventoryPanel::GetLocalCharacter() const
{
	const UWorld* World = WorldContext.IsValid() ? WorldContext->GetWorld() : nullptr;
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	return PC ? Cast<ACSCharacter>(PC->GetPawn()) : nullptr;
}

const ACSPlayerInventory* SCSInventoryPanel::GetInventory() const
{
	const ACSCharacter* Pawn = GetLocalCharacter();
	return Pawn ? ACSPlayerInventory::Find(Pawn, Pawn->GetOwningPlayerId()) : nullptr;
}

bool SCSInventoryPanel::CanAct() const
{
	const ACSCharacter* Pawn = GetLocalCharacter();
	return Pawn && GetInventory() && Pawn->IsAliveAuthoritative();
}

uint32 SCSInventoryPanel::ComputeSignature() const
{
	uint32 Hash = GetTypeHash(Selected) ^ (bInspect ? 0x9e3779b9u : 0u);
	const ACSPlayerInventory* Inventory = GetInventory();
	Hash = HashCombine(Hash, GetTypeHash(Inventory != nullptr));
	Hash = HashCombine(Hash, GetTypeHash(CanAct()));
	if (Inventory)
	{
		Hash = HashCombine(Hash, GetTypeHash(Inventory->GetEquippedSlot()));
		for (const FCSInventorySlot& Slot : Inventory->GetSlots())
		{
			Hash = HashCombine(Hash, GetTypeHash(Slot.ItemIndex));
			Hash = HashCombine(Hash, GetTypeHash(Slot.Count));
			Hash = HashCombine(Hash, GetTypeHash(Slot.AmmoInMag));
		}
	}
	return Hash;
}

void SCSInventoryPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	const uint32 Signature = ComputeSignature();
	if (Signature != LastSignature)
	{
		RebuildList();
	}
}

void SCSInventoryPanel::Select(int32 Slot)
{
	if (Selected != Slot)
	{
		Selected = Slot;
		bInspect = false;
	}
	RebuildList();
}

void SCSInventoryPanel::RebuildList()
{
	LastSignature = ComputeSignature();
	ListBox->ClearChildren();

	const ACSPlayerInventory* Inventory = GetInventory();
	const UCSItemSettings* Items = UCSItemSettings::Get();
	const int32 NumSlots = Inventory ? Inventory->GetSlots().Num() : Items->InventorySlots;

	// A selection that points at a slot that emptied falls back to the pistol.
	FCSInventorySlot SelectedData;
	if (Selected != INDEX_NONE && (!Inventory || !Inventory->GetSlot(Selected, SelectedData) || SelectedData.IsEmpty()))
	{
		Selected = INDEX_NONE;
		LastSignature = ComputeSignature();
	}

	ListBox->AddSlot().AutoHeight()
	[
		MakeRow(INDEX_NONE, nullptr, FCSInventorySlot(), !Inventory || Inventory->GetEquippedSlot() == INDEX_NONE)
	];

	for (int32 i = 0; i < NumSlots; ++i)
	{
		FCSInventorySlot Data;
		if (Inventory)
		{
			Inventory->GetSlot(i, Data);
		}
		const UCSItemDefinition* Item = Data.IsEmpty() ? nullptr : Items->GetItem(Data.ItemIndex);
		ListBox->AddSlot().AutoHeight()
		[
			MakeRow(i, Item, Data, Inventory && Inventory->GetEquippedSlot() == i)
		];
	}

	if (!Inventory)
	{
		ListBox->AddSlot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NotInMatch", "You are not in a match. Items you pick up exist only on the match authority and drop to the ground when you die or leave - nothing is carried between matches."))
			.Font(CSUI::Font(14))
			.ColorAndOpacity(CSUI::TextDim)
			.AutoWrapText(true)
		];
	}

	RebuildDetails();
}

TSharedRef<SWidget> SCSInventoryPanel::MakeRow(int32 Slot, const UCSItemDefinition* Item, const FCSInventorySlot& Data, bool bEquipped)
{
	const bool bPistol = Slot == INDEX_NONE;
	const bool bEmpty = !bPistol && !Item;
	const bool bSelected = Selected == Slot && !bEmpty;

	FText Name;
	FText IconTag;
	FText Detail;
	FText Category;
	FLinearColor Color = CSUI::Hover;

	if (bPistol)
	{
		const UCSWeaponDefinition* Pistol = StarterWeapon();
		Name = Pistol ? Pistol->DisplayName : LOCTEXT("Pistol", "Starter pistol");
		IconTag = LOCTEXT("PistolTag", "P");
		Detail = LOCTEXT("PistolDetail", "always carried");
		Category = LOCTEXT("Starter", "STARTER");
		Color = FLinearColor(0.45f, 0.48f, 0.52f);
	}
	else if (Item)
	{
		Name = Item->DisplayName;
		IconTag = CSUI::ShortTag(Item->DisplayName);
		Category = TypeName(Item->ItemType);
		Color = Item->PlaceholderColor;
		Detail = Item->IsWeapon()
			? FText::Format(LOCTEXT("Rounds", "{0} rds loaded"), FText::AsNumber(Data.AmmoInMag))
			: FText::Format(LOCTEXT("Count", "x{0}"), FText::AsNumber(Data.Count));
	}
	else
	{
		Name = LOCTEXT("Empty", "Empty");
		Category = FText::GetEmpty();
	}

	// Number key hint: 1 = pistol, 2.. = slots.
	const FText KeyHint = FText::AsNumber(bPistol ? 1 : Slot + 2);

	return SNew(SBox).Padding(FMargin(0.f, 3.f))
		[
			SNew(SButton).IsFocusable(false)
			.ButtonStyle(&CSUI::ButtonStyle(bSelected ? CSUI::EButtonKind::Primary : CSUI::EButtonKind::Normal))
			.IsEnabled(!bEmpty)
			.ContentPadding(FMargin(10.f, 8.f))
			.OnClicked_Lambda([this, Slot]() { Select(Slot); return FReply::Handled(); })
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
				[
					SNew(SBox).WidthOverride(18.f)
					[
						SNew(STextBlock).Text(KeyHint).Font(CSUI::Font(13, true)).ColorAndOpacity(CSUI::TextDim)
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 14.f, 0.f)
				[
					CSUI::MakeItemIcon(bEmpty ? CSUI::Hover : Color, IconTag, 52.f)
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Text(Name).Font(CSUI::Font(17, !bEmpty)).ColorAndOpacity(bEmpty ? CSUI::TextDim : CSUI::Text)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
					[
						SNew(STextBlock).Text(Category).Font(CSUI::Font(11, true)).ColorAndOpacity(CSUI::TextDim)
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10.f, 0.f)
				[
					SNew(STextBlock).Text(Detail).Font(CSUI::Font(14)).ColorAndOpacity(CSUI::Text)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Equipped", "EQUIPPED"))
					.Font(CSUI::Font(11, true))
					.ColorAndOpacity(bSelected ? CSUI::Text : CSUI::Accent)
					.Visibility(bEquipped ? EVisibility::Visible : EVisibility::Collapsed)
				]
			]
		];
}

TSharedRef<SWidget> SCSInventoryPanel::MakeStats(const UCSItemDefinition* Item, const UCSWeaponDefinition* Weapon, const FCSInventorySlot& Data) const
{
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	auto Stat = [&Box](const FText& Label, const FText& Value)
	{
		Box->AddSlot().AutoHeight().Padding(0.f, 3.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(STextBlock).Text(Label).Font(CSUI::Font(14)).ColorAndOpacity(CSUI::TextDim)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock).Text(Value).Font(CSUI::Font(14, true)).ColorAndOpacity(CSUI::Text)
			]
		];
	};

	FNumberFormattingOptions OneDecimal;
	OneDecimal.SetMaximumFractionalDigits(1);

	if (Weapon)
	{
		const FText Damage = Weapon->PelletsPerShot > 1
			? FText::Format(LOCTEXT("PelletDmg", "{0} x {1}"), FText::AsNumber(Weapon->BaseDamage, &OneDecimal), FText::AsNumber(Weapon->PelletsPerShot))
			: FText::AsNumber(Weapon->BaseDamage, &OneDecimal);
		Stat(LOCTEXT("Damage", "Damage"), Damage);
		Stat(LOCTEXT("Rpm", "Fire rate"), FText::Format(LOCTEXT("RpmValue", "{0} rpm"), FText::AsNumber(FMath::RoundToInt(Weapon->RoundsPerMinute))));
		Stat(LOCTEXT("Mode", "Mode"), Weapon->bAutomatic ? LOCTEXT("Auto", "Automatic") : LOCTEXT("Semi", "Semi-auto"));
		Stat(LOCTEXT("Mag", "Magazine"), FText::AsNumber(Weapon->MagazineSize));
		if (bInspect)
		{
			Stat(LOCTEXT("Head", "Headshot multiplier"), FText::Format(LOCTEXT("Mult", "x{0}"), FText::AsNumber(Weapon->HeadshotMultiplier, &OneDecimal)));
			Stat(LOCTEXT("Reload", "Reload time"), FText::Format(LOCTEXT("Secs", "{0} s"), FText::AsNumber(Weapon->ReloadSeconds, &OneDecimal)));
			Stat(LOCTEXT("Range", "Effective range"), FText::Format(LOCTEXT("Meters", "{0} m"), FText::AsNumber(FMath::RoundToInt(Weapon->FalloffStartDistance / 100.f))));
			Stat(LOCTEXT("Hip", "Hip / aim spread"), FText::Format(LOCTEXT("Spread", "{0} / {1} deg"),
				FText::AsNumber(Weapon->HipSpreadDegrees, &OneDecimal), FText::AsNumber(Weapon->AimSpreadDegrees, &OneDecimal)));
			Stat(LOCTEXT("Reserve", "Reserve"), Weapon->ReserveAmmo < 0 ? LOCTEXT("Infinite", "Unlimited") : LOCTEXT("FromAmmo", "Ammo items in inventory"));
		}
	}

	if (Item)
	{
		if (Item->IsWeapon())
		{
			Stat(LOCTEXT("Loaded", "Loaded"), FText::AsNumber(Data.AmmoInMag));
		}
		else
		{
			Stat(LOCTEXT("Quantity", "Quantity"), Item->bStackable
				? FText::Format(LOCTEXT("OfMax", "{0} / {1}"), FText::AsNumber(Data.Count), FText::AsNumber(Item->MaxStack))
				: FText::AsNumber(Data.Count));
		}
		if (Item->HealAmount > 0.f)
		{
			Stat(LOCTEXT("Heals", "Restores health"), FText::AsNumber(FMath::RoundToInt(Item->HealAmount)));
		}
		if (Item->ArmorAmount > 0.f)
		{
			Stat(LOCTEXT("ArmorAmt", "Adds armor"), FText::AsNumber(FMath::RoundToInt(Item->ArmorAmount)));
		}
		if (bInspect)
		{
			Stat(LOCTEXT("Rarity", "Rarity"), RarityName(Item->Rarity));
			Stat(LOCTEXT("Weight", "Weight"), FText::AsNumber(Item->Weight, &OneDecimal));
			Stat(LOCTEXT("Id", "Item id"), FText::FromName(Item->ItemId));
		}
	}

	return Box;
}

void SCSInventoryPanel::RebuildDetails()
{
	const ACSPlayerInventory* Inventory = GetInventory();
	const UCSItemSettings* Items = UCSItemSettings::Get();

	FCSInventorySlot Data;
	const UCSItemDefinition* Item = nullptr;
	if (Selected != INDEX_NONE && Inventory && Inventory->GetSlot(Selected, Data) && !Data.IsEmpty())
	{
		Item = Items->GetItem(Data.ItemIndex);
	}

	const bool bPistol = Item == nullptr;
	const UCSWeaponDefinition* Weapon = bPistol ? StarterWeapon() : (Item->IsWeapon() ? Item->Weapon.LoadSynchronous() : nullptr);

	const FText Name = bPistol ? (Weapon ? Weapon->DisplayName : LOCTEXT("Pistol", "Starter pistol")) : Item->DisplayName;
	const FText Description = bPistol
		? LOCTEXT("PistolDesc", "Your sidearm. Never stored in the inventory, never dropped, never stolen - you respawn with it every time. Unlimited reserve ammo.")
		: Item->Description;
	const FLinearColor Color = bPistol ? FLinearColor(0.45f, 0.48f, 0.52f) : Item->PlaceholderColor;
	const FText IconTag = bPistol ? LOCTEXT("PistolTag", "P") : CSUI::ShortTag(Item->DisplayName);
	const FText Kind = bPistol ? LOCTEXT("StarterWeapon", "STARTER WEAPON")
		: FText::Format(LOCTEXT("KindRarity", "{0}  -  {1}"), TypeName(Item->ItemType), RarityName(Item->Rarity));
	const FLinearColor KindColor = bPistol ? CSUI::TextDim : RarityColor(Item->Rarity);

	const bool bEquipped = Inventory && Inventory->GetEquippedSlot() == Selected;
	FText ActionLabel = LOCTEXT("Equip", "EQUIP");
	bool bActionEnabled = CanAct() && !bEquipped;
	if (Item)
	{
		switch (Item->ItemType)
		{
		case ECSItemType::Medkit:
		case ECSItemType::Armor:
			ActionLabel = LOCTEXT("Use", "USE");
			bActionEnabled = CanAct();
			break;
		case ECSItemType::Weapon:
			break;
		default:
			// Ammo is consumed by reloading; grenades are thrown in a later stage.
			ActionLabel = Item->ItemType == ECSItemType::Ammo ? LOCTEXT("UsedOnReload", "USED ON RELOAD") : LOCTEXT("NotUsable", "NOT USABLE YET");
			bActionEnabled = false;
			break;
		}
	}
	if (bEquipped)
	{
		ActionLabel = LOCTEXT("EquippedBtn", "EQUIPPED");
	}

	DetailsBox->SetContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
		[
			CSUI::MakeItemIcon(Color, IconTag, 120.f)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 16.f, 0.f, 0.f)
		[
			SNew(STextBlock).Text(Name).Font(CSUI::Font(26, true)).ColorAndOpacity(CSUI::Text)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
		[
			SNew(STextBlock).Text(Kind).Font(CSUI::Font(12, true)).ColorAndOpacity(KindColor)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 12.f)
		[
			SNew(STextBlock).Text(Description).Font(CSUI::Font(14)).ColorAndOpacity(CSUI::TextDim).AutoWrapText(true)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeStats(Item, Weapon, Data)
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SBox)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 16.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)
			[
				CSUI::MakeButton(ActionLabel, FOnClicked::CreateLambda([this]() { EquipSelected(); return FReply::Handled(); }),
					CSUI::EButtonKind::Primary, bActionEnabled, 15)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)
			[
				CSUI::MakeButton(LOCTEXT("Drop", "DROP"), FOnClicked::CreateLambda([this]() { DropSelected(); return FReply::Handled(); }),
					CSUI::EButtonKind::Danger, CanAct() && !bPistol, 15)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				CSUI::MakeButton(bInspect ? LOCTEXT("Less", "LESS") : LOCTEXT("Inspect", "INSPECT"),
					FOnClicked::CreateLambda([this]() { bInspect = !bInspect; RebuildList(); return FReply::Handled(); }),
					CSUI::EButtonKind::Normal, true, 15)
			]
		]);
}

FReply SCSInventoryPanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (bClosable && (Key == EKeys::Escape || (CloseKey.IsValid() && Key == CloseKey)))
	{
		OnClose.ExecuteIfBound();
		return FReply::Handled();
	}

	// Number keys select like they equip in game: 1 = pistol, 2.. = slots.
	static const FKey Digits[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
	for (int32 i = 0; i < UE_ARRAY_COUNT(Digits); ++i)
	{
		if (Key == Digits[i])
		{
			Select(i == 0 ? INDEX_NONE : i - 1);
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

void SCSInventoryPanel::EquipSelected()
{
	if (ACSCharacter* Pawn = GetLocalCharacter())
	{
		// Same request as pressing the slot's number key; the authority decides.
		Pawn->RequestSlot(Selected);
	}
}

void SCSInventoryPanel::DropSelected()
{
	if (Selected == INDEX_NONE)
	{
		return;
	}
	if (ACSCharacter* Pawn = GetLocalCharacter())
	{
		Pawn->RequestDropSlot(Selected);
	}
}

#undef LOCTEXT_NAMESPACE
