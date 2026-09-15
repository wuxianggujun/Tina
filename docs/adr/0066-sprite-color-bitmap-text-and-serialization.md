# ADR 0066: Sprite color transforms, bitmap text, and structured serialization

Status: Accepted

## Context

Real game consumers need signed/HDR per-sprite color transforms, hand-authored
bitmap fonts without a source outline font, and polymorphic gameplay save data.
The maintainer approved a current-only implementation, without compatibility APIs.

## Decision

- Core owns the value-only float RGBA `ColorTransform`: sampled * multiply + add.
  RGB is linear; alpha is clamped before a single premultiplication. Additive alpha
  intentionally changes coverage. The transform is vertex data, never a batch key.
  Scene, tile/particle/trail extraction, World2D v8 and Fx2D v2 share this contract.
  Sprite fragment varying ABI is breaking; Shader payload advances to v4.
- Bitmap fonts are typed cooked Font assets with required Texture2D pages. A
  non-UI Text module owns mapping/metrics/layout; Scene emits ordinary sprite
  quads and UI consumes the same font through its text SPI. No per-glyph entities,
  runtime source-image loading, implicit FreeType dependency, or second UI tree.
- Serialization is a Core-only codec/registry layer above JSON and below game
  DTOs. Stable explicit type IDs select registered factories; no RTTI persistence,
  global registry, raw pointer persistence or mandatory Bundlable inheritance.
  Graph references are stable IDs and resolve before game-state publication.
  SaveStore remains a byte/envelope/IO owner; product dataVersion and explicit
  migrations remain separate from storage schema.
- Public headers stay third-party-free. Runtime additions are internal OBJECT
  groups in the single GameSDK archive. New source and documents are UTF-8.

## Consequences

All affected producers/consumers and authored fixtures migrate together. Older
wire formats fail closed. Source completion, compilation, automated tests and
actual pixel/interaction evidence are reported separately.
