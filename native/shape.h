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
    kShapeCircle = 1,  // inscribed in the panel
    kShapeRing = 2,    // inscribed in the panel, p1 = inner radius
    kShapeSector = 3,  // annular sector with rounded corners, explicit center (LG_SetSector)
};

constexpr float kPi = 3.14159265358979f;

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
    // Point the zoom animation grows from.
    virtual void Origin(float& x, float& y) const {
        ShapeBounds b = Bounds();
        x = b.x + b.w / 2;
        y = b.y + b.h / 2;
    }
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

// Disc (inner <= 0) or ring around (cx, cy).
class RingShape final : public Shape {
public:
    RingShape(float cx, float cy, float outer, float inner)
        : m_cx(cx), m_cy(cy), m_out(std::max(0.0f, outer)), m_in(std::clamp(inner, 0.0f, m_out)) {}

    float Distance(float x, float y, float& nx, float& ny) const override {
        x -= m_cx;
        y -= m_cy;
        float rho = std::sqrt(x * x + y * y);
        if (rho > 1e-4f) {
            nx = x / rho;
            ny = y / rho;
        } else {
            nx = 0;
            ny = -1;
        }
        if (m_in <= 0) return rho - m_out;
        float mid = (m_out + m_in) / 2, half = (m_out - m_in) / 2;
        if (rho < mid) {  // nearer the hole: the outward normal points to the center
            nx = -nx;
            ny = -ny;
        }
        return std::fabs(rho - mid) - half;
    }

    ShapeBounds Bounds() const override { return {m_cx - m_out, m_cy - m_out, 2 * m_out, 2 * m_out}; }
    void Origin(float& x, float& y) const override {
        x = m_cx;
        y = m_cy;
    }

private:
    float m_cx, m_cy, m_out, m_in;
};

// Annular sector around (cx, cy): radii inner..outer, starting at `startDeg` (0 = 12 o'clock,
// clockwise) and spanning `sweepDeg`, with corners rounded by `corner`. inner = 0 gives a pie
// slice. The exact SDF is built as an opening: the sharp sector is eroded by `corner` (its
// boundary is then two arcs and two segments) and the distance is dilated back by `corner`.
class SectorShape final : public Shape {
public:
    SectorShape(float cx, float cy, float outer, float inner, float startDeg, float sweepDeg, float corner)
        : m_cx(cx), m_cy(cy) {
        outer = std::max(0.0f, outer);
        inner = std::clamp(inner, 0.0f, outer);
        float sweep = std::clamp(sweepDeg, 0.0f, 360.0f) * kPi / 180;
        m_full = sweep >= 2 * kPi - 1e-4f;
        m_a0 = (startDeg - 90) * kPi / 180;  // screen angle (y down): 0 = 3 o'clock, clockwise
        m_sweep = sweep;
        m_reflex = sweep > kPi;
        // Largest corner that leaves a non-empty eroded shape.
        float r = std::max(0.0f, corner);
        r = std::min(r, std::max(0.0f, (outer - inner) / 2 - 0.5f));
        if (!m_full && !m_reflex) {
            float s = std::sin(sweep / 2);
            r = std::min(r, std::max(0.0f, outer * s / (1 + s) - 0.5f));
        }
        if (m_full) r = 0;
        m_r = r;
        m_ro = outer - r;
        m_ri = inner + r;
        m_out = outer;
        m_in = inner;
        m_e0x = std::cos(m_a0), m_e0y = std::sin(m_a0);
        float a1 = m_a0 + sweep;
        m_e1x = std::cos(a1), m_e1y = std::sin(a1);
        m_n0x = -m_e0y, m_n0y = m_e0x;  // inward normals of the two straight edges
        m_n1x = m_e1y, m_n1y = -m_e1x;
        // Eroded arcs and segments.
        float clipO = m_ro > 0 ? std::asin(std::min(1.0f, r / m_ro)) : 0;
        m_oa0 = m_a0 + clipO;
        m_oa1 = a1 - clipO;
        m_hasInner = m_ri > 0;
        if (m_hasInner) {
            float clipI = std::asin(std::min(1.0f, r / m_ri));
            m_ia0 = m_a0 + clipI;
            m_ia1 = a1 - clipI;
            m_hasInner = m_full || m_ia1 > m_ia0;
        }
        m_tMax = std::sqrt(std::max(0.0f, m_ro * m_ro - r * r));
        m_tMin = std::sqrt(std::max(0.0f, m_ri * m_ri - r * r));
        if (!m_reflex && r > 0 && sweep > 0) m_tMin = std::max(m_tMin, r / std::tan(sweep / 2));
        m_tMin = std::min(m_tMin, m_tMax);
        ComputeBounds();
    }

