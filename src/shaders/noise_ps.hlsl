// Soft-TEMPEST noise pixel shader.
//
// Every pixel gets an independent pseudo-random value that ALSO depends on a
// per-frame seed, so the noise is re-drawn from scratch every frame. This is
// what defeats N-frame averaging (static noise would fall off as sqrt(N)).
//
// Output is premultiplied alpha, as required by the DirectComposition swap
// chain (DXGI_ALPHA_MODE_PREMULTIPLIED).

cbuffer NoiseConstants : register(b0)
{
    float  uTime;        // seconds since start (informational)
    float  uAmplitude;   // 0..1, overlay opacity
    int    uMode;        // 0 = WHITE, 1 = HFREQ_HORIZONTAL, 2 = TEMPORAL
    uint   uSeed;        // fresh random seed every frame
    float2 uResolution;  // render target size in pixels
    float2 _pad;
};

// PCG-style integer hash (O'Neill). Cheap, good distribution for this use.
uint pcg(uint v)
{
    uint s = v * 747796405u + 2891336453u;
    uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}

uint hash3(uint3 v)
{
    return pcg(v.x ^ pcg(v.y ^ pcg(v.z)));
}

// Uniform float in [0, 1).
float rnd(uint3 v)
{
    return (hash3(v) & 0x00FFFFFFu) / float(0x01000000u);
}

float4 main(float4 pos : SV_Position) : SV_Target
{
    uint2 pix   = uint2(pos.xy);
    uint  frame = uSeed;
    float a     = uAmplitude;
    float n;

    if (uMode == 1)
    {
        // HFREQ_HORIZONTAL: push energy towards the horizontal Nyquist
        // frequency (half the pixel clock). Neighbouring column pairs share one
        // random value and take it with opposite sign, so the signal alternates
        // strongly from column to column while staying random from pair to pair
        // and from frame to frame.
        float r   = rnd(uint3(pix.x >> 1u, pix.y, frame));
        float alt = ((pix.x & 1u) == 0u) ? 1.0 : -1.0;
        n = 0.5 + 0.5 * alt * (2.0 * r - 1.0);
    }
    else
    {
        // WHITE: independent uniform value per pixel per frame.
        n = rnd(uint3(pix, frame));

        if (uMode == 2)
        {
            // TEMPORAL: additionally randomise the alpha of each pixel every
            // frame so the overlay's contribution itself flickers.
            a *= rnd(uint3(pix.x ^ 2654435769u, pix.y, frame ^ 2246822519u));
        }
    }

    float3 rgb = float3(n, n, n);
    return float4(rgb * a, a);   // premultiplied alpha
}
