# Draft reports for cppgm

Two reports cover what still fails on cppgm's main branch at commit 0025136: a wrong-code bug, and four optimizer crashes that each need a switch case its selector can't reach. Each section below is one issue, ready to paste. Before filing, check cppgm's open issues for duplicates and retest on the latest main.

---

## lowiropt -O1: a switch on an i1 with cases 0 and 1 always takes the default

Found in 0.10.2; still present on main at 0025136. x86-64 Linux, tools built with GCC 13.

At `-O1`, `-O2` and `-O3`, `lowiropt` rewrites a `switch` on an `i1`, whose cases 0 and 1 both go to one block, into code that always takes the default.

`switch-on-i1.lowir`:

```
function @f(%x : i64) -> i64 [no_inline=yes] {
  block ^0:
    %c = cmp lt i64 %x, 2
    %b = convert trunc i1 i64 %c
    switch %b, ^2, 0:^1, 1:^1

  block ^1:
    return i64 0

  block ^2:
    return i64 1
}

function @main(%argc : i32, %argv : ptr) -> i32 [role=entry] {
  block ^0:
    %x = convert sext i64 i32 %argc
    %r = call i64 @f(%x)
    %code = convert trunc i32 i64 %r
    return i32 %code
}
```

```
$ dev/lowiropt -O0 -o o0.lowir switch-on-i1.lowir && dev/lowir2native -O0 o0.lowir -o o0 && ./o0; echo $?
0
$ dev/lowiropt -O1 -o o1.lowir switch-on-i1.lowir && dev/lowir2native -O0 o1.lowir -o o1 && ./o1; echo $?
1
```

Expected 0 at every level: every `i1` value has a case that goes to `^1`. At `-O1`, the body of `@f` is `return i64 1`.

On a larger input, 0.10.2's `-O1` output shows the step that goes wrong. The switch became a range test whose bound is 0:

```
    %t3 = convert trunc i1 i64 %t2
    %t9 = cmp ult i1 %t3, 0
    branch %t9, ^4, ^5
```

Here `^4` is the case block and `^5` the default. The bound of a range test for cases 0 to 1 is 2, and 2 wraps to 0 in `i1`. That suggests the switch-to-range rewrite computes the bound in the selector's type.

---

## lowiropt: four crashes on a switch with a case its selector can't reach

Found in 0.10.2; still present on main at 0025136. x86-64 Linux, tools built with GCC 13.

`lowiropt` segfaults in four places on generated LowIR. Each reduced input has a `switch` with a case outside the selector's known range: a negative case on a zero-extended `u8` or `u16`, or 149 on a value masked to 0..127. In each, making that case reachable stops the crash. Two of the inputs crash in `compact_body` or in the writer at some levels, as if a block reference outlives the block, so the four may share a cause.

| File | Level | Crashes in |
|---|---|---|
| `inliner-crash.lowir` | `-O1` | `opt::clone_call`, inlining `@h` into `@e` |
| `jump-threading-crash.lowir` | `-O1` | `JumpThreading::find_use_dominators`, from `opt::thread_jumps` |
| | `-O2`, `-O3` | `opt::compact_body` |
| `compact-body-crash.lowir` | `-O1` | `lowir::LocalNames::block`, writing the output |
| | `-O2`, `-O3` | `opt::compact_body`, from `finish_function` |
| `sink-values-crash.lowir` | `-O2`, `-O3` | `opt::sink_values`, from `finish_function` |

```
$ dev/lowiropt -O1 -o out.lowir inliner-crash.lowir
Segmentation fault
```

`inliner-crash.lowir` has two switches in a row on one zero-extended `u8`, the second with the case -61, and a loop after them:

```
declare function @trap(%kind : i32) -> void [unwind=no, return=noreturn, linkage=c, cold=yes]

function @h(%x : i64) -> i64 [linkage=c, binding=strong] {
  slot $n : i64

  block ^entry:
    store i64 0, $n
    %small = cmp ult i64 %x, 31909797
    branch %small, ^switch1, ^preheader

  block ^switch1:
    %low = binary and i64 %x, 255
    %byte = convert trunc u8 i64 %low
    %wide = convert zext i32 u8 %byte
    switch %wide, ^default1, -44:^switch2

  block ^switch2:
    switch %wide, ^default2, -61:^arm

  block ^arm:
    jump ^arm_exit

  block ^arm_exit:
    jump ^preheader

  block ^preheader:
    jump ^header

  block ^header:
    jump ^body

  block ^body:
    %i = load i64 $n
    %overflows = cmp gt i64 %i, 9223372036854775806
    branch unlikely %overflows, ^overflow, ^next

  block ^next:
    %j = binary add nowrap i64 %i, 1
    store i64 %j, $n
    jump ^header

  block ^overflow:
    call void @trap(1)
    unreachable

  block ^default1:
    unreachable

  block ^default2:
    unreachable
}

function @e(%x : i64) -> i64 [linkage=c, binding=strong] {
  block ^entry:
    %masked = binary and i64 %x, 32767
    %r = call i64 @h(%masked)
    return i64 %r
}
```

