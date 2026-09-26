// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// v1.0 weapon presentation self-test (-cstestweapons, offline). Gives the
// player every weapon in the item registry and, for each, through the real
// slot keys and aim state:
//   - screenshots at the hip, aimed, and from the side (third person),
//     Saved/CSTest/wpn_<weapon>_{hip,ads,tp}.png
//   - measures the left-hand IK: gap between the palm (HandGrip_L) and the support
//     point (first person), and the aim: how far the sight point sits from
//     the screen centre when fully aimed.

#include "Player/CSPlayerController.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/CSCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Combat/CSMatchDirector.h"
#include "Components/StaticMeshComponent.h"
#include "Core/CSAuthority.h"
#include "Core/CSLog.h"
#include "Inventory/CSPlayerInventory.h"
#include "Items/CSItemDefinition.h"
#include "Items/CSItemSettings.h"
#include "TimerManager.h"
#include "Weapons/CSWeaponComponent.h"
#include "Weapons/CSWeaponDefinition.h"
#include "Weapons/CSWeaponPresentation.h"

namespace
{
	FString WeaponLabel(const ACSCharacter* Pawn)
	{
		const UCSWeaponDefinition* W = Pawn ? Pawn->GetDisplayedWeapon() : nullptr;
		return W ? W->GetName().Replace(TEXT("DA_Weapon_"), TEXT("")) : TEXT("none");
	}
}

void ACSPlayerController::CSTestWeapons()
{
	CS_SELF_TEST_ONLY();
	ACSCharacter* Self = Cast<ACSCharacter>(GetPawn());
	if (!Self || !Self->IsAliveAuthoritative() || !UCSAuthority::IsGameAuthority(this))
	{
		GetWorldTimerManager().SetTimer(TestWeaponsTimer, this, &ACSPlayerController::CSTestWeapons, 1.f, false);
		return;
	}

	// v2.0: one gun per slot, so each weapon item is handed over in turn
	// (authority, offline) - the primary is swapped for the next one.
	const UCSItemSettings* Items = GetDefault<UCSItemSettings>();
	WeaponTestSlots.Reset();
	// -testweapon=SMG checks only the weapons whose data asset name contains it.
	FString Only;
	FParse::Value(FCommandLine::Get(), TEXT("testweapon="), Only);
	for (int32 i = 0; Items->IsValidIndex(i); ++i)
	{
		const UCSItemDefinition* Item = Items->GetItem(i);
		const bool bWanted = Only.IsEmpty() || (Item && Item->Weapon.GetAssetName().Contains(Only));
		if (Item && bWanted && ACSPlayerInventory::SlotForItem(Item) != INDEX_NONE)
		{
			WeaponTestSlots.Add(i); // item index, not a slot
		}
	}
	UE_LOG(LogCS, Log, TEXT("WEAPON TEST: %d weapons to check."), WeaponTestSlots.Num());
	WeaponTestIndex = -1;
	WeaponTestNext();
}

