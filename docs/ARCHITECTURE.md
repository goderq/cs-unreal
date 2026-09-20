# Архитектура CS-Fusion

## 1. Главная проблема ТЗ: у Fusion 3 нет сервера

Я проверил актуальную документацию, а не память. Результат:

**Хорошая новость.** Photon Fusion 3 **для Unreal существует** — «Fusion
Unreal», UE 5.4+, отдельный SDK, отдельный плагин. Это не Unity-only, как было
у Fusion 1/2. Версия под UE 5.8 есть.

**Плохая новость.** Fusion Unreal 3 — это **shared / distributed authority**.
Цитата из официального intro: Fusion «serverless в том смысле, что ни один
инстанс Unreal не выступает сервером». Gameplay симулируется на самих
клиентах. Комната в Photon Cloud хранит состояние и рассылает обновления, но
**ничего не симулирует**.

Отсюда прямое противоречие с ТЗ:

| Требование ТЗ | Реальность Fusion 3 |
|---|---|
| «dedicated/server-authoritative multiplayer» | сервера нет, dedicated под Fusion невозможен |
| «сервер сам проверяет pickup / damage / inventory» | нет процесса, который мог бы это делать |
| «никогда не доверяй клиенту» | по архитектуре клиенты пишут своё состояние сами |

Photon сам пишет в сравнительной таблице, что ranked competitive PvP —
это случай **для dedicated-сервера**, а shared authority хорош для co-op,
casual, sandbox, social.

### Решение (механика ТЗ сохранена полностью)

**Master Client назначается «сервером».**

Fusion выбирает одного пира комнаты Master Client'ом и автоматически отдаёт
ему владение GameState, переназначая его при миграции. Это единственный
стабильный общекомнатный арбитр, который существует. Весь серверный код ТЗ
(проверка урона, патронов, скорострельности, дистанции подбора, владения
предметом, спавн лута, смерть, респавн, боты, match state) выполняется
**только** там, где `UCSAuthority::IsGameAuthority()` истинно.

Клиенты никогда не пишут это состояние. Они отправляют **запрос** — Fusion
RPC с таргетом `TargetMasterClient` — авторитет валидирует и пишет результат в
реплицируемое свойство объекта, которым владеет он сам. Fusion физически
отбрасывает запись не-владельца, так что подделать чужое состояние клиент не
может.

**Что это даёт:**

- ✅ Единственный победитель при одновременном подборе предмета
- ✅ Клиент не может выдать себе AK-47, патроны, HP
- ✅ Клиент не может назначить себе damage
- ✅ Fire rate, дистанция подбора, ammo — проверяются авторитетом
- ✅ Лут при смерти и при disconnect создаёт только авторитет

**Чего это не даёт:**

- ❌ Игрок, который сам стал Master Client, теоретически может читерить.
  При shared authority от этого нет защиты в принципе.
- ❌ Движение остаётся клиентским (иначе не будет отзывчивости). Speed/teleport
  ловятся пост-фактум проверкой на авторитете, а не предотвращаются.

**Путь к настоящей server authority.** Вся проверка авторитета проходит через
один файл — [`Core/CSAuthority.h`](../Source/CSFusion/Core/CSAuthority.h) — и
через один backend-enum `ECSAuthorityBackend`. Значение `DedicatedServer` уже
объявлено. Когда/если проект переедет на нативный dedicated server (или Photon
выпустит серверный режим), меняется `CSAuthority.cpp`, а не игровой код.

---

## 2. Второе уточнение: «Fusion vs Unreal Replication» — ложная дихотомия

ТЗ: «репликация через Fusion, а не через обычную Unreal Replication».

Fusion Unreal 3 **переиспользует разметку Unreal как свой авторский слой**:
`UPROPERTY(Replicated)` и `ReplicatedUsing=OnRep_X` сканируются Fusion,
попадают в его `UFusionTypeDescriptor` и уходят по сети через Photon Cloud
собственным `UFusionNetDriver`. `GetLifetimeReplicatedProps` при этом не
вызывается вообще.

