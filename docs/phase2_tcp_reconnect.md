# Phase 2 TCP 连接池与重连机制

## 核心能力落地

1. 连接状态机：`Disconnected/Connecting/Connected/Backoff`
2. endpoint 连接池：按 `ip:port` 维护独立状态
3. 幂等建连：同 endpoint 重复 `connectToHost/sendMessage` 不重复建连
4. 指数退避重连：`reconnectMinMs/reconnectMaxMs/reconnectMultiplier`
5. 发送队列策略：`DropOldest/DropNewest/FailFast`
6. 队列限长：`maxPendingMessages`
7. 健康检查：写失败与 socket 状态变化触发掉线与重连

## 新增 TransportConfig 参数

- `autoReconnect`
- `reconnectMinMs`
- `reconnectMaxMs`
- `reconnectMultiplier`
- `maxPendingMessages`
- `sendQueueOverflowPolicy`

## 运行验证

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run_stage2_tcp_reconnect_smoke.ps1
```

通过标准：输出 `[stage2_tcp] result=ok`
