# B1: Checker prior art for chibi-v2

Researched 2026-11-12 from primary sources only: project source at pinned commits or tags,
release notes, design docs, pull requests and posts by the authors. Anything not confirmed in
a primary source is marked UNVERIFIED. Quotes are kept under 15 words. Line numbers refer to
the pinned revision named in each section.

Already known (not repeated): Carbon's one-pass postorder check, 16-byte SemIR instructions,
CanonicalValueStore and PR #4408 (985k lines/s); Zig Sema single-threaded at 0.15.2 and the
InternPool's per-thread Locals and sharded maps; Odin body checking on workers; Go types2
serial per package.

## Pinned sources

| Project | Revision | Date |
|---|---|---|
| Carbon | carbon-lang `ca1898506be64134c3a25a5381622667a25cf67f` | 2026-11-06 |
| Zig | codeberg.org/ziglang/zig tags `0.16.0` (`24fdd5b7a4c1`), `0.17.0` (`7647adab80dd`), `0.17.1` (`be248cc1fdc3`) | 2026-04-13, 2026-10-01, 2026-11-02 |
| Odin | odin-lang/Odin `85afcb386c389aafcec4f2e33d27ff8b42579822` | 2026-11-09 |
| Go | golang/go `go1.27.4` (`7b2881024c95`) | 2026-11-10 |
| TypeScript 7 | microsoft/typescript-go `89d5d5b2849a0db0957065889ca58536fa6d2e4a` (last commit; repo closed, work moved to microsoft/TypeScript) | 2026-08-20 |
| C3 | c3lang/c3c `ff06cbadd05aaebe4729c5ca3ec7450c0bf02338` | 2026-11-12 |
| rustc | rust-lang/rust tag `1.99.0` (source references only) | n/a |

Zig line numbers are for 0.17.1 unless noted. Scratch copies of these sources are in
`/tmp/b1-src/` and downloaded pages in `/tmp/b1-web/`.

---

## 1. Carbon

### 1a. Context-dependent expressions: literals converted at the point of use

Carbon types every expression bottom-up and converts it where it is used. Context never flows
down into a literal.

- **Struct literals.** `handle_struct.cpp`: `StructLiteralStart` (L16) pushes a solo node and
  opens a field array. Each `StructLiteralField` (L51) appends `{name_id, type of the value}`.
  `StructLiteral` (L80-105) builds a `StructLiteral` instruction whose type is the canonical
  *structural* struct type of those fields. A field `.x = 1` therefore starts with type
  `{.x: IntLiteral}`, and no target type is consulted.
- **Expression category `Mixed`.** Struct and tuple literals have category `Mixed`.
  Conversion "recurses into the elements" before converting
  (`toolchain/docs/check/README.md`, "ExprCategory::Mixed", L626-633).
- **Convert.** `convert.h` defines `ConversionTarget{kind, type_id, storage_id,
  storage_access_block, diagnose}`. Its kinds are `NoOp, Value, ValueOrRef, DurableRef,
  RefParam, UnmarkedRefParam, CppThunkRef, ExplicitAs, ExplicitUnsafeAs, Discarded,
  Initializing, InPlaceInitializing`. The single entry point is
  `Convert(context, loc, expr, target)`.
- **Struct literal to class or struct.** `ConvertStructToStructOrClass` (`convert.cpp` L797)
  uses the literal's element instructions directly when the source is a literal (comment at
  L816) and materializes a temporary otherwise. It matches fields by `NameId`, using a
  `Map<NameId,int>` only when the field lists differ. It then converts each element with an
  element-level target kind, writing into `ClassElementAccess`/`StructAccess` of the storage.
  The literal is therefore built into its destination without a copy.
- **Builtin versus library conversions.** `PerformBuiltinConversion` (L1399-1431) handles
  struct and tuple literals as builtins because they "cannot be implemented by invoking
  `ImplicitAs`". The same comment notes the performance gain from skipping an impl lookup for
  common conversions.
- **Integer literals.** These have type `Core.IntLiteral` (`literal.h`, `MakeIntLiteral`).
  They convert through prelude impls:
  `impl forall [To: IntLiteral] IntLiteral as ImplicitAs(Int(To))` calls the builtin
  `"int.convert_checked"` (`core/prelude/types/int.carbon` L31-32). Integer-literal conversion
  therefore goes through generic impl lookup, not a special case.
- **Initializing expressions.** The README's "ExprCategory::Initializing" section (L601-624)
  separates in-place from by-copy initializing representations. An initializer must be
  consumed by an instruction that finishes the initialization.
  - `Initialize(storage, pending_block, value)` (`convert.h`) can overwrite a placeholder
    storage instruction.
  - `InitializeExisting(storage, value, for_return)` needs storage that dominates the value.
- **Return slots.** `return.cpp` L131 converts a `return expr` with
  `InitializeExisting(context, loc, out_param_id, expr, /*for_return=*/true)`, where the out
  parameter is the return slot. When the type's init representation cannot be in place, the
  slot is used only for its type (L133-136).
