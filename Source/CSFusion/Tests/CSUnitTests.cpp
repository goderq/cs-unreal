// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Stage 8 unit tests for the pure game rules: damage model, hit zones, bot
// tuning, settings sanitising and the anti-cheat guard. They need no world
// and no network, so they run headless in seconds:
//
//   UnrealEditor-Cmd.exe CSFusion.uproject -ExecCmds="Automation RunTests CSFusion.Unit; Quit"
//       -unattended -nullrhi -nosplash -log
//
// (Scripts/run_tests.ps1 runs these plus the in-game self-tests.)

#include "AI/CSBotTuning.h"
#include "Audio/CSAudio.h"
#include "Audio/CSAudioSettings.h"
#include "Graphics/CSGraphics.h"
#include "Kismet/GameplayStatics.h"
#include "Settings/CSSettingsSave.h"
#include "Characters/CSCharacter.h"
#include "Combat/CSCheatGuard.h"
#include "Combat/CSMatchDirector.h"
#include "Components/CapsuleComponent.h"
#include "Misc/AutomationTest.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Settings/CSSettingsSubsystem.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundConcurrency.h"
#include "Weapons/CSWeaponComponent.h"
#include "Weapons/CSWeaponDefinition.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags CSUnitFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
}

// ---------------------------------------------------------------------------
// Damage model
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSUnitDamageTest, "CSFusion.Unit.Weapon.Damage", CSUnitFlags)
bool FCSUnitDamageTest::RunTest(const FString& Parameters)
{
	UCSWeaponDefinition* Weapon = NewObject<UCSWeaponDefinition>();
	Weapon->BaseDamage = 20.f;
	Weapon->HeadshotMultiplier = 4.f;
	Weapon->LimbMultiplier = 0.75f;
	Weapon->FalloffStartDistance = 1000.f;
	Weapon->FalloffEndDistance = 3000.f;
	Weapon->MinDamageMultiplier = 0.5f;

	TestEqual(TEXT("torso, point blank"), Weapon->ComputeDamage(ECSHitZone::Torso, 100.f), 20.f);
	TestEqual(TEXT("head, point blank"), Weapon->ComputeDamage(ECSHitZone::Head, 100.f), 80.f);
	TestEqual(TEXT("limb, point blank"), Weapon->ComputeDamage(ECSHitZone::Limb, 100.f), 15.f);

	TestEqual(TEXT("no falloff at start"), Weapon->GetDistanceMultiplier(1000.f), 1.f);
	TestEqual(TEXT("half way"), Weapon->GetDistanceMultiplier(2000.f), 0.75f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("floor at end"), Weapon->GetDistanceMultiplier(3000.f), 0.5f);
	TestEqual(TEXT("floor beyond end"), Weapon->GetDistanceMultiplier(50000.f), 0.5f);

	// Misconfigured range must not divide by zero.
	Weapon->FalloffEndDistance = Weapon->FalloffStartDistance;
	TestEqual(TEXT("degenerate range"), Weapon->GetDistanceMultiplier(1500.f), 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSUnitHitZoneTest, "CSFusion.Unit.Weapon.HitZones", CSUnitFlags)
bool FCSUnitHitZoneTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("head"), UCSWeaponComponent::ResolveHitZone(TEXT("head")) == ECSHitZone::Head);
	TestTrue(TEXT("neck_01"), UCSWeaponComponent::ResolveHitZone(TEXT("neck_01")) == ECSHitZone::Head);
	TestTrue(TEXT("spine_03"), UCSWeaponComponent::ResolveHitZone(TEXT("spine_03")) == ECSHitZone::Torso);
	TestTrue(TEXT("upperarm_l"), UCSWeaponComponent::ResolveHitZone(TEXT("upperarm_l")) == ECSHitZone::Limb);
	TestTrue(TEXT("calf_r"), UCSWeaponComponent::ResolveHitZone(TEXT("calf_r")) == ECSHitZone::Limb);
	TestTrue(TEXT("unknown bone defaults to torso"), UCSWeaponComponent::ResolveHitZone(TEXT("root")) == ECSHitZone::Torso);
	TestTrue(TEXT("capsule (no bone)"), UCSWeaponComponent::ResolveHitZone(NAME_None) == ECSHitZone::Torso);
	return true;
}

