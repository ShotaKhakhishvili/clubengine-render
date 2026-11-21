#version 460 core

// Scene shader for clubengine-render. Same inputs as the engine's default.frag, plus:
//  - u_Mode 0: lit surface with soft sky/ground ambient, rim light, fog and tonemapping
//  - u_Mode 1: gradient sky (drawn on a big sphere around the camera)
//  - u_Mode 2: ground, lit like mode 0 and slightly darkened right under objects (soft contact occlusion)
//  - u_Mode 3: projected shadow, drawn exactly like the ground but without (most of) the sun

in vec3 v_Normal;
in vec4 v_Tangent;
in vec4 v_Color;
in vec2 v_TexUV;
in vec3 v_FragPos;

out vec4 FragColor;

uniform vec4 u_BaseColor;
uniform float u_Metallic;
uniform float u_Roughness;
uniform vec3 u_Emissive;
uniform float u_NormalScale;
uniform float u_OcclusionStrength;
uniform float u_AlphaCutoff;
uniform bool u_Unlit;

uniform sampler2D u_BaseColorTexture;
uniform sampler2D u_MetallicRoughnessTexture;
uniform sampler2D u_NormalTexture;
uniform sampler2D u_OcclusionTexture;
uniform sampler2D u_EmissiveTexture;

uniform bool u_HasBaseColorTexture;
uniform bool u_HasMetallicRoughnessTexture;
uniform bool u_HasNormalTexture;
uniform bool u_HasOcclusionTexture;
uniform bool u_HasEmissiveTexture;

uniform int u_Mode;
uniform float u_Opacity;
uniform vec3 u_SkyTop;
uniform vec3 u_SkyHorizon;
uniform vec3 u_SkyBottom;
uniform vec3 u_GroundColor;
uniform float u_Ambient;
uniform float u_Exposure;
uniform bool u_FogEnabled;
uniform float u_FogStart;
uniform float u_FogEnd;
uniform float u_ShadowStrength;

// Contact shadows on the ground: xy = center, z = radius, w = strength
const int MAX_BLOBS = 64;
uniform int u_BlobCount;
uniform vec4 u_Blobs[MAX_BLOBS];

layout(std140, binding = 0) uniform FrameData
{
    mat4 view;
    mat4 proj;
    vec4 viewOrigin;
    ivec4 lightCounts;
} frameData;

struct DirectionalLight { vec4 direction; vec4 color; };
struct LocalLight { vec4 positionInvRadius; vec4 color; vec4 direction; vec4 spotAngles; };

layout(std430, binding = 1) readonly buffer DirectionalLights { DirectionalLight directionalLights[]; };
layout(std430, binding = 2) readonly buffer LocalLights       { LocalLight localLights[]; };

const float INV_EIGHT_PI = 0.0397887;

struct SurfaceData
{
    vec3 diffuseColor;
    vec3 specularColor;
    float shininess;
};

vec3 BlinnPhong(vec3 N, vec3 V, vec3 L, vec3 lightColor, SurfaceData surface)
{
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0)
        return vec3(0.0);

    vec3 H = normalize(L + V);
    float normalization = (surface.shininess + 8.0) * INV_EIGHT_PI;
    vec3 specular = surface.specularColor * pow(max(dot(N, H), 0.0), surface.shininess) * normalization;

    return (surface.diffuseColor + specular) * NdotL * lightColor;
}

vec3 GetNormal()
{
    vec3 N = normalize(v_Normal);
    if (!gl_FrontFacing)
        N = -N;

    if (!u_HasNormalTexture)
        return N;

    vec3 T = normalize(v_Tangent.xyz - N * dot(N, v_Tangent.xyz));
    vec3 B = cross(N, T) * v_Tangent.w;
    vec3 tangentNormal = texture(u_NormalTexture, v_TexUV).xyz * 2.0 - 1.0;
    tangentNormal.xy *= u_NormalScale;
    return normalize(mat3(T, B, N) * tangentNormal);
}

vec3 SkyColor(vec3 dir)
{
    if (dir.z >= 0.0)
        return mix(u_SkyHorizon, u_SkyTop, smoothstep(0.0, 0.55, dir.z));
    return mix(u_SkyHorizon, u_SkyBottom, smoothstep(0.0, 0.25, -dir.z));
}

// Keeps colors as authored up to 0.75, then rolls highlights off smoothly instead of clipping
vec3 Tonemap(vec3 x)
{
    x *= u_Exposure;
    vec3 over = max(x - 0.75, 0.0);
    return min(x, 0.75) + 0.25 * (1.0 - exp(-over / 0.25));
}

float ContactShadow()
{
    float light = 1.0;
    for (int i = 0; i < u_BlobCount && i < MAX_BLOBS; ++i)
    {
        vec4 blob = u_Blobs[i];
        float d = length(v_FragPos.xy - blob.xy) / blob.z;   // 0 at the center, 1 at the edge
        float shape = exp(-3.0 * d * d) * (1.0 - smoothstep(0.7, 1.0, d));
        light *= 1.0 - blob.w * shape;
    }
    return light;
}

vec3 ToDisplay(vec3 linearColor)
{
    return pow(clamp(linearColor, 0.0, 1.0), vec3(1.0 / 2.2));
}