- **Deduced return types.** These are directly relevant to chibi-v2's omitted private return
  types.
  - Each `return expr` in a function with a deduced return type is recorded as a
    `PendingReturn{expr_id, splice_id, return_inst_id}`, with a placeholder `SpliceBlock`
    (`scope_stack.h` L226-237).
  - At the end of the body, `DeduceReturnTypeAtEndOfBody` (`return.cpp` L422-475) takes the
    type of the first non-error return and requires every other return to match ("TODO: Use
    the common type"). It then sets the type and splices in the conversions.
  - A call made before deduction is an error: "We can't call a function before its return
    type is known." (`call.cpp` L252). The diagnostic is `CallBeforeReturnTypeDeduced`.
- **Rewrites.** The README "Rewrites" section (L528-539) describes the general tool for
  "information arrives later": a placeholder later replaced by a `splice_block` pointing at new
  IR, or instructions held in a separate `PendingBlock` (`pending_block.h`) and merged once
  decided.
- **Why this works single-pass.** Carbon's information accumulation principle: "information
  is accumulated incrementally within each source file"
  (`docs/project/principles/information_accumulation.md`). Names must be declared before use,
  and forward declarations break cycles. A lookup that misses poisons the name so that a later
  declaration is rejected (`NameScope::LookupOrPoison`, `sem_ir/name_scope.h` L199-203). This
  is what lets Carbon skip a declaration-collection pass. chibi-v2's any-order package
  declarations rule out this part of the design at package level.

### 1b. Name lookup structures and cost

- **Lexical scopes: one array indexed by NameId.** `check/lexical_lookup.h` documents
  "constant-time lookup of names, regardless of how many scopes" lie between. Its layout is
  `llvm::SmallVector<llvm::SmallVector<Result, 2>, 0> lookup_`, sized once to the identifier
  count (L52).
  - Each `Result` holds `{InstId, ScopeIndex, is_decl_reachable, use_loc_id}`.
  - Declaring a name pushes a result. Leaving a scope pops every name that scope recorded in
    its `Set<SemIR::NameId, 16> names` (`scope_stack.h` L447).
  - `Suspend`/`Restore` temporarily remove and re-add results. Deferred function bodies use
    this.
- **Non-lexical scopes (namespaces, classes, the package).** Each `SemIR::NameScope` holds
  `llvm::SmallVector<Entry> names_` plus `Map<NameId, EntryId> name_map_` (`name_scope.h`
  L286-287).
- **Lookup algorithm.** `ScopeStack::LookupInLexicalScopes` (`scope_stack.cpp` L285-316) works
  as follows:
  1. Index `lookup_[name]` and take the last result (O(1)).
  2. Binary-search the non-lexical scope stack for scopes nested inside that result's scope
     (`llvm::lower_bound`).
  3. `LookupUnqualifiedName` (`name_lookup.cpp` L193-) then probes each of those non-lexical
     scopes from innermost to outermost with one hash lookup each.

  Cost: O(1) for a local name with no class or namespace in between. Otherwise O(log s)
  plus one hash probe per enclosing non-lexical scope. A package-level name referenced from a
  function costs one probe per enclosing non-lexical scope, usually one to three.
- **Scope stack.** `ScopeStack` keeps the lexical scope entries (`ScopeIndex`, names set,
  flags) and a separate `non_lexical_scope_stack_`. Everything is per `Check::Context`, which
  is one file and one thread.

### 1c. Deferred definitions: inline method bodies checked after the class

- **The parser records the skip targets.** `parse/tree.h` L33-46:
  `DeferredDefinition{start_id, definition_id, next_definition_index}` for "a function whose
  definition is deferred because it is defined inline in a class or similar scope".
- **The checker's traversal is not a plain postorder loop.** `NodeIdTraversal::Next()`
  (`check/node_id_traversal.cpp` L27-85) works as follows:
  - On reaching a deferred definition's start node, it calls
    `SuspendFunctionAndPush(index, HandleFunctionDefinitionSuspend(...))`. This saves the
    function's declaration-name state and its lexical lookup entries.
  - It then jumps the iterator to `definition_id + 1`.
  - At the end of a non-nested `ClassDefinition`/`ImplDefinition`/`InterfaceDefinition`/
    `NamedConstraintDefinition`, it switches to "checking deferred definitions" mode. In that
    mode it replays each suspended body by pushing a new chunk, a postorder range over that
    body's nodes.
- **The worklist.** `deferred_definition_worklist.h` holds `CheckSkippedDefinition`,
  `DefineThunk`, `EnterNestedDeferredDefinitionScope` and `LeaveNestedDeferredDefinitionScope`
  tasks. Nested classes defer to the outermost class:
  "A.B.F is type-checked here, with A complete."
- **Stale doc.** `toolchain/docs/check/README.md` L507-516 still calls delayed evaluation "not
  yet implemented". The code above is the current mechanism.

### 1d. Node stack: popping children without child links

From `toolchain/docs/check/README.md` L483-505 and `check/node_stack.h`:

- **Entry layout.** Each entry is `{Parse::NodeId node_id; IdUnion id;}`, with
  `static_assert(sizeof(Entry) == 8)` (`node_stack.h` L357-366). The id's type (`InstId`,
  `NameId`, `InstBlockId`, `FunctionId`, `ClassId`, and so on) is not stored. It follows from
  the parse node's kind or category: `NodeCategoryToIdKind` and `NodeKindToIdKindSpecialCases`
  (L368-470) make the mapping a compile-time function of `NodeKind`. Pops verify the kind in
  debug builds.
- **How a parent finds its children.**
  - Fixed arity: the handler pops that many entries (`PopExpr`, `Pop<Kind>`, `PopIf<Kind>`
    for optional children).
  - Variable arity: introducer or bracketing nodes push a solo parse node with no id
    (`Push(node_id)`). The parent pops until it reaches that solo node
    (`PopAndDiscardSoloNodeId<Kind>`, `PopForSoloNodeId`). The arity or bracketing kind for
    each parent is documented in `parse/node_kind.def`.
- **Side stacks so the main stack is never searched.** The design goal is that "we never need
  to search through the stack" (README L488-492).
  - `ParamAndArgRefsStack` moves each element off the node stack on every comma (`ApplyComma`)
    into an `ArrayStack`. At the closing node, `EndAndPop(start_kind)` returns the element
    list as an `InstBlockId` (`param_and_arg_refs_stack.h`).
  - `StructLiteral` similarly reads field names from `struct_type_fields_stack`.
  - Context holds 18 stacks (`context.h` L502-647), including the node stack, inst-block
    stacks, pattern-block stack, struct field stack, declaration-name stack, scope stack and
    generic-region stack.
- **Introducers set context.** The README (L480-481) describes state "set up by an introducer
  node, updated by its siblings, and then consumed by the bracketing parent node". This
  postorder stand-in for top-down context is how a declaration's introducer establishes what
  its children mean.
- **Parse node size.** A Carbon parse node is 8 bytes (`NodeImpl`: kind, error bit, token
  index; `static_assert(sizeof(NodeImpl) == 8)`, `parse/tree.h` L241). Subtree sizes are not
  stored. `Parse::TreeAndSubtrees` computes them on demand for "less performance sensitive"
  uses such as diagnostics (`parse/tree_and_subtrees.h`).

### 1e. Plans or docs about parallel checking

- **Nothing found at the pinned commit.** I searched `toolchain/docs/**`, `toolchain/check/*`,
  `toolchain/driver/*` and `docs/project/roadmap.md`. Threads appear only in the driver for
  building runtimes (`driver.cpp` L278-292, `--threads`).
- **Files are checked serially.** `check/check.cpp` L543-556 checks units one at a time in Kahn
  topological order of imports (`ready_to_check` list, `imports_remaining` counters). An import
  cycle produces "import cannot be used due to a cycle" (L561-580).
- **Parallelism is planned at file and library granularity.** Proposal p0107
  (`proposals/p000107-code-and-name-organization.md` ~L640) says `impl` files "allow for better
  parallelism" than `api` files alone. p6333 discusses caching checked IR. Neither proposes
  parallelism inside check.

---

## 2. Zig

### 2.1. Is Sema parallel in 0.16 or 0.17? No.

- **Devlog, 2025-06-14, Matthew Lugg, "Parallel Self-Hosted Code Generation"
  (ziglang.org/devlog/2025).** After parallel codegen, the compiler has "one thread performing
  semantic analysis", many codegen threads and one linker thread. Future work lists
  "(in the more distant future) parallelizing semantic analysis". The parallel codegen work
  gave 5% to 50% wall-clock gains for the x86_64 backend.
- **Release notes.**
  - 0.16.0 (tag dated 2026-04-13) has no parallel-Sema item. It mentions work "towards fully
    parallelising" the LLVM backend's IR generation, and adds "Reworked Type Resolution" and
    "Simplified Dependency Loop Rules".
  - 0.17.0 (tag dated 2026-10-01) mentions only the SPIR-V backend becoming
    "multi-threaded like the other backends".