// ---------------------------------------------------------------------------
// Bots
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSUnitBotTuningTest, "CSFusion.Unit.Bots.Tuning", CSUnitFlags)
bool FCSUnitBotTuningTest::RunTest(const FString& Parameters)
{
	const FCSBotTuning Easy = FCSBotTuning::For(ECSBotDifficulty::Easy);
	const FCSBotTuning Normal = FCSBotTuning::For(ECSBotDifficulty::Normal);
	const FCSBotTuning Hard = FCSBotTuning::For(ECSBotDifficulty::Hard);

	// Harder = reacts faster, aims tighter, sees further. Strictly ordered.
	TestTrue(TEXT("reaction ordered"), Easy.ReactionTime > Normal.ReactionTime && Normal.ReactionTime > Hard.ReactionTime);
	TestTrue(TEXT("aim error ordered"), Easy.AimErrorDegrees > Normal.AimErrorDegrees && Normal.AimErrorDegrees > Hard.AimErrorDegrees);
	TestTrue(TEXT("sight ordered"), Easy.SightRadius < Normal.SightRadius && Normal.SightRadius < Hard.SightRadius);
	// v1.1: head share of the body-part weights, turn speed.
	auto HeadShare = [](const FCSBotTuning& T)
	{
		float Total = 0.f;
		for (const float W : T.PartWeights) { Total += W; }
		return Total > 0.f ? T.PartWeights[static_cast<int32>(ECSBotAimPart::Head)] / Total : 0.f;
	};
	TestTrue(TEXT("headshots ordered"), HeadShare(Easy) < HeadShare(Normal) && HeadShare(Normal) < HeadShare(Hard));
	TestTrue(TEXT("turning ordered"), Easy.TurnSpeed < Normal.TurnSpeed && Normal.TurnSpeed < Hard.TurnSpeed);
	TestTrue(TEXT("never only the head"), HeadShare(Hard) < 0.35f);
	TestTrue(TEXT("even hard bots are human-like"), Hard.ReactionTime >= 0.15f && Hard.AimErrorDegrees > 0.f && Hard.SwayDegrees > 0.f);
	return true;
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSUnitSettingsTest, "CSFusion.Unit.Settings.Sanitize", CSUnitFlags)
bool FCSUnitSettingsTest::RunTest(const FString& Parameters)
{
	FCSPlayerPreferences P;
	P.MouseSensitivity = 999.f;
	P.FieldOfView = 10.f;
	P.MasterVolume = -3.f;
	P.MusicVolume = 7.f;
	P.EffectsVolume = 0.5f;
	P.KeyOverrides.Add(TEXT("Jump"), EKeys::SpaceBar);
	P.KeyOverrides.Add(TEXT("Fire"), FKey());
	UCSSettingsSubsystem::SanitizePreferences(P);

	TestEqual(TEXT("sensitivity clamped"), P.MouseSensitivity, 5.f);
	TestEqual(TEXT("FOV clamped"), P.FieldOfView, 70.f);
	TestEqual(TEXT("master clamped"), P.MasterVolume, 0.f);
	TestEqual(TEXT("music clamped"), P.MusicVolume, 1.f);
	TestEqual(TEXT("legal value kept"), P.EffectsVolume, 0.5f);
	TestEqual(TEXT("invalid key dropped"), P.KeyOverrides.Num(), 1);
	TestTrue(TEXT("valid key kept"), P.KeyOverrides.Contains(TEXT("Jump")));

	const FCSPlayerPreferences Defaults = UCSSettingsSubsystem::DefaultPreferences();
	FCSPlayerPreferences Copy = Defaults;
	UCSSettingsSubsystem::SanitizePreferences(Copy);
	TestEqual(TEXT("defaults are legal (FOV)"), Copy.FieldOfView, Defaults.FieldOfView);
	TestEqual(TEXT("defaults are legal (sensitivity)"), Copy.MouseSensitivity, Defaults.MouseSensitivity);
	TestFalse(TEXT("FPS counter off by default"), Defaults.bShowFps);
	return true;
}

// v2.0 phase 6 (C11): the preferences survive a restart (written to disk and
// read back by a fresh load), and a schema-1 file keeps its values.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSUnitSettingsRestartTest, "CSFusion.Unit.Settings.Restart", CSUnitFlags)
bool FCSUnitSettingsRestartTest::RunTest(const FString& Parameters)
{
	const FString Slot = TEXT("CSSettingsUnitTest");
	FCSPlayerPreferences P;
	P.MouseSensitivity = 2.25f;
	P.AimSensitivity = 0.6f;
	P.bToggleAim = true;
	P.CrosshairStyle = 3;
	P.CrosshairColor = 2;
	P.CrosshairSize = 12.f;
	P.ColorVision = 2;
	P.UIScale = 1.15f;
	P.CameraMotion = 0.25f;
	P.bReduceFlash = true;
	P.bShowNetStats = true;
	P.KeyOverrides.Add(TEXT("Jump"), EKeys::F);
	TestTrue(TEXT("written"), UCSSettingsSubsystem::SavePreferencesToSlot(P, Slot));

	FCSPlayerPreferences Back;
	int32 Schema = 0;
	TestTrue(TEXT("read back"), UCSSettingsSubsystem::LoadPreferencesFromSlot(Slot, Back, &Schema));
	TestEqual(TEXT("schema"), Schema, UCSSettingsSave::CurrentSchema);
	TestEqual(TEXT("sensitivity"), Back.MouseSensitivity, 2.25f);
	TestEqual(TEXT("aim sensitivity"), Back.AimSensitivity, 0.6f);
	TestTrue(TEXT("toggle aim"), Back.bToggleAim);
	TestEqual(TEXT("crosshair style"), Back.CrosshairStyle, 3);
	TestEqual(TEXT("crosshair colour"), Back.CrosshairColor, 2);
	TestEqual(TEXT("crosshair size"), Back.CrosshairSize, 12.f);
	TestEqual(TEXT("colour vision"), Back.ColorVision, 2);
	TestEqual(TEXT("UI scale"), Back.UIScale, 1.15f);
	TestEqual(TEXT("camera motion"), Back.CameraMotion, 0.25f);
	TestTrue(TEXT("reduced flash"), Back.bReduceFlash);
	TestTrue(TEXT("net stats"), Back.bShowNetStats);
	TestTrue(TEXT("key override"), Back.KeyOverrides.FindRef(TEXT("Jump")) == EKeys::F);

	// A file from before phase 6: schema 1, only the old fields set.
	UCSSettingsSave* Old = Cast<UCSSettingsSave>(UGameplayStatics::CreateSaveGameObject(UCSSettingsSave::StaticClass()));
	Old->SchemaVersion = 1;
	Old->Preferences = FCSPlayerPreferences();
	Old->Preferences.FieldOfView = 90.f;
	Old->Preferences.MasterVolume = 0.4f;
	TestTrue(TEXT("schema-1 file written"), UGameplayStatics::SaveGameToSlot(Old, Slot, 0));
	FCSPlayerPreferences Migrated;
	TestTrue(TEXT("schema-1 file read"), UCSSettingsSubsystem::LoadPreferencesFromSlot(Slot, Migrated, &Schema));
	TestEqual(TEXT("its schema is reported"), Schema, 1);
	TestEqual(TEXT("old FOV kept"), Migrated.FieldOfView, 90.f);
	TestEqual(TEXT("old volume kept"), Migrated.MasterVolume, 0.4f);
	TestEqual(TEXT("new option at its default"), Migrated.CameraMotion, 1.f);

	UGameplayStatics::DeleteGameInSlot(Slot, 0);
	return true;
}

