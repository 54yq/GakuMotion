#pragma once

#include "Retarget.hpp"
#include <numbers>
#include <optional>

namespace KineTexture
{
    struct TwoBoneInput
    {
        Vector3d root_position, goal_position;
        Vector3d upper_offset, lower_offset; // Child position in each parent's local frame.
        Quaterniond upper_world_hint, lower_world_hint;
        std::optional<Vector3d> previous_pole, pole_hint; // World directions, caller-owned temporal state.
    };
    struct TwoBoneSolution
    {
        Quaterniond upper_world, lower_world;
        Vector3d joint_position, end_position, pole;
        double requested_distance{0}, reachable_distance{0}, residual{0};
        bool reachable{false};
    };
    namespace NativeDetail
    {
        inline Vector3d LocalOrthogonal(Vector3d direction) noexcept
        {
            const double x=std::abs(direction.x),y=std::abs(direction.y),z=std::abs(direction.z);
            const auto reference=x<=y && x<=z ? Vector3d{1,0,0} : (y<=z ? Vector3d{0,1,0} : Vector3d{0,0,1});
            const auto axis=Cross(direction,reference);
            return Scale(axis,1/Length(axis));
        }
        inline bool AimOffset(Quaterniond hint, Vector3d localUnit, Vector3d desired, Quaterniond& output) noexcept
        {
            if (!IsFinite(desired)) return false;
            const auto current=Rotate(hint,localUnit);
            const auto cross=Cross(current,desired);
            const double length=Length(cross),rawDot=Dot(current,desired);
            if (!IsFinite(cross) || !std::isfinite(length) || !std::isfinite(rawDot)) return false;
            const double dot=std::clamp(rawDot,-1.0,1.0);
            Vector3d axis;
            double angle;
            if (length<1e-12)
            {
                if (dot>=0) { output=hint; return true; }
                axis=Rotate(hint,LocalOrthogonal(localUnit));
                angle=std::numbers::pi;
            }
            else { axis=Scale(cross,1/length); angle=std::atan2(length,dot); }
            const double sine=std::sin(angle/2);
            return Normalize(Multiply({axis.x*sine,axis.y*sine,axis.z*sine,std::cos(angle/2)},hint),output);
        }
    }
    // Analytical chain geometry, not a contact or target-character policy.
    // Segment lengths and every position share caller-declared units. World
    // rotations preserve hint roll via minimal swing. Pole precedence matches
    // the research implementation, including the explicit-pole fallback order.
    // Unreachable goals clamp radial distance and return actual FK endpoints.
    // Reset previous_pole at cuts or IK-disabled intervals. No state is cached.
    // No heap allocations; on every error the caller's solution is unchanged.
    inline Status SolveTwoBone(const TwoBoneInput& input, TwoBoneSolution& output) noexcept
    {
        using namespace NativeDetail;
        if (!IsFinite(input.root_position) || !IsFinite(input.goal_position) ||
            !IsFinite(input.upper_offset) || !IsFinite(input.lower_offset) ||
            (input.previous_pole && !IsFinite(*input.previous_pole)) ||
            (input.pole_hint && !IsFinite(*input.pole_hint))) return {ErrorCode::NonFinite};
        Quaterniond upper,lower;
        if (!Normalize(input.upper_world_hint,upper) || !Normalize(input.lower_world_hint,lower)) return {ErrorCode::InvalidRotation};
        const double a=Length(input.upper_offset),b=Length(input.lower_offset);
        if (!std::isfinite(a) || !std::isfinite(b)) return {ErrorCode::NonFinite};
        if (std::min(a,b)<1e-12) return {ErrorCode::InvalidAxes};
        const auto upperUnit=Scale(input.upper_offset,1/a),lowerUnit=Scale(input.lower_offset,1/b);
        const auto upperDirection=Rotate(upper,upperUnit),lowerDirection=Rotate(lower,lowerUnit);
        const auto intrinsic=Rotate(upper,LocalOrthogonal(upperUnit));
        const auto goal=Sub(input.goal_position,input.root_position);
        const double requested=Length(goal),minimum=std::abs(a-b),maximum=a+b;
        if (!std::isfinite(requested) || !std::isfinite(maximum)) return {ErrorCode::NonFinite};
        const double distance=std::clamp(requested,minimum,maximum);
        Vector3d ray;
        if (requested>0) ray={goal.x/requested,goal.y/requested,goal.z/requested};
        else
        {
            const auto chain=Add(Scale(upperDirection,a),Scale(lowerDirection,b));
            const double n=Length(chain);
            if (!IsFinite(chain) || !std::isfinite(n)) return {ErrorCode::NonFinite};
            ray=n>maximum*1e-12 ? Scale(chain,1/n) : intrinsic;
        }
        std::array<Vector3d,4> candidates;
        std::size_t count=0;
        if (input.pole_hint) candidates[count++]=*input.pole_hint;
        else candidates[count++]=upperDirection;
        if (input.previous_pole) candidates[count++]=*input.previous_pole;
        if (input.pole_hint)
        {
            candidates[count++]=intrinsic;
            candidates[count++]=Cross(upperDirection,intrinsic);
        }
        else { candidates[count++]=Scale(lowerDirection,-1); candidates[count++]=intrinsic; }
        Vector3d pole;
        bool found=false;
        for (std::size_t i=0;i<count;++i)
        {
            const auto projection=Sub(candidates[i],Scale(ray,Dot(candidates[i],ray)));
            const double n=Length(projection);
            if (!IsFinite(projection) || !std::isfinite(n)) return {ErrorCode::NonFinite};
            if (n>1e-8) { pole=Scale(projection,1/n); found=true; break; }
        }
        if (!found) return {ErrorCode::InvalidAxes};
        Vector3d joint,end;
        if (distance==0)
        {
            joint=Add(input.root_position,Scale(upperDirection,a));
            end=input.root_position;
        }
        else
        {
            const double along=((a-b)*maximum+distance*distance)/(2*distance);
            const double heightSquared=(maximum-distance)*(maximum+distance)*(distance-minimum)*(distance+minimum)/(4*distance*distance);
            if (!std::isfinite(along) || !std::isfinite(heightSquared)) return {ErrorCode::NonFinite};
            joint=Add(input.root_position,Add(Scale(ray,along),Scale(pole,std::sqrt(std::max(0.0,heightSquared)))));
            end=Add(input.root_position,Scale(ray,distance));
        }
        if (!IsFinite(joint) || !IsFinite(end)) return {ErrorCode::NonFinite};
        TwoBoneSolution result;
        if (!AimOffset(upper,upperUnit,Scale(Sub(joint,input.root_position),1/a),result.upper_world) ||
            !AimOffset(lower,lowerUnit,Scale(Sub(end,joint),1/b),result.lower_world)) return {ErrorCode::NonFinite};
        result.joint_position=Add(input.root_position,Rotate(result.upper_world,input.upper_offset));
        result.end_position=Add(result.joint_position,Rotate(result.lower_world,input.lower_offset));
        result.pole=pole;
        result.requested_distance=requested; result.reachable_distance=distance;
        result.residual=Length(Sub(input.goal_position,result.end_position));
        const double tolerance=maximum*1e-10;
        result.reachable=requested>=minimum-tolerance && requested<=maximum+tolerance;
        if (!IsFinite(result.upper_world) || !IsFinite(result.lower_world) || !IsFinite(result.joint_position) ||
            !IsFinite(result.end_position) || !std::isfinite(result.residual)) return {ErrorCode::NonFinite};
        output=result;
        return {};
    }
}
