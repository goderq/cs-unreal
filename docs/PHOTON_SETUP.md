# PHOTON SETUP

Полная настройка Photon Fusion 3 (Unreal SDK) для этого проекта.

> **Важно.** Fusion **Unreal** SDK 3.0.0 сейчас в статусе **Preview**
> (сборка 1498, 29 июля 2026). Photon прямо пишет: не для релизных игр, API
> может ломаться. Для разработки — годится.

---

## 1. Создание Photon application

1. Зайти на <https://dashboard.photonengine.com> (создать аккаунт, если нет).
2. **Create a New App**.
3. **SDK** → `Fusion`.
4. **SDK Version** → `Fusion 3 Unreal`.
   Это критично: App ID, созданный под `Fusion 2` или под `Realtime`, с этим
   SDK работать не будет.
5. Имя приложения → `cs-fusion`. **Create**.

## 2. App ID

App ID уже прописан в репозитории:

```
e871d2ff-75f2-47c9-8f0d-35e3a847ff92
```

Файл: [`Config/DefaultEngine.ini`](../Config/DefaultEngine.ini), секция
`[/Script/PhotonFusion.FusionSettings]`.

**Проверь в дашборде**, что у этого App ID тип именно `Fusion / Fusion 3
Unreal`. Если он был создан под другой SDK — сделай новый по шагам выше и
замени значение.

> App ID Photon предназначен для встраивания в клиент и не является секретом
> уровня пароля, но он лежит в публичном репозитории и по нему любой может
> отправлять трафик в твою квоту CCU. Если проект станет публичным всерьёз —
> заведи отдельный App ID для релиза и оставь этот для разработки.

## 3. Скачивание и установка плагина

SDK нельзя положить в репозиторий (лицензия Photon, скачивание только из
дашборда), поэтому `Plugins/PhotonFusion/` в `.gitignore`.

1. Открыть <https://doc.photonengine.com/fusion-unreal/current/getting-started/sdk-download>
2. Скачать **Fusion Unreal 5.8 SDK 3.0.0 Preview Build 1498** (`.7z`).
3. Распаковать. Внутри — папка с `PhotonFusion.uplugin`, `Source/`, `Resources/`.
4. Скопировать так, чтобы получилось:

```
cs unreal/
└── Plugins/
    └── PhotonFusion/
        ├── PhotonFusion.uplugin
        ├── Resources/
        └── Source/
```

