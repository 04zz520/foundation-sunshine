# 个人 DPI 修复分支

上游：[AlkaidLab/foundation-sunshine](https://github.com/AlkaidLab/foundation-sunshine)。基于 `v2026.823.92127.杂鱼`，commit `adecdec1664f8ce43be3c90679484f6ae307bb8f`。原许可证及作者署名保持不变。

当前个人功能修复包括：`src/platform/windows/display_device/windows_utils.cpp` 中 Windows `DISPLAYCONFIG_DEVICE_INFO_GET_DPI_SCALE` / `SET_DPI_SCALE` 的相对索引映射，以及连接准备阶段的安全编码器验证缓存、Vulkan HDR 验证复用和集成式桌面配置生命周期。

原表混入 120/140/160/180，导致本机 Windows 实际 150%（索引 2）被报告为 125%。改为标准 12 档：100、125、150、175、200、225、250、300、350、400、450、500。

`58a5f8574f170e8626f7633de19a16edd77d45be` 已通过 GitHub Actions 完整构建、测试并部署到个人电脑。后续提交仍须分别经过构建、部署和实机连接测试，不能仅凭源码提交视为稳定发行版。

个人改动放 `custom` 分支；`upstream` 追踪原作者，`origin` 指向个人 Fork。先在测试分支合并上游更新，再构建测试；如果上游已修复同一问题，应检查并移除重复补丁。

## 集成式连接准备优化

- `stream_profile_settings_path` 为空时完全禁用，保持上游行为；个人电脑启用后读取现有 `sunshine-display-settings.json`。
- Sunshine 在 VDD 配置完成后直接调用 Windows 原生 DPI API、维护原有生命周期状态，不再在连接关键路径启动 PowerShell。
- 桌面图标访问必须处于交互式用户会话，因此 Windows 包携带 `tools/SunshineStreamLayout.exe`，由 Sunshine 以当前用户令牌无窗口同步执行；失败只记录警告，不阻止串流。
- Windows DXGI 因分辨率、拓扑或 HDR 切换而使 factory 失效时，会比较完整 GPU 适配器签名；GPU 集合未变化则继续复用编码能力，GPU 重置、eGPU/显卡变化或无法可靠读取签名时仍执行完整验证。
- Sunshine 启动时完成的通用编码器探测可覆盖首次 VDD-compatible 串流；相同 VDD 目标的后续连接直接复用结果，真正的流编码器仍在会话阶段按 Moonlight 选择初始化并保持失败关闭。
- Vulkan HDR bridge 的成功呈现验证在 Sunshine 服务生命周期内按活动 Zako 显示器复用；退出串流仍注销临时 Vulkan layer，下一次连接只重新注册，不再重复呈现探测。
- DPI 目标优先使用 Sunshine 已解析的活动输出和 Windows 枚举得到的 Zako 监视器 ID，不再把 `Root\\ZakoVDD` 适配器 PnP ID 错当成显示器 ID；短暂枚举延迟会进行有界重试。
- 为获得稳定缓存命中，个人运行配置需同时启用 `vdd_keep_enabled` 和 `vdd_reuse`。Sunshine 服务重启、GPU 变化或安全判定失败后的首次连接仍可能完整验证。

构建依赖及 Windows 完整构建方法见原 `docs/building.md`。不要照搬上游的签名、自动发版凭证。公开仓库不保存 Sunshine 运行配置、配对状态、证书、日志或电脑端个人布局。

个人工作流 `Build custom Sunshine (unsigned)` 在 `custom` 分支推送后自动运行，也可手动触发；它不自动发布 Release，不使用上游签名凭证。缺失的私有驱动依赖允许跳过，因此产物不等于上游完整安装包，不能据此替换现有驱动。每次使用新产物前仍需检查构建日志、测试结果和校验值。