```
#0 opt::(anonymous namespace)::clone_call(...)
#1 opt::(anonymous namespace)::InlineWaveRunner::process(...)
#2 opt::(anonymous namespace)::InlineWaveRunner::run()
#3 opt::run_inline_wave(...)
#4 opt::optimize_program(...)
```

The crash goes away when the second case is one the selector can reach (61 in place of -61), when either switch becomes a `cmp` and a `branch`, or when the loop goes. The first case can be reachable: 44 in place of -44 still crashes. The generated input this came from crashes at `-O1`, `-O2` and `-O3`; this reduced file crashes only at `-O1`.

<details>
<summary><code>jump-threading-crash.lowir</code>: the case 149 on a value masked to 0..127</summary>

```
target linux 1
function @p_e2(%__param0 : ptr, %__param1 : i64, %__param2 : i64, %__param3 : i64, %__param4 : f64) -> i64 [linkage=c, binding=strong] {
  slot $1 : f32
  slot $7 : f64
  slot $9 : u16
  slot $10 : i16
  slot $12 : obj<64x4>
  slot $16 : obj<24x8>
  slot $18 : obj<40x8>
  slot $23 : obj<144x8>
  slot $27 : i64
  slot $28 : i64
  slot $29 : u32
  slot $32 : ptr
  slot $33 : i64
  slot $34 : f64
  slot $59 : i64
  slot $60 : f64
  block ^0:
    %f = convert sitofp f32 i64 %__param2
    store f32 %f, $1
    store f64 0, $7
    store u16 0, $9
    store i16 0, $10
    store i64 0, $27
    store i64 0, $28
    store u32 0, $29
    store i64 0, $33
    store f64 0, $34
    store i64 0, $59
    store f64 0, $60
    store f64 2, $7
    %t16 = binary and i64 %__param3, 65535
    %t17 = convert trunc u16 i64 %t16
    store i16 %t17, $9
    %t18 = binary and i64 %__param1, 32767
    %t19 = convert trunc i16 i64 %t18
    store i16 %t19, $10
    jump ^3
  block ^3:
    %t64 = load i16 $10
    %t65 = binary and i16 %t64, 127
    %t66 = convert trunc i8 i16 %t65
    jump ^5
  block ^5:
    %t68 = convert sext u16 i8 %t66
    switch %t68, ^4, 50:^6, 80:^6
  block ^6:
    switch %t68, ^11, 50:^8, 80:^9, 149:^12
  block ^8:
    %t73 = load f32 $1
    %t74 = convert fpext f64 f32 %t73
    store f64 %t74, $60
    jump ^12
  block ^9:
    %t75 = load u16 $9
    %t76 = convert zext i32 u16 %t75
    %t77 = convert sitofp f64 i32 %t76
    store f64 %t77, $60
    jump ^12
  block ^12:
    %t80 = load f64 $60
    %t85 = addr $16
    %t86 = index i8 %t85, 8
    store f64 %t80, %t86
    jump ^16
  block ^16:
    %t106 = addr $18
    %t107 = addr $16
    copyobj 24x8 %t107, %t106
    jump ^17
  block ^17:
    %t144 = addr $23
    %t145 = addr $18
    copyobj 40x8 %t145, %t144
    jump ^18
  block ^18:
    %t164 = load i64 $28
    %t165 = cmp lt i64 %t164, 16
    branch %t165, ^19, ^29
  block ^19:
    %t166 = addr $12
    %t167 = load i64 $28
    jump ^21
  block ^21:
    %t169 = index i32 %t166, %t167
    %t170 = load u32 %t169
    store u32 %t170, $29
    %t171 = load i64 $27
    %t172 = binary mul i64 %t171, 31
    %t173 = load u32 $29
    %t174 = convert zext i64 u32 %t173
    %t175 = binary add i64 %t172, %t174
    store i64 %t175, $27
    jump ^22
  block ^22:
    %t176 = load i64 $28
    %t177 = binary add i64 %t176, 1
    store i64 %t177, $28
    jump ^18
  block ^29:
    %t192 = addr $23
    store ptr %t192, $32
    jump ^30
  block ^30:
    %t193 = load i64 $33
    %t194 = cmp lt i64 %t193, 3
    branch %t194, ^31, ^93
  block ^31:
    %t195 = load ptr $32
    %t196 = load i64 $33
    jump ^33
  block ^33:
    %t198 = index i64 %t195, %t196
    %t199 = load f64 %t198
    store f64 %t199, $34
    %t200 = load f64 $34
    %t201 = cmp lt f64 %t200, 0
    branch %t201, ^34, ^38
  block ^34:
    %t202 = load i64 $27
    %t203 = binary add i64 %t202, 1
    store i64 %t203, $27
    jump ^38
  block ^38:
    %t210 = load i64 $33
    %t211 = binary add i64 %t210, 1
    store i64 %t211, $33
    jump ^30
  block ^93:
    %t394 = load i64 $27
    %t395 = load i64 $59
    %t396 = binary add i64 %t394, %t395
    return i64 %t396
  block ^4:
    unreachable
  block ^11:
    unreachable
}
```

