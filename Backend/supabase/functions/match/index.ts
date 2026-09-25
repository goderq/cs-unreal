// CS-Fusion: match registration, participation tickets and results.
//
//   start   the host (Master Client) registers a match when it begins; the
//           SERVER stamps the start time. Returns the match id and the host's
//           own ticket.
//   ticket  every signed-in player asks for their own ticket, with their own
//           login, and hands it to the host only. The ticket proves that this
//           profile played in this match.
//   report  the host reports stats per ticket when the match ends.
//   incident  the host reports an anti-cheat measure (a player suspended or
//           removed from the match), naming the player by their ticket; it
//           goes to the security log (migration 008).
//
// The checks themselves live in the database (migration 005): tickets,
// duplicates, the server's clock, rate limits, plausibility. A report that
// fails them is kept but marked suspicious and counts for nobody until staff
// look at it. Offline matches and matches with bots are practice, not ranked.
//
// Honest limit: the host is a player. A dishonest host can still bend the
// numbers of a real match within the plausibility limits; only a dedicated
// server would fix that (docs/AUDIT.md 3.6).
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
const MAX_BODY = 32 * 1024;

function uuid(value: unknown, name: string): string {
    if (typeof value !== "string" || !UUID.test(value)) {
        throw new HttpError(400, `${name} is required`);
    }
    return value;
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
    if (payload.role !== "authenticated" || payload.aud !== "authenticated" || typeof payload.sub !== "string"
        || !UUID.test(payload.sub)) {
        throw new HttpError(401, "not a player login token");
    }
    return payload.sub;
}

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
        const caller = await callerProfileId(request);
        const raw = await request.text();
        if (raw.length > MAX_BODY) {
            throw new HttpError(413, "the report is too large");
        }
        let body: Record<string, unknown>;
        try {
            body = JSON.parse(raw);
        } catch {
            throw new HttpError(400, "the request is not JSON");
        }

        let fn: string;
        let args: Record<string, unknown>;
        switch (String(body.action ?? "")) {
            case "start":
                fn = "match_start";
                args = {
                    p_host: caller,
                    p_mode: String(body.mode ?? "").slice(0, 8),
                    p_map: String(body.map ?? "").slice(0, 32),
                    p_room: String(body.room ?? "").slice(0, 96),
                    p_offline: body.offline === true,
                };
                break;
            case "ticket":
                fn = "match_ticket";
                args = { p_profile: caller, p_match: uuid(body.match_id, "match_id") };
                break;
            case "report": {
                fn = "match_report";
                const players = Array.isArray(body.players) ? body.players.slice(0, 32) : [];
                args = {
                    p_reporter: caller,
                    p_match: uuid(body.match_id, "match_id"),
                    p_payload: {
                        winner_team: body.winner_team, rounds: body.rounds, bots: body.bots, humans: body.humans,
                        players,
                    },
                };
                break;
            }
            case "incident":
                fn = "match_incident";
                args = {
                    p_reporter: caller,
                    p_match: uuid(body.match_id, "match_id"),
                    p_payload: {
                        kind: String(body.kind ?? "").slice(0, 16),
                        ticket: typeof body.ticket === "string" ? body.ticket.slice(0, 36) : null,
                        player: Number.isFinite(Number(body.player)) ? Math.trunc(Number(body.player)) : null,
                        reason: String(body.reason ?? "").slice(0, 200),
                    },
                };
                break;
            default:
                throw new HttpError(400, "unknown action");
        }

        const db = createClient(Deno.env.get("SUPABASE_URL")!, Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")!,
            { auth: { persistSession: false } });
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
