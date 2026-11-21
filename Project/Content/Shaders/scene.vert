#version 460 core

// Scene vertex shader for clubengine-render: the engine's default vertex stage plus
// u_Mode 3, which flattens a mesh onto the ground along the sun direction (a projected shadow).

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec2 aTexUV;
layout(location = 4) in vec4 aTangent;

layout(std140, binding = 0) uniform FrameData
{
    mat4 view;
    mat4 proj;
    vec4 viewOrigin;
    ivec4 lightCounts;
} frameData;

uniform mat4 u_Model;

uniform int u_Mode;
uniform vec3 u_ShadowDir;        // direction the sun's light travels, z < 0
uniform float u_GroundHeight;

out vec3 v_Normal;
out vec4 v_Tangent;
out vec4 v_Color;
out vec2 v_TexUV;
out vec3 v_FragPos;

void main()
{
    v_FragPos = vec3(u_Model * vec4(aPos, 1.0));

    if (u_Mode == 3)
    {
        // Slide the point along the light ray down to just above the ground
        float height = max(v_FragPos.z - u_GroundHeight, 0.0);
        vec3 flattened = v_FragPos + u_ShadowDir * (height / max(-u_ShadowDir.z, 0.2));
        flattened.z = u_GroundHeight + 0.004;
        v_FragPos = flattened;
    }

    // Normals use the inverse-transpose, tangents follow the surface like positions
    mat3 model3 = mat3(u_Model);
    v_Normal = u_Mode == 3 ? vec3(0.0, 0.0, 1.0) : transpose(inverse(model3)) * aNormal;

    // A mirroring model matrix flips the bitangent, so the handedness flips with it
    float handedness = aTangent.w * sign(determinant(model3));
    v_Tangent = vec4(model3 * aTangent.xyz, handedness);

    v_Color = aColor;
    v_TexUV = aTexUV;

    gl_Position = frameData.proj * frameData.view * vec4(v_FragPos, 1.0);
}