# Absolute TODO

## Сборка и структура компилятора

### Выполнено

- [x] Разделить Analyzer на отдельные translation units: access, operators, values и statements.
- [x] Вынести таблицу символов и анализ деклараций типов из `analyzer.cpp`.
- [x] Разделить CodeGen на отдельные translation units: access, operators, values и statements.
- [x] Оставить `analyzer.cpp` и `codegen.cpp` тонкими координаторами.
- [x] Подключить private PCH через `target_precompile_headers` для Analyzer и CodeGen.
- [x] Разделить внутренности CodeGen на state, types, runtime и module.
- [x] Проверить Release-сборку с LLVM 18.1.3.
- [x] Прогнать полный набор тестов: 108/108.

### P0 — реальное ускорение incremental build

- [x] Оставить в `codegen_internal.h` только структуру `Impl`, поля и объявления методов.
- [x] Перенести тела методов `CodeGenerator::Impl` из `.inc` в отдельные `.cpp`:
  - [x] `codegen_types.cpp` — типы, классы, структуры и интерфейсы.
  - [x] `codegen_runtime.cpp` — значения, массивы, managed/raw pointers и runtime-вызовы.
  - [x] `codegen_builtins.cpp` — print, format, async и прочие встроенные операции.
  - [x] `codegen_module.cpp` — функции, глобальные значения, создание LLVM module/object.
  - [x] `codegen_core.cpp` — конструктор и общие ошибки `Impl`.
- [x] Убедиться, что изменение одного visitor-файла не приводит к повторной оптимизации всех методов `Impl`.
  Проверено на `codegen_values.cpp`: пересобираются только его object-файл, библиотека и `absolutec`.
  На WSL `/mnt/f` время уменьшилось примерно с 261 до 236 секунд; критерий ускорения в два раза пока не достигнут.
- [x] Добавить `benchmarks/build-suite/run.bat` для измерения:
  - [x] чистой Release-сборки;
  - [x] повторной сборки без изменений;
  - [x] изменения одного Analyzer-файла;
  - [x] изменения одного CodeGen-файла;
  - [x] пересоздания PCH после изменения заголовка.
- [x] Сохранять результаты build-бенчмарка в CSV вместе с компилятором, генератором, количеством потоков и расположением build-каталога.
  Первый полный запуск на WSL `/mnt/f`, GNU 13.3, Unix Makefiles и четырёх потоках:
  clean Release — 497,18 с; no-op — 13,51 с; Analyzer unit — 224,77 с;
  CodeGen unit — 247,10 с; CodeGen PCH — 375,31 с.
  При переносе build-каталога на Linux FS: clean Release — 90,24 с; no-op — 2,77 с;
  Analyzer unit — 7,44 с; CodeGen unit — 10,89 с; CodeGen PCH — 46,73 с.

### P1 — окружение сборки

- [x] Для WSL хранить build-каталог на файловой системе Linux, а не на `/mnt/f`.
  Build benchmark по умолчанию использует `/root/.cache/absolute/build-benchmark`.
- [x] Добавить CMake preset для `Ninja + Release + LLVM 18`.
  Preset добавлен; в текущем WSL Ninja пока не установлен. Проверен fallback `wsl-release` с Unix Makefiles.
- [x] Добавить Windows preset для MSVC Release с `/MP`.
- [x] Проверить поддержку `ccache` или `sccache` как необязательного ускорителя.
  Добавлена опция `ABSOLUTE_USE_COMPILER_CACHE`; в текущем WSL ни один cache launcher не установлен.
- [x] Исправить предупреждение, где путь к `zstd.h` передаётся как include-directory.
- [x] Явно оформить LibEdit, CURL и X11 как необязательные зависимости, чтобы сообщения конфигурации были понятными.

### P2 — стабильность архитектуры

- [x] Минимизировать зависимости private PCH от часто меняющихся заголовков AST.
  Analyzer PCH теперь содержит только стабильные заголовки стандартной библиотеки, CodeGen PCH — LLVM и стандартную библиотеку; AST, analyzer и plugin API подключаются явно после PCH.
- [x] Добавить forward declarations в публичные заголовки Analyzer и CodeGen, где это возможно.
  `CodeGenerator` использует forward declaration для `Analyzer`; из `analyzer.h` удалена лишняя зависимость от `type.h`.
- [x] Добавить CI-проверку чистой Debug/Release-сборки без заранее созданного PCH.
- [x] Добавить CI-проверку полного `ctest`.
- [x] Документировать назначение каждого внутреннего модуля в `docs/compiler-architecture.md`.

### P1.5 — Native Windows без WSL

