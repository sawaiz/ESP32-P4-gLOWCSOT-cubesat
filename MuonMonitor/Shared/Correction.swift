import Foundation

struct Correction: Codable, Equatable {
    var enabled=true
    var referencePressure=982.864
    var referenceTemperature=24.637
    // Fractional coefficients: values in the settings form are percent/unit.
    var pressureBeta = -0.0015
    var totalTemperatureBeta = -0.00393
    var pairTemperatureBetas = [-0.0070,-0.0062,0.0016]
    var provenance="External-battery run: chosen pressure coefficient; provisional temperature fits consistent with zero."
    func factor(pressure:Double,temperature:Double,channel:Int) -> Double? {
        guard enabled,pressure.isFinite,temperature.isFinite,referencePressure.isFinite,
              referenceTemperature.isFinite,pressureBeta.isFinite,(-1...2).contains(channel),pairTemperatureBetas.count==3 else { return nil }
        let beta=channel == -1 ? totalTemperatureBeta : pairTemperatureBetas[channel]
        let exponent = -pressureBeta*(pressure-referencePressure)-beta*(temperature-referenceTemperature)
        guard exponent.isFinite,abs(exponent)<20 else { return nil }
        let f=exp(exponent);return f.isFinite ? f:nil
    }
    func corrected(sample:Telemetry,channel:Int) -> Double? {
        guard sample.hasSample,sample.physics,let p=sample.pressure,let t=sample.temperature,
              let f=factor(pressure:p,temperature:t,channel:channel), sample.interval>0 else { return nil }
        let n=channel == -1 ? Double(sample.coincidenceCount) : Double(sample.counts[channel])
        return n*60000/Double(sample.interval)*f
    }
}
