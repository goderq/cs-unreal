// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Stage 6 visual self-test (-cstestvisual): screenshots of the first-person
// arms and weapon, a shot (flash, tracer, impact), a reload, and a look at
// another player's animated third-person body, plus a log of the
// presentation state each step checks.

#include "Player/CSPlayerController.h"

#include "Animation/CSAnimInstance.h"
#include "Characters/CSCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/CSLog.h"
#include "EngineUtils.h"
#include "FX/CSEffects.h"
#include "FX/CSTransientFX.h"
#include "TimerManager.h"
#include "Weapons/CSWeaponDefinition.h"

namespace
{
	FString DescribeAnim(const UCSAnimInstance* Anim)
	{
		if (!Anim)
		{
			return TEXT("no CSAnimInstance");
		}
		return FString::Printf(TEXT("stance %s, speed %.0f, upper %s, dead %s"),
			*UEnum::GetValueAsString(Anim->GetStance()), Anim->GetGroundSpeed(),
			Anim->IsPlayingUpperBody() ? TEXT("yes") : TEXT("no"), Anim->IsDead() ? TEXT("yes") : TEXT("no"));
	}
}

void ACSPlayerController::CSTestVisual()
{
	ACSCharacter* Self = Cast<ACSCharacter>(GetPawn());
	if (!Self)
	{
		UE_LOG(LogCS, Warning, TEXT("VISUAL TEST: no pawn."));
		return;
	}

	const USkeletalMeshComponent* Arms = Self->GetFirstPersonMesh();
	const UCSWeaponDefinition* Weapon = Self->GetDisplayedWeapon();
	UE_LOG(LogCS, Log, TEXT("VISUAL TEST RESULT: arms mesh %s, arms anim [%s], body anim [%s], weapon %s -> %s"),
		*GetNameSafe(Arms ? Arms->GetSkeletalMeshAsset() : nullptr),
		*DescribeAnim(Self->GetArmsAnim()), *DescribeAnim(Self->GetBodyAnim()),
		Weapon ? *Weapon->DisplayName.ToString() : TEXT("none"),
		(Self->GetArmsAnim() && Self->GetBodyAnim() && Weapon) ? TEXT("PRESENTATION OK") : TEXT("PRESENTATION BROKEN"));

	TestScreenshot(TEXT("fp_idle"));

	// Long-lived copies of every effect in front of the camera, so a single
	// screenshot shows whether each material renders at all.
	{
		FVector Eye;
		FVector Dir;
		Self->GetAimRay(Eye, Dir);
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Dir).GetSafeNormal();
		const FVector Base = Eye + Dir * 250.f;

		ACSTransientFX::FParams Flash;
		Flash.Shape = ECSFXShape::Star;
		Flash.Color = FLinearColor(1.f, 0.62f, 0.22f);
		Flash.Intensity = 12.f;
		Flash.Lifetime = 0.9f;
		Flash.StartScale = Flash.EndScale = FVector(0.4f);
		ACSTransientFX::Spawn(GetWorld(), FTransform(Dir.Rotation(), Base - Right * 80.f), Flash);

		ACSTransientFX::FParams Dust;
		Dust.bAdditive = false;
		Dust.Color = FLinearColor(0.2f, 0.19f, 0.17f);
		Dust.Opacity = 0.85f;
		Dust.Lifetime = 0.9f;
		Dust.StartScale = Dust.EndScale = FVector(0.4f);
		ACSTransientFX::Spawn(GetWorld(), FTransform(Base), Dust);

		ACSTransientFX::FParams Streak;
		Streak.Shape = ECSFXShape::Cylinder;
		Streak.Color = FLinearColor(1.f, 0.75f, 0.35f);
		Streak.Intensity = 20.f;
		Streak.Lifetime = 0.9f;
		Streak.StartScale = Streak.EndScale = FVector(3.f, 0.02f, 0.02f);
		ACSTransientFX::Spawn(GetWorld(), FTransform(Right.Rotation(), Base - FVector(0, 0, 60.f) - Right * 150.f), Streak);
		CSEffects::Impact(GetWorld(), Eye + Dir * 300.f - FVector(0, 0, 150.f) + Right * 80.f, FVector::UpVector, false);

		FTimerHandle DebugShot;
		GetWorldTimerManager().SetTimer(DebugShot, [this]() { TestScreenshot(TEXT("fx_debug")); }, 0.03f, false);
	}

	GetWorldTimerManager().SetTimer(TestVisualTimer, [this]()
	{
		// Aim at the floor a few metres ahead so the impact is close enough to see.
		FRotator View = GetControlRotation();
		View.Pitch = -22.f;
		SetControlRotation(View);
		PressKey(EKeys::LeftMouseButton);

		// The input is processed next frame and the flash lives ~50 ms.
		GetWorldTimerManager().SetTimer(TestVisualTimer, [this]()
		{
			TestScreenshot(TEXT("fp_fire"));
		}, 0.03f, false);

		FTimerHandle ImpactTimer;
		GetWorldTimerManager().SetTimer(ImpactTimer, [this]()
		{
			TestScreenshot(TEXT("fp_impact"));
			PressKey(EKeys::R);

			GetWorldTimerManager().SetTimer(TestVisualTimer, [this]()
			{
				const ACSCharacter* Me = Cast<ACSCharacter>(GetPawn());
				UE_LOG(LogCS, Log, TEXT("VISUAL TEST RESULT: reload -> arms [%s]"),
					Me ? *DescribeAnim(Me->GetArmsAnim()) : TEXT("?"));
				TestScreenshot(TEXT("fp_reload"));

				// Look at another player, if there is one, for the third-person body.
				ACSCharacter* Other = nullptr;
				for (TActorIterator<ACSCharacter> It(GetWorld()); It; ++It)
				{
					if (*It != GetPawn())
					{
						Other = *It;
						break;
					}
				}
				if (Other && GetPawn())
				{
					FVector Eye;
					FVector Unused;
					Cast<ACSCharacter>(GetPawn())->GetAimRay(Eye, Unused);
					SetControlRotation((Other->GetActorLocation() - Eye).Rotation());
					UE_LOG(LogCS, Log, TEXT("VISUAL TEST RESULT: remote body mesh %s, anim [%s], %.0f cm away"),
						*GetNameSafe(Other->GetMesh()->GetSkeletalMeshAsset()), *DescribeAnim(Other->GetBodyAnim()),
						FVector::Dist(Other->GetActorLocation(), GetPawn()->GetActorLocation()));
				}
				else
				{
					UE_LOG(LogCS, Log, TEXT("VISUAL TEST: no other player to look at."));
				}

				GetWorldTimerManager().SetTimer(TestVisualTimer, [this]()
				{
					TestScreenshot(TEXT("tp_remote"));
				}, 0.5f, false);
			}, 0.7f, false);
		}, 0.25f, false);
	}, 1.0f, false);
}
