-- 009: sign-in with email + password next to Epic, both linked to ONE profile.
--
-- Email accounts live in Supabase Auth (auth.users: password hashing, recovery
-- codes, refresh tokens). A profile now has up to two sign-in identities:
--   epic_account_id  the Epic account (functions/eos-login)
--   auth_user_id     the Supabase Auth user of an email account (functions/email-login)
-- Each is unique across profiles, so one Epic account or one email can never
-- open two profiles. An identity is linked once and never changed or moved:
-- linking needs proof of both identities in one session (functions/account-link)
-- and runs through link_identity() below, service role only.
-- Re-runnable.

-- Email-only profiles have no Epic account.
alter table public.profiles alter column epic_account_id drop not null;

alter table public.profiles add column if not exists auth_user_id uuid;

do $$
begin
    if not exists (select 1 from pg_constraint
                   where conname = 'profiles_auth_user_id_key' and conrelid = 'public.profiles'::regclass) then
        alter table public.profiles add constraint profiles_auth_user_id_key unique (auth_user_id);
    end if;
    if not exists (select 1 from pg_constraint
                   where conname = 'profiles_auth_user_id_fkey' and conrelid = 'public.profiles'::regclass) then
        -- Deleting the Supabase Auth user (the dashboard) leaves the profile, unlinked.
        alter table public.profiles add constraint profiles_auth_user_id_fkey
            foreign key (auth_user_id) references auth.users(id) on delete set null;
    end if;
end $$;

-- The guard of 003, extended: an identity goes from empty to a value once, then
-- stays. (The only way back to empty is Supabase deleting the Auth user.)
create or replace function public.profiles_guard()
returns trigger language plpgsql set search_path = '' as $$
begin
    if old.epic_account_id is not null and new.epic_account_id is distinct from old.epic_account_id then
        raise exception 'epic_account_id is immutable once linked';
    end if;
    if old.auth_user_id is not null and new.auth_user_id is distinct from old.auth_user_id
       and not (new.auth_user_id is null and not exists (select 1 from auth.users u where u.id = old.auth_user_id)) then
        raise exception 'auth_user_id is immutable once linked';
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
        new.epic_account_id := old.epic_account_id;
        new.auth_user_id := old.auth_user_id;
    end if;
    return new;
end $$;

-- Link a second sign-in identity to a profile. Called by functions/account-link
-- after it has verified BOTH the profile's login token and the new identity.
--   p_kind 'epic'  p_value = Epic account id
--   p_kind 'email' p_value = Supabase Auth user id
-- Errors: P0002 no profile, 22023 bad input, 23505 the identity belongs to another
-- profile (hint 'taken'), 23514 the profile already has a different one of this
-- kind (hint 'already_linked'). Linking what is already linked is a no-op.
create or replace function public.link_identity(p_profile uuid, p_kind text, p_value text)
returns jsonb language plpgsql security definer set search_path = '' as $$
declare
    v_profile public.profiles%rowtype;
    v_owner   uuid;
begin
    if p_kind not in ('epic', 'email') or coalesce(p_value, '') = '' or length(p_value) > 128 then
        raise exception 'bad identity' using errcode = '22023';
    end if;
    select * into v_profile from public.profiles where id = p_profile for update;
    if not found then
        raise exception 'no such profile' using errcode = 'P0002';
    end if;

    if p_kind = 'epic' then
        select id into v_owner from public.profiles where epic_account_id = p_value;
        if v_owner = p_profile then
            return jsonb_build_object('linked', 'epic', 'changed', false);
        end if;
        if v_owner is not null then
            raise exception 'this Epic account belongs to another profile' using errcode = '23505', hint = 'taken';
        end if;
        if v_profile.epic_account_id is not null then
            raise exception 'this profile already has an Epic account' using errcode = '23514', hint = 'already_linked';
        end if;
        update public.profiles set epic_account_id = p_value where id = p_profile;
    else
        select id into v_owner from public.profiles where auth_user_id = p_value::uuid;
        if v_owner = p_profile then
            return jsonb_build_object('linked', 'email', 'changed', false);
        end if;
        if v_owner is not null then
            raise exception 'this email belongs to another profile' using errcode = '23505', hint = 'taken';
        end if;
        if v_profile.auth_user_id is not null then
            raise exception 'this profile already has an email' using errcode = '23514', hint = 'already_linked';
        end if;
        update public.profiles set auth_user_id = p_value::uuid where id = p_profile;
    end if;

    insert into public.security_log (source, actor_id, actor_role, action, target_id, reason, new_value)
    values ('system', p_profile, v_profile.role, 'account.link_' || p_kind, p_profile,
            'the player linked a sign-in identity', jsonb_build_object('kind', p_kind));
    return jsonb_build_object('linked', p_kind, 'changed', true);
end $$;

-- Which identities a profile has (the game shows "email linked / Epic linked").
create or replace function public.profile_identities(p_profile uuid)
returns jsonb language sql stable security definer set search_path = '' as $$
    select jsonb_build_object(
        'epic', p.epic_account_id is not null,
        'email', p.auth_user_id is not null,
        'email_address', (select u.email from auth.users u where u.id = p.auth_user_id))
    from public.profiles p where p.id = p_profile;
$$;

-- Service role only, like every function that changes data (007).
revoke all on function public.link_identity(uuid, text, text) from public, anon, authenticated;
revoke all on function public.profile_identities(uuid) from public, anon, authenticated;
grant execute on function public.link_identity(uuid, text, text) to service_role;
grant execute on function public.profile_identities(uuid) to service_role;
