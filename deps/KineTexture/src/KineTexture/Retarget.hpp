#pragma once

#include "Sampling.hpp"
#include <array>
#include <new>
#include <utility>
#include <vector>

namespace KineTexture
{
    // New double-precision numerical APIs. The established float runtime types
    // in Math.hpp retain their layouts and arithmetic unchanged.
    struct Vector3d { double x{0}, y{0}, z{0}; };
    struct Quaterniond { double x{0}, y{0}, z{0}, w{1}; };
    using Matrix3d = std::array<std::array<double, 3>, 3>;
    enum class DeltaSpace { Local, Parent };

    inline bool IsFinite(const Vector3d& v) noexcept
    { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
    inline bool IsFinite(const Quaterniond& q) noexcept
    { return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w); }

    namespace NativeDetail
    {
        inline Vector3d Add(Vector3d a, Vector3d b) noexcept { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
        inline Vector3d Sub(Vector3d a, Vector3d b) noexcept { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
        inline Vector3d Scale(Vector3d a, double s) noexcept { return {a.x*s,a.y*s,a.z*s}; }
        inline double Dot(Vector3d a, Vector3d b) noexcept { return a.x*b.x+a.y*b.y+a.z*b.z; }
        inline Vector3d Cross(Vector3d a, Vector3d b) noexcept
        { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
        inline double Length(Vector3d a) noexcept { return std::hypot(a.x,a.y,a.z); }
        inline Quaterniond Conjugate(Quaterniond q) noexcept { return {-q.x,-q.y,-q.z,q.w}; }
        inline Quaterniond Multiply(Quaterniond a, Quaterniond b) noexcept
        {
            return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
                    a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
                    a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
                    a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
        }
        inline bool Normalize(Quaterniond q, Quaterniond& out) noexcept
        {
            const double n = std::hypot(std::hypot(q.x,q.y),std::hypot(q.z,q.w));
            if (!IsFinite(q) || !std::isfinite(n) || n < 1e-12) return false;
            out = {q.x/n,q.y/n,q.z/n,q.w/n};
            return true;
        }
        // q must be normalized. Public numerical operations validate this first.
        inline Vector3d Rotate(Quaterniond q, Vector3d v) noexcept
        {
            const Vector3d axis{q.x,q.y,q.z};
            const auto t = Scale(Cross(axis,v),2);
            return Add(v,Add(Scale(t,q.w),Cross(axis,t)));
        }
        inline Vector3d Transform(const Matrix3d& b, Vector3d v) noexcept
        {
            return {b[0][0]*v.x+b[0][1]*v.y+b[0][2]*v.z,
                    b[1][0]*v.x+b[1][1]*v.y+b[1][2]*v.z,
                    b[2][0]*v.x+b[2][1]*v.y+b[2][2]*v.z};
        }
        inline bool BoneBasis(Vector3d primary, Vector3d hinge, Matrix3d& out) noexcept
        {
            const double a = Length(primary);
            if (!IsFinite(primary) || !IsFinite(hinge) || !std::isfinite(a) || a < 1e-12) return false;
            const auto x = Scale(primary,1/a);
            auto y = Sub(hinge,Scale(x,Dot(x,hinge)));
            const double b = Length(y);
            if (!IsFinite(y) || !std::isfinite(b) || b < 1e-8) return false;
            y = Scale(y,1/b);
            const auto z = Cross(x,y);
            out = {{{x.x,y.x,z.x},{x.y,y.y,z.y},{x.z,y.z,z.z}}};
            return true;
        }
        inline Quaterniond FromMatrix(const Matrix3d& m) noexcept
        {
            // Largest-component construction avoids division by a tiny scalar
            // at 180 degrees. The input is a prepared right-handed frame.
            Quaterniond q;
            const double trace = m[0][0]+m[1][1]+m[2][2];
            if (trace > 0)
            {
                const double s=2*std::sqrt(1+trace);
                q={(m[2][1]-m[1][2])/s,(m[0][2]-m[2][0])/s,(m[1][0]-m[0][1])/s,s/4};
            }
            else
            {
                const int i = m[0][0] >= m[1][1] && m[0][0] >= m[2][2] ? 0 : (m[1][1] >= m[2][2] ? 1 : 2);
                const int j=(i+1)%3,k=(i+2)%3;
                const double s=2*std::sqrt(std::max(0.0,1+m[i][i]-m[j][j]-m[k][k]));
                double v[3]{};
                v[i]=s/4; v[j]=(m[j][i]+m[i][j])/s; v[k]=(m[k][i]+m[i][k])/s;
                q={v[0],v[1],v[2],(m[k][j]-m[j][k])/s};
            }
            Quaterniond normalized;
            Normalize(q,normalized);
            return normalized;
        }
        inline bool Overlap(std::span<const Quaterniond> input, std::span<Quaterniond> output) noexcept
        {
            if (input.empty() || output.empty()) return false;
            const auto a=reinterpret_cast<std::uintptr_t>(input.data());
            const auto b=reinterpret_cast<std::uintptr_t>(output.data());
            return a < b+output.size_bytes() && b < a+input.size_bytes();
        }
    }

    // K maps target-frame coordinates into source-frame coordinates. Both axes
    // are explicit: a segment direction alone cannot determine roll.
    inline Status AlignBoneFrames(Vector3d sourcePrimary, Vector3d sourceHinge,
        Vector3d targetPrimary, Vector3d targetHinge, Quaterniond& output) noexcept
    {
        Matrix3d source,target,k{};
        if (!NativeDetail::BoneBasis(sourcePrimary,sourceHinge,source) ||
            !NativeDetail::BoneBasis(targetPrimary,targetHinge,target)) return {ErrorCode::InvalidAxes};
        for (int i=0;i<3;++i) for (int j=0;j<3;++j)
            for (int a=0;a<3;++a) k[i][j]+=source[i][a]*target[j][a];
        const auto result=NativeDetail::FromMatrix(k);
        if (!IsFinite(result)) return {ErrorCode::InvalidRotation};
        output=result;
        return {};
    }
    inline Status RebaseAbsoluteRotation(Quaterniond source, Quaterniond targetToSource,
        Quaterniond& output) noexcept
    {
        Quaterniond a,k,result;
        if (!NativeDetail::Normalize(source,a) || !NativeDetail::Normalize(targetToSource,k)) return {ErrorCode::InvalidRotation};
        NativeDetail::Normalize(NativeDetail::Multiply(a,k),result);
        output=result;
        return {};
    }
    inline Status RebaseRotationDelta(Quaterniond source, Quaterniond targetToSource,
        Quaterniond& output) noexcept
    {
        Quaterniond a,k,result;
        if (!NativeDetail::Normalize(source,a) || !NativeDetail::Normalize(targetToSource,k)) return {ErrorCode::InvalidRotation};
        NativeDetail::Normalize(NativeDetail::Multiply(NativeDetail::Multiply(NativeDetail::Conjugate(k),a),k),result);
        output=result;
        return {};
    }
    inline Status ComposeRotationDelta(Quaterniond reference, Quaterniond delta, DeltaSpace space,
        Quaterniond& output) noexcept
    {
        if (space!=DeltaSpace::Local && space!=DeltaSpace::Parent) return {ErrorCode::InvalidSchema};
        Quaterniond a,b,result;
        if (!NativeDetail::Normalize(reference,a) || !NativeDetail::Normalize(delta,b)) return {ErrorCode::InvalidRotation};
        NativeDetail::Normalize(space==DeltaSpace::Local ? NativeDetail::Multiply(a,b) : NativeDetail::Multiply(b,a),result);
        output=result;
        return {};
    }

    // Passive basis conversion B and positive length scale. Reflections use
    // det(B)*B on q.xyz and preserve q.w and the input quaternion norm.
    // Prepare and conversion errors preserve the previous object/output.
    class CoordinateTransform
    {
        Matrix3d basis_{};
        double scale_{1}, parity_{1};
        bool prepared_{false};
        std::array<int,3> signedAxes_{{-1,-1,-1}};
    public:
        Status Prepare(const Matrix3d& basis, double lengthScale) noexcept
        {
            if (!std::isfinite(lengthScale) || lengthScale<=0) return {ErrorCode::InvalidSchema};
            for (const auto& row:basis) for (double v:row) if (!std::isfinite(v)) return {ErrorCode::NonFinite};
            for (int i=0;i<3;++i) for (int j=0;j<3;++j)
            {
                double dot=0;
                for (int a=0;a<3;++a) dot+=basis[a][i]*basis[a][j];
                if (!std::isfinite(dot) || std::abs(dot-(i==j ? 1.0 : 0.0))>1e-10) return {ErrorCode::InvalidAxes};
            }
            const double determinant=basis[0][0]*(basis[1][1]*basis[2][2]-basis[1][2]*basis[2][1])
                -basis[0][1]*(basis[1][0]*basis[2][2]-basis[1][2]*basis[2][0])
                +basis[0][2]*(basis[1][0]*basis[2][1]-basis[1][1]*basis[2][0]);
            std::array<int,3> axes{{-1,-1,-1}};
            bool signedPermutation=true;
            for (int i=0;i<3;++i)
            {
                int count=0;
                for (int j=0;j<3;++j) if (basis[i][j]!=0)
                {
                    ++count; axes[i]=j;
                    if (std::abs(basis[i][j])!=1) signedPermutation=false;
                }
                if (count!=1) signedPermutation=false;
            }
            basis_=basis; scale_=lengthScale; parity_=determinant>0 ? 1 : -1;
            signedAxes_=signedPermutation ? axes : std::array<int,3>{{-1,-1,-1}};
            prepared_=true;
            return {};
        }
        Status Inverse(CoordinateTransform& output) const noexcept
        {
            if (!prepared_) return {ErrorCode::NotPrepared};
            Matrix3d transpose{};
            for (int i=0;i<3;++i) for (int j=0;j<3;++j) transpose[i][j]=basis_[j][i];
            CoordinateTransform result;
            if (const auto s=result.Prepare(transpose,1/scale_); !s) return s;
            output=result;
            return {};
        }
        Status Vector(Vector3d input, Vector3d& output) const noexcept
        {
            if (!prepared_) return {ErrorCode::NotPrepared};
            if (!IsFinite(input)) return {ErrorCode::NonFinite};
            const auto result=NativeDetail::Scale(NativeDetail::Transform(basis_,input),scale_);
            if (!IsFinite(result)) return {ErrorCode::NonFinite};
            output=result;
            return {};
        }
        Status Rotation(Quaterniond input, Quaterniond& output) const noexcept
        {
            if (!prepared_) return {ErrorCode::NotPrepared};
            Quaterniond normalized;
            if (!NativeDetail::Normalize(input,normalized)) return {ErrorCode::InvalidRotation};
            Vector3d vector;
            if (signedAxes_[0]>=0)
            {
                const double v[3]{input.x,input.y,input.z};
                double result[3];
                for (int i=0;i<3;++i) result[i]=(parity_*basis_[i][signedAxes_[i]])*v[signedAxes_[i]];
                vector={result[0],result[1],result[2]};
            }
            else vector=NativeDetail::Scale(NativeDetail::Transform(basis_,{input.x,input.y,input.z}),parity_);
            const Quaterniond result{vector.x,vector.y,vector.z,input.w};
            if (!IsFinite(result)) return {ErrorCode::NonFinite};
            output=result;
            return {};
        }
    };

    // Explicit topology only; no names, humanoid defaults, masks or rest-pose
    // inference. Prepare allocates once. Evaluation allocates no heap storage.
    // Inputs and outputs must not overlap. Each consumer owns its output buffer.
    class RotationHierarchy
    {
        std::vector<std::int32_t> parents_;
        std::vector<std::size_t> order_;
        bool prepared_{false};
        Status Preflight(std::span<const Quaterniond> input, std::span<Quaterniond> output) const noexcept
        {
            if (!prepared_) return {ErrorCode::NotPrepared};
            if (input.size()!=parents_.size() || output.size()!=parents_.size()) return {ErrorCode::InvalidSize};
            if (NativeDetail::Overlap(input,output)) return {ErrorCode::InvalidSize};
            for (std::size_t i=0;i<input.size();++i)
            {
                Quaterniond q;
                if (!NativeDetail::Normalize(input[i],q)) return {ErrorCode::InvalidRotation,i};
            }
            return {};
        }
    public:
        Status Prepare(std::span<const std::int32_t> parents, std::size_t jointBudget=65536)
        {
            if (parents.size()>jointBudget || parents.size()>static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
                return {ErrorCode::BudgetExceeded};
            try
            {
                std::vector<std::int32_t> copy(parents.begin(),parents.end());
                std::vector<std::size_t> order,path;
                std::vector<std::uint8_t> state(parents.size(),0);
                order.reserve(parents.size()); path.reserve(parents.size());
                for (std::size_t i=0;i<parents.size();++i)
                    if (parents[i]<-1 || (parents[i]>=0 && static_cast<std::size_t>(parents[i])>=parents.size())) return {ErrorCode::InvalidParent,i};
                for (std::size_t start=0;start<parents.size();++start)
                {
                    path.clear(); auto index=static_cast<std::int32_t>(start);
                    while (index!=-1 && state[index]==0)
                    { state[index]=1; path.push_back(static_cast<std::size_t>(index)); index=parents[index]; }
                    if (index!=-1 && state[index]==1) return {ErrorCode::Cycle,static_cast<std::size_t>(index)};
                    for (auto it=path.rbegin();it!=path.rend();++it) { state[*it]=2; order.push_back(*it); }
                }
                parents_.swap(copy); order_.swap(order); prepared_=true;
                return {};
            }
            catch (const std::bad_alloc&) { return {ErrorCode::AllocationFailed}; }
        }
        Status WorldToLocal(std::span<const Quaterniond> world, std::span<Quaterniond> output) const noexcept
        {
            if (const auto s=Preflight(world,output); !s) return s;
            for (std::size_t i=0;i<world.size();++i)
            {
                Quaterniond value;
                NativeDetail::Normalize(world[i],value);
                if (parents_[i]>=0)
                {
                    Quaterniond parent;
                    NativeDetail::Normalize(world[parents_[i]],parent);
                    NativeDetail::Normalize(NativeDetail::Multiply(NativeDetail::Conjugate(parent),value),value);
                }
                output[i]=value;
            }
            return {};
        }
        Status LocalToWorld(std::span<const Quaterniond> local, std::span<Quaterniond> output) const noexcept
        {
            if (const auto s=Preflight(local,output); !s) return s;
            for (const auto i:order_)
            {
                Quaterniond value;
                NativeDetail::Normalize(local[i],value);
                if (parents_[i]>=0)
                    NativeDetail::Normalize(NativeDetail::Multiply(output[parents_[i]],value),value);
                output[i]=value;
            }
            return {};
        }
    };
}