- [x] Обеспечить полную сборку Absolute через MSVC и Windows CMake без `wsl.exe`.
- [x] Автоматически находить native Windows LLVM development package и проверять его версию.
- [x] Добавить понятную диагностику и инструкцию установки, если Windows LLVM не содержит CMake development files.
- [x] Добавить native Windows варианты `run.bat` для build, array и pointer/object benchmark suites.
- [x] Убрать обязательную проверку WSL из benchmark-скриптов при выбранном Windows backend.
- [x] Собирать и загружать syntax/math/shader/desktop plugins как `.dll` без Linux-промежуточных шагов.
- [x] Проверить генерацию `.obj`/`.exe`, native runtime и C ABI через MSVC linker.
- [x] Запускать все доступные semantic, emit и runtime tests непосредственно на Windows.
  На Windows проходят 98/98 тестов; десять Linux-only `lli`-тестов заменены покрытием через скомпилированные `.exe`.
- [x] Добавить Windows Release build benchmark: clean, no-op, Analyzer unit, CodeGen unit и PCH.
  Первый native MSVC запуск: clean — 44,82 с; no-op — 1,17 с; Analyzer unit — 6,38 с;
  CodeGen unit — 5,43 с; CodeGen PCH — 17,11 с.
- [x] Сравнить MSVC `/MP` с Ninja + `clang-cl` и выбрать рекомендуемый Windows preset.
  Рекомендуется MSVC `/MP`: LLVM 18 `clang-cl` несовместим с STL из Visual Studio 2026,
  который требует Clang 19 или новее. Скрипт определяет это до запуска CMake.
- [x] Добавить необязательный `sccache` для MSVC/clang-cl.
  Флаг `build-windows.bat --sccache` использует локальный Ninja и отдельный build-каталог.
- [x] Добавить CI job с полным Windows LLVM backend на воспроизводимом portable LLVM 18.1.8 SDK.
- [x] Документировать workflow в `docs/windows-build.md`.

## Критерии готовности ускорения

- [x] Изменение одного visitor-файла пересобирает только соответствующий объектный файл и необходимые библиотеки/исполняемый файл.
- [x] PCH не пересоздаётся при изменении обычного `.cpp`.
- [x] Incremental CodeGen build минимум в два раза быстрее прежней монолитной сборки на одной и той же машине.
- [x] После каждого этапа проходят все 108 тестов.
- [x] Build-бенчмарк воспроизводится одной командой из `.bat`.

## Функциональность языка

Этот раздел описывает roadmap самого Absolute. Отмеченными считаются только
возможности, которые уже представлены в документации и тестах проекта.

### Базовый язык

- [x] Переменные, функции, возвращаемые значения и области видимости.
- [x] `if`, `for`, `while`, `do-while`, `foreach`, `break` и `continue`.
- [x] Перегрузка функций и методов.
- [x] Методы расширения.
- [x] Script-style top-level code без явного `main`: executable statements и
  локальные script-переменные заворачиваются в скрытый `int32 main()` с
  автоматическим `return 0`; смешение top-level кода с explicit `main` запрещено.
- [x] Функциональные значения и лямбды.
  Поддерживаются captureless-функции, expression/block-body, вывод и явное
  указание возвращаемого типа, immutable capture-by-value, копирование closure,
  переназначение и вложенные escaping-замыкания.
- [x] Пространства имён, файловые и namespace-импорты (включая точечный `std.collections`, относительный `./std/collections` и FQN `std.collections.Vector`).
- [x] Строковые шаблоны, форматирование, `print` и `println`.
- [x] Добавить `switch`/`match` и проверку полноты вариантов.
  `match` проверяет полноту `bool` и enum-вариантов; для integer/char требуется `default`.
  Ветки не имеют неявного fallthrough.
- [x] Определить модель ошибок языка: typed unchecked exceptions для аварийных ошибок;
  будущий `Result<T, E>` остаётся обычным generic-типом для ожидаемых ошибок.
  Portable ABI, ownership, cleanup, async и FFI описаны в `docs/error-model.md`.
- [x] Добавить `throw`, typed `try`/`catch`/`finally`, portable propagation ABI,
  cleanup на всех выходах и перенос исключений через `await`.
- [x] Добавить `defer` для гарантированного LIFO cleanup при обычном выходе,
  `return`, `break`, `continue` и распространении исключения.
- [x] Добавить атрибуты/аннотации для compiler и plugin metadata.
- [x] Спроектировать и реализовать мономорфизируемые generics для функций,
  структур, классов и интерфейсов; constraints/traits остаются следующим слоем.
- [x] Добавить type aliases в ядро, не зависящие от plugin prelude.
  `using Alias = Type;` прозрачно разрешает primitive, aggregate, concrete generic,
  array, task и pointer-типы, поддерживает namespaces и выявляет циклы.
- [x] Определить правила `const`/immutability для переменных, параметров, полей и методов.
  Const binding запрещает mutation/delete/mutable address; const value objects и
  массивы неизменяемы вглубь, pointer binding остаётся shallow, а const-метод не
  меняет поля и не вызывает non-const методы.

### Типы и ООП

