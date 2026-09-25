// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "UI/SCSShopPanel.h"

#include "Characters/CSCharacter.h"
#include "Combat/CSMatchDirector.h"
#include "Core/CSAuthority.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Inventory/CSPlayerInventory.h"
#include "Items/CSItemDefinition.h"
#include "Items/CSItemSettings.h"
#include "Styling/SlateBrush.h"
#include "UI/CSUIStyle.h"
#include "Weapons/CSWeaponDefinition.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CSShop"

namespace
{
	const UCSItemDefinition* ShopItem(int32 ShopIndex)
	{
		const FCSShopEntry* Entry = UCSShopSettings::Get()->GetEntry(ShopIndex);
		const UCSItemSettings* Items = UCSItemSettings::Get();
		return Entry ? Items->GetItem(Items->FindItemIndex(Entry->ItemId)) : nullptr;
	}

	FText ResultText(ECSBuyResult Result)
	{
		switch (Result)
		{
		case ECSBuyResult::ShopClosed:		return LOCTEXT("Closed", "The shop is closed");
		case ECSBuyResult::NotEnoughMoney:	return LOCTEXT("Money", "Not enough money");
		case ECSBuyResult::InventoryFull:	return LOCTEXT("Full", "Inventory full");
		case ECSBuyResult::AlreadyOwned:	return LOCTEXT("Owned", "Already owned");
		case ECSBuyResult::Dead:			return LOCTEXT("Dead", "You are dead");
		default:							return LOCTEXT("Invalid", "Not available");
		}
	}
}

void SCSShopPanel::Construct(const FArguments& InArgs)
{
	WorldContext = InArgs._WorldContext;
	OnClose = InArgs._OnClose;
	CloseKey = InArgs._CloseKey;

	TSharedRef<SVerticalBox> Rail = SNew(SVerticalBox);
	Rail->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)[ MakeCategoryButton(INDEX_NONE, LOCTEXT("All", "ALL")) ];
	// Only categories that have something in them (v2.0 has no ammo section).
	for (int32 Cat = 0; Cat <= static_cast<int32>(ECSShopCategory::Grenades); ++Cat)
	{
		const bool bUsed = UCSShopSettings::Get()->Entries.ContainsByPredicate([Cat](const FCSShopEntry& E) { return static_cast<int32>(E.Category) == Cat; });
		if (!bUsed)
		{
			continue;
		}
		Rail->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
		[
			MakeCategoryButton(Cat, UCSShopSettings::CategoryName(static_cast<ECSShopCategory>(Cat)))
		];
	}

	ChildSlot
	[
		SNew(SVerticalBox)

		// --- Header: title, money, closing timer ---
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(6.f).HeightOverride(44.f)
				[
					SNew(SImage).Image(CSUI::WhiteBrush()).ColorAndOpacity(CSUI::Accent)
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(16.f, 0.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(LOCTEXT("Title", "BUY MENU")).Font(CSUI::Font(30, true)).ColorAndOpacity(CSUI::Text)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Font(CSUI::Font(13)).ColorAndOpacity(CSUI::TextDim)
					.Text_Lambda([this]()
					{
						const float Left = GetTimeLeft();
						return Left > 0.f
							? FText::Format(LOCTEXT("ClosesIn", "Closes in {0} s  -  moving, jumping or crouching closes it now"), FText::AsNumber(FMath::CeilToInt(Left)))
							: LOCTEXT("ClosedNow", "Closed until your next spawn / round");
					})
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
				[
					SNew(STextBlock).Text(LOCTEXT("Balance", "BALANCE")).Font(CSUI::Font(11, true)).ColorAndOpacity(CSUI::TextDim)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
				[
					SNew(STextBlock).Font(CSUI::Font(34, true)).ColorAndOpacity(CSUI::Money)
					.Text_Lambda([this]() { return CSUI::MoneyText(GetMoney()); })
				]
			]
		]

		// Time bar: drains as the shop window runs out.
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 18.f)
		[
			SNew(SBox).HeightOverride(3.f)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()[ SNew(SImage).Image(CSUI::WhiteBrush()).ColorAndOpacity(CSUI::Stroke) ]
				+ SOverlay::Slot().HAlign(HAlign_Left)
				[
					SNew(SBox).WidthOverride_Lambda([this]()
					{
						const float Left = GetTimeLeft();
						return 1168.f * FMath::Clamp(Left / 15.f, 0.f, 1.f);
					})
					[
						SNew(SImage).Image(CSUI::WhiteBrush())
						.ColorAndOpacity_Lambda([this]() { return GetTimeLeft() < 3.f ? CSUI::Danger : CSUI::Accent; })
					]
				]
			]
		]

		// --- Body: rail | list | details ---
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(170.f)[ Rail ]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(18.f, 0.f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()[ SAssignNew(ListBox, SVerticalBox) ]
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBorder).BorderImage(CSUI::WhiteBrush()).BorderBackgroundColor(CSUI::PanelRaised).Padding(22.f)
				[
					SAssignNew(DetailsBox, SBox).WidthOverride(300.f)
				]
			]
		]

		// --- Footer ---
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Font(CSUI::Font(15, true))
				.Text_Lambda([this]() { return FPlatformTime::Seconds() < FeedbackUntil ? Feedback : FText::GetEmpty(); })
				.ColorAndOpacity_Lambda([this]() { return FSlateColor(FeedbackColor); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).Font(CSUI::Font(12)).ColorAndOpacity(CSUI::TextDim)
				.Text(FText::Format(LOCTEXT("Footer", "[1-9] quick buy     [{0}] / [Esc] close"), CloseKey.GetDisplayName()))
			]
		]
	];

	RebuildList();
	RebuildDetails();
	// Built before the panel is on screen: rebuild once it is (see Refresh).
	LastSignature = 0;
}

