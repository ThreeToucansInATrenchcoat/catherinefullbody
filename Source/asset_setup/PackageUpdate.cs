using System.IO.Compression;
using System.Text;
namespace Catherine.AssetSetup;

// .NET 10 updates only the changed suffix. Keep the old directory/footer so a
// failed or interrupted shader append can restore the completed asset archive.
static class PackageUpdate {
 public static (long Offset,byte[] Tail,Dictionary<string,ushort> Methods) Directory(FileStream file){
  using var r=new BinaryReader(file,Encoding.UTF8,true);
  if(file.Length<22)throw new InvalidDataException("Truncated asset archive.");
  file.Position=file.Length-22;
  if(r.ReadUInt32()!=0x06054b50)throw new InvalidDataException("Unexpected asset archive footer.");
  file.Position=file.Length-6;long offset=r.ReadUInt32();
  if(offset==uint.MaxValue){
   file.Position=file.Length-42;if(r.ReadUInt32()!=0x07064b50)throw new InvalidDataException("Missing ZIP64 locator.");
   r.ReadUInt32();long record=checked((long)r.ReadUInt64());file.Position=record;
   if(r.ReadUInt32()!=0x06064b50)throw new InvalidDataException("Missing ZIP64 directory.");
   file.Position=record+48;offset=checked((long)r.ReadUInt64());
  }
  long length=file.Length-offset;if(offset<0||length<22||length>64*1024*1024)throw new InvalidDataException("Invalid asset archive directory.");
  file.Position=offset;byte[] tail=r.ReadBytes((int)length);var methods=new Dictionary<string,ushort>(StringComparer.Ordinal);
  long backupOffset=offset;using var directory=new BinaryReader(new MemoryStream(tail));
  while(directory.ReadUInt32()==0x02014b50){
   long start=directory.BaseStream.Position-4;directory.BaseStream.Position=start+10;ushort method=directory.ReadUInt16();
   directory.BaseStream.Position=start+28;int name=directory.ReadUInt16(),extra=directory.ReadUInt16(),comment=directory.ReadUInt16();
   directory.BaseStream.Position=start+20;uint compressed=directory.ReadUInt32(),uncompressed=directory.ReadUInt32();
   directory.BaseStream.Position=start+42;long local=directory.ReadUInt32();
   directory.BaseStream.Position=start+46;string path=Encoding.UTF8.GetString(directory.ReadBytes(name));methods.Add(path,method);
   long extraStart=directory.BaseStream.Position,extraEnd=extraStart+extra;
   if(local==uint.MaxValue){
    while(directory.BaseStream.Position+4<=extraEnd){int kind=directory.ReadUInt16(),size=directory.ReadUInt16();long end=directory.BaseStream.Position+size;if(end>extraEnd)throw new InvalidDataException("Invalid ZIP64 entry.");if(kind==1){if(uncompressed==uint.MaxValue)directory.ReadUInt64();if(compressed==uint.MaxValue)directory.ReadUInt64();local=checked((long)directory.ReadUInt64());break;}directory.BaseStream.Position=end;}
    if(local==uint.MaxValue)throw new InvalidDataException("Missing ZIP64 entry offset.");
   }
   if(path.StartsWith("spirv/",StringComparison.Ordinal)||path=="catherine-pak.manifest")backupOffset=Math.Min(backupOffset,local);
   directory.BaseStream.Position=extraEnd+comment;
  }
  if(backupOffset<0||file.Length-backupOffset>64*1024*1024)throw new InvalidDataException("Invalid shader archive suffix.");
  file.Position=backupOffset;return(backupOffset,r.ReadBytes(checked((int)(file.Length-backupOffset))),methods);
 }
 public static void Recover(string pack){
  string recovery=pack+".shader-update";if(!File.Exists(recovery))return;
  using(var input=File.OpenRead(recovery))using(var r=new BinaryReader(input)){
   if(input.Length<38||input.Length>64*1024*1024+16||r.ReadUInt64()!=0x3155504b504143UL)throw new InvalidDataException("Invalid shader recovery file.");
   long offset=r.ReadInt64();byte[] tail=r.ReadBytes(checked((int)(input.Length-16)));
   using var file=new FileStream(pack,FileMode.Open,FileAccess.ReadWrite,FileShare.None);
   if(offset<0||offset>file.Length)throw new InvalidDataException("Invalid shader recovery offset.");
   file.Position=offset;file.Write(tail);file.SetLength(offset+tail.Length);file.Flush(true);
  }
  File.Delete(recovery);
 }
 public static void Append(string pack,Action<ZipArchive,Dictionary<string,ushort>> append){
  string recovery=pack+".shader-update",temp=recovery+".tmp";
  try{
   using(var file=new FileStream(pack,FileMode.Open,FileAccess.ReadWrite,FileShare.None)){
    var directory=Directory(file);
    using(var backup=new FileStream(temp,FileMode.Create,FileAccess.Write,FileShare.None)){
     using var w=new BinaryWriter(backup,Encoding.UTF8,true);w.Write(0x3155504b504143UL);w.Write(directory.Offset);w.Write(directory.Tail);w.Flush();backup.Flush(true);
    }
    File.Move(temp,recovery,true);file.Position=0;
    using(var zip=new ZipArchive(file,ZipArchiveMode.Update,true,Encoding.UTF8))append(zip,directory.Methods);
    file.Flush(true);
   }
   File.Delete(recovery);
  }catch{Recover(pack);throw;}
 }
}