- **Source at 0.17.1.** `src/Zcu/PerThread.zig` `update()`:
  - AstGen runs per file on an `Io.Group` (L164-206, `astgen_group.async(io, workerUpdateFile,
    ...)`).
  - Then "the main semantic analysis loop" (L320-350) runs on the calling thread:
    `while (try zcu.findOutdatedToAnalyze()) |unit| { switch (unit.unwrap()) ... }`.
  - The 0.16.0 tag has the same loop (`PerThread.zig` L321-324).
  - Codegen is a separate task pool (`Zcu.zig` L4060-4095 explains the threading contract).
- **What blocks parallelism.** GitHub ziglang/zig issue #22410 (mlugg, 2025-01-05, still open
  when fetched): generic instances inherit the eval branch quota of their first caller, which
  "disallows parallelizing semantic analysis". The 0.17.1 Sema still carries that comment
  (`Sema.zig` L7060-7063). Lesson: any language rule whose result depends on which use is
  analyzed first blocks parallel checking.

### 2.2. Analysis on demand per declaration: AnalUnit and dependency tracking

- **`AnalUnit`** (`InternPool.zig` L422) is a `packed struct(u64){kind: u32-enum, id: u32}`.
  Its kinds are:
  - `comptime` (a comptime block);
  - `nav_val` (value of a named declaration);
  - `nav_ty` (type of a named declaration, resolvable without the value);
  - `type_layout`;
  - `struct_defaults`;
  - `func` (runtime function body);
  - `memoized_state`.

  Splitting a declaration's type (`nav_ty`) from its value (`nav_val`) and from its body
  (`func`) lets users of a signature avoid waiting for the body.
- **Roots.** Analysis starts from roots: each analysis-root module's file is populated
  (`ensureFilePopulated`), which makes `comptime`, `export` and test declarations referenced
  (`PerThread.zig` L303-316). `scanNamespace` (L3053-) registers every declaration as a `Nav`
  without analyzing it. Nothing else is analyzed until something references it.
- **Referencing a declaration.** `Sema.ensureNavResolved` (`Sema.zig` L30934-30975) does four
  things:
  1. `addReferenceEntry` records the reference graph used for reachability and reference
     traces.
  2. `declareDependency(.{ .nav_ty | .nav_val = nav })` records an incremental dependency edge.
  3. It checks `analysis_in_progress` (loop detection, 2.3).
  4. It recursively calls `pt.ensureNavTypeUpToDate`/`ensureNavValUpToDate` on the same thread
     and stack.
- **Dependency storage.** Lives in the InternPool (`InternPool.zig` L38-98). There is one hash
  map per dependee kind: `src_hash_deps`, `nav_val_deps`, `nav_ty_deps`, `func_ies_deps`,
  `type_layout_deps`, `struct_defaults_deps`, `source_file_deps`, `embed_file_deps`,
  `namespace_deps`, `namespace_name_deps` and memoized-state heads. Each maps to the head of a
  linked list in `dep_entries: ArrayList(DepEntry)` with a free list. `first_dependency:
  AutoArrayHashMap(AnalUnit, DepEntry.Index)` iterates a unit's dependencies so they can be
  dropped on re-analysis.
- **Inferred error sets are lazy.** `Sema.InferredErrorSet` (L207-245) keeps the directly
  returned `errors`, plus `inferred_error_sets` it "should include" without resolving them.
  `ensureFuncIesResolved` (L34071-34096) forces the callee's body only when the set's contents
  are actually needed. This is close to chibi-v2's "error sets computed in a pass after
  checking".
- **Mapping instructions to results.** Sema maps ZIR instructions to AIR results with
  `InstMap`, "simply a slice" indexed by ZIR index minus a start offset instead of a hash table
  (`Sema.zig` L248-258).

### 2.3. Dependency loop detection

- **The in-progress stack.** `Zcu.analysis_in_progress` is
  `AutoArrayHashMap(AnalUnit, ?*const DependencyReason)` (`Zcu.zig` L179-181). Insertion order
  makes it a stack. Every `ensure*UpToDate` puts the unit before analyzing it and
  `swapRemove`s it after (`PerThread.zig`, for example L1413-1414).
- **Detection.** A requested unit that is already in the map is a loop.
  `failWithDependencyLoop` (`Sema.zig` L35700-35739) takes the map suffix from the wanted unit
  to the top and records each edge with its reason in `dependency_loop_nodes`. One
  representative per loop goes in `dependency_loops` (`Zcu.zig` L197-208). Every unit in the
  loop is marked a transitive failure.
- **Reporting.** `addDependencyLoopErrors` (`Zcu.zig` L5003-) prints "dependency loop with
  length N" with one note per edge, for example "type 'Foo' depends on type 'Bar' for field
  declared here". The 0.16.0 notes and the 2026-03-10 devlog ("Type resolution redesign")
  describe this. Order is not yet reproducible: "TODO: sort the dependency loops somehow to
  make the error bundle reproducible" (`Zcu.zig` L5011).
- **0.16 semantics.** Reworked type resolution is lazier: field types are resolved only if the
  type is actually initialized, so a type used purely as a namespace is never laid out. Some
  new cases are loops, for example a struct asking its own `@alignOf` (0.16.0 notes,
  "Simplified Dependency Loop Rules").

### 2.4. Result types and result locations for anonymous literals in AstGen

AstGen walks the AST (which has child links) top-down and passes context down as `ResultInfo`
(`lib/std/zig/AstGen.zig` L267-370).

- **`ResultInfo.rl: Loc` variants:**
  - `discard`;
  - `none`;
  - `ty: Ref`, a type to coerce to;
  - `coerced_ty: Ref`, same, but Sema is guaranteed to coerce, so no `as` is emitted;
  - `ref`, `ref_coerced_ty`, `ref_const`;
  - `ptr`, a typed pointer to store into;
  - `inferred_ptr`, an `alloc_inferred`;
  - `destructure`.

  `resultType()` derives the type when one exists. `br()` (L276) turns `coerced_ty` back into
  `ty` at branch points, so each `if`/`switch` arm receives the type.
- **`.{...}` (`structInitExpr`, L1689-1880).**
  - With `.ty`/`.coerced_ty`: emit `validate_struct_init_result_ty`, then
    `structInitExprTyped` (L1915-1951). For each field that emits
    `struct_init_field_type{container_type, name}` and lowers the initializer with
    `.{ .coerced_ty = field_ty }`. The field type is thus computed from the result type and
    the field name before the field is lowered.
  - With `.ptr`: `structInitExprPtr` initializes fields in place through field pointers.
  - With `.none`/`.inferred_ptr`: `structInitExprAnon` emits `struct_init_anon`, an anonymous
    struct value coerced later by Sema.
  - Typed inits `T{...}` deliberately do not use result locations: "Typed inits do not use RLS
    for language simplicity" (L1827; the array form says the same at L1500).
