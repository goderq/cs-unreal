// CS-Fusion: Photon custom authentication (docs/AUDIT.md B5).
//
// Photon asks this function whether a player may connect at all. The game
// sends its login token (from eos-login) as the authentication parameter
// "token"; Photon forwards it here. The answer, in Photon's format:
//   { "ResultCode": 1, "UserId": <profile id>, "Nickname": <nickname> }  let in
//   { "ResultCode": 2, "Message": "..." }                                keep out
// A banned player, an expired or forged token, or no token at all is kept out.
// With "Allow anonymous clients" OFF in the Photon Dashboard this is the only
// way into a room, so a ban works in the game too.
//
// Deploy with "Verify JWT with legacy secret" OFF: Photon calls this URL
// without any Supabase header; the function verifies the token itself.
// Secrets: CS_JWT_SECRET (plus SUPABASE_URL and SUPABASE_SERVICE_ROLE_KEY).

import { createClient } from "https://esm.sh/@supabase/supabase-js@2";
import { verify } from "https://deno.land/x/djwt@v3.0.2/mod.ts";

const UUID = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;

const answer = (body: Record<string, unknown>) =>
    new Response(JSON.stringify(body), { status: 200, headers: { "content-type": "application/json" } });
const refuse = (message: string) => answer({ ResultCode: 2, Message: message });

/** The token from the query (Photon's GET), the JSON body (POST) or, for our own diagnostics, a Bearer header. */
async function findToken(request: Request): Promise<string> {
    const url = new URL(request.url);
    const fromQuery = url.searchParams.get("token");
    if (fromQuery) {
        return fromQuery;
    }
    if (request.method === "POST") {
        try {
            const body = await request.json();
            if (typeof body?.token === "string") {
                return body.token;
            }
        } catch {
            // not JSON - fall through
        }
    }
    const header = request.headers.get("authorization") ?? "";
    return header.toLowerCase().startsWith("bearer ") ? header.slice(7) : "";
}

Deno.serve(async (request) => {
    try {
        const token = await findToken(request);
        if (!token || token.length > 4096) {
            return answer({ ResultCode: 3, Message: "sign in to play online" });
        }
        const key = await crypto.subtle.importKey("raw", new TextEncoder().encode(Deno.env.get("CS_JWT_SECRET")!),
            { name: "HMAC", hash: "SHA-256" }, false, ["sign", "verify"]);
        let payload: Record<string, unknown>;
        try {
            payload = await verify(token, key) as Record<string, unknown>;
        } catch {
            return refuse("the login has expired - sign in again");
        }
        if (payload.role !== "authenticated" || payload.aud !== "authenticated" || typeof payload.sub !== "string"
            || !UUID.test(payload.sub)) {
            return refuse("not a player login");
        }

        const db = createClient(Deno.env.get("SUPABASE_URL")!, Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")!,
            { auth: { persistSession: false } });
        const { data: allowed } = await db.rpc("cs_rate_limit",
            { p_key: `photon:${payload.sub}`, p_max: 60, p_window_seconds: 600 });
        if (allowed === false) {
            return refuse("too many connections, wait a few minutes");
        }
        const { data: profile, error } = await db.from("profiles").select("id, nickname, banned_until")
            .eq("id", payload.sub).maybeSingle();
        if (error) {
            console.error(error);
            return refuse("the account server is unavailable");
        }
        if (!profile) {
            return refuse("unknown player");
        }
        if (profile.banned_until && new Date(profile.banned_until) > new Date()) {
            return refuse(`banned until ${profile.banned_until}`);
        }
        return answer({ ResultCode: 1, UserId: profile.id, Nickname: profile.nickname });
    } catch (error) {
        console.error(error);
        return refuse("server error");
    }
});
