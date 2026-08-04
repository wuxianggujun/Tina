#include <tina/render/RenderPassScheduler.hpp>

static_assert(Tina::Render::RenderPassSchedule::MaximumPassCount == 9U);
static_assert(Tina::Render::RenderPassKind::SpotLightShadowDepth !=
              Tina::Render::RenderPassKind::Opaque3D);
static_assert(Tina::Render::RenderPassResource::SpotLightShadowMap !=
              Tina::Render::RenderPassResource::PrimarySurface);
