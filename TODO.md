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
- [x] Добавить `absolutec --version`, чтобы установленный compiler можно было
  однозначно обнаружить и проверить из терминала и install-скриптов.
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
- [x] Структурные tuple-значения и безопасные variadic-параметры:
  `tuple<T...>`, литералы `(a, b, ...)`, `itemN`/`length`, передача и возврат
  через value ABI, а также финальный `params T[] args` со стековой упаковкой
  хвоста вызова и прямой передачей готового массива.
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
- [x] Перегрузка конструкторов class/struct: несколько ctor с разными
  списками параметров, разрешение как у методов, `base(...)` выбирает
  overload базы; LLVM `Type.__ctor` + mangling по `CallableKey`.
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
- [x] Добавить явный протокол глубокого копирования без C++ rule-of-five:
  `copy(value)` вызывает public zero-argument `clone() const`, struct возвращает
  тот же value-тип, class/interface — новый managed pointer того же статического
  типа; virtual clone использует обычный vtable dispatch, а исходное значение
  остаётся живым. Массивы и slices сохраняют отдельную семантику `copy(...)`.
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
- [x] Определить стабильную структуру standard library и правила
  версионирования: package `absolute.std` (`std/abspackage.json` 0.1.0),
  namespace↔file map, SemVer + tiers Stable/Experimental/Internal,
  pre-1.0 policy; normative doc `docs/standard-library.md`.
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

#### Roadmap std после 0.3

- [ ] P0 collections:
  - [x] `Deque<T>` на кольцевом буфере с O(1) вставкой и удалением с обоих концов.
  - [x] `Queue<T>` и `Stack<T>` как компактные типизированные фасады без отдельных storage engines.
  - [x] `PriorityQueue<T>` на стабильной бинарной куче с пользовательским comparator.
  - [x] Стандартный контракт hashing/equality и настоящие `HashMap<K, V>` / `HashSet<T>`;
    текущие `Map` / `Set` оставить совместимыми ordered-linear контейнерами.
    - [x] Закрыть CodeGen ABI для callback-полей в generic-классах с несколькими
      параметрами и virtual override value-type параметров в generic-наследниках;
      добавить отдельные semantic/native/WASM regression-тесты.
  - [x] Типизированный `Channel<T>` с явной моделью передачи ownership между tasks:
    codec-based value messages, checked close/receive semantics, `seal(move(owner))`
    and one-shot `std.concurrent.TransferChannel<T>` with native/WASM regressions.
- [ ] P0 data / I/O:
  - [ ] `std.bytes.ByteBuffer`: position/limit/capacity, slices и endian-aware primitives.
  - [ ] `std.io`: `Reader`, `Writer`, buffered streams и memory streams.
- [ ] P1 platform:
  - [ ] Расширить `std.fs`: directory listing/walk, metadata, temporary files и watcher.
  - [ ] Расширить `std.http`: headers collection, HTTPS/TLS, redirects, streaming,
    timeout/cancellation и multipart.
  - [ ] `Semaphore`, `RwLock`, `ConditionVariable` и `Once`.
  - [x] `std.mime` модуль для авто-определения MIME типов по расширению файла.
  - [x] `std.form` модуль для парсинга `application/x-www-form-urlencoded` и `multipart/form-data`.
  - [x] `std.task.TaskGroup` с consuming transfer дочерних `task<void>`,
    cooperative cancellation и автоматическим cancel/join на выходе из scope.
- [ ] P1 utilities:
  - [ ] `std.encoding` (Base64/hex/UTF-8 bytes), `std.uuid`, `std.regex`,
    `std.crypto` (hash/HMAC), `std.compress`, `std.cli`, CSV/TOML и SemVer.

### Desktop, игры и графика

- [x] Native Win32/X11 окно, event loop и headless backend через desktop plugin.
- [x] Shader-блоки как opaque plugin syntax.
- [x] Векторные и матричные math-типы, projection и lookAt.
- [x] Keyboard/mouse held + edge input (`keyDown`/`keyPressed`/`keyReleased`,
  mouse equivalents).
- [x] Text-input queue (`textPop` / `textCount`) + Windows XInput gamepad API
  (Linux stub); example `examples/desktop/input.abs`.