- [x] Value-типы `struct`, поля, конструкторы и методы.
- [x] Классы, наследование, virtual dispatch и деструкторы.
- [x] Raw и managed pointers на структуры и классы.
- [x] Интерфейсы, наследование интерфейсов и реализация несколькими интерфейсами.
- [x] Проверка перегрузок и возвращаемых типов при реализации интерфейса.
- [x] Реализовать static-поля и static-методы классов и структур в CodeGen.
  Static-поле не входит в object layout, static-метод не имеет `this`; до появления
  module initialization допускаются constant scalar/string/raw-pointer инициализаторы,
  а static-члены generic-типов остаются отдельным слоем.
- [x] Добавить автоматическую цепочку base-конструкторов и явный вызов `base(...)`.
  `base(...)` разрешён только первой инструкцией конструктора; если он опущен,
  компилятор вызывает нулевой конструктор базового класса и при необходимости
  синтезирует конструкторы для всей цепочки наследования.
- [x] Добавить default-реализации методов интерфейсов.
  Default-методы проходят полный parser/analyzer/LLVM pipeline, поддерживают
  namespace и diamond-наследование; метод класса имеет приоритет, а конфликт
  разных defaults требует явной реализации в классе.
- [x] Добавить properties в классы, структуры и интерфейсы.
  Поддерживаются explicit и auto `get`/`set`, read-only/write-only свойства,
  accessors с отдельным уровнем доступа, virtual/override, interface/default
  dispatch, generic-типы и backing storage без изменения публичного layout API.
- [x] Добавить indexers для пользовательских контейнеров.
  Поддерживаются перегруженные `this[...]` с explicit `get`/`set`, отдельным
  доступом accessor-ов, class/struct, virtual/override, generic-типы и
  raw/managed interface dispatch. В интерфейсе `;` означает контракт;
  auto-indexer в class/struct запрещён, потому что индекс требует явной модели хранения.
- [x] Добавить static-члены интерфейсов.
  Интерфейсы владеют public static scalar/string/enum/raw-pointer полями и
  перегруженными static-методами с телом. Они вызываются через имя интерфейса,
  наследуются без копирования storage, не входят в vtable и не создают контракт
  для класса. Static abstract methods отложены до generic constraints.
- [x] Завершить и протестировать правила доступа `public`/`protected`/`private`.
  Доступ проверяется для instance/static полей и методов, конструкторов, наследования
  и реализаций интерфейсов; protected разрешён объявляющему и производным классам,
  private — только объявляющему типу. Члены без модификатора остаются public.
- [x] Добавить безопасные downcast/type-test операции (`is`, `as`).
  Runtime-проверка использует vtable динамического класса, поддерживает классы и
  интерфейсы для raw/managed ссылок; неудачный `as` возвращает null без смены ownership.
- [x] Определить ABI и правила копирования/перемещения больших value-типов.
  `struct` размером до 16 байт передаётся и возвращается напрямую; более крупный
  value-тип получает изолированную caller-side копию аргумента и скрытый result
  pointer. Присваивание, параметры и return сохраняют value semantics, а
  copy/move-elision разрешён только без наблюдаемого aliasing. Правила и порядок
  скрытых параметров описаны в `docs/value-type-abi.md` и проверяются LLVM/native
  тестами функций, методов и конструкторов.

### Память и указатели

- [x] Managed `T*` с generation-check и автоматическим освобождением владельца.
- [x] `raw T*`, адресная арифметика, сравнение и явный `delete`.
- [x] Flow-анализ и диагностика uninitialized, expired и invalid pointers.
- [x] Передача и возврат raw/managed pointers из функций.
- [x] Добавить автоматическое освобождение heap-копий массивов и безопасную передачу
  ownership через `return`: дескриптор хранит отдельные `data`/`owner`, параметры
  заимствуют, slices сохраняют корневого владельца, а временные результаты очищаются
  после вызова или при отбрасывании.
- [x] Формализовать ownership для managed- и array-полей классов и структур:
  поля владеют принятым свежим ресурсом, перезапись очищает старое значение,
  синтетический деструктор агрегата освобождает поля в обратном порядке, а
  динамическое удаление class/interface вызывает деструктор через slot 0 vtable.
- [x] Добавить явную операцию `move` для передачи resource-owning структур между
  переменными, параметрами и return; до этого их неявное копирование запрещено.
- [x] Открыть ownership-адаптер для структур и плагиновых типов: поддержка пользовательского метода `destroy()`, проверка ресурсоёмкости (`TypeOwnsResources`), автоматический вызов деструктора при завершении области видимости/return/delete и передача владения через `move(...)`.
- [x] Не вводить отдельный тип/ключевое слово `borrow`: managed-параметры уже являются
  безопасными non-owning заимствованиями, slices всегда остаются zero-copy views, а
  `raw` используется только для явно небезопасного доступа.
