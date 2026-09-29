param([string]$outdir, [double]$seconds = 20)
# burst.ps1: capture the primary screen as fast as possible for $seconds; per frame write time, the mean brightness
# of the left edge strip (x 40-360 of 3840: black when pillarboxed to 16:9) and of the centre, and save a small jpg.
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
New-Item -ItemType Directory -Force $outdir | Out-Null
$b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$bmp = New-Object System.Drawing.Bitmap($b.Width, $b.Height)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$csv = Join-Path $outdir "frames.csv"
"time,left,centre,file" | Out-File -Encoding ascii $csv
$end = (Get-Date).AddSeconds($seconds); $n = 0
while ((Get-Date) -lt $end) {
  $g.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
  $t = (Get-Date).ToString("HH:mm:ss.fff")
  $small = New-Object System.Drawing.Bitmap($bmp, 480, 200)
  $l = 0; $c = 0
  for ($y = 20; $y -lt 180; $y += 10) {
    for ($x = 5; $x -lt 45; $x += 5) { $p = $small.GetPixel($x, $y); $l += $p.R + $p.G + $p.B }
    for ($x = 220; $x -lt 260; $x += 5) { $p = $small.GetPixel($x, $y); $c += $p.R + $p.G + $p.B }
  }
  $f = "f{0:D4}.jpg" -f $n
  $small.Save((Join-Path $outdir $f), [System.Drawing.Imaging.ImageFormat]::Jpeg)
  $small.Dispose()
  "$t,$l,$c,$f" | Out-File -Append -Encoding ascii $csv
  $n++
}
