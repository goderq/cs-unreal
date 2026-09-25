-- CS-Fusion backend security tests.
--
-- Run in the Supabase SQL editor AFTER the migrations. Everything happens
-- inside one DO block that always ends by raising an exception, so every row
-- the tests create is rolled back and nothing stays in the database. The
-- result is the error message:
--
--     CS-FUSION SECURITY TESTS: 57 passed, 0 failed.
--
-- or, when something fails, the list of failed checks.
--
-- Roles are simulated the way PostgREST does it: SET ROLE anon/authenticated
-- plus request.jwt.claims. Admin functions are called directly with an actor
-- id, exactly as the admin Edge Function calls them after verifying a token.

create or replace function pg_temp.cs_try(p_role text, p_sub uuid, p_sql text)
returns text language plpgsql as $$
declare
    v_state text := 'ok';
begin
    perform set_config('request.jwt.claims',
        coalesce(json_build_object('role', p_role, 'sub', p_sub)::text, '{}'), true);
    perform set_config('request.jwt.claim.role', p_role, true);
    perform set_config('request.jwt.claim.sub', coalesce(p_sub::text, ''), true);
    execute format('set local role %I', p_role);
    begin
        execute p_sql;
    exception when others then
        v_state := sqlstate;
    end;
    execute 'reset role';
    perform set_config('request.jwt.claims', '', true);
    perform set_config('request.jwt.claim.role', '', true);
    return v_state;
end $$;

-- Runs p_sql as the service role would (directly), returns 'ok' or the SQLSTATE.
create or replace function pg_temp.cs_call(p_sql text)
returns text language plpgsql as $$
begin
    execute p_sql;
    return 'ok';
exception when others then
    return sqlstate;
end $$;

do $$
declare
    v_pass    integer := 0;
    v_fail    text[] := '{}';
    v_player  uuid;
    v_player2 uuid;
    v_mod     uuid;
    v_admin   uuid;
    v_super   uuid;
    v_admin2  uuid;
    v_match   uuid;
    v_start   jsonb;
    v_ticket1 uuid;
    v_ticket2 uuid;
    v_result  jsonb;
    v_got     text;
    v_kills   integer;
    v_matches integer;
    v_tag     text := 'zt' || substr(md5(random()::text), 1, 6);
