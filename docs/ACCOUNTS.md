# Аккаунты: вход через Epic (EOS) и база Supabase

Вход в игру — только по аккаунту Epic. Пароли игра не видит: логин полностью
на стороне Epic. Профиль, ник и статистика лежат в Supabase.

Как это устроено:

```
игра ──1. вход через Epic──────────────► Epic (EOS)
   │                                      │
   │◄──── токен доступа ──────────────────┘
   │
   └──2. токен──► Supabase, функция eos-login
                     │ спрашивает у Epic, настоящий ли токен
                     │ заводит профиль при первом входе
                     ▼
               ◄── ключ доступа на 12 часов + профиль + статистика

игра ──3. запросы к базе с этим ключом──► Supabase (PostgREST)
авторитет матча ──4. итоги матча───────► функция report-match
```

Ключ сервиса в игру не попадает: что можно читать и писать, решают политики
доступа в базе (`Backend/supabase/schema.sql`).

---

## 1. Продукт в Epic Dev Portal

1. Открой [dev.epicgames.com/portal](https://dev.epicgames.com/portal) и войди
   своим аккаунтом Epic. Если организации ещё нет, создай её (нужно принять
   соглашение разработчика; это бесплатно).
2. **Create Product** → назови, например, `CS-Fusion`.
3. Зайди в продукт → **Product Settings**. Оттуда понадобится:
   - **Product ID**;
   - **Sandbox ID** (для теста — dev-песочница);
   - **Deployment ID** (вкладка Deployments, для той же песочницы).
4. **Product Settings → Clients → Add New Client**:
   - создай **Client Policy** типа **Peer2Peer** или **GameClient** (подойдёт
     политика с доступом к Epic Account Services и Connect);
   - после создания клиента станут видны **Client ID** и **Client Secret**.
5. **Epic Account Services → Create Application** (или **Configure**):
   - в разделе **Permissions** включи как минимум **Basic Profile** (ник) и
     **Online Presence**, если нужен статус;
   - в **Brand Settings** заполни название и политику конфиденциальности —
     без этого вход выдаёт ошибку у чужих аккаунтов;
   - в **Application → Clients** привяжи созданный клиент.
6. **EncryptionKey в портале искать не надо** — Epic его не выдаёт. Это любые
   64 шестнадцатеричных символа, которые ты придумываешь сам: ими SDK шифрует
   облачные сохранения. Сгенерировать: `openssl rand -hex 32`. Менять ключ
   после первых сохранений нельзя — старые данные станут нечитаемыми.

Всё вместе даст шесть значений: ProductId, SandboxId, DeploymentId, ClientId,
ClientSecret, EncryptionKey.

---

## 2. База Supabase

1. В проекте Supabase открой **SQL Editor → New query**, вставь целиком
   `Backend/supabase/schema.sql` и выполни. Создадутся таблицы `profiles`,
   `player_stats`, `matches`, `match_players`, представление `leaderboard`,
   политики доступа и функция начисления статистики.
2. Выложи обе функции из `Backend/supabase/functions/`. Проще всего прямо в
   дашборде: **Edge Functions → Deploy a new function → Via Editor**, вставить
   код, задать имя (`eos-login`, затем `report-match`) и нажать Deploy.
   Переключатель **Verify JWT with legacy secret** оставь включённым: игра
   всегда присылает в `Authorization` либо anon-ключ, либо свой токен, и оба
   подписаны старым секретом.

   Через CLI то же самое (из корня проекта):

   ```bash
   supabase login
   supabase link --project-ref ТВОЙ_PROJECT_REF
   supabase functions deploy eos-login --no-verify-jwt
   supabase functions deploy report-match --no-verify-jwt
   ```

   Функции лежат в `Backend/supabase/functions/`. Ключ проверяется внутри них,
   поэтому проверка платформой отключена (`--no-verify-jwt`).
3. Пропиши секреты функций (**Dashboard → Edge Functions → Secrets** или CLI):

   ```bash
   supabase secrets set EOS_CLIENT_ID=... EOS_CLIENT_SECRET=... \
       SUPABASE_JWT_SECRET=...
   ```

   - `EOS_CLIENT_ID` и `EOS_CLIENT_SECRET` — из Epic Dev Portal (шаг 1.4);
   - `SUPABASE_JWT_SECRET` — **Project Settings → JWT Keys → Legacy JWT
     Secret**;
   - `SUPABASE_URL` и `SUPABASE_SERVICE_ROLE_KEY` Supabase подставляет сам.
4. **Project Settings → General** даст Project URL, а **API Keys → Legacy
   anon, service_role API keys** — ключ `anon public`. Оба идут в игру. Ключ
   `service_role` в игру не попадает никогда.

> Нужен именно **старый** `anon`-ключ, а не новый `publishable`: игра сама
> подписывает свои токены старым JWT-секретом (HS256), и функции проверяют их
> тем же секретом. В новых проектах Supabase текущий ключ подписи — ECC, а
> старый секрет числится предыдущим и пока используется для проверки. Если
> когда-нибудь отключить старые ключи (**Disable JWT-based API keys**), вход
> сломается, и `eos-login` придётся переводить на выдачу настоящей сессии
> Supabase Auth.

---

## 3. Ключи в проекте

Скопируй `Config/Backend.ini.example` в `Config/Backend.ini` и заполни:

```ini
[Supabase]
Url=https://ТВОЙ-ПРОЕКТ.supabase.co
AnonKey=eyJ...

[EOS]
ProductId=...
SandboxId=...
DeploymentId=...
ClientId=...
ClientSecret=...
EncryptionKey=...
```

`Config/Backend.ini` в `.gitignore`: репозиторий публичный, ключи в него
попадать не должны. Сборочная машина может вместо файла задать переменные
окружения `CS_SUPABASE_URL`, `CS_SUPABASE_ANON_KEY`, `CS_EOS_PRODUCT_ID`,
`CS_EOS_SANDBOX_ID`, `CS_EOS_DEPLOYMENT_ID`, `CS_EOS_CLIENT_ID`,
`CS_EOS_CLIENT_SECRET`, `CS_EOS_ENCRYPTION_KEY`.

Игра читает этот файл при старте и подставляет данные EOS прямо в конфигурацию
в памяти (`Source/CSFusion/Account/CSBackendConfig.cpp`), на диск ничего не
пишется.

> В упакованной игре ключи клиента EOS лежат внутри бинарника — так устроен
> любой клиент EOS. Это не «секрет» в строгом смысле: важно лишь не
> выкладывать их в публичный репозиторий, чтобы их нельзя было взять
> автоматически. При утечке клиент в портале можно пересоздать.

---

## 4. Что происходит в игре

- Первый экран — вход. Кнопка открывает окно Epic (оверлей или браузер).
  Повторный запуск проходит молча: SDK помнит сессию.
- После входа игра меняет токен Epic на ключ доступа к базе и показывает меню
  с ником и личной статистикой.
- В матче каждый игрок рассылает свой ник остальным: он виден в килфиде, в
  таблице счёта и на экране смерти.
- Когда матч заканчивается, мастер-клиент отправляет итог в `report-match`:
  убийства, смерти, хедшоты, урон, победа. Боты и незалогиненные игроки не
  учитываются.

Честно о границах: авторитет матча — это машина одного из игроков, а не
выделенный сервер. Функция обрезает неправдоподобные числа, проверяет, что
отправитель действительно играл, и считает каждый матч один раз, но полностью
доверять статистике нельзя, пока нет своего сервера.

---

## 5. Разработка и тесты

- Автотесты не могут проходить вход в Epic, поэтому `Scripts/run_tests.ps1`
  всегда добавляет `-noaccount`: экран входа пропускается, статистика не
  отправляется.
- Тот же ключ удобен для локальной отладки без интернета.
- Если `Config/Backend.ini` нет, игра запускается, но на экране входа честно
  пишет, что аккаунты не настроены, и ссылается на этот документ.
