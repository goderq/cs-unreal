-- CS-Fusion migration 001: baseline.
--
-- The v1.2 tables exactly as the live project has them, so that a fresh
-- database and the live one start the migration chain from the same place.
-- Safe to run on a database that already has them: everything is
-- "if not exists" / "or replace", and no data is touched.
--
-- Apply the migrations in order (001, 002, ...) in the Supabase SQL editor,
-- or all at once with Backend/supabase/schema.sql (generated from them).

create extension if not exists pgcrypto;

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

create table if not exists public.matches (
    id          uuid primary key default gen_random_uuid(),
    mode        text not null check (mode in ('DM', 'TDM', '5V5')),
    map         text not null,
    room        text,
    reported_by uuid references public.profiles(id) on delete set null,
    started_at  timestamptz not null,
    ended_at    timestamptz not null default now(),
    winner_team smallint not null default 0,
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
create unique index if not exists matches_room_start_idx on public.matches (room, started_at);

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

alter table public.profiles      enable row level security;
alter table public.player_stats  enable row level security;
alter table public.matches       enable row level security;
alter table public.match_players enable row level security;

-- Everything a player may see is public; nobody but the service role writes.
drop policy if exists profiles_read on public.profiles;
create policy profiles_read on public.profiles for select using (true);
drop policy if exists stats_read on public.player_stats;
create policy stats_read on public.player_stats for select using (true);
drop policy if exists matches_read on public.matches;
create policy matches_read on public.matches for select using (true);
drop policy if exists match_players_read on public.match_players;
create policy match_players_read on public.match_players for select using (true);

grant usage on schema public to anon, authenticated, service_role;
grant select, insert, update, delete on public.profiles, public.player_stats,
                public.matches, public.match_players
    to service_role;
grant select on public.leaderboard to anon, authenticated, service_role;
