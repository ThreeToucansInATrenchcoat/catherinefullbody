using System.Globalization;
namespace Catherine.AssetSetup;
static class LauncherProgress {
 public static void Report(ProgressState state) {
  string message=(state.Stage+": "+state.Detail).Replace('\r',' ').Replace('\n',' ');
  Console.WriteLine("progress "+Math.Clamp(state.Fraction,0,1).ToString("0.000",CultureInfo.InvariantCulture)+" "+message);
  Console.Out.Flush();
 }
}
