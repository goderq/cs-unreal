// CS-Fusion: AdminService - every administration action goes through here.
//
//   game (admin panel) -> this function -> login token checked -> SQL function
//   (migration 006): role + permission + target rank -> change -> security_log
//
// The caller is whoever the login token (from eos-login) says, and nothing the
// client sends changes that: the actor id passed to the database is the
// token's subject. Roles and bans are read from the database on every call,
// so taking a role away or banning a staff member works at once. Hiding the
// admin page in the game is only cosmetics; this is the check.
//
// POST { "action": "...", ... } ->
//   200 { "result": ... }    403 forbidden    404 not found
//   400 bad input            409 conflict      429 too many requests
//
// Deploy with "Verify JWT with legacy secret" ON. Secrets: CS_JWT_SECRET (plus
// SUPABASE_URL and SUPABASE_SERVICE_ROLE_KEY, provided by Supabase).

import { createClient } from "https://esm.sh/@supabase/supabase-js@2";
import { verify } from "https://deno.land/x/djwt@v3.0.2/mod.ts";

class HttpError extends Error {
    constructor(public status: number, message: string) {
        super(message);
    }
}

const json = (status: number, body: unknown) =>
    new Response(JSON.stringify(body), { status, headers: { "content-type": "application/json" } });

const UUID = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;

function uuid(value: unknown, name: string): string {
    if (typeof value !== "string" || !UUID.test(value)) {
        throw new HttpError(400, `${name} is required`);
    }
    return value;
}

function text(value: unknown, max: number): string {
    return typeof value === "string" ? value.slice(0, max) : "";
}

function int(value: unknown, fallback: number): number {
    const n = Number(value);
    return Number.isFinite(n) ? Math.trunc(n) : fallback;
}

