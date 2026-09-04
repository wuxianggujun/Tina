# 任务：给 Tina 游戏项目引入按前端分目录的统一输出布局

## 背景

Tina 引擎（`C:/Users/wuxianggujun/CodeSpace/CMakeProjects/Tina`，分支 `codex/tina-vnext-runtime`）提供一套脚手架，让外部游戏项目以"已安装 SDK"方式消费引擎。`Grimwold`（同级目录）是第一个真实消费者。

引擎给**自己**设了统一输出目录，`CMakeLists.txt:36-45`：

```cmake
set(BIN_DIR ${CMAKE_BINARY_DIR}/bin CACHE PATH "Binary output directory path" FORCE)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${BIN_DIR})
foreach(cfg ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER "${cfg}" cfg_up)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg_up} ${BIN_DIR}/${cfg})
endforeach()
```

但**没有把这件事传递给游戏项目**。已核实以下四处均无任何 `OUTPUT_DIRECTORY` 设置：

- `templates/game-project/`（模板全部 CMakeLists）
- `cmake/TinaNewProject.cmake`（脚手架生成器）
- `cmake/TinaGameProject.cmake`（前端/内容 helper）
- `cmake/TinaProductInstall.cmake`（产品安装 helper）

## 现状（实测 Grimwold 构建树）

产物按源码目录结构散落，`CMakeCache.txt` 中 `CMAKE_RUNTIME_OUTPUT_DIRECTORY` 完全未设置：

```
build/
├── core/Debug/grimwold_content.lib
└── platforms/desktop/Debug/
    ├── grimwold.exe
    ├── grimwold.lib / .exp / .pdb
    ├── assets/game.recipe        ← tina_product_data_file 落点
    ├── content/                  ← 启动时 cook 产物
    └── glfw3.dll  box2dd.dll  freetyped.dll
```

## 为什么要改

1. **多前端时 exe 路径不可预测。** 桌面在 `platforms/desktop/Debug/`，web 会是 `platforms/web/Debug/`。没有稳定路径可写进脚本、CI 或调试器配置。
2. **每个前端各存一份 DLL 与资源。** `tina_product_data_file()` 与 DLL staging 都落在 `$<TARGET_FILE_DIR:target>`，两个前端就是两份 `glfw3.dll`、两份 recipe、两棵 cook 出来的 `content/`。
3. **与引擎自身约定不一致。** 引擎是 `out/build/<preset>/bin/<Config>/`，游戏项目是另一套，切换时要记两种路径。
4. **`.gitignore` 需要写通配才稳。** 排除 cook 产物时若写死 `platforms/desktop/Debug/content/`，目录结构一变就失效。

## 目标布局（已决定，不要改成别的方案）

**按前端分子目录**，而不是所有前端共用一个 `bin/<Config>/`：

```
build/bin/<Config>/<frontend>/
```

例如 `build/bin/Debug/desktop/grimwold.exe`、`build/bin/Debug/web/grimwold.html`。

### 为什么不用引擎那套「所有 exe 挤同一个 bin/<Config>/」

两个具体理由，都有实测证据：

1. **会撞名。** 同一个游戏的桌面和 web 前端 target 名可能产出同名文件；共用目录时后写的覆盖先写的。

2. **引擎自己已经因此丢过文件。** 2026-09-04 实测：在 `out/build/windows-msvc-vnext-sdk` 以 `--parallel 2` 构建后，`tina_assetc.exe` 从 `bin/Debug/` 消失，而同目录的 `tina_assetc.pdb` 保留。时间戳显示 `shaderc.exe`（43MB，bgfx 工具链）在 15 分钟后写入同一目录时挤掉了它。后果是 `cmake --install` 报出一条自相矛盾的错误：

   ```
   file INSTALL cannot find ".../bin/Debug/tina_assetc.exe": File exists.
   ```

   单独 `cmake --build ... --target tina_assetc` 重建后恢复，install 随即成功。**根因尚未定位**（不清楚是哪一方的清理步骤删的），但足以说明：把不同构建单元的产物堆在同一目录会制造冲突面。因此不要把引擎那套设计复制给游戏项目。

## 改哪里

改动必须落在 **`cmake/TinaGameProject.cmake` 的前端 helper 内部**，通过 `set_target_properties()` 设置 per-target 的输出目录属性。

**不要**改 `templates/game-project/` 里的 CMakeLists —— 放模板意味着每个用脚手架创建的项目各自持有一份副本，以后无法统一修改。这与该文件里已有的设计取向一致：DLL 复制、资源 staging 等都收在 helper 内，注释明确记载"sample 树曾把同一段手写了十二次"。

### 三个前端 helper（签名已核实）

| 函数 | 行号 | 产物 | 建议子目录 |
|---|---|---|---|
| `tina_add_desktop_frontend(target)` | `TinaGameProject.cmake:190` | exe | `desktop` |
| `tina_add_web_frontend(target)` | `TinaGameProject.cmake:244` | html | `web` |
| `tina_add_cli_frontend(target)` | `TinaGameProject.cmake:145` | exe | `cli`（headless 工具/样例用） |

内容库 `tina_add_game_content()`（`:38`）产出静态库，是链接中间产物而非可运行物。**建议不动它**——把 `.lib` 也搬进 `bin/` 只会让那个目录混入不该拷贝的东西。如果你判断该动，说明理由。