- [x] Software 2D: rect, line, circle, blit; `deltaTime` + sleep для game loop.
- [x] `Desktop.FixedStep` (fixed timestep accumulator + render alpha).
- [x] Soft `Desktop.Sprite` (offscreen buffer + drawSprite).
- [x] Примеры: `window.abs`, `pong.abs`, `sprites.abs` (fixed step + sprites).
- [x] Soft BMP load (`sprite.loadBmp`), `colorKey`, atlas `drawSpriteRect`;
  example `examples/desktop/image.abs` + `assets/*.bmp`.
- [x] Soft bitmap font (встроенный 8×8 ASCII): `window.drawText` /
  `sprite.drawText`, `Desktop.measureText` / `measureTextHeight`, glyph metrics;
  example `examples/desktop/text.abs`; scores in `pong.abs`.
- [x] Soft 2D sprite batch / atlas batch: `Desktop.SpriteBatch` (`begin` /
  `draw` / `drawRect` / `drawSprite` / `setAtlas` / `flush` / `end`),
  zero-copy `drawSpriteRect` via strided blit; example `examples/desktop/batch.abs`.
- [x] OpenGL RHI (`Desktop.Gpu`): Windows WGL + Linux GLX (when OpenGL found);
  frame model `beginFrame` / `clear` / `bind` / `draw` / `endFrame` / `present`;
  `backend()` → `opengl-wgl` / `opengl-glx`.
- [x] GPU resources: `GpuShader`, `GpuBuffer`, `GpuIndexBuffer`, `GpuSampler`,
  `GpuTexture`, `VertexLayout` + `createPipeline`.
- [x] GPU triangle example uses full pipeline path: `examples/desktop/triangle.abs`.
- [x] GPU sprite scene: textured indexed quads + sampler; example
  `examples/desktop/gpu-sprites.abs` (ship + atlas stars, WASD).
- [x] Index buffers (`createIndexBuffer` / `drawIndexed`) и sampler objects
  (`createSampler` / `bind(sampler)`).
- [x] PNG load for soft sprites: `sprite.loadPng` / `loadImage` (Windows WIC;
  portable zlib decoder for 8-bit RGB/RGBA elsewhere), assets `*.png`,
  example `image-png.abs`.
- [x] Soft TTF / system fonts: `Desktop.Font` (Windows GDI), `loadFile` for
  private `.ttf`/`.otf`, `drawFontText` / `measure` / `lineHeight`;
  example `examples/desktop/font.abs`.
- [x] Audio output/mixer: `Desktop.Audio` + `Sound` (WAV PCM load, waveOut
  software mixer, 32 voices, master volume, play/loop/stop);
  assets `beep.wav`/`blip.wav`/`thud.wav`, example `audio.abs`.
- [x] 3D mesh load: Wavefront OBJ → `Desktop.Mesh` (pos/normal/uv, indices),
  GPU upload helpers + `createLayoutPos3Normal3Uv2`; example `mesh.abs` + `cube.obj`.
- [x] Базовый soft UI toolkit: `Desktop.Ui` + `UiTheme` (immediate-mode
  panel/label/button/checkbox/slider/progress); example `examples/desktop/ui.abs`.
- [x] `absolute.shader` reflection + RHI bind (OpenGL): typed input/output/uniform,
  generated GLSL 330, optional `code { ... }` raw GLSL body, LLVM reflection +
  `absolute_shader_glsl_*` accessors, `Desktop.Gpu.createLayout3Attr`;
  examples `shader-rhi.abs`, `shader-code.abs`.
- [x] Dual OpenGL backends: WGL (Windows) + GLX (Linux/X11); `native_display` hook.
- [x] Multi-backend `Desktop.Gpu`: magic-header dispatch; `BackendAuto` /
  `BackendOpenGL` / `BackendD3D11`; D3D11 clear/present (Windows) + OpenGL
  full RHI; example `examples/desktop/d3d-clear.abs`.
- [x] D3D11 mesh RHI (Windows): HLSL `createShader`, VB/IB, pipeline + input
  layout (POSITION / TEXCOORDn), bind/draw/drawIndexed, uniforms via cbuffer b0
  + D3DReflect; examples `d3d-triangle.abs`, `d3d-triangle-smoke.abs`.
