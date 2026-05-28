
#include "hairProceduralDataSources.h"
#include "hairProceduralSceneIndex.h"
#include "tokens.h"

#include "pxr/pxr.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/overlayContainerDataSource.h"

#include <memory>
#include <iostream>

PXR_NAMESPACE_OPEN_SCOPE


HairProcHairProceduralSceneIndexRefPtr
HairProcHairProceduralSceneIndex::New(const HdSceneIndexBaseRefPtr& inputSceneIndex) {
    return TfCreateRefPtr(new HairProcHairProceduralSceneIndex(inputSceneIndex));
}


HairProcHairProceduralSceneIndex::HairProcHairProceduralSceneIndex(const HdSceneIndexBaseRefPtr& inputSceneIndex) 
        : HdSingleInputFilteringSceneIndexBase(inputSceneIndex) {
}


HdSceneIndexPrim HairProcHairProceduralSceneIndex::GetPrim(const SdfPath& primPath) const {
    HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(primPath);
    if (prim.primType == HdPrimTypeTokens->basisCurves) {

        if (auto it = _deformerMap.find(primPath); it != _deformerMap.end()) {
            prim.dataSource = _HairProcDataSource::New(primPath, prim.dataSource, it->second);
        }
    }
    return prim;
}

SdfPathVector
HairProcHairProceduralSceneIndex::GetChildPrimPaths(const SdfPath& primPath) const {
    return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
}

void
HairProcHairProceduralSceneIndex::_PrimsAdded(
        const HdSceneIndexBase& sender,
        const HdSceneIndexObserver::AddedPrimEntries& entries) {

    for (const HdSceneIndexObserver::AddedPrimEntry& entry: entries) {
        if (entry.primType == HdPrimTypeTokens->basisCurves) {
            auto prim = _GetInputSceneIndex()->GetPrim(entry.primPath);
            HdBasisCurvesSchema curveSchema = HdBasisCurvesSchema::GetFromParent(prim.dataSource);
            HdPrimvarsSchema primvarSchema = HdPrimvarsSchema::GetFromParent(prim.dataSource);
            HairProcHairProceduralSchema hairProcSchema = HairProcHairProceduralSchema::GetFromParent(prim.dataSource);

            if (curveSchema && primvarSchema && hairProcSchema) {
                _init_deformer(entry.primPath, hairProcSchema, curveSchema, primvarSchema);
            } else if (curveSchema && primvarSchema) {
                // Fallback for pipelines that don't invoke UsdImagingAPISchemaAdapter
                // (e.g. Houdini's HdLegacyPrimSceneIndex). Read binding data from primvars.
                auto primPv    = primvarSchema.GetPrimvar(TfToken("hairProc_prim"));
                auto uvPv      = primvarSchema.GetPrimvar(TfToken("hairProc_paramuv"));
                auto restPv    = primvarSchema.GetPrimvar(TfToken("hairProc_rest"));
                auto targetPv  = primvarSchema.GetPrimvar(TfToken("hairProc_target"));

                if (primPv && uvPv && restPv && targetPv) {
                    VtValue vPrim   = primPv.GetPrimvarValue()->GetValue(0);
                    VtValue vUv     = uvPv.GetPrimvarValue()->GetValue(0);
                    VtValue vRest   = restPv.GetPrimvarValue()->GetValue(0);
                    VtValue vTarget = targetPv.GetPrimvarValue()->GetValue(0);

                    if (!vPrim.IsHolding<VtIntArray>() || !vUv.IsHolding<VtVec2fArray>() ||
                        !vRest.IsHolding<VtVec3fArray>() || !vTarget.IsHolding<VtStringArray>()) {
                        continue;
                    }

                    VtArray<SdfPath> targetPaths;
                    for (const auto& s : vTarget.UncheckedGet<VtStringArray>()) {
                        targetPaths.push_back(SdfPath(s));
                    }

                    auto hairProcRetained = HdRetainedContainerDataSource::New(
                        HairProcHairProceduralSchemaTokens->prim,
                            HdRetainedTypedSampledDataSource<VtIntArray>::New(vPrim.UncheckedGet<VtIntArray>()),
                        HairProcHairProceduralSchemaTokens->paramuv,
                            HdRetainedTypedSampledDataSource<VtVec2fArray>::New(vUv.UncheckedGet<VtVec2fArray>()),
                        HairProcHairProceduralSchemaTokens->rest,
                            HdRetainedTypedSampledDataSource<VtVec3fArray>::New(vRest.UncheckedGet<VtVec3fArray>()),
                        HairProcHairProceduralSchemaTokens->target,
                            HdRetainedTypedSampledDataSource<VtArray<SdfPath>>::New(targetPaths)
                    );

                    auto augmentedSource = HdOverlayContainerDataSource::New(
                        HdRetainedContainerDataSource::New(
                            HairProcHairProceduralSchemaTokens->hairProcedural, hairProcRetained),
                        prim.dataSource
                    );

                    HairProcHairProceduralSchema syntheticSchema =
                        HairProcHairProceduralSchema::GetFromParent(augmentedSource);
                    if (syntheticSchema) {
                        _init_deformer(entry.primPath, syntheticSchema, curveSchema, primvarSchema, augmentedSource);
                    }
                }
            }
        }
    }

    if (!_IsObserved()) {
        return;
    }
    _SendPrimsAdded(entries);
}