- [x] Усилить compile-time escape-проверки без нового runtime-вида указателя:
  локальные массивы и borrowed slices нельзя вернуть или сохранить в поле без
  `copy(...)`, а managed subscriber и managed-поле агрегата нельзя вернуть как
  передаваемого владельца — в том числе через метод временного объекта.
- [x] Закрыть lifetime/ABI-границу async-captures и результатов: текущий task-context
  принимает только независимые от lifetime скаляры и enum; managed/raw pointers,
  arrays/slices, string, func/task и aggregate value-типы отклоняются Analyzer до
  CodeGen. Новый runtime-вид указателя не добавлялся.
- [x] Исследовать `T&`/`const T&` для больших resource-free value-типов отдельно
  от lifetime correctness: parameter-only borrow по ABI является `nonnull nocapture`
  pointer без нового runtime pointer kind; escape/alias/async-границы зафиксированы,
  benchmark 128-байтного struct показал 1.19x на 20 млн opaque-вызовов.
- [x] Реализовать parameter-only `const T&`/`T&` по `docs/value-references.md`:
  `ref T`/`const ref T` остаются source-алиасами и сразу нормализуются к единой
  ampersand-signature; parser contracts, conservative mutable-alias checking, temporary
  lifetime для `const T&`, virtual/interface/constructor ABI и LLVM
  `nonnull`/`nocapture`/`readonly`; async/C ABI/closures/resources отсекаются.
- [x] Определить и реализовать `weak T*` для явных non-owning managed references:
  используется существующий generation-checked handle без нового runtime-вида;
  strong-to-weak разрешён, обратное преобразование/`new`/`move`/`delete` запрещены,
  weak-поля не очищают pointee и позволяют безопасные graph back-edges.
- [x] Доделать `move(owner)` для managed `T*`: destination получает owner-роль,
  source зануляется и становится compile-time moved-from до новой инициализации,
  lifetime существующих subscriber/weak алиасов переносится на нового владельца.
  Move subscriber/weak/const/invalid, потерянный результат и передача ownership в
  обычный borrowed-параметр отклоняются Analyzer; runtime handle не менялся.
- [x] Проверить циклические графы объектов и выбрать стратегию их очистки:
  strong managed-поля образуют лес уникального владения и рекурсивно очищаются;
  parent/peer/cross-link рёбра используют `weak T*` и не участвуют в cleanup.
  Явные strong back-edges отклоняются как `E_MANAGED_OWNERSHIP_CYCLE`; новый GC,
  refcount, cycle collector или runtime-вид указателя не добавлялся.
- [x] Добавить sanitizer-набор тестов на use-after-free, double-free и утечки:
  compile-time flow diagnostics дополнены настоящими `--sanitize=address`
  executables; ASan подтверждает heap-use-after-free/double-free, а managed runtime
  аварийно завершает процесс при оставшемся generation-slot. Флаг sanitizer
  передаётся в LLVM object pipeline и native linker на Windows/Linux.
- [x] Оптимизировать managed dereference и доказать корректность удаления
  bounds/lifetime checks в Release: локальный неизменённый managed owner хранит
  cached pointee после единственной проверки allocation; в hot loop отсутствуют
  slot bounds/generation calls. Borrowed/subscriber/weak доступ сохраняет checked
  fast path, `delete` зануляет cache, а dereference после delete отклоняется Analyzer.

### Массивы, slices и коллекции

Архитектурная модель snapshot/COW и transient builder/cursor описана в
`docs/isolation-model.md`; generation-check не является её гарантией safety.

- [x] Одномерные и многомерные прямоугольные массивы.
- [x] Локальные и глобальные массивы, литералы и runtime-размеры.
- [x] Параметры и возврат массивов.
- [x] Одномерные slices без копирования.
- [x] Явное независимое копирование массивов и slices через `copy(...)`; скрытое копирование при `return` удалено.
- [x] `foreach` по массивам и slices.
- [x] Проверка выхода за границы.
- [x] Добавить многомерные slices.
- [x] Добавить пользовательский протокол iteration и lowering `foreach`.
- [x] Сделать iterators `Vector`/`Map`/`Set` безопасными через immutable snapshot:
  `iterate()` создаёт независимую копию видимой части backing storage, поэтому
  push/remove/clear и замена элементов исходной коллекции физически не могут
  инвалидировать iterator. Lowering `foreach` владеет managed iterator и очищает
  его snapshot на normal exit, `break` и `return`; generation panic не нужен.
- [x] Заменить eager copy при `iterate()` на разделяемую immutable-версию backing
  storage с copy-on-write. Это оптимизация времени и памяти без изменения уже
  реализованной snapshot-семантики: iterator продолжает видеть версию на момент
  `iterate()` и не требует lexical borrow checker.
