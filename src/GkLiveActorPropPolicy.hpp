#pragma once
#include <algorithm>
#include <cctype>
#include <string>
namespace GakuMotion::live_actor_props
{
    inline bool IsSupportedPair(const std::string& root, const std::string& prop) {
        if(root.size()<=5 || root.size()>120 || root.compare(0,5,"Root_")!=0 || prop!="Prop_"+root.substr(5))return false;
        std::string suffix=root.substr(5);
        std::transform(suffix.begin(),suffix.end(),suffix.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        const char* protectedNames[]{"body","face","hair","head","bone","skeleton","hand","foot","costume"};
        for(const char* protectedName:protectedNames) if(suffix.find(protectedName)!=std::string::npos)return false;
        return true;
    }
}
