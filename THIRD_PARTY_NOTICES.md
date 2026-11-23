# Third-party notices

The source snapshot retains upstream license and attribution files:

- PC port: MIT, copyright 2026 aikatore and contributors; `port/LICENSE`.
- Original game decompilation: MIT, copyright 2024–2026 AshfordFamily;
  `port/decomp/LICENSE`.
- Khronos OpenXR headers/Android loader: Apache-2.0;
  `arm64/third_party/openxr/LICENSE`.
- Other PC dependencies: see `port/THIRD_PARTY_NOTICES.md` and the individual
  vendored headers. Its references to the former nested repository layout are
  historical; this repository stores the port and decompilation source directly.

Katana and CRI SDK-header dependencies are external Git submodules, not vendored
copies of the SDK repositories. They retain their respective third-party terms;
the ARM64 build uses headers, not the legacy SDK binary libraries. Metrowerks
and legacy PS2 compiler distributions are not included in this snapshot.

Retail game data must be supplied separately by the user. Resident Evil and
Code: Veronica are trademarks of Capcom. This project is not affiliated with
or endorsed by Capcom, Sony, Sega or CRI Middleware.
