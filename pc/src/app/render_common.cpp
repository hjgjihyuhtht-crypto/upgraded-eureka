#include "app/render_common.h"

#include <cmath>
#include <initializer_list>

#include "raymath.h"
#include "rlgl.h"

namespace k3d {

namespace {
#if defined(__ANDROID__)
// OpenGL ES 2.0 (GLSL ES 1.00): mesma iluminação e névoa da versão de PC.
const char *kVs = R"(#version 100
attribute vec3 vertexPosition;
attribute vec2 vertexTexCoord;
attribute vec3 vertexNormal;
attribute vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
varying vec2 fragTexCoord;
varying vec4 fragColor;
varying vec3 fragNormal;
varying vec3 fragPos;
void main() {
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));
    fragPos = vec3(matModel * vec4(vertexPosition, 1.0));
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

const char *kFs = R"(#version 100
precision mediump float;
varying vec2 fragTexCoord;
varying vec4 fragColor;
varying vec3 fragNormal;
varying vec3 fragPos;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 lightDir;
uniform vec3 viewPos;
uniform vec3 fogColor;
uniform float fogNear;
uniform float fogFar;
uniform float ambient;
void main() {
    vec4 base = texture2D(texture0, fragTexCoord) * colDiffuse * fragColor;
    vec3 n = normalize(fragNormal);
    float diff = max(dot(n, -normalize(lightDir)), 0.0);
    vec3 v = normalize(viewPos - fragPos);
    vec3 h = normalize(v - normalize(lightDir));
    float spec = pow(max(dot(n, h), 0.0), 24.0) * 0.25;
    float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0) * 0.25;
    vec3 c = base.rgb * (ambient + (1.0 - ambient) * diff) + vec3(spec + rim);
    float d = length(viewPos - fragPos);
    float f = clamp((d - fogNear) / max(0.001, fogFar - fogNear), 0.0, 1.0);
    gl_FragColor = vec4(mix(c, fogColor, f), base.a);
}
)";
#else
const char *kVs = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;
out vec3 fragPos;
void main() {
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));
    fragPos = vec3(matModel * vec4(vertexPosition, 1.0));
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

const char *kFs = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;
in vec3 fragPos;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 lightDir;
uniform vec3 viewPos;
uniform vec3 fogColor;
uniform float fogNear;
uniform float fogFar;
uniform float ambient;
out vec4 finalColor;
void main() {
    vec4 base = texture(texture0, fragTexCoord) * colDiffuse * fragColor;
    vec3 n = normalize(fragNormal);
    float diff = max(dot(n, -normalize(lightDir)), 0.0);
    vec3 v = normalize(viewPos - fragPos);
    vec3 h = normalize(v - normalize(lightDir));
    float spec = pow(max(dot(n, h), 0.0), 24.0) * 0.25;
    float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0) * 0.25;
    vec3 c = base.rgb * (ambient + (1.0 - ambient) * diff) + vec3(spec + rim);
    float d = length(viewPos - fragPos);
    float f = clamp((d - fogNear) / max(0.001, fogFar - fogNear), 0.0, 1.0);
    finalColor = vec4(mix(c, fogColor, f), base.a);
}
)";
#endif

Vector3 lastView{};

