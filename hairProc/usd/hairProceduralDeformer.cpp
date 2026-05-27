#include "hairProceduralDeformer.h"
#include "tokens.h"

#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/meshSchema.h"
#include "pxr/imaging/hd/meshTopologySchema.h"
#include "pxr/imaging/hd/basisCurvesSchema.h"
#include "pxr/imaging/hd/primvarsSchema.h"
#include "pxr/imaging/hd/xformSchema.h"
#include "pxr/imaging/hd/basisCurvesTopologySchema.h"
#include "pxr/base/vt/value.h"
#include "pxr/base/gf/matrix3f.h"
#include "pxr/base/gf/matrix4f.h"
#include "pxr/base/gf/vec2f.h"

#include <iostream>
#include <mutex>
#include <numeric>

#include "peasyocl/Context.h"

// peasyocl Context is a singleton with a single command queue — not thread-safe.
// Karma CPU calls GetPrim (and therefore Deform) from worker threads concurrently.
static std::mutex _oclGlobalMutex;


PXR_NAMESPACE_OPEN_SCOPE

template<typename T>
std::vector<int> ArgSort(const VtArray<T>& v) {

  // initialize original index locations
  std::vector<int> idx(v.size());
  std::iota(idx.begin(), idx.end(), 0);
  std::sort(idx.begin(), idx.end(),
       [&v](int i1, int i2) {return v[i1] < v[i2];});

  return idx;
}

HairProcHairProceduralDeformer::HairProcHairProceduralDeformer(
        VtArray<HdContainerDataSourceHandle> targetContainers,
        HdContainerDataSourceHandle sourceContainer,
        const SdfPath& primPath) : _targetContainers(targetContainers), _sourceContainer(sourceContainer), _primPath(primPath.GetAsString()) {
    InitOCL();
}

VtVec3fArray HairProcHairProceduralDeformer::Deform(const HdSampledDataSource::Time& shutterOffset) {
    return _DeformOCL(shutterOffset);
}

VtVec3fArray HairProcHairProceduralDeformer::_DeformOCL(const HdSampledDataSource::Time& shutterOffset) {
    std::lock_guard<std::mutex> lock(_oclGlobalMutex);

    auto tgtPrimvarsSchema = HdPrimvarsSchema::GetFromParent(_targetContainers[0]);
    auto srcPrimvarsSchema = HdPrimvarsSchema::GetFromParent(_sourceContainer);
    auto srcCurveSchema = HdBasisCurvesSchema::GetFromParent(_sourceContainer);
    auto xformSchema = HdXformSchema::GetFromParent(_targetContainers[0]);

    GfMatrix4f xform(1.0);
    if (auto mat = xformSchema.GetMatrix()) {
        xform = static_cast<GfMatrix4f>(mat->GetTypedValue(shutterOffset));
    }

    auto srcPointsPv = srcPrimvarsSchema.GetPrimvar(HdTokens->points).GetPrimvarValue();
    auto tgtPointsPv = tgtPrimvarsSchema.GetPrimvar(HdTokens->points).GetPrimvarValue();
    auto srcCurveCountsSrc = srcCurveSchema.GetTopology().GetCurveVertexCounts();
    if (!srcPointsPv || !tgtPointsPv || !srcCurveCountsSrc) {
        return VtVec3fArray();
    }

    VtVec3fArray srcPos = srcPointsPv->GetValue(shutterOffset).UncheckedGet<VtArray<GfVec3f>>();
    VtVec3fArray tgtPos = tgtPointsPv->GetValue(shutterOffset).UncheckedGet<VtArray<GfVec3f>>();

    peasyocl::Context* _oclContext = peasyocl::Context::GetInstance();
    peasyocl::KernelHandle* procKernel = _oclContext->GetKernelHandle(_primPath + "HairProc");
    if (!procKernel) {
        return VtVec3fArray();
    }

    VtMatrix3fArray tgtFrames = _CalcTargetFrames(shutterOffset, false, tgtPos, xform);
    if (tgtFrames.empty()) {
        return VtVec3fArray();
    }

    procKernel->SetBufferData<float>(tgtPos.data()->data(), "tgtPos", tgtPos.size() * sizeof(GfVec3f));
    procKernel->SetBufferData<float>(tgtFrames.data()->data(), "frames", tgtFrames.size() * sizeof(GfMatrix3f));

    size_t global = srcCurveCountsSrc->GetTypedValue(0).size();

    int err = _oclContext->Execute(_primPath + "HairProc", cl::NDRange(global));
    if (err != CL_SUCCESS) {
        std::cout << "Failed: Could not deform OCL" << std::endl;
        return VtVec3fArray();
    }

    VtVec3fArray result;
    result.resize(srcPos.size(), GfVec3f(0));
    procKernel->ReadBufferData(result.data()->data(), "result", srcPos.size() * sizeof(GfVec3f));

    _oclContext->Finish();
    return result;
}


