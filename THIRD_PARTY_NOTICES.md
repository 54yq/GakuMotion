# Third-party notices for this source staging

GakuMotion's selected first-party software is covered by the root MIT `LICENSE`.
This staging omits the data-bearing inputs listed in `SOURCE_REVIEW.md`; it does
not grant a license to omitted game assets, models, captures or derived datasets.

## Dear ImGui and embedded stb components

The declared upstream is https://github.com/ocornut/imgui at
`f3f6295d532b98e322691afb8ef418209d963786` (v1.89.4). The included local core
snapshot is fixed by individual SHA256 records. No upstream pristine-byte claim
is made because an independent source object was unavailable locally.

Retain `deps/imgui/LICENSE.txt` (MIT, Omar Cornut) and all original copyright and
license sections in the bundled `imstb_*.h` headers and embedded font source.
The upstream license at the declared commit was reviewed by the Loader staging
reviewer; that confirms the license text, not byte identity of every source file.

## MinHook, HDE32 and HDE64

Retain `include/minhook/LICENSE.txt` and each original source copyright header.
The complete license contains the MinHook and separate HDE32/HDE64 BSD-2-Clause
notices (Tsuda Kageyu and Vyacheslav Patkov). The license text was obtained by the
Loader staging reviewer from:

https://raw.githubusercontent.com/TsudaKageyu/minhook/8af6b4acae5a9388fd742b56fa79ece89d96f823/LICENSE.txt

This retained UTF-8/LF text is normalized; its hash is not presented as the raw
upstream blob hash. The selected local implementation is independently hashed.
The old research notice's “3-Clause” label was inaccurate for these texts.

## KineTexture

Retain `deps/KineTexture/src/KineTexture/LICENSE` and `NOTICE` (Apache-2.0).
The manifest records the original core revision and the separate corrected
NOTICE revision. No game-specific KineTexture export presets or model resources
are bundled in this core-only dependency.

## NumPy scalar float32 trigonometry adaptation

`candidate/csharp/src/GakuMotion.VmdImporter/Core/NativeFloatTrig.cs` transcribes
the scalar operation sequence and coefficients from NumPy 1.26.4. It is a
BSD-3-Clause third-party adaptation, not wholly first-party MIT code.

Retain `candidate/csharp/THIRD_PARTY_NOTICES.md` and
`candidate/csharp/third_party/numpy/LICENSE.txt` with relevant source and any
future derived binaries. No NumPy binary or Python runtime is included here.