begin
    -- Test players (every check below compares an expected SQLSTATE or 'ok') ----------
    insert into public.profiles (epic_account_id, nickname) values (v_tag || 'p1', v_tag || '_p1') returning id into v_player;
    insert into public.profiles (epic_account_id, nickname) values (v_tag || 'p2', v_tag || '_p2') returning id into v_player2;
    insert into public.profiles (epic_account_id, nickname, role) values (v_tag || 'm', v_tag || '_mod', 'moderator') returning id into v_mod;
    insert into public.profiles (epic_account_id, nickname, role) values (v_tag || 'a', v_tag || '_adm', 'admin') returning id into v_admin;
    insert into public.profiles (epic_account_id, nickname, role) values (v_tag || 'a2', v_tag || '_adm2', 'admin') returning id into v_admin2;
    insert into public.profiles (epic_account_id, nickname, role) values (v_tag || 's', v_tag || '_sup', 'superadmin') returning id into v_super;
    insert into public.player_stats (profile_id) values (v_player), (v_player2), (v_mod), (v_admin), (v_admin2), (v_super);

    -- 1. Anonymous and signed-in players: read only public data ------------------------
    v_got := pg_temp.cs_try('anon', null, 'select epic_account_id from public.profiles limit 1');
    if v_got = '42501' then v_pass := v_pass + 1; else v_fail := v_fail || ('anon reads epic_account_id: ' || v_got); end if;

    v_got := pg_temp.cs_try('anon', null, 'select role, banned_until, is_admin from public.profiles limit 1');
    if v_got = '42501' then v_pass := v_pass + 1; else v_fail := v_fail || ('anon reads roles/bans: ' || v_got); end if;

    v_got := pg_temp.cs_try('anon', null, 'select id, nickname, created_at from public.profiles limit 1');
    if v_got = 'ok' then v_pass := v_pass + 1; else v_fail := v_fail || ('anon cannot read public profile columns: ' || v_got); end if;

    v_got := pg_temp.cs_try('anon', null, 'select * from public.leaderboard limit 5');
    if v_got = 'ok' then v_pass := v_pass + 1; else v_fail := v_fail || ('anon cannot read the leaderboard: ' || v_got); end if;

    v_got := pg_temp.cs_try('authenticated', v_player, format('select * from public.player_stats where profile_id = %L', v_player));
    if v_got = 'ok' then v_pass := v_pass + 1; else v_fail := v_fail || ('player cannot read stats: ' || v_got); end if;

    v_got := pg_temp.cs_try('authenticated', v_player, 'select * from public.security_log limit 1');
    if v_got = '42501' then v_pass := v_pass + 1; else v_fail := v_fail || ('player reads security_log: ' || v_got); end if;

    v_got := pg_temp.cs_try('authenticated', v_player, 'select * from public.match_tickets limit 1');
    if v_got = '42501' then v_pass := v_pass + 1; else v_fail := v_fail || ('player reads match_tickets: ' || v_got); end if;

    v_got := pg_temp.cs_try('authenticated', v_player, 'select * from public.matches limit 1');
    if v_got = '42501' then v_pass := v_pass + 1; else v_fail := v_fail || ('player reads matches: ' || v_got); end if;

    -- 2. Players write nothing: no renaming, no roles, no bans, no stats -----------------
    v_got := pg_temp.cs_try('authenticated', v_player, format('update public.profiles set nickname = %L where id = %L', v_tag || 'x', v_player));
    if v_got = '42501' then v_pass := v_pass + 1; else v_fail := v_fail || ('player renames themselves: ' || v_got); end if;

    v_got := pg_temp.cs_try('authenticated', v_player, format('update public.profiles set role = ''admin'' where id = %L', v_player));
    if v_got = '42501' then v_pass := v_pass + 1; else v_fail := v_fail || ('player sets own role: ' || v_got); end if;

    v_got := pg_temp.cs_try('authenticated', v_player, format('update public.profiles set banned_until = null where id = %L', v_player));
    if v_got = '42501' then v_pass := v_pass + 1; else v_fail := v_fail || ('player lifts own ban: ' || v_got); end if;

    v_got := pg_temp.cs_try('authenticated', v_player, format('update public.player_stats set kills = 99999 where profile_id = %L', v_player));
    if v_got = '42501' then v_pass := v_pass + 1; else v_fail := v_fail || ('player writes stats: ' || v_got); end if;

    v_got := pg_temp.cs_try('authenticated', v_player, format('insert into public.profiles (epic_account_id, nickname) values (%L, %L)', v_tag || 'y', v_tag || 'y'));
    if v_got = '42501' then v_pass := v_pass + 1; else v_fail := v_fail || ('player inserts a profile: ' || v_got); end if;

    v_got := pg_temp.cs_try('anon', null, format('update public.profiles set role = ''superadmin'' where id = %L', v_player));
    if v_got = '42501' then v_pass := v_pass + 1; else v_fail := v_fail || ('anon sets a role: ' || v_got); end if;

    -- 3. Players cannot call the backend functions through the REST API ----------------
    v_got := pg_temp.cs_try('authenticated', v_player, format('select public.admin_ban(%L, %L, 1, ''abc'')', v_player, v_player2));
    if v_got = '42501' then v_pass := v_pass + 1; else v_fail := v_fail || ('player executes admin_ban: ' || v_got); end if;

    v_got := pg_temp.cs_try('authenticated', v_admin, format('select public.admin_set_role(%L, %L, ''admin'', ''abc'')', v_admin, v_player));
    if v_got = '42501' then v_pass := v_pass + 1; else v_fail := v_fail || ('admin token executes admin function directly: ' || v_got); end if;

    v_got := pg_temp.cs_try('authenticated', v_player, format('select public.match_report(%L, %L, ''{}''::jsonb)', v_player, gen_random_uuid()));
    if v_got = '42501' then v_pass := v_pass + 1; else v_fail := v_fail || ('player executes match_report: ' || v_got); end if;

    v_got := pg_temp.cs_try('authenticated', v_player, format('select public.cs_match_apply(%L, 1)', gen_random_uuid()));
    if v_got = '42501' then v_pass := v_pass + 1; else v_fail := v_fail || ('player executes cs_match_apply: ' || v_got); end if;

    v_got := pg_temp.cs_try('anon', null, 'select public.cs_rate_limit(''x'', 1, 1)');
    if v_got = '42501' then v_pass := v_pass + 1; else v_fail := v_fail || ('anon executes cs_rate_limit: ' || v_got); end if;

    v_got := pg_temp.cs_try('authenticated', v_player, 'select public.apply_match_result(''{}''::jsonb)');
    if v_got in ('42883', '42501') then v_pass := v_pass + 1; else v_fail := v_fail || ('old apply_match_result still callable: ' || v_got); end if;

    -- 4. Role checks inside AdminService ----------------------------------------------------
    v_got := pg_temp.cs_call(format('select public.admin_players_search(%L, '''', 10)', v_player));
    if v_got = '42501' then v_pass := v_pass + 1; else v_fail := v_fail || ('player uses admin search: ' || v_got); end if;

    v_got := pg_temp.cs_call(format('select public.admin_whoami(%L)', v_player));
    if v_got = '42501' then v_pass := v_pass + 1; else v_fail := v_fail || ('player passes whoami: ' || v_got); end if;

    v_got := pg_temp.cs_call(format('select public.admin_players_search(%L, %L, 10)', v_mod, v_tag));
    if v_got = 'ok' then v_pass := v_pass + 1; else v_fail := v_fail || ('moderator cannot search: ' || v_got); end if;

    v_result := public.admin_players_search(v_mod, v_tag || '_p1', 10);
    if (v_result->0->>'epic_account_id') is null then v_pass := v_pass + 1; else v_fail := v_fail || 'moderator sees Epic account ids'; end if;

    v_result := public.admin_players_search(v_admin, v_tag || '_p1', 10);
    if (v_result->0->>'epic_account_id') = v_tag || 'p1' then v_pass := v_pass + 1; else v_fail := v_fail || 'admin does not see Epic account ids'; end if;

    v_got := pg_temp.cs_call(format('select public.admin_ban(%L, %L, 24, ''griefing'')', v_mod, v_player));
    if v_got = 'ok' then v_pass := v_pass + 1; else v_fail := v_fail || ('moderator cannot ban for 24 h: ' || v_got); end if;

    v_got := pg_temp.cs_call(format('select public.admin_ban(%L, %L, 0, ''forever'')', v_mod, v_player2));
    if v_got = '42501' then v_pass := v_pass + 1; else v_fail := v_fail || ('moderator bans permanently: ' || v_got); end if;

    v_got := pg_temp.cs_call(format('select public.admin_ban(%L, %L, 500, ''long one'')', v_mod, v_player2));
    if v_got = '42501' then v_pass := v_pass + 1; else v_fail := v_fail || ('moderator bans for 500 h: ' || v_got); end if;

    v_got := pg_temp.cs_call(format('select public.admin_ban(%L, %L, 24, '''')', v_mod, v_player2));
    if v_got = '22023' then v_pass := v_pass + 1; else v_fail := v_fail || ('ban without a reason accepted: ' || v_got); end if;

    v_got := pg_temp.cs_call(format('select public.admin_ban(%L, %L, 24, ''abuse'')', v_mod, v_admin));
    if v_got = '42501' then v_pass := v_pass + 1; else v_fail := v_fail || ('moderator bans an admin: ' || v_got); end if;

    v_got := pg_temp.cs_call(format('select public.admin_ban(%L, %L, 24, ''abuse'')', v_admin, v_admin2));
    if v_got = '42501' then v_pass := v_pass + 1; else v_fail := v_fail || ('admin bans another admin: ' || v_got); end if;

    v_got := pg_temp.cs_call(format('select public.admin_ban(%L, %L, 24, ''abuse'')', v_admin, v_admin));
    if v_got = '42501' then v_pass := v_pass + 1; else v_fail := v_fail || ('admin bans themselves: ' || v_got); end if;

    v_got := pg_temp.cs_call(format('select public.admin_set_nickname(%L, %L, ''Newname'', ''rename'')', v_mod, v_player2));
    if v_got = '42501' then v_pass := v_pass + 1; else v_fail := v_fail || ('moderator sets a nickname: ' || v_got); end if;

    v_got := pg_temp.cs_call(format('select public.admin_reset_nickname(%L, %L, ''offensive'')', v_mod, v_player2));
    if v_got = 'ok' then v_pass := v_pass + 1; else v_fail := v_fail || ('moderator cannot reset a nickname: ' || v_got); end if;

    v_got := pg_temp.cs_call(format('select public.admin_set_role(%L, %L, ''moderator'', ''trusted'')', v_mod, v_player2));
    if v_got = '42501' then v_pass := v_pass + 1; else v_fail := v_fail || ('moderator grants roles: ' || v_got); end if;

    v_got := pg_temp.cs_call(format('select public.admin_reset_stats(%L, %L, ''cheated'')', v_mod, v_player2));
    if v_got = '42501' then v_pass := v_pass + 1; else v_fail := v_fail || ('moderator resets stats: ' || v_got); end if;

    v_got := pg_temp.cs_call(format('select public.admin_set_role(%L, %L, ''moderator'', ''trusted'')', v_admin, v_player2));
    if v_got = 'ok' then v_pass := v_pass + 1; else v_fail := v_fail || ('admin cannot make a moderator: ' || v_got); end if;

    v_got := pg_temp.cs_call(format('select public.admin_set_role(%L, %L, ''admin'', ''promote'')', v_admin, v_player));
    if v_got = '42501' then v_pass := v_pass + 1; else v_fail := v_fail || ('admin makes an admin: ' || v_got); end if;

    v_got := pg_temp.cs_call(format('select public.admin_set_role(%L, %L, ''superadmin'', ''promote'')', v_super, v_player));
    if v_got = '22023' then v_pass := v_pass + 1; else v_fail := v_fail || ('superadmin granted through the API: ' || v_got); end if;

    v_got := pg_temp.cs_call(format('select public.admin_set_role(%L, %L, ''player'', ''demote'')', v_admin, v_admin));
    if v_got = '42501' then v_pass := v_pass + 1; else v_fail := v_fail || ('admin changes own role: ' || v_got); end if;

    v_got := pg_temp.cs_call(format('select public.admin_set_role(%L, %L, ''admin'', ''promote'')', v_super, v_player));
    if v_got = 'ok' then v_pass := v_pass + 1; else v_fail := v_fail || ('superadmin cannot make an admin: ' || v_got); end if;

    v_got := pg_temp.cs_call(format('select public.admin_set_nickname(%L, %L, %L, ''clash'')', v_super, v_player2, v_tag || '_mod'));
    if v_got = '23505' then v_pass := v_pass + 1; else v_fail := v_fail || ('duplicate nickname accepted: ' || v_got); end if;

    v_got := pg_temp.cs_call(format('select public.admin_set_nickname(%L, %L, ''Administrator'', ''test'')', v_super, v_player2));
    if v_got = '22023' then v_pass := v_pass + 1; else v_fail := v_fail || ('reserved nickname accepted: ' || v_got); end if;

    -- A banned admin can do nothing.
    update public.profiles set banned_until = now() + interval '1 hour' where id = v_admin2;
    v_got := pg_temp.cs_call(format('select public.admin_players_search(%L, '''', 10)', v_admin2));
    if v_got = '42501' then v_pass := v_pass + 1; else v_fail := v_fail || ('banned admin still acts: ' || v_got); end if;

    -- Every action above is in the log, with old and new values.
    if exists (select 1 from public.security_log where actor_id = v_mod and action = 'player.ban'
               and target_id = v_player and reason = 'griefing' and new_value ? 'banned_until' and old_value ? 'banned_until') then
        v_pass := v_pass + 1;
    else
        v_fail := v_fail || 'ban not logged with old/new values';
    end if;
    if exists (select 1 from public.security_log where actor_id = v_admin and action = 'player.role'
               and old_value->>'role' = 'player' and new_value->>'role' = 'moderator') then
        v_pass := v_pass + 1;
    else
        v_fail := v_fail || 'role change not logged';
    end if;

    -- Moderators see only their own admin actions.
    v_result := public.admin_security_log(v_mod, 500, null);
    if not exists (select 1 from jsonb_array_elements(v_result) e where e->>'actor' = v_tag || '_adm') then
        v_pass := v_pass + 1;
    else
        v_fail := v_fail || 'moderator sees other staff actions';
    end if;

    -- 5. Match reports ---------------------------------------------------------------------------
    update public.profiles set banned_until = null where id in (v_player, v_player2);

    v_start := public.match_start(v_player, 'DM', 'Depot', v_tag || ' room', false);
    v_match := (v_start->>'match_id')::uuid;
    v_ticket1 := (v_start->>'ticket')::uuid;
    v_ticket2 := public.match_ticket(v_player2, v_match);

    v_got := pg_temp.cs_call(format('select public.match_start(%L, ''XX'', ''Depot'', ''r'', false)', v_player));
    if v_got = '22023' then v_pass := v_pass + 1; else v_fail := v_fail || ('unknown mode accepted: ' || v_got); end if;

    v_got := pg_temp.cs_call(format('select public.match_start(%L, ''DM'', ''Nowhere'', ''r'', false)', v_player));
    if v_got = '22023' then v_pass := v_pass + 1; else v_fail := v_fail || ('unknown map accepted: ' || v_got); end if;

    -- Someone who did not play cannot report.
    v_got := pg_temp.cs_call(format('select public.match_report(%L, %L, ''{"players": []}''::jsonb)', v_mod, v_match));
    if v_got = '42501' then v_pass := v_pass + 1; else v_fail := v_fail || ('non-participant reports: ' || v_got); end if;

    -- Let the match "last" five minutes, then report it twice over: the second must fail.
    update public.matches set started_at = now() - interval '5 minutes' where id = v_match;
    select coalesce(kills, 0), coalesce(matches, 0) into v_kills, v_matches from public.player_stats where profile_id = v_player;
    v_result := public.match_report(v_player, v_match, jsonb_build_object(
        'winner_team', 0, 'rounds', 0, 'bots', false, 'humans', 2,
        'players', jsonb_build_array(
            jsonb_build_object('ticket', v_ticket1, 'team', 0, 'kills', 6, 'deaths', 4, 'headshots', 2, 'damage', 900, 'won', true),
            jsonb_build_object('ticket', v_ticket2, 'team', 0, 'kills', 4, 'deaths', 6, 'headshots', 1, 'damage', 700, 'won', false))));
    if (v_result->>'ranked')::boolean and not (v_result->>'suspicious')::boolean then
        v_pass := v_pass + 1;
    else
        v_fail := v_fail || ('honest report not ranked: ' || v_result::text);
    end if;
    if (select kills from public.player_stats where profile_id = v_player) = v_kills + 6
       and (select matches from public.player_stats where profile_id = v_player) = v_matches + 1 then
        v_pass := v_pass + 1;
    else
        v_fail := v_fail || 'honest report not counted exactly once';
    end if;
    v_got := pg_temp.cs_call(format('select public.match_report(%L, %L, ''{"players": []}''::jsonb)', v_player, v_match));
    if v_got = '23505' then v_pass := v_pass + 1; else v_fail := v_fail || ('match reported twice: ' || v_got); end if;

    -- The same player 16 times, 300 kills in five minutes, a forged ticket:
    -- counted for nobody, marked suspicious.
    v_start := public.match_start(v_player, 'DM', 'Depot', v_tag || ' room 2', false);
    v_match := (v_start->>'match_id')::uuid;
    v_ticket1 := (v_start->>'ticket')::uuid;
    update public.matches set started_at = now() - interval '5 minutes' where id = v_match;
    select kills into v_kills from public.player_stats where profile_id = v_player;
    v_result := public.match_report(v_player, v_match, jsonb_build_object(
        'winner_team', 0, 'rounds', 0, 'bots', false, 'humans', 1,
        'players', (select jsonb_agg(jsonb_build_object('ticket', v_ticket1, 'team', 0, 'kills', 300, 'deaths', 0,
                                                        'headshots', 300, 'damage', 30000, 'won', true))
                    from generate_series(1, 16))
                   || jsonb_build_array(jsonb_build_object('ticket', gen_random_uuid(), 'kills', 50))));
    if (v_result->>'suspicious')::boolean
       and v_result->'reasons' ? 'duplicate_player' and v_result->'reasons' ? 'kill_rate' and v_result->'reasons' ? 'bad_ticket' then
        v_pass := v_pass + 1;
    else
        v_fail := v_fail || ('forged report not flagged: ' || v_result::text);
    end if;
    if (select kills from public.player_stats where profile_id = v_player) = v_kills then
        v_pass := v_pass + 1;
    else
        v_fail := v_fail || 'forged report changed the stats';
    end if;

    -- Staff can void a counted match: its numbers come back out.
    select id into v_match from public.matches where host_id = v_player and room = v_tag || ' room';
    select kills into v_kills from public.player_stats where profile_id = v_player;
    perform public.admin_match_void(v_admin, v_match, 'test void');
    if (select kills from public.player_stats where profile_id = v_player) = v_kills - 6
       and (select voided and not applied from public.matches where id = v_match) then
        v_pass := v_pass + 1;
    else
        v_fail := v_fail || 'void did not take the match back out';
    end if;
    v_got := pg_temp.cs_call(format('select public.admin_match_void(%L, %L, ''again'')', v_mod, v_match));
    if v_got = '42501' then v_pass := v_pass + 1; else v_fail := v_fail || ('moderator voids a match: ' || v_got); end if;

    -- Offline practice goes to practice stats, not the leaderboard, and nobody
    -- else can take a ticket for it.
    v_start := public.match_start(v_player2, 'DM', 'Depot', null, true);
    v_match := (v_start->>'match_id')::uuid;
    v_got := pg_temp.cs_call(format('select public.match_ticket(%L, %L)', v_player, v_match));
    if v_got = '42501' then v_pass := v_pass + 1; else v_fail := v_fail || ('ticket for someone else''s offline match: ' || v_got); end if;
    update public.matches set started_at = now() - interval '5 minutes' where id = v_match;
    select kills into v_kills from public.player_stats where profile_id = v_player2;
    v_result := public.match_report(v_player2, v_match, jsonb_build_object(
        'winner_team', 0, 'rounds', 0, 'bots', true, 'humans', 1,
        'players', jsonb_build_array(jsonb_build_object('ticket', v_start->>'ticket', 'team', 0, 'kills', 20,
                                                        'deaths', 3, 'headshots', 5, 'damage', 2500, 'won', true))));
    if not (v_result->>'ranked')::boolean
       and (select kills from public.player_stats where profile_id = v_player2) = v_kills
       and (select practice_kills from public.player_stats where profile_id = v_player2) >= 20 then
        v_pass := v_pass + 1;
    else
        v_fail := v_fail || ('offline match ranked or lost: ' || v_result::text);
    end if;

    -- A banned player gets no ticket.
    v_start := public.match_start(v_super, 'TDM', 'OldTown', v_tag || ' room 3', false);
    update public.profiles set banned_until = now() + interval '1 hour' where id = v_player;
    v_got := pg_temp.cs_call(format('select public.match_ticket(%L, %L)', v_player, v_start->>'match_id'));
    if v_got = '42501' then v_pass := v_pass + 1; else v_fail := v_fail || ('banned player got a ticket: ' || v_got); end if;

    -- Rate limit: the 31st match start within an hour is refused.
    begin
        for i in 1..31 loop
            perform public.match_start(v_super, 'DM', 'Depot', v_tag || ' flood ' || i, false);
        end loop;
        v_fail := v_fail || 'match_start rate limit did not trigger';
    exception when others then
        if sqlstate = 'P0001' then v_pass := v_pass + 1; else v_fail := v_fail || ('rate limit wrong error: ' || sqlstate); end if;
    end;

    raise exception 'CS-FUSION SECURITY TESTS: % passed, % failed.%', v_pass, cardinality(v_fail),
        case when cardinality(v_fail) = 0 then ' (all changes rolled back)'
             else E'\nFAILED:\n - ' || array_to_string(v_fail, E'\n - ') end;
end $$;
