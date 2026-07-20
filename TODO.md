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
- [x] Пространства имён, файловые и namespace-импорты.
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
  структур и классов; constraints/traits остаются следующим слоем.
- [ ] Добавить type aliases в ядро, не зависящие от plugin prelude.
- [ ] Определить правила `const`/immutability для переменных, параметров и методов.

### Типы и ООП

- [x] Value-типы `struct`, поля, конструкторы и методы.
- [x] Классы, наследование, virtual dispatch и деструкторы.
- [x] Raw и managed pointers на структуры и классы.
- [x] Интерфейсы, наследование интерфейсов и реализация несколькими интерфейсами.
- [x] Проверка перегрузок и возвращаемых типов при реализации интерфейса.
- [ ] Реализовать static-поля и static-методы классов в CodeGen.
- [ ] Добавить автоматическую цепочку base-конструкторов и явный вызов `base(...)`.
- [ ] Добавить default-реализации методов интерфейсов.
- [ ] Добавить properties в классы, структуры и интерфейсы.
- [ ] Добавить indexers для пользовательских контейнеров.
- [ ] Добавить static-члены интерфейсов.
- [ ] Завершить и протестировать правила доступа `public`/`protected`/`private`.
- [ ] Добавить безопасные downcast/type-test операции (`is`, `as`).
- [ ] Определить ABI и правила копирования/перемещения больших value-типов.

### Память и указатели

- [x] Managed `T*` с generation-check и автоматическим освобождением владельца.
- [x] `raw T*`, адресная арифметика, сравнение и явный `delete`.
- [x] Flow-анализ и диагностика uninitialized, expired и invalid pointers.
- [x] Передача и возврат raw/managed pointers из функций.
- [ ] Добавить автоматическое освобождение heap-копий возвращаемых массивов.
- [ ] Формализовать ownership для полей классов, структур, массивов и плагиновых типов.
- [ ] Добавить borrow/lifetime-проверки для slices и временных значений.
- [ ] Определить weak/non-owning managed references.
- [ ] Проверить циклические графы объектов и выбрать стратегию их очистки.
- [ ] Добавить sanitizer-набор тестов на use-after-free, double-free и утечки.
- [ ] Оптимизировать managed dereference и доказать корректность удаления bounds/lifetime checks в Release.

### Массивы, slices и коллекции

- [x] Одномерные и многомерные прямоугольные массивы.
- [x] Локальные и глобальные массивы, литералы и runtime-размеры.
- [x] Параметры и возврат массивов.
- [x] Одномерные slices без копирования.
- [x] `foreach` по массивам и slices.
- [x] Проверка выхода за границы.
- [ ] Добавить многомерные slices.
- [ ] Добавить безопасные iterators и пользовательский протокол iteration.
- [ ] Добавить динамические коллекции: `Vector`, `Map`, `Set` и queue/deque.
- [ ] Добавить стандартные алгоритмы: sort, search, transform, reduce и filter.
- [ ] Реализовать Release-elimination доказуемо лишних bounds checks.
- [ ] Добавить SIMD/vectorization-тесты для числовых массивов.

### Async и параллельность

- [x] `async`-функции, `spawn`, `await` и runtime worker pool.
- [x] Проверка незавершённых локальных tasks анализатором.
- [ ] Добавить async-методы классов и структур.
- [ ] Добавить cancellation tokens и timeout.
- [ ] Добавить channels и безопасные concurrent queues.
- [ ] Добавить async file/network I/O.
- [ ] Добавить `select`/`whenAny` для ожидания нескольких tasks.
- [ ] Добавить mutex, semaphore и atomic API в стандартную библиотеку.
- [ ] Формализовать thread-safety managed pointers и объектов.

### Модули, проекты и пакеты

- [x] `.absproj`, несколько source-файлов и рекурсивные импорты.
- [x] Namespace imports.
- [x] Подключение native libraries через проект.
- [ ] Добавить package manifest и lock-файл зависимостей.
- [ ] Добавить локальный/удалённый package registry.
- [ ] Добавить версионирование модулей и проверку конфликтов зависимостей.
- [ ] Добавить отдельные library/application targets в одном workspace.
- [ ] Добавить incremental module cache вместо повторного анализа всех файлов.

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

