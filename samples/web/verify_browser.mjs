// Browser gate driver for tina_sample_web.
//
// A headless --dump-dom run only proves the loop starts: it cannot press a key, so it
// leaves the whole DOM-event path unverified. This drives the page over the DevTools
// protocol instead, so it can focus the canvas, synthesize real keydown/keyup, and read
// the counters back. The point is a falsifiable input check, not a screenshot.
//
// Usage: node verify_browser.mjs <chrome-path> <url> [targetFrames]
// Exit 0 only if the loop advanced AND the injected keys were counted.

const [chromePath, url, targetFramesArg] = process.argv.slice(2);
if (!chromePath || !url) {
    console.error('usage: node verify_browser.mjs <chrome-path> <url> [targetFrames]');
    process.exit(2);
}
const framesToSee = Number(targetFramesArg ?? 30);
const KEY_PRESSES = 5;
const TAP_COUNT = 3;
const RIGHT_CLICKS = 2;
const MIDDLE_CLICKS = 2;
// A straight horizontal drag, so the expected travel is a plain sum and any Y contribution
// would show up as an overshoot rather than hiding inside a diagonal.
const MOVE_STEPS = 8;
const MOVE_STEP_PX = 10;
const WHEEL_TICKS = 3;
// One "click" of a typical wheel in DOM_DELTA_PIXEL, which is the mode CDP produces.
const WHEEL_PIXELS_PER_TICK = 120;
// Must match PixelsPerNotch in src/platform/html5/Html5Platform.cpp.
const WHEEL_PIXELS_PER_NOTCH = 100;
// DefaultSceneClearColor from include/tina/render/RenderScene.hpp, put through the ADR 0042
// sRGB encode in src/render/bgfx/BgfxClearColor.cpp: linear (0.005182, 0.023153, 0.056128)
// encodes to these bytes. Nothing in the sample should still be showing it, since the sprite
// covers the whole camera -- so it is kept as the value a blank frame would land on.
const EXPECTED_CLEAR = [16, 42, 67];
// The 2x2 texture from SpriteTexels in samples/web/main.cpp, in screen quadrants. The sprite
// spans the whole camera and the sampler is Point, so each quadrant is one flat texel: row 0
// of the upload is the top of the sprite (writeSprite gives the +Y vertices v0, mtxOrtho maps
// world +Y to NDC +Y, GL samples v=0 from the first uploaded row).
//
// Asserted as exact bytes, not a band. Every channel is 0 or 255, which are the two fixed
// points of the sRGB transfer function, so the value survives regardless of whether the
// sampler's decode flag was honoured -- there is no rounding to allow for.
const EXPECTED_QUADRANTS = [
    { name: 'top-left', fx: 0.25, fy: 0.25, rgb: [255, 0, 0] },
    { name: 'top-right', fx: 0.75, fy: 0.25, rgb: [0, 255, 0] },
    { name: 'bottom-left', fx: 0.25, fy: 0.75, rgb: [0, 0, 255] },
    { name: 'bottom-right', fx: 0.75, fy: 0.75, rgb: [255, 255, 255] },
];
// Compositor scaling and PNG round-tripping are byte-exact for flat regions, so this covers
// only that. The smallest defect worth catching is a quadrant landing on a neighbouring
// texel's colour, which is 255 out on at least one channel.
const QUADRANT_TOLERANCE = 2;

const { spawn } = await import('node:child_process');
const os = await import('node:os');
const path = await import('node:path');
const fs = await import('node:fs');

const profileDir = fs.mkdtempSync(path.join(os.tmpdir(), 'tina-cdp-'));
const chrome = spawn(chromePath, [
    '--headless=new',
    '--disable-gpu',
    '--no-sandbox',
    '--remote-debugging-port=9333',
    `--user-data-dir=${profileDir}`,
    // Headless throttles rAF for a backgrounded page; without these the loop advances a
    // few frames per second and the run looks stalled rather than slow.
    '--disable-background-timer-throttling',
    '--disable-renderer-backgrounding',
    '--disable-backgrounding-occluded-windows',
    url,
]);
chrome.stderr.on('data', (chunk) => {
    const text = String(chunk);
    if (/CONSOLE|ERROR|Aborted/i.test(text)) {
        process.stdout.write(`[chrome] ${text}`);
    }
});

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

