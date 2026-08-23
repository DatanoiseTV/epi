/*
  Epi — physically modeled electric pianos
  Copyright (C) 2026 DatanoiseTV

  This program is free software: you can redistribute it and/or modify it under
  the terms of the GNU General Public License as published by the Free Software
  Foundation, either version 3 of the License, or (at your option) any later
  version, and distributed WITHOUT ANY WARRANTY. See <https://www.gnu.org/licenses/>.
  You must retain this notice and the attribution to DatanoiseTV in any
  redistributed or derivative version.
*/

#pragma once

#include "ModalCore.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

// Framework-free physical cores for the electromechanical keyboards: the metal
// that vibrates, the thing that hits it, and the structure it is bolted to.
// Transduction lives in its own headers because the four instruments sense
// motion in four different ways.
//
// Everything is in SI units. Where a number came from a measurement or a
// published model, the comment says which one.
namespace epi
{

inline constexpr float kPi = 3.14159265358979f;

// ---------------------------------------------------------------------------
// Materials. Young's modulus, density, and the internal (material) loss factor
// eta, from which a mode's material-limited Q is 1/eta. Metals are extremely
// lightly damped internally, which is why a Rhodes tine rings for many seconds
// and why almost all of the real decay comes from what the metal is clamped
// to, not from the metal itself.
// ---------------------------------------------------------------------------
struct Material
{
    float youngs;    // Pa
    float density;   // kg/m^3
    float lossEta;   // dimensionless internal loss factor
    bool  ferro = true;      // ferromagnetic: fully visible to a magnetic pickup
    bool  conductive = true; // conductive: usable as an electrostatic plate
    float sigmaRel = 0.1f;   // electrical conductivity relative to copper
};

// Spring steel / music wire: the Rhodes tine and the Wurlitzer reed are both
// hardened steel. Brass for the Rhodes tonebar.
inline constexpr Material kSpringSteel { 200.0e9f, 7850.0f, 2.0e-4f };
inline constexpr Material kBrass       { 100.0e9f, 8500.0f, 8.0e-4f, false };
inline constexpr Material kMusicWire   { 200.0e9f, 7850.0f, 1.5e-4f };

// ---------------------------------------------------------------------------
// The selectable resonator materials. Same physics, different constants: at a
// fixed pitch the geometry re-solves, so what a material changes is the
// inharmonicity (B scales as E/rho for a string at fixed pitch and gauge),
// the modal mass (a light metal swings further for the same strike, driving
// the transducer nonlinearity harder), and the internal loss floor (eta sets
// the material-limited Q; the clamp losses on top are the instrument's own).
// The two flags are transducer facts, not tone: a non-ferromagnetic tine is
// invisible to a magnetic pickup, and an insulator cannot be the moving plate
// of an electrostatic one. Loss factors are room-temperature order-of-
// magnitude values from the damping literature (Lazan; Zener for Al);
// metals vary by alloy and work state, so these are representative, not
// certified.
// ---------------------------------------------------------------------------
inline constexpr Material kMaterials[] = {
    { 200.0e9f,  7850.0f, 1.5e-4f, true,  true,  0.10f },  // 0 music wire (stock)
    { 200.0e9f,  7800.0f, 3.0e-4f, true,  true,  0.03f },  // 1 stainless (ferritic 430)
    { 110.0e9f,  8860.0f, 5.0e-4f, false, true,  0.15f },  // 2 phosphor bronze
    { 100.0e9f,  8500.0f, 8.0e-4f, false, true,  0.28f },  // 3 brass
    { 114.0e9f,  4430.0f, 2.0e-4f, false, true,  0.01f },  // 4 titanium Ti-6Al-4V
    {  69.0e9f,  2700.0f, 1.0e-3f, false, true,  0.61f },  // 5 aluminium
    { 411.0e9f, 19300.0f, 3.0e-4f, false, true,  0.31f },  // 6 tungsten
    {   4.0e9f,  1140.0f, 2.0e-2f, false, false, 0.0f  },  // 7 nylon
};
inline constexpr int kNumMaterials = 8;

// What a magnetic pickup hears from a material: a ferromagnet couples in
// full; a bare CONDUCTOR still speaks faintly through the eddy currents its
// motion drives in the pole's field -- the reason bronze acoustic strings
// are nearly-but-not-quite silent on a magnetic soundhole pickup. Order-of-
// magnitude coupling scaled by conductivity: aluminium lands near -25 dB
// against steel, bronze near -37, titanium (a famously poor conductor) is
// effectively gone. An insulator is exactly gone.
inline double magneticCoupling (const Material& m)
{
    if (m.ferro) return 1.0;
    return 0.092 * static_cast<double> (m.sigmaRel);
}

// ---------------------------------------------------------------------------
// Body materials: what the frame, bar, case or soundboard is made of. Index 0
// is always "stock" -- whatever the instrument was calibrated with -- and the
// scalers below are RELATIVE to stock, so the default is bit-exact by
// construction. Wood constants are along-grain values for soundboard-grade
// stock; loss factors are room-temperature order-of-magnitude values from the
// wood-acoustics and damping literature (Haines; Lazan). A body's modal
// frequencies scale with sqrt(E/rho) at fixed geometry and with 1/s under a
// uniform size scale s (plate and beam alike: t/L^2 with every dimension
// scaled); modal mass scales with rho times s cubed; the material's internal
// loss adds to the calibrated structural loss on a per-mode rate basis, zero
// for stock.
// ---------------------------------------------------------------------------
struct BodyMaterial
{
    float youngs;    // Pa, along grain for woods
    float density;   // kg/m^3
    float lossEta;   // internal loss factor
};

inline constexpr BodyMaterial kBodyMaterials[] = {
    {  11.0e9f,  450.0f, 8.0e-3f },  // 0 stock (spruce-class reference; scalers vs this cancel at index 0 by definition)
    {  11.0e9f,  450.0f, 8.0e-3f },  // 1 spruce
    {  12.6e9f,  700.0f, 1.0e-2f },  // 2 maple
    {  13.0e9f,  680.0f, 1.5e-2f },  // 3 birch ply
    {  69.0e9f, 2700.0f, 1.0e-3f },  // 4 aluminium
    { 200.0e9f, 7850.0f, 2.0e-4f },  // 5 steel
    { 100.0e9f, 8500.0f, 8.0e-4f },  // 6 brass
    {  60.0e9f, 1550.0f, 3.0e-3f },  // 7 carbon composite
};
inline constexpr int kNumBodyMaterials = 8;

// The three relative scalers every body consumer needs, against the stock
// entry. At index 0 all three are exactly one.
struct BodyScalers
{
    double freq = 1.0;   // sqrt(E/rho) ratio: where the modes sit
    double mass = 1.0;   // rho ratio: how heavy each mode is
    double etaAdd = 0.0; // added internal loss vs stock (>= 0)
};

inline BodyScalers bodyScalers (int index)
{
    const BodyMaterial& m = kBodyMaterials[index < 0 ? 0 : index >= kNumBodyMaterials ? kNumBodyMaterials - 1 : index];
    const BodyMaterial& s = kBodyMaterials[0];
    BodyScalers r;
    r.freq = std::sqrt ((static_cast<double> (m.youngs) / static_cast<double> (m.density))
                      / (static_cast<double> (s.youngs) / static_cast<double> (s.density)));
    r.mass = static_cast<double> (m.density) / static_cast<double> (s.density);
    r.etaAdd = std::max (0.0, static_cast<double> (m.lossEta) - static_cast<double> (s.lossEta));
    return r;
}

// A measured T60 is a clamp-limited number: steel's internal loss is
// negligible against what the mount takes (the comment above), so the
// calibrated decay IS the clamp. A different material adds its own internal
// loss on top -- per-mode decay rate sigma = pi * f * eta, so the extra is
// pi * f * (eta_mat - eta_ref), zero by construction for the stock metal.
// Rates add; T60s do not.
inline double materialT60 (double t60Meas, double freqHz, const Material& m, float etaRef)
{
    const double dEta  = std::max (0.0, static_cast<double> (m.lossEta) - static_cast<double> (etaRef));
    const double sigma = 6.9078 / std::max (1.0e-6, t60Meas)
                       + 3.14159265358979 * freqHz * dEta;
    return 6.9078 / sigma;
}

// ---------------------------------------------------------------------------
// Clamped-free (cantilever) beam.
//
// The transverse modes of a uniform beam fixed at one end and free at the
// other satisfy cos(bL) cosh(bL) + 1 = 0, whose roots give the classic
// frequency ratios: with the fundamental at 1, the overtones land at 6.267,
// 17.548, 34.387 and 56.843. They are nowhere near a harmonic series, which is
// exactly why a struck tine sounds like a tuning fork rather than a string.
//
// Two things pull the upper modes down from those ideal ratios:
//
//  - The tuning spring. A Rhodes tine is tuned by sliding a small coil spring
//    along it, which adds mass. Mass at the tip loads the modes in proportion
//    to the square of their tip amplitude, and since every clamped-free mode
//    has an antinode at the tip, it drags all of them down -- but the higher
//    ones further, because they are stiffer relative to the added inertia.
//
//  - Shear. Euler-Bernoulli ignores shear deformation and rotary inertia. For
//    a short, thick beam -- which a tine is, at the top of the keyboard -- the
//    upper modes are noticeably overestimated without it. Pfeifle (DAFx-17,
//    section 5.3) uses a shear beam for precisely this reason, following
//    Trail-Nash & Collar.
// ---------------------------------------------------------------------------
struct CantileverModes
{
    static constexpr int kMaxModes = 8;

