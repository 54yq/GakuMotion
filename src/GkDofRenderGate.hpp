#pragma once
namespace GakuMotion {
// Experimental bypass assumes false means the caller keeps its source target.
// The native ABI and actual return semantics require in-game acceptance.
template<class Original, class... Args>
bool InvokeDofRender(bool disabled, Original original, Args... args) {
    if (disabled) return false;
    return original(args...);
}
}