void SCSShopPanel::Refresh()
{
	// Rows built while the panel was off screen keep a zero layout until
	// rebuilt; the next Tick does it now that the panel is shown.
	LastSignature = 0;
	ShownDetails = -2;
	FeedbackUntil = 0.0;
}

ACSCharacter* SCSShopPanel::GetLocalCharacter() const
{
	const UWorld* World = WorldContext.IsValid() ? WorldContext->GetWorld() : nullptr;
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	return PC ? Cast<ACSCharacter>(PC->GetPawn()) : nullptr;
}

int32 SCSShopPanel::GetMoney() const
{
	const ACSCharacter* Pawn = GetLocalCharacter();
	const ACSMatchDirector* Director = Pawn ? ACSMatchDirector::Get(Pawn) : nullptr;
	return Director ? Director->GetMoney(Pawn->GetOwningPlayerId()) : 0;
}

float SCSShopPanel::GetTimeLeft() const
{
	const ACSCharacter* Pawn = GetLocalCharacter();
	const ACSMatchDirector* Director = Pawn ? ACSMatchDirector::Get(Pawn) : nullptr;
	return Director ? Director->GetBuyTimeRemaining(Pawn->GetOwningPlayerId()) : 0.f;
}

ECSBuyResult SCSShopPanel::Predict(int32 ShopIndex) const
{
	const FCSShopEntry* Entry = UCSShopSettings::Get()->GetEntry(ShopIndex);
	const UCSItemDefinition* Item = ShopItem(ShopIndex);
	const ACSCharacter* Pawn = GetLocalCharacter();
	if (!Entry || !Item || !Pawn)
	{
		return ECSBuyResult::Invalid;
	}
	const int32 PlayerId = Pawn->GetOwningPlayerId();
	const ACSMatchDirector* Director = ACSMatchDirector::Get(Pawn);
	if (!Director || !Director->IsPlayerAlive(PlayerId))
	{
		return ECSBuyResult::Dead;
	}
	if (!Director->CanBuy(PlayerId))
	{
		return ECSBuyResult::ShopClosed;
	}
	const ACSPlayerInventory* Inventory = ACSPlayerInventory::Find(Pawn, PlayerId);
	const int32 ItemIndex = UCSItemSettings::Get()->FindItemIndex(Entry->ItemId);
	const int32 Slot = ACSPlayerInventory::SlotForItem(Item);
	FCSInventorySlot Held;
	const bool bHeld = Inventory && Inventory->GetSlot(Slot, Held) && !Held.IsEmpty();
	if (CSLoadout::IsDroppable(Slot) && bHeld && Held.ItemIndex == ItemIndex)
	{
		return ECSBuyResult::AlreadyOwned;
	}
	if (Item->ItemType == ECSItemType::Armor && Director->GetArmor(PlayerId) >= 99.f)
	{
		return ECSBuyResult::AlreadyOwned;
	}
	if (Director->GetMoney(PlayerId) < Entry->Price)
	{
		return ECSBuyResult::NotEnoughMoney;
	}
	if (CSLoadout::IsGrenadeSlot(Slot) && bHeld && Held.Count >= Item->GetMaxStack())
	{
		return ECSBuyResult::InventoryFull;
	}
	return ECSBuyResult::Ok;
}

