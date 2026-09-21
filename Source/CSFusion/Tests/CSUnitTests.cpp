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
#include "Combat/CSCheatGuard.h"
#include "Misc/AutomationTest.h"
#include "Settings/CSSettingsSubsystem.h"
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
	TestTrue(TEXT("headshots ordered"), Easy.HeadshotChance <= Normal.HeadshotChance && Normal.HeadshotChance < Hard.HeadshotChance);
	TestTrue(TEXT("even hard bots are human-like"), Hard.ReactionTime >= 0.15f && Hard.AimErrorDegrees > 0.f && Hard.HeadshotChance < 0.5f);
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
		const double Later = Walk(Guard, 3, Pos, Now, 300.f, FCSCheatGuard::SuspensionSeconds + 0.5);
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

#endif // WITH_DEV_AUTOMATION_TESTS
