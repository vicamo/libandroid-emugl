/*
* Copyright (C) 2017 The Android Open Source Project
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

const char kDrawTexOESGles2_vshader[] = R"(
precision highp float;
attribute highp vec3 pos;
attribute highp vec2 texcoord;
varying highp vec2 texcoord_varying;
void main() {
    gl_Position = vec4(pos.x, pos.y, pos.z, 1.0);
    texcoord_varying = texcoord;
}
)";

const char kDrawTexOESGles2_fshader[] = R"(
precision highp float;
uniform sampler2D tex_sampler;
varying highp vec2 texcoord_varying;
void main() {
    gl_FragColor = texture2D(tex_sampler, texcoord_varying);
}
)";

const char kDrawTexOESCore_vshader[] = R"(#version 330 core
layout(location = 0) in vec3 pos;
layout(location = 1) in vec2 texcoord;
out vec2 texcoord_varying;
void main() {
    gl_Position = vec4(pos.x, pos.y, pos.z, 1.0);
    texcoord_varying = texcoord;
}
)";

const char kDrawTexOESCore_fshader[] = R"(#version 330 core
uniform sampler2D tex_sampler;
in vec2 texcoord_varying;
out vec4 frag_color;
void main() {
    frag_color = texture(tex_sampler, texcoord_varying);
}
)";

// version, flat,
const char kGeometryDrawVShaderSrcTemplateCore[] = R"(%s
layout(location = 0) in vec4 pos;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 color;
layout(location = 3) in float pointsize;
layout(location = 4) in vec4 texcoord;

uniform mat4 projection;
uniform mat4 modelview;
uniform mat4 modelview_invtr;
uniform mat4 texture_matrix;

uniform bool enable_rescale_normal;
uniform bool enable_normalize;

out vec4 pos_varying;
out vec3 normal_varying;
%s out vec4 color_varying;
out float pointsize_varying;
out vec4 texcoord_varying;

void main() {

    pos_varying = modelview * pos;
    mat3 mvInvTr3 = mat3(modelview_invtr);
    normal_varying = mvInvTr3 * normal;

    if (enable_rescale_normal) {
        float rescale = 1.0;
        vec3 rescaleVec = vec3(mvInvTr3[2]);
        float len = length(rescaleVec);
        if (len > 0.0) {
            rescale = 1.0 / len;
        }
        normal_varying *= rescale;
    }

    if (enable_normalize) {
        normal_varying = normalize(normal_varying);
    }

    color_varying = color;
    pointsize_varying = pointsize;
    texcoord_varying = texture_matrix * texcoord;

    gl_Position = projection * modelview * pos;
}
)";

// version, flat,
const char kGeometryDrawFShaderSrcTemplateCore[] = R"(%s
// Defines
#define kMaxLights 8

#define kModulate 0x2100
#define kCombine 0x8570
#define kReplace 0x1E01

#define kAlpha 0x1906
#define kRGB 0x1907
#define kRGBA 0x1908
#define kLuminance 0x1909
#define kLuminanceAlpha 0x190A

precision highp float;
uniform sampler2D tex_sampler;
uniform samplerCube tex_cube_sampler;
uniform bool enable_textures;
uniform bool enable_lighting;
uniform bool enable_color_material;
uniform bool enable_fog;
uniform bool enable_reflection_map;

uniform int texture_env_mode;
uniform int texture_format;

// material (front+back)
uniform vec4 material_ambient;
uniform vec4 material_diffuse;
uniform vec4 material_specular;
uniform vec4 material_emissive;
uniform float material_specular_exponent;

// lights
uniform vec4 light_model_scene_ambient;
uniform bool light_model_two_sided;

uniform bool light_enables[kMaxLights];
uniform vec4 light_ambients[kMaxLights];
uniform vec4 light_diffuses[kMaxLights];
uniform vec4 light_speculars[kMaxLights];
uniform vec4 light_positions[kMaxLights];
uniform vec3 light_directions[kMaxLights];
uniform float light_spotlight_exponents[kMaxLights];
uniform float light_spotlight_cutoff_angles[kMaxLights];
uniform float light_attenuation_consts[kMaxLights];
uniform float light_attenuation_linears[kMaxLights];
uniform float light_attenuation_quadratics[kMaxLights];

in vec4 pos_varying;
in vec3 normal_varying;
%s in vec4 color_varying;
in float pointsize_varying;
in vec4 texcoord_varying;

out vec4 frag_color;

void main() {
    if (enable_textures) {
        vec4 textureColor;
        if (enable_reflection_map) {
            textureColor = texture(tex_cube_sampler, reflect(pos_varying.xyz, normalize(normal_varying)));
            frag_color = textureColor;
        } else {
            textureColor = texture(tex_sampler, texcoord_varying.xy);
            switch (texture_env_mode) {
            case kReplace:
                switch (texture_format) {
                case kAlpha:
                    frag_color.rgb = color_varying.rgb;
                    frag_color.a = textureColor.a;
                    break;
                case kRGBA:
                case kLuminanceAlpha:
                    frag_color.rgba = textureColor.rgba;
                    break;
                case kRGB:
                case kLuminance:
                default:
                    frag_color.rgb = textureColor.rgb;
                    frag_color.a = color_varying.a;
                    break;
                }
                break;
            case kCombine:
            case kModulate:
            default:
                switch (texture_format) {
                case kAlpha:
                    frag_color.rgb = color_varying.rgb;
                    frag_color.a = color_varying.a * textureColor.a;
                    break;
                case kRGBA:
                case kLuminanceAlpha:
                    frag_color.rgba = color_varying.rgba * textureColor.rgba;
                    break;
                case kRGB:
                case kLuminance:
                default:
                    frag_color.rgb = color_varying.rgb * textureColor.rgb;
                    frag_color.a = color_varying.a;
                    break;
                }
                break;
            }
        }
    } else {
        frag_color = color_varying;
    }

    if (!enable_lighting) return;

    vec4 materialAmbientActual = material_ambient;
    vec4 materialDiffuseActual = material_diffuse;

    if (enable_color_material) {
        materialAmbientActual = color_varying;
        materialDiffuseActual = color_varying;
    }

    vec4 lit = material_emissive +
               materialAmbientActual * light_model_scene_ambient;

    for (int i = 0; i < kMaxLights; i++) {

        if (!light_enables[i]) continue;

        vec4 lightAmbient = light_ambients[i];
        vec4 lightDiffuse = light_diffuses[i];
        vec4 lightSpecular = light_speculars[i];
        vec4 lightPos = light_positions[i];
        vec3 lightDir = light_directions[i];
        float attConst = light_attenuation_consts[i];
        float attLinear = light_attenuation_linears[i];
        float attQuadratic = light_attenuation_quadratics[i];
        float spotAngle = light_spotlight_cutoff_angles[i];
        float spotExponent = light_spotlight_exponents[i];

        vec3 toLight = (lightPos - pos_varying).xyz;
        float lightDist = length(toLight);
        vec3 norm = normalize(normal_varying);
        vec3 h = toLight + vec3(0.0, 0.0, 1.0);
        float ndotL = clamp(dot(norm, normalize(toLight)), 0.0, 1.0);
        float ndoth = clamp(dot(norm, normalize(h)), 0.0, 1.0);

        float specAtt;

        if (ndotL != 0.0) {
            specAtt = 1.0;
        } else {
            specAtt = 0.0;
        }

        float att;

        if (lightPos.w != 0.0) {
            att = 1.0 / (attConst + attLinear * lightDist +
                                    attQuadratic * lightDist * lightDist);
        } else {
            att = 1.0;
        }

        float spot;

        float spotAngleCos = cos(radians(spotAngle));
        vec3 toSurfaceDir = -normalize(toLight);
        float spotDot = clamp(dot(toSurfaceDir, normalize(lightDir)), 0.0, 1.0);

        if (spotAngle == 180.0 || lightPos.w == 0.0) {
            spot = 1.0;
        } else {
            if (spotDot < spotAngleCos) {
                spot = 0.0;
            } else {
                spot = pow(spotDot, spotExponent);
            }
        }

        vec4 contrib = materialAmbientActual * lightAmbient;
        contrib += ndotL * materialDiffuseActual * lightDiffuse;
        contrib += specAtt * pow(ndoth, material_specular_exponent) *
                             material_specular * lightSpecular;
        contrib *= att * spot;
        lit += contrib;
    }

    frag_color = lit;
}
)";