bool SCSShopPanel::Buy(int32 ShopIndex)
{
	const ECSBuyResult Hint = Predict(ShopIndex);
	const FCSShopEntry* Entry = UCSShopSettings::Get()->GetEntry(ShopIndex);
	const UCSItemDefinition* Item = ShopItem(ShopIndex);
	FeedbackUntil = FPlatformTime::Seconds() + 2.5;
	if (Hint != ECSBuyResult::Ok || !Entry || !Item)
	{
		Feedback = ResultText(Hint);
		FeedbackColor = CSUI::Danger;
		return false;
	}
	if (ACSCharacter* Pawn = GetLocalCharacter())
	{
		Pawn->RequestBuy(ShopIndex);
	}
	Feedback = FText::Format(LOCTEXT("Bought", "Bought {0}  -{1}"), Item->DisplayName, CSUI::MoneyText(Entry->Price));
	FeedbackColor = CSUI::Money;
	return true;
}

void SCSShopPanel::SetCategory(int32 NewCategory)
{
	Category = NewCategory;
	RebuildList();
}

uint32 SCSShopPanel::ComputeSignature() const
{
	uint32 Hash = GetTypeHash(GetMoney()) ^ (GetTimeLeft() > 0.f ? 0x9e3779b9u : 0u);
	const ACSCharacter* Pawn = GetLocalCharacter();
	if (const ACSPlayerInventory* Inventory = Pawn ? ACSPlayerInventory::Find(Pawn, Pawn->GetOwningPlayerId()) : nullptr)
	{
		for (const FCSInventorySlot& Slot : Inventory->GetSlots())
		{
			Hash = HashCombine(Hash, HashCombine(GetTypeHash(Slot.ItemIndex), GetTypeHash(Slot.Count)));
		}
	}
	if (const ACSMatchDirector* Director = Pawn ? ACSMatchDirector::Get(Pawn) : nullptr)
	{
		Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt(Director->GetArmor(Pawn->GetOwningPlayerId()))));
	}
	return Hash;
}

void SCSShopPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	const uint32 Signature = ComputeSignature();
	if (Signature != LastSignature)
	{
		RebuildList();
	}
	if (Hovered != ShownDetails)
	{
		RebuildDetails();
	}
}

