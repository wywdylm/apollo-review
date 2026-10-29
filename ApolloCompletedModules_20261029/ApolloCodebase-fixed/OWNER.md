# 本目录所有权（2026-10-29 17:45 起）

**唯一所有者：** 主导交接的会话（下称"接管方"）。

## 规则

1. 其他人/会话**只读**，不得修改 `src/`、`*.sh`、`pack.sh`、`CMakeLists.txt`、`assets/`
2. 如需试验：复制整个目录到 `~/robocup/experiments/<名字>/` 再做，**不要改本目录**
3. 冻结包只由接管方用 `pack.sh` 生成，并记录 sha256
4. 比赛现场只解压 `ApolloCodeBase_final*.tar.gz`，不重新编译

## 冻结包

见工作区 `dist/` 与 `docs/交付清单.md` 中的 sha256。

## 变更登记

| 时间 | 改动 | 影响 |
|---|---|---|
| 17:20 | `motion_diagnostics.h` 增加 `pm=` 日志字段 | 仅日志 |
| 17:36 | `agent_app.cc` 增加 `APOLLO_MOTION_LOG_EVERY` 日志抽稀 | 仅日志 |
| 17:36 | 回滚跨步踢球实验 | 源码与冻结包一致 |