    // Roots of cos(x)cosh(x) + 1 = 0. Beyond the fourth, (2n-1)*pi/2 is
    // accurate to better than one part in a million.
    static double betaL (int mode)
    {
        static constexpr double kRoots[] = { 1.8751040687119611, 4.6940911329741746,
                                             7.8547574382376126, 10.9955407348754666,
                                             14.1371683910464705 };
        if (mode < 5) return kRoots[mode];
        return (2.0 * static_cast<double> (mode) + 1.0) * kPiD * 0.5;
    }

    // Mode shape of a clamped-free beam, evaluated at a fraction of the length
    // from the clamp, normalised so the tip has unit amplitude.
    static double shape (int mode, double xOverL)
    {
        const double b  = betaL (mode);
        const double bx = b * std::clamp (xOverL, 0.0, 1.0);
        const double sg = (std::sin (b) - std::sinh (b)) / (std::cos (b) + std::cosh (b));
        auto psi = [sg] (double u)
        {
            return (std::cos (u) - std::cosh (u)) + sg * (std::sin (u) - std::sinh (u));
        };
        const double tip = psi (b);
        return std::abs (tip) > 1.0e-12 ? psi (bx) / tip : 0.0;
    }

    // Slope of the mode shape, needed for the stretching integral that drives
    // the tine's geometric nonlinearity.
    static double slope (int mode, double xOverL)
    {
        const double b  = betaL (mode);
        const double bx = b * std::clamp (xOverL, 0.0, 1.0);
        const double sg = (std::sin (b) - std::sinh (b)) / (std::cos (b) + std::cosh (b));
        auto psi  = [sg] (double u)
        {
            return (std::cos (u) - std::cosh (u)) + sg * (std::sin (u) - std::sinh (u));
        };
        auto dpsi = [sg] (double u)
        {
            return (-std::sin (u) - std::sinh (u)) + sg * (std::cos (u) - std::cosh (u));
        };
        const double tip = psi (b);
        return std::abs (tip) > 1.0e-12 ? b * dpsi (bx) / tip : 0.0;
    }

