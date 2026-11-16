// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

#include <QPoint>
#include <QRect>
#include <QStringView>

// Window-free decisions behind the rack's expand/collapse behavior.
inline constexpr int RackCollapseDelayMs = 200;
inline constexpr int RackAnimationMs = 170;
inline constexpr int RackPointerPollMs = 50;

enum class RackDisplayMode
{
    Hover,
    Always
};

enum class RackPosition
{
    Left,
    Center,
    Right
};

// Unknown names fall back to hover, matching the configuration default.
[[nodiscard]] RackDisplayMode rackDisplayModeFromName(QStringView name);
// Unknown names fall back to right, matching the configuration default.
[[nodiscard]] RackPosition rackPositionFromName(QStringView name);
// `screen` is the full screen geometry; `available` excludes the panel area.
[[nodiscard]] QRect rackTargetGeometry(const QRect& screen,
                                       const QRect& available,
                                       bool expanded,
                                       RackPosition position);
[[nodiscard]] bool rackInHotZone(const QRect& screen,
                                 const QRect& available,
                                 const QPoint& cursor,
                                 RackPosition position);
// The band above the window up to the screen edge counts as inside the rack.
[[nodiscard]] bool rackInHoverArea(const QRect& window,
                                   const QRect& screen,
                                   const QPoint& cursor);

struct RackInteractionState
{
    bool contentInteraction = false;
    bool popupOpen = false;
    bool modalOpen = false;
    bool searchFocused = false;
};

enum class RackCollapseTrigger
{
    Automatic,
    Explicit
};

// Explicit dismissal overrides search focus, but never an active drag, menu
// or dialog.
[[nodiscard]] bool rackCollapseBlocked(const RackInteractionState& state,
                                       RackCollapseTrigger trigger);

struct RackPointerState
{
    RackDisplayMode mode = RackDisplayMode::Hover;
    bool expanded = false;
    bool inHotZone = false;
    bool inHoverArea = false;
    bool interacting = false;
    bool collapsePending = false;
};

enum class RackPointerAction
{
    None,
    Expand,
    CancelCollapse,
    ScheduleCollapse
};

[[nodiscard]] RackPointerAction rackPointerAction(
  const RackPointerState& state);
// Re-checked when the collapse delay elapses, since the pointer may have
// returned.
[[nodiscard]] bool rackCollapseAfterDelay(bool inHoverArea, bool interacting);
