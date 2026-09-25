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
