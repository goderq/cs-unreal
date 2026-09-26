// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "UI/SCSArsenalPanel.h"

#include "Core/CSCombatSettings.h"
#include "Core/CSModeSettings.h"
#include "Engine/Texture2D.h"
#include "Items/CSItemDefinition.h"
#include "Items/CSItemSettings.h"
#include "Items/CSShopSettings.h"
#include "UI/CSUIStyle.h"
#include "Weapons/CSWeaponDefinition.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CSArsenal"

namespace
{
	const FCSShopEntry* EntryFor(const UCSItemDefinition* Item)
	{
		for (const FCSShopEntry& Entry : UCSShopSettings::Get()->Entries)
		{
			if (Item && Entry.ItemId == Item->ItemId)
			{
				return &Entry;
			}
		}
		return nullptr;
	}

	FText Number(float Value, int32 Digits = 0)
	{
		return FText::AsNumber(Value, &FNumberFormattingOptions().SetMinimumFractionalDigits(Digits).SetMaximumFractionalDigits(Digits));
	}

	/** Label, value and a bar, as in the in-match shop. */
	void AddStat(const TSharedRef<SVerticalBox>& Box, const FText& Label, float Fraction, const FText& Value)
	{
		Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)[ SNew(STextBlock).Text(Label).Font(CSUI::Font(13)).ColorAndOpacity(CSUI::TextDim) ]
			+ SHorizontalBox::Slot().AutoWidth()[ SNew(STextBlock).Text(Value).Font(CSUI::Font(13, true)).ColorAndOpacity(CSUI::Text) ]
		];
		Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 9.f)[ CSUI::MakeStatBar(FMath::Clamp(Fraction, 0.f, 1.f), CSUI::Accent) ];
	}

	/** Label over its value: for long labels in a narrow column. */
	void AddFactStacked(const TSharedRef<SVerticalBox>& Box, const FText& Label, const FText& Value)
	{
		Box->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
		[
			SNew(STextBlock).Text(Label).Font(CSUI::Font(12, true)).ColorAndOpacity(CSUI::TextDim)
		];
		Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
		[
			SNew(STextBlock).Text(Value).Font(CSUI::Font(14, true)).ColorAndOpacity(CSUI::Text).AutoWrapText(true)
		];
	}

	void AddFact(const TSharedRef<SVerticalBox>& Box, const FText& Label, const FText& Value)
	{
		Box->AddSlot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)[ SNew(STextBlock).Text(Label).Font(CSUI::Font(13)).ColorAndOpacity(CSUI::TextDim) ]
			+ SHorizontalBox::Slot().AutoWidth()[ SNew(STextBlock).Text(Value).Font(CSUI::Font(13, true)).ColorAndOpacity(CSUI::Text) ]
		];
	}
}

void SCSArsenalPanel::Construct(const FArguments& InArgs)
{
	// Weapons in shop order, then any the shop does not sell (the starter
	// pistol, the knife).
	const UCSItemSettings* Items = UCSItemSettings::Get();
	for (const FCSShopEntry& Entry : UCSShopSettings::Get()->Entries)
	{
		const int32 Index = Items->FindItemIndex(Entry.ItemId);
		const UCSItemDefinition* Item = Items->GetItem(Index);
		if (Item && Item->IsWeapon())
		{
			Weapons.AddUnique(Index);
		}
	}
	for (int32 i = 0; Items->IsValidIndex(i); ++i)
	{
		if (const UCSItemDefinition* Item = Items->GetItem(i); Item && Item->IsWeapon())
		{
			Weapons.AddUnique(i);
		}
	}

	ChildSlot
	[
		InArgs._bCatalog ? MakeCatalog() : MakeArsenal()
	];
	if (Weapons.Num() > 0)
	{
		SelectWeapon(Weapons[0]);
	}
}

