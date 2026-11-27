using System.IO.Compression;
using System.Security.Cryptography;
namespace Catherine.AssetSetup;

sealed class PackageShaderSource:IDisposable {
 const int BlockSize=262144;
 readonly ZipArchive? zip;
 readonly string? folder;
 readonly AssetManifest manifest;
 readonly object gate=new();
 readonly Dictionary<string,byte[]> modules=new(StringComparer.Ordinal);
 readonly Dictionary<string,InputFile> files;
 public PackageShaderSource(string pack,AssetManifest manifest){if(Directory.Exists(pack))folder=pack;else zip=ZipFile.OpenRead(pack);this.manifest=manifest;files=manifest.Files.ToDictionary(f=>f.Path,StringComparer.Ordinal);}
 public byte[] Read(ShaderSource shader){
  lock(gate){
   if(shader.Offset<0||shader.Length<=0||shader.Length>16*1024*1024)throw new InvalidDataException("Invalid shader range.");
   if(shader.Kind=="module"){
    if(!modules.TryGetValue(shader.Path,out var image)){
     var spec=manifest.Modules.Single(m=>m.Name==shader.Path);
     if(folder!=null)image=File.ReadAllBytes(AssetManifest.Resolve(folder,spec.Name+".image"));
     else{var entry=zip!.GetEntry(spec.Name+".image")??throw new InvalidDataException("Missing shader module.");
      if(entry.Length!=spec.Size)throw new InvalidDataException("Invalid shader module size.");
      image=new byte[spec.Size];using(var stream=entry.Open())stream.ReadExactly(image);}
     if(image.Length!=spec.Size)throw new InvalidDataException("Invalid shader module size.");
     if(!Convert.ToHexString(SHA256.HashData(image)).Equals(spec.ImageSha256,StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("Shader module hash mismatch.");
     modules.Add(shader.Path,image);
    }
    return image.AsSpan(shader.Offset,shader.Length).ToArray();
   }
   if(shader.Kind!="raw"||!files.TryGetValue(shader.Path,out var source)||source.RomfsOffset<0||(long)shader.Offset+shader.Length>source.Size)throw new InvalidDataException("Invalid RomFS shader source.");
   byte[] code=new byte[shader.Length];
   if(folder!=null){using var stream=File.OpenRead(AssetManifest.Resolve(folder,source.Path));stream.Position=shader.Offset;stream.ReadExactly(code);return code;}
   long offset=checked(source.RomfsOffset+shader.Offset);int copied=0;
   while(copied<code.Length){
    long block=offset/BlockSize;int within=(int)(offset%BlockSize),count=Math.Min(code.Length-copied,BlockSize-within);
    var entry=zip!.GetEntry($"romfs/blocks/{block:D6}.bin")??throw new InvalidDataException("Missing shader RomFS block.");
    using var stream=entry.Open();Skip(stream,within);stream.ReadExactly(code.AsSpan(copied,count));offset+=count;copied+=count;
   }
   return code;
  }
 }
 static void Skip(Stream stream,int count){Span<byte> buffer=stackalloc byte[4096];while(count>0){int n=Math.Min(count,buffer.Length);stream.ReadExactly(buffer[..n]);count-=n;}}
 public void Dispose()=>zip?.Dispose();
}
