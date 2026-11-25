using System.Security.Cryptography;
using System.Text;
namespace Catherine.AssetSetup;
static class Shaders {
 public static (byte[] binary,string resources,string stage) Translate(byte[] code,byte[] ctrl){var r=SpirvHarness.Program.Translate(code,ctrl,out var binary);if(binary==null)throw new InvalidDataException("Shader translation failed: "+r.Error);return(SpirvSanitizer.Sanitize(binary),r.Resources,r.Stage);}
 public static void Generate(AssetManifest manifest,string pack,string output,Action<ProgressState> progress,CancellationToken cancel){
  using var sources=new PackageShaderSource(pack,manifest);
  SpirvSanitizer.PrepareTools();var rows=new SortedDictionary<string,string>(StringComparer.Ordinal);int completed=0;object gate=new();
  Parallel.ForEach(manifest.Shaders,new ParallelOptions{MaxDegreeOfParallelism=4,CancellationToken=cancel},s=>{
   byte[] code=sources.Read(s);if(Convert.ToHexString(SHA1.HashData(code)).ToLowerInvariant()[..16]!=s.Hash)throw new InvalidDataException("Shader source hash mismatch: "+s.Hash);byte[] ctrl=new byte[0x800];foreach(var(k,v)in s.Control)BitConverter.TryWriteBytes(ctrl.AsSpan(k,4),v);
   var (binary,resources,stage)=Translate(code,ctrl);File.WriteAllBytes(Path.Combine(output,s.Hash+".spv"),binary);
   lock(gate){rows[s.Hash]=$"{s.Hash}\tgame\t{stage}\t1\tok\t0.0\t{binary.Length}\t{resources}\t\t\t";progress(new("Generating Vulkan shaders",$"{++completed} / {manifest.Shaders.Count}",completed/(double)manifest.Shaders.Count));}
  });
  File.WriteAllText(Path.Combine(output,"results.tsv"),"hash\tgroup\tstage\tstages_using\tstatus\tms\tspv_bytes\tresources\ttranslator_messages\terror\tdraw_time_queries\n"+string.Join('\n',rows.Values)+"\n",new UTF8Encoding(false));
 }
}
