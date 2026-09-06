#pragma once
#include <cstdint>
#include <immintrin.h>

namespace FastMath {

    // --- スカラー版 (従来通り・互換性維持) ---
    inline float fast_log2(float x) {
        if (x <= 0.00001f) return -16.6f;
        union { float f; uint32_t i; } vx = { x };
        float y = (float)vx.i;
        y *= 1.1920928955078125e-7f;
        return y - 126.94269504f;
    }

    inline float fast_exp2(float x) {
        if (x < -126.0f) return 0.0f;
        if (x > 126.0f) return 3.402823466e+38f;
        int32_t i = (int32_t)x;
        float f = x - (float)i;
        if (x < 0.0f && f != 0.0f) { i--; f += 1.0f; }
        float y = 1.0f + f * (0.693147f + f * (0.240226f + f * 0.055504f));
        union { float f; uint32_t i; } res = { y };
        res.i += (uint32_t)(i << 23);
        return res.f;
    }

    // --- AVX2 ベクトル版 (完全ブランチレス) ---

    // 8要素同時の超高速 log2 近似
    inline __m256 fast_log2_256(__m256 x) {
        // -inf を防ぐため下限を 1e-7f (約-140dB) にクランプ
        __m256 clamped_x = _mm256_max_ps(x, _mm256_set1_ps(1e-7f));

        __m256i vx = _mm256_castps_si256(clamped_x);
        __m256 y = _mm256_cvtepi32_ps(vx);
        y = _mm256_mul_ps(y, _mm256_set1_ps(1.1920928955078125e-7f));
        return _mm256_sub_ps(y, _mm256_set1_ps(126.94269504f));
    }

    // 8要素同時の超高速 exp2 近似
    inline __m256 fast_exp2_256(__m256 x) {
        // 範囲を -126.0 ~ 126.0 にクランプ
        __m256 clamped_x = _mm256_max_ps(_mm256_set1_ps(-126.0f),
            _mm256_min_ps(x, _mm256_set1_ps(126.0f)));

        __m256 floor_x = _mm256_floor_ps(clamped_x);
        __m256i i = _mm256_cvtps_epi32(floor_x);
        __m256 f = _mm256_sub_ps(clamped_x, floor_x);

        // 多項式近似: 1 + f * (0.693147 + f * (0.240226 + f * 0.055504))
        __m256 y = _mm256_add_ps(_mm256_set1_ps(1.0f),
            _mm256_mul_ps(f, _mm256_add_ps(_mm256_set1_ps(0.693147f),
                _mm256_mul_ps(f, _mm256_add_ps(_mm256_set1_ps(0.240226f),
                    _mm256_mul_ps(f, _mm256_set1_ps(0.055504f)))))));

        // 指数部へのビットシフトと加算
        __m256i exp_bits = _mm256_slli_epi32(i, 23);
        __m256i res_i = _mm256_add_epi32(_mm256_castps_si256(y), exp_bits);

        return _mm256_castsi256_ps(res_i);
    }

} // namespace FastMath