// Deliberately incompatible test-only DLLs. Never deployed to the game.
#include <cstdint>
#include <cstring>
static int calls=0;
extern "C" __declspec(dllexport) std::int32_t GkVmdImporter_AbiVersion() { ++calls; return BRIDGE_FAKE_ABI; }
extern "C" __declspec(dllexport) std::int32_t GkCameraImporter_AbiVersion() { return 1; }
extern "C" __declspec(dllexport) std::int32_t GkFakeAbi_CallCount() { return calls; }
extern "C" __declspec(dllexport) std::int32_t GkVmdImport_Run(const char*,char* output,std::int32_t capacity)
{
    const char text[]="{\"ok\":true,\"dir\":\"fixture\"}";
    if (capacity>static_cast<std::int32_t>(sizeof(text))) std::memcpy(output,text,sizeof(text));
    return BRIDGE_FAKE_RETURN;
}
extern "C" __declspec(dllexport) std::int32_t GkCameraImport_Run(const char* input,char* output,std::int32_t capacity)
{ return GkVmdImport_Run(input,output,capacity); }