/** Letters of any script, digits, space, _ - #; 3..20. The database adds its own checks. */
function nickname(value: unknown): string {
    const name = text(value, 64).normalize("NFKC").replace(/\s+/g, " ").trim();
    if (!/^[\p{L}\p{N}_\- #]{3,20}$/u.test(name)) {
        throw new HttpError(400, "a nickname has 3 to 20 letters, digits, spaces, _ - or #");
    }
    return name;
}

async function callerProfileId(request: Request): Promise<string> {
    const header = request.headers.get("authorization") ?? "";
    const token = header.toLowerCase().startsWith("bearer ") ? header.slice(7) : "";
    if (!token) {
        throw new HttpError(401, "sign in first");
    }
    const key = await crypto.subtle.importKey("raw", new TextEncoder().encode(Deno.env.get("CS_JWT_SECRET")!),
        { name: "HMAC", hash: "SHA-256" }, false, ["sign", "verify"]);
    let payload: Record<string, unknown>;
    try {
        payload = await verify(token, key) as Record<string, unknown>;
    } catch {
        throw new HttpError(401, "the login has expired - sign in again");
    }
    // Only a player login token; the anon key and service keys carry no subject.
    if (payload.role !== "authenticated" || payload.aud !== "authenticated" || typeof payload.sub !== "string"
        || !UUID.test(payload.sub)) {
        throw new HttpError(401, "not a player login token");
    }
    return payload.sub;
}

type Call = [fn: string, args: Record<string, unknown>];

const ACTIONS: Record<string, (actor: string, b: Record<string, unknown>) => Call> = {
    whoami: (actor) => ["admin_whoami", { p_actor: actor }],
    players: (actor, b) => ["admin_players_search", { p_actor: actor, p_query: text(b.query, 64), p_limit: int(b.limit, 50) }],
    player: (actor, b) => ["admin_player_get", { p_actor: actor, p_target: uuid(b.profile_id, "profile_id") }],
    ban: (actor, b) => ["admin_ban", {
        p_actor: actor, p_target: uuid(b.profile_id, "profile_id"), p_hours: int(b.hours, 0), p_reason: text(b.reason, 300),
    }],
    unban: (actor, b) => ["admin_unban", { p_actor: actor, p_target: uuid(b.profile_id, "profile_id"), p_reason: text(b.reason, 300) }],
    kick: (actor, b) => ["admin_kick", {
        p_actor: actor, p_target: uuid(b.profile_id, "profile_id"), p_minutes: int(b.minutes, 10), p_reason: text(b.reason, 300),
    }],
    set_role: (actor, b) => ["admin_set_role", {
        p_actor: actor, p_target: uuid(b.profile_id, "profile_id"), p_role: text(b.role, 16), p_reason: text(b.reason, 300),
    }],
    reset_nickname: (actor, b) => ["admin_reset_nickname", {
        p_actor: actor, p_target: uuid(b.profile_id, "profile_id"), p_reason: text(b.reason, 300),
    }],
    set_nickname: (actor, b) => ["admin_set_nickname", {
        p_actor: actor, p_target: uuid(b.profile_id, "profile_id"), p_nickname: nickname(b.nickname), p_reason: text(b.reason, 300),
    }],
    reset_stats: (actor, b) => ["admin_reset_stats", { p_actor: actor, p_target: uuid(b.profile_id, "profile_id"), p_reason: text(b.reason, 300) }],
    matches: (actor, b) => ["admin_matches_list", { p_actor: actor, p_filter: text(b.filter, 16) || "all", p_limit: int(b.limit, 50) }],
    match: (actor, b) => ["admin_match_get", { p_actor: actor, p_match: uuid(b.match_id, "match_id") }],
    match_flag: (actor, b) => ["admin_match_flag", { p_actor: actor, p_match: uuid(b.match_id, "match_id"), p_reason: text(b.reason, 300) }],
    match_void: (actor, b) => ["admin_match_void", { p_actor: actor, p_match: uuid(b.match_id, "match_id"), p_reason: text(b.reason, 300) }],
    match_approve: (actor, b) => ["admin_match_approve", { p_actor: actor, p_match: uuid(b.match_id, "match_id"), p_reason: text(b.reason, 300) }],
    log: (actor, b) => ["admin_security_log", {
        p_actor: actor, p_limit: int(b.limit, 100), p_target: b.profile_id ? uuid(b.profile_id, "profile_id") : null,
    }],
};

/** SQLSTATE from the database -> HTTP status. */
function statusFor(code: string | undefined, hint: string | undefined): number {
    switch (code) {
        case "42501": return 403;
        case "P0002": return 404;
        case "22023": case "22P02": case "23514": case "22001": return 400;
        case "23505": return 409;
        case "P0001": return hint === "rate_limited" ? 429 : 400;
        default: return 500;
    }
}

Deno.serve(async (request) => {
    if (request.method !== "POST") {
        return json(405, { error: "POST only" });
    }
    try {
        const actor = await callerProfileId(request);
        let body: Record<string, unknown>;
        try {
            body = await request.json();
        } catch {
            throw new HttpError(400, "the request is not JSON");
        }
        const build = ACTIONS[String(body.action ?? "")];
        if (!build) {
            throw new HttpError(400, "unknown action");
        }
        const [fn, args] = build(actor, body);

        const db = createClient(Deno.env.get("SUPABASE_URL")!, Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")!,
            { auth: { persistSession: false } });
        const { data: allowed, error: limitError } = await db.rpc("cs_rate_limit",
            { p_key: `admin:${actor}`, p_max: 120, p_window_seconds: 60 });
        if (limitError) {
            throw limitError;
        }
        if (!allowed) {
            throw new HttpError(429, "too many requests");
        }

        const { data, error } = await db.rpc(fn, args);
        if (error) {
            const status = statusFor(error.code, error.hint);
            if (status === 500) {
                console.error(fn, error);
                return json(500, { error: "server error" });
            }
            return json(status, { error: error.message, code: error.code });
        }
        return json(200, { result: data });
    } catch (error) {
        if (error instanceof HttpError) {
            return json(error.status, { error: error.message });
        }
        console.error(error);
        return json(500, { error: "server error" });
    }
});
