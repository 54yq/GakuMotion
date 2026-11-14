#include "FrameDetector.hpp"
#include <cassert>
int main(){FrameDetector d;for(int i=0;i<120;++i)assert(!d.sample(16.67));assert(d.sample(70));assert(!d.sample(25));assert(d.sample(2500));assert(!d.sample(11000));assert(!d.sample(16.67));}