- [ ] Разделить capability-интерфейсы на `AbsoluteCompilerPluginV1`,
  `AbsoluteLanguagePluginV1`, `AbsoluteEditorPluginV1` и
  `AbsoluteRuntimePluginV1`; один package может предоставлять несколько
  независимых интерфейсов.
- [ ] Добавить `struct_size`, capability bitset/table, reserved fields и
  `required_host_version` во все расширяемые C ABI descriptors; использовать
  только ABI-safe типы, opaque handles и пары `pointer + length`.
- [ ] Реализовать capability/version negotiation между host и plugin с
  диагностикой недостающих `parser.*`, `semantic.*`, `ir.*`, `backend.*`,
  `ide.*` возможностей до вызова plugin-кода.
- [ ] Зафиксировать lifecycle `load -> initialize -> begin_compilation ->
  begin_module -> end_module -> end_compilation -> shutdown -> unload`,
  правила thread safety, параллельных вызовов, владения памятью и времени жизни
  строк/AST handles.
- [ ] Добавить ABI compatibility matrix и тесты старого/нового host и plugin на
  Windows и Linux, включая неизвестные поля, урезанный `struct_size` и
  отсутствующие optional capabilities.
- [ ] Дать parser plugin доступ к сырому исходнику и точным span через
  `source_slice`, `source_location`, `capture_raw_block` и `capture_until`,
  чтобы embedded HLSL/GLSL/SQL не проходили через lexer Absolute.
- [ ] Расширить parser cursor транзакциями `checkpoint`, `restore`, `commit`,
  атомарным rollback при отказе plugin rule и гарантией progress.
- [ ] Открыть безопасную базовую грамматику host через `parse_expression`,
  `parse_statement`, `parse_type`, `parse_declaration`, `parse_block`,
  `parse_parameter_list` и `parse_function_body`.
- [ ] Ввести структурированные plugin diagnostics: severity, code, primary и
  secondary spans, notes, fixits и mapping ошибок внешнего компилятора обратно
  в embedded-блок `.abs`.
- [ ] Расширить opaque AST vtable операциями `clone`, `visit_children`,
  `serialize`, `deserialize`, `compute_hash`, `validate` и `lower`; сохранить
  прямой `emit` только как optional backend capability.
- [ ] Добавить source mapping для expansion/foreign/generated code и IR:
  `map_generated_span`, `map_foreign_span`, `map_ir_instruction`.
- [ ] Расширить manifest полями `optional_dependencies`, `conflicts`, `targets`,
  `permissions` и namespace для syntax rules; конфликты правил разрешать явно,
  а не порядком загрузки DLL/SO.

#### P1 — semantic, типы и ownership

- [ ] Добавить semantic context: module, namespace, scope, current function,
  generic parameters, attributes, target platform и ownership context.
- [ ] Открыть versioned semantic API: `resolve_symbol`, `resolve_type`,
  `declare_symbol`, `declare_type`, `declare_function`, `check_conversion`,
  `check_trait`, `infer_expression_type` и `report_diagnostic`.
- [ ] Разрешить регистрацию opaque, primitive, generic, address-space,
  resource и compiler-known типов через descriptor с size/alignment,
  copy/move/destroy, validation и lowering hooks.
- [ ] Добавить host-controlled регистрацию operators, conversions, literals,
  attributes и intrinsics с детерминированным разрешением приоритетов и
  конфликтов между плагинами.
- [ ] Открыть ownership/lifetime-запросы `query_pointer_mode`, `require_raw`,
  `require_managed`, `transfer_ownership`, `register_resource`, `mark_escape`,
  чтобы `GpuTexture`, `NativeWindow`, `Socket` и другие resource-типы
  участвовали в анализе владения Absolute.
- [ ] Добавить безопасную генерацию символов: `register_generated_function`,
  `register_generated_type`, `register_generated_constant` и
  `register_runtime_symbol`.
