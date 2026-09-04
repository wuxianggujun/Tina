#pragma once

// Input actions the gallery binds, shared between the two hosts.
//
// Declared here rather than in either host because both must bind the same ids: the host owns the
// binding (it builds EngineConfig) while the scenes own the reaction, so a mismatch would leave a scene
// reading an action nothing ever produces -- and that reads as "the scene is broken" with nothing in the
// log to contradict it.

#include <tina/runtime/InputActions.hpp>
#include <tina/runtime/EngineConfig.hpp>

namespace Tina::Gallery {

// Returns to the menu from a scene. Bound to Escape, which Android's BACK key maps to, so the platform's
// own back gesture is the same gesture -- rather than the gallery inventing a second way out.
inline constexpr InputActionId GalleryBackAction{1};

// Moves the menu selection, and activates it. Bound to arrows and Enter so a keyboard or a TV remote can
// drive the gallery without a pointer; touch and mouse go through UI hit-testing instead.
inline constexpr InputActionId GalleryUpAction{2};
inline constexpr InputActionId GalleryDownAction{3};
inline constexpr InputActionId GalleryActivateAction{4};

// Appends the gallery's bindings to a config.
//
// A function rather than a table constant because InputActionBinding carries a variant, so the list
// cannot be a constexpr array without pinning its layout -- and each host has other bindings of its own
// to add around these.
void appendGalleryBindings(EngineConfig& config) noexcept;

} // namespace Tina::Gallery
