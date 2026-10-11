# 署名与第三方说明

**鼠卫兵 / ShuWeiBing**  
作者：**Phoenix Feng**  
Copyright (c) 2026 Phoenix Feng and contributors

## 项目代码

项目代码使用 MIT 许可证，完整授权与责任限制见随包提供的 LICENSE。分发源代码或软件的重要部分时，应保留该许可证及版权声明。项目开发使用 AI 辅助工具。

## 图标

`assets/mouse-guardian.png`、历史图标及其转换生成的 `src/app.ico` 使用 AI 图像工具生成、编辑；说明见 `assets/图标说明.md`。图标未从游戏资源提取，也不是官方标识。项目提供的图标按 MIT 条件供使用；不保证 AI 生成素材在各司法辖区均享有著作权、具备排他性或不存在与第三方作品相似的情况。

## 构建工具与系统组件

- Zig 0.15.2 是构建工具，项目未将编译器打入应用发布包。Zig 及其依赖的许可请查阅 [Zig 官方仓库](https://github.com/ziglang/zig)及下载包附带的许可文件。
- 应用使用 Windows Win32 和系统 Common Controls 等接口；发布包不分发 Windows 系统 DLL。Windows 属于 Microsoft 的产品。
- GitHub Actions 使用官方 checkout、upload-artifact、download-artifact 工具；它们不随应用分发。

## 名称与参考来源

阴阳师、网易、MuMu 等名称及相关商标、游戏软件和素材归各自权利人所有，仅用于说明兼容对象。本项目没有官方隶属、赞助、认证或背书关系。MIT 授权不授予第三方商标、游戏软件、账号、素材或服务的使用权。

多开入口分析参考 [yys-mumu-multi 的机制说明](https://github.com/Calpis-Water/yys-mumu-multi/blob/main/MECHANISM_ANALYSIS.md)，README 的章节组织也参考该项目；未复制其程序实现。鼠卫兵的功能、测试范围与限制以本仓库说明为准。