- [ ] Заменить глобальную строку prelude на virtual/prelude modules и import
  resolver, чтобы плагины предоставляли `import Desktop`, `import Shader` и
  другие модули без загрязнения глобального namespace.
- [ ] Ввести общий lowering API в Absolute HIR/MIR или host IR; плагин должен
  уметь понижать узел без доступа к внутренним C++ AST/Analyzer/LLVM-классам.

#### P2 — artifacts, backends, build graph и runtime

- [ ] Отвязать codegen ABI от одного `emit_llvm`: добавить artifact kinds для
  LLVM IR/bitcode, object/static/shared library, SPIR-V, DXIL, PTX, CUBIN,
  AMDGPU code object, source text и custom binary.
- [ ] Добавить `supports_target`, `emit_artifact` и `link_artifact`, а затем
  versioned backend registration: `register_target`, `query_target_features`,
  `compile_module`, `link_module`, `run_toolchain`.
- [ ] Добавить build graph API для source/binary/tool/environment dependencies,
  generated files, include paths и link libraries.
- [ ] Добавить plugin cache keys, artifact hashes и incremental state; opaque
  AST сериализовать с версией plugin/schema, не инвалидируя весь module cache.
- [ ] Формализовать runtime-интеграцию: initialize/shutdown runtime,
  native libraries/functions, allocate/release resource и упаковку compiler,
  runtime, prelude, IDE и debugger частей в один plugin package.

#### P3 — IDE, debugger и надёжность

- [ ] Добавить отдельный IDE API для completion, hover, definition, references,
  semantic tokens, formatting, code actions, rename, inlay hints, outline и
  folding ranges.
- [ ] Добавить `embedded_language` и virtual-document/source-map contract для
  передачи HLSL/GLSL/SQL блоков специализированному language server.
- [ ] Добавить debugger/profiler hooks: debug info, debug adapter, отображение
  runtime value, просмотр plugin-defined типов и profiler events.
- [ ] Добавить unload/reload плагинов для IDE без перезапуска с проверкой живых
  AST/type/runtime handles перед выгрузкой.
- [ ] Изолировать падение и исключение плагина от процесса компилятора; ввести
  режимы trusted in-process, isolated process, sandbox и WASM plugin.
- [ ] Реализовать permission enforcement из manifest для filesystem, network,
  environment, toolchain и native-library доступа перед plugin marketplace.

### Стандартная библиотека

- [x] Базовый console I/O и форматирование.
- [x] Async task runtime.
- [x] Math-типы и функции как пример плагина.
- [ ] Определить стабильную структуру standard library и правила версионирования.
- [ ] Добавить полноценные String/StringBuilder и Unicode API.
- [ ] Добавить filesystem, paths и streams.
- [ ] Добавить date/time, timers и random.
- [ ] Добавить JSON и binary serialization.
- [ ] Добавить networking: sockets, HTTP client/server и URI.
- [ ] Добавить collections и algorithms.
- [ ] Добавить logging, assertions и test framework.

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
- [ ] Перевести language intelligence в отдельный LSP server.
- [ ] Добавить go-to-definition, references, rename и document symbols.
- [ ] Добавить semantic highlighting и code actions.
- [ ] Добавить formatter и конфигурируемый linter.
- [ ] Добавить debugger visualization для arrays, slices, tasks и managed pointers.
- [ ] Добавить breakpoints/source mapping для opaque plugin nodes.
- [ ] Добавить REPL и expression evaluator.
- [ ] Добавить генератор документации из исходного кода.
- [ ] Добавить `absolute test`, `absolute fmt`, `absolute doc` и `absolute package`.

## Критерии готовности языка

- [ ] Для каждой заявленной возможности есть semantic, error, LLVM emit и runtime test.
- [ ] Документация не называет экспериментальную возможность стабильной.
- [ ] Примеры собираются на Windows и Linux одной командой.
- [ ] Публичные ABI и plugin ABI имеют версию и compatibility policy.
- [ ] Ошибки компилятора всегда содержат файл, строку, колонку и понятный diagnostic code.
- [ ] Есть минимум один реальный desktop/game example и одна reusable library на Absolute.
