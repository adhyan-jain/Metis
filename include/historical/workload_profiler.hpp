#pragma once
// BUDGET-SYM Review-2 extension: online feature extraction over the first
// sampleTarget inserted identifiers, used to characterize the current
// workload for ThresholdPredictor (include/predicted_thresholds.hpp) without
// needing a second pass over the data. See docs/architecture.md's component
// map and budget_sym_v2.tex Section VIII-F for how this feeds the ML
// threshold predictor.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <unordered_map>

namespace budgetsym {

struct WorkloadFeatures {
    double meanNameLength = 0.0;
    double prefixSimilarityCoeff = 0.0;  // fraction of consecutive pairs sharing >=4 chars
    double repeatRate = 0.0;             // duplicates / total insertions
    double scopeDepthVariance = 0.0;     // variance of scope depth across insertions
    double nameEntropy = 0.0;            // Shannon entropy over char distribution
};

class WorkloadProfiler {
public:
    int sampleTarget = 100;

    bool isComplete() const { return profileComplete_; }

    void observe(const std::string& name, int scopeDepth, bool isRepeat) {
        if (profileComplete_) return;

        totalNameLength_ += static_cast<double>(name.size());
        if (isRepeat) repeatCount_++;

        double depth = static_cast<double>(scopeDepth);
        depthSum_ += depth;
        depthSumSq_ += depth * depth;

        if (!lastName_.empty()) {
            size_t shared = 0;
            size_t n = std::min(lastName_.size(), name.size());
            while (shared < n && lastName_[shared] == name[shared]) shared++;
            if (shared >= 4) prefixSimilarPairs_++;
            consecutivePairs_++;
        }
        lastName_ = name;

        for (unsigned char c : name) charFreq_[c]++;
        totalChars_ += static_cast<long long>(name.size());

        sampleCount_++;
        if (sampleCount_ >= sampleTarget) profileComplete_ = true;
    }

    // Valid once at least one observation has been made; computed from the
    // running accumulators rather than stored samples so profiling stays
    // O(1) memory regardless of sampleTarget. Intended to be called once
    // isComplete() fires.
    WorkloadFeatures extract() const {
        WorkloadFeatures f;
        if (sampleCount_ == 0) return f;

        double n = static_cast<double>(sampleCount_);
        f.meanNameLength = totalNameLength_ / n;
        f.repeatRate = static_cast<double>(repeatCount_) / n;
        f.prefixSimilarityCoeff = consecutivePairs_ > 0
            ? static_cast<double>(prefixSimilarPairs_) / static_cast<double>(consecutivePairs_)
            : 0.0;

        double meanDepth = depthSum_ / n;
        f.scopeDepthVariance = (depthSumSq_ / n) - (meanDepth * meanDepth);
        if (f.scopeDepthVariance < 0.0) f.scopeDepthVariance = 0.0;

        f.nameEntropy = 0.0;
        if (totalChars_ > 0) {
            double total = static_cast<double>(totalChars_);
            for (auto& kv : charFreq_) {
                double p = static_cast<double>(kv.second) / total;
                if (p > 0.0) f.nameEntropy -= p * std::log2(p);
            }
        }
        return f;
    }

    int sampleCount() const { return sampleCount_; }

private:
    int sampleCount_ = 0;
    bool profileComplete_ = false;

    double totalNameLength_ = 0.0;
    int repeatCount_ = 0;

    double depthSum_ = 0.0;
    double depthSumSq_ = 0.0;

    std::string lastName_;
    int prefixSimilarPairs_ = 0;
    int consecutivePairs_ = 0;

    std::unordered_map<unsigned char, long long> charFreq_;
    long long totalChars_ = 0;
};

} // namespace budgetsym