FReply SCSShopPanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape || Key == CloseKey)
	{
		OnClose.ExecuteIfBound();
		return FReply::Handled();
	}

	static const FKey Digits[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
		EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
	for (int32 i = 0; i < UE_ARRAY_COUNT(Digits); ++i)
	{
		if (Key == Digits[i] && Visible.IsValidIndex(i))
		{
			Buy(Visible[i]);
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

TSharedRef<SWidget> SCSShopPanel::MakeCategoryButton(int32 Cat, const FText& Label)
{
	return SNew(SButton)
		.IsFocusable(false)
		.ButtonStyle(&CSUI::ButtonStyle(CSUI::EButtonKind::Nav))
		.OnClicked_Lambda([this, Cat]() { SetCategory(Cat); return FReply::Handled(); })
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(3.f).HeightOverride(38.f)
				[
					SNew(SImage).Image(CSUI::WhiteBrush())
					.ColorAndOpacity_Lambda([this, Cat]() { return Category == Cat ? CSUI::Accent : FLinearColor::Transparent; })
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(14.f, 0.f)
			[
				SNew(STextBlock).Text(Label).Font(CSUI::Font(15, true))
				.ColorAndOpacity_Lambda([this, Cat]() { return FSlateColor(Category == Cat ? CSUI::Text : CSUI::TextDim); })
			]
		];
}

TSharedRef<SWidget> SCSShopPanel::MakeIcon(const UCSItemDefinition* Item, float Size, TArray<TSharedPtr<FSlateBrush>>& Sink)
{
	if (Item)
	{
		if (UTexture2D* Texture = Item->Icon.LoadSynchronous())
		{
			TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateBrush>();
			Brush->SetResourceObject(Texture);
			const float Aspect = Texture->GetSizeY() > 0 ? static_cast<float>(Texture->GetSizeX()) / Texture->GetSizeY() : 1.f;
			Brush->ImageSize = FVector2D(Size * FMath::Max(1.f, Aspect), Size);
			Sink.Add(Brush);
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

TSharedRef<SWidget> SCSShopPanel::MakeRow(int32 ShopIndex, int32 Hotkey)
{
	const FCSShopEntry* Entry = UCSShopSettings::Get()->GetEntry(ShopIndex);
	const UCSItemDefinition* Item = ShopItem(ShopIndex);
	const ECSBuyResult State = Predict(ShopIndex);
	const bool bOk = State == ECSBuyResult::Ok;
	const bool bOwned = State == ECSBuyResult::AlreadyOwned;

	const FText Status = bOwned ? LOCTEXT("OwnedTag", "OWNED")
		: (State == ECSBuyResult::NotEnoughMoney ? CSUI::MoneyText(Entry->Price) : CSUI::MoneyText(Entry->Price));
	const FLinearColor PriceColor = bOwned ? CSUI::TextDim : (State == ECSBuyResult::NotEnoughMoney ? CSUI::Danger : CSUI::Money);

	return SNew(SBox).HeightOverride(66.f).Padding(FMargin(0.f, 0.f, 0.f, 4.f))
		[
			SNew(SButton)
			.IsFocusable(false)
			.ButtonStyle(&CSUI::ButtonStyle(CSUI::EButtonKind::Normal))
			.OnClicked_Lambda([this, ShopIndex]() { Buy(ShopIndex); return FReply::Handled(); })
			.OnHovered_Lambda([this, ShopIndex]() { Hovered = ShopIndex; })
			[
				SNew(SHorizontalBox)
				// Hotkey badge.
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.f, 0.f, 8.f, 0.f)
				[
					SNew(SBox).WidthOverride(26.f).HeightOverride(26.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Font(CSUI::Font(13, true)).ColorAndOpacity(CSUI::TextDim)
						.Text(Hotkey > 0 ? FText::AsNumber(Hotkey) : FText::GetEmpty())
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 14.f, 0.f)
				[
					MakeIcon(Item, 40.f, ListBrushes)
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Text(Item ? Item->DisplayName : FText::FromName(Entry->ItemId))
						.Font(CSUI::Font(17, true)).ColorAndOpacity(bOk || bOwned ? CSUI::Text : CSUI::TextDim)
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Text(FText::FromString(Entry->Blurb)).Font(CSUI::Font(11)).ColorAndOpacity(CSUI::TextDim)
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.f, 0.f, 18.f, 0.f)
				[
					SNew(STextBlock).Text(Status).Font(CSUI::Font(18, true)).ColorAndOpacity(PriceColor)
				]
			]
		];
}

void SCSShopPanel::RebuildList()
{
	LastSignature = ComputeSignature();
	if (!ListBox.IsValid())
	{
		return;
	}
	ListBox->ClearChildren();
	Visible.Reset();
	ListBrushes.Reset();

	const TArray<FCSShopEntry>& Entries = UCSShopSettings::Get()->Entries;
	int32 LastCategory = -1;
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		const int32 EntryCategory = static_cast<int32>(Entries[i].Category);
		if (Category != INDEX_NONE && EntryCategory != Category)
		{
			continue;
		}
		if (Category == INDEX_NONE && EntryCategory != LastCategory)
		{
			// Section headers in the full list.
			ListBox->AddSlot().AutoHeight().Padding(4.f, Visible.Num() == 0 ? 0.f : 12.f, 0.f, 6.f)
			[
				SNew(STextBlock).Text(UCSShopSettings::CategoryName(Entries[i].Category))
				.Font(CSUI::Font(11, true)).ColorAndOpacity(CSUI::Accent)
			];
			LastCategory = EntryCategory;
		}
		Visible.Add(i);
		ListBox->AddSlot().AutoHeight()[ MakeRow(i, Visible.Num() <= 9 ? Visible.Num() : 0) ];
	}
	// The list is rebuilt from Tick, which runs while this panel paints - after
	// the frame's layout prepass. Without measuring the new rows now they are
	// arranged with zero size for one frame and drawn on top of each other.
	ListBox->SlatePrepass();
}

void SCSShopPanel::RebuildDetails()
{
	ShownDetails = Hovered;
	if (!DetailsBox.IsValid())
	{
		return;
	}

	TArray<TSharedPtr<FSlateBrush>> OldBrushes = MoveTemp(DetailBrushes);
	const FCSShopEntry* Entry = UCSShopSettings::Get()->GetEntry(Hovered);
	const UCSItemDefinition* Item = ShopItem(Hovered);
	if (!Entry || !Item)
	{
		DetailsBox->SetContent(
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(LOCTEXT("PickOne", "Point at an item to see its stats.\nClick or press its number to buy."))
				.Font(CSUI::Font(14)).ColorAndOpacity(CSUI::TextDim).AutoWrapText(true)
			]);
		DetailsBox->SlatePrepass(); // same reason as in RebuildList
		return;
	}

	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 14.f)
		[
			MakeIcon(Item, 72.f, DetailBrushes)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock).Text(Item->DisplayName).Font(CSUI::Font(24, true)).ColorAndOpacity(CSUI::Text)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 12.f)
		[
			SNew(STextBlock).Text(UCSShopSettings::CategoryName(Entry->Category)).Font(CSUI::Font(11, true)).ColorAndOpacity(CSUI::Accent)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 16.f)
		[
			SNew(STextBlock).Text(Item->Description.IsEmpty() ? FText::FromString(Entry->Blurb) : Item->Description)
			.Font(CSUI::Font(13)).ColorAndOpacity(CSUI::TextDim).AutoWrapText(true)
		];

	auto Stat = [&Box](const FText& Label, float Fraction, const FText& Value)
	{
		Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)[ SNew(STextBlock).Text(Label).Font(CSUI::Font(12)).ColorAndOpacity(CSUI::TextDim) ]
			+ SHorizontalBox::Slot().AutoWidth()[ SNew(STextBlock).Text(Value).Font(CSUI::Font(12, true)).ColorAndOpacity(CSUI::Text) ]
		];
		Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)[ CSUI::MakeStatBar(Fraction, CSUI::Accent) ];
	};

	if (const UCSWeaponDefinition* Weapon = Item->IsWeapon() ? Item->Weapon.LoadSynchronous() : nullptr)
	{
		const float Damage = Weapon->BaseDamage * FMath::Max(1, Weapon->PelletsPerShot);
		Stat(LOCTEXT("Damage", "Damage"), Damage / 120.f, FText::AsNumber(FMath::RoundToInt(Damage)));
		Stat(LOCTEXT("Rate", "Fire rate"), Weapon->RoundsPerMinute / 900.f,
			FText::Format(LOCTEXT("Rpm", "{0} rpm"), FText::AsNumber(FMath::RoundToInt(Weapon->RoundsPerMinute))));
		Stat(LOCTEXT("Accuracy", "Accuracy"), 1.f - Weapon->HipSpreadDegrees / 6.f,
			FText::AsPercent(FMath::Clamp(1.f - Weapon->HipSpreadDegrees / 6.f, 0.f, 1.f)));
		Stat(LOCTEXT("Mag", "Magazine"), Weapon->MagazineSize / 40.f, FText::AsNumber(Weapon->MagazineSize));
		Stat(LOCTEXT("Range", "Range"), Weapon->FalloffEndDistance / 10000.f,
			FText::Format(LOCTEXT("Metres", "{0} m"), FText::AsNumber(FMath::RoundToInt(Weapon->FalloffEndDistance / 100.f))));
		Box->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
		[
			SNew(STextBlock).Font(CSUI::Font(12)).ColorAndOpacity(CSUI::TextDim)
			.Text(FText::Format(LOCTEXT("Includes", "Includes {0} spare magazines"), FText::AsNumber(Entry->Bundle)))
		];
	}
	else if (Item->ArmorAmount > 0.f)
	{
		Stat(LOCTEXT("Armor", "Armor"), Item->ArmorAmount / 100.f, FText::AsNumber(FMath::RoundToInt(Item->ArmorAmount)));
	}
	else if (Item->HealAmount > 0.f)
	{
		Stat(LOCTEXT("Heal", "Heals"), Item->HealAmount / 100.f, FText::AsNumber(FMath::RoundToInt(Item->HealAmount)));
	}

	Box->AddSlot().FillHeight(1.f)[ SNullWidget::NullWidget ];
	Box->AddSlot().AutoHeight().HAlign(HAlign_Right)
	[
		SNew(STextBlock).Text(CSUI::MoneyText(Entry->Price)).Font(CSUI::Font(28, true)).ColorAndOpacity(CSUI::Money)
	];

	DetailsBox->SetContent(Box);
	DetailsBox->SlatePrepass(); // same reason as in RebuildList
}

#undef LOCTEXT_NAMESPACE
