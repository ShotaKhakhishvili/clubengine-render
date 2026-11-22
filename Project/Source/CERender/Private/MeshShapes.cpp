#include "MeshShapes.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>

using namespace CE;

namespace CER
{

namespace
{

constexpr double kPi = 3.14159265358979323846;

const std::set<std::string> kGenerated = { "torus", "ring", "gem", "roundbox", "lathe", "rock" };

FVectorF ToF(const Vec3& v) { return FVectorF{ static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z) }; }
Vec3 ToD(const FVectorF& v) { return Vec3{ v.x, v.y, v.z }; }

Vec3 Unit(const Vec3& v)
{
    const double len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return len > 1e-12 ? v * (1.0 / len) : Vec3{ 0.0, 0.0, 1.0 };
}

// Collects vertices and triangles. Every generator sets vertex normals pointing outward, and triangles
// are wound to match: in the engine's left-handed space a front face has Cross(c - a, b - a) facing out.
struct FBuilder
{
    FMeshImportResult mesh;

    uint32_t Add(const Vec3& position, const Vec3& normal)
    {
        FVertex v;
        v.coord = ToF(position);
        v.normal = ToF(Unit(normal));
        mesh.vertices.push_back(v);
        return static_cast<uint32_t>(mesh.vertices.size() - 1);
    }

    void Tri(uint32_t a, uint32_t b, uint32_t c)
    {
        const Vec3 pa = ToD(mesh.vertices[a].coord), pb = ToD(mesh.vertices[b].coord), pc = ToD(mesh.vertices[c].coord);
        const Vec3 face = Cross(pc - pa, pb - pa);
        if (Dot(face, face) < 1e-16)
            return; // degenerate (poles, points on the axis)

        const Vec3 hint = ToD(mesh.vertices[a].normal) + ToD(mesh.vertices[b].normal) + ToD(mesh.vertices[c].normal);
        if (Dot(face, hint) < 0.0)
            std::swap(b, c);
        mesh.indices.insert(mesh.indices.end(), { a, b, c });
    }

