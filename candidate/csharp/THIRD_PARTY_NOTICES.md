# NumPy float32 trigonometric behavior

`Core/NativeFloatTrig.cs` adapts the scalar operation sequence and coefficients
from NumPy 1.26.4 `numpy/core/src/umath/loops_trigonometric.dispatch.c.src`:
https://github.com/numpy/numpy/blob/v1.26.4/numpy/core/src/umath/loops_trigonometric.dispatch.c.src

NumPy is distributed under the BSD 3-Clause license. Its license is retained in
`third_party/numpy/LICENSE.txt`. No NumPy binary or Python runtime is required by
the C# importer. The scalar transcription preserves the frozen Python evaluator's
float32 results; it does not add a SIMD or performance path.