- [x] Скрыть mutable backing стандартных коллекций за private runtime capability:
  safe API не возвращает адрес элемента или storage. Для изменяющего обхода дать
  transient builder/cursor, который форкается от immutable snapshot и публикует
  новую collection только через `finish()`; старые aliases остаются корректными и
  видят старую версию. `move` может разрешить reuse уникального storage только как
  оптимизация, но не как условие safety. Unsafe raw/FFI export всегда создаёт
  отдельную копию или явно выходит из safe-модели.
- [x] Добавить динамические коллекции: `Vector`, `Map`, `Set` (queue/deque остаются следующим шагом).
- [x] Добавить стандартные алгоритмы: sort, search, transform, reduce и filter.
- [x] Реализовать Release-elimination доказуемо лишних bounds checks.
- [x] Добавить SIMD/vectorization-тесты для числовых массивов.

### Async и параллельность

Task-isolate, закрытый message envelope и transfer capsule описаны в
`docs/isolation-model.md`; общий mutable object graph между tasks не допускается.

- [x] `async`-функции, `spawn`, `await` и runtime worker pool.
- [x] Проверка незавершённых локальных tasks анализатором.
- [x] Compile-time проверка task payload/result: только scalar/enum ABI без
  заимствованных pointers, slices, строк и агрегатов.
- [x] Добавить scheduling-атрибуты для tasks: `@task(core, priority, role)` задаёт
  defaults async-функции, `@spawn(...)` переопределяет отдельный запуск; runtime
  использует priority-очередь, временную CPU affinity и доступную через `std.task` роль.
- [x] Добавить async-методы классов и структур: static-методы используют обычный
  task ABI; instance-методы требуют `const` и стабильный именованный receiver без
  собственных ресурсов; virtual class dispatch выполняется внутри task thunk,
  а mutable/raw/subscriber/temporary receivers отсекаются анализатором. Это
  закрывает текущую lifetime-границу, но ещё не является полной гарантией
  отсутствия data races для будущих pointer/aggregate payloads.
- [x] До расширения task payload ABI ввести task-isolates: у каждой task свой
  object/handle domain, а `spawn` не захватывает произвольное окружение и принимает
  только закрытый message envelope. Обычный managed/raw/weak pointer не имеет
  представления в envelope и потому архитектурно не может сослаться на mutable
  объект другой task; это ограничение ABI/runtime capability, а не соглашение и
  не borrow checker.
- [x] Разрешить ровно два способа пересечь isolate boundary: immutable value/blob
  копируется или разделяет read-only backing; уникальный object graph передаётся
  только внутри sealed transfer capsule. Capsule не создаётся из произвольного
  `move(pointer)`: его storage изначально создаётся opaque, не выдаёт обычных
  pointer aliases и при отправке атомарно rehome-ится в domain получателя. Старый
  capability после отправки непригоден независимо от Analyzer; одноразовый `await`
  возвращает результат тем же envelope ABI.
- [x] Добавить cancellation tokens и timeout.
- [x] Добавить channels и concurrent queues поверх того же message envelope:
  channel не принимает произвольные адреса/объекты, а только immutable message или
  consuming transfer capsule, поэтому не создаёт shared mutable alias.
- [x] Добавить async file/network I/O.
- [x] Добавить `select`/`whenAny` для ожидания нескольких tasks.
- [x] Общий mutable state предоставлять только как opaque concurrent capability
  (`Atomic`, `MutexCell`, semaphore или actor/service): внутреннее `T` нельзя
  извлечь как pointer/reference, все операции capability синхронизированы, а lock
  guard task-local и runtime не позволяет пронести его через `await`/channel.
- [x] Сделать managed runtime domain-aware для transfer capsule (atomic detach,
  publication, rehome и destroy). Обычный managed handle остаётся task-local и не
  становится способом совместного доступа между domains.
- [x] Расширить plugin resource descriptor операциями `copy_message`,
  `make_immutable` или `detach/rehome`; без них plugin resource является task-local
  и message ABI его не принимает.

### Модули, проекты и пакеты

- [x] `.absproj`, несколько source-файлов и рекурсивные импорты.
- [x] Namespace imports.
- [x] Подключение native libraries через проект.
- [x] Добавить package manifest и lock-файл зависимостей.
- [x] Добавить локальный/удалённый package registry.
- [x] Добавить версионирование модулей и проверку конфликтов зависимостей.
- [x] Добавить отдельные library/application targets в одном workspace.
- [x] Добавить incremental module cache вместо повторного анализа всех файлов.

### Plugin API

#### Готовая основа

- [x] Versioned C ABI для syntax plugins.
- [x] Новые ключевые слова и lowering в обычный Absolute AST.
- [x] Opaque AST nodes с собственным анализом и генерацией LLVM IR.
- [x] Plugin prelude, операторы, extension methods и встроенные типы плагина.
- [x] `.absplugin` manifests, semver-зависимости, обнаружение циклов,
  топологическая загрузка и `provides`/`requires` capabilities.
