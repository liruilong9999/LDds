# Src Unit Test Report

- Generated At: 2026-03-05 01:31:44 +08:00
- Config: Debug
- Command: scripts/run_src_unit_tests.ps1 -Config Debug
- Exit Code: 0

## 1. Summary

- total=9, passed=9, failed=0, duration_ms=423.822

## 2. Case Results

| Case | Status | Duration(ms) | Detail |
|---|---|---:|---|
| ByteBuffer.ReadWrite | PASS | 0.0193 | - |
| ByteBuffer.Bounds | PASS | 0.0518 | - |
| Message.RoundTrip | PASS | 0.0319 | - |
| Message.AckFactory | PASS | 0.0085 | - |
| Qos.LoadXml | PASS | 0.1517 | - |
| Qos.Compatibility | PASS | 0.0039 | - |
| TypeRegistry.Basic | PASS | 0.0703 | - |
| Domain.CacheBehavior | PASS | 0.0592 | - |
| LDds.InProcessPubSub | PASS | 423.264 | - |

## 3. Raw Output

```text
[ut][case] PASS ByteBuffer.ReadWrite duration_ms=0.0193
[ut][case] PASS ByteBuffer.Bounds duration_ms=0.0518
[ut][case] PASS Message.RoundTrip duration_ms=0.0319
[ut][case] PASS Message.AckFactory duration_ms=0.0085
[ut][case] PASS Qos.LoadXml duration_ms=0.1517
[ut][case] PASS Qos.Compatibility duration_ms=0.0039
[ut][case] PASS TypeRegistry.Basic duration_ms=0.0703
[ut][case] PASS Domain.CacheBehavior duration_ms=0.0592
[ut][case] PASS LDds.InProcessPubSub duration_ms=423.264
[ut][summary] total=9 passed=9 failed=0 duration_ms=423.822
```

