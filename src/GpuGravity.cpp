#include "GpuGravity.h"

#include "Types.h"

#include <GL/gl.h>
#include <GL/glx.h>

#ifndef GL_COMPUTE_SHADER
#define GL_COMPUTE_SHADER 0x91B9
#endif

#ifndef GL_SHADER_STORAGE_BUFFER
#define GL_SHADER_STORAGE_BUFFER 0x90D2
#endif

#ifndef GL_SHADER_STORAGE_BARRIER_BIT
#define GL_SHADER_STORAGE_BARRIER_BIT 0x2000
#endif

namespace
{
typedef void(APIENTRYP PFNGLDISPATCHCOMPUTEPROC)(GLuint num_groups_x, GLuint num_groups_y, GLuint num_groups_z);
typedef void(APIENTRYP PFNGLBINDBUFFERBASEPROC)(GLenum target, GLuint index, GLuint buffer);
typedef void(APIENTRYP PFNGLMEMORYBARRIERPROC)(GLbitfield barriers);

PFNGLDISPATCHCOMPUTEPROC pglDispatchCompute = nullptr;
PFNGLBINDBUFFERBASEPROC pglBindBufferBase = nullptr;
PFNGLMEMORYBARRIERPROC pglMemoryBarrier = nullptr;

bool LoadComputeFunctions()
{
    if (pglDispatchCompute && pglBindBufferBase && pglMemoryBarrier)
        return true;

    pglDispatchCompute = reinterpret_cast<PFNGLDISPATCHCOMPUTEPROC>(glXGetProcAddress(reinterpret_cast<const GLubyte *>("glDispatchCompute")));
    pglBindBufferBase = reinterpret_cast<PFNGLBINDBUFFERBASEPROC>(glXGetProcAddress(reinterpret_cast<const GLubyte *>("glBindBufferBase")));
    pglMemoryBarrier = reinterpret_cast<PFNGLMEMORYBARRIERPROC>(glXGetProcAddress(reinterpret_cast<const GLubyte *>("glMemoryBarrier")));

    return pglDispatchCompute && pglBindBufferBase && pglMemoryBarrier;
}
} // namespace

namespace
{
const char *kComputeShaderSrc = R"GLSL(#version 460

layout(local_size_x = 256) in;

layout(std430, binding = 0) readonly buffer StateIn
{
    vec4 state[];
};

layout(std430, binding = 1) readonly buffer MassIn
{
    float mass[];
};

layout(std430, binding = 2) writeonly buffer DerivOut
{
    vec4 deriv[];
};

uniform int uCount;
uniform float uGamma;
uniform float uSoftening;

void main()
{
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= uint(uCount))
        return;

    vec2 pos = state[idx].xy;
    vec2 vel = state[idx].zw;
    vec2 acc = vec2(0.0);

    for (int j = 0; j < uCount; ++j)
    {
        if (j == int(idx))
            continue;

        vec2 dir = state[j].xy - pos;
        float distSqr = dot(dir, dir) + uSoftening;
        float invDist = inversesqrt(distSqr);
        float invDist3 = invDist * invDist * invDist;
        acc += (uGamma * mass[j]) * dir * invDist3;
    }

    deriv[idx] = vec4(vel, acc);
}
)GLSL";
} // namespace

GpuGravity::GpuGravity()
    :_program(0)
    ,_stateBuffer(0)
    ,_massBuffer(0)
    ,_derivBuffer(0)
    ,_countUniform(-1)
    ,_gammaUniform(-1)
    ,_softeningUniform(-1)
    ,_capacity(0)
    ,_ready(false)
{}

GpuGravity::~GpuGravity()
{
    if (_stateBuffer)
        glDeleteBuffers(1, &_stateBuffer);
    if (_massBuffer)
        glDeleteBuffers(1, &_massBuffer);
    if (_derivBuffer)
        glDeleteBuffers(1, &_derivBuffer);
    if (_program)
        glDeleteProgram(_program);
}