- [x] Безопасный editor-sidecar для чтения keywords, namespaces, типов,
  функций и snippets без загрузки native-библиотеки в IDE.
- [x] Math, shader и desktop example plugins.

#### P0 — стабильный ABI и безопасное расширение парсера

- [x] Разделить capability-интерфейсы на `AbsoluteCompilerPluginV1`,
  `AbsoluteLanguagePluginV1`, `AbsoluteEditorPluginV1` и
  `AbsoluteRuntimePluginV1`; один package может предоставлять несколько
  независимых интерфейсов.
- [x] Добавить `struct_size`, capability bitset/table, reserved fields и
  `required_host_version` во все расширяемые C ABI descriptors; использовать
  только ABI-safe типы, opaque handles и пары `pointer + length`.
- [x] Реализовать capability/version negotiation между host и plugin с
  диагностикой недостающих `parser.*`, `semantic.*`, `ir.*`, `backend.*`,
  `ide.*` возможностей до вызова plugin-кода.
- [x] Зафиксировать lifecycle `load -> initialize -> begin_compilation ->
  begin_module -> end_module -> end_compilation -> shutdown -> unload`,
  правила thread safety, параллельных вызовов, владения памятью и времени жизни
  строк/AST handles.
- [x] Добавить ABI compatibility matrix и тесты старого/нового host и plugin на
  Windows и Linux, включая неизвестные поля, урезанный `struct_size` и
  отсутствующие optional capabilities.
- [x] Дать parser plugin доступ к сырому исходнику и точным span через
  `source_slice`, `source_location`, `capture_raw_block` и `capture_until`,
  чтобы embedded HLSL/GLSL/SQL не проходили через lexer Absolute.
- [x] Расширить parser cursor транзакциями `checkpoint`, `restore`, `commit`,
  атомарным rollback при отказе plugin rule и гарантией progress.
- [x] Открыть безопасную базовую грамматику host через `parse_expression`,
  `parse_statement`, `parse_type`, `parse_declaration`, `parse_block`,
  `parse_parameter_list` и `parse_function_body`.
- [x] Ввести структурированные plugin diagnostics: severity, code, primary и
  secondary spans, notes, fixits и mapping ошибок внешнего компилятора обратно
  в embedded-блок `.abs`.
- [x] Расширить opaque AST vtable операциями `clone`, `visit_children`,
  `serialize`, `deserialize`, `compute_hash`, `validate` и `lower`; сохранить
  прямой `emit` только как optional backend capability.
- [x] Добавить source mapping для expansion/foreign/generated code и IR:
  `map_generated_span`, `map_foreign_span`, `map_ir_instruction`.
- [x] Расширить manifest полями `optional_dependencies`, `conflicts`, `targets`,
  `permissions` и namespace для syntax rules; конфликты правил разрешать явно,
  а не порядком загрузки DLL/SO.

#### P1 — semantic, типы и ownership

- [x] Добавить semantic context: module, namespace, scope, current function,
  generic parameters, attributes, target platform и ownership context.
- [x] Открыть versioned semantic API: `resolve_symbol`, `resolve_type`,
  `declare_symbol`, `declare_type`, `declare_function`, `check_conversion`,
  `check_trait`, `infer_expression_type` и `report_diagnostic`.
- [x] Разрешить регистрацию opaque, primitive, generic, address-space,
  resource и compiler-known типов через descriptor с size/alignment,
  copy/move/destroy, validation и lowering hooks.
- [x] Добавить host-controlled регистрацию operators, conversions, literals,
  attributes и intrinsics с детерминированным разрешением приоритетов и
  конфликтов между плагинами.
- [x] Открыть ownership/lifetime-запросы `query_pointer_mode`, `require_raw`,
  `require_managed`, `transfer_ownership`, `register_resource`, `mark_escape`,
  чтобы `GpuTexture`, `NativeWindow`, `Socket` и другие resource-типы
  участвовали в анализе владения Absolute.
- [x] Добавить безопасную генерацию символов: `register_generated_function`,
  `register_generated_type`, `register_generated_constant` и
  `register_runtime_symbol`.
- [x] Заменить глобальную строку prelude на virtual/prelude modules и import
  resolver, чтобы плагины предоставляли `import Desktop`, `import Shader` и
  другие модули без загрязнения глобального namespace.
- [x] Ввести общий lowering API в Absolute HIR/MIR или host IR; плагин должен
  уметь понижать узел без доступа к внутренним C++ AST/Analyzer/LLVM-классам.

#### P2 — artifacts, backends, build graph и runtime

- [x] Отвязать codegen ABI от одного `emit_llvm`: добавить artifact kinds для
  LLVM IR/bitcode, object/static/shared library, SPIR-V, DXIL, PTX, CUBIN,
  AMDGPU code object, source text и custom binary.
