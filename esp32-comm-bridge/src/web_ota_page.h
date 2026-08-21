#pragma once

/* Kept in the ESP32 application image so the control panel remains available
 * while SPIFFS is being used to stage STM32 firmware. */
static const char WEB_OTA_PAGE[] = R"WEBOTA(
<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
  <meta name="theme-color" content="#07101d">
  <title>EnvLink 控制台</title>
  <style>
    :root{color-scheme:dark;--bg:#07101d;--surface:#0d1928;--surface2:#111f31;--surface3:#15263a;--line:#21344b;--text:#f3f7fc;--muted:#8192a9;--green:#55d6a7;--blue:#57a7ff;--cyan:#45d2e8;--orange:#ff9a62;--yellow:#f3c75f;--violet:#a98bff;--danger:#ff6878;--shadow:0 22px 70px rgba(0,0,0,.28)}
    *{box-sizing:border-box}
    html{scroll-behavior:smooth}
    body{margin:0;min-height:100vh;background:radial-gradient(circle at 78% -15%,rgba(49,113,174,.28),transparent 34rem),radial-gradient(circle at -10% 85%,rgba(30,126,101,.18),transparent 30rem),var(--bg);font-family:-apple-system,BlinkMacSystemFont,"Segoe UI","PingFang SC","Microsoft YaHei",sans-serif;color:var(--text)}
    button,input{font:inherit}
    button{color:inherit}
    .shell{width:min(1120px,100%);margin:auto;padding:26px 24px 44px}
    .topbar{display:flex;align-items:center;justify-content:space-between;gap:18px;margin-bottom:28px}
    .brand{display:flex;align-items:center;gap:12px}
    .brand-mark{display:grid;place-items:center;width:42px;height:42px;border-radius:13px;background:linear-gradient(145deg,#183650,#0e263b);border:1px solid #28506c;box-shadow:inset 0 1px rgba(255,255,255,.08);font:800 17px/1 ui-monospace,SFMono-Regular,monospace;color:var(--green)}
    .brand strong{display:block;font-size:18px;letter-spacing:.01em}.brand small{display:block;margin-top:3px;color:var(--muted);font-size:11px;letter-spacing:.12em;text-transform:uppercase}
    .connection{display:flex;align-items:center;gap:9px;padding:9px 13px;border:1px solid var(--line);border-radius:999px;background:rgba(13,25,40,.78);font-size:12px;font-weight:700;color:#c8d3e1}
    .dot{width:8px;height:8px;border-radius:50%;background:var(--muted);box-shadow:0 0 0 4px rgba(129,146,169,.12)}
    .connection.online .dot,.state-dot.on{background:var(--green);box-shadow:0 0 0 4px rgba(85,214,167,.12)}
    .connection.busy .dot{background:var(--yellow);box-shadow:0 0 0 4px rgba(243,199,95,.12);animation:pulse 1s infinite}
    .connection.offline .dot{background:var(--danger);box-shadow:0 0 0 4px rgba(255,104,120,.12)}
    @keyframes pulse{50%{opacity:.35}}
    .hero{display:flex;align-items:end;justify-content:space-between;gap:24px;margin-bottom:24px}
    .eyebrow{color:var(--green);font:700 11px/1 ui-monospace,SFMono-Regular,monospace;letter-spacing:.17em;text-transform:uppercase}
    h1{margin:10px 0 7px;font-size:clamp(30px,4.6vw,48px);line-height:1;letter-spacing:-.045em}
    .hero p{margin:0;color:var(--muted);line-height:1.6;font-size:14px}
    .mode-pill{flex:none;padding:10px 14px;border-radius:12px;border:1px solid rgba(85,214,167,.25);background:rgba(85,214,167,.08);color:var(--green);font-size:12px;font-weight:800;letter-spacing:.05em}
    .nav{display:flex;gap:5px;width:max-content;margin-bottom:18px;padding:5px;border:1px solid var(--line);border-radius:14px;background:rgba(9,19,31,.72)}
    .nav button{min-width:92px;padding:10px 15px;border:0;border-radius:10px;background:transparent;color:var(--muted);font-size:13px;font-weight:700;cursor:pointer}
    .nav button.active{background:var(--surface3);color:var(--text);box-shadow:0 5px 16px rgba(0,0,0,.18)}
    .page{display:none}.page.active{display:block;animation:fade .18s ease-out}@keyframes fade{from{opacity:.35;transform:translateY(4px)}}
    .sensor-grid{display:grid;grid-template-columns:repeat(4,1fr);gap:14px}
    .sensor{position:relative;min-height:178px;padding:19px;border:1px solid var(--line);border-radius:19px;background:linear-gradient(145deg,rgba(20,38,58,.95),rgba(11,24,39,.96));box-shadow:var(--shadow);overflow:hidden}
    .sensor:after{content:"";position:absolute;right:-35px;bottom:-50px;width:130px;height:130px;border-radius:50%;background:var(--tone);filter:blur(2px);opacity:.09}
    .sensor-head{display:flex;align-items:center;justify-content:space-between;gap:10px;color:#b7c4d3;font-size:13px;font-weight:700}
    .sensor-icon{display:grid;place-items:center;width:30px;height:30px;border-radius:9px;background:color-mix(in srgb,var(--tone) 14%,transparent);color:var(--tone);font:800 12px/1 ui-monospace,SFMono-Regular,monospace}
    .sensor-value{position:relative;margin-top:26px;font-size:clamp(31px,4vw,43px);font-weight:750;line-height:1;letter-spacing:-.055em}.sensor-value small{margin-left:4px;color:var(--muted);font-size:14px;font-weight:600;letter-spacing:0}
    .sensor-foot{position:absolute;left:19px;bottom:18px;color:var(--muted);font-size:11px}
    .temperature{--tone:var(--orange)}.humidity{--tone:var(--cyan)}.light{--tone:var(--yellow)}.pressure{--tone:var(--violet)}
    .lower-grid{display:grid;grid-template-columns:1.35fr .65fr;gap:14px;margin-top:14px}
    .panel{border:1px solid var(--line);border-radius:19px;background:linear-gradient(145deg,rgba(17,31,49,.97),rgba(10,22,36,.97));box-shadow:var(--shadow);padding:20px}
    .panel-title{display:flex;align-items:center;justify-content:space-between;gap:14px;margin-bottom:18px}.panel-title h2{margin:0;font-size:15px}.panel-title span{color:var(--muted);font-size:11px}
    .device-grid{display:grid;grid-template-columns:repeat(5,1fr);gap:9px}
    .device{padding:13px 11px;border:1px solid var(--line);border-radius:13px;background:rgba(7,16,29,.45)}
    .device-top{display:flex;align-items:center;justify-content:space-between;gap:8px;margin-bottom:14px}.device-top .state-dot{width:7px;height:7px;border-radius:50%;background:#516277}.device b{display:block;font-size:12px}.device small{display:block;margin-top:4px;color:var(--muted);font-size:10px}
    .summary{display:grid;gap:14px}.summary-row{display:flex;justify-content:space-between;align-items:center;gap:12px;padding-bottom:13px;border-bottom:1px solid var(--line);font-size:12px}.summary-row:last-child{padding:0;border:0}.summary-row span{color:var(--muted)}.summary-row b{font-size:12px}
    .demo-note{display:none;margin-bottom:14px;padding:11px 13px;border:1px solid rgba(243,199,95,.26);border-radius:12px;background:rgba(243,199,95,.07);color:#efd997;font-size:12px}.demo-note.show{display:block}
    .control-grid{display:grid;grid-template-columns:1fr 1fr;gap:14px}
    .control-card{display:flex;align-items:center;justify-content:space-between;gap:18px;min-height:104px;padding:18px;border:1px solid var(--line);border-radius:17px;background:linear-gradient(145deg,var(--surface2),rgba(9,21,35,.95))}
    .control-card h3{margin:0 0 6px;font-size:14px}.control-card p{margin:0;color:var(--muted);font-size:11px;line-height:1.5}
    .switch{position:relative;flex:none;width:46px;height:26px;border:0;border-radius:999px;background:#26384e;cursor:pointer;transition:background .2s}.switch:after{content:"";position:absolute;top:4px;left:4px;width:18px;height:18px;border-radius:50%;background:#8a9ab0;transition:transform .2s,background .2s}.switch.on{background:var(--green)}.switch.on:after{transform:translateX(20px);background:#0c1b12}
    .range-card{grid-column:1/-1;display:block}.range-head{display:flex;justify-content:space-between;align-items:center}.range-value{color:var(--yellow);font-weight:800}input[type=range]{width:100%;margin:20px 0 2px;accent-color:var(--yellow)}input[type=range]:disabled{opacity:.35;cursor:not-allowed}
    .pending{display:inline-flex;margin-top:8px;padding:4px 7px;border-radius:7px;background:rgba(129,146,169,.1);color:#92a2b8;font-size:9px;font-weight:800;letter-spacing:.06em}
    .system-grid{display:grid;grid-template-columns:.7fr 1.3fr;gap:14px}.system-stack{display:grid;gap:14px}
    .bridge-state{display:flex;align-items:center;gap:12px;padding:15px;border-radius:14px;background:rgba(7,16,29,.5);border:1px solid var(--line)}.bridge-state .big-dot{width:12px;height:12px;border-radius:50%;background:var(--muted)}.bridge-state.online .big-dot{background:var(--green);box-shadow:0 0 18px rgba(85,214,167,.7)}.bridge-state b{font-size:13px}.bridge-state small{display:block;margin-top:3px;color:var(--muted);font-size:10px}
    label{display:block;margin:17px 0 8px;color:#c7d2e0;font-size:12px;font-weight:700}
    input[type=number]{width:100%;min-height:47px;padding:11px 13px;border:1px solid var(--line);border-radius:12px;background:#081421;color:var(--text);outline:none}input[type=number]:focus{border-color:var(--blue);box-shadow:0 0 0 3px rgba(87,167,255,.12)}
    .drop{position:relative;display:grid;place-items:center;min-height:128px;padding:18px;border:1.5px dashed #36506c;border-radius:14px;background:rgba(5,13,23,.48);text-align:center;transition:.2s}.drop.active{border-color:var(--green);background:rgba(85,214,167,.06)}.drop input{position:absolute;inset:0;width:100%;opacity:0;cursor:pointer}.drop b{display:block;font-size:13px}.drop small{display:block;margin-top:5px;color:var(--muted);font-size:10px}.file-chip{display:grid;place-items:center;width:38px;height:38px;margin:0 auto 9px;border-radius:11px;background:#172b42;color:var(--green);font:800 11px ui-monospace,SFMono-Regular,monospace}
    .file-meta{display:none;margin-top:9px;padding:10px 12px;border-radius:10px;background:#081421;color:#bdc9d8;font-size:11px;overflow-wrap:anywhere}.file-meta.show{display:block}
    .progress-wrap{display:none;margin-top:16px}.progress-wrap.show{display:block}.progress-head{display:flex;justify-content:space-between;margin-bottom:7px;color:var(--muted);font-size:10px}.track{height:7px;border-radius:999px;background:#06101b;overflow:hidden}.bar{width:0;height:100%;border-radius:inherit;background:linear-gradient(90deg,var(--blue),var(--green));transition:width .25s}
    .primary{width:100%;min-height:48px;margin-top:17px;border:0;border-radius:12px;background:linear-gradient(115deg,var(--blue),var(--green));color:#07141b;font-weight:850;cursor:pointer}.primary:disabled{cursor:not-allowed;filter:grayscale(.65);opacity:.45}
    .result{min-height:19px;margin:11px 1px 0;color:var(--muted);font-size:11px;line-height:1.55}.result.ok{color:var(--green)}.result.error{color:var(--danger)}
    .ota-facts{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:12px}.ota-fact{padding:10px;border:1px solid var(--line);border-radius:11px;background:rgba(7,16,29,.45)}.ota-fact span{display:block;margin-bottom:4px;color:var(--muted);font-size:9px}.ota-fact b{font-size:11px}
    .note{margin:12px 1px 0;color:var(--muted);font-size:10px;line-height:1.6}
    @supports not (color:color-mix(in srgb,red,blue)){.sensor-icon{background:#1a2d42}}
    @media(max-width:850px){.sensor-grid{grid-template-columns:1fr 1fr}.lower-grid,.system-grid{grid-template-columns:1fr}.device-grid{grid-template-columns:repeat(3,1fr)}}
    @media(max-width:560px){.shell{padding:18px 14px calc(92px + env(safe-area-inset-bottom))}.topbar{margin-bottom:24px}.brand small{display:none}.connection{padding:8px 10px}.hero{align-items:start}.hero p{max-width:260px}.mode-pill{padding:8px 10px}.nav{position:fixed;z-index:20;left:12px;right:12px;bottom:calc(10px + env(safe-area-inset-bottom));width:auto;margin:0;padding:5px;background:rgba(8,18,30,.94);backdrop-filter:blur(18px);box-shadow:0 10px 35px rgba(0,0,0,.45)}.nav button{flex:1;min-width:0}.sensor-grid{gap:10px}.sensor{min-height:150px;padding:15px;border-radius:16px}.sensor-value{margin-top:20px;font-size:32px}.sensor-foot{left:15px;bottom:15px}.lower-grid{gap:10px;margin-top:10px}.panel{padding:16px;border-radius:16px}.device-grid{grid-template-columns:1fr 1fr;gap:8px}.control-grid{grid-template-columns:1fr;gap:10px}.range-card{grid-column:auto}.system-grid{gap:10px}}
    @media(max-width:360px){.hero .mode-pill{display:none}.sensor-value{font-size:28px}.sensor-value small{font-size:11px}}
  </style>
</head>
<body>
<main class="shell">
  <header class="topbar">
    <div class="brand"><span class="brand-mark">E</span><div><strong>EnvLink</strong><small>Local environment console</small></div></div>
    <div class="connection busy" id="connection"><i class="dot"></i><span id="connectionText">正在连接</span></div>
  </header>

  <section class="hero">
    <div><div class="eyebrow">Blue Pill · ESP32 Bridge</div><h1 id="pageTitle">环境总览</h1><p id="pageSubtitle">传感器与执行器的本地实时状态</p></div>
    <div class="mode-pill" id="modePill">AUTO 自动</div>
  </section>

  <nav class="nav" aria-label="主导航">
    <button class="active" data-page="overview">首页</button>
    <button data-page="control">控制</button>
    <button data-page="system">系统</button>
  </nav>

  <section class="page active" id="overview">
    <div class="demo-note" id="demoNote">当前展示演示数据，仅用于界面预览。</div>
    <div class="sensor-grid">
      <article class="sensor temperature"><div class="sensor-head"><span>温度</span><i class="sensor-icon">T</i></div><div class="sensor-value"><span id="temperature">--</span><small>°C</small></div><div class="sensor-foot" id="temperatureHint">等待传感器数据</div></article>
      <article class="sensor humidity"><div class="sensor-head"><span>湿度</span><i class="sensor-icon">H</i></div><div class="sensor-value"><span id="humidity">--</span><small>%RH</small></div><div class="sensor-foot" id="humidityHint">等待传感器数据</div></article>
      <article class="sensor light"><div class="sensor-head"><span>光照</span><i class="sensor-icon">LX</i></div><div class="sensor-value"><span id="light">--</span><small>lux</small></div><div class="sensor-foot" id="lightHint">调光范围 5–1000 lux</div></article>
      <article class="sensor pressure"><div class="sensor-head"><span>气压</span><i class="sensor-icon">P</i></div><div class="sensor-value"><span id="pressure">--</span><small>hPa</small></div><div class="sensor-foot" id="pressureHint">等待传感器数据</div></article>
    </div>

    <div class="lower-grid">
      <article class="panel">
        <div class="panel-title"><h2>设备状态</h2><span>STM32 actuators</span></div>
        <div class="device-grid">
          <div class="device"><div class="device-top"><i class="state-dot" id="pirDot"></i><span>人体</span></div><b id="pirState">--</b><small>PIR sensor</small></div>
          <div class="device"><div class="device-top"><i class="state-dot" id="relay1Dot"></i><span>继电器 1</span></div><b id="relay1State">--</b><small>暂未使用</small></div>
          <div class="device"><div class="device-top"><i class="state-dot" id="relay2Dot"></i><span>继电器 2</span></div><b id="relay2State">--</b><small>灯带 VCC · NO2</small></div>
          <div class="device"><div class="device-top"><i class="state-dot" id="ledDot"></i><span>灯带</span></div><b id="ledState">--</b><small id="ledDetail">WS2812B</small></div>
          <div class="device"><div class="device-top"><i class="state-dot" id="buzzerDot"></i><span>蜂鸣器</span></div><b id="buzzerState">--</b><small>Active low</small></div>
        </div>
      </article>
      <article class="panel">
        <div class="panel-title"><h2>系统摘要</h2><span>Live</span></div>
        <div class="summary">
          <div class="summary-row"><span>灯带模式</span><b id="modeSummary">--</b></div>
          <div class="summary-row"><span>STM32 链路</span><b id="stm32Link">等待协议</b></div>
          <div class="summary-row"><span>ESP32 Bridge</span><b id="bridgeSummary">连接中</b></div>
          <div class="summary-row"><span>最后更新</span><b id="lastUpdate">--:--:--</b></div>
        </div>
      </article>
    </div>
  </section>

  <section class="page" id="control">
    <div class="panel-title"><h2>本地设备控制</h2><span>通过 ESP32 → STM32 实时下发</span></div>
    <div class="control-grid">
      <article class="control-card"><div><h3>灯带电源</h3><p>继电器 2 · NO2 通断 WS2812B VCC</p></div><button class="switch" data-ctl="light" aria-label="灯带电源开关"></button></article>
      <article class="control-card"><div><h3>灯带模式</h3><p>AUTO 根据 5–1000 lux 调节；MANUAL 使用设定亮度</p></div><button class="switch" data-ctl="light_auto" aria-label="自动模式开关"></button></article>
      <article class="control-card"><div><h3>蜂鸣器</h3><p>低电平触发 · 告警提示</p></div><button class="switch" data-ctl="buzzer" aria-label="蜂鸣器开关"></button></article>
      <article class="control-card range-card"><div class="range-head"><div><h3>灯带亮度</h3><p id="brightnessHint">AUTO 模式由 BH1750 自动映射</p></div><b class="range-value" id="rangeValue">--%</b></div><input id="brightnessRange" type="range" min="1" max="100" value="50" disabled></article>
    </div>
  </section>

  <section class="page" id="system">
    <div class="system-grid">
      <div class="system-stack">
        <article class="panel">
          <div class="panel-title"><h2>系统状态</h2><span>Bridge</span></div>
          <div class="bridge-state" id="bridgeState"><i class="big-dot"></i><div><b id="bridgeStateTitle">正在连接 ESP32</b><small id="bridgeStateDetail">192.168.4.1</small></div></div>
          <div class="summary" style="margin-top:17px">
            <div class="summary-row"><span>设备</span><b>STM32F103C8T6</b></div>
            <div class="summary-row"><span>通信</span><b>UART 115200</b></div>
            <div class="summary-row"><span>固件状态</span><b id="otaSummary">检查中</b></div>
            <div class="summary-row"><span>目标版本</span><b id="systemVersion">--</b></div>
            <div class="summary-row"><span>TFT 显示语言</span><span style="display:flex;align-items:center;gap:10px"><b id="languageState">中文</b><button class="switch" data-ctl="ui_chinese" aria-label="TFT 中英文切换"></button></span></div>
          </div>
        </article>
        <article class="panel"><div class="panel-title"><h2>远程服务</h2><span>Live</span></div><div class="summary"><div class="summary-row"><span>传感器 API</span><b style="color:var(--green)">实时</b></div><div class="summary-row"><span>WebSocket</span><b>待接入</b></div><div class="summary-row"><span>MQTT / EMQX</span><b id="mqttState">连接中</b></div></div></article>
      </div>

      <article class="panel">
        <div class="panel-title"><h2>STM32 OTA</h2><span id="otaStatusText">正在检查</span></div>
        <label for="firmware">Application 固件</label>
        <div class="drop" id="drop"><input id="firmware" type="file" accept=".bin,application/octet-stream"><div><span class="file-chip">BIN</span><b>选择或拖入 firmware.bin</b><small>仅支持 STM32 Application，最大 54 KB</small></div></div>
        <div class="file-meta" id="fileMeta"></div>
        <label for="version">新固件版本</label>
        <input id="version" type="number" min="1" max="4294967295" step="1" value="2" inputmode="numeric">
        <div class="progress-wrap" id="progressWrap"><div class="progress-head"><span id="phase">准备上传</span><span id="percent">0%</span></div><div class="track"><div class="bar" id="bar"></div></div></div>
        <button class="primary" id="start" disabled>开始 OTA 升级</button>
        <div class="result" id="result">选择固件后即可开始，升级期间不要断电。</div>
        <div class="ota-facts"><div class="ota-fact"><span>当前暂存</span><b id="staged">0 bytes</b></div><div class="ota-fact"><span>目标版本</span><b id="targetVersion">--</b></div></div>
        <p class="note">保持连接到 <b>STM32-OTA-Bridge</b> 热点。升级成功后 STM32 会自动运行新固件。</p>
      </article>
    </div>
  </section>
</main>
<script>
  const $=id=>document.getElementById(id);
  const fileInput=$('firmware'),start=$('start'),result=$('result');
  const demo=new URLSearchParams(location.search).get('demo')==='1';
  let busy=false,pollTimer=0;
  const stateLabels={idle:'等待固件',uploading:'正在接收',staged:'校验完成',transferring:'正在写入',complete:'升级成功',failed:'升级失败'};
  const pageCopy={overview:['环境总览','传感器与执行器的本地实时状态'],control:['设备控制','手动控制与自动策略配置'],system:['系统与升级','连接状态、远程服务和 STM32 OTA']};

  document.querySelectorAll('.nav button').forEach(button=>button.addEventListener('click',()=>{
    document.querySelectorAll('.nav button,.page').forEach(node=>node.classList.remove('active'));
    button.classList.add('active');$(button.dataset.page).classList.add('active');
    $('pageTitle').textContent=pageCopy[button.dataset.page][0];$('pageSubtitle').textContent=pageCopy[button.dataset.page][1];
    scrollTo({top:0,behavior:'smooth'});
  }));

  function humanSize(n){return n<1024?n+' B':(n/1024).toFixed(1)+' KB'}
  function clock(){return new Date().toLocaleTimeString('zh-CN',{hour12:false})}
  function setProgress(value,label){$('progressWrap').classList.add('show');$('bar').style.width=value+'%';$('percent').textContent=Math.round(value)+'%';$('phase').textContent=label}
  function setResult(text,type=''){result.textContent=text;result.className='result '+type}
  function setBusy(value){busy=value;start.disabled=value||!fileInput.files.length;fileInput.disabled=value;$('version').disabled=value}
  function setConnection(mode,text){$('connection').className='connection '+mode;$('connectionText').textContent=text;$('bridgeSummary').textContent=text;$('bridgeState').className='bridge-state '+(mode==='online'||mode==='busy'?'online':'');$('bridgeStateTitle').textContent=mode==='offline'?'ESP32 连接中断':'ESP32 Bridge 在线'}
  function setDevice(name,on,label){$(name+'State').textContent=label;$(name+'Dot').className='state-dot '+(on?'on':'')}
  function setMqttStatus(connected){const el=$('mqttState');el.textContent=connected?'已连接 EMQX':'等待 Wi-Fi / Broker';el.style.color=connected?'var(--green)':''}
  function setCtlSwitch(name,on){const el=document.querySelector('.switch[data-ctl="'+name+'"]');if(el)el.classList.toggle('on',!!on)}
  function syncCtlSwitches(data){setCtlSwitch('light',!!data.relay2);setCtlSwitch('buzzer',!!data.buzzer);setCtlSwitch('light_auto',!!data.auto_mode);setCtlSwitch('ui_chinese',!!data.ui_chinese);$('languageState').textContent=data.ui_chinese?'中文':'English';$('modeSummary').textContent=data.auto_mode?'自动':'手动'}
  function paintSensors(data){
    setMqttStatus(!!data.mqtt_connected);
    if(!data.online){
      ['temperature','humidity','light','pressure'].forEach(id=>$(id).textContent='--');
      ['pir','relay1','relay2','led','buzzer'].forEach(id=>setDevice(id,false,'--'));
      ['light','buzzer','light_auto','ui_chinese'].forEach(name=>setCtlSwitch(name,false));
      $('brightnessRange').disabled=true;$('ledDetail').textContent='WS2812B';$('rangeValue').textContent='--%';$('stm32Link').textContent='数据超时';$('modePill').textContent='DATA 中断';return;
    }
    const values={temperature:data.temperature,humidity:data.humidity,light:data.lux,pressure:data.pressure};
    const environmentValid=data.environment_valid!==false,lightValid=data.light_valid!==false;
    ['temperature','humidity','pressure'].forEach(key=>$(key).textContent=environmentValid&&values[key]!=null?Number(values[key]).toFixed(1):'--');
    $('light').textContent=lightValid&&values.light!=null?Number(values.light).toFixed(0):'--';
    $('temperatureHint').textContent=!environmentValid?'传感器数据无效':values.temperature>30?'环境偏热':'温度正常';
    $('humidityHint').textContent=!environmentValid?'传感器数据无效':values.humidity<40?'空气偏干':'湿度正常';
    $('pressureHint').textContent=environmentValid?'传感器在线':'传感器数据无效';
    $('lightHint').textContent=lightValid?'调光范围 5–1000 lux':'传感器数据无效';
    const pirLabel=!data.pir_ready?'未就绪':!data.pir_warmed_up?'预热中':data.pir?'检测到人体':'无人';
    const ledPercent=data.led_percent??data.led_brightness??0;
    setDevice('pir',!!data.pir&&!!data.pir_warmed_up,pirLabel);setDevice('relay1',!!data.relay1,data.relay1?'开启':'关闭');setDevice('relay2',!!data.relay2,data.relay2?'开启':'关闭');setDevice('led',!!data.relay2&&ledPercent>0,ledPercent+'%');setDevice('buzzer',!!data.buzzer,data.buzzer?'鸣响':'静音');
    syncCtlSwitches(data);
    $('brightnessRange').value=Math.max(1,ledPercent);$('brightnessRange').disabled=!!data.auto_mode;$('brightnessHint').textContent=data.auto_mode?'AUTO 模式由 BH1750 自动映射':'MANUAL 模式：网页滑块与 TFT 旋钮同步';
    $('ledDetail').textContent='WS2812B · '+ledPercent+'%';$('rangeValue').textContent=ledPercent+'%';$('stm32Link').textContent='在线 · '+(data.age_ms??0)+'ms';$('lastUpdate').textContent=clock();$('modePill').textContent=data.auto_mode?'AUTO 自动':'MANUAL 手动';
  }
  function paintStatus(data){
    const label=stateLabels[data.state]||data.state;$('otaStatusText').textContent=label;$('otaSummary').textContent=label;
    $('staged').textContent=humanSize(data.staged_size||0);$('targetVersion').textContent=data.version||'--';$('systemVersion').textContent=data.version||'--';
    setConnection(data.state==='transferring'||data.state==='uploading'?'busy':'online',data.state==='transferring'?'OTA 写入中':'ESP32 在线');$('lastUpdate').textContent=clock();
    if(data.state==='transferring'&&data.total)setProgress(55+45*data.sent/data.total,'写入 STM32');
    if(data.state==='complete'){setProgress(100,'升级完成');setResult('升级成功，STM32 已启动新固件。','ok');setBusy(false);clearInterval(pollTimer)}
    if(data.state==='failed'){setResult(data.message||'升级失败，请检查接线和设备日志。','error');setBusy(false);clearInterval(pollTimer)}
  }
  async function refresh(){try{const r=await fetch('/api/status',{cache:'no-store'});if(!r.ok)throw new Error();paintStatus(await r.json())}catch(e){setConnection('offline','ESP32 离线');$('otaStatusText').textContent='连接中断';$('otaSummary').textContent='离线'}}
  async function refreshSensors(){if(demo)return;try{const r=await fetch('/api/sensors',{cache:'no-store'});if(r.ok)paintSensors(await r.json());else $('stm32Link').textContent='API 异常'}catch(e){$('stm32Link').textContent='API 连接失败'}}
  async function sendControl(name,value){const r=await fetch('/api/control',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({[name]:value})});const data=await r.json();if(!r.ok||!data.ok)throw new Error(data.message||'控制失败');paintControlState(data);return data}
  function paintControlState(data){syncCtlSwitches(data);if(data.led_percent!=null){$('brightnessRange').value=Math.max(1,data.led_percent);$('rangeValue').textContent=data.led_percent+'%'}$('brightnessRange').disabled=!!data.auto_mode;$('brightnessHint').textContent=data.auto_mode?'AUTO 模式由 BH1750 自动映射':'MANUAL 模式：网页滑块与 TFT 旋钮同步';$('modePill').textContent=data.auto_mode?'AUTO 自动':'MANUAL 手动'}
  document.querySelectorAll('.switch[data-ctl]').forEach(btn=>btn.addEventListener('click',async()=>{
    const name=btn.dataset.ctl,target=!btn.classList.contains('on');
    if(demo){setCtlSwitch(name,target);if(name==='light_auto')$('brightnessRange').disabled=target;return}
    try{await sendControl(name,target);await refreshSensors()}catch(e){btn.classList.toggle('on',!target);alert('控制失败：'+(e.message||'请检查链路'))}
  }));
  $('brightnessRange').addEventListener('input',()=>{$('rangeValue').textContent=$('brightnessRange').value+'%'});
  $('brightnessRange').addEventListener('change',async()=>{const value=Number($('brightnessRange').value);if(demo)return;try{await sendControl('brightness',value);await refreshSensors()}catch(e){alert('亮度设置失败：'+(e.message||'请检查链路'));await refreshSensors()}});

  fileInput.addEventListener('change',()=>{
    const f=fileInput.files[0];if(!f){$('fileMeta').classList.remove('show');start.disabled=true;return}
    $('fileMeta').textContent=f.name+' · '+humanSize(f.size);$('fileMeta').classList.add('show');
    if(f.size<1||f.size>55296){setResult('固件大小必须在 1 B 到 54 KB 之间。','error');start.disabled=true;return}
    setResult('固件已选择，确认版本号后开始升级。');start.disabled=busy;
  });
  ['dragenter','dragover'].forEach(e=>$('drop').addEventListener(e,()=>$('drop').classList.add('active')));
  ['dragleave','drop'].forEach(e=>$('drop').addEventListener(e,()=>$('drop').classList.remove('active')));
  function upload(file,version){return new Promise((resolve,reject)=>{const xhr=new XMLHttpRequest();xhr.open('POST','/api/upload?version='+encodeURIComponent(version));xhr.setRequestHeader('Content-Type','application/octet-stream');xhr.upload.onprogress=e=>{if(e.lengthComputable)setProgress(50*e.loaded/e.total,'上传到 ESP32')};xhr.onload=()=>{let body={};try{body=JSON.parse(xhr.responseText)}catch(e){}xhr.status<300?resolve(body):reject(new Error(body.message||'上传失败'))};xhr.onerror=()=>reject(new Error('网络连接中断'));xhr.send(file)})}
  start.addEventListener('click',async()=>{
    const file=fileInput.files[0],version=Number($('version').value);if(!file)return;if(!Number.isInteger(version)||version<1||version>4294967295){setResult('请输入 1 到 4294967295 的整数版本号。','error');return}
    setBusy(true);setResult('正在上传并校验固件…');setProgress(0,'上传到 ESP32');
    try{await upload(file,version);setProgress(52,'暂存校验完成');setResult('固件校验通过，准备写入 STM32…');const r=await fetch('/api/start',{method:'POST'}),body=await r.json();if(!r.ok)throw new Error(body.message||'无法启动 OTA');pollTimer=setInterval(refresh,700);await refresh()}catch(e){setResult(e.message,'error');setBusy(false);await refresh()}
  });

  if(demo){$('demoNote').classList.add('show');setConnection('online','ESP32 在线');paintSensors({online:true,mqtt_connected:true,age_ms:28,environment_valid:true,light_valid:true,temperature:26.3,humidity:61,lux:428,pressure:1012.4,pir_ready:true,pir_warmed_up:true,pir:true,relay1:false,relay2:true,auto_mode:false,led_percent:42,buzzer:false,ui_chinese:true});$('otaStatusText').textContent='等待固件';$('otaSummary').textContent='就绪'}else{refresh();refreshSensors();setInterval(()=>{if(!busy)refresh()},3000);setInterval(refreshSensors,1000)}
</script>
</body>
</html>
)WEBOTA";
