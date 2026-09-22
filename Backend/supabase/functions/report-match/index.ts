// CS-Fusion: a finished match is reported here by the Master Client.
//
// Honest limits: the authority is still a player's machine, so these numbers
// cannot be fully trusted. What this function does enforce:
//   * the caller holds a valid login token (from eos-login);
//   * the caller took part in the match being reported;
//   * every number is inside a sane range - no 9000-kill matches;
//   * the same match (room + start time) is only counted once.
// A dedicated server would be the only way to make the numbers authoritative.
//
// Deploy:
//   supabase functions deploy report-match --no-verify-jwt
// Secrets: SUPABASE_JWT_SECRET (plus the automatic SUPABASE_URL and
// SUPABASE_SERVICE_ROLE_KEY).

import { createClient } from "https://esm.sh/@supabase/supabase-js@2";
import { verify } from "https://deno.land/x/djwt@v3.0.2/mod.ts";

const MAX_PLAYERS = 16;
const LIMITS = { kills: 300, deaths: 300, headshots: 300, damage: 200000, money: 100000 };
const MAX_MATCH_SECONDS = 3 * 60 * 60;

const json = (status: number, body: unknown) =>
    new Response(JSON.stringify(body), { status, headers: { "content-type": "application/json" } });

const clamp = (value: unknown, max: number) =>
    Math.max(0, Math.min(max, Math.round(Number(value) || 0)));

async function callerProfileId(request: Request): Promise<string> {
    const header = request.headers.get("authorization") ?? "";
    const token = header.toLowerCase().startsWith("bearer ") ? header.slice(7) : "";
    if (!token) {
        throw new Error("missing login token");
    }
    const key = await crypto.subtle.importKey(
        "raw", new TextEncoder().encode(Deno.env.get("SUPABASE_JWT_SECRET")!),
        { name: "HMAC", hash: "SHA-256" }, false, ["sign", "verify"],
    );
    const payload = await verify(token, key) as Record<string, unknown>;
    if (!payload.sub) {
        throw new Error("token has no subject");
    }
    return String(payload.sub);
}

Deno.serve(async (request) => {
    if (request.method !== "POST") {
        return json(405, { error: "POST only" });
    }
    try {
        const reporter = await callerProfileId(request);
        const body = await request.json();

        const mode = String(body.mode ?? "").toUpperCase();
        if (!["DM", "TDM", "5V5"].includes(mode)) {
            return json(400, { error: "unknown mode" });
        }
        const startedAt = new Date(body.started_at);
        const endedAt = body.ended_at ? new Date(body.ended_at) : new Date();
        if (isNaN(startedAt.getTime()) || endedAt <= startedAt) {
            return json(400, { error: "bad match times" });
        }
        const seconds = (endedAt.getTime() - startedAt.getTime()) / 1000;
        if (seconds > MAX_MATCH_SECONDS) {
            return json(400, { error: "match too long" });
        }

        const rawPlayers = Array.isArray(body.players) ? body.players.slice(0, MAX_PLAYERS) : [];
        const players = rawPlayers
            .filter((p: Record<string, unknown>) => typeof p.profile_id === "string" && p.profile_id.length === 36)
            .map((p: Record<string, unknown>) => ({
                profile_id: p.profile_id,
                team: clamp(p.team, 2),
                kills: clamp(p.kills, LIMITS.kills),
                deaths: clamp(p.deaths, LIMITS.deaths),
                headshots: clamp(p.headshots, LIMITS.headshots),
                damage: clamp(p.damage, LIMITS.damage),
                money: clamp(p.money, LIMITS.money),
                won: Boolean(p.won),
            }));
        if (players.length === 0) {
            return json(400, { error: "no players with accounts" });
        }
        if (!players.some((p) => p.profile_id === reporter)) {
            return json(403, { error: "the reporter did not play in this match" });
        }
        // Headshots are a subset of kills.
        for (const p of players) {
            p.headshots = Math.min(p.headshots, p.kills);
        }

        const admin = createClient(
            Deno.env.get("SUPABASE_URL")!,
            Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")!,
            { auth: { persistSession: false } },
        );

        // Every profile id must exist, or somebody is inventing players.
        const { data: known, error: knownError } = await admin
            .from("profiles")
            .select("id")
            .in("id", players.map((p) => p.profile_id));
        if (knownError) {
            return json(500, { error: knownError.message });
        }
        const knownIds = new Set((known ?? []).map((row: { id: string }) => row.id));
        const finalPlayers = players.filter((p) => knownIds.has(p.profile_id));
        if (finalPlayers.length === 0) {
            return json(400, { error: "no known players" });
        }

        const { data, error } = await admin.rpc("apply_match_result", {
            payload: {
                mode,
                map: String(body.map ?? "").slice(0, 64),
                room: String(body.room ?? "").slice(0, 96),
                reported_by: reporter,
                started_at: startedAt.toISOString(),
                ended_at: endedAt.toISOString(),
                winner_team: clamp(body.winner_team, 2),
                rounds: clamp(body.rounds, 64),
                players: finalPlayers,
            },
        });
        if (error) {
            return json(500, { error: error.message });
        }
        return json(200, { match_id: data, counted: data ? finalPlayers.length : 0, duplicate: data === null });
    } catch (error) {
        console.error(error);
        return json(401, { error: String((error as Error)?.message ?? error) });
    }
});
