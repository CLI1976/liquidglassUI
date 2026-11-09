// LiquidGlass - signed distance field shapes.
// Copyright (C) 2026 LiquidGlass contributors. AGPL-3.0-or-later.
// RoundRect distance function adapted from DM74164/TaskbarPopup src/glass.cpp
// (Copyright (C) 2026 DM74164, AGPL-3.0).
#pragma once

#include <algorithm>
#include <cmath>

namespace lg {

enum ShapeKind {
    kShapeRoundRect = 0,
    kShapeCircle = 1,  // reserved for stage 2 of the roadmap
    kShapeRing = 2,    // reserved
    kShapeSector = 3,  // reserved
};

struct ShapeBounds {
    float x = 0, y = 0, w = 0, h = 0;
};

// A shape is described by its signed distance field: negative inside, positive outside.
// Coordinates are window pixels (pixel centers at .5). The glass material (refraction,
// rim light, shadow) is derived only from Distance() and Bounds(), so new shapes only
// need to implement these two functions.
class Shape {
public:
    virtual ~Shape() = default;
    // Signed distance to the edge; nx/ny receive the outward unit normal.
    virtual float Distance(float x, float y, float& nx, float& ny) const = 0;
    float Distance(float x, float y) const {
        float nx, ny;
        return Distance(x, y, nx, ny);
    }
    // Axis-aligned bounding box (used for the top gloss gradient).
    virtual ShapeBounds Bounds() const = 0;
};

class RoundRectShape final : public Shape {
public:
    RoundRectShape(float x, float y, float w, float h, float radius) {
        m_b = {x, y, std::max(0.0f, w), std::max(0.0f, h)};
        m_r = std::clamp(radius, 0.0f, std::min(m_b.w, m_b.h) / 2);
        m_cx = x + m_b.w / 2;
        m_cy = y + m_b.h / 2;
        m_hx = m_b.w / 2 - m_r;
        m_hy = m_b.h / 2 - m_r;
    }

    float Distance(float x, float y, float& nx, float& ny) const override {
        x -= m_cx;
        y -= m_cy;
        float sx = x < 0 ? -1.0f : 1.0f, sy = y < 0 ? -1.0f : 1.0f;
        float qx = std::fabs(x) - m_hx, qy = std::fabs(y) - m_hy;
        if (qx > 0 && qy > 0) {
            float len = std::sqrt(qx * qx + qy * qy);
            nx = sx * qx / len;
            ny = sy * qy / len;
            return len - m_r;
        }
        if (qx > qy) {
            nx = sx;
            ny = 0;
            return qx - m_r;
        }
        nx = 0;
        ny = sy;
        return qy - m_r;
    }

    ShapeBounds Bounds() const override { return m_b; }

private:
    ShapeBounds m_b;
    float m_r, m_cx, m_cy, m_hx, m_hy;
};

}  // namespace lg
