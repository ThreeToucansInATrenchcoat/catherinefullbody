using System.IO.Compression;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
namespace Catherine.AssetSetup;

sealed class Installer {
 readonly string root,pending,pack;readonly AssetManifest manifest;readonly Action<ProgressState> progress;readonly CancellationToken cancel;
 readonly Dictionary<string,InputFile> missing;readonly bool headless;readonly Func<bool,string?> select;readonly Action<string> warn;
 readonly Dictionary<string,string> locations=new(StringComparer.OrdinalIgnoreCase);
 readonly HashSet<string> sourceFolders=new(StringComparer.OrdinalIgnoreCase);
 public Installer(string root,string pack,bool headless,Action<ProgressState> progress,Func<bool,string?> select,Action<string> warn,CancellationToken cancel){this.root=Path.GetFullPath(root);this.pack=Path.GetFullPath(pack);pending=Path.Combine(root,"Pending");this.headless=headless;this.progress=progress;this.select=select;this.warn=warn;this.cancel=cancel;manifest=AssetManifest.Load(Path.Combine(root,"assets-required.json"));missing=manifest.Files.ToDictionary(x=>x.Path,StringComparer.OrdinalIgnoreCase);}
 void Report(string stage,string detail,double fraction){cancel.ThrowIfCancellationRequested();progress(new(stage,detail,Math.Clamp(fraction,0,1)));}
 static bool Matches(string path,InputFile f){using var s=File.OpenRead(path);return f.AcceptsSize(s.Length)&&f.Accepts(s.Length,Convert.ToHexString(SHA256.HashData(s)));}
 IEnumerable<string> Walk(string folder){
  var stack=new Stack<string>();stack.Push(folder);while(stack.Count>0){cancel.ThrowIfCancellationRequested();string dir=stack.Pop();string[] paths;try{paths=Directory.GetFileSystemEntries(dir);}catch(UnauthorizedAccessException){continue;}foreach(string p in paths){FileAttributes a;try{a=File.GetAttributes(p);}catch(IOException){continue;}if((a&FileAttributes.ReparsePoint)!=0)continue;if((a&FileAttributes.Directory)!=0)stack.Push(p);else yield return p;}}
 }
 public void Scan(string folder){
  if(!Directory.Exists(folder))return;folder=Path.GetFullPath(folder);sourceFolders.Add(folder);File.WriteAllText(Path.Combine(root,"assets-sources.json"),JsonSerializer.Serialize(sourceFolders.Order(StringComparer.OrdinalIgnoreCase)));var byName=missing.Values.GroupBy(f=>Path.GetFileName(f.Path),StringComparer.OrdinalIgnoreCase).ToDictionary(g=>g.Key,g=>g.ToList(),StringComparer.OrdinalIgnoreCase);
  foreach(string path in Walk(folder)){
   if(!byName.TryGetValue(Path.GetFileName(path),out var candidates))continue;long size=new FileInfo(path).Length;
   foreach(var f in candidates){if(!missing.ContainsKey(f.Path)||!f.AcceptsSize(size))continue;Report("Checking game files",f.Path,(manifest.Files.Count-missing.Count)/(double)manifest.Files.Count);
    if(!Matches(path,f))continue;locations[f.Path]=Path.GetFullPath(path);missing.Remove(f.Path);
   }
  }
 }
 public void Run(IEnumerable<string> sources,bool scanOnly=false){
  PackageUpdate.Recover(pack);
  if(!File.Exists(pack)){
   string saved=Path.Combine(root,"assets-sources.json");string[] previous=Array.Empty<string>();if(File.Exists(saved)){try{previous=JsonSerializer.Deserialize<string[]>(File.ReadAllText(saved))??previous;}catch(JsonException){}}
   Scan(pending);Scan(Path.Combine(root,"Assets"));foreach(string source in previous.Concat(sources).Distinct(StringComparer.OrdinalIgnoreCase))Scan(source);
   while(missing.Count>0){
    bool update=missing.Values.All(f=>f.Kind=="update");string message=update?"Update files are required. Please select the folder containing your extracted update files.":"Game files are required. Please select the folder containing your extracted game files.";
    File.WriteAllText(Path.Combine(root,"assets-missing.txt"),message+Environment.NewLine+$"Missing {missing.Count} of {manifest.Files.Count} required files."+Environment.NewLine+string.Join(Environment.NewLine,missing.Keys.Order(StringComparer.Ordinal)));
    if(headless||scanOnly)throw new FileNotFoundException(message+" See assets-missing.txt.");warn(message);string? folder=select(update);if(folder==null)throw new OperationCanceledException();Scan(folder);
   }
   string report=Path.Combine(root,"assets-missing.txt");if(File.Exists(report))File.Delete(report);if(scanOnly)return;
   Build();
  }
  if(!scanOnly)CompleteShaders();
  try{Verify();}catch(IOException e){warn("assets.pak could not be fully verified and may be corrupt. The game may not function correctly. Catherine will try to launch anyway.\n\n"+e.Message);}catch(UnauthorizedAccessException e){warn("assets.pak could not be verified. Catherine will try to launch anyway.\n\n"+e.Message);}
 }
 void Verify(){
  if(PackageVerification.TryReuse(root,pack,manifest))return;
  Report("Verifying assets.pak","Checking the package SHA-256",0);using var f=File.OpenRead(pack);var identity=PackageVerification.Begin(root,pack,f);using var sha=IncrementalHash.CreateHash(HashAlgorithmName.SHA256);byte[] buf=new byte[4*1024*1024];int n;while((n=f.Read(buf))>0){sha.AppendData(buf,0,n);Report("Verifying assets.pak",$"{f.Position/(1024*1024):N0} / {f.Length/(1024*1024):N0} MB",f.Position/(double)f.Length);}string hash=Convert.ToHexString(sha.GetHashAndReset()).ToLowerInvariant();
  File.AppendAllText(Path.Combine(root,"assets-setup.log"),$"{DateTime.UtcNow:O} package SHA256 {hash}\n");
  if(!manifest.PackageSha256.Contains(hash,StringComparer.OrdinalIgnoreCase)&&!hash.Equals(manifest.CanonicalPackageSha256,StringComparison.OrdinalIgnoreCase))warn("assets.pak may be corrupt or from an unsupported version. The game may not function correctly. Catherine will try to launch anyway.\n\nSHA-256: "+hash);
  PackageVerification.Remember(root,pack,manifest,f,identity,hash);
 }
 void Build(){
  Report("Preparing game modules","Decompressing and relocating the supplied executables",0);var modules=NsoImage.Prepare(manifest,locations);
  string temp=pack+".building";Directory.CreateDirectory(Path.GetDirectoryName(pack)!);if(File.Exists(temp))File.Delete(temp);
  try{
   using(var output=new FileStream(temp,FileMode.CreateNew,FileAccess.ReadWrite,FileShare.None,1024*1024))using(var zip=new ZipArchive(output,ZipArchiveMode.Create,true,Encoding.UTF8)){
    var records=new List<(string name,long size,uint crc,ushort method)>();
    void Add(string name,byte[] bytes,bool store=false){cancel.ThrowIfCancellationRequested();var e=zip.CreateEntry(name,store?CompressionLevel.NoCompression:CompressionLevel.Fastest);e.LastWriteTime=new DateTimeOffset(1980,1,1,0,0,0,TimeSpan.Zero);using(var s=e.Open())s.Write(bytes);records.Add((name,bytes.Length,Crc.Compute(bytes),(ushort)(store?0:8)));}
    foreach(var spec in manifest.Modules)Add(spec.Name+".image",modules[spec.Name]);
    var ranges=manifest.Files.Where(f=>f.RomfsOffset>=0).Select(f=>(offset:f.RomfsOffset,length:f.Size,path:locations[f.Path],data:(byte[]?)null)).Concat(manifest.RomfsMetadata.Select(m=>(m.Offset,(long)Convert.FromBase64String(m.Data).Length,"",(byte[]?)Convert.FromBase64String(m.Data)))).OrderBy(r=>r.Item1).ToList();
    byte[] block=new byte[262144],copy=new byte[262144];int used=0,index=0;long position=0;
    void Feed(ReadOnlySpan<byte> data){while(!data.IsEmpty){int n=Math.Min(block.Length-used,data.Length);data[..n].CopyTo(block.AsSpan(used));used+=n;position+=n;data=data[n..];if(used==block.Length){Add($"romfs/blocks/{index++:D6}.bin",block,position>=2718562544&&position<14940185280);used=0;Report("Building assets.pak",$"{position/(1024*1024):N0} / {manifest.RomfsSize/(1024*1024):N0} MB",position/(double)manifest.RomfsSize);}}}
    void Zeros(long count){Array.Clear(copy);while(count>0){int n=(int)Math.Min(count,copy.Length);Feed(copy.AsSpan(0,n));count-=n;}}
    foreach(var r in ranges){if(r.Item1<position&&r.Item2>0)throw new InvalidDataException("Overlapping RomFS layout.");if(r.Item1>position)Zeros(r.Item1-position);if(r.Item4!=null)Feed(r.Item4);else{using var input=File.OpenRead(r.Item3);int n;while((n=input.Read(copy))>0)Feed(copy.AsSpan(0,n));}}
    Zeros(manifest.RomfsSize-position);if(used>0)Add($"romfs/blocks/{index:D6}.bin",block[..used]);
    using var directory=new MemoryStream();using(var writer=new BinaryWriter(directory,Encoding.UTF8,true))foreach(var r in records.OrderBy(r=>r.name,StringComparer.Ordinal)){byte[] name=Encoding.ASCII.GetBytes(r.name);writer.Write((ushort)name.Length);writer.Write(name);writer.Write((ulong)r.size);writer.Write(r.crc);writer.Write(r.method);}
    string text=$"CATHERINE_PAK 1\nROMFS_SIZE {manifest.RomfsSize}\nBLOCK_SIZE 262144\nENTRY_COUNT {records.Count}\nDIRECTORY_CRC32 {Crc.Compute(directory.ToArray()):x8}\n";Add("catherine-pak.manifest",Encoding.ASCII.GetBytes(text));
   }
   File.Move(temp,pack,false);
  }catch{if(File.Exists(temp))File.Delete(temp);throw;}
 }
 void CompleteShaders(){
  using(var zip=ZipFile.OpenRead(pack)){
   if(zip.GetEntry("spirv/results.tsv") is {Length:>0}&&manifest.Shaders.All(s=>zip.GetEntry("spirv/"+s.Hash+".spv") is {Length:>0}))return;
  }
  Report("Generating Vulkan shaders","Reading shader sources from assets.pak",0);
  string shaderDir=Path.Combine(root,"assets-setup-work","GeneratedShaders");Directory.CreateDirectory(shaderDir);
  Shaders.Generate(manifest,pack,shaderDir,progress,cancel);
  Report("Saving Vulkan shaders","Adding completed shaders to assets.pak",0);
  // Translation/cancellation happens before touching the archive. The suffix
  // update preserves all module and RomFS bytes; recovery restores its footer.
  PackageUpdate.Append(pack,(zip,methods)=>{
   foreach(var entry in zip.Entries.Where(e=>e.FullName.StartsWith("spirv/",StringComparison.Ordinal)||e.FullName=="catherine-pak.manifest").ToArray())entry.Delete();
   var records=zip.Entries.Select(e=>(name:e.FullName,size:e.Length,crc:e.Crc32,method:methods[e.FullName])).ToList();
   void Add(string name,byte[] bytes){var entry=zip.CreateEntry(name,CompressionLevel.Fastest);entry.LastWriteTime=new DateTimeOffset(1980,1,1,0,0,0,TimeSpan.Zero);using(var stream=entry.Open())stream.Write(bytes);records.Add((name,bytes.Length,Crc.Compute(bytes),8));}
   foreach(var shader in manifest.Shaders.OrderBy(s=>s.Hash,StringComparer.Ordinal))Add("spirv/"+shader.Hash+".spv",File.ReadAllBytes(Path.Combine(shaderDir,shader.Hash+".spv")));
   Add("spirv/results.tsv",File.ReadAllBytes(Path.Combine(shaderDir,"results.tsv")));
   using var directory=new MemoryStream();using(var writer=new BinaryWriter(directory,Encoding.UTF8,true))foreach(var record in records.OrderBy(r=>r.name,StringComparer.Ordinal)){byte[] name=Encoding.ASCII.GetBytes(record.name);writer.Write((ushort)name.Length);writer.Write(name);writer.Write((ulong)record.size);writer.Write(record.crc);writer.Write(record.method);}
   Add("catherine-pak.manifest",Encoding.ASCII.GetBytes($"CATHERINE_PAK 1\nROMFS_SIZE {manifest.RomfsSize}\nBLOCK_SIZE 262144\nENTRY_COUNT {records.Count}\nDIRECTORY_CRC32 {Crc.Compute(directory.ToArray()):x8}\n"));
  });
  Report("Saving Vulkan shaders","Vulkan shaders are ready",1);
 }
}
static class Crc {
 static readonly uint[] Table=Enumerable.Range(0,256).Select(n=>{uint c=(uint)n;for(int i=0;i<8;i++)c=c%2!=0?0xedb88320^(c>>1):c>>1;return c;}).ToArray();
 public static uint Compute(ReadOnlySpan<byte> bytes){uint c=uint.MaxValue;foreach(byte b in bytes)c=Table[(c^b)&255]^(c>>8);return ~c;}
}
