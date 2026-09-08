# 个人 DPI 修复分支

上游：[AlkaidLab/foundation-sunshine](https://github.com/AlkaidLab/foundation-sunshine)。基于 `v2026.823.92127.杂鱼`，commit `adecdec1664f8ce43be3c90679484f6ae307bb8f`。原许可证及作者署名保持不变。

当前唯一功能修复：`src/platform/windows/display_device/windows_utils.cpp` 中 Windows `DISPLAYCONFIG_DEVICE_INFO_GET_DPI_SCALE` / `SET_DPI_SCALE` 的相对索引映射。

原表混入 120/140/160/180，导致本机 Windows 实际 150%（索引 2）被报告为 125%。改为标准 12 档：100、125、150、175、200、225、250、300、350、400、450、500。

该源码修复尚未编译部署到现有电脑，不能把它标为已验证的 Sunshine 可执行发行版。当前稳定使用的是原版 Sunshine 加 Moonlight 的旧表兼容层。

个人改动放 `custom` 分支；`upstream` 追踪原作者，`origin` 指向个人 Fork。先在测试分支合并上游更新，再构建测试；如果上游已修复同一问题，应检查并移除重复补丁。

构建依赖及 Windows 完整构建方法见原 `docs/building.md`。不要照搬上游的签名、自动发版凭证。公开仓库不保存 Sunshine 运行配置、配对状态、证书、日志或电脑端个人布局。
