#include "SceneParser.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
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

const std::set<std::string> kBuiltinMeshes = { "cube", "sphere", "cylinder", "cone", "plane" };

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
                             "scale", "parent", "lookAt", "shadow", "visible" });

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
                        Error(path + ".mesh", "\"" + mesh + "\" is not a built-in mesh (cube, sphere, cylinder, cone, plane) or a .glb/.gltf file");
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

        if (v.contains("color"))
        {
            // Shortcut: a color on top of the named/inline material (or the default one)
            FMaterialDesc m = out.inlineMaterial.value_or(
                materials.contains(out.materialName) ? materials.at(out.materialName) : FMaterialDesc{});
            Color(v["color"], path + ".color", m.color);
            out.inlineMaterial = m;
            out.materialName.clear();
        }

        if (out.kind != EObjectKind::Mesh && (v.contains("material") || v.contains("color") || v.contains("shadow")))
            Error(path, "\"material\", \"color\" and \"shadow\" only apply to mesh objects (lights use light.color, groups have no look)");

        if (v.contains("position")) Vector(v["position"], path + ".position", out.position);
        if (v.contains("rotation")) Vector(v["rotation"], path + ".rotation", out.rotation);
        if (v.contains("scale"))    Scale(v["scale"], path + ".scale", out.scale);
        if (v.contains("parent"))   String(v["parent"], path + ".parent", out.parent);
        if (v.contains("lookAt"))   LookAt(v["lookAt"], path + ".lookAt", out.lookAt);
        if (v.contains("visible"))  Bool(v["visible"], path + ".visible", out.visible);

        out.shadow = out.kind == EObjectKind::Mesh && out.mesh != "plane";
        if (v.contains("shadow"))   Bool(v["shadow"], path + ".shadow", out.shadow);
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
        CheckKeys(v, path, { "target", "keys", "ease", "spin", "wave", "orbit" });

        std::string target;
        if (!v.contains("target")) { Error(path, "missing \"target\" (like \"box.position\" or \"box.position.z\")"); return; }
        if (!String(v["target"], path + ".target", target)) return;

        // "id.property" or "id.property.x"
        std::vector<std::string> parts;
        std::stringstream ss(target);
        for (std::string part; std::getline(ss, part, '.');) parts.push_back(part);

        if (parts.size() < 2 || parts.size() > 3)
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

        const int generators = static_cast<int>(v.contains("keys")) + v.contains("spin") + v.contains("wave") + v.contains("orbit");
        if (generators != 1)
        {
            Error(path, "needs exactly one of \"keys\", \"spin\", \"wave\" or \"orbit\"");
            return;
        }

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
                if (!k.is_array() || k.size() < 2 || k.size() > 3)
                {
                    Error(keyPath, "expected [time, value] or [time, value, \"ease\"]");
                    continue;
                }

                FKey key;
                key.ease = defaultEase;
                if (!Number(k[0], keyPath + "[0]", key.time)) continue;
                if (!Value(k[1], keyPath + "[1]", isVector, out.property, key.value)) continue;
                if (k.size() == 3) key.ease = Ease(k[2], keyPath + "[2]");

                if (!out.keys.empty() && key.time < out.keys.back().time)
                    Error(keyPath, "key times must not go backwards");

                out.keys.push_back(key);
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

bool SceneParser::Parse(const std::filesystem::path& file, FSceneDesc& outScene)
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

    Reader r(errors, file.parent_path());

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
