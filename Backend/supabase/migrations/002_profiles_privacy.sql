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
