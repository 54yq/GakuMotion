#pragma once
// Renderer-only mask: no GameObject activation, transform, Animator or Light writes.
namespace ModelRendererMask {
    struct Entry { StageLoader::Handle handle{}; int id{}; bool forceOff{}, enabled{}; };
    inline const char* lastFailure="not_started";
    inline void Release(std::vector<Entry>& entries) { for(auto& e:entries) if(e.handle && StageLoader::freeHandle) StageLoader::freeHandle(e.handle); entries.clear(); }
    inline bool ReadBool(const void* method, void* object, bool& value) {
        if(!method || !object || !g_faceRuntime.invoke || !g_safeBones.unbox)return false;
        void* ex=nullptr; void* boxed=g_faceRuntime.invoke(method,object,nullptr,&ex);
        void* raw=boxed&&!ex?g_safeBones.unbox(boxed):nullptr; if(!raw)return false;
        value=*static_cast<unsigned char*>(raw)!=0; return true;
    }
    inline bool WriteBool(const void* set,const void* get,void* object,bool value) {
        if(!set || !get || !g_faceRuntime.invoke)return false;
        void* args[]{&value};void* ex=nullptr;g_faceRuntime.invoke(set,object,args,&ex);
        bool after{};return !ex && ReadBool(get,object,after) && after==value;
    }
    struct Methods { const void *children{},*element{},*getForce{},*setForce{},*getEnabled{},*setEnabled{}; void* rendererClass{}; };
    inline bool Resolve(Methods& m) {
        static Methods cached{};
        if(cached.children) { m=cached; return true; }
        void* component=FindClass("UnityEngine.CoreModule.dll","UnityEngine","Component");
        m.rendererClass=FindClass("UnityEngine.CoreModule.dll","UnityEngine","Renderer");
        void* array=FindClass("mscorlib.dll","System","Array");
        if(!array)array=FindClass("System.Private.CoreLib.dll","System","Array");
        auto& md=StageLoader::metadata;
        m.children=component?md.Find(component,"GetComponentsInChildren",false,{0x1d},{{0x12},{0x02}}):nullptr;
        m.element=array?md.Find(array,"GetValue",false,{0x1c},{{0x08}}):nullptr;
        m.getForce=m.rendererClass?md.Find(m.rendererClass,"get_forceRenderingOff",false,{0x02},{}):nullptr;
        m.setForce=m.rendererClass?md.Find(m.rendererClass,"set_forceRenderingOff",false,{0x01},{{0x02}}):nullptr;
        m.getEnabled=m.rendererClass?md.Find(m.rendererClass,"get_enabled",false,{0x02},{}):nullptr;
        m.setEnabled=m.rendererClass?md.Find(m.rendererClass,"set_enabled",false,{0x01},{{0x02}}):nullptr;
        if (!m.children) { lastFailure="GetComponentsInChildren"; return false; }
        if (!m.rendererClass) { lastFailure="Renderer_class"; return false; }
        if (!m.getEnabled || !m.setEnabled) { lastFailure="Renderer_enabled_methods"; return false; }
        if (!StageLoader::arrayLength || !StageLoader::newHandle || !StageLoader::freeHandle || !StageLoader::instanceId || !LiveActorProps::Target()) {
            lastFailure="handles_or_array_api"; return false;
        }
        // forceRenderingOff is optional: older game builds use Renderer.enabled.
        if (!m.getForce || !m.setForce) { m.getForce=nullptr; m.setForce=nullptr; }
        cached=m; return true;
    }
    inline bool Capture(void* actor,std::vector<Entry>& entries) {
        Methods m{};if(!Resolve(m))return false;
        using ClassType=void*(*)(void*);using TypeObject=void*(*)(void*);
        auto classType=reinterpret_cast<ClassType>(GetProcAddress(g_gameAssembly,"il2cpp_class_get_type"));
        auto typeObject=reinterpret_cast<TypeObject>(GetProcAddress(g_gameAssembly,"il2cpp_type_get_object"));
        if(!classType||!typeObject){lastFailure="type_api";return false;}
        void* rendererType=classType(m.rendererClass); if(!rendererType){lastFailure="renderer_type";return false;}
        void* type=typeObject(rendererType);if(!type){lastFailure="type_object";return false;}
        StageLoader::Handle typeHandle=StageLoader::newHandle(type,false);if(!typeHandle){lastFailure="type_handle";return false;}
        auto target=LiveActorProps::Target(); type=target(typeHandle);
        bool inactive=true;void* args[]{type,&inactive};void* ex=nullptr;
        void* array=type?g_faceRuntime.invoke(m.children,actor,args,&ex):nullptr;
        StageLoader::Handle arrayHandle=array&&!ex?StageLoader::newHandle(array,false):nullptr;
        StageLoader::freeHandle(typeHandle);
        if(!arrayHandle){lastFailure="children_invoke";return false;}
        array=target(arrayHandle);const auto count=array?StageLoader::arrayLength(array):0;
        if(!array||count==0||count>512){lastFailure="renderer_count";StageLoader::freeHandle(arrayHandle);return false;}
        // Most builds expose Array.GetValue. When it is stripped, use the
        // IL2CPP-reported array header size, requiring the known x64 layout.
        using HeaderSize=size_t(*)();
        auto headerSize=reinterpret_cast<HeaderSize>(GetProcAddress(g_gameAssembly,"il2cpp_array_object_header_size"));
        const bool direct=!m.element && headerSize && headerSize()==32;
        if(!m.element && !direct){lastFailure="array_element_api";StageLoader::freeHandle(arrayHandle);return false;}
        bool ok=true;
        for(uintptr_t i=0;i<count;++i) {
            int index=static_cast<int>(i);void* elementArgs[]{&index};ex=nullptr;
            array=target(arrayHandle);
            void* renderer=array ? (m.element ? g_faceRuntime.invoke(m.element,array,elementArgs,&ex) :
                reinterpret_cast<void**>(static_cast<char*>(array)+32)[i]) : nullptr;
            if(ex||!renderer||!CameraBridge::IsAlive(renderer)){lastFailure="renderer_element";ok=false;break;}
            auto handle=StageLoader::newHandle(renderer,false);if(!handle){lastFailure="renderer_handle";ok=false;break;}
            renderer=target(handle);bool force{},enabled{};
            const int id=renderer?StageLoader::ReadInt(StageLoader::instanceId,renderer):0;
            if(!id||(m.getForce&&!ReadBool(m.getForce,renderer,force))||!ReadBool(m.getEnabled,renderer,enabled)){
                lastFailure="renderer_read";StageLoader::freeHandle(handle);ok=false;break;
            }
            entries.push_back({handle,id,force,enabled});
        }
        StageLoader::freeHandle(arrayHandle);if(!ok)Release(entries);else lastFailure="none";return ok;
    }
    inline bool Apply(std::vector<Entry>& entries,bool hide) {
        Methods m{};if(!Resolve(m))return false;
        const auto target=LiveActorProps::Target();bool ok=true;size_t valid=0;
        for(auto& e:entries) {
            void* renderer=target(e.handle);
            if(!renderer||!CameraBridge::IsAlive(renderer))continue;
            if(StageLoader::ReadInt(StageLoader::instanceId,renderer)!=e.id){ok=false;continue;}
            ++valid;
            if(m.getForce) {
                bool force{};if(!ReadBool(m.getForce,renderer,force)){ok=false;continue;}
                const bool desired=hide?true:e.forceOff;
                if(force!=desired && !WriteBool(m.setForce,m.getForce,renderer,desired))ok=false;
            }
            if(!hide || !m.getForce) {
                const bool desired=hide?false:e.enabled;
                bool enabled{};if(!ReadBool(m.getEnabled,renderer,enabled) ||
                    (enabled!=desired && !WriteBool(m.setEnabled,m.getEnabled,renderer,desired)))ok=false;
            }
        }
        return ok && (!hide || valid!=0);
    }
}
