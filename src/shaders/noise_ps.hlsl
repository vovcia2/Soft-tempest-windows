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
    int    uMode;        // 0 = WHITE, 1 = HFREQ_HORIZONTAL, 2 = TEMPORAL, 3 = WAVES
    uint   uSeed;        // fresh random seed every frame
    float2 uResolution;  // render target size in pixels
    uint   uBandSeed;    // fixed for the session: shapes the wave bands of mode 3
    float  _pad;
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

// ---------------------------------------------------------------------------
// Mode 3 (WAVES): horizontal wave crests. The crests are horizontal stripes
// (period kStripePeriod) whose vertical position undulates along X. The
// undulation is a travelling sine wave; the screen is split into tall bands and
// each band gets its own random undulation wavelength, amplitude, phase, speed
// and drift direction (left or right), blended smoothly between bands.
// The wave only modulates the ALPHA of the noise; the noise values themselves
// are still re-randomised every frame.
// ---------------------------------------------------------------------------
static const float kTwoPi        = 6.28318530718;
static const float kBandHeight   = 160.0;  // px per band of independent drift
static const float kStripePeriod = 40.0;   // px between successive crests

// Phase argument (in periods) of the stripe pattern for one band.
float waveArg(uint band, float2 p, float time, uint bandSeed)
{
    float r0 = rnd(uint3(band, bandSeed, 11u));
    float r1 = rnd(uint3(band, bandSeed, 23u));
    float r2 = rnd(uint3(band, bandSeed, 37u));
    float r3 = rnd(uint3(band, bandSeed, 53u));
    float r4 = rnd(uint3(band, bandSeed, 71u));

    float waveLen = lerp(180.0, 560.0, r0);     // px, horizontal undulation period
    float speed   = lerp(10.0, 45.0, r1);       // px per second (slow drift)
    float dir     = (r2 < 0.5) ? -1.0 : 1.0;    // random drift direction
    float ampl    = lerp(10.0, 26.0, r3);       // px, undulation amplitude
    float phase   = r4;

    float disp = ampl * sin(kTwoPi * ((p.x - dir * speed * time) / waveLen + phase));
    return (p.y + disp) / kStripePeriod + phase;
}

float wavesEnvelope(float2 p, float time, uint bandSeed)
{
    float fy = p.y / kBandHeight + 1024.0;      // offset keeps fy positive
    float b  = floor(fy);
    float t  = smoothstep(0.0, 1.0, frac(fy));
    uint  b0 = (uint)b;

    // Blend the phase arguments (same stripe period in both bands), so the
    // transition between bands is seamless.
    float arg = lerp(waveArg(b0, p, time, bandSeed), waveArg(b0 + 1u, p, time, bandSeed), t);
    float w   = 0.5 + 0.5 * sin(kTwoPi * arg);
    return w * w;                               // sharper crests, wider troughs
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
        else if (uMode == 3)
        {
            // WAVES: fresh noise every frame, alpha shaped by slowly drifting
            // horizontal waves (never fully dark, so every pixel keeps noise).
            float w = wavesEnvelope(pos.xy, uTime, uBandSeed);
            a *= 0.12 + 0.88 * w;
        }
    }

    float3 rgb = float3(n, n, n);
    return float4(rgb * a, a);   // premultiplied alpha
}
