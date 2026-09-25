-- CS-Fusion database schema - GENERATED, do not edit by hand.
--
-- This file is every migration in Backend/supabase/migrations, in order
-- (Scripts/build_schema.ps1). For a NEW database run it once in the Supabase
-- SQL editor. For an existing database apply the migrations it has not had
-- yet, in order - each one is safe to re-run.
--
-- Then run Backend/supabase/tests/security_tests.sql to check the result.

-- ==============================================================================
-- 001_baseline.sql
-- ==============================================================================
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

-- ==============================================================================
-- 002_profiles_privacy.sql
-- ==============================================================================
-- CS-Fusion migration 002: players write nothing, and read only public data.
--
-- 1. No direct writes for players at all. In v1.2 a signed-in player could
--    update their own profile row through the REST API (that is how renaming
--    worked). Nicknames are now changed only by the administration, through
--    the admin functions (migration 006), never by the player.
-- 2. The profiles table was readable in full with the anon key that ships in
--    every build: Epic account ids, admin flags, bans, activity times. Only the
--    public columns stay readable now.
-- 3. The leaderboard view ran with its owner's rights and so ignored all of
--    the above; it now runs with the caller's.
-- 4. Match rows are no longer public (the game never read them).
--
-- Re-runnable.

drop policy if exists profiles_update_own on public.profiles;

revoke insert, update, delete, truncate, references, trigger
    on public.profiles, public.player_stats, public.matches, public.match_players
    from anon, authenticated;

-- Column-level read access: the table grant goes, three columns come back.
revoke select on public.profiles from anon, authenticated;
grant select (id, nickname, created_at) on public.profiles to anon, authenticated;

-- Lifetime stats stay public (profile page, leaderboard). Match rows are not
-- read by the game and will carry anti-cheat flags (migration 005): staff
-- only, through the admin functions.
grant select on public.player_stats to anon, authenticated;
revoke select on public.matches, public.match_players from anon, authenticated;

alter view public.leaderboard set (security_invoker = true);
grant select on public.leaderboard to anon, authenticated, service_role;

-- ==============================================================================
-- 003_roles.sql
-- ==============================================================================
-- CS-Fusion migration 003: roles instead of a single admin flag.
--
--   player      everybody (default)
--   moderator   looks after players: temporary bans up to 7 days, kicks,
--               resetting a nickname to an automatic one, flagging matches
--   admin       everything a moderator does, plus permanent bans, choosing a
--               nickname, resetting stats, voiding and approving matches,
--               making and unmaking moderators
--   superadmin  everything, plus making and unmaking admins. Assigned only
--               with SQL in the dashboard, never through the API.
--
-- The permission table lives in code (cs_has_permission) rather than in a
-- table, so it cannot be widened by writing a row. Nobody changes their own
-- role, and nobody acts on a player of the same or a higher role (enforced by
-- the admin functions in migration 006).
--
-- is_admin stays for v1.2 clients but is now derived from the role.
-- Re-runnable.

alter table public.profiles add column if not exists role text not null default 'player';

do $$
begin
    if not exists (select 1 from pg_constraint
                   where conname = 'profiles_role_check' and conrelid = 'public.profiles'::regclass) then
        alter table public.profiles
            add constraint profiles_role_check check (role in ('player', 'moderator', 'admin', 'superadmin'));
    end if;
end $$;

-- Trigger first, so the role update below goes through the new rules.
create or replace function public.profiles_guard()
returns trigger language plpgsql set search_path = '' as $$
begin
    if new.epic_account_id is distinct from old.epic_account_id then
        raise exception 'epic_account_id is immutable';
    end if;
    new.created_at := old.created_at;
    -- Players have no write grant at all (migration 002); this is the second
    -- line of defence. The Edge Functions (service role) and the dashboard
    -- (postgres) may change roles, bans and names - through the checks of the
    -- admin functions.
    if coalesce(auth.role(), '') in ('authenticated', 'anon') then
        new.role := old.role;
        new.banned_until := old.banned_until;
        new.nickname := old.nickname;
    end if;
    return new;
end $$;

drop trigger if exists profiles_guard_trigger on public.profiles;
create trigger profiles_guard_trigger before update on public.profiles
    for each row execute function public.profiles_guard();

-- is_admin -> derived from role. The v1.2 column held the only admin flag, so
-- whoever had it becomes an admin first.
do $$
begin
    if exists (select 1 from pg_attribute
               where attrelid = 'public.profiles'::regclass and attname = 'is_admin'
                 and not attisdropped and attgenerated = '') then
        update public.profiles set role = 'admin' where is_admin and role = 'player';
        alter table public.profiles drop column is_admin;
    end if;
    if not exists (select 1 from pg_attribute
                   where attrelid = 'public.profiles'::regclass and attname = 'is_admin' and not attisdropped) then
        alter table public.profiles
            add column is_admin boolean generated always as (role in ('admin', 'superadmin')) stored;
    end if;
end $$;

create or replace function public.cs_role_rank(p_role text)
returns integer language sql immutable set search_path = '' as $$
    select case p_role
        when 'superadmin' then 3
        when 'admin' then 2
        when 'moderator' then 1
        else 0
    end;
$$;

create or replace function public.cs_has_permission(p_role text, p_permission text)
returns boolean language sql immutable set search_path = '' as $$
    select case
        when p_permission in ('players.view', 'players.ban_temp', 'players.unban_temp', 'players.kick',
                              'players.nickname_reset', 'matches.view', 'matches.flag', 'security.view_own')
            then public.cs_role_rank(p_role) >= 1
        when p_permission in ('players.view_private', 'players.ban_permanent', 'players.unban',
                              'players.nickname_set', 'players.stats_reset', 'players.role_set',
                              'matches.void', 'matches.approve', 'security.view_all')
            then public.cs_role_rank(p_role) >= 2
        when p_permission in ('players.role_set_admin')
            then public.cs_role_rank(p_role) >= 3
        else false
    end;
$$;

-- Every permission name, for "what may I do" answers.
create or replace function public.cs_permissions()
returns text[] language sql immutable set search_path = '' as $$
    select array['players.view', 'players.view_private', 'players.ban_temp', 'players.ban_permanent',
                 'players.unban_temp', 'players.unban', 'players.kick', 'players.nickname_reset',
                 'players.nickname_set', 'players.stats_reset', 'players.role_set', 'players.role_set_admin',
                 'matches.view', 'matches.flag', 'matches.void', 'matches.approve',
                 'security.view_own', 'security.view_all'];
$$;

-- Functions are callable only by the service role (the Edge Functions).
do $$
declare f record;
begin
    for f in select p.oid::regprocedure as sig
             from pg_proc p join pg_namespace n on n.oid = p.pronamespace
             where n.nspname = 'public'
               and (p.proname like 'cs\_%' or p.proname = 'profiles_guard') loop
        execute format('revoke execute on function %s from public, anon, authenticated', f.sig);
        execute format('grant execute on function %s to service_role', f.sig);
    end loop;
end $$;