    // How much a sliding mass loads mode n.
    //
    // Rayleigh quotient: a point mass of `mu` beam-masses sitting at
    // `posOverL` adds mu * phi_n(pos)^2 to that mode's generalised mass while
    // leaving its stiffness alone. With the tip normalisation used here every
    // clamped-free mode has the same modal mass integral, rho*A*L/4, so the
    // frequency scales by 1/sqrt(1 + 4*mu*phi_n(pos)^2).
    //
    // The phi_n(pos)^2 is the whole point. A mass exactly at the tip loads
    // every mode identically and therefore only transposes the beam -- it
    // cannot change the overtone ratios. A mass part-way along sits at a
    // different fraction of each mode's shape, and near a node of some upper
    // mode it barely touches that one while still pulling the fundamental
    // down. Which is why sliding the Rhodes tuning spring does not merely
    // retune the tine: it re-voices it.
    static double massLoadFactor (int mode, double mu, double posOverL)
    {
        const double phi = shape (mode, posOverL);
        return 1.0 / std::sqrt (1.0 + 4.0 * std::max (0.0, mu) * phi * phi);
    }

    // Frequency of mode n relative to the fundamental, for a beam carrying a
    // tuning mass of `mu` beam-masses at `posOverL`, with shear-flexibility
    // number `s` (0 = Euler-Bernoulli).
    static double ratio (int mode, double mu, double posOverL, double shearNumber)
    {
        const double r0 = betaL (mode) * betaL (mode) / (betaL (0) * betaL (0));

        // Shear correction. The shear-beam first-order result lowers mode n by
        // 1/sqrt(1 + s*(betaL_n)^2), so it barely touches the fundamental and
        // pulls the upper modes in hard. Pfeifle (DAFx-17, 5.3) reaches for a
        // shear beam rather than Euler-Bernoulli for exactly this reason: a
        // tine is short and thick enough that ignoring shear puts the
        // overtones measurably sharp.
        const double b2    = betaL (mode) * betaL (mode);
        const double b2ref = betaL (0) * betaL (0);
        const double shear = std::sqrt ((1.0 + shearNumber * b2ref)
                                      / (1.0 + shearNumber * b2));

        const double load = massLoadFactor (mode, mu, posOverL)
                          / massLoadFactor (0,    mu, posOverL);

        return r0 * shear * load;
    }

