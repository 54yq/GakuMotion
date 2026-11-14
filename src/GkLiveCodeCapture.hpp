#pragma once
#include <set>
#include <fstream>
#include <iomanip>
// Included in the plugin namespace. Metadata enumeration and bounded code reads
// only. No runtime_invoke, hooks, memory protection changes, or object writes.
namespace LiveCodeCapture {
    inline std::set<void*> classes;
    inline std::set<uintptr_t> addresses;
    inline std::set<std::string> rows;
    inline size_t totalBytes{};
    inline unsigned files{}, failures{};
    inline std::filesystem::path directory;
    inline std::ofstream manifest;
    constexpr size_t MaxFunctionBytes=32768, MaxTotalBytes=16*1024*1024;
    using GakuMotion::code_read::CopyCode;
    using GakuMotion::code_read::Executable;
    inline bool Open() {
        if(manifest.is_open()) return true;
        const auto& root=GakuMotion::Core::Instance().diagnosticsRoot;
        if(root.empty()) return false;
        try {
            directory=GakuMotion::MotionPathFromUtf8(root)/("live_code_"+std::to_string(GetCurrentProcessId()));
            std::filesystem::create_directories(directory);
            manifest.open(directory/"methods.tsv",std::ios::out|std::ios::trunc);
            manifest<<"# schema=1; read-only; local analysis only; no game methods invoked\n"
                <<"# GameAssembly base=0x"<<std::hex<<reinterpret_cast<uintptr_t>(g_gameAssembly)<<std::dec
                <<"; limits: function=32768 total=16777216 classes=64\n"
                <<"kind\towner\tmember\taddress\timage_base\tbegin\tend\tbytes\tstatus\tfile\n";
            manifest.flush(); return bool(manifest);
        } catch(...) { return false; }
    }
    inline void Address(const std::string& owner,const std::string& member,void* code) {
        if(!Open() || !code) return;
        const auto address=reinterpret_cast<uintptr_t>(code);
        if(addresses.size()>=2048) return;
        const auto label=owner+"\t"+member+"\t"+std::to_string(address);
        if(rows.size()>=8192 || !rows.insert(label).second) return;
        DWORD64 base{};
        auto* function=RtlLookupFunctionEntry(address,&base,nullptr);
        uintptr_t begin=address,end=address;
        const char* status="prefix_only_no_unwind";
        size_t size=128;
        if(function && base+function->BeginAddress<=address && address<base+function->EndAddress) {
            begin=base+function->BeginAddress; end=base+function->EndAddress;
            size=static_cast<size_t>(end-begin);status="unwind_range";
        }
        if(size>MaxFunctionBytes) {size=MaxFunctionBytes;status="truncated_unwind_range";}
        manifest<<"code\t"<<owner<<'\t'<<member<<"\t0x"<<std::hex<<address<<"\t0x"<<base<<"\t0x"<<begin<<"\t0x"<<end<<std::dec;
        if(addresses.count(address)) {manifest<<"\t0\talias\t-\n";manifest.flush();return;}
        addresses.insert(address);
        if(size>MaxTotalBytes-totalBytes) {manifest<<"\t0\tbudget_exhausted\t-\n";manifest.flush();return;}
        std::vector<unsigned char> data(size);
        size_t copied=0;
        // Page-by-page validation; do not change protection to make a read work.
        while(copied<size) {
            const auto at=begin+copied;
            const size_t n=(std::min)(size-copied,size_t(4096-(at&4095)));
            if(!Executable(reinterpret_cast<void*>(at)) || !CopyCode(data.data()+copied,reinterpret_cast<void*>(at),n)) break;
            copied+=n;
        }
        if(copied!=size) {++failures;manifest<<"\t0\tread_denied\t-\n";manifest.flush();return;}
        const std::string file="code_"+std::to_string(files++)+".bin";
        std::ofstream binary(directory/file,std::ios::binary);
        binary.write(reinterpret_cast<const char*>(data.data()),size);binary.close();
        if(!binary) {++failures;manifest<<"\t0\twrite_failed\t-\n";}
        else {totalBytes+=size;manifest<<'\t'<<size<<'\t'<<status<<'\t'<<file<<'\n';}
        manifest.flush();
    }
    inline std::string ClassName(void* klass) {
        using Text=const char*(*)(void*);
        const auto name=reinterpret_cast<Text>(GetProcAddress(g_gameAssembly,"il2cpp_class_get_name"));
        const auto ns=reinterpret_cast<Text>(GetProcAddress(g_gameAssembly,"il2cpp_class_get_namespace"));
        const char* a=klass&&name?name(klass):nullptr;const char* b=klass&&ns?ns(klass):nullptr;
        return std::string(b?b:"")+"."+(a?a:"?");
    }
    inline void Class(void* klass,unsigned depth=0) {
        // Reserve slots for player classes discovered after scene setup. Loading
        // async state machines must not consume the entire class budget first.
        if(!klass || depth>3 || classes.size()>=64 || (depth>0 && classes.size()>=40) || classes.count(klass) || !Open()) return;
        classes.insert(klass);const auto owner=ClassName(klass);
        using Fields=void*(*)(void*,void**);
        using FieldName=const char*(*)(void*);
        using FieldOffset=size_t(*)(void*);
        const auto fields=reinterpret_cast<Fields>(GetProcAddress(g_gameAssembly,"il2cpp_class_get_fields"));
        const auto fieldName=reinterpret_cast<FieldName>(GetProcAddress(g_gameAssembly,"il2cpp_field_get_name"));
        const auto offset=reinterpret_cast<FieldOffset>(GetProcAddress(g_gameAssembly,"il2cpp_field_get_offset"));
        if(fields && fieldName && offset && g_faceRuntime.fieldType && g_faceRuntime.typeCode) {
            void* it=nullptr;unsigned n=0;
            while(void* field=fields(klass,&it)) {
                if(++n>256) break;
                void* type=g_faceRuntime.fieldType(field);const char* name=fieldName(field);
                manifest<<"field\t"<<owner<<'\t'<<(name?name:"?")<<"\t-\t-\t"<<offset(field)<<"\t-\t0\t"
                    <<(type?g_faceRuntime.typeCode(type):0)<<"\t-\n";
            }
            manifest.flush();
        }
        const auto api=GroundShadowProbeBootstrap::Resolve(g_gameAssembly).method;
        if(!api.methods || !api.name || !api.count || !api.parameter || !api.result || !api.typeCode) return;
        void* iterator=nullptr;unsigned count=0;
        while(const void* method=api.methods(klass,&iterator)) {
            if(++count>256) break;
            const char* name=api.name(method);if(!name) continue;
            std::string signature=name;signature+='(';
            const unsigned params=api.count(method);
            for(unsigned i=0;i<params && i<32;++i) {if(i)signature+=',';void* t=api.parameter(method,i);signature+=t?std::to_string(api.typeCode(t)):"?";
                if(t && api.classFromType) signature+=":"+ClassName(api.classFromType(t));
                if(t && api.byRef && api.byRef(t)) signature+='&';}
            void* result=api.result(method);signature+=") -> "+(result?std::to_string(api.typeCode(result)):"?");
            if(result && api.classFromType) signature+=":"+ClassName(api.classFromType(result));
            Address(owner,signature,GroundShadowProbeBootstrap::MethodPointer(method));
        }
        using Nested=void*(*)(void*,void**);
        auto nested=reinterpret_cast<Nested>(GetProcAddress(g_gameAssembly,"il2cpp_class_get_nested_types"));
        if(nested) {iterator=nullptr;unsigned n=0;while(void* child=nested(klass,&iterator)) {if(++n>64)break;Class(child,depth+1);}}
    }
    inline void* ObjectField(void* object,void* klass,const char* fieldName) {
        if(!object || !klass || !g_faceRuntime.findField || !g_faceRuntime.readField || !g_faceRuntime.objectClass ||
            !g_faceRuntime.fieldType || !g_faceRuntime.fieldFlags || !g_faceRuntime.typeCode ||
            g_faceRuntime.objectClass(object)!=klass) return nullptr;
        void* field=g_faceRuntime.findField(klass,fieldName);if(!field) return nullptr;
        void* type=g_faceRuntime.fieldType(field);
        if(!type || (g_faceRuntime.fieldFlags(field)&0x10)) return nullptr;
        const int code=g_faceRuntime.typeCode(type);if(code!=0x12 && code!=0x1c) return nullptr;
        void* value=nullptr;g_faceRuntime.readField(object,field,&value);
        if(value) {void* actual=g_faceRuntime.objectClass(value);const auto label=ClassName(klass)+"."+fieldName+"="+ClassName(actual);
            if(Open() && rows.size()<8192 && rows.insert(label).second){manifest<<"object_type\t"<<ClassName(klass)<<'\t'<<fieldName<<"\t-\t-\t-\t-\t0\t"<<ClassName(actual)<<"\t-\n";manifest.flush();}Class(actual);}
        return value;
    }
    inline void Capture(void* componentClass,void* actualPlayerClass,void* playerBase,void* scene) {
        try {
            // Run before this plugin installs its scheduling detours.
            Class(g_liveApi.presenterClass);Class(g_liveApi.sceneClass);Class(g_liveApi.playerClass);
            Class(FindClass("Assembly-CSharp.dll","Campus.Live","Live3DPresenter"));
            Class(componentClass);Class(actualPlayerClass);Class(playerBase);
            if(scene) {
                void* live=ObjectField(scene,g_liveApi.sceneClass,"_live");
                if(live) ObjectField(live,g_liveApi.presenterClass,"_liveSequence");
                void* livePlayer=ObjectField(scene,g_liveApi.sceneClass,"_livePlayer");
                if(livePlayer) {void* threeD=ObjectField(livePlayer,g_liveApi.playerClass,"_3dLive");
                    if(threeD) ObjectField(threeD,g_faceRuntime.objectClass(threeD),"_controller");}
            }
            Address("observed_caller","actor_evaluate",reinterpret_cast<void*>(LiveSchedulingProbe::evalCaller.load()));
            Address("observed_caller","live_end",reinterpret_cast<void*>(LiveSchedulingProbe::endCaller.load()));
            auto& monitor=GakuMotion::Core::Instance().physicsMonitor;
            monitor.codeCaptureFiles=files;monitor.codeCaptureFailures=failures;
        } catch(...) {++failures;}
    }
}