-- ==============================================================================
-- 004_security_and_limits.sql
-- ==============================================================================
-- CS-Fusion migration 004: the security log and request rate limits.
--
-- security_log records who did what to whom, why, and what changed:
--   source     admin | anticheat | report | system
--   actor      the profile that acted (and its role at the time)
--   target     the player (or match) acted on
--   reason     required for every admin action
--   old_value / new_value   the fields that changed
-- No IP addresses or other personal data: none of the checks need them.
-- Only the service role reads or writes it (no policies, no grants to players).
--
-- rate_limits backs cs_rate_limit(key, max, window): a fixed-window counter
-- the Edge Functions use per player and per action.
--
-- Re-runnable.

create table if not exists public.security_log (
    id          bigserial primary key,
    created_at  timestamptz not null default now(),
    source      text not null default 'admin' check (source in ('admin', 'anticheat', 'report', 'system')),
    actor_id    uuid references public.profiles(id) on delete set null,
    actor_role  text,
    action      text not null,
    target_id   uuid references public.profiles(id) on delete set null,
    match_id    uuid references public.matches(id) on delete set null,
    reason      text,
    old_value   jsonb,
    new_value   jsonb,
    details     jsonb not null default '{}'::jsonb
);
alter table public.security_log enable row level security;
create index if not exists security_log_created_idx on public.security_log (created_at desc);
create index if not exists security_log_target_idx on public.security_log (target_id, created_at desc);
create index if not exists security_log_actor_idx on public.security_log (actor_id, created_at desc);

revoke all on public.security_log from anon, authenticated;
grant select, insert on public.security_log to service_role;
grant usage, select on sequence public.security_log_id_seq to service_role;

-- The v2.0 draft had an admin_log table; carry its rows over if it exists.
do $$
begin
    if to_regclass('public.admin_log') is not null then
        insert into public.security_log (created_at, source, actor_id, action, target_id, new_value)
        select l.created_at, 'admin', l.admin_id, l.action, l.target_id, l.details
        from public.admin_log l;
        drop table public.admin_log;
    end if;
end $$;

create table if not exists public.rate_limits (
    key          text primary key,
    window_start timestamptz not null default now(),
    hits         integer not null default 0
);
alter table public.rate_limits enable row level security;
revoke all on public.rate_limits from anon, authenticated;
grant select, insert, update, delete on public.rate_limits to service_role;

-- True while the key has made at most p_max calls in the current window.
create or replace function public.cs_rate_limit(p_key text, p_max integer, p_window_seconds integer)
returns boolean language plpgsql set search_path = '' as $$
declare
    v_hits integer;
begin
    insert into public.rate_limits as r (key, window_start, hits)
    values (p_key, now(), 1)
    on conflict (key) do update set
        hits = case when r.window_start < now() - make_interval(secs => p_window_seconds) then 1 else r.hits + 1 end,
        window_start = case when r.window_start < now() - make_interval(secs => p_window_seconds) then now() else r.window_start end
    returning hits into v_hits;

    -- Now and then, forget keys nobody has used for a day.
    if random() < 0.01 then
        delete from public.rate_limits where window_start < now() - interval '1 day';
    end if;
    return v_hits <= p_max;
end $$;

-- Security log entry. p_actor may be null (system, anti-cheat).
create or replace function public.cs_log(p_source text, p_actor uuid, p_action text, p_target uuid,
                                         p_match uuid, p_reason text, p_old jsonb, p_new jsonb,
                                         p_details jsonb default '{}'::jsonb)
returns void language sql set search_path = '' as $$
    insert into public.security_log (source, actor_id, actor_role, action, target_id, match_id,
                                     reason, old_value, new_value, details)
    values (p_source, p_actor, (select role from public.profiles where id = p_actor), p_action, p_target,
            p_match, p_reason, p_old, p_new, coalesce(p_details, '{}'::jsonb));
$$;

do $$
declare f record;
begin
    for f in select p.oid::regprocedure as sig
             from pg_proc p join pg_namespace n on n.oid = p.pronamespace
             where n.nspname = 'public' and p.proname like 'cs\_%' loop
        execute format('revoke execute on function %s from public, anon, authenticated', f.sig);
        execute format('grant execute on function %s to service_role', f.sig);
    end loop;
end $$;

-- ==============================================================================
-- 005_match_integrity.sql
-- ==============================================================================
-- CS-Fusion migration 005: match reports that cannot be multiplied or invented.
--
-- What v1.2 allowed (docs/AUDIT.md A3, A4):
--   * the same profile listed 16 times counted 16 times;
--   * any room name + start time made a "new" match, as often as wanted;
--   * the reporter named the other participants (any existing profile);
--   * the end time came from the client;
--   * offline practice against bots went into the leaderboard.
--
-- The flow now:
--   1. match_start  - the host registers the match; the SERVER stamps the
--                     start time. The host gets the first ticket.
--   2. match_ticket - every signed-in player asks for their own ticket with
--                     their own login, and hands it to the host only.
--   3. match_report - the host reports stats per ticket. The server checks
--                     tickets, duplicates, times, rates and consistency.
--                     Anything odd -> the match is kept but marked suspicious
--                     and counts for nobody until staff approve it.
--
-- Ranked (leaderboard) stats come only from online matches with at least two
-- ticket holders and no bots. Everything else is practice and goes to the
-- practice_* columns. A dishonest host can still bend the numbers of a real
-- match within these limits - that needs a dedicated server (AUDIT 3.6).
--
-- apply_match_result, the v1.2 path, is removed in migration 007.
-- Re-runnable.

-- Matches ------------------------------------------------------------------

alter table public.matches add column if not exists status text not null default 'reported';
alter table public.matches add column if not exists host_id uuid references public.profiles(id) on delete set null;
alter table public.matches add column if not exists ranked boolean not null default false;
alter table public.matches add column if not exists bots boolean not null default false;
alter table public.matches add column if not exists suspicious boolean not null default false;
alter table public.matches add column if not exists suspicious_reasons text[] not null default '{}';
alter table public.matches add column if not exists voided boolean not null default false;

-- v1.2 matches were counted in full when reported: mark them applied and
-- ranked, so staff can still void them (wins are not stored per player for
-- them, so voiding one leaves its wins in place).
do $$
begin
    if not exists (select 1 from information_schema.columns
                   where table_schema = 'public' and table_name = 'matches' and column_name = 'applied') then
        alter table public.matches add column applied boolean not null default false;
        update public.matches set applied = true, ranked = true;
    end if;
end $$;

-- v1.2 stored no host: the Master Client that reported a match was its host.
update public.matches set host_id = reported_by
where host_id is null and reported_by is not null and status = 'reported';

-- An open match has no end yet.
alter table public.matches alter column ended_at drop not null;
alter table public.matches alter column ended_at drop default;

do $$
begin
    if not exists (select 1 from pg_constraint
                   where conname = 'matches_status_check' and conrelid = 'public.matches'::regclass) then
        alter table public.matches add constraint matches_status_check check (status in ('open', 'reported'));
    end if;
end $$;

create index if not exists matches_open_idx on public.matches (host_id, started_at desc) where status = 'open';
create index if not exists matches_suspicious_idx on public.matches (started_at desc) where suspicious;