async function findPageTarget() {
    for (let attempt = 0; attempt < 60; ++attempt) {
        try {
            const response = await fetch('http://127.0.0.1:9333/json/list');
            const targets = await response.json();
            const page = targets.find((t) => t.type === 'page' && t.webSocketDebuggerUrl);
            if (page) {
                return page;
            }
        } catch {
            // DevTools endpoint is not up yet.
        }
        await sleep(250);
    }
    throw new Error('DevTools target never appeared');
}

const page = await findPageTarget();
const socket = new WebSocket(page.webSocketDebuggerUrl);
await new Promise((resolve, reject) => {
    socket.addEventListener('open', resolve, { once: true });
    socket.addEventListener('error', reject, { once: true });
});

let nextId = 1;
const pending = new Map();
socket.addEventListener('message', (event) => {
    const message = JSON.parse(event.data);
    if (message.id && pending.has(message.id)) {
        const { resolve, reject } = pending.get(message.id);
        pending.delete(message.id);
        message.error ? reject(new Error(JSON.stringify(message.error))) : resolve(message.result);
    }
});

function send(method, params = {}) {
    const id = nextId++;
    socket.send(JSON.stringify({ id, method, params }));
    return new Promise((resolve, reject) => pending.set(id, { resolve, reject }));
}

async function evaluate(expression, awaitPromise = false) {
    const result = await send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise });
    return result.result?.value;
}

// Captures the canvas through the compositor and reports one pixel per quadrant plus how
// many distinct colours the capture holds. Decoding happens in the page because the browser
// already has a PNG decoder; shipping one here would be a second decoder to get wrong.
async function readCanvasPixels() {
    try {
        const box = await evaluate(`(() => { const r = document.getElementById('canvas').getBoundingClientRect();
            return { x: Math.round(r.x), y: Math.round(r.y),
                     w: Math.round(r.width), h: Math.round(r.height) }; })()`);
        const shot = await send('Page.captureScreenshot', {
            format: 'png',
            clip: { x: box.x, y: box.y, width: box.w, height: box.h, scale: 1 },
            captureBeyondViewport: false,
        });
        const dataUrl = `data:image/png;base64,${shot.data}`;
        const probes = EXPECTED_QUADRANTS.map((q) => ({ fx: q.fx, fy: q.fy }));
        const sampled = await evaluate(`(async () => {
            const img = new Image();
            img.src = ${JSON.stringify(dataUrl)};
            await img.decode();
            const c = document.createElement('canvas');
            c.width = img.width; c.height = img.height;
            const g = c.getContext('2d');
            g.drawImage(img, 0, 0);
            const probes = ${JSON.stringify(probes)};
            const samples = probes.map((p) => Array.from(g.getImageData(
                Math.min(c.width - 1, Math.floor(c.width * p.fx)),
                Math.min(c.height - 1, Math.floor(c.height * p.fy)), 1, 1).data));
            const centre = Array.from(g.getImageData(Math.floor(c.width / 2),
                                                    Math.floor(c.height / 2), 1, 1).data);
            const all = g.getImageData(0, 0, c.width, c.height).data;
            const seen = new Set();
            for (let i = 0; i < all.length; i += 4) {
                seen.add(all[i] + ',' + all[i + 1] + ',' + all[i + 2]);
            }
            return { samples, centre, distinctColours: seen.size };
        })()`, true);
        if (!sampled?.samples) {
            return { error: 'the capture decoded to no pixels' };
        }
        return sampled;
    } catch (error) {
        return { error: String(error) };
    }
}

