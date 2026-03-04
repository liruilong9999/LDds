# Phase Definition of Done 验收清单

本清单用于执行 `step.md` 第 13 节的总门槛，确保每个 Phase 结束时具备可验证证据。

## 1. 总门槛（必须全部满足）

1. 新能力测试全部通过。
2. 历史回归测试（至少 stage3/4/7/56/8）通过。
3. 无明显性能回退（吞吐和延迟有对比数据）。
4. 文档更新完成（配置项、行为变化、兼容性说明）。
5. 失败场景可回滚（开关、配置或分支策略已定义）。

## 2. 建议验收流程（执行顺序）

1. 构建：`cmake --build build --config Debug`
2. 回归：`powershell -ExecutionPolicy Bypass -File .\scripts\run_all_stage_tests.ps1`
3. 本阶段专项：运行本 Phase 对应 smoke/stress 脚本（例如 stage1/stage2/stage10/stage11/stage12/stage13）。
4. 性能对比：执行本阶段性能脚本或基线脚本，输出对比结果（吞吐、延迟、恢复时间等）。
5. 文档检查：确认 README、`step.md`、Phase 规则文档和迁移说明已更新。
6. 回滚检查：确认存在开关、配置或分支级回滚方案，并经过最小验证。

## 3. 证据归档模板（建议随提交附带）

```markdown
## Phase X 验收记录
1. 新能力测试：
   - 命令：`...`
   - 结果：`PASS/FAIL`
   - 关键输出：`...`
2. 历史回归：
   - 命令：`powershell -ExecutionPolicy Bypass -File .\scripts\run_all_stage_tests.ps1`
   - 结果：`PASS/FAIL`
   - 关键输出：`[stage-run] PASS all stages`
3. 性能对比：
   - 基线版本：`commit-id`
   - 当前版本：`commit-id`
   - 指标变化：`吞吐 +x% / 延迟 +y% / 恢复时间 z ms`
4. 文档更新：
   - 文件：`README.md` `step.md` `docs/...`
5. 回滚策略：
   - 方式：`配置开关 / 编译开关 / 回退分支`
   - 验证结论：`可回滚 / 风险说明`
```

## 4. 判定规则

1. 任一项为 `FAIL` 时，Phase 不通过，不进入下一阶段。
2. 若性能回退超出预设阈值，需补充原因分析与优化计划后再推进。
3. 若存在 breaking change，必须提供迁移说明和兼容窗口策略。