```
#0 opt::(anonymous namespace)::JumpThreading::find_use_dominators()
#1 opt::(anonymous namespace)::JumpThreading::constant_phis(bool)
#2 opt::thread_jumps(...)
#3 opt::(anonymous namespace)::run_pass(...)
#4 opt::(anonymous namespace)::scalar_rounds(...)
#5 opt::optimize_program(...)
```

With 100 in place of 149, it compiles.

</details>

<details>
<summary><code>compact-body-crash.lowir</code>: the case -3 on a zero-extended <code>u16</code></summary>

```
target linux 1
declare function @chibi_trap(%arg0 : i32) -> void [unwind=no, return=noreturn, linkage=c, binding=strong, cold=yes]
function @p_h0(%__param0 : ptr, %__param1 : u32, %__param2 : ptr, %__param3 : i64) -> obj<16x8> [result_classes=integer_integer, linkage=c, binding=strong] {
  slot $3 : i64
  slot $6 : obj<16x8>
  slot $7 : i64
  slot $8 : i64
  block ^25:
    store i64 0, $3
    store i64 0, $7
    store i64 0, $8
    %z6 = addr $6
    store i64 0, %z6
    %z6b = index i8 %z6, 8
    store i64 0, %z6b
    %t47 = convert zext i64 u32 %__param1
    store i64 %t47, $7
    jump ^26
  block ^26:
    %t48 = load i64 $7
    store i64 %t48, $8
    jump ^1
  block ^1:
    %t63 = load i64 $8
    store i64 %t63, $3
    %t64 = load i64 $3
    %t65 = binary and i64 %t64, 255
    %t66 = convert trunc u8 i64 %t65
    %t67 = binary umod u8 %t66, 3
    %t68 = convert zext i64 u8 %t67
    jump ^38
  block ^38:
    %t75 = binary sub i64 %__param3, %t68
    %t76 = cmp lt i64 0, 0
    %t77 = cmp gt i64 0, 2
    %t78 = cmp gt i64 2, %t75
    %t79 = binary or i64 %t76, %t77
    %t80 = binary or i64 %t79, %t78
    branch unlikely %t80, ^6, ^34
  block ^34:
    %t83 = addr $6
    %t85 = load obj<16x8> %t83
    return obj<16x8> %t85
  block ^6:
    call void @chibi_trap(6)
    unreachable
}
function @p_h2(%__param0 : ptr, %__param1 : u16) -> f32 [linkage=c, binding=strong] {
  slot $0 : i64
  slot $1 : obj<16x4>
  slot $16 : u32
  block ^0:
    store i64 0, $0
    store u32 0, $16
    jump ^1
  block ^1:
    %t1 = load i64 $0
    %t2 = cmp lt i64 %t1, 3
    branch %t2, ^2, ^38
  block ^2:
    %t3 = load i64 $0
    jump ^5
  block ^5:
    %t5 = binary add nowrap i64 %t3, 1
    store i64 %t5, $0
    %t6 = load i64 $0
    %t7 = binary and i64 %t6, 4294967295
    %t8 = convert trunc u32 i64 %t7
    switch %t8, ^8, 2:^6, 3:^6
  block ^6:
    %t9 = convert zext i32 u16 %__param1
    switch %t9, ^13, -3:^14, 3:^12
  block ^12:
    %t16 = binary and u16 %__param1, 32767
    %t17 = copy i16 %t16
    %t18 = binary and i16 %t17, 32767
    %t19 = convert sext u32 i16 %t18
    store u32 %t19, $16
    jump ^14
  block ^13:
    %t20 = load i64 $0
    %t21 = binary and i64 %t20, 4294967295
    %t22 = convert trunc u32 i64 %t21
    store u32 %t22, $16
    jump ^14
  block ^14:
    %t23 = load u32 $16
    %t33 = addr $1
    %t34 = call obj<16x8> @p_h0(%__param0, %t23, %t33, 4)
    jump ^25
  block ^25:
    jump ^1
  block ^8:
    %t100 = cmp ult u16 3, %__param1
    branch unlikely %t100, ^4, ^1
  block ^38:
    jump ^38
  block ^4:
    call void @chibi_trap(1)
    unreachable
}
```

