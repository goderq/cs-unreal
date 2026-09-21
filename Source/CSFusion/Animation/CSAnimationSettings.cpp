// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Animation/CSAnimationSettings.h"

#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"

namespace
{
	const TCHAR* GAnimRoot = TEXT("/Game/Characters/Mannequins/Anims");

	TSoftObjectPtr<UAnimSequence> Anim(const FString& Relative)
	{
		const FString Name = FPaths::GetBaseFilename(Relative);
		return TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(FString::Printf(TEXT("%s/%s.%s"), GAnimRoot, *Relative, *Name)));
	}

	void FillStance(FCSStanceAnimSet& Set, const FString& Kind, const FString& AimFolder, const FString& AimPrefix)
	{
		static const TCHAR* Dirs[] = { TEXT("Fwd"), TEXT("Fwd_Right"), TEXT("Right"), TEXT("Bwd_Right"),
			TEXT("Bwd"), TEXT("Bwd_Left"), TEXT("Left"), TEXT("Fwd_Left") };

		Set.Idle = Anim(FString::Printf(TEXT("%s/MF_%s_Idle_ADS"), *Kind, *Kind));
		Set.Walk.Reset();
		Set.Jog.Reset();
		for (const TCHAR* Dir : Dirs)
		{
			Set.Walk.Add(Anim(FString::Printf(TEXT("%s/Walk/MF_%s_Walk_%s"), *Kind, *Kind, Dir)));
			Set.Jog.Add(Anim(FString::Printf(TEXT("%s/Jog/MF_%s_Jog_%s"), *Kind, *Kind, Dir)));
		}
		Set.FallLoop = Anim(FString::Printf(TEXT("%s/Jump/MM_%s_Jump_Fall_Loop"), *Kind, *Kind));
		Set.JumpStart = Anim(FString::Printf(TEXT("%s/Jump/MM_%s_Jump_Start"), *Kind, *Kind));
		Set.LandRecovery = Anim(FString::Printf(TEXT("%s/Jump/MM_%s_Jump_RecoveryAdditive"), *Kind, *Kind));
		Set.AimUp = Anim(FString::Printf(TEXT("%s/%s/%s_CU"), *Kind, *AimFolder, *AimPrefix));
		Set.AimDown = Anim(FString::Printf(TEXT("%s/%s/%s_CD"), *Kind, *AimFolder, *AimPrefix));
		Set.Fire = Anim(FString::Printf(TEXT("%s/MM_%s_Fire"), *Kind, *Kind));
		Set.Reload = Anim(FString::Printf(TEXT("%s/MM_%s_Reload"), *Kind, *Kind));
		Set.Equip = Anim(FString::Printf(TEXT("%s/MM_%s_Equip"), *Kind, *Kind));
		Set.DryFire = Anim(FString::Printf(TEXT("%s/MM_%s_DryFire"), *Kind, *Kind));
	}
}

UCSAnimationSettings::UCSAnimationSettings()
{
	CategoryName = TEXT("Game");

	FillStance(Pistol, TEXT("Pistol"), TEXT("Aim"), TEXT("MF_Pistol_Idle_ADS_AO"));
	FillStance(Rifle, TEXT("Rifle"), TEXT("AIM"), TEXT("MM_Rifle_Idle_ADS_AO"));
	Pistol.WalkClipSpeed = 170.f;
	Pistol.JogClipSpeed = 400.f;

	HitReactFront = Anim(TEXT("Rifle/HitReact/MM_HitReact_Front_Lgt_01"));
	HitReactBack = Anim(TEXT("Rifle/HitReact/MM_HitReact_Back_Med_01"));

	Death = {
		Anim(TEXT("Death/MM_Death_Front_01")),
		Anim(TEXT("Death/MM_Death_Back_01")),
		Anim(TEXT("Death/MM_Death_Left_01")),
		Anim(TEXT("Death/MM_Death_Right_01")),
	};

	SpawnProtectionMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/FX/M_SpawnGhost.M_SpawnGhost")));

	CharacterMeshes = {
		TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"))),
		TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"))),
	};
}
