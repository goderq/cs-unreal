// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Phase 3 (AUDIT C10) effects self-test, enabled with -cstestfx
// (Scripts/run_tests.ps1 suite "fx"):
//
//   1. a gallery: every Niagara system starts in front of the camera and a
//      screenshot is taken at its busiest moment (Saved/CSTest/fx_*.png);
//   2. the transient-actor pool: 60 flashes and tracers at 30 per second must
//      come from a handful of actors;
//   3. the Niagara component pool: a second volley of impacts must reuse the
//      components of the first.

#include "Player/CSPlayerController.h"

#include "Characters/CSCharacter.h"
#include "Core/CSLog.h"
#include "FX/CSEffects.h"
#include "FX/CSTransientFX.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "TimerManager.h"

namespace
{
	struct FGalleryItem
	{
		const TCHAR* Name;
		float ShotDelay;
		enum class EPlace : uint8 { Floor, FarFloor, NearRight } Place;
	};

	const FGalleryItem GGallery[] = {
		{ TEXT("NS_Impact_Stone"), 0.12f, FGalleryItem::EPlace::Floor },
		{ TEXT("NS_Impact_Metal"), 0.07f, FGalleryItem::EPlace::Floor },
		{ TEXT("NS_Impact_Wood"), 0.1f, FGalleryItem::EPlace::Floor },
		{ TEXT("NS_Impact_Dirt"), 0.12f, FGalleryItem::EPlace::Floor },
		{ TEXT("NS_Impact_Blood"), 0.1f, FGalleryItem::EPlace::Floor },
		{ TEXT("NS_MuzzleSmoke"), 0.35f, FGalleryItem::EPlace::NearRight },
		{ TEXT("NS_ShellEject"), 0.15f, FGalleryItem::EPlace::NearRight },
		{ TEXT("NS_Explosion"), 0.2f, FGalleryItem::EPlace::FarFloor },
		{ TEXT("NS_FlashbangBurst"), 0.15f, FGalleryItem::EPlace::FarFloor },
	};

	TSoftObjectPtr<UNiagaraSystem> GallerySystem(const TCHAR* Name)
	{
		return TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(FString::Printf(TEXT("/Game/FX/Niagara/%s.%s"), Name, Name)));
	}
}

void ACSPlayerController::CSTestFX()
{
	CS_SELF_TEST_ONLY();
#if !UE_BUILD_SHIPPING
	ACSCharacter* Mine = Cast<ACSCharacter>(GetPawn());
	if (TestRetryUntil(Mine != nullptr, TestFXTimer, &ACSPlayerController::CSTestFX, TEXT("a pawn")))
	{
		return;
	}
	if (!Mine)
	{
		UE_LOG(LogCS, Log, TEXT("FX TEST RESULT: no pawn -> MISSING"));
		return;
	}
	FRotator View = GetControlRotation();
	View.Pitch = -18.f;
	SetControlRotation(View);
	FXGalleryIndex = 0;
	FXSystemsLoaded = 0;
	GetWorldTimerManager().SetTimer(TestFXTimer, this, &ACSPlayerController::CSTestFXStep, 0.5f, false);
#endif
}