- [x] D3D11 textures/samplers: `createTextureFromSprite` (RGBA8 + color-key),
  `createSampler` (nearest/linear, clamp/repeat/mirror), bind t0/s0; HLSL
  `Texture2D` + `SamplerState`; examples `d3d-sprites.abs`, `d3d-sprites-smoke.abs`.
- [x] D3D12 multi-backend (Windows): `BackendD3D12` clear/present (command queue,
  FLIP_DISCARD swap chain, RTV heap, fence); examples `d3d12-clear.abs`,
  `d3d12-clear-smoke.abs`.
- [x] D3D12 mesh RHI: HLSL shaders, root CBV b0, upload-heap VB/IB, PSO + input
  layout (POSITION/TEXCOORDn), draw/drawIndexed, uniforms; textures still
  GL/D3D11; examples `d3d12-triangle.abs`, `d3d12-triangle-smoke.abs`.
- [x] D3D12 textures/samplers: root SRV t0 + Sampler s0, DEFAULT+upload
  `createTextureFromSprite` (RGBA8 color-key), `createSampler`
  (nearest/linear, clamp/repeat/mirror), bind on draw; examples
  `d3d12-sprites.abs`, `d3d12-sprites-smoke.abs`.
- [x] Vulkan backend (`BackendVulkan` = 4): Win32 surface + swapchain,
  dynamic `vulkan-1.dll`, HLSL→SPIR-V via portable DXC
  (`.absolute/toolchains/dxc-spirv` or `ABSOLUTE_DXC`), mesh RHI
  (VB/IB/pipeline/draw/drawIndexed), UBO b0 + sampled image t0 +
  sampler s0, textures from soft sprites; examples
  `vulkan-triangle.abs`, `vulkan-triangle-smoke.abs`,
  `vulkan-sprites.abs`, `vulkan-sprites-smoke.abs`.
- [x] SPIR-V·DXIL·Metal IR as first-class artifact kinds beyond host GPU paths:
  `ABSOLUTE_ARTIFACT_METAL_IR` + target triples in `plugin_api.h`;
  `absolute.shader` emits GLSL/HLSL/MSL source and optional SPIR-V/DXIL
  binaries (DXC) with module accessors; Metal AIR reserved (`has=0`);
  example `shader-multi-ir-smoke.abs`.

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
- [x] Формализовать ABI массивов, strings, structs, interfaces и callbacks:
  нормативный `docs/native-c-abi.md`; analyzer `ValidateCAbiType` отклоняет
  managed/`T[]`/func/task/by-value struct|class|interface; C ABI `bool` → `i8`
  (`_Bool`); callbacks как first-class C fnptr остаются следующим шагом.
- [x] Официально оставить только C ABI: `extern "C++"`/`export "C++"` отклоняются
  парсером; C++ библиотеки требуют thin `extern "C"` shim (см. native-c-abi.md).
- [x] Добавить безопасные wrappers для native handles и callbacks:
  `cfunc<Return, Params...>` — raw C function pointer (export/extern only,
  nullable, C CC call); handle-pattern: `struct` + `destroy()` + `move` with
  tests `cfunc-callbacks`, `native-handle-wrapper` (см. `docs/native-c-abi.md`).
- [x] Добавить генератор Absolute declarations из C headers:
  `tools/absolute-bindgen.js` / `absolute-dev bindgen` (clang AST JSON);
  maps scalars, `raw` pointers, `const char*`→`string`, typedef handles,
  `cfunc` for function pointers; skips variadic/unsupported with comments;
  fixtures in `tests/bindgen/`.
- [x] Проверить targets в CI (host-native matrix):
  Windows x64 (frontend + full LLVM), Linux x64 (full LLVM), macOS smoke
  (`macos-15`, arm64 host) — см. `docs/platforms.md` и
  `.github/workflows/ci.yml`. Windows ARM64 и Linux ARM64 runners отложены.
- [x] WebAssembly stack: `--target`, wasm-ld, runtime (heap/managed/errors/sync
  tasks/virtual FS/env/process), host imports (`absolute_log`, `absolute_http_get`,
  TCP connect/send/recv mocks), WASI console object, `absolute-wasm-run.js`,
  tests export/smoke/managed/task/fs/http/net, browser demo, Windows+Linux CI
  wasm jobs (wasmtime installed on Linux for WASI smoke when linked).