```
#0 opt::compact_body(...)
#1 opt::(anonymous namespace)::finish_function(...)
#2 opt::optimize_program(...)
```

With 5 in place of -3, it compiles.

</details>

<details>
<summary><code>sink-values-crash.lowir</code>: the case -11 on a zero-extended <code>u8</code></summary>

```
target linux 1
declare function @chibi_trap(%arg0 : i32) -> void [unwind=no, return=noreturn, linkage=c, binding=strong, cold=yes]
function @p_h5(%__param0 : ptr, %__param1 : f32) -> i64 [linkage=c, binding=strong] {
  slot $4 : i64
  block ^44:
    store i64 0, $4
    %t248 = convert fpext f64 f32 %__param1
    jump ^45
  block ^45:
    %t252 = convert fptoui u32 f64 %t248
    %t253 = binary umod u32 %t252, 2
    %t254 = load i64 $4
    jump ^46
  block ^46:
    %t258 = convert trunc u32 i64 %t254
    %t259 = binary umod u32 %t258, 3
    jump ^47
  block ^47:
    %t261 = binary add nowrap u32 %t259, 1
    %t263 = convert zext i64 u32 %t253
    %t264 = convert zext i64 u32 %t261
    jump ^48
  block ^48:
    %t271 = binary sub i64 %t264, %t263
    return i64 %t271
}
function @p_e0(%__param0 : ptr, %__param1 : i64, %__param2 : i64, %__param3 : i64, %__param4 : f64) -> i64 [linkage=c, binding=strong] {
  slot $1 : u8
  slot $2 : i32
  slot $3 : i64
  slot $6 : i32
  slot $7 : i64
  slot $11 : i64
  slot $13 : i1
  block ^0:
    store u8 0, $1
    store i32 0, $2
    store i64 0, $3
    store i32 0, $6
    store i64 0, $7
    store i64 0, $11
    %t2 = binary and i64 %__param3, 255
    %t3 = convert trunc u8 i64 %t2
    store i8 %t3, $1
    %t4 = load u8 $1
    %t5 = convert zext i32 u8 %t4
    store i32 %t5, $2
    jump ^1
  block ^1:
    %t6 = load i64 $3
    %t7 = cmp lt i64 %t6, 2
    branch %t7, ^2, ^9
  block ^2:
    %t8 = load i64 $3
    jump ^5
  block ^5:
    %t10 = binary add nowrap i64 %t8, 1
    store i64 %t10, $3
    %t12 = load i32 $2
    %t13 = cmp eq i32 %t12, 0
    branch unlikely %t13, ^6, ^1
  block ^9:
    %t25 = load i32 $2
    store i32 %t25, $6
    %t26 = load i32 $6
    switch %t26, ^14, -11:^15, 6:^28
  block ^15:
    jump ^39
  block ^28:
    %t43 = load i64 $11
    %t44 = cmp lt i64 %t43, 2
    branch %t44, ^29, ^39
  block ^29:
    %t45 = load i64 $11
    jump ^31
  block ^31:
    %t47 = binary add nowrap i64 %t45, 1
    store i64 %t47, $11
    %t48 = load i32 $6
    %t49 = unary bitnot i32 %t48
    store i32 %t49, $6
    jump ^28
  block ^14:
    %t60 = binary and i64 %__param1, 65535
    %t61 = convert trunc u16 i64 %t60
    %t62 = cmp ult u16 56, %t61
    %t63 = convert trunc i1 i64 %t62
    store i1 %t63, $13
    %t64 = load i1 $13
    %t65 = cmp eq i1 %t64, 0
    switch %t65, ^36, 1:^39
  block ^39:
    %t74 = load i32 $6
    %t75 = binary and i32 %t74, 127
    %t76 = convert trunc i8 i32 %t75
    %t77 = binary or i8 %t76, 0
    %t78 = convert sitofp f32 i8 %t77
    %t79 = call i64 @p_h5(%__param0, %t78)
    jump ^40
  block ^6:
    call void @chibi_trap(2)
    unreachable
  block ^40:
    unreachable
  block ^36:
    unreachable
}
```

```
#0 opt::sink_values(...)
#1 opt::(anonymous namespace)::run_pass(...)
#2 opt::(anonymous namespace)::finish_function(...)
#3 opt::optimize_program(...)
```

With 11 in place of -11, it compiles.

</details>