То есть `UPROPERTY(Replicated)` в этом проекте — это **и есть** репликация
через Fusion. Transport не Unreal-овский. Мы всё равно объявляем
`GetLifetimeReplicatedProps`, чтобы те же классы продолжили работать, если
проект когда-нибудь переключат на нативный dedicated server.

---

## 3. Остальные ограничения Fusion 3, влияющие на дизайн

| Ограничение | Последствие для проекта |
|---|---|
| Реплицируемые массивы преаллоцируются, **максимум 64 элемента** | размер инвентаря задаётся через `meta=(FusionArraySize=N)`, слоты фиксированные (Этап 3) |
| RPC **не переигрываются** для late-joiner'ов | всё, что должно пережить вход в комнату, — только в реплицируемых свойствах, не в RPC |
| `AGameMode` выполняется **на каждом** пире локально | любой match-flow код обязан быть под `CS_AUTHORITY_ONLY` |
| `APlayerState` принадлежит **самому игроку** (PlayerAttached) | HP / броня / инвентарь **нельзя** хранить на PlayerState |
| `HasAuthority()` до Fusion-handshake возвращает `true` у всех | читать роль только после `OnObjectReady`, а лучше через `UCSAuthority::CanWrite()` |
| SDK в статусе **Preview** | версионно-зависимый код изолирован в `CSFusionCompat.h` и `CSSessionSubsystem::StartRoomOperation` |

---

## 4. Карта владения объектами

| Объект | Fusion Ownership | Кто пишет | Что хранит |
|---|---|---|---|
| `ACSGameState` | `MasterClient` (автоматически) | авторитет | фаза матча, таймер, счёт команд |
| `ACSMatchDirector` *(Этап 2)* | `MasterClient` | авторитет | **HP, броня, инвентарь, ammo, kills/deaths всех игроков** |
| `ACSPlayerState` | `PlayerAttached` (автоматически) | сам игрок | id, ник, команда — только идентификация |
| `ACSCharacter` | `PlayerAttached` | сам игрок | позиция, поза, view pitch |
| `ACSBotCharacter` *(Этап 7)* | `MasterClient` | авторитет | боты симулируются только на авторитете |
| `ACSWorldPickup` *(Этап 3)* | `MasterClient` | авторитет | лут; подбор арбитрируется, не гонка |
| `ACSProjectile` *(Этап 2)* | `Dynamic` | стрелявший | визуал; урон считает авторитет |

**Почему `ACSMatchDirector`, а не PlayerState:** PlayerState принадлежит
игроку, значит игрок может писать в него что угодно. Единственный способ
сделать HP и инвентарь неподделываемыми в Fusion — держать их в объекте,
которым владеет Master Client. Директор — один актор с массивом записей на
игрока (лимит 64 элемента в самый раз для 16 игроков).

**Почему пикапы `MasterClient`, а не `Transaction`:** `Transaction` отдаёт
решение текущему владельцу. Если владелец — игрок, то при одновременном
подборе двумя игроками решает клиент. Владение Master Client'ом гарантирует,
что предмет получит ровно один.

---

## 5. Паттерн «запрос → валидация → состояние»

Единый шаблон для всего геймплея. Пример на подборе предмета (Этап 3):

```cpp
// --- Клиент -----------------------------------------------------------
void ACSCharacter::TryPickup(ACSWorldPickup* Pickup)
{
    // Ничего не меняем локально. Только просим.
    RequestPickup(Pickup);
}

void ACSCharacter::RequestPickup(ACSWorldPickup* Pickup)
{
#if CS_WITH_FUSION
    if (UCSAuthority::IsSessionActive(this)) { RpcRequestPickup(Pickup); return; }
#endif
    RpcRequestPickup_Receive(Pickup);   // offline: тот же путь
}

#if CS_WITH_FUSION
SEND_FUSIONRPC(CS_RPC_TO_MASTER)
void RpcRequestPickup(ACSWorldPickup* Pickup);
#endif

// --- Авторитет --------------------------------------------------------
void ACSCharacter::RpcRequestPickup_Receive(ACSWorldPickup* Pickup)
{
    CS_AUTHORITY_ONLY(this);

    if (!IsValid(Pickup) || Pickup->IsClaimed())            return;
    if (FVector::Dist(GetActorLocation(), Pickup->GetActorLocation()) > MaxPickupDistance) return;
    if (!Director->HasInventorySpace(PlayerId))             return;

    Director->AddItem(PlayerId, Pickup->GetItemInstance()); // реплицируемое свойство
    Pickup->ConsumeAndDestroy();                            // единственный победитель
}
```