    void Quad(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { Tri(a, b, c); Tri(a, c, d); }

    FMeshImportResult Finish(const std::string& name)
    {
        mesh.name = name;
        FMeshImportSection section;
        section.indexCount = static_cast<uint32_t>(mesh.indices.size());
        mesh.sections = { section };
        return std::move(mesh);
    }
};

// One vertex per triangle corner with the face normal: the low-poly look
FMeshImportResult Flatten(const FMeshImportResult& source)
{
    FMeshImportResult out;
    out.name = source.name;
    out.vertices.reserve(source.indices.size());
    for (size_t i = 0; i + 2 < source.indices.size(); i += 3)
    {
        const FVertex& a = source.vertices[source.indices[i]];
        const FVertex& b = source.vertices[source.indices[i + 1]];
        const FVertex& c = source.vertices[source.indices[i + 2]];
        const FVectorF n = ToF(Unit(Cross(ToD(c.coord) - ToD(a.coord), ToD(b.coord) - ToD(a.coord))));
        for (const FVertex* v : { &a, &b, &c })
        {
            FVertex copy = *v;
            copy.normal = n;
            out.vertices.push_back(copy);
            out.indices.push_back(static_cast<uint32_t>(out.vertices.size() - 1));
        }
    }
    out.sections = source.sections;
    return out;
}

FMeshImportResult IcoSphere(int detail, const std::string& name)
{
    const double t = (1.0 + std::sqrt(5.0)) / 2.0;
    std::vector<Vec3> points = {
        { -1, t, 0 }, { 1, t, 0 }, { -1, -t, 0 }, { 1, -t, 0 },
        { 0, -1, t }, { 0, 1, t }, { 0, -1, -t }, { 0, 1, -t },
        { t, 0, -1 }, { t, 0, 1 }, { -t, 0, -1 }, { -t, 0, 1 },
    };
    std::vector<std::array<uint32_t, 3>> faces = {
        { 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 },
        { 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 },
        { 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 },
        { 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 },
    };
    for (Vec3& p : points) p = Unit(p);

    for (int level = 0; level < detail; ++level)
    {
        std::map<std::pair<uint32_t, uint32_t>, uint32_t> midpoints;
        auto Mid = [&](uint32_t a, uint32_t b)
        {
            const auto key = std::minmax(a, b);
            const auto it = midpoints.find(key);
            if (it != midpoints.end()) return it->second;
            points.push_back(Unit((points[a] + points[b]) * 0.5));
            return midpoints[key] = static_cast<uint32_t>(points.size() - 1);
        };

        std::vector<std::array<uint32_t, 3>> next;
        for (const auto& f : faces)
        {
            const uint32_t ab = Mid(f[0], f[1]), bc = Mid(f[1], f[2]), ca = Mid(f[2], f[0]);
            next.insert(next.end(), { { f[0], ab, ca }, { f[1], bc, ab }, { f[2], ca, bc }, { ab, bc, ca } });
        }
        faces = std::move(next);
    }

    FBuilder b;
    for (const Vec3& p : points) b.Add(p * 0.5, p);
    for (const auto& f : faces) b.Tri(f[0], f[1], f[2]);
    return b.Finish(name);
}

// Upright along Z, radius 0.5, z from -0.5 to 0.5; a cone narrows to a point at the top
FMeshImportResult Cylinder(int sides, bool cone, const std::string& name)
{
    FBuilder b;
    for (int i = 0; i < sides; ++i)
    {
        const double a0 = 2.0 * kPi * i / sides, a1 = 2.0 * kPi * (i + 1) / sides, am = 0.5 * (a0 + a1);
        const Vec3 d0{ std::cos(a0), std::sin(a0), 0.0 }, d1{ std::cos(a1), std::sin(a1), 0.0 }, dm{ std::cos(am), std::sin(am), 0.0 };
        const Vec3 up{ 0.0, 0.0, 0.5 };   // cone side slope: radius 0.5 over height 1
        const Vec3 n0 = cone ? d0 + up : d0, n1 = cone ? d1 + up : d1;

        const uint32_t b0 = b.Add(d0 * 0.5 + Vec3{ 0, 0, -0.5 }, n0);
        const uint32_t b1 = b.Add(d1 * 0.5 + Vec3{ 0, 0, -0.5 }, n1);
        if (cone)
            b.Tri(b0, b1, b.Add(Vec3{ 0, 0, 0.5 }, dm + up));
        else
            b.Quad(b0, b1, b.Add(d1 * 0.5 + Vec3{ 0, 0, 0.5 }, n1), b.Add(d0 * 0.5 + Vec3{ 0, 0, 0.5 }, n0));

        for (const double z : cone ? std::vector<double>{ -0.5 } : std::vector<double>{ -0.5, 0.5 })
        {
            const Vec3 n{ 0.0, 0.0, z };
            b.Tri(b.Add(Vec3{ 0, 0, z }, n), b.Add(d0 * 0.5 + Vec3{ 0, 0, z }, n), b.Add(d1 * 0.5 + Vec3{ 0, 0, z }, n));
        }
    }
    return b.Finish(name);
}

// Lying flat in XY, outer diameter 1, tube diameter = thickness
FMeshImportResult Torus(double thickness, int segments, const std::string& name)
{
    const double minor = 0.5 * thickness;
    const double major = 0.5 - minor;
    const int tube = std::max(6, segments / 3);

    FBuilder b;
    for (int i = 0; i <= segments; ++i)
    {
        const double u = 2.0 * kPi * i / segments;
        const Vec3 radial{ std::cos(u), std::sin(u), 0.0 };
        for (int j = 0; j <= tube; ++j)
        {
            const double v = 2.0 * kPi * j / tube;
            const Vec3 n = radial * std::cos(v) + Vec3{ 0.0, 0.0, std::sin(v) };
            b.Add(radial * major + n * minor, n);
        }
    }
    for (int i = 0; i < segments; ++i)
        for (int j = 0; j < tube; ++j)
        {
            const uint32_t a = i * (tube + 1) + j, c = (i + 1) * (tube + 1) + j;
            b.Quad(a, c, c + 1, a + 1);
        }
    return b.Finish(name);
}

// A brilliant-cut stone: table on top, crown, girdle 1 wide at z = 0.1, pavilion down to a point
FMeshImportResult Gem(int facets, const std::string& name)
{
    const double girdleZ = 0.1, tableZ = 0.5, tableR = 0.28;
    FBuilder b;
    const uint32_t top = b.Add(Vec3{ 0, 0, tableZ }, Vec3{ 0, 0, 1 });
    const uint32_t bottom = b.Add(Vec3{ 0, 0, -0.5 }, Vec3{ 0, 0, -1 });

    std::vector<uint32_t> girdle, table;
    for (int i = 0; i < facets; ++i)
    {
        const double a = 2.0 * kPi * i / facets, t = a + kPi / facets;
        const Vec3 g{ 0.5 * std::cos(a), 0.5 * std::sin(a), girdleZ };
        const Vec3 p{ tableR * std::cos(t), tableR * std::sin(t), tableZ };
        girdle.push_back(b.Add(g, g));      // normals only orient the triangles; flattening replaces them
        table.push_back(b.Add(p, p));
    }
    for (int i = 0; i < facets; ++i)
    {
        const int n = (i + 1) % facets;
        b.Tri(girdle[i], girdle[n], table[i]);
        b.Tri(table[i], girdle[n], table[n]);
        b.Tri(top, table[i], table[n]);
        b.Tri(bottom, girdle[n], girdle[i]);
    }
    return Flatten(b.Finish(name));
}

// A cube with rounded edges: a subdivided cube whose points are pushed onto the rounded surface
FMeshImportResult RoundBox(double bevel, int detail, const std::string& name)
{
    const double r = std::clamp(bevel, 0.0, 0.5), inner = 0.5 - r;

    // Grid lines per face: the flat middle, then the bevel band spaced so the arc steps are even
    std::vector<double> coords;
    for (int k = detail; k >= 0; --k) coords.push_back(-(inner + r * std::tan(0.25 * kPi * k / detail)));
    for (int k = 0; k <= detail; ++k) coords.push_back(inner + r * std::tan(0.25 * kPi * k / detail));
    coords.erase(std::unique(coords.begin(), coords.end(), [](double a, double c) { return std::abs(a - c) < 1e-9; }), coords.end());

    FBuilder b;
    const int n = static_cast<int>(coords.size());
    for (int axis = 0; axis < 3; ++axis)
        for (const double side : { -0.5, 0.5 })
        {
            const uint32_t first = static_cast<uint32_t>(b.mesh.vertices.size());
            for (int i = 0; i < n; ++i)
                for (int j = 0; j < n; ++j)
                {
                    double p[3];
                    p[axis] = side;
                    p[(axis + 1) % 3] = coords[i];
                    p[(axis + 2) % 3] = coords[j];
                    const Vec3 point{ p[0], p[1], p[2] };
                    const Vec3 core{ std::clamp(p[0], -inner, inner), std::clamp(p[1], -inner, inner), std::clamp(p[2], -inner, inner) };
                    const Vec3 normal = r > 0.0 ? Unit(point - core) : Unit(Vec3{ axis == 0 ? side : 0.0, axis == 1 ? side : 0.0, axis == 2 ? side : 0.0 });
                    b.Add(r > 0.0 ? core + normal * r : point, normal);
                }
            for (int i = 0; i + 1 < n; ++i)
                for (int j = 0; j + 1 < n; ++j)
                {
                    const uint32_t a = first + i * n + j;
                    b.Quad(a, a + n, a + n + 1, a + 1);
                }
        }
    return b.Finish(name);
}

// Revolves [radius, z] points around Z. Corners sharper than ~50 degrees stay sharp.
FMeshImportResult Lathe(const std::vector<std::array<double, 2>>& profile, int sides, const std::string& name)
{
    struct FRing { double r, z, nr, nz; bool joinPrevious; };
    std::vector<FRing> rings;

    auto SegmentNormal = [&](size_t i) // outward normal of segment i -> i + 1, for a profile drawn bottom to top
    {
        const double dr = profile[i + 1][0] - profile[i][0], dz = profile[i + 1][1] - profile[i][1];
        const double len = std::max(1e-12, std::sqrt(dr * dr + dz * dz));
        return std::array<double, 2>{ dz / len, -dr / len };
    };

    for (size_t i = 0; i < profile.size(); ++i)
    {
        const bool hasPrev = i > 0, hasNext = i + 1 < profile.size();
        const auto prev = hasPrev ? SegmentNormal(i - 1) : SegmentNormal(i);
        const auto next = hasNext ? SegmentNormal(i) : SegmentNormal(i - 1);
        const double cosAngle = prev[0] * next[0] + prev[1] * next[1];

        if (cosAngle < 0.64)
        {
            // Sharp corner: end the previous surface here and start a new one at the same point
            rings.push_back({ profile[i][0], profile[i][1], prev[0], prev[1], true });
            rings.push_back({ profile[i][0], profile[i][1], next[0], next[1], false });
        }
        else
        {
            const double nr = prev[0] + next[0], nz = prev[1] + next[1];
            const double len = std::max(1e-12, std::sqrt(nr * nr + nz * nz));
            rings.push_back({ profile[i][0], profile[i][1], nr / len, nz / len, true });
        }
    }

    FBuilder b;
    for (const FRing& ring : rings)
        for (int s = 0; s <= sides; ++s)
        {
            const double a = 2.0 * kPi * s / sides;
            const Vec3 d{ std::cos(a), std::sin(a), 0.0 };
            b.Add(d * ring.r + Vec3{ 0.0, 0.0, ring.z }, d * ring.nr + Vec3{ 0.0, 0.0, ring.nz });
        }
    for (size_t i = 1; i < rings.size(); ++i)
    {
        if (!rings[i].joinPrevious)
            continue;
        for (int s = 0; s < sides; ++s)
        {
            const uint32_t a = static_cast<uint32_t>((i - 1) * (sides + 1) + s), c = static_cast<uint32_t>(i * (sides + 1) + s);
            b.Quad(a, a + 1, c + 1, c);
        }
    }
    return b.Finish(name);
}

// Smooth pseudo-random value in -1..1 for a point (value noise on a lattice)
double Noise(const Vec3& p, int seed)
{
    auto Hash = [seed](int x, int y, int z)
    {
        uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u
                   + static_cast<uint32_t>(z) * 2147483647u + static_cast<uint32_t>(seed) * 144665u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return ((h ^ (h >> 16)) & 0xffff) / 32767.5 - 1.0;
    };
    const int x = static_cast<int>(std::floor(p.x)), y = static_cast<int>(std::floor(p.y)), z = static_cast<int>(std::floor(p.z));
    auto Fade = [](double t) { return t * t * (3.0 - 2.0 * t); };
    const double fx = Fade(p.x - x), fy = Fade(p.y - y), fz = Fade(p.z - z);
    auto L = [](double a, double c, double t) { return a + (c - a) * t; };
    return L(L(L(Hash(x, y, z), Hash(x + 1, y, z), fx), L(Hash(x, y + 1, z), Hash(x + 1, y + 1, z), fx), fy),
             L(L(Hash(x, y, z + 1), Hash(x + 1, y, z + 1), fx), L(Hash(x, y + 1, z + 1), Hash(x + 1, y + 1, z + 1), fx), fy), fz);
}

// A lumpy icosphere, squashed a little, scaled back to fit the unit box
FMeshImportResult Rock(int detail, int seed, const std::string& name)
{
    FMeshImportResult mesh = IcoSphere(detail, name);
    Vec3 lo{ 1e9, 1e9, 1e9 }, hi{ -1e9, -1e9, -1e9 };
    for (FVertex& v : mesh.vertices)
    {
        const Vec3 d = Unit(ToD(v.coord));
        const double bump = 0.28 * Noise(d * 1.6 + Vec3{ 10.0, 20.0, 30.0 }, seed) + 0.12 * Noise(d * 3.7, seed + 7);
        Vec3 p = d * (0.5 * (1.0 + bump));
        p.z *= 0.75;
        v.coord = ToF(p);
        v.normal = ToF(d);
        lo = Vec3{ std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z) };
        hi = Vec3{ std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z) };
    }
    const Vec3 center = (lo + hi) * 0.5;
    const double extent = std::max({ hi.x - lo.x, hi.y - lo.y, hi.z - lo.z });
    for (FVertex& v : mesh.vertices)
        v.coord = ToF((ToD(v.coord) - center) * (1.0 / extent));

