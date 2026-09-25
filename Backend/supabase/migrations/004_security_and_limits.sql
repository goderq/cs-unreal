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
