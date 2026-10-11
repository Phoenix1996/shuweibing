$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$source=[System.Drawing.Image]::FromFile((Join-Path $PSScriptRoot '..\assets\mouse-guardian.png'))
$sizes=@(16,20,24,32,48,64,128,256)
$frames=@()
foreach($size in $sizes){
    $bitmap=[System.Drawing.Bitmap]::new($size,$size)
    $g=[System.Drawing.Graphics]::FromImage($bitmap)
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.InterpolationMode=[System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode=[System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.DrawImage($source,0,0,$size,$size)
    $memory=[System.IO.MemoryStream]::new()
    $bitmap.Save($memory,[System.Drawing.Imaging.ImageFormat]::Png)
    $frames+=,@($memory.ToArray())
    $memory.Dispose();$g.Dispose();$bitmap.Dispose()
}
$stream=[System.IO.File]::Create((Join-Path $PSScriptRoot 'app.ico'))
$writer=[System.IO.BinaryWriter]::new($stream)
$writer.Write([UInt16]0);$writer.Write([UInt16]1);$writer.Write([UInt16]$sizes.Count)
$offset=6+16*$sizes.Count
for($i=0;$i -lt $sizes.Count;$i++){
    $size=$sizes[$i];$frame=[byte[]]$frames[$i]
    $writer.Write([byte]($size % 256));$writer.Write([byte]($size % 256))
    $writer.Write([byte]0);$writer.Write([byte]0);$writer.Write([UInt16]1);$writer.Write([UInt16]32)
    $writer.Write([UInt32]$frame.Length);$writer.Write([UInt32]$offset);$offset+=$frame.Length
}
foreach($frame in $frames){$writer.Write([byte[]]$frame)}
$writer.Dispose();$source.Dispose()