// Octaedro (cristal) com normais por face
Mesh genOctahedron() {
    const Vector3 v[6] = {{0, 1, 0}, {0, -1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
    const int f[8][3] = {{0, 4, 2}, {0, 2, 5}, {0, 5, 3}, {0, 3, 4}, {1, 2, 4}, {1, 5, 2}, {1, 3, 5}, {1, 4, 3}};
    Mesh m{};
    m.vertexCount = 24;
    m.triangleCount = 8;
    m.vertices = static_cast<float *>(MemAlloc(24 * 3 * sizeof(float)));
    m.normals = static_cast<float *>(MemAlloc(24 * 3 * sizeof(float)));
    m.texcoords = static_cast<float *>(MemAlloc(24 * 2 * sizeof(float)));
    for (int i = 0; i < 8; ++i) {
        const Vector3 a = v[f[i][0]], b = v[f[i][1]], c = v[f[i][2]];
        const Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
        const Vector3 tri[3] = {a, b, c};
        for (int k = 0; k < 3; ++k) {
            const int idx = i * 3 + k;
            m.vertices[idx * 3] = tri[k].x; m.vertices[idx * 3 + 1] = tri[k].y; m.vertices[idx * 3 + 2] = tri[k].z;
            m.normals[idx * 3] = n.x; m.normals[idx * 3 + 1] = n.y; m.normals[idx * 3 + 2] = n.z;
            m.texcoords[idx * 2] = 0; m.texcoords[idx * 2 + 1] = 0;
        }
    }
    UploadMesh(&m, false);
    return m;
}

void drawModelColored(Model &m, Matrix t, Color c) {
    m.transform = t;
    DrawModel(m, {0, 0, 0}, 1.f, c);
}
} // namespace

World3D &world3d() {
    static World3D w;
    if (!w.ok) w.init();
    return w;
}

void World3D::init() {
    lit = LoadShaderFromMemory(kVs, kFs);
    locLightDir = GetShaderLocation(lit, "lightDir");
    locViewPos = GetShaderLocation(lit, "viewPos");
    locFogColor = GetShaderLocation(lit, "fogColor");
    locFogNear = GetShaderLocation(lit, "fogNear");
    locFogFar = GetShaderLocation(lit, "fogFar");
    locAmbient = GetShaderLocation(lit, "ambient");
    lit.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(lit, "matModel");
    lit.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(lit, "matNormal");
    auto mk = [&](Mesh m) {
        Model md = LoadModelFromMesh(m);
        md.materials[0].shader = lit;
        return md;
    };
    sphere = mk(GenMeshSphere(1.f, 14, 18));
    cube = mk(GenMeshCube(1.f, 1.f, 1.f));
    cylinder = mk(GenMeshCylinder(1.f, 1.f, 16));
    cone = mk(GenMeshCone(1.f, 1.f, 14));
    torus = mk(GenMeshTorus(0.18f, 2.f, 12, 28));   // raio maior 1, tubo 0,18
    diamond = mk(genOctahedron());
    plane = mk(GenMeshPlane(1.f, 1.f, 1, 1));
    ok = true;
}

void World3D::shutdown() {
    if (!ok) return;
    for (Model *m : {&sphere, &cube, &cylinder, &cone, &torus, &diamond, &plane}) {
        m->materials[0].shader = Shader{rlGetShaderIdDefault(), rlGetShaderLocsDefault()};
        UnloadModel(*m);
    }
    UnloadShader(lit);
    ok = false;
}

void World3D::begin(const Camera3D &cam, Color fog, float fogNear, float fogFar, Vector3 lightDir, float ambient) {
    const float fc[3] = {fog.r / 255.f, fog.g / 255.f, fog.b / 255.f};
    const float vp[3] = {cam.position.x, cam.position.y, cam.position.z};
    const float ld[3] = {lightDir.x, lightDir.y, lightDir.z};
    SetShaderValue(lit, locFogColor, fc, SHADER_UNIFORM_VEC3);
    SetShaderValue(lit, locViewPos, vp, SHADER_UNIFORM_VEC3);
    SetShaderValue(lit, locLightDir, ld, SHADER_UNIFORM_VEC3);
    SetShaderValue(lit, locFogNear, &fogNear, SHADER_UNIFORM_FLOAT);
    SetShaderValue(lit, locFogFar, &fogFar, SHADER_UNIFORM_FLOAT);
    SetShaderValue(lit, locAmbient, &ambient, SHADER_UNIFORM_FLOAT);
    lastView = cam.position;
    BeginMode3D(cam);
}

void World3D::end() { EndMode3D(); }

void drawSphere(Vector3 p, float r, Color c) {
    drawModelColored(world3d().sphere, MatrixMultiply(MatrixScale(r, r, r), MatrixTranslate(p.x, p.y, p.z)), c);
}

void drawEllipsoid(Vector3 p, Vector3 rad, Color c) {
    drawModelColored(world3d().sphere, MatrixMultiply(MatrixScale(rad.x, rad.y, rad.z), MatrixTranslate(p.x, p.y, p.z)), c);
}

void drawBox(Vector3 p, Vector3 s, Color c) {
    drawModelColored(world3d().cube, MatrixMultiply(MatrixScale(s.x, s.y, s.z), MatrixTranslate(p.x, p.y, p.z)), c);
}

void drawCylinderY(Vector3 base, float r, float h, Color c) {
    drawModelColored(world3d().cylinder, MatrixMultiply(MatrixScale(r, h, r), MatrixTranslate(base.x, base.y, base.z)), c);
}

void drawCone(Vector3 base, float r, float h, Color c) {
    drawModelColored(world3d().cone, MatrixMultiply(MatrixScale(r, h, r), MatrixTranslate(base.x, base.y, base.z)), c);
}

void drawGem(Vector3 p, float size, float spin, Color c) {
    const Matrix m = MatrixMultiply(MatrixMultiply(MatrixScale(size * 0.7f, size, size * 0.7f), MatrixRotateY(spin)),
                                    MatrixTranslate(p.x, p.y, p.z));
    drawModelColored(world3d().diamond, m, c);
}

void drawTorus(Vector3 p, float radius, Vector3 axis, float angleDeg, Color c) {
    const Matrix m = MatrixMultiply(MatrixMultiply(MatrixScale(radius, radius, radius),
                                                   MatrixRotate(axis, angleDeg * DEG2RAD)),
                                    MatrixTranslate(p.x, p.y, p.z));
    drawModelColored(world3d().torus, m, c);
}

void drawSky(int w, int h, Color top, Color bottom) { DrawRectangleGradientV(0, 0, w, h, top, bottom); }

Camera3D shakeCamera(Camera3D c, float shake, float t) {
    if (shake <= 0) return c;
    const float a = shake * 0.12f;
    c.position.x += std::sin(t * 61.f) * a;
    c.position.y += std::cos(t * 47.f) * a;
    c.target.x += std::sin(t * 53.f) * a * 0.5f;
    return c;
}

} // namespace k3d
