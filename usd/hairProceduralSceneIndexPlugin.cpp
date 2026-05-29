
#include "pxr/imaging/hd/sceneIndexPluginRegistry.h"

#include "hairProceduralSceneIndexPlugin.h"
#include "hairProceduralSceneIndex.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
  _tokens, ((sceneIndexPluginName, "HairProcHairProceduralSceneIndexPlugin")));

TF_REGISTRY_FUNCTION(TfType)
{
  HdSceneIndexPluginRegistry::Define<HairProcHairProceduralSceneIndexPlugin>();
}

TF_REGISTRY_FUNCTION(HdSceneIndexPlugin)
{
  HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(TfToken(),
    _tokens->sceneIndexPluginName, nullptr, 0, HdSceneIndexPluginRegistry::InsertionOrderAtStart);
}

HairProcHairProceduralSceneIndexPlugin::HairProcHairProceduralSceneIndexPlugin() = default;
HairProcHairProceduralSceneIndexPlugin::~HairProcHairProceduralSceneIndexPlugin() = default;

HdSceneIndexBaseRefPtr HairProcHairProceduralSceneIndexPlugin::_AppendSceneIndex(
  const HdSceneIndexBaseRefPtr& inputSceneIndex, const HdContainerDataSourceHandle& inputArgs)
{

  TF_UNUSED(inputArgs);
  return HairProcHairProceduralSceneIndex::New(inputSceneIndex);
}

PXR_NAMESPACE_CLOSE_SCOPE

#ifdef _WIN32
// On Windows + Houdini 20.5, the TF_REGISTRY_FUNCTION bodies above are not
// invoked at DLL load even though the DLL loads and other static initializers
// in this translation unit fire. Perform the same registration directly here
// so the scene index plugin is actually wired into Hydra.
namespace
{
struct _HairProcWindowsRegistration
{
  _HairProcWindowsRegistration()
  {
    try
    {
      PXR_NS::HdSceneIndexPluginRegistry::Define<PXR_NS::HairProcHairProceduralSceneIndexPlugin>();
      PXR_NS::HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        PXR_NS::TfToken(), PXR_NS::TfToken("HairProcHairProceduralSceneIndexPlugin"), nullptr, 0,
        PXR_NS::HdSceneIndexPluginRegistry::InsertionOrderAtStart);
    }
    catch (...)
    {
    }
  }
};
static _HairProcWindowsRegistration _registration;
}
#endif