    // Free length of a uniform circular beam whose fundamental is `f0`.
    //
    //   f_1 = (betaL_1)^2 / (2 pi L^2) * sqrt(E I / (rho A)),  I/A = r^2/4
    //
    // so L = sqrt( (betaL_1)^2 * r/2 * sqrt(E/rho) / (2 pi f0) ).
    static double lengthForFrequency (double f0, double radius, const Material& m)
    {
        const double b2 = betaL (0) * betaL (0);
        const double c  = 0.5 * radius * std::sqrt (m.youngs / m.density);
        return std::sqrt (b2 * c / (2.0 * kPiD * std::max (1.0, f0)));
    }
};

// ---------------------------------------------------------------------------
// Hunt-Crossley hysteretic contact.
//
//   F = k * d^alpha + lambda * d^alpha * d_dot      while d > 0
//   F = 0                                          otherwise
//
// (Pfeifle, DAFx-17, Eq. 1, after Hunt & Crossley.) The damping term is
// multiplied by the compression rather than added to it, which is what makes
// the force fall to zero at separation instead of pulling the hammer back --
// the failure of a plain Kelvin-Voigt contact.
//
// alpha is set by the geometry of the contact and the felt's own nonlinearity:
// a neoprene Rhodes hammer tip is close to 2, a piano's compressed felt runs
// higher and stiffens with register.
//
// The hammer is free after it leaves. On these instruments the key escapes
// before contact, so nothing holds the hammer against the metal and the
// contact duration is a property of the collision, not of how long the key is
// held. Measured on a Rhodes: about 6.4 ms (ISMA 2014, section 3.1.1).
// ---------------------------------------------------------------------------
class HuntCrossleyHammer
{
public:
    struct Config
    {
        double mass       = 0.010;    // kg
        double stiffness  = 2.0e6;    // N/m^alpha
        double alpha      = 2.0;
        double lambda     = 1.2;      // hysteretic loss, s/m
    };

    void prepare (double sampleRate) { fs = sampleRate; reset(); }

    void reset()
    {
        position = 0.0;
        velocity = 0.0;
        inFlight = false;
        contacted = false;
        contactSamples = 0;
    }

    // Launch the hammer toward the metal from `gap` metres away at `speed`
    // metres per second. Positive position is toward the metal.
    void strike (double speed, double gap)
    {
        position = -std::max (0.0, gap);
        velocity = std::max (0.01, speed);
        inFlight = true;
        contacted = false;
        contactSamples = 0;
    }

