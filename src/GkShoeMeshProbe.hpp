#pragma once

// Included in GakuMotionPlugin.cpp's anonymous namespace after the IL2CPP helpers.
// This is a one-shot evidence collector, not a contact solver. It never changes
// the actor, its bones, renderer settings, sharedMesh, or measurement profiles.
namespace ShoeMeshProbe
{
	using Json = GakuMotion::jsonlite::Value;
	using Method = const void*;
	// Current 6000.0.77f1 GameAssembly uses an opaque pointer-width GCHandle.
	// A uint32_t signature truncates it on this build; confirmed by crash dump,
	// exact relink MAP and current VRify's live IL2CPP binding. This is not the old
	// uint32 IL2CPP ABI. Keep every return/argument/storage site the same width.
	using Handle = void*;
	static_assert(sizeof(Handle) == sizeof(void*));
	constexpr int kRendererLimit = 128, kMeshVertexLimit = 100000, kTotalVertexLimit = 250000;
	constexpr int kCandidateLimit = 50000;
	constexpr float kNeighborhoodRadius = 0.4f;

	enum class Stage { Handle, Renderers, Vertices };

	enum class Source { Photo, OfficialLive };

	struct SnapshotContext
	{
		Source source = Source::Photo;
		DWORD processId = 0;
		int sceneHandle = 0;
		std::string sceneName, costumeId;
	};

	inline const char* StageName(Stage stage)
	{
		switch (stage)
		{
		case Stage::Renderers: return "renderers";
		case Stage::Vertices: return "vertices";
		default: return "handle";
		}
	}

	inline bool ParseStage(const Json& request, Stage& stage, std::string& error)
	{
		stage = Stage::Handle;
		const Json* value = request.Find("stage");
		if (!value)return true;
		if (value->IsString())
		{
			if (value->stringValue == "handle")return true;
			if (value->stringValue == "renderers")
			{
				stage = Stage::Renderers;
				return true;
			}
			if (value->stringValue == "vertices")
			{
				stage = Stage::Vertices;
				return true;
			}
		}
		error = "stage must be handle, renderers, or vertices; omitted stage defaults to handle";
		return false;
	}

	inline Json Object()
	{
		Json v;
		v.type = GakuMotion::jsonlite::Type::Object;
		return v;
	}

	inline Json Array()
	{
		Json v;
		v.type = GakuMotion::jsonlite::Type::Array;
		return v;
	}

	inline Json Text(const std::string& s)
	{
		Json v;
		v.type = GakuMotion::jsonlite::Type::String;
		v.stringValue = s;
		return v;
	}

	inline Json Number(double n)
	{
		Json v;
		if (std::isfinite(n))
		{
			v.type = GakuMotion::jsonlite::Type::Number;
			v.numberValue = n;
		}
		return v;
	}

	inline Json Flag(bool b)
	{
		Json v;
		v.type = GakuMotion::jsonlite::Type::Bool;
		v.boolValue = b;
		return v;
	}

	inline void Set(Json& j, const std::string& key, Json value)
	{
		j.objectMembers.emplace_back(key, std::move(value));
	}

	inline void Push(Json& j, Json value) { j.arrayItems.emplace_back(std::move(value)); }

	inline bool IsCurrentActiveFootRenderer(const Json& renderer)
	{
		for (const char* key : {
			     "enabled_known", "enabled", "active_in_hierarchy_known", "active_in_hierarchy",
			     "has_current_foot_or_toe_bone"
		     })
		{
			const Json* value = renderer.Find(key);
			if (!value || !value->IsBool() || !value->boolValue)return false;
		}
		return true;
	}

	inline std::string Quote(const std::string& s)
	{
		std::string out = "\"";
		for (unsigned char c : s)
		{
			if (c == '"' || c == '\\')
			{
				out += '\\';
				out += static_cast<char>(c);
			}
			else if (c < 32)
			{
				char escaped[7];
				std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
				out += escaped;
			}
			else out += static_cast<char>(c);
		}
		return out + '"';
	}

	inline void WriteJson(std::ostream& out, const Json& j)
	{
		switch (j.type)
		{
		case GakuMotion::jsonlite::Type::String: out << Quote(j.stringValue);
			break;
		case GakuMotion::jsonlite::Type::Number:
			{
				char n[40];
				std::snprintf(n, sizeof(n), "%.17g", j.numberValue);
				out << n;
				break;
			}
		case GakuMotion::jsonlite::Type::Bool: out << (j.boolValue ? "true" : "false");
			break;
		case GakuMotion::jsonlite::Type::Array:
			out << '[';
			for (size_t i = 0; i < j.arrayItems.size(); ++i)
			{
				if (i)out << ',';
				WriteJson(out, j.arrayItems[i]);
			}
			out << ']';
			break;
		case GakuMotion::jsonlite::Type::Object:
			out << '{';
			for (size_t i = 0; i < j.objectMembers.size(); ++i)
			{
				if (i)out << ',';
				out << Quote(j.objectMembers[i].first) << ':';
				WriteJson(out, j.objectMembers[i].second);
			}
			out << '}';
			break;
		default: out << "null";
			break;
		}
	}

	// Fixed file for a request, atomically replaced and flushed before native calls.
	// Only first use of an API in a phase and phase boundaries touch disk. In
	// particular Array.GetValue/readField/GCHandle use inside vertex loops do not.
	struct MethodSignature
	{
		Method method;
		std::string signature;
	};

	inline std::vector<MethodSignature> methodSignatures;
	inline std::string tracePath, tracePhase, traceOperation;
	inline auto traceStage = Stage::Handle;
	inline uint32_t traceSeq = 0;
	inline int traceFrame = 0, traceWrites = 0;
	inline float traceClipTime = 0;
	inline bool traceActive = false, traceHealthy = true;
	inline bool probeFaulted = false, traceNativeFault = false;
	inline DWORD traceFaultCode = 0;
	inline Json traceProgress = Array();
	inline std::vector<std::string> traceSeen;

