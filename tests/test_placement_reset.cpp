#include "../src/GkPlacementResetDebounce.hpp"
#include <cassert>
int main() {
 GakuMotion::PlacementResetDebounce d; void* a=(void*)1; void* b=(void*)2;
 assert(!d.Observe(0,a,3,9,0,0,0,0));
 assert(!d.Observe(0,a,3,9,1,0,0,1));
 assert(!d.Observe(0,a,3,9,2,0,0,1.4));
 assert(!d.Observe(0,a,3,9,2,0,0,1.89));
 assert(d.Observe(0,a,3,9,2,0,0,1.91));
 assert(!d.Observe(0,a,3,9,2,0,0,2.5));
 assert(!d.Observe(1,b,3,4,0,0,0,2.5));
 assert(!d.Observe(1,b,3,4,1,0,0,3));
 assert(!d.Observe(0,a,4,9,2,0,0,4));
 assert(!d.Observe(1,b,4,4,1,0,0,4));
 assert(!d.Observe(1,b,4,4,1,0,0,5));
 assert(!d.Observe(1,b,4,4,2,0,0,6));
 assert(!d.Observe(1,b,4,5,2,0,0,7));
 assert(!d.Observe(1,b,4,5,2,0,0,8));
}
