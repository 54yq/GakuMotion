#include "../src/GakuMotion.hpp"
#include "../src/GkCsharpImport.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <thread>
namespace fs=std::filesystem;
using namespace GakuMotion;
template<class T> void put(std::ofstream& f,T x){f.write(reinterpret_cast<const char*>(&x),sizeof(x));}
void zero(std::ofstream& f,size_t n){std::string x(n,'\0');f.write(x.data(),static_cast<std::streamsize>(x.size()));}
void vmd(const fs::path& p){
 std::ofstream f(p,std::ios::binary);const std::string h="Vocaloid Motion Data 0002";f.write(h.data(),h.size());zero(f,30-h.size());zero(f,20);put<uint32_t>(f,2);
 for(uint32_t frame:{0u,2u}){const char center[]{char(0x83),char(0x5a),char(0x83),char(0x93),char(0x83),char(0x5e),char(0x81),char(0x5b)};
 f.write(center,sizeof(center));zero(f,15-sizeof(center));put(f,frame);for(int i=0;i<6;++i)put(f,0.f);put(f,1.f);zero(f,64);}
 for(int i=0;i<5;++i)put<uint32_t>(f,0);
}
int wmain(int argc,wchar_t** argv){
 assert(argc==3);auto base=fs::path(argv[1])/"build"/"tests"/"gk-folder-e2e";assert(!fs::exists(base));fs::create_directories(base/"source");
 vmd(base/"source"/"Song_01_A.vmd");vmd(base/"source"/"Song_02_B.vmd");
 csharp_import::SetPluginsRoot(MotionPathToUtf8(fs::path(argv[2])));Core core;core.Initialize(MotionPathToUtf8(base/"plugin"));
 core.fnStartImport=csharp_import::Begin;core.fnPollImportProgress=csharp_import::Poll;
 core.AddLibraryFolder(MotionPathToUtf8(base/"source"));
 for(int i=0;i<60000 && (core.folderScan.valid() || core.importBusy || !core.folderQueue.empty());++i){core.PollImport();core.PollFolderLibrary();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 if(core.folderFailed || core.folderImported!=2){std::fprintf(stderr,"imported=%zu failed=%zu notice=%s\n",core.folderImported,core.folderFailed,core.playlistNotice.c_str());return 1;}
 assert(!core.importBusy && !core.folderScan.valid() && core.folderQueue.empty());assert(core.motions.size()==2);
 for(const auto& m:core.motions){assert(m.name.rfind("folder_",0)==0);assert(fs::exists(MotionPathFromUtf8(m.dir)/"motion.gkclip"));assert(fs::exists(MotionPathFromUtf8(m.sourceVmd)));}
 auto matched=MatchNumberedMotions(core.motions,0);assert(matched.size()==2 && matched[1]>=0 && matched[2]>=0);
 core.AddLibraryFolder(MotionPathToUtf8(base/"source"));
 for(int i=0;i<10000 && core.folderScan.valid();++i){core.PollFolderLibrary();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 assert(core.folderQueue.empty() && !core.importBusy && core.folderImported==2);
 csharp_import::Shutdown();fs::remove_all(base);
}
