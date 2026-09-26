// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Phase 3 first-person motion self-test, enabled with -cstestview
// (Scripts/run_tests.ps1 suite "view"), with the spawn pistol in hand:
//
//   1. F starts the inspect: the weapon turns well away from its rest pose;
//   2. aiming ends the inspect at once;
//   3. the sights come up in the weapon's own AimSeconds;
//   4. a jump lands with the weapon dipping.

#include "Player/CSPlayerController.h"

#include "Characters/CSCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Core/CSLog.h"
#include "TimerManager.h"
#include "Weapons/CSWeaponComponent.h"
#include "Weapons/CSWeaponDefinition.h"

void ACSPlayerController::CSTestView()
{
	CS_SELF_TEST_ONLY();
#if !UE_BUILD_SHIPPING
	ACSCharacter* Mine = Cast<ACSCharacter>(GetPawn());
	const bool bReady = Mine && Mine->GetDisplayedWeapon() && Mine->GetFirstPersonWeaponModel() && Mine->GetWeaponComponent();
	if (TestRetryUntil(bReady, TestViewTimer, &ACSPlayerController::CSTestView, TEXT("a weapon in hand")))
	{
		return;
	}
	if (!bReady)
	{
		UE_LOG(LogCS, Log, TEXT("VIEW TEST RESULT: no weapon in hand -> MISSING"));
		return;
	}
	ViewRestRotation = Mine->GetFirstPersonWeaponModel()->GetRelativeRotation().Quaternion();
	PressKey(EKeys::F);
	// The screenshot stalls a frame; take it well before the aim is timed.
	FTimerHandle ShotTimer;
	GetWorldTimerManager().SetTimer(ShotTimer, [this]() { TestScreenshot(TEXT("fp_inspect")); }, 0.5f, false);

	GetWorldTimerManager().SetTimer(TestViewTimer, [this]()
	{
		ACSCharacter* Me = Cast<ACSCharacter>(GetPawn());
		if (!Me)
		{
			return;
		}
		const float Turned = FMath::RadiansToDegrees(Me->GetFirstPersonWeaponModel()->GetRelativeRotation().Quaternion().AngularDistance(ViewRestRotation));
		const bool bInspecting = Me->IsInspecting();
		UE_LOG(LogCS, Log, TEXT("VIEW TEST RESULT: F -> inspecting %s, weapon turned %.0f deg -> %s"),
			bInspecting ? TEXT("yes") : TEXT("no"), Turned, (bInspecting && Turned > 20.f) ? TEXT("INSPECT OK") : TEXT("INSPECT BROKEN"));

		// Aiming cuts the inspect short and brings the sights up.
		Me->GetWeaponComponent()->SetAiming(true);
		ViewAimStart = GetWorld()->GetTimeSeconds();
		ViewAimTook = -1.f;
		GetWorldTimerManager().SetTimer(TestViewTimer, [this]()
		{
			ACSCharacter* Aimer = Cast<ACSCharacter>(GetPawn());
			if (!Aimer)
			{
				return;
			}
			const float Elapsed = GetWorld()->GetTimeSeconds() - ViewAimStart;
			if (ViewAimTook < 0.f && Aimer->GetAimAlpha() >= 0.999f)
			{
				ViewAimTook = Elapsed;
			}
			if (Elapsed < 0.8f)
			{
				return;
			}
			GetWorldTimerManager().ClearTimer(TestViewTimer);
			const float Expected = Aimer->GetDisplayedWeapon()->AimSeconds;
			const bool bAimOk = ViewAimTook > 0.f && FMath::Abs(ViewAimTook - Expected) <= 0.06f;
			UE_LOG(LogCS, Log, TEXT("VIEW TEST RESULT: aim -> inspect ended %s, sights up in %.2f s (weapon %.2f s) -> %s"),
				Aimer->IsInspecting() ? TEXT("no") : TEXT("yes"), ViewAimTook, Expected,
				(!Aimer->IsInspecting() && bAimOk) ? TEXT("AIM OK") : TEXT("AIM BROKEN"));
			Aimer->GetWeaponComponent()->SetAiming(false);

			// A jump: the weapon dips when the feet touch down.
			ViewMaxDip = 0.f;
			ViewJumpTicks = 0;
			Aimer->Jump();
			GetWorldTimerManager().SetTimer(TestViewTimer, [this]()
			{
				ACSCharacter* Jumper = Cast<ACSCharacter>(GetPawn());
				if (!Jumper)
				{
					return;
				}
				Jumper->StopJumping();
				ViewMaxDip = FMath::Max(ViewMaxDip, Jumper->GetWeaponLandDip());
				if (++ViewJumpTicks < 150)
				{
					return;
				}
				GetWorldTimerManager().ClearTimer(TestViewTimer);
				UE_LOG(LogCS, Log, TEXT("VIEW TEST RESULT: jump -> weapon landing dip %.2f -> %s"),
					ViewMaxDip, ViewMaxDip > 0.2f ? TEXT("LANDING OK") : TEXT("LANDING BROKEN"));
				UE_LOG(LogCS, Log, TEXT("VIEW TEST: done"));
			}, 0.01f, true);
		}, 0.005f, true);
	}, 0.8f, false);
#endif
}
