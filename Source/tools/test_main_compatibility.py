"""Verify both real NSO inputs produce identical supported runtime modules."""
from pathlib import Path
import hashlib,json,subprocess
R=Path(__file__).resolve().parents[2];D=R/'Build/main-compatibility/tests';D.mkdir(parents=True,exist_ok=True)
def main():
 sources=R/'Source/asset_setup'
 project='''<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><UseAppHost>false</UseAppHost></PropertyGroup><ItemGroup>'''
 for name in ['Models.cs','NsoImages.cs','ModuleCompatibility.cs']:
  project+=f'<Compile Include="{sources/name}" Link="{name}"/>'
 project+=f'<EmbeddedResource Include="{sources/"Compatibility/main-93a79c77.xor.zlib"}" LogicalName="Catherine.AssetSetup.Compatibility.Main93a79c77"/></ItemGroup></Project>'
 (D/'Tests.csproj').write_text(project)
 (D/'Program.cs').write_text(r'''using Catherine.AssetSetup;
using System.Security.Cryptography;
using System.Text.Json;
var root=Path.GetFullPath(args[0]);var output=Path.GetFullPath(args[1]);
var manifest=AssetManifest.Load(Path.Combine(root,"Source/resources/assets-required.json"));
var variant=Path.Combine("C:/catherine/go/exefs/main");
var original=Path.Combine(root,"Build/asset-setup/qa_full/Pending/exefs/main");
string Hash(byte[] bytes)=>Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();
var paths=manifest.Modules.ToDictionary(s=>"exefs/"+s.Name,s=>Path.Combine("C:/catherine/go/exefs",s.Name));
paths["exefs/main"]=original;var a=NsoImage.Prepare(manifest,paths);
paths["exefs/main"]=variant;var b=NsoImage.Prepare(manifest,paths);
var result=new Dictionary<string,object>();
foreach(var s in manifest.Modules){
 if(!a[s.Name].AsSpan().SequenceEqual(b[s.Name])||Hash(b[s.Name])!=s.ImageSha256)throw new Exception("Module mismatch: "+s.Name);
 result[s.Name]=new{identical=true,sha256=Hash(b[s.Name]),bytes=b[s.Name].Length};
}
var input=manifest.Files.Single(s=>s.Path=="exefs/main");
foreach(var path in new[]{original,variant}){
 var raw=File.ReadAllBytes(path);if(!input.Accepts(raw.Length,Hash(raw)))throw new Exception("Known input rejected.");
}
var bad=File.ReadAllBytes(variant);bad[0x40]^=1;
if(input.Accepts(bad.Length,Hash(bad)))throw new Exception("Unknown input accepted.");
var changed=Path.Combine(output,"modified-main");File.WriteAllBytes(changed,bad);paths["exefs/main"]=changed;
bool rejected=false;try{NsoImage.Prepare(manifest,paths);}catch(InvalidDataException){rejected=true;}
if(!rejected)throw new Exception("Unrecognized alternate module accepted.");
rejected=false;try{ModuleCompatibility.Normalize(File.ReadAllBytes(variant),new byte[63758336]);}catch(InvalidDataException){rejected=true;}
if(!rejected)throw new Exception("Invalid unpacked module accepted.");
var invalid=Path.Combine(output,"invalid-manifest.json");
var text=File.ReadAllText(Path.Combine(root,"Source/resources/assets-required.json"));
File.WriteAllText(invalid,text.Replace(input.Alternatives[0].Sha256,"invalid"));
rejected=false;try{AssetManifest.Load(invalid);}catch(InvalidDataException){rejected=true;}
if(!rejected)throw new Exception("Invalid alternative metadata accepted.");
var report=new{passed=true,modules=result,both_inputs_accepted=true,unknown_input_rejected=true,invalid_image_rejected=true,invalid_variant_metadata_rejected=true};
File.WriteAllText(Path.Combine(output,"result.json"),JsonSerializer.Serialize(report,new JsonSerializerOptions{WriteIndented=true}));
Console.WriteLine(JsonSerializer.Serialize(report));
''')
 originals=[Path('C:/catherine/go/exefs/main'),R/'Build/asset-setup/qa_full/Pending/exefs/main']
 before={str(p):(hashlib.sha256(p.read_bytes()).hexdigest(),p.stat().st_mtime_ns)for p in originals}
 subprocess.run(['dotnet','run','--project',str(D/'Tests.csproj'),'-c','Release','--',str(R),str(D)],check=True)
 assert all((hashlib.sha256(Path(p).read_bytes()).hexdigest(),Path(p).stat().st_mtime_ns)==state for p,state in before.items()),'Original NSO changed'
 print('Both executable inputs preserved; all four runtime modules match exactly.',flush=True)
if __name__=='__main__':main()
