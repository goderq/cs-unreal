// CS-Fusion: sign in with an Epic (EOS) account.
//
// The game sends the EOS access token it got from the Epic login. This
// function asks Epic whether that token is real, and only then creates or
// finds the profile and hands back a short-lived Supabase JWT. The game never
// holds the service key, so a modified client cannot write other people's
// rows - row level security in schema.sql decides what the returned JWT may do.
//
// Deploy:
//   supabase functions deploy eos-login --no-verify-jwt
// Secrets (Dashboard -> Edge Functions -> Secrets, or `supabase secrets set`):
//   EOS_CLIENT_ID, EOS_CLIENT_SECRET   from the Epic Dev Portal client
//   SUPABASE_JWT_SECRET                Project Settings -> API -> JWT Secret
//   SUPABASE_URL, SUPABASE_SERVICE_ROLE_KEY are provided automatically.

import { createClient } from "https://esm.sh/@supabase/supabase-js@2";
import { create, getNumericDate } from "https://deno.land/x/djwt@v3.0.2/mod.ts";

const EPIC_TOKEN_INFO = "https://api.epicgames.dev/epic/oauth/v2/tokenInfo";
const TOKEN_LIFETIME_HOURS = 12;

const json = (status: number, body: unknown) =>
    new Response(JSON.stringify(body), { status, headers: { "content-type": "application/json" } });

/** Nickname from the Epic display name: trimmed, safe characters, 3..20 chars. */
function cleanNickname(raw: string): string {
    const base = (raw ?? "").normalize("NFKC").replace(/[^\p{L}\p{N}_\- ]/gu, "").trim().slice(0, 20);
    return base.length >= 3 ? base : "Player";
}

async function verifyEpicToken(token: string) {
    const clientId = Deno.env.get("EOS_CLIENT_ID") ?? "";
    const clientSecret = Deno.env.get("EOS_CLIENT_SECRET") ?? "";
    if (!clientId || !clientSecret) {
        throw new Error("EOS_CLIENT_ID / EOS_CLIENT_SECRET are not set");
    }
    const basic = btoa(`${clientId}:${clientSecret}`);
    const response = await fetch(EPIC_TOKEN_INFO, {
        method: "POST",
        headers: {
            authorization: `Basic ${basic}`,
            "content-type": "application/x-www-form-urlencoded",
        },
        body: new URLSearchParams({ token }),
    });
    if (!response.ok) {
        throw new Error(`Epic rejected the token (${response.status})`);
    }
    const info = await response.json();
    // Epic answers with the account the token belongs to and which client it
    // was issued for. Both have to match ours, or someone is replaying a token
    // from a different game.
    if (info.active === false || !info.account_id) {
        throw new Error("token is not active");
    }
    if (info.client_id && info.client_id !== clientId) {
        throw new Error("token was issued for a different client");
    }
    return { accountId: String(info.account_id), displayName: String(info.display_name ?? "") };
}

Deno.serve(async (request) => {
    if (request.method !== "POST") {
        return json(405, { error: "POST only" });
    }
    try {
        const { epic_token, display_name } = await request.json();
        if (!epic_token || typeof epic_token !== "string") {
            return json(400, { error: "epic_token is required" });
        }

        const epic = await verifyEpicToken(epic_token);
        const admin = createClient(
            Deno.env.get("SUPABASE_URL")!,
            Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")!,
            { auth: { persistSession: false } },
        );

        // Existing profile?
        const { data: existing, error: readError } = await admin
            .from("profiles")
            .select("id, nickname, banned_until")
            .eq("epic_account_id", epic.accountId)
            .maybeSingle();
        if (readError) {
            return json(500, { error: readError.message });
        }

        let profile = existing;
        if (!profile) {
            // New player: take the Epic display name, add a suffix if taken.
            const wanted = cleanNickname(epic.displayName || display_name || "");
            for (let attempt = 0; attempt < 6 && !profile; attempt++) {
                const nickname = attempt === 0 ? wanted : `${wanted.slice(0, 15)}#${Math.floor(1000 + Math.random() * 9000)}`;
                const { data, error } = await admin
                    .from("profiles")
                    .insert({ epic_account_id: epic.accountId, nickname })
                    .select("id, nickname, banned_until")
                    .single();
                if (!error) {
                    profile = data;
                    await admin.from("player_stats").insert({ profile_id: data.id });
                } else if (!String(error.message).includes("duplicate")) {
                    return json(500, { error: error.message });
                }
            }
            if (!profile) {
                return json(500, { error: "could not allocate a nickname" });
            }
        } else {
            await admin.from("profiles").update({ last_seen_at: new Date().toISOString() }).eq("id", profile.id);
        }

        if (profile.banned_until && new Date(profile.banned_until) > new Date()) {
            return json(403, { error: "banned", banned_until: profile.banned_until });
        }

        const { data: stats } = await admin
            .from("player_stats")
            .select("matches, wins, kills, deaths, headshots, playtime_seconds")
            .eq("profile_id", profile.id)
            .maybeSingle();

        // Short-lived Supabase JWT: the game talks to PostgREST as this profile.
        const secret = Deno.env.get("SUPABASE_JWT_SECRET")!;
        const key = await crypto.subtle.importKey(
            "raw", new TextEncoder().encode(secret),
            { name: "HMAC", hash: "SHA-256" }, false, ["sign", "verify"],
        );
        const token = await create(
            { alg: "HS256", typ: "JWT" },
            {
                sub: profile.id,
                role: "authenticated",
                aud: "authenticated",
                epic: epic.accountId,
                exp: getNumericDate(TOKEN_LIFETIME_HOURS * 60 * 60),
                iat: getNumericDate(0),
            },
            key,
        );

        return json(200, {
            token,
            expires_in: TOKEN_LIFETIME_HOURS * 3600,
            profile: { id: profile.id, nickname: profile.nickname },
            stats: stats ?? { matches: 0, wins: 0, kills: 0, deaths: 0, headshots: 0, playtime_seconds: 0 },
        });
    } catch (error) {
        console.error(error);
        return json(401, { error: String(error?.message ?? error) });
    }
});
