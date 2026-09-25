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