TSharedRef<SWidget> SCSArsenalPanel::MakeIcon(const UCSItemDefinition* Item, float Size)
{
	if (Item)
	{
		if (UTexture2D* Texture = Item->Icon.LoadSynchronous())
		{
			TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateBrush>();
			Brush->SetResourceObject(Texture);
			const float Aspect = Texture->GetSizeY() > 0 ? static_cast<float>(Texture->GetSizeX()) / Texture->GetSizeY() : 1.f;
			Brush->ImageSize = FVector2D(Size * FMath::Max(1.f, Aspect), Size);
			Brushes.Add(Brush);
			return SNew(SBox).WidthOverride(Size * 2.f).HeightOverride(Size).HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(SImage).Image(Brush.Get())
				];
		}
		return SNew(SBox).WidthOverride(Size * 2.f).HAlign(HAlign_Left)
			[
				CSUI::MakeItemIcon(Item->PlaceholderColor, CSUI::ShortTag(Item->DisplayName), Size)
			];
	}
	return SNew(SBox).WidthOverride(Size * 2.f);
}

// ---------------------------------------------------------------------------
// Arsenal
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SCSArsenalPanel::MakeArsenal()
{
	struct FSlotInfo { FText Key; FText Name; FText Body; };
	const FSlotInfo Slots[] = {
		{ LOCTEXT("S1", "1"), LOCTEXT("S1Name", "PRIMARY"), LOCTEXT("S1Body", "Rifle, SMG, shotgun or sniper, bought in the match. Drops where you die.") },
		{ LOCTEXT("S2", "2"), LOCTEXT("S2Name", "PISTOL"), LOCTEXT("S2Body", "A P2000 at every spawn; better pistols in the shop.") },
		{ LOCTEXT("S3", "3"), LOCTEXT("S3Name", "KNIFE"), LOCTEXT("S3Body", "Always carried. Light and heavy stab, extra damage from behind.") },
		{ LOCTEXT("S4", "4"), LOCTEXT("S4Name", "FRAG"), LOCTEXT("S4Body", "Bought. Bounces, then explodes; hurts everyone close.") },
		{ LOCTEXT("S5", "5"), LOCTEXT("S5Name", "FLASHBANG"), LOCTEXT("S5Body", "Bought. Blinds whoever looks at it, bots included.") },
	};
	TSharedRef<SHorizontalBox> SlotRow = SNew(SHorizontalBox);
	for (int32 i = 0; i < UE_ARRAY_COUNT(Slots); ++i)
	{
		SlotRow->AddSlot().FillWidth(1.f).Padding(i ? 8.f : 0.f, 0.f, 0.f, 0.f)
		[
			SNew(SBorder).BorderImage(CSUI::WhiteBrush()).BorderBackgroundColor(CSUI::PanelRaised).Padding(FMargin(14.f, 10.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)
					[
						SNew(STextBlock).Text(Slots[i].Key).Font(CSUI::Font(18, true)).ColorAndOpacity(CSUI::Accent)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(Slots[i].Name).Font(CSUI::Font(13, true)).ColorAndOpacity(CSUI::Text)
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
				[
					SNew(STextBlock).Text(Slots[i].Body).Font(CSUI::Font(12)).ColorAndOpacity(CSUI::TextDim).AutoWrapText(true)
				]
			]
		];
	}

	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	const UCSItemSettings* Items = UCSItemSettings::Get();
	for (const int32 Index : Weapons)
	{
		const UCSItemDefinition* Item = Items->GetItem(Index);
		const FCSShopEntry* Entry = EntryFor(Item);
		List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
		[
			SNew(SButton).IsFocusable(false)
			.ButtonStyle(&CSUI::ButtonStyle(CSUI::EButtonKind::Normal))
			.ContentPadding(FMargin(10.f, 6.f))
			.OnClicked_Lambda([this, Index]() { SelectWeapon(Index); return FReply::Handled(); })
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
				[
					MakeIcon(Item, 26.f)
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Text(Item->DisplayName).Font(CSUI::Font(15, true))
						.ColorAndOpacity_Lambda([this, Index]() { return FSlateColor(Selected == Index ? CSUI::Accent : CSUI::Text); })
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Font(CSUI::Font(11, true)).ColorAndOpacity(CSUI::TextDim)
						.Text(Entry ? UCSShopSettings::CategoryName(Entry->Category) : LOCTEXT("Standard", "STANDARD ISSUE"))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock).Font(CSUI::Font(14, true)).ColorAndOpacity(CSUI::Money)
					.Text(Entry ? CSUI::MoneyText(Entry->Price) : LOCTEXT("Free", "free"))
				]
			]
		];
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			CSUI::MakeHeader(LOCTEXT("ArsenalTitle", "ARSENAL"),
				LOCTEXT("ArsenalBody", "What you carry and what every weapon does. Weapons are bought during a match with the money it pays; the numbers are the ones the match uses."))
		]
		+ SVerticalBox::Slot().AutoHeight()[ CSUI::MakeSectionLabel(LOCTEXT("Loadout", "LOADOUT")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 16.f)[ SlotRow ]
		+ SVerticalBox::Slot().AutoHeight()[ CSUI::MakeSectionLabel(LOCTEXT("WeaponsLabel", "WEAPONS")) ]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.42f)
			[
				SNew(SScrollBox) + SScrollBox::Slot().Padding(FMargin(0.f, 0.f, 10.f, 0.f))[ List ]
			]
			+ SHorizontalBox::Slot().FillWidth(0.58f).Padding(16.f, 0.f, 0.f, 0.f)
			[
				SNew(SBorder).BorderImage(CSUI::WhiteBrush()).BorderBackgroundColor(CSUI::PanelRaised).Padding(FMargin(22.f, 18.f))
				[
					SNew(SScrollBox) + SScrollBox::Slot()[ SAssignNew(Details, SBox) ]
				]
			]
		];
}

