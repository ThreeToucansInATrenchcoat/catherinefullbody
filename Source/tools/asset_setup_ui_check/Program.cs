using System.Reflection;
using System.Text.Json;
using System.Drawing;
using System.Windows.Forms;
using System.Runtime.InteropServices;
using System.ComponentModel;

static class Program {
 [DllImport("kernel32.dll")]static extern uint GetCurrentThreadId();
 [DllImport("user32.dll")]static extern IntPtr GetThreadDesktop(uint thread);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern bool GetUserObjectInformationW(IntPtr handle,int index,System.Text.StringBuilder value,int length,out int needed);
 [STAThread] static void Main(string[] args){
  var desktopName=new System.Text.StringBuilder(256);GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),2,desktopName,512,out _);if(desktopName.ToString()!="CatherineAssetSetupQA")throw new Exception("Run with Source/tools/test_asset_setup_ui.py to isolate the layout test desktop.");
  Application.SetHighDpiMode(HighDpiMode.DpiUnaware);Application.EnableVisualStyles();
  string root=Path.GetFullPath(args[0]),output=Path.Combine(root,"Build/asset-setup/ui-check");Directory.CreateDirectory(output);
  var assembly=Assembly.LoadFrom(Path.Combine(root,"Build/asset-setup/bin/Release/net10.0-windows/win-x64/CatherineAssetSetup.dll"));var type=assembly.GetType("Catherine.AssetSetup.SetupWindow",true)!;
  var reports=new List<object>();
  foreach(float factor in new[]{1f,1.25f,1.5f,1.75f,2f}){
   using var form=(Form)Activator.CreateInstance(type,root,Path.Combine(root,"assets.pak"),new List<string>(),false)!;
   var key=typeof(Form).GetFields(BindingFlags.Static|BindingFlags.NonPublic).Single(f=>f.Name.Contains("shown",StringComparison.OrdinalIgnoreCase)).GetValue(null)!;
   var events=(EventHandlerList)typeof(Component).GetProperty("Events",BindingFlags.Instance|BindingFlags.NonPublic)!.GetValue(form)!;events.RemoveHandler(key,events[key]);
   form.Show();Application.DoEvents();form.Scale(new SizeF(factor,factor));form.Font=new Font(form.Font.FontFamily,form.Font.Size*factor,form.Font.Style);form.PerformLayout();Application.DoEvents();
   IEnumerable<Control> All(Control p){foreach(Control c in p.Controls){yield return c;foreach(var d in All(c))yield return d;}}
   var stop=All(form).OfType<Button>().Single(b=>b.Text=="Cancel");var bar=All(form).OfType<ProgressBar>().Single();
   Rectangle Relative(Control c){var point=Point.Empty;for(Control p=c;p!=form;p=p.Parent!){point.X+=p.Left;point.Y+=p.Top;}return new Rectangle(point,c.Size);}
   var button=Relative(stop);var progress=Relative(bar);var text=TextRenderer.MeasureText(stop.Text,stop.Font);
   if(!form.ClientRectangle.Contains(button)||button.Height<text.Height+8||progress.Bottom>button.Top)throw new Exception($"Clipped Cancel at {factor}: {button}, text {text}, client {form.ClientSize}");
   using var bitmap=new Bitmap(form.Width,form.Height);form.DrawToBitmap(bitmap,new Rectangle(Point.Empty,bitmap.Size));bitmap.Save(Path.Combine(output,$"ui-{factor*100:0}.png"));
   reports.Add(new{scale=factor,client=new{form.ClientSize.Width,form.ClientSize.Height},cancel=new{button.X,button.Y,button.Width,button.Height},textHeight=text.Height,passed=true});
  }
  File.WriteAllText(Path.Combine(output,"result.json"),JsonSerializer.Serialize(reports,new JsonSerializerOptions{WriteIndented=true}));Console.WriteLine("Cancel layout passed at 100%, 125%, 150%, 175%, and 200% simulated scaling.");
 }
}