    bool isActive() const { return inFlight; }
    bool hasTouched() const { return contacted; }
    int  contactDurationSamples() const { return contactSamples; }

    // Advance one sample against a surface currently displaced by
    // `surfaceDisplacement` (metres, positive away from the hammer) moving at
    // `surfaceVelocity`. Returns the contact force, which the caller applies
    // to the resonator.
    //
    // Integrated explicitly, which is safe here for a reason worth stating:
    // the contact stiffness that produces the measured 6.4 ms Rhodes contact
    // puts the collision's own resonance near 150 Hz, four orders of magnitude
    // below the sample rate. That is nowhere near the stiff regime where an
    // explicit contact needs a quadratised treatment. testHammerContactIsStable
    // measures it rather than taking this on trust; if a future instrument
    // needs a harder tip, the modal system has a spare quadratised slot for it.
    double tick (double surfaceDisplacement, double surfaceVelocity, const Config& cfg)
    {
        if (! inFlight) return 0.0;

        const double dt = 1.0 / fs;
        const double compression = position - surfaceDisplacement;

        double force = 0.0;
        if (compression > 0.0)
        {
            contacted = true;
            ++contactSamples;

            const double rate = velocity - surfaceVelocity;
            const double dAlpha = std::pow (compression, cfg.alpha);
            force = cfg.stiffness * dAlpha * (1.0 + cfg.lambda * rate);

            // The hysteretic term can drive the total negative during
            // separation, which would mean the surface pulling the hammer
            // back. Contact cannot pull.
            force = std::max (0.0, force);
        }
        else if (contacted)
        {
            // Left the metal. It does not come back: the key has already
            // escaped, so nothing is pushing the hammer forward any more.
            inFlight = false;
            return 0.0;
        }

        // Semi-implicit Euler: velocity first, then position with the new
        // velocity. Symplectic, so free flight does not gain speed.
        velocity -= force / cfg.mass * dt;
        position += velocity * dt;

        // A hammer that has been turned around and is heading away without
        // ever having touched anything cannot reach the metal.
        if (! contacted && velocity <= 0.0) inFlight = false;

        return force;
    }

private:
    double fs = 48000.0;
    double position = 0.0;   // m, relative to the resting surface
    double velocity = 0.0;   // m/s, positive toward the metal
    bool   inFlight = false;
    bool   contacted = false;
    int    contactSamples = 0;
};

// ---------------------------------------------------------------------------
// One-pole helpers. The double version is used where it sits inside the
// mechanical loop; the float one for the output chain.
// ---------------------------------------------------------------------------
class OnePoleD
{
public:
    void setCutoff (double hz, double fs)
    {
        const double x = std::exp (-2.0 * kPiD * std::clamp (hz, 1.0, 0.49 * fs) / fs);
        a = 1.0 - x;
    }
    double lowpass (double x)  { z += a * (x - z); return z; }
    double highpass (double x) { z += a * (x - z); return x - z; }
    void   reset() { z = 0.0; }

private:
    double a = 0.1, z = 0.0;
};


class OnePole
{
public:
    void setCutoff (float hz, float fs)
    {
        const float x = std::exp (-2.0f * kPi * std::clamp (hz, 1.0f, 0.49f * fs) / fs);
        a = 1.0f - x;
    }
    float lowpass (float x)  { z += a * (x - z); return z; }
    float highpass (float x) { z += a * (x - z); return x - z; }
    void  reset() { z = 0.0f; }

private:
    float a = 0.1f, z = 0.0f;
};

// Cheap deterministic noise, seeded per voice so two notes are not identical.
class Rng
{
public:
    explicit Rng (std::uint32_t seed = 0x9e3779b9u) : s (seed | 1u) {}
    float next()
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return static_cast<float> (static_cast<std::int32_t> (s)) * (1.0f / 2147483648.0f);
    }
    // 0..1
    double nextUnipolar() { return 0.5 * (1.0 + static_cast<double> (next())); }
private:
    std::uint32_t s;
};

} // namespace epi