void main()
{
    if (u_Mode == 1)
    {
        vec3 dir = normalize(v_FragPos - frameData.viewOrigin.xyz);
        FragColor = vec4(ToDisplay(SkyColor(dir)), 1.0);
        return;
    }

    bool isShadow = u_Mode == 3;
    bool isGround = u_Mode == 2 || isShadow;

    vec4 albedo = isShadow ? vec4(u_GroundColor, 1.0) : u_BaseColor * v_Color;
    if (u_HasBaseColorTexture)
        albedo *= texture(u_BaseColorTexture, v_TexUV);

    if (albedo.a < u_AlphaCutoff)
        discard;

    vec3 emissive = u_Emissive;
    if (u_HasEmissiveTexture)
        emissive *= texture(u_EmissiveTexture, v_TexUV).rgb;

    vec3 color;

    if (u_Unlit)
    {
        color = albedo.rgb + emissive;
    }
    else
    {
        float metallic = u_Metallic;
        float roughness = u_Roughness;
        if (u_HasMetallicRoughnessTexture)
        {
            vec4 mr = texture(u_MetallicRoughnessTexture, v_TexUV);
            roughness *= mr.g;
            metallic *= mr.b;
        }

        float occlusion = 1.0;
        if (u_HasOcclusionTexture)
            occlusion = mix(1.0, texture(u_OcclusionTexture, v_TexUV).r, u_OcclusionStrength);

        SurfaceData surface;
        surface.diffuseColor = albedo.rgb * (1.0 - metallic);
        surface.specularColor = mix(vec3(0.04), albedo.rgb, metallic);
        surface.shininess = mix(256.0, 4.0, clamp(roughness, 0.0, 1.0));

        // The ground is a matte backdrop, a highlight on it reads as a stain
        if (isGround)
            surface.specularColor = vec3(0.0);

        // Ground and flattened shadows always face up (flattening scrambles the triangles' winding)
        vec3 N = isGround ? vec3(0.0, 0.0, 1.0) : GetNormal();
        vec3 V = normalize(frameData.viewOrigin.xyz - v_FragPos);

        // Soft ambient: sky color from above, light bounced off the ground from below
        vec3 skyAmbient = mix(u_SkyHorizon, u_SkyTop, 0.5);
        vec3 groundAmbient = u_GroundColor * 0.5;
        vec3 ambient = mix(groundAmbient, skyAmbient, N.z * 0.5 + 0.5) * u_Ambient;

        // Reflections: the sky, plus a bright studio softbox overhead so metals never go black
        vec3 R = reflect(-V, N);
        float softbox = smoothstep(0.1, 0.9, R.z);
        vec3 reflected = SkyColor(R) * (0.5 + u_Ambient) + vec3(softbox * 0.9);
        float fresnel = 0.04 + 0.96 * pow(1.0 - max(dot(N, V), 0.0), 5.0);
        vec3 specularReflect = mix(surface.specularColor, vec3(1.0), fresnel * (1.0 - metallic) * 0.5);
        vec3 lighting = surface.diffuseColor * ambient * occlusion
                      + specularReflect * reflected * (1.0 - roughness * 0.75) * occlusion * (0.25 + 0.75 * metallic);

        // Thin rim so silhouettes separate from the background
        float rim = pow(1.0 - max(dot(N, V), 0.0), 4.0);
        lighting += skyAmbient * rim * 0.25 * u_Ambient;

        // Shadows block the first sun. One flat strength: overlapping flattened triangles sit at the
        // same depth, so anything that varies per triangle would flicker.
        float sunVisible = isShadow ? 1.0 - u_ShadowStrength : 1.0;

        for (int i = 0; i < frameData.lightCounts.x; ++i)
        {
            DirectionalLight light = directionalLights[i];
            lighting += BlinnPhong(N, V, -light.direction.xyz, light.color.rgb, surface) * (i == 0 ? sunVisible : 1.0);
        }

        for (int i = 0; i < frameData.lightCounts.y; ++i)
        {
            LocalLight light = localLights[i];
            vec3 toLight = light.positionInvRadius.xyz - v_FragPos;
            float distSq = max(dot(toLight, toLight), 1e-4);
            vec3 L = toLight * inversesqrt(distSq);

            float distNorm = sqrt(distSq) * light.positionInvRadius.w;
            float window = clamp(1.0 - distNorm * distNorm * distNorm * distNorm, 0.0, 1.0);
            float attenuation = (window * window) / (distSq + 1.0);

            float spot = clamp((dot(-L, light.direction.xyz) - light.spotAngles.x) * light.spotAngles.y, 0.0, 1.0);
            spot *= spot;

            lighting += BlinnPhong(N, V, L, light.color.rgb, surface) * attenuation * spot;
        }

        if (isGround)
            lighting *= ContactShadow();

        color = lighting + emissive;
    }

    // Unlit colors are shown as given; lit ones go through the tonemapper
    vec3 display = u_Unlit ? color : Tonemap(color);

    if (u_FogEnabled)
    {
        float dist = length(v_FragPos - frameData.viewOrigin.xyz);
        float fog = smoothstep(u_FogStart, u_FogEnd, dist);
        display = mix(display, u_SkyHorizon, fog);
    }

    FragColor = vec4(ToDisplay(display), albedo.a);
}
