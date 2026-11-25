# Spine C++ Runtime

`include/`, `src/` and `LICENSE` are taken from Spine Runtimes 4.3 at
[`f1dffa7f27dadeda93c613e4835d60ff81469e48`](https://github.com/EsotericSoftware/spine-runtimes/tree/f1dffa7f27dadeda93c613e4835d60ff81469e48/spine-cpp).

The Edgi-Talk Spine project includes this component through `SConscript` and
provides the `spine::SpineExtension` implementation. The build defines
`SPINE_NO_FILE_IO` for this component. The demo loads its rig and atlas from
firmware resources.

See [LICENSE](LICENSE) for the Spine Runtimes license and distribution terms.
