# CS-Fusion

Соревновательный multiplayer FPS на **Unreal Engine 5.8.2** + **Photon Fusion 3
(Unreal SDK)**. Логика на C++, Blueprint только там, где он реально удобнее.

> **Статус: v0.3.0-alpha — Этапы 1–3 проверены на упакованном билде двумя клиентами.**

---

## Что сделано на Этапе 1

- Каркас проекта UE 5.8 с C++ модулем `CSFusion`
- Интеграция Photon Fusion 3; SDK — жёсткая зависимость, при его отсутствии
  сборка падает с понятным сообщением, а не молча ломается
  ([почему](docs/ARCHITECTURE.md#6-offline-режим--и-почему-он-рантаймовый-а-не-через-if))
- Offline-игра сохранена как **рантаймовый** путь: без подключённой комнаты
  каждый RPC вызывает свой `_Receive` напрямую
- **Фасад авторитета** (`UCSAuthority`) — единственная точка, где решается, кто
  «сервер». Сейчас это Master Client, backend для dedicated-сервера заложен
- Слой сессий: connect / create / join / quick match / leave / reconnect
- `CSGameMode` с фазами матча (warmup → round → post-match), выполняемыми
  только на авторитете
- `CSGameState` с реплицируемым состоянием матча (MasterClient-owned)
- FPS-персонаж: разделение FP-руки / TP-тело, камера, Enhanced Input,
  спринт с предсказанием через `FSavedMove`, присед, прыжок
- Детерминированный выбор точки спавна (без гонок между клиентами)
- Python-скрипт, который генерирует Input Actions, IMC, data asset, Blueprint
  персонажа и тестовую карту

## Быстрый старт

```bash
git clone https://github.com/goderq/cs-unreal.git "cs unreal"
```

1. Поставить Visual Studio 2022 с workload **Game development with C++**.
2. Скачать Fusion Unreal 5.8 SDK 3.0 и распаковать в `Plugins/PhotonFusion/`
   → [инструкция](docs/PHOTON_SETUP.md#3-скачивание-и-установка-плагина).
3. ПКМ по `CSFusion.uproject` → **Generate Visual Studio project files**.
4. Собрать конфигурацию `Development Editor`.
5. Открыть проект, выполнить в Python-консоли редактора:
   ```
   exec(open(r"Scripts/bootstrap_content.py").read())
   ```
6. Play с двумя игроками в режиме **Play Standalone**.

## Документация

| Файл | О чём |
|---|---|
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | модель авторитета, ограничения Fusion 3, карта владения объектами, дорожная карта |
| [docs/PHOTON_SETUP.md](docs/PHOTON_SETUP.md) | App ID, установка SDK, настройки, регионы, запуск двух клиентов |
| [docs/TESTS.md](docs/TESTS.md) | приёмочные тесты по этапам |

## Важное предупреждение об архитектуре

Photon Fusion 3 — **shared authority**: выделенного сервера не существует,
геймплей считают сами клиенты. Требование ТЗ «server-authoritative /
dedicated» в чистом виде на Fusion 3 невыполнимо.

Реализованный компромисс: **Master Client выступает авторитетом** для всей
игровой логики, все остальные клиенты только присылают запросы. Вся игровая
механика из ТЗ при этом сохраняется без изменений. Подробный разбор —
[ARCHITECTURE.md §1](docs/ARCHITECTURE.md#1-главная-проблема-тз-у-fusion-3-нет-сервера).

## Лицензии

Код проекта — собственный. Photon Fusion SDK распространяется по лицензии
Photon и в репозиторий не включён. Сторонние ассеты (Этап 6) будут
перечислены в `docs/ASSETS.md` с источником и лицензией каждого.