void ACSPlayerController::WeaponTestNext()
{
	ACSCharacter* Self = Cast<ACSCharacter>(GetPawn());
	if (!Self || ++WeaponTestIndex >= WeaponTestSlots.Num())
	{
		UE_LOG(LogCS, Log, TEXT("WEAPON TEST: done."));
		return;
	}
	const int32 ItemIndex = WeaponTestSlots[WeaponTestIndex];
	if (ACSMatchDirector* Director = ACSMatchDirector::Get(this))
	{
		Director->GiveItem(Self->GetOwningPlayerId(), ItemIndex, /*bEquip*/ true);
	}
	Self->RequestSlot(ACSPlayerInventory::SlotForItem(UCSItemSettings::Get()->GetItem(ItemIndex)));

	// Equip clip plays ~0.6 s; the view settles after it.
	GetWorldTimerManager().SetTimer(TestWeaponsTimer, [this]()
	{
		ACSCharacter* S1 = Cast<ACSCharacter>(GetPawn());
		if (!S1)
		{
			return;
		}
		const FString Name = WeaponLabel(S1);
		TestScreenshot(FString::Printf(TEXT("wpn_%s_hip"), *Name));

		// Left-hand gap, first person.
		float Gap = -1.f;
		const FCSWeaponModel* Model = S1->GetDisplayedModel();
		const UStaticMeshComponent* ModelComp = S1->GetFirstPersonWeaponModel();
		if (Model && ModelComp && S1->GetFirstPersonMesh())
		{
			const FVector Support = ModelComp->GetComponentTransform().TransformPosition(Model->Support);
			Gap = FVector::Dist(Support, S1->GetFirstPersonMesh()->GetSocketLocation(TEXT("HandGrip_L")));
		}
		WeaponTestGap = Gap;

		if (UCSWeaponComponent* W = S1->GetWeaponComponent())
		{
			W->SetAiming(true);
		}
		GetWorldTimerManager().SetTimer(TestWeaponsTimer, [this, Name]()
		{
			ACSCharacter* S2 = Cast<ACSCharacter>(GetPawn());
			if (!S2)
			{
				return;
			}
			TestScreenshot(FString::Printf(TEXT("wpn_%s_ads"), *Name));

			// Sight offset from the view axis, cm, at full aim.
			float SightOff = -1.f;
			const FCSWeaponModel* M = S2->GetDisplayedModel();
			const UStaticMeshComponent* MC = S2->GetFirstPersonWeaponModel();
			if (M && MC && S2->GetFirstPersonCamera() && !S2->IsScopedView())
			{
				const FVector World = MC->GetComponentTransform().TransformPosition(M->Sight);
				const FVector InCam = S2->GetFirstPersonCamera()->GetComponentTransform().InverseTransformPosition(World);
				SightOff = FVector2D(InCam.Y, InCam.Z).Size();
			}
			const bool bScope = M && M->bScope;
			// One-handed things (knife, grenades) neither aim nor put the left hand on them.
			const bool bOneHanded = M && M->bOneHanded;
			const bool bHandOk = bOneHanded || (WeaponTestGap >= 0.f && WeaponTestGap < 3.f);
			const bool bAimOk = bOneHanded || (bScope ? S2->IsScopedView() : (SightOff >= 0.f && SightOff < 1.5f));
			UE_LOG(LogCS, Log, TEXT("WEAPON TEST: view pitch %.1f"), GetControlRotation().Pitch);
			UE_LOG(LogCS, Log, TEXT("WEAPON TEST RESULT: %s -> left hand gap %.1f cm, %s, aim alpha %.2f -> %s"),
				*Name, WeaponTestGap,
				bScope ? (S2->IsScopedView() ? TEXT("scope view up") : TEXT("scope view NOT up"))
					: *FString::Printf(TEXT("sight %.2f cm off centre"), SightOff),
				S2->GetAimAlpha(), (bHandOk && bAimOk) ? TEXT("WEAPON OK") : TEXT("WEAPON BROKEN"));

			if (UCSWeaponComponent* W = S2->GetWeaponComponent())
			{
				W->SetAiming(false);
			}

			// Side view: show the body to its owner, look at it from the right.
			GetWorldTimerManager().SetTimer(TestWeaponsTimer, [this, Name]()
			{
				ACSCharacter* S3 = Cast<ACSCharacter>(GetPawn());
				if (!S3)
				{
					return;
				}
				S3->GetMesh()->SetOwnerNoSee(false);
				const FVector Eye = S3->GetActorLocation() + S3->GetActorRightVector() * 170.f
					+ S3->GetActorForwardVector() * 70.f + FVector(0.f, 0.f, 30.f);
				const FVector LookAt = S3->GetActorLocation() + FVector(0.f, 0.f, 25.f);
				ACameraActor* Cam = GetWorld()->SpawnActor<ACameraActor>(Eye, (LookAt - Eye).Rotation());
				if (Cam)
				{
					Cam->GetCameraComponent()->SetFieldOfView(60.f);
					SetViewTarget(Cam);
				}
				GetWorldTimerManager().SetTimer(TestWeaponsTimer, [this, Name, Cam]()
				{
					TestScreenshot(FString::Printf(TEXT("wpn_%s_tp"), *Name));
					// Close-up of both hands on the weapon from the left (support-hand
					// side): shows a hand sinking into the stock or floating off it.
					// The camera moves a frame after the side shot so that shot keeps its view.
					FTimerHandle MoveCam;
					GetWorldTimerManager().SetTimer(MoveCam, [this, Cam]()
					{
						ACSCharacter* S5 = Cast<ACSCharacter>(GetPawn());
						if (!S5 || !Cam)
						{
							return;
						}
						const FVector Mid = (S5->GetMesh()->GetBoneLocation(TEXT("hand_l")) + S5->GetMesh()->GetBoneLocation(TEXT("hand_r"))) * 0.5f;
						const FVector Eye = Mid - S5->GetActorRightVector() * 75.f + S5->GetActorForwardVector() * 25.f + FVector(0.f, 0.f, 12.f);
						Cam->SetActorLocationAndRotation(Eye, (Mid - Eye).Rotation());
						Cam->GetCameraComponent()->SetFieldOfView(50.f);
					}, 0.1f, false);
					FTimerHandle CloseShot;
					GetWorldTimerManager().SetTimer(CloseShot, [this, Name]()
					{
						TestScreenshot(FString::Printf(TEXT("wpn_%s_hands"), *Name));
					}, 0.3f, false);
					GetWorldTimerManager().SetTimer(TestWeaponsTimer, [this, Cam]()
					{
						if (ACSCharacter* S4 = Cast<ACSCharacter>(GetPawn()))
						{
							S4->GetMesh()->SetOwnerNoSee(true);
							SetViewTarget(S4);
						}
						if (Cam)
						{
							Cam->Destroy();
						}
						WeaponTestNext();
					}, 0.5f, false);
				}, 0.5f, false);
			}, 0.5f, false);
		}, 0.9f, false);
	}, 1.8f, false);
}
