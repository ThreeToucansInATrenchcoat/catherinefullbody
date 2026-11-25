using System.Security.Cryptography;
using System.Text.Json;
namespace Catherine.AssetSetup;
static class EntryPoint {
 [STAThread] public static int Main(string[] args){
  string errorRoot=AppContext.BaseDirectory;
  try{
   if(args.Length>0&&args[0]=="--translate"){
    SpirvSanitizer.PrepareTools();var(binary,resources,stage)=Shaders.Translate(File.ReadAllBytes(args[1]),File.ReadAllBytes(args[2]));File.WriteAllBytes(args[3]+".spv",binary);File.WriteAllText(args[3]+".resources",resources);return 0;
   }
   string Opt(string name,string fallback){int at=Array.IndexOf(args,name);return at>=0&&at+1<args.Length?args[at+1]:fallback;}
   string root=Path.GetFullPath(Opt("--root",AppContext.BaseDirectory)),pack=Path.GetFullPath(Opt("--pack",Path.Combine(root,"assets.pak")));errorRoot=root;bool hidden=args.Contains("--headless");var sources=new List<string>();for(int i=0;i<args.Length-1;i++)if(args[i]=="--scan")sources.Add(args[++i]);
   void Log(string message){File.AppendAllText(Path.Combine(root,"assets-setup.log"),DateTime.UtcNow.ToString("O")+" "+message+Environment.NewLine);}
   if(!File.Exists(Path.Combine(root,"assets-required.json"))){if(!File.Exists(pack))throw new FileNotFoundException("Missing assets-required.json beside Catherine.exe. Restore the required-files list before installing game assets.");string warning="Cannot verify assets.pak: assets-required.json is missing. Catherine will try to launch anyway.";Log(warning);if(!hidden)MessageBox.Show(warning,"Catherine asset verification",MessageBoxButtons.OK,MessageBoxIcon.Warning);return 0;}
   PackageUpdate.Recover(pack);
   if(File.Exists(pack)&&PackageVerification.TryReuse(root,pack,AssetManifest.Load(Path.Combine(root,"assets-required.json"))))return 0;
   if(hidden){long last=0;string lastStage="";new Installer(root,pack,true,p=>{long now=Environment.TickCount64;if(p.Stage!=lastStage||now-last>=100||p.Fraction>=1){File.WriteAllText(Path.Combine(root,"assets-progress.json"),JsonSerializer.Serialize(p));last=now;lastStage=p.Stage;}},_=>null,Log,CancellationToken.None).Run(sources,args.Contains("--scan-only"));return 0;}
   ApplicationConfiguration.Initialize();using var form=new SetupWindow(root,pack,sources,args.Contains("--scan-only"));Application.Run(form);return form.Result;
  }catch(OperationCanceledException){return 2;}catch(Exception e){File.AppendAllText(Path.Combine(errorRoot,"assets-errors.log"),DateTime.UtcNow+" "+e+Environment.NewLine);return 1;}
 }
}
sealed class SetupWindow:Form {
 public int Result=1;
 readonly Label stage=new(){AutoSize=true,Dock=DockStyle.Fill,Text="Game files are required.",Margin=new(0,0,0,8)};
 readonly Label detail=new(){Dock=DockStyle.Fill,AutoEllipsis=true,Text="Choose the folder containing your extracted game files.",Margin=new(0,0,0,12)};
 readonly ProgressBar bar=new(){Dock=DockStyle.Fill,Height=24,Maximum=1000,Margin=new(0)};
 readonly Button stop=new(){AutoSize=true,AutoSizeMode=AutoSizeMode.GrowAndShrink,MinimumSize=new(100,36),Text="Cancel",Margin=new(0)};
 readonly CancellationTokenSource cancellation=new();bool complete;
 public SetupWindow(string root,string pack,List<string> sources,bool scanOnly){
  AutoScaleDimensions=new(96,96);AutoScaleMode=AutoScaleMode.Dpi;
  Text="Catherine — Preparing Game Assets";ClientSize=new(580,240);FormBorderStyle=FormBorderStyle.FixedDialog;MaximizeBox=false;MinimizeBox=false;StartPosition=FormStartPosition.CenterScreen;
  var layout=new TableLayoutPanel{Dock=DockStyle.Fill,ColumnCount=1,RowCount=4,Padding=new(20),Margin=new(0)};
  layout.ColumnStyles.Add(new(SizeType.Percent,100));layout.RowStyles.Add(new(SizeType.AutoSize));layout.RowStyles.Add(new(SizeType.Percent,100));layout.RowStyles.Add(new(SizeType.Absolute,24));layout.RowStyles.Add(new(SizeType.AutoSize));
  var buttons=new FlowLayoutPanel{Dock=DockStyle.Fill,AutoSize=true,AutoSizeMode=AutoSizeMode.GrowAndShrink,FlowDirection=FlowDirection.RightToLeft,WrapContents=false,Padding=new(0,12,0,0),Margin=new(0)};buttons.Controls.Add(stop);
  layout.Controls.Add(stage,0,0);layout.Controls.Add(detail,0,1);layout.Controls.Add(bar,0,2);layout.Controls.Add(buttons,0,3);Controls.Add(layout);
  stop.Click+=(_,_)=>{cancellation.Cancel();stop.Enabled=false;};FormClosing+=(_,e)=>{if(!complete){cancellation.Cancel();e.Cancel=true;}};
  void Update(ProgressState p){if(IsDisposed)return;BeginInvoke(()=>{stage.Text=p.Stage;detail.Text=p.Detail;bar.Value=(int)(p.Fraction*1000);});}
  void Warn(string text){File.AppendAllText(Path.Combine(root,"assets-setup.log"),DateTime.UtcNow.ToString("O")+" "+text+Environment.NewLine);bool request=text.StartsWith("Game files are required.")||text.StartsWith("Update files are required.");Invoke(()=>MessageBox.Show(this,text,"Catherine asset setup",MessageBoxButtons.OK,request?MessageBoxIcon.Information:MessageBoxIcon.Warning));}
  string? Select(bool update){string? result=null;Invoke(()=>{using var dialog=new FolderBrowserDialog{Description=update?"Please Locate the Update files":"Please Locate the Game Files",UseDescriptionForTitle=true,ShowNewFolderButton=false};if(dialog.ShowDialog(this)==DialogResult.OK)result=dialog.SelectedPath;});return result;}
  Shown+=async(_,_)=>{try{await Task.Run(()=>new Installer(root,pack,false,Update,Select,Warn,cancellation.Token).Run(sources,scanOnly));Result=0;}catch(OperationCanceledException){Result=2;}catch(Exception e){File.AppendAllText(Path.Combine(root,"assets-errors.log"),DateTime.UtcNow+" "+e+Environment.NewLine);MessageBox.Show(this,e.Message+"\n\nSee assets-errors.log for details.","Catherine asset setup failed",MessageBoxButtons.OK,MessageBoxIcon.Error);Result=1;}finally{complete=true;Close();}};
 }
}
