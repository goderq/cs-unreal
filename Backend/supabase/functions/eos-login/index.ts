// CS-Fusion: sign in with an Epic (EOS) account.
//
// The game sends the access token it got from Epic (first launch: the Epic
// account portal; later launches: EOS persistent auth, no window). This
// function:
//   1. asks Epic whether the token is real, still active and issued to OUR
//      game client - a token from another game is refused;
//   2. finds the profile, or creates it with the display name read from
//      Epic's own API. The client never chooses its name: a nickname changes
//      afterwards only through the administration (functions/admin);
//   3. refuses banned players;
//   4. returns a short-lived login token (2 h). The game renews it silently
//      by calling this function again with its current Epic token.
//
// Deploy with "Verify JWT with legacy secret" ON (the game sends the anon key).
// Secrets:
//   EOS_CLIENT_ID, EOS_CLIENT_SECRET  the EOS client this function uses to ask
//                                     Epic (ideally a backend-only client with
//                                     a Trusted Server policy, docs/ACCOUNTS.md)
//   EOS_GAME_CLIENT_ID                the game's client id, when it differs
//                                     from EOS_CLIENT_ID
//   CS_JWT_SECRET                     the Legacy JWT secret (signs login tokens)
//   SUPABASE_URL, SUPABASE_SERVICE_ROLE_KEY are provided by Supabase.

import { createClient } from "https://esm.sh/@supabase/supabase-js@2";
import { create, getNumericDate } from "https://deno.land/x/djwt@v3.0.2/mod.ts";

const EPIC = "https://api.epicgames.dev";
const TOKEN_LIFETIME_SECONDS = 2 * 60 * 60;
const RESERVED = new Set([
    "admin", "administrator", "moderator", "mod", "staff", "support", "system", "server", "official",
    "csfusion", "epic", "epicgames", "root", "owner", "developer", "dev", "gm",
]);

class HttpError extends Error {
    constructor(public status: number, message: string, public extra: Record<string, unknown> = {}) {
        super(message);
    }
}

const json = (status: number, body: unknown) =>
    new Response(JSON.stringify(body), { status, headers: { "content-type": "application/json" } });

function isReserved(name: string): boolean {
    const lower = name.toLowerCase();
    return RESERVED.has(lower.replace(/[^a-z0-9]/g, "")) || lower.startsWith("admin") || lower.startsWith("moderator");
}

/** Epic display name -> nickname: safe characters, 3..20, nothing reserved. */
function cleanNickname(raw: string): string {
    const base = (raw ?? "").normalize("NFKC").replace(/[^\p{L}\p{N}_\- ]/gu, "").replace(/\s+/g, " ").trim().slice(0, 20);
    return base.length >= 3 && !isReserved(base) ? base : "Player";
}

async function verifyEpicToken(token: string): Promise<string> {
    const clientId = Deno.env.get("EOS_CLIENT_ID") ?? "";
    const clientSecret = Deno.env.get("EOS_CLIENT_SECRET") ?? "";
    const gameClientId = Deno.env.get("EOS_GAME_CLIENT_ID") || clientId;
    if (!clientId || !clientSecret) {
        throw new HttpError(500, "the server is not configured for Epic sign-in");
    }
    const response = await fetch(`${EPIC}/epic/oauth/v2/tokenInfo`, {
        method: "POST",
        headers: {
            authorization: `Basic ${btoa(`${clientId}:${clientSecret}`)}`,
            "content-type": "application/x-www-form-urlencoded",
        },
        body: new URLSearchParams({ token }),
    });
    if (!response.ok) {
        throw new HttpError(401, `Epic rejected the token (${response.status})`);
    }
    const info = await response.json();
    if (info.active !== true || !info.account_id) {
        throw new HttpError(401, "the Epic session has expired - sign in again");
    }
    // Strict: a token issued to any other client (another game) is replayed.
    if (info.client_id !== gameClientId) {
        throw new HttpError(401, "the Epic token was issued to a different game");
    }
    return String(info.account_id);
}

/** The display name, straight from Epic. Empty when Epic does not answer. */
async function epicDisplayName(token: string, accountId: string): Promise<string> {
    try {
        const response = await fetch(`${EPIC}/epic/id/v2/accounts?accountId=${encodeURIComponent(accountId)}`, {
            headers: { authorization: `Bearer ${token}` },
        });
        if (!response.ok) {
            return "";
        }
        const list = await response.json();
        const me = Array.isArray(list) ? list.find((a) => a?.accountId === accountId) : null;
        return typeof me?.displayName === "string" ? me.displayName : "";
    } catch {
        return "";
    }
}