    // Smooth normals of the displaced surface (used when "flat": false)
    std::vector<Vec3> normals(mesh.vertices.size(), Vec3{});
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
    {
        const uint32_t a = mesh.indices[i], c = mesh.indices[i + 1], e = mesh.indices[i + 2];
        const Vec3 face = Cross(ToD(mesh.vertices[e].coord) - ToD(mesh.vertices[a].coord), ToD(mesh.vertices[c].coord) - ToD(mesh.vertices[a].coord));
        for (const uint32_t k : { a, c, e }) normals[k] = normals[k] + face;
    }
    for (size_t i = 0; i < normals.size(); ++i) mesh.vertices[i].normal = ToF(Unit(normals[i]));
    return mesh;
}

}

bool IsGeneratedShape(const std::string& name)
{
    return kGenerated.contains(name);
}

bool NeedsGeneratedMesh(const std::string& name, const FShapeDesc& shape)
{
    if (IsGeneratedShape(name))
        return true;
    return (name == "sphere" || name == "cylinder" || name == "cone") && (shape.flat.value_or(false) || shape.detail >= 0);
}

FMeshImportResult MakeShape(const std::string& name, const FShapeDesc& shape)
{
    const bool flat = shape.flat.value_or(name == "rock");
    auto Detail = [&](int smooth, int faceted) { return shape.detail >= 0 ? shape.detail : (flat ? faceted : smooth); };

    FMeshImportResult mesh;
    if (name == "sphere")         mesh = IcoSphere(Detail(3, 1), name);
    else if (name == "cylinder")  mesh = Cylinder(Detail(32, 10), false, name);
    else if (name == "cone")      mesh = Cylinder(Detail(32, 10), true, name);
    else if (name == "torus" || name == "ring")
        mesh = Torus(shape.thickness > 0.0 ? shape.thickness : (name == "ring" ? 0.12 : 0.35), Detail(48, 14), name);
    else if (name == "gem")       return Gem(Detail(8, 8), name);
    else if (name == "roundbox")  mesh = RoundBox(shape.bevel, Detail(5, 2), name);
    else if (name == "lathe")     mesh = Lathe(shape.profile, Detail(48, 12), name);
    else if (name == "rock")      mesh = Rock(Detail(2, 1), shape.seed, name);

    return flat ? Flatten(mesh) : mesh;
}

}
