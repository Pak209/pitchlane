#include "pitchlane/OfflineAnalyzer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>

#include "pitchlane/Filters.h"
#include "pitchlane/NoteMath.h"
#include "pitchlane/Yin.h"

namespace pitchlane {

namespace {

struct Candidate
{
    float midi;
    float prob;
};

constexpr int kMaxCandidates = 8;
constexpr int kNumThresholds = 100;

std::vector<double> betaThresholdWeights()
{
    // Beta(2, 18) pdf sampled at 0.01 .. 1.00, normalised (pYIN's default prior, mean 0.1).
    std::vector<double> w(kNumThresholds);
    double sum = 0.0;
    for (int k = 0; k < kNumThresholds; ++k)
    {
        const double s = (k + 1) * 0.01;
        const double v = s * std::pow(1.0 - s, 17.0);
        w[static_cast<size_t>(k)] = v;
        sum += v;
    }
    for (auto& v : w) v /= sum;
    return w;
}

double medianOf(std::vector<float>& v)
{
    if (v.empty()) return 0.0;
    const size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + static_cast<long>(mid), v.end());
    double m = v[mid];
    if (v.size() % 2 == 0)
    {
        const float lower = *std::max_element(v.begin(), v.begin() + static_cast<long>(mid));
        m = 0.5 * (m + lower);
    }
    return m;
}


// ---- small radix-2 FFT (analysis-time only) ------------------------------------------
void fftInPlace(std::vector<std::complex<float>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * 3.14159265358979323846 / static_cast<double>(len);
        const std::complex<float> wl(static_cast<float>(std::cos(ang)), static_cast<float>(std::sin(ang)));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<float> w(1.f, 0.f);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

/** Harmonic-sum salience of f0 (Hz) in a magnitude spectrum (bin width binHz). */
float harmonicSalience(const std::vector<float>& mag, double binHz, double f0, int harmonics = 8)
{
    float sum = 0.f, w = 1.f;
    for (int h = 1; h <= harmonics; ++h, w *= 0.84f)
    {
        const double b = h * f0 / binHz;
        const int c = static_cast<int>(std::lround(b));
        if (c + 1 >= static_cast<int>(mag.size())) break;
        float m = 0.f;
        for (int k = std::max(1, c - 1); k <= c + 1; ++k) m = std::max(m, mag[static_cast<size_t>(k)]);
        sum += w * m;
    }
    return sum;
}

} // namespace

AnalysisResult analyzeMonophonic(const float* mono, size_t numSamples, double sampleRate,
                                 const AnalyzerSettings& s, const ProgressFn& progress)
{
    AnalysisResult result;
    if (mono == nullptr || numSamples == 0 || sampleRate <= 0.0)
    {
        result.error = "No audio";
        return result;
    }

    auto report = [&](float p) -> bool {
        if (progress && !progress(std::clamp(p, 0.f, 1.f)))
        {
            result.cancelled = true;
            return false;
        }
        return true;
    };

    // ---- 1. filter + decimate ---------------------------------------------------------
    const int D = std::max(1, static_cast<int>(std::floor(sampleRate / 11025.0 + 1e-6)));
    const double r = sampleRate / D;
    Decimator dec;
    dec.prepare(sampleRate, D, std::min(4500.0, 0.4 * r), 50.0);

    const int W = std::max(32, static_cast<int>(std::lround(s.windowMs * 0.001 * r)));
    const int maxLag = static_cast<int>(std::ceil(r / s.minHz)) + 2;
    const int minLag = std::max(2, static_cast<int>(std::floor(r / s.maxHz)));
    const int bufLen = W + maxLag + 1;
    const int hop = std::max(1, static_cast<int>(std::lround(s.hopMs * 0.001 * r)));
    const double hopSec = hop / r;
    const int pad = bufLen / 2;

    std::vector<float> x(static_cast<size_t>(pad), 0.f);
    x.reserve(numSamples / static_cast<size_t>(D) + static_cast<size_t>(2 * bufLen + 2));
    for (size_t i = 0; i < numSamples; ++i)
    {
        float y;
        if (dec.push(mono[i], y)) x.push_back(y);
        if ((i & 0xFFFFu) == 0 && !report(0.02f * static_cast<float>(i) / static_cast<float>(numSamples))) return result;
    }
    const size_t nDec = x.size() - static_cast<size_t>(pad);
    x.resize(x.size() + static_cast<size_t>(bufLen), 0.f);

    const size_t numFrames = nDec / static_cast<size_t>(hop) + 1;
    if (!report(0.02f)) return result;

    // ---- 2. per-frame YIN candidates ---------------------------------------------------
    const auto thrW = betaThresholdWeights();
    std::vector<float> diff(static_cast<size_t>(maxLag + 2)), cm(static_cast<size_t>(maxLag + 2));
    std::vector<std::array<Candidate, kMaxCandidates>> cands(numFrames);
    std::vector<uint8_t> candCount(numFrames, 0);
    std::vector<float> rmsDb(numFrames, -120.f), shortRms(numFrames, -120.f), voicedProb(numFrames, 0.f);
    const int shortW = std::max(8, static_cast<int>(std::lround(0.010 * r)));

    for (size_t f = 0; f < numFrames; ++f)
    {
        const float* fx = x.data() + f * static_cast<size_t>(hop);
        const float ms = yin::meanSquare(fx + maxLag / 2, W);
        rmsDb[f] = ms > 1e-12f ? 10.f * std::log10(ms) : -120.f;
        const float sms = yin::meanSquare(fx + pad - shortW / 2, shortW);
        shortRms[f] = sms > 1e-12f ? 10.f * std::log10(sms) : -120.f;
        if ((f & 255u) == 0 && !report(0.02f + 0.60f * static_cast<float>(f) / numFrames)) return result;
    }

    // Loudness reference: 95th percentile of frame RMS (robust to a few peaks).
    std::vector<float> sortedRms(rmsDb);
    std::sort(sortedRms.begin(), sortedRms.end());
    const float loudRef = sortedRms[static_cast<size_t>(0.95 * (sortedRms.size() - 1))];
    const float gate = static_cast<float>(std::max(s.gateDbAbsolute, loudRef + s.gateDbRelative));

    for (size_t f = 0; f < numFrames; ++f)
    {
        if ((f & 255u) == 0 && !report(0.02f + 0.60f * static_cast<float>(f) / numFrames)) return result;
        if (rmsDb[f] < gate) continue;

        const float* fx = x.data() + f * static_cast<size_t>(hop);
        yin::difference(fx, W, maxLag, diff.data());
        yin::cumulativeMeanNormalised(diff.data(), maxLag, cm.data());

        // Local minima in lag order.
        int mins[kMaxCandidates];
        int nMins = 0;
        int globalMin = -1;
        float globalVal = std::numeric_limits<float>::max();
        for (int t = minLag; t < maxLag; ++t)
        {
            if (cm[static_cast<size_t>(t)] < globalVal) { globalVal = cm[static_cast<size_t>(t)]; globalMin = t; }
            if (nMins < kMaxCandidates && cm[static_cast<size_t>(t)] < cm[static_cast<size_t>(t - 1)]
                && cm[static_cast<size_t>(t)] <= cm[static_cast<size_t>(t + 1)])
                mins[nMins++] = t;
        }
        if (globalMin < 0) continue;

        float probs[kMaxCandidates] = {};
        float globalProb = 0.f;
        for (int k = 0; k < kNumThresholds; ++k)
        {
            const float thr = (k + 1) * 0.01f;
            bool found = false;
            for (int m = 0; m < nMins; ++m)
            {
                if (cm[static_cast<size_t>(mins[m])] < thr)
                {
                    probs[m] += static_cast<float>(thrW[static_cast<size_t>(k)]);
                    found = true;
                    break;
                }
            }
            if (!found) globalProb += static_cast<float>(thrW[static_cast<size_t>(k)] * 0.01);
        }

        // Real voices: the dip at the true period is often a little shallower than the one at
        // twice (three times) the period, so low thresholds pick the sub-octave. Hand such a
        // minimum's probability to the dip near half (a third of) its lag when that dip is
        // almost as deep.
        if (s.octaveSlack > 0.0)
        {
            for (int m = nMins - 1; m >= 0; --m)
            {
                if (probs[m] <= 0.f) continue;
                for (int div : { 2, 3 })
                {
                    const double want = static_cast<double>(mins[m]) / div;
                    int best = -1;
                    for (int m2 = 0; m2 < m; ++m2)
                        if (std::abs(mins[m2] - want) <= std::max(1.5, 0.04 * want)
                            && cm[static_cast<size_t>(mins[m2])] < cm[static_cast<size_t>(mins[m])] + static_cast<float>(s.octaveSlack)
                            && cm[static_cast<size_t>(mins[m2])] < 0.5f)
                            best = m2;
                    if (best >= 0) { probs[best] += probs[m]; probs[m] = 0.f; break; }
                }
            }
        }

        auto addCandidate = [&](int tau, float p) {
            if (p <= 1e-6f) return;
            const double period = yin::parabolicMin(diff.data(), maxLag + 1, tau);
            if (period <= 0.0) return;
            const float midi = static_cast<float>(hzToMidi(r / period));
            for (int c = 0; c < candCount[f]; ++c)
                if (std::abs(cands[f][static_cast<size_t>(c)].midi - midi) < 0.01f) { cands[f][static_cast<size_t>(c)].prob += p; return; }
            if (candCount[f] < kMaxCandidates) cands[f][candCount[f]++] = { midi, p };
        };
        float vp = 0.f;
        for (int m = 0; m < nMins; ++m) { addCandidate(mins[m], probs[m]); vp += probs[m]; }
        addCandidate(globalMin, globalProb);
        vp += globalProb;
        voicedProb[f] = std::min(vp, 1.f);
    }
    if (!report(0.62f)) return result;

    // ---- 3. Viterbi over pitch bins + unvoiced ----------------------------------------
    const double minMidi = hzToMidi(s.minHz), maxMidi = hzToMidi(s.maxHz);
    const int bps = std::max(1, s.binsPerSemitone);
    const int B = static_cast<int>(std::ceil((maxMidi - minMidi) * bps)) + 1;
    const int U = B; // unvoiced state index
    const int S = B + 1;
    const int J = std::max(1, s.maxJumpBins);
    auto binOf = [&](double midi) { return static_cast<int>(std::lround((midi - minMidi) * bps)); };
    auto binMidi = [&](int b) { return minMidi + static_cast<double>(b) / bps; };

    const double pSw = std::clamp(s.voicingSwitchProb, 1e-6, 0.5);
    std::vector<float> logTri(static_cast<size_t>(J + 1));
    {
        double norm = 0.0;
        for (int d = -J; d <= J; ++d) norm += (J + 1 - std::abs(d));
        for (int d = 0; d <= J; ++d)
            logTri[static_cast<size_t>(d)] = static_cast<float>(std::log((1.0 - pSw) * 0.999 * (J + 1 - d) / norm));
    }
    const float logFloor = static_cast<float>(std::log((1.0 - pSw) * 0.001 / B));
    // The unvoiced state behaves like pYIN's unvoiced copies of every pitch bin: it keeps
    // tracking a hidden pitch, so it pays the same per-frame "stay on this pitch" cost as a
    // voiced bin. (Without that, every voiced frame paid ~log(31/961) more than an unvoiced
    // one and real, reverberant vocals decoded as mostly unvoiced.)
    double triNorm = 0.0;
    for (int d = -J; d <= J; ++d) triNorm += (J + 1 - std::abs(d));
    const double stay = 0.999 * (J + 1) / triNorm;
    const float logVtoU = static_cast<float>(std::log(pSw * stay));
    const float logUtoU = static_cast<float>(std::log((1.0 - pSw) * stay));
    const float logUtoV = static_cast<float>(std::log(pSw * stay));
    constexpr float kEps = 1e-7f;
    const float uWeight = static_cast<float>(std::clamp(s.unvoicedWeight, 1e-3, 1.0));

    std::vector<float> delta(static_cast<size_t>(S)), next(static_cast<size_t>(S)), obs(static_cast<size_t>(S));
    std::vector<int16_t> back(numFrames * static_cast<size_t>(S));

    auto fillObs = [&](size_t f) {
        std::fill(obs.begin(), obs.end(), kEps);
        float vp = 0.f;
        for (int c = 0; c < candCount[f]; ++c)
        {
            const auto& cd = cands[f][static_cast<size_t>(c)];
            const int b = binOf(cd.midi);
            if (b < 0 || b >= B) continue;
            obs[static_cast<size_t>(b)] += cd.prob;
            vp += cd.prob;
        }
        obs[static_cast<size_t>(U)] = std::max(kEps, uWeight * (1.f - std::min(vp, 1.f)));
        for (auto& o : obs) o = std::log(o);
    };

    fillObs(0);
    for (int st = 0; st < S; ++st)
        delta[static_cast<size_t>(st)] = obs[static_cast<size_t>(st)] + static_cast<float>(st == U ? std::log(0.5) : std::log(0.5 / B));

    for (size_t f = 1; f < numFrames; ++f)
    {
        if ((f & 255u) == 0 && !report(0.62f + 0.30f * static_cast<float>(f) / numFrames)) return result;
        fillObs(f);
        int16_t* bp = back.data() + f * static_cast<size_t>(S);

        int bestV = 0;
        for (int b = 1; b < B; ++b)
            if (delta[static_cast<size_t>(b)] > delta[static_cast<size_t>(bestV)]) bestV = b;
        const float fromAny = delta[static_cast<size_t>(bestV)] + logFloor;
        const float fromU = delta[static_cast<size_t>(U)] + logUtoV;

        for (int b = 0; b < B; ++b)
        {
            float best = fromAny;
            int arg = bestV;
            if (fromU > best) { best = fromU; arg = U; }
            const int lo = std::max(0, b - J), hi = std::min(B - 1, b + J);
            for (int p = lo; p <= hi; ++p)
            {
                const float v = delta[static_cast<size_t>(p)] + logTri[static_cast<size_t>(std::abs(p - b))];
                if (v > best) { best = v; arg = p; }
            }
            next[static_cast<size_t>(b)] = best + obs[static_cast<size_t>(b)];
            bp[b] = static_cast<int16_t>(arg);
        }
        {
            float best = delta[static_cast<size_t>(U)] + logUtoU;
            int arg = U;
            const float v = delta[static_cast<size_t>(bestV)] + logVtoU;
            if (v > best) { best = v; arg = bestV; }
            next[static_cast<size_t>(U)] = best + obs[static_cast<size_t>(U)];
            bp[U] = static_cast<int16_t>(arg);
        }
        // Normalise to avoid drift.
        const float mx = *std::max_element(next.begin(), next.end());
        for (int st = 0; st < S; ++st) delta[static_cast<size_t>(st)] = next[static_cast<size_t>(st)] - mx;
    }

    std::vector<int> path(numFrames);
    {
        int st = static_cast<int>(std::max_element(delta.begin(), delta.end()) - delta.begin());
        for (size_t f = numFrames; f-- > 0;)
        {
            path[f] = st;
            if (f > 0) st = back[f * static_cast<size_t>(S) + static_cast<size_t>(st)];
        }
    }
    if (!report(0.93f)) return result;

    // ---- decoded frames ---------------------------------------------------------------
    result.frames.resize(numFrames);
    for (size_t f = 0; f < numFrames; ++f)
    {
        auto& fr = result.frames[f];
        fr.time = static_cast<double>(f) * hopSec;
        fr.rmsDb = rmsDb[f];
        fr.shortRmsDb = shortRms[f];
        fr.voicedProb = voicedProb[f];
        const int st = path[f];
        if (st == U) continue;
        const double centre = binMidi(st);
        double bestMidi = centre, bestDist = 1.0;
        for (int c = 0; c < candCount[f]; ++c)
        {
            const double d = std::abs(cands[f][static_cast<size_t>(c)].midi - centre);
            if (d < bestDist) { bestDist = d; bestMidi = cands[f][static_cast<size_t>(c)].midi; }
        }
        fr.midi = static_cast<float>(bestMidi);
    }

    if (s.removeSpikes) removePitchSpikes(result.frames);

    // ---- spectral pass: harmonic-salience correction of period-multiple errors, and the
    //      second-voice check (harmonic cancellation) --------------------------------------
    if (s.salienceCorrection || s.detectHarmonies)
    {
        constexpr size_t N = 1024;
        const double binHz = r / static_cast<double>(N);
        std::vector<float> win(N), mag(N / 2), resid(N / 2);
        for (size_t i = 0; i < N; ++i) win[i] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * i / (N - 1)));
        std::vector<std::complex<float>> buf(N);
        std::vector<double> candHz;
        for (double m = minMidi; m <= maxMidi; m += 1.0 / 3.0) candHz.push_back(midiToHz(m));
        auto salAround = [&](const std::vector<float>& sp, double hz) {
            float best = 0.f;
            for (double c : { 0.985, 1.0, 1.015 }) best = std::max(best, harmonicSalience(sp, binHz, hz * c));
            return best;
        };
        size_t voicedCount = 0;
        for (size_t f = 0; f < numFrames; ++f)
        {
            auto& fr = result.frames[f];
            if (fr.midi <= 0.f) continue;
            const bool checkAlt = s.detectHarmonies && (voicedCount & 3u) == 0;
            ++voicedCount;
            if (!s.salienceCorrection && !checkAlt) continue;
            if ((voicedCount & 255u) == 1 && !report(0.93f + 0.05f * static_cast<float>(f) / numFrames)) return result;
            const long centre = static_cast<long>(f * static_cast<size_t>(hop)) + pad;
            const long start = centre - static_cast<long>(N / 2);
            for (size_t i = 0; i < N; ++i)
            {
                const long idx = start + static_cast<long>(i);
                const float v = (idx >= 0 && idx < static_cast<long>(x.size())) ? x[static_cast<size_t>(idx)] : 0.f;
                buf[i] = { v * win[i], 0.f };
            }
            fftInPlace(buf);
            for (size_t k = 0; k < N / 2; ++k) mag[k] = std::sqrt(std::abs(buf[k]));   // compressed magnitude
            double f0 = midiToHz(fr.midi);
            float primary = salAround(mag, f0);

            for (int pass = 0; pass < 3 && s.salienceCorrection; ++pass)   // 66 Hz -> 264 -> 528 needs two
            {
                // YIN on mixtures / breathy voices can lock onto 2x, 3x or 4x the period. If a
                // multiple of the decoded frequency (or half of it) explains the spectrum clearly
                // better, move there.
                double bestHz = f0;
                float bestSal = primary * static_cast<float>(s.salienceSwitch);
                for (double k : { 0.5, 2.0, 3.0, 4.0 })
                {
                    const double hz = f0 * k;
                    if (hz < s.minHz || hz > s.maxHz) continue;
                    const float sal = salAround(mag, hz) * (k == 0.5 ? 0.8f : 1.f);   // be shy of going down
                    if (sal > bestSal) { bestSal = sal; bestHz = hz; }
                }
                if (bestHz == f0) break;
                fr.midi = static_cast<float>(fr.midi + 12.0 * std::log2(bestHz / f0));
                f0 = bestHz;
                primary = salAround(mag, f0);
            }
            if (!checkAlt || primary <= 1e-9f) continue;

            // Cancel the decoded voice's harmonics (and so its sub-octave, which shares them).
            resid = mag;
            for (int h = 1; h * f0 < 0.5 * r; ++h)
            {
                const int c = static_cast<int>(std::lround(h * f0 / binHz));
                for (int k = std::max(0, c - 2); k <= std::min(static_cast<int>(N / 2) - 1, c + 2); ++k) resid[static_cast<size_t>(k)] = 0.f;
            }
            float best = 0.f;
            double bestHz = 0.0;
            for (double hz : candHz)
            {
                const double rel = 12.0 * std::log2(hz / f0);
                if (std::abs(rel) < 0.6 || std::abs(rel + 12.0) < 0.6) continue;
                const float sal = harmonicSalience(resid, binHz, hz);
                if (sal > best) { best = sal; bestHz = hz; }
            }
            fr.altRatio = best / primary;
            fr.altMidi = bestHz > 0.0 ? static_cast<float>(hzToMidi(bestHz)) : 0.f;
        }
        if (s.removeSpikes) removePitchSpikes(result.frames);
    }

    // ---- 4-5. notes -------------------------------------------------------------------
    result.notes = segmentNotes(result.frames, hopSec, s);
    if (s.fixOctaveErrors) fixOctaveErrors(result.notes);
    if (s.detectHarmonies) markHarmonySuspects(result.notes, result.frames, hopSec, s);
    report(1.f);
    return result;
}

