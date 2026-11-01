namespace GakuMotion.VmdImporter.Native;

using System.Numerics;
using GakuMotion.VmdImporter.Core;
using GakuMotion.VmdImporter.Output;
using GakuMotion.VmdImporter.Rig;
using GakuMotion.VmdImporter.Vmd;

/// <summary>Preserve authored knee FK when the corresponding foot IK is disabled.
/// Product correction after frozen source evaluation; leaves thumb mapping intact.</summary>
internal static class DisabledLegIkFk
{
    internal static void Apply(VmdMotion motion, SourceEvaluationResult result, VirtualMMDRig rig, GkClipData clip)
    {
        var world = result.BoneNames.Select((n,i)=>(n,i)).ToDictionary(x=>x.n,x=>result.DenseRotations[0,x.i]);
        var positions = result.BoneNames.Select((n,i)=>(n,i)).ToDictionary(x=>x.n,x=>result.DensePositions[0,x.i]);
        var alignment = FrameAlignment.BuildFrameAlignment(rig,world,positions);
        foreach (var (side,name,upper,lower) in new[] { ("左","LeftLowerLeg",1,3),("右","RightLowerLeg",2,4) })
        {
            int slot=clip.Bones.FindIndex(b=>b.Name==name);
            if(slot<0) continue;
            Quaternion[] authored;
            if(motion.BoneKeyframes.TryGetValue(side+"ひざ",out var keys))
                authored=MotionSampling.SampleBoneKeyframes(keys,result.TotalFrames,RotationInterpolationMode.NormalizedComponent).Item2;
            else authored=Enumerable.Repeat(Quaternion.Identity,result.TotalFrames).ToArray();
            var properties=motion.PropertyKeyframes.OrderBy(k=>k.Frame).ToArray();
            bool enabled=true; int cursor=0;
            for(int f=0;f<result.TotalFrames;f++)
            {
                while(cursor<properties.Length && properties[cursor].Frame<=(uint)f)
                {
                    if(properties[cursor].IkSwitches.TryGetValue(side+"足ＩＫ",out bool value)) enabled=value;
                    cursor++;
                }
                if(!enabled)
                    clip.BoneDeltas![f,slot]=Math32.NormalizeQuaternionWxyz(
                        Quaternion.Inverse(alignment[upper])*Math32.ToUnityRotation(authored[f])*alignment[lower]);
            }
        }
    }
}
