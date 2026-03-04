# Src Unit Test Report

- Generated At: 2026-03-05 01:42:19 +08:00
- Config: Debug
- Command: scripts/run_src_unit_tests.ps1 -Config Debug
- Exit Code: 0

## 1. Summary

- total=9, passed=9, failed=0, duration_ms=415.92

## 2. Case Results

| Case | Status | Duration(ms) | Detail |
|---|---|---:|---|
| ByteBuffer.ReadWrite | PASS | 0.0202 | - |
| ByteBuffer.Bounds | PASS | 0.0498 | - |
| Message.RoundTrip | PASS | 0.0354 | - |
| Message.AckFactory | PASS | 0.0086 | - |
| Qos.LoadXml | PASS | 0.173 | - |
| Qos.Compatibility | PASS | 0.0095 | - |
| TypeRegistry.Basic | PASS | 0.0787 | - |
| Domain.CacheBehavior | PASS | 0.0637 | - |
| LDds.InProcessPubSub | PASS | 415.321 | - |

## 3. Raw Output

```text
[ut][case] PASS ByteBuffer.ReadWrite duration_ms=0.0202
[ut][case] PASS ByteBuffer.Bounds duration_ms=0.0498
[ut][case] PASS Message.RoundTrip duration_ms=0.0354
[ut][case] PASS Message.AckFactory duration_ms=0.0086
[ut][case] PASS Qos.LoadXml duration_ms=0.173
[ut][case] PASS Qos.Compatibility duration_ms=0.0095
[ut][case] PASS TypeRegistry.Basic duration_ms=0.0787
[ut][case] PASS Domain.CacheBehavior duration_ms=0.0637
[ut][case] PASS LDds.InProcessPubSub duration_ms=415.321
[ut][summary] total=9 passed=9 failed=0 duration_ms=415.92
```