- [x] Real OS TCP for wasm under Node: worker_threads + SharedArrayBuffer/Atomics
  bridge (`tools/absolute-wasm-tcp-worker.js`), test `wasm-net-real` with local
  echo server in a dedicated worker (same-thread peers deadlock under
  `Atomics.wait`); mock TCP remains for deterministic unit tests.
- [x] wasm multi-thread task workers (Node): isolated-instance pool via
  `taskWorkers` + SAB job queue (`absolute-wasm-task-worker.js`),
  test `wasm-task-mt`; sync path remains default. Shared-memory pthread model
  and full wasi-sdk libc sysroot still open.
- [x] Richer WASI preview1 runtime (no full wasi-sdk yet): `fd_write`,
  `clock_time_get`, `random_get`, `args_*`, `environ_*`, `proc_exit`,
  `_initialize` for Node reactors; `tools/absolute-wasm-wasi-run.js`;
  tests `wasm-wasi-services` + smoke under Node WASI; absolutec can select
  WASI object via `*wasi*` target / `ABSOLUTE_WASM_RUNTIME=wasi` when built
  with `ABSOLUTE_WASM_WASI_OBJECT`.
- [x] Browser wasm host parity (mocks): `absolute-wasm-browser-host.js` / demo
  loader HTTP+TCP mocks + task stubs; test `run-wasm-browser-host`.
- [x] Optional wasi-sysroot bootstrap (`scripts/windows/bootstrap-wasi-sysroot.ps1`)
  + CMake `AbsoluteWasi.cmake` discovery (`WASI_SYSROOT` / `WASI_SDK_PATH`).
  Full wasi-libc link into Absolute modules still open (symbol clashes).
- [x] Worker-hosted browser session + WebSocket TCP bridge (COOP/COEP serve):
  `absolute-wasm-browser-session-{client,worker}.js`, `absolute-wasm-ws-tcp-worker.js`,
  `scripts/serve-wasm-demo.mjs`, demo mode switch, test `run-wasm-browser-session`.
- [x] Browser task worker pool in session Worker (`taskWorkers`,
  `absolute-wasm-browser-task-worker.js`); demo default N=2 under COOP/COEP;
  test `run-wasm-browser-task-pool`.
- [x] Shared-memory wasm foundation: `absolute_wasm_runtime_shared.o` (atomics heap
  lock), link `--shared-memory --import-memory`, host imports `env.memory` as
  SharedArrayBuffer, test `run-wasm-shared-memory`.
- [x] Shared-instance multi-thread tasks: heap ctrl in linear memory; Node
  `absolute-wasm-shared-task-worker.js` runs `entry(contextPtr)` in-place on the
  shared heap; `taskPoolMode: 'shared'`; test `run-wasm-shared-tasks`.
- [x] wasi-libc coexistence (selective kits): bootstrap sysroot + builtins,
  `AbsoluteWasiLibcExtras` STRTOL kit, probe `wasi_libc_strtol`, test
  `run-wasm-wasi-libc` (no full `-lc` — duplicate malloc/exit).
- [x] Browser shared-instance task pool: session worker detects shared
  `env.memory`, nested `absolute-wasm-browser-shared-task-worker.js`,
  `taskPoolMode: 'shared'|'isolated'`; wiring checked in
  `run-wasm-browser-task-pool`.
- [x] Complete wasm/std ABI parity: JSON, binary serialization, datetime,
  atomics/mutexes, cancellation and task delay implementations; Node/browser
  host clocks, entropy and `std.env` launch arguments; ABI audit reports zero
  missing `absolute_*` symbols; end-to-end `run-wasm-full-runtime` regression.
- [x] Remove the `std.time` / `std.datetime` import collision by renaming the
  legacy global compatibility namespace to `LegacyTime`.
- [x] Larger wasi-libc kits: `STRTOL` + `STRTOD` (default), dual probe, kit
  registry in `AbsoluteWasiLibcExtras.cmake`, `WASI_LIBC_KIT` switch.
