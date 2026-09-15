# 手绘位图字体

美术直接提供 PNG 字模和 metrics，不要求 TTF/OTF，也不经 FreeType 烤字。能力分为四层：

| 层 | 职责 |
| --- | --- |
| `Tina::Text` | owning immutable `BitmapFont`、CPU `BitmapFontAtlas`、UTF-8 scalar layout/kerning/fallback |
| `Tina::AssetFormat` / Cooker | Font v1、required Texture2D 页、源 JSON/PNG → cooked catalog |
| `Tina::Asset::FontBindingRegistry` | Font 资产 intern：一份 metrics/`BitmapFontAtlas` 供 UI 与世界文字共用 |
| `Tina::Scene::BitmapText2D` | 一个 layout owner，按 glyph 提交普通 Sprite2D quad，不创建逐字 Entity |
| `Tina::UI::UIBitmapTextRasterizer` | 复用 IUITextRasterizer、现有布局/paint/GlyphAtlas，不建立第二套 UI |

Text/Serialization 都只依赖 Core，内部 OBJECT 分组并入唯一 GameSDK。公开头无第三方类型，
关闭 FreeType 时 bitmap 路径仍可使用。

## Authoring 与 Cooker

recipe 增加一行（ID 使用项目自己的唯一值）：

```text
bitmapfont 00000000000000000000000000000001 pixel_font.json
```

`pixel_font.json` 是 UTF-8 JSON schema 1；下面示例至少要求6×8的 PNG，`?` 为 fallback：

```json
{
  "schema": 1,
  "nominalSize": 8,
  "lineHeight": 9,
  "baseline": 7,
  "fallback": 63,
  "pages": [{
    "textureId": "00000000000000000000000000000002",
    "image": "pixel_font.png",
    "kind": "coverage"
  }],
  "glyphs": [
    {"codepoint": 32, "page": 0, "rect": [0, 0, 0, 0], "advance": 3, "bearing": [0, 0]},
    {"codepoint": 63, "page": 0, "rect": [0, 0, 5, 7], "advance": 6, "bearing": [0, 7]}
  ],
  "kerning": []
}
```

- `codepoint` 是 Unicode scalar 数值，不是 UTF-8 byte、glyph 顺序或字符在字符串中的下标。
- `rect` 为左上原点的 `x,y,width,height`；advance 是 pen 前进量；bearingY 从 baseline 向上为正。
  宽高同时为0表达空格，仍保留 advance。kerning 项是 `{"left":65,"right":86,"adjustment":-1}`。
- PNG 路径相对 JSON 目录，不接受绝对路径、NUL 或 `..`；SourceImport 还校验 project sourceRoot containment，
  捕获 JSON 与每页 PNG 的实际读取字节用于 reimport。recipe importer contract 已提升为4。
- `coverage` 忽略 PNG RGB，只取 alpha；`color` 保留 straight sRGBA 并接受文字 tint。
- Font+所有页一次生成；Texture2D 必须 sRGBA8、Point min/mag、Clamp U/V、single-level/no mip。
  不能用普通图片默认生成 mip 的结果冒充字模页，避免跨 glyph 混色。
- 源页可按任意顺序填写；Cooker 按 Texture AssetId 升序重排页并重映射 glyph.page。低层
  `writeCookedBitmapFontAsset()` 则要求调用者传入已按 AssetId 严格升序排列且与 metrics 对应的页。
- font 最多16页、65,536 glyphs、262,144 kerning pairs，单边最多16,384；CPU页总量最多64 MiB。
  非法/重复 scalar、fallback 缺失、越界 rect、非有限 metrics、倒退 caret interval 均拒绝。

Font wire 是 little-endian：32-byte header（schema/pageCount/glyphCount/kerningCount 四个 u32，
nominalSize/lineHeight/baseline 三个 float，fallback u32）、12-byte page、36-byte glyph、12-byte kerning。
header schema 与 Cooked assetTypeVersion 都必须是1，依赖数量/种类/Required 标志必须与页完全一致。
stage/full validation 验证实际纹理，旧或损坏的数据不会发布。

## Runtime 加载与寿命

```text
Cooked Catalog -> AssetSystem / CookedAssetFile
  -> parseBitmapFontFromCooked -> owning BitmapFont + Texture AssetIds
      -> Sprite2DBindingRegistry / packet-local resolver -> Scene BitmapText2D
  -> loadBitmapFontAtlasFromCooked(fontFile, pageFiles) -> owning CPU atlas
      -> UI bitmap rasterizer -> existing GlyphAtlas -> DisplayList -> bgfx
```