async function readCounters() {
    return await evaluate(`(() => ({
        state: document.getElementById('state').textContent,
        frames: Number(document.getElementById('frames').textContent),
        forward: Number(document.getElementById('forward').textContent),
        taps: Number(document.getElementById('taps').textContent),
        secondary: Number(document.getElementById('secondary').textContent),
        middle: Number(document.getElementById('middle').textContent),
        moveFrames: Number(document.getElementById('moveFrames').textContent),
        travel: Number(document.getElementById('travel').textContent),
        lock: document.getElementById('lock').textContent,
        escape: document.getElementById('escape').textContent,
        texture: document.getElementById('texture').textContent,
        // The wheel and finger cells are formatted for a human, so parse rather than Number():
        // "-3.60 over 4 frames" and "2 / 0b11".
        wheelNotches: Number(document.getElementById('wheel').textContent.split(' ')[0]),
        wheelFrames: Number(document.getElementById('wheel').textContent.split(' ')[2]),
        maxFingers: Number(document.getElementById('fingers').textContent.split(' / ')[0]),
        fingerSlots: document.getElementById('fingers').textContent.split(' / ')[1],
    }))()`);
}

async function mouse(type, x, y, button = 'none', extra = {}) {
    await send('Input.dispatchMouseEvent', {
        type,
        x,
        y,
        button,
        buttons: 0,
        clickCount: type === 'mousePressed' || type === 'mouseReleased' ? 1 : 0,
        ...extra,
    });
}

// One press/release pair. Chrome maps left/middle/right to DOM button 0/1/2, which is the
// numbering the backend translates, so this is what pins the mapping.
async function click(x, y, button) {
    await mouse('mousePressed', x, y, button);
    await sleep(40);
    await mouse('mouseReleased', x, y, button);
    await sleep(40);
}

// Canvas-relative coordinates, since the backend reads targetX/targetY.
async function canvasCentre() {
    return await evaluate(`(() => { const r = document.getElementById('canvas').getBoundingClientRect();
        return { x: Math.round(r.left + r.width / 2), y: Math.round(r.top + r.height / 2) }; })()`);
}

async function touch(type, points) {
    await send('Input.dispatchTouchEvent', {
        type,
        touchPoints: points.map((p) => ({ x: p.x, y: p.y, id: p.id })),
    });
}

