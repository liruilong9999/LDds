# Phase 7 Industrial Baseline Report

- Generated At: 2026-03-05 00:51:19 +08:00
- Host OS: Microsoft Windows NT 10.0.26200.0
- Config: Debug

## 1. Verification Matrix

| Item | Status | Notes |
|---|---|---|
| Windows Smoke/Stress | DONE | run_all_stage_tests PASS; stage8: callbacks=47968, deadlineMissed=8 |
| Phase7 IDL Round-trip | DONE | stage13: codegen=ok, validator=ok |
| Linux Smoke/Stress | TODO | Add Linux CI runner and mirror stage scripts |
| Network Emulation (loss/reorder/jitter) | TODO | Recommend `tc netem` profiles: loss 5%, reorder 20%, delay 50ms+20ms |
| OpenDDS Interop Pre-study | TODO | Separate spike project, map message header compatibility and discovery path |

## 2. Baseline Numbers

- Stage8 stress summary: callbacks=47968, deadlineMissed=8
- Phase6 security perf summary: plain_ms=92, secure_ms=93, delta_pct=1.08696
- Phase7 codegen summary: codegen=ok, validator=ok

## 3. Next Actions

1. Add Linux job to CI and run stage3/4/7/56/8 + stage13.
2. Add `tc netem` scripted profiles and collect latency/throughput/recovery curves.
3. Create OpenDDS interop spike repo with protocol adapter experiments.