bool GpuGravity::Initialize()
{
    if (_ready)
        return true;

    if (!LoadComputeFunctions())
        return false;

    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(shader, 1, &kComputeShaderSrc, nullptr);
    glCompileShader(shader);

    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE)
    {
        glDeleteShader(shader);
        return false;
    }

    _program = glCreateProgram();
    glAttachShader(_program, shader);
    glLinkProgram(_program);
    glDeleteShader(shader);

    glGetProgramiv(_program, GL_LINK_STATUS, &status);
    if (status != GL_TRUE)
    {
        glDeleteProgram(_program);
        _program = 0;
        return false;
    }

    _countUniform = glGetUniformLocation(_program, "uCount");
    _gammaUniform = glGetUniformLocation(_program, "uGamma");
    _softeningUniform = glGetUniformLocation(_program, "uSoftening");

    glGenBuffers(1, &_stateBuffer);
    glGenBuffers(1, &_massBuffer);
    glGenBuffers(1, &_derivBuffer);

    _capacity = 0;
    _ready = _stateBuffer != 0 && _massBuffer != 0 && _derivBuffer != 0;
    return _ready;
}

bool GpuGravity::IsReady() const
{
    return _ready;
}

bool GpuGravity::EnsureCapacity(int count)
{
    if (count <= _capacity)
        return true;

    _capacity = count;
    const GLsizeiptr stateSize = static_cast<GLsizeiptr>(_capacity) * 4 * sizeof(float);
    const GLsizeiptr massSize = static_cast<GLsizeiptr>(_capacity) * sizeof(float);
    const GLsizeiptr derivSize = static_cast<GLsizeiptr>(_capacity) * 4 * sizeof(float);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, _stateBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, stateSize, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, _massBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, massSize, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, _derivBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, derivSize, nullptr, GL_DYNAMIC_READ);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    _stateHost.resize(static_cast<std::size_t>(_capacity) * 4);
    _massHost.resize(static_cast<std::size_t>(_capacity));
    _derivHost.resize(static_cast<std::size_t>(_capacity) * 4);

    return true;
}

bool GpuGravity::UploadState(int count, const PODState *state, const PODAuxState *aux)
{
    for (int i = 0; i < count; ++i)
    {
        const int idx = i * 4;
        _stateHost[static_cast<std::size_t>(idx) + 0] = static_cast<float>(state[i].x);
        _stateHost[static_cast<std::size_t>(idx) + 1] = static_cast<float>(state[i].y);
        _stateHost[static_cast<std::size_t>(idx) + 2] = static_cast<float>(state[i].vx);
        _stateHost[static_cast<std::size_t>(idx) + 3] = static_cast<float>(state[i].vy);
        _massHost[static_cast<std::size_t>(i)] = static_cast<float>(aux[i].mass);
    }

    const GLsizeiptr stateSize = static_cast<GLsizeiptr>(count) * 4 * sizeof(float);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, _stateBuffer);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, stateSize, _stateHost.data());

    const GLsizeiptr massSize = static_cast<GLsizeiptr>(count) * sizeof(float);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, _massBuffer);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, massSize, _massHost.data());

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    return true;
}

bool GpuGravity::DownloadDerivatives(int count, PODDeriv *deriv)
{
    const GLsizeiptr derivSize = static_cast<GLsizeiptr>(count) * 4 * sizeof(float);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, _derivBuffer);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, derivSize, _derivHost.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    for (int i = 0; i < count; ++i)
    {
        const int idx = i * 4;
        deriv[i].vx = static_cast<double>(_derivHost[static_cast<std::size_t>(idx) + 0]);
        deriv[i].vy = static_cast<double>(_derivHost[static_cast<std::size_t>(idx) + 1]);
        deriv[i].ax = static_cast<double>(_derivHost[static_cast<std::size_t>(idx) + 2]);
        deriv[i].ay = static_cast<double>(_derivHost[static_cast<std::size_t>(idx) + 3]);
    }

    return true;
}

bool GpuGravity::Compute(int count,
                         const PODState *state,
                         const PODAuxState *aux,
                         PODDeriv *deriv,
                         float gamma,
                         float softening)
{
    if (!_ready)
        return false;

    if (!EnsureCapacity(count))
        return false;

    if (!UploadState(count, state, aux))
        return false;

    pglBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, _stateBuffer);
    pglBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, _massBuffer);
    pglBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, _derivBuffer);

    glUseProgram(_program);
    if (_countUniform >= 0)
        glUniform1i(_countUniform, count);
    if (_gammaUniform >= 0)
        glUniform1f(_gammaUniform, gamma);
    if (_softeningUniform >= 0)
        glUniform1f(_softeningUniform, softening);

    const GLuint groupSize = 256;
    GLuint numGroups = static_cast<GLuint>((count + groupSize - 1) / groupSize);
    if (numGroups == 0)
        numGroups = 1;

    pglDispatchCompute(numGroups, 1, 1);
    pglMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    glUseProgram(0);

    return DownloadDerivatives(count, deriv);
}
