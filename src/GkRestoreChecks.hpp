#pragma once

namespace GakuMotion
{
	// A confirmed result means owned state was written back and read back through
	// the supplied consumer API. It is not proof of final rendered pixels.
	struct CaptureRestoreStatus
	{
		bool modified{false};
		bool confirmed{true};
	};
	struct BodyFaceRestoreResult
	{
		bool bodyModified{false}, bodyConfirmed{true};
		bool faceModified{false}, faceConfirmed{true};
		bool gazeModified{false}, gazeConfirmed{true};
	};
}