- **`.red` (L1000-1011).** With a result type, AstGen emits `decl_literal{lhs = res_ty,
  field_name}`, which looks `red` up in the result type. Without one, it emits an
  `enum_literal` value of the `@Type(.enum_literal)` type that coerces later.
- **Implication for chibi-v2.** This design needs the parent before the child, which a
  postorder stream does not give for free (see Options).

### 2.5. Representation of comptime_int and other comptime values

- **Values are interned in the InternPool.** The tags are documented at `InternPool.zig`
  L4955-4990:
  - `int_comptime_int_u32` / `int_comptime_int_i32`: "A comptime_int that fits in a u32/i32",
    with the value stored directly in the item's 32-bit `data`.
  - `int_u8`, `int_u16`, `int_u32`, `int_i32`, `int_usize`: same idea for small typed ints.
  - `int_small`: a typed int that fits in 32 bits.
  - `int_positive` / `int_negative`: "data is a limbs index" (BigInt limbs stored in the pool).
- **Key form.** `Key.Int.Storage = union { u64, i64, big_int }` (L2371-2394).
- **Arithmetic fast path.** `comptimeIntAdd` (`Sema/arith.zig` L1416-1443) tries
  `@addWithOverflow` on u64 first and falls back to `std.math.big.int` with an arena-allocated
  result, which is then interned.
- **Floats.** `comptime_float` is stored as f128 (`float_comptime_float`, "data is extra
  index to Float128", L5029-5031). It is not exact.

---

## 3. Odin

### 3.1. Checker phases (`check_parsed_files`, `src/checker.cpp` L8178-)

In order:

1. Map file paths to scopes; per-thread worker data.
2. `check_import_entities`. Creates file scopes and imports, and collects `when` blocks, in
   parallel (commit b2b03b68a, 2026-10-05). Global `when` and `foreign` blocks are resolved
   on demand through "placeholders" in scopes (`checker_global.cpp` header comment, L1-2 and
   ~L88).
3. Type aliases are corrected per package in parallel, while the global entities are gathered
   from per-file lists.
4. Export entities. Then every package and file scope is marked
   `ScopeFlag_ReadOnly` (L8226-8236): "no global name is declared from here on". From here,
   global lookups take no lock.
5. Merge per-thread arrays. If out of order, `natural_merge_sort` the entities by source
   position.
6. **Check all global entities** in dependency-ordered SCC groups, in parallel (3.2).
7. File-scope directives (parallel). Global untyped expressions are queued.
8. **Check procedure bodies** in parallel (`check_procedure_bodies`, L7358-).
9. Deferred procedures, Objective-C procedures, init-order graph, scope usage, type and
   inline cycle checks (parallel chunks), minimum dependency set, test procedures, entry
   point, unchecked-safety, atomics and escape analysis.

### 3.2. Parallel checking when a declaration needs another declaration

There are three mechanisms at the pinned commit.

1. **Schedule global declarations by a syntactic dependency graph** (`src/checker_global.cpp`).
   - `build_global_groups` (L1271-) finds graph nodes in parallel chunks of 4096 entities.
     It then walks each global declaration's AST in parallel batches, skipping procedure and
     asm bodies. Identifiers are resolved with scope lookups to add edges
     (`global_graph_walk`, L866-, an over-approximation: names bound inside the expression
     may add extra edges).
   - Nodes are unresolved file-scope constants, types, variables, procedures, procedure groups
     and asm templates (L1110-1128).
   - An iterative Tarjan (L686-) numbers SCCs "so that every edge v->w has comp(w) <= comp(v)".
   - `check_global_groups` (L1611-1653) seeds the thread pool with groups whose
     `pending == 0`. Finishing a group decrements each dependent's atomic `pending`, and a
     dependent that reaches 0 becomes a new task (L1597-1609).
   - Members of one SCC are checked serially in a fixed order, because which cycle member
     enters first "can decide whether it checks" (L1512-1513).
   - Each group owns its untyped-expression map and `#soa` list, so no shared queue is touched
     while it runs (L1514).
   - `-no-threaded-checker` or one thread falls back to a serial loop.
2. **Claim on demand with an atomic owner, as the correctness backstop**
   (`check_decl.cpp` L2217-2242). The comment: "checked by whichever thread claims it first".
   - `e->checking_thread.compare_exchange_strong(0, thread_index + 1)`.
   - If another thread owns the entity, `thread_wait_for_owner` (`thread_pool.cpp` L227-266)
     records `waiting_for = owner` and walks the chain of owners' `waiting_for` links:
     - If the chain reaches itself, a cross-thread cycle exists, and the caller reports
       "Illegal declaration cycle".
     - Otherwise it blocks on a futex until the owner finishes. It does not run other tasks
       while waiting.
   - Same-thread re-entry (`owner == me`) is the ordinary single-thread cycle.
   - A global is checked in a fresh context of its own file, not the requester's
     (L2264-2277).
   - `global_group_check_edge` (L1479-1510) counts the cases where a group needed an entity
     outside itself and its finished dependencies ("missing edges").
     `-internal-check-global-edges` prints them and fails, which tests the graph's
     completeness.
3. **Bodies after all signatures.**
   - Procedure bodies are checked only after phase 6, so a body reads only resolved global
     entities.
   - Nested procedure literals are parked on their parent until its body is checked (commit
     14950b8ae).
   - Polymorphic instantiation during body checking takes a per-procedure `GenProcsData`
     `RwMutex`: shared lock for lookups, exclusive lock to insert, with a type-identity hash
     per instance (`checker.hpp` L763-768, `check_expr.cpp` L831-981).
   - "Mark a generated polymorphic procedure before publishing it to other threads" (commit
     9f2a5147b) fixed a publication race.

A testing aid: `-internal-shuffle-global-entities <seed>` processes ready groups in random
order (`checker_global.cpp` L1614-1648; flag at `main.cpp` L838).

### 3.3. Source for "Odin made its checker fully parallel in October 2026"

**Found.**

- **odin-lang/Odin PR #7701, "Multithread The Entire Semantic Checker"** (gingerBill, merged
  2026-10-02T13:17Z). The body says it "Multithreads the semantic checker". It states that
  global entities, procedure bodies, the minimum dependency set and initialization order are
  now checked in parallel.
  - Generated code was verified identical for any `-thread-count`, but "error messages may
    differ between runs".
  - Timings for `odin check` on Windows with 32 threads:

    | Program | Before | After |
    |---|---|---|
    | `examples/all` | 820 ms | 337 ms |
    | `examples/demo` | 101 ms | 49 ms |

  - A full `odin build` of the demo went from 395 ms to 350 ms, because LLVM codegen and
    linking are unchanged.
- **Preceded by PR #7694, "Make global checking codegen order-independent"** (merged
  2026-10-01).
- **Follow-up commits, 2026-10-02 to 2026-10-11, made more stages parallel.** File scopes and
  imports, type aliases, cycle checks, untyped values and the init-order graph.
- **Caveat on "fully".** The word comes from the PR title. Several later stages in
  `check_parsed_files` still run as plain calls on the main thread: export entities, deferred
  procedures, tests and the entry point. Each SCC is still serial.