- [ ] Guest-on-libc mode (drop Absolute malloc; size_t ABI); wasi-threads/TLS;
  more kits (qsort/locale) as needed.

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

## Следующий этап: стабильность, параллельность и production-ready toolchain

Этот этап идёт после первого hardening-слоя. Цель — не добавлять ещё десятки
синтаксических возможностей, а довести существующий язык, runtime и инструменты
до состояния, где ими можно пользоваться в больших проектах без надежды на удачу.

### Уже заложенная база

- [x] Добавить real-concurrency stress для `@task` / `@spawn`: mutex contention,
  atomic counter, bounded MPMC channel, cancellation races, priority и role metadata.
- [x] Добавить snapshot iterator stress для `Vector` / `Map` / `Set`: `clear`,
  alias mutation, многократные reallocations, вложенные iterators и replacement.
- [x] Добавить generated property-based тесты коллекций и deterministic
  grammar/mutation fuzzer с сохранением reproducer-ов.
- [x] Добавить ThreadSanitizer job для native runtime и scheduler harness.
- [x] Исправить найденную TSan-гонку между завершением task, `notify_all()` и
  уничтожением `Task` через `await`.
- [x] Устранить обычный bounded-channel deadlock на малых runner-ах минимальным
  запасом worker threads; окончательное решение остаётся частью Scheduler v2.

### P0 — полностью зелёная CI-матрица

- [ ] Добиться стабильного прохождения Windows Debug/Release, Windows LLVM,
  Linux Debug/Release, Linux WASM, macOS smoke, Termux smoke и hardening/TSan.
  - [x] Зафиксировать hosted runners: `ubuntu-24.04`, `windows-2022`, `macos-15`;
    Windows native Release локально проходит 410/410, Termux host contract —
    143/143, scheduler harness — 100/100 повторов. Ручной on-device прогон на
    ARM64 Android/Bionic с Termux Clang/LLVM 21.1.8 проходит 411/411 доступных
    тестов; усиленный Scheduler v2 с `epoll` дополнительно стабилен в 50/50
    повторах.
  - [x] Исправить ложное падение TSan cancellation race: writer запускается до
    readers, а общий release/acquire start gate исключает завершение readers до
    начала гонки.
  - [x] Ограничить дочерний процесс sanitizer negative-тестов: macOS timeout
    допустим только после появления ожидаемой ASan-сигнатуры, поэтому зависание
    runtime не маскирует отсутствие диагностики.
  - [ ] Подтвердить новый commit полным hosted run обоих required gates без rerun.
  - [ ] Подключить настоящий ARM64 Android/Termux self-hosted runner; Linux
    `ABSOLUTE_TERMUX=ON` contract не выдавать за Bionic/on-device выполнение.
- [x] Разделить platform-specific failures и реальные regression failures, чтобы
  нестабильная установка toolchain не маскировала поломку языка.
- [x] Зафиксировать поддерживаемые версии LLVM, Clang, MSVC, CMake, Ninja, Node,
  WASI SDK и Android/Termux toolchain в `docs/ci-policy.md`.
- [ ] Включить в ruleset ветки `master` обязательные уникальные checks
  `CI required gate` и `Hardening required gate`; сами aggregate jobs готовы,
  остаётся repository-admin настройка GitHub.
- [x] Сохранять логи, generated reproducers, sanitizer reports и failed binaries
  как CI artifacts для каждого падения.
- [x] Установить отдельные timeout и retry-policy для build, runtime, fuzz и
  platform bootstrap, не скрывая детерминированные падения повторным запуском.

### P1 — Scheduler v2 без блокировки worker threads