const PROFILE_COLUMNS = "id, nickname, role, banned_until";
const STATS_COLUMNS = "matches, wins, kills, deaths, headshots, playtime_seconds, " +
    "practice_matches, practice_kills, practice_deaths, practice_headshots, practice_playtime_seconds";

Deno.serve(async (request) => {
    if (request.method !== "POST") {
        return json(405, { error: "POST only" });
    }
    try {
        let body: Record<string, unknown>;
        try {
            body = await request.json();
        } catch {
            throw new HttpError(400, "the request is not JSON");
        }
        const epicToken = typeof body.epic_token === "string" ? body.epic_token : "";
        if (!epicToken || epicToken.length > 8192) {
            throw new HttpError(400, "epic_token is required");
        }

        const accountId = await verifyEpicToken(epicToken);
        const db = createClient(Deno.env.get("SUPABASE_URL")!, Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")!,
            { auth: { persistSession: false } });

        const { data: allowed, error: limitError } = await db.rpc("cs_rate_limit",
            { p_key: `login:${accountId}`, p_max: 30, p_window_seconds: 600 });
        if (limitError) {
            throw limitError;
        }
        if (!allowed) {
            throw new HttpError(429, "too many sign-ins, wait a few minutes");
        }

        let { data: profile, error: readError } = await db.from("profiles").select(PROFILE_COLUMNS)
            .eq("epic_account_id", accountId).maybeSingle();
        if (readError) {
            throw readError;
        }

        if (!profile) {
            const wanted = cleanNickname(await epicDisplayName(epicToken, accountId));
            for (let attempt = 0; attempt < 8 && !profile; attempt++) {
                const nickname = attempt === 0 && wanted !== "Player"
                    ? wanted
                    : `${wanted.slice(0, 13)}#${Math.floor(1000 + Math.random() * 9000)}`;
                const { data, error } = await db.from("profiles")
                    .insert({ epic_account_id: accountId, nickname })
                    .select(PROFILE_COLUMNS).single();
                if (!error) {
                    profile = data;
                    await db.from("player_stats").upsert({ profile_id: data.id },
                        { onConflict: "profile_id", ignoreDuplicates: true });
                    break;
                }
                if (error.code !== "23505") {
                    throw error;
                }
                // Taken name - or a first sign-in running in parallel created the profile.
                const again = await db.from("profiles").select(PROFILE_COLUMNS)
                    .eq("epic_account_id", accountId).maybeSingle();
                profile = again.data;
            }
            if (!profile) {
                throw new HttpError(500, "could not create the profile");
            }
        }

        if (profile.banned_until && new Date(profile.banned_until) > new Date()) {
            throw new HttpError(403, "banned", { banned_until: profile.banned_until });
        }

        await db.from("profiles").update({ last_seen_at: new Date().toISOString() }).eq("id", profile.id);
        const { data: stats } = await db.from("player_stats").select(STATS_COLUMNS)
            .eq("profile_id", profile.id).maybeSingle();

        const key = await crypto.subtle.importKey("raw", new TextEncoder().encode(Deno.env.get("CS_JWT_SECRET")!),
            { name: "HMAC", hash: "SHA-256" }, false, ["sign", "verify"]);
        const token = await create({ alg: "HS256", typ: "JWT" }, {
            sub: profile.id,
            role: "authenticated",
            aud: "authenticated",
            iss: "cs-fusion",
            epic: accountId,
            iat: getNumericDate(0),
            exp: getNumericDate(TOKEN_LIFETIME_SECONDS),
        }, key);

        const role = String(profile.role ?? "player");
        return json(200, {
            token,
            expires_in: TOKEN_LIFETIME_SECONDS,
            profile: {
                id: profile.id,
                nickname: profile.nickname,
                role,
                // For v1.2 clients; the admin functions never trust it.
                is_admin: role === "admin" || role === "superadmin",
                is_staff: role !== "player",
            },
            stats: stats ?? {},
        });
    } catch (error) {
        if (error instanceof HttpError) {
            return json(error.status, { error: error.message, ...error.extra });
        }
        console.error(error);
        return json(500, { error: "server error" });
    }
});
