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