// v2.0 phase 5: presets, custom mixes and the render scale table.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSUnitGraphicsTest, "CSFusion.Unit.Settings.Graphics", CSUnitFlags)
bool FCSUnitGraphicsTest::RunTest(const FString& Parameters)
{
	FCSGraphicsSettings G;
	UCSSettingsSubsystem::SetPreset(G, 4);
	bool bAllCinematic = true;
	for (const int32 Level : G.Groups)
	{
		bAllCinematic &= Level == 4;
	}
	TestTrue(TEXT("Cinematic sets every group to 4"), bAllCinematic);

	G.Groups[static_cast<int32>(ECSGraphicsGroup::Shadows)] = 1;
	UCSSettingsSubsystem::SanitizeGraphics(G);
	TestEqual(TEXT("a changed group makes the preset Custom"), G.Preset, FCSGraphicsSettings::CustomPreset);

	UCSSettingsSubsystem::SetPreset(G, FCSGraphicsSettings::CustomPreset);
	TestEqual(TEXT("Custom leaves the groups alone"), G.Groups[static_cast<int32>(ECSGraphicsGroup::Shadows)], 1);

	FCSGraphicsSettings Same;
	for (int32& Level : Same.Groups)
	{
		Level = 1;
	}
	Same.Preset = FCSGraphicsSettings::CustomPreset;
	UCSSettingsSubsystem::SanitizeGraphics(Same);
	TestEqual(TEXT("Custom with equal groups stays Custom"), Same.Preset, FCSGraphicsSettings::CustomPreset);
	Same.Preset = 3;
	UCSSettingsSubsystem::SanitizeGraphics(Same);
	TestEqual(TEXT("a preset that does not match its groups follows them"), Same.Preset, 1);

	FCSGraphicsSettings Bad;
	Bad.Preset = 42;
	Bad.Upscaler = 9;
	Bad.RenderScale = -3;
	Bad.Groups[0] = 17;
	UCSSettingsSubsystem::SanitizeGraphics(Bad);
	TestEqual(TEXT("upscaler clamped"), Bad.Upscaler, 2);
	TestEqual(TEXT("render scale clamped"), Bad.RenderScale, 0);
	TestEqual(TEXT("group clamped"), Bad.Groups[0], 4);
	TestEqual(TEXT("preset clamped, groups differ -> Custom"), Bad.Preset, FCSGraphicsSettings::CustomPreset);

	TestEqual(TEXT("native is 100%"), CSGraphics::ScreenPercentage(ECSRenderScale::Native), 100.f);
	TestEqual(TEXT("performance is 50%"), CSGraphics::ScreenPercentage(ECSRenderScale::Performance), 50.f);
	TestTrue(TEXT("scales go down"), CSGraphics::ScreenPercentage(ECSRenderScale::Quality) > CSGraphics::ScreenPercentage(ECSRenderScale::Balanced)
		&& CSGraphics::ScreenPercentage(ECSRenderScale::Balanced) > CSGraphics::ScreenPercentage(ECSRenderScale::UltraPerformance));
	return true;
}

// ---------------------------------------------------------------------------
// Anti-cheat
// ---------------------------------------------------------------------------