bool HairProcHairProceduralDeformer::InitOCL() {
    std::lock_guard<std::mutex> lock(_oclGlobalMutex);

    peasyocl::Context* _oclContext = peasyocl::Context::GetInstance();
    _oclContext->Init();
    std::string clCode = peasyocl::utils::ClFile::GetClFileByName("hairProc.cl").LoadClKernelSource();
    if (clCode.empty()) {
        return false;
    }
    peasyocl::KernelHandle* procKernel = _oclContext->AddKernel(clCode, {}, "HairProc", _primPath + "HairProc");
    peasyocl::KernelHandle* tgtKernel = _oclContext->AddKernel(clCode, {}, "CalcTargetFrames", _primPath + "TargetFrames");
    if (!procKernel || !tgtKernel) {
        return false;
    }

    auto tgtPrimvarsSchema = HdPrimvarsSchema::GetFromParent(_targetContainers[0]);
    auto tgtMeshSchema = HdMeshSchema::GetFromParent(_targetContainers[0]);
    auto xformSchema = HdXformSchema::GetFromParent(_targetContainers[0]);

    auto srcCurvesSchema = HdBasisCurvesSchema::GetFromParent(_sourceContainer);
    auto srcPrimvarsSchema = HdPrimvarsSchema::GetFromParent(_sourceContainer);
    auto srcProcSchema = HairProcHairProceduralSchema::GetFromParent(_sourceContainer);

    int err;
    int t = 0;

    /* TARGET */
    auto tgtPointsPv = tgtPrimvarsSchema.GetPrimvar(HdTokens->points).GetPrimvarValue();
    if (!tgtPointsPv) {
        return false;
    }
    VtVec3fArray tgtPos = tgtPointsPv->GetValue(t).UncheckedGet<VtArray<GfVec3f>>();

    auto tgtTopology = tgtMeshSchema.GetTopology();
    auto tgtIndicesSrc = tgtTopology.GetFaceVertexIndices();
    auto tgtCountsSrc  = tgtTopology.GetFaceVertexCounts();
    if (!tgtIndicesSrc || !tgtCountsSrc) {
        return false;
    }
    VtIntArray tgtPrimIndices = tgtIndicesSrc->GetTypedValue(t);
    VtIntArray tgtPrimLengths = tgtCountsSrc->GetTypedValue(t);
    VtIntArray tgtPrimOffset;

    size_t size = tgtPrimLengths.size();
    tgtPrimOffset.resize(size);
    int total = 0;
    for (int i = 0; i < size; i++) {
        tgtPrimOffset[i] = total;
        total += tgtPrimLengths[i];
    }
    if (tgtPrimIndices.empty() || tgtPrimLengths.empty()) {
        return false;
    }

    /* CAPTURE ATTRIBUTES */
    auto paramuvSrc = srcProcSchema.GetParamuv();
    auto primSrc    = srcProcSchema.GetPrim();
    auto restSrc    = srcProcSchema.GetRest();
    if (!paramuvSrc || !primSrc || !restSrc) {
        return false;
    }
    VtVec2fArray captUv   = paramuvSrc->GetTypedValue(t);
    VtIntArray   captPrim = primSrc->GetTypedValue(t);
    VtVec3fArray captRest = restSrc->GetTypedValue(t);
    if (captUv.empty() || captPrim.empty() || captRest.empty()) {
        return false;
    }

    /* SOURCE */
    auto srcPointsPv = srcPrimvarsSchema.GetPrimvar(HdTokens->points).GetPrimvarValue();
    auto srcCurvesTopo = srcCurvesSchema.GetTopology().GetCurveVertexCounts();
    if (!srcPointsPv || !srcCurvesTopo) {
        return false;
    }
    VtVec3fArray srcPos = srcPointsPv->GetValue(t).UncheckedGet<VtArray<GfVec3f>>();
    VtIntArray srcPrimLengths = srcCurvesTopo->GetTypedValue(t);
    VtIntArray srcPrimIndices;

    size = srcPrimLengths.size();
    srcPrimIndices.resize(size);
    total = 0;

    for (int i = 0; i < size; i++) {
        srcPrimIndices[i] = total;
        total += srcPrimLengths[i];
    }
    if (srcPos.empty() || srcPrimLengths.empty()) {
        return false;
    }

    std::vector<int> args = ArgSort(captPrim);
    _sortedCaptPrims.resize(captPrim.size());
    _uniquePrims.clear();

    int offset = 0;
    for (int j = 0; j < (int)args.size(); j++) {
        int i = args[j];
        if (j == 0 || captPrim[args[j]] != captPrim[args[j-1]]) {
            _uniquePrims.push_back(captPrim[i]);
            if (j > 0) offset++;
        }
        _sortedCaptPrims[i] = offset;
    }
    if (_uniquePrims.empty()) {
        return false;
    }

    err  = tgtKernel->AddArgument<float>(CL_MEM_READ_WRITE, "tgtPos", tgtPos.size() * sizeof(GfVec3f), (float*)nullptr);
    err |= tgtKernel->AddArgument<int>(CL_MEM_READ_ONLY, "tgtIndices", tgtPrimIndices.size() * sizeof(int), tgtPrimIndices.data());
    err |= tgtKernel->AddArgument<int>(CL_MEM_READ_ONLY, "tgtLengthsa", tgtPrimLengths.size() * sizeof(int), tgtPrimLengths.data());
    err |= tgtKernel->AddArgument<int>(CL_MEM_READ_ONLY, "tgtOffset", tgtPrimOffset.size() * sizeof(int), tgtPrimOffset.data());
    err |= tgtKernel->AddArgument<float>(CL_MEM_READ_ONLY, "tgtXform", sizeof(GfMatrix4f), (float*)nullptr);
    err |= tgtKernel->AddArgument<int>(CL_MEM_READ_ONLY, "unique_prims", _uniquePrims.size() * sizeof(int), _uniquePrims.data());
    err |= tgtKernel->AddArgument<float>(CL_MEM_WRITE_ONLY, "result", _uniquePrims.size() * sizeof(GfMatrix3f), (float*)nullptr);
    int invert_val = 0;
    err |= tgtKernel->AddArgument<int>(CL_MEM_READ_ONLY, "invert", sizeof(int), &invert_val);

    if (err != CL_SUCCESS) {
        std::cout << "Failed to add parameters to Target Context" << std::endl;
        return false;
    }

    GfMatrix4f xform(1.0);
    if (auto mat = xformSchema.GetMatrix()) {
        xform = static_cast<GfMatrix4f>(mat->GetTypedValue(t));
    }
    VtMatrix3fArray targetRestFrames = _CalcTargetFrames(0, true, tgtPos, xform);
    if (targetRestFrames.empty()) {
        return false;
    }

    err  = procKernel->AddArgument<float>(CL_MEM_WRITE_ONLY, "result", srcPos.size() * sizeof(GfVec3f), (float*)nullptr);
    err |= procKernel->AddArgument<float>(CL_MEM_READ_ONLY, "srcPos", srcPos.size() * sizeof(GfVec3f), srcPos.data()->data());
    err |= procKernel->AddArgument<int>(CL_MEM_READ_ONLY, "srcLengths", srcPrimLengths.size() * sizeof(int), srcPrimLengths.data());
    err |= procKernel->AddArgument<int>(CL_MEM_READ_ONLY, "srcIndices", srcPrimIndices.size() * sizeof(int), srcPrimIndices.data());

    err |= procKernel->AddArgument<float>(CL_MEM_READ_ONLY, "tgtPos", tgtPos.size() * sizeof(GfVec3f), (float*)nullptr);
    err |= procKernel->AddArgument<int>(CL_MEM_READ_ONLY, "tgtLengths", tgtPrimLengths.size() * sizeof(int), tgtPrimLengths.data());
    err |= procKernel->AddArgument<int>(CL_MEM_READ_ONLY, "tgtIndices", tgtPrimIndices.size() * sizeof(int), tgtPrimIndices.data());
    err |= procKernel->AddArgument<int>(CL_MEM_READ_ONLY, "tgtOffsets", tgtPrimOffset.size() * sizeof(int), tgtPrimOffset.data());

    err |= procKernel->AddArgument<float>(CL_MEM_READ_ONLY, "restFrames", _uniquePrims.size() * sizeof(GfMatrix3f), targetRestFrames.data()->data());
    err |= procKernel->AddArgument<float>(CL_MEM_READ_ONLY, "frames", _uniquePrims.size() * sizeof(GfMatrix3f), (float*)nullptr);

    err |= procKernel->AddArgument<float>(CL_MEM_READ_ONLY, "captUv", captUv.size() * sizeof(GfVec2f), captUv.data()->data());
    err |= procKernel->AddArgument<int>(CL_MEM_READ_ONLY, "captPrim", _sortedCaptPrims.size() * sizeof(int), _sortedCaptPrims.data());
    err |= procKernel->AddArgument<int>(CL_MEM_READ_ONLY, "uniquePrims", _uniquePrims.size() * sizeof(int), _uniquePrims.data());

    if (err != CL_SUCCESS) {
        std::cout << "Failed to add parameters to Deformer Context" << std::endl;
        return false;
    }
    return true;
}

