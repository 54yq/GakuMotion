#include "../src/GkNumberedMotion.hpp"
#include <cassert>
using namespace GakuMotion;
int main(){
 std::vector<MotionEntry> m(8);
 m[0].sourceVmd="C:/vmd/show/Song_10_x.vmd"; m[1].sourceVmd="C:/vmd/show/Song_2_x.vmd";
 m[2].sourceVmd="C:/vmd/show/Song_1_x.vmd"; m[3].sourceVmd="C:/vmd/other/Song_3_x.vmd";
 m[4].sourceVmd="C:/vmd/show/Other_3_x.vmd"; m[5].sourceVmd="C:/vmd/show/Song_A_x.vmd";
 m[6].sourceVmd="C:/vmd/show/Song_B_x.vmd"; m[7].sourceVmd=m[6].sourceVmd;
 auto p=MatchSortedMotions(m,1); assert((p==std::vector<int>{2,1,0,5,6}));
 m[0].sourceVmd="C:/vmd/show/B.vmd"; m[1].sourceVmd="C:/vmd/show/A.vmd";
 p=MatchSortedMotions(m,0); assert((p==std::vector<int>{1,0}));
 assert(NaturalMotionLess("x02", "x10")); assert(NaturalMotionLess("x9", "x10"));
}
