using System.Diagnostics;
using System.Reflection;
using System.Text;
using System.Text.RegularExpressions;
namespace Catherine.AssetSetup;
static class SpirvSanitizer {
 record Ins(string Text,string Result,string Op,string[] Args);
 static readonly Regex Token=new("\"(?:[^\"\\\\]|\\\\.)*\"|[^\\s]+",RegexOptions.Compiled);
 static string Tools="";
 public static void PrepareTools(){
#if CATHERINE_LINUX_SETUP
  foreach(string name in new[]{"spirv-dis","spirv-as","spirv-val"}){
   if(!((Environment.GetEnvironmentVariable("PATH")??"").Split(Path.PathSeparator).Any(dir=>dir.Length>0&&File.Exists(Path.Combine(dir,name)))))
    throw new FileNotFoundException("Install SPIRV-Tools and make "+name+" available on PATH.");
  }
#else
  Tools=Path.Combine(Environment.GetEnvironmentVariable("RECOMPLAUNCHER_DATA") is {Length:>0} data?Path.Combine(data,"cache"):Path.GetTempPath(),"CatherineAssetTools",Assembly.GetExecutingAssembly().ManifestModule.ModuleVersionId.ToString());Directory.CreateDirectory(Tools);
  foreach(string name in new[]{"spirv-dis.exe","spirv-as.exe","spirv-val.exe"}){
   string path=Path.Combine(Tools,name);using var s=Assembly.GetExecutingAssembly().GetManifestResourceStream(name)??throw new FileNotFoundException(name);using var bytes=new MemoryStream();s.CopyTo(bytes);var expected=bytes.ToArray();
   if(File.Exists(path)&&File.ReadAllBytes(path).AsSpan().SequenceEqual(expected))continue;
   string temp=path+"."+Environment.ProcessId+".tmp";File.WriteAllBytes(temp,expected);
   try{File.Move(temp,path,false);}catch(IOException){File.Delete(temp);if(!File.Exists(path)||!File.ReadAllBytes(path).AsSpan().SequenceEqual(expected))throw;}
  }
#endif
 }
 static byte[] Run(string name,string args,byte[] input){
#if CATHERINE_LINUX_SETUP
  name=Path.GetFileNameWithoutExtension(name);
#endif
  using var p=new Process{StartInfo=new(Path.Combine(Tools,name),args){UseShellExecute=false,CreateNoWindow=true,RedirectStandardInput=true,RedirectStandardOutput=true,RedirectStandardError=true}};p.Start();var output=new MemoryStream();var o=p.StandardOutput.BaseStream.CopyToAsync(output);var e=p.StandardError.ReadToEndAsync();p.StandardInput.BaseStream.Write(input);p.StandardInput.Close();p.WaitForExit();o.GetAwaiter().GetResult();string error=e.GetAwaiter().GetResult();if(p.ExitCode!=0)throw new InvalidDataException(name+": "+error);return output.ToArray();
 }
 public static byte[] Sanitize(byte[] binary){
  string asm=Encoding.UTF8.GetString(Run("spirv-dis.exe","--raw-id -",binary));var list=new List<Ins>();
  foreach(var line in asm.Split('\n')){var t=Token.Matches(line).Select(m=>m.Value).ToArray();if(t.Length==0||t[0].StartsWith(';'))continue;bool result=t.Length>2&&t[1]=="=";list.Add(new(line,result?t[0]:"",t[result?2:0],t.Skip(result?3:1).ToArray()));}
  bool interlock=list.Any(i=>i.Op is "OpBeginInvocationInterlockEXT" or "OpEndInvocationInterlockEXT"||((i.Op is "OpExecutionMode" or "OpExecutionModeId")&&i.Args[1].Contains("Interlock")));
  var shortened=new Dictionary<string,int>();
  if(list.Where(i=>i.Op=="OpEntryPoint").All(i=>i.Args[0]=="Vertex")){
   var defs=list.Where(i=>i.Result!="").ToDictionary(i=>i.Result);var refs=new Dictionary<string,List<Ins>>();var constants=new Dictionary<string,int>();var candidates=new Dictionary<string,HashSet<int>>();
   foreach(var i in list){foreach(var a in i.Args.Distinct().Where(a=>a.StartsWith('%'))){if(!refs.ContainsKey(a))refs[a]=new();refs[a].Add(i);}if(i.Op=="OpConstant"&&defs[i.Args[0]].Op=="OpTypeInt")constants[i.Result]=int.Parse(i.Args[1]);if(i.Op=="OpMemberDecorate"&&i.Args.Length==4&&i.Args[2]=="BuiltIn"&&(i.Args[3] is "ClipDistance" or "CullDistance")){if(!candidates.ContainsKey(i.Args[0]))candidates[i.Args[0]]=new();candidates[i.Args[0]].Add(int.Parse(i.Args[1]));}}
   foreach(var (block,members) in candidates){if(defs[block].Op!="OpTypeStruct")continue;bool valid=true,output=false;var used=new HashSet<int>();
    foreach(var r in refs.GetValueOrDefault(block,new())){if(r.Op is "OpName" or "OpDecorate" or "OpMemberName" or "OpMemberDecorate"){if(r.Args[0]!=block)valid=false;continue;}if(r.Op!="OpTypePointer"||!r.Args.SequenceEqual(new[]{"Output",block})){valid=false;break;}
     foreach(var d in refs.GetValueOrDefault(r.Result,new())){if(d.Op=="OpName"&&d.Args[0]==r.Result)continue;if(d.Op!="OpVariable"||!d.Args.SequenceEqual(new[]{r.Result,"Output"})){valid=false;break;}output=true;
      foreach(var u in refs.GetValueOrDefault(d.Result,new())){if(u.Op=="OpEntryPoint"&&u.Args.Skip(3).Contains(d.Result))continue;if((u.Op is "OpName" or "OpDecorate")&&u.Args[0]==d.Result)continue;if(!(u.Op is "OpAccessChain" or "OpInBoundsAccessChain")||u.Args.Length<3||u.Args[1]!=d.Result||!constants.TryGetValue(u.Args[2],out int member)){valid=false;break;}used.Add(member);}
     }
    }
    if(!valid||!output)continue;int total=defs[block].Args.Length,keep=total;while(keep>1&&members.Contains(keep-1)&&!used.Contains(keep-1))keep--;if(keep<total)shortened[block]=keep;
   }
  }
  var text=new StringBuilder();bool changed=false;
  foreach(var i in list){if(!interlock&&(i.Op=="OpCapability"&&i.Args[0].Contains("InterlockEXT")||i.Op=="OpExtension"&&i.Args[0]=="\"SPV_EXT_fragment_shader_interlock\"")){changed=true;continue;}if(i.Op=="OpTypeStruct"&&shortened.TryGetValue(i.Result,out int n)){text.AppendLine(i.Result+" = OpTypeStruct "+string.Join(' ',i.Args.Take(n)));changed=true;continue;}if((i.Op is "OpMemberName" or "OpMemberDecorate")&&shortened.TryGetValue(i.Args[0],out n)&&int.Parse(i.Args[1])>=n){changed=true;continue;}text.AppendLine(i.Text);}
  if(changed){uint version=BitConverter.ToUInt32(binary,4);binary=Run("spirv-as.exe",$"--preserve-numeric-ids --target-env spv{version>>16&255}.{version>>8&255} - -o -",Encoding.UTF8.GetBytes(text.ToString()));}
  Run("spirv-val.exe","--target-env vulkan1.2 -",binary);return binary;
 }
}