void removePitchSpikes(std::vector<AnalysisFrame>& frames)
{
    const size_t n = frames.size();
    // Edges of voiced runs: 1-3 frames that jump >= 6 st away from the run are unvoiced.
    for (size_t i = 0; i < n; ++i)
    {
        if (frames[i].midi <= 0.f || (i > 0 && frames[i - 1].midi > 0.f)) continue;
        size_t e = i;
        while (e < n && frames[e].midi > 0.f) ++e;       // run [i, e)
        if (e - i < 8) { i = e; continue; }
        for (int side = 0; side < 2; ++side)
        {
            for (size_t len = 3; len >= 1; --len)
            {
                const size_t a = side == 0 ? i : e - len;
                const size_t ref = side == 0 ? i + len : e - len - 1;
                bool off = true;
                for (size_t k = a; k < a + len; ++k)
                    if (std::abs(frames[k].midi - frames[ref].midi) < 6.f) { off = false; break; }
                if (off) { for (size_t k = a; k < a + len; ++k) frames[k].midi = 0.f; break; }
            }
        }
        i = e;
    }
    for (size_t i = 1; i + 1 < n; ++i)
    {
        if (frames[i].midi <= 0.f || frames[i - 1].midi <= 0.f) continue;
        const float before = frames[i - 1].midi;
        for (size_t len = 1; len <= 3 && i + len < n; ++len)
        {
            const float after = frames[i + len].midi;
            if (after <= 0.f || std::abs(after - before) > 2.f) continue;
            bool outlier = true;
            for (size_t k = i; k < i + len; ++k)
                if (frames[k].midi <= 0.f || std::abs(frames[k].midi - before) < 6.f || std::abs(frames[k].midi - after) < 6.f) { outlier = false; break; }
            if (!outlier) continue;
            for (size_t k = i; k < i + len; ++k)
            {
                const float t = static_cast<float>(k - i + 1) / static_cast<float>(len + 1);
                frames[k].midi = before + t * (after - before);
            }
            i += len - 1;
            break;
        }
    }
}

