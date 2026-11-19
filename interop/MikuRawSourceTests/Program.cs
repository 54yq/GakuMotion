using GakuMotion.VmdImporter.Native.Facial;
using GakuMotion.VmdImporter.Vmd;

int checks = 0;
void Require(bool value, string label) { checks++; if (!value) throw new Exception(label); }
Dictionary<string, float[]> Translate(params (string Name, float[] Values)[] tracks)
{
    var input = tracks.ToDictionary(t => t.Name, t => (IReadOnlyList<MorphKeyframe>)t.Values.Select((v,i) => new MorphKeyframe(t.Name,(uint)i,v)).ToArray());
    var result = MorphTranslator.Translate(input, 3, null);
    return result.Names.Select((name,i) => (name, values: Enumerable.Range(0,3).Select(f => result.Weights[f,i]).ToArray())).ToDictionary(t=>t.name,t=>t.values);
}
const string P = "miku.raw.v1.";
var e = Translate(("え",new[]{.3f,.6f,.9f}));
Require(e[P+"e"].SequenceEqual(new[]{.3f,.6f,.9f}),"raw E retained");
Require(e[P+"a"].All(v=>v==0) && e[P+"present"].All(v=>v==1),"missing/intent explicit");
Require(e.Keys.Any(n=>n.StartsWith("b_mouth.")),"legacy E mapped curve retained");
Require(!Translate(("まばたき",new[]{.7f,.7f,.7f})).ContainsKey(P+"present"),"no vowels no intent");
var eye = Translate(("あ",new float[3]),("まばたき",new[]{.7f,.7f,.7f}));
Require(eye[P+"unsupported"].All(v=>v==0),"known eyes supported");
foreach (var name in new[]{"mystery","a"}) {
    var c = Translate(("あ",new float[3]),(name,new[]{0f,.25f,.5f}));
    Require(c[P+"unsupported"].SequenceEqual(new[]{0f,.25f,.5f}),"unknown/alias activity blocks");
}
foreach (var name in new[]{"b_mouth.mouth_028","tooth"})
    Require(Translate(("あ",new[]{.5f,.5f,.5f}),(name,new float[3]))[P+"native_authored"].All(v=>v==1),"explicit zero native blocks");
var invalid = Translate(("あ",new[]{-.2f,.5f,1.2f}),("い",new[]{.8f,.8f,.8f}));
Require(invalid[P+"unsupported"].SequenceEqual(new[]{1f,0f,1f}),"invalid values guarded");
Require(invalid[P+"a"].SequenceEqual(new[]{0f,.5f,1f}),"valid serialization");
Require(invalid[P+"a"][1]+invalid[P+"i"][1]>1,"no hidden simplex normalization");

const string E = "miku.raweye.v1.";
var rawEye = Translate(("真面目",new[]{.2f,.5f,.9f}));
Require(rawEye[E+"真面目"].SequenceEqual(new[]{.2f,.5f,.9f}),"raw eye key retained verbatim");
Require(rawEye[E+"present"].All(v=>v==1),"eye intent explicit");
Require(rawEye[E+"unsupported"].All(v=>v==0),"a fitted key is not an unsupported source");
Require(rawEye[E+"native_authored"].All(v=>v==0),"a mapped curve is not native eye authorship");
Require(rawEye.Keys.Count(n=>n.StartsWith(E,StringComparison.Ordinal))==MikuRawEyeKeys.Names.Count+3,
    "every table key is emitted even when the clip leaves it at zero");
Require(!Translate(("え",new[]{.3f,.3f,.3f})).Keys.Any(n=>n.StartsWith(E,StringComparison.Ordinal)),
    "no fitted eye source, no eye contract");
// Eight of the twelve accepted keys have no morph-map entry at all; the fit
// must still see them, which is the whole reason the key table is embedded.
Require(Translate(("たれ目",new float[3]))[E+"たれ目"].All(v=>v==0),"unmapped accepted key keeps its identity");
Require(Translate(("まばたき",new[]{.7f,.7f,.7f}))[E+"まばたき"].All(v=>v==.7f),"a key that also maps natively stays raw");
foreach (var name in new[]{"b_eye.eye_001","b_brow.brow_003"})
    Require(Translate(("真面目",new float[3]),(name,new float[3]))[E+"native_authored"].All(v=>v==1),"explicit zero native eye blocks");
var strayEye = Translate(("blink",new[]{0f,.25f,.5f}));
Require(strayEye[E+"unsupported"].SequenceEqual(new[]{0f,.25f,.5f}),"eye-family source outside the table refuses the clip");
Require(strayEye[E+"present"].All(v=>v==1),"the refusal is still an eye contract, not a legacy clip");
Require(Translate(("真面目",new[]{-1f,.5f,2f}))[E+"真面目"].SequenceEqual(new[]{0f,.5f,1f}),"eye keys clamp to [0,1]");
Console.WriteLine($"Miku raw-source product translator: {checks} checks passed.");