- [x] Ввести suspension/resume task при ожидании channel, timeout, I/O и sync
  primitive вместо блокировки настоящего OS thread.
  - [x] Реализовать stackful fiber backend: Win32 Fibers, POSIX `ucontext`,
    Termux `libucontext`; после первого запуска fiber закреплён за worker и не
    мигрирует вместе с TLS.
  - [x] Перевести `await`, bounded `Channel`, `TransferChannel`, runtime mutex и
    task delay на suspension с возвратом OS worker в scheduler.
  - [x] Подключить filesystem/network к portable blocking-offload executor:
    scheduler fiber приостанавливается, host-вызов выполняется в отдельном I/O
    pool, completion возвращает task через scheduler queue.
  - [ ] Добавить масштабируемые OS-native readiness/completion backend-ы:
    - [x] Linux/Termux `epoll`: one-shot reactor для TCP/UDP
      accept/send/receive, несколько read/write waiters на одном descriptor и
      возврат completion напрямую в scheduler queue.
    - [x] Windows IOCP: один process-wide completion port, overlapped
      `AcceptEx`/`WSASend`/`WSARecv`/`WSASendTo`/`WSARecvFrom`, независимые
      operation buffers и возврат completion напрямую в scheduler queue.
    - [ ] macOS `kqueue`:
      - [x] Реализовать отдельный one-shot reactor на
        `EVFILT_READ`/`EVFILT_WRITE`, wake-up через `EVFILT_USER`, несколько
        waiters на descriptor и общий TCP/UDP suspension path.
      - [ ] Подтвердить backend настоящим `macos-15` hosted run; локально
        выполнены platform-guard regression и syntax-check контракта `kevent`.
    - [x] Перевести nonblocking connect и timed socket waits в native reactors:
      Linux/Termux/macOS регистрируют deadline прямо в `epoll`/`kqueue`, Windows
      отменяет просроченный overlapped I/O через `CancelIoEx` и ждёт финальный
      IOCP completion packet перед resume; DNS остаётся на blocking-offload.
    - [x] Сохранить blocking-offload fallback для DNS и файловых операций без
      portable async API.
- [x] Сделать wake-up через scheduler queue: `send`/completion возвращает
  приостановленную task в runnable state без polling.
- [x] Добавить structured concurrency и `TaskGroup`: дочерние tasks принадлежат
  lexical scope и автоматически join/cancel при выходе.
- [x] Реализовать propagation cancellation и deadline через всю иерархию tasks:
  reference-counted parent control chain без циклов, наследуемый monotonic
  deadline для scheduler delay и native socket waits, cooperative
  `std.task.cancelled()` для всего поддерева.
- [x] Добавить work stealing между worker queues: внешние submissions
  распределяются round-robin, вложенный spawn попадает в локальную очередь,
  свободные workers обходят victim queues по вращающемуся cursor и крадут только
  ещё не запущенные fibers; возобновляемые fibers остаются pinned к владельцу.
  - [x] Ограничить OS worker pool диапазоном `1..32` и добавить
    `ABSOLUTE_SCHEDULER_WORKERS`/`std.task.workerCount()`.
- [x] Формализовать fairness между `role`, starvation protection и статистическое
  преимущество priority: smooth weighted scheduling с весами
  `1/2/3/4/6/8/12` для priority `-3..3`, round-robin между активными role lanes
  и чередование pinned/новых задач одного уровня без требования хрупкого
  точного порядка исполнения.
- [x] Проверять CPU affinity как best-effort capability с понятным fallback:
  `std.task.affinitySupported()`, `coreAvailable(core)` и per-task
  `affinityApplied()`; Linux/Termux учитывают исходную allowed CPU mask,
  Windows поддерживает logical cores через processor groups, macOS/WASM явно
  сообщают unsupported, а отказ ОС не мешает выполнению task.
- [x] Добавить scheduler metrics: `std.task.metrics()` возвращает gauges
  runnable/suspended и cumulative completed, queue samples/total/max latency,
  worker busy time/utilization, steals, wake-ups, blocked time и starvation
  events для queue episodes от 100 ms.
- [x] Добавить отдельный native/TSan stress harness на spawn/await storm,
  cancel-vs-complete, destroy-vs-wait, channel close races, timeout races и
  nested task groups. Один task handle по-прежнему имеет ровно одного
  consuming-владельца (`await`, `destroy` или `TaskGroup`).
- [x] Удалить временную зависимость корректности bounded channels от минимального
  количества worker threads после внедрения suspension.

### P2 — differential testing компилятора и backend-ов

- [x] Генерировать одинаковые программы и сравнивать checksum/вывод для `-O0`,
  `-O1`, `-O2`, `-O3`, Debug и Release: `absolutec` принимает явный optimization
  level, deterministic runner проверяет oracle и полный stdout, а один CTest
  запускается обеими конфигурациями compiler-а в Linux CI.