Обрати внимание: обёртка `RequestX()` даёт один и тот же код-пас онлайн и
offline. Это же позволяет тестировать геймплей до установки SDK.

---

## 6. Offline-режим

`CSFusion.Build.cs` проверяет наличие `Plugins/PhotonFusion/PhotonFusion.uplugin`:

- найден → `CS_WITH_FUSION=1`, сеть через Photon;
- не найден → `CS_WITH_FUSION=0`, `FUSION_BODY` и `SEND_FUSIONRPC` становятся
  пустыми макросами, `IsGameAuthority()` всегда `true`, каждый RPC вызывает
  свой `_Receive` напрямую.

Смысл: можно писать и проверять геймплей, не имея SDK, и не иметь двух версий
игрового кода.

---

## 7. Структура исходников

```
Source/CSFusion/
├── CSFusion.Build.cs            детект Fusion, зависимости модулей
├── CSFusion.h/.cpp              точка входа модуля
├── Core/
│   ├── CSLog.h/.cpp             категории логов
│   ├── CSCoreTypes.h            общие enum'ы
│   ├── CSFusionCompat.h         ЕДИНСТВЕННЫЙ мост к Fusion API
│   ├── CSFusionCompatNoop.h     заглушка для offline
│   └── CSAuthority.h/.cpp       ФАСАД АВТОРИТЕТА — весь проект ходит сюда
├── Multiplayer/
│   ├── CSGameInstance.h/.cpp    глобальные Fusion RPC, переживают смену карты
│   └── CSSessionSubsystem.h/.cpp connect / create / join / leave / reconnect
├── GameModes/
│   ├── CSGameMode.h/.cpp        фазы матча (под authority), выбор спавна
│   └── CSGameState.h/.cpp       MasterClient-owned состояние матча
├── Player/
│   ├── CSPlayerController.h/.cpp локальный игрок, режимы ввода, CSNetInfo
│   └── CSPlayerState.h/.cpp     ТОЛЬКО идентификация
├── Characters/
│   ├── CSCharacter.h/.cpp       FPS-персонаж, FP-руки + TP-тело, камера
│   └── CSCharacterMovementComponent.h/.cpp  спринт через SavedMove
└── Input/
    └── CSInputConfig.h/.cpp     data asset со всеми Enhanced Input action'ами
```

Планируемые каталоги (этапы 2–8): `Weapons/`, `Inventory/`, `Items/`,
`Pickups/`, `AI/`, `UI/`, `Audio/`, `Animation/`, `Settings/`.

---

## 8. Дорожная карта

| Этап | Содержание | Статус |
|---|---|---|
| 1 | Архитектура, проект, Fusion-интеграция, персонаж, камера, движение, спавн | ✅ код готов, требует компиляции |
| 2 | `ACSMatchDirector`, стартовый пистолет, hitscan, damage, смерть, респавн | ⬜ |
| 3 | `FItemDefinition`, `UInventoryComponent`, пикапы, оружие в инвентаре | ⬜ |
| 4 | Лут при смерти, лут при disconnect (защита от двойного дропа), синхронизация подбора | ⬜ |
| 5 | Главное меню, браузер сессий, HUD, экран инвентаря, ESC-меню | ⬜ |
| 6 | Анимации (FP/TP), звуки, VFX, замена placeholder-моделей | ⬜ |
| 7 | Боты: AIController, Behavior Tree, Blackboard, Perception, навигация | ⬜ |
| 8 | Настройки, сохранение, оптимизация, анти-чит, тесты, packaging | ⬜ |
