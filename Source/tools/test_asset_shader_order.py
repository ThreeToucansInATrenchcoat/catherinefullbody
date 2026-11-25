"""Exercise package-first setup, offline shader repair and interrupted suffix recovery."""
from pathlib import Path
import base64,copy,hashlib,json,shutil,subprocess,zipfile
R=Path(__file__).resolve().parents[2];B=R/'Build';D=B/'asset-setup/shader-order-test';D.mkdir(parents=True,exist_ok=True)

def main():
 manifest=json.loads((R/'Source/resources/assets-required.json').read_text())
 raw=next(s for s in manifest['shaders']if s['kind']=='raw');module=next(s for s in manifest['shaders']if s['kind']=='module')
 mini=copy.deepcopy(manifest);mini['shaders']=[raw,module];mini['files']=[f for f in mini['files']if f['path'].startswith('exefs/')or f['path']==raw['path']]
 for f in mini['files']:
  if f['path']==raw['path']:f['romfsOffset']=262144-raw['offset']-32;mini['romfsSize']=f['romfsOffset']+f['size']
 mini.update(romfsMetadata=[],packageSha256=[],canonicalPackageSha256='')
 fixture=D/'fixture'
 if fixture.exists():shutil.rmtree(fixture)
 fixture.mkdir();(fixture/'assets-required.json').write_text(json.dumps(mini))
 for f in mini['files']:
  target=fixture/'Inputs'/f['path'];target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(Path('C:/catherine/go')/f['path'],target)
 source=R/'Source/asset_setup'
 (D/'Test.csproj').write_text(f'''<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows</TargetFramework><UseWindowsForms>true</UseWindowsForms><UseAppHost>false</UseAppHost><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup><ItemGroup><Compile Include="{source.as_posix()}/*.cs" Exclude="{source.as_posix()}/EntryPoint.cs"/><Reference Include="{source.as_posix()}/vendor/lib/*.dll"/><EmbeddedResource Include="{source.as_posix()}/vendor/tools/*.exe" LogicalName="%(Filename)%(Extension)"/><EmbeddedResource Include="{source.as_posix()}/Compatibility/main-93a79c77.xor.zlib" LogicalName="Catherine.AssetSetup.Compatibility.Main93a79c77"/></ItemGroup></Project>''')
 (D/'Program.cs').write_text(r'''
using Catherine.AssetSetup;
using System.IO.Compression;
using System.Security.Cryptography;
using System.Text.Json;
string root=args[0],pack=Path.Combine(root,"assets.pak");
string Hash()=>Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(pack)));
if(args.Contains("--crash")){PackageUpdate.Append(pack,(zip,methods)=>Environment.Exit(99));return;}
var stages=new List<string>();bool cancelled=false;
try{
 new Installer(root,pack,true,p=>{
  stages.Add(p.Stage);
  if(p.Stage=="Generating Vulkan shaders"){
   if(!File.Exists(pack))throw new Exception("Shaders began before the asset package existed");
   using var z=ZipFile.OpenRead(pack);
   if(z.GetEntry("main.image")==null||z.GetEntry("romfs/blocks/000000.bin")==null||z.GetEntry("spirv/results.tsv")!=null)throw new Exception("Invalid completed base package");
   Directory.Move(Path.Combine(root,"Inputs"),Path.Combine(root,"Inputs-offline"));
   throw new OperationCanceledException();
  }
 },_=>null,_=>{},CancellationToken.None).Run(new[]{Path.Combine(root,"Inputs")});
}catch(OperationCanceledException){cancelled=true;}
if(!cancelled||!stages.Contains("Building assets.pak"))throw new Exception("Wrong stage order");
byte[] baseData;using(var z=ZipFile.OpenRead(pack)){using var s=z.GetEntry("romfs/blocks/000000.bin")!.Open();using var m=new MemoryStream();s.CopyTo(m);baseData=m.ToArray();}
void Run()=>new Installer(root,pack,true,p=>{if(p.Stage=="Checking game files"||p.Stage=="Preparing game modules"||p.Stage=="Building assets.pak")throw new Exception("Rescanned or rebuilt assets during shader repair");},_=>null,_=>{},CancellationToken.None).Run(Array.Empty<string>());
Run();
var manifest=AssetManifest.Load(Path.Combine(root,"assets-required.json"));
using(var z=ZipFile.OpenRead(pack))using(var reference=ZipFile.OpenRead(args[1])){
 foreach(var shader in manifest.Shaders){string name="spirv/"+shader.Hash+".spv";using var a=z.GetEntry(name)!.Open();using var b=reference.GetEntry(name)!.Open();using var ma=new MemoryStream();using var mb=new MemoryStream();a.CopyTo(ma);b.CopyTo(mb);if(!ma.ToArray().SequenceEqual(mb.ToArray()))throw new Exception("Shader output changed: "+name);}
 using var s=z.GetEntry("romfs/blocks/000000.bin")!.Open();using var m=new MemoryStream();s.CopyTo(m);if(!m.ToArray().SequenceEqual(baseData))throw new Exception("Asset bytes changed");
}
string completed=Hash();
PackageUpdate.Append(pack,(zip,methods)=>zip.GetEntry("spirv/"+manifest.Shaders[0].Hash+".spv")!.Delete());Run();
// Repaired contents must have no duplicates and a valid package-directory CRC.
using(var file=File.OpenRead(pack)){var d=PackageUpdate.Directory(file);using var z=new ZipArchive(file,ZipArchiveMode.Read);if(z.Entries.Select(e=>e.FullName).Distinct().Count()!=z.Entries.Count)throw new Exception("Duplicate entries");using var data=new MemoryStream();using(var w=new BinaryWriter(data,System.Text.Encoding.UTF8,true))foreach(var e in z.Entries.Where(e=>e.FullName!="catherine-pak.manifest").OrderBy(e=>e.FullName,StringComparer.Ordinal)){var name=System.Text.Encoding.ASCII.GetBytes(e.FullName);w.Write((ushort)name.Length);w.Write(name);w.Write((ulong)e.Length);w.Write(e.Crc32);w.Write(d.Methods[e.FullName]);}using var r=new StreamReader(z.GetEntry("catherine-pak.manifest")!.Open());if(!r.ReadToEnd().Contains($"DIRECTORY_CRC32 {Crc.Compute(data.ToArray()):x8}"))throw new Exception("Wrong directory CRC");}
string repaired=Hash();try{PackageUpdate.Append(pack,(zip,methods)=>{zip.GetEntry("catherine-pak.manifest")!.Delete();throw new IOException("Simulated suffix failure");});}catch(IOException){}if(Hash()!=repaired)throw new Exception("Failed append did not roll back");
var child=new System.Diagnostics.ProcessStartInfo("dotnet"){UseShellExecute=false,CreateNoWindow=true};child.ArgumentList.Add(System.Reflection.Assembly.GetExecutingAssembly().Location);child.ArgumentList.Add(root);child.ArgumentList.Add("--crash");using(var process=System.Diagnostics.Process.Start(child)!){process.WaitForExit();if(process.ExitCode!=99)throw new Exception("Crash fixture failed");}
using(var damaged=new FileStream(pack,FileMode.Open,FileAccess.Write)){damaged.Position=damaged.Length-22;damaged.Write(new byte[22]);damaged.Write(new byte[70000]);}
PackageUpdate.Recover(pack);if(Hash()!=repaired)throw new Exception("Interrupted append did not recover");
string sparse=Path.Combine(root,"zip64.pak");
using(var file=new FileStream(sparse,FileMode.Create,FileAccess.ReadWrite)){
 if(!Sparse.Enable(file.SafeFileHandle,0x900c4,IntPtr.Zero,0,IntPtr.Zero,0,out _,IntPtr.Zero))throw new Exception("Cannot create sparse ZIP64 test");
 file.Position=(long)uint.MaxValue+4096;
 using var z=new ZipArchive(file,ZipArchiveMode.Create,true);
 using(var s=z.CreateEntry("main.image").Open())s.Write(new byte[]{1,2,3});
 using(var s=z.CreateEntry("catherine-pak.manifest").Open())s.Write(new byte[]{4,5,6});
}
using(var file=File.OpenRead(sparse)){if(PackageUpdate.Directory(file).Offset<=uint.MaxValue)throw new Exception("ZIP64 fixture was not ZIP64");}
PackageUpdate.Append(sparse,(z,methods)=>{z.GetEntry("catherine-pak.manifest")!.Delete();using var s=z.CreateEntry("spirv/results.tsv").Open();s.Write(new byte[]{7,8,9});});
using(var z=ZipFile.OpenRead(sparse)){using var s=z.GetEntry("main.image")!.Open();if(s.ReadByte()!=1||z.GetEntry("spirv/results.tsv")==null)throw new Exception("ZIP64 append failed");}
using(var file=File.OpenRead(sparse)){PackageUpdate.Directory(file);}
File.Delete(sparse);
Console.WriteLine(JsonSerializer.Serialize(new{passed=true,assets_before_shaders=true,cancel_preserves_assets=true,offline_resume=true,missing_shader_repaired=true,cross_block_shader=true,module_shader=true,outputs_unchanged=true,directory_crc=true,failed_append_rollback=true,interrupted_append_recovery=true,zip64_append=true}));
static class Sparse {
 [System.Runtime.InteropServices.DllImport("kernel32.dll",EntryPoint="DeviceIoControl",SetLastError=true)]
 internal static extern bool Enable(Microsoft.Win32.SafeHandles.SafeFileHandle file,uint code,IntPtr input,uint inputSize,IntPtr output,uint outputSize,out uint returned,IntPtr overlap);
}
''')
 project=D/'Test.csproj';project.write_text(project.read_text().replace('<Reference Include=',f'<Compile Include="{source.as_posix()}/Shaders/*.cs"/><Reference Include=',1))
 subprocess.run(['dotnet','build',str(D/'Test.csproj'),'-c','Release','--nologo','-v','quiet'],check=True)
 result=subprocess.run(['dotnet',str(D/'bin/Release/net10.0-windows/Test.dll'),str(fixture),str(B/'assets.pak')],capture_output=True,text=True,timeout=180,creationflags=subprocess.CREATE_NO_WINDOW)
 assert result.returncode==0,result.stdout+result.stderr
 report=json.loads(result.stdout.strip().splitlines()[-1]);(D/'result.json').write_text(json.dumps(report,indent=2));print(json.dumps(report))
if __name__=='__main__':main()
