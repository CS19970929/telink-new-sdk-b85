using System;
using System.Windows;

namespace BmsTool.Windows;

public partial class MainWindow
{
    protected override void OnSourceInitialized(EventArgs e)
    {
        base.OnSourceInitialized(e);
        Title = $"{Title} · App Commit {BuildIdentity.Display}";
    }
}
