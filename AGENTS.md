# AGENTS.md — руководство для агентов по работе с проектом UEFI-tools

## О проекте

Репозиторий `UEFI-tools` содержит форк утилиты [UEFITool](https://github.com/LongSoft/UEFITool) (ветка `new_engine`, версия `NE alpha 76`) и набор тестовых файлов BIOS. В форке реализован функционал редактирования UEFI-образов (insert/replace/remove/rebuild) в графической утилите и добавлена консольная утилита `UEFIEdit` для автоматизированного тестирования.

Полное описание реализации — в [`IMPLEMENTATION.md`](IMPLEMENTATION.md) в корне репозитория. Перед началом работы прочтите его.

## Структура репозитория

```
UEFI-tools/                      (корень git-репозитория = форк LongSoft/UEFITool)
├── AGENTS.md                    # этот файл
├── IMPLEMENTATION.md            # подробное описание реализации редактирования
├── README.md                    # оригинальный README UEFITool + дополнение про форк
├── LICENSE.md                   # BSD-2-Clause
├── .gitignore
├── version.h                    # версия программы
├── meson.build                  # корневой meson
├── CMakeLists.txt               # корневой cmake
├── fw/                          # тестовые файлы (НЕ модифицировать)
│   ├── HNX99TF_200525_original_E5C88C6F.bin    # 16 МБ образ BIOS (Intel flash)
│   ├── Mashinist_DXE_driver_SerialIo_SerialIo.ffs
│   └── MAshinist_DXE_driver_TerminalSrc_TerminalSrc.ffs
├── common/                      # общий движок (парсер, builder, ops, типы, сжатие)
├── UEFITool/                    # графическая утилита (Qt5)
├── UEFIExtract/                 # консольный дампер
├── UEFIFind/                    # консольный поиск
└── UEFIEdit/                    # консольный редактор (добавлен в форке)
```

### Каталог `common/` — общий движок

| Файл | Назначение |
|------|-----------|
| `basetypes.h` | Базовые типы (UINT8/16/32/64), USTATUS-коды, `EFI_GUID`, режимы (CREATE_MODE_*, EXTRACT_MODE_*, REPLACE_MODE_*) |
| `types.h` | `Actions::*` (NoAction/Insert/Rebuild/Remove/...), `Types::*` (Volume/File/Section/...), `Subtypes::*` |
| `ffs.h` | Структуры FFS: `EFI_FFS_FILE_HEADER`, `EFI_COMMON_SECTION_HEADER`, константы типов секций, GUID-ы |
| `ffs.cpp` | Реализация структур, `uint32ToUint24`, `uint24ToUint32`, `guidToUString` |
| `parsingdata.h` | `VOLUME_PARSING_DATA`, `FILE_PARSING_DATA`, `COMPRESSED_SECTION_PARSING_DATA` |
| `treemodel.h/cpp` | `TreeModel` (наследник `QAbstractItemModel` при QT_CORE_LIB, иначе своя реализация) |
| `treeitem.h/cpp` | `TreeItem` — элемент дерева; `clearChildren()` для удаления дочерних элементов |
| `ffsparser.h/cpp` | `FfsParser` — парсинг образа в дерево (6947 строк) |
| `ffsbuilder.h/cpp` | `FfsBuilder` — пересборка дерева в образ; `buildSection` сжимает LZMA/Tiano GUIDed-секции |
| `ffsops.h/cpp` | `FfsOperations` — extract/replace/remove/rebuild; `replace` вызывает `clearChildren` |
| `utility.h/cpp` | `calculateSum8`, `calculateChecksum8/16`, `decompress`, `errorCodeToUString` |
| `LZMA/`, `Tiano/`, `brotli/`, `zlib/` | Сжатие/декомпрессия (bundled) |
| `kaitai/`, `generated/`, `ksy/` | KaitaiStruct-парсеры NVRAM-хранилищ |

### Каталог `UEFITool/` — графическая утилита

| Файл | Назначение |
|------|-----------|
| `uefitool.h/cpp` | `UEFITool` (QMainWindow) — слоты insert/replace/remove/rebuild/saveImageFile |
| `uefitool.ui` | UI-форма (меню, контекстные меню, dock-виджеты) |
| `uefitool.pro` | qmake-проект |
| `uefitool_main.cpp` | `main()` + `UEFIToolApplication` |
| `QHexView/` | Hex-viewer (внешняя библиотека) |
| `searchdialog.*`, `hexviewdialog.*`, `goto*dialog.*` | Диалоги |

### Каталог `UEFIEdit/` — консольный редактор (новый)

| Файл | Назначение |
|------|-----------|
| `uefiedit.h/cpp` | Класс `UEFIEdit`: `Target` (Guid/Path/GuidSection), `parseTarget`, `resolveTarget`, `findItem`, `findSectionByTypeRecursive` |
| `uefiedit_main.cpp` | CLI-парсер: `dump`/`list`/`save`/`insert*`/`remove`/`replace*`/`rebuild` |
| `uefiedit.pro` | qmake (без Qt) |
| `CMakeLists.txt` | cmake |
| `meson.build` | meson |

## Сборка

### Графическая утилита UEFITool (qmake, обязательно)

```bash
cd UEFITool
qmake-qt5 uefitool.pro
make -j$(nproc)
./UEFITool  # ELF 64-bit LSB executable, x86-64
```

После изменения исходников:
```bash
cd UEFITool
touch ../common/ffsbuilder.cpp ../common/ffsops.cpp ../common/treemodel.cpp
make -j$(nproc)
```

### Консольная утилита UEFIEdit

Три системы сборки — все эквивалентны:

```bash
# qmake (быстро, для разработки)
cd UEFIEdit
qmake-qt5 uefiedit.pro
make -j$(nproc)

# cmake
mkdir build && cd build && cmake ../UEFIEdit && make -j$(nproc)

# meson (собирает все утилиты)
meson setup build .
ninja -C build UEFIEdit/UEFIEdit
```

## Проверка (lint / typecheck)

Стандартные линтеры (ruff, cppcheck, clang-format) в проекте не настроены и не требуются. Используется только компилятор:

```bash
# Проверка компиляции GUI
cd UEFITool && make -j$(nproc) 2>&1 | grep -E "error:"

# Проверка компиляции UEFIEdit
cd UEFIEdit && make -j$(nproc) 2>&1 | grep -E "error:"

# Быстрый smoke-тест GUI (headless)
QT_QPA_PLATFORM=offscreen timeout 6 ./UEFITool/UEFITool fw/HNX99TF_200525_original_E5C88C6F.bin
# exit=0 — OK

# Smoke-тест UEFIEdit
./UEFIEdit/UEFIEdit fw/HNX99TF_200525_original_E5C88C6F.bin dump | head
./UEFIEdit/UEFIEdit fw/HNX99TF_200525_original_E5C88C6F.bin save /tmp/out.bin
```

Запускайте эти команды **после каждого изменения** в `common/` или `UEFITool/`/`UEFIEdit/`, чтобы убедиться, что сборка не сломалась.

## Соглашения по коду

- **C++11** (`CONFIG += c++11` в qmake, `-std=gnu++11` в gcc).
- **Без комментариев** в коде, если явно не запрошено. Оригинальный UEFITool следует BSD-стилю с copyright-заголовками в каждом файле — сохраняйте их при создании новых файлов.
- **Отступы**: 4 пробела (см. существующие файлы).
- **Скобки**: открывающая на той же строке для `else if`/`else`, на новой для функций/классов.
- **Имена**: `camelCase` для методов/переменных, `PascalCase` для классов, `UPPER_CASE` для констант/макросов.
- **Структуры FFS** (`EFI_FFS_FILE_HEADER`, `EFI_COMMON_SECTION_HEADER` и т.д.) — не модифицировать, они из спецификации UEFI PI.
- **UString/UByteArray** — абстракции над `QString`/`QByteArray` (при `QT_CORE_LIB`) или bstrlib (без Qt). Код в `common/` должен работать в обоих режимах.
- **`toLocal8Bit()`** возвращает `const char*` без Qt и `QByteArray` с Qt. В консольных утилитах (без Qt) можно писать `std::cerr << s.toLocal8Bit()`; в GUI-коде Qt — `.constData()` если нужен `const char*`.

## Ключевые архитектурные моменты

### TreeModel::addItem — критичная семантика аргумента `parent`

```cpp
UModelIndex TreeModel::addItem(offset, type, subtype, name, text, info,
                                header, body, tail, fixed,
                                const UModelIndex & parent, const UINT8 mode);
```

- `CREATE_MODE_APPEND`/`PREPEND`: `parent` = **контейнер**, новый элемент становится его ребёнком.
- `CREATE_MODE_BEFORE`/`AFTER`: `parent` = **reference-элемент**, `addItem` берёт `parent.internalPointer()` и вставляет новый элемент рядом с его родителем.

Это означает, что для insert-before/after нужно передавать **выбранный элемент**, а не его родителя. Подробно — в IMPLEMENTATION.md, баг 1.

### Отображаемое имя Volume — это не FileSystemGuid

Имя тома в GUI/UEFIEdit (`5C60F367-...`) берётся из `EFI_FIRMWARE_VOLUME_EXT_HEADER.FvName`, который хранится в `VOLUME_PARSING_DATA.extendedHeaderGuid`. `FileSystemGuid` из `EFI_FIRMWARE_VOLUME_HEADER` — это GUID файловой системы (`8C8CE578` для FFSv2, `5473C07A` для FFSv3), одинаковый для всех томов одной версии. При поиске тома по GUID используйте `parsingData`, а не `header`.

### Каскадная пометка для rebuild

Любое изменение (insert/remove/replace) требует пометки `Actions::Insert`/`Remove`/`Replace` для изменённого элемента **и** `Actions::Rebuild` для всех его предков до root. Без этого `FfsBuilder::build()` не пересоберёт образ. Подробно — в IMPLEMENTATION.md, баг 8.

```cpp
for (UModelIndex p = index.parent(); p.isValid() && model->type(p) != Types::Root; p = p.parent()) {
    if (model->action(p) == Actions::NoAction)
        model->setAction(p, Actions::Rebuild);
}
```

### FreeSpace в Volume

`buildVolume` откладывает построение `Types::FreeSpace` детей до конца, суммирует их размер, и после построения остальных детей проверяет, что превышение body помещается в freeSpace. Это позволяет вставлять/заменять файлы, если в томе есть свободное место. Не удаляйте эту логику.

### `replace` и `clearChildren` — критично для замены файлов с секциями

`FfsOperations::replace` (оба режима: `REPLACE_MODE_AS_IS` и `REPLACE_MODE_BODY`) вызывает `model->clearChildren(index)` перед `setBody`. Без этого `FfsBuilder::buildFile`/`buildSection` пересобирают элемент из **старых** дочерних секций, игнорируя новый body — замена не вступает в силу. Подробно — в IMPLEMENTATION.md, баг 9.

### LZMA-компрессия GUIDed-секций при rebuild

`FfsBuilder::buildSection` для `EFI_SECTION_GUID_DEFINED` сжимает новый body в зависимости от GUID из `GUIDED_SECTION_PARSING_DATA`:
- `EFI_GUIDED_SECTION_LZMA`/`LZMA_HP`/`LZMA_MS` → `LzmaCompress` с `dictionarySize` из parsing data.
- `EFI_GUIDED_SECTION_TIANO` → `compressData(EFI_STANDARD_COMPRESSION)`.
- `EFI_GUIDED_SECTION_LZMAF86` → `LzmaCompress` (без x86 BCJ пост-фильтра).
- Прочие GUID → `body = newBody` (без сжатия).

Это позволяет заменять FFS-файлы с LZMA-сжатыми GUIDed-секциями (например, Setup-модуль AMI BIOS). Подробно — в IMPLEMENTATION.md, баг 10.

### `buildSection` сжимает body в общем пути (P1.2)

`FfsBuilder::buildSection` для инкапсулирующих секций (`EFI_SECTION_COMPRESSION`, `EFI_SECTION_GUID_DEFINED`, `EFI_SECTION_DISPOSABLE`, `EFI_SECTION_FIRMWARE_VOLUME_IMAGE`) собирает `newBody` из детей при `rowCount > 0`, либо берёт `model->body(index)` при `rowCount == 0` (после `clearChildren` в `replace`/`replace-body`). Сжатие выполняется **всегда**, независимо от `rowCount` — это позволяет `replace-body` сжатой секции корректно пересжимать тело.

### Универсальная адресация элементов (P2)

Все команды `UEFIEdit` (`insert`/`insert-before`/`insert-after`/`remove`/`replace`/`replace-body`/`rebuild`) принимают `TARGET` — строку, адресующую элемент дерева одним из трёх способов:

| Формат | Пример | Описание |
|--------|--------|----------|
| **GUID** | `5C60F367-A505-419A-859E-2A4FF6CA6FE5` | Поиск по GUID: File (header GUID), Volume (`extendedHeaderGuid` из parsingData), GUIDed/Freeform-section (GUID из parsingData) |
| **Путь** | `0/2/2/27/1/0` | Спуск по дереву: первый элемент — root (всегда `0`), остальные — индексы детей. Выводится в `dump` и `list` |
| **GUID:тип[:N]** | `899407D7-...:0x10` или `899407D7-...:0x10:2` | Найти файл по GUID, затем N-ную (0-индекс, по умолчанию 0) секцию указанного типа (hex) |

Парсер `parseTarget` различает форматы: строка из цифр и `/` → Path; строка с `:` после GUID → GuidSection; иначе → GUID. `resolveTarget` диспетчеризует поиск. `findSectionByTypeRecursive` обходит дерево с DFS-подсчётом вхождений.

`dump` пишет в **stdout** с префиксом пути для копирования. `list` выводит TSV-таблицу (`path<TAB>type<TAB>subtype<TAB>guid<TAB>offset<TAB>size<TAB>name`) в stdout для скриптов.

### Баг парсера: GUIDed-секция теряла GUID в parsingData (P2)

`FfsParser::parseGuidDefinedSection` (два места: строка 3017 и 3465) дважды вызывает `setParsingData`. Первый раз — с `guid`, но без `dictionarySize`. Второй раз — с `dictionarySize`, но **без** `guid` (занулив его). Из-за этого `FfsBuilder::buildSection` не мог определить алгоритм сжатия (GUID был нулевой) и пересборка GUIDed-секций не работала. Фикс: второй `setParsingData` теперь сохраняет `guid` в `pdata.guid`.

### Контрольные суммы FFS

При любой модификации файла (`buildFile` с `Rebuild`/`Replace`/`Insert`) пересчитываются:
- **Header checksum**: `0x100 - (sum8(header) - Header - File - State)`.
- **Data checksum**: реальная если `FFS_ATTRIB_CHECKSUM`, иначе `FFS_FIXED_CHECKSUM` (0x5A для rev1) или `FFS_FIXED_CHECKSUM2` (0xAA для rev2).
- **Tail** (только rev1 + `FFS_ATTRIB_TAIL_PRESENT`): `~IntegrityCheck.TailReference`.

## Тестирование

### Тестовые файлы

- `fw/HNX99TF_200525_original_E5C88C6F.bin` — образ BIOS для всех тестов.
- `fw/Mashinist_DXE_driver_SerialIo_SerialIo.ffs` — GUID `97C81E5D-8FA0-486A-AAEA-0EFDF090FE4F`, 7675 байт.
- `fw/MAshinist_DXE_driver_TerminalSrc_TerminalSrc.ffs` — GUID `54891A9E-763E-4377-8841-8D5C90D88CDE`, 13403 байт.

**Не модифицируйте файлы в `fw/`**. Тестовые образы пишите в `/tmp/opencode/`.

### Ключевые GUID-ы в тестовом образе

| GUID | Тип | Описание |
|------|-----|----------|
| `8C8CE578-8A3D-4F1C-9935-896185C32DD3` | Volume | Первый том (FFSv2, 262072 байт) |
| `5C60F367-A505-419A-859E-2A4FF6CA6FE5` | Volume | Второй том (FFSv2, 216 файлов) — основной для тестов |
| `61C0F511-A691-4F54-974F-B9A42172CE53` | Volume | Третий том (FFSv2, 58 файлов) |
| `A0327FE0-1FDA-4E5B-905D-B510C45A61D0` | File | DXE-драйвер во втором томе (строка 214) — цель для insert-after |
| `CEF5B9A3-476D-49F7-9FDC-E98143E0422C` | File | Первый файл в первом томе |
| `97C81E5D-8FA0-486A-AAEA-0EFDF090FE4F` | File | SerialIo — вставляемый файл |
| `899407D7-99FE-43D8-9A21-79EC328CAC21` | File | Setup (DXE driver, содержит LZMA GUIDed-секцию `EE4E5898-...`) — цель для replace с пересжатием |
| `EE4E5898-3914-4259-9D6E-DC7BD79403CF` | Section GUID | AMI LZMA GUIDed-секция внутри Setup — проверка LZMA-компрессии при rebuild |

### Тестовые скрипты

Три bash-скрипта в `tests/` покрывают smoke-проверки, регрессии и адресацию:

| Скрипт | Тестов | Назначение | Когда запускать |
|--------|--------|-----------|-----------------|
| `tests/smoke.sh` | 10 | Быстрые проверки: rebuild=identity, insert/remove, GUI headless, dump | После каждой сборки |
| `tests/regression.sh` | 9 | Глубокие проверки: clearChildren (byte flip), LZMA round-trip, rebuild-cascade | Перед коммитом |
| `tests/p2_addressing.sh` | 12 | Адресация: dump/list TSV, path==GUID, GUID:sectionType, invalid targets | После изменений в UEFIEdit |

Все скрипты самодостаточны: берут `UEFIEdit`/`UEFITool`/BIOS из путей по умолчанию, создают temp-каталог, требуют только `python3` (для манипуляций с байтами в regression). Запуск:

```bash
tests/smoke.sh        # exit 0 = все 10 прошли
tests/regression.sh   # exit 0 = все 9 прошли
tests/p2_addressing.sh # exit 0 = все 12 прошли
```

Старые ручные тесты (AGENTS.md до P2) сохранены в `tests/regression.sh` как тесты 1-5, 8-9. Тесты 6-7 (clearChildren, LZMA round-trip) автоматизированы в `tests/regression.sh` через python3-хелпер.

### Примеры использования UEFIEdit

```bash
# Dump дерева в stdout (с путями для адресации)
UEFIEdit bios.bin dump

# TSV-листинг для скриптов
UEFIEdit bios.bin list > items.tsv

# Rebuild файла по GUID
UEFIEdit bios.bin rebuild 899407D7-99FE-43D8-9A21-79EC328CAC21 save out.bin

# Rebuild PE32-секции по пути (0/2/2/27/1/0 — из dump/list)
UEFIEdit bios.bin rebuild 0/2/2/27/1/0 save out.bin

# Rebuild PE32-секции по GUID:тип (0x10 = EFI_SECTION_PE32)
UEFIEdit bios.bin rebuild 899407D7-99FE-43D8-9A21-79EC328CAC21:0x10 save out.bin

# Replace-body секции (PE32 внутри Setup)
UEFIEdit bios.bin replace-body 899407D7-99FE-43D8-9A21-79EC328CAC21:0x10 new_pe32.bin save out.bin

# Цепочка операций
UEFIEdit bios.bin \
  insert 5C60F367-... SerialIo.ffs \
  insert-after A0327FE0-... TerminalSrc.ffs \
  remove 97C81E5D-... \
  save out.bin
```

## Git

- Корневой репозиторий — форк [LongSoft/UEFITool](https://github.com/LongSoft/UEFITool) (ветка `new_engine`).
- `origin` — ваш форк на GitHub (push/pull), `upstream` — оригинальный LongSoft/UEFITool (только pull для синхронизации с обновлениями upstream).
- **Не делайте коммиты**, если явно не запрошено пользователем.
- При коммите следуйте стилю существующих сообщений: `"Improve ..."` / `"Add ..."` / `"Fix ..."` — короткое описание в настоящем времени, без префиксов типа `feat:`/`fix:`.

## Что не реализовано (известные ограничения)

- **Нет компрессии Brotli/GZip/Zlib для GUIDed-секций при rebuild**: `buildSection` сжимает только LZMA (`EFI_GUIDED_SECTION_LZMA`/`LZMA_HP`/`LZMA_MS`/`LZMAF86`) и Tiano (`EFI_GUIDED_SECTION_TIANO`) GUIDed-секции. Brotli, GZip, Zlib — только декомпрессия; при rebuild body используется как есть.
- **Нет x86 BCJ пост-фильтра для LZMAF86**: `EFI_GUIDED_SECTION_LZMAF86` сжимается через plain `LzmaCompress` без BCJ-фильтра. Большинство прошивок принимает это, но теоретически возможны несовместимости.
- **LZMA не идемпотентен**: `rebuild` секции внутри LZMA GUIDed-секции пересжимает тело. Даже без изменения данных результат может отличаться от оригинала (разные версии LZMA-компрессора дают разные потоки). `rebuild` **файла** (не секции) = identity, т.к. `buildFile` не пересжимает GUIDed-секции с `NoAction`.
- **Нет переименования UI-секций**: поле `EFI_SECTION_USER_INTERFACE` (имя файла) не обновляется при insert.
- **Нет обновления FIT-таблицы**: при вставке/удалении микрокода FIT не пересчитывается.
- **Нет проверки свободного места перед вставкой**: `buildVolume` сообщает об ошибке только при сохранении, а не при insert.
- **Нет rebase PEI-модулей**: при insert/remove PEI-файлов базы PE32/TE не пересчитываются (нет `rebase`/`patchVtf` из old_engine).
- **Нет VTF-обработки**: Volume Top File не перемещается в конец тома при rebuild.
- **Нет `growVolume`**: если вставляемый FFS не помещается (нет FreeSpace) — `buildVolume` возвращает ошибку, volume не увеличивается.
- **Нет undo/redo** в GUI.
- **Builder messages dock** в GUI отключён (`enableDock(ui->builderMessagesDock, false)` в конструкторе) — можно включить для отладки.
- **Нет `extract`/`extract-body` в UEFIEdit**: для извлечения используйте `UEFIExtract` (отдельная утилита).