	inline std::string PointerText(const void* pointer)
	{
		char value[2 + sizeof(uintptr_t) * 2 + 1]{};
		std::snprintf(value, sizeof(value), "0x%llx",
		              static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(pointer)));
		return value;
	}

	inline bool Sequence(const Json& request, uint32_t& seq)
	{
		const Json* value = request.Find("seq");
		if (!value || !value->IsNumber())return false;
		const double n = value->numberValue;
		if (!std::isfinite(n) || n < 1 || n > 2147483647.0 || std::floor(n) != n)return false;
		seq = static_cast<uint32_t>(n);
		return true;
	}

	// Pure request checks: no metadata resolution or Unity/managed getter calls.
	// A request for a different callback is ignored, never redirected to its actor.
	inline bool MatchRequest(const Json& request, void* actor, const SnapshotContext* context, std::string& error)
	{
		const Json* source = request.Find("source");
		const Json* profile = request.Find("profile");
		const bool official = context && context->source == Source::OfficialLive;
		if (!profile || !profile->IsString())
		{
			error = "profile missing or invalid";
			return false;
		}
		if (!official)
		{
			const bool photoSource = !source || (source->IsString() && source->stringValue == "photo");
			if (!photoSource || profile->stringValue != "saki-blue-training-boots")
			{
				error = "not a compatible photo request";
				return false;
			}
			return true;
		}
		if (!actor || context->sceneName != "Live" || !context->processId || context->sceneHandle == 0)
		{
			error = "current callback is not a verified Live actor";
			return false;
		}
		if (!source || !source->IsString() || source->stringValue != "official-live" || profile->stringValue !=
			"saki-current-outfit")
		{
			error = "official source/profile mismatch";
			return false;
		}
		const Json* pid = request.Find("process_id");
		const Json* scene = request.Find("scene_handle");
		const Json* pointer = request.Find("actor_pointer");
		if (!pid || !pid->IsNumber() || !std::isfinite(pid->numberValue) || pid->numberValue != static_cast<double>(
			context->processId))
		{
			error = "process_id mismatch";
			return false;
		}
		if (!scene || !scene->IsNumber() || !std::isfinite(scene->numberValue) || scene->numberValue != static_cast<
			double>(context->sceneHandle))
		{
			error = "scene_handle mismatch";
			return false;
		}
		if (!pointer || !pointer->IsString() || pointer->stringValue != PointerText(actor))
		{
			error = "actor_pointer mismatch";
			return false;
		}
		const Json* costume = request.Find("costume_id");
		if (costume && (!costume->IsString() || costume->stringValue.size() > 128))
		{
			error = "costume_id must be an optional requester string of at most 128 bytes";
			return false;
		}
		return true;
	}

	inline void EvidenceContext(Json& report, float clipTime, const SnapshotContext* context)
	{
		const bool official = context && context->source == Source::OfficialLive;
		Set(report, "source", Text(official ? "official-live" : "photo"));
		Set(report, "requested_profile", Text(official ? "saki-current-outfit" : "saki-blue-training-boots"));
		Set(report, "profile_identity",
		    Text("requester assertion; actor/outfit identity is not automatically authenticated"));
		Set(report, "clip_time", official ? Json{} : Number(clipTime));
		Set(report, "clock_source",
		    Text(official ? "official animation time not read; clip_time unavailable" : "gakumotion-player"));
		if (official)
		{
			Set(report, "process_id", Number(context->processId));
			Set(report, "scene_handle", Number(context->sceneHandle));
			Set(report, "scene_name", Text(context->sceneName));
			Set(report, "costume_id", context->costumeId.empty() ? Json{} : Text(context->costumeId));
			Set(report, "costume_identity",
			    Text("requester label only; current costume/ownership is not authenticated by the collector"));
		}
	}

	inline SnapshotContext traceContext;
	inline bool traceHasContext = false;

	inline bool TraceWrite(const std::string& operation, const std::string& state)
	{
		if (!traceActive)return true;
		if (!traceHealthy)return false;
		traceOperation = operation;
		Json trace = Object();
		Set(trace, "schema", Text("gakumotion-shoe-mesh-trace-v1"));
		Set(trace, "seq", Number(traceSeq));
		Set(trace, "stage", Text(StageName(traceStage)));
		Set(trace, "unity_frame", Number(traceFrame));
		EvidenceContext(trace, traceClipTime, traceHasContext ? &traceContext : nullptr);
		Set(trace, "phase", Text(tracePhase));
		Set(trace, "operation", Text(operation));
		Set(trace, "state", Text(state));
		Set(trace, "write_index", Number(++traceWrites));
		if (traceNativeFault)
		{
			Set(trace, "native_fault_code", Number(traceFaultCode));
			Set(trace, "probe_requests_blocked", Flag(true));
		}
		std::ostringstream text;
		WriteJson(text, trace);
		text << '\n';
		const auto bytes = text.str();
		const std::filesystem::path target(tracePath), temporary(tracePath + ".tmp");
		HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
		                          nullptr);
		if (file == INVALID_HANDLE_VALUE)
		{
			traceHealthy = false;
			return false;
		}
		DWORD written = 0;
		const bool good = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written
			== bytes.size() && FlushFileBuffers(file);
		CloseHandle(file);
		traceHealthy = good && MoveFileExW(temporary.c_str(), target.c_str(),
		                                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
		return traceHealthy;
	}

	inline bool TraceFirst(const std::string& signature)
	{
		if (!traceActive)return true;
		if (!traceHealthy)return false;
		if (std::find(traceSeen.begin(), traceSeen.end(), signature) != traceSeen.end())return true;
		traceSeen.push_back(signature);
		return TraceWrite(signature, "before_native_call");
	}

	inline bool Progress(const std::string& phase)
	{
		tracePhase = phase;
		traceSeen.clear();
		Push(traceProgress, Text(phase));
		return TraceWrite("phase_boundary", "entered");
	}

	inline void BeginTrace(const std::string& path, Stage stage, uint32_t seq, int frame, float clipTime,
	                       const SnapshotContext* context = nullptr)
	{
		tracePath = path;
		traceStage = stage;
		traceSeq = seq;
		traceFrame = frame;
		traceClipTime = clipTime;
		traceHasContext = context != nullptr;
		traceContext = context ? *context : SnapshotContext{};
		traceWrites = 0;
		traceHealthy = true;
		traceActive = !path.empty();
		traceProgress = Array();
		traceSeen.clear();
		tracePhase.clear();
		traceOperation.clear();
		traceNativeFault = false;
		traceFaultCode = 0;
	}

	inline std::string Signature(Method method)
	{
		for (const auto& item : methodSignatures)if (item.method == method)return item.signature;
		return "il2cpp_runtime_invoke (unregistered metadata method)";
	}

	struct Api
	{
		void* (*methods)(void*, void**) = nullptr;
		const char* (*methodName)(Method) = nullptr;
		uint32_t (*paramCount)(Method) = nullptr;
		void* (*paramType)(Method, uint32_t) = nullptr;
		void* (*returnType)(Method) = nullptr;
		uint32_t (*flags)(Method, uint32_t*) = nullptr;
		char* (*typeName)(void*) = nullptr;
		void (*freeMemory)(void*) = nullptr;
		void* (*classType)(void*) = nullptr;
		void* (*typeObject)(void*) = nullptr;
		void* (*objectNew)(void*) = nullptr;
		void* (*objectClass)(void*) = nullptr;
		uintptr_t (*arrayLength)(void*) = nullptr;
		size_t (*arrayHeaderSize)() = nullptr;
		uint32_t (*arrayElementSize)(void*) = nullptr;
		Handle (*handleNew)(void*, bool) = nullptr;
		void* (*handleTarget)(Handle) = nullptr;
		void (*handleFree)(Handle) = nullptr;
		int32_t (*stringLength)(void*) = nullptr;
		const wchar_t* (*stringChars)(void*) = nullptr;
		void (*formatException)(void*, char*, int) = nullptr;
		int32_t (*valueSize)(void*, uint32_t*) = nullptr;
	};

	inline Api api;
	inline Json apiEvidence = Object();
	inline std::string configDirectory;
	inline DWORD resolveThread = 0;
	inline bool available = false;
	inline bool rendererAvailable = false;
	inline uint32_t lastSeq = 0;
	inline std::chrono::steady_clock::time_point nextPoll{};
	inline Json officialCachedRequest;
	inline std::string officialCachedPath;
	inline std::chrono::steady_clock::time_point officialNextPoll{};
	// Cache only the small request JSON, not Actor/Transform/Renderer pointers. All
	// actors in a multi-actor frame must still get their own exact identity check.
	inline bool OfficialRequestReady(const std::string& directory, void* actor, SnapshotContext& context)
	{
		if (probeFaulted || directory.empty() || context.source != Source::OfficialLive)return false;
		const std::string path = directory + "/shoe_mesh_probe.request.json";
		const auto now = std::chrono::steady_clock::now();
		if (path != officialCachedPath || now >= officialNextPoll)
		{
			officialCachedPath = path;
			officialNextPoll = now + std::chrono::seconds(1);
			officialCachedRequest = Json{};
			std::error_code ec;
			const auto size = std::filesystem::file_size(path, ec);
			std::string error;
			if (!ec && size > 0 && size <= 16384 && !
				GakuMotion::jsonlite::ParseFile(path, officialCachedRequest, error))officialCachedRequest = Json{};
		}
		std::string error;
		uint32_t seq = 0;
		Stage stage;
		if (!officialCachedRequest.IsObject() || !MatchRequest(officialCachedRequest, actor, &context, error) || !
			Sequence(officialCachedRequest, seq) || seq <= lastSeq || !ParseStage(officialCachedRequest, stage, error))
			return false;
		// A cached positive is only a cheap candidate. Re-read before the caller
		// may Resolve, so a removed/replaced/malformed request cannot enable it.
		Json current;
		std::error_code ec;
		const auto size = std::filesystem::file_size(path, ec);
		if (ec || size == 0 || size > 16384 || !GakuMotion::jsonlite::ParseFile(path, current, error) || !current.
			IsObject() ||
			!MatchRequest(current, actor, &context, error) || !Sequence(current, seq) || seq <= lastSeq || !ParseStage(
				current, stage, error))
			return false;
		const Json* costume = current.Find("costume_id");
		context.costumeId = costume ? costume->stringValue : "";
		if (std::filesystem::exists(directory + "/shoe_mesh_probe_" + std::to_string(seq) + ".json", ec) || ec)return
			false;
		if (std::filesystem::exists(directory + "/shoe_mesh_probe_" + std::to_string(seq) + ".trace.json", ec) || ec)
			return false;
		return true;
	}

	inline void* vectorClass = nullptr;
	inline void* matrixClass = nullptr;
	inline void* meshClass = nullptr;
	inline void* rendererClass = nullptr;
	inline void* vectorFields[3]{};
	inline void* matrixFields[16]{};
	inline Method getComponents = nullptr, getTransform = nullptr, getName = nullptr, getBones = nullptr;
	inline Method getSharedMesh = nullptr, getVertexCount = nullptr, getReadable = nullptr, meshCtor = nullptr;
	inline Method bakeMesh = nullptr, getVertices = nullptr, destroyObject = nullptr, arrayGetValue = nullptr;
	inline Method getLocalToWorld = nullptr, getWorldToLocal = nullptr;
	inline Method getEnabled = nullptr, getGameObject = nullptr, getActive = nullptr, getLossyScale = nullptr,
	              getWorldPosition = nullptr;
	inline Handle rendererTypeHandle = nullptr;

	// Pin all captured managed objects: even a moving collector cannot invalidate
	// a pointer while a subsequent runtime_invoke allocates a boxed value.
	struct Pin
	{
		Handle id = nullptr;

		explicit Pin(void* object = nullptr)
		{
			if (object && api.handleNew && TraceFirst("void* il2cpp_gchandle_new(void*, bool pinned=true)"))id = api.
				handleNew(object, true);
		}

		void Close()
		{
			if (id && api.handleFree)
			{
				TraceFirst("void il2cpp_gchandle_free(void*)");
				api.handleFree(id);
				id = nullptr;
			}
		}

		~Pin() { Close(); }
		Pin(const Pin&) = delete;
		Pin& operator=(const Pin&) = delete;

		void* Get() const
		{
			return id && api.handleTarget && TraceFirst("void* il2cpp_gchandle_get_target(void*)")
				       ? api.handleTarget(id)
				       : nullptr;
		}
	};

	inline std::string TypeName(void* type)
	{
		if (!type || !api.typeName || !api.freeMemory)return {};
		char* s = api.typeName(type);
		std::string name = s ? s : "";
		if (s)api.freeMemory(s);
		return name;
	}

	inline std::string ManagedString(void* object)
	{
		if (!object || !api.stringLength || !api.stringChars)return {};
		Pin pin(object);
		if (!pin.Get())return {};
		if (!TraceFirst("int32_t il2cpp_string_length(void*)"))return {};
		const int n = api.stringLength(pin.Get());
		if (n < 0 || n > 2048)return "<name length outside limit>";
		if (!TraceFirst("const wchar_t* il2cpp_string_chars(void*)"))return {};
		const wchar_t* chars = api.stringChars(pin.Get());
		if (!chars)return {};
		int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, chars, n, nullptr, 0, nullptr, nullptr);
		if (bytes <= 0)return n ? "<invalid UTF-16>" : "";
		std::string s(static_cast<size_t>(bytes), '\0');
		WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, chars, n, &s[0], bytes, nullptr, nullptr);
		return s;
	}

	inline std::string ExceptionText(void* exception)
	{
		if (!exception)return {};
		Pin pin(exception);
		char message[1024]{};
		if (api.formatException && pin.Get() && TraceFirst("void il2cpp_format_exception(void*, char*, int)"))api.
			formatException(pin.Get(), message, sizeof(message));
		return message[0] ? std::string(message) : "managed exception (formatter unavailable)";
	}

	inline void* Invoke(Method method, void* instance, void** args, std::string& error)
	{
		if (!method || !g_safeBones.invoke)
		{
			error = "required method/runtime_invoke unavailable";
			return nullptr;
		}
		if (!TraceFirst(Signature(method)))
		{
			error = "trace persistence failed; managed call not attempted";
			return nullptr;
		}
		void* exception = nullptr;
		void* result = g_safeBones.invoke(method, instance, args, &exception);
		if (exception)
		{
			error = ExceptionText(exception);
			return nullptr;
		}
		return result;
	}

	inline Method Match(void* klass, const char* name, const char* result,
	                    std::initializer_list<const char*> params, bool isStatic)
	{
		Json evidence = Object();
		Set(evidence, "name", Text(name));
		Set(evidence, "expected_return", Text(result));
		Set(evidence, "expected_static", Flag(isStatic));
		Json expected = Array();
		for (auto p : params)Push(expected, Text(p));
		Set(evidence, "expected_parameters", std::move(expected));
		Json observed = Array();
		Method selected = nullptr;
		int matches = 0;
		if (klass && api.methods && api.methodName && api.paramCount && api.paramType && api.returnType && api.flags)
		{
			void* iter = nullptr;
			while (Method method = api.methods(klass, &iter))
			{
				const char* methodName = api.methodName(method);
				if (!methodName || std::strcmp(methodName, name) != 0)continue;
				uint32_t impl = 0;
				bool actualStatic = (api.flags(method, &impl) & 0x10) != 0;
				const std::string ret = TypeName(api.returnType(method));
				const uint32_t count = api.paramCount(method);
				Json signature = Object();
				Set(signature, "return", Text(ret));
				Set(signature, "static", Flag(actualStatic));
				Json actual = Array();
				bool same = count == params.size() && ret == result && actualStatic == isStatic;
				auto wanted = params.begin();
				for (uint32_t i = 0; i < count; ++i)
				{
					const auto p = TypeName(api.paramType(method, i));
					Push(actual, Text(p));
					if (i < params.size())
					{
						same = same && p == *wanted;
						++wanted;
					}
				}
				Set(signature, "parameters", std::move(actual));
				Set(signature, "matches", Flag(same));
				Push(observed, std::move(signature));
				if (same)
				{
					selected = method;
					++matches;
				}
			}
		}
		Set(evidence, "overloads", std::move(observed));
		Set(evidence, "exact_matches", Number(matches));
		// Duplicate metadata entries are ambiguous; do not select the first pointer.
		if (matches != 1)selected = nullptr;
		Set(evidence, "resolved", Flag(selected != nullptr));
		if (selected)
		{
			std::string signature = isStatic ? "static " : "";
			signature += result;
			signature += ' ';
			signature += name;
			signature += '(';
			bool first = true;
			for (auto p : params)
			{
				if (!first)signature += ", ";
				signature += p;
				first = false;
			}
			signature += ')';
			methodSignatures.push_back({selected, std::move(signature)});
		}
		Push(apiEvidence.objectMembers.back().second, std::move(evidence));
		return selected;
	}

	inline bool Fields(void* klass, const std::vector<std::string>& names, void** fields, Json& evidence)
	{
		if (!klass || !g_faceRuntime.findField || !g_faceRuntime.fieldType || !g_faceRuntime.fieldFlags ||
			!g_faceRuntime.typeCode || !g_faceRuntime.readField || !il2cpp_field_get_offset || !api.valueSize)
			return false;
		uint32_t alignment = 0;
		const int size = api.valueSize(klass, &alignment);
		Set(evidence, "value_size", Number(size));
		Set(evidence, "alignment", Number(alignment));
		Json metadata = Array();
		bool good = size == static_cast<int>(4 * names.size());
		std::vector<int> offsets;
		for (size_t i = 0; i < names.size(); ++i)
		{
			fields[i] = g_faceRuntime.findField(klass, names[i].c_str());
			void* type = fields[i] ? g_faceRuntime.fieldType(fields[i]) : nullptr;
			const int offset = fields[i] ? il2cpp_field_get_offset(fields[i]) : -1;
			const bool valid = type && g_faceRuntime.typeCode(type) == 0x0c && !(g_faceRuntime.fieldFlags(fields[i]) &
				0x10) && offset >= 0;
			Json f = Object();
			Set(f, "name", Text(names[i]));
			Set(f, "offset", Number(offset));
			Set(f, "float_instance_field", Flag(valid));
			Push(metadata, std::move(f));
			good = good && valid;
			offsets.push_back(offset);
		}
		std::sort(offsets.begin(), offsets.end());
		for (size_t i = 1; i < offsets.size(); ++i)good = good && offsets[i] == offsets[0] + static_cast<int>(4 * i);
		Set(evidence, "fields", std::move(metadata));
		Set(evidence, "validated", Flag(good));
		return good;
	}

	inline void Resolve(HMODULE assembly, const std::string& root)
	{
		if (probeFaulted)return; // Sticky for the lifetime of this loaded probe.
		configDirectory = root;
		resolveThread = GetCurrentThreadId();
		available = false;
		rendererAvailable = false;
		nextPoll = {};
		lastSeq = 0;
		traceActive = false;
		methodSignatures.clear();
		if (rendererTypeHandle && api.handleFree)api.handleFree(rendererTypeHandle);
		rendererTypeHandle = 0;
		api = {};
		apiEvidence = Object();
		Json exports = Object();
#define SHOE_EXPORT(member,symbol) api.member=reinterpret_cast<decltype(api.member)>(GetProcAddress(assembly,symbol)); Set(exports,symbol,Flag(api.member!=nullptr))
		SHOE_EXPORT(methods, "il2cpp_class_get_methods");
		SHOE_EXPORT(methodName, "il2cpp_method_get_name");
		SHOE_EXPORT(paramCount, "il2cpp_method_get_param_count");
		SHOE_EXPORT(paramType, "il2cpp_method_get_param");
		SHOE_EXPORT(returnType, "il2cpp_method_get_return_type");
		SHOE_EXPORT(flags, "il2cpp_method_get_flags");
		SHOE_EXPORT(typeName, "il2cpp_type_get_name");
		SHOE_EXPORT(freeMemory, "il2cpp_free");
		SHOE_EXPORT(classType, "il2cpp_class_get_type");
		SHOE_EXPORT(typeObject, "il2cpp_type_get_object");
		SHOE_EXPORT(objectNew, "il2cpp_object_new");
		SHOE_EXPORT(objectClass, "il2cpp_object_get_class");
		SHOE_EXPORT(arrayLength, "il2cpp_array_length");
		SHOE_EXPORT(arrayHeaderSize, "il2cpp_array_object_header_size");
		SHOE_EXPORT(arrayElementSize, "il2cpp_array_element_size");
		SHOE_EXPORT(handleNew, "il2cpp_gchandle_new");
		SHOE_EXPORT(handleTarget, "il2cpp_gchandle_get_target");
		SHOE_EXPORT(handleFree, "il2cpp_gchandle_free");
		SHOE_EXPORT(stringLength, "il2cpp_string_length");
		SHOE_EXPORT(stringChars, "il2cpp_string_chars");
		SHOE_EXPORT(formatException, "il2cpp_format_exception");
		SHOE_EXPORT(valueSize, "il2cpp_class_value_size");
#undef SHOE_EXPORT
		Set(apiEvidence, "exports", std::move(exports));
		Set(apiEvidence, "gchandle_storage_bytes", Number(sizeof(Handle)));
#if defined(_MSC_VER)
		Set(apiEvidence, "native_seh_guard", Flag(true));
#else
		Set(apiEvidence, "native_seh_guard", Flag(false));
#endif
		// Store method evidence as the final member while Match appends to it.
		Set(apiEvidence, "methods", Array());
		void* component = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Component");
		void* object = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Object");
		void* transform = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Transform");
		void* rendererBase = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Renderer");
		void* gameObject = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject");
		rendererClass = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "SkinnedMeshRenderer");
		meshClass = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Mesh");
		matrixClass = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Matrix4x4");
		vectorClass = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Vector3");
		void* array = FindClass("mscorlib.dll", "System", "Array");
		if (!array)array = FindClass("System.Private.CoreLib.dll", "System", "Array");
		getComponents = Match(component, "GetComponentsInChildren", "UnityEngine.Component[]",
		                      {"System.Type", "System.Boolean"}, false);
		getTransform = Match(component, "get_transform", "UnityEngine.Transform", {}, false);
		getGameObject = Match(component, "get_gameObject", "UnityEngine.GameObject", {}, false);
		getEnabled = Match(rendererBase, "get_enabled", "System.Boolean", {}, false);
		getActive = Match(gameObject, "get_activeInHierarchy", "System.Boolean", {}, false);
		getName = Match(object, "get_name", "System.String", {}, false);
		getBones = Match(rendererClass, "get_bones", "UnityEngine.Transform[]", {}, false);
		getSharedMesh = Match(rendererClass, "get_sharedMesh", "UnityEngine.Mesh", {}, false);
		bakeMesh = Match(rendererClass, "BakeMesh", "System.Void", {"UnityEngine.Mesh", "System.Boolean"}, false);
		getVertexCount = Match(meshClass, "get_vertexCount", "System.Int32", {}, false);
		getReadable = Match(meshClass, "get_isReadable", "System.Boolean", {}, false);
		getVertices = Match(meshClass, "get_vertices", "UnityEngine.Vector3[]", {}, false);
		meshCtor = Match(meshClass, ".ctor", "System.Void", {}, false);
		destroyObject = Match(object, "Destroy", "System.Void", {"UnityEngine.Object"}, true);
		getLocalToWorld = Match(transform, "get_localToWorldMatrix", "UnityEngine.Matrix4x4", {}, false);
		getWorldToLocal = Match(transform, "get_worldToLocalMatrix", "UnityEngine.Matrix4x4", {}, false);
		getLossyScale = Match(transform, "get_lossyScale", "UnityEngine.Vector3", {}, false);
		getWorldPosition = Match(transform, "get_position", "UnityEngine.Vector3", {}, false);
		arrayGetValue = Match(array, "GetValue", "System.Object", {"System.Int32"}, false);
		Json vectorMetadata = Object(), matrixMetadata = Object();
		const bool vectorFieldsOk = Fields(vectorClass, {"x", "y", "z"}, vectorFields, vectorMetadata);
		std::vector<std::string> matrixNames;
		for (int r = 0; r < 4; ++r)for (int c = 0; c < 4; ++c)matrixNames.push_back(
			"m" + std::to_string(r) + std::to_string(c));
		const bool matrixFieldsOk = Fields(matrixClass, matrixNames, matrixFields, matrixMetadata);
		Set(apiEvidence, "vector3", std::move(vectorMetadata));
		Set(apiEvidence, "matrix4x4", std::move(matrixMetadata));
		available = assembly && api.handleNew && api.handleTarget && api.handleFree && api.objectNew && api.objectClass
			&& api.classType && api.typeObject && api.stringLength && api.stringChars &&
			api.arrayLength && g_safeBones.invoke && g_safeBones.unbox && getComponents && getTransform && getName &&
			getBones &&
			getSharedMesh && getVertexCount && getReadable && meshCtor && bakeMesh && getVertices && destroyObject &&
			arrayGetValue &&
			getLocalToWorld && getWorldToLocal && getEnabled && getGameObject && getActive && getLossyScale &&
			getWorldPosition && vectorFieldsOk && matrixFieldsOk;
		rendererAvailable = assembly && api.handleNew && api.handleTarget && api.handleFree && api.objectClass && api.
			classType && api.typeObject && api.stringLength && api.stringChars &&
			api.arrayLength && g_safeBones.invoke && g_safeBones.unbox && getComponents && getName && getBones &&
			getSharedMesh && getVertexCount && getReadable && arrayGetValue &&
			getLocalToWorld && getWorldToLocal && getEnabled && getGameObject && getActive && getWorldPosition &&
			vectorFieldsOk && matrixFieldsOk && rendererClass;
		// Creating a managed System.Type and its handle belongs to renderers, never
		// Resolve or the isolated actor-handle stage.
		Set(apiEvidence, "handle_ready", Flag(api.handleNew && api.handleTarget && api.handleFree));
		Set(apiEvidence, "renderers_ready", Flag(rendererAvailable));
		Set(apiEvidence, "ready", Flag(available));
		Set(apiEvidence, "array_access", Text("System.Array.GetValue(Int32), no array memory offsets used"));
		if (api.arrayHeaderSize)Set(apiEvidence, "runtime_array_header_bytes",
		                            Number(static_cast<double>(api.arrayHeaderSize())));
	}

	inline std::string Name(void* object)
	{
		if (!object)return {};
		std::string error;
		return ManagedString(Invoke(getName, object, nullptr, error));
	}

	inline void* ObjectClass(void* object)
	{
		return object && api.objectClass && TraceFirst("Il2CppClass* il2cpp_object_get_class(void*)")
			       ? api.objectClass(object)
			       : nullptr;
	}

	inline uintptr_t ArrayLength(void* object)
	{
		return object && api.arrayLength && TraceFirst("uintptr_t il2cpp_array_length(void*)")
			       ? api.arrayLength(object)
			       : 0;
	}

	inline bool Primitive(Method method, void* object, int code, void* destination, std::string& error)
	{
		Pin boxed(Invoke(method, object, nullptr, error));
		if (!boxed.Get())return false;
		void* klass = ObjectClass(boxed.Get());
		void* type = klass && api.classType && TraceFirst("Il2CppType* il2cpp_class_get_type(Il2CppClass*)")
			             ? api.classType(klass)
			             : nullptr;
		if (!type || !g_faceRuntime.typeCode || g_faceRuntime.typeCode(type) != code)
		{
			error = "boxed primitive return type mismatch";
			return false;
		}
		const void* payload = TraceFirst("void* il2cpp_object_unbox(void*)") ? g_safeBones.unbox(boxed.Get()) : nullptr;
		if (!payload)
		{
			error = "boxed primitive unbox failed";
			return false;
		}
		std::memcpy(destination, payload, code == 0x02 ? sizeof(bool) : sizeof(int32_t));
		return true;
	}

	inline void* Element(void* array, int index, std::string& error)
	{
		void* args[] = {&index};
		return Invoke(arrayGetValue, array, args, error);
	}

	inline Json ArrayInfo(void* array)
	{
		Json j = Object();
		Set(j, "length", Number(static_cast<double>(ArrayLength(array))));
		if (api.arrayElementSize && TraceFirst("uint32_t il2cpp_array_element_size(Il2CppClass*)"))Set(
			j, "runtime_element_bytes", Number(api.arrayElementSize(ObjectClass(array))));
		return j;
	}

	struct Matrix
	{
		float m[16]{};
	};

	inline Json Vector(const UnityVector3& v)
	{
		Json j = Array();
		Push(j, Number(v.x));
		Push(j, Number(v.y));
		Push(j, Number(v.z));
		return j;
	}

	inline Json MatrixJson(const Matrix& m)
	{
		Json j = Array();
		for (float v : m.m)Push(j, Number(v));
		return j;
	}

	inline bool ReadMatrix(Method method, void* transform, Matrix& out, std::string& error)
	{
		Pin boxed(Invoke(method, transform, nullptr, error));
		if (!boxed.Get())return false;
		if (ObjectClass(boxed.Get()) != matrixClass)
		{
			error = "Matrix4x4 boxed class mismatch";
			return false;
		}
		if (!TraceFirst("void il2cpp_field_get_value(boxed Matrix4x4, field, float*)"))
		{
			error = "trace persistence failed";
			return false;
		}
		for (int i = 0; i < 16; ++i)
		{
			g_faceRuntime.readField(boxed.Get(), matrixFields[i], &out.m[i]);
			if (!std::isfinite(out.m[i]))
			{
				error = "nonfinite transform matrix";
				return false;
			}
		}
		if (std::fabs(out.m[12]) > 1e-4f || std::fabs(out.m[13]) > 1e-4f || std::fabs(out.m[14]) > 1e-4f ||
			std::fabs(out.m[15] - 1) > 1e-4f)
		{
			error = "nonaffine transform matrix";
			return false;
		}
		return true;
	}

	inline bool InversePair(const Matrix& a, const Matrix& b)
	{
		for (int r = 0; r < 4; ++r)
			for (int c = 0; c < 4; ++c)
			{
				double v = 0;
				for (int k = 0; k < 4; ++k)v += a.m[r * 4 + k] * b.m[k * 4 + c];
				if (std::fabs(v - (r == c ? 1 : 0)) > 3e-3)return false;
			}
		return true;
	}

	inline UnityVector3 Point(const Matrix& m, const UnityVector3& p)
	{
		return {
			m.m[0] * p.x + m.m[1] * p.y + m.m[2] * p.z + m.m[3], m.m[4] * p.x + m.m[5] * p.y + m.m[6] * p.z + m.m[7],
			m.m[8] * p.x + m.m[9] * p.y + m.m[10] * p.z + m.m[11]
		};
	}

	inline float Distance(const UnityVector3& a, const UnityVector3& b)
	{
		const float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
		return std::sqrt(x * x + y * y + z * z);
	}

	inline bool ReadVector(void* boxed, UnityVector3& value, std::string& error)
	{
		if (!boxed || ObjectClass(boxed) != vectorClass)
		{
			error = "Vector3 boxed class mismatch";
			return false;
		}
		if (!TraceFirst("void il2cpp_field_get_value(boxed Vector3, field, float*)"))
		{
			error = "trace persistence failed";
			return false;
		}
		g_faceRuntime.readField(boxed, vectorFields[0], &value.x);
		g_faceRuntime.readField(boxed, vectorFields[1], &value.y);
		g_faceRuntime.readField(boxed, vectorFields[2], &value.z);
		if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
		{
			error = "nonfinite baked vertex";
			return false;
		}
		return true;
	}

	struct Foot
	{
		Pin bone;
		Matrix toWorld, toLocal;
		UnityVector3 origin{}, worldPosition{};
		bool valid = false;
		std::string error;

		static void* CurrentBone(void* actor, int id)
		{
			if (!Progress("foot_toe_matrices.humanoid_" + std::to_string(id)))return nullptr;
			return TraceFirst("HelperGetBoneTransform(actor, humanoid_id) [existing validated helper]")
				       ? HelperGetBoneTransform(actor, id)
				       : nullptr;
		}

		Foot(void* actor, int id) : bone(CurrentBone(actor, id))
		{
			valid = bone.Get() && ReadMatrix(getLocalToWorld, bone.Get(), toWorld, error) && ReadMatrix(
				getWorldToLocal, bone.Get(), toLocal, error);
			if (valid && !InversePair(toWorld, toLocal))
			{
				valid = false;
				error = "bone matrix inverse check failed";
			}
			if (valid)
			{
				origin = Point(toWorld, {0, 0, 0});
				Pin position(Invoke(getWorldPosition, bone.Get(), nullptr, error));
				valid = error.empty() && ReadVector(position.Get(), worldPosition, error) && Distance(
					origin, worldPosition) < 0.001f;
				if (!valid && error.empty())error = "world position disagrees with matrix origin";
			}
			else if (error.empty())error = "current humanoid bone unavailable";
		}

		Json Evidence() const
		{
			Json j = Object();
			Set(j, "valid", Flag(valid));
			Set(j, "name", Text(Name(bone.Get())));
			Set(j, "bone_pointer", Text(PointerText(bone.Get())));
			Set(j, "error", Text(error));
			if (valid)
			{
				Set(j, "local_to_world", MatrixJson(toWorld));
				Set(j, "world_to_local", MatrixJson(toLocal));
				Set(j, "world_origin", Vector(origin));
				Set(j, "world_position_getter", Vector(worldPosition));
			}
			return j;
		}
	};

	inline void PoseReadback(Json& report, const Foot* const* feet)
	{
		Json frames = Array();
		bool allUnchanged = true;
		double maximum = 0;
		constexpr double tolerance = 1e-5;
		for (int i = 0; i < 4; ++i)
		{
			const Foot& before = *feet[i];
			Matrix afterWorld, afterLocal;
			std::string error;
			const bool read = before.valid && ReadMatrix(getLocalToWorld, before.bone.Get(), afterWorld, error) &&
				ReadMatrix(getWorldToLocal, before.bone.Get(), afterLocal, error) &&
				InversePair(afterWorld, afterLocal);
			Json frame = Object();
			Set(frame, "humanoid_frame_index", Number(i));
			Set(frame, "readback_valid", Flag(read));
			bool same = false;
			double delta = 0;
			if (read)
			{
				for (int k = 0; k < 16; ++k)
				{
					delta = (std::max)(delta, std::fabs(static_cast<double>(before.toWorld.m[k]) - afterWorld.m[k]));
					delta = (std::max)(delta, std::fabs(static_cast<double>(before.toLocal.m[k]) - afterLocal.m[k]));
				}
				same = delta <= tolerance;
				maximum = (std::max)(maximum, delta);
				Set(frame, "local_to_world_after", MatrixJson(afterWorld));
				Set(frame, "world_to_local_after", MatrixJson(afterLocal));
				Set(frame, "max_abs_matrix_delta", Number(delta));
			}
			if (!read && error.empty())error = before.valid
				                                   ? "matrix inverse readback failed"
				                                   : "before frame unavailable";
			Set(frame, "unchanged_within_tolerance", Flag(same));
			Set(frame, "error", Text(error));
			Push(frames, std::move(frame));
			allUnchanged = allUnchanged && same;
		}
		Set(report, "pose_readback", std::move(frames));
		Set(report, "pose_matrix_tolerance", Number(tolerance));
		Set(report, "max_abs_matrix_delta", Number(maximum));
		Set(report, "pose_frames_unchanged", Flag(allUnchanged));
		Set(report, "data_consistency", Text(allUnchanged
			                                     ? "four Foot/Toe matrices unchanged within tolerance across collection"
			                                     : "unverified: a Foot/Toe frame was unavailable, failed readback, or changed during collection"));
	}

	struct Scratch
	{
		Pin mesh;
		Json& evidence;
		bool closed = false;

		Scratch(void* object, Json& j) : mesh(object), evidence(j)
		{
		}

		void Close()
		{
			if (!closed && mesh.Get())
			{
				closed = true;
				std::string error;
				void* args[] = {mesh.Get()};
				Invoke(destroyObject, nullptr, args, error);
				Set(evidence, "scratch_destroy_error", Text(error));
				Set(evidence, "scratch_destroy_requested", Flag(error.empty()));
			}
		}

		~Scratch() { Close(); }
	};

	inline Json Renderer(void* renderer, const Foot* const* feet, int& processed, int& candidates, Stage stage)
	{
		Json j = Object();
		Set(j, "name", Text(Name(renderer)));
		Set(j, "renderer_pointer", Text(PointerText(renderer)));
		std::string error;
		bool enabled = false, active = false;
		const bool enabledKnown = Primitive(getEnabled, renderer, 0x02, &enabled, error);
		Set(j, "enabled_known", Flag(enabledKnown));
		Set(j, "enabled", enabledKnown ? Flag(enabled) : Json{});
		Set(j, "enabled_error", Text(error));
		error.clear();
		Pin gameObject(Invoke(getGameObject, renderer, nullptr, error));
		const bool activeKnown = gameObject.Get() && Primitive(getActive, gameObject.Get(), 0x02, &active, error);
		Set(j, "active_in_hierarchy_known", Flag(activeKnown));
		Set(j, "active_in_hierarchy", activeKnown ? Flag(active) : Json{});
		Set(j, "active_error", Text(error));
		error.clear();
		Pin shared(Invoke(getSharedMesh, renderer, nullptr, error));
		Set(j, "shared_mesh_error", Text(error));
		if (!shared.Get())
		{
			Set(j, "status", Text("shared_mesh_unavailable"));
			return j;
		}
		Set(j, "shared_mesh_name", Text(Name(shared.Get())));
		Set(j, "shared_mesh_pointer", Text(PointerText(shared.Get())));
		int32_t count = 0;
		bool readable = false;
		if (!Primitive(getVertexCount, shared.Get(), 0x08, &count, error))
		{
			Set(j, "status", Text("vertex_count_failed"));
			Set(j, "error", Text(error));
			return j;
		}
		Set(j, "vertex_count", Number(count));
		error.clear();
		const bool readableKnown = Primitive(getReadable, shared.Get(), 0x02, &readable, error);
		Set(j, "shared_mesh_readable_known", Flag(readableKnown));
		Set(j, "shared_mesh_is_readable", readableKnown ? Flag(readable) : Json{});
		Set(j, "readable_error", Text(error));
		error.clear();
		Pin bones(Invoke(getBones, renderer, nullptr, error));
		Json names = Array();
		bool hasCurrentFoot = false;
		if (bones.Get())
		{
			Set(j, "bones_array", ArrayInfo(bones.Get()));
			const uintptr_t boneCount = ArrayLength(bones.Get());
			if (boneCount > 512)
			{
				Set(j, "status", Text("bone_count_limit"));
				return j;
			}
			for (int b = 0; b < static_cast<int>(boneCount); ++b)
			{
				Pin bone(Element(bones.Get(), b, error));
				if (!error.empty())break;
				Json item = Object();
				Set(item, "index", Number(b));
				Set(item, "name", Text(Name(bone.Get())));
				Json matches = Array();
				for (int f = 0; f < 4; ++f)if (bone.Get() && bone.Get() == feet[f]->bone.Get())
				{
					Push(matches, Number(f));
					hasCurrentFoot = true;
				}
				Set(item, "current_humanoid_matches", std::move(matches));
				Push(names, std::move(item));
			}
		}
		Set(j, "bones", std::move(names));
		Set(j, "bones_error", Text(error));
		Set(j, "has_current_foot_or_toe_bone", Flag(hasCurrentFoot));
		Set(j, "bone_weights",
		    Text(
			    "not attempted in this minimal probe; no inferred weights, bind-pose vertices, or semantic shoe labels"));
		if (!enabledKnown || !activeKnown || !enabled || !active)
		{
			Set(j, "status", Text("renderer_not_confirmed_current_active"));
			return j;
		}
		if (!hasCurrentFoot)
		{
			Set(j, "status", Text("no_current_foot_toe_bone_match"));
			return j;
		}
		if (stage != Stage::Vertices)
		{
			Set(j, "status", Text("active_renderer_current_foot_toe_observed"));
			Set(j, "vertex_collection_attempted", Flag(false));
			return j;
		}
		if (count <= 0 || count > kMeshVertexLimit || processed + count > kTotalVertexLimit)
		{
			Set(j, "status", Text("vertex_budget_limit"));
			return j;
		}
		error.clear();
		Pin transform(Invoke(getTransform, renderer, nullptr, error));
		Matrix toWorld, toLocal;
		if (!transform.Get() || !ReadMatrix(getLocalToWorld, transform.Get(), toWorld, error) || !ReadMatrix(
			getWorldToLocal, transform.Get(), toLocal, error) || !InversePair(toWorld, toLocal))
		{
			Set(j, "status", Text("renderer_matrix_failed"));
			Set(j, "error", Text(error.empty() ? "renderer matrix inverse check failed" : error));
			return j;
		}
		Set(j, "renderer_local_to_world", MatrixJson(toWorld));
		Set(j, "renderer_world_to_local", MatrixJson(toLocal));
		Pin scaleBox(Invoke(getLossyScale, transform.Get(), nullptr, error));
		UnityVector3 scale{};
		const bool scaleKnown = error.empty() && ReadVector(scaleBox.Get(), scale, error);
		Set(j, "renderer_lossy_scale", scaleKnown ? Vector(scale) : Json{});
		Set(j, "renderer_scale_error", Text(error));
		const bool unitScale = scaleKnown && std::fabs(scale.x - 1) < 0.001f && std::fabs(scale.y - 1) < 0.001f &&
			std::fabs(scale.z - 1) < 0.001f;
		Set(j, "bake_scale_conversion_verified", Flag(unitScale));
		Set(j, "scale_interpretation", Text(unitScale
			                                    ? "unit renderer scale; complete renderer matrix used"
			                                    : "non-unit/unknown renderer scale: useScale=false compensation not established; preserve raw baked vertices and matrices as evidence, world/Foot-local candidates are provisional"));
		Json points = Array();
		bool bakeSucceeded = false;
		int32_t bakedCount = 0;
		{
			const std::string rendererPhase = tracePhase;
			if (!Progress(rendererPhase + ".scratch_mesh_allocate"))
			{
				Set(j, "status", Text("trace_write_failed"));
				return j;
			}
			Scratch scratch(TraceFirst("void* il2cpp_object_new(UnityEngine.Mesh class)")
				                ? api.objectNew(meshClass)
				                : nullptr, j);
			if (!scratch.mesh.Get())
			{
				Set(j, "status", Text("scratch_allocation_failed"));
				return j;
			}
			error.clear();
			Invoke(meshCtor, scratch.mesh.Get(), nullptr, error);
			if (!error.empty())
			{
				Set(j, "status", Text("scratch_ctor_failed"));
				Set(j, "error", Text(error));
				scratch.Close();
				return j;
			}
			bool useScale = false;
			void* args[] = {scratch.mesh.Get(), &useScale};
			if (!Progress(rendererPhase + ".bake_mesh"))
			{
				Set(j, "status", Text("trace_write_failed"));
				scratch.Close();
				return j;
			}
			Invoke(bakeMesh, renderer, args, error);
			bakeSucceeded = error.empty();
			Set(j, "bake_use_scale", Flag(useScale));
			Set(j, "bake_error", Text(error));
			Set(j, "bake_succeeded", Flag(bakeSucceeded));
			if (!bakeSucceeded)
			{
				Set(j, "status", Text("bake_failed"));
				scratch.Close();
				return j;
			}
			if (!Primitive(getVertexCount, scratch.mesh.Get(), 0x08, &bakedCount, error) || bakedCount != count)
			{
				Set(j, "status", Text("baked_vertex_count_mismatch"));
				Set(j, "baked_vertex_count", Number(bakedCount));
				Set(j, "error", Text(error));
				scratch.Close();
				return j;
			}
			if (!Progress(rendererPhase + ".vertices_read"))
			{
				Set(j, "status", Text("trace_write_failed"));
				scratch.Close();
				return j;
			}
			Pin vertices(Invoke(getVertices, scratch.mesh.Get(), nullptr, error));
			if (!vertices.Get())
			{
				Set(j, "status", Text("baked_vertices_unavailable"));
				Set(j, "error", Text(error));
				scratch.Close();
				return j;
			}
			Set(j, "vertices_array", ArrayInfo(vertices.Get()));
			const uintptr_t length = ArrayLength(vertices.Get());
			if (length != static_cast<uintptr_t>(bakedCount))
			{
				Set(j, "status", Text("baked_array_count_mismatch"));
				scratch.Close();
				return j;
			}
			if (api.arrayElementSize && TraceFirst("uint32_t il2cpp_array_element_size(Il2CppClass*)") && api.
				arrayElementSize(ObjectClass(vertices.Get())) != sizeof(UnityVector3))
			{
				Set(j, "status", Text("vector_array_element_size_mismatch"));
				scratch.Close();
				return j;
			}
			processed += bakedCount;
			int inspected = 0;
			bool conversionFinite = true;
			UnityVector3 lower{}, upper{};
			Json rawSamples = Array(), allVertices = Array();
			std::vector<int> sampleIndices;
			for (int i = 0; i < (std::min)(16, bakedCount); ++i)sampleIndices.push_back(i);
			for (int k = 0; k < 16; ++k)
			{
				const int index = (bakedCount - 1) * k / 15;
				if (std::find(sampleIndices.begin(), sampleIndices.end(), index) == sampleIndices.end())sampleIndices.
					push_back(index);
			}
			for (int i = 0; i < bakedCount; ++i)
			{
				Pin boxed(Element(vertices.Get(), i, error));
				UnityVector3 local{};
				if (!error.empty() || !ReadVector(boxed.Get(), local, error))break;
				++inspected;
				// Complete index-aligned evidence, independent of changing radius
				// membership. Foot/Toe coordinates are reconstructed offline.
				Json vertex = Object();
				Set(vertex, "vertex_index", Number(i));
				Set(vertex, "renderer_local", Vector(local));
				Push(allVertices, std::move(vertex));
				if (inspected == 1)lower = upper = local;
				else
				{
					lower.x = (std::min)(lower.x, local.x);
					lower.y = (std::min)(lower.y, local.y);
					lower.z = (std::min)(lower.z, local.z);
					upper.x = (std::max)(upper.x, local.x);
					upper.y = (std::max)(upper.y, local.y);
					upper.z = (std::max)(upper.z, local.z);
				}
				if (std::find(sampleIndices.begin(), sampleIndices.end(), i) != sampleIndices.end())
				{
					Json raw = Object();
					Set(raw, "vertex_index", Number(i));
					Set(raw, "renderer_local", Vector(local));
					Push(rawSamples, std::move(raw));
				}
				const UnityVector3 world = Point(toWorld, local);
				if (!std::isfinite(world.x) || !std::isfinite(world.y) || !std::isfinite(world.z))
				{
					conversionFinite = false;
					continue;
				}
				Json neighborhoods = Array();
				for (int side = 0; side < 2; ++side)
				{
					const Foot& foot = *feet[side];
					const Foot& toe = *feet[side + 2];
					const float footDistance = foot.valid ? Distance(world, foot.origin) : 1e9f;
					const float toeDistance = toe.valid ? Distance(world, toe.origin) : 1e9f;
					if ((std::min)(footDistance, toeDistance) > kNeighborhoodRadius)continue;
					Json n = Object();
					Set(n, "side", Text(side == 0 ? "left" : "right"));
					if (foot.valid)
					{
						Set(n, "foot_local", Vector(Point(foot.toLocal, world)));
						Set(n, "foot_distance_world_m", Number(footDistance));
					}
					if (toe.valid)
					{
						Set(n, "toe_local", Vector(Point(toe.toLocal, world)));
						Set(n, "toe_distance_world_m", Number(toeDistance));
					}
					Push(neighborhoods, std::move(n));
				}
				if (neighborhoods.arrayItems.empty())continue;
				if (candidates >= kCandidateLimit)
				{
					Set(j, "candidate_limit_reached", Flag(true));
					continue;
				}
				Json p = Object();
				Set(p, "vertex_index", Number(i));
				Set(p, "renderer_local", Vector(local));
				Set(p, "world", Vector(world));
				Set(p, "neighborhoods", std::move(neighborhoods));
				Push(points, std::move(p));
				++candidates;
			}
			Set(j, "inspected_vertices", Number(inspected));
			Set(j, "vertex_read_error", Text(error));
			Set(j, "finite_renderer_local_vertices", Number(inspected));
			Set(j, "baked_vertex_read_complete", Flag(inspected == bakedCount));
			Set(j, "raw_baked_samples", std::move(rawSamples));
			Set(j, "baked_vertices", std::move(allVertices));
			Set(j, "vertex_index_basis",
			    Text(
				    "unchanged sharedMesh object and vertex count; index in each complete BakeMesh vertices array; no topology writes by this collector"));
			Json envelope = Object();
			if (inspected)
			{
				Set(envelope, "min", Vector(lower));
				Set(envelope, "max", Vector(upper));
			}
			Set(j, "renderer_local_aabb", std::move(envelope));
			Set(j, "world_conversion_finite", Flag(conversionFinite));
			Set(j, "foot_world_conversion_reliable",
			    Flag(unitScale && conversionFinite && feet[0]->valid && feet[1]->valid && feet[2]->valid && feet[3]->
				    valid));
			Set(j, "status", Text(inspected == bakedCount ? "baked_vertices_read" : "partial_vertex_read_failure"));
			Progress(rendererPhase + ".scratch_mesh_destroy");
			scratch.Close();
		}
		if (!Progress(tracePhase + ".renderer_readback"))
		{
			Set(j, "renderer_frames_unchanged", Flag(false));
			Set(j, "renderer_readback_error", Text("trace_write_failed"));
		}
		else
		{
			Matrix afterWorld, afterLocal;
			error.clear();
			const bool read = ReadMatrix(getLocalToWorld, transform.Get(), afterWorld, error) && ReadMatrix(
				getWorldToLocal, transform.Get(), afterLocal, error) && InversePair(afterWorld, afterLocal);
			double delta = 0;
			if (read)for (int k = 0; k < 16; ++k)
			{
				delta = (std::max)(delta, std::fabs(static_cast<double>(toWorld.m[k]) - afterWorld.m[k]));
				delta = (std::max)(delta, std::fabs(static_cast<double>(toLocal.m[k]) - afterLocal.m[k]));
			}
			Set(j, "renderer_frames_unchanged", Flag(read && delta <= 1e-5));
			Set(j, "renderer_max_abs_matrix_delta", read ? Number(delta) : Json{});
			Set(j, "renderer_readback_error", Text(error));
			if (read)
			{
				Set(j, "renderer_local_to_world_after", MatrixJson(afterWorld));
				Set(j, "renderer_world_to_local_after", MatrixJson(afterLocal));
			}
			error.clear();
			Pin afterMesh(Invoke(getSharedMesh, renderer, nullptr, error));
			Set(j, "shared_mesh_identity_unchanged", Flag(error.empty() && afterMesh.Get() == shared.Get()));
			Set(j, "shared_mesh_readback_error", Text(error));
		}
		Set(j, "candidates", std::move(points));
		return j;
	}

	inline Json Collect(void* actor, int frame, float clipTime, uint32_t seq, Stage stage = Stage::Handle,
	                    const SnapshotContext* context = nullptr)
	{
		Json report = Object();
		Set(report, "schema", Text("gakumotion-shoe-mesh-evidence-v2"));
		Set(report, "seq", Number(seq));
		Set(report, "stage", Text(StageName(stage)));
		EvidenceContext(report, clipTime, context);
		Set(report, "actor_pointer", Text(PointerText(actor)));
		Set(report, "unity_frame", Number(frame));
		Set(report, "api", apiEvidence);
		Json handleApi = Object();
		Set(handleApi, "new", Text("void* il2cpp_gchandle_new(void*, bool pinned=true)"));
		Set(handleApi, "get_target", Text("void* il2cpp_gchandle_get_target(void*)"));
		Set(handleApi, "free", Text("void il2cpp_gchandle_free(void*)"));
		Set(report, "handle_api", std::move(handleApi));
		Set(report, "collector_pose_setter_calls", Number(0));
		Set(report, "matrix_layout",
		    Text("row-major JSON, named boxed fields; complete affine transforms include scale"));
		Set(report, "vertex_source", Text(stage == Stage::Vertices
			                                  ? "temporary Mesh populated by SkinnedMeshRenderer.BakeMesh(mesh,false); never sharedMesh.vertices"
			                                  : "not attempted by this stage"));
		Set(report, "selection", Text(stage == Stage::Vertices
			                              ? "unlabelled actual baked vertices within 0.4 world metres of a current Foot or Toe origin; may include body/boot/sole geometry, not an authenticated shoe or sole segmentation"
			                              : "no vertex or shoe geometry evidence requested"));
		Set(report, "renderer_limit", Number(kRendererLimit));
		Set(report, "mesh_vertex_limit", Number(kMeshVertexLimit));
		Set(report, "total_vertex_limit", Number(kTotalVertexLimit));
		Set(report, "candidate_limit", Number(kCandidateLimit));
		if (!Progress("actor_handle_new"))
		{
			Set(report, "status", Text("trace_write_failed"));
			return report;
		}
		if (!actor || !api.handleNew || !api.handleTarget || !api.handleFree)
		{
			Set(report, "status", Text("actor_handle_api_unavailable"));
			return report;
		}
		Pin actorPin(actor);
		Json handle = Object();
		Set(handle, "storage_bytes", Number(sizeof(Handle)));
		Set(handle, "actor_pointer", Text(PointerText(actor)));
		Set(handle, "handle_pointer", Text(PointerText(actorPin.id)));
		Set(handle, "new_returned_nonnull", Flag(actorPin.id != nullptr));
		Progress("actor_handle_first_target");
		void* first = actorPin.Get();
		Set(handle, "first_target_matches_actor", Flag(first == actor));
		Progress("actor_handle_repeated_target");
		void* second = actorPin.Get();
		Set(handle, "repeated_target_matches_actor", Flag(second == actor));
		const bool identity = actorPin.id && first == actor && second == actor;
		if (stage == Stage::Handle || !identity)
		{
			Progress("actor_handle_free");
			const bool hadHandle = actorPin.id != nullptr;
			actorPin.Close();
			Set(handle, "free_completed", Flag(hadHandle));
			Set(report, "actor_handle_validation", std::move(handle));
			Set(report, "status",
			    Text(identity ? "actor_handle_identity_verified" : "actor_gc_handle_identity_failed"));
			Set(report, "renderer_collection_attempted", Flag(false));
			Set(report, "vertex_collection_attempted", Flag(false));
			return report;
		}
		Set(report, "actor_handle_validation", std::move(handle));
		if (!(stage == Stage::Vertices ? available : rendererAvailable))
		{
			Set(report, "status", Text("required_stage_runtime_path_unavailable"));
			return report;
		}
		if (!Progress("foot_toe_matrices"))
		{
			Set(report, "status", Text("trace_write_failed"));
			return report;
		}
		Set(report, "actor_name", Text(Name(actorPin.Get())));
		Foot left(actorPin.Get(), 5), right(actorPin.Get(), 6), leftToe(actorPin.Get(), 19), rightToe(
			     actorPin.Get(), 20);
		const Foot* feet[] = {&left, &right, &leftToe, &rightToe};
		Json bones = Object();
		Set(bones, "left_foot", left.Evidence());
		Set(bones, "right_foot", right.Evidence());
		Set(bones, "left_toe", leftToe.Evidence());
		Set(bones, "right_toe", rightToe.Evidence());
		Set(report, "humanoid_frames", std::move(bones));
		if (!left.valid && !right.valid)
		{
			Set(report, "status", Text("current_foot_frames_unavailable"));
			PoseReadback(report, feet);
			return report;
		}
		if (!Progress("renderer_type_handle"))
		{
			Set(report, "status", Text("trace_write_failed"));
			return report;
		}
		if (!rendererTypeHandle)
		{
			void* klassType = TraceFirst("Il2CppType* il2cpp_class_get_type(SkinnedMeshRenderer class)")
				                  ? api.classType(rendererClass)
				                  : nullptr;
			void* managedType = klassType && TraceFirst("System.Type* il2cpp_type_get_object(Il2CppType*)")
				                    ? api.typeObject(klassType)
				                    : nullptr;
			if (managedType && TraceFirst("void* il2cpp_gchandle_new(void*, bool pinned=true)"))rendererTypeHandle = api
				.handleNew(managedType, true);
		}
		if (!rendererTypeHandle)
		{
			Set(report, "status", Text("renderer_type_handle_unavailable"));
			PoseReadback(report, feet);
			return report;
		}
		if (!Progress("renderer_enumeration"))
		{
			Set(report, "status", Text("trace_write_failed"));
			return report;
		}
		// The Type handle uses the same pointer-width ABI as the actor handle.
		if (!TraceFirst("void* il2cpp_gchandle_get_target(void*)"))
		{
			Set(report, "status", Text("trace_write_failed"));
			return report;
		}
		bool includeInactive = true;
		void* type = api.handleTarget(rendererTypeHandle);
		void* args[] = {type, &includeInactive};
		std::string error;
		if (!type)
		{
			Set(report, "status", Text("renderer_type_handle_target_unavailable"));
			return report;
		}
		Pin renderers(Invoke(getComponents, actorPin.Get(), args, error));
		if (!renderers.Get())
		{
			Set(report, "status", Text("renderer_enumeration_failed"));
			Set(report, "error", Text(error));
			PoseReadback(report, feet);
			return report;
		}
		Set(report, "renderers_array", ArrayInfo(renderers.Get()));
		const uintptr_t count = ArrayLength(renderers.Get());
		Set(report, "renderer_list_truncated", Flag(count > kRendererLimit));
		Json records = Array();
		int processed = 0, candidates = 0, failures = 0, matched = 0;
		const int bounded = static_cast<int>((std::min)(count, static_cast<uintptr_t>(kRendererLimit)));
		for (int i = 0; i < bounded; ++i)
		{
			if (!Progress("renderer_" + std::to_string(i)))
			{
				Set(report, "status", Text("trace_write_failed"));
				return report;
			}
			Pin renderer(Element(renderers.Get(), i, error));
			if (!error.empty() || !renderer.Get() || ObjectClass(renderer.Get()) != rendererClass)
			{
				Json failed = Object();
				Set(failed, "index", Number(i));
				Set(failed, "status", Text("renderer_array_element_failed"));
				Set(failed, "error", Text(error));
				Push(records, std::move(failed));
				++failures;
				error.clear();
				continue;
			}
			Json j = Renderer(renderer.Get(), feet, processed, candidates, stage);
			// Membership remains true after the vertices phase changes status.
			if (IsCurrentActiveFootRenderer(j))++matched;
			Set(j, "index", Number(i));
			Push(records, std::move(j));
		}
		Set(report, "renderers", std::move(records));
		Set(report, "processed_vertices", Number(processed));
		Set(report, "candidate_vertices", Number(candidates));
		Set(report, "renderer_element_failures", Number(failures));
		Set(report, "active_foot_toe_renderers", Number(matched));
		Set(report, "renderer_collection_attempted", Flag(true));
		Set(report, "vertex_collection_attempted", Flag(stage == Stage::Vertices));
		Progress("foot_toe_pose_readback");
		PoseReadback(report, feet);
		Set(report, "status", Text(stage == Stage::Renderers
			                           ? (matched > 0
				                              ? "current_foot_toe_renderers_observed"
				                              : "no_current_foot_toe_renderer_evidence")
			                           : (candidates > 0
				                              ? "neighborhood_vertices_observed"
				                              : "no_neighborhood_vertex_evidence")));
		return report;
	}

	// Keep SEH in a separate POD-only frame. /EHsc cannot catch native AV; putting
	// __try in Collect/Tick (which own Json, Pin and strings) would cause C2712.
	// A native fault can bypass C++ destructors. Do not touch Unity to recover or
	// claim that the actor, GCHandles, or temporary Mesh remain consistent.
	inline void CollectInto(Json* report, void* actor, int frame, float clipTime, uint32_t seq, Stage stage,
	                        const SnapshotContext* context)
	{
		*report = Collect(actor, frame, clipTime, seq, stage, context);
	}

	inline bool GuardedCollect(Json* report, void* actor, int frame, float clipTime, uint32_t seq, Stage stage,
	                           DWORD* faultCode, const SnapshotContext* context = nullptr)
	{
#if defined(_MSC_VER)
		__try
		{
			CollectInto(report, actor, frame, clipTime, seq, stage, context);
			return true;
		}
		__except (GetExceptionCode() == 0xE06D7363UL ? EXCEPTION_CONTINUE_SEARCH : EXCEPTION_EXECUTE_HANDLER)
		{
			*faultCode = GetExceptionCode();
			probeFaulted = true;
			return false;
		}
#else
		// Legacy offline MinGW builds have no MSVC SEH guard. Runtime probe builds
		// use the project's MSVC toolchain; never describe this as native recovery.
		CollectInto(report, actor, frame, clipTime, seq, stage, context);return true;
#endif
	}

	inline void Tick(void* actor, int unityFrame, float clipTime, const SnapshotContext* context = nullptr)
	{
		// The caller owns scene gating. Fail closed if called from any other thread.
		if (probeFaulted || configDirectory.empty() || !actor || GetCurrentThreadId() != resolveThread)return;
		const auto now = std::chrono::steady_clock::now();
		if (now < nextPoll)return;
		nextPoll = now + std::chrono::seconds(1);
		const std::string requestPath = configDirectory + "/shoe_mesh_probe.request.json";
		std::error_code ec;
		const auto size = std::filesystem::file_size(requestPath, ec);
		if (ec || size == 0 || size > 16384)return;
		Json request;
		std::string error;
		if (!GakuMotion::jsonlite::ParseFile(requestPath, request, error) || !request.IsObject())return;
		if (!MatchRequest(request, actor, context, error))return;
		uint32_t seq = 0;
		if (!Sequence(request, seq) || seq <= lastSeq)return;
		lastSeq = seq;
		const std::string output = configDirectory + "/shoe_mesh_probe_" + std::to_string(seq) + ".json";
		if (std::filesystem::exists(output, ec))return; // Re-loading the DLL must not repeat the same request.
		const std::string trace = configDirectory + "/shoe_mesh_probe_" + std::to_string(seq) + ".trace.json";
		if (std::filesystem::exists(trace, ec))return; // Native crash is not permission to replay the same seq.
		auto stage = Stage::Handle;
		const bool validStage = ParseStage(request, stage, error);
		SnapshotContext currentContext = context ? *context : SnapshotContext{};
		if (currentContext.source == Source::OfficialLive)
		{
			const Json* costume = request.Find("costume_id");
			currentContext.costumeId = costume ? costume->stringValue : "";
		}
		const SnapshotContext* evidenceContext = context ? &currentContext : nullptr;
		BeginTrace(trace, stage, seq, unityFrame, clipTime, evidenceContext);
		Json report;
		try
		{
			if (validStage)
			{
				DWORD fault = 0;
				if (!GuardedCollect(&report, actor, unityFrame, clipTime, seq, stage, &fault, evidenceContext))
				{
					traceNativeFault = true;
					traceFaultCode = fault;
					report = Object();
					Set(report, "schema", Text("gakumotion-shoe-mesh-evidence-v2"));
					Set(report, "seq", Number(seq));
					Set(report, "api", apiEvidence);
					Set(report, "status", Text("forensic_failure"));
					Set(report, "native_fault_code", Number(fault));
					Set(report, "fault_phase", Text(tracePhase));
					Set(report, "fault_operation", Text(traceOperation));
					Set(report, "probe_requests_blocked", Flag(true));
					Set(report, "collector_pose_setter_calls", Number(0));
					Set(report, "data_consistency",
					    Text(
						    "unverified after native fault; no subsequent Unity calls or recovery attempted; RAII cleanup may have been bypassed"));
					TraceWrite(traceOperation, "native_fault_stopped");
				}
			}
			else
			{
				Progress("request_rejected");
				report = Object();
				Set(report, "schema", Text("gakumotion-shoe-mesh-evidence-v2"));
				Set(report, "seq", Number(seq));
				Set(report, "status", Text("invalid_stage"));
				Set(report, "error", Text(error));
				Set(report, "collector_pose_setter_calls", Number(0));
			}
		}
		catch (const std::exception& exception)
		{
			report = Object();
			Set(report, "seq", Number(seq));
			Set(report, "status", Text("native_collection_exception"));
			Set(report, "error", Text(exception.what()));
			Set(report, "collector_pose_setter_calls", Number(0));
			Set(report, "data_consistency", Text("unverified: collection terminated before readback"));
		}
		catch (...)
		{
			report = Object();
			Set(report, "seq", Number(seq));
			Set(report, "status", Text("native_collection_exception"));
			Set(report, "collector_pose_setter_calls", Number(0));
			Set(report, "data_consistency", Text("unverified: collection terminated before readback"));
		}
		if (!report.Find("clock_source"))EvidenceContext(report, clipTime, evidenceContext);
		if (!report.Find("actor_pointer"))Set(report, "actor_pointer", Text(PointerText(actor)));
		if (!traceNativeFault)Progress("collection_returned");
		Set(report, "stage", Text(validStage ? StageName(stage) : "invalid"));
		Set(report, "progress", traceProgress);
		Set(report, "trace_path", Text(trace));
		Set(report, "trace_persisted", Flag(traceHealthy));
		Set(report, "trace_write_count", Number(traceWrites));
		traceActive = false;
		const std::string temporary = output + ".tmp";
		{
			std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
			if (!file)
			{
				if (g_hostContext.logWarn)g_hostContext.
					logWarn("[GakuMotion][ShoeMeshProbe] cannot open evidence file");
				return;
			}
			WriteJson(file, report);
			file << '\n';
			file.flush();
			if (!file)
			{
				if (g_hostContext.logWarn)g_hostContext.logWarn("[GakuMotion][ShoeMeshProbe] evidence write failed");
				return;
			}
		}
		std::filesystem::rename(temporary, output, ec);
		if (ec)
		{
			if (g_hostContext.logWarn)g_hostContext.logWarn("[GakuMotion][ShoeMeshProbe] evidence rename failed: %s",
			                                                ec.message().c_str());
			return;
		}
		if (g_hostContext.logInfo)g_hostContext.logInfo("[GakuMotion][ShoeMeshProbe] one-shot evidence saved: %s",
		                                                output.c_str());
	}
} // namespace ShoeMeshProbe
