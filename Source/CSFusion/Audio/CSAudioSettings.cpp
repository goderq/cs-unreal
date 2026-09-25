// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Audio/CSAudioSettings.h"

#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"

namespace
{
	template <typename T>
	TSoftObjectPtr<T> At(const TCHAR* Path)
	{
		const FString P(Path);
		return TSoftObjectPtr<T>(FSoftObjectPath(P + TEXT(".") + FPaths::GetBaseFilename(P)));
	}
}

UCSAudioSettings::UCSAudioSettings()
{
	CategoryName = TEXT("Game");

	MusicClass = At<USoundClass>(TEXT("/Game/Audio/Mix/SC_Music"));
	EffectsClass = At<USoundClass>(TEXT("/Game/Audio/Mix/SC_Effects"));
	WorldAttenuation = At<USoundAttenuation>(TEXT("/Game/Audio/Mix/ATT_World"));

	Footsteps = {
		At<USoundBase>(TEXT("/Game/Audio/Player/S_Footstep_01")),
		At<USoundBase>(TEXT("/Game/Audio/Player/S_Footstep_02")),
		At<USoundBase>(TEXT("/Game/Audio/Player/S_Footstep_03")),
		At<USoundBase>(TEXT("/Game/Audio/Player/S_Footstep_04")),
	};
	JumpLand = At<USoundBase>(TEXT("/Game/Audio/Player/S_Land"));
	GrenadeExplode = At<USoundBase>(TEXT("/Game/Audio/Weapons/S_Grenade_Explode"));
	GrenadeBounce = At<USoundBase>(TEXT("/Game/Audio/Weapons/S_Grenade_Bounce"));
	GrenadePin = At<USoundBase>(TEXT("/Game/Audio/Weapons/S_Grenade_Pin"));
	// v2.0 (stand-ins from generate_audio.py until recordings replace them)
	FlashbangExplode = At<USoundBase>(TEXT("/Game/Audio/Weapons/S_Flashbang_Explode"));
	FlashbangRing = At<USoundBase>(TEXT("/Game/Audio/Weapons/S_Flashbang_Ring"));
	KnifeSwing = At<USoundBase>(TEXT("/Game/Audio/Weapons/S_Knife_Swing"));
	KnifeHitBody = At<USoundBase>(TEXT("/Game/Audio/Weapons/S_Knife_HitBody"));
	KnifeHitWall = At<USoundBase>(TEXT("/Game/Audio/Weapons/S_Knife_HitWall"));
	AmmoMachineBuy = At<USoundBase>(TEXT("/Game/Audio/Weapons/S_AmmoMachine_Buy"));
	BulletImpact = At<USoundBase>(TEXT("/Game/Audio/Impacts/S_Impact_Surface"));
	BodyHit = At<USoundBase>(TEXT("/Game/Audio/Impacts/S_Impact_Body"));
	Death = At<USoundBase>(TEXT("/Game/Audio/Player/S_Death"));
	Pickup = At<USoundBase>(TEXT("/Game/Audio/Player/S_Pickup"));
	Respawn = At<USoundBase>(TEXT("/Game/Audio/Player/S_Respawn"));

	HitMarker = At<USoundBase>(TEXT("/Game/Audio/Feedback/S_HitMarker"));
	Headshot = At<USoundBase>(TEXT("/Game/Audio/Feedback/S_Headshot"));
	KillConfirm = At<USoundBase>(TEXT("/Game/Audio/Feedback/S_Kill"));
	Hurt = At<USoundBase>(TEXT("/Game/Audio/Feedback/S_Hurt"));

	UIClick = At<USoundBase>(TEXT("/Game/Audio/UI/S_UI_Click"));
	UIHover = At<USoundBase>(TEXT("/Game/Audio/UI/S_UI_Hover"));
	MenuMusic = At<USoundBase>(TEXT("/Game/Audio/Music/S_Music_Menu"));
}