alter table public.match_players add column if not exists won boolean not null default false;

-- Tickets --------------------------------------------------------------------

-- The ticket id is a random UUID: a capability only its holder (and the host
-- it was given to) knows. It proves that this profile took part.
create table if not exists public.match_tickets (
    id         uuid primary key default gen_random_uuid(),
    match_id   uuid not null references public.matches(id) on delete cascade,
    profile_id uuid not null references public.profiles(id) on delete cascade,
    issued_at  timestamptz not null default now(),
    unique (match_id, profile_id)
);
alter table public.match_tickets enable row level security;
revoke all on public.match_tickets from anon, authenticated;
grant select, insert, update, delete on public.match_tickets to service_role;

-- Practice stats ---------------------------------------------------------------

alter table public.player_stats add column if not exists practice_matches integer not null default 0;
alter table public.player_stats add column if not exists practice_kills integer not null default 0;
alter table public.player_stats add column if not exists practice_deaths integer not null default 0;
alter table public.player_stats add column if not exists practice_headshots integer not null default 0;
alter table public.player_stats add column if not exists practice_playtime_seconds integer not null default 0;

-- Applying a match to the lifetime totals -----------------------------------------

-- p_sign = 1 adds the match to the totals, -1 takes it back out (void, flag).
-- Idempotent through matches.applied.
create or replace function public.cs_match_apply(p_match uuid, p_sign integer)
returns void language plpgsql set search_path = '' as $$
declare
    v_match   public.matches%rowtype;
    v_seconds integer;
    v_sign    integer := case when p_sign < 0 then -1 else 1 end;
begin
    select * into v_match from public.matches where id = p_match for update;
    if not found or (v_sign > 0 and v_match.applied) or (v_sign < 0 and not v_match.applied) then
        return;
    end if;
    v_seconds := greatest(0, least(10800,
        extract(epoch from (coalesce(v_match.ended_at, now()) - v_match.started_at))::integer));

    if v_sign > 0 and v_match.ranked then
        insert into public.player_stats as s (profile_id, matches, wins, kills, deaths, headshots, damage,
                                              playtime_seconds, updated_at)
        select mp.profile_id, 1, mp.won::integer, mp.kills, mp.deaths, mp.headshots, mp.damage, v_seconds, now()
        from public.match_players mp where mp.match_id = p_match
        on conflict (profile_id) do update set
            matches          = s.matches + 1,
            wins             = s.wins + excluded.wins,
            kills            = s.kills + excluded.kills,
            deaths           = s.deaths + excluded.deaths,
            headshots        = s.headshots + excluded.headshots,
            damage           = s.damage + excluded.damage,
            playtime_seconds = s.playtime_seconds + excluded.playtime_seconds,
            updated_at       = now();
    elsif v_sign > 0 then
        insert into public.player_stats as s (profile_id, practice_matches, practice_kills, practice_deaths,
                                              practice_headshots, practice_playtime_seconds, updated_at)
        select mp.profile_id, 1, mp.kills, mp.deaths, mp.headshots, v_seconds, now()
        from public.match_players mp where mp.match_id = p_match
        on conflict (profile_id) do update set
            practice_matches          = s.practice_matches + 1,
            practice_kills            = s.practice_kills + excluded.practice_kills,
            practice_deaths           = s.practice_deaths + excluded.practice_deaths,
            practice_headshots        = s.practice_headshots + excluded.practice_headshots,
            practice_playtime_seconds = s.practice_playtime_seconds + excluded.practice_playtime_seconds,
            updated_at                = now();
    elsif v_match.ranked then
        update public.player_stats s set
            matches          = greatest(0, s.matches - 1),
            wins             = greatest(0, s.wins - mp.won::integer),
            kills            = greatest(0, s.kills - mp.kills),
            deaths           = greatest(0, s.deaths - mp.deaths),
            headshots        = greatest(0, s.headshots - mp.headshots),
            damage           = greatest(0, s.damage - mp.damage),
            playtime_seconds = greatest(0, s.playtime_seconds - v_seconds),
            updated_at       = now()
        from public.match_players mp
        where mp.match_id = p_match and mp.profile_id = s.profile_id;
    else
        update public.player_stats s set
            practice_matches          = greatest(0, s.practice_matches - 1),
            practice_kills            = greatest(0, s.practice_kills - mp.kills),
            practice_deaths           = greatest(0, s.practice_deaths - mp.deaths),
            practice_headshots        = greatest(0, s.practice_headshots - mp.headshots),
            practice_playtime_seconds = greatest(0, s.practice_playtime_seconds - v_seconds),
            updated_at                = now()
        from public.match_players mp
        where mp.match_id = p_match and mp.profile_id = s.profile_id;
    end if;

    update public.matches set applied = (v_sign > 0) where id = p_match;
end $$;

-- 1. The host registers a match ------------------------------------------------------

create or replace function public.match_start(p_host uuid, p_mode text, p_map text, p_room text, p_offline boolean)
returns jsonb language plpgsql set search_path = '' as $$
declare
    v_mode   text := upper(coalesce(p_mode, ''));
    v_banned timestamptz;
    v_id     uuid := gen_random_uuid();
    v_ticket uuid;
begin
    select banned_until into v_banned from public.profiles where id = p_host;
    if not found then
        raise exception 'unknown profile' using errcode = 'P0002';
    end if;
    if v_banned is not null and v_banned > now() then
        raise exception 'banned' using errcode = '42501';
    end if;
    if not public.cs_rate_limit('match_start:' || p_host, 30, 3600) then
        raise exception 'too many matches started' using errcode = 'P0001', hint = 'rate_limited';
    end if;
    if v_mode not in ('DM', 'TDM', '5V5') then
        raise exception 'unknown mode' using errcode = '22023';
    end if;
    if coalesce(p_map, '') not in ('Depot', 'OldTown', 'Warehouse') then
        raise exception 'unknown map' using errcode = '22023';
    end if;

    insert into public.matches (id, mode, map, room, host_id, started_at, ended_at, status)
    values (v_id, v_mode, p_map,
            case when coalesce(p_offline, false) or coalesce(trim(p_room), '') = ''
                 then 'offline:' || v_id::text
                 else left(trim(p_room), 96) end,
            p_host, now(), null, 'open');
    insert into public.match_tickets (match_id, profile_id) values (v_id, p_host) returning id into v_ticket;
    return jsonb_build_object('match_id', v_id, 'ticket', v_ticket);
end $$;

-- 2. A player asks for their ticket --------------------------------------------------------

create or replace function public.match_ticket(p_profile uuid, p_match uuid)
returns uuid language plpgsql set search_path = '' as $$
declare
    v_banned timestamptz;
    v_match  public.matches%rowtype;
    v_ticket uuid;
