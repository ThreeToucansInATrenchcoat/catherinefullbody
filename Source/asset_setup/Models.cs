using System.Text.Json;
namespace Catherine.AssetSetup;

sealed class InputFile {
 public string Path {get;set;}="";
 public long Size {get;set;}
 public string Sha256 {get;set;}="";
 public string Kind {get;set;}="game";
 public long RomfsOffset {get;set;}=-1;
 public List<InputVariant> Alternatives {get;set;}=new();
 public bool AcceptsSize(long size)=>Size==size||Alternatives.Any(v=>v.Size==size);
 public bool Accepts(long size,string hash)=>Size==size&&Sha256.Equals(hash,StringComparison.OrdinalIgnoreCase)||Alternatives.Any(v=>v.Size==size&&v.Sha256.Equals(hash,StringComparison.OrdinalIgnoreCase));
}
sealed class InputVariant {
 public long Size {get;set;}
 public string Sha256 {get;set;}="";
}
sealed class ModuleSpec {
 public string Name {get;set;}="";
 public ulong Base {get;set;}
 public int Size {get;set;}
 public string ImageSha256 {get;set;}="";
}
sealed class MetadataSegment {public long Offset{get;set;} public string Data{get;set;}="";}
sealed class ShaderSource {
 public string Hash{get;set;}="";
 public string Kind{get;set;}="";
 public string Path{get;set;}="";
 public string Member{get;set;}="";
 public int Offset{get;set;}
 public int Length{get;set;}
 public Dictionary<int,uint> Control{get;set;}=new();
}
sealed class AssetManifest {
 public int Version {get;set;}
 public List<InputFile> Files {get;set;}=new();
 public List<ModuleSpec> Modules{get;set;}=new();
 public long RomfsSize{get;set;}
 public List<MetadataSegment> RomfsMetadata{get;set;}=new();
 public List<ShaderSource> Shaders{get;set;}=new();
 public List<string> PackageSha256{get;set;}=new();
 public string CanonicalPackageSha256{get;set;}="";
 public static AssetManifest Load(string path) {
  if(new FileInfo(path).Length>32*1024*1024)throw new InvalidDataException("The required-files list is too large.");
  var m=JsonSerializer.Deserialize<AssetManifest>(File.ReadAllText(path),new JsonSerializerOptions{PropertyNameCaseInsensitive=true})??throw new InvalidDataException("The required-files list is empty.");
  if(m.Version!=1||m.Files.Count==0||m.Files.Count>100000)throw new InvalidDataException("Unsupported required-files list.");
  var seen=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
  foreach(var f in m.Files){
   if(!SafeRelative(f.Path)||!seen.Add(f.Path)||f.Size<0||!IsHash(f.Sha256))throw new InvalidDataException("Invalid required-file record: "+f.Path);
   if(f.Alternatives is null||f.Alternatives.Count>16||f.Alternatives.Any(v=>v is null||v.Size<0||!IsHash(v.Sha256)))throw new InvalidDataException("Invalid alternative-file record: "+f.Path);
   if(f.Alternatives.Count!=0&&f.RomfsOffset>=0)throw new InvalidDataException("RomFS inputs must match their exact packaged layout: "+f.Path);
  }
  if(m.PackageSha256.Any(x=>!IsHash(x))||m.CanonicalPackageSha256!=""&&!IsHash(m.CanonicalPackageSha256))throw new InvalidDataException("Invalid package hash in the required-files list.");
  return m;
 }
 public static bool IsHash(string s)=>s.Length==64&&s.All(Uri.IsHexDigit);
 public static bool SafeRelative(string s)=>s.Length>0&&s.Length<512&&!s.Contains('\\')&&!s.Contains(':')&&!s.StartsWith('/')&&s.Split('/').All(x=>x.Length>0&&x!="."&&x!="..")&&s.All(c=>c>=32&&c<127);
 public static string Resolve(string root,string relative) {
  if(!SafeRelative(relative))throw new InvalidDataException("Unsafe file path in the required-files list.");
  string result=System.IO.Path.GetFullPath(System.IO.Path.Combine(root,relative.Replace('/',System.IO.Path.DirectorySeparatorChar)));
  if(!result.StartsWith(System.IO.Path.GetFullPath(root).TrimEnd('\\')+"\\",StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("File path escapes Pending.");
  return result;
 }
}
record ProgressState(string Stage,string Detail,double Fraction);
