// CS-Fusion: link a second sign-in method to the signed-in profile.
//
// A player who plays with Epic can add an email + password, and a player who
// registered with email can add their Epic account - both then open the SAME
// profile, with the same stats. Linking needs proof of BOTH identities in one
// request:
//   Authorization: Bearer <login token>   the profile (eos-login / email-login)
//   body {kind: "email", access_token}    a Supabase Auth access token of the email
//   body {kind: "epic",  epic_token}      an Epic access token, checked with Epic
// The database (migration 009, link_identity) keeps every identity on at most
// one profile and never moves or replaces a linked one:
//   409 taken           the email / Epic account already opens another profile
//   409 already_linked  this profile already has a different email / Epic account
//
// Deploy with "Verify JWT with legacy secret" ON. Secrets: CS_JWT_SECRET,
// EOS_CLIENT_ID, EOS_CLIENT_SECRET, EOS_GAME_CLIENT_ID (as eos-login), plus
// SUPABASE_URL and SUPABASE_SERVICE_ROLE_KEY, provided by Supabase.

import { createClient } from "https://esm.sh/@supabase/supabase-js@2";
import { verify } from "https://deno.land/x/djwt@v3.0.2/mod.ts";

const EPIC = "https://api.epicgames.dev";
const UUID = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;

class HttpError extends Error {
    constructor(public status: number, message: string) {
        super(message);
    }
}

const json = (status: number, body: unknown) =>
    new Response(JSON.stringify(body), { status, headers: { "content-type": "application/json" } });

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
    // Only OUR login tokens: a Supabase Auth token is signed with the same secret,
    // but its subject is an Auth user, not a profile.
    if (payload.iss !== "cs-fusion" || payload.role !== "authenticated" || payload.aud !== "authenticated"
        || typeof payload.sub !== "string" || !UUID.test(payload.sub)) {
        throw new HttpError(401, "not a player login token");
    }
    return payload.sub;
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
    if (info.client_id !== gameClientId) {
        throw new HttpError(401, "the Epic token was issued to a different game");
    }
    return String(info.account_id);
}

Deno.serve(async (request) => {
    if (request.method !== "POST") {
        return json(405, { error: "POST only" });
    }
    try {
        const profileId = await callerProfileId(request);
        let body: Record<string, unknown>;
        try {
            body = await request.json();
        } catch {
            throw new HttpError(400, "the request is not JSON");
        }
        const db = createClient(Deno.env.get("SUPABASE_URL")!, Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")!,
            { auth: { persistSession: false } });

        const { data: allowed, error: limitError } = await db.rpc("cs_rate_limit",
            { p_key: `link:${profileId}`, p_max: 10, p_window_seconds: 600 });
        if (limitError) {
            throw limitError;
        }
        if (!allowed) {
            throw new HttpError(429, "too many attempts, wait a few minutes");
        }

        let kind: string;
        let value: string;
        let emailAddress: string | null = null;
        if (body.kind === "email") {
            const accessToken = typeof body.access_token === "string" ? body.access_token : "";
            if (!accessToken || accessToken.length > 8192) {
                throw new HttpError(400, "access_token is required");
            }
            const { data: auth, error: authError } = await db.auth.getUser(accessToken);
            if (authError || !auth?.user?.email) {
                throw new HttpError(401, "the email session has expired - sign in again");
            }
            kind = "email";
            value = auth.user.id;
            emailAddress = auth.user.email;
        } else if (body.kind === "epic") {
            const epicToken = typeof body.epic_token === "string" ? body.epic_token : "";
            if (!epicToken || epicToken.length > 8192) {
                throw new HttpError(400, "epic_token is required");
            }
            kind = "epic";
            value = await verifyEpicToken(epicToken);
        } else {
            throw new HttpError(400, "kind must be email or epic");
        }

        const { data, error } = await db.rpc("link_identity", { p_profile: profileId, p_kind: kind, p_value: value });
        if (error) {
            if (error.code === "23505") {
                throw new HttpError(409, "taken");
            }
            if (error.code === "23514") {
                throw new HttpError(409, "already_linked");
            }
            if (error.code === "P0002") {
                throw new HttpError(404, "no such profile");
            }
            if (error.code === "22023") {
                throw new HttpError(400, "bad identity");
            }
            throw error;
        }
        const { data: linked } = await db.rpc("profile_identities", { p_profile: profileId });
        return json(200, {
            ok: true,
            changed: data?.changed === true,
            linked: {
                epic: linked?.epic === true,
                email: linked?.email === true,
                email_address: linked?.email_address ?? emailAddress,
            },
        });
    } catch (error) {
        if (error instanceof HttpError) {
            return json(error.status, { error: error.message });
        }
        console.error(error);
        return json(500, { error: "server error" });
    }
});
