#include "GkReadOnlyCode.hpp"
#include <cstdio>
#define CHECK(x) do {if(!(x)){std::printf("FAIL line %d\n",__LINE__);return 1;}}while(0)
int main() {
    using namespace GakuMotion::code_read;
    auto* p=static_cast<unsigned char*>(VirtualAlloc(nullptr,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    CHECK(p);p[0]=0xc3;CHECK(!Executable(p));
    DWORD old{};CHECK(VirtualProtect(p,4096,PAGE_EXECUTE_READ,&old));CHECK(Executable(p));
    unsigned char data[2]{};CHECK(CopyCode(data,p,1));CHECK(data[0]==0xc3);
    CHECK(VirtualProtect(p+4096,4096,PAGE_NOACCESS,&old));CHECK(!Executable(p+4096));
    CHECK(!CopyCode(data,p+4095,2)); // access violation is contained
    MEMORY_BASIC_INFORMATION m{};CHECK(VirtualQuery(p+4096,&m,sizeof(m)));CHECK(m.Protect==PAGE_NOACCESS);
    CHECK(VirtualProtect(p,4096,PAGE_EXECUTE_READ|PAGE_GUARD,&old));CHECK(!Executable(p));
    CHECK(VirtualQuery(p,&m,sizeof(m)));CHECK((m.Protect&PAGE_GUARD)!=0); // guard not consumed by inspection
    CHECK(!Executable(nullptr));CHECK(VirtualFree(p,0,MEM_RELEASE));
    std::puts("read-only code memory tests passed");return 0;
}
