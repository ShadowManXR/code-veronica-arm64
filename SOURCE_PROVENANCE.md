# Source provenance

This repository snapshots the current modified source tree on 23 November 2026.
It is not an unmodified upstream release or a complete campaign port.

| Component | Upstream/base | Base commit before local ARM64 changes |
| --- | --- | --- |
| PC/PCVR port (`port/`) | https://github.com/neto1495mn-sudo/recvx-enhanced-port-pcvr | `15c0a7ebf15e5ee1967d988258490b5d7b8b7650` |
| Game decompilation (`port/decomp/`) | https://github.com/neto1495mn-sudo/recvx-decomp-VR (original project: https://github.com/AshfordFamily/recvx-decomp) | `5a2e1be91209329e6a9bf3c51e805c8dc711bca9` |
| Katana SDK headers (external submodule) | https://github.com/SkeletonPicture/recvx-decomp-katana | `9dc75fcb2f8d4369b3644d0432c7f209c2e0b1b6` |
| CRI headers (external submodule) | https://github.com/SkeletonPicture/recvx-decomp-cri | `ed21f6c8f1332dcf79a48f774e4ff57c8bb7a273` |
| OpenXR SDK headers/Android loader | Khronos OpenXR-SDK release `1.1.54` | See `arm64/third_party/openxr/README.md` and `LICENSE` |

The current port/decompilation modifications are included as ordinary source
files. SDK headers remain external, pinned dependencies. The local Katana
`NJS_TEXNAME.texaddr` native-width change is preserved as an apply-once patch;
run `Prepare-Dependencies.ps1` after cloning.

Upstream MIT notices remain in `port/LICENSE` and `port/decomp/LICENSE`. Keep
them with derived source. Other dependency licenses remain applicable. No retail
disc image or extracted asset package is included. Legacy PS2 compiler binaries
are omitted; the ARM64 target uses the Android NDK.
