#pragma once
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace GakuMotion
{
	// Metadata handles are process-lifetime metadata, never managed instances.
	struct ExactMethodMetadata
	{
		const void* (*methods)(void*, void**){nullptr};
		const char* (*name)(const void*){nullptr};
		uint32_t (*count)(const void*){nullptr};
		uint32_t (*flags)(const void*, uint32_t*){nullptr};
		bool (*generic)(const void*){nullptr};
		bool (*inflated)(const void*){nullptr};
		void* (*declaringClass)(const void*){nullptr};
		void* (*parameter)(const void*, uint32_t){nullptr};
		void* (*result)(const void*){nullptr};
		int (*typeCode)(void*){nullptr};
		bool (*byRef)(void*){nullptr};
		void* (*classFromType)(void*){nullptr};

		struct Type { int code; void* klass{nullptr}; bool isByRef{false}; };
		bool Matches(void* actual, Type expected) const
		{
			return actual && byRef(actual) == expected.isByRef && typeCode(actual) == expected.code &&
				(!expected.klass || classFromType(actual) == expected.klass);
		}

		const void* Find(void* owner, const char* methodName, bool isStatic,
			Type returns, std::initializer_list<Type> parameters) const
		{
			if (!owner || !methodName || !methods || !name || !count || !flags || !generic ||
				!inflated || !declaringClass || !parameter || !result || !typeCode || !byRef || !classFromType)
				return nullptr;
			const void* match = nullptr;
			void* iterator = nullptr;
			while (const void* method = methods(owner, &iterator))
			{
				const char* candidate = name(method);
				if (!candidate || std::strcmp(candidate, methodName) || generic(method) || inflated(method) ||
					declaringClass(method) != owner || count(method) != parameters.size()) continue;
				uint32_t implementation = 0;
				const uint32_t attributes = flags(method, &implementation);
				if (((attributes & 0x10) != 0) != isStatic || (attributes & 0x400) ||
					!Matches(result(method), returns)) continue;
				bool valid = true;
				uint32_t index = 0;
				for (Type expected : parameters)
					if (!Matches(parameter(method, index++), expected)) { valid = false; break; }
				if (!valid) continue;
				if (match) return nullptr;
				match = method;
			}
			return match;
		}
	};
}
