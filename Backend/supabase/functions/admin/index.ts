// CS-Fusion: admin actions from the in-game admin panel.
//
// The caller proves who they are with the login token from eos-login (same
// check as report-match). Only a profile with is_admin = true may do
// anything here; the check runs against the database on every call, so
// revoking admin takes effect immediately. Every action is written to
// admin_log so there is a trail of who did what.
//
// Actions (POST JSON { action, ... }):
//   players      { query?, limit? }             list/search profiles with stats
//   ban          { profile_id, hours }          hours <= 0 means permanent
//   unban        { profile_id }
//   set_admin    { profile_id, is_admin }       an admin cannot demote themselves
//   rename       { profile_id, nickname }       3..20 characters, unique
//   reset_stats  { profile_id }
//   log          { limit? }                     the latest admin actions
//
// Deploy with "Verify JWT with legacy secret" on (the dashboard default).
// Secrets: CS_JWT_SECRET (plus the automatic SUPABASE_URL and
// SUPABASE_SERVICE_ROLE_KEY).

import { createClient } from "https://esm.sh/@supabase/supabase-js@2";
import { verify } from "https://deno.land/x/djwt@v3.0.2/mod.ts";

const json = (status: number, body: unknown) =>
    new Response(JSON.stringify(body), { status, headers: { "content-type": "application/json" } });

async function callerProfileId(request: Request): Promise<string> {
    const header = request.headers.get("authorization") ?? "";
    const token = header.toLowerCase().startsWith("bearer ") ? header.slice(7) : "";
    if (!token) {
        throw new Error("missing login token");
    }
    const key = await crypto.subtle.importKey(
        "raw", new TextEncoder().encode(Deno.env.get("CS_JWT_SECRET")!),
        { name: "HMAC", hash: "SHA-256" }, false, ["sign", "verify"],
    );
    const payload = await verify(token, key) as Record<string, unknown>;
    if (!payload.sub) {
        throw new Error("token has no subject");
    }
    return String(payload.sub);
}

const isUuid = (value: unknown) =>
    typeof value === "string" && /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(value);

Deno.serve(async (request) => {
    if (request.method !== "POST") {
        return json(405, { error: "POST only" });
    }
    try {
        const caller = await callerProfileId(request);
        const body = await request.json();
        const action = String(body.action ?? "");

        const admin = createClient(
            Deno.env.get("SUPABASE_URL")!,
            Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")!,
            { auth: { persistSession: false } },
        );

        // Admin right now, according to the database - not to the token.
        const { data: me, error: meError } = await admin
            .from("profiles").select("id, nickname, is_admin").eq("id", caller).maybeSingle();
        if (meError) {
            return json(500, { error: meError.message });
        }
        if (!me?.is_admin) {
            return json(403, { error: "not an admin" });
        }

        const audit = async (target: string | null, details: Record<string, unknown>) => {
            await admin.from("admin_log").insert({ admin_id: caller, action, target_id: target, details });
        };

        switch (action) {
        case "players": {
            const limit = Math.min(Math.max(Number(body.limit) || 50, 1), 200);
            let query = admin
                .from("profiles")
                .select("id, nickname, epic_account_id, created_at, last_seen_at, banned_until, is_admin, player_stats(matches, wins, kills, deaths, headshots, playtime_seconds)")
                .order("last_seen_at", { ascending: false })
                .limit(limit);
            const text = String(body.query ?? "").trim();
            if (text) {
                query = query.ilike("nickname", `%${text.replace(/[%_]/g, "")}%`);
            }
            const { data, error } = await query;
            if (error) {
                return json(500, { error: error.message });
            }
            return json(200, { players: data ?? [] });
        }

        case "ban": {
            if (!isUuid(body.profile_id)) {
                return json(400, { error: "profile_id is required" });
            }
            if (body.profile_id === caller) {
                return json(400, { error: "you cannot ban yourself" });
            }
            const hours = Number(body.hours) || 0;
            // Permanent = far in the future; the game only compares dates.
            const until = hours > 0
                ? new Date(Date.now() + Math.min(hours, 24 * 365 * 10) * 3600 * 1000)
                : new Date("2999-01-01T00:00:00Z");
            const { error } = await admin.from("profiles").update({ banned_until: until.toISOString() }).eq("id", body.profile_id);
            if (error) {
                return json(500, { error: error.message });
            }
            await audit(body.profile_id, { hours, until: until.toISOString() });
            return json(200, { ok: true, banned_until: until.toISOString() });
        }

        case "unban": {
            if (!isUuid(body.profile_id)) {
                return json(400, { error: "profile_id is required" });
            }
            const { error } = await admin.from("profiles").update({ banned_until: null }).eq("id", body.profile_id);
            if (error) {
                return json(500, { error: error.message });
            }
            await audit(body.profile_id, {});
            return json(200, { ok: true });
        }

        case "set_admin": {
            if (!isUuid(body.profile_id)) {
                return json(400, { error: "profile_id is required" });
            }
            const makeAdmin = Boolean(body.is_admin);
            if (body.profile_id === caller && !makeAdmin) {
                return json(400, { error: "you cannot remove your own admin rights" });
            }
            // profiles_guard lets only the service role change is_admin.
            const { error } = await admin.from("profiles").update({ is_admin: makeAdmin }).eq("id", body.profile_id);
            if (error) {
                return json(500, { error: error.message });
            }
            await audit(body.profile_id, { is_admin: makeAdmin });
            return json(200, { ok: true });
        }

        case "rename": {
            if (!isUuid(body.profile_id)) {
                return json(400, { error: "profile_id is required" });
            }
            const nickname = String(body.nickname ?? "").normalize("NFKC").replace(/[^\p{L}\p{N}_\- #]/gu, "").trim();
            if (nickname.length < 3 || nickname.length > 20) {
                return json(400, { error: "the name must be 3 to 20 characters" });
            }
            const { error } = await admin.from("profiles").update({ nickname }).eq("id", body.profile_id);
            if (error) {
                return json(String(error.message).includes("duplicate") ? 409 : 500,
                    { error: String(error.message).includes("duplicate") ? "that name is taken" : error.message });
            }
            await audit(body.profile_id, { nickname });
            return json(200, { ok: true, nickname });
        }

        case "reset_stats": {
            if (!isUuid(body.profile_id)) {
                return json(400, { error: "profile_id is required" });
            }
            const { error } = await admin.from("player_stats").update({
                matches: 0, wins: 0, rounds_won: 0, kills: 0, deaths: 0, headshots: 0, damage: 0, playtime_seconds: 0,
                updated_at: new Date().toISOString(),
            }).eq("profile_id", body.profile_id);
            if (error) {
                return json(500, { error: error.message });
            }
            await audit(body.profile_id, {});
            return json(200, { ok: true });
        }

        case "log": {
            const limit = Math.min(Math.max(Number(body.limit) || 50, 1), 200);
            const { data, error } = await admin
                .from("admin_log")
                .select("created_at, action, details, admin:admin_id(nickname), target:target_id(nickname)")
                .order("created_at", { ascending: false })
                .limit(limit);
            if (error) {
                return json(500, { error: error.message });
            }
            return json(200, { log: data ?? [] });
        }

        default:
            return json(400, { error: "unknown action" });
        }
    } catch (error) {
        console.error(error);
        return json(401, { error: String((error as Error)?.message ?? error) });
    }
});