void SCSArsenalPanel::SelectWeapon(int32 ItemIndex)
{
	Selected = ItemIndex;
	RebuildDetails();
}

void SCSArsenalPanel::RebuildDetails()
{
	if (!Details.IsValid())
	{
		return;
	}
	const UCSItemDefinition* Item = UCSItemSettings::Get()->GetItem(Selected);
	const UCSWeaponDefinition* Weapon = Item ? Item->Weapon.LoadSynchronous() : nullptr;
	if (!Item || !Weapon)
	{
		Details->SetContent(SNullWidget::NullWidget);
		return;
	}
	const FCSShopEntry* Entry = EntryFor(Item);
	const float Health = GetDefault<UCSCombatSettings>()->MaxHealth;

	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 14.f, 0.f)[ MakeIcon(Item, 48.f) ]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Text(Item->DisplayName).Font(CSUI::Font(24, true)).ColorAndOpacity(CSUI::Text) ]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Font(CSUI::Font(12, true)).ColorAndOpacity(CSUI::Money)
					.Text(Entry ? FText::Format(LOCTEXT("PriceLine", "{0}  -  {1}"), CSUI::MoneyText(Entry->Price), UCSShopSettings::CategoryName(Entry->Category))
						: LOCTEXT("Issued", "Standard issue"))
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 14.f)
		[
			SNew(STextBlock).Text(Item->Description.IsEmpty() && Entry ? FText::FromString(Entry->Blurb) : Item->Description)
			.Font(CSUI::Font(13)).ColorAndOpacity(CSUI::TextDim).AutoWrapText(true)
		];

	if (Weapon->Kind == ECSWeaponKind::Knife)
	{
		AddStat(Box, LOCTEXT("Stab", "Light stab"), Weapon->MeleeDamage / Health, Number(Weapon->MeleeDamage));
		AddStat(Box, LOCTEXT("Heavy", "Heavy stab"), Weapon->MeleeHeavyDamage / Health, Number(Weapon->MeleeHeavyDamage));
		AddFact(Box, LOCTEXT("Back", "From behind"), FText::Format(LOCTEXT("Times", "x{0}"), Number(Weapon->BackstabMultiplier, 1)));
		AddFact(Box, LOCTEXT("Reach", "Reach"), FText::Format(LOCTEXT("Metres1", "{0} m"), Number(Weapon->MeleeRange / 100.f, 1)));
		AddFact(Box, LOCTEXT("Interval", "Stabs per second"), Number(1.f / FMath::Max(0.05f, Weapon->MeleeInterval), 1));
		Details->SetContent(Box);
		return;
	}

	const float Damage = Weapon->BaseDamage * FMath::Max(1, Weapon->PelletsPerShot);
	const int32 BodyShots = FMath::CeilToInt(Health / FMath::Max(1.f, Damage));
	const int32 HeadShots = FMath::CeilToInt(Health / FMath::Max(1.f, Damage * Weapon->HeadshotMultiplier));
	const float Interval = 60.f / FMath::Max(1.f, Weapon->RoundsPerMinute);
	const float TtkMs = (BodyShots - 1) * Interval * 1000.f;
	const FText PerShot = Weapon->PelletsPerShot > 1
		? FText::Format(LOCTEXT("Pellets", "{0} ({1} pellets)"), Number(Damage), Number(Weapon->PelletsPerShot))
		: Number(Damage);

	AddStat(Box, LOCTEXT("Damage", "Damage per shot"), Damage / 120.f, PerShot);
	AddStat(Box, LOCTEXT("Rate", "Fire rate"), Weapon->RoundsPerMinute / 900.f,
		FText::Format(LOCTEXT("Rpm", "{0} rpm{1}"), Number(Weapon->RoundsPerMinute), Weapon->bAutomatic ? LOCTEXT("Auto", ", automatic") : FText::GetEmpty()));
	AddStat(Box, LOCTEXT("Dps", "Damage per second"), Damage * Weapon->RoundsPerMinute / 60.f / 700.f, Number(Damage * Weapon->RoundsPerMinute / 60.f));
	AddStat(Box, LOCTEXT("Accuracy", "Accuracy from the hip"), 1.f - Weapon->HipSpreadDegrees / 6.f,
		FText::Format(LOCTEXT("Deg", "{0} deg (aimed {1})"), Number(Weapon->HipSpreadDegrees, 1), Number(Weapon->AimSpreadDegrees, 1)));
	AddStat(Box, LOCTEXT("Recoil", "Recoil"), Weapon->RecoilPitch / 1.5f,
		FText::Format(LOCTEXT("RecoilValue", "{0} deg a shot"), Number(Weapon->RecoilPitch, 2)));
	AddStat(Box, LOCTEXT("Range", "Full damage up to"), Weapon->FalloffStartDistance / 5000.f,
		FText::Format(LOCTEXT("RangeValue", "{0} m, then down to {1}% at {2} m"), Number(Weapon->FalloffStartDistance / 100.f),
			Number(Weapon->MinDamageMultiplier * 100.f), Number(Weapon->FalloffEndDistance / 100.f)));

	Box->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 4.f)[ CSUI::MakeSectionLabel(LOCTEXT("ToKill", "TO KILL (100 HEALTH, NO ARMOR, CLOSE RANGE)")) ];
	AddFact(Box, LOCTEXT("BodyShots", "Body shots"), FText::Format(LOCTEXT("ShotsTtk", "{0}  ({1} ms)"), Number(BodyShots), Number(TtkMs)));
	AddFact(Box, LOCTEXT("HeadShots", "Headshots"), FText::Format(LOCTEXT("HeadValue", "{0}  (x{1} damage)"), Number(HeadShots), Number(Weapon->HeadshotMultiplier, 1)));

	Box->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 4.f)[ CSUI::MakeSectionLabel(LOCTEXT("Handling", "HANDLING")) ];
	AddFact(Box, LOCTEXT("Mag", "Magazine / spare"), FText::Format(LOCTEXT("MagValue", "{0} / {1}"), Number(Weapon->MagazineSize), Number(Weapon->ReserveAmmo)));
	AddFact(Box, LOCTEXT("Reload", "Reload"), FText::Format(LOCTEXT("Seconds", "{0} s"), Number(Weapon->ReloadSeconds, 1)));
	AddFact(Box, LOCTEXT("Aim", "Time to aim"), FText::Format(LOCTEXT("Seconds2", "{0} s"), Number(Weapon->AimSeconds, 2)));
	AddFact(Box, LOCTEXT("Moving", "Extra spread when moving / jumping"),
		FText::Format(LOCTEXT("MoveValue", "{0} / {1} deg"), Number(Weapon->MoveSpreadDegrees, 1), Number(Weapon->JumpSpreadDegrees, 1)));
	Details->SetContent(Box);
}

