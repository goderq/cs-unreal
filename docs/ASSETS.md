# Ассеты и лицензии

Все ассеты проекта: откуда взяты, по какой лицензии, что с ними можно делать
и чем их можно заменить. Для каждой группы указано, лежит ли она в
репозитории.

## 1. Что используется сейчас

### Контент Epic Games из установки движка

| Что | Путь в проекте | Откуда | В git |
|---|---|---|---|
| Персонажи Manny и Quinn (скелетные меши, материалы, текстуры) | `Content/Characters/Mannequins/Meshes`, `Materials`, `Textures` | `UE_5.8/Templates/TemplateResources/High/Characters` | нет |
| Анимации: стойки пистолет/винтовка, ходьба и бег в 8 направлениях, прыжок, прицеливание вверх/вниз, стрельба, перезарядка, смена оружия, реакции на попадание, 6 анимаций смерти | `Content/Characters/Mannequins/Anims` | там же | нет |
| Пистолет, винтовка, гранатомёт (скелетные и статические меши, текстуры) | `Content/Weapons/Pistol`, `Rifle`, `GrenadeLauncher` | `TemplateResources/Standard/Weapons` | нет |
| Сеточные материалы и меши для прототипирования карты | `Content/LevelPrototyping` | `TemplateResources/High/LevelPrototyping` | нет |

