/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**************************************************************************************************
 *** This file was autogenerated from GrRectBlurEffect.fp; do not modify.
 **************************************************************************************************/
#ifndef GrRectBlurEffect_DEFINED
#define GrRectBlurEffect_DEFINED

#include "include/core/SkM44.h"
#include "include/core/SkTypes.h"

#include <cmath>
#include "include/core/SkRect.h"
#include "include/core/SkScalar.h"
#include "include/gpu/GrRecordingContext.h"
#include "src/core/SkBlurMask.h"
#include "src/core/SkMathPriv.h"
#include "src/gpu/GrBitmapTextureMaker.h"
#include "src/gpu/GrProxyProvider.h"
#include "src/gpu/GrRecordingContextPriv.h"
#include "src/gpu/GrShaderCaps.h"
#include "src/gpu/effects/GrTextureEffect.h"

#include "src/gpu/GrFragmentProcessor.h"

class GrRectBlurEffect : public GrFragmentProcessor {
public:
    static std::unique_ptr<GrFragmentProcessor> MakeIntegralFP(GrRecordingContext* context,
                                                               float sixSigma) {
        // The texture we're producing represents the integral of a normal distribution over a
        // six-sigma range centered at zero. We want enough resolution so that the linear
        // interpolation done in texture lookup doesn't introduce noticeable artifacts. We
        // conservatively choose to have 2 texels for each dst pixel.
        int minWidth = 2 * sk_float_ceil2int(sixSigma);
        // Bin by powers of 2 with a minimum so we get good profile reuse.
        int width = std::max(SkNextPow2(minWidth), 32);

        static const GrUniqueKey::Domain kDomain = GrUniqueKey::GenerateDomain();
        GrUniqueKey key;
        GrUniqueKey::Builder builder(&key, kDomain, 1, "Rect Blur Mask");
        builder[0] = width;
        builder.finish();

        SkMatrix m = SkMatrix::Scale(width / sixSigma, 1.f);

        GrProxyProvider* proxyProvider = context->priv().proxyProvider();
        if (sk_sp<GrTextureProxy> proxy = proxyProvider->findOrCreateProxyByUniqueKey(key)) {
            GrSwizzle swizzle = context->priv().caps()->getReadSwizzle(proxy->backendFormat(),
                                                                       GrColorType::kAlpha_8);
            GrSurfaceProxyView view{std::move(proxy), kTopLeft_GrSurfaceOrigin, swizzle};
            return GrTextureEffect::Make(std::move(view), kPremul_SkAlphaType, m,
                                         GrSamplerState::Filter::kLinear);
        }

        SkBitmap bitmap;
        if (!bitmap.tryAllocPixels(SkImageInfo::MakeA8(width, 1))) {
            return {};
        }
        *bitmap.getAddr8(0, 0) = 255;
        const float invWidth = 1.f / width;
        for (int i = 1; i < width - 1; ++i) {
            float x = (i + 0.5f) * invWidth;
            x = (-6 * x + 3) * SK_ScalarRoot2Over2;
            float integral = 0.5f * (std::erf(x) + 1.f);
            *bitmap.getAddr8(i, 0) = SkToU8(sk_float_round2int(255.f * integral));
        }
        *bitmap.getAddr8(width - 1, 0) = 0;
        bitmap.setImmutable();

        GrBitmapTextureMaker maker(context, bitmap, GrImageTexGenPolicy::kNew_Uncached_Budgeted);
        auto view = maker.view(GrMipmapped::kNo);
        if (!view) {
            return {};
        }
        SkASSERT(view.origin() == kTopLeft_GrSurfaceOrigin);
        proxyProvider->assignUniqueKeyToProxy(key, view.asTextureProxy());
        return GrTextureEffect::Make(std::move(view), kPremul_SkAlphaType, m,
                                     GrSamplerState::Filter::kLinear);
    }

    static std::unique_ptr<GrFragmentProcessor> Make(std::unique_ptr<GrFragmentProcessor> inputFP,
                                                     GrRecordingContext* context,
                                                     const GrShaderCaps& caps,
                                                     const SkRect& rect,
                                                     float sigma) {
        SkASSERT(rect.isSorted());
        if (!caps.floatIs32Bits()) {
            // We promote the math that gets us into the Gaussian space to full float when the rect
            // coords are large. If we don't have full float then fail. We could probably clip the
            // rect to an outset device bounds instead.
            if (SkScalarAbs(rect.fLeft) > 16000.f || SkScalarAbs(rect.fTop) > 16000.f ||
                SkScalarAbs(rect.fRight) > 16000.f || SkScalarAbs(rect.fBottom) > 16000.f) {
                return nullptr;
            }
        }

        const float sixSigma = 6 * sigma;
        std::unique_ptr<GrFragmentProcessor> integral = MakeIntegralFP(context, sixSigma);
        if (!integral) {
            return nullptr;
        }

        // In the fast variant we think of the midpoint of the integral texture as aligning
        // with the closest rect edge both in x and y. To simplify texture coord calculation we
        // inset the rect so that the edge of the inset rect corresponds to t = 0 in the texture.
        // It actually simplifies things a bit in the !isFast case, too.
        float threeSigma = sixSigma / 2;
        SkRect insetRect = {rect.fLeft + threeSigma, rect.fTop + threeSigma,
                            rect.fRight - threeSigma, rect.fBottom - threeSigma};

        // In our fast variant we find the nearest horizontal and vertical edges and for each
        // do a lookup in the integral texture for each and multiply them. When the rect is
        // less than 6 sigma wide then things aren't so simple and we have to consider both the
        // left and right edge of the rectangle (and similar in y).
        bool isFast = insetRect.isSorted();
        return std::unique_ptr<GrFragmentProcessor>(
                new GrRectBlurEffect(std::move(inputFP), insetRect, std::move(integral), isFast,
                                     GrSamplerState::Filter::kLinear));
    }
    GrRectBlurEffect(const GrRectBlurEffect& src);
    std::unique_ptr<GrFragmentProcessor> clone() const override;
    const char* name() const override { return "RectBlurEffect"; }
    SkRect rect;
    bool isFast;

private:
    GrRectBlurEffect(std::unique_ptr<GrFragmentProcessor> inputFP,
                     SkRect rect,
                     std::unique_ptr<GrFragmentProcessor> integral,
                     bool isFast,
                     GrSamplerState samplerParams)
            : INHERITED(kGrRectBlurEffect_ClassID,
                        (OptimizationFlags)(inputFP ? ProcessorOptimizationFlags(inputFP.get())
                                                    : kAll_OptimizationFlags) &
                                kCompatibleWithCoverageAsAlpha_OptimizationFlag)
            , rect(rect)
            , isFast(isFast) {
        this->registerChild(std::move(inputFP), SkSL::SampleUsage::PassThrough());
        SkASSERT(integral);
        this->registerChild(std::move(integral), SkSL::SampleUsage::Explicit());
    }
    GrGLSLFragmentProcessor* onCreateGLSLInstance() const override;
    void onGetGLSLProcessorKey(const GrShaderCaps&, GrProcessorKeyBuilder*) const override;
    bool onIsEqual(const GrFragmentProcessor&) const override;
    GR_DECLARE_FRAGMENT_PROCESSOR_TEST
    typedef GrFragmentProcessor INHERITED;
};
#endif
