// MakeCode micro:bit · NeoPixel × 64 · Pin P14
// 1) 擴充套件 → 搜尋 NeoPixel → 加入
// 2) 進階 → JavaScript → 全選貼上
// A 鍵：換效果（1–5）  B 鍵：調亮度

let strip = neopixel.create(DigitalPin.P14, 64, NeoPixelMode.RGB)
strip.setBrightness(100)

let mode = 0
let tick = 0

basic.forever(function () {
    if (mode == 0) {
        // 彩虹波浪
        for (let i = 0; i < 64; i++) {
            strip.setPixelColor(i, neopixel.hsl((i * 6 + tick * 4) % 360, 99, 50))
        }
    } else if (mode == 1) {
        // 彗星拖尾
        for (let i = 0; i < 64; i++) {
            strip.setPixelColor(i, neopixel.rgb(0, 0, 0))
        }
        let head = tick % 64
        for (let t = 0; t < 12; t++) {
            let idx = (head - t + 64) % 64
            let bri = 255 - t * 20
            if (bri < 0) bri = 0
            strip.setPixelColor(idx, neopixel.rgb(bri, Math.idiv(bri, 2), 255 - t * 10))
        }
    } else if (mode == 2) {
        // 火花／火焰
        for (let i = 0; i < 64; i++) {
            let base = Math.randomRange(20, 80)
            strip.setPixelColor(i, neopixel.rgb(base + 100, Math.idiv(base, 2), 0))
        }
        for (let n = 0; n < 8; n++) {
            let i = Math.randomRange(0, 63)
            strip.setPixelColor(i, neopixel.rgb(255, Math.randomRange(80, 200), Math.randomRange(0, 40)))
        }
    } else if (mode == 3) {
        // 劇院追逐
        let off = tick % 3
        let hue = (tick * 5) % 360
        for (let i = 0; i < 64; i++) {
            if (i % 3 == off) {
                strip.setPixelColor(i, neopixel.hsl(hue, 99, 50))
            } else {
                strip.setPixelColor(i, neopixel.rgb(0, 0, 0))
            }
        }
    } else {
        // 雙向掃描
        for (let i = 0; i < 64; i++) {
            strip.setPixelColor(i, neopixel.rgb(0, 0, 0))
        }
        let a = tick % 64
        let b = 63 - a
        strip.setPixelColor(a, neopixel.rgb(0, 255, 140))
        strip.setPixelColor((a + 63) % 64, neopixel.rgb(0, 90, 50))
        strip.setPixelColor(b, neopixel.rgb(255, 50, 160))
        strip.setPixelColor((b + 1) % 64, neopixel.rgb(90, 15, 50))
    }
    strip.show()
    tick += 1
    basic.pause(28)
})

input.onButtonPressed(Button.A, function () {
    mode = (mode + 1) % 5
    basic.showNumber(mode + 1)
})

input.onButtonPressed(Button.B, function () {
    let b = strip.brightness()
    if (b >= 200) {
        b = 40
    } else {
        b = b + 40
    }
    strip.setBrightness(b)
    basic.showIcon(IconNames.Heart)
})