5. `Build.cs` проекта сам обнаружит плагин и включит `CS_WITH_FUSION=1`.
   Без плагина проект тоже собирается, но в offline-режиме — см.
   [ARCHITECTURE.md](ARCHITECTURE.md#offline-режим).

## 4. Модули и зависимости

Плагин даёт три модуля:

| Модуль | Тип | Назначение |
|---|---|---|
| `PhotonFusion` | Runtime | то, с чем линкуется игровой модуль |
| `PhotonFusionEditor` | Editor | инструменты редактора, вырезается из кука |
| `PhotonFusionBlueprintNodes` | UncookedOnly | BP-ноды Fusion |

`PhotonFusion.Build.cs` ре-экспортирует `Core`, `Engine`, `DeveloperSettings`,
`PhysicsCore`, поэтому в нашем `CSFusion.Build.cs` они не дублируются.

Подключение уже сделано в
[`Source/CSFusion/CSFusion.Build.cs`](../Source/CSFusion/CSFusion.Build.cs) —
ничего править не нужно.

## 5. Настройки проекта

После первого открытия проекта с плагином:

1. **Edit → Plugins** → убедиться, что `Photon Fusion` включён.
2. **Edit → Project Settings → Fusion Settings**:
   - `App Id` → вставить App ID (или проверить, что он подтянулся из ini).
   - `App Version` → `0.1.0`. Это разделяет несовместимые сборки: игроки с
     разным AppVersion не попадут в одну комнату.
3. **Project Settings → Maps & Modes**:
   - `Game Instance Class` → `CSGameInstance`
   - `Default GameMode` → `CSGameMode`

## 6. Выбор региона

Регион задаётся в коде, не в настройках проекта —
[`FCSSessionRequest`](../Source/CSFusion/Multiplayer/CSSessionSubsystem.h):

| `bSelectRegion` | `Region` | Поведение |
|---|---|---|
| `false` (по умолчанию) | игнорируется | пинг всех регионов, выбирается самый быстрый (`EFusionRegionSelectionMode::Best`) |
| `true` | `"eu"`, `"us"`, `"ru"`, … | жёстко заданный регион (`Select`) |

Рекомендация Photon: первый запуск — `Best`, дальше сохранить выбранный регион
и переключиться на `Select`, чтобы не тратить время на пинг.

## 7. Создание и вход в сессию

Весь сетевой вход — через `UCSSessionSubsystem` (C++ и Blueprint):

```cpp
UCSSessionSubsystem* Session = GetGameInstance()->GetSubsystem<UCSSessionSubsystem>();

FCSSessionRequest Request;
Request.RoomName   = TEXT("Match-42");   // пусто = случайная комната
Request.MaxPlayers = 8;
Request.InitialWorld = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Maps/Lvl_Warehouse")));

Session->HostOrJoin(Request);   // создать или войти
Session->QuickMatch(Request);   // быстрый поиск
Session->JoinByName(Request);   // войти по точному имени
Session->LeaveMatch();          // выйти, остаться на облаке
Session->Disconnect();          // полный разрыв
```

События: `OnSessionStateChanged`, `OnSessionJoined`, `OnSessionLeft`,
`OnSessionFailed`, `OnMasterClientChanged`.

## 8. Запуск двух клиентов для теста

### Авто-подключение (временно, вместо меню)

Пока нет главного меню (Этап 5), `UCSGameInstance::OnStart()` сам входит в
комнату, иначе в собранном билде никто не вызвал бы `HostOrJoin` и
мультиплеера бы не было. Ключи командной строки:

| Ключ | Действие | По умолчанию |
|---|---|---|
| `-room=NAME` | имя комнаты | `cs-alpha` |
| `-region=CODE` | жёсткий регион (`eu`, `us`, …) | лучший по пингу |
| `-maxplayers=N` | вместимость комнаты | 8 |
| `-noautoconnect` | не подключаться, остаться offline | — |

Два запущенных клиента с одинаковым `-room` встретятся в одной комнате.
Этот код помечен `ALPHA ONLY` и удаляется, когда появится меню.


**Вариант A — PIE (быстрее всего)**

1. Панель **Play** → стрелка вниз → **Advanced Settings**.
2. `Number of Players` = **2**.
3. `Net Mode` = **Play Standalone**.
   Это важно: Fusion не использует listen-server Unreal, каждое PIE-окно
   должно быть самостоятельным процессом-клиентом.
4. `Run Under One Process` — **снять галочку**. С одним процессом Fusion 3
   Preview имеет известные проблемы со сменой карты.
5. Play.

**Вариант B — отдельные билды**

```bash
"C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe" "C:/Users/gankl/Desktop/cs unreal/CSFusion.uproject" /Game/Maps/Lvl_Warehouse -game -windowed -ResX=1280 -ResY=720
```

Запустить эту команду дважды.

**Проверка**: в любом окне открыть консоль (`~`) и ввести `CSNetInfo`.
Должно вывести backend, authority, состояние сессии, имя комнаты, RTT и
network time.

---

## Требования окружения

| Требование | Статус на этой машине |
|---|---|
| Unreal Engine 5.8 | ✅ `C:\Program Files\Epic Games\UE_5.8` (5.8.2) |
| Visual Studio 2019+ / Rider | ✅ VS 2022 Community, MSVC 14.44.35207 |
| Windows SDK | ✅ 10.0.22621.0 и 10.0.26100.0 |
| Windows 10/11 | ✅ Windows 11 Pro |
| C++17 и выше | ✅ модуль собирается под C++20 |
| Fusion 3 AppId | ✅ выдан |
| Fusion Unreal SDK | ✅ 3.0.0-Preview-1498, EngineVersion 5.8.0 |

### Установка компилятора (для чистой машины)

Без MSVC ни движок, ни плагин не соберутся. **Visual Studio 2022 Community**
(или Build Tools) с компонентами:

- Workload: **Game development with C++**
- Workload: **Desktop development with C++**
- Individual: **MSVC v143 x64/x86 build tools**
- Individual: **Windows 11 SDK (10.0.22621 или новее)**

Ссылка: <https://visualstudio.microsoft.com/downloads/>

Из командной строки (запускать **от администратора**, иначе установщик вернёт
код 5007; путь обязательно в кавычках, иначе код 87):

```bash
"C:\Program Files (x86)\Microsoft Visual Studio\Installer\setup.exe" modify --installPath "C:\Program Files\Microsoft Visual Studio\2022\Community" --add Microsoft.VisualStudio.Workload.NativeGame --add Microsoft.VisualStudio.Workload.NativeDesktop --add Microsoft.VisualStudio.Component.Windows11SDK.22621 --includeRecommended --passive --norestart
```

### Настройки таргетов

`CSFusion.Target.cs` и `CSFusionEditor.Target.cs` используют
`DefaultBuildSettings = BuildSettingsVersion.Latest` и
`IncludeOrderVersion = EngineIncludeOrderVersion.Latest`. Это не косметика:
бинарный движок из лаунчера собран с `Latest`, и любое понижение
`BuildSettingsVersion` меняет `CppCompileWarningSettings`, после чего UBT
отказывается собирать таргет с ошибкой
«modifies the values of properties … has build products in common with
UnrealEditor». По той же причине `CppStandard` задан на уровне модуля
(`CSFusion.Build.cs`), а не таргета.

---

## Источники

- [Fusion Unreal — Intro](https://doc.photonengine.com/fusion-unreal/current/fusion-intro)
- [Fusion Unreal — SDK Download](https://doc.photonengine.com/fusion-unreal/current/getting-started/sdk-download)
- [Fusion Unreal — Quick Start](https://doc.photonengine.com/fusion-unreal/current/getting-started/quick-start-guide)
- [Fusion Unreal — Connection](https://doc.photonengine.com/fusion-unreal/v3/manual/connection)
- [Fusion Unreal — C++ Integration](https://doc.photonengine.com/fusion-unreal/v3/manual/cpp-integration)
- [Fusion Unreal — Ownership](https://doc.photonengine.com/fusion-unreal/v3/manual/replication/ownership)
- [Fusion Unreal — Spawning](https://doc.photonengine.com/fusion-unreal/v3/manual/spawning)
- [Fusion Unreal — Game Classes](https://doc.photonengine.com/fusion-unreal/v3/manual/game-classes)
- [Fusion Unreal — RPC Basics](https://doc.photonengine.com/fusion-unreal/current/getting-started/rpcs-in-depth)
- [Fusion Unreal — Release Notes](https://doc.photonengine.com/fusion-unreal/current/getting-started/release-notes)