### 必须一并处理的连带点

这几处都读 `$<TARGET_FILE_DIR:target>` 或与之耦合，改了输出目录后必须验证仍然自洽：

1. **`tina_product_data_file()`**（`TinaProductInstall.cmake:37`）——post-build 拷到 `$<TARGET_FILE_DIR:${target}>/${relative_destination}`，同时发 install 规则到 `tina_product_install_dir()` 算出的目录。该文件顶部注释强调构建树与安装树布局**必须逐字一致**，因为 `Core::applicationFilePath()` 是运行期唯一的查找方式；两半分开写正是 UI 字体曾"构建树里有、每个 install 里都没有"的成因。改输出目录不应破坏这个一致性。

2. **DLL staging 三处**——`tina_install_product()`（`TinaProductInstall.cmake:78`）、`tina_add_desktop_frontend()` 非 INSTALL 分支（`:221`）、`tina_add_cli_frontend()` 的 `COPY_RUNTIME_DLLS`（`:168`）。注意 `:169` 那个写法用 genex 包住了整个命令来处理"DLL 列表为空"的情形（空列表会让 `cmake -E copy_if_different <dir>` 变成 usage error 从而失败整个 target），保留这个防护。

3. **`tina_cook_catalog()`**（`TinaGameProject.cmake:423`）——输出到 `$<TARGET_FILE_DIR:target>`。Grimwold 目前是启动时 cook 没用它，但它是 recipe 落点与 `contentRoot.resolve("content")` 的构建期替代路径。

4. **`tina_sample_runtime_cook_dir()`**（`:496`）——查清它算什么、是否受影响。

5. **`tina_verify_game_content_portable()`**（`:509`）——走完整链接图检查可移植性，确认不受输出目录变化影响。

6. **多配置生成器**——这台机器用 Visual Studio 18 2026（多配置）。设 per-target 输出目录时必须处理 `_<CONFIG>` 后缀变体，否则 VS 会在路径里自己插一层 `$(Configuration)`，得到 `bin/Debug/desktop/Debug/`。引擎 `CMakeLists.txt:38-45` 的 foreach 就是在处理这件事，可参考但注意那是目录级变量而非 target 属性。

## 约束

- **保持既有结构，只改必要部分。** 不要重构这些 helper 的参数解析或整体形状。
- **注释风格照抄该文件现有惯例**：解释"为什么"而非"做了什么"，尤其记录非显然的取舍与踩过的坑。该文件现有注释密度较高且都在讲原因，请匹配。
- 不要为"以后可能有别的前端"预留配置开关。子目录名由 helper 自己决定即可。
- 引擎侧那条 `bin/<Config>/` 竞态（见上文）**不在本次范围**，但不要让新代码复制出同类问题。

## 验证要求（必须真实执行，不能只推理）

改完后按顺序实跑，每步都要真实退出码：

1. **重装 SDK**（helper 文件随包安装，改了必须重装消费侧才看得到）：
   ```bash
   cd Tina
   cmake --build out/build/windows-msvc-vnext-sdk --config Debug \
     --target tina_sdk_install_artifacts --parallel 2 -- /nr:false
   cmake --install out/build/windows-msvc-vnext-sdk --config Debug \
     --prefix "D:/ProgramData/Tina" --component sdk
   ```
   注意：若遇到上文那条 `tina_assetc.exe` 竞态，单独重建该 target 即可继续。

2. **引擎自身构建不能回归**——`samples/` 大量使用这些 helper。至少让 SDK-only 图与一个带示例的图都通过 configure + build。

3. **消费侧验证**——用脚手架新建一次性项目（不要污染 `Grimwold`）：
   ```bash
   cmake -DNAME=OutDirProbe -DDEST=<临时目录> \
     -DDEPS="<Tina>/out/build/windows-msvc-vnext-sdk/vcpkg_installed/x64-windows" \
     -DCONFIG=Debug -P "D:/ProgramData/Tina/lib/cmake/Tina/TinaNewProject.cmake"
   cmake --preset default && cmake --build --preset default
   ```
   确认：
   - exe 落在 `build/bin/Debug/desktop/`，**没有**多余的 `Debug/Debug/` 嵌套
   - DLL（`glfw3.dll`、`box2dd.dll`、`freetyped.dll`）与 exe 同目录
   - `assets/game.recipe` 在 exe 旁的相对位置正确
   - **实际运行 exe**，确认启动 cook 产出 `content/manifest.tmnft` 与 `content/objects/`，且 stderr 为空
   - 跑完 kill 掉进程（模板无自动退出路径），并清理临时项目

4. **install 布局一致性**——`cmake --install` 一份产品到临时前缀，确认 exe、DLL、`assets/` 的相对布局与构建树一致（这是 `TinaProductInstall.cmake` 顶部注释要求的不变量）。

5. 清理本轮所有临时 build tree、install prefix、探针项目。

## 交付

- 说明改了哪些文件、每处为什么这么改
- 给出上述每步验证的真实结果（退出码 + 关键路径的实际 `ls` 输出）
- 如果发现连带点里有你判断**不该**改的，说明理由
- `Grimwold` 的 `.gitignore` 目前排除了 `build/`、`out/`、`content/`；若新布局让其中某条失效或多余，指出来（但不要直接改 Grimwold，它是独立仓库 `wuxianggujun/Grimwold`）