    float Distance(float x, float y, float& nx, float& ny) const override {
        x -= m_cx;
        y -= m_cy;
        if (m_full) return RingShape(0, 0, m_out, m_in).Distance(x, y, nx, ny);
        float qx = 0, qy = 0, best = 1e30f;
        auto consider = [&](float px, float py) {
            float dx = x - px, dy = y - py, d2 = dx * dx + dy * dy;
            if (d2 < best) {
                best = d2;
                qx = px;
                qy = py;
            }
        };
        NearestOnArc(x, y, m_ro, m_oa0, m_oa1, consider);
        if (m_hasInner) NearestOnArc(x, y, m_ri, m_ia0, m_ia1, consider);
        NearestOnEdge(x, y, m_e0x, m_e0y, m_n0x, m_n0y, consider);
        NearestOnEdge(x, y, m_e1x, m_e1y, m_n1x, m_n1y, consider);
        float dist = std::sqrt(best);
        float sign = InsideEroded(x, y) ? -1.0f : 1.0f;
        if (dist > 1e-4f) {
            nx = sign * (x - qx) / dist;
            ny = sign * (y - qy) / dist;
        } else {
            float rho = std::sqrt(x * x + y * y);
            nx = rho > 1e-4f ? x / rho : 0;
            ny = rho > 1e-4f ? y / rho : -1;
        }
        return sign * dist - m_r;
    }

    ShapeBounds Bounds() const override { return m_b; }
    void Origin(float& x, float& y) const override {
        x = m_cx;
        y = m_cy;
    }

private:
    static float Wrap(float a) {  // to [0, 2 pi)
        a = std::fmod(a, 2 * kPi);
        return a < 0 ? a + 2 * kPi : a;
    }

    template <class F>
    static void NearestOnArc(float x, float y, float radius, float a0, float a1, F&& consider) {
        float rho = std::sqrt(x * x + y * y);
        float rel = Wrap(std::atan2(y, x) - a0);
        if (rel <= a1 - a0 && rho > 1e-4f) {
            consider(x / rho * radius, y / rho * radius);
            return;
        }
        consider(std::cos(a0) * radius, std::sin(a0) * radius);
        consider(std::cos(a1) * radius, std::sin(a1) * radius);
    }

    // Edge line offset inwards by the corner radius: q = t * e + r * n, t in [tMin, tMax].
    template <class F>
    void NearestOnEdge(float x, float y, float ex, float ey, float nx, float ny, F&& consider) const {
        float t = std::clamp(x * ex + y * ey, m_tMin, m_tMax);
        consider(t * ex + m_r * nx, t * ey + m_r * ny);
    }

    bool InsideEroded(float x, float y) const {
        float rho = std::sqrt(x * x + y * y);
        if (rho > m_ro || rho < m_ri) return false;
        float s0 = x * m_n0x + y * m_n0y, s1 = x * m_n1x + y * m_n1y;
        if (!m_reflex) return s0 >= m_r && s1 >= m_r;
        if (s0 < 0 && s1 < 0) return false;  // outside the reflex wedge
        // distance to each edge ray (the complement of a reflex wedge is convex)
        float t0 = x * m_e0x + y * m_e0y, t1 = x * m_e1x + y * m_e1y;
        float d0 = t0 >= 0 ? std::fabs(s0) : rho, d1 = t1 >= 0 ? std::fabs(s1) : rho;
        return d0 >= m_r && d1 >= m_r;
    }

    void ComputeBounds() {
        float minX = 0, minY = 0, maxX = 0, maxY = 0;
        bool first = true;
        auto add = [&](float px, float py) {
            if (first) {
                minX = maxX = px;
                minY = maxY = py;
                first = false;
            }
            minX = std::min(minX, px), maxX = std::max(maxX, px);
            minY = std::min(minY, py), maxY = std::max(maxY, py);
        };
        float a1 = m_a0 + m_sweep;
        add(std::cos(m_a0) * m_out, std::sin(m_a0) * m_out);
        add(std::cos(a1) * m_out, std::sin(a1) * m_out);
        add(std::cos(m_a0) * m_in, std::sin(m_a0) * m_in);
        add(std::cos(a1) * m_in, std::sin(a1) * m_in);
        for (int k = 0; k < 4; ++k) {  // axis extremes of the outer arc
            float a = k * kPi / 2;
            if (m_full || Wrap(a - m_a0) <= m_sweep) add(std::cos(a) * m_out, std::sin(a) * m_out);
        }
        m_b = {m_cx + minX, m_cy + minY, maxX - minX, maxY - minY};
    }

    float m_cx, m_cy, m_out, m_in;
    float m_a0, m_sweep;
    bool m_full = false, m_reflex = false, m_hasInner = false;
    float m_r = 0, m_ro = 0, m_ri = 0;
    float m_e0x, m_e0y, m_e1x, m_e1y, m_n0x, m_n0y, m_n1x, m_n1y;
    float m_oa0 = 0, m_oa1 = 0, m_ia0 = 0, m_ia1 = 0;
    float m_tMin = 0, m_tMax = 0;
    ShapeBounds m_b;
};

}  // namespace lg