void
HairProcHairProceduralSceneIndex::_PrimsRemoved(
        const HdSceneIndexBase& sender,
        const HdSceneIndexObserver::RemovedPrimEntries& entries) {
    if (!_IsObserved()) {
        return;
    }
    _SendPrimsRemoved(entries);
}

void
HairProcHairProceduralSceneIndex::_PrimsDirtied(
        const HdSceneIndexBase& sender,
        const HdSceneIndexObserver::DirtiedPrimEntries& entries) {

    // If any prims in entries are part of _targets, we need to also dirty their sources, ie the hairProcedural prims
    if (!_IsObserved()) {
        return;
    }

    HdSceneIndexObserver::DirtiedPrimEntries dirty = entries;

    for (const HdSceneIndexObserver::DirtiedPrimEntry& entry: entries) {
        if (auto it = _targets.find(entry.primPath); it != _targets.end()) {
            for (const SdfPath& path : it->second) {

                auto prim = _GetInputSceneIndex()->GetPrim(path);
                HdPrimvarsSchema primvarSchema = HdPrimvarsSchema::GetFromParent(prim.dataSource);
                dirty.emplace_back(path, primvarSchema.GetPointsLocator());
            }
        }
    }
    _SendPrimsDirtied(dirty);
}

void HairProcHairProceduralSceneIndex::_init_deformer(
        const SdfPath& primPath,
        HairProcHairProceduralSchema& procSchema,
        HdBasisCurvesSchema& basisCurvesSchema,
        HdPrimvarsSchema& primvarSchema,
        HdContainerDataSourceHandle sourceDs){

    if (_deformerMap.count(primPath)) {
        return;
    }

    HdPathArrayDataSourceHandle target = procSchema.GetTarget();
    VtArray<SdfPath> targets = target->GetTypedValue(0);
    if (!targets.size()) {
        return;
    }

    VtArray<HdContainerDataSourceHandle> targetDs;
    for (auto it: targets) {
        HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(it);
        targetDs.push_back(prim.dataSource);
    }
    if (targetDs.size() == 0){
        return;
    }

    if (!sourceDs) {
        sourceDs = _GetInputSceneIndex()->GetPrim(primPath).dataSource;
    }
    HairProcHairProceduralDeformerSharedPtr deformer = std::make_shared<HairProcHairProceduralDeformer>(targetDs, sourceDs, primPath);

    _deformerMap[primPath] = deformer;
    for (SdfPath& path : targets) {
        _targets[path].insert(primPath);
    }
}

PXR_NAMESPACE_CLOSE_SCOPE