const failures = [];
try {
    await send('Runtime.enable');

    // Wait for the loop to actually advance. A 40+MB debug wasm needs real time to
    // instantiate, so "state is still loading" early on is expected, not a failure.
    let counters = null;
    let sawRunning = false;
    for (let attempt = 0; attempt < 240; ++attempt) {
        counters = await readCounters();
        if (counters && counters.state !== 'loading') {
            sawRunning = true;
            if (counters.frames >= framesToSee || counters.state !== 'running') {
                break;
            }
        }
        await sleep(250);
    }
    if (!sawRunning) {
        failures.push('the module never left the loading state');
    }
    const framesBeforeInput = counters?.frames ?? 0;
    if (framesBeforeInput < framesToSee) {
        failures.push(`expected at least ${framesToSee} frames, saw ${framesBeforeInput}`);
    }

    // The backend listens on the canvas, not the window, so an unfocused canvas would
    // make every synthesized key vanish with no error anywhere.
    const focused = await evaluate(
        `(() => { const c = document.getElementById('canvas'); c.focus();
                  return document.activeElement === c; })()`);
    if (focused !== true) {
        failures.push('the canvas did not take focus, so no key event could reach the backend');
    }

    // code is what the backend translates; key/keyCode are filled in only so the event
    // looks like a real one to anything else on the page.
    for (let press = 0; press < KEY_PRESSES; ++press) {
        await send('Input.dispatchKeyEvent', {
            type: 'keyDown', code: 'KeyW', key: 'w',
            windowsVirtualKeyCode: 87, nativeVirtualKeyCode: 87,
        });
        // Held keys collapse into one Started transition, so each press must be
        // released before the next or the count would be 1 regardless of KEY_PRESSES.
        await sleep(120);
        await send('Input.dispatchKeyEvent', {
            type: 'keyUp', code: 'KeyW', key: 'w',
            windowsVirtualKeyCode: 87, nativeVirtualKeyCode: 87,
        });
        await sleep(120);
    }

    let afterInput = await readCounters();
    for (let attempt = 0; attempt < 20 && afterInput.forward < KEY_PRESSES; ++attempt) {
        await sleep(150);
        afterInput = await readCounters();
    }

    if (afterInput.forward !== KEY_PRESSES) {
        failures.push(`W presses: expected ${KEY_PRESSES}, counted ${afterInput.forward}`);
    }
    if (afterInput.frames <= framesBeforeInput) {
        failures.push('the loop stopped advancing while input was injected');
    }

    // Single-finger taps. The first finger owns the primary pointer slot, so each tap must
    // raise the pointer-button action -- that is the only touch signal a game can observe.
    const centre = await canvasCentre();
    for (let tap = 0; tap < TAP_COUNT; ++tap) {
        await touch('touchStart', [{ x: centre.x, y: centre.y, id: 1 }]);
        await sleep(120);
        await touch('touchEnd', []);
        await sleep(120);
    }

    let afterTaps = await readCounters();
    for (let attempt = 0; attempt < 20 && afterTaps.taps < TAP_COUNT; ++attempt) {
        await sleep(150);
        afterTaps = await readCounters();
    }
    if (afterTaps.taps !== TAP_COUNT) {
        failures.push(`taps: expected ${TAP_COUNT}, counted ${afterTaps.taps}`);
    }

    // Without this, a passing tap count proves nothing about the touch path: a browser
    // synthesizes mouse events for a tap it thinks nobody handled, and those would drive the
    // same primary slot and the same counter. Read before the multi-touch gesture adds more.
    const probe = await evaluate('globalThis.tinaTouchProbe');
    if (probe?.starts !== TAP_COUNT) {
        failures.push(`touchstart events on the canvas: expected ${TAP_COUNT}, saw ${probe?.starts}`);
    }
    if (probe?.prevented !== probe?.starts) {
        failures.push(`the backend consumed only ${probe?.prevented} of ${probe?.starts} touchstart events`);
    }
    if (probe?.syntheticMouseDowns !== 0) {
        failures.push(
            `${probe?.syntheticMouseDowns} synthesized mousedown events fired, so the tap count is not touch-only`);
    }

    // Two fingers at once. A game cannot see the second finger, so what this checks is the
    // backend's per-slot bookkeeping: if a non-primary slot ended up absent while still
    // holding a button, setPrimaryWindowSnapshot would reject the frame and tick() would fail
    // -- the state would leave 'running' and the frame counter would stop.
    const framesBeforeMulti = afterTaps.frames;
    await touch('touchStart', [{ x: centre.x - 40, y: centre.y, id: 11 }]);
    await sleep(60);
    await touch('touchStart', [
        { x: centre.x - 40, y: centre.y, id: 11 },
        { x: centre.x + 40, y: centre.y, id: 12 },
    ]);
    await sleep(60);
    await touch('touchMove', [
        { x: centre.x - 60, y: centre.y + 20, id: 11 },
        { x: centre.x + 60, y: centre.y - 20, id: 12 },
    ]);
    await sleep(60);
    // Lift one, keep the other down: this is the path that strands a finger if release order
    // is wrong.
    await touch('touchEnd', [{ x: centre.x + 60, y: centre.y - 20, id: 12 }]);
    await sleep(60);
    await touch('touchEnd', []);
    await sleep(400);

    const afterMulti = await readCounters();
    if (afterMulti.state !== 'running') {
        failures.push(`multi-touch left the loop in state '${afterMulti.state}' (frame rejected?)`);
    }
    if (afterMulti.frames <= framesBeforeMulti) {
        failures.push('the loop stopped advancing during the multi-touch gesture');
    }
    // Two fingers must have been visible in game code in the same frame. Counting touch starts
    // cannot show this: one finger tapping twice produces the same count. maxFingers is read
    // from FrameActionSnapshot::pointers, so it is only nonzero if the pointer table is
    // populated and only 2 if both slots were present simultaneously.
    if (afterMulti.maxFingers < 2) {
        failures.push(
            `game code saw at most ${afterMulti.maxFingers} pointer(s) at once during a two-finger gesture`);
    }
    // Slot allocation starts at 0, so both fingers down means the low two bits. A gap here
    // would mean the backend reserved slot 0 for the mouse, which would leave a touch-only
    // device unable to fire any pointer-button action.
    if (afterMulti.fingerSlots !== '0b11') {
        failures.push(`pointer slots seen were ${afterMulti.fingerSlots}, expected 0b11`);
    }

    // Right and middle clicks, counted separately. The browser numbers middle 1 and right 2
    // while the engine enum orders them Secondary then Middle, so this is a real swap in the
    // translation; binding only one button would pass whether or not it is correct.
    for (let i = 0; i < RIGHT_CLICKS; ++i) {
        await click(centre.x, centre.y, 'right');
    }
    for (let i = 0; i < MIDDLE_CLICKS; ++i) {
        await click(centre.x, centre.y, 'middle');
    }
    let afterClicks = await readCounters();
    for (let attempt = 0; attempt < 20 &&
        (afterClicks.secondary < RIGHT_CLICKS || afterClicks.middle < MIDDLE_CLICKS); ++attempt) {
        await sleep(150);
        afterClicks = await readCounters();
    }
    if (afterClicks.secondary !== RIGHT_CLICKS) {
        failures.push(`right clicks: expected ${RIGHT_CLICKS}, counted ${afterClicks.secondary}`);
    }
    if (afterClicks.middle !== MIDDLE_CLICKS) {
        failures.push(`middle clicks: expected ${MIDDLE_CLICKS}, counted ${afterClicks.middle}`);
    }

    // A horizontal drag. The game sees motion only as the primary pointer's look delta, so
    // what is checkable is that motion arrived and that the accumulated distance is the right
    // order -- not an exact float, which depends on how the browser batches moves.
    const travelBefore = afterClicks.travel;
    const moveFramesBefore = afterClicks.moveFrames;
    for (let step = 1; step <= MOVE_STEPS; ++step) {
        await mouse('mouseMoved', centre.x + step * MOVE_STEP_PX, centre.y);
        await sleep(30);
    }
    let afterMove = await readCounters();
    for (let attempt = 0; attempt < 20 && afterMove.moveFrames === moveFramesBefore; ++attempt) {
        await sleep(150);
        afterMove = await readCounters();
    }
    if (afterMove.moveFrames <= moveFramesBefore) {
        failures.push('no frame reported a pointer look delta after the drag');
    }
    const travelled = afterMove.travel - travelBefore;
    const expectedTravel = MOVE_STEPS * MOVE_STEP_PX;
    // Generous band on purpose: the first move differences against wherever the cursor
    // already was, and moves can coalesce. Outside it means the delta is not a distance.
    if (travelled < expectedTravel / 2 || travelled > expectedTravel * 3) {
        failures.push(`pointer travel ${travelled} is not within range of the ${expectedTravel}px drag`);
    }

    // Wheel, asserted numerically. The backend normalises DOM_DELTA_PIXEL by dividing by 100
    // and negates Y so that away-from-the-user is positive, matching the desktop backends. So
    // WHEEL_TICKS injections of deltaY -120 must surface as +1.20 notches each. Both halves of
    // that are worth pinning: a missing divide gives 360, a missing negate gives -3.60.
    const framesBeforeWheel = afterMove.frames;
    for (let i = 0; i < WHEEL_TICKS; ++i) {
        await mouse('mouseWheel', centre.x, centre.y, 'none', { deltaX: 0, deltaY: -WHEEL_PIXELS_PER_TICK });
        await sleep(40);
    }
    await sleep(300);
    let afterWheel = await readCounters();
    for (let attempt = 0; attempt < 20 && afterWheel.wheelFrames === 0; ++attempt) {
        await sleep(150);
        afterWheel = await readCounters();
    }
    if (afterWheel.state !== 'running') {
        failures.push(`wheel left the loop in state '${afterWheel.state}' (frame rejected?)`);
    }
    if (afterWheel.frames <= framesBeforeWheel) {
        failures.push('the loop stopped advancing during the wheel injection');
    }
    const expectedNotches = (WHEEL_TICKS * WHEEL_PIXELS_PER_TICK) / WHEEL_PIXELS_PER_NOTCH;
    // The tolerance covers only representation error, not behaviour. Two sources: 1.2 notches
    // is not exactly representable in binary, and the bridge quantises to hundredths of a
    // notch (+/-0.005). Every defect this check exists to catch is at least 1.2 notches out --
    // a missing divide reads 360, a missing negate reads -3.6, a dropped tick reads 2.4 --
    // so the band is roughly sixty times smaller than the smallest real failure. Unlike
    // pointer motion there is no coalescing to allow for: each wheel event becomes its own
    // transition and they are summed.
    const WHEEL_TOLERANCE_NOTCHES = 0.02;
    if (Math.abs(afterWheel.wheelNotches - expectedNotches) > WHEEL_TOLERANCE_NOTCHES) {
        failures.push(
            `wheel total ${afterWheel.wheelNotches} notches, expected ${expectedNotches.toFixed(2)}` +
            ` (sign inverted or pixel normalisation wrong)`);
    }
    if (afterWheel.wheelFrames === 0) {
        failures.push('no frame reported a wheel delta to game code');
    }

    // Pointer lock. A browser grants it only from inside a user gesture and can drop it at
    // any time, so the assertion is deliberately about the request path, not about the lock
    // engaging: 'request-failed' would mean setMode returned an error, which is a real defect.
    // Whether headless Chrome actually grants the lock is reported but not asserted.
    await click(centre.x, centre.y, 'left');
    await send('Input.dispatchKeyEvent', { type: 'keyDown', code: 'KeyL', key: 'l', windowsVirtualKeyCode: 76 });
    await sleep(40);
    await send('Input.dispatchKeyEvent', { type: 'keyUp', code: 'KeyL', key: 'l', windowsVirtualKeyCode: 76 });
    await sleep(400);
    const afterLock = await readCounters();
    if (!afterLock.lock.includes('requested')) {
        failures.push(`the L key did not reach the lock request path (lock cell: '${afterLock.lock}')`);
    }
    if (afterLock.lock.includes('request-failed')) {
        failures.push('setMode(Locked) returned an error');
    }
    if (afterLock.state !== 'running') {
        failures.push(`pointer lock left the loop in state '${afterLock.state}'`);
    }

    // A frame counter only proves the loop ran, and a correct clear colour would only prove
    // bgfx reached the canvas -- the clear path runs no shader at all. This reads the four
    // quadrants of the drawn sprite instead, which is the first check here that the essl
    // 300_es vertex and fragment programs actually executed on WebGL2: the colours can only
    // differ per quadrant if the vertex stage interpolated UVs and the fragment stage
    // sampled the texture with them.
    //
    // The screenshot goes through the compositor rather than toDataURL, because bgfx does
    // not request preserveDrawingBuffer and the drawing buffer is therefore already
    // cleared by the time script could read it.
    if (afterLock.texture !== 'uploaded') {
        failures.push(`the sprite texture was not uploaded (texture cell: '${afterLock.texture}')`);
    }
    const pixels = await readCanvasPixels();
    if (pixels.error) {
        failures.push(`could not read the canvas pixels: ${pixels.error}`);
    } else {
        EXPECTED_QUADRANTS.forEach((quadrant, index) => {
            const [r, g, b] = pixels.samples[index] ?? [];
            if (r === undefined) {
                failures.push(`the ${quadrant.name} quadrant produced no sample`);
                return;
            }
            const off = Math.max(Math.abs(r - quadrant.rgb[0]), Math.abs(g - quadrant.rgb[1]),
                                 Math.abs(b - quadrant.rgb[2]));
            if (off > QUADRANT_TOLERANCE) {
                failures.push(`${quadrant.name} quadrant is rgb(${r},${g},${b}), expected ` +
                    `rgb(${quadrant.rgb.join(',')}) within ${QUADRANT_TOLERANCE}`);
            }
        });
        // Four distinct colours are what separates "the shader ran" from "something flat
        // filled the canvas". A single colour is the shape every interesting failure takes:
        // an unbound texture falls back to the device's 1x1 white, a dead fragment program
        // leaves the clear colour, and an ignored UV samples one texel everywhere.
        const distinctSamples = new Set(pixels.samples.map((s) => s.slice(0, 3).join(',')));
        if (distinctSamples.size !== EXPECTED_QUADRANTS.length) {
            failures.push(`the four quadrants hold ${distinctSamples.size} distinct colour(s), ` +
                `so the texture was not sampled per-fragment`);
        }
        const centre = pixels.centre.slice(0, 3);
        if (centre.every((c, i) => Math.abs(c - EXPECTED_CLEAR[i]) <= 1)) {
            failures.push('the canvas still shows the scene clear colour, so nothing was drawn');
        }
        if (centre.every((c) => c === 0)) {
            failures.push('canvas centre is black, which is the CSS background showing through');
        }
    }

    console.log(JSON.stringify({
        status: failures.length === 0 ? 'ok' : 'error',
        gate: 'tina_sample_web browser',
        framesBeforeInput,
        framesAfterInput: afterInput.frames,
        keyPressesInjected: KEY_PRESSES,
        keyPressesCounted: afterInput.forward,
        tapsInjected: TAP_COUNT,
        tapsCounted: afterTaps.taps,
        touchStartsObserved: probe?.starts,
        touchStartsConsumedByBackend: probe?.prevented,
        synthesizedMouseDowns: probe?.syntheticMouseDowns,
        framesAfterMultiTouch: afterMulti.frames,
        rightClicksInjected: RIGHT_CLICKS,
        rightClicksCounted: afterClicks.secondary,
        middleClicksInjected: MIDDLE_CLICKS,
        middleClicksCounted: afterClicks.middle,
        dragPixels: MOVE_STEPS * MOVE_STEP_PX,
        pointerTravelCounted: afterMove.travel - travelBefore,
        maxConcurrentFingersSeenByGame: afterMulti.maxFingers,
        pointerSlotsSeenByGame: afterMulti.fingerSlots,
        wheelTicksInjected: WHEEL_TICKS,
        wheelNotchesExpected: Number(expectedNotches.toFixed(2)),
        wheelNotchesCounted: afterWheel.wheelNotches,
        wheelFramesCounted: afterWheel.wheelFrames,
        framesAfterWheel: afterWheel.frames,
        pointerLock: afterLock.lock,
        canvasFocused: focused === true,
        spriteTextureUploaded: afterLock.texture,
        quadrantsExpected: EXPECTED_QUADRANTS.map((q) => `${q.name}=rgb(${q.rgb.join(',')})`),
        quadrantsSampled: pixels.samples
            ? EXPECTED_QUADRANTS.map((q, i) => `${q.name}=rgb(${pixels.samples[i].slice(0, 3).join(',')})`)
            : null,
        canvasClearWhenBlank: `rgb(${EXPECTED_CLEAR.join(',')})`,
        canvasCentreSampled: pixels.centre ? `rgb(${pixels.centre.slice(0, 3).join(',')})` : null,
        canvasDistinctColours: pixels.distinctColours ?? null,
        state: afterLock.state,
        failures,
    }));
} catch (error) {
    console.log(JSON.stringify({ status: 'error', gate: 'tina_sample_web browser', failures: [String(error)] }));
    failures.push(String(error));
} finally {
    socket.close();
    chrome.kill();
    // Chrome still holds files in the profile for a moment after kill, and on Windows
    // that makes the delete fail with EPERM. A leftover temp directory must not change
    // the verdict, so cleanup is best-effort.
    try {
        fs.rmSync(profileDir, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 });
    } catch {
        // Left for the OS to reap.
    }
}

process.exit(failures.length === 0 ? 0 : 1);
