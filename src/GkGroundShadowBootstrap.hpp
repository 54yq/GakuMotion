#pragma once

// Included in the plugin's private namespace after StageLoader. Metadata and
// current-process code reads only: no managed invocation, hook, or bounds write.
namespace GroundShadowProbeBootstrap
{
	inline std::string Quote(const std::string& value)
	{
		return "\"" + GakuMotion::jsonlite::EscapeString(value) + "\"";
	}
	inline std::string Pointer(const void* value)
	{
		std::ostringstream out;
		out << "\"0x" << std::hex << reinterpret_cast<uintptr_t>(value) << "\"";
		return out.str();
	}
	inline std::string Text(const char* value)
	{
		if (!value) return "null";
		size_t length = 0;
		while (length < 2048 && value[length]) ++length;
		return Quote(std::string(value, length));
	}
	inline const char* Boolean(bool value) { return value ? "true" : "false"; }
	struct Access
	{
		GakuMotion::ExactMethodMetadata method;
		void* (*field)(void*, const char*) = nullptr;
		void* (*fieldType)(void*) = nullptr;
		uint32_t (*fieldFlags)(void*) = nullptr;
		int (*fieldOffset)(void*) = nullptr;
		int (*valueSize)(void*, uint32_t*) = nullptr;
		const char* (*className)(void*) = nullptr;
		const char* (*classNamespace)(void*) = nullptr;
		char* (*typeName)(void*) = nullptr;
		void (*freeMemory)(void*) = nullptr;
		uint32_t (*token)(const void*) = nullptr;
		void* (*arrayClass)(void*, uint32_t) = nullptr;
	};
	inline Access Resolve(HMODULE module)
	{
		Access api;
		// These are metadata API exports, not generated managed-method entry points.
#define GROUND_BOOT_EXPORT(member, exportName) api.member = reinterpret_cast<decltype(api.member)>(GetProcAddress(module, exportName))
		GROUND_BOOT_EXPORT(method.methods, "il2cpp_class_get_methods");
		GROUND_BOOT_EXPORT(method.name, "il2cpp_method_get_name");
		GROUND_BOOT_EXPORT(method.count, "il2cpp_method_get_param_count");
		GROUND_BOOT_EXPORT(method.flags, "il2cpp_method_get_flags");
		GROUND_BOOT_EXPORT(method.generic, "il2cpp_method_is_generic");
		GROUND_BOOT_EXPORT(method.inflated, "il2cpp_method_is_inflated");
		GROUND_BOOT_EXPORT(method.declaringClass, "il2cpp_method_get_class");
		GROUND_BOOT_EXPORT(method.parameter, "il2cpp_method_get_param");
		GROUND_BOOT_EXPORT(method.result, "il2cpp_method_get_return_type");
		GROUND_BOOT_EXPORT(method.typeCode, "il2cpp_type_get_type");
		GROUND_BOOT_EXPORT(method.byRef, "il2cpp_type_is_byref");
		GROUND_BOOT_EXPORT(method.classFromType, "il2cpp_class_from_type");
		GROUND_BOOT_EXPORT(field, "il2cpp_class_get_field_from_name");
		GROUND_BOOT_EXPORT(fieldType, "il2cpp_field_get_type");
		GROUND_BOOT_EXPORT(fieldFlags, "il2cpp_field_get_flags");
		GROUND_BOOT_EXPORT(fieldOffset, "il2cpp_field_get_offset");
		GROUND_BOOT_EXPORT(valueSize, "il2cpp_class_value_size");
		GROUND_BOOT_EXPORT(className, "il2cpp_class_get_name");
		GROUND_BOOT_EXPORT(classNamespace, "il2cpp_class_get_namespace");
		GROUND_BOOT_EXPORT(typeName, "il2cpp_type_get_name");
		GROUND_BOOT_EXPORT(freeMemory, "il2cpp_free");
		GROUND_BOOT_EXPORT(token, "il2cpp_method_get_token");
		GROUND_BOOT_EXPORT(arrayClass, "il2cpp_array_class_get");
#undef GROUND_BOOT_EXPORT
		return api;
	}
	inline std::string Class(const Access& api, void* klass)
	{
		std::ostringstream out;
		out << "{\"pointer\":" << Pointer(klass)
			<< ",\"name\":" << Text(klass && api.className ? api.className(klass) : nullptr)
			<< ",\"namespace\":" << Text(klass && api.classNamespace ? api.classNamespace(klass) : nullptr) << '}';
		return out.str();
	}
	inline std::string Type(const Access& api, void* type)
	{
		std::ostringstream out;
		out << "{\"pointer\":" << Pointer(type) << ",\"typeName\":";
		// il2cpp_type_get_name allocates. Do not call it unless its matching free
		// export is available, including on builds where optional exports differ.
		if (type && api.typeName && api.freeMemory)
		{
			char* name = api.typeName(type);
			try { out << Text(name); }
			catch (...) { if (name) api.freeMemory(name); throw; }
			if (name) api.freeMemory(name);
		}
		else out << "null";
		out << ",\"typeCode\":";
		if (type && api.method.typeCode) out << api.method.typeCode(type); else out << "null";
		out << ",\"byref\":";
		if (type && api.method.byRef) out << Boolean(api.method.byRef(type)); else out << "null";
		out << ",\"class\":" << Class(api, type && api.method.classFromType ? api.method.classFromType(type) : nullptr) << '}';
		return out.str();
	}
	inline std::string Field(const Access& api, void* klass, const char* name)
	{
		void* field = klass && api.field ? api.field(klass, name) : nullptr;
		void* type = field && api.fieldType ? api.fieldType(field) : nullptr;
		std::ostringstream out;
		out << "{\"name\":" << Quote(name) << ",\"pointer\":" << Pointer(field) << ",\"fieldFlags\":";
		if (field && api.fieldFlags) out << api.fieldFlags(field); else out << "null";
		out << ",\"offset\":";
		if (field && api.fieldOffset) out << api.fieldOffset(field); else out << "null";
		out << ",\"type\":" << Type(api, type) << '}';
		return out.str();
	}
	inline std::string ValueClass(const Access& api, void* klass, const char* const* fields, size_t count)
	{
		std::ostringstream out;
		uint32_t alignment = 0;
		const int size = klass && api.valueSize ? api.valueSize(klass, &alignment) : -1;
		out << "{\"class\":" << Class(api, klass) << ",\"valueSize\":";
		if (size >= 0) out << size; else out << "null";
		out << ",\"alignment\":";
		if (size >= 0) out << alignment; else out << "null";
		out << ",\"fields\":[";
		for (size_t i = 0; i < count; ++i) out << (i ? "," : "") << Field(api, klass, fields[i]);
		out << "]}";
		return out.str();
	}
	inline void* MethodPointer(const void* method)
	{
		void* pointer = nullptr;
		SIZE_T copied = 0;
		// Il2CppMethodInfo::methodPointer is the declared first member in this
		// plugin. Use a guarded current-process read even for a metadata handle.
		if (!method || !ReadProcessMemory(GetCurrentProcess(), method, &pointer, sizeof(pointer), &copied) || copied != sizeof(pointer))
			return nullptr;
		return pointer;
	}
	inline std::string Method(const Access& api, const void* method, void* expectedClass, uint32_t arity)
	{
		const auto& m = api.method;
		const uint32_t count = method && m.count ? m.count(method) : 0;
		void* declaring = method && m.declaringClass ? m.declaringClass(method) : nullptr;
		std::ostringstream out;
		out << "{\"method\":" << Pointer(method) << ",\"pointer\":" << Pointer(MethodPointer(method))
			<< ",\"declaredName\":" << Text(method && m.name ? m.name(method) : nullptr)
			<< ",\"declaringClass\":" << Class(api, declaring)
			<< ",\"declaring_matches\":" << Boolean(declaring == expectedClass && expectedClass)
			<< ",\"arity\":";
		if (method && m.count) out << count; else out << "null";
		out << ",\"arity_matches\":" << Boolean(method && m.count && count == arity)
			<< ",\"flags\":";
		uint32_t implementation = 0;
		if (method && m.flags) out << m.flags(method, &implementation); else out << "null";
		out << ",\"implementationFlags\":";
		if (method && m.flags) out << implementation; else out << "null";
		out << ",\"generic\":";
		if (method && m.generic) out << Boolean(m.generic(method)); else out << "null";
		out << ",\"inflated\":";
		if (method && m.inflated) out << Boolean(m.inflated(method)); else out << "null";
		out << ",\"token\":";
		if (method && api.token) out << api.token(method); else out << "null";
		out << ",\"return\":" << Type(api, method && m.result ? m.result(method) : nullptr) << ",\"parameters\":[";
		// Expected methods have at most three parameters. A corrupt/unexpected
		// metadata count cannot cause an unbounded diagnostic traversal.
		const uint32_t boundedCount = count < 64 ? count : 64;
		for (uint32_t i = 0; i < boundedCount; ++i)
			out << (i ? "," : "") << Type(api, method && m.parameter ? m.parameter(method, i) : nullptr);
		out << "],\"parameters_truncated\":" << Boolean(count > boundedCount) << '}';
		return out.str();
	}
	struct Query
	{
		const char* className;
		const char* methodName;
		uint32_t arity;
		void* klass;
		const void* selected = nullptr;
		std::string candidates;
		size_t matches = 0;
		bool truncated = false;
	};
	inline void Enumerate(const Access& api, Query& query)
	{
		const auto& m = api.method;
		std::ostringstream out;
		out << '[';
		if (query.klass && m.methods && m.name)
		{
			void* iterator = nullptr;
			size_t emitted = 0;
			for (size_t visited = 0; visited < 8192; ++visited)
			{
				const void* method = m.methods(query.klass, &iterator);
				if (!method) break;
				if (visited == 8191) query.truncated = true;
				const char* name = m.name(method);
				if (!name || std::strcmp(name, query.methodName)) continue;
				if (emitted >= 64) { query.truncated = true; continue; }
				out << (emitted++ ? "," : "") << Method(api, method, query.klass, query.arity);
				if (m.count && m.declaringClass && m.count(method) == query.arity && m.declaringClass(method) == query.klass)
				{
					++query.matches;
					query.selected = method;
				}
			}
		}
		// Only uniqueness of declared name + arity + declaring class selects a
		// code window. Parameter type guesses never suppress the raw evidence.
		if (query.matches != 1 || query.truncated) query.selected = nullptr;
		out << ']';
		query.candidates = out.str();
	}
	inline std::string NativeWindow(HMODULE module, const Query& query, size_t limit = 2048)
	{
		void* pointer = MethodPointer(query.selected);
		MEMORY_BASIC_INFORMATION region{};
		const bool queried = pointer && VirtualQuery(pointer, &region, sizeof(region)) == sizeof(region);
		const DWORD protection = region.Protect & 0xff;
		const bool readable = queried && region.State == MEM_COMMIT && !(region.Protect & PAGE_GUARD) &&
			(protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY);
		std::vector<unsigned char> bytes;
		SIZE_T copied = 0;
		size_t requested = 0;
		bool read = false;
		if (readable)
		{
			const uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
			const uintptr_t start = reinterpret_cast<uintptr_t>(region.BaseAddress);
			if (address >= start && address - start < region.RegionSize)
				requested = (std::min)((std::min)(limit, size_t{8192}), region.RegionSize - static_cast<size_t>(address - start));
			bytes.resize(requested);
			read = requested && ReadProcessMemory(GetCurrentProcess(), pointer, bytes.data(), requested, &copied) && copied == requested;
		}
		std::ostringstream out;
		out << "{\"class\":" << Quote(query.className) << ",\"name\":" << Quote(query.methodName)
			<< ",\"method\":" << Pointer(query.selected) << ",\"pointer\":" << Pointer(pointer)
			<< ",\"query_ok\":" << Boolean(queried) << ",\"read_ok\":" << Boolean(read)
			<< ",\"protection\":" << region.Protect << ",\"requested_bytes\":" << requested
			<< ",\"window_only\":true,\"function_extent_known\":false,\"rva\":\"0x" << std::hex;
		if (queried && region.AllocationBase == module)
			out << reinterpret_cast<uintptr_t>(pointer) - reinterpret_cast<uintptr_t>(module);
		else out << '0';
		out << "\",\"read_bytes\":" << std::dec << (read ? copied : 0) << ",\"bytes\":\"";
		if (read) for (unsigned char byte : bytes)
			out << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(byte);
		out << "\"}";
		return out.str();
	}
	inline bool Request(const std::string& root, uint64_t& sequence)
	{
		std::ifstream file(root + "/ground_shadow_probe.request.json", std::ios::binary | std::ios::ate);
		if (!file) return false;
		const auto length = file.tellg();
		if (length <= 0 || length > 65536) return false;
		std::string text(static_cast<size_t>(length), '\0');
		file.seekg(0);
		if (!file.read(&text[0], static_cast<std::streamsize>(text.size()))) return false;
		GakuMotion::jsonlite::Value request;
		std::string error;
		if (!GakuMotion::jsonlite::ParseText(text, request, error) || !request.IsObject()) return false;
		const auto* seq = request.Find("seq");
		if (!seq || !seq->IsNumber()) return false;
		const double number = seq->AsDouble(0);
		if (!std::isfinite(number) || number < 1 || number > 9007199254740991.0 || std::floor(number) != number) return false;
		const auto* pid = request.Find("pid");
		if (pid && !pid->IsNumber()) return false;
		const double process = pid ? pid->AsDouble(-1) : 0;
		if (!std::isfinite(process) || (process != 0 && process != GetCurrentProcessId())) return false;
		sequence = static_cast<uint64_t>(number);
		return true;
	}
	inline bool Write(const std::string& path, const std::string& contents)
	{
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file) return false;
		file << contents;
		file.flush();
		return static_cast<bool>(file);
	}
	inline bool Capture(HMODULE gameAssembly, const std::string& root)
	{
		uint64_t sequence = 0;
		if (!gameAssembly || !Request(root, sequence)) return false;
		const Access api = Resolve(gameAssembly);
		auto unity = [](const char* name) { return FindClass("UnityEngine.CoreModule.dll", "UnityEngine", name); };
		void* downscale = FindClass("vl-unity.Runtime.dll", "VL.Rendering", "DownscalePass");
		void* actor = FindClass("vl-unity.Runtime.dll", "VL.Core", "VLActorController");
		void* calc = FindClass("vl-unity.Runtime.dll", "VL.AABB", "AABBCalcJob");
		void* ao = FindClass("vl-unity.Runtime.dll", "VL", "VLActorAO");
		void* vector = unity("Vector3");
		void* bounds = unity("Bounds");
		void* plane = unity("Plane");
		void* planes = plane && api.arrayClass ? api.arrayClass(plane, 1) : nullptr;
		Query queries[]{
			{"DownscalePass", "RenderActorAO", 3, downscale},
			{"DownscalePass", "RenderActorAOs", 2, downscale},
			{"VLActorController", "get_bounds", 0, actor},
			{"VLActorController", "RunAABBJob", 0, actor},
			{"VLActorController", "ApplyAABB", 0, actor},
			{"AABBCalcJob", "Execute", 1, calc},
			{"VLActorAO", "OnLateUpdate", 0, ao}
		};
		for (auto& query : queries) Enumerate(api, query);
		std::ostringstream report, windows;
		report << "{\"schema\":1,\"phase\":\"install_bootstrap\",\"pid\":" << GetCurrentProcessId()
			<< ",\"seq\":" << sequence << ",\"thread\":" << GetCurrentThreadId()
			<< ",\"game_assembly\":" << Pointer(gameAssembly)
			<< ",\"captured_before_own_draw_hooks\":true,\"metadata_only\":true,\"exports\":{";
#define GROUND_BOOT_REPORT(member, exportName) report << exportName << Pointer(reinterpret_cast<const void*>(api.member))
		GROUND_BOOT_REPORT(method.methods, "\"class_get_methods\":");
		GROUND_BOOT_REPORT(method.name, ",\"method_get_name\":");
		GROUND_BOOT_REPORT(method.count, ",\"method_get_param_count\":");
		GROUND_BOOT_REPORT(method.flags, ",\"method_get_flags\":");
		GROUND_BOOT_REPORT(method.generic, ",\"method_is_generic\":");
		GROUND_BOOT_REPORT(method.inflated, ",\"method_is_inflated\":");
		GROUND_BOOT_REPORT(method.declaringClass, ",\"method_get_class\":");
		GROUND_BOOT_REPORT(method.parameter, ",\"method_get_param\":");
		GROUND_BOOT_REPORT(method.result, ",\"method_get_return_type\":");
		GROUND_BOOT_REPORT(method.typeCode, ",\"type_get_type\":");
		GROUND_BOOT_REPORT(method.byRef, ",\"type_is_byref\":");
		GROUND_BOOT_REPORT(method.classFromType, ",\"class_from_type\":");
		GROUND_BOOT_REPORT(field, ",\"class_get_field_from_name\":");
		GROUND_BOOT_REPORT(fieldType, ",\"field_get_type\":");
		GROUND_BOOT_REPORT(fieldFlags, ",\"field_get_flags\":");
		GROUND_BOOT_REPORT(fieldOffset, ",\"field_get_offset\":");
		GROUND_BOOT_REPORT(valueSize, ",\"class_value_size\":");
		GROUND_BOOT_REPORT(typeName, ",\"type_get_name\":");
		GROUND_BOOT_REPORT(freeMemory, ",\"free\":");
		GROUND_BOOT_REPORT(token, ",\"method_get_token\":");
		GROUND_BOOT_REPORT(arrayClass, ",\"array_class_get\":");
#undef GROUND_BOOT_REPORT
		report << "},\"classes\":{\"DownscalePass\":" << Class(api, downscale)
			<< ",\"VLActorController\":" << Class(api, actor) << ",\"AABBCalcJob\":" << Class(api, calc)
			<< ",\"VLActorAO\":" << Class(api, ao)
			<< ",\"IVLActorController\":" << Class(api, FindClass("vl-unity.Runtime.dll", "VL", "IVLActorController"))
			<< ",\"CommandBuffer\":" << Class(api, FindClass("UnityEngine.CoreModule.dll", "UnityEngine.Rendering", "CommandBuffer"))
			<< ",\"Camera\":" << Class(api, unity("Camera")) << ",\"Plane\":" << Class(api, plane)
			<< ",\"PlaneArray\":" << Class(api, planes)
			<< ",\"Object\":" << Class(api, unity("Object")) << "},\"value_types\":{";
		const char* vectorFields[]{"x", "y", "z"};
		const char* boundsFields[]{"m_Center", "m_Extents"};
		report << "\"Vector3\":" << ValueClass(api, vector, vectorFields, 3)
			<< ",\"Bounds\":" << ValueClass(api, bounds, boundsFields, 2) << "},\"methods\":[";
		windows << "{\"schema\":1,\"pid\":" << GetCurrentProcessId() << ",\"seq\":" << sequence
			<< ",\"source\":\"install_bootstrap\",\"captured_before_own_hooks\":true,\"methods\":[";
		for (size_t i = 0; i < sizeof(queries) / sizeof(queries[0]); ++i)
		{
			const auto& query = queries[i];
			report << (i ? "," : "") << "{\"class\":" << Quote(query.className) << ",\"name\":" << Quote(query.methodName)
				<< ",\"requested_arity\":" << query.arity << ",\"matches\":" << query.matches
				<< ",\"enumeration_truncated\":" << Boolean(query.truncated) << ",\"selected\":" << Pointer(query.selected)
				<< ",\"reason\":" << Quote(query.selected ? "unique_declared_name_and_arity" :
					(query.truncated ? "enumeration_truncated" : (query.matches > 1 ? "ambiguous" : "missing_class_or_metadata_or_match")))
				<< ",\"candidates\":" << query.candidates << '}';
			windows << (i ? "," : "") << NativeWindow(gameAssembly, query);
		}
		report << "]}\n";
		windows << "]}\n";
		const bool metadataWritten = Write(root + "/ground_shadow_install.json", report.str());
		const bool codeWritten = Write(root + "/ground_shadow_methods.json", windows.str());
		return metadataWritten && codeWritten;
	}
}