begin
    select banned_until into v_banned from public.profiles where id = p_profile;
    if not found then
        raise exception 'unknown profile' using errcode = 'P0002';
    end if;
    if v_banned is not null and v_banned > now() then
        raise exception 'banned' using errcode = '42501';
    end if;
    if not public.cs_rate_limit('match_ticket:' || p_profile, 120, 3600) then
        raise exception 'too many tickets' using errcode = 'P0001', hint = 'rate_limited';
    end if;
    select * into v_match from public.matches where id = p_match;
    if not found then
        raise exception 'unknown match' using errcode = 'P0002';
    end if;
    if v_match.status <> 'open' or v_match.started_at < now() - interval '3 hours' then
        raise exception 'the match is closed' using errcode = '22023';
    end if;
    if v_match.room like 'offline:%' and v_match.host_id is distinct from p_profile then
        raise exception 'an offline match has one player' using errcode = '42501';
    end if;
    insert into public.match_tickets (match_id, profile_id) values (p_match, p_profile)
    on conflict (match_id, profile_id) do update set issued_at = public.match_tickets.issued_at
    returning id into v_ticket;
    return v_ticket;
end $$;

-- 3. The host reports the result ----------------------------------------------------------

-- p_payload:
-- { "winner_team": 1, "rounds": 9, "bots": false, "humans": 4,
--   "players": [{ "ticket": "<uuid>", "team": 1, "kills": 12, "deaths": 7, "headshots": 3,
--                 "damage": 1840, "money": 12500, "won": true }] }
create or replace function public.match_report(p_reporter uuid, p_match uuid, p_payload jsonb)
returns jsonb language plpgsql set search_path = '' as $$
declare
    v_match      public.matches%rowtype;
    v_minutes    numeric;
    v_reasons    text[] := '{}';
    v_player     jsonb;
    v_ticket     uuid;
    v_profile    uuid;
    v_seen       uuid[] := '{}';
    v_team       integer;
    v_kills      integer;
    v_deaths     integer;
    v_headshots  integer;
    v_damage     integer;
    v_money      integer;
    v_won        boolean;
    v_winner     integer;
    v_rounds     integer;
    v_bots       boolean;
    v_humans     integer;
    v_count      integer := 0;
    v_sum_kills  integer := 0;
    v_sum_deaths integer := 0;
    v_won_count  integer := 0;
    v_ranked     boolean;
    v_suspicious boolean;
begin
    if not public.cs_rate_limit('match_report:' || p_reporter, 30, 3600) then
        raise exception 'too many reports' using errcode = 'P0001', hint = 'rate_limited';
    end if;

    select * into v_match from public.matches where id = p_match for update;
    if not found then
        raise exception 'unknown match' using errcode = 'P0002';
    end if;
    if v_match.status <> 'open' then
        raise exception 'the match was already reported' using errcode = '23505';
    end if;
    if not exists (select 1 from public.match_tickets t where t.match_id = p_match and t.profile_id = p_reporter) then
        raise exception 'the reporter did not play in this match' using errcode = '42501';
    end if;

    -- Numbers from the payload, with hard limits against garbage.
    begin
        v_winner := greatest(0, least(2, coalesce((p_payload->>'winner_team')::integer, 0)));
        v_rounds := greatest(0, least(64, coalesce((p_payload->>'rounds')::integer, 0)));
        v_bots   := coalesce((p_payload->>'bots')::boolean, true);
        v_humans := greatest(0, least(64, coalesce((p_payload->>'humans')::integer, 0)));
    exception when others then
        raise exception 'malformed report' using errcode = '22023';
    end;

    -- The server's own clock decides how long the match was.
    v_minutes := greatest(0, extract(epoch from (now() - v_match.started_at)) / 60.0);
    if v_minutes < 1 then
        v_reasons := array_append(v_reasons, 'too_short');
    elsif v_minutes > 180 then
        v_reasons := array_append(v_reasons, 'too_long');
    end if;
    -- A room holds at most 16 players; only the first 16 entries are read.
    if jsonb_typeof(p_payload->'players') = 'array' and jsonb_array_length(p_payload->'players') > 16 then
        v_reasons := array_append(v_reasons, 'too_many_players');
    end if;

    for v_player in select value from jsonb_array_elements(
                        case when jsonb_typeof(p_payload->'players') = 'array' then p_payload->'players' else '[]'::jsonb end)
                    limit 16 loop
        v_profile := null;
        begin
            v_ticket := (v_player->>'ticket')::uuid;
        exception when others then
            v_ticket := null;
        end;
        select t.profile_id into v_profile from public.match_tickets t where t.id = v_ticket and t.match_id = p_match;
        if v_profile is null then
            v_reasons := array_append(v_reasons, 'bad_ticket');
            continue;
        end if;
        if v_profile = any(v_seen) then
            v_reasons := array_append(v_reasons, 'duplicate_player');
            continue;
        end if;
        v_seen := array_append(v_seen, v_profile);

        begin
            v_team      := greatest(0, least(2, coalesce((v_player->>'team')::integer, 0)));
            v_kills     := greatest(0, least(2000, coalesce((v_player->>'kills')::integer, 0)));
            v_deaths    := greatest(0, least(2000, coalesce((v_player->>'deaths')::integer, 0)));
            v_headshots := greatest(0, least(2000, coalesce((v_player->>'headshots')::integer, 0)));
            v_damage    := greatest(0, least(1000000, coalesce((v_player->>'damage')::integer, 0)));
            v_money     := greatest(0, least(1000000, coalesce((v_player->>'money')::integer, 0)));
            v_won       := coalesce((v_player->>'won')::boolean, false);
        exception when others then
            raise exception 'malformed player entry' using errcode = '22023';
        end;

        -- What one player can plausibly do in that time.
        if v_kills > 12 * v_minutes + 5 then v_reasons := array_append(v_reasons, 'kill_rate'); end if;
        if v_deaths > 12 * v_minutes + 5 then v_reasons := array_append(v_reasons, 'death_rate'); end if;
        if v_headshots > v_kills then v_reasons := array_append(v_reasons, 'headshots_over_kills'); end if;
        if v_damage > 2500 * v_minutes + 500 then v_reasons := array_append(v_reasons, 'damage_rate'); end if;

        -- Teams and the result must agree with the mode.
        if v_match.mode = 'DM' then
            if v_team <> 0 then v_reasons := array_append(v_reasons, 'team_in_dm'); end if;
        else
            if v_team not in (1, 2) then v_reasons := array_append(v_reasons, 'no_team'); end if;
            if v_won is distinct from (v_winner <> 0 and v_team = v_winner) then
                v_reasons := array_append(v_reasons, 'won_mismatch');
            end if;
        end if;

        insert into public.match_players (match_id, profile_id, team, kills, deaths, headshots, damage, money, won)
        values (p_match, v_profile, v_team, v_kills, v_deaths, least(v_headshots, v_kills), v_damage, v_money, v_won);

        v_count := v_count + 1;
        v_sum_kills := v_sum_kills + v_kills;
        v_sum_deaths := v_sum_deaths + v_deaths;
        v_won_count := v_won_count + v_won::integer;
    end loop;

    if v_count = 0 then
        raise exception 'no players with tickets' using errcode = '22023';
    end if;
    if not (p_reporter = any(v_seen)) then
        v_reasons := array_append(v_reasons, 'reporter_not_listed');
    end if;
    if v_match.mode = 'DM' and (v_winner <> 0 or v_won_count > 1) then
        v_reasons := array_append(v_reasons, 'won_mismatch');
    end if;
    if v_match.mode = '5V5' and (v_rounds < 1 or v_rounds > 30) then
        v_reasons := array_append(v_reasons, 'rounds');
    elsif v_match.mode <> '5V5' and v_rounds <> 0 then
        v_reasons := array_append(v_reasons, 'rounds');
    end if;
    if v_humans < v_count then
        v_reasons := array_append(v_reasons, 'humans_count');
    end if;
    -- Without bots and guests every kill is somebody's death in this list.
    if not v_bots and v_humans = v_count and v_sum_kills > v_sum_deaths then
        v_reasons := array_append(v_reasons, 'kills_exceed_deaths');
    end if;

    select coalesce(array_agg(distinct r order by r), '{}') into v_reasons from unnest(v_reasons) r;
    v_suspicious := cardinality(v_reasons) > 0;
    v_ranked := not v_bots and v_count >= 2 and v_match.room not like 'offline:%';

    update public.matches set
        status = 'reported',
        ended_at = now(),
        reported_by = p_reporter,
        winner_team = v_winner,
        rounds = v_rounds,
        bots = v_bots,
        ranked = v_ranked,
        suspicious = v_suspicious,
        suspicious_reasons = v_reasons,
        applied = false
    where id = p_match;

    if v_suspicious then
        perform public.cs_log('report', p_reporter, 'match.suspicious', null, p_match,
                              array_to_string(v_reasons, ', '), null, null,
                              jsonb_build_object('payload', p_payload, 'minutes', round(v_minutes, 1)));
    else
        perform public.cs_match_apply(p_match, 1);
    end if;

    return jsonb_build_object('match_id', p_match, 'ranked', v_ranked and not v_suspicious,
                              'suspicious', v_suspicious, 'reasons', to_jsonb(v_reasons), 'counted', v_count);
