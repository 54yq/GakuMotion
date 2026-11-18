#pragma once
#include <cstdint>

namespace GakuMotion
{
	// Runtime metadata only: no guessed object offsets or unchecked managed calls.
	// Invoked by the actor hook on Unity's main thread.
	struct FaceRuntimeAccess
	{
		void* (*objectClass)(void*){nullptr};
		void* (*parentClass)(void*){nullptr};
		void* (*findField)(void*, const char*){nullptr};
		void* (*fieldType)(void*){nullptr};
		uint32_t (*fieldFlags)(void*){nullptr};
		int (*typeCode)(void*){nullptr};
		void (*readField)(void*, void*, void*){nullptr};
		void (*writeField)(void*, void*, void*){nullptr};
		const void* (*findMethod)(void*, const char*, int){nullptr};
		void* (*returnType)(const void*){nullptr};
		uint32_t (*methodFlags)(const void*, uint32_t*){nullptr};
		void* (*invoke)(const void*, void*, void**, void**){nullptr};

		void* InstanceField(void* object, const char* name, int expectedType) const
		{
			if (!object || !objectClass || !findField || !fieldType || !fieldFlags || !typeCode)
				return nullptr;
			for (void* klass = objectClass(object); klass;
			     klass = parentClass ? parentClass(klass) : nullptr)
			{
				void* field = findField(klass, name);
				void* type = field ? fieldType(field) : nullptr;
				if (type && !(fieldFlags(field) & 0x10) && typeCode(type) == expectedType)
					return field;
			}
			return nullptr;
		}

		void* Facial(void* actor) const
		{
			if (!readField) return nullptr;
			void* field = InstanceField(actor, "_facial", 0x12); // Managed class reference.
			if (!field) return nullptr;
			void* value = nullptr;
			readField(actor, field, &value);
			return value;
		}

		void* Talk(void* actor, int slot) const
		{
			if (!readField || slot < 0 || slot > 1) return nullptr;
			const char* name = slot == 0 ? "_talk" : "_criTalk";
			void* field = InstanceField(actor, name, 0x12);
			if (!field) field = InstanceField(actor, name, 0x15); // Generic class instance.
			void* value = nullptr;
			if (field) readField(actor, field, &value);
			return value;
		}
		bool ReadTalk(void* talk, bool& enabled) const
		{
			void* field = InstanceField(talk, "_talkEnabled", 0x02);
			if (!field || !readField) return false;
			uint8_t value = 0;
			readField(talk, field, &value);
			enabled = value != 0;
			return true;
		}
		bool WriteTalk(void* talk, bool enabled) const
		{
			void* field = InstanceField(talk, "_talkEnabled", 0x02);
			if (!field || !writeField || (fieldFlags(field) & 0x20)) return false;
			uint8_t value = enabled ? 1 : 0;
			writeField(talk, field, &value);
			bool after = !enabled;
			return ReadTalk(talk, after) && after == enabled;
		}

		bool ReadBlink(void* facial, bool& useBlink, bool& autoDisabled) const
		{
			if (!readField) return false;
			void* use = InstanceField(facial, "useBlink", 0x02);
			void* disabled = InstanceField(facial, "autoBlinkDisabled", 0x02);
			if (!use || !disabled) return false;
			uint8_t u = 0, d = 0;
			readField(facial, use, &u);
			readField(facial, disabled, &d);
			useBlink = u != 0;
			autoDisabled = d != 0;
			return true;
		}

		bool WriteBlink(void* facial, bool useBlink, bool autoDisabled) const
		{
			if (!writeField) return false;
			void* use = InstanceField(facial, "useBlink", 0x02);
			void* disabled = InstanceField(facial, "autoBlinkDisabled", 0x02);
			// Resolve both before either mutation, and reject readonly fields.
			if (!use || !disabled || ((fieldFlags(use) | fieldFlags(disabled)) & 0x20)) return false;
			uint8_t u = useBlink ? 1 : 0, d = autoDisabled ? 1 : 0;
			writeField(facial, use, &u);
			writeField(facial, disabled, &d);
			return true;
		}

		bool RefreshHighlight(void* actor) const
		{
			if (!actor || !objectClass || !findMethod || !returnType || !typeCode || !methodFlags || !invoke)
				return false;
			for (void* klass = objectClass(actor); klass;
			     klass = parentClass ? parentClass(klass) : nullptr)
			{
				const void* method = findMethod(klass, "UpdateHighLightMaterial", 0);
				uint32_t implementationFlags = 0;
				void* type = method ? returnType(method) : nullptr;
				if (!method || (methodFlags(method, &implementationFlags) & 0x10) ||
					!type || typeCode(type) != 0x01)
					continue; // Instance void, no arguments.
				void* exception = nullptr;
				invoke(method, actor, nullptr, &exception);
				return !exception;
			}
			return false;
		}
	};
} // namespace GakuMotion
