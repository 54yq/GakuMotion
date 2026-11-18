#include "../src/GakuMotion.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <thread>
using namespace GakuMotion;
static int importCalls=0;
static bool BeginImport(const MotionImportRequest&,std::string&) {++importCalls;return true;}
static int FailedImport(std::string& message) {message="synthetic failure";return -1;}
namespace fs=std::filesystem;
static void vmd(const fs::path& p,bool body) {
 fs::create_directories(p.parent_path()); std::ofstream f(p,std::ios::binary);
 char h[30]{};std::memcpy(h,"Vocaloid Motion Data 0002",25);f.write(h,30);char model[20]{};f.write(model,20);
 uint32_t count=body?1u:0u;f.write(reinterpret_cast<char*>(&count),4);
 if(body){char row[111]{};std::memcpy(row,"bone",4);f.write(row,111);}
}
static void clip(const fs::path& p) {
 fs::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary);f.write("GKCLIPCL",8);
 auto w=[&](auto x){f.write(reinterpret_cast<const char*>(&x),sizeof(x));};
 w(uint32_t{4});w(30.f);w(uint32_t{2});w(2.f/30.f);w(uint32_t{1});w(.083f);w(uint16_t{0});w(uint8_t{4});w(uint8_t{0});f.write("Hips",4);
 for(int i=0;i<2;++i){w(Vector3{});w(Quaternion::Identity());}w(uint8_t{0});w(uint32_t{0});
}
int main(int argc,char** argv){
 assert(argc==2);auto root=fs::path(argv[1])/"build"/"tests"/"gk-folder-test";
 assert(!fs::exists(root));fs::create_directories(root);
 auto input=root/"input";auto managed=root/"managed";
 vmd(input/"show"/"song_01_a.vmd",true);vmd(input/"show"/"song_02_b.vmd",true);vmd(input/"show"/"camera.vmd",false);
 vmd(input/"compiled"/"source.vmd",true);clip(input/"compiled"/"motion.gkclip");
 vmd(input/"compiled"/"song_03_c.vmd",true);
 vmd(input/"legacy"/"source.vmd",true);clip(input/"legacy"/"motion.gkmotion");
 vmd(managed/"cache"/"source.vmd",true);clip(managed/"cache"/"motion.gkclip");
 auto result=ScanFolderMotions({MotionPathToUtf8(input),MotionPathToUtf8(managed)},MotionPathToUtf8(managed));
 assert(result.error.empty());assert(result.files.size()==3);assert(result.compiled.size()==2);
 assert(result.compiled[0].externalFolder && fs::exists(input/"compiled"/"motion.gkclip"));
 assert(FolderCacheKey(result.files[0].source)!=FolderCacheKey(result.files[1].source));
 const auto importedDir=root/"queue_runtime"/"motions"/"already";
 clip(importedDir/"motion.gkclip");
 {std::ofstream info(importedDir/"info.json");info << "{\"source_vmd\":\"" << jsonlite::EscapeString(MotionPathToUtf8(input/"show"/"song_01_a.vmd")) << "\"}";}
 Core queue;queue.Initialize(MotionPathToUtf8(root/"queue_runtime"));
 queue.fnStartImport=BeginImport;queue.fnPollImport=FailedImport;
 queue.AddLibraryFolder(MotionPathToUtf8(input));
 for(int i=0;i<10000 && (queue.folderScan.valid() || queue.importBusy || !queue.folderQueue.empty());++i) {
  queue.PollImport();queue.PollFolderLibrary();std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
 assert(!queue.folderScan.valid() && !queue.importBusy && queue.folderQueue.empty());
 assert(importCalls==2 && queue.folderFailed==2);
 queue.RemoveLibraryFolder(MotionPathToUtf8(input));
 assert(queue.folderLibraryRoots.empty() && fs::exists(input/"compiled"/"motion.gkclip"));
 assert(fs::exists(input/"show"/"song_01_a.vmd") && fs::exists(input/"compiled"/"motion.gkclip"));
 Core core;core.Initialize(MotionPathToUtf8(root/"runtime"));
 core.folderCompiledMotions=result.compiled;assert(core.RefreshMotionLibrary());
 assert(!core.DeleteMotionFiles(result.compiled[0].name,true));assert(fs::exists(input/"compiled"/"motion.gkclip"));
 core.loadedMotionEntry=result.compiled[0];core.loadedMotionKey=result.compiled[0].name;
 assert(!core.SaveLiveOverridesToMotion());assert(!fs::exists(input/"compiled"/"bone_overrides.json") && !fs::exists(input/"legacy"/"bone_overrides.json"));
 core.fnStartImport=BeginImport;core.fnPollImport=FailedImport;
 MotionImportRequest edit;edit.editKey=result.compiled[0].name;const int callsBefore=importCalls;core.StartImport(edit);
 assert(importCalls==callsBefore && !core.importBusy);
 CameraImportRequest camera;camera.motionKey=result.compiled[0].name;core.StartCameraImport(camera);
 assert(!core.cameraImportBusy);
 core.loadedMotionKey.clear();
 for(int role: {1,2,4}) {auto dir=root/("track"+std::to_string(role));clip(dir/"motion.gkclip");MotionEntry e;e.name="R"+std::to_string(role);e.dir=MotionPathToUtf8(dir);e.sourceVmd=MotionPathToUtf8(input/"show"/("song_0"+std::to_string(role)+"_x.vmd"));core.motions.push_back(e);}
 core.sceneActorRegistry.Publish({{(void*)1,1,"a"},{(void*)2,2,"b"},{(void*)3,3,"c"},{(void*)4,4,"d"}});
 core.player.controller.SetTargetActor((void*)1); int selected=FindPlaylistIndex(core.motions,"R2");
 assert(core.SelectMotionByIndex(selected,false,true));assert(core.loadedMotionKey=="R1");
 const auto& followers=core.ensemble.Followers();assert(followers.size()>=3);
 assert(followers[0].motionKey=="R2" && !followers[1].actorToken && followers[2].motionKey=="R4");
 assert(core.ensembleAutoActors.size()==3 && core.ensembleAutoActors[1]!=0);
 core.player.Stop(); core.ensemble.Abandon();fs::remove_all(root);
}