- [x] Сравнивать native и WASM execution для одинакового deterministic corpus:
  фиксированный seed и checksum-oracle общие с optimization differential,
  полный stdout/exit code native executable сравнивается с
  `wasm32-unknown-unknown` через официальный Absolute WASM host.
- [x] Добавить матрицу LLVM 18/19/20/21 там, где host toolchain доступен:
  отдельный обязательный Release-контракт собирает `absolutec`, сверяет точную
  major-версию SDK, прогоняет differential O0–O3 и валидирует representative
  LLVM IR через соответствующий `llvm-as`.
- [x] Сравнивать Windows MSVC linker, Linux ELF linker и WASM linker на одном
  ABI corpus: общий versioned manifest проверяет arrays, value/reference ABI,
  structs, interfaces, generic callbacks, managed/raw pointers и C exports;
  каждый runner сверяет формат артефакта, exit code и полный stdout с одним
  oracle.
- [x] Включить в corpus arithmetic overflow, signed/unsigned conversions, floats,
  constant folding, generics, lambdas, interfaces, exceptions, `defer`, async,
  managed/weak/raw pointers, arrays, slices и snapshot collections: versioned
  linker manifest содержит 17 фиксированных cases с общим stdout oracle для
  PE/ELF/WebAssembly.
- [ ] Сохранять минимальный source и все расходящиеся outputs при mismatch.
- [ ] Добавить metamorphic properties: dead-code insertion, alpha-renaming,
  эквивалентная перестановка независимых declarations и constant/runtime variants
  не должны менять наблюдаемый результат.

### P3 — coverage-guided fuzzing

- [ ] Добавить in-process libFuzzer harness для lexer.
- [ ] Добавить in-process libFuzzer harness для parser с лимитами глубины, памяти
  и времени, чтобы malformed input не создавал stack overflow или hang.
- [ ] Добавить analyzer fuzzer для синтаксически корректного generated AST/source.
- [ ] Добавить codegen fuzzer для валидных typed programs с запуском под ASan/UBSan.
- [ ] Добавить fuzzing package manifest, lock-file, plugin manifest и project parser.
- [ ] Вести persistent seed corpus и regression corpus в репозитории.
- [ ] Автоматически минимизировать crashing/hanging inputs и печатать точную команду
  воспроизведения.
- [ ] Запускать короткий fuzz budget в PR, расширенный nightly и длительный weekly.
- [ ] Добавить coverage report по lexer/parser/analyzer/codegen, чтобы fuzzer не
  праздновал тысячи вариантов одной и той же закрывающей скобки.

### P4 — ownership torture suite

- [ ] Покрыть уничтожение `managed(owner, sub)` при живых subscriber и `weak` aliases.
- [ ] Проверить глубокие цепочки owner → sub → sub и cleanup в обратном порядке.
- [ ] Проверить weak после destroy, generation reuse и большое количество reuse cycles.
- [ ] Проверить compile-time запрет strong cycles и корректность графов с weak back-edge.
- [ ] Проверить `move(owner)` через return, parameters, fields, generic wrappers,
  interface dispatch и исключения между move и завершением операции.
- [ ] Проверить частично построенные объекты и исключения в constructor/base constructor.
- [ ] Проверить closure capture, escaping lambda, async boundary и task result ownership.
- [ ] Проверить transfer capsule: seal/send/rehome/destroy, double-send, use-after-send,
  receiver cancellation и закрытие channel с непринятыми capsules.
- [ ] Проверить ownership plugin resources и native handles при unload/reload plugin.
- [ ] Запускать ownership corpus под ASan, UBSan, LSan и TSan, где применимо.
- [ ] Добавить model-based generator операций `new`/alias/weak/move/delete/throw/return
  с эталонной моделью состояния lifetime.

### P5 — freeze Plugin ABI 1.0 и production validation

- [ ] Зафиксировать набор `AbsoluteCompilerPluginV1`, `AbsoluteLanguagePluginV1`,
  `AbsoluteEditorPluginV1` и `AbsoluteRuntimePluginV1` как первый публичный ABI tier.
