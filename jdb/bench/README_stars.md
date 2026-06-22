# update_stars - Vectorisation Benchmark

## Files
- `bench_stars.jdb` - single-N (140) detailed comparison of three impls
- `bench_stars_sweep.jdb` - crossover sweep N ∈ {140 … 14000}, v1 vs v3

## Result

```
                  Interpreter           Native
            v1     v3   speed       v1     v3   speed
N=140       582   1218  0.48x       34    330   0.11x
N=280       580   1203  0.48x       36    322   0.11x
N=700       579   1107  0.52x       38    377   0.10x
N=1400      577   1099  0.53x       42    368   0.11x
N=2800      577   1431  0.40x       41    355   0.12x
N=7000      604   1743  0.35x       37    325   0.11x
N=14000     607   1781  0.34x       37    323   0.12x
```

(All ms-totals; 30000 iters at N=140 down to 300 iters at N=14000.)

## Why vector loses here

* Pro Frame allokiert die Vektor-Variante **5-7 frische N-Element-Arrays**
  (mask, 1-mask, y*keep, new_x, x*keep, new_x*mask, sum). Allokations-Churn
  dominiert die per-Element-Arbeit.
* Die Wrap-Bedingung `y >= SCR_H` ist **dünn**: pro Frame wrappen typisch
  1-3 von 140 Sternen. Branch-Prediction trifft 95%+, der FOR-loop läuft
  fast immer in den No-Op-Pfad.
* Native compile inlinet den FOR-loop sehr aggressiv (~17× speedup gegen
  Interp). Vektor-Ops gehen weiter durch Arena-Alloc + Runtime-Dispatch
  und gewinnen nur ~4× - Ergebnis: v3 ist **immer** schlechter, nicht
  nur bei kleinem N.

## Conclusion

Die in `space_shooter.jdb` verwendete `update_stars`-Implementierung
(FOR + Branch) ist bereits **nahe Optimum**. Eine APL-Version würde
die Sache verlangsamen, nicht beschleunigen.

## Wann Vektorisierung in jdBasic gewinnt

Ergänzung zu `feedback_vectorize_loops.md`:

* **N groß UND dense Bedingung** (jedes Element braucht die Operation)
* **Reine Arithmetik** ohne Verzweigung (z. B. `star_y = star_y + speed`
  bleibt vektor - DAS Element wird intern auch in einer Schleife mit
  SIMD-Potenzial verarbeitet, aber ohne Allokations-Overhead pro Op)
* **Pairwise / Outer-Product** Patterns (Collision-Detection, Distanz-
  Matrizen) - da gewinnt der Vektor-Approach um Größenordnungen
* **Kein per-Element-FUNC-Call** (SELECT mit Lambda hat hohe Overhead;
  dann lieber FOR)

## Wann FOR gewinnt (oder gleich gut ist)

* **N klein** (≤ ein paar Tausend) UND **sparse Bedingung** (≤ 10 % der
  Elemente betroffen) - dann Branch-Prediction + In-Place-Schreiben
  schlagen die Allokations-Kosten der Vektor-Ops
* **Per-Element komplexe Logik** mit verschiedenen Code-Pfaden je
  Element-Typ (z. B. `e_kind`-dispatch in `update_enemies`)
* **Iteration mit early-exit** (FOR mit EXIT FOR gewinnt immer)

## Suspected real wins in space_shooter (untested)

* **collisions()** - pairwise N_BULLETS × N_ENEMIES = 64 × 24 = 1536
  Paare. Outer-product `dx² + dy² < r²` Vektor-Form sollte gut gewinnen.
* **update_fx** - wenn alle aktiven FX gleichbehandelt werden, vektor
  arithmetik OHNE intermediate masks könnte schneller sein
* **draw_*** - schon batched via GFX.PLOT_POINTS / matrix-LINE
