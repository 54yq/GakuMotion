#include <GakuPlugin.h>
#include <windows.h>
#include <cassert>
#include <cstring>
static GakuStatus Missing(void*,const char*,uint32_t,uint32_t,void*) { return GAKU_E_UNAVAILABLE; }
int wmain(int argc,wchar_t** argv) {
 assert(argc==2); HMODULE dll=LoadLibraryExW(argv[1],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);assert(dll);
 auto query=reinterpret_cast<FnGakuPlugin_Query>(GetProcAddress(dll,"GakuPlugin_Query"));assert(query);
 GakuPluginDescriptor d{};assert(query(999,sizeof(d),&d)==GAKU_E_VERSION);assert(query(1,1,&d)==GAKU_E_SIZE);
 assert(query(1,sizeof(d),&d)==GAKU_OK&&std::strcmp(d.id,"gakuframemonitor")==0&&d.activate&&d.onTick&&d.onDesktopUi);
 void* state=nullptr;assert(d.activate(nullptr,&state)==GAKU_E_INVALID_ARGUMENT);
 GakuHostApi host{1,sizeof(host),nullptr,Missing,nullptr};assert(d.activate(&host,&state)==GAKU_OK&&state);
 for(int i=0;i<3;++i)d.onTick(state,0.016f);d.onDesktopUi(state);
 FreeLibrary(dll);
}