**Лицензия:** [Unreal Engine EULA](https://www.unrealengine.com/eula/unreal).
Контент можно использовать в проектах на Unreal Engine и поставлять в
составе упакованной (cooked) игры. Распространять исходные `.uasset`
публично EULA не разрешает — только другим лицензиатам UE. Поэтому этих
файлов в репозитории нет. После клонирования их нужно скопировать из своей
установки движка:

```bash
python Scripts/copy_epic_content.py "C:/Program Files/Epic Games/UE_5.8"
```

В релизных архивах (упакованная игра) этот контент есть в скомпилированном
виде, как разрешает EULA.

### Модели оружия: Quaternius «Ultimate Gun Pack» (v1.0)

| Что | Путь в проекте | Откуда | В git |
|---|---|---|---|
| 6 разных моделей оружия: пистолет `Pistol_1`, AK-47 `AssaultRifle_5`, M4 `AssaultRifle2_1`, SMG `SubmachineGun_1`, дробовик `Shotgun_2`, снайперская винтовка `SniperRifle_4`, плюс запасные модели пака | `SourceArt/Weapons/Quaternius/*.fbx` → `Content/Weapons/Quaternius` | [quaternius.com/packs/ultimategun.html](https://quaternius.com/packs/ultimategun.html), официальная ссылка на Google Диск, `License.txt` лежит рядом с FBX | да |
| Материалы оружия: мастер-материал и 13 цветов (дерево, металл, чёрный полимер, стекло прицела…) | `Content/Weapons/Materials` | `Scripts/import_weapons.py`: свой PBR-материал вместо импортированного Phong, цвета по превью пака | да |

**Лицензия:** [CC0 1.0 (public domain)](https://creativecommons.org/publicdomain/zero/1.0/).
Можно использовать, изменять и распространять без ограничений и без
атрибуции, в том числе в репозитории. Автор просит поддержать его на
[Patreon](https://www.patreon.com/quaternius), но это не условие лицензии.

Импорт и подготовка:

```bash
UnrealEditor-Cmd.exe CSFusion.uproject -run=pythonscript -script=Scripts/import_weapons.py
UnrealEditor-Cmd.exe CSFusion.uproject -run=pythonscript -script=Scripts/render_weapon_profiles.py
```

Второй скрипт рисует виды сбоку с координатной сеткой
(`Saved/WeaponProfiles/*.png`). По ним сняты точки хвата, цевья, прицела и
дула в `Config/DefaultGame.ini` → `[/Script/CSFusion.CSWeaponPresentationSettings]`.

### Модели оружия v2.0: Fab (фаза 4, одобрены владельцем 26.09.2026)

Лицензия Fab Standard разрешает использовать эти модели в игре, но не
выкладывать их в публичный репозиторий. Поэтому исходники и готовые ассеты
лежат в папках, которые игнорирует git:
- `Content/FPS_Weapon_Bundle/`;
- `Content/Weapons/Fab/`;
- `SourceArt/_fab/`.

В git попадают только скрипты и точки хвата в `Config/DefaultGame.ini` →
`ReplacementModels`. В копии проекта без этих файлов игра берёт модели
Quaternius выше.

| Слот | Модель | Автор | Лицензия | Откуда |
|---|---|---|---|---|
| AK-47, M4, SMG, нож | KA47, AR4, SMG11 (без приклада), M9 из [FPS Weapon Bundle](https://www.fab.com/listings/8aeb9c48-b404-4dcd-9e56-1d0ecedba7f5) | Deadghost Interactive | Fab Standard, Epic Permanent Collection (бесплатно) | владелец добавил пак в проект UE 5.8 и скопировал `Content/FPS_Weapon_Bundle` |
| Пистолет | [Semi Auto Pistol G-17](https://www.fab.com/listings/ea9c0258-2a32-4d82-bb51-8f18301798ec) | DJHaski | Fab Standard, бесплатный личный тариф | FBX от владельца, `SourceArt/_fab/semi-auto-pistol-g-17` |
| Дробовик | [Pump-Action Shotgun (M870)](https://www.fab.com/listings/bc8a6bc9-5e63-4c68-b303-5b54cc697cd1) | Wilbruh | Fab Standard, бесплатный личный тариф | FBX от владельца, `SourceArt/_fab/pump_action_shotgun` |
| Снайперская | [Modular AWP Sniper Rifle](https://www.fab.com/listings/cd5da902-6505-40b9-b0b5-0463ef3c1e85) | HexmireLive | CC BY 4.0 (нужно указать автора); тайловые текстуры ambientCG (CC0) | FBX от владельца, `SourceArt/_fab/AWP_Sniper_Rifle` |

Как собрать:
1. `Scripts/import_fab_weapons.py`:
   - разворачивает меши под соглашение проекта (ствол вдоль +X, верх +Z);
   - импортирует FBX;
   - делает материалы на мастере `M_FabWeapon`.
2. Точки хвата снимаются так:
   - `Scripts/render_weapon_profiles.py` с переменной `CS_PROFILE_PATHS` —
     профиль модели сбоку;
   - `Scripts/measure_weapon_sections.py` — точные срезы по слотам
     материалов. Правило: опорная ладонь стоит на нижней поверхности цевья.

### Карты: Poly Haven (v1.1)

| Что | Путь в проекте | Откуда | В git |
|---|---|---|---|
| 16 PBR-текстур 2K (Diffuse, Normal GL, ARM): asphalt_02, concrete_floor_worn_001, concrete_wall_003, corrugated_iron, metal_plate, rusty_painted_metal, painted_concrete, red_brick_03, castle_brick_02_red, cobblestone_floor_08, stone_tiles_02, plastered_wall_02, white_plaster_02, yellow_plaster, clay_roof_tiles_02, weathered_planks | `SourceArt/Maps/PolyHaven/<id>/` → `Content/Environment/Textures`, инстансы в `Content/Environment/Surfaces` | [polyhaven.com](https://polyhaven.com), скачаны скриптом `Scripts/download_polyhaven.py` через официальный API | да |
| 10 моделей 1K: Barrel_01, wooden_crate_01, wooden_crate_02, concrete_road_barrier, cardboard_box_01, utility_box_02, old_military_crate, wine_barrel_01, planter_box_01, painted_wooden_bench | `SourceArt/Maps/PolyHaven/<id>/` → `Content/Environment/Props` | то же | да |

**Лицензия:** [CC0 1.0](https://polyhaven.com/license). Можно использовать и
распространять без ограничений и без атрибуции.

Материалы собраны из узлов движка в `Scripts/import_polyhaven.py`:
- `M_EnvTriplanar`: мировая проекция по трём осям, поэтому текстура не
  растягивается на блоках любого размера;
- `M_PropPBR`: обычный PBR по UV для пропсов.

Обе карты (`Lvl_Depot`, `Lvl_OldTown`) целиком строит `Scripts/build_maps.py`.
Этот файл и есть их дизайн. Треугольная призма для фронтонов
(`Content/Environment/Meshes/SM_Wedge`) сделана им же через Geometry Script.

### Звуки выстрелов: The Free Firearm Sound Library (v1.1)

| Что | Путь | Откуда | В git |
|---|---|---|---|
| Выстрелы 6 видов оружия из реальных записей: Walther PPQ (пистолет), AK-47, AR-15 (M4), Carl Gustav M45 (SMG), Benelli Nova (дробовик), Tikka T3 .30-06 (снайперская) | `SourceArt/Audio/Weapons/S_Real_*_Fire.wav` → `Content/Audio/Weapons` | [opengameart.org/content/the-free-firearm-sound-library](https://opengameart.org/content/the-free-firearm-sound-library), авторы Ben Jaszczak, Brian Nelson, Kevin Heras, Matthew Nanney | только нарезанные клипы; исходный архив (194 МБ) лежит в `SourceArt/_download`, это игнорируется git |

**Лицензия:** CC0 1.0: без ограничений, атрибуция не требуется.

Как делаются клипы: `Scripts/prepare_weapon_sounds.py` вырезает первый выстрел
от атаки до естественного хвоста, делает плавное затухание, нормализует и
переводит в 44.1 кГц моно, чтобы звук работал в 3D.

### Собственные ассеты проекта (v1.1)

| Что | Путь | Как сделано |
|---|---|---|
| Осколочная граната: корпус «ананас», запал, рычаг, кольцо с чекой | `Content/Weapons/Grenade/SM_FragGrenade` | Geometry Script в `Scripts/bootstrap_v11.py` |
| Звуки гранаты: взрыв, отскок, чека | `SourceArt/Audio/Weapons/S_Grenade_*.wav` | синтез в `generate_audio.py` |
| Материал защиты на спавне («призрак») | `Content/FX/M_SpawnGhost` | узлы материала в `bootstrap_v11.py` |

### Собственные ассеты проекта

| Что | Путь | Как сделано | В git |
|---|---|---|---|
| Все звуки (28 шт.): выстрелы пяти типов, перезарядка трёх типов, смена оружия, щелчок пустого магазина, 4 шага, приземление, попадание в поверхность и в тело, смерть, подбор, респаун, хитмаркер, хедшот, убийство, получение урона, клик и наведение в UI, музыка главного меню | `SourceArt/Audio/*.wav` → `Content/Audio` | синтез в `Scripts/generate_audio.py`: шум, синусы, пила, фильтры, огибающие. Ни одной чужой записи | да |
| Sound Class музыки и эффектов, Sound Mix громкостей, затухание по расстоянию | `Content/Audio/Mix` | `Scripts/bootstrap_stage6.py` | да |
| v2.0: шаги по металлу, дереву и грунту (по 4) | `SourceArt/Audio/Player/S_Footstep_{Metal,Wood,Dirt}_*.wav` → `Content/Audio/Player` | синтез в `generate_audio.py` (`make_phase3`: шаг + резонатор металла, глухой корпус дерева, хруст грунта) | да |
| v2.0: дерево Sound Class, затухания с окклюзией, concurrency; физические материалы `PM_Metal`/`PM_Wood`/`PM_Dirt` | `Content/Audio/Mix`, `Content/Environment/Physics` | `Scripts/bootstrap_phase3_audio.py` | да |
| v2.0: материалы частиц `M_CS_ParticleAdditive`, `M_CS_ParticleTranslucent` | `Content/FX/Materials` | узлы материалов движка, `Scripts/bootstrap_phase3_fx.py` | да |
| v2.0: 9 систем Niagara (попадания по 4 поверхностям, кровь, дым из ствола, гильза, взрыв, светошумовая) | `Content/FX/Niagara` | коммандлет `-run=CSFXBuilder` (`Source/CSFusion/FX/CSFXBuilderCommandlet.cpp`): копия шаблона движка `DirectionalBurstLightweight` (плагин Niagara, контент UE по EULA) с нашими настройками | да |
| Материалы эффектов: вспышка (аддитивный с радиальной маской), трассер/искры (аддитивный), дым/кровь (полупрозрачный с мягкими краями), след от пули (декаль) | `Content/FX/Materials` | генерируются из узлов материалов движка в `bootstrap_stage6.py` | да |
| Карта, игровые data asset'ы, Input Actions | `Content/Maps`, `Items`, `Weapons/DA_*`, `Input` | `Scripts/bootstrap_content.py` | да |
| UI и HUD | — | код на Slate/Canvas, без текстур | да |

**Лицензия:** собственность проекта. Звуки созданы с нуля, поэтому
никаких сторонних ограничений на них нет.

### Базовые формы движка

`/Engine/BasicShapes` (сфера, цилиндр, плоскость) используются для эффектов,
предметов на земле (кроме оружия) и геометрии карты. Входят в движок,
лицензия — UE EULA.

## 2. Чего не хватает и чем заменить

Честно о том, где сейчас заглушки, и какие есть легальные бесплатные замены.
Скачивание сторонних паков проект не выполняет сам: файлы нужно проверить и
принять их лицензию, а это решение владельца проекта (пак Quaternius скачан
с его разрешения).

| Что сейчас | Проблема | Замена (лицензия) |
|---|---|---|
| Оружие — low-poly модели Quaternius без текстур (v1.0: у каждого своя модель) | стилизованный вид | оружие из [Lyra Starter Game на Fab](https://www.fab.com) (Fab Standard License, только UE) · любые модели с точками хвата в `DefaultGame.ini` |
| Патроны, броня, аптечка — окрашенные скруглённые кубы | не читаются как предметы | [Quaternius — Survival / Ultimate Items](https://quaternius.com) (CC0) · [Kenney — Survival Kit](https://kenney.nl/assets/survival-kit) (CC0) |
| Иконки в инвентаре — цветные плашки с сокращением названия | нет картинок | [Kenney — Game Icons](https://kenney.nl/assets/game-icons) (CC0) · [game-icons.net](https://game-icons.net) (CC BY 3.0, нужна атрибуция) |
| Синтезированные звуки | звучат упрощённо | [Sonniss GDC Game Audio Bundle](https://sonniss.com/gameaudiogdc) (royalty-free, коммерческое использование разрешено) · [Freesound, фильтр CC0](https://freesound.org) |
| От 1-го лица: анимации Manny для плеч и локтей, кисти ставит IK на оружие, перезарядка и смена оружия процедурные (v1.0) | нет отдельных FP-анимаций | FP-анимации из [Lyra](https://www.fab.com) (Fab Standard License, только UE) · [Mixamo](https://www.mixamo.com) (бесплатно, royalty-free в играх; исходники не распространять) |
| Эффекты из базовых форм | просто, без частиц | Niagara-эффекты из бесплатных паков Fab (Fab Standard License) |
| Карта из серых блоков | прототип | текстуры и HDRI [Poly Haven](https://polyhaven.com) (CC0) |

Чтобы заменить ассет, достаточно поменять ссылку: модель оружия и точки
хвата — `DefaultGame.ini` → `CSWeaponPresentationSettings`, баллистика и
звуки оружия —
`DA_Weapon_*` (меши, звуки, стойка, масштаб), предметы — `DA_Item_*`
(`WorldMesh`), анимации — Project Settings → CS Animation, игровые звуки —
Project Settings → CS Audio. Код менять не нужно.