### 3.4. ExactValue and big integers

- **`ExactValue`** (`src/exact_value.cpp` L55-74) is a tagged union. The kinds are bool,
  string, `BigInt value_integer`, `f64 value_float`, `BigRat *value_rational`, pointer,
  complex, quaternion, compound, procedure, typeid, string16, asm template and variant.
- **`BigInt` is libtommath's `mp_int`** (`big_int.cpp` L48).
  - `MP_MALLOC`/`MP_REALLOC` allocate from the per-thread permanent arena, and `MP_FREE` is a
    no-op (L17-41).
  - There is no small-value fast path: `big_int_add` calls `mp_add` directly (L353-355).
  - Only formatting got a 64-bit shortcut (commit 0e9c80985).
- **Exact rationals for untyped floats, new on 2026-09-29.** `ExactValue_Rational`, "exact
  num/den for untyped float constants" (L31; `big_rat.cpp`, commit db10c6ba3).
  - Results whose numerator or denominator exceed 65536 bits fall back to f64
    (`exact_value_rational_arith_result`, L218-224).
  - The promotion order is Integer < Rational < Float < Complex (L805-825).
- **Untyped expressions.** These are recorded in an `UntypedExprInfoMap` (Ast* → ExprInfo)
  and finalized by `update_untyped_expr_type` (`check_expr.cpp` L5361-), which walks the AST
  children. This mirrors Go (4.2).

### 3.5. Type identity

- **Types are heap `Type*` with structural identity.** `are_types_identical_internal`
  (`types.cpp` L3404-) returns true on pointer equality and otherwise compares structure
  recursively: arrays by count and element, enums field by field with exact values, unions by
  variants, and so on. Named types compare by identity, and aliases unwrap to their base.
- **There is no global interning.** A `canonical_hash` early-out is commented out (L3413-3415).
  Partial interning exists for pointers only: `alloc_type_pointer` caches `^T` on the element
  type with an atomic CAS (`cached_pointer`, L1085-1099), so concurrent threads converge on
  one pointer type.
- **Canonical names.** `name_canonicalization.cpp` produces canonical mangled names (rules in
  `name_canonicalization.hpp`) and type hashes for type info, made deterministic in Feb 2025
  (commit 99d91ccd3).

---

## 4. Go types2 (go1.27.4)

### 4.1. Untyped constants: go/constant

- **Representations** (`src/go/constant/value.go` L75-98):
  - `int64Val` for ints that fit in int64;
  - `intVal{*big.Int}` otherwise;
  - `ratVal{*big.Rat}` for floats representable as a fraction;
  - `floatVal{*big.Float}` otherwise, with 512-bit mantissa precision (`prec = 512`, L69);
  - `complexVal{re, im}`;
  - lazily concatenated `stringVal`.

  The comment explains the policy: the smaller or more precise representation is chosen when
  possible, and once a value is `floatVal` it never returns to `ratVal`.
- **Small-case fast path.** `BinaryOp` on two `int64Val`s (L1144-1181) pre-checks `is63bit`
  for `+`/`-` and `is32bit` for `*`, and only then falls back to `big.Int`. `makeInt`
  normalizes back to `int64Val` when the result fits (L285-290).
- **Rationals.** These stay exact while numerator and denominator have fewer than
  `maxExp = 4 << 10` (4096) bits (`smallInt`, L347-355). Larger results switch to `floatVal`.
- **Integer size cap.** types2 rejects integer constants over 512 bits as overflow
  (`types2/const.go` L41-42).

### 4.2. Delayed typing of untyped expressions (updateExprType)

- **Recording.** `Checker.untyped map[syntax.Expr]exprInfo` (`types2/check.go` L152) holds
  every expression without a final type. `exprInfo{isLhs, mode, typ *Basic, val}` (L27-32).
- **Finalizing.** `updateExprType(x, typ, final)` (`types2/expr.go` L253-262) updates x's
  recorded type and "invokes itself recursively for the operands of x". It checks
  representability for constants and the integer requirement for a formerly untyped
  non-constant shift operand.
- **Leftovers.** Expressions still untyped at the end get their default types recorded in
  `recordUntyped` (`recording.go` L42-54, called at `check.go` L460).
- **Dependency on a tree.** The mechanism walks syntax-tree children downward. Odin does the
  same.

### 4.3. Package-level object ordering and cycle detection

- **Order.** `checkFiles` (`check.go` L422-462) runs these steps in sequence: `initFiles`,
  `collectObjects`, `sortObjects`, `directCycles`, `packageObjects`, `processDelayed(0)`,
  `cleanup`, `initOrder`, `unusedImports`, `recordUntyped`, `monomorph`.
  - `sortObjects` (`resolver.go` L516-) sorts package-level objects "by source-order for
    reproducible processing".
  - `packageObjects` (L646-701) calls `objDecl` on non-alias type declarations first, then
    aliases, then everything else. This works around alias problems (go.dev/issue/25838).
- **Colors.** `objDecl` (`decl.go` L48-) describes white, grey and black objects. Since
  go1.26 these are no longer color bits on the object:
  - not in `Checker.objPathIdx` and `type == nil`: white;
  - in `objPathIdx`: grey;
  - not in `objPathIdx` and `type != nil`: black.

  go1.22-1.25 used `obj.color()`; checked against `decl.go` at go1.22.0, go1.25.0 and go1.26.0.
  "A dependency on a grey object indicates a (possibly invalid) cycle." `push`/`pop` maintain
  `objPath` and `objPathIdx` (`check.go` L200-215).
- **Classifying a cycle.** `validCycle` (`decl.go` L170-) counts the values (`nval`) and type
  definitions (`ndef`) on `objPath[start:]`.
  - Cycles through type parameter lists are allowed.
  - Value-only cycles are deferred to the initialization-order check.
  - Other cycles need at least one type definition.
- **Direct type cycles.** `directCycles` (`cycles.go`, new in go1.26.0, absent in go1.25.0)
  pre-checks direct type-name cycles with the same three-state map.

### 4.4. Delayed function bodies (check.later)

- `later(f)` pushes an action "either at the end of the current statement" or, for local
  constants and variables, before the new name enters scope (`check.go` L188-198).
- `funcDecl` delays the body: `check.later(func() { check.funcBody(...) })` (`decl.go`
  L705-707).
- `processDelayed(0)` runs after all package-level objects (`check.go` L486-512). Actions may
  append more actions, for example nested function literals. Each action restores the Go
  version captured when it was queued.

### 4.5. Published speed figures

- I found no published lines-per-second figure for types2.
- The Go 1.18 release notes say the generics changes, including the full type-checker
  replacement, made compile speed "roughly 15% slower" than 1.17.
- The Go 1.20 notes claim build speed improvements of up to 10%, "back in line with" 1.17.

---

## 5. TypeScript 7 (tsgo)

### 5.1. How checking is parallelized