end $$;

do $$
declare f record;
begin
    for f in select p.oid::regprocedure as sig
             from pg_proc p join pg_namespace n on n.oid = p.pronamespace
             where n.nspname = 'public' and (p.proname like 'cs\_%' or p.proname like 'match\_%') loop
        execute format('revoke execute on function %s from public, anon, authenticated', f.sig);
        execute format('grant execute on function %s to service_role', f.sig);
    end loop;
end $$;

-- ==============================================================================
-- 006_admin_service.sql
-- ==============================================================================
-- CS-Fusion migration 006: AdminService.
--
-- Every admin action is one SQL function. Each one, in a single transaction:
--   1. loads the acting profile from the database (never from the client) and
--      refuses anyone who is not staff or is banned;
--   2. checks the role has the permission (cs_has_permission, migration 003);
--   3. checks the target is a different player of a LOWER role;
--   4. makes the change;
--   5. writes security_log with the reason and the old and new values.
--
-- The functions run with the caller's rights (no SECURITY DEFINER) and only
-- the service role may call them: the admin Edge Function verifies the login
-- token and passes the token's subject as p_actor. A player cannot reach them
-- through the REST API, and a client that shows the admin panel anyway gets
-- "forbidden" for every button.
--
-- Errors carry a SQLSTATE the Edge Function turns into an HTTP status:
--   42501 forbidden, P0002 not found, 22023 bad input, 23505 conflict,
--   P0001 + hint rate_limited -> too many requests.
--
-- Re-runnable.

-- Helpers -------------------------------------------------------------------

create or replace function public.cs_admin_actor(p_actor uuid, p_permission text)
returns public.profiles language plpgsql stable set search_path = '' as $$
declare
    v public.profiles%rowtype;
begin
    select * into v from public.profiles where id = p_actor;
    if not found or public.cs_role_rank(v.role) < 1 then
        raise exception 'staff only' using errcode = '42501';
    end if;
    if v.banned_until is not null and v.banned_until > now() then
        raise exception 'banned' using errcode = '42501';
    end if;
    if p_permission is not null and not public.cs_has_permission(v.role, p_permission) then
        raise exception 'missing permission: %', p_permission using errcode = '42501';
    end if;
    return v;
end $$;

create or replace function public.cs_admin_target(p_actor public.profiles, p_target uuid)
returns public.profiles language plpgsql stable set search_path = '' as $$
declare
    t public.profiles%rowtype;
begin
    select * into t from public.profiles where id = p_target;
    if not found then
        raise exception 'unknown player' using errcode = 'P0002';
    end if;
    if t.id = p_actor.id then
        raise exception 'not on yourself' using errcode = '42501';
    end if;
    if public.cs_role_rank(t.role) >= public.cs_role_rank(p_actor.role) then
        raise exception 'the player has the same or a higher role' using errcode = '42501';
    end if;
    return t;
end $$;

create or replace function public.cs_reason(p_reason text)
returns text language plpgsql immutable set search_path = '' as $$
declare
    v text := trim(coalesce(p_reason, ''));
begin
    if char_length(v) < 3 then
        raise exception 'a reason is required (at least 3 characters)' using errcode = '22023';
    end if;
    return left(v, 300);
end $$;

create or replace function public.cs_is_reserved_name(p_name text)
returns boolean language sql immutable set search_path = '' as $$
    select lower(regexp_replace(coalesce(p_name, ''), '[^A-Za-z0-9]', '', 'g')) in
               ('admin', 'administrator', 'moderator', 'mod', 'staff', 'support', 'system', 'server',
                'official', 'csfusion', 'epic', 'epicgames', 'root', 'owner', 'developer', 'dev', 'gm')
        or lower(coalesce(p_name, '')) like 'admin%'
        or lower(coalesce(p_name, '')) like 'moderator%';
$$;

