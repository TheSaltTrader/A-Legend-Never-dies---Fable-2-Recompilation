param([string]$out)
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
$b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$bmp = New-Object System.Drawing.Bitmap($b.Width, $b.Height)
$g = [System.Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
$s = New-Object System.Drawing.Bitmap($bmp, [int]($b.Width/3), [int]($b.Height/3))
$s.Save($out, [System.Drawing.Imaging.ImageFormat]::Jpeg)
Add-Type -Namespace W -Name F -MemberDefinition '[DllImport("user32.dll")] public static extern System.IntPtr GetForegroundWindow(); [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(System.IntPtr h, out uint p);'
$p = 0; [void][W.F]::GetWindowThreadProcessId([W.F]::GetForegroundWindow(), [ref]$p)
$n = (Get-Process -Id $p -ErrorAction SilentlyContinue).ProcessName
"foreground=$n pid=$p" | Out-File -Encoding ascii ($out + ".fg.txt")
"shot foreground=$n"