- **Announcement.** "Announcing TypeScript 7.0" (Daniel Rosenwasser, devblogs, 2026-07-08):
  - TS 7.0 "creates a fixed number of type-checker workers with their own view of the world".
    The default is 4, set with `--checkers`.
  - Given the same inputs they "will always divide them identically and produce the same
    results".
  - `--checkers 1` makes checking single-threaded.
  - `--builders` multiplies with `--checkers` for project references.
  - Parsing, binding and emit are separately parallel per file.
- **Source** (`internal/compiler/checkerpool.go` at the final typescript-go commit):
  - `checkerCount := 4`, overridable, clamped to `[1, min(files, 256)]` (L306-312).
  - Each checker has its own symbol, type and instantiation caches.
  - `forEachCheckerGroupDo` (L472-) runs one task per checker. Each task visits its associated
    files in program order.
  - `BindSourceFiles` binds per file on a work group (`program.go` L554-).

### 5.2. How files are assigned to checkers

- **Round-robin until August 2026.** At commit `caf3ecdece` (2026-03-31) the code was
  `p.fileAssociations[file] = p.checkers[i%checkerCount]` (`checkerpool.go` L105). PR #4313
  begins "Right now, we assign checkers round-robin." TS 7.0 (2026-07-08) therefore shipped
  round-robin.
- **PR #4313 "Assign files to checkers using balanced import affinity"** (jakebailey, merged
  2026-08-13). Assignment is weighted FENNEL streaming graph partitioning:
  - Vertices are files. Edges are resolved in-program imports.
  - A file's weight is AST nodes plus bytes/100, plus normalized import fan-out.
  - Score = already-assigned neighbors minus a convex load penalty, with a hard cap near 101%
    of the average load.
  - Constants were tuned empirically, with stream order chosen per project shape
    (`checkerpool.go` L38-145, L150-232).
  - The source comment records the cost of a bad order. With four checkers, degree-first
    streams gave per-checker completion times of 0.6/2.8/15.3/19.0 s on MUI docs.
  - Users reported "roughly 10% faster with 10% less memory" (PR body).

### 5.3. Measured speedups (TS 6 JS compiler vs TS 7 native; announcement)

These are full builds on one machine. They mix native-code and parallelism gains.

| Codebase | TS 6 | TS 7 (`--checkers 4`) | Speedup | TS 7 (`--checkers 8`) | Speedup |
|---|---|---|---|---|---|
| vscode | 125.7 s | 10.6 s | 11.9x | 7.51 s | 16.7x |
| sentry | 139.8 s | 15.7 s | 8.9x | 12.08 s | 11.6x |
| bluesky | 24.3 s | 2.8 s | 8.7x | 2.01 s | 12.1x |
| playwright | 12.8 s | 1.47 s | 8.7x | 1.16 s | 11x |
| tldraw | 11.2 s | 1.46 s | 7.7x | 1.06 s | 10.6x |

Going from 4 to 8 checkers gives 1.27x to 1.41x, from these numbers. Memory falls 6% to 26%
in the 4-checker configuration.

### 5.4. The duplicated-work note and determinism

- **Duplicated work.** The workers "may end up duplicating some common work". `--checkers 1`
  means "eliminating duplicate work". The source says the same of partitioning: grouping
  related files "reduces duplicated cache construction" (`checkerpool.go` L48).
- **Order dependence.** "In rare cases, varying the number of `--checkers` may surface
  order-dependent results". The team suggests a fixed count across environments.
- **Content-based ordering.** From "Announcing TypeScript 6.0" (2026-03-23):
  - Type IDs used to be assigned in encounter order and used to sort unions.
  - TS 7 instead "sorts its internal objects (e.g. types and symbols)" by their content, so
    every checker sees the same order.
  - TS 6's `--stableTypeOrdering` emulates this at a cost "up to 25% depending on codebase".
