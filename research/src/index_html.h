#pragma once
#include <Arduino.h>
static const char INDEX_HTML[] PROGMEM = R"RAWHTML(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">


        <link href='https://fonts.googleapis.com/css2?family=Rajdhani:wght@400;500;600;700&display=swap' rel='stylesheet'>
        <style>
            :root {
                --primaryfont: 'Rajdhani', sans-serif;
                --primarycolor: #008493;
                --pagebackground: #1b1d21;
                --boxbackground: #202327;
                --orange: #ff8e02;
                --orange_01: #ff7903;
                --orange_02: #ffb101;
                --petrol_01: #008493;
                --petrol_02: #006875;
                --textcolor: #e7e7e7;
                --radius: 4px;
                --pattern: repeating-linear-gradient(315deg, #2f2f2f 0, #2f2f2f 0.5px, var(--boxbackground) 0, var(--boxbackground) 50%);
            }

            html {
                margin: 0;
                padding: 0;
            }

            body {
                font-family: var(--primaryfont), sans-serif;
                text-align: center;
                background: var(--pagebackground);
                color: #ffffff;
                margin: 0;
                padding: 0;
            }


            h2 {
                margin-top: 0px;
                margin-bottom: 0px;
                background-image: var(--pattern);
                background-size: 5px 5px;
                padding: 0.8rem 1rem;
                font-size: 1.2rem;
            }

            section {
                border-radius: var(--radius);
                margin-bottom: 1rem;
                overflow: hidden;
            }
            .option {
                background: var(--boxbackground);
                display: flex;
                justify-content: space-between;
                height: 55px;
                margin-top: 2px;
                margin-bottom: 2px;
            }
            .option > div {
                padding: 18px 21px;
            }
            .option_label {font-weight: 700;}

            .content {padding: 18px; background: var(--boxbackground); margin-bottom: 1px;}
            .content label{margin-bottom: 0.5rem;}

            .fieldwrap{margin-bottom: 18px;}
            .fieldwrap:last-child{margin-bottom: 0px;}
            .fieldwrap span{background:#333; color:#fff; padding:1rem; border-right: 0; border-radius:4px 0 0 4px; font-family:monospace;}
            .fieldrow .fieldwrap select{width:100%; box-sizing:border-box;}

            label {display: block;}


            .grid2 {
                display: flex;
                justify-content: center;
                gap: 2rem;
            }

            .button {
                text-shadow: 1px 1px 3px #00000094;
                width: 100%;
                display: block;
                overflow: hidden;
                margin-bottom: 1rem;
                padding: 15px 2rem;
                border: 0;
                color: #fff;
                letter-spacing: 1px;
                border-radius: var(--radius);
                cursor: pointer;
                line-height: 1rem;
                font-family: var(--primaryfont);
                font-weight: 500;
                font-size: 1rem;
                text-transform: uppercase;
                text-decoration: none;
                background: var(--primarycolor);
                box-sizing: border-box;
            }

            a {text-decoration: none;}

            select{color: #FFF; background: var(--pagebackground); padding: 1rem; border-radius: var(--radius); border: none; width: 100%;}

            input[type="text"], input[type="number"], input[type="time"] {color: #FFF; background: var(--pagebackground); padding: 1rem; border-radius: var(--radius); border: none; width: 100%; box-sizing: border-box;}

            input[type="radio"] {display: none;}

            input[type="time"]{padding: 0.85rem;}
            input[type="time"]::-webkit-calendar-picker-indicator {opacity: 1; filter: invert(1);}

            label.radio {
                position: relative;
                color: var(--textcolor);
                cursor: pointer;
                display: inline-flex;
                align-items: center;
                gap: 0.8em;
                padding: 0.5em 1em;
                border-radius: var(--radius)
                font-weight: 700;
                background: none;
                margin: 0px;
            }

            label.radio:before {
                content: "";
                height: 1.5em;
                width: 1.5em;
                background: var(--textcolor);
                border-radius: 50%;
            }

            input[type="radio"]:checked + label:before {
                height: 0.5em;
                width: 0.5em;
                background: var(--orange);
                border: 8px solid var(--orange);
                }

            input[type="radio"]:checked + label {
                color: var(--orange);
            }

            .fieldrow {display: flex; gap: 1rem; white-space: nowrap;}
            .fieldrow .fieldwrap {flex: 1; display: flex; flex-direction: column;}
            .fieldrow .fieldwrap input {width: 100%; box-sizing: border-box;}

            .orange {
                background: -webkit-linear-gradient(127deg, var(--orange_01), var(--orange_02));
                -webkit-background-clip: text;
                -webkit-text-fill-color: transparent;
                text-shadow: none;
            }


            .sexyCheckbox {
                position: relative;
                display: inline-block;
                width: 38px;
                height: 21px;
                -webkit-tap-highlight-color: transparent;
            }

            .sexyCheckbox input {
                position: absolute;
                opacity: 0;
                pointer-events: none;
                width: 0;
                height: 0;
            }

            .sexyCheckbox .slider {
                position: absolute;
                inset: 0;
                cursor: pointer;
                background: var(--textcolor);         /* AUS */
                border-radius: 34px;
                min-width: 37px;
                outline: 0;
                backface-visibility: hidden;          /* Anti-Flicker */
            }

            .sexyCheckbox .slider:before {
                content: "";
                position: absolute;
                left: 3px;
                bottom: 2.5px;
                width: 16px;
                height: 16px;
                background: var(--pagebackground);
                border-radius: 50%;
                transition: transform .25s ease;      /* nur den Knopf animieren */
                will-change: transform;               /* eigenes Layer */
            }

            .sexyCheckbox input:checked + .slider {
                background: linear-gradient(15deg, var(--orange_01) 0%, var(--orange_02) 100%);
            }

            .sexyCheckbox input:checked + .slider:before {
                transform: translateX(16px);
            }

            .option-slider { align-items: center; }

            .option.option-slider > .slider-inline {
                flex: 1;
                display: flex;
                align-items: center;
                justify-content: flex-end;
                gap: 10px;
                padding: 0 21px 0 0;
            }

            .slider-val {
                min-width: 38px;
                text-align: center;
                font-weight: 600;
                font-size: 0.85rem;
                white-space: nowrap;
                color: #ccc;
                background: #333;
                border-radius: 3px;
                padding: 2px 7px;
            }

            .slider-inline input[type="range"] {
                flex: 1;
                margin-top: 0;
                width: auto;
            }

            #priceSlider {
                width: 100%;
                max-width: 270px;
                margin-top: 0;
                appearance: none;
                height: 6px;
                border-radius: 6px;
                background: linear-gradient(to right, #f7931a 0%, #ddd 0%);
                outline: none;
                border: none;
                transition: background 0.2s;
            }

            #priceSlider::-webkit-slider-thumb {
                appearance: none;
                width: 20px;
                height: 20px;
                border-radius: 50%;
                background: #f7931a;
                cursor: pointer;
                box-shadow: 0 0 2px rgba(0,0,0,0.3);
            }

            /* Firefox */
            #priceSlider::-moz-range-thumb {
                width: 16px;
                height: 16px;
                border-radius: 50%;
                background: #f7931a;
                cursor: pointer;
            }

            /* Firefox-Track */
            #priceSlider::-moz-range-track {
                background: #ddd;
                height: 6px;
                border-radius: 3px;
            }

            #brightnessSlider, #ledBrightnessSlider, #volumeSlider {
                width: 100%;
                max-width: 270px;
                margin-top: 0;
                appearance: none;
                height: 6px;
                border-radius: 6px;
                outline: none;
                border: none;
                transition: background 0.2s;
            }

            #brightnessSlider, #ledBrightnessSlider {
                background: linear-gradient(to right, #ff8e02 0%, #ddd 0%);
            }

            #brightnessSlider::-webkit-slider-thumb,
            #ledBrightnessSlider::-webkit-slider-thumb {
                appearance: none;
                width: 20px;
                height: 20px;
                border-radius: 50%;
                background: #ff8e02;
                cursor: pointer;
                box-shadow: 0 0 2px rgba(0,0,0,0.3);
            }

            #brightnessSlider::-moz-range-thumb,
            #ledBrightnessSlider::-moz-range-thumb {
                width: 16px;
                height: 16px;
                border-radius: 50%;
                background: #ff8e02;
                cursor: pointer;
                border: none;
            }

            #brightnessSlider::-moz-range-track,
            #ledBrightnessSlider::-moz-range-track {
                background: #ddd;
                height: 6px;
                border-radius: 3px;
            }

            #volumeSlider {
                background: linear-gradient(to right, #008493 0%, #ddd 0%);
            }

            #volumeSlider::-webkit-slider-thumb {
                appearance: none;
                width: 20px;
                height: 20px;
                border-radius: 50%;
                background: #008493;
                cursor: pointer;
                box-shadow: 0 0 2px rgba(0,0,0,0.3);
            }

            #volumeSlider::-moz-range-thumb {
                width: 16px;
                height: 16px;
                border-radius: 50%;
                background: #008493;
                cursor: pointer;
                border: none;
            }

            #volumeSlider::-moz-range-track {
                background: #ddd;
                height: 6px;
                border-radius: 3px;
            }

            .slider-wrap {
                position: relative;
                padding-top: 26px;
                margin-top: 12px;
            }

            .slider-wrap > input[type="range"] { margin-top: 0; }

            .slider-tooltip {
                position: absolute;
                top: 0;
                transform: translateX(-50%);
                background: #444;
                color: #fff;
                padding: 2px 7px;
                border-radius: 3px;
                font-size: 0.78rem;
                font-weight: 600;
                white-space: nowrap;
                pointer-events: none;
            }

            .slider-tooltip::after {
                content: '';
                position: absolute;
                top: 100%;
                left: 50%;
                transform: translateX(-50%);
                border: 4px solid transparent;
                border-top-color: #444;
            }

            /* Blendet Steuerelement im number-Field aus */
            input::-webkit-outer-spin-button,
            input::-webkit-inner-spin-button {
            -webkit-appearance: none;
            margin: 0;
            }

            input[type=number] {
            -moz-appearance: textfield;
            }

            .button:disabled {
                cursor: not-allowed;
                background: #646464;
            }

            .button:disabled:hover {
                background: var(--primarycolor); /* verhindert Hover-Effekt */
            }

            .page-sort-grid {
                display: flex;
                flex-wrap: wrap;
                gap: 0.5rem;
            }

            .sort-item {
                position: relative;
                background: var(--pagebackground);
                border: 1px solid #444;
                border-radius: var(--radius);
                padding: 1.4rem 0.75rem 1.2rem;
                min-width: 80px;
                text-align: center;
                cursor: grab;
                font-weight: 700;
                font-size: 0.9rem;
                letter-spacing: 1px;
                user-select: none;
                transition: border-color .15s, opacity .15s;
            }

            .sort-item:active { cursor: grabbing; }

            .sort-item.drag-over {
                border-color: var(--primarycolor);
                background: #1e3236;
            }

            .sort-item.dragging { opacity: 0.35; }

            .sort-pos {
                position: absolute;
                top: 3px;
                left: 5px;
                font-size: 0.6rem;
                opacity: .55;
                font-weight: 500;
            }

        </style>
    
