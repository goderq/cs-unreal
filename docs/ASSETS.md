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

### Собственные ассеты проекта

| Что | Путь | Как сделано | В git |
|---|---|---|---|
| Все звуки (28 шт.): выстрелы пяти типов, перезарядка трёх типов, смена оружия, щелчок пустого магазина, 4 шага, приземление, попадание в поверхность и в тело, смерть, подбор, респаун, хитмаркер, хедшот, убийство, получение урона, клик и наведение в UI, музыка главного меню | `SourceArt/Audio/*.wav` → `Content/Audio` | синтез в `Scripts/generate_audio.py`: шум, синусы, пила, фильтры, огибающие. Ни одной чужой записи | да |
| Sound Class музыки и эффектов, Sound Mix громкостей, затухание по расстоянию | `Content/Audio/Mix` | `Scripts/bootstrap_stage6.py` | да |
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
