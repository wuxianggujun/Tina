#include <tina/render/RenderPassScheduler.hpp>

static_assert(Tina::Render::RenderPassSchedule::MaximumPassCount == 15U);
static_assert(Tina::Render::RenderPassKind::SpotLightShadowDepth !=
              Tina::Render::RenderPassKind::Opaque3D);
static_assert(Tina::Render::RenderPassResource::SpotLightShadowMap !=
              Tina::Render::RenderPassResource::PrimarySurface);
static_assert(Tina::Render::RenderPassKind::PointLightShadowDepth !=
              Tina::Render::RenderPassKind::Opaque3D);
static_assert(Tina::Render::RenderPassResource::PointLightShadowMap !=
              Tina::Render::RenderPassResource::PrimarySurface);
