-- CS-Fusion migration 008: anti-cheat incidents in the security log (docs/AUDIT.md B11).
--
-- The Master Client suspends a player after repeated violations and removes
-- them from the match after repeated suspensions. It reports each such
-- measure here, through functions/match (action "incident"):
--   - only the host of that match may report;
--   - the offender is named by their participation ticket, never by a
--     profile id the host could pick;
--   - at most 40 incidents per match.
-- The host is a player too, so an incident is a report, not a verdict: it
-- lands in security_log (source 'anticheat') for staff to look at.
--
-- Re-runnable.

create or replace function public.match_incident(p_reporter uuid, p_match uuid, p_payload jsonb)
returns jsonb
language plpgsql
set search_path = ''
as $$
declare
    v_match  public.matches;
    v_kind   text := left(coalesce(p_payload->>'kind', ''), 16);
    v_reason text := left(coalesce(p_payload->>'reason', ''), 200);
    v_ticket uuid;
    v_target uuid;
begin
    select * into v_match from public.matches where id = p_match;
    if not found then
        raise exception 'unknown match' using errcode = 'P0002';
    end if;
    if v_match.host_id is distinct from p_reporter then
        raise exception 'only the host of the match reports incidents' using errcode = '42501';
    end if;
    if v_kind not in ('suspended', 'removed') then
        raise exception 'unknown incident kind' using errcode = '22023';
    end if;
    if not public.cs_rate_limit('incident:' || p_match::text, 40, 7200) then
        raise exception 'too many incidents in this match' using errcode = 'P0001', hint = 'rate_limited';
    end if;

    begin
        v_ticket := (p_payload->>'ticket')::uuid;
    exception when others then
        v_ticket := null;
    end;
    if v_ticket is not null then
        select profile_id into v_target from public.match_tickets where id = v_ticket and match_id = p_match;
    end if;

    perform public.cs_log('anticheat', p_reporter, 'anticheat.' || v_kind, v_target, p_match, v_reason, null, null,
        jsonb_build_object('player_number', p_payload->'player', 'signed_in', v_target is not null));
    return jsonb_build_object('logged', true, 'target_known', v_target is not null);
end $$;

revoke execute on function public.match_incident(uuid, uuid, jsonb) from public, anon, authenticated;
grant execute on function public.match_incident(uuid, uuid, jsonb) to service_role;
