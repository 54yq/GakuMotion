#pragma once

// Included after GkGroundShadowBootstrap.hpp, before CameraBridge installs its
// actor-shadow hook. This is a bounded metadata/code-window report only: it does
// not invoke managed methods, inspect managed instances, hook, or infer layouts.
namespace BodyShadowBootstrap
{
	using Ground = GroundShadowProbeBootstrap::Access;

	inline bool Request(const std::string& root, uint64_t& sequence)
	{
		std::ifstream file(GakuMotion::MotionPathFromUtf8(root + "/body_shadow_probe.request.json"), std::ios::binary | std::ios::ate);
		if (!file) return false;
		const auto length = file.tellg();
		if (length <= 0 || length > 65536) return false;
		std::string contents(static_cast<size_t>(length), '\0');
		file.seekg(0);
		if (!file.read(&contents[0], static_cast<std::streamsize>(contents.size()))) return false;
		GakuMotion::jsonlite::Value request;
		std::string error;
		if (!GakuMotion::jsonlite::ParseText(contents, request, error) || !request.IsObject()) return false;
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

	inline std::string AllFields(HMODULE module, const Ground& api, void* klass)
	{
		using NextField = void* (*)(void*, void**);
		using FieldName = const char* (*)(void*);
		const auto next = reinterpret_cast<NextField>(GetProcAddress(module, "il2cpp_class_get_fields"));
		const auto name = reinterpret_cast<FieldName>(GetProcAddress(module, "il2cpp_field_get_name"));
		using namespace GroundShadowProbeBootstrap;
		std::ostringstream out;
		out << "{\"class\":" << Class(api, klass) << ",\"enumeration_available\":"
			<< Boolean(klass && next && name) << ",\"fields\":[";
		bool truncated = false;
		size_t emitted = 0;
		if (klass && next && name)
		{
			void* iterator = nullptr;
			for (size_t visited = 0; visited < 129; ++visited)
			{
				void* field = next(klass, &iterator);
				if (!field) break;
				if (visited == 128) { truncated = true; break; }
				void* type = api.fieldType ? api.fieldType(field) : nullptr;
				out << (emitted++ ? "," : "") << "{\"name\":" << Text(name(field))
					<< ",\"pointer\":" << Pointer(field) << ",\"fieldFlags\":";
				if (api.fieldFlags) out << api.fieldFlags(field); else out << "null";
				out << ",\"offset\":";
				if (api.fieldOffset) out << api.fieldOffset(field); else out << "null";
				out << ",\"type\":" << Type(api, type) << '}';
			}
		}
		out << "],\"field_limit\":128,\"enumeration_truncated\":" << Boolean(truncated) << '}';
		return out.str();
	}

	inline bool Capture(HMODULE gameAssembly, const std::string& root)
	{
		uint64_t sequence = 0;
		if (!gameAssembly || !Request(root, sequence)) return false;
		using namespace GroundShadowProbeBootstrap;
		const auto api = Resolve(gameAssembly);
		auto unity = [](const char* name) { return FindClass("UnityEngine.CoreModule.dll", "UnityEngine", name); };
		auto universal = [](const char* name) { return FindClass("Unity.RenderPipelines.Universal.Runtime.dll",
			"UnityEngine.Rendering.Universal", name); };
		void* feature = FindClass("vl-unity.Runtime.dll", "VL.Rendering", "DrawActorShadowPass");
		void* pass = FindClass("vl-unity.Runtime.dll", "VL.Rendering", "ActorShadowPass");
		// ActorShadowData is nested in ActorShadowPass. Resolve its identity
		// through the actual instance field rather than a top-level class lookup.
		void* dataField = pass && api.field ? api.field(pass, "_actorShadowData") : nullptr;
		void* dataType = dataField && api.fieldType ? api.fieldType(dataField) : nullptr;
		void* data = dataType && api.method.classFromType ? api.method.classFromType(dataType) : nullptr;
		void* renderingData = universal("RenderingData");
		void* cameraData = universal("CameraData");
		void* context = FindClass("UnityEngine.CoreModule.dll", "UnityEngine.Rendering", "ScriptableRenderContext");
		void* camera = unity("Camera");
		void* matrix = unity("Matrix4x4");
		void* vector = unity("Vector3");
		Query queries[]{
			{"DrawActorShadowPass", "AddRenderPasses", 2, feature},
			{"ActorShadowPass", "Setup", 2, pass},
			{"ActorShadowPass", "UpdateShadowData", 3, pass},
			{"ActorShadowPass", "DrawShadow", 3, pass},
			{"ActorShadowPass", "Execute", 2, pass},
			{"ActorShadowPass", "SetupActorShadowReceiverConstants", 1, pass},
			{"ActorShadowPass", "GetShadowTransform", 2, pass},
			{"Camera", "get_worldToCameraMatrix", 0, camera},
			{"Camera", "get_projectionMatrix", 0, camera},
			{"Camera", "get_cameraToWorldMatrix", 0, camera}
		};
		for (auto& query : queries) Enumerate(api, query);
		std::ostringstream report, windows;
		report << "{\"schema\":1,\"phase\":\"install_bootstrap\",\"pid\":" << GetCurrentProcessId()
			<< ",\"seq\":" << sequence << ",\"thread\":" << GetCurrentThreadId()
			<< ",\"game_assembly\":" << Pointer(gameAssembly)
			<< ",\"metadata_only\":true,\"captured_before_own_body_hooks\":true,\"exports\":{"
			<< "\"class_get_fields\":" << Pointer(reinterpret_cast<const void*>(GetProcAddress(gameAssembly, "il2cpp_class_get_fields")))
			<< ",\"field_get_name\":" << Pointer(reinterpret_cast<const void*>(GetProcAddress(gameAssembly, "il2cpp_field_get_name")))
			<< ",\"class_get_methods\":" << Pointer(reinterpret_cast<const void*>(api.method.methods))
			<< ",\"field_get_offset\":" << Pointer(reinterpret_cast<const void*>(api.fieldOffset))
			<< ",\"field_get_type\":" << Pointer(reinterpret_cast<const void*>(api.fieldType))
			<< ",\"class_value_size\":" << Pointer(reinterpret_cast<const void*>(api.valueSize))
			<< "},\"classes\":{\"DrawActorShadowPass\":" << Class(api, feature)
			<< ",\"ActorShadowPass\":" << Class(api, pass)
			<< ",\"ActorShadowData\":" << Class(api, data)
			<< ",\"RenderingData\":" << Class(api, renderingData)
			<< ",\"CameraData\":" << Class(api, cameraData)
			<< ",\"ScriptableRenderContext\":" << Class(api, context)
			<< ",\"Camera\":" << Class(api, camera)
			<< ",\"Matrix4x4\":" << Class(api, matrix)
			<< ",\"Vector3\":" << Class(api, vector) << "},\"fields\":{";
		const char* featureFields[]{"settings", "_pass"};
		const char* passFields[]{"_actorShadowData", "_actorShadowBias", "_supportsShadow", "getBounds",
			"_WorldToActorShadow", "_ActorShadowParams", "_ActorShadowParams2", "_LightDirection",
			"_frustumCornersCached", "_transformBoundsCached", "_filteringSettings", "_actorShadowmapTexture"};
		auto fields = [&](void* klass, const char* const* names, size_t count)
		{
			report << '[';
			for (size_t i = 0; i < count; ++i) report << (i ? "," : "") << Field(api, klass, names[i]);
			report << ']';
		};
		report << "\"DrawActorShadowPass\":";
		fields(feature, featureFields, sizeof(featureFields) / sizeof(featureFields[0]));
		report << ",\"ActorShadowPass\":";
		fields(pass, passFields, sizeof(passFields) / sizeof(passFields[0]));
		const char* cameraObjectFields[]{"m_CachedPtr"};
		report << ",\"Camera\":";
		fields(camera, cameraObjectFields, sizeof(cameraObjectFields) / sizeof(cameraObjectFields[0]));
		report << ",\"ActorShadowData_all\":" << AllFields(gameAssembly, api, data)
			<< "},\"value_types\":{";
		const char* dataFields[]{"viewMatrix", "projMatrix", "shadowTransform", "lightToWorldMatrix", "resolution",
			"startDistance2", "endDistance2", "fade", "strength", "useOffset", "facePartsStrength"};
		const char* renderingFields[]{"cameraData", "cullResults", "shadowData", "lightData"};
		const char* cameraFields[]{"camera", "m_ViewMatrix", "viewMatrix", "m_ProjectionMatrix", "m_CameraProjectionMatrix",
			"worldSpaceCameraPos", "renderType", "targetTexture"};
		const char* contextFields[]{"m_Ptr", "m_Safety"};
		const char* matrixFields[]{"m00", "m10", "m20", "m30", "m01", "m11", "m21", "m31",
			"m02", "m12", "m22", "m32", "m03", "m13", "m23", "m33"};
		const char* vectorFields[]{"x", "y", "z"};
		report << "\"ActorShadowData\":" << ValueClass(api, data, dataFields, sizeof(dataFields) / sizeof(dataFields[0]))
			<< ",\"RenderingData\":" << ValueClass(api, renderingData, renderingFields, sizeof(renderingFields) / sizeof(renderingFields[0]))
			<< ",\"CameraData\":" << ValueClass(api, cameraData, cameraFields, sizeof(cameraFields) / sizeof(cameraFields[0]))
			<< ",\"ScriptableRenderContext\":" << ValueClass(api, context, contextFields, sizeof(contextFields) / sizeof(contextFields[0]))
			<< ",\"Matrix4x4\":" << ValueClass(api, matrix, matrixFields, sizeof(matrixFields) / sizeof(matrixFields[0]))
			<< ",\"Vector3\":" << ValueClass(api, vector, vectorFields, sizeof(vectorFields) / sizeof(vectorFields[0]))
			<< "},\"methods\":[";
		windows << "{\"schema\":1,\"pid\":" << GetCurrentProcessId() << ",\"seq\":" << sequence
			<< ",\"source\":\"install_bootstrap\",\"captured_before_own_body_hooks\":true,\"methods\":[";
		for (size_t i = 0; i < sizeof(queries) / sizeof(queries[0]); ++i)
		{
			const auto& query = queries[i];
			report << (i ? "," : "") << "{\"class\":" << Quote(query.className) << ",\"name\":" << Quote(query.methodName)
				<< ",\"requested_arity\":" << query.arity << ",\"matches\":" << query.matches
				<< ",\"enumeration_truncated\":" << Boolean(query.truncated) << ",\"selected\":" << Pointer(query.selected)
				<< ",\"reason\":" << Quote(query.selected ? "unique_declared_name_and_arity" :
					(query.truncated ? "enumeration_truncated" : (query.matches > 1 ? "ambiguous" : "missing_class_or_metadata_or_match")))
				<< ",\"candidates\":" << query.candidates << '}';
			// The first 2 KiB ended before UpdateShadowData's observed branches.
			// Keep this finite and label it a window, never a proven function extent.
			windows << (i ? "," : "") << NativeWindow(gameAssembly, query,
				std::strcmp(query.methodName,"UpdateShadowData") == 0 ? 8192 : 2048);
		}
		report << "]}\n";
		windows << "]}\n";
		const bool metadataWritten = Write(root + "/body_shadow_install.json", report.str());
		const bool codeWritten = Write(root + "/body_shadow_methods.json", windows.str());
		return metadataWritten && codeWritten;
	}
}
