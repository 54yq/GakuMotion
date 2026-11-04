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
Console.WriteLine($"Miku raw-source product translator: {checks} checks passed.");
