using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json;
using Microsoft.Win32.SafeHandles;
namespace Catherine.AssetSetup;

static class PackageVerification {
 [StructLayout(LayoutKind.Sequential)]struct FileInfo {public uint Attributes,CreationLow,CreationHigh,AccessLow,AccessHigh,WriteLow,WriteHigh,Volume,SizeHigh,SizeLow,Links,IndexHigh,IndexLow;}
 [StructLayout(LayoutKind.Sequential)]struct BasicInfo {public long Creation,Access,Write,Change;public uint Attributes;}
 [DllImport("kernel32.dll",SetLastError=true)]static extern bool GetFileInformationByHandle(SafeFileHandle file,out FileInfo info);
 [DllImport("kernel32.dll",SetLastError=true)]static extern bool GetFileInformationByHandleEx(SafeFileHandle file,int kind,out BasicInfo info,uint size);
 internal record Identity(string Path,uint Volume,ulong FileId,long Length,long Write,long Change,string Manifest);
 record Cache(Identity File,string Sha256);
 static Identity Identify(string path,string manifest,FileStream file){
  if(!GetFileInformationByHandle(file.SafeFileHandle,out var id)||!GetFileInformationByHandleEx(file.SafeFileHandle,0,out var basic,(uint)Marshal.SizeOf<BasicInfo>()))throw new IOException("Cannot identify assets.pak for verification.");
  return new(System.IO.Path.GetFullPath(path),id.Volume,((ulong)id.IndexHigh<<32)|id.IndexLow,file.Length,basic.Write,basic.Change,Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(manifest))));
 }
 static string CachePath(string root)=>System.IO.Path.Combine(root,"assets-verification.json");
 static bool Accepted(AssetManifest manifest,string hash)=>manifest.PackageSha256.Contains(hash,StringComparer.OrdinalIgnoreCase)||hash.Equals(manifest.CanonicalPackageSha256,StringComparison.OrdinalIgnoreCase);
 public static bool TryReuse(string root,string pack,AssetManifest manifest){
  if(!File.Exists(pack)||!File.Exists(CachePath(root)))return false;
  try{
   if(new System.IO.FileInfo(CachePath(root)).Length>65536)return false;
   var cache=JsonSerializer.Deserialize<Cache>(File.ReadAllText(CachePath(root)));if(cache?.File==null||cache.Sha256==null||!AssetManifest.IsHash(cache.Sha256)||!Accepted(manifest,cache.Sha256))return false;
   using var file=File.OpenRead(pack);if(Identify(pack,System.IO.Path.Combine(root,"assets-required.json"),file)!=cache.File)return false;
   File.AppendAllText(System.IO.Path.Combine(root,"assets-setup.log"),$"{DateTime.UtcNow:O} cached package verification: unchanged file identity; SHA256 {cache.Sha256}\n");return true;
  }catch(Exception e)when(e is IOException or UnauthorizedAccessException or JsonException){return false;}
 }
 public static Identity Begin(string root,string pack,FileStream file)=>Identify(pack,System.IO.Path.Combine(root,"assets-required.json"),file);
 public static void Remember(string root,string pack,AssetManifest manifest,FileStream file,Identity before,string hash){
  if(!Accepted(manifest,hash)||before!=Identify(pack,System.IO.Path.Combine(root,"assets-required.json"),file))return;
  try{string path=CachePath(root),temp=path+"."+Environment.ProcessId+".tmp";File.WriteAllText(temp,JsonSerializer.Serialize(new Cache(before,hash)));File.Move(temp,path,true);}catch(Exception e)when(e is IOException or UnauthorizedAccessException){}
 }
}
