# 轻块桌面开发约定

在本仓库开发前，阅读 [项目 Skill](skills/light-block-desktop-dev/SKILL.md) 和相关领域的 [项目地图](skills/light-block-desktop-dev/references/project-map.md)。发布与贡献分别遵循 [发布手册](docs/release.md) 和 [贡献约定](CONTRIBUTING.md)。

- 基线为 Windows 10 / 11 x64、Python 3.11、PySide6。运行 `python main.py --data-dir <隔离目录>` 进行人工检查。
- 默认测试为 `python tools/run_tests.py`。Windows 交互会话可运行 `python tools/run_tests.py --desktop` 检查实际桌面 GIF。不要把运行时测试数据、个人截图、用户配置或构建输出加入 Git。
- `still/model.py` 中的 `VERSION` 是发布版本的唯一来源。普通功能和修复 PR 不提前递增版本。保留许可证和第三方声明。
