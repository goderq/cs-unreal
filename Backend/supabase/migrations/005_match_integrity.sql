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
