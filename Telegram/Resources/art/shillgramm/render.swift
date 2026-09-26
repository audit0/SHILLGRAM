import AppKit
let a = CommandLine.arguments
let img = NSImage(contentsOfFile: a[1])!
let size = Int(a[3])!
let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: size, pixelsHigh: size, bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
NSGraphicsContext.saveGraphicsState()
let ctx = NSGraphicsContext(bitmapImageRep: rep)!
ctx.imageInterpolation = .high
NSGraphicsContext.current = ctx
let mode = a.count > 4 ? a[4] : "full"
if mode == "mac" {
  // macOS icon grid: 824pt body inside 1024 canvas, soft drop shadow.
  let s = CGFloat(size) / 1024
  let body = NSRect(x: 100*s, y: 100*s + 10*s, width: 824*s, height: 824*s)
  let sh = NSShadow(); sh.shadowBlurRadius = 28*s; sh.shadowOffset = NSSize(width: 0, height: -12*s)
  sh.shadowColor = NSColor.black.withAlphaComponent(0.35); sh.set()
  img.draw(in: body)
} else {
  img.draw(in: NSRect(x: 0, y: 0, width: size, height: size))
}
NSGraphicsContext.restoreGraphicsState()
try! rep.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: a[2]))
