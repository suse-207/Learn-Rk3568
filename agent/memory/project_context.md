# Project Context (Level 1 Memory)

- Project: `rk3568-automotive-platform`, a C++17 automotive communication and OTA learning project.
- Main backend: vsomeip3 provides SOME/IP transport and service discovery. The `com` module wraps the binding behind `BindRuntime`, `BindHandle`, and `BindSkeleton`.
- Build selection: root `CMakeLists.txt` uses `WITH_VSOMEIP`; `src/com/CMakeLists.txt` selects `COM_WITH_VSOMEIP`. The runtime factory currently creates the vsomeip binding; DDS and raw TCP are not implemented.
- SOME/IP integration: `src/com/src/vsomeip/` contains the binding; `apps/someip_server/`, `apps/someip_client/`, `apps/vehicle_status_service/`, `apps/vehicle_status_client/`, and `apps/ucm_service/` / `apps/ucm_client/` are application examples.
- VehicleStatus bindings are generated from `src/vehicle_status/vehicle_status.json` by `tools/generate_vehicle_status.py` during the CMake build. Do not edit generated files under a build directory.
- OTA code is in `src/ota/`; UCM transport adapters are in `src/ucm/`. DoIP and CAPI-derived UDS are separate diagnostic paths and are not vsomeip.
- Configuration examples are under `config/`; architecture, contract, and board bring-up notes are under `docs/`.
- Build with CMake and a working vsomeip3 installation: `cmake -S . -B build-agent-debug -DCMAKE_BUILD_TYPE=Debug`, then `cmake --build build-agent-debug --parallel 4`. The agent uses a dedicated `build-agent-*` directory to avoid overwriting existing developer build trees.
- Treat repository state as authoritative. Some features are demos or mocks; distinguish implemented code from planned documentation and do not infer production readiness.
- The agent defaults to read-only. File edits and builds are exposed only with `--allow-write` and require explicit approval.
