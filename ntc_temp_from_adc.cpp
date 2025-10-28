#include <cstdlib>
#include <cstdint>
#include <math.h>
// Возвращает температуру в °C по коду АЦП.
// adc         — значение MCP3204 (0..4095)
// max_counts  — максимум кода (для MCP3204 = 4095)
// r_fixed     — сопротивление второго плеча (Ом), у вас 10000.0
// ntc_on_top  — true, если NTC подключён к +Vref (а 10k к земле)
//               false, если NTC внизу (к земле), а 10k — к +Vref
// r0, beta    — параметры NTC (например R0=4700 Ом @ 25°C, β≈3977 K)
// t0_c        — опорная температура в °C (обычно 25.0)
double ntc_temp_c_from_adc(uint16_t adc,
                           uint16_t max_counts,
                           double   r_fixed,
                           bool     ntc_on_top,
                           double   r0,
                           double   beta,
                           double   t0_c)
{
    // защита от крайних и невалидных значений
    if (adc == 0)     adc = 1;
    if (adc >= max_counts) adc = max_counts - 1;

    // восстановим сопротивление NTC из делителя
    // Vout = Vref * Rfixed / (Rntc + Rfixed)  (NTC сверху)
    // => Rntc = Rfixed * (max-adc)/adc
    // Если NTC снизу: Rntc = Rfixed * adc / (max-adc)
    double r_ntc;
    if (ntc_on_top) {
        r_ntc = r_fixed * (double)(max_counts - adc) / (double)adc;
    } else {
        r_ntc = r_fixed * (double)adc / (double)(max_counts - adc);
    }

    // β-модель: 1/T = 1/T0 + (1/β) * ln(R/R0)
    const double t0_k = t0_c + 273.15;
    double invT = (1.0 / t0_k) + (1.0 / beta) * log(r_ntc / r0);
    double t_k  = 1.0 / invT;
    return t_k - 273.15;
}