// ---------------------------------------------------------------------------
// Catalog
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SCSArsenalPanel::MakeCatalog()
{
	const UCSShopSettings* Shop = UCSShopSettings::Get();
	const UCSItemSettings* Items = UCSItemSettings::Get();
	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);

	const ECSShopCategory Order[] = { ECSShopCategory::Pistols, ECSShopCategory::SMGs, ECSShopCategory::Rifles, ECSShopCategory::Heavy,
		ECSShopCategory::Snipers, ECSShopCategory::Grenades, ECSShopCategory::Gear, ECSShopCategory::Ammo };
	for (const ECSShopCategory Category : Order)
	{
		bool bHeader = false;
		for (const FCSShopEntry& Entry : Shop->Entries)
		{
			if (Entry.Category != Category)
			{
				continue;
			}
			const UCSItemDefinition* Item = Items->GetItem(Items->FindItemIndex(Entry.ItemId));
			if (!Item)
			{
				continue;
			}
			if (!bHeader)
			{
				bHeader = true;
				Body->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)[ CSUI::MakeSectionLabel(UCSShopSettings::CategoryName(Category)) ];
			}
			FText Facts = FText::FromString(Entry.Blurb);
			if (const UCSWeaponDefinition* Weapon = Item->IsWeapon() ? Item->Weapon.LoadSynchronous() : nullptr; Weapon && Weapon->Kind != ECSWeaponKind::Knife)
			{
				Facts = FText::Format(LOCTEXT("CatalogFacts", "{0} damage   {1} rpm   {2} rounds   {3}"),
					Number(Weapon->BaseDamage * FMath::Max(1, Weapon->PelletsPerShot)), Number(Weapon->RoundsPerMinute),
					Number(Weapon->MagazineSize), FText::FromString(Entry.Blurb));
			}
			Body->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
			[
				SNew(SBorder).BorderImage(CSUI::WhiteBrush()).BorderBackgroundColor(CSUI::PanelRaised).Padding(FMargin(12.f, 8.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 12.f, 0.f)[ MakeIcon(Item, 28.f) ]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Text(Item->DisplayName).Font(CSUI::Font(15, true)).ColorAndOpacity(CSUI::Text) ]
						+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Text(Facts).Font(CSUI::Font(12)).ColorAndOpacity(CSUI::TextDim).AutoWrapText(true) ]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.f, 0.f, 0.f, 0.f)
					[
						SNew(STextBlock).Text(CSUI::MoneyText(Entry.Price)).Font(CSUI::Font(18, true)).ColorAndOpacity(CSUI::Money)
					]
				]
			];
		}
	}

	// The economy around the shop.
	TSharedRef<SVerticalBox> Economy = SNew(SVerticalBox);
	AddFactStacked(Economy, LOCTEXT("Machine", "AMMO MACHINE - TOPS UP BOTH GUNS"), CSUI::MoneyText(Shop->AmmoMachinePrice));
	for (const ECSGameModeType Mode : { ECSGameModeType::Deathmatch, ECSGameModeType::TeamDeathmatch, ECSGameModeType::Competitive })
	{
		const FCSModeRules& Rules = UCSModeSettings::Rules(Mode);
		AddFactStacked(Economy, UCSModeSettings::ModeName(Mode).ToUpper(),
			FText::Format(LOCTEXT("ModeMoney", "start {0}, kill {1}{2}"), CSUI::MoneyText(Rules.StartMoney), CSUI::MoneyText(Rules.KillReward),
				Rules.HeadshotBonus > 0 ? FText::Format(LOCTEXT("HsBonus", ", headshot +{0}"), CSUI::MoneyText(Rules.HeadshotBonus)) : FText::GetEmpty()));
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			CSUI::MakeHeader(LOCTEXT("CatalogTitle", "CATALOG"),
				LOCTEXT("CatalogBody", "Everything the shop sells during a match (B while protected after a spawn, or in the buy time of 5 vs 5), and what the match pays."))
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.64f)
			[
				SNew(SScrollBox) + SScrollBox::Slot().Padding(FMargin(0.f, 0.f, 12.f, 0.f))[ Body ]
			]
			+ SHorizontalBox::Slot().FillWidth(0.36f).Padding(16.f, 8.f, 0.f, 0.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ CSUI::MakeSectionLabel(LOCTEXT("EconomyLabel", "ECONOMY")) ]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBorder).BorderImage(CSUI::WhiteBrush()).BorderBackgroundColor(CSUI::PanelRaised).Padding(FMargin(14.f, 10.f))
					[
						Economy
					]
				]
			]
		];
}

#undef LOCTEXT_NAMESPACE
