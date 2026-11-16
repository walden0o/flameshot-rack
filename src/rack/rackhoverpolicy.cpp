// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "rackhoverpolicy.h"

#include <algorithm>

RackDisplayMode rackDisplayModeFromName(QStringView name)
{
    if (name == u"always") {
        return RackDisplayMode::Always;
    }
    return RackDisplayMode::Hover;
}

RackPosition rackPositionFromName(QStringView name)
{
    if (name == u"left") {
        return RackPosition::Left;
    }
    if (name == u"center") {
        return RackPosition::Center;
    }
    return RackPosition::Right;
}

QRect rackTargetGeometry(const QRect& screen,
                         const QRect& available,
                         bool expanded,
                         RackPosition position)
{
    // Keep the rack below the top panel so the collapsed handle stays visible.
    const int top = available.top();
    const int width = std::min(660, screen.width() - 24);
    int expandedX;
    int collapsedX;
    switch (position) {
        case RackPosition::Left:
            expandedX = screen.left() + 12;
            collapsedX = screen.left() + 44;
            break;
        case RackPosition::Center:
            expandedX = screen.center().x() - width / 2;
            collapsedX = screen.center().x() - 56;
            break;
        case RackPosition::Right:
        default:
            expandedX = screen.right() - width - 12;
            collapsedX = screen.right() - 156;
            break;
    }
    return expanded ? QRect(expandedX, top, width, 260)
                    : QRect(collapsedX, top, 112, 8);
}

bool rackInHotZone(const QRect& screen,
                   const QRect& available,
                   const QPoint& cursor,
                   RackPosition position)
{
    // Hover anywhere above the collapsed handle's lane triggers expansion.
    const QRect handle = rackTargetGeometry(screen, available, false, position);
    return cursor.y() >= screen.top() && cursor.y() <= available.top() + 8 &&
           cursor.x() >= handle.left() - 40 &&
           cursor.x() <= handle.right() + 40;
}

bool rackInHoverArea(const QRect& window,
                     const QRect& screen,
                     const QPoint& cursor)
{
    QRect area = window;
    area.setTop(std::min(window.top(), screen.top()));
    return area.contains(cursor);
}

bool rackCollapseBlocked(const RackInteractionState& state,
                         RackCollapseTrigger trigger)
{
    if (state.contentInteraction || state.popupOpen || state.modalOpen) {
        return true;
    }
    return trigger == RackCollapseTrigger::Automatic && state.searchFocused;
}

RackPointerAction rackPointerAction(const RackPointerState& state)
{
    if (state.mode != RackDisplayMode::Hover) {
        return RackPointerAction::None;
    }
    if (!state.expanded) {
        return state.inHotZone ? RackPointerAction::Expand
                               : RackPointerAction::None;
    }
    if (state.inHoverArea || state.interacting) {
        return state.collapsePending ? RackPointerAction::CancelCollapse
                                     : RackPointerAction::None;
    }
    return state.collapsePending ? RackPointerAction::None
                                 : RackPointerAction::ScheduleCollapse;
}

bool rackCollapseAfterDelay(bool inHoverArea, bool interacting)
{
    return !inHoverArea && !interacting;
}