- [x] Добавить `supports_target`, `emit_artifact` и `link_artifact`, а затем
  versioned backend registration: `register_target`, `query_target_features`,
  `compile_module`, `link_module`, `run_toolchain`.
- [x] Добавить build graph API для source/binary/tool/environment dependencies,
  generated files, include paths и link libraries.
- [x] Добавить plugin cache keys, artifact hashes и incremental state; opaque
  AST сериализовать с версией plugin/schema, не инвалидируя весь module cache.
- [x] Формализовать runtime-интеграцию: initialize/shutdown runtime,
  native libraries/functions, allocate/release resource и упаковку compiler,
  runtime, prelude, IDE и debugger частей в один plugin package.

#### P3 — IDE, debugger и надёжность

- [x] Добавить отдельный IDE API для completion, hover, definition, references,
  semantic tokens, formatting, code actions, rename, inlay hints, outline и
  folding ranges.
- [x] Добавить `embedded_language` и virtual-document/source-map contract для
  передачи HLSL/GLSL/SQL блоков специализированному language server.
- [x] Добавить debugger/profiler hooks: debug info, debug adapter, отображение
  runtime value, просмотр plugin-defined типов и profiler events.
- [x] Добавить unload/reload плагинов для IDE без перезапуска с проверкой живых
  AST/type/runtime handles перед выгрузкой.
- [x] Изолировать падение и исключение плагина от процесса компилятора; ввести
  режимы trusted in-process, isolated process, sandbox и WASM plugin.
- [x] Реализовать permission enforcement из manifest для filesystem, network,
  environment, toolchain и native-library доступа перед plugin marketplace.

### Стандартная библиотека

- [x] Базовый console I/O и форматирование.
- [x] Async task runtime.
- [x] Math-типы и функции как пример плагина.
- [x] Добавить `std.time` для Unix wall clock, монотонных измерений, sleep и
  benchmark. Единица выбирается через `Unit` (`Nanoseconds`, `Microseconds`,
  `Milliseconds`, `Seconds`); основной API — `now`, `mono`, `elapsed`, `sleep`,
  `measure`, `bench`, старый `Time.*` сохранён совместимыми обёртками.
- [x] Добавить базовые collections: `Vector`, `Map` и `Set`.
- [ ] **Следующее: определить стабильную структуру standard library и правила
  версионирования.**
- [x] Добавить интерполяцию строк `"${var}"`: каноничное обессахаривание (desugaring) строк с выражениями `${...}` на этапе парсера в вызовы встроенной функции `format(...)` с экранированием `\$`.
- [x] Добавить `std.string`: `StringBuilder` для эффективного накопления строк, UTF-8 и декодирование/кодирование Unicode кодовых точек, изменение регистра (ASCII + Unicode basic plane), `trim`/`trimStart`/`trimEnd`, `startsWith`/`endsWith`/`contains`, `indexOf`/`lastIndexOf`, `replace`, `substring` и `join`.
- [x] Добавить `std.env`: чтение, запись, удаление и проверка переменных окружения UTF-8 с диагностикой ошибок.
- [x] Добавить `std.process`: PID процесса, аргументы командной строки, путь к исполняемому файлу, имя хоста, `exit`/`abort`, смену рабочей директории и запуск команд (`run`, `runCapture`).
- [x] Добавить `std.fs`: UTF-8 paths, exists/type/size, create/remove/rename/copy,
  whole-file read/write/append и ресурсный streaming `File`; Win32/Linux runtime
  использует native Unicode paths и автоматически закрывает opaque handle.
- [x] Добавить календарные `Date`/`Time`/`DateTime`/`TimeZone`: высокосные года, дни в месяце, день недели, конвертации между временными зонами, Unix epoch timestamps, добавлении дней/секунд и форматирование/парсинг ISO-8601 (`std.datetime`).
- [x] Добавить `std.random`: `Rng` использует воспроизводимый xoshiro256** с
  явным seed и коротким API `u64`/`i32`/`range`/`real`/`boolean`; `entropy()` и
  `create()` отделяют системный источник seed от детерминированного PRNG.
- [x] Добавить JSON (`std.json`) и binary serialization (`std.binary`: `BinaryReader` / `BinaryWriter`).
- [x] Добавить `std.net` blocking TCP: connect/listen/accept, ephemeral/local port,
  send/receive, timeout, shutdown и автоматическое закрытие opaque socket handle;
  Windows использует Winsock 2, Linux — POSIX sockets.
- [x] Добавить UDP (`UdpSocket`), DNS API верхнего уровня (`std.net.resolve`), `std.uri` (`Uri` parser, `encode`, `decode`) и `std.http` (HTTP Client `get`/`post` и HTTP Server `HttpServer`) поверх транспортного слоя `std.net` с использованием операторов `defer`.
- [x] Довести стандартные algorithms: существующие sort/search/reverse дополнить
  transform, reduce и filter и подключить весь набор к CTest.
- [x] Добавить logging, assertions и test framework.

