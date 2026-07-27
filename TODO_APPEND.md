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
- [ ] Разделить platform-specific failures и реальные regression failures, чтобы
  нестабильная установка toolchain не маскировала поломку языка.
- [ ] Зафиксировать поддерживаемые версии LLVM, Clang, MSVC, CMake, Ninja, Node,
  WASI SDK и Android/Termux toolchain.
- [ ] Добавить обязательный status gate для merge в основную рабочую ветку.
- [ ] Сохранять логи, generated reproducers, sanitizer reports и failed binaries
  как CI artifacts для каждого падения.
- [ ] Установить отдельные timeout и retry-policy для build, runtime, fuzz и
  platform bootstrap, не скрывая детерминированные падения повторным запуском.

### P1 — Scheduler v2 без блокировки worker threads

- [ ] Ввести suspension/resume task при ожидании channel, timeout, I/O и sync
  primitive вместо блокировки настоящего OS thread.
- [ ] Сделать wake-up через scheduler queue: `send`/completion возвращает
  приостановленную task в runnable state без polling.
- [ ] Добавить structured concurrency и `TaskGroup`: дочерние tasks принадлежат
  lexical scope и автоматически join/cancel при выходе.
- [ ] Реализовать propagation cancellation и deadline через всю иерархию tasks.
- [ ] Добавить work stealing между worker queues и ограничение количества OS threads.
- [ ] Формализовать fairness между `role`, starvation protection и статистическое
  преимущество priority без требования хрупкого точного порядка исполнения.
- [ ] Проверять CPU affinity как best-effort capability с понятным fallback.
- [ ] Добавить scheduler metrics: runnable/suspended/completed tasks, queue latency,
  worker utilization, steals, wake-ups, blocked time и starvation counters.
- [ ] Добавить stress на spawn/await storm, cancel-vs-complete, destroy-vs-wait,
  channel close races, timeout races и nested task groups.
- [ ] Удалить временную зависимость корректности bounded channels от минимального
  количества worker threads после внедрения suspension.

### P2 — differential testing компилятора и backend-ов

- [ ] Генерировать одинаковые программы и сравнивать checksum/вывод для `-O0`,
  `-O1`, `-O2`, `-O3`, Debug и Release.
- [ ] Сравнивать native и WASM execution для одинакового deterministic corpus.
- [ ] Добавить матрицу LLVM 18/19/20/21 там, где host toolchain доступен.
- [ ] Сравнивать Windows MSVC linker, Linux ELF linker и WASM linker на одном ABI corpus.
- [ ] Включить в corpus arithmetic overflow, signed/unsigned conversions, floats,
  constant folding, generics, lambdas, interfaces, exceptions, `defer`, async,
  managed/weak/raw pointers, arrays, slices и snapshot collections.
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
- [ ] Scheduler не блокирует worker thread на channel/I/O/task wait.
- [ ] Differential corpus не расходится между native/WASM и optimization levels.
- [ ] Coverage-guided fuzzing работает в PR/nightly/weekly режимах и сохраняет corpus.
- [ ] Ownership torture suite проходит под sanitizers.
- [ ] Plugin ABI V1 проверяется старым SDK против нового host.
- [ ] Несколько реальных integration projects собираются и выполняются в CI.
- [ ] Incremental edit и LSP используют compiler semantic cache, а не полный re-run.