int fixOctaveErrors(NoteList& notes)
{
    int fixed = 0;
    constexpr double kNear = 0.5; // neighbours must be within 0.5 s
    for (size_t i = 0; i < notes.size(); ++i)
    {
        auto& n = notes[i];
        if (n.length >= 0.8) continue;
        const RefNote* prev = (i > 0 && n.start - notes[i - 1].end() <= kNear) ? &notes[i - 1] : nullptr;
        const RefNote* next = (i + 1 < notes.size() && notes[i + 1].start - n.end() <= kNear) ? &notes[i + 1] : nullptr;
        if (prev == nullptr || next == nullptr) continue;   // needs context on both sides
        for (int shift : { 12, -12 })
        {
            const int cand = n.pitch + shift;
            auto fits = [&](const RefNote* o) { return std::abs(o->pitch - cand) <= 4 && std::abs(o->pitch - n.pitch) >= 8; };
            if (fits(prev) && fits(next) && cand >= 0 && cand <= 127)
            {
                n.pitch = cand;
                ++fixed;
                break;
            }
        }
    }
    return fixed;
}

int markHarmonySuspects(NoteList& notes, const std::vector<AnalysisFrame>& frames, double hopSec,
                        const AnalyzerSettings& s)
{
    int flagged = 0;
    constexpr double kNear = 0.3;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        auto& n = notes[i];
        bool suspect = false;

        // (1) melodic excursion: a short hop away and straight back.
        const RefNote* prev = (i > 0 && n.start - notes[i - 1].end() <= kNear) ? &notes[i - 1] : nullptr;
        const RefNote* next = (i + 1 < notes.size() && notes[i + 1].start - n.end() <= kNear) ? &notes[i + 1] : nullptr;
        if (n.length < 0.35 && prev != nullptr && next != nullptr && std::abs(prev->pitch - next->pitch) <= 2
            && std::abs(n.pitch - prev->pitch) >= 5 && std::abs(n.pitch - next->pitch) >= 5)
            suspect = true;

        // (2) a second voice for most of the note.
        if (!suspect && !frames.empty() && hopSec > 0.0)
        {
            const size_t a = static_cast<size_t>(std::max(0.0, std::floor(n.start / hopSec)));
            const size_t b = std::min(frames.size(), static_cast<size_t>(std::ceil(n.end() / hopSec)));
            int checked = 0, poly = 0, strongerAbove = 0, clearlyStronger = 0;
            for (size_t f = a; f < b; ++f)
            {
                const auto& fr = frames[f];
                if (fr.midi <= 0.f || fr.altMidi <= 0.f) continue;
                ++checked;
                if (fr.altRatio >= s.polyRatio)
                {
                    ++poly;
                    if (fr.altRatio >= 1.0f && fr.altMidi > fr.midi + 2.5f) ++strongerAbove;
                    if (fr.altRatio >= 1.25f) ++clearlyStronger;
                }
            }
            if (checked >= 2)
            {
                const double pf = static_cast<double>(poly) / checked;
                if (pf >= 0.5 && (static_cast<double>(strongerAbove) / checked >= 0.4 || static_cast<double>(clearlyStronger) / checked >= 0.5))
                    suspect = true;
            }
        }

        // (3) short and barely voiced.
        if (!suspect && n.length < 0.25 && n.confidence < 0.15f) suspect = true;

        if (suspect)
        {
            n.setFlag(RefNote::HarmonySuspect, true);
            if (s.muteHarmonySuspects) n.setFlag(RefNote::Muted, true);
            ++flagged;
        }
    }
    return flagged;
}

