// MakeCode micro:bit V2 · WS2812 × 128 · Pin P14
// 聲控變色 + A 換模式 + B 調亮度
// 擴充套件：NeoPixel

let strip = neopixel.create(DigitalPin.P14, 128, NeoPixelMode.RGB)
let bright = 120
let mode = 0
let tick = 0
let smooth = 0

strip.setBrightness(bright)

basic.forever(function () {
    // V2 咪高峰：0–255
    let level = input.soundLevel()
    // 平滑，減少跳動
    smooth = Math.idiv(smooth * 3 + level, 4)

    if (mode == 0) {
        soundRainbow(smooth)
    } else if (mode == 1) {
        soundMeter(smooth)
    } else if (mode == 2) {
        soundPulse(smooth, tick)
    } else {
        soundFire(smooth)
    }

    strip.show()
    tick += 1
    basic.pause(20)
})

input.onButtonPressed(Button.A, function () {
    mode = (mode + 1) % 4
    basic.showNumber(mode + 1)
})

input.onButtonPressed(Button.B, function () {
    bright = bright >= 200 ? 40 : bright + 40
    strip.setBrightness(bright)
    basic.showIcon(IconNames.Heart)
})

// 1) 聲越大，色相越移（全條彩虹跟聲）
function soundRainbow(level) {
    let shift = Math.idiv(level * 360, 255)
    for (let i = 0; i < 128; i++) {
        strip.setPixelColor(i, neopixel.hsl((i * 3 + shift) % 360, 99, 20 + Math.idiv(level, 5)))
    }
}

// 2) VU 聲級條：由頭點亮，顏色綠→黃→紅
function soundMeter(level) {
    let lit = Math.idiv(level * 128, 255)
    for (let i = 0; i < 128; i++) {
        if (i < lit) {
            let hue = 120 - Math.idiv(i * 120, 128)
            if (hue < 0) hue = 0
            strip.setPixelColor(i, neopixel.hsl(hue, 99, 50))
        } else {
            strip.setPixelColor(i, neopixel.rgb(0, 0, 0))
        }
    }
}

// 3) 中間向外脈衝：聲越大範圍越大、越亮
function soundPulse(level, t) {
    let radius = Math.idiv(level * 64, 255)
    let baseHue = (t * 3) % 360
    for (let i = 0; i < 128; i++) {
        let dist = Math.abs(i - 64)
        if (dist <= radius) {
            let bri = 60 - Math.idiv(dist * 50, ((radius < 1) ? 1 : radius))
            if (bri < 10) bri = 10
            strip.setPixelColor(i, neopixel.hsl((baseHue + dist * 4) % 360, 99, bri))
        } else {
            strip.setPixelColor(i, neopixel.rgb(0, 0, 0))
        }
    }
}

// 4) 火焰：安靜暗橙，大聲變白熱火花
function soundFire(level) {
    let heat = 40 + Math.idiv(level * 180, 255)
    for (let i = 0; i < 128; i++) {
        let flicker = Math.randomRange(0, 40)
        let h = heat + flicker
        if (h > 255) h = 255
        strip.setPixelColor(i, neopixel.rgb(h, Math.idiv(h, 3) + Math.idiv(level, 4), Math.idiv(level, 8)))
    }
    let sparks = 2 + Math.idiv(level, 40)
    for (let n = 0; n < sparks; n++) {
        let i = Math.randomRange(0, 127)
        strip.setPixelColor(i, neopixel.rgb(255, 220, 120))
    }
}