namespace
{
	/** Moves a pawn at Speed cm/s along X for Seconds, observing at 30 Hz. Returns the end time. */
	double Walk(FCSCheatGuard& Guard, int32 Id, FVector& Pos, double Now, float Speed, double Seconds, int32 Respawn = 0)
	{
		const double Dt = 1.0 / 30.0;
		const int32 Steps = FMath::CeilToInt(Seconds / Dt);
		for (int32 i = 0; i < Steps; ++i)
		{
			Now += Dt;
			Pos.X += Speed * Dt;
			Guard.ObservePosition(Id, Pos, Now, false, Respawn);
		}
		return Now;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSUnitCheatMovementTest, "CSFusion.Unit.AntiCheat.Movement", CSUnitFlags)
bool FCSUnitCheatMovementTest::RunTest(const FString& Parameters)
{
	// Legal sprinting for a long time: never a strike.
	{
		FCSCheatGuard Guard;
		FVector Pos = FVector::ZeroVector;
		const double End = Walk(Guard, 1, Pos, 100.0, 620.f, 20.0);
		TestEqual(TEXT("sprint: no strikes"), Guard.GetStrikes(1), 0);
		TestFalse(TEXT("sprint: not suspended"), Guard.IsSuspended(1, End));
		TestTrue(TEXT("sprint: no reason"), Guard.GetLastReason(1) == ECSCheatReason::None);
	}

	// Speed hack at 2x sprint: suspended within a few seconds, for Speed.
	{
		FCSCheatGuard Guard;
		FVector Pos = FVector::ZeroVector;
		double Now = Walk(Guard, 2, Pos, 100.0, 0.f, 3.0); // past the first-sighting grace
		Now = Walk(Guard, 2, Pos, Now, 1240.f, 5.0);
		TestTrue(TEXT("speed hack: suspended"), Guard.IsSuspended(2, Now));
		TestTrue(TEXT("speed hack: reason"), Guard.GetLastReason(2) == ECSCheatReason::Speed);
	}

	// Teleport: suspended immediately; lifted after SuspensionSeconds.
	{
		FCSCheatGuard Guard;
		FVector Pos = FVector::ZeroVector;
		double Now = Walk(Guard, 3, Pos, 100.0, 0.f, 3.0);
		Pos.Y += 1500.f;
		Now += 0.1;
		Guard.ObservePosition(3, Pos, Now, false, 0);
		TestTrue(TEXT("teleport: suspended"), Guard.IsSuspended(3, Now));
		TestTrue(TEXT("teleport: reason"), Guard.GetLastReason(3) == ECSCheatReason::Teleport);
		TestFalse(TEXT("teleport: fire refused"), Guard.AllowRequest(3, ECSRequestKind::Fire, Now));
		TestTrue(TEXT("teleport: reload still allowed"), Guard.AllowRequest(3, ECSRequestKind::Reload, Now));
		const double Later = Walk(Guard, 3, Pos, Now, 300.f, FCSCheatTuning().SuspensionSeconds + 0.5);
		TestFalse(TEXT("teleport: suspension lifted"), Guard.IsSuspended(3, Later));
		TestTrue(TEXT("teleport: fire allowed again"), Guard.AllowRequest(3, ECSRequestKind::Fire, Later));
	}

	// Respawn teleports and first sightings are legal.
	{
		FCSCheatGuard Guard;
		FVector Pos(5000.f, 0.f, 0.f);
		double Now = Walk(Guard, 4, Pos, 100.0, 0.f, 3.0, /*Respawn*/ 0);
		Pos = FVector(-5000.f, 3000.f, 0.f);
		Now = Walk(Guard, 4, Pos, Now, 0.f, 3.0, /*Respawn*/ 1);
		TestEqual(TEXT("respawn: no strikes"), Guard.GetStrikes(4), 0);
		TestFalse(TEXT("respawn: not suspended"), Guard.IsSuspended(4, Now));
	}

	// A lag spike - one late update after a second of silence - is not a teleport.
	{
		FCSCheatGuard Guard;
		FVector Pos = FVector::ZeroVector;
		double Now = Walk(Guard, 5, Pos, 100.0, 600.f, 3.0);
		Pos.X += 600.f;
		Now += 1.0;
		Guard.ObservePosition(5, Pos, Now, false, 0);
		Now = Walk(Guard, 5, Pos, Now, 600.f, 2.0);
		TestEqual(TEXT("lag spike: no strikes"), Guard.GetStrikes(5), 0);
		TestFalse(TEXT("lag spike: not suspended"), Guard.IsSuspended(5, Now));
	}

	// Forget clears a player completely.
	{
		FCSCheatGuard Guard;
		FVector Pos = FVector::ZeroVector;
		const double Now = Walk(Guard, 6, Pos, 100.0, 0.f, 3.0);
		Pos.Y += 5000.f;
		Guard.ObservePosition(6, Pos, Now + 0.1, false, 0);
		Guard.Forget(6);
		TestFalse(TEXT("forget: not suspended"), Guard.IsSuspended(6, Now + 0.2));
		TestTrue(TEXT("forget: no reason"), Guard.GetLastReason(6) == ECSCheatReason::None);
	}
	return true;
}

namespace
{
	/** One 30 Hz step of a full sample: moves by Velocity, reports the floor gap and walls. */
	double Step(FCSCheatGuard& Guard, int32 Id, FVector& Pos, double Now, const FVector& Velocity, float AboveFloor,
		bool bBlocked = false, int32 Respawn = 0, const FVector* Spawn = nullptr)
	{
		Now += 1.0 / 30.0;
		Pos += Velocity / 30.0;
		FCSMoveSample Sample;
		Sample.Location = Pos;
		Sample.Now = Now;
		Sample.RespawnCounter = Respawn;
		Sample.HeightAboveFloor = AboveFloor;
		Sample.bPathBlocked = bBlocked;
		if (Spawn)
		{
			Sample.SpawnPoint = *Spawn;
			Sample.bHasSpawnPoint = true;
		}
		Guard.Observe(Id, Sample);
		return Now;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSUnitCheatMovement2Test, "CSFusion.Unit.AntiCheat.Movement2", CSUnitFlags)
bool FCSUnitCheatMovement2Test::RunTest(const FString& Parameters)
{
	// Phase 2 (B10): flying, walls, spawn points; B11: the ladder.

	// Stairs at a sprint (rising ~430 cm/s) and a normal jump: no strikes.
	{
		FCSCheatGuard Guard;
		FVector Pos = FVector::ZeroVector;
		double Now = 100.0;
		for (int32 i = 0; i < 90; ++i) { Now = Step(Guard, 1, Pos, Now, FVector(0.f, 0.f, 0.f), 0.f); }
		for (int32 i = 0; i < 60; ++i) { Now = Step(Guard, 1, Pos, Now, FVector(600.f, 0.f, 430.f), 0.f); }
		for (int32 i = 0; i < 24; ++i)
		{
			const float T = i / 30.f;
			const float Height = FMath::Max(0.f, 420.f * T - 490.f * T * T); // a 0.86 s jump, 90 cm high
			Now = Step(Guard, 1, Pos, Now, FVector(600.f, 0.f, 0.f), Height);
		}
		TestEqual(TEXT("stairs and a jump: no strikes"), Guard.GetStrikes(1), 0);
	}

	// Hovering 200 cm above the floor: a strike every 3 s, suspended by the second one.
	{
		FCSCheatGuard Guard;
		FVector Pos = FVector::ZeroVector;
		double Now = 100.0;
		for (int32 i = 0; i < 90; ++i) { Now = Step(Guard, 2, Pos, Now, FVector::ZeroVector, 0.f); }
		for (int32 i = 0; i < 30 * 7; ++i) { Now = Step(Guard, 2, Pos, Now, FVector::ZeroVector, 200.f); }
		TestTrue(TEXT("hovering: suspended"), Guard.IsSuspended(2, Now));
		TestTrue(TEXT("hovering: reason"), Guard.GetLastReason(2) == ECSCheatReason::Flying);
	}

	// Rising at 900 cm/s (a fly hack going up): a strike for flying.
	{
		FCSCheatGuard Guard;
		FVector Pos = FVector::ZeroVector;
		double Now = 100.0;
		for (int32 i = 0; i < 90; ++i) { Now = Step(Guard, 3, Pos, Now, FVector::ZeroVector, 0.f); }
		for (int32 i = 0; i < 45; ++i) { Now = Step(Guard, 3, Pos, Now, FVector(0.f, 0.f, 900.f), 0.f); }
		TestTrue(TEXT("rising: reason"), Guard.GetLastReason(3) == ECSCheatReason::Flying);
	}

	// Four steps through walls in quick succession: suspended (three decay to just under the limit).
	{
		FCSCheatGuard Guard;
		FVector Pos = FVector::ZeroVector;
		double Now = 100.0;
		for (int32 i = 0; i < 90; ++i) { Now = Step(Guard, 4, Pos, Now, FVector::ZeroVector, 0.f); }
		for (int32 i = 0; i < 4; ++i) { Now = Step(Guard, 4, Pos, Now, FVector(300.f, 0.f, 0.f), 0.f, /*bBlocked*/ true); }
		TestTrue(TEXT("walls: suspended"), Guard.IsSuspended(4, Now));
		TestTrue(TEXT("walls: reason"), Guard.GetLastReason(4) == ECSCheatReason::Wall);
	}

	// Respawn: near the spawn point is fine, 50 m away is not.
	{
		FCSCheatGuard Guard;
		FVector Pos = FVector::ZeroVector;
		const FVector Spawn(8000.f, 0.f, 0.f);
		double Now = 100.0;
		for (int32 i = 0; i < 90; ++i) { Now = Step(Guard, 5, Pos, Now, FVector::ZeroVector, 0.f, false, 1, &Spawn); }
		Pos = Spawn + FVector(200.f, 0.f, 0.f);
		for (int32 i = 0; i < 90; ++i) { Now = Step(Guard, 5, Pos, Now, FVector::ZeroVector, 0.f, false, 2, &Spawn); }
		TestEqual(TEXT("respawn at the spawn point: no strikes"), Guard.GetStrikes(5), 0);

		FCSCheatGuard Cheat;
		FVector Other = FVector::ZeroVector;
		Now = 100.0;
		for (int32 i = 0; i < 90; ++i) { Now = Step(Cheat, 6, Other, Now, FVector::ZeroVector, 0.f, false, 1, &Spawn); }
		Other = Spawn + FVector(0.f, 5000.f, 0.f); // "respawned" somewhere else entirely
		for (int32 i = 0; i < 90; ++i) { Now = Step(Cheat, 6, Other, Now, FVector::ZeroVector, 0.f, false, 2, &Spawn); }
		TestTrue(TEXT("respawn 50 m away: suspended"), Cheat.IsSuspended(6, Now));
		TestTrue(TEXT("respawn 50 m away: reason"), Cheat.GetLastReason(6) == ECSCheatReason::SpawnPoint);
	}

	// The ladder: the third suspension removes the player; every step is an incident.
	{
		FCSCheatGuard Guard;
		double Now = 100.0;
		for (int32 i = 0; i < 3; ++i)
		{
			Guard.ReportViolation(7, ECSCheatReason::ForgedRpc, Now, 3.f, TEXT("test"));
			Now += Guard.Tuning.SuspensionSeconds + 1.0;
		}
		const TArray<FCSCheatIncident> Incidents = Guard.TakeIncidents();
		TestEqual(TEXT("ladder: three incidents"), Incidents.Num(), 3);
		TestEqual(TEXT("ladder: three suspensions"), Guard.GetSuspensions(7), 3);
		TestTrue(TEXT("ladder: removed"), Guard.ShouldRemove(7));
		TestTrue(TEXT("ladder: last incident is the removal"), Incidents.Num() == 3 && Incidents[2].bRemoved && !Incidents[1].bRemoved);
		TestFalse(TEXT("ladder: removed player's requests refused"), Guard.AllowRequest(7, ECSRequestKind::Reload, Now));
		TestEqual(TEXT("ladder: incidents taken once"), Guard.TakeIncidents().Num(), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSUnitCheatFloodTest, "CSFusion.Unit.AntiCheat.Flood", CSUnitFlags)
bool FCSUnitCheatFloodTest::RunTest(const FString& Parameters)
{
	FCSCheatGuard Guard;
	const double Now = 0.0; // the very first instant of a match must work too

	// Burst of 30 fire requests is allowed, the rest are not.
	int32 Allowed = 0;
	for (int32 i = 0; i < 40; ++i)
	{
		Allowed += Guard.AllowRequest(1, ECSRequestKind::Fire, Now) ? 1 : 0;
	}
	TestEqual(TEXT("fire burst"), Allowed, 30);
	TestTrue(TEXT("flood reason"), Guard.GetLastReason(1) == ECSCheatReason::Flood);
	TestEqual(TEXT("one strike per flood"), Guard.GetStrikes(1), 1);

	// Tokens refill at 20/s.
	Allowed = 0;
	for (int32 i = 0; i < 40; ++i)
	{
		Allowed += Guard.AllowRequest(1, ECSRequestKind::Fire, Now + 1.0) ? 1 : 0;
	}
	TestEqual(TEXT("refill after 1 s"), Allowed, 20);

	// Buckets are independent per kind and per player.
	TestTrue(TEXT("other kind unaffected"), Guard.AllowRequest(1, ECSRequestKind::Slot, Now + 1.0));
	TestTrue(TEXT("other player unaffected"), Guard.AllowRequest(2, ECSRequestKind::Fire, Now + 1.0));

	// A legitimate automatic weapon (~13 shots/s) is never throttled.
	FCSCheatGuard Legit;
	int32 Refused = 0;
	for (int32 i = 0; i < 13 * 30; ++i)
	{
		Refused += Legit.AllowRequest(7, ECSRequestKind::Fire, 50.0 + i / 13.0) ? 0 : 1;
	}
	TestEqual(TEXT("13 shots/s for 30 s: none refused"), Refused, 0);
	TestEqual(TEXT("13 shots/s: no strikes"), Legit.GetStrikes(7), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSUnitFireBudgetTest, "CSFusion.Unit.AntiCheat.FireRate", CSUnitFlags)
bool FCSUnitFireBudgetTest::RunTest(const FString& Parameters)
{
	// Phase 2 (B15): the fire-rate budget. Same arithmetic as ValidateFire / CommitFire.
	auto Fire = [](FCSFireBudget& Budget, bool& bFirst, double Interval, double Now)
	{
		const double Shots = FCSFireBudget::Available(bFirst ? nullptr : &Budget, Interval, 0.2, Now);
		if (Shots < 1.0 - 1.0e-6)
		{
			return false;
		}
		Budget = FCSFireBudget{ FMath::Max(0.0, Shots - 1.0), Now };
		bFirst = false;
		return true;
	};

	// A rifle (0.1 s) at its exact rate; every other packet arrives 80 ms late (gaps of 20 and 180 ms): all shots count.
	{
		FCSFireBudget Budget;
		bool bFirst = true;
		int32 Refused = 0;
		for (int32 i = 0; i < 300; ++i)
		{
			const double Jitter = (i % 2 == 0) ? 0.08 : 0.0; // network delay 0..80 ms
			Refused += Fire(Budget, bFirst, 0.1, 10.0 + i * 0.1 + Jitter) ? 0 : 1;
		}
		TestEqual(TEXT("rifle, packets delayed 0-80 ms: none refused"), Refused, 0);
	}

	// Ten shots in one frame: at most two (the jitter allowance), never ten.
	{
		FCSFireBudget Budget;
		bool bFirst = true;
		int32 Accepted = 0;
		for (int32 i = 0; i < 10; ++i)
		{
			Accepted += Fire(Budget, bFirst, 0.1, 20.0) ? 1 : 0;
		}
		TestEqual(TEXT("10 shots in one frame"), Accepted, 2);
	}

	// A rate hack at 2x for 10 s gets no more than the weapon rate (+ the burst).
	{
		FCSFireBudget Budget;
		bool bFirst = true;
		int32 Accepted = 0;
		for (int32 i = 0; i < 200; ++i)
		{
			Accepted += Fire(Budget, bFirst, 0.1, 30.0 + i * 0.05) ? 1 : 0;
		}
		TestTrue(TEXT("2x rate hack capped at the weapon rate"), Accepted <= 102);
	}

	// A sniper rifle (1.5 s) cannot double-tap, but 150 ms of jitter is fine.
	{
		FCSFireBudget Budget;
		bool bFirst = true;
		TestTrue(TEXT("sniper first shot"), Fire(Budget, bFirst, 1.5, 40.0));
		TestFalse(TEXT("sniper double tap"), Fire(Budget, bFirst, 1.5, 40.3));
		TestTrue(TEXT("sniper shot 150 ms early"), Fire(Budget, bFirst, 1.5, 41.35));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSUnitShotModelTest, "CSFusion.Unit.Weapon.ShotModel", CSUnitFlags)
bool FCSUnitShotModelTest::RunTest(const FString& Parameters)
{
	// Phase 3 (B8, C6): the authority's shot model and the light hitboxes.
	UCSWeaponDefinition* Rifle = NewObject<UCSWeaponDefinition>();
	Rifle->RoundsPerMinute = 600.f;
	Rifle->RecoilPitch = 1.2f;
	Rifle->RecoilYaw = 0.35f;
	Rifle->RecoilPatternShots = 9;
	Rifle->RecoilRecoverySeconds = 0.45f;
	Rifle->HipSpreadDegrees = 2.2f;
	Rifle->AimSpreadDegrees = 0.35f;
	Rifle->SpreadPerShot = 0.6f;
	Rifle->MaxBloomSpreadDegrees = 5.f;
	Rifle->MoveSpreadDegrees = 4.5f;
	Rifle->JumpSpreadDegrees = 9.f;

	// A spray at the fire rate: the series counts shots, the kick climbs to the top of the pattern.
	FCSSprayState Spray;
	double Now = 100.0;
	TArray<FRotator> Kicks;
	for (int32 i = 0; i < 12; ++i)
	{
		const float Series = CSShotModel::SeriesAt(Spray, *Rifle, Now);
		TestTrue(FString::Printf(TEXT("series counts shots (%d)"), i), FMath::IsNearlyEqual(Series, static_cast<float>(FMath::Min(i, 13)), 0.01f));
		Kicks.Add(CSShotModel::RecoilAt(*Rifle, Series));
		CSShotModel::CommitShot(Spray, *Rifle, Now);
		Now += Rifle->GetFireInterval();
	}
	TestTrue(TEXT("first shot: no kick"), Kicks[0].IsNearlyZero());
	TestTrue(TEXT("the pattern climbs"), Kicks[1].Pitch > 0.f && Kicks[5].Pitch > Kicks[1].Pitch);
	TestTrue(TEXT("the climb stops at the top"), FMath::IsNearlyEqual(Kicks[11].Pitch, 1.2f * 9.f, 0.01f));
	TestTrue(TEXT("the pattern sways sideways"), FMath::Abs(Kicks[6].Yaw) > 0.05f);

	// The same spray gives the same pattern: deterministic.
	FCSSprayState Again;
	double Then = 100.0;
	bool bSame = true;
	for (int32 i = 0; i < 12; ++i)
	{
		bSame &= CSShotModel::RecoilAt(*Rifle, CSShotModel::SeriesAt(Again, *Rifle, Then)).Equals(Kicks[i], 0.0001f);
		CSShotModel::CommitShot(Again, *Rifle, Then);
		Then += Rifle->GetFireInterval();
	}
	TestTrue(TEXT("deterministic pattern"), bSame);

	// Released: held for 1.25 intervals, then gone after the recovery time.
	const double Last = Now - Rifle->GetFireInterval();
	TestTrue(TEXT("held right after the last shot"), CSShotModel::SeriesAt(Spray, *Rifle, Last + 0.1) >= 11.99f);
	TestTrue(TEXT("recovering"), CSShotModel::SeriesAt(Spray, *Rifle, Last + 0.3) < 12.f);
	TestEqual(TEXT("recovered"), CSShotModel::SeriesAt(Spray, *Rifle, Last + 0.125 + 0.45 * 13.f / 9.f + 0.01), 0.f);

	// Spread: aimed and still vs running from the hip vs in the air; bloom capped.
	FCSShooterState Still;
	Still.bAimed = true;
	TestTrue(TEXT("aimed, still, first shot"), FMath::IsNearlyEqual(CSShotModel::SpreadDegrees(*Rifle, Still, 0.f), 0.35f, 0.001f));
	FCSShooterState Running;
	Running.SpeedRatio = CSShotModel::SpeedRatioFor(620.f);
	TestTrue(TEXT("hip fire at a run"), FMath::IsNearlyEqual(CSShotModel::SpreadDegrees(*Rifle, Running, 0.f), 2.2f + 4.5f, 0.001f));
	TestEqual(TEXT("a slow walk is free"), CSShotModel::SpeedRatioFor(140.f), 0.f);
	FCSShooterState Air;
	Air.bAirborne = true;
	TestTrue(TEXT("in the air"), FMath::IsNearlyEqual(CSShotModel::SpreadDegrees(*Rifle, Air, 0.f), 2.2f + 9.f, 0.001f));
	TestTrue(TEXT("bloom capped"), FMath::IsNearlyEqual(CSShotModel::SpreadDegrees(*Rifle, FCSShooterState(), 30.f), 2.2f + 5.f, 0.001f));

	// Hitboxes (C6): a body with its feet at the origin, facing +X, shot along +X.
	auto Shoot = [](bool bCrouched, float Y, float Z, ECSHitZone& Zone)
	{
		float Distance = 0.f;
		return CSShotModel::TraceHitboxes(FVector::ZeroVector, FVector(-1.f, 0.f, 0.f), bCrouched,
			FVector(-500.f, Y, Z), FVector(1.f, 0.f, 0.f), 10000.f, Zone, Distance);
	};
	ECSHitZone Zone = ECSHitZone::None;
	TestTrue(TEXT("standing: head"), Shoot(false, 0.f, 164.f, Zone) && Zone == ECSHitZone::Head);
	TestTrue(TEXT("standing: torso"), Shoot(false, 0.f, 120.f, Zone) && Zone == ECSHitZone::Torso);
	TestTrue(TEXT("standing: legs"), Shoot(false, 0.f, 50.f, Zone) && Zone == ECSHitZone::Limb);
	TestFalse(TEXT("beside the chest (inside the old capsule): miss"), Shoot(false, 25.f, 120.f, Zone));
	TestFalse(TEXT("over the head: miss"), Shoot(false, 0.f, 182.f, Zone));
	TestFalse(TEXT("crouched: standing head height is empty"), Shoot(true, 0.f, 164.f, Zone));
	TestTrue(TEXT("crouched: head"), Shoot(true, 0.f, 111.f, Zone) && Zone == ECSHitZone::Head);
	float Straight = -1.f;
	TestTrue(TEXT("ray-sphere distance"), FMath::IsNearlyEqual(
		Straight = CSShotModel::RayCapsule(FVector(-100.f, 0.f, 0.f), FVector(1.f, 0.f, 0.f), FVector::ZeroVector, FVector::ZeroVector, 10.f), 90.f, 0.01f));
	return true;
}


// ---------------------------------------------------------------------------
// Audio mixing (v2.0 phase 3, AUDIT C9)
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSUnitAudioMixingTest, "CSFusion.Unit.Audio.Mixing", CSUnitFlags)
bool FCSUnitAudioMixingTest::RunTest(const FString& Parameters)
{
	const UCSAudioSettings* Audio = UCSAudioSettings::Get();

	// Each surface has its own complete footstep set; unknown surfaces play stone.
	const auto AllLoad = [this](const TCHAR* What, const TArray<TSoftObjectPtr<USoundBase>>& Set)
	{
		TestEqual(FString::Printf(TEXT("%s: four steps"), What), Set.Num(), 4);
		for (const TSoftObjectPtr<USoundBase>& Step : Set)
		{
			TestNotNull(FString::Printf(TEXT("%s: %s loads"), What, *Step.ToString()), Step.LoadSynchronous());
		}
	};
	AllLoad(TEXT("stone"), Audio->Footsteps);
	AllLoad(TEXT("metal"), Audio->FootstepsMetal);
	AllLoad(TEXT("wood"), Audio->FootstepsWood);
	AllLoad(TEXT("dirt"), Audio->FootstepsDirt);
	TestTrue(TEXT("metal set"), &CSAudio::FootstepsFor(CSSurface::Metal) == &Audio->FootstepsMetal);
	TestTrue(TEXT("wood set"), &CSAudio::FootstepsFor(CSSurface::Wood) == &Audio->FootstepsWood);
	TestTrue(TEXT("dirt set"), &CSAudio::FootstepsFor(CSSurface::Dirt) == &Audio->FootstepsDirt);
	TestTrue(TEXT("default is stone"), &CSAudio::FootstepsFor(SurfaceType_Default) == &Audio->Footsteps);
	TestTrue(TEXT("unmapped is stone"), &CSAudio::FootstepsFor(SurfaceType10) == &Audio->Footsteps);

	// Every kind of 3D sound has attenuation with occlusion on the channel
	// characters ignore, and a voice limit.
	const TSoftObjectPtr<USoundAttenuation>* Attenuations[] = {
		&Audio->WeaponAttenuation, &Audio->FootstepAttenuation, &Audio->ImpactAttenuation, &Audio->ExplosionAttenuation };
	for (const TSoftObjectPtr<USoundAttenuation>* Soft : Attenuations)
	{
		const USoundAttenuation* Asset = Soft->LoadSynchronous();
		if (TestNotNull(FString::Printf(TEXT("%s loads"), *Soft->ToString()), Asset))
		{
			TestTrue(TEXT("occlusion on"), Asset->Attenuation.bEnableOcclusion);
			TestEqual(TEXT("occlusion channel"), Asset->Attenuation.OcclusionTraceChannel.GetValue(), CSCollision::AudioOcclusion);
		}
	}
	const TSoftObjectPtr<USoundConcurrency>* Limits[] = {
		&Audio->WeaponConcurrency, &Audio->FootstepConcurrency, &Audio->ImpactConcurrency, &Audio->ExplosionConcurrency };
	for (const TSoftObjectPtr<USoundConcurrency>* Soft : Limits)
	{
		const USoundConcurrency* Asset = Soft->LoadSynchronous();
		if (TestNotNull(FString::Printf(TEXT("%s loads"), *Soft->ToString()), Asset))
		{
			TestTrue(TEXT("voice limit set"), Asset->Concurrency.MaxCount > 0 && Asset->Concurrency.MaxCount <= 16);
		}
	}

	// Players never occlude sounds; the world does.
	const ACSCharacter* Character = GetDefault<ACSCharacter>();
	TestEqual(TEXT("capsule ignores occlusion"),
		Character->GetCapsuleComponent()->GetCollisionResponseToChannel(CSCollision::AudioOcclusion), ECR_Ignore);
	TestEqual(TEXT("capsule still blocks shots"),
		Character->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Visibility), ECR_Block);

	// Physical materials carry the surfaces the footsteps key on.
	const TPair<const TCHAR*, EPhysicalSurface> Surfaces[] = {
		{ TEXT("/Game/Environment/Physics/PM_Metal.PM_Metal"), CSSurface::Metal },
		{ TEXT("/Game/Environment/Physics/PM_Wood.PM_Wood"), CSSurface::Wood },
		{ TEXT("/Game/Environment/Physics/PM_Dirt.PM_Dirt"), CSSurface::Dirt } };
	for (const TPair<const TCHAR*, EPhysicalSurface>& Surface : Surfaces)
	{
		const UPhysicalMaterial* Material = LoadObject<UPhysicalMaterial>(nullptr, Surface.Key);
		if (TestNotNull(FString::Printf(TEXT("%s loads"), Surface.Key), Material))
		{
			TestEqual(TEXT("surface type"), UPhysicalMaterial::DetermineSurfaceType(Material), Surface.Value);
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
