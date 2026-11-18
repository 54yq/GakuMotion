#pragma once
#include "GkRuntimeConfig.hpp"
#include <sstream>
#include <iomanip>
#include <map>
#include <cstring>
namespace GakuMotion {
struct FolderMotionCandidate { std::string source, title, audio, camera; };
struct FolderScanResult { std::vector<FolderMotionCandidate> files; std::vector<MotionEntry> compiled; std::string error; };
inline bool HasBodyMotion(const std::filesystem::path& path) {
 std::ifstream f(path,std::ios::binary|std::ios::ate); if(!f)return false; const auto len=static_cast<std::streamoff>(f.tellg());
 if(len<54)return false; f.seekg(0); char h[30]{};f.read(h,30);
 if(!f || std::strncmp(h,"Vocaloid Motion Data ",21)!=0)return false;
 const auto model=std::strncmp(h,"Vocaloid Motion Data 0002",25)==0?20:10;
 f.seekg(30+model); uint32_t count=0; f.read(reinterpret_cast<char*>(&count),4);
 if(!f || !count || uint64_t(count)>uint64_t(len-(30+model+4))/111) return false;
 for(uint32_t i=0;i<count;++i) {
  char name[15]{}; f.read(name,15); if(!f)return false;
  const bool eye=(std::memcmp(name,"\x97\xBC\x96\xDA",4)==0 || std::memcmp(name,"\x8D\xB6\x96\xDA",4)==0 ||
    std::memcmp(name,"\x89\x45\x96\xDA",4)==0 || std::memcmp(name,"\x96\xDA\x96\xDF",4)==0 ||
    (std::memcmp(name,"\x96\xDA",2)==0 && (name[2]==0 || name[2]==' ')));
  if(name[0] && !eye)return true;
  f.seekg(96,std::ios::cur);
 }
 return false;
}
inline std::string FolderCacheKey(const std::string& source) {
 auto path=MotionPathToUtf8(MotionPathFromUtf8(source).lexically_normal());
#ifdef _WIN32
 for(char& c:path) if(c>='A'&&c<='Z')c=char(c-'A'+'a');
#endif
 uint64_t h=14695981039346656037ull;for(unsigned char c:path){h^=c;h*=1099511628211ull;}
 std::ostringstream out;out<<"folder_"<<std::hex<<std::setw(16)<<std::setfill('0')<<h;return out.str();
}
inline bool FolderIsWithin(std::filesystem::path path, const std::string& parent) {
 if(parent.empty()) return false; std::error_code ec; path=std::filesystem::absolute(path,ec).lexically_normal(); if(ec)return false;
 for(;;) { if(MotionPathEqualsUtf8(MotionPathToUtf8(path),parent))return true; auto next=path.parent_path(); if(next==path || next.empty())return false; path=next; }
}
inline FolderScanResult ScanFolderMotions(const std::vector<std::string>& roots, const std::string& managedRoot = {}) {
 FolderScanResult out; std::error_code ec; size_t visited=0;
 std::map<std::string,std::vector<MotionEntry>> compiledParentCache;
 struct Media {std::vector<std::string> audio,cameras,vmdCameras;};
 std::map<std::string,Media> mediaCache;
 for(const auto& root:roots) {
  const auto p=MotionPathFromUtf8(root); if(FolderIsWithin(p,managedRoot)) continue;
  if(!std::filesystem::is_directory(p,ec)||ec) {out.error="部分文件夹无法访问";ec.clear();continue;}
  std::filesystem::recursive_directory_iterator it(p,std::filesystem::directory_options::skip_permission_denied,ec),end;
  if(ec){out.error="部分文件夹无法读取";ec.clear();continue;}
  for(;it!=end;it.increment(ec)) {
   if(ec){out.error="部分文件无法读取";ec.clear();continue;}
   if(++visited>20000){out.error="文件过多，扫描已限制在 20000 项";return out;}
   if(it.depth()>=16)it.disable_recursion_pending();
   if(it->is_symlink(ec)) {it.disable_recursion_pending();ec.clear();continue;} ec.clear();
   if(it->is_directory(ec)) { if(!managedRoot.empty() && MotionPathEqualsUtf8(MotionPathToUtf8(it->path()),managedRoot)) it.disable_recursion_pending(); continue; }
   ec.clear(); if(!it->is_regular_file(ec)){ec.clear();continue;}
   if(it->path().filename()=="motion.gkclip" || it->path().filename()=="motion.gk_world_dump" || it->path().filename()=="motion.gkmotion") {
    const auto directory=it->path().parent_path();
    const auto parent=MotionPathToUtf8(directory.parent_path());
    auto [cache,inserted]=compiledParentCache.try_emplace(parent);
    if(inserted) cache->second=ScanMotionLibraryDetailed(parent).motions;
    for(auto m:cache->second) if(MotionPathEqualsUtf8(m.dir,MotionPathToUtf8(directory))) {
      m.name="external_"+FolderCacheKey(m.dir); m.externalFolder=true; m.libraryFolder=MotionPathToUtf8(directory);
      if(m.sourceVmd.empty() && std::filesystem::is_regular_file(directory/"source.vmd",ec)) m.sourceVmd=MotionPathToUtf8(directory/"source.vmd");
      ec.clear();
      if(std::none_of(out.compiled.begin(),out.compiled.end(),[&](const auto& e){return MotionPathEqualsUtf8(e.dir,m.dir);})) out.compiled.push_back(std::move(m));
      break;
    }
    continue;
   }
   auto ext=MotionPathToUtf8(it->path().extension());for(char& c:ext)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
   if(ext!=".vmd" || !HasBodyMotion(it->path())) continue;
   {
     const auto dir=it->path().parent_path(); bool compiled=false;
     for(const char* filename:{"motion.gkclip","motion.gk_world_dump","motion.gkmotion"}) {
       const auto file=dir/filename; MotionEntry probe; std::string error;
       if(std::filesystem::is_regular_file(file,ec) && !ec && ProbeMotionLibraryFile(file,probe,error)) {compiled=true;break;} ec.clear();
     }
     if(compiled) {
       if(it->path().filename()=="source.vmd") {ec.clear();continue;}
       jsonlite::Value info; std::string parseError;
       if(jsonlite::ParseFile(MotionPathToUtf8(dir/"info.json"),info,parseError)) {
         const auto source=info.StringPath("source_vmd", "");
         if(!source.empty() && MotionPathEqualsUtf8(source,MotionPathToUtf8(it->path()))) {ec.clear();continue;}
       }
     }
   } ec.clear();
   FolderMotionCandidate c; c.source=MotionPathToUtf8(it->path()); c.title=FolderCacheKey(c.source);
   const auto parent=MotionPathToUtf8(it->path().parent_path());
   auto [media, fresh]=mediaCache.try_emplace(parent);
   if(fresh) {
     std::error_code local; std::filesystem::directory_iterator siblings(it->path().parent_path(),local), finish;
     for(; !local && siblings!=finish; siblings.increment(local)) {
      if(!siblings->is_regular_file(local)){local.clear();continue;}
      auto e=MotionPathToUtf8(siblings->path().extension()); for(char& ch:e)if(ch>='A'&&ch<='Z')ch=char(ch-'A'+'a');
      if(e==".wav")media->second.audio.push_back(MotionPathToUtf8(siblings->path()));
      if(e==".gkcamera")media->second.cameras.push_back(MotionPathToUtf8(siblings->path()));
      if(e==".vmd") { auto name=MotionPathToUtf8(siblings->path().filename());
        for(char& ch:name)if(ch>='A'&&ch<='Z')ch=char(ch-'A'+'a');
        if((name.find("camera")!=std::string::npos || name.find("镜头")!=std::string::npos || name.find("鏡頭")!=std::string::npos) && !HasBodyMotion(siblings->path()))
          media->second.vmdCameras.push_back(MotionPathToUtf8(siblings->path()));
      }
     }
   }
   const auto& mediaFiles=media->second;
   if(mediaFiles.audio.size()==1)c.audio=mediaFiles.audio.front();
   if(mediaFiles.cameras.size()==1)c.camera=mediaFiles.cameras.front();
   else if(mediaFiles.cameras.empty() && mediaFiles.vmdCameras.size()==1)c.camera=mediaFiles.vmdCameras.front();
   if(std::none_of(out.files.begin(),out.files.end(),[&](const auto& e){return MotionPathEqualsUtf8(e.source,c.source);})) out.files.push_back(std::move(c));
   if(out.files.size()>=2048){out.error="动作数量过多，扫描已限制在 2048 项";return out;}
  }
 }
 std::sort(out.files.begin(),out.files.end(),[](const auto& a,const auto& b){return a.source<b.source;});
 return out;
}
}
