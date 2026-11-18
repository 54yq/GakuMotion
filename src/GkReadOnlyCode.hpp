#pragma once
#include <windows.h>
#include <cstring>
namespace GakuMotion::code_read {
    inline bool CopyCode(void* dst,const void* src,size_t size) {
        __try {std::memcpy(dst,src,size);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    inline bool Executable(const void* p) {
        MEMORY_BASIC_INFORMATION m{};
        if(!VirtualQuery(p,&m,sizeof(m)) || m.State!=MEM_COMMIT || (m.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
        const DWORD mode=m.Protect&0xff;
        return mode==PAGE_EXECUTE || mode==PAGE_EXECUTE_READ || mode==PAGE_EXECUTE_READWRITE || mode==PAGE_EXECUTE_WRITECOPY;
    }
}