### Desktop, игры и графика

- [x] Native Win32/X11 окно, event loop и headless backend через desktop plugin.
- [x] Shader-блоки как opaque plugin syntax.
- [x] Векторные и матричные math-типы, projection и lookAt.
- [ ] Добавить keyboard, mouse, gamepad и text-input API.
- [ ] Добавить OpenGL/Vulkan/Direct3D backend либо общий RHI.
- [ ] Добавить GPU buffers, textures, samplers, pipelines и shader reflection.
- [ ] Добавить загрузку изображений, шрифтов, моделей и аудио.
- [ ] Добавить 2D renderer, sprites и batching.
- [ ] Добавить базовый UI toolkit для desktop-приложений.
- [ ] Добавить audio output/mixer.
- [ ] Добавить game loop, frame timing и fixed update.
- [ ] Добавить примеры: triangle, sprite scene, UI window и небольшая игра.

### Native interop и платформы

- [x] C ABI imports и native FFI.
- [x] Runtime-загрузка внешних `.dll`/`.so` через `bool load(string path)`:
  UTF-8 пути на Windows, `RTLD_NOW | RTLD_GLOBAL` на POSIX, идемпотентная загрузка
  и `false` вместо аварии для отсутствующей или несовместимой библиотеки;
  `isLoaded(path)` проверяет кэш, `loadError()` возвращает thread-local ошибку ОС.
- [x] C ABI exports через `export "C"` с неманглированным символом, Windows
  `dllexport`, проверкой ABI-safe сигнатур и запретом overload/generics/default
  parameters; PE export table и вызов из Absolute покрыты native/LLVM тестами.
- [x] Генерация LLVM IR и native object/executable.
- [ ] Добавить генератор Absolute declarations из C headers.
- [ ] Добавить безопасные wrappers для native handles и callbacks.
- [ ] Определить ограниченную поддержку C++ ABI либо официально оставить только C ABI.
- [ ] Проверить targets x64, ARM64, Windows, Linux и macOS в CI.
- [ ] Добавить WebAssembly target и browser runtime.
- [ ] Формализовать ABI массивов, strings, structs, interfaces и callbacks.

### IDE, debugger и developer tools

- [x] VS Code extension с project/plugin discovery, completion и hover.
- [x] Запуск проекта и подключение native debugger.
- [x] Перевести language intelligence в отдельный LSP server.
  Реализован zero-dep `absolute-extension/server/lsp-server.js` (stdio LSP) и
  `client/lsp-client.js`; extension v0.3.0 только build/debug + LSP client.
- [x] Добавить go-to-definition, references, rename и document symbols.
  Workspace/document symbols, definition, references и rename через LSP;
  индекс строится эвристическим разбором `.abs` + plugin editor metadata.
- [x] Добавить semantic highlighting и code actions.
  Semantic tokens (`keyword`/`type`/`function`/`namespace`) и code actions
  Format document / Refresh diagnostics.
- [x] Добавить formatter и конфигурируемый linter.
  Document formatting provider + `tools/absolute-dev.js fmt`; diagnostics
  linter через `absolutec` (path/args из `absolute.compilerPath` /
  `absolute.compilerArguments`).
- [x] Добавить debugger visualization для arrays, slices, tasks и managed pointers.
  `debugging/Absolute.natvis` (cppvsdbg), `debugging/absolute_gdb.py`,
  helper types `absolute_debug_types.h`; extension injects visualizerFile /
  GDB setupCommands. Layouts documented in `docs/debugging.md`.
- [x] Добавить breakpoints/source mapping для opaque plugin nodes.
  Virtual scheme `absolute-opaque:`, extract `shader` blocks, Open Opaque Block,
  DAP tracker maps breakpoints back to host `.abs` line; LSP
  `absolute/opaqueSourceMaps`.
- [x] Добавить REPL и expression evaluator.
  `tools/absolute-repl.js`, `absolute-dev eval|repl`, VS Code commands
  Evaluate Expression / Open REPL (compile-run via absolutec).
- [x] Добавить генератор документации из исходного кода.
  `tools/absolute-dev.js doc` пишет Markdown outline по symbols workspace.
- [x] Добавить `absolute test`, `absolute fmt`, `absolute doc` и `absolute package`.
  CLI: `tools/absolute-dev.bat` (`fmt`, `test`, `doc`, `package list|resolve`).

## Критерии готовности языка

- [ ] Для каждой заявленной возможности есть semantic, error, LLVM emit и runtime test.
- [ ] Документация не называет экспериментальную возможность стабильной.
- [ ] Примеры собираются на Windows и Linux одной командой.
- [ ] Публичные ABI и plugin ABI имеют версию и compatibility policy.
- [ ] Ошибки компилятора всегда содержат файл, строку, колонку и понятный diagnostic code.
- [ ] Есть минимум один реальный desktop/game example и одна reusable library на Absolute.
