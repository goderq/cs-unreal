-- CS-Fusion accounts and stats (Supabase / Postgres).
--
-- Run this once in the Supabase SQL editor (Dashboard -> SQL -> New query).
-- Safe to re-run: everything is CREATE ... IF NOT EXISTS / OR REPLACE.
--
-- Identity comes from Epic (EOS). The game never sees the service key: it gets
-- a short-lived Supabase JWT from the eos-login Edge Function, and row level
-- security below decides what that JWT may read and write.

create extension if not exists pgcrypto;

-- ---------------------------------------------------------------------------
-- profiles: one row per Epic account
-- ---------------------------------------------------------------------------
create table if not exists public.profiles (
    id              uuid primary key default gen_random_uuid(),
    epic_account_id text        not null unique,
    nickname        text        not null unique,
    created_at      timestamptz not null default now(),
    last_seen_at    timestamptz not null default now(),
    banned_until    timestamptz,
    is_admin        boolean     not null default false,
    constraint nickname_shape check (char_length(nickname) between 3 and 20)
);

-- ---------------------------------------------------------------------------
-- player_stats: lifetime totals, one row per profile
-- ---------------------------------------------------------------------------
create table if not exists public.player_stats (
    profile_id       uuid primary key references public.profiles(id) on delete cascade,
    matches          integer not null default 0,
    wins             integer not null default 0,
    rounds_won       integer not null default 0,
    kills            integer not null default 0,
    deaths           integer not null default 0,
    headshots        integer not null default 0,
    damage           bigint  not null default 0,
    playtime_seconds integer not null default 0,
    updated_at       timestamptz not null default now()
);

-- ---------------------------------------------------------------------------
-- matches / match_players: one row per finished match, one per participant
-- ---------------------------------------------------------------------------
create table if not exists public.matches (
    id          uuid primary key default gen_random_uuid(),
    mode        text not null check (mode in ('DM', 'TDM', '5V5')),
    map         text not null,
    room        text,
    reported_by uuid references public.profiles(id) on delete set null,
    started_at  timestamptz not null,
    ended_at    timestamptz not null default now(),
    winner_team smallint not null default 0,   -- 0 none, 1 Alpha, 2 Bravo
    rounds      smallint not null default 0
);

create table if not exists public.match_players (
    match_id   uuid not null references public.matches(id) on delete cascade,
    profile_id uuid not null references public.profiles(id) on delete cascade,
    team       smallint not null default 0,
    kills      integer  not null default 0,
    deaths     integer  not null default 0,
    headshots  integer  not null default 0,
    damage     integer  not null default 0,
    money      integer  not null default 0,
    primary key (match_id, profile_id)
);

create index if not exists match_players_profile_idx on public.match_players (profile_id);
create index if not exists matches_ended_idx on public.matches (ended_at desc);

-- ---------------------------------------------------------------------------
-- Leaderboard: read-only view for the menu
-- ---------------------------------------------------------------------------
create or replace view public.leaderboard as
select p.id,
       p.nickname,
       s.kills,
       s.deaths,
       s.matches,
       s.wins,
       case when s.deaths = 0 then s.kills::numeric else round(s.kills::numeric / s.deaths, 2) end as kd
from public.profiles p
join public.player_stats s on s.profile_id = p.id
order by s.kills desc
limit 100;

-- ---------------------------------------------------------------------------
-- Row level security
--
-- The game holds a JWT whose `sub` is the profile id (minted by eos-login).
-- Reading is public (nicknames and stats are shown to everyone); writing is
-- limited to the owner, and stats/matches are written only by the Edge
-- Functions, which use the service key and bypass RLS.
-- ---------------------------------------------------------------------------
alter table public.profiles      enable row level security;
alter table public.player_stats  enable row level security;
alter table public.matches       enable row level security;
alter table public.match_players enable row level security;

drop policy if exists profiles_read on public.profiles;
create policy profiles_read on public.profiles for select using (true);

-- A player may only rename themselves, and only their own row.
drop policy if exists profiles_update_own on public.profiles;
create policy profiles_update_own on public.profiles
    for update using (id = auth.uid()) with check (id = auth.uid());

drop policy if exists stats_read on public.player_stats;
create policy stats_read on public.player_stats for select using (true);

drop policy if exists matches_read on public.matches;
create policy matches_read on public.matches for select using (true);

drop policy if exists match_players_read on public.match_players;
create policy match_players_read on public.match_players for select using (true);

-- No insert/update/delete policies on stats, matches or match_players on
-- purpose: only the service key (Edge Functions) writes them.

-- ---------------------------------------------------------------------------
-- Guard: the nickname and the Epic id cannot be changed into someone else's,
-- and a player cannot promote themselves.
-- ---------------------------------------------------------------------------
create or replace function public.profiles_guard()
returns trigger language plpgsql as $$
begin
    if new.epic_account_id is distinct from old.epic_account_id then
        raise exception 'epic_account_id is immutable';
    end if;
    new.is_admin := old.is_admin;
    new.banned_until := old.banned_until;
    new.created_at := old.created_at;
    return new;
end;
$$;

drop trigger if exists profiles_guard_trigger on public.profiles;
create trigger profiles_guard_trigger before update on public.profiles
    for each row execute function public.profiles_guard();

-- One match is reported once: the room name plus its start time identify it.
create unique index if not exists matches_room_start_idx on public.matches (room, started_at);

-- ---------------------------------------------------------------------------
-- apply_match_result: writes the match, its players and their lifetime totals
-- in one transaction. Called by the report-match Edge Function (service key).
--
-- payload:
-- {
--   "mode": "5V5", "map": "OldTown", "room": "QM 5V5 OldTown",
--   "started_at": "...", "ended_at": "...", "winner_team": 1, "rounds": 9,
--   "reported_by": "<profile uuid>",
--   "players": [{ "profile_id": "...", "team": 1, "kills": 12, "deaths": 7,
--                 "headshots": 3, "damage": 1840, "money": 12500, "won": true }]
-- }
-- ---------------------------------------------------------------------------
create or replace function public.apply_match_result(payload jsonb)
returns uuid language plpgsql security definer as $$
declare
    v_match_id uuid;
    v_player   jsonb;
    v_seconds  integer;
begin
    insert into public.matches (mode, map, room, reported_by, started_at, ended_at, winner_team, rounds)
    values (payload->>'mode',
            payload->>'map',
            payload->>'room',
            (payload->>'reported_by')::uuid,
            (payload->>'started_at')::timestamptz,
            coalesce((payload->>'ended_at')::timestamptz, now()),
            coalesce((payload->>'winner_team')::smallint, 0),
            coalesce((payload->>'rounds')::smallint, 0))
    on conflict (room, started_at) do nothing
    returning id into v_match_id;

    if v_match_id is null then
        -- Already reported (a second peer sent the same match): nothing to do.
        return null;
    end if;

    v_seconds := greatest(0, least(10800,
        extract(epoch from (coalesce((payload->>'ended_at')::timestamptz, now()) - (payload->>'started_at')::timestamptz))::integer));

    for v_player in select * from jsonb_array_elements(payload->'players')
    loop
        insert into public.match_players (match_id, profile_id, team, kills, deaths, headshots, damage, money)
        values (v_match_id,
                (v_player->>'profile_id')::uuid,
                coalesce((v_player->>'team')::smallint, 0),
                coalesce((v_player->>'kills')::integer, 0),
                coalesce((v_player->>'deaths')::integer, 0),
                coalesce((v_player->>'headshots')::integer, 0),
                coalesce((v_player->>'damage')::integer, 0),
                coalesce((v_player->>'money')::integer, 0))
        on conflict do nothing;

        insert into public.player_stats as s (profile_id, matches, wins, kills, deaths, headshots, damage, playtime_seconds, updated_at)
        values ((v_player->>'profile_id')::uuid,
                1,
                case when (v_player->>'won')::boolean then 1 else 0 end,
                coalesce((v_player->>'kills')::integer, 0),
                coalesce((v_player->>'deaths')::integer, 0),
                coalesce((v_player->>'headshots')::integer, 0),
                coalesce((v_player->>'damage')::integer, 0),
                v_seconds,
                now())
        on conflict (profile_id) do update set
            matches          = s.matches + 1,
            wins             = s.wins + case when (v_player->>'won')::boolean then 1 else 0 end,
            kills            = s.kills + coalesce((v_player->>'kills')::integer, 0),
            deaths           = s.deaths + coalesce((v_player->>'deaths')::integer, 0),
            headshots        = s.headshots + coalesce((v_player->>'headshots')::integer, 0),
            damage           = s.damage + coalesce((v_player->>'damage')::integer, 0),
            playtime_seconds = s.playtime_seconds + v_seconds,
            updated_at       = now();
    end loop;

    return v_match_id;
end;
$$;

revoke all on function public.apply_match_result(jsonb) from public, anon, authenticated;