- **Variance (PR #4820, Hejlsberg, merged 2026-08-05).** Variance computation order made
  baselines differ between partitions. The fix restarts a circular variance computation from
  the symbol that sorts lowest in declaration order. PR #4313 relies on this fix to change
  partitions safely.

---

## 6. rustc parallel front end (status 2025-2026)

- **Baseline numbers** (Inside Rust blog, 2023-11-09, "Faster compilation with the parallel
  front-end in nightly"):
  - The front end uses Rayon for fine-grained parallelism, with locks and atomics on shared
    structures.
  - With `-Z threads=8`, "compile times can be reduced by up to 50%".
  - Single-threaded mode costs 0% to 2%.
  - Memory use rises up to 35%.
- **2025H1 goal** (goals.rust-lang.org/2025h1/parallel-front-end.html):
  - Aimed to raise the "average speed increase from 20% to 25%" at 8 cores and 8 threads.
  - Noted deadlocks, and slowdowns above 16 threads caused by data races.
- **2026 goal** ("Promoting Parallel Front End", tracking issue rust-lang/goals#121, contact
  petrochenkov):
  - Targets stabilization.
  - Notes "inconstancy of query cycle errors between parallel and serial compilers".
  - Plans parallel name resolution and macro expansion.
- **Status updates on goals#121:**

  | Date | Update |
  |---|---|
  | 2026-06-25 | Last known deadlocks and crashes fixed (rust#143035, #151509). Stabilization MCP filed: compiler-team#1005. |
  | 2026-07-28 | Parallel UI test suite made blocking on CI (rust#159833). |
  | 2026-09-02 | `-j`/`--jobs` limits merged (rust#159675). |
  | 2026-10-27 | Deterministic def-id and HashMap encoding fixed the last known binary-reproducibility issues; the top 1000 crates were tested. "Enabling parallel frontend by default on nightly" is still in progress: rust#162848, "Use 2 parallel frontend threads by default on the nightly and dev channel", open since 2026-09-16. A parallel AST-to-HIR lowering PR (rust#163495) measured 2.87% to 5.78% wall-time gains on serde. |

- **As of 2026-11-12:** not on by default in nightly and not stable (`-Zthreads` only).
- **Non-determinism catalogued in MCP compiler-team#1005** (2026-06-23):
  1. "diagnostics are simply reported in different order", failing at least 5-10% of UI tests;
  2. global IDs such as const-eval allocation IDs leaking into messages;
  3. notes attached to the "first reported diagnostic of this kind";
  4. query-cycle diagnostics, which depend on the execution graph's state when the cycle is
     found.
- **Mechanisms** (rust 1.99.0 source):
  - Interned type sets are `ShardedHashMap` (`rustc_middle/src/ty/context.rs` L131), with 32
    cache-aligned locked shards in multi-threaded mode and one lock otherwise
    (`rustc_data_structures/src/sharded.rs` L14-40).
  - In parallel mode, a query cycle leaves threads blocked on each other's query latches, and a
    deadlock handler breaks it. `break_query_cycle` runs a DFS over all active query jobs,
    breaks one cycle, and resumes one waiter per round (`rustc_query_impl/src/job.rs`
    L421-440).

---

## 7. Jai: all UNVERIFIED

Jai has no public compiler source or public release; the beta is closed. I found no primary
text source by the author on the checker's internals, and video talks were not reviewed. The
following comes from community material (jai.community wiki, "The Way to Jai" book) and is
UNVERIFIED:

- **Workspaces.** A metaprogram creates a workspace (`compiler_create_workspace`), sets build
  options, adds files, and runs a message loop: `compiler_begin_intercept`, then
  `compiler_wait_for_message` until `.COMPLETE`, then `compiler_end_intercept`.
- **Message kinds.** Reported to include `FILE`, `IMPORT`, `PHASE`, `TYPECHECKED` (with the
  typechecked declarations) and `COMPLETE`.
- **`#run`.** Executes compile-time code in a bytecode interpreter inside the compiler.
- **Declaration order and threading.** Order is said not to matter, and the compiler is said
  to run "multi-threaded as a kind of job system".
- **Waiting on dependencies.** The claim that typechecking units sleep on unresolved
  identifiers and resume later is UNVERIFIED. No source found.

---

## 8. Untyped or arbitrary-precision compile-time numbers

| Compiler | Representation | Small case kept fast by | Source |
|---|---|---|---|
| Zig | `comptime_int` interned in the InternPool; `comptime_float` is f128. | u32/i32 values stored inline in the item (`int_comptime_int_u32/_i32`); key storage `u64`/`i64`; arithmetic tries u64 with overflow check first, then BigInt limbs (arena), then interns the result. | `InternPool.zig` L4955-4990, L2371-2394; `Sema/arith.zig` L1416-1443 |
| Odin | `ExactValue` with libtommath `mp_int` for every integer; `BigRat` for untyped floats since 2026-09-29, falling back to f64 above 65536-bit components. | Only arena allocation (`MP_FREE` no-op). No small-integer path in arithmetic. Parsing and 64-bit formatting were optimized (commits 374024fc7, 0e9c80985). | `big_int.cpp` L17-48, L353; `exact_value.cpp` L31, L218-224 |
| C3 | No arbitrary precision. Constant ints are `Int{Int128 i; TypeKind type}`; floats `Float{Real f; TypeKind}`. A literal's type is the first of int, uint, long, ulong, int128, uint128 that fits. A constant narrows implicitly when the value fits (`expr_const_will_overflow`). | Fixed 128-bit arithmetic, no allocation; there is no "untyped" integer. `{...}` without a target becomes a `CONST_UNTYPED_LIST`, converted later by cast rules (ulist to struct, array or slice). | `compiler_internal.h` L114-125; `parse_expr.c` L1696-1719; `sema_casts.c` L1499-1530, L1003-1050 |
| Jai | UNVERIFIED (no primary source found). | UNVERIFIED | n/a |
| Go (reference) | `go/constant`: int64Val, big.Int, big.Rat (under 4096-bit components), big.Float (512-bit); ints capped at 512 bits. | int64 path with `is63bit`/`is32bit` pre-checks; `makeInt` renormalizes to int64. | 4.1 |
| Carbon (reference) | `IntId`: "Small values are internalized into the ID itself"; large values index canonical `APInt`s. | The top 3/4 of the 32-bit range embeds values; indices use the rest, so most ints need no store access. Literals have type `IntLiteral`, converted at use. | `toolchain/base/int.h` L29-60 |

---

## 9. Name-resolution data structures

Cost assumes interned names. "Depth" means the number of enclosing scopes walked.

| Compiler | Local or lexical names | Package, namespace or module names | Cost per lookup | Threading |
|---|---|---|---|---|
| Carbon | Dense array indexed by `NameId`; each slot is a small stack of results (innermost last). Push on declare; pop per name on scope exit (each scope keeps a `Set<NameId,16>`). | Per-scope `Map<NameId, EntryId>` plus an entries vector; the non-lexical scope stack is binary-searched, then probed innermost-first. Missed lookups poison the name. | O(1) for locals; plus one hash probe per enclosing non-lexical scope | One file per thread |
| Zig AstGen | Linked list of scope nodes walked outward, comparing u32 string indices. It keeps walking after a match to reject shadowing and ambiguity. | `ns.decls` hash map per namespace scope (AstGen). In Sema, `pub_decls`/`priv_decls` array-hash maps keyed by `Nav`, hashed by interned name, walked up namespace parents (`lookupIdentifier`, `Sema.zig` L5984). | O(depth) compares for locals; one hash probe per enclosing namespace | AstGen per file in parallel; Sema serial |
| Odin | Per-scope `ScopeMap`: Robin Hood open addressing keyed by `InternedString` (u32) with a stored 32-bit hash, allocated on first insert. `scope_lookup_parent` walks parents. | Same structure. Global scopes are read-only after collection. | O(depth) probes | Read-only scopes need no lock; other scopes take a shared `RwMutex` per probe |
| Go types2 | Per-scope `map[string]Object` (lazily allocated); `lookupScope` walks parents. | Same, with the universe scope last. | O(depth) string-hash probes | Serial per package |
| TypeScript 7 | Per-container `SymbolTable = map[string]*Symbol` (binder); `NameResolver.Resolve` walks AST parent links checking `Locals()`, then globals. | Merged global symbol table. | O(depth) string-hash probes | Each checker resolves independently over shared, read-only bound ASTs |
| rustc | Stack of `Rib`s, each `FxIndexMap<Ident, Res>`, searched innermost-out (`rustc_resolve/src/late.rs` L287-291). | Module resolution maps (details UNVERIFIED). | O(ribs) probes | Name resolution not yet parallel (2026 goal plans it) |
| C3 | Flat array of live local `Decl*`, scanned from the top comparing interned name pointers (`sema_find_local`, `sema_name_resolution.c` L513-540). | Unit-local hash table, then module or global `decltable`. | O(live locals) compares | Serial (threads only in backend codegen) |
| Jai | UNVERIFIED | UNVERIFIED | UNVERIFIED | UNVERIFIED |

---

## Options for chibi-v2's checker

### What the checker reads

- **A. Interpret the postorder stream directly** (Carbon).
  - Use a value stack of `(node u32, value u32)`, 8 bytes like Carbon's `NodeStack::Entry`,
    with the value's meaning implied by the node kind.
  - A parent pops its fixed arity, or pops to its `*_START` marker.
  - Carbon avoids searching the stack by moving list elements into a side array on each comma.
    chibi-v2 has no comma nodes, so an equivalent is to push the value-stack height when a
    start marker is visited. The closing list node then takes the slice `[height, top)`. This
    is my adaptation, not something Carbon does verbatim.
  - Context-dependent meaning must be set up by nodes visited earlier: start markers and
    already-visited siblings.
- **B. Lower to a flat IR first** (Zig AstGen to ZIR, then Sema over ZIR with a dense
  `InstMap`). This costs an extra pass and memory. AstGen relies on a tree to pass result
  types down.
- **C. Keep the stream link-free, record ranges where needed.**
  - The parser records per-declaration and per-body node ranges, like Carbon's
    `DeferredDefinition{start_id, definition_id, next}`.
  - With these the checker can skip, defer, claim and schedule declarations and bodies.
  - Subtree data can be computed lazily, for diagnostics only (Carbon `TreeAndSubtrees`).

### Order of checking and claiming

- **Declaration-before-use, single pass** (Carbon, with forward declarations and name
  poisoning). Not compatible with any-order package declarations, but usable inside function
  bodies.
- **Collect all names, then resolve on demand with an in-progress stack** (Go `objDecl` with
  grey via `objPathIdx`; Zig `AnalUnit` with `analysis_in_progress`). A cycle is the stack
  suffix and can be reported with the reason for each edge, as Zig does. Single-threaded in
  both.
- **Dependency-graph scheduling plus an atomic claim backstop** (Odin, October 2026).
  1. Build a syntactic graph over declaration headers, skipping bodies.
  2. Compute Tarjan SCCs.
  3. Schedule SCCs by ready counts on the thread pool.
  4. As a fallback, CAS-claim with owner = thread + 1, futex-wait, and detect cross-thread
     cycles by walking each waiting thread's single `waiting_for` link.

  This matches chibi-v2's not-started/in-progress/done mark. A blocked waiter idles its
  thread, so graph quality matters; Odin can count missing edges
  (`-internal-check-global-edges`).
- **Split each declaration into separately claimable units** (Zig `nav_ty`, `nav_val`,
  `func`), so that signature users never wait on bodies. The exception is chibi-v2's private
  functions with omitted return types.
  - Carbon forbids calls before the return type is deduced.
  - chibi-v2 would instead claim the callee's body. That makes body-to-body dependencies, so
    such cycles need a rule, for example an error like Zig's dependency loop.
- **Inferred error sets.** Zig records "include the inferred error set of f" edges and resolves
  them only when inspected. A post-check pass can union along those edges with an SCC fixed
  point.
- **Ban order-dependent rules.** Examples are Zig #22410 (quota inherited from the first
  caller), TS variance order (#4820) and TS union order by type ID.

### How literals get their type from context

- **Top-down expected type** (Zig `ResultInfo`, C3 `sema_analyse_inferred_expr(to)`).
  - In postorder the context is usually visited before the literal:
    - `x: T = .{..}`: the type child precedes the initializer;
    - a call: the callee precedes the `NODE_CALL_ARGS` marker and the arguments;
    - `return`: the declared return type;
    - assignment: the left-hand side comes first.
  - An "expected type stack" pushed by that earlier sibling and read at
    `NODE_LITERAL_START`/`NODE_ENUM_LITERAL` gives Zig's `struct_init_field_type` and
    `decl_literal` behavior without child links.
- **Bottom-up provisional value, converted at the use site** (Carbon `StructLiteral` with a
  structural type, then `Convert` recursing into elements and initializing storage in place;
  C3 `CONST_UNTYPED_LIST`; Zig `struct_init_anon` and `enum_literal` without a result type).
  This always works, including when the context is not yet known, such as an inferred return
  type.
- **Hybrid.** Use the expected type when present, and a provisional anonymous literal
  otherwise. Zig's AstGen effectively does this.

### Untyped constants

- **Representation choices:**
  - an inline small value plus an interned big value (Zig, and Carbon's `IntId` embedding);
  - an int64 fast path with overflow pre-checks, falling back to bignum (Go);
  - always bignum, arena-allocated (Odin);
  - fixed Int128 with a kind and no untyped type (C3).

  Go and Odin keep untyped floats exact as rationals up to a size bound (4096 bits for Go,
  65536 for Odin), then go lossy.
- **Typing at use.** Go and Odin keep a side map of untyped expressions and push the final
  type down the operand tree (`updateExprType`), which needs child links. Carbon and Zig
  convert at the use site instead. With no child links, folding constant expressions eagerly
  means only the folded value needs a type at its use. Go's non-constant untyped cases, such
  as shift operands, are the ones that force the downward walk.

### Name lookup

- **Locals.**
  - Carbon gives O(1) through a dense array indexed by interned name id, per thread. The
    memory cost is threads × distinct ids × entry size. My estimate, with 4-byte entries
    pointing into a binding stack: 200k ids × 16 threads ≈ 12.8 MB.
  - C3's linear scan of live locals is simpler.
  - Zig's linked scope walk also catches shadowing.
- **Package level.** Collect names first, then freeze the table read-only so that lookups take
  no lock (Odin `ScopeFlag_ReadOnly`). Key it by u32 name id (Odin Robin Hood map; Carbon
  `Map<NameId,EntryId>`).

### Type interning across threads

- **Global interner with per-thread locals and sharded maps** (Zig InternPool, already known).
- **Sharded locked hash sets** (rustc: 32 cache-aligned shards, one lock when single-threaded).
- **Canonical store per file, single-threaded** (Carbon).
- **No interning.** Structural `are_types_identical` plus an atomic per-type cached pointer
  type (Odin). Identity tests then cost a recursive compare.
- **Private type tables per checker with content-based ordering** (TS 7). Accepts duplicated
  work. Ordering cost "up to 25%" in TS 6's emulation.

### Deterministic diagnostics

- **Sort at the end by source position.**
  - Odin uses a stable natural merge sort and merges diagnostics at the same position.
  - Zig sorts failed files by path and analysis errors by `ErrorMsg.order` before emitting
    (`Compilation.zig` L3917-3996).

  Sorting fixes order only, not content. Odin still says "error messages may differ between
  runs", and Zig's dependency-loop reports are not yet reproducible (`Zcu.zig` L5011).
- **Content sources to design out** (rustc MCP #1005):
  - which diagnostic is "first" when notes depend on it;
  - global IDs or counters leaking into messages;
  - cycle reports that depend on which thread found the cycle.

  Mitigations: report a cycle from its lowest declaration index, as TS #4820 does with symbol
  order; number anonymous things by source position; make cycle members' checking order fixed
  (Odin SCC member order).
- **Test tools.**
  - Odin `-internal-shuffle-global-entities <seed>`;
  - Odin `-internal-check-global-edges`;
  - rustc's parallel UI test suite, blocking on CI since July 2026;
  - the TS PRNG partition experiment (PR #4313).

---

## Not verified or not found

- **Jai.** All checker internals, the message loop, workspaces, `#run` and constant
  representation are UNVERIFIED. There is no public primary text source.
- **Carbon.** No document or plan for parallel checking inside a file or library exists at the
  pinned commit (searched docs, check and driver). Parallelism is only implied at file or
  library granularity (p0107).
- **Zig.** No source says Sema is parallel in 0.16.0 or 0.17.x. Issue #22410's state was read
  from GitHub; Zig's tracker has moved to Codeberg.
- **Go.** No lines-per-second figure for types2 found; only relative figures from the 1.18 and
  1.20 notes.
- **TypeScript.** Which TS 7 release first ships the FENNEL partitioning after PR #4313 was
  not checked.
- **rustc.** Module-level resolution map details are UNVERIFIED. I did not check whether any
  2026 release enables the parallel front end by default; as of the 2026-10-27 update it was
  still in progress.