{"price":{"usd":%.0f,"eur":%.0f,"chf":%.0f,"chg_usd":%.2f,"chg_eur":%.2f,"chg_chf":%.2f,"moscow":%d,"moscow_eur":%d}

<style>
input[name="tab"]{display:none}
.layout{display:flex;align-items:flex-start}
.sidebar{width:190px;flex-shrink:0;background:var(--boxbackground);min-height:100vh;position:sticky;top:0;display:flex;flex-direction:column}
.s-logo{padding:1rem 1.25rem;font-size:1.5rem;font-weight:700;color:#fff;border-bottom:1px solid #333;text-align:left}
.logo-wrap{display:inline-block; line-height: 1.3rem;}
.logo-b{font-weight:700;color:#fff;letter-spacing:0.6px}
.logo-t{font-weight:400;color:#fff;letter-spacing:0.6px}
.logo-sub{font-size:.9rem;background:linear-gradient(127deg,var(--orange_01),var(--orange_02));-webkit-background-clip:text;background-clip:text;-webkit-text-fill-color:transparent;color:transparent;text-align:right;letter-spacing:.1em;margin-top:1px;font-weight:600}
.tab-lbl{display:block;padding:.75rem 1.25rem;cursor:pointer;color:#bbb;border-left:3px solid transparent;font-size:.9rem;text-align:left}
.s-footer{margin-top:auto;padding:.75rem 1.25rem;font-size:.68rem;color:#bbb;border-top:1px solid #2a2d32;line-height:1.7}
.tab-lbl:hover{color:#fff;background:#2a2d32}
#t2:checked~.layout .sidebar label[for="t2"],
#t3:checked~.layout .sidebar label[for="t3"],
#t4:checked~.layout .sidebar label[for="t4"],
#t5:checked~.layout .sidebar label[for="t5"],
#t6:checked~.layout .sidebar label[for="t6"],
#t7:checked~.layout .sidebar label[for="t7"]{color:#fff;border-left:3px solid var(--primarycolor);background:#2a2d32}
.panels{flex:1;padding:1rem;max-width:600px;text-align:left}
.panel{display:none}
#t2:checked~.layout .panels #p2{display:block}
#t3:checked~.layout .panels #p3{display:block}
#t4:checked~.layout .panels #p4{display:block}
#t5:checked~.layout .panels #p5{display:block}
#t6:checked~.layout .panels #p6{display:block}
#t7:checked~.layout .panels #p7{display:block}
#wks-reset{display:none}
#t7:checked~.layout .panels #wks-reset{display:block}
#t5:checked~.layout .panels input.button[type="submit"]{display:none}
.btn-row{display:flex;gap:.5rem}
#restart-btn{display:none}
#t7:checked~.layout .panels #restart-btn{display:block}
.dash-grid{display:grid;grid-template-columns:1fr 1fr;gap:1rem}
.db-card{background:var(--boxbackground);border-top:1px solid #888;border-radius:var(--radius);padding:1.25rem;padding-top:0.8rem;text-align:right}
.db-card h3{font-size:.75rem;color:#888;margin:0 0 .6rem;text-transform:uppercase;letter-spacing:.08em;text-align:left}
.db-val{font-size:2rem;font-weight:700;color:#fff;line-height:1.1}
.db-sub{font-size:.85rem;color:#aaa;margin-top:.3rem}
.chg-pos{color:#4caf50}
.chg-neg{color:#f44336}
.fee-row{display:flex;gap:.5rem;margin-top:.4rem}
.fee-item{flex:1;text-align:center;background:#2a2d32;border-radius:var(--radius);padding:.6rem .25rem}
.fee-item .fv{font-size:1.8rem;font-weight:700;color:#fff;line-height:1}
.fee-item .fu{font-size:.68rem;color:#777;margin-top:.1rem}
.fee-item .fl{font-size:.78rem;color:#aaa;margin-top:.35rem;font-weight:600}
.fee-item .fc{font-size:.7rem;color:#666;margin-top:.2rem;line-height:1.4}
.bx-row{margin-bottom:.5rem;padding-bottom:.5rem;border-bottom:1px solid #333}
.bx-row:last-child{margin-bottom:0;padding-bottom:0;border-bottom:none}
.diff-bar{height:4px;background:#2a2d32;border-radius:2px;margin-top:.6rem}
.info-tip{position:relative;font-size:.75rem;color:#666;cursor:default;font-weight:400;text-transform:none;letter-spacing:0;vertical-align:middle}
.info-tip::before{content:attr(data-tip);position:absolute;bottom:calc(100% + .5rem);left:50%;transform:translateX(-50%);background:#2a2d32;color:#ccc;font-size:.72rem;padding:.5rem .75rem;border-radius:var(--radius);white-space:normal;width:210px;line-height:1.5;pointer-events:none;opacity:0;transition:opacity .2s;z-index:99;text-transform:none;letter-spacing:0;font-weight:400;border:1px solid #3a3d42}
.info-tip:hover::before{opacity:1}
.diff-fill{height:100%;background:linear-gradient(90deg,var(--orange_01),var(--orange_02));border-radius:2px;transition:width .4s}
.news-item{text-align:left;padding:.4rem 0;border-bottom:1px solid #2a2d32}
.news-item:last-child{border-bottom:none}
.news-hl{font-size:0.85rem;color:#ddd;line-height:1.4}
.news-meta{font-size:.8rem;color:#666;margin-top:.2rem}
@media(max-width:600px){.dash-grid{grid-template-columns:1fr}}
#toast{position:fixed;top:1.25rem;right:1.25rem;background:#4caf50;color:#fff;padding:.75rem 1.5rem;border-radius:var(--radius);font-weight:600;font-size:.9rem;opacity:0;transform:translateY(-8px);transition:opacity .25s,transform .25s;z-index:9999;pointer-events:none}
#toast.show{opacity:1;transform:translateY(0)}
#toast.err{background:#f44336}
.mob-hdr{display:none;justify-content:space-between;align-items:center;background:var(--boxbackground);padding:.75rem 1rem;position:sticky;top:0;z-index:100}
.m-logo{font-size:1.1rem;font-weight:700;color:#fff}
.m-logo .logo-wrap{display:flex;align-items:baseline;gap:.5rem;line-height:normal}
.m-logo .logo-sub{font-size:inherit;margin-top:0;text-align:left}
.mob-btn{background:none;border:none;color:#fff;font-size:1.5rem;cursor:pointer;line-height:1;padding:.2rem .4rem}
.overlay{display:none;position:fixed;inset:0;background:rgba(0,0,0,.5);z-index:199}
.overlay.open{display:block}
#restartOverlay{display:none;position:fixed;inset:0;background:rgba(0,0,0,.85);z-index:9999;align-items:center;justify-content:center;flex-direction:column;gap:1rem;color:#fff;text-align:center}
#restartOverlay.show{display:flex}
#restartOverlay .spin{width:2.5rem;height:2.5rem;border:.3rem solid #444;border-top-color:#fff;border-radius:50%;animation:spin 1s linear infinite}
@keyframes spin{to{transform:rotate(360deg)}}
@media(max-width:600px){
  .mob-hdr{display:flex}
  .sidebar{position:fixed;left:0;top:0;height:100%;overflow-y:auto;transform:translateX(-100%);transition:transform .22s ease;z-index:200;min-height:unset}
  .sidebar.open{transform:translateX(0)}
  .panels{padding:1rem;width:100%;box-sizing:border-box}
  .layout{min-height:unset}
}
</style>
</head>
<body>
<input type="radio" name="tab" id="t2">
<input type="radio" name="tab" id="t3">
<input type="radio" name="tab" id="t4">
<input type="radio" name="tab" id="t5" checked>
<input type="radio" name="tab" id="t6">
<input type="radio" name="tab" id="t7">
<div class="mob-hdr">
  <div class="m-logo"><div class="logo-wrap"><div><span class="logo-b">BLOCK</span><span class="logo-t">TRAINER</span></div><div class="logo-sub">TERMINAL</div></div></div>
  <button class="mob-btn" id="hambBtn" type="button">&#9776;</button>
</div>
<div id="toast"></div>
<div class="overlay" id="overlay"></div>
<div id="restartOverlay"><div class="spin"></div><div>Neustart l
uft &hellip;</div></div>
<div class="layout">
  <nav class="sidebar" id="sidebar">
    <div class="s-logo"><div class="logo-wrap"><div><span class="logo-b">BLOCK</span><span class="logo-t">TRAINER</span></div><div class="logo-sub">TERMINAL</div></div></div>
    <label class="tab-lbl" for="t5">Dashboard</label>
    <label class="tab-lbl" for="t2">Anzeige</label>
    <label class="tab-lbl" for="t3">Nachtmodus</label>
    <label class="tab-lbl" for="t4">Bitaxe</label>
    <label class="tab-lbl" for="t6">Preisalarm</label>
    <label class="tab-lbl" for="t7">System</label>
    <div class="s-footer">Made with &#x2764;&#xFE0F; v %FIRMWARE_VERSION%<br><span id="mac-tap" style="cursor:default;user-select:none">%MAC%</span></div>
  </nav>
  <div class="panels">
    <form method="POST" action="/save">

      <div id="p7" class="panel">
        <section>
          <h2>Zeitzone</h2>
          <div class="content">
            <div class="fieldwrap">
              <select name="timezone">


              </select>
            </div>
          </div>
        </section>

        <section id="secret-hw" style="display:none">
          <h2>Benachrichtigung</h2>
          <div class="option">
            <div class="option_label">LED</div>
            <div>
              <label class="sexyCheckbox">
                <input type="checkbox" name="ledEnabled" id="ledEnabled" value="1" %LED_ENABLED%>
                <span class="slider round"></span>
              </label>
            </div>
          </div>
          <div class="option">
            <div class="option_label">LED-Streifen Typ</div>
            <div style="display:flex;align-items:center;padding:0 21px">
              <select name="ledStripType" class="select" style="padding:.35rem .75rem">
                <option value="1" %LED_STRIP_1%>WS2811 
 GRB</option>
              </select>
            </div>
          </div>
          <div class="option option-slider">
            <div class="option_label">LED Helligkeit</div>
            <div class="slider-inline">
              <span class="slider-val" id="ledBrightnessVal"></span>
              <input type="range" id="ledBrightnessSlider" name="ledBrightness"
                min="0" max="100" step="5" value="%LED_BRIGHTNESS%">
            </div>
          </div>
          <div class="option option-slider">
            <div class="option_label">Lautst&auml;rke</div>
            <div class="slider-inline">
              <span class="slider-val" id="volumeVal"></span>
              <input type="range" id="volumeSlider" name="buzzerVolume"
                min="0" max="100" step="5" value="%BUZZER_VOLUME%">
            </div>
          </div>
        </section>

        <section>
          <h2>Allgemein</h2>
          <div class="option">
            <div class="option_label">Beim Start auf Updates pr&uuml;fen</div>
            <div>
              <label class="sexyCheckbox">
                <input type="checkbox" name="checkForUpdates" id="checkForUpdates" value="1" %UPDATES_ON%>
                <span class="slider round"></span>
              </label>
            </div>
          </div>
          <div class="option">
            <div class="option_label">Debugging</div>
            <div>
              <label class="sexyCheckbox">
                <input type="checkbox" name="debug" id="debug" value="1" %DEBUG_ON%>
                <span class="slider round"></span>
              </label>
            </div>
          </div>
        </section>

      </div><!-- /p7 System -->

      <div id="p2" class="panel">
        <section>
          <h2>Hintergrundbeleuchtung</h2>
          <div class="option option-slider">
            <div class="option_label">Helligkeit</div>
            <div class="slider-inline">
              <span class="slider-val" id="brightnessVal"></span>
              <input type="range" id="brightnessSlider" name="brightness"
                min="2" max="150" step="1" value="%BRIGHTNESS%">
            </div>
          </div>
          <div class="option">
            <div class="option_label">Rotation 180&deg;</div>
            <div>
              <label class="sexyCheckbox">
                <input type="checkbox" name="rotation" id="rotation" value="1" %ROTATION_CHECKED%>
                <span class="slider round"></span>
              </label>
            </div>
          </div>
        </section>

        <section>
          <h2>Seitenreihenfolge</h2>
          <div class="content">
            <div id="pageSort" class="page-sort-grid">
              %PAGE_SORT_ITEMS%
            </div>
            <input type="hidden" name="pageOrder" id="pageOrderInput" value="">
            <p style="font-size:.85rem;opacity:.65;margin-top:.75rem;margin-bottom:0;">
              Seiten per Drag &amp; Drop sortieren. RECHNER ist immer nur im Men&uuml; verf&uuml;gbar.
            </p>
          </div>
        </section>

        <section>
          <h2>W&auml;hrung</h2>
          <div class="content">
            <div class="grid2">
              <div>
                <input type="radio" name="currency" id="EUR" value="EUR" %EUR_SELECTED%/>
                <label class="radio" for="EUR">EUR</label>
              </div>
              <div>
                <input type="radio" name="currency" id="USD" value="USD" %USD_SELECTED%/>
                <label class="radio" for="USD">USD</label>
              </div>
              <div>
                <input type="radio" name="currency" id="CHF" value="CHF" %CHF_SELECTED%/>
                <label class="radio" for="CHF">CHF</label>
              </div>
            </div>
          </div>
        </section>

        <section>
          <h2>Bitcoinpreis</h2>
          <div class="option option-slider">
            <div class="option_label">Update Interval</div>
            <div class="slider-inline">
              <span class="slider-val" id="priceVal"></span>
              <input type="range" id="priceSlider" name="priceInterval"
                min="5000" max="60000" step="1000" value="%PRICE_INTERVAL%">
            </div>
          </div>
          <div class="option">
            <div class="option_label">Tausenderpunkt</div>
            <div>
              <label class="sexyCheckbox">
                <input type="checkbox" name="priceSeparator" id="priceSeparator" value="1" %PRICESEPARATOR_ON%>
                <span class="slider round"></span>
              </label>
            </div>
          </div>
          <div class="option">
            <div class="option_label">Splitscreen (2 W
bereinander)</div>
            <div>
              <label class="sexyCheckbox">
                <input type="checkbox" name="priceSplitScreen" id="priceSplitScreen" value="1" %PRICESPLITSCREEN_ON%>
                <span class="slider round"></span>
              </label>
            </div>
          </div>
          <div id="secondaryCurrencyWrap" style="%SECONDARY_CURRENCY_DISPLAY%">
            <div class="content fieldrow">
              <div class="fieldwrap">
                <label for="splitSecondaryCurrency">Sekund
hrung (untere Zeile)</label>
                <select name="splitSecondaryCurrency" id="splitSecondaryCurrency">
                  <option value="" %SPLITSEC_AUTO_SEL%>Automatisch (n&auml;chste nach Hauptw&auml;hrung)</option>
                  <option value="EUR" %SPLITSEC_EUR_SEL%>EUR</option>
                  <option value="USD" %SPLITSEC_USD_SEL%>USD</option>
                  <option value="CHF" %SPLITSEC_CHF_SEL%>CHF</option>
                </select>
              </div>
            </div>
            <p style="font-size:.85rem;opacity:.65;margin-top:.5rem;margin-bottom:0;">
              Ist die Sekund
hrung in der Reihenfolge EUR &rarr; USD &rarr; CHF angezeigt.
            </p>
          </div>
        </section>

        <section>
          <h2>Weitere Einstellungen</h2>
          <div class="option">
            <div class="option_label">Livestream Anzeige</div>
            <div>
              <label class="sexyCheckbox">
                <input type="checkbox" name="showYoutubeBar" id="showYoutubeBar" value="1" %YOUTUBE_ON%>
                <span class="slider round"></span>
              </label>
            </div>
          </div>
          <div class="option">
            <div class="option_label">Kalender Anzeige</div>
            <div>
              <label class="sexyCheckbox">
                <input type="checkbox" name="showEvents" id="showEvents" value="1" %EVENTS_ON%>
                <span class="slider round"></span>
              </label>
            </div>
          </div>
        </section>
      </div><!-- /p2 Anzeige -->

      <div id="p3" class="panel">
        <section>
          <h2>Nachtmodus</h2>
          <div class="option">
            <div class="option_label">Nachtmodus aktiv</div>
            <div>
              <label class="sexyCheckbox">
                <input type="checkbox" name="nightmodeActive" id="nightmodeActive" value="1" %NIGHTMODE_ACTIVE_ON%>
                <span class="slider round"></span>
              </label>
            </div>
          </div>
          <div class="content fieldrow">
            <div class="fieldwrap">
              <label for="nightmodeStart">Startzeit:</label>
              <input type="time" id="nightmodeStart" name="nightmodeStart" value="%NIGHTMODE_START%">
            </div>
            <div class="fieldwrap">
              <label for="nightmodeStop">Stopzeit:</label>
              <input type="time" id="nightmodeStop" name="nightmodeStop" value="%NIGHTMODE_STOP%">
            </div>
            <div class="fieldwrap">
              <label for="nightmodeDelay">Delay (10&ndash;60 Sekunden):</label>
              <input type="number" id="nightmodeDelay" name="nightmodeDelay"
                min="10" max="60" step="1" value="%NIGHTMODE_DELAY%">
            </div>
          </div>
        </section>
      </div><!-- /p3 Nachtmodus -->

      <div id="p4" class="panel">
        <section>
          <h2>Bitaxe</h2>
          <div class="content">
            <label>Bitaxe IPs (Format: 192.168.0.0)</label>
            <div id="bitaxeList">
              <div class="fieldwrap bitaxe-row" style="display:flex;align-items:center;">
                <span>http://</span>
                <input type="text" name="bitaxeIP"
                  pattern="^([0-9]{1,3}\.){3}[0-9]{1,3}$"
                  placeholder="192.168.0.0" value=""
                  oninput="this.value=this.value.replace(/^https?:\/\//i,'')"
                  style="flex:1;border-radius:0 4px 4px 0;">
              </div>
            </div>
            <button type="button" id="addBitaxe" class="button"
              style="margin-top:1rem;padding:.4rem .8rem;font-size:.9rem;">
              + Bitaxe hinzuf&uuml;gen
            </button>
            <div style="opacity:.7;font-size:.9rem;margin-top:.5rem;">
              <b>Alle Zeilen leer lassen</b> = Seite im Men&uuml; ausgeblendet.
            </div>
            <div style="font-size:.9rem;margin-top:1rem;">
              <div style="display:flex;gap:.5rem;justify-content:center;">
                <div style="font-size:2rem;line-height:2.2rem;">&#9888;&#65039;</div>
                <div>F&uuml;r einen einwandfreien Betrieb sollte auf dem Bitaxe<br>
                  mindestens die Firmware-Version 2.11.0 installiert sein.</div>
              </div>
            </div>
          </div>
        </section>

      </div><!-- /p4 Bitaxe -->

      <div id="p6" class="panel">
        <section>
          <h2>Preisalarm</h2>
          <div class="option">
            <div class="option_label">Preisalarm</div>
            <div>
              <label class="sexyCheckbox">
                <input type="checkbox" name="alertEnabled" id="alertEnabled" value="1" %ALERT_ENABLED%>
                <span class="slider round"></span>
              </label>
            </div>
          </div>
          <div class="content fieldrow">
            <div class="fieldwrap">
              <label for="alertCurrency">W&auml;hrung</label>
              <select name="alertCurrency" id="alertCurrency">
                <option value="EUR" %ALERT_EUR_SEL%>EUR</option>
                <option value="USD" %ALERT_USD_SEL%>USD</option>
                <option value="CHF" %ALERT_CHF_SEL%>CHF</option>
              </select>
            </div>
            <div class="fieldwrap">
              <label for="alertBelow">&#9660; Unter</label>
              <input type="number" name="alertBelow" id="alertBelow"
                value="%ALERT_BELOW%" min="0" step="100" placeholder="0 = aus">
            </div>
            <div class="fieldwrap">
              <label for="alertAbove">&#9650; &Uuml;ber</label>
              <input type="number" name="alertAbove" id="alertAbove"
                value="%ALERT_ABOVE%" min="0" step="100" placeholder="0 = aus">
            </div>
          </div>
        </section>
      </div><!-- /p6 Benachrichtigungen -->

      <div id="p5" class="panel">
        <div class="dash-grid">
          <div class="db-card">
            <h3>BTC / USD</h3>
            <div class="db-val" id="d-priceUsd">-</div>
            <div class="db-sub" id="d-chgUsd">-</div>
          </div>
          <div class="db-card">
            <h3>BTC / EUR</h3>
            <div class="db-val" id="d-priceEur">-</div>
            <div class="db-sub" id="d-chgEur">-</div>
          </div>
          <div class="db-card">
            <h3>BTC / CHF</h3>
            <div class="db-val" id="d-priceChf">-</div>
            <div class="db-sub" id="d-chgChf">-</div>
          </div>
          <div class="db-card">
            <h3>Moscow Time</h3>
            <div style="display:flex;justify-content:flex-end;align-items:stretch;gap:0">
              <div style="text-align:right;padding-right:.9rem"><div class="db-val" id="d-moscow">-</div><div class="db-sub">sats/$</div></div>
              <div style="width:1px;background:#555;margin:0 0"></div>
              <div style="text-align:right;padding-left:.9rem"><div class="db-val" id="d-moscow-eur">-</div><div class="db-sub">sats/&#x20AC;</div></div>
            </div>
          </div>
          <div class="db-card">
            <h3>Aktueller Block</h3>
            <div class="db-val" id="d-block">-</div>
          </div>
          <div class="db-card">
            <h3>Mempool-Geb&uuml;hren</h3>
            <div class="fee-row">
              <div class="fee-item"><div class="fv" id="d-feeLow">-</div><div class="fu">sat/vB</div><div class="fl">Niedrig</div><div class="fc" id="d-feeLowCost"></div></div>
              <div class="fee-item"><div class="fv" id="d-feeMid">-</div><div class="fu">sat/vB</div><div class="fl">Mittel</div><div class="fc" id="d-feeMidCost"></div></div>
              <div class="fee-item"><div class="fv" id="d-feeHigh">-</div><div class="fu">sat/vB</div><div class="fl">Hoch</div><div class="fc" id="d-feeHighCost"></div></div>
            </div>
          </div>
          <div class="db-card">
            <h3>Difficulty-Anpassung <span class="info-tip" data-tip="Erwartete &#196;nderung beim n&#228;chsten Retarget (alle 2016 Bl&#246;cke &#8776; 2 Wochen). + = Bl&#246;cke schneller als 10 min &#8594; Difficulty steigt. &#8722; = langsamer &#8594; sinkt.">&#9432;</span></h3>
            <div class="db-val" id="d-diffChg">-</div>
            <div style="display:flex;justify-content:space-between;align-items:baseline">
              <div class="db-sub" id="d-diffBlocks">-</div>
              <div class="db-sub" id="d-diffProg">-</div>
            </div>
            <div class="diff-bar"><div class="diff-fill" id="d-diffFill" style="width:0%"></div></div>
          </div>
          <div class="db-card" style="text-align:center;display:flex;flex-direction:column">
            <h3>Uhrzeit</h3>
            <div style="flex:1;display:flex;flex-direction:column;justify-content:center;align-items:center">
              <div class="db-val" id="d-clock" style="font-size:2.4rem;letter-spacing:.05em">-</div>
              <div class="db-sub" id="d-clockdate" style="margin-top:.4rem;color:#aaa">-</div>
            </div>
          </div>
          <div class="db-card" id="d-bitaxeCard" style="display:none">
            <h3>Bitaxe</h3>
            <div id="d-bitaxeRows"></div>
          </div>
          <div class="db-card" id="d-newsCard" style="display:none;grid-column:1/-1">
            <h3>Bitcoin News</h3>
            <div id="d-newsList"></div>
          </div>
        </div>
      </div><!-- /p5 Dashboard -->

      <div class="btn-row">
        <button class="button" type="button" id="restart-btn" onclick="doRestart()">Neustarten</button>
        <input class="button" type="submit" value="Speichern">
      </div>

      <section id="wks-reset" style="margin-top:2rem">
        <h2>Werkseinstellungen</h2>
        <div class="content" style="font-size:.85rem">
          Es werden alle Einstellungen einschlie&szlig;lich die WLAN-Verbindung
          auf Werkseinstellungen zur&uuml;ckgesetzt.
        </div>
        <button class="button" type="button" style="background:#FF4444;margin-top:.5rem" onclick="doReset()">
          Zur&uuml;cksetzen
        </button>
      </section>
    </form>
  </div><!-- .panels -->
</div><!-- .layout -->


<script>
// Toast
function showToast(msg,isErr){
  var t=document.getElementById('toast');
  t.textContent=msg;
  t.classList.toggle('err',!!isErr);
  t.classList.add('show');
  clearTimeout(t._tid);
  t._tid=setTimeout(function(){t.classList.remove('show');},isErr?8000:2500);
}
function doReset(){
  fetch('/reset').then(function(r){return r.json();}).then(function(d){
    if(d.ok)showToast('Werkseinstellungen zur
cksetzen',true);});
}
function doRestart(){
  document.getElementById('restartOverlay').classList.add('show');
  fetch('/restart').catch(function(){});
  setTimeout(function(){pollAfterRestart(0);},3000);
}
function pollAfterRestart(attempt){
  fetch('/status.json',{cache:'no-store'}).then(function(r){
    if(r.ok) location.reload();
    else retryPollAfterRestart(attempt);
  }).catch(function(){retryPollAfterRestart(attempt);});
}
function retryPollAfterRestart(attempt){
  if(attempt>=30){location.reload();return;} // Fallback nach ~33s, falls Polling nie erfolgreich ist
  setTimeout(function(){pollAfterRestart(attempt+1);},1000);
}
// Formular per fetch absenden
document.querySelector('form').addEventListener('submit',function(e){
  e.preventDefault();
  fetch('/save',{method:'POST',body:new FormData(this)})
    .then(function(r){return r.json();})
    .then(function(d){if(d.ok)showToast('Erfolgreich gespeichert 
');})
    .catch(function(){showToast('Fehler beim Speichern');});
});

// Hamburger-Men&uuml;
(function(){
  var sb=document.getElementById('sidebar');
  var ov=document.getElementById('overlay');
  var btn=document.getElementById('hambBtn');
  if(!btn)return;
  function closeNav(){sb.classList.remove('open');ov.classList.remove('open');}
  btn.addEventListener('click',function(){sb.classList.toggle('open');ov.classList.toggle('open');});
  ov.addEventListener('click',closeNav);
  document.querySelectorAll('.tab-lbl').forEach(function(l){l.addEventListener('click',closeNav);});
})();

const BITAXE_IPS=[%BITAXE_IPS%];

// Gespeicherte Bitaxe-IPs anzeigen
(function(){
  var list=document.getElementById('bitaxeList');
  if(!list)return;
  var tmpl=list.querySelector('.bitaxe-row');
  if(!tmpl)return;
  if(typeof BITAXE_IPS!=="undefined"&&BITAXE_IPS.length>0){
    list.innerHTML="";
    BITAXE_IPS.forEach(function(ip){
      var row=tmpl.cloneNode(true);
      var input=row.querySelector('input[name="bitaxeIP"]');
      if(input)input.value=ip;
      list.appendChild(row);
    });
  }
})();

// Bitaxe: weitere IP-Zeilen (max. 3)
(function(){
  var MAX_ROWS=3;
  var list=document.getElementById('bitaxeList');
  var addBtn=document.getElementById('addBitaxe');
  if(!list||!addBtn)return;
  function updateBtn(){
    var rows=list.querySelectorAll('.bitaxe-row').length;
    addBtn.disabled=rows>=MAX_ROWS;
    addBtn.style.opacity=addBtn.disabled?"0.5":"1";
    addBtn.style.cursor=addBtn.disabled?"not-allowed":"pointer";
  }
  addBtn.addEventListener('click',function(){
    var rows=list.querySelectorAll('.bitaxe-row').length;
    if(rows>=MAX_ROWS)return;
    var firstRow=list.querySelector('.bitaxe-row');
    if(!firstRow)return;
    var clone=firstRow.cloneNode(true);
    var input=clone.querySelector('input[name="bitaxeIP"]');
    if(input)input.value="";
    list.appendChild(clone);
    updateBtn();
  });
  updateBtn();
})();

// Slider
function initSlider(sliderId,valId,fillColor,displayFn){
  var slider=document.getElementById(sliderId);
  var valEl=document.getElementById(valId);
  if(!slider)return;
  function update(){
    var min=parseFloat(slider.min),max=parseFloat(slider.max);
    var pct=(parseFloat(slider.value)-min)/(max-min);
    slider.style.background='linear-gradient(to right,'+fillColor+' '+(pct*100)+'%,#ddd '+(pct*100)+'%)';
    if(valEl)valEl.textContent=displayFn(slider.value);
  }
  slider.addEventListener('input',update);
  update();
}
// Sekund
hrung nur einblenden, solange Splitscreen aktiv ist
(function(){
  var cb=document.getElementById('priceSplitScreen');
  var wrap=document.getElementById('secondaryCurrencyWrap');
  if(!cb||!wrap)return;
  cb.addEventListener('change',function(){wrap.style.display=cb.checked?'':'none';});
})();

// Sekund
t selbst)
(function(){
  var radios=document.querySelectorAll('input[name="currency"]');
  var sec=document.getElementById('splitSecondaryCurrency');
  if(!radios.length||!sec)return;
  radios.forEach(function(r){
    r.addEventListener('change',function(){
      if(r.checked && sec.value===r.value) sec.value='';
    });
  });
})();

initSlider('priceSlider','priceVal','#f7931a',function(v){return(v/1000)+' s';});
initSlider('brightnessSlider','brightnessVal','#ff8e02',function(v){return v;});
initSlider('ledBrightnessSlider','ledBrightnessVal','#ff8e02',function(v){return v+' %';});
initSlider('volumeSlider','volumeVal','#008493',function(v){return v+' %';});

// Seitenreihenfolge: Drag & Drop
(function(){
  var grid=document.getElementById('pageSort');
  var orderInput=document.getElementById('pageOrderInput');
  if(!grid||!orderInput)return;
  var dragSrc=null;
  function updateOrder(){
    var items=grid.querySelectorAll('.sort-item');
    orderInput.value=Array.from(items).map(function(el){return el.dataset.key;}).join(',');
    items.forEach(function(el,i){var p=el.querySelector('.sort-pos');if(p)p.textContent=i+1;});
  }
  updateOrder();
  grid.addEventListener('dragstart',function(e){
    dragSrc=e.target.closest('.sort-item');
    if(dragSrc){dragSrc.classList.add('dragging');e.dataTransfer.effectAllowed='move';}
  });
  grid.addEventListener('dragend',function(){
    grid.querySelectorAll('.sort-item').forEach(function(el){el.classList.remove('dragging','drag-over');});
  });
  grid.addEventListener('dragover',function(e){
    e.preventDefault();
    var t=e.target.closest('.sort-item');
    if(!t||t===dragSrc)return;
    grid.querySelectorAll('.sort-item').forEach(function(el){el.classList.remove('drag-over');});
    t.classList.add('drag-over');
  });
  grid.addEventListener('drop',function(e){
    e.preventDefault();
    var t=e.target.closest('.sort-item');
    if(!t||t===dragSrc||!dragSrc)return;
    var items=Array.from(grid.querySelectorAll('.sort-item'));
    if(items.indexOf(dragSrc)<items.indexOf(t))grid.insertBefore(dragSrc,t.nextSibling);
    else grid.insertBefore(dragSrc,t);
    t.classList.remove('drag-over');
    updateOrder();
  });
  var touchSrc=null;
  grid.addEventListener('touchstart',function(e){
    touchSrc=e.target.closest('.sort-item');
    if(touchSrc)touchSrc.classList.add('dragging');
  },{passive:true});
  grid.addEventListener('touchmove',function(e){
    if(!touchSrc)return;
    e.preventDefault();
    var touch=e.touches[0];
    var el=document.elementFromPoint(touch.clientX,touch.clientY);
    var t=el?el.closest('.sort-item'):null;
    grid.querySelectorAll('.sort-item').forEach(function(el){el.classList.remove('drag-over');});
    if(t&&t!==touchSrc)t.classList.add('drag-over');
  },{passive:false});
  grid.addEventListener('touchend',function(e){
    if(!touchSrc)return;
    var touch=e.changedTouches[0];
    var el=document.elementFromPoint(touch.clientX,touch.clientY);
    var t=el?el.closest('.sort-item'):null;
    touchSrc.classList.remove('dragging');
    grid.querySelectorAll('.sort-item').forEach(function(el){el.classList.remove('drag-over');});
    if(t&&t!==touchSrc){
      var items=Array.from(grid.querySelectorAll('.sort-item'));
      if(items.indexOf(touchSrc)<items.indexOf(t))grid.insertBefore(touchSrc,t.nextSibling);
      else grid.insertBefore(touchSrc,t);
      updateOrder();
    }
    touchSrc=null;
  },{passive:true});
})();

// Dashboard-Tab
(function(){
  function el(id){return document.getElementById(id);}
  function fmtN(n){return n>=1000?n.toLocaleString('de-DE'):String(n);}
  function refreshDash(){
    fetch('/status.json').then(function(r){return r.json();}).then(function(d){
      var e;
      e=el('d-priceUsd'); if(e)e.textContent=fmtN(d.price.usd)+' USD';
      var cu=d.price.chg_usd,ce=d.price.chg_eur;
      e=el('d-chgUsd'); if(e){e.className='db-sub';e.innerHTML='<span style="color:#aaa;margin-right:.65rem">24h</span><span class="'+(cu>=0?'chg-pos':'chg-neg')+'">'+(cu>=0?'+':'')+cu.toFixed(2)+' %</span>';}
      e=el('d-priceEur'); if(e)e.textContent=fmtN(d.price.eur)+' EUR';
      e=el('d-chgEur'); if(e){e.className='db-sub';e.innerHTML='<span style="color:#aaa;margin-right:.65rem">24h</span><span class="'+(ce>=0?'chg-pos':'chg-neg')+'">'+(ce>=0?'+':'')+ce.toFixed(2)+' %</span>';};
      e=el('d-priceChf'); if(e)e.textContent=fmtN(d.price.chf)+' CHF';
      var cc=d.price.chg_chf;
      e=el('d-chgChf'); if(e){e.className='db-sub';e.innerHTML='<span style="color:#aaa;margin-right:.65rem">24h</span><span class="'+(cc>=0?'chg-pos':'chg-neg')+'">'+(cc>=0?'+':'')+cc.toFixed(2)+' %</span>';};
      e=el('d-moscow'); if(e&&d.price.moscow)e.textContent=fmtN(d.price.moscow);
      e=el('d-moscow-eur'); if(e&&d.price.moscow_eur)e.textContent=fmtN(d.price.moscow_eur);
      if(d.clock){
        e=el('d-clock'); if(e)e.textContent=d.clock.time;
        e=el('d-clockdate'); if(e)e.textContent=d.clock.weekday+' | '+d.clock.date;
      }
      e=el('d-block'); if(e)e.textContent=fmtN(d.block.height);
      e=el('d-feeLow'); if(e)e.textContent=d.fees.low;
      e=el('d-feeMid'); if(e)e.textContent=d.fees.mid;
      e=el('d-feeHigh'); if(e)e.textContent=d.fees.high;
      function dfc(r){var b=r*140/1e8;return(b*d.price.usd).toFixed(2)+' $ / '+(b*d.price.eur).toFixed(2)+' 
';}
      e=el('d-feeLowCost'); if(e)e.textContent=dfc(d.fees.low);
      e=el('d-feeMidCost'); if(e)e.textContent=dfc(d.fees.mid);
      e=el('d-feeHighCost'); if(e)e.textContent=dfc(d.fees.high);
      if(d.difficulty){
        var chg=d.difficulty.change;
        e=el('d-diffChg');
        if(e){e.textContent=(chg>=0?'+':'')+chg.toFixed(2)+' %';e.className='db-val '+(chg>=0?'chg-pos':'chg-neg');}
        e=el('d-diffProg'); if(e)e.textContent=d.difficulty.progress.toFixed(1)+' % bis Anpassung';
        e=el('d-diffBlocks'); if(e&&d.difficulty.blocks)e.textContent='noch '+d.difficulty.blocks.toLocaleString('de-DE')+' Bl
cke';
        e=el('d-diffFill'); if(e)e.style.width=d.difficulty.progress.toFixed(1)+'%';
      }
      if(d.bitaxe&&d.bitaxe.length>0){
        e=el('d-bitaxeCard'); if(e)e.style.display='block';
        e=el('d-bitaxeRows');
        if(e)e.innerHTML=d.bitaxe.map(function(b){
          return '<div class="bx-row"><b>'+b.name+'</b><br><span style="color:#aaa">'+b.hash+' &bull; '+b.temp+' &bull; Fan: '+b.fan+'</span></div>';
        }).join('');
      }
      if(d.news&&d.news.length>0){
        e=el('d-newsCard'); if(e)e.style.display='block';
        e=el('d-newsList');
        if(e)e.innerHTML=d.news.slice(0,3).map(function(n){
          var dt=new Date(n.ts*1000).toLocaleDateString('de-DE');
          return '<div class="news-item"><div class="news-hl">'+n.headline+'</div><div class="news-meta">'+n.author+' 
 '+dt+'</div></div>';
        }).join('');
      }
    }).catch(function(){});
  }
  refreshDash();
  setInterval(refreshDash,5000);
})();
(function(){
  var taps=0,t;
  var m=document.getElementById('mac-tap');
  if(!m)return;
  m.addEventListener('click',function(){
    clearTimeout(t);
    if(++taps>=10){
      var s=document.getElementById('secret-hw');
      if(s)s.style.display='';
      taps=0;
    }
    t=setTimeout(function(){taps=0;},3000);
  });
})();
</script>
</body>
</html>

"<C:/Users/USER01/.platformio/packages/framework-arduinoespressif32/libraries/SPI/src/SPI.cpp
C:/Users/USER01/.platformio/packages/framework-arduinoespressif32/libraries/Wire/src/Wire.cpp
2023
2022
2021
2020
2019
2018
2017
2016
2015
2014
2013
2012
2011
2010
2009
2008
2007
2006
2005
2004
2003
2002
2001
2000
1999
1998
1997
1996
1995
1994
1993
1992
1991
1990
1989
1988
1987
1986
1985
1984
1983
1982
1981
1980
1979
1978
1977
1976
1975
1974
1973
1972
1971
1970
1969
1968
1967
1966
1965
1964
1963
1962
1961
1960
1959
1958
1957
1956
1955
1954
1953
1952
1951
1950
1949
1948
1947
1946
1945
1944
1943
1942
1941
1940
1939
1938
1937
1936
1935
1934
1933
1932
1931
1930
1929
1928
1927
1926
1925
1924
1923
1922
1921
1920
1919
1918
1917
1916
1915
1914
1913
1912
1911
1910
1909
1908
1907
1906
1905
1904
1903
1902
1901
)RAWHTML";