Runtime 不读 PNG/JSON source。`parseBitmapFontFromCooked` 拷贝 metrics/IDs；`loadBitmapFontAtlasFromCooked`
按 ID 找对应 cooked page、校验并复制像素，所以它们不会借用已释放的 CookedAssetFile。
Scene 持有 `shared_ptr<const BitmapFont>`，UI 持有 `shared_ptr<const BitmapFontAtlas>`。
GPU页仍由现有 Asset Lease/binding/retirement owner 管理；Text owner 不自行管理 GPU 或伪造 FrameResourceRef。

## 世界文字

```cpp
auto loaded = Tina::Asset::parseBitmapFontFromCooked(fontFile);
if (!loaded) return Tina::Core::failure(loaded.error());
auto font = std::make_shared<const Tina::Text::BitmapFont>(std::move(loaded->font));
auto text = Tina::Scene::BitmapText2D::Create(font, loaded->textureIds);
if (!text) return Tina::Core::failure(text.error());
if (auto status = text->setText("获得金币 +10", {.scale = 2}); !status) return status;
// 在既有 Render extraction phase：
return text->extract(writer, frameResources, textureResolver,
    {.x = 1, .y = 2, .pixelsPerMeter = 32, .firstStableKey = 1000});
```

游戏先上传/注册这些 Texture2D pages，resolver 必须返回同页 AssetId 的 packet-local resource 和真实尺寸。
`setText` 只在候选完整 layout 成功后替换旧文本。每帧只 emit quad，不重新 shape、不一字一实体。
local layout 向右/向下，world anchor 按 camera 投影一次；glyph 作为 billboard 绕 anchor 旋转，
所有字共享 anchor 的空间 sortDepth，避免等距世界里一行字自行前后穿插。
stable key 使用 `firstStableKey + glyphIndexInLayout`，游戏需预留与其它 Sprite 不冲突的区间。

`layoutBitmapText` 返回 owning layout；`layoutBitmapTextInto` 写 caller scratch（至少容纳输入 scalar 数），
失败可能覆盖 scratch，但不返回半份 view。strict UTF-8 无 NUL，支持 LF、fallback、kerning；
wrapWidth>0 是按 scalar 硬换行，0只按LF分行，不冒充复杂字形 shaping。空串尺寸为0。
scale/位置/geometry 溢出均失败。

## UI 接入

Desktop 普通游戏在创建引擎前载入 cooked CPU atlas：

```cpp
Tina::Desktop::CreateEngineOptions options;
options.uiBitmapFont = std::make_shared<const Tina::Text::BitmapFontAtlas>(std::move(atlas));
auto host = Tina::Desktop::CreateEngine(config, std::move(options));
```

它与 uiFontBytes/uiFontAtlasBytes/uiFallbackFontBytes 互斥。高级组合可调用
`createBitmapTextRasterizer(atlasOwner, capacity, resource)`，注入既有 `UIContext::Create`，再
`context.text().openTextFont({})`；只有空 bytes、faceIndex=0 合法。字体自身的 fallback glyph 负责缺字，
非空 outline fallback chain 和 MSDF cache seed 明确 Unsupported。

shape/measure 只生成逻辑 metrics，不生成图像，按 maxTextBytes 限制输入；排布存储按实际文本长度增长，
不受 maxGlyphsPerRaster/image 预算限制。UI 高度为完整的 `logicalSize * lineHeightScale` 行框，
字体自然 ascent/descent 居中放入行框；与世界文字的自然末行高度不同。
raster 使用预分配像素 scratch 拷贝原字模，重复 glyph 共用像素区。
atlas key 的 rasterSize 固定为源 nominal size，不因 DPI/文字动画创建新图；字号只改变逻辑 geometry。
`BitmapCoverage`/`BitmapColor` 强制 nearest，彩色字接受 tint，Emoji 的原语义不变。
为了像素整齐，游戏应采用合适整数倍字号/相机 PPM 和 run-origin pixel snapping，而不是依赖重采样。

当前是 LTR scalar 字体适配：Auto 当作 LTR，显式 RTL 返回 Unsupported。阿拉伯/复杂 shaping/BiDi
继续使用现有 FreeType/HarfBuzz adapter。UI 的 TextEdit/换行/selection 仍使用原来的 scalar/grapheme 接口。
UI scratch 的 maxGlyphsPerRaster、coverageByteCapacity、glyphImageCapacity、maxTextBytes/maxFontBytes
分别受配置预算限制；maxFontBytes 计入源像素及 metrics 记录，与 Text 的 source/atlas 限制分开校验。

## 验证边界

回归面包括 UTF-8/CJK/fallback/kerning/LF/wrap、越界及 budgets、Font+Texture cooked 往返/重排、
Scene geometry/pins/stable keys、UI shape 不烤图/DPI 不重采样/nearest batch，以及与 FreeType 共存的构建。
编译、CPU 测试、像素观测三种证据分开；测试或 sample 仅在明确授权后运行。