NoteList segmentNotes(const std::vector<AnalysisFrame>& frames, double hopSec, const AnalyzerSettings& s)
{
    NoteList notes;
    const size_t n = frames.size();
    const int holdFrames = std::max(1, static_cast<int>(std::lround(s.splitHoldMs * 0.001 / hopSec)));
    const double splitSemis = s.splitCents / 100.0;
    constexpr int kMedianWindow = 40;

    std::vector<float> scratch;
    const int edgeFrames = std::max(0, static_cast<int>(std::lround(s.edgeRefineMs * 0.001 / hopSec)));
    auto emit = [&](size_t a, size_t b) {
        if (b <= a) return;
        // Voicing needs most of the YIN window to contain the note, so voiced runs start late
        // and end early by ~half a window. Refine note edges that border silence (not legato
        // splits) using the short-term energy envelope.
        scratch.clear();
        for (size_t i = a; i < b; ++i) scratch.push_back(frames[i].shortRmsDb);
        const double level = medianOf(scratch) + s.edgeRefineDb;
        if (a > 0 && frames[a - 1].midi <= 0.f)
        {
            const size_t limit = a > static_cast<size_t>(edgeFrames) ? a - static_cast<size_t>(edgeFrames) : 0;
            const size_t floorIdx = notes.empty() ? 0 : static_cast<size_t>(std::max(0.0, std::ceil(notes.back().end() / hopSec)));
            while (a > limit && a - 1 >= floorIdx && frames[a - 1].midi <= 0.f && frames[a - 1].shortRmsDb > level) --a;
        }
        if (b < frames.size() && frames[b].midi <= 0.f)
        {
            const size_t limit = std::min(frames.size(), b + static_cast<size_t>(edgeFrames));
            while (b < limit && frames[b].midi <= 0.f && frames[b].shortRmsDb > level) ++b;
        }
        scratch.clear();
        double conf = 0.0;
        // Pitch from the voiced frames only (refined edges may add unvoiced frames).
        for (size_t i = a; i < b; ++i) { if (frames[i].midi > 0.f) scratch.push_back(frames[i].midi); conf += frames[i].voicedProb; }
        if (scratch.empty()) return;
        RefNote note;
        note.start = frames[a].time;
        note.length = frames[b - 1].time - frames[a].time + hopSec;
        note.pitch = nearestNote(medianOf(scratch));
        note.confidence = static_cast<float>(conf / static_cast<double>(b - a));
        notes.push_back(note);
    };

    size_t i = 0;
    while (i < n)
    {
        if (frames[i].midi <= 0.f) { ++i; continue; }
        size_t runEnd = i;
        while (runEnd < n && frames[runEnd].midi > 0.f) ++runEnd;

        size_t segStart = i;
        size_t k = i + 1;
        while (k < runEnd)
        {
            const size_t winStart = (k - segStart > kMedianWindow) ? k - kMedianWindow : segStart;
            scratch.clear();
            for (size_t j = winStart; j < k; ++j) scratch.push_back(frames[j].midi);
            const double centre = medianOf(scratch);
            const double dev = frames[k].midi - centre;
            if (std::abs(dev) > splitSemis)
            {
                // Does the departure persist (same direction) for holdFrames?
                size_t m = k;
                while (m < runEnd && (frames[m].midi - centre) * (dev > 0 ? 1.0 : -1.0) > splitSemis) ++m;
                if (static_cast<int>(m - k) >= holdFrames)
                {
                    emit(segStart, k);
                    segStart = k;
                    k = k + 1;
                    continue;
                }
                k = m > k ? m : k + 1;
                continue;
            }
            ++k;
        }
        emit(segStart, runEnd);
        i = runEnd;
    }

    sortNotes(notes);
    mergeSamePitchGaps(notes, s.mergeGapMs * 0.001);
    removeShortNotes(notes, s.minNoteMs * 0.001);
    notes.erase(std::remove_if(notes.begin(), notes.end(),
                               [&](const RefNote& nt) { return nt.confidence < s.minConfidence; }),
                notes.end());
    mergeSamePitchGaps(notes, s.mergeGapMs * 0.001);
    return notes;
}

} // namespace pitchlane
