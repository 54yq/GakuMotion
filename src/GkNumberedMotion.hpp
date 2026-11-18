#pragma once
#include "GkRuntimeConfig.hpp"
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>
namespace GakuMotion {
struct MotionGroupName { std::string prefix, parent, stem; bool hasRole{}, valid{}; };
inline std::string MotionAsciiLower(std::string s) {
 for (char& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
 return s;
}
inline MotionGroupName ParseMotionGroup(const MotionEntry& e) {
 MotionGroupName out;
 if (e.sourceVmd.empty()) return out;
 const auto path = MotionPathFromUtf8(e.sourceVmd);
 if (MotionAsciiLower(MotionPathToUtf8(path.extension())) != ".vmd") return out;
 out.stem = MotionPathToUtf8(path.stem()); out.parent = MotionPathToUtf8(path.parent_path());
 if (out.parent.empty() || out.stem.empty()) return out;
 out.valid = true;
 // A role is a single letter or a run of digits separated by '_' or '-'.
 // A standalone A/B or 01/02 is also a valid group.
 for (size_t start=0; start<out.stem.size();) {
  size_t end=start; while(end<out.stem.size() && out.stem[end]!='_' && out.stem[end]!='-') ++end;
  const auto token=out.stem.substr(start,end-start);
  const bool digits=!token.empty() && std::all_of(token.begin(),token.end(),[](unsigned char c){return c>='0'&&c<='9';});
  const bool letter=token.size()==1 && ((token[0]>='a'&&token[0]<='z')||(token[0]>='A'&&token[0]<='Z'));
  if ((start>0 || end==out.stem.size()) && (digits || letter)) {
   out.prefix = start ? MotionAsciiLower(out.stem.substr(0,start-1)) : std::string{};
   out.hasRole=true; break;
  }
  if(end==out.stem.size()) break; start=end+1;
 }
 return out;
}
inline bool NaturalMotionLess(const std::string& a,const std::string& b) {
 size_t i=0,j=0;
 while(i<a.size() && j<b.size()) {
  if(a[i]>='0'&&a[i]<='9'&&b[j]>='0'&&b[j]<='9') {
   size_t ae=i,be=j; while(ae<a.size()&&a[ae]>='0'&&a[ae]<='9')++ae; while(be<b.size()&&b[be]>='0'&&b[be]<='9')++be;
   size_t az=i,bz=j; while(az<ae&&a[az]=='0')++az; while(bz<be&&b[bz]=='0')++bz;
   if(ae-az!=be-bz) return ae-az<be-bz;
   int cmp=a.compare(az,ae-az,b,bz,be-bz); if(cmp) return cmp<0;
   if(ae-i!=be-j) return ae-i<be-j; i=ae;j=be;continue;
  }
  unsigned char ac=static_cast<unsigned char>(a[i]),bc=static_cast<unsigned char>(b[j]);
  if(ac>='A'&&ac<='Z')ac+=32; if(bc>='A'&&bc<='Z')bc+=32;
  if(ac!=bc)return ac<bc; ++i;++j;
 }
 if(i!=a.size()||j!=b.size())return i==a.size(); return a<b;
}
inline std::vector<int> MatchSortedMotions(const std::vector<MotionEntry>& motions,int selected) {
 std::vector<int> result;
 if(selected<0 || static_cast<size_t>(selected)>=motions.size())return result;
 const auto source=ParseMotionGroup(motions[selected]); if(!source.valid)return result;
 for(size_t i=0;i<motions.size();++i) {
  const auto n=ParseMotionGroup(motions[i]);
  if(!n.valid || !MotionPathEqualsUtf8(n.parent,source.parent)) continue;
  if(source.hasRole && (!n.hasRole || n.prefix!=source.prefix)) continue;
  bool duplicate=false; for(int k:result) if(MotionPathEqualsUtf8(motions[k].sourceVmd,motions[i].sourceVmd)){duplicate=true;break;}
  if(!duplicate)result.push_back(static_cast<int>(i));
 }
 std::sort(result.begin(),result.end(),[&](int a,int b){return NaturalMotionLess(ParseMotionGroup(motions[a]).stem,ParseMotionGroup(motions[b]).stem);});
 return result;
}
}
