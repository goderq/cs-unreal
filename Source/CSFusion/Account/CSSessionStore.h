// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// v2.4: remembers how the player signed in, so the next launch signs them in
// again without a form.
//
//   method "epic"   nothing secret is kept here: the EOS SDK keeps the Epic
//                   refresh token in the system credential store itself.
//   method "email"  the Supabase refresh token (and the email, for the form).
//                   It is encrypted with Windows DPAPI for the current Windows
//                   user, so the file is useless on another PC or account.
//                   Supabase rotates it at every use; a stolen old copy is dead.
//
// The file lives in Saved/Account/session.dat. Sign out deletes it. On other
// platforms nothing secret is written (the player signs in again).

#pragma once

#include "CoreMinimal.h"

struct CSFUSION_API FCSStoredSession
{
	/** "epic" | "email" | "" (nothing stored). */
	FString Method;
	FString Email;
	FString RefreshToken;
};

struct CSFUSION_API FCSSessionStore
{
	static bool Load(FCSStoredSession& Out);
	static bool Save(const FCSStoredSession& Session);
	static void Clear();
	static FString FilePath();
};