void ACSPlayerController::CSTestFXStep()
{
#if !UE_BUILD_SHIPPING
	ACSCharacter* Mine = Cast<ACSCharacter>(GetPawn());
	UWorld* World = GetWorld();
	if (!Mine || !World)
	{
		return;
	}
	FVector Eye;
	FVector Dir;
	Mine->GetAimRay(Eye, Dir);
	const FVector Flat = FVector(Dir.X, Dir.Y, 0.f).GetSafeNormal();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Flat).GetSafeNormal();

	const auto FloorAhead = [&](float Distance)
	{
		FHitResult Hit;
		const FVector Above = Eye + Flat * Distance;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CSTestFX), false, Mine);
		return World->LineTraceSingleByChannel(Hit, Above, Above - FVector(0.f, 0.f, 500.f), ECC_Visibility, Params)
			? Hit.ImpactPoint : Above - FVector(0.f, 0.f, 160.f);
	};

	// 1. Gallery.
	if (FXGalleryIndex < UE_ARRAY_COUNT(GGallery))
	{
		const FGalleryItem& Item = GGallery[FXGalleryIndex++];
		FVector Location = FloorAhead(400.f);
		FRotator Rotation(90.f, 0.f, 0.f);
		if (Item.Place == FGalleryItem::EPlace::FarFloor)
		{
			Location = FloorAhead(750.f);
		}
		else if (Item.Place == FGalleryItem::EPlace::NearRight)
		{
			Location = Eye + Dir * 90.f - Right * 25.f;
			Rotation = Right.Rotation();
		}
		UNiagaraComponent* Effect = CSEffects::SpawnSystem(World, GallerySystem(Item.Name), Location, Rotation);
		FXSystemsLoaded += (Effect && Effect->GetAsset()) ? 1 : 0;
		UE_LOG(LogCS, Log, TEXT("FX TEST: %s -> %s"), Item.Name, Effect ? TEXT("started") : TEXT("MISSING"));
		const FString Shot = FString(TEXT("fx_")) + Item.Name;
		// Three frames: the burst, its spread, and what lingers.
		for (const TPair<float, const TCHAR*>& Moment : { TPair<float, const TCHAR*>(Item.ShotDelay * 0.4f, TEXT("_a")),
			TPair<float, const TCHAR*>(Item.ShotDelay * 1.5f, TEXT("_b")), TPair<float, const TCHAR*>(Item.ShotDelay * 4.f, TEXT("_c")) })
		{
			FTimerHandle ShotTimer;
			const FString Name = Shot + Moment.Value;
			GetWorldTimerManager().SetTimer(ShotTimer, [this, Name]() { TestScreenshot(Name); }, Moment.Key, false);
		}
		GetWorldTimerManager().SetTimer(TestFXTimer, this, &ACSPlayerController::CSTestFXStep, 1.4f, false);
		return;
	}

	// 2. Transient-actor pool: a 2 s burst of flashes and tracers.
	UCSTransientFXPool* Pool = World->GetSubsystem<UCSTransientFXPool>();
	if (FXGalleryIndex == UE_ARRAY_COUNT(GGallery))
	{
		++FXGalleryIndex;
		FXPoolCreatedBefore = Pool ? Pool->GetNumCreated() : 0;
		FXPoolReusedBefore = Pool ? Pool->GetNumReused() : 0;
		FXBurstShots = 0;
		GetWorldTimerManager().SetTimer(TestFXTimer, [this]()
		{
			ACSCharacter* Shooter = Cast<ACSCharacter>(GetPawn());
			if (!Shooter || ++FXBurstShots > 60)
			{
				GetWorldTimerManager().SetTimer(TestFXTimer, this, &ACSPlayerController::CSTestFXStep, 0.5f, false);
				return;
			}
			FVector E;
			FVector D;
			Shooter->GetAimRay(E, D);
			CSEffects::MuzzleFlash(GetWorld(), FTransform(D.Rotation(), E + D * 60.f), 1.f, Shooter, false);
			CSEffects::Tracer(GetWorld(), E + D * 60.f, E + D * 2000.f, FLinearColor(1.f, 0.75f, 0.35f));
		}, 1.f / 30.f, true);
		return;
	}

	// 3. Niagara pool: two volleys of impacts, the second after the first ended.
	if (FXGalleryIndex == UE_ARRAY_COUNT(GGallery) + 1)
	{
		++FXGalleryIndex;
		FXFirstVolley.Reset();
		for (int32 i = 0; i < 20; ++i)
		{
			const FVector At = FloorAhead(200.f + 15.f * i);
			if (UNiagaraComponent* Effect = CSEffects::SpawnSystem(World, GallerySystem(TEXT("NS_Impact_Stone")), At, FRotator(90.f, 0.f, 0.f)))
			{
				FXFirstVolley.Add(Effect);
			}
		}
		GetWorldTimerManager().SetTimer(TestFXTimer, this, &ACSPlayerController::CSTestFXStep, 3.f, false);
		return;
	}

	int32 Reused = 0;
	for (int32 i = 0; i < 20; ++i)
	{
		const FVector At = FloorAhead(200.f + 15.f * i);
		UNiagaraComponent* Effect = CSEffects::SpawnSystem(World, GallerySystem(TEXT("NS_Impact_Stone")), At, FRotator(90.f, 0.f, 0.f));
		Reused += (Effect && FXFirstVolley.Contains(Effect)) ? 1 : 0;
	}
	const int32 Created = Pool ? Pool->GetNumCreated() - FXPoolCreatedBefore : -1;
	const int32 ReusedActors = Pool ? Pool->GetNumReused() - FXPoolReusedBefore : -1;
	const int32 NumSystems = UE_ARRAY_COUNT(GGallery);
	// 120 effects (60 flashes + 60 tracers) live 50-60 ms each at 30 per second.
	const bool bActorPool = Pool && Created <= 12 && ReusedActors >= 100;
	const bool bNiagaraPool = Reused >= 15;
	const bool bOk = FXSystemsLoaded == NumSystems && bActorPool && bNiagaraPool;
	UE_LOG(LogCS, Log, TEXT("FX TEST RESULT: systems %d/%d, flash+tracer actors created %d reused %d, niagara components reused %d/20 -> %s"),
		FXSystemsLoaded, NumSystems, Created, ReusedActors, Reused, bOk ? TEXT("OK") : TEXT("BROKEN"));
#endif
}
