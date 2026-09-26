import Foundation
let data=try Data(contentsOf:URL(fileURLWithPath:CommandLine.arguments[1]))
guard let t=Telemetry(data:data) else { fatalError("C packet rejected") }
assert(t.bootID==0xFEDCBA9876543210)
assert(t.sequence==4000000000)
assert(t.totals[6]==(1<<40)+6)
assert(t.counts==[100,101,102,103,104,105,106])
assert(t.temperature == -12.345 && t.pressure == 982.86)
assert(Telemetry(data:data.prefix(159))==nil)
var invalid=data;invalid[2]=9;assert(Telemetry(data:invalid)==nil)
func put(_ b:inout Data,_ at:Int,_ v:UInt64,_ n:Int) { for i in 0..<n { b[at+i]=UInt8(truncatingIfNeeded:v>>(i*8)) } }
var next=data
put(&next,12,4000000002,4);put(&next,16,240000,8);put(&next,146,240010,8);put(&next,24,180000,8)
for i in 0..<7 { put(&next,56+i*8,(1<<40)+UInt64(i)+60,8) }
let n=Telemetry(data:next)!,delta=n.delta(from:t)!
assert(delta.counts == Array(repeating:60,count:7) && delta.exposureMS==120000)
assert(delta.coincidencesPerMinute==90)
put(&next,4,123,8);assert(Telemetry(data:next)!.delta(from:t)==nil)
assert(t.delta(from:t)==nil)
var correction=Correction()
assert(abs(correction.factor(pressure:982.864,temperature:24.637,channel:-1)!-1)<1e-12)
assert(abs(correction.factor(pressure:992.864,temperature:24.637,channel:-1)!-exp(0.015))<1e-12)
assert(abs(correction.factor(pressure:982.864,temperature:25.637,channel:-1)!-exp(0.00393))<1e-12)
assert(abs(correction.factor(pressure:982.864,temperature:25.637,channel:2)!-exp(-0.0016))<1e-12)
assert(correction.factor(pressure:982.864,temperature:24.637,channel:3)==nil)
var setup=data;setup[3]=1;let s=Telemetry(data:setup)!
assert(correction.corrected(sample:s,channel:-1)==nil)
assert(s.temperature==nil && s.pressure==nil)
correction.enabled=false;assert(correction.corrected(sample:t,channel:-1)==nil)
print("PASS: C/Swift wire compatibility, all channels, signed temperature, missed updates, reboot detection, correction sign/units, setup exclusion")
