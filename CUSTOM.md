# 个人 DPI 修复分支

上游：[AlkaidLab/foundation-sunshine](https://github.com/AlkaidLab/foundation-sunshine)。基于 `v2026.823.92127.杂鱼`，commit `adecdec1664f8ce43be3c90679484f6ae307bb8f`。原许可证及作者署名保持不变。

当前个人功能修复包括：`src/platform/windows/display_device/windows_utils.cpp` 中 Windows `DISPLAYCONFIG_DEVICE_INFO_GET_DPI_SCALE` / `SET_DPI_SCALE` 的相对索引映射，以及连接准备阶段的安全编码器验证缓存和集成式桌面配置生命周期。

原表混入 120/140/160/180，导致本机 Windows 实际 150%（索引 2）被报告为 125%。改为标准 12 档：100、125、150、175、200、225、250、300、350、400、450、500。

该源码修复尚未编译部署到现有电脑，不能把它标为已验证的 Sunshine 可执行发行版。当前稳定使用的是原版 Sunshine 加 Moonlight 的旧表兼容层。

个人改动放 `custom` 分支；`upstream` 追踪原作者，`origin` 指向个人 Fork。先在测试分支合并上游更新，再构建测试；如果上游已修复同一问题，应检查并移除重复补丁。

## 集成式连接准备优化

- `stream_profile_settings_path` 为空时完全禁用，保持上游行为；个人电脑启用后读取现有 `sunshine-display-settings.json`。
- Sunshine 在 VDD 配置完成后直接调用 Windows 原生 DPI API、维护原有生命周期状态，不再在连接关键路径启动 PowerShell。
- 桌面图标访问必须处于交互式用户会话，因此 Windows 包携带 `tools/SunshineStreamLayout.exe`，由 Sunshine 以当前用户令牌无窗口同步执行；失败只记录警告，不阻止串流。
- 相同捕获目标的编码器结果只有在 DXGI 确认 GPU/输出集合未变化时才复用。VDD 重建、驱动/显卡变化、捕获目标变化或编码器要求重探测时仍执行完整验证。
- 为获得稳定缓存命中，个人运行配置需同时启用 `vdd_keep_enabled` 和 `vdd_reuse`；首次连接以及失效后的首次连接仍会完整验证。

构建依赖及 Windows 完整构建方法见原 `docs/building.md`。不要照搬上游的签名、自动发版凭证。公开仓库不保存 Sunshine 运行配置、配对状态、证书、日志或电脑端个人布局。

个人工作流 `Build custom Sunshine (manual, unsigned)` 仅手动触发，不自动发布 Release，不使用上游签名凭证。缺失的私有驱动依赖允许跳过，因此产物不等于上游完整安装包，不能据此替换现有驱动；本次迁移未执行完整 Sunshine 云端构建。首次使用前仍需检查构建日志和全部测试结果。
