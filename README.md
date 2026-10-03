# Modular Exponentiation Side-Channel Study

An educational C project exploring how implementation choices in modular exponentiation affect observable timing. It compares branch-based square-and-multiply, a dummy-multiply mitigation, secret-indexed operand selection, and a mask-based conditional-move experiment. Each implementation is measured across four input scenarios in unoptimized and optimized builds.


## Project highlights

- Four modular-exponentiation implementations with different exponent-bit selection strategies.
- Debug (`-O0`) and Release (`-O3`) timing experiments, 1,000 samples per scenario and implementation.
- Archived raw timing measurements and a complete [results and graph gallery](RESULTS.md).
- Discussion of what the experiments can and cannot establish from a security perspective.

## Implementation comparison

| Implementation | Exponent-bit selection | What it demonstrates |
| --- | --- | --- |
| Branch-based square-and-multiply | Skips the multiply when the bit is zero | The number of operations and control flow vary with exponent bits. |
| Dummy-multiply variant | Branches between multiplying by the message or by 1 | Both branches multiply, but control flow still depends on the bit and operand costs may differ. |
| Secret-indexed table lookup | Uses `table[bit]` to select an operand | Removes the explicit selection branch but creates a secret-dependent memory access. |
| Mask-based conditional move | Selects operand limbs using a bit mask | Avoids the table index at the selection point; does not make the arithmetic library constant-time. |

Implementation source: [`src/main.c`](src/main.c). The bundled legacy MPI implementation is under [`third_party/polarssl/`](third_party/polarssl/).

## Build and run

Requirements: `make`, GCC (or compatible C compiler), and a Unix-like environment.

```sh
make all
build/sidechannel-debug build/debug-data
build/sidechannel-release build/release-data
```

Build just one profile with `make debug` or `make release`. Use `make clean` to remove generated binaries. The optional output directory must exist before running the benchmark. The benchmark uses `clock()` CPU ticks, and results depend on machine, compiler, and system load.

The graph gallery is in [`RESULTS.md`](RESULTS.md). Historical raw measurements are stored in [`results/debug/`](results/debug/) and [`results/release/`](results/release/); subsequent runs produce fresh, potentially different data.
