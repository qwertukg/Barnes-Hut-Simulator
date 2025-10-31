#ifndef GPU_GRAVITY_H
#define GPU_GRAVITY_H

#include <vector>

class PODState;
class PODAuxState;
class PODDeriv;

class GpuGravity
{
public:
    GpuGravity();
    ~GpuGravity();

    bool Initialize();
    bool IsReady() const;

    bool Compute(int count,
                 const PODState *state,
                 const PODAuxState *aux,
                 PODDeriv *deriv,
                 float gamma,
                 float softening);

private:
    bool EnsureCapacity(int count);
    bool UploadState(int count, const PODState *state, const PODAuxState *aux);
    bool DownloadDerivatives(int count, PODDeriv *deriv);

    unsigned int _program;
    unsigned int _stateBuffer;
    unsigned int _massBuffer;
    unsigned int _derivBuffer;
    int _countUniform;
    int _gammaUniform;
    int _softeningUniform;

    int _capacity;
    bool _ready;

    std::vector<float> _stateHost;
    std::vector<float> _massHost;
    std::vector<float> _derivHost;
};

#endif // GPU_GRAVITY_H
