// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// v2.4 self-test of email sign-in against the LIVE backend (compiled out of
// Shipping). One stage per launch, -cstestemailauth=STAGE:
//
//   register   wrong password and a bad recovery code are refused, a reserved
//              nickname is refused, then a new random account registers and
//              signs in; its login token works for the game backend; a
//              Supabase token is not accepted as a login token
//   relaunch   (next launch) signed in automatically with the remembered
//              session, same profile; then SIGN OUT forgets it
//   after      (next launch) nobody is signed in automatically any more
//   link       signed in with Epic (saved session): the test email cannot be
//              linked (it has its own profile), a second new email can
//   linkcheck  (next launch) the second email opens the Epic profile; sign out
//
// The test accounts (cstest-...@example.com) stay in Supabase Auth until they
// are deleted (docs/ACCOUNTS.md, "Test accounts"). State between stages:
// Saved/CSTest/email_auth.json. Every stage ends with the log line
// "EMAIL AUTH TEST RESULT: ... -> EMAIL AUTH OK | EMAIL AUTH BROKEN" and quits.

#pragma once

#include "CoreMinimal.h"

class UCSAccountSubsystem;

namespace CSAuthSelfTest
{
	/** The stage on the command line, or empty (never in Shipping). */
	FString Stage();
	/** Starts the stage (called by the menu controller when the menu level begins). */
	void Start(UCSAccountSubsystem* Account);
}
