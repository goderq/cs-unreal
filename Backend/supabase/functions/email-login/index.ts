// CS-Fusion: sign in with email + password.
//
// The password never reaches this function. The game talks to Supabase Auth
// directly (sign-up, sign-in, refresh tokens, recovery codes; passwords are
// hashed there) and sends only the Supabase access token it got. This
// function:
//   1. asks Supabase Auth whether the access token is real (auth.getUser);
//   2. finds the profile linked to that Auth user, or - on the first sign-in
//      after registration - creates it with the nickname chosen at
//      registration. The nickname is set ONCE; afterwards it changes only
//      through the administration (functions/admin);
//   3. refuses banned players;
//   4. returns the same short-lived login token eos-login returns, so Photon
//      (photon-auth), matches, stats and the admin tools work unchanged.
//
// Actions (body.action):
//   "check_nickname" {nickname}                  is the name valid and free? (the
//                                                registration form asks before sign-up)
//   "login"          {access_token, nickname?}   the sign-in itself
//
// Deploy with "Verify JWT with legacy secret" ON (the game sends the anon key).
// Secrets: CS_JWT_SECRET (plus SUPABASE_URL and SUPABASE_SERVICE_ROLE_KEY,
// provided by Supabase).

import { createClient } from "https://esm.sh/@supabase/supabase-js@2";
import { create, getNumericDate } from "https://deno.land/x/djwt@v3.0.2/mod.ts";

const TOKEN_LIFETIME_SECONDS = 2 * 60 * 60;
const RESERVED = new Set([
    "admin", "administrator", "moderator", "mod", "staff", "support", "system", "server", "official",
    "csfusion", "epic", "epicgames", "root", "owner", "developer", "dev", "gm", "player",
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

/** A nickname typed by the player: the same rules as eos-login, but strict - a
 *  bad name is an error the form shows, never silently changed. */
function validNickname(raw: unknown): string {
    if (typeof raw !== "string") {
        throw new HttpError(400, "nickname_required");
    }
    const name = raw.normalize("NFKC").trim();
    if (name.length < 3 || name.length > 20) {
        throw new HttpError(400, "nickname_length");
    }
    if (!/^[\p{L}\p{N}_\-]+( [\p{L}\p{N}_\-]+)*$/u.test(name)) {
        throw new HttpError(400, "nickname_chars");
    }
    if (isReserved(name)) {
        throw new HttpError(400, "nickname_reserved");
    }
    return name;
}

const PROFILE_COLUMNS = "id, nickname, role, banned_until, epic_account_id";
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
        const db = createClient(Deno.env.get("SUPABASE_URL")!, Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")!,
            { auth: { persistSession: false } });
        const nameTaken = async (name: string) => {
            const { data, error } = await db.from("profiles").select("id").ilike("nickname", name.replace(/[\\%_]/g, "\\$&"))
                .limit(1);
            if (error) {
                throw error;
            }
            return (data ?? []).length > 0;
        };

        if (body.action === "check_nickname") {
            const ip = (request.headers.get("x-forwarded-for") ?? "").split(",")[0].trim() || "unknown";
            const { data: allowed } = await db.rpc("cs_rate_limit",
                { p_key: `nickcheck:${ip}`, p_max: 60, p_window_seconds: 600 });
            if (allowed === false) {
                throw new HttpError(429, "too many requests, wait a few minutes");
            }
            const name = validNickname(body.nickname);
            if (await nameTaken(name)) {
                throw new HttpError(409, "nickname_taken");
            }
            return json(200, { ok: true, nickname: name });
        }
        if (body.action !== "login") {
            throw new HttpError(400, "unknown action");
        }

        const accessToken = typeof body.access_token === "string" ? body.access_token : "";
        if (!accessToken || accessToken.length > 8192) {
            throw new HttpError(400, "access_token is required");
        }
        const { data: auth, error: authError } = await db.auth.getUser(accessToken);
        if (authError || !auth?.user) {
            throw new HttpError(401, "the email session has expired - sign in again");
        }
        const user = auth.user;
        if (!user.email) {
            throw new HttpError(401, "not an email account");
        }

        const { data: allowed, error: limitError } = await db.rpc("cs_rate_limit",
            { p_key: `login:${user.id}`, p_max: 30, p_window_seconds: 600 });
        if (limitError) {
            throw limitError;
        }
        if (!allowed) {
            throw new HttpError(429, "too many sign-ins, wait a few minutes");
        }

        let { data: profile, error: readError } = await db.from("profiles").select(PROFILE_COLUMNS)
            .eq("auth_user_id", user.id).maybeSingle();
        if (readError) {
            throw readError;
        }

        if (!profile) {
            // First sign-in of this email: the nickname from the request, else the one
            // stored at sign-up (user metadata, written by the game at registration).
            const nickname = validNickname(body.nickname ?? user.user_metadata?.nickname);
            if (await nameTaken(nickname)) {
                throw new HttpError(409, "nickname_taken");
            }
            const { data, error } = await db.from("profiles").insert({ auth_user_id: user.id, nickname })
                .select(PROFILE_COLUMNS).single();
            if (error) {
                if (error.code !== "23505") {
                    throw error;
                }
                // A parallel first sign-in created it - or the nickname is taken.
                const again = await db.from("profiles").select(PROFILE_COLUMNS)
                    .eq("auth_user_id", user.id).maybeSingle();
                if (!again.data) {
                    throw new HttpError(409, "nickname_taken");
                }
                profile = again.data;
            } else {
                profile = data;
                await db.from("player_stats").upsert({ profile_id: data.id },
                    { onConflict: "profile_id", ignoreDuplicates: true });
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
                is_admin: role === "admin" || role === "superadmin",
                is_staff: role !== "player",
            },
            stats: stats ?? {},
            method: "email",
            linked: { epic: !!profile.epic_account_id, email: true, email_address: user.email },
        });
    } catch (error) {
        if (error instanceof HttpError) {
            return json(error.status, { error: error.message, ...error.extra });
        }
        console.error(error);
        return json(500, { error: "server error" });
    }
});