- [ ] Выпустить отдельный plugin SDK с headers, schema, examples и compatibility rules.
- [ ] Проверить plugin, собранный старым SDK, на новом host без пересборки.
- [ ] Добавить allocator-boundary tests: память освобождается той стороной, которая
  её выделила, либо только через явно переданный allocator API.
- [ ] Проверить concurrent callbacks и thread-safety contract всех capability tables.
- [ ] Добавить crash/timeout containment для isolated plugin process и WASM plugin.
- [ ] Проверить serialization/version migration opaque AST и incremental cache между
  несколькими версиями plugin schema.
- [ ] Добавить ABI dump и автоматическое сравнение layout/exported symbols в CI.
- [ ] Подготовить deprecation policy до ABI v2 без молчаливого изменения V1 structs.

### P6 — реальные программы как обязательные integration tests

- [ ] Добавить CLI file indexer с `std.fs`, collections, JSON и parallel workers.
- [ ] Добавить HTTP server с routing, JSON, cancellation, timeout и graceful shutdown.
- [ ] Добавить multithreaded crawler с DNS/HTTP, bounded concurrency и deduplication.
- [ ] Добавить reusable parsing/serialization library как отдельный package target.
- [ ] Добавить desktop calculator/editor с persistent settings и file dialogs.
- [ ] Добавить полноценную OpenGL/Vulkan/D3D scene вместо одного triangle smoke.
- [ ] Добавить WASM web app с browser host, async task pool и persistent state.
- [ ] Добавить end-to-end shader plugin demo с embedded source, diagnostics, reflection
  и несколькими target artifacts.
- [ ] Добавить workspace с несколькими packages, diamond dependencies и lock-file.
- [ ] Держать каждый integration project примерно в диапазоне 500–3000 строк,
  собирать и запускать его в CI, а не хранить как мёртвый showcase.
- [ ] Ввести performance budgets для compile time, peak memory, binary size и runtime.

### P7 — incremental compiler и LSP следующего уровня

- [ ] Перейти от module-level cache к dependency graph на уровне declarations/symbols.
- [ ] Кешировать parsed AST, semantic model, instantiated generics и backend artifacts
  с точными invalidation keys.
- [ ] Делать selective re-analysis и selective LLVM regeneration только затронутых
  declarations и их dependents.
- [ ] Добавить correctness tests invalidation: изменение overload, generic constraint,
  interface method, public ABI и private implementation.
- [ ] Добавить background diagnostics с cancellation устаревшего анализа.
- [ ] Перевести definition/references/rename с эвристического индекса на compiler semantic model.
- [ ] Добавить semantic completion, signature help, inlay hints и безопасные code actions.
- [ ] Добавить incremental workspace benchmark на 10k/50k/100k строк: cold load,
  single-file edit, public API edit, rename и full rebuild.
- [ ] Добавить memory budget и eviction policy для daemon/LSP cache.
- [ ] Проверить несколько одновременных editor clients и корректный shutdown/restart.

### Критерии завершения этапа

- [ ] Вся поддерживаемая CI-матрица зелёная без manual rerun.
- [x] Scheduler не блокирует worker thread на channel/I/O/task wait.
- [ ] Differential corpus не расходится между native/WASM и optimization levels.
- [ ] Coverage-guided fuzzing работает в PR/nightly/weekly режимах и сохраняет corpus.
- [ ] Ownership torture suite проходит под sanitizers.
- [ ] Plugin ABI V1 проверяется старым SDK против нового host.
- [ ] Несколько реальных integration projects собираются и выполняются в CI.
- [ ] Incremental edit и LSP используют compiler semantic cache, а не полный re-run.

## Критерии готовности языка

- [ ] Для каждой заявленной возможности есть semantic, error, LLVM emit и runtime test.
- [ ] Документация не называет экспериментальную возможность стабильной.
- [ ] Примеры собираются на Windows и Linux одной командой.
- [x] Публичные ABI и plugin ABI имеют версию и compatibility policy
  (plugins: `.absplugin` + SemVer; std: `absolute.std` + `docs/standard-library.md`).
- [ ] Ошибки компилятора всегда содержат файл, строку, колонку и понятный diagnostic code.
- [ ] Есть минимум один реальный desktop/game example и одна reusable library на Absolute.