-- The Edge Function checks the character set (Unicode letters, digits,
-- space, _ - #); this is the last line: length, no control characters,
-- nothing reserved.
create or replace function public.cs_clean_nickname(p_name text)
returns text language plpgsql immutable set search_path = '' as $$
declare
    v text := trim(regexp_replace(coalesce(p_name, ''), '\s+', ' ', 'g'));
begin
    if char_length(v) < 3 or char_length(v) > 20 then
        raise exception 'a nickname has 3 to 20 characters' using errcode = '22023';
    end if;
    if v ~ '[[:cntrl:]]' then
        raise exception 'control characters are not allowed' using errcode = '22023';
    end if;
    if public.cs_is_reserved_name(v) then
        raise exception 'this name is reserved' using errcode = '22023';
    end if;
    return v;
end $$;

-- Who am I and what may I do ---------------------------------------------------------

create or replace function public.admin_whoami(p_actor uuid)
returns jsonb language plpgsql stable set search_path = '' as $$
declare
    v public.profiles%rowtype;
begin
    v := public.cs_admin_actor(p_actor, null);
    return jsonb_build_object(
        'id', v.id, 'nickname', v.nickname, 'role', v.role,
        'permissions', coalesce((select jsonb_agg(p order by p) from unnest(public.cs_permissions()) p
                                 where public.cs_has_permission(v.role, p)), '[]'::jsonb));
end $$;

-- Players -------------------------------------------------------------------------------

create or replace function public.admin_players_search(p_actor uuid, p_query text, p_limit integer)
returns jsonb language plpgsql stable set search_path = '' as $$
declare
    v         public.profiles%rowtype;
    v_private boolean;
    v_q       text := trim(coalesce(p_query, ''));
    v_like    text;
    v_limit   integer := greatest(1, least(coalesce(p_limit, 50), 200));
begin
    v := public.cs_admin_actor(p_actor, 'players.view');
    v_private := public.cs_has_permission(v.role, 'players.view_private');
    v_like := '%' || replace(replace(replace(v_q, '\', '\\'), '%', '\%'), '_', '\_') || '%';
    return coalesce((
        select jsonb_agg(to_jsonb(x) order by x.last_seen_at desc) from (
            select p.id, p.nickname, p.role, p.created_at, p.last_seen_at, p.banned_until,
                   (p.banned_until is not null and p.banned_until > now()) as banned,
                   case when v_private then p.epic_account_id end as epic_account_id,
                   coalesce(s.matches, 0) as matches, coalesce(s.wins, 0) as wins,
                   coalesce(s.kills, 0) as kills, coalesce(s.deaths, 0) as deaths,
                   coalesce(s.headshots, 0) as headshots, coalesce(s.damage, 0) as damage,
                   coalesce(s.playtime_seconds, 0) as playtime_seconds
            from public.profiles p
            left join public.player_stats s on s.profile_id = p.id
            where v_q = ''
               or p.nickname ilike v_like
               or p.id::text = v_q
               or (v_private and p.epic_account_id = v_q)
            order by p.last_seen_at desc
            limit v_limit) x), '[]'::jsonb);
end $$;

create or replace function public.admin_player_get(p_actor uuid, p_target uuid)
returns jsonb language plpgsql stable set search_path = '' as $$
declare
    v         public.profiles%rowtype;
    t         public.profiles%rowtype;
    v_private boolean;
    v_all     boolean;
begin
    v := public.cs_admin_actor(p_actor, 'players.view');
    v_private := public.cs_has_permission(v.role, 'players.view_private');
    v_all := public.cs_has_permission(v.role, 'security.view_all');
    select * into t from public.profiles where id = p_target;
    if not found then
        raise exception 'unknown player' using errcode = 'P0002';
    end if;
    return jsonb_build_object(
        'profile', jsonb_build_object(
            'id', t.id, 'nickname', t.nickname, 'role', t.role, 'created_at', t.created_at,
            'last_seen_at', t.last_seen_at, 'banned_until', t.banned_until,
            'banned', t.banned_until is not null and t.banned_until > now(),
            'epic_account_id', case when v_private then t.epic_account_id end),
        'stats', coalesce((select to_jsonb(s) - 'profile_id' from public.player_stats s where s.profile_id = t.id),
                          '{}'::jsonb),
        'recent_matches', coalesce((
            select jsonb_agg(to_jsonb(m) order by m.started_at desc) from (
                select mt.id, mt.mode, mt.map, mt.started_at, mt.ended_at, mt.ranked, mt.suspicious, mt.voided,
                       mp.team, mp.kills, mp.deaths, mp.headshots, mp.damage, mp.won
                from public.match_players mp
                join public.matches mt on mt.id = mp.match_id
                where mp.profile_id = t.id
                order by mt.started_at desc
                limit 20) m), '[]'::jsonb),
        'security', coalesce((
            select jsonb_agg(to_jsonb(l) order by l.created_at desc) from (
                select sl.created_at, sl.source, sl.action, sl.reason, sl.old_value, sl.new_value,
                       (select a.nickname from public.profiles a where a.id = sl.actor_id) as actor
                from public.security_log sl
                where sl.target_id = t.id
                  and (v_all or sl.actor_id = v.id or sl.source <> 'admin')
                order by sl.created_at desc
                limit 20) l), '[]'::jsonb));
end $$;

-- Hours <= 0 bans permanently. More than 7 days or permanent needs an admin.
create or replace function public.admin_ban(p_actor uuid, p_target uuid, p_hours integer, p_reason text)
returns jsonb language plpgsql set search_path = '' as $$
declare
    v        public.profiles%rowtype;
    t        public.profiles%rowtype;
    v_reason text;
    v_hours  integer := coalesce(p_hours, 0);
    v_until  timestamptz;
begin
    v := public.cs_admin_actor(p_actor, 'players.ban_temp');
    t := public.cs_admin_target(v, p_target);
    v_reason := public.cs_reason(p_reason);
    if v_hours <= 0 or v_hours > 168 then
        perform public.cs_admin_actor(p_actor, 'players.ban_permanent');
    end if;
    v_until := case when v_hours <= 0 then timestamptz '2999-01-01 00:00:00+00'
                    else now() + make_interval(hours => least(v_hours, 87600)) end;
    update public.profiles set banned_until = v_until where id = t.id;
    perform public.cs_log('admin', v.id, 'player.ban', t.id, null, v_reason,
                          jsonb_build_object('banned_until', t.banned_until),
                          jsonb_build_object('banned_until', v_until));
    return jsonb_build_object('banned_until', v_until);
end $$;

-- A moderator lifts temporary bans; an admin lifts any.
create or replace function public.admin_unban(p_actor uuid, p_target uuid, p_reason text)
returns jsonb language plpgsql set search_path = '' as $$
declare
    v        public.profiles%rowtype;
    t        public.profiles%rowtype;
    v_reason text;
begin
    v := public.cs_admin_actor(p_actor, 'players.unban_temp');
    t := public.cs_admin_target(v, p_target);
    v_reason := public.cs_reason(p_reason);
    if t.banned_until is null or t.banned_until <= now() then
        raise exception 'the player is not banned' using errcode = '22023';
    end if;
    if t.banned_until > now() + interval '8 days' then
        perform public.cs_admin_actor(p_actor, 'players.unban');
    end if;
    update public.profiles set banned_until = null where id = t.id;
    perform public.cs_log('admin', v.id, 'player.unban', t.id, null, v_reason,
                          jsonb_build_object('banned_until', t.banned_until),
                          jsonb_build_object('banned_until', null));
    return jsonb_build_object('banned_until', null);
end $$;

-- A kick is a short ban (1..60 minutes): the player cannot sign in, get a
-- match ticket or (with Photon custom auth, Phase 2) join a room until it ends.
create or replace function public.admin_kick(p_actor uuid, p_target uuid, p_minutes integer, p_reason text)
returns jsonb language plpgsql set search_path = '' as $$
declare
    v         public.profiles%rowtype;
    t         public.profiles%rowtype;
    v_reason  text;
    v_minutes integer := greatest(1, least(coalesce(p_minutes, 10), 60));
    v_until   timestamptz;
begin
    v := public.cs_admin_actor(p_actor, 'players.kick');
    t := public.cs_admin_target(v, p_target);
    v_reason := public.cs_reason(p_reason);
    v_until := greatest(coalesce(t.banned_until, now()), now() + make_interval(mins => v_minutes));
    update public.profiles set banned_until = v_until where id = t.id;
    perform public.cs_log('admin', v.id, 'player.kick', t.id, null, v_reason,
                          jsonb_build_object('banned_until', t.banned_until),
                          jsonb_build_object('banned_until', v_until, 'minutes', v_minutes));
    return jsonb_build_object('banned_until', v_until);
end $$;

-- player <-> moderator needs an admin; admin needs a superadmin; superadmin
-- is never granted through the API.
create or replace function public.admin_set_role(p_actor uuid, p_target uuid, p_role text, p_reason text)
returns jsonb language plpgsql set search_path = '' as $$
declare
    v        public.profiles%rowtype;
    t        public.profiles%rowtype;
    v_reason text;
    v_role   text := lower(trim(coalesce(p_role, '')));
begin
    v := public.cs_admin_actor(p_actor, 'players.role_set');
    t := public.cs_admin_target(v, p_target);
    v_reason := public.cs_reason(p_reason);
    if v_role not in ('player', 'moderator', 'admin') then
        raise exception 'unknown or protected role: %', v_role using errcode = '22023';
    end if;
    if v_role = 'admin' then
        perform public.cs_admin_actor(p_actor, 'players.role_set_admin');
    end if;
    if public.cs_role_rank(v_role) >= public.cs_role_rank(v.role) then
        raise exception 'you cannot grant a role equal to or above your own' using errcode = '42501';
    end if;
    if v_role = t.role then
        raise exception 'the player already has this role' using errcode = '22023';
    end if;
    update public.profiles set role = v_role where id = t.id;
    perform public.cs_log('admin', v.id, 'player.role', t.id, null, v_reason,
                          jsonb_build_object('role', t.role), jsonb_build_object('role', v_role));
    return jsonb_build_object('role', v_role);
end $$;

-- Back to an automatic "Player#1234".
create or replace function public.admin_reset_nickname(p_actor uuid, p_target uuid, p_reason text)
returns jsonb language plpgsql set search_path = '' as $$
declare
    v        public.profiles%rowtype;
    t        public.profiles%rowtype;
    v_reason text;
    v_name   text;
    v_done   boolean := false;
begin
    v := public.cs_admin_actor(p_actor, 'players.nickname_reset');
    t := public.cs_admin_target(v, p_target);
    v_reason := public.cs_reason(p_reason);
    for i in 1..40 loop
        v_name := 'Player#' || case when i <= 20 then (1000 + floor(random() * 9000))::integer::text
                                    else (100000 + floor(random() * 900000))::integer::text end;
        begin
            update public.profiles set nickname = v_name where id = t.id;
            v_done := true;
            exit;
        exception when unique_violation then
            null;
        end;
    end loop;
    if not v_done then
        raise exception 'could not find a free automatic name' using errcode = '23505';
    end if;
    perform public.cs_log('admin', v.id, 'player.nickname_reset', t.id, null, v_reason,
                          jsonb_build_object('nickname', t.nickname), jsonb_build_object('nickname', v_name));
    return jsonb_build_object('nickname', v_name);
end $$;

create or replace function public.admin_set_nickname(p_actor uuid, p_target uuid, p_nickname text, p_reason text)
returns jsonb language plpgsql set search_path = '' as $$
declare
    v        public.profiles%rowtype;
    t        public.profiles%rowtype;
    v_reason text;
    v_name   text;
begin
    v := public.cs_admin_actor(p_actor, 'players.nickname_set');
    t := public.cs_admin_target(v, p_target);
    v_reason := public.cs_reason(p_reason);
    v_name := public.cs_clean_nickname(p_nickname);
    begin
        update public.profiles set nickname = v_name where id = t.id;
    exception when unique_violation then
        raise exception 'that name is taken' using errcode = '23505';
    end;
    perform public.cs_log('admin', v.id, 'player.nickname_set', t.id, null, v_reason,
                          jsonb_build_object('nickname', t.nickname), jsonb_build_object('nickname', v_name));
    return jsonb_build_object('nickname', v_name);
end $$;

create or replace function public.admin_reset_stats(p_actor uuid, p_target uuid, p_reason text)
returns jsonb language plpgsql set search_path = '' as $$
declare
    v        public.profiles%rowtype;
    t        public.profiles%rowtype;
    v_reason text;
    v_old    jsonb;
begin
    v := public.cs_admin_actor(p_actor, 'players.stats_reset');
    t := public.cs_admin_target(v, p_target);
    v_reason := public.cs_reason(p_reason);
    select to_jsonb(s) - 'profile_id' - 'updated_at' into v_old from public.player_stats s where s.profile_id = t.id;
    update public.player_stats set
        matches = 0, wins = 0, rounds_won = 0, kills = 0, deaths = 0, headshots = 0, damage = 0,
        playtime_seconds = 0, practice_matches = 0, practice_kills = 0, practice_deaths = 0,
        practice_headshots = 0, practice_playtime_seconds = 0, updated_at = now()
    where profile_id = t.id;
    perform public.cs_log('admin', v.id, 'player.stats_reset', t.id, null, v_reason, v_old, '{}'::jsonb);
    return jsonb_build_object('reset', true);
end $$;

-- Matches ----------------------------------------------------------------------------------

create or replace function public.admin_matches_list(p_actor uuid, p_filter text, p_limit integer)
returns jsonb language plpgsql stable set search_path = '' as $$
declare
    v        public.profiles%rowtype;
    v_filter text := lower(coalesce(p_filter, 'all'));
    v_limit  integer := greatest(1, least(coalesce(p_limit, 50), 200));
begin
    v := public.cs_admin_actor(p_actor, 'matches.view');
    return coalesce((
        select jsonb_agg(to_jsonb(x) order by x.started_at desc) from (
            select m.id, m.mode, m.map, m.room, m.started_at, m.ended_at, m.status, m.ranked, m.bots,
                   m.suspicious, m.suspicious_reasons, m.voided, m.applied, m.winner_team, m.rounds,
                   (select count(*) from public.match_players mp where mp.match_id = m.id) as players,
                   (select p.nickname from public.profiles p where p.id = m.host_id) as host,
                   (select p.nickname from public.profiles p where p.id = m.reported_by) as reported_by
            from public.matches m
            where case v_filter
                      when 'suspicious' then m.suspicious and not m.voided
                      when 'voided' then m.voided
                      when 'open' then m.status = 'open'
                      else true
                  end
            order by m.started_at desc
            limit v_limit) x), '[]'::jsonb);
end $$;

create or replace function public.admin_match_get(p_actor uuid, p_match uuid)
returns jsonb language plpgsql stable set search_path = '' as $$
declare
    v public.profiles%rowtype;
    m public.matches%rowtype;
begin
    v := public.cs_admin_actor(p_actor, 'matches.view');
    select * into m from public.matches where id = p_match;
    if not found then
        raise exception 'unknown match' using errcode = 'P0002';
    end if;
    return jsonb_build_object(
        'match', to_jsonb(m),
        'players', coalesce((
            select jsonb_agg(to_jsonb(x) order by x.team, x.kills desc) from (
                select mp.profile_id, p.nickname, mp.team, mp.kills, mp.deaths, mp.headshots, mp.damage,
                       mp.money, mp.won
                from public.match_players mp
                join public.profiles p on p.id = mp.profile_id
                where mp.match_id = p_match) x), '[]'::jsonb));
end $$;

-- Flag: suspicious; its numbers come back out of everybody's totals.
create or replace function public.admin_match_flag(p_actor uuid, p_match uuid, p_reason text)
returns jsonb language plpgsql set search_path = '' as $$
declare
    v        public.profiles%rowtype;
    m        public.matches%rowtype;
    v_reason text;
begin
    v := public.cs_admin_actor(p_actor, 'matches.flag');
    v_reason := public.cs_reason(p_reason);
    select * into m from public.matches where id = p_match for update;
    if not found then
        raise exception 'unknown match' using errcode = 'P0002';
    end if;
    if m.voided then
        raise exception 'the match is void' using errcode = '22023';
    end if;
    perform public.cs_match_apply(p_match, -1);
    update public.matches set suspicious = true,
        suspicious_reasons = (select coalesce(array_agg(distinct r order by r), '{}')
                              from unnest(array_append(m.suspicious_reasons, 'flagged_by_staff')) r)
    where id = p_match;
    perform public.cs_log('admin', v.id, 'match.flag', null, p_match, v_reason,
                          jsonb_build_object('suspicious', m.suspicious, 'applied', m.applied),
                          jsonb_build_object('suspicious', true, 'applied', false));
    return jsonb_build_object('suspicious', true);
end $$;

-- Void: the match counts for nobody, for good.
create or replace function public.admin_match_void(p_actor uuid, p_match uuid, p_reason text)
returns jsonb language plpgsql set search_path = '' as $$
declare
    v        public.profiles%rowtype;
    m        public.matches%rowtype;
    v_reason text;
begin
    v := public.cs_admin_actor(p_actor, 'matches.void');
    v_reason := public.cs_reason(p_reason);
    select * into m from public.matches where id = p_match for update;
    if not found then
        raise exception 'unknown match' using errcode = 'P0002';
    end if;
    if m.voided then
        raise exception 'the match is already void' using errcode = '22023';
    end if;
    perform public.cs_match_apply(p_match, -1);
    update public.matches set voided = true where id = p_match;
    perform public.cs_log('admin', v.id, 'match.void', null, p_match, v_reason,
                          jsonb_build_object('voided', false, 'applied', m.applied),
                          jsonb_build_object('voided', true, 'applied', false));
    return jsonb_build_object('voided', true);
end $$;

-- Approve a suspicious match after review: it counts (ranked if it qualifies).
create or replace function public.admin_match_approve(p_actor uuid, p_match uuid, p_reason text)
returns jsonb language plpgsql set search_path = '' as $$
declare
    v        public.profiles%rowtype;
    m        public.matches%rowtype;
    v_reason text;
    v_ranked boolean;
begin
    v := public.cs_admin_actor(p_actor, 'matches.approve');
    v_reason := public.cs_reason(p_reason);
    select * into m from public.matches where id = p_match for update;
    if not found then
        raise exception 'unknown match' using errcode = 'P0002';
    end if;
    if m.status <> 'reported' or m.voided or m.applied then
        raise exception 'only a reported, not void, not counted match can be approved' using errcode = '22023';
    end if;
    v_ranked := not m.bots and m.room not like 'offline:%'
                and (select count(*) from public.match_players mp where mp.match_id = p_match) >= 2;
    update public.matches set suspicious = false, ranked = v_ranked where id = p_match;
    perform public.cs_match_apply(p_match, 1);
    perform public.cs_log('admin', v.id, 'match.approve', null, p_match, v_reason,
                          jsonb_build_object('suspicious', m.suspicious, 'applied', false),
                          jsonb_build_object('suspicious', false, 'applied', true, 'ranked', v_ranked));
    return jsonb_build_object('applied', true, 'ranked', v_ranked);
end $$;

-- Security log --------------------------------------------------------------------------------

-- Moderators see their own actions and the automatic entries (anti-cheat,
-- reports); admins see everything.
create or replace function public.admin_security_log(p_actor uuid, p_limit integer, p_target uuid)
returns jsonb language plpgsql stable set search_path = '' as $$
declare
    v       public.profiles%rowtype;
    v_all   boolean;
    v_limit integer := greatest(1, least(coalesce(p_limit, 100), 500));
begin
    v := public.cs_admin_actor(p_actor, 'security.view_own');
    v_all := public.cs_has_permission(v.role, 'security.view_all');
    return coalesce((
        select jsonb_agg(to_jsonb(x) order by x.created_at desc) from (
            select sl.id, sl.created_at, sl.source, sl.action, sl.reason, sl.old_value, sl.new_value,
                   sl.match_id, sl.actor_role,
                   (select p.nickname from public.profiles p where p.id = sl.actor_id) as actor,
                   (select p.nickname from public.profiles p where p.id = sl.target_id) as target,
                   sl.target_id
            from public.security_log sl
            where (v_all or sl.actor_id = v.id or sl.source in ('anticheat', 'report'))
              and (p_target is null or sl.target_id = p_target)
            order by sl.created_at desc
            limit v_limit) x), '[]'::jsonb);
end $$;

do $$
declare f record;
begin
    for f in select p.oid::regprocedure as sig
             from pg_proc p join pg_namespace n on n.oid = p.pronamespace
             where n.nspname = 'public' and (p.proname like 'cs\_%' or p.proname like 'admin\_%') loop
        execute format('revoke execute on function %s from public, anon, authenticated', f.sig);
        execute format('grant execute on function %s to service_role', f.sig);
    end loop;
end $$;

-- ==============================================================================
-- 007_hardening.sql
-- ==============================================================================
-- CS-Fusion migration 007: close the old paths and tighten the defaults.
--
-- 1. apply_match_result (v1.2) counted duplicate players several times and
--    trusted the client's times; it is replaced by match_report (005).
-- 2. Every function in the public schema that is not part of an extension:
--    callable only by the service role, never through the REST API by
--    players (the game calls no database function directly).
-- 3. Objects created later in the public schema start with no access for
--    players, so a forgotten grant fails closed instead of open.
--
-- Re-runnable.

drop function if exists public.apply_match_result(jsonb);

do $$
declare f record;
begin
    for f in select p.oid::regprocedure as sig
             from pg_proc p
             join pg_namespace n on n.oid = p.pronamespace
             where n.nspname = 'public'
               and not exists (select 1 from pg_depend d
                               where d.classid = 'pg_proc'::regclass and d.objid = p.oid and d.deptype = 'e') loop
        execute format('revoke execute on function %s from public, anon, authenticated', f.sig);
        execute format('grant execute on function %s to service_role', f.sig);
    end loop;
end $$;

alter default privileges in schema public revoke execute on functions from public, anon, authenticated;
alter default privileges in schema public revoke all on tables from anon, authenticated;
alter default privileges in schema public revoke all on sequences from anon, authenticated;