VtMatrix3fArray HairProcHairProceduralDeformer::_CalcTargetFrames(
        const HdSampledDataSource::Time& shutterOffset,
        const bool invert,
        VtVec3fArray& pts,
        GfMatrix4f& xform) {
    peasyocl::Context* _oclContext = peasyocl::Context::GetInstance();
    peasyocl::KernelHandle* tgtHandle = _oclContext->GetKernelHandle(_primPath + "TargetFrames");

    tgtHandle->SetBufferData<float>(xform.data(), "tgtXform", sizeof(GfMatrix4f));
    tgtHandle->SetBufferData<float>(pts.data()->data(), "tgtPos", pts.size() * sizeof(GfVec3f));

    tgtHandle->SetArgument<int>("invert", (int)invert);

    const size_t global = _uniquePrims.size();

    int err = _oclContext->Execute(_primPath + "TargetFrames", cl::NDRange(global));
    if (err != CL_SUCCESS) {
        std::cout << "Failed: Could not execute CalcTargetFrames" << std::endl;
        return VtMatrix3fArray();
    }

    VtMatrix3fArray result;
    result.resize(global, GfMatrix3f(0));
    tgtHandle->ReadBufferData(result.data()->data(), "result", global * sizeof(GfMatrix3f));
    tgtHandle->ReadBufferData(pts.data()->data(), "tgtPos", pts.size() * sizeof(GfVec3f));

    _oclContext->Finish();
    return result;
}

PXR_NAMESPACE_CLOSE_SCOPE
