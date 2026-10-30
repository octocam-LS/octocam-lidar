#pragma once

// Web page for the Octocam Lidar (ESP32), kept in its own header (like
// tofpage.h / senderpage.h) so Arduino's ctags scan never sees the JavaScript.

const char PAGE_HTML[] PROGMEM = R"HTMLPAGE(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="color-scheme" content="dark">
<title>Octocam Lidar</title>
<style>
  :root { --bg:#232323; --fg:#f5f5f0; --accent:#f5f5f0; --panel:#2c2c2c; --line:#444;
          --muted:#999; --steel:#4682b4; --pink:#e48fa6; --teal:#4fd1c5; }
  * { box-sizing:border-box; }
  html, body { margin:0; padding:0; background:var(--bg); color:var(--fg);
    font-family:Arial, Helvetica, sans-serif; width:100%; min-height:100%; }
  .wrap { padding:24px 16px 48px 16px; max-width:520px; margin:0 auto; }
  h1 { font-family:'Arial Black', Arial, Helvetica, sans-serif; font-weight:900;
    text-align:center; color:var(--fg); font-size:clamp(1.6rem,11vw,3.2rem);
    line-height:1.05; margin:8px 0 6px 0; word-break:break-word; }
  .live { text-align:center; font-size:0.8rem; color:var(--muted); margin-bottom:22px;
    font-variant-numeric:tabular-nums; }
  .live.ok::before { content:"\25CF  "; color:var(--steel); }

  .card { background:var(--panel); border:1px solid var(--line); border-radius:12px;
    padding:12px 10px 8px; margin-bottom:18px; transition:border-color 0.15s ease; }
  .card.flash { border-color:#e6e6e6; }

  /* live distance readout */
  .readout { display:flex; align-items:baseline; justify-content:center; gap:6px;
    font-variant-numeric:tabular-nums; margin:2px 0 4px; }
  .readout .big { font-family:'Arial Black', Arial, sans-serif; font-weight:900;
    font-size:clamp(2.2rem,13vw,3.4rem); line-height:1; }
  .readout .u { font-size:1rem; color:var(--muted); font-weight:700; }
  .state { text-align:center; font-size:0.75rem; font-weight:700; letter-spacing:0.08em;
    color:var(--muted); min-height:1.1em; margin-bottom:6px; }
  .state.in { color:var(--steel); }

  /* the ruler */
  #ruler { width:100%; display:block; touch-action:none; cursor:ew-resize;
    -webkit-user-select:none; user-select:none; }

  /* min / max distance, radar style side by side */
  .speed-grid { display:grid; grid-template-columns:1fr 1fr; gap:clamp(8px,3vw,16px); margin-bottom:6px; }
  .field-label { font-weight:700; font-size:clamp(0.85rem,3.6vw,1rem); text-align:center;
    margin-bottom:10px; letter-spacing:0.03em; }
  .field-row { display:flex; align-items:stretch; justify-content:center; width:100%;
    gap:clamp(4px,1.5vw,10px); }
  .arrow-btn { flex:0 0 auto; width:clamp(30px,9vw,52px); background:#333; border:none; border-radius:8px;
    display:flex; align-items:center; justify-content:center; cursor:pointer; touch-action:manipulation;
    user-select:none; -webkit-user-select:none; padding:0; }
  .arrow-btn:active { background:#444; }
  .arrow-btn:disabled { opacity:0.35; pointer-events:none; }
  .arrow-shape { width:0; height:0; border-top:clamp(8px,2.6vw,12px) solid transparent;
    border-bottom:clamp(8px,2.6vw,12px) solid transparent; }
  .arrow-left .arrow-shape { border-right:clamp(10px,3.4vw,16px) solid var(--accent); }
  .arrow-right .arrow-shape { border-left:clamp(10px,3.4vw,16px) solid var(--accent); }
  .value-box { flex:1 1 auto; min-width:0; display:flex; align-items:baseline; justify-content:center;
    background:#2c2c2c; border:1px solid var(--line); border-radius:8px; padding:10px 4px; }
  .value-box input { width:3.2ch; min-width:0; background:transparent; border:none; outline:none;
    color:var(--fg); font-family:inherit; font-size:clamp(1.05rem,5vw,1.4rem); font-weight:600;
    text-align:right; padding:0; -moz-appearance:textfield; }
  .value-box input::-webkit-outer-spin-button,
  .value-box input::-webkit-inner-spin-button { -webkit-appearance:none; margin:0; }
  .value-box .unit { font-size:clamp(0.65rem,2.8vw,0.85rem); color:var(--muted); margin-left:4px; white-space:nowrap; }

  .field-block { margin-bottom:28px; }
  .value-input { flex:1 1 auto; min-width:0; text-align:center; font-size:1.4rem; font-weight:600;
    background:#2c2c2c; color:var(--fg); border:1px solid #444; border-radius:8px; padding:10px 6px; }
  .value-input:disabled { opacity:0.6; }
  .unit-note { text-align:center; font-size:0.75rem; color:var(--muted); margin-top:6px; }
  .password-input { width:100%; text-align:center; font-size:1.2rem; background:#2c2c2c;
    color:var(--fg); border:1px solid #444; border-radius:8px; padding:10px 6px; }
  .status-note { text-align:center; font-size:0.8rem; color:#aaa; margin-top:14px; }
  .section-rule { border:none; border-top:1px solid var(--line); margin:30px 0 26px; }

  /* receiver-style compact rows */
  .duration-row { display:flex; align-items:center; gap:6px; margin-bottom:14px; width:100%; }
  .duration-label { flex:0 0 auto; width:58px; font-size:0.68rem; font-weight:700;
    letter-spacing:0.01em; line-height:1.15; }
  .duration-row .field-row { flex:1 1 auto; width:auto; min-width:0; gap:5px; justify-content:stretch; }
  .duration-row .arrow-btn { width:32px; flex:0 0 auto; }
  .duration-row .value-input { flex:1 1 auto; min-width:0; width:auto; font-size:1rem; padding:8px 2px; }
  .chip-label { background:#5a5a5a; border-radius:8px; min-height:40px; text-align:center;
    display:flex; align-items:center; justify-content:center; }
  .mode-switch { display:flex; gap:4px; padding:4px; margin:4px 0 10px; background:#2c2c2c;
    border:1px solid var(--line); border-radius:10px; }
  .mode-switch button { flex:1 1 0; padding:12px 4px; border:none; border-radius:8px; background:transparent;
    color:#999; font-family:inherit; font-weight:700; font-size:clamp(0.85rem,3.8vw,1rem); cursor:pointer;
    touch-action:manipulation; transition:background 0.15s ease, color 0.15s ease; }
  .mode-switch button.active { background:var(--steel); color:var(--fg); }
  .dim { opacity:0.4; }

  /* Sensitivity presets + sliders */
  .preset-row { display:grid; grid-template-columns:repeat(5, minmax(0,1fr)); gap:4px; padding:4px;
    background:#2c2c2c; border:1px solid var(--line); border-radius:10px; }
  .preset-row button { padding:11px 2px; border:none; border-radius:8px; background:transparent; color:#999;
    font-family:inherit; font-weight:700; font-size:clamp(0.78rem,3.4vw,0.95rem); cursor:pointer;
    touch-action:manipulation; transition:background 0.2s ease, color 0.2s ease; }
  .preset-row button.active { background:var(--steel); color:var(--fg); }
  .preset-note { text-align:center; font-size:0.78rem; color:var(--muted); margin:8px 0 18px; min-height:1.2em; }
  .preset-note b { color:var(--fg); }
  .sliders { display:grid; gap:20px; margin-bottom:6px; }
  .sl-head { display:flex; justify-content:space-between; align-items:baseline; gap:8px; margin-bottom:2px; }
  .sl-name { font-weight:700; font-size:0.9rem; letter-spacing:0.03em; }
  .sl-val { font-weight:700; font-size:0.95rem; font-variant-numeric:tabular-nums; color:var(--fg); }
  .sl-val small { font-weight:400; color:var(--muted); font-size:0.75rem; }
  .sl-track { position:relative; height:40px; margin:0 13px; cursor:pointer; touch-action:none;
    -webkit-user-select:none; user-select:none; outline:none; }
  .sl-track::before { content:""; position:absolute; left:0; right:0; top:17px; height:6px; border-radius:3px; background:#3a3a3a; }
  .sl-fill { position:absolute; left:0; top:17px; height:6px; border-radius:3px; background:var(--steel); width:0; }
  .sl-tick { position:absolute; top:28px; width:2px; height:6px; margin-left:-1px; background:#555; }
  .sl-thumb { position:absolute; top:7px; width:26px; height:26px; margin-left:-13px; border-radius:50%;
    background:var(--fg); border:3px solid var(--steel); box-shadow:0 2px 4px rgba(0,0,0,0.45);
    transition:transform 0.15s ease, box-shadow 0.15s ease; left:0; }
  .sl-track.grabbed .sl-thumb { transform:scale(1.15); box-shadow:0 4px 10px rgba(0,0,0,0.55); }
  .sl-track:focus-visible .sl-thumb { outline:2px solid var(--fg); outline-offset:3px; }
  .sl-labels { position:relative; height:16px; margin:0 13px; font-size:0.68rem; color:var(--muted);
    font-variant-numeric:tabular-nums; }
  .sl-labels span { position:absolute; transform:translateX(-50%); white-space:nowrap; }
  .sl-labels span.on { color:var(--fg); font-weight:700; }
  .sl-ends { display:flex; justify-content:space-between; font-size:0.68rem; color:#777; margin:2px 0 0;
    text-transform:uppercase; letter-spacing:0.06em; }
  .chip-toggle { display:table; background:#5a5a5a; border:none; border-radius:8px; color:var(--fg);
    font-family:inherit; font-weight:700; font-size:1rem; letter-spacing:0.03em;
    padding:8px 16px; margin:0 auto 10px auto; cursor:pointer; touch-action:manipulation; }
  .chip-toggle.active { background:#3d6f90; }

  .interval-btn { width:100%; padding:16px; font-size:1.1rem; font-weight:700;
    border:none; border-radius:10px; background:#808080; color:var(--fg);
    margin-bottom:10px; cursor:pointer; transition:background 0.15s ease;
    -webkit-user-select:none; user-select:none; -webkit-touch-callout:none; touch-action:manipulation; }
  .interval-btn.off { background:#4a4a4a; color:#999; }
  .interval-btn.pulse { background:#b0b0b0; }
  .interval-btn.on-state { background:var(--teal); color:#123; }
  .interval-btn.forced { box-shadow:inset 0 0 0 3px var(--pink); }

  .trigger-btn-wrap { text-align:center; margin-bottom:32px; }
  .trigger-btn { width:104px; height:104px; border-radius:50%; background:#8b1e1e;
    border:2px solid #641414; cursor:pointer; touch-action:manipulation;
    color:var(--fg); font-family:Arial, Helvetica, sans-serif; font-weight:900;
    font-size:1.1rem; letter-spacing:0.1em; }
  .trigger-btn:active { background:#a52a2a; }

  .small-btn-wrap { text-align:center; margin-top:8px; }
  .small-btn { padding:6px 14px; font-size:0.75rem; font-weight:700; border:none; margin:4px;
    border-radius:8px; background:#808080; color:var(--fg); cursor:pointer; }
  .small-btn:active { background:#666; }
  .small-btn:disabled { background:#4a4a4a; color:#888; cursor:default; }
  .small-btn.on-state { background:var(--teal); color:#123; }
  .small-btn.warn { background:#8b1e1e; }
  .small-btn.warn:active { background:#a52a2a; }

  .hist-block { margin-bottom:32px; }
  .hist-title-row { display:flex; align-items:baseline; justify-content:space-between; margin-bottom:10px; flex-wrap:wrap; gap:6px; }
  .hist-title-row .field-label { margin-bottom:0; }
  .hist-count { font-weight:900; font-size:1rem; color:var(--fg); }
  .legend-dot { display:inline-block; width:9px; height:9px; border-radius:2px; margin-right:5px; }
  .reset-btn { padding:10px 18px; font-size:0.85rem; font-weight:700; border:none;
    border-radius:8px; background:#808080; color:var(--fg); cursor:pointer; }
  .reset-btn:active { background:#666; }
  .hist-row { display:flex; align-items:stretch; }
  .hist-yaxis { width:34px; flex:0 0 auto; height:128px; position:relative; font-size:0.65rem; color:#999; }
  .hist-yaxis div { position:absolute; right:4px; transform:translateY(-50%); }
  .hist-scroll { flex:1 1 auto; overflow-x:auto; height:150px; }
  #histCanvas { display:block; }

  .shutdown-btn-wrap { text-align:center; margin-bottom:28px; }
  .shutdown-btn { padding:14px 24px; font-size:0.95rem; font-weight:700; border:none;
    border-radius:10px; background:#808080; color:var(--fg); cursor:pointer; }
  .shutdown-btn:active { background:#666; }
  .shutdown-btn.alive-on { background:var(--teal); color:#123; }
  .save-wrap { text-align:center; margin-bottom:14px; }
  .save-btn.unsaved { background:var(--teal); color:#123; }
  .save-btn.saved { background:#3d6f90; }

  .pairing-block { text-align:center; margin-top:8px; margin-bottom:8px; }
  .pairing-count { font-size:0.85rem; color:#ccc; margin-top:10px; }
  .pairing-count b { font-weight:900; color:var(--fg); }
  .pairing-block .small-btn { margin:0 4px; }

  /* Paired Devices: one toggle button per paired sender / trigger, titled with its SSID.
     Grey = off (ignored / not sent to), blue = on. Any number can be on. */
  .device-group-label { font-weight:700; font-size:0.85rem; letter-spacing:0.03em; color:#ccc;
    margin:14px 0 8px 2px; }
  .sender-list { display:flex; flex-direction:column; gap:8px; }
  .sender-btn { width:100%; padding:14px 12px; font-size:1rem; font-weight:700; border:none;
    border-radius:10px; background:#4a4a4a; color:#999; cursor:pointer; touch-action:manipulation;
    word-break:break-word; transition: background 0.15s ease; user-select:none; -webkit-user-select:none; }
  .sender-btn.active { background:#2f6fd0; color:var(--fg); }
  .sender-btn:active { filter:brightness(1.15); }
  .sender-btn .sender-mac { display:block; font-size:0.65rem; font-weight:400; opacity:0.75; margin-top:3px; }
  .sender-row { display:flex; align-items:stretch; gap:8px; }
  .sender-row .sender-btn { flex:1 1 auto; min-width:0; width:auto; }
  .forget-btn { flex:0 0 auto; width:84px; padding:6px 4px; font-size:0.8rem; font-weight:700; border:none;
    border-radius:10px; background:#8b1e1e; color:var(--fg); cursor:pointer; touch-action:manipulation; }
  .forget-btn:active { background:#a52a2a; }
  .forget-btn.armed { background:#c0392b; }

  .wrap { position:relative; }
  .bat-badge { position:absolute; top:6px; right:16px; display:flex; align-items:center; gap:6px;
    font-size:0.72rem; font-weight:700; color:var(--muted); font-variant-numeric:tabular-nums; }
  .bat-ico { position:relative; width:33px; height:16px; border:2px solid currentColor; border-radius:4px;
    box-sizing:border-box; padding:1.5px; margin-right:4px; }
  .bat-ico::after { content:""; position:absolute; right:-5px; top:3.5px; width:3px; height:5px;
    background:currentColor; border-radius:0 1px 1px 0; }
  .bat-fill { display:block; height:100%; width:0; border-radius:1px; background:#4cc35c; }
  .bat-badge.low { color:#e08a8a; }
  .bat-badge.low .bat-fill { background:#d64949; }
  .bat-badge.mid .bat-fill { background:#e0a33a; }
  .upd-block { text-align:center; margin-top:8px; }
  .upd-btn { background:var(--teal); color:#123; }
  .upd-btn:disabled { background:#4a4a4a; color:#888; }
  .status-note.err { color:#e08a8a; }
  .status-note b { color:var(--fg); }
</style>
</head>
<body>
<div class="wrap">
  <div class="bat-badge" id="batBadge" title="battery"><span id="batVolt"></span><span class="bat-ico"><span class="bat-fill" id="batFill"></span></span><span id="batPct">--</span></div>
  <h1>Octocam Lidar</h1>
  <div class="live" id="live">connecting...</div>

  <!-- ================= distance range ruler ================= -->
  <div class="card" id="rulerCard">
    <div class="readout"><span class="big" id="distBig">---</span><span class="u">cm</span></div>
    <div class="state" id="distState">&nbsp;</div>
    <canvas id="ruler"></canvas>
  </div>

  <div class="speed-grid">
    <div>
      <div class="field-label">Min Distance</div>
      <div class="field-row">
        <button class="arrow-btn arrow-left" id="minDown"><span class="arrow-shape"></span></button>
        <div class="value-box"><input type="number" id="minValue" inputmode="numeric"><span class="unit">cm</span></div>
        <button class="arrow-btn arrow-right" id="minUp"><span class="arrow-shape"></span></button>
      </div>
    </div>
    <div>
      <div class="field-label">Max Distance</div>
      <div class="field-row">
        <button class="arrow-btn arrow-left" id="maxDown"><span class="arrow-shape"></span></button>
        <div class="value-box"><input type="number" id="maxValue" inputmode="numeric"><span class="unit">cm</span></div>
        <button class="arrow-btn arrow-right" id="maxUp"><span class="arrow-shape"></span></button>
      </div>
    </div>
  </div>
  <div class="unit-note" style="margin-bottom:24px;">Drag the handle bars on the ruler (or use the arrows) to set the trigger range. A trigger fires the camera on this device and all paired receivers, once per pass. Press Save Settings below to keep it after a power cycle.</div>

  <div class="mode-switch" role="group" aria-label="Trigger mode">
    <button type="button" id="modeMove">Movement</button>
    <button type="button" id="modeBarrier">Light-Barrier</button>
  </div>
  <div class="unit-note" id="modeNote" style="margin-top:0; margin-bottom:18px;"></div>

  <div class="duration-row" id="moveFilterRow">
    <div class="duration-label chip-label" style="width:76px;">Sideways Movement Threshold</div>
    <div class="field-row">
      <button class="arrow-btn arrow-left" id="moveDown"><span class="arrow-shape"></span></button>
      <input class="value-input" type="text" id="moveValue" readonly>
      <button class="arrow-btn arrow-right" id="moveUp"><span class="arrow-shape"></span></button>
    </div>
  </div>
  <div class="unit-note" id="moveNote" style="margin-top:-6px; margin-bottom:22px;">Movement mode: how much closer a subject has to come before it triggers.</div>

  <div class="field-label" style="margin-bottom:10px;">Sensor Sensitivity</div>
  <div class="preset-row" role="group" aria-label="Sensitivity presets">
    <button type="button" data-preset="bright">Bright</button>
    <button type="button" data-preset="dark">Dark</button>
    <button type="button" data-preset="far">Far</button>
    <button type="button" data-preset="macro">Macro</button>
    <button type="button" data-preset="rain">Rain</button>
  </div>
  <div class="preset-note" id="presetNote"></div>

  <div class="sliders">
    <div class="sl" id="slRate">
      <div class="sl-head"><span class="sl-name">Readings/s</span><span class="sl-val"></span></div>
      <div class="sl-track" tabindex="0" role="slider" aria-label="Readings per second"><div class="sl-fill"></div><div class="sl-thumb"></div></div>
      <div class="sl-labels"></div>
    </div>
    <div class="sl" id="slIso">
      <div class="sl-head"><span class="sl-name">Sensitivity</span><span class="sl-val"></span></div>
      <div class="sl-track" tabindex="0" role="slider" aria-label="Sensitivity (ISO)"><div class="sl-fill"></div><div class="sl-thumb"></div></div>
      <div class="sl-labels"></div>
    </div>
    <div class="sl" id="slFilter">
      <div class="sl-head"><span class="sl-name">Movement Filter</span><span class="sl-val"></span></div>
      <div class="sl-track" tabindex="0" role="slider" aria-label="Movement Filter"><div class="sl-fill"></div><div class="sl-thumb"></div></div>
      <div class="sl-labels"></div>
      <div class="sl-ends"><span>steady</span><span>fast</span></div>
    </div>
  </div>
  <div class="unit-note" id="filterNote" style="margin-bottom:4px;">Movement Filter: readings in a row before triggering (Light-Barrier only).</div>

  <hr class="section-rule">

  <!-- ================= camera output on this device ================= -->
  <div class="duration-row">
    <div class="duration-label chip-label" style="width:76px;">Trigger Duration</div>
    <div class="field-row">
      <button class="arrow-btn arrow-left" id="durDown"><span class="arrow-shape"></span></button>
      <input class="value-input" type="text" id="durValue" inputmode="numeric">
      <button class="arrow-btn arrow-right" id="durUp"><span class="arrow-shape"></span></button>
    </div>
  </div>
  <div class="unit-note" style="margin-top:-6px; margin-bottom:26px;">how long the shutter is held for the camera on this device (default 250 ms)</div>

  <div class="field-block">
    <button type="button" class="chip-toggle" id="extraToggleBtn">Extra Triggers</button>
    <div class="field-row">
      <button class="arrow-btn arrow-left" id="extraCountDown"><span class="arrow-shape"></span></button>
      <input class="value-input" type="number" id="extraCountValue" inputmode="numeric">
      <button class="arrow-btn arrow-right" id="extraCountUp"><span class="arrow-shape"></span></button>
    </div>
    <div class="unit-note" id="extraCountNote"></div>
  </div>

  <div class="duration-row">
    <div class="duration-label" style="width:76px;">Gap Between Triggers</div>
    <div class="field-row">
      <button class="arrow-btn arrow-left" id="extraGapDown"><span class="arrow-shape"></span></button>
      <input class="value-input" type="text" id="extraGapValue" inputmode="numeric">
      <button class="arrow-btn arrow-right" id="extraGapUp"><span class="arrow-shape"></span></button>
    </div>
  </div>
  <div class="unit-note" style="margin-top:-6px; margin-bottom:26px;">between extra triggers</div>

  <div class="duration-row">
    <div class="duration-label chip-label">Delay</div>
    <div class="field-row">
      <button class="arrow-btn arrow-left" id="triggerDelayDown"><span class="arrow-shape"></span></button>
      <input class="value-input" type="text" id="triggerDelayValue" inputmode="numeric">
      <button class="arrow-btn arrow-right" id="triggerDelayUp"><span class="arrow-shape"></span></button>
    </div>
  </div>
  <div class="unit-note" style="margin-top:-6px; margin-bottom:26px;">delay for the camera on this device (paired receivers use their own delay)</div>

  <div class="field-block" id="intervalBlock">
    <button class="interval-btn off" id="intervalToggleBtn">Interval</button>
    <div class="unit-note" id="intervalHint">tap to show interval, delayed start and timer</div>

    <div id="intervalPanel" style="display:none;">
      <div class="field-row">
        <button class="arrow-btn arrow-left" id="intervalDown"><span class="arrow-shape"></span></button>
        <input class="value-input" type="text" id="intervalValue" inputmode="numeric">
        <button class="arrow-btn arrow-right" id="intervalUp"><span class="arrow-shape"></span></button>
      </div>
      <div class="unit-note" id="intervalNote"></div>

      <div class="field-block" style="margin-top:24px;">
        <button class="interval-btn" id="startDelayToggleBtn">Delayed Start</button>
        <div class="field-row">
          <button class="arrow-btn arrow-left" id="startDelayDown"><span class="arrow-shape"></span></button>
          <input class="value-input" type="text" id="startDelayValue" inputmode="numeric" readonly>
          <button class="arrow-btn arrow-right" id="startDelayUp"><span class="arrow-shape"></span></button>
        </div>
        <div class="unit-note" id="startDelayNote"></div>
      </div>

      <div class="field-block" style="margin-top:24px; margin-bottom:0;">
        <button class="interval-btn" id="timerToggleBtn">Timer</button>
        <div class="field-row">
          <button class="arrow-btn arrow-left" id="timerDown"><span class="arrow-shape"></span></button>
          <input class="value-input" type="text" id="timerValue" inputmode="numeric" readonly>
          <button class="arrow-btn arrow-right" id="timerUp"><span class="arrow-shape"></span></button>
        </div>
        <div class="unit-note" id="timerNote"></div>
      </div>
    </div>
  </div>

  <div class="trigger-btn-wrap" style="margin-top:28px;">
    <button class="trigger-btn" id="triggerBtn" aria-label="Manual trigger">TEST</button>
  </div>

  <div class="save-wrap">
    <button class="shutdown-btn save-btn" id="saveBtn">Save Settings</button>
    <div class="unit-note" id="saveNote" style="margin-top:6px;">all settings are saved</div>
  </div>

  <div class="small-btn-wrap" style="margin-bottom:28px;">
    <button class="small-btn warn" id="resetDefaultsBtn">Reset to Default</button>
    <div class="unit-note" style="margin-top:8px;">Changes work right away but are only kept after a power cycle when you press Save Settings (password, SSID and pairings are always kept). Reset to Default sets the range back to 0.3-8 m, the movement filter to 60 cm, the trigger duration to 250 ms, extra triggers, gap and delay to their defaults and switches the interval off -- press Save Settings to keep that too. Mode and sensitivity are not changed.</div>
  </div>

  <div class="field-block">
    <div class="field-label">Signal Strength</div>
    <div class="field-row">
      <button class="arrow-btn arrow-left" id="powerDown"><span class="arrow-shape"></span></button>
      <input class="value-input" type="text" id="powerValue" readonly>
      <button class="arrow-btn arrow-right" id="powerUp"><span class="arrow-shape"></span></button>
    </div>
    <div class="unit-note">decrease to save battery</div>
  </div>

  <div class="hist-block">
    <div class="hist-title-row">
      <div class="field-label">Signal Frequency <span class="hist-count" id="histCount">0</span></div>
      <button class="reset-btn" id="resetHistBtn">Reset</button>
    </div>
    <div class="hist-row">
      <div class="hist-yaxis" id="histYAxis"></div>
      <div class="hist-scroll" id="histScroll"><canvas id="histCanvas"></canvas></div>
    </div>
    <div class="unit-note">signals per minute, this session only<br><span class="legend-dot" style="background:#e48fa6"></span>distance triggers &nbsp; <span class="legend-dot" style="background:#4fd1c5"></span>interval &nbsp; <span class="legend-dot" style="background:#4682b4"></span>received from LS senders</div>
  </div>

  <div class="shutdown-btn-wrap">
    <button class="shutdown-btn" id="keepAliveBtn">Keep App Alive</button>
    <div class="unit-note" style="margin:6px 0 16px 0;">stops the automatic shutdown 2 minutes after your phone disconnects (not saved)</div>
    <button class="shutdown-btn" id="shutdownBtn">Shutdown App</button>
    <div class="unit-note" style="margin-top:6px;">hold the BOOT button on the board for 1-2.5 s to bring the app back (the display shows "Web App" while you hold it)</div>
  </div>

  <div class="field-block">
    <div class="field-label">Password</div>
    <input class="password-input" type="password" id="passwordValue" minlength="8" placeholder="enter a new password">
    <div class="unit-note">New devices start with the password 88888888 -- change it here. 8+ characters; clearing the field makes the network open. The network is always open for the first 10s after power-on. Only one phone can be connected at a time.</div>
    <div class="status-note" id="statusNote"></div>
  </div>

  <div class="field-block">
    <div class="field-label">Change SSID</div>
    <input class="password-input" type="text" id="ssidValue" maxlength="32" placeholder="leave blank to use the automatic name">
    <div class="unit-note">Replaces the automatic ID with a name of your choice, saved permanently. Applying this disconnects your phone right away; reconnect to the new name afterward.</div>
    <div class="small-btn-wrap"><button class="small-btn" id="ssidApplyBtn">Apply SSID</button></div>
    <div class="status-note" id="ssidStatusNote"></div>
  </div>

  <div class="field-block" id="devicesBlock">
    <button class="interval-btn" id="devicesToggleBtn">Paired Devices</button>
    <div class="pairing-block" style="margin-top:0;">
      <button class="small-btn" id="pairSenderBtn">Pair Senders</button>
      <button class="small-btn warn" id="clearSendersBtn">Clear Senders</button>
      <div class="pairing-count">Paired senders: <b id="pairedSenderCount">0</b></div>
    </div>
    <div class="pairing-block" style="margin-bottom:12px;">
      <button class="small-btn" id="pairBtn">Pairing Signal</button>
      <button class="small-btn warn" id="clearPairingsBtn">Clear Triggers</button>
      <div class="pairing-count">Paired triggers: <b id="pairedCount">0</b></div>
    </div>
    <div class="unit-note" id="devicesHint">tap Paired Devices to show the senders and triggers this device is paired with</div>

    <div id="devicesPanel" style="display:none;">
      <div class="device-group-label">Senders</div>
      <div class="sender-list" id="senderList"></div>
      <div class="device-group-label">Triggers</div>
      <div class="sender-list" id="targetList"></div>
      <div class="unit-note" style="margin-top:10px;">tap a sender or trigger to switch it on (blue) or off. Signals from senders that are off are ignored; triggers that are off are not fired. Several can be on at once (saved right away). Forget removes one device (tap twice); to use it again, pair it again. Pair Senders opens an 8 s window: switch the sender on during it. Pairing Signal makes this device pairable for 10 s: open the trigger's pairing window first. Tap Paired Devices again to close.</div>
    </div>
  </div>

  <div class="field-block upd-block">
    <div class="field-label">Firmware</div>
    <div class="unit-note" id="verNote">Installed: ...</div>
    <div class="shutdown-btn-wrap" style="margin:12px 0 4px;"><button class="shutdown-btn upd-btn" id="updBtn">Update Firmware</button></div>
    <div class="status-note" id="updNote"></div>
  </div>

  <div class="field-block">
    <div class="field-label">Home WiFi (for updates)</div>
    <input class="password-input" type="text" id="homeSsid" maxlength="32" placeholder="WiFi name" autocapitalize="off" autocorrect="off" style="margin-bottom:8px;">
    <input class="password-input" type="password" id="homePass" maxlength="64" placeholder="password (8+ characters)">
    <div class="small-btn-wrap"><button class="small-btn" id="homeSaveBtn">Save WiFi</button></div>
    <div class="status-note" id="homeNote">Used only for Update Firmware. The password is saved only on this device.</div>
  </div>
</div>

<script>
  // Number fields show their unit ("250 ms", "5 min"). While one is being typed in it shows the
  // bare number, so editing works normally, and the unit comes back when you leave the field.
  function showMs(inp, v) { inp.value = (document.activeElement === inp) ? String(v) : (v + ' ms'); }
  function attachMsField(inp, getValue) {
    inp.addEventListener('focus', () => { inp.value = String(getValue()); inp.select(); });
    inp.addEventListener('blur', () => { inp.value = getValue() + ' ms'; });
  }
  const $ = id => document.getElementById(id);
  const css = getComputedStyle(document.documentElement);
  const STEEL = css.getPropertyValue('--steel').trim() || '#4682b4';
  const PINK = css.getPropertyValue('--pink').trim() || '#e48fa6';

  let cfg = { minCm:30, maxCm:800, rangeCm:800, minLimitCm:20, filter:2, lunaRate:100, lunaMinAmp:100, mode:0, moveFilter:60, minSpanCm:100 };
  let live = { ok:false, v:false, d:0, a:0, t:0, in:false, rps:0, n:null };

  let post = function (params) {
    return fetch('/api/config', { method:'POST',
      headers:{'Content-Type':'application/x-www-form-urlencoded'},
      body:new URLSearchParams(params).toString() });
  };
  function postJson(params) { return post(params).then(r => r.json()); }

  // accelerating hold, same feel as the other Octocam apps
  function attachHold(btn, stepFn) {
    let holdT = null, repT = null;
    const rep = tick => { stepFn(tick); repT = setTimeout(() => rep(tick + 1), Math.max(40, 220 - tick * 15)); };
    const stop = () => { clearTimeout(holdT); clearTimeout(repT); };
    btn.addEventListener('pointerdown', () => { stepFn(0); clearTimeout(holdT); holdT = setTimeout(() => rep(1), 300); });
    ['pointerup', 'pointerleave', 'pointercancel'].forEach(e => btn.addEventListener(e, stop));
    btn.addEventListener('contextmenu', e => e.preventDefault());
  }
  function closestIndexIn(steps, value) {
    let best = 0, diff = Infinity;
    steps.forEach((s, i) => { const d = Math.abs(s - value); if (d < diff) { diff = d; best = i; } });
    return best;
  }
  function formatMMSS(totalSeconds) {
    const s = Math.max(0, Math.floor(totalSeconds));
    return String(Math.floor(s / 60)).padStart(2, '0') + ':' + String(s % 60).padStart(2, '0');
  }
  function armConfirm(btn, label, action) {           // destructive buttons: tap twice within 5 s
    let armed = false, t = null;
    btn.addEventListener('click', () => {
      if (!armed) {
        armed = true; btn.textContent = 'Tap again to confirm';
        t = setTimeout(() => { armed = false; btn.textContent = label; }, 5000);
        return;
      }
      clearTimeout(t); armed = false; btn.textContent = label;
      action();
    });
  }

  // =============================== RULER ===============================
  const rc = $('ruler'), rg = rc.getContext('2d');
  const RH = 124, PAD = 18, BODY_T = 50, BODY_B = 82;
  const minInput = $('minValue'), maxInput = $('maxValue');
  let dragging = null;

  function rulerSize() {
    const r = window.devicePixelRatio || 1, w = rc.clientWidth;
    rc.width = w * r; rc.height = RH * r; rc.style.height = RH + 'px';
    rg.setTransform(r, 0, 0, r, 0, 0);
  }
  const span = () => rc.clientWidth - 2 * PAD;
  const xOf = cm => PAD + Math.max(0, Math.min(cfg.rangeCm, cm)) / cfg.rangeCm * span();
  const cmOf = x => (x - PAD) / span() * cfg.rangeCm;
  const fmtM = cm => (cm / 100).toFixed(2) + ' m';

  function roundRect(x, y, w, h, r) {
    rg.beginPath();
    rg.moveTo(x + r, y); rg.lineTo(x + w - r, y); rg.quadraticCurveTo(x + w, y, x + w, y + r);
    rg.lineTo(x + w, y + h - r); rg.quadraticCurveTo(x + w, y + h, x + w - r, y + h);
    rg.lineTo(x + r, y + h); rg.quadraticCurveTo(x, y + h, x, y + h - r);
    rg.lineTo(x, y + r); rg.quadraticCurveTo(x, y, x + r, y); rg.closePath();
  }

  function drawHandle(x, active) {
    const w = 14, top = BODY_T - 9, bot = BODY_B + 9;
    rg.fillStyle = active ? '#ffffff' : '#e6e6e6';
    rg.strokeStyle = '#232323'; rg.lineWidth = 1.5;
    roundRect(x - w / 2, top, w, bot - top, 4); rg.fill(); rg.stroke();
    rg.strokeStyle = '#555'; rg.lineWidth = 1;
    for (const dy of [-5, 0, 5]) {
      const y = (top + bot) / 2 + dy + 0.5;
      rg.beginPath(); rg.moveTo(x - 3.5, y); rg.lineTo(x + 3.5, y); rg.stroke();
    }
  }

  function drawRuler() {
    const W = rc.clientWidth;
    rg.clearRect(0, 0, W, RH);

    // body + steel-blue trigger range
    rg.fillStyle = '#383838';
    roundRect(PAD, BODY_T, span(), BODY_B - BODY_T, 5); rg.fill();
    const x0 = xOf(cfg.minCm), x1 = xOf(cfg.maxCm);
    rg.fillStyle = STEEL;
    rg.fillRect(x0, BODY_T, x1 - x0, BODY_B - BODY_T);

    // ruler ticks: 10 cm / 50 cm / 1 m
    rg.strokeStyle = 'rgba(245,245,240,0.6)'; rg.lineWidth = 1;
    for (let cm = 0; cm <= cfg.rangeCm; cm += 10) {
      const x = Math.round(xOf(cm)) + 0.5;
      const len = cm % 100 === 0 ? 14 : cm % 50 === 0 ? 9 : 4;
      rg.beginPath(); rg.moveTo(x, BODY_T); rg.lineTo(x, BODY_T + len); rg.stroke();
    }
    rg.fillStyle = '#999'; rg.font = '11px Arial'; rg.textAlign = 'center';
    for (let m = 0; m * 100 <= cfg.rangeCm; m++) rg.fillText(m + ' m', xOf(m * 100), BODY_B + 26);

    // dead zone below the TF-Luna's minimum distance
    rg.fillStyle = 'rgba(0,0,0,0.35)';
    rg.fillRect(PAD, BODY_T, xOf(cfg.minLimitCm) - PAD, BODY_B - BODY_T);

    // live distance marker
    if (live.ok && live.v) {
      const x = xOf(live.d);
      rg.strokeStyle = PINK; rg.lineWidth = 2;
      rg.beginPath(); rg.moveTo(x, BODY_T - 6); rg.lineTo(x, BODY_B); rg.stroke();
      rg.fillStyle = PINK;
      rg.beginPath(); rg.moveTo(x - 7, BODY_T - 18); rg.lineTo(x + 7, BODY_T - 18); rg.lineTo(x, BODY_T - 6); rg.closePath(); rg.fill();
      rg.lineWidth = 1;
    }

    // handle bars + their values (pushed apart when the handles are close)
    drawHandle(x0, dragging === 'min');
    drawHandle(x1, dragging === 'max');
    rg.font = 'bold 12px Arial'; rg.fillStyle = '#f5f5f0';
    const close = x1 - x0 < 70;
    rg.textAlign = close ? 'right' : 'center';
    rg.fillText(fmtM(cfg.minCm), close ? x0 + 6 : Math.max(30, x0), 16);
    rg.textAlign = close ? 'left' : 'center';
    rg.fillText(fmtM(cfg.maxCm), close ? x1 - 6 : Math.min(W - 30, x1), 16);
    rg.textAlign = 'start';
  }

  function renderRange() {
    minInput.value = cfg.minCm;
    maxInput.value = cfg.maxCm;
    drawRuler();
  }
  function clampRange(which) {
    const lim = cfg.minLimitCm, top = cfg.rangeCm, gap = cfg.minSpanCm || 10;   // Movement mode: 1 m
    cfg.minCm = Math.max(lim, Math.min(top - gap, Math.round(cfg.minCm)));
    cfg.maxCm = Math.max(lim + gap, Math.min(top, Math.round(cfg.maxCm)));
    if (cfg.maxCm - cfg.minCm < gap) {
      if (which === 'min') cfg.maxCm = cfg.minCm + gap; else cfg.minCm = cfg.maxCm - gap;
    }
  }
  let rangePushT = null;
  function pushRange(delay) {
    clearTimeout(rangePushT);
    rangePushT = setTimeout(() => postJson({ minCm:cfg.minCm, maxCm:cfg.maxCm })
      .then(d => { cfg.minCm = d.minCm; cfg.maxCm = d.maxCm; renderRange(); }).catch(() => {}), delay);
  }
  function setMin(v) { cfg.minCm = v; clampRange('min'); renderRange(); pushRange(250); }
  function setMax(v) { cfg.maxCm = v; clampRange('max'); renderRange(); pushRange(250); }

  function pointerX(e) { return e.clientX - rc.getBoundingClientRect().left; }
  rc.addEventListener('pointerdown', e => {
    const x = pointerX(e), x0 = xOf(cfg.minCm), x1 = xOf(cfg.maxCm);
    if (Math.abs(x0 - x1) < 2) dragging = x < x0 ? 'min' : 'max';   // stacked handles: pick by side
    else dragging = Math.abs(x - x0) <= Math.abs(x - x1) ? 'min' : 'max';
    rc.setPointerCapture(e.pointerId);
    moveDrag(e);
  });
  function moveDrag(e) {
    if (!dragging) return;
    const v = Math.round(cmOf(pointerX(e)) / 5) * 5;       // 5 cm steps while dragging
    if (dragging === 'min') cfg.minCm = v; else cfg.maxCm = v;
    clampRange(dragging);
    renderRange();
  }
  rc.addEventListener('pointermove', moveDrag);
  const endDrag = () => { if (!dragging) return; dragging = null; drawRuler(); pushRange(0); };
  rc.addEventListener('pointerup', endDrag);
  rc.addEventListener('pointercancel', endDrag);

  // Long press speeds up twice: faster repeats AND bigger steps (5 -> 10 -> 25 -> 50 cm),
  // snapped to round numbers so the value stays tidy.
  const holdStep = tick => tick < 8 ? 5 : tick < 18 ? 10 : tick < 30 ? 25 : 50;
  const stepCm = (v, dir, tick) => {
    const st = holdStep(tick);
    return dir > 0 ? Math.floor(v / st) * st + st : Math.ceil(v / st) * st - st;
  };
  attachHold($('minDown'), t => setMin(stepCm(cfg.minCm, -1, t)));
  attachHold($('minUp'),   t => setMin(stepCm(cfg.minCm,  1, t)));
  attachHold($('maxDown'), t => setMax(stepCm(cfg.maxCm, -1, t)));
  attachHold($('maxUp'),   t => setMax(stepCm(cfg.maxCm,  1, t)));
  minInput.addEventListener('change', () => { const v = parseInt(minInput.value, 10); if (!isNaN(v)) setMin(v); else renderRange(); });
  maxInput.addEventListener('change', () => { const v = parseInt(maxInput.value, 10); if (!isNaN(v)) setMax(v); else renderRange(); });

  // ---------- Trigger mode + movement filter ----------
  const MOVE_STEPS = [20, 40, 60, 80, 100];
  const modeMoveBtn = $('modeMove'), modeBarrierBtn = $('modeBarrier');
  let moveT = null;
  function renderMode() {
    const mv = cfg.mode === 0;
    modeMoveBtn.classList.toggle('active', mv);
    modeBarrierBtn.classList.toggle('active', !mv);
    $('moveFilterRow').classList.toggle('dim', !mv);
    $('moveNote').classList.toggle('dim', !mv);
    $('slFilter').classList.toggle('dim', mv);
    $('filterNote').classList.toggle('dim', mv);
    $('moveValue').value = cfg.moveFilter + ' cm';
    $('modeNote').textContent = mv
      ? 'Movement: only something coming towards the sensor inside the range triggers, like the radar. Crossing the beam sideways is ignored. Min and Max stay at least 1 m apart.'
      : 'Light-Barrier: anything inside the range triggers, moving or not.';
  }
  function setMode(m) {
    if (m === cfg.mode) return;
    cfg.mode = m;
    cfg.minSpanCm = m === 0 ? 100 : 10;
    clampRange('min');
    renderMode(); renderRange();
    postJson({ mode:m }).then(d => {
      cfg.minCm = d.minCm; cfg.maxCm = d.maxCm; cfg.minSpanCm = d.minSpanCm; renderRange();
    }).catch(() => {});
  }
  modeMoveBtn.addEventListener('click', () => setMode(0));
  modeBarrierBtn.addEventListener('click', () => setMode(1));
  function stepMove(dir) {
    if (cfg.mode !== 0) return;
    cfg.moveFilter = MOVE_STEPS[Math.max(0, Math.min(MOVE_STEPS.length - 1, closestIndexIn(MOVE_STEPS, cfg.moveFilter) + dir))];
    renderMode();
    clearTimeout(moveT);
    moveT = setTimeout(() => post({ moveFilter:cfg.moveFilter }).catch(() => {}), 250);
  }
  $('moveDown').addEventListener('click', () => stepMove(-1));
  $('moveUp').addEventListener('click', () => stepMove(1));

  // ---------- Sensor sensitivity: 5 presets + 3 sliders ----------
  // Sensitivity uses the camera's ISO scale: higher ISO = lower Min Signal (TF-Luna amplitude).
  const ISO = [[100, 200], [200, 150], [400, 100], [800, 50], [1600, 30]];      // [ISO, Min Signal]
  const PRESETS = {
    bright: { rate:100, amp:100, filter:2, name:'Bright', note:'Sunlit or light background. The default.' },
    dark:   { rate:250, amp:30,  filter:1, name:'Dark',   note:'Shaded or dark background, dusk, indoors.' },
    far:    { rate:100, amp:50,  filter:3, name:'Far',    note:'Dark subjects near the end of the range.' },
    macro:  { rate:250, amp:50,  filter:1, name:'Macro',  note:'Insects, small birds, up close. Light-Barrier mode, backdrop behind, tight range.' },
    rain:   { rate:100, amp:150, filter:4, name:'Rain',   note:'Rain, spray or dust. Very fast subjects may be missed.' }
  };
  const reduceMotion = window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  const easeHand = t => t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2;   // speed up, then slow down

  // A slider with fixed stops (left to right). Drag, tap or arrow keys pick a stop;
  // animateTo() moves the thumb smoothly like a hand would.
  function makeSlider(id, stops, label, fmt, onPick) {
    const root = $(id), track = root.querySelector('.sl-track'), thumb = root.querySelector('.sl-thumb');
    const fill = root.querySelector('.sl-fill'), val = root.querySelector('.sl-val'), labels = root.querySelector('.sl-labels');
    const last = stops.length - 1;
    let pos = 0, idx = 0, anim = null;
    const lblEls = stops.map((v, i) => {
      const t = document.createElement('div'); t.className = 'sl-tick'; t.style.left = (i / last * 100) + '%'; track.appendChild(t);
      const l = document.createElement('span'); l.textContent = label(v); l.style.left = (i / last * 100) + '%'; labels.appendChild(l);
      return l;
    });
    track.setAttribute('aria-valuemin', 0); track.setAttribute('aria-valuemax', last);
    function place(p) {
      pos = p;
      const pct = p / last * 100;
      thumb.style.left = pct + '%'; fill.style.width = pct + '%';
      const i = Math.round(p);
      val.innerHTML = fmt(stops[i]);
      lblEls.forEach((l, k) => l.classList.toggle('on', k === i));
      track.setAttribute('aria-valuenow', i); track.setAttribute('aria-valuetext', val.textContent);
    }
    function stop() { if (anim) { cancelAnimationFrame(anim); anim = null; track.classList.remove('grabbed'); } }
    function set(i) { stop(); idx = i; place(i); }
    function pick(i) { set(i); onPick(stops[i]); }
    function animateTo(i) {
      stop(); idx = i;
      if (reduceMotion || Math.abs(pos - i) < 0.01) { place(i); return; }
      const from = pos, dist = Math.abs(i - from) / last;
      const dur = 650 + 900 * dist;                    // longer moves take longer, like a hand
      let t0 = null;
      track.classList.add('grabbed');
      const step = ts => {
        if (t0 === null) t0 = ts;
        const t = Math.min(1, (ts - t0) / dur);
        place(from + (i - from) * easeHand(t));
        if (t < 1) anim = requestAnimationFrame(step);
        else { anim = null; place(i); setTimeout(() => track.classList.remove('grabbed'), 120); }
      };
      anim = requestAnimationFrame(step);
    }
    const posFromEvent = e => {
      const r = track.getBoundingClientRect();
      return Math.max(0, Math.min(last, (e.clientX - r.left) / r.width * last));
    };
    let dragging = false;
    track.addEventListener('pointerdown', e => {
      stop(); dragging = true; track.setPointerCapture(e.pointerId); track.classList.add('grabbed'); place(posFromEvent(e));
    });
    track.addEventListener('pointermove', e => { if (dragging) place(posFromEvent(e)); });
    const end = () => {
      if (!dragging) return;
      dragging = false; track.classList.remove('grabbed');
      const i = Math.round(pos);
      animateTo(i); onPick(stops[i]);                  // snap to the nearest stop
    };
    track.addEventListener('pointerup', end);
    track.addEventListener('pointercancel', end);
    track.addEventListener('keydown', e => {
      let i = idx;
      if (e.key === 'ArrowRight' || e.key === 'ArrowUp') i = Math.min(last, idx + 1);
      else if (e.key === 'ArrowLeft' || e.key === 'ArrowDown') i = Math.max(0, idx - 1);
      else if (e.key === 'Home') i = 0; else if (e.key === 'End') i = last;
      else return;
      e.preventDefault(); pick(i);
    });
    const indexOf = v => { let b = 0; stops.forEach((s, k) => { if (Math.abs(s - v) < Math.abs(stops[b] - v)) b = k; }); return b; };
    return { set: v => set(indexOf(v)), animateTo: v => animateTo(indexOf(v)), refresh: () => place(pos), indexOf };
  }

  const isoOf = amp => (ISO.find(x => x[1] === amp) || ISO[2])[0];
  const readingMs = () => Math.round(1000 / cfg.lunaRate);
  let sensT = null;
  function pushSensitivity() {
    clearTimeout(sensT);
    sensT = setTimeout(() => post({ lunaRate:cfg.lunaRate, lunaMinAmp:cfg.lunaMinAmp, filter:cfg.filter }).catch(() => {}), 250);
  }
  const slRate = makeSlider('slRate', [100, 125, 166, 250], v => v, v => v + ' <small>Hz</small>',
    v => { cfg.lunaRate = v; slFilter.refresh(); renderPreset(); pushSensitivity(); });
  const slIso = makeSlider('slIso', ISO.map(x => x[1]), v => isoOf(v), v => 'ISO ' + isoOf(v),
    v => { cfg.lunaMinAmp = v; renderPreset(); pushSensitivity(); });
  const slFilter = makeSlider('slFilter', [8, 7, 6, 5, 4, 3, 2, 1], v => v,
    v => v + (v === 1 ? ' reading' : ' readings') + ' <small>' + (v * readingMs()) + ' ms</small>',
    v => { cfg.filter = v; renderPreset(); pushSensitivity(); });

  const presetBtns = document.querySelectorAll('.preset-row button');
  function matchingPreset() {
    return Object.keys(PRESETS).find(k => {
      const p = PRESETS[k];
      return p.rate === cfg.lunaRate && p.amp === cfg.lunaMinAmp && p.filter === cfg.filter;
    });
  }
  function renderPreset() {
    const k = matchingPreset();
    presetBtns.forEach(b => b.classList.toggle('active', b.dataset.preset === k));
    $('presetNote').innerHTML = k ? PRESETS[k].note : '<b>Custom</b>: your own combination.';
  }
  // set the sliders straight to the device's values (page load, reload)
  function renderSensitivity() {
    slRate.set(cfg.lunaRate); slIso.set(cfg.lunaMinAmp); slFilter.set(cfg.filter);
    renderPreset();
  }
  presetBtns.forEach(b => b.addEventListener('click', () => {
    const p = PRESETS[b.dataset.preset];
    cfg.lunaRate = p.rate; cfg.lunaMinAmp = p.amp; cfg.filter = p.filter;
    slRate.animateTo(p.rate); slIso.animateTo(p.amp); slFilter.animateTo(p.filter);   // all three at once
    renderPreset();
    clearTimeout(sensT);
    post({ lunaRate:p.rate, lunaMinAmp:p.amp, filter:p.filter }).catch(() => {});
  }));

  // ---------- live distance (one request at a time, ~8 per second) ----------
  const liveEl = $('live'), distBig = $('distBig'), distState = $('distState'), rulerCard = $('rulerCard');
  let lastLiveOk = 0;
  function renderLive() {
    if (!live.ok) { distBig.textContent = '---'; distState.textContent = 'NO SENSOR DATA -- CHECK TF-LUNA TX -> IO27'; distState.className = 'state'; }
    else if (!live.v) { distBig.textContent = '---'; distState.textContent = 'NO TARGET'; distState.className = 'state'; }
    else {
      distBig.textContent = live.d;
      distState.textContent = live.in ? 'IN RANGE' : (live.d < cfg.minCm ? 'CLOSER THAN RANGE' : (live.d > cfg.maxCm ? 'BEYOND RANGE' : 'ENTERING'));
      distState.className = live.in ? 'state in' : 'state';
    }
    if (!dragging) drawRuler();
  }
  function pollLive() {
    if (document.hidden) { setTimeout(pollLive, 500); return; }
    fetch('/api/live').then(r => r.json()).then(d => {
      if (live.n !== null && d.n !== live.n) {
        rulerCard.classList.add('flash');
        setTimeout(() => rulerCard.classList.remove('flash'), 300);
      }
      live = d; lastLiveOk = performance.now();
      renderBattery(d);
      if (d.m !== undefined && d.m !== cfg.mode) loadSettings();   // switched with the BOOT button
      pairSenderBtn.classList.toggle('on-state', !!d.pw);
      if (pairWindowWasOpen && !d.pw) { loadSettings(); if (devicesPanelOpen) loadDevices(); }   // pairing window just closed
      pairWindowWasOpen = !!d.pw;
      renderLive();
    }).catch(() => {}).then(() => setTimeout(pollLive, 120));
  }
  setInterval(() => {
    const ok = performance.now() - lastLiveOk < 1500;
    liveEl.textContent = !ok ? 'no connection' :
      (live.ok ? `${live.rps} readings/s · signal ${live.a} · ${live.t}°C · ${live.n} sent` + (live.r ? ` · ${live.r} received` : '') : 'no TF-Luna data');
    liveEl.className = ok && live.ok ? 'live ok' : 'live';
  }, 1000);

  // =============================== CAMERA OUTPUT ===============================
  const durInput = $('durValue');
  let triggerDuration = 250;
  let triggerDelay = 0, extraCount = 1, extraEnabled = false, extraGapValue = 50;
  const extraGapPresets = [50, 100, 150, 250, 400];
  const extraCountInput = $('extraCountValue'), extraGapInput = $('extraGapValue');
  const extraToggleBtn = $('extraToggleBtn'), extraCountNote = $('extraCountNote');
  const extraCountDownBtn = $('extraCountDown'), extraCountUpBtn = $('extraCountUp');
  const triggerDelayInput = $('triggerDelayValue');
  let durT = null, delayT = null, xcT = null, xgT = null;

  const scheduleDur = () => { clearTimeout(durT); durT = setTimeout(() => post({ triggerDuration }), 250); };
  const scheduleDelay = () => { clearTimeout(delayT); delayT = setTimeout(() => post({ triggerDelay }), 250); };
  const scheduleXc = () => { clearTimeout(xcT); xcT = setTimeout(() => post({ extraCount }), 250); };
  const scheduleXg = () => { clearTimeout(xgT); xgT = setTimeout(() => post({ extraGap:extraGapValue }), 250); };

  durInput.addEventListener('input', () => { const v = parseInt(durInput.value, 10); if (!isNaN(v)) { triggerDuration = v; scheduleDur(); } });
  durInput.addEventListener('change', () => { triggerDuration = Math.max(10, Math.min(10000, triggerDuration || 10)); showMs(durInput, triggerDuration); scheduleDur(); });
  attachHold($('durDown'), () => { triggerDuration = Math.max(10, triggerDuration - 50); showMs(durInput, triggerDuration); scheduleDur(); });
  attachHold($('durUp'),   () => { triggerDuration = Math.min(10000, triggerDuration + 50); showMs(durInput, triggerDuration); scheduleDur(); });

  triggerDelayInput.addEventListener('input', () => { const v = parseInt(triggerDelayInput.value, 10); if (!isNaN(v) && v >= 0) { triggerDelay = v; scheduleDelay(); } });
  attachHold($('triggerDelayDown'), () => { triggerDelay = Math.max(0, triggerDelay - 50); showMs(triggerDelayInput, triggerDelay); scheduleDelay(); });
  attachHold($('triggerDelayUp'),   () => { triggerDelay = Math.min(10000, triggerDelay + 50); showMs(triggerDelayInput, triggerDelay); scheduleDelay(); });

  extraCountInput.addEventListener('input', () => { const v = parseInt(extraCountInput.value, 10); if (!isNaN(v) && v >= 0) { extraCount = v; scheduleXc(); } });
  attachHold(extraCountDownBtn, () => { extraCount = Math.max(0, Math.min(10, extraCount - 1)); extraCountInput.value = extraCount; scheduleXc(); });
  attachHold(extraCountUpBtn,   () => { extraCount = Math.max(0, Math.min(10, extraCount + 1)); extraCountInput.value = extraCount; scheduleXc(); });

  extraGapInput.addEventListener('input', () => { const v = parseInt(extraGapInput.value, 10); if (!isNaN(v)) { extraGapValue = v; scheduleXg(); } });
  attachHold($('extraGapDown'), () => { extraGapValue = extraGapPresets[Math.max(0, closestIndexIn(extraGapPresets, extraGapValue) - 1)]; showMs(extraGapInput, extraGapValue); scheduleXg(); });
  attachHold($('extraGapUp'),   () => { extraGapValue = extraGapPresets[Math.min(extraGapPresets.length - 1, closestIndexIn(extraGapPresets, extraGapValue) + 1)]; showMs(extraGapInput, extraGapValue); scheduleXg(); });

  extraToggleBtn.addEventListener('click', () => {
    extraEnabled = !extraEnabled;
    extraToggleBtn.classList.toggle('active', extraEnabled);
    postJson({ extraEnabled: extraEnabled ? 1 : 0 }).then(updateExtra).catch(() => {});
  });

  function updateExtra(data) {
    extraEnabled = !!data.extraEnabled;
    extraToggleBtn.classList.toggle('active', extraEnabled);
    extraCount = data.extraCount;
    extraCountInput.value = extraCount;
    extraCountNote.textContent = 'tap Extra Triggers to switch them on (steel-blue) or off. Extra shutter releases after every trigger (arrows up to 10, type any number). Not saved: off after power-up.';
  }

  // ---------- Interval / Delayed Start / Timer (nothing here is saved) ----------
  const INTERVAL_STEPS = [300, 500, 750, 1000, 1500, 2000, 2500, 3000, 4000, 5000];
  const intervalBtn = $('intervalToggleBtn'), intervalPanel = $('intervalPanel'), intervalHint = $('intervalHint');
  const intervalInput = $('intervalValue'), intervalNote = $('intervalNote');
  const startDelayBtn = $('startDelayToggleBtn'), startDelayInput = $('startDelayValue');
  const startDelayDownBtn = $('startDelayDown'), startDelayUpBtn = $('startDelayUp'), startDelayNote = $('startDelayNote');
  const timerBtn = $('timerToggleBtn'), timerInput = $('timerValue');
  const timerDownBtn = $('timerDown'), timerUpBtn = $('timerUp'), timerNote = $('timerNote');

  let intervalMsVal = 2000, intervalOn = true, intervalForced = false, inRange = false;
  let timerStaged = 5, startDelayStaged = 5, timerRunning = false, startDelayRunning = false;
  let intervalPanelOpen = false, intervalPollStarted = false, lastIntervalCounter = null, intervalPushT = null;

  function renderInterval() {
    intervalBtn.classList.toggle('off', !intervalOn);
    intervalBtn.classList.toggle('forced', intervalForced);
    intervalBtn.textContent = !intervalOn ? 'Interval' : intervalForced ? 'Interval: Forced'
      : inRange ? 'Interval: Shooting' : 'Interval: Stand-by';
    intervalNote.textContent = intervalForced
      ? 'FORCED: fires all the time, with or without an object in range. Hold the Interval button to switch this off.'
      : 'On after power-on with 2000 ms (not saved). Stand-by until something is inside the range, then repeats the trigger while it stays there. Hold the Interval button 0.7s to force it to run all the time. Tap Interval to switch it on or off.';
  }
  function setTimerUI(running) {
    timerRunning = running;
    timerDownBtn.disabled = running; timerUpBtn.disabled = running;
    timerBtn.classList.toggle('on-state', running);
    if (!running) { timerInput.value = timerStaged + ' min'; timerNote.textContent = 'press Timer to switch the interval off after the countdown'; }
  }
  function setStartDelayUI(running) {
    startDelayRunning = running;
    startDelayDownBtn.disabled = running; startDelayUpBtn.disabled = running;
    startDelayBtn.classList.toggle('on-state', running);
    if (!running) { startDelayInput.value = startDelayStaged + ' min'; startDelayNote.textContent = 'press Delayed Start to start the interval after the countdown'; }
  }
  function openIntervalPanel() {
    intervalPanelOpen = true;
    intervalPanel.style.display = 'block';
    intervalHint.style.display = 'none';
    if (!intervalPollStarted) { intervalPollStarted = true; setInterval(pollIntervalStatus, 500); }
  }
  function applyIntervalConfig(data) {
    intervalMsVal = data.interval; showMs(intervalInput, intervalMsVal);
    intervalOn = !!data.intervalEnabled; intervalForced = !!data.intervalForced; inRange = !!data.present;
    renderInterval();
    setTimerUI(!!data.timerActive);
    if (data.timerActive) timerInput.value = formatMMSS(data.timerRemainingSeconds);
    setStartDelayUI(!!data.startDelayActive);
    if (data.startDelayActive) startDelayInput.value = formatMMSS(data.startDelayRemainingSeconds);
    if (data.intervalEnabled || data.intervalForced || data.timerActive || data.startDelayActive) openIntervalPanel();
  }
  function pollIntervalStatus() {
    fetch('/api/interval-status').then(r => r.json()).then(data => {
      if (lastIntervalCounter !== null && data.counter !== lastIntervalCounter) {
        intervalBtn.classList.add('pulse');
        setTimeout(() => intervalBtn.classList.remove('pulse'), 150);
      }
      lastIntervalCounter = data.counter;
      intervalOn = data.enabled; intervalForced = data.forced; inRange = !!data.present;
      renderInterval();
      if (data.timerActive) {
        timerRunning = true; timerBtn.classList.add('on-state');
        timerDownBtn.disabled = true; timerUpBtn.disabled = true;
        timerInput.value = formatMMSS(data.timerRemainingSeconds);
      } else if (timerRunning) setTimerUI(false);
      if (data.startDelayActive) {
        startDelayRunning = true; startDelayBtn.classList.add('on-state');
        startDelayDownBtn.disabled = true; startDelayUpBtn.disabled = true;
        startDelayInput.value = formatMMSS(data.startDelayRemainingSeconds);
      } else if (startDelayRunning) setStartDelayUI(false);
      $('pairedCount').textContent = data.pairedReceiverCount;
    }).catch(() => {});
  }
  const pushIntervalMs = () => post({ interval:intervalMsVal });
  intervalInput.addEventListener('input', () => { const v = parseInt(intervalInput.value, 10); if (!isNaN(v)) { intervalMsVal = v; clearTimeout(intervalPushT); intervalPushT = setTimeout(pushIntervalMs, 250); } });
  intervalInput.addEventListener('change', () => { intervalMsVal = Math.min(10000, Math.max(300, intervalMsVal || 300)); showMs(intervalInput, intervalMsVal); pushIntervalMs(); });
  $('intervalDown').addEventListener('click', () => { intervalMsVal = INTERVAL_STEPS[Math.max(0, closestIndexIn(INTERVAL_STEPS, intervalMsVal) - 1)]; showMs(intervalInput, intervalMsVal); pushIntervalMs(); });
  $('intervalUp').addEventListener('click', () => { intervalMsVal = INTERVAL_STEPS[Math.min(INTERVAL_STEPS.length - 1, closestIndexIn(INTERVAL_STEPS, intervalMsVal) + 1)]; showMs(intervalInput, intervalMsVal); pushIntervalMs(); });

  // Long press (0.7 s) toggles FORCED, like the LS sender. First plain tap only
  // reveals the panel (like the receiver); every tap after that switches it on/off.
  let intervalHoldT = null, intervalLongFired = false;
  intervalBtn.addEventListener('pointerdown', () => {
    intervalLongFired = false;
    clearTimeout(intervalHoldT);
    intervalHoldT = setTimeout(() => {
      intervalLongFired = true;
      intervalForced = !intervalForced;
      renderInterval();
      openIntervalPanel();
      post({ intervalForced: intervalForced ? 1 : 0 });
    }, 700);
  });
  ['pointerup', 'pointerleave', 'pointercancel'].forEach(e => intervalBtn.addEventListener(e, () => clearTimeout(intervalHoldT)));
  intervalBtn.addEventListener('contextmenu', e => e.preventDefault());
  intervalBtn.addEventListener('click', () => {
    if (intervalLongFired) { intervalLongFired = false; return; }
    if (!intervalPanelOpen) { openIntervalPanel(); return; }
    intervalOn = !intervalOn;
    renderInterval();
    post({ intervalEnabled: intervalOn ? 1 : 0 });
  });

  timerDownBtn.addEventListener('click', () => { if (!timerRunning) { timerStaged = Math.max(0, timerStaged - 1); timerInput.value = timerStaged + ' min'; } });
  timerUpBtn.addEventListener('click',   () => { if (!timerRunning) { timerStaged = Math.min(10000, timerStaged + 1); timerInput.value = timerStaged + ' min'; } });
  startDelayDownBtn.addEventListener('click', () => { if (!startDelayRunning) { startDelayStaged = Math.max(0, startDelayStaged - 1); startDelayInput.value = startDelayStaged + ' min'; } });
  startDelayUpBtn.addEventListener('click',   () => { if (!startDelayRunning) { startDelayStaged = Math.min(10000, startDelayStaged + 1); startDelayInput.value = startDelayStaged + ' min'; } });
  timerBtn.addEventListener('click', () => {
    if (!timerRunning) { post({ timerMinutes:Math.max(1, timerStaged) }); setTimerUI(true); }
    else               { post({ timerMinutes:0 }); setTimerUI(false); }
  });
  startDelayBtn.addEventListener('click', () => {
    if (!startDelayRunning) { post({ startDelayMinutes:Math.max(1, startDelayStaged) }); setStartDelayUI(true); }
    else                    { post({ startDelayMinutes:0 }); setStartDelayUI(false); }
  });

  // TEST: a full signal -- this device's camera (duration / extras / delay apply) + all paired receivers
  $('triggerBtn').addEventListener('click', () => { fetch('/api/trigger', { method:'POST' }).catch(() => {}); });

  // ---------- Save Settings ----------
  const saveBtn = $('saveBtn'), saveNote = $('saveNote');
  function renderUnsaved(unsaved) {
    saveBtn.classList.toggle('unsaved', !!unsaved);
    saveNote.textContent = unsaved ? 'unsaved changes -- they are lost at the next power cycle' : 'all settings are saved';
  }
  saveBtn.addEventListener('click', () => {
    postJson({}).catch(() => {});        // let any pending arrow changes go out first
    setTimeout(() => fetch('/api/save-settings', { method:'POST' }).then(r => r.json()).then(d => {
      renderUnsaved(d.unsaved);
      saveBtn.classList.add('saved'); saveBtn.textContent = 'Saved';
      setTimeout(() => { saveBtn.classList.remove('saved'); saveBtn.textContent = 'Save Settings'; }, 1200);
    }).catch(() => {}), 400);
  });
  // every POST answer carries the unsaved flag
  const _post = post;
  post = params => _post(params).then(r => { r.clone().json().then(d => { if (d && 'unsaved' in d) renderUnsaved(d.unsaved); }).catch(() => {}); return r; });

  armConfirm($('resetDefaultsBtn'), 'Reset to Default', () => {
    fetch('/api/reset-defaults', { method:'POST' }).then(r => r.json()).then(applyConfig).catch(() => {});
  });

  // =============================== RADIO / APP ===============================
  const powerLabels = ["2dBm","5dBm","7dBm","8.5dBm","11dBm","13dBm","15dBm","17dBm","18.5dBm","19dBm","19.5dBm"];
  let txPowerIndex = 8;
  const powerInput = $('powerValue');
  $('powerDown').addEventListener('click', () => { txPowerIndex = Math.max(0, txPowerIndex - 1); powerInput.value = powerLabels[txPowerIndex]; post({ txPowerIndex }); });
  $('powerUp').addEventListener('click',   () => { txPowerIndex = Math.min(10, txPowerIndex + 1); powerInput.value = powerLabels[txPowerIndex]; post({ txPowerIndex }); });

  const keepAliveBtn = $('keepAliveBtn');
  let keepAlive = false;
  keepAliveBtn.addEventListener('click', () => {
    keepAlive = !keepAlive;
    keepAliveBtn.classList.toggle('alive-on', keepAlive);
    post({ keepAlive: keepAlive ? 1 : 0 });
  });
  $('shutdownBtn').addEventListener('click', () => { fetch('/api/shutdown-webapp', { method:'POST' }).catch(() => {}); });

  const passwordInput = $('passwordValue'), statusNote = $('statusNote');
  let pwT = null;
  function refreshStatus(data) {
    if (data.passwordSet) statusNote.textContent = data.protectedNow ? 'Network is password-protected.' : 'Password saved. The network becomes protected once nobody is connected.';
    else statusNote.textContent = data.protectedNow ? 'Password removed. The network opens once nobody is connected.' : 'Network is open.';
  }
  passwordInput.addEventListener('input', () => { clearTimeout(pwT); pwT = setTimeout(() => postJson({ password:passwordInput.value }).then(refreshStatus).catch(() => {}), 600); });

  const ssidInput = $('ssidValue'), ssidStatusNote = $('ssidStatusNote');
  $('ssidApplyBtn').addEventListener('click', () => {
    ssidStatusNote.textContent = 'Applying... this will disconnect you from the network.';
    postJson({ ssid:ssidInput.value }).then(d => {
      ssidInput.value = ''; ssidInput.placeholder = d.ssid;
      ssidStatusNote.textContent = 'Network renamed to "' + d.ssid + '". Reconnect to it in your WiFi settings.';
    }).catch(() => { ssidStatusNote.textContent = 'You were disconnected -- reconnect to the new name in your WiFi settings.'; });
  });

  const pairSenderBtn = $('pairSenderBtn');
  let pairWindowWasOpen = false;
  pairSenderBtn.addEventListener('click', () => {
    pairSenderBtn.classList.add('on-state');
    fetch('/api/pair-sender', { method:'POST' }).catch(() => {});
  });
  armConfirm($('clearSendersBtn'), 'Clear Senders', () => {
    fetch('/api/clear-senders', { method:'POST' }).then(() => {
      $('pairedSenderCount').textContent = '0';
      if (devicesPanelOpen) setTimeout(loadDevices, 300);     // the clear runs on the device's next loop
    }).catch(() => {});
  });

  const pairBtn = $('pairBtn');
  pairBtn.addEventListener('click', () => {
    fetch('/api/pair', { method:'POST' }).catch(() => {});
    pairBtn.classList.add('on-state');
    setTimeout(() => { pairBtn.classList.remove('on-state'); loadSettings(); if (devicesPanelOpen) loadDevices(); }, 11000);
  });
  armConfirm($('clearPairingsBtn'), 'Clear Triggers', () => {
    fetch('/api/clear-pairings', { method:'POST' }).then(() => {
      $('pairedCount').textContent = '0';
      if (devicesPanelOpen) setTimeout(loadDevices, 300);
    }).catch(() => {});
  });

  // ---------- Paired Devices: senders, then triggers (same as the trigger / Octocam BT) ----------
  const devicesBtn = $('devicesToggleBtn'), devicesPanel = $('devicesPanel'), devicesHint = $('devicesHint');
  const senderList = $('senderList'), targetList = $('targetList');
  let devicesPanelOpen = false, devicesPollTimer = null, deviceTapBusy = false;
  let forgetArmedKey = null, forgetArmTimer = null;   // a Forget waiting for its confirming tap

  function deviceLabel(d, word) {
    return d.name ? d.name : (word + ' ' + d.mac.slice(-5));   // paired before names existed
  }

  function renderGroup(listEl, items, kind) {
    const word = kind === 'sender' ? 'Sender' : 'Trigger';
    listEl.innerHTML = '';
    if (items.length === 0) {
      const empty = document.createElement('div');
      empty.className = 'unit-note';
      empty.style.marginTop = '0';
      empty.textContent = kind === 'sender'
        ? 'No paired senders yet. Tap Pair Senders, then switch the sender on.'
        : 'No paired triggers yet. Open the trigger\'s pairing window, then tap Pairing Signal.';
      listEl.appendChild(empty);
      return;
    }
    const counts = {};
    items.forEach(d => { const l = deviceLabel(d, word); counts[l] = (counts[l] || 0) + 1; });
    items.forEach(d => {
      const b = document.createElement('button');
      b.type = 'button';
      b.className = 'sender-btn' + (d.active ? ' active' : '');
      b.textContent = deviceLabel(d, word);
      if (counts[deviceLabel(d, word)] > 1) {           // same name twice: show the MAC underneath
        const m = document.createElement('span');
        m.className = 'sender-mac';
        m.textContent = d.mac;
        b.appendChild(m);
      }
      b.addEventListener('click', () => {
        if (deviceTapBusy) return;
        deviceTapBusy = true;
        const on = !b.classList.contains('active');
        b.classList.toggle('active', on);
        const body = new URLSearchParams();
        body.set('mac', d.mac);
        body.set('active', on ? 1 : 0);
        fetch(kind === 'sender' ? '/api/sender-active' : '/api/target-active', { method:'POST', body })
          .then(r => r.json()).then(renderDevices)
          .catch(() => { b.classList.toggle('active', !on); })
          .finally(() => { deviceTapBusy = false; });
      });

      const key = kind + d.mac;
      const f = document.createElement('button');
      f.type = 'button';
      f.className = 'forget-btn';
      f.textContent = 'Forget';
      f.addEventListener('click', () => {
        if (forgetArmedKey !== key) {
          document.querySelectorAll('.forget-btn.armed').forEach(x => { x.classList.remove('armed'); x.textContent = 'Forget'; });
          forgetArmedKey = key;
          f.classList.add('armed');
          f.textContent = 'Confirm?';
          clearTimeout(forgetArmTimer);
          forgetArmTimer = setTimeout(() => {
            forgetArmedKey = null; f.classList.remove('armed'); f.textContent = 'Forget';
          }, 5000);
          return;
        }
        clearTimeout(forgetArmTimer);
        forgetArmedKey = null;
        deviceTapBusy = true;
        const body = new URLSearchParams();
        body.set('mac', d.mac);
        fetch(kind === 'sender' ? '/api/sender-forget' : '/api/target-forget', { method:'POST', body })
          .then(r => r.json()).then(renderDevices)
          .catch(() => {})
          .finally(() => { deviceTapBusy = false; });
      });

      const row = document.createElement('div');
      row.className = 'sender-row';
      row.appendChild(b);
      row.appendChild(f);
      listEl.appendChild(row);
    });
  }

  function renderDevices(data) {
    const senders = (data && data.senders) ? data.senders : [];
    const triggers = (data && data.triggers) ? data.triggers : [];
    $('pairedSenderCount').textContent = senders.length;
    $('pairedCount').textContent = triggers.length;
    renderGroup(senderList, senders, 'sender');
    renderGroup(targetList, triggers, 'trigger');
  }

  function loadDevices() {
    if (deviceTapBusy || forgetArmedKey) return;
    fetch('/api/devices').then(r => r.json()).then(renderDevices).catch(() => {});
  }

  // Works like the Interval button: the first tap opens the list, the next one closes it.
  devicesBtn.addEventListener('click', () => {
    devicesPanelOpen = !devicesPanelOpen;
    devicesPanel.style.display = devicesPanelOpen ? 'block' : 'none';
    devicesHint.style.display = devicesPanelOpen ? 'none' : 'block';
    clearInterval(devicesPollTimer);
    if (devicesPanelOpen) {
      loadDevices();
      devicesPollTimer = setInterval(loadDevices, 3000);
    }
  });

  // ---------- histogram ----------
  function renderHistogram(data) {
    const contacts = data.contacts, intervals = data.interval, first = data.first;
    const received = data.received || contacts.map(() => 0);
    const totalMinutes = contacts.length;
    const maxCount = Math.max(1, ...contacts, ...intervals, ...received);
    const scrollDiv = $('histScroll'), canvas = $('histCanvas');
    const visibleWidth = scrollDiv.clientWidth || 300;
    const pxPerMin = Math.max(1, visibleWidth / 60);
    const totalWidth = Math.max(visibleWidth, totalMinutes * pxPerMin);
    const barAreaHeight = 128, xAxisHeight = 20;
    canvas.width = totalWidth; canvas.height = barAreaHeight + xAxisHeight;
    const ctx = canvas.getContext('2d');
    ctx.clearRect(0, 0, totalWidth, barAreaHeight + xAxisHeight);
    ctx.strokeStyle = '#3a3a3a'; ctx.fillStyle = '#888'; ctx.font = '10px Arial';
    for (let i = 0; i < totalMinutes; i++) {
      const serial = first + i;
      if (serial % 60 === 0) {
        const x = i * pxPerMin;
        ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, barAreaHeight); ctx.stroke();
        if (serial > 0) ctx.fillText((serial / 60) + 'h', x + 2, barAreaHeight + 14);
      }
    }
    const barW = Math.max(1, pxPerMin - 1);
    const drawBar = (i, v, color) => {
      if (v <= 0) return;
      const h = (v / maxCount) * barAreaHeight;
      ctx.fillStyle = color; ctx.fillRect(i * pxPerMin, barAreaHeight - h, barW, h);
    };
    for (let i = 0; i < totalMinutes; i++) {          // tallest bar behind, shortest in front
      [[contacts[i], '#e48fa6'], [intervals[i], '#4fd1c5'], [received[i], '#4682b4']]
        .sort((a, b) => b[0] - a[0]).forEach(([v, col]) => drawBar(i, v, col));
    }
    const yAxis = $('histYAxis');
    yAxis.innerHTML = '';
    [1, 0.5, 0].forEach(f => {
      const div = document.createElement('div');
      div.style.top = ((1 - f) * barAreaHeight) + 'px';
      div.textContent = Math.round(maxCount * f);
      yAxis.appendChild(div);
    });
    const wasNearEnd = scrollDiv._lastWidth === undefined ||
      (scrollDiv.scrollLeft + scrollDiv.clientWidth >= scrollDiv._lastWidth - 40);
    scrollDiv._lastWidth = totalWidth;
    if (wasNearEnd) scrollDiv.scrollLeft = totalWidth;
  }
  function refreshHistogram() {
    fetch('/api/histogram').then(r => r.json()).then(d => { $('histCount').textContent = d.total; renderHistogram(d); }).catch(() => {});
  }
  $('resetHistBtn').addEventListener('click', () => { fetch('/api/reset-histogram', { method:'POST' }).then(refreshHistogram).catch(() => {}); });

  // ---------- firmware update (from GitHub, over the home WiFi) ----------
  const HOW = 'The Lidar leaves this network, joins your home WiFi, gets the newest firmware from GitHub and installs it by itself (about a minute). Detection is paused meanwhile. ';
  let updAway = false, homeSsidTouched = false, homePassSet = false;
  function postForm(url, params) {
    return fetch(url, { method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'},
      body:new URLSearchParams(params || {}).toString() });
  }
  function renderUpdate(d) {
    if (d.version) $('verNote').innerHTML = 'Installed: <b>' + d.version + '</b>';
    homePassSet = !!d.wifiPassSet;
    if (!homeSsidTouched && d.wifiSsid !== undefined) $('homeSsid').value = d.wifiSsid;
    $('homePass').placeholder = homePassSet ? 'password saved (type to change)' : 'password (8+ characters)';
    const busy = d.upd === 'joining' || d.upd === 'checking' || d.upd === 'installing';
    $('updBtn').disabled = busy || !homePassSet;
    const n = $('updNote');
    if (updAway) { if (d.upd === 'joining') return; updAway = false; }   // back on this network: show the result
    n.classList.remove('err');
    if (!homePassSet) n.textContent = 'Enter your home WiFi below first.';
    else if (d.upd === 'uptodate') n.innerHTML = 'Up to date (GitHub: <b>' + d.remoteVersion + '</b>).';
    else if (d.upd === 'error') { n.textContent = 'Update failed: ' + (d.updError || 'something went wrong.'); n.classList.add('err'); }
    else if (busy) n.textContent = 'Updating...';
    else n.textContent = HOW;
  }
  $('homeSsid').addEventListener('input', () => { homeSsidTouched = true; });
  $('updBtn').addEventListener('click', () => {
    postForm('/api/update').then(r => r.json()).then(d => {
      if (d.upd === 'joining') {
        updAway = true;
        $('updNote').innerHTML = HOW + 'Your phone loses this network meanwhile: afterwards reconnect to <b>' +
          d.ssid + '</b> and this page shows the result. A new version shows up as "Installed" above.';
      }
      renderUpdate(d);
    }).catch(() => {});
  });
  $('homeSaveBtn').addEventListener('click', () => {
    const ssid = $('homeSsid').value.trim(), pass = $('homePass').value;
    const note = $('homeNote');
    note.classList.remove('err');
    const fail = t => { note.textContent = t; note.classList.add('err'); };
    if (!ssid) return fail('Enter the WiFi name.');
    if (!pass && !homePassSet) return fail('Enter the password (8+ characters).');
    if (pass && pass.length < 8) return fail('The password needs at least 8 characters.');
    postForm('/api/wifi', { ssid:ssid, pass:pass }).then(r => r.json()).then(d => {
      if (d.ok === false) return fail(d.error);
      $('homePass').value = '';
      homeSsidTouched = false;
      note.innerHTML = 'Saved: <b>' + ssid + '</b>. Used only for Update Firmware.';
      renderUpdate(d);
    }).catch(() => fail('Could not save.'));
  });

  // ---------- battery badge (top right) ----------
  function renderBattery(d) {
    if (d.bp === undefined) return;
    const b = $('batBadge');
    const ok = d.bs === 'ok';
    $('batPct').textContent = ok ? d.bp + ' %' : (d.bs === 'usb' ? 'USB' : '--');
    $('batVolt').textContent = ok ? d.bv.toFixed(2) + ' V' : '';
    $('batFill').style.width = (ok ? d.bp : 0) + '%';
    b.classList.toggle('low', ok && d.bp <= 15);
    b.classList.toggle('mid', ok && d.bp > 15 && d.bp <= 40);
    b.title = ok ? 'battery ' + d.bv.toFixed(2) + ' V' : 'no battery reading';
  }

  // =============================== LOAD ===============================
  function applyConfig(data) {
    cfg.minCm = data.minCm; cfg.maxCm = data.maxCm; cfg.rangeCm = data.rangeCm;
    cfg.minLimitCm = data.minLimitCm; cfg.filter = data.filter;
    cfg.lunaRate = data.lunaRate; cfg.lunaMinAmp = data.lunaMinAmp;
    renderSensitivity();
    renderRange();
    cfg.mode = data.mode; cfg.moveFilter = data.moveFilter; cfg.minSpanCm = data.minSpanCm;
    renderMode(); renderRange();
    triggerDuration = data.triggerDuration; showMs(durInput, triggerDuration);
    triggerDelay = data.triggerDelay; showMs(triggerDelayInput, triggerDelay);
    extraGapValue = data.extraGap; showMs(extraGapInput, extraGapValue);
    updateExtra(data);
    applyIntervalConfig(data);
    txPowerIndex = data.txPowerIndex; powerInput.value = powerLabels[txPowerIndex];
    keepAlive = !!data.keepAlive; keepAliveBtn.classList.toggle('alive-on', keepAlive);
    $('pairedCount').textContent = data.pairedReceiverCount;
    $('pairedSenderCount').textContent = data.pairedSenderCount;
    ssidInput.placeholder = data.ssid;
    ssidStatusNote.textContent = 'Current name: ' + data.ssid;
    refreshStatus(data);
    renderUnsaved(data.unsaved);
    renderUpdate(data);
  }
  function loadSettings() { fetch('/api/config').then(r => r.json()).then(applyConfig).catch(() => {}); }
  // light poll so counts stay current without overwriting fields being typed in
  function pollState() { fetch('/api/config').then(r => r.json()).then(d => {
    updateExtra(d);
    $('pairedCount').textContent = d.pairedReceiverCount;
    $('pairedSenderCount').textContent = d.pairedSenderCount;
    renderUnsaved(d.unsaved);
    renderUpdate(d);
    if (d.mode !== cfg.mode) loadSettings();
  }).catch(() => { if (updAway) $('live').textContent = 'Lidar is away updating...'; }); }

  attachMsField(extraGapInput, () => extraGapValue);
  attachMsField(triggerDelayInput, () => triggerDelay);
  attachMsField(intervalInput, () => intervalMsVal);
  attachMsField(durInput, () => triggerDuration);
  showMs(intervalInput, intervalMsVal);
  renderInterval(); setTimerUI(false); setStartDelayUI(false);
  window.addEventListener('resize', () => { rulerSize(); drawRuler(); });
  rulerSize(); renderMode(); renderRange();
  loadSettings();
  refreshHistogram();
  setInterval(refreshHistogram, 5000);
  setInterval(pollState, 3000);
  pollLive();
</script>
</body>
</html>
)HTMLPAGE";