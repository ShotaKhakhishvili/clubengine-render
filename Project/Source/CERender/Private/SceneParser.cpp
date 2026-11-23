#include "SceneParser.h"
#include "MeshShapes.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <set>
#include <sstream>

namespace CER
{

using nlohmann::json;

int FOutputDesc::FrameCount() const
{
    if (duration <= 0.0)
        return 1;
    return std::max(1, static_cast<int>(std::lround(duration * fps)));
}

namespace
{

const std::set<std::string> kBuiltinMeshes = { "cube", "sphere", "cylinder", "cone", "plane", "torus", "ring", "gem", "roundbox", "lathe", "rock" };
const char* kBuiltinList = "cube, sphere, cylinder, cone, plane, torus, ring, gem, roundbox, lathe, rock";
constexpr int kMaxEmitterInstances = 20000;

size_t EditDistance(const std::string& a, const std::string& b)
{
    std::vector<size_t> row(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) row[j] = j;
    for (size_t i = 1; i <= a.size(); ++i)
    {
        size_t prev = row[0];
        row[0] = i;
        for (size_t j = 1; j <= b.size(); ++j)
        {
            const size_t cur = row[j];
            row[j] = std::min({ row[j] + 1, row[j - 1] + 1, prev + (a[i - 1] == b[j - 1] ? 0 : 1) });
            prev = cur;
        }
    }
    return row[b.size()];
}

std::string Join(const std::vector<std::string>& items)
{
    std::string out;
    for (size_t i = 0; i < items.size(); ++i)
        out += (i ? ", " : "") + items[i];
    return out;
}

double SrgbToLinear(double c)
{
    c = std::clamp(c, 0.0, 1.0);
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

class Reader
{
public:
    explicit Reader(std::vector<std::string>& errors, std::filesystem::path baseDir)
        : errors(errors), baseDir(std::move(baseDir)) {}

    void Error(const std::string& path, const std::string& message)
    {
        errors.push_back(path + ": " + message);
    }

    // Flags keys that aren't in the allowed list, with a "did you mean" hint for typos
    void CheckKeys(const json& obj, const std::string& path, const std::vector<std::string>& allowed)
    {
        for (const auto& [key, value] : obj.items())
        {
            if (std::find(allowed.begin(), allowed.end(), key) != allowed.end())
                continue;

            std::string best;
            size_t bestDistance = 3;
            for (const auto& candidate : allowed)
            {
                const size_t d = EditDistance(key, candidate);
                if (d < bestDistance) { bestDistance = d; best = candidate; }
            }

            Error(path, "unknown key \"" + key + "\"" + (best.empty() ? " (allowed: " + Join(allowed) + ")" : " (did you mean \"" + best + "\"?)"));
        }
    }

    bool Number(const json& v, const std::string& path, double& out)
    {
        if (!v.is_number()) { Error(path, "expected a number"); return false; }
        out = v.get<double>();
        return true;
    }

    bool Int(const json& v, const std::string& path, int& out, int minValue, int maxValue)
    {
        if (!v.is_number_integer() || v.get<long long>() < minValue || v.get<long long>() > maxValue)
        {
            Error(path, "expected a whole number from " + std::to_string(minValue) + " to " + std::to_string(maxValue));
            return false;
        }
        out = v.get<int>();
        return true;
    }

    bool Bool(const json& v, const std::string& path, bool& out)
    {
        if (!v.is_boolean()) { Error(path, "expected true or false"); return false; }
        out = v.get<bool>();
        return true;
    }

    bool String(const json& v, const std::string& path, std::string& out)
    {
        if (!v.is_string()) { Error(path, "expected a string"); return false; }
        out = v.get<std::string>();
        return true;
    }

    bool Vector(const json& v, const std::string& path, Vec3& out)
    {
        if (!v.is_array() || v.size() != 3 || !v[0].is_number() || !v[1].is_number() || !v[2].is_number())
        {
            Error(path, "expected [x, y, z]");
            return false;
        }
        out = Vec3{ v[0].get<double>(), v[1].get<double>(), v[2].get<double>() };
        return true;
    }

    // A number means the same value on all three axes
    bool Scale(const json& v, const std::string& path, Vec3& out)
    {
        if (v.is_number())
        {
            const double s = v.get<double>();
            out = Vec3{ s, s, s };
            return true;
        }
        if (!v.is_array() || v.size() != 3)
        {
            Error(path, "expected a number or [x, y, z]");
            return false;
        }
        return Vector(v, path, out);
    }

    // "#rrggbb", "#rgb" or [r, g, b] in 0..1, all sRGB like CSS. Returned in linear space.
    bool Color(const json& v, const std::string& path, Vec3& out)
    {
        double r = 0, g = 0, b = 0;

        if (v.is_string())
        {
            std::string s = v.get<std::string>();
            if (!s.empty() && s[0] == '#') s = s.substr(1);
            if (s.size() == 3) s = { s[0], s[0], s[1], s[1], s[2], s[2] };

            if (s.size() != 6 || s.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
            {
                Error(path, "expected a color like \"#ff8a65\"");
                return false;
            }
            r = std::stoi(s.substr(0, 2), nullptr, 16) / 255.0;
            g = std::stoi(s.substr(2, 2), nullptr, 16) / 255.0;
            b = std::stoi(s.substr(4, 2), nullptr, 16) / 255.0;
        }
        else if (v.is_array() && v.size() == 3 && v[0].is_number() && v[1].is_number() && v[2].is_number())
        {
            r = v[0].get<double>(); g = v[1].get<double>(); b = v[2].get<double>();
        }
        else
        {
            Error(path, "expected a color like \"#ff8a65\" or [r, g, b] in 0..1");
            return false;
        }

        out = Vec3{ SrgbToLinear(r), SrgbToLinear(g), SrgbToLinear(b) };
        return true;
    }

    void Material(const json& v, const std::string& path, FMaterialDesc& out)
    {
        if (!v.is_object()) { Error(path, "expected an object"); return; }
        CheckKeys(v, path, { "color", "roughness", "metallic", "emissive", "unlit" });

        if (v.contains("color"))     Color(v["color"], path + ".color", out.color);
        if (v.contains("roughness")) Number(v["roughness"], path + ".roughness", out.roughness);
        if (v.contains("metallic"))  Number(v["metallic"], path + ".metallic", out.metallic);
        if (v.contains("unlit"))     Bool(v["unlit"], path + ".unlit", out.unlit);

        if (v.contains("emissive"))
        {
            // A number makes the material glow in its own color
            if (v["emissive"].is_number())
                out.emissive = out.color * v["emissive"].get<double>();
            else
                Color(v["emissive"], path + ".emissive", out.emissive);
        }
    }

    void LookAt(const json& v, const std::string& path, FLookAt& out)
    {
        if (v.is_string())
            out.objectId = v.get<std::string>();
        else
        {
            Vec3 p;
            if (Vector(v, path, p))
                out.point = p;
        }
    }

    void Light(const json& v, const std::string& path, FLightDesc& out)
    {
        if (!v.is_object()) { Error(path, "expected an object"); return; }
        CheckKeys(v, path, { "type", "color", "intensity", "radius", "innerAngle", "outerAngle" });

        std::string type = "directional";
        if (v.contains("type")) String(v["type"], path + ".type", type);

        if (type == "directional")      { out.kind = ELightKind::Directional; out.intensity = 1.1; }
        else if (type == "point")       { out.kind = ELightKind::Point; out.intensity = 40.0; }
        else if (type == "spot")        { out.kind = ELightKind::Spot; out.intensity = 60.0; }
        else Error(path + ".type", "unknown \"" + type + "\" (expected directional, point, spot)");

        if (v.contains("color"))      Color(v["color"], path + ".color", out.color);
        if (v.contains("intensity"))  Number(v["intensity"], path + ".intensity", out.intensity);
        if (v.contains("radius"))     Number(v["radius"], path + ".radius", out.radius);
        if (v.contains("innerAngle")) Number(v["innerAngle"], path + ".innerAngle", out.innerAngle);
        if (v.contains("outerAngle")) Number(v["outerAngle"], path + ".outerAngle", out.outerAngle);
    }

    void Object(const json& v, const std::string& path, FObjectDesc& out, const std::map<std::string, FMaterialDesc>& materials)
    {
        if (!v.is_object()) { Error(path, "expected an object"); return; }
        CheckKeys(v, path, { "id", "mesh", "light", "camera", "material", "color", "position", "rotation",
                             "scale", "parent", "lookAt", "shadow", "visible", "pivot", "detail", "flat",
                             "thickness", "bevel", "profile", "seed", "emitter" });

        if (!v.contains("id")) Error(path, "missing \"id\"");
        else String(v["id"], path + ".id", out.id);

        // None of them makes a group: an invisible pivot that other objects can be parented to
        const int kinds = static_cast<int>(v.contains("mesh")) + v.contains("light") + v.contains("camera");
        if (kinds > 1)
            Error(path, "can only be one of \"mesh\", \"light\" or \"camera\"");
        if (kinds == 0)
            out.kind = EObjectKind::Group;

        if (v.contains("mesh"))
        {
            out.kind = EObjectKind::Mesh;
            std::string mesh;
            if (String(v["mesh"], path + ".mesh", mesh))
            {
                if (kBuiltinMeshes.contains(mesh))
                    out.mesh = mesh;
                else
                {
                    const std::filesystem::path p = baseDir / mesh;
                    const std::string ext = p.extension().string();
                    if (ext != ".glb" && ext != ".gltf")
                        Error(path + ".mesh", "\"" + mesh + "\" is not a built-in mesh (" + std::string(kBuiltinList) + ") or a .glb/.gltf file");
                    else if (!std::filesystem::exists(p))
                        Error(path + ".mesh", "file not found: " + p.string());
                    out.mesh = p.string();
                    out.meshIsBuiltin = false;
                }
            }
        }
        else if (v.contains("light"))
        {
            out.kind = EObjectKind::Light;
            Light(v["light"], path + ".light", out.light);
        }
        else if (v.contains("camera"))
        {
            out.kind = EObjectKind::Camera;
            if (!v["camera"].is_object() || !v["camera"].empty())
                Error(path + ".camera", "expected {} (the field of view is fixed at 90 degrees vertical for now)");
        }

        if (v.contains("material"))
        {
            if (v["material"].is_string())
            {
                out.materialName = v["material"].get<std::string>();
                if (!materials.contains(out.materialName))
                    Error(path + ".material", "no material named \"" + out.materialName + "\" in \"materials\"");
            }
            else
            {
                FMaterialDesc m;
                Material(v["material"], path + ".material", m);
                out.inlineMaterial = m;
            }
        }

        // An emitter is a mesh object whose mesh is used for many instances
        if (v.contains("emitter"))
        {
            if (out.kind != EObjectKind::Mesh || !v.contains("mesh"))
                Error(path + ".emitter", "an emitter needs a \"mesh\" for its instances");
            else
            {
                out.kind = EObjectKind::Emitter;
                Emitter(v["emitter"], path + ".emitter", out.emitter);
            }
        }

        const bool hasMesh = out.kind == EObjectKind::Mesh || out.kind == EObjectKind::Emitter;

        if (v.contains("color"))
        {
            // Shortcut: a color on top of the named/inline material (or the default one)
            FMaterialDesc m = out.inlineMaterial.value_or(
                materials.contains(out.materialName) ? materials.at(out.materialName) : FMaterialDesc{});

            // Emitters can take a list to pick from, one color per instance
            const json& c = v["color"];
            if (c.is_array() && !c.empty() && (c[0].is_string() || c[0].is_array()))
            {
                if (out.kind != EObjectKind::Emitter)
                    Error(path + ".color", "a list of colors only works on emitters; give one color");
                for (size_t i = 0; i < c.size(); ++i)
                    if (Vec3 color; Color(c[i], path + ".color[" + std::to_string(i) + "]", color))
                        out.emitter.palette.push_back(color);
                if (!out.emitter.palette.empty())
                    m.color = out.emitter.palette[0];
            }
            else
                Color(c, path + ".color", m.color);
            out.inlineMaterial = m;
            out.materialName.clear();
        }

        if (!hasMesh && (v.contains("material") || v.contains("color") || v.contains("shadow")))
            Error(path, "\"material\", \"color\" and \"shadow\" only apply to mesh objects (lights use light.color, groups have no look)");

        if (hasMesh)
            Shape(v, path, out);
        else
            for (const char* key : { "pivot", "detail", "flat", "thickness", "bevel", "profile", "seed" })
                if (v.contains(key))
                    Error(path + "." + key, "only applies to mesh objects");

        if (v.contains("position")) Vector(v["position"], path + ".position", out.position);
        if (v.contains("rotation")) Vector(v["rotation"], path + ".rotation", out.rotation);
        if (v.contains("scale"))    Scale(v["scale"], path + ".scale", out.scale);
        if (v.contains("parent"))   String(v["parent"], path + ".parent", out.parent);
        if (v.contains("lookAt"))   LookAt(v["lookAt"], path + ".lookAt", out.lookAt);
        if (v.contains("visible"))  Bool(v["visible"], path + ".visible", out.visible);

        // Emitter instances cast no shadows unless asked: hundreds of copies cost a lot and rarely read well
        out.shadow = out.kind == EObjectKind::Mesh && out.mesh != "plane";
        if (v.contains("shadow"))   Bool(v["shadow"], path + ".shadow", out.shadow);
    }

    void Emitter(const json& v, const std::string& path, FEmitterDesc& out)
    {
        if (!v.is_object()) { Error(path, "expected {\"count\", \"area\", ...}"); return; }
        CheckKeys(v, path, { "count", "area", "size", "scale", "rotation", "motion", "loop", "minGap", "avoid", "seed" });

        if (!v.contains("count")) Error(path, "missing \"count\"");
        else Int(v["count"], path + ".count", out.count, 1, kMaxEmitterInstances);

        if (!v.contains("area")) Error(path, "missing \"area\": {\"min\": [x, y, z], \"max\": [x, y, z]} or {\"center\": [x, y, z], \"radius\": r}");
        else Area(v["area"], path + ".area", out.area);

        if (v.contains("size"))
        {
            const json& s = v["size"];
            if (s.is_number())
                out.sizeMin = out.sizeMax = s.get<double>();
            else if (s.is_array() && s.size() == 2 && s[0].is_number() && s[1].is_number())
            {
                out.sizeMin = s[0].get<double>();
                out.sizeMax = s[1].get<double>();
            }
            else
                Error(path + ".size", "expected a number or [min, max]");
            if (out.sizeMin <= 0.0 || out.sizeMax < out.sizeMin)
                Error(path + ".size", "needs 0 < min <= max");
        }

        if (v.contains("scale")) Scale(v["scale"], path + ".scale", out.scale);

        if (v.contains("rotation"))
        {
            const json& r = v["rotation"];
            if (r.is_string())
            {
                const std::string mode = r.get<std::string>();
                if (mode == "random")   out.rotation = EEmitterRotation::Random;
                else if (mode == "yaw") out.rotation = EEmitterRotation::Yaw;
                else Error(path + ".rotation", "unknown \"" + mode + "\" (expected \"random\", \"yaw\" or [pitch, yaw, roll])");
            }
            else
                Vector(r, path + ".rotation", out.fixedRotation);
        }

        if (v.contains("seed")) Int(v["seed"], path + ".seed", out.seed, 0, 1000000);

        if (v.contains("motion"))
        {
            out.moving = true;
            const json& m = v["motion"];
            const std::string motionPath = path + ".motion";
            if (!m.is_object()) { Error(motionPath, "expected {\"fall\", \"drift\", \"sway\", \"spin\"}"); return; }
            CheckKeys(m, motionPath, { "fall", "drift", "sway", "spin" });

            double fall = 0.0;
            if (m.contains("fall"))  Number(m["fall"], motionPath + ".fall", fall);
            if (m.contains("drift")) Vector(m["drift"], motionPath + ".drift", out.velocity);
            out.velocity.z -= fall;

            if (m.contains("sway"))
            {
                const json& s = m["sway"];
                if (!s.is_object()) Error(motionPath + ".sway", "expected {\"amplitude\", \"period\"}");
                else
                {
                    CheckKeys(s, motionPath + ".sway", { "amplitude", "period" });
                    if (!s.contains("amplitude")) Error(motionPath + ".sway", "missing \"amplitude\"");
                    else Number(s["amplitude"], motionPath + ".sway.amplitude", out.swayAmplitude);
                    if (s.contains("period") && Number(s["period"], motionPath + ".sway.period", out.swayPeriod) && out.swayPeriod <= 0.0)
                        Error(motionPath + ".sway.period", "must be more than 0");
                }
            }

            if (m.contains("spin"))
            {
                const json& s = m["spin"];
                if (s.is_number())
                    out.spinMin = out.spinMax = s.get<double>();
                else if (s.is_array() && s.size() == 2 && s[0].is_number() && s[1].is_number())
                {
                    out.spinMin = s[0].get<double>();
                    out.spinMax = s[1].get<double>();
                }
                else
                    Error(motionPath + ".spin", "expected degrees per second, or [min, max]");
                if (out.spinMin < 0.0 || out.spinMax < out.spinMin)
                    Error(motionPath + ".spin", "needs 0 <= min <= max (the direction is random)");
            }

            if (v.contains("loop")) Bool(v["loop"], path + ".loop", out.loop);

            // A loop needs room to travel through
            const Vec3& vel = out.velocity;
            if (out.loop && std::abs(vel.z) > 1e-9 && out.area.Height() <= 0.0)
                Error(path + ".area", "falling or rising instances loop through the area's height, so it needs one ("
                      + std::string(out.area.disc ? "\"height\"" : "max z above min z") + "), or set \"loop\": false");
        }
        else if (v.contains("loop"))
            Error(path + ".loop", "only applies with \"motion\"");

        if (v.contains("minGap") && Number(v["minGap"], path + ".minGap", out.minGap) && out.minGap < 0.0)
            Error(path + ".minGap", "must be 0 or more");

        if (v.contains("avoid"))
        {
            const json& a = v["avoid"];
            if (!a.is_array()) Error(path + ".avoid", "expected [{\"center\": [x, y], \"radius\": r} or {\"min\": [x, y], \"max\": [x, y]}, ...]");
            else
                for (size_t i = 0; i < a.size(); ++i)
                {
                    FAvoidDesc region;
                    if (AvoidRegion(a[i], path + ".avoid[" + std::to_string(i) + "]", region))
                        out.avoid.push_back(region);
                }
        }

        if (out.moving && (v.contains("minGap") || v.contains("avoid")))
            Error(path, "\"minGap\" and \"avoid\" only apply to static scatter (no \"motion\")");
    }

    void Area(const json& v, const std::string& path, FAreaDesc& out)
    {
        if (!v.is_object()) { Error(path, "expected {\"min\", \"max\"} (a box) or {\"center\", \"radius\"} (a disc)"); return; }
        CheckKeys(v, path, { "min", "max", "center", "radius", "inner", "height" });

        const bool box = v.contains("min") || v.contains("max");
        const bool disc = v.contains("center") || v.contains("radius") || v.contains("inner") || v.contains("height");
        if (box == disc)
        {
            Error(path, box ? "mixes a box (\"min\", \"max\") with a disc (\"center\", \"radius\", \"inner\", \"height\"); use one"
                            : "expected {\"min\", \"max\"} (a box) or {\"center\", \"radius\"} (a disc)");
            return;
        }

        if (box)
        {
            if (!v.contains("min") || !v.contains("max")) { Error(path, "a box needs both \"min\" and \"max\""); return; }
            if (Vector(v["min"], path + ".min", out.min) && Vector(v["max"], path + ".max", out.max)
                && (out.max.x < out.min.x || out.max.y < out.min.y || out.max.z < out.min.z))
                Error(path, "\"max\" must be at least \"min\" on every axis");
            return;
        }

        out.disc = true;
        if (v.contains("center")) Vector(v["center"], path + ".center", out.center);
        if (!v.contains("radius")) Error(path, "a disc needs \"radius\"");
        else if (Number(v["radius"], path + ".radius", out.radius) && out.radius <= 0.0)
            Error(path + ".radius", "must be more than 0");
        if (v.contains("inner") && Number(v["inner"], path + ".inner", out.inner) && (out.inner < 0.0 || out.inner >= out.radius))
            Error(path + ".inner", "must be at least 0 and less than \"radius\"");
        if (v.contains("height") && Number(v["height"], path + ".height", out.height) && out.height < 0.0)
            Error(path + ".height", "must be 0 or more");
    }

    bool AvoidRegion(const json& v, const std::string& path, FAvoidDesc& out)
    {
        if (!v.is_object()) { Error(path, "expected {\"center\": [x, y], \"radius\": r} or {\"min\": [x, y], \"max\": [x, y]}"); return false; }
        CheckKeys(v, path, { "center", "radius", "min", "max" });

        // Avoid regions are horizontal: [x, y] (a z is allowed and ignored)
        auto Point = [&](const char* key, Vec3& point)
        {
            const json& p = v[key];
            if (!p.is_array() || p.size() < 2 || p.size() > 3 || !p[0].is_number() || !p[1].is_number() || (p.size() == 3 && !p[2].is_number()))
            {
                Error(path + "." + key, "expected [x, y]");
                return false;
            }
            point = Vec3{ p[0].get<double>(), p[1].get<double>(), 0.0 };
            return true;
        };

        if (v.contains("radius") || v.contains("center"))
        {
            if (v.contains("min") || v.contains("max")) { Error(path, "is either a circle (\"center\", \"radius\") or a box (\"min\", \"max\")"); return false; }
            if (!v.contains("center") || !v.contains("radius")) { Error(path, "a circle needs \"center\" and \"radius\""); return false; }
            out.circle = true;
            return Point("center", out.center) && Number(v["radius"], path + ".radius", out.radius);
        }
        if (!v.contains("min") || !v.contains("max")) { Error(path, "a box needs \"min\" and \"max\""); return false; }
        out.circle = false;
        return Point("min", out.min) && Point("max", out.max);
    }

    // Pivot and the options of generated shapes
    void Shape(const json& v, const std::string& path, FObjectDesc& out)
    {
        const std::string& mesh = out.mesh;
        FShapeDesc& shape = out.shape;

        if (v.contains("pivot"))
        {
            std::string pivot;
            if (String(v["pivot"], path + ".pivot", pivot))
            {
                if (pivot == "center")      out.pivot = EPivot::Center;
                else if (pivot == "bottom") out.pivot = EPivot::Bottom;
                else if (pivot == "origin") out.pivot = EPivot::Origin;
                else Error(path + ".pivot", "unknown \"" + pivot + "\" (expected center, bottom, origin)");
            }
        }

        auto Only = [&](const char* key, std::initializer_list<const char*> shapes) -> bool
        {
            if (!v.contains(key))
                return false;
            for (const char* name : shapes)
                if (mesh == name) return true;
            std::string list;
            for (const char* name : shapes) list += (list.empty() ? "" : ", ") + std::string(name);
            Error(path + "." + key, "only applies to these meshes: " + list);
            return false;
        };

        if (Only("flat", { "sphere", "cylinder", "cone", "torus", "ring", "roundbox", "lathe", "rock" }))
        {
            bool flat = false;
            if (Bool(v["flat"], path + ".flat", flat)) shape.flat = flat;
        }

        if (Only("detail", { "sphere", "cylinder", "cone", "torus", "ring", "gem", "roundbox", "lathe", "rock" }))
        {
            // Sphere/rock: subdivision levels. Roundbox: segments per bevel. Others: segments around.
            const bool levels = mesh == "sphere" || mesh == "rock";
            Int(v["detail"], path + ".detail", shape.detail, levels ? 0 : (mesh == "roundbox" ? 1 : 3), levels ? (mesh == "rock" ? 4 : 5) : (mesh == "roundbox" ? 16 : 128));
        }

        if (Only("thickness", { "torus", "ring" }) && Number(v["thickness"], path + ".thickness", shape.thickness)
            && (shape.thickness < 0.01 || shape.thickness > 0.9))
            Error(path + ".thickness", "must be between 0.01 and 0.9 (tube diameter, the ring is 1 across)");

        if (Only("bevel", { "roundbox" }) && Number(v["bevel"], path + ".bevel", shape.bevel)
            && (shape.bevel < 0.0 || shape.bevel > 0.5))
            Error(path + ".bevel", "must be between 0 and 0.5");

        if (Only("seed", { "rock" }))
            Int(v["seed"], path + ".seed", shape.seed, 0, 1000000);

        if (mesh == "lathe" && !v.contains("profile"))
            Error(path, "a lathe needs \"profile\": [[radius, z], ...] from bottom to top");
        if (Only("profile", { "lathe" }))
        {
            const json& p = v["profile"];
            if (!p.is_array() || p.size() < 2)
                Error(path + ".profile", "expected at least two [radius, z] points");
            else
                for (size_t i = 0; i < p.size(); ++i)
                {
                    const std::string pointPath = path + ".profile[" + std::to_string(i) + "]";
                    if (!p[i].is_array() || p[i].size() != 2 || !p[i][0].is_number() || !p[i][1].is_number() || p[i][0].get<double>() < 0.0)
                        Error(pointPath, "expected [radius, z] with radius >= 0");
                    else
                        shape.profile.push_back({ p[i][0].get<double>(), p[i][1].get<double>() });
                }
        }
    }

    EEase Ease(const json& v, const std::string& path)
    {
        static const std::vector<std::pair<std::string, EEase>> eases = {
            {"linear", EEase::Linear}, {"step", EEase::Step},
            {"inQuad", EEase::InQuad}, {"outQuad", EEase::OutQuad}, {"inOutQuad", EEase::InOutQuad},
            {"inCubic", EEase::InCubic}, {"outCubic", EEase::OutCubic}, {"inOutCubic", EEase::InOutCubic},
            {"inSine", EEase::InSine}, {"outSine", EEase::OutSine}, {"inOutSine", EEase::InOutSine},
            {"inBack", EEase::InBack}, {"outBack", EEase::OutBack}, {"inOutBack", EEase::InOutBack},
            {"outBounce", EEase::OutBounce}, {"outElastic", EEase::OutElastic},
        };

        std::string name;
        if (!String(v, path, name))
            return EEase::InOutCubic;

        for (const auto& [n, e] : eases)
            if (n == name) return e;

        std::vector<std::string> names;
        for (const auto& [n, e] : eases) names.push_back(n);
        Error(path, "unknown ease \"" + name + "\" (expected " + Join(names) + ")");
        return EEase::InOutCubic;
    }

    // One animated value: a vector for whole-vector targets, a number for one component or scalar properties
    bool Value(const json& v, const std::string& path, bool isVector, EProperty property, Vec3& out)
    {
        if (property == EProperty::Color && isVector)
            return Color(v, path, out);

        if (property == EProperty::Visible)
        {
            bool b = true;
            if (!Bool(v, path, b)) return false;
            out = Vec3{ b ? 1.0 : 0.0, 0, 0 };
            return true;
        }

        if (property == EProperty::Scale && isVector)
            return Scale(v, path, out);

        if (isVector)
            return Vector(v, path, out);

        double d = 0;
        if (!Number(v, path, d)) return false;
        out = Vec3{ d, d, d };
        return true;
    }

    void Track(const json& v, const std::string& path, FTrackDesc& out, const std::map<std::string, const FObjectDesc*>& objects)
    {
        if (!v.is_object()) { Error(path, "expected an object"); return; }
        CheckKeys(v, path, { "target", "keys", "ease", "repeat", "period", "spin", "wave", "orbit", "follow", "path", "bounce", "walk" });

        std::string target;
        if (!v.contains("target")) { Error(path, "missing \"target\" (like \"box.position\" or \"box.position.z\")"); return; }
        if (!String(v["target"], path + ".target", target)) return;

        // "id.property" or "id.property.x"; a walk targets the rig root by its id alone
        std::vector<std::string> parts;
        std::stringstream ss(target);
        for (std::string part; std::getline(ss, part, '.');) parts.push_back(part);

        const bool walk = v.contains("walk");
        if (walk && parts.size() != 1)
        {
            Error(path + ".target", "a walk targets the rig's root object by id alone, like \"bot\"");
            return;
        }
        if (!walk && (parts.size() < 2 || parts.size() > 3))
        {
            Error(path + ".target", "expected \"id.property\" or \"id.property.x\", got \"" + target + "\"");
            return;
        }

        out.objectId = parts[0];
        const auto object = objects.find(out.objectId);
        if (object == objects.end())
        {
            Error(path + ".target", "no object with id \"" + out.objectId + "\"");
            return;
        }

        if (walk)
        {
            out.kind = ETrackKind::Walk;
            for (const char* key : { "keys", "spin", "wave", "orbit", "follow", "path", "bounce" })
                if (v.contains(key)) Error(path, "needs exactly one of \"keys\", \"spin\", \"wave\", \"orbit\", \"follow\", \"path\", \"bounce\" or \"walk\"");
            Walk(v["walk"], path + ".walk", out, objects);
            return;
        }

        static const std::vector<std::pair<std::string, EProperty>> properties = {
            {"position", EProperty::Position}, {"rotation", EProperty::Rotation}, {"scale", EProperty::Scale},
            {"color", EProperty::Color}, {"intensity", EProperty::Intensity}, {"visible", EProperty::Visible},
            {"lookAt", EProperty::LookAt},
        };

        bool found = false;
        for (const auto& [name, p] : properties)
            if (name == parts[1]) { out.property = p; found = true; }

        if (!found)
        {
            Error(path + ".target", "unknown property \"" + parts[1] + "\" (expected position, rotation, scale, color, intensity, visible, lookAt)");
            return;
        }

        const EObjectKind kind = object->second->kind;
        if (out.property == EProperty::Intensity && kind != EObjectKind::Light)
            Error(path + ".target", "\"intensity\" only applies to lights");
        if (out.property == EProperty::Color && (kind == EObjectKind::Camera || kind == EObjectKind::Group))
            Error(path + ".target", "only meshes and lights have a color");
        if (out.property == EProperty::Color && kind == EObjectKind::Emitter)
            Error(path + ".target", "emitter instances keep their colors (animate position, rotation, scale or visible to move the whole emitter)");

        const bool vectorProperty = out.property == EProperty::Position || out.property == EProperty::Rotation
                                 || out.property == EProperty::Scale || out.property == EProperty::LookAt
                                 || out.property == EProperty::Color;

        if (parts.size() == 3)
        {
            if (!vectorProperty || out.property == EProperty::Color)
            {
                Error(path + ".target", "\"" + parts[1] + "\" has no components");
                return;
            }
            if (parts[2] == "x") out.component = 0;
            else if (parts[2] == "y") out.component = 1;
            else if (parts[2] == "z") out.component = 2;
            else { Error(path + ".target", "component must be x, y or z"); return; }
        }

        const bool isVector = vectorProperty && out.component < 0;

        const int generators = static_cast<int>(v.contains("keys")) + v.contains("spin") + v.contains("wave") + v.contains("orbit")
                             + v.contains("follow") + v.contains("path") + v.contains("bounce");
        if (generators != 1)
        {
            Error(path, "needs exactly one of \"keys\", \"spin\", \"wave\", \"orbit\", \"follow\", \"path\", \"bounce\" or \"walk\"");
            return;
        }
        if ((v.contains("repeat") || v.contains("period")) && !v.contains("keys"))
            Error(path, "\"repeat\" and \"period\" only apply to \"keys\" tracks");

        const bool wholePosition = (out.property == EProperty::Position || out.property == EProperty::LookAt) && out.component < 0;

        EEase defaultEase = EEase::InOutCubic;
        if (v.contains("ease")) defaultEase = Ease(v["ease"], path + ".ease");

        if (v.contains("keys"))
        {
            out.kind = ETrackKind::Keys;
            const json& keys = v["keys"];
            if (!keys.is_array() || keys.empty())
            {
                Error(path + ".keys", "expected [[time, value], ...]");
                return;
            }

            for (size_t i = 0; i < keys.size(); ++i)
            {
                const std::string keyPath = path + ".keys[" + std::to_string(i) + "]";
                const json& k = keys[i];
                // Compact form for vector values: [time, x, y, z] instead of [time, [x, y, z]]
                const bool compact = isVector && k.is_array() && k.size() >= 4 && k[1].is_number();
                const size_t size = compact ? k.size() - 2 : k.size();
                if (isVector && k.is_array() && k.size() == 3 && k[1].is_number() && k[2].is_number())
                {
                    Error(keyPath, "expected [time, x, y, z] (three numbers after the time)");
                    continue;
                }
                if (!k.is_array() || size < 2 || size > 3)
                {
                    Error(keyPath, isVector ? "expected [time, [x, y, z]], [time, x, y, z] or either with an \"ease\" at the end"
                                            : "expected [time, value] or [time, value, \"ease\"]");
                    continue;
                }

                FKey key;
                key.ease = defaultEase;
                if (!Number(k[0], keyPath + "[0]", key.time)) continue;
                const json value = compact ? json::array({ k[1], k[2], k[3] }) : k[1];
                if (!Value(value, compact ? keyPath : keyPath + "[1]", isVector, out.property, key.value)) continue;
                if (k.size() == (compact ? 5u : 3u)) key.ease = Ease(k.back(), keyPath + "[" + std::to_string(k.size() - 1) + "]");

                if (!out.keys.empty() && key.time < out.keys.back().time)
                    Error(keyPath, "key times must not go backwards");

                out.keys.push_back(key);
            }

            // Looping: "repeat": true plays the key range back to back, "period" sets the loop length
            bool repeat = false;
            if (v.contains("repeat")) Bool(v["repeat"], path + ".repeat", repeat);
            const double span = out.keys.empty() ? 0.0 : out.keys.back().time - out.keys.front().time;
            if (v.contains("period"))
            {
                if (Number(v["period"], path + ".period", out.repeatPeriod) && out.repeatPeriod < span - 1e-9)
                    Error(path + ".period", std::format("must be at least the key range ({:g} s)", span));
                if (v.contains("repeat") && !repeat)
                    Error(path + ".repeat", "\"period\" repeats the keys, so \"repeat\": false contradicts it");
            }
            else if (repeat)
            {
                out.repeatPeriod = span;
                if (span <= 0.0) Error(path + ".repeat", "needs keys at two or more different times");
            }
        }
        else if (v.contains("spin"))
        {
            out.kind = ETrackKind::Spin;
            if (out.property != EProperty::Rotation || out.component >= 0)
                Error(path + ".target", "\"spin\" needs a whole \"rotation\" target");
            Vector(v["spin"], path + ".spin", out.spinRate);
        }
        else if (v.contains("wave"))
        {
            out.kind = ETrackKind::Wave;
            const json& w = v["wave"];
            if (!w.is_object()) { Error(path + ".wave", "expected {\"amplitude\", \"period\", \"phase\"}"); return; }
            CheckKeys(w, path + ".wave", { "amplitude", "period", "phase" });

            if (w.contains("amplitude"))
            {
                if (isVector)
                {
                    out.waveIsVector = true;
                    Scale(w["amplitude"], path + ".wave.amplitude", out.waveAmplitudeVec);
                }
                else
                    Number(w["amplitude"], path + ".wave.amplitude", out.waveAmplitude);
            }
            else
                Error(path + ".wave", "missing \"amplitude\"");

            if (w.contains("period")) Number(w["period"], path + ".wave.period", out.wavePeriod);
            if (w.contains("phase"))  Number(w["phase"], path + ".wave.phase", out.wavePhase);
            if (out.wavePeriod == 0.0) Error(path + ".wave.period", "must not be 0");
            if (out.property == EProperty::Color || out.property == EProperty::Visible)
                Error(path + ".target", "\"wave\" works on position, rotation, scale, intensity and lookAt");
        }
        else if (v.contains("orbit"))
        {
            out.kind = ETrackKind::Orbit;
            if ((out.property != EProperty::Position && out.property != EProperty::LookAt) || out.component >= 0)
                Error(path + ".target", "\"orbit\" needs a whole \"position\" target");

            const json& o = v["orbit"];
            if (!o.is_object()) { Error(path + ".orbit", "expected an object"); return; }
            CheckKeys(o, path + ".orbit", { "center", "radius", "height", "period", "startAngle" });

            if (o.contains("center"))     Vector(o["center"], path + ".orbit.center", out.orbitCenter);
            if (o.contains("radius"))     Number(o["radius"], path + ".orbit.radius", out.orbitRadius);
            if (o.contains("height"))     Number(o["height"], path + ".orbit.height", out.orbitHeight);
            if (o.contains("period"))     Number(o["period"], path + ".orbit.period", out.orbitPeriod);
            if (o.contains("startAngle")) Number(o["startAngle"], path + ".orbit.startAngle", out.orbitStartAngle);
            if (out.orbitPeriod == 0.0) Error(path + ".orbit.period", "must not be 0");
        }
        else if (v.contains("follow"))
        {
            out.kind = ETrackKind::Follow;
            if (!wholePosition)
                Error(path + ".target", "\"follow\" needs a whole \"position\" or \"lookAt\" target");

            const json& f = v["follow"];
            if (!f.is_object()) { Error(path + ".follow", "expected {\"object\", \"offset\", \"smoothing\"}"); return; }
            CheckKeys(f, path + ".follow", { "object", "offset", "smoothing" });

            if (!f.contains("object")) Error(path + ".follow", "missing \"object\" (the id to follow)");
            else if (String(f["object"], path + ".follow.object", out.followId))
            {
                if (!objects.contains(out.followId)) Error(path + ".follow.object", "no object with id \"" + out.followId + "\"");
                else if (out.followId == out.objectId) Error(path + ".follow.object", "an object can't follow itself");
            }
            if (f.contains("offset"))    Vector(f["offset"], path + ".follow.offset", out.followOffset);
            if (f.contains("smoothing") && Number(f["smoothing"], path + ".follow.smoothing", out.followSmoothing)
                && (out.followSmoothing < 0.0 || out.followSmoothing > 5.0))
                Error(path + ".follow.smoothing", "must be between 0 (rigid) and 5 seconds");
        }
        else if (v.contains("path"))
        {
            out.kind = ETrackKind::Path;
            if (!wholePosition)
                Error(path + ".target", "\"path\" needs a whole \"position\" or \"lookAt\" target");

            const json& p = v["path"];
            if (!p.is_object()) { Error(path + ".path", "expected {\"points\": [[x, y, z], ...], ...}"); return; }
            CheckKeys(p, path + ".path", { "points", "curve", "start", "duration", "loop", "face", "ease" });

            if (!p.contains("points") || !p["points"].is_array())
                Error(path + ".path.points", "expected [[x, y, z], ...]");
            else
                for (size_t i = 0; i < p["points"].size(); ++i)
                {
                    Vec3 point;
                    if (Vector(p["points"][i], path + ".path.points[" + std::to_string(i) + "]", point))
                        out.pathPoints.push_back(point);
                }

            if (p.contains("curve"))
            {
                std::string curve;
                if (String(p["curve"], path + ".path.curve", curve))
                {
                    if (curve == "smooth")      out.pathCurve = ECurve::Smooth;
                    else if (curve == "linear") out.pathCurve = ECurve::Linear;
                    else if (curve == "bezier") out.pathCurve = ECurve::Bezier;
                    else Error(path + ".path.curve", "unknown \"" + curve + "\" (expected smooth, linear, bezier)");
                }
            }
            if (p.contains("start"))    Number(p["start"], path + ".path.start", out.pathStart);
            if (p.contains("duration") && Number(p["duration"], path + ".path.duration", out.pathDuration) && out.pathDuration <= 0.0)
                Error(path + ".path.duration", "must be more than 0");
            if (p.contains("loop"))     Bool(p["loop"], path + ".path.loop", out.pathLoop);
            if (p.contains("face") && Bool(p["face"], path + ".path.face", out.pathFace) && out.pathFace && out.property == EProperty::LookAt)
                Error(path + ".path.face", "only applies to a \"position\" path");
            if (p.contains("ease"))     out.pathEase = Ease(p["ease"], path + ".path.ease");

            const size_t n = out.pathPoints.size();
            if (n < 2)
                Error(path + ".path.points", "needs at least 2 points");
            else if (out.pathCurve == ECurve::Bezier && (out.pathLoop ? n % 3 != 0 : (n - 1) % 3 != 0))
                Error(path + ".path.points", out.pathLoop
                    ? "a looping bezier needs 3 points per segment: point, handle, handle, point, handle, handle, ..."
                    : "a bezier needs 3n + 1 points: point, handle, handle, point, ...");
        }
        else if (v.contains("bounce"))
        {
            out.kind = ETrackKind::Bounce;
            if (out.property != EProperty::Position || out.component != 2)
                Error(path + ".target", "\"bounce\" needs a \"position.z\" target");

            const json& b = v["bounce"];
            if (!b.is_object()) { Error(path + ".bounce", "expected {\"height\", \"period\", ...}"); return; }
            CheckKeys(b, path + ".bounce", { "height", "period", "restitution", "squash", "start" });

            if (b.contains("height"))      Number(b["height"], path + ".bounce.height", out.bounceHeight);
            if (b.contains("period"))      Number(b["period"], path + ".bounce.period", out.bouncePeriod);
            if (b.contains("restitution")) Number(b["restitution"], path + ".bounce.restitution", out.bounceRestitution);
            if (b.contains("squash"))      Number(b["squash"], path + ".bounce.squash", out.bounceSquash);
            if (b.contains("start"))       Number(b["start"], path + ".bounce.start", out.bounceStart);
            if (out.bouncePeriod <= 0.0) Error(path + ".bounce.period", "must be more than 0");
            if (out.bounceRestitution <= 0.0 || out.bounceRestitution > 1.0) Error(path + ".bounce.restitution", "must be more than 0 and at most 1");
            if (out.bounceSquash < 0.0 || out.bounceSquash > 0.6) Error(path + ".bounce.squash", "must be between 0 and 0.6");
        }
    }

    void Walk(const json& w, const std::string& path, FTrackDesc& out, const std::map<std::string, const FObjectDesc*>& objects)
    {
        if (!w.is_object()) { Error(path, "expected {\"legs\": [\"hipL\", \"hipR\"], ...}"); return; }
        CheckKeys(w, path, { "legs", "knees", "feet", "arms", "period", "swing", "kneeBend", "armSwing", "bob", "start", "end" });

        // Joint pairs, left then right
        auto Pair = [&](const char* key, std::array<std::string, 2>& joints)
        {
            if (!w.contains(key)) return;
            const json& j = w[key];
            const std::string jointPath = path + "." + key;
            if (!j.is_array() || j.size() != 2 || !j[0].is_string() || !j[1].is_string())
            {
                Error(jointPath, "expected [\"left id\", \"right id\"]");
                return;
            }
            for (int i = 0; i < 2; ++i)
            {
                joints[i] = j[i].get<std::string>();
                if (!objects.contains(joints[i])) Error(jointPath, "no object with id \"" + joints[i] + "\"");
            }
        };
        Pair("legs", out.walkLegs);
        Pair("knees", out.walkKnees);
        Pair("feet", out.walkFeet);
        Pair("arms", out.walkArms);
        if (out.walkLegs[0].empty() && out.walkArms[0].empty())
            Error(path, "needs \"legs\" and/or \"arms\" (pairs of joint ids, left then right)");

        if (w.contains("period"))   Number(w["period"], path + ".period", out.walkPeriod);
        if (w.contains("swing"))    Number(w["swing"], path + ".swing", out.walkSwing);
        if (w.contains("kneeBend")) Number(w["kneeBend"], path + ".kneeBend", out.walkKneeBend);
        if (w.contains("armSwing")) Number(w["armSwing"], path + ".armSwing", out.walkArmSwing);
        if (w.contains("bob"))      Number(w["bob"], path + ".bob", out.walkBob);
        if (w.contains("start"))    Number(w["start"], path + ".start", out.walkStart);
        if (w.contains("end"))      Number(w["end"], path + ".end", out.walkEnd);
        if (out.walkPeriod <= 0.0) Error(path + ".period", "must be more than 0");
        if (out.walkEnd >= 0.0 && out.walkEnd <= out.walkStart) Error(path + ".end", "must be after \"start\"");
    }

    void Environment(const json& v, const std::string& path, FEnvironmentDesc& out)
    {
        if (!v.is_object()) { Error(path, "expected an object"); return; }
        CheckKeys(v, path, { "sky", "ground", "ambient", "fog", "exposure" });

        if (v.contains("sky"))
        {
            const json& s = v["sky"];
            if (s.is_array() && s.size() == 3 && s[0].is_string())
            {
                Color(s[0], path + ".sky[0]", out.skyTop);
                Color(s[1], path + ".sky[1]", out.skyHorizon);
                Color(s[2], path + ".sky[2]", out.skyBottom);
            }
            else if (s.is_string())
            {
                // One color: a flat studio background
                Color(s, path + ".sky", out.skyTop);
                out.skyHorizon = out.skyBottom = out.skyTop;
            }
            else
                Error(path + ".sky", "expected \"#color\" or [\"#top\", \"#horizon\", \"#bottom\"]");
        }

        if (v.contains("ground"))
        {
            const json& g = v["ground"];
            if (g.is_boolean())
                out.hasGround = g.get<bool>();
            else if (g.is_string())
                Color(g, path + ".ground", out.groundColor);
            else if (g.is_object())
            {
                CheckKeys(g, path + ".ground", { "color", "height" });
                if (g.contains("color"))  Color(g["color"], path + ".ground.color", out.groundColor);
                if (g.contains("height")) Number(g["height"], path + ".ground.height", out.groundHeight);
            }
            else
                Error(path + ".ground", "expected false, \"#color\" or {\"color\", \"height\"}");
        }

        if (v.contains("ambient"))  Number(v["ambient"], path + ".ambient", out.ambient);
        if (v.contains("exposure")) Number(v["exposure"], path + ".exposure", out.exposure);

        if (v.contains("fog"))
        {
            const json& f = v["fog"];
            if (f.is_boolean())
                out.fog = f.get<bool>();
            else if (f.is_object())
            {
                CheckKeys(f, path + ".fog", { "start", "end" });
                if (f.contains("start")) Number(f["start"], path + ".fog.start", out.fogStart);
                if (f.contains("end"))   Number(f["end"], path + ".fog.end", out.fogEnd);
            }
            else
                Error(path + ".fog", "expected false or {\"start\", \"end\"}");
        }
    }

    void Output(const json& v, const std::string& path, FOutputDesc& out)
    {
        if (!v.is_object()) { Error(path, "expected an object"); return; }
        CheckKeys(v, path, { "width", "height", "fps", "duration" });

        if (v.contains("width"))    Int(v["width"], path + ".width", out.width, 16, 4096);
        if (v.contains("height"))   Int(v["height"], path + ".height", out.height, 16, 4096);
        if (v.contains("fps"))      Int(v["fps"], path + ".fps", out.fps, 1, 120);
        if (v.contains("duration")) Number(v["duration"], path + ".duration", out.duration);

        if (out.duration < 0.0 || out.duration > 120.0)
            Error(path + ".duration", "must be between 0 (still image) and 120 seconds");
    }

private:
    std::vector<std::string>& errors;
    std::filesystem::path baseDir;
};

}

bool SceneParser::Parse(const std::filesystem::path& file, FSceneDesc& outScene, const std::filesystem::path& baseDir)
{
    errors.clear();

    std::ifstream stream(file);
    if (!stream)
    {
        errors.push_back("can't open scene file: " + file.string());
        return false;
    }

    json root;
    try
    {
        root = json::parse(stream, nullptr, true, /*ignore_comments*/ true);
    }
    catch (const json::parse_error& e)
    {
        errors.push_back(std::string("invalid JSON: ") + e.what());
        return false;
    }

    Reader r(errors, baseDir.empty() ? file.parent_path() : baseDir);

    if (!root.is_object())
    {
        r.Error("scene", "expected a JSON object");
        return false;
    }

    r.CheckKeys(root, "scene", { "version", "output", "environment", "materials", "objects", "animations" });

    if (root.contains("version") && root["version"] != 1)
        r.Error("version", "only version 1 is supported");

    if (root.contains("output"))      r.Output(root["output"], "output", outScene.output);
    if (root.contains("environment")) r.Environment(root["environment"], "environment", outScene.environment);

    if (root.contains("materials"))
    {
        const json& materials = root["materials"];
        if (!materials.is_object())
            r.Error("materials", "expected {\"name\": {...}}");
        else
            for (const auto& [name, value] : materials.items())
                r.Material(value, "materials." + name, outScene.materials[name]);
    }

    if (!root.contains("objects") || !root["objects"].is_array())
    {
        r.Error("objects", "missing \"objects\" array");
        return false;
    }

    const json& objects = root["objects"];
    for (size_t i = 0; i < objects.size(); ++i)
    {
        FObjectDesc object;
        r.Object(objects[i], "objects[" + std::to_string(i) + "]", object, outScene.materials);
        outScene.objects.push_back(object);
    }

    // Ids, parents, look-at targets
    std::map<std::string, const FObjectDesc*> byId;
    int cameras = 0;
    for (size_t i = 0; i < outScene.objects.size(); ++i)
    {
        const FObjectDesc& o = outScene.objects[i];
        if (o.id.empty()) continue;
        if (byId.contains(o.id))
            r.Error("objects[" + std::to_string(i) + "].id", "duplicate id \"" + o.id + "\"");
        byId[o.id] = &o;
        cameras += o.kind == EObjectKind::Camera;
    }

    if (cameras != 1)
        r.Error("objects", "needs exactly one camera object, found " + std::to_string(cameras));

    for (size_t i = 0; i < outScene.objects.size(); ++i)
    {
        const FObjectDesc& o = outScene.objects[i];
        const std::string path = "objects[" + std::to_string(i) + "]";

        if (!o.parent.empty())
        {
            if (!byId.contains(o.parent))
                r.Error(path + ".parent", "no object with id \"" + o.parent + "\"");
            else
            {
                // Walk up the chain to reject cycles
                std::string cur = o.parent;
                for (size_t depth = 0; !cur.empty() && byId.contains(cur); ++depth)
                {
                    if (cur == o.id || depth > outScene.objects.size())
                    {
                        r.Error(path + ".parent", "parent chain loops back to \"" + o.id + "\"");
                        break;
                    }
                    cur = byId[cur]->parent;
                }
            }
        }

        if (!o.lookAt.objectId.empty() && !byId.contains(o.lookAt.objectId))
            r.Error(path + ".lookAt", "no object with id \"" + o.lookAt.objectId + "\"");
        if (o.lookAt.objectId == o.id && !o.id.empty())
            r.Error(path + ".lookAt", "an object can't look at itself");
    }

    if (root.contains("animations"))
    {
        const json& tracks = root["animations"];
        if (!tracks.is_array())
            r.Error("animations", "expected an array");
        else
            for (size_t i = 0; i < tracks.size(); ++i)
            {
                FTrackDesc track;
                r.Track(tracks[i], "animations[" + std::to_string(i) + "]", track, byId);
                outScene.tracks.push_back(track);
            }
    }

    return errors.empty();
}

}
