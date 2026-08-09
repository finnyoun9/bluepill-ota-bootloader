#pragma once

/* Kept in the ESP32 application image so the OTA page remains available even
 * when SPIFFS is being used to stage the STM32 firmware. */
static const char WEB_OTA_PAGE[] = R"WEBOTA(
<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
  <meta name="theme-color" content="#101827">
  <title>STM32 OTA Bridge</title>
  <style>
    :root{color-scheme:dark;--bg:#0b1019;--panel:#121b2a;--panel2:#182438;--line:#293851;--text:#f4f7fb;--muted:#91a0b7;--accent:#61d7a8;--accent2:#45a5ff;--danger:#ff6f7d;--warn:#f1bf64}
    *{box-sizing:border-box}
    body{margin:0;min-height:100vh;background:radial-gradient(circle at 85% 5%,#17375a 0,transparent 34%),radial-gradient(circle at 5% 90%,#123b31 0,transparent 30%),var(--bg);font-family:-apple-system,BlinkMacSystemFont,"Segoe UI","PingFang SC",sans-serif;color:var(--text)}
    main{width:min(680px,100%);margin:auto;padding:24px 18px calc(32px + env(safe-area-inset-bottom))}
    header{padding:12px 2px 24px}
    .eyebrow{font:700 12px/1.2 ui-monospace,SFMono-Regular,monospace;letter-spacing:.16em;color:var(--accent);text-transform:uppercase}
    h1{margin:9px 0 7px;font-size:clamp(28px,8vw,44px);line-height:1.06;letter-spacing:-.04em}
    header p{margin:0;color:var(--muted);line-height:1.65}
    .card{background:linear-gradient(145deg,rgba(24,36,56,.96),rgba(16,25,39,.96));border:1px solid var(--line);border-radius:20px;padding:20px;box-shadow:0 24px 70px rgba(0,0,0,.3)}
    .status-row{display:flex;align-items:center;justify-content:space-between;gap:12px;padding-bottom:17px;border-bottom:1px solid var(--line)}
    .status-title{font-size:13px;color:var(--muted)}
    .badge{display:inline-flex;align-items:center;gap:8px;font-size:13px;font-weight:700;padding:8px 11px;border-radius:999px;background:#203047;color:#c8d4e5}
    .dot{width:8px;height:8px;border-radius:50%;background:var(--muted);box-shadow:0 0 0 4px rgba(145,160,183,.12)}
    .badge.ok .dot{background:var(--accent);box-shadow:0 0 0 4px rgba(97,215,168,.14)}
    .badge.busy .dot{background:var(--warn);box-shadow:0 0 0 4px rgba(241,191,100,.14);animation:pulse 1s infinite}
    .badge.error .dot{background:var(--danger);box-shadow:0 0 0 4px rgba(255,111,125,.14)}
    @keyframes pulse{50%{opacity:.35}}
    label{display:block;margin:18px 0 8px;font-size:13px;font-weight:700;color:#cbd6e6}
    input{width:100%;min-height:50px;border:1px solid var(--line);border-radius:13px;background:#0d1522;color:var(--text);font:inherit;padding:12px 14px;outline:none}
    input:focus{border-color:var(--accent2);box-shadow:0 0 0 3px rgba(69,165,255,.13)}
    .drop{position:relative;display:grid;place-items:center;text-align:center;min-height:150px;border:1.5px dashed #3b506d;border-radius:15px;background:rgba(8,14,23,.48);padding:18px;transition:.2s}
    .drop.active{border-color:var(--accent);background:rgba(97,215,168,.07)}
    .drop input{position:absolute;inset:0;opacity:0;cursor:pointer}
    .file-icon{display:grid;place-items:center;width:44px;height:44px;margin-bottom:10px;border-radius:13px;background:#21324a;color:var(--accent);font:800 13px ui-monospace,SFMono-Regular,monospace}
    .drop strong{display:block;font-size:15px}
    .drop small{display:block;margin-top:6px;color:var(--muted);line-height:1.5}
    .file-meta{display:none;margin-top:10px;padding:11px 13px;border-radius:12px;background:#0d1522;color:#cbd6e6;font-size:13px;overflow-wrap:anywhere}
    .file-meta.show{display:block}
    .progress-wrap{display:none;margin-top:18px}
    .progress-wrap.show{display:block}
    .progress-head{display:flex;justify-content:space-between;margin-bottom:8px;color:var(--muted);font-size:12px}
    .track{height:9px;border-radius:999px;background:#0a111c;overflow:hidden}
    .bar{width:0;height:100%;border-radius:inherit;background:linear-gradient(90deg,var(--accent2),var(--accent));transition:width .25s}
    button{width:100%;min-height:52px;margin-top:20px;border:0;border-radius:14px;background:linear-gradient(110deg,var(--accent2),var(--accent));color:#07131a;font:800 16px/1 inherit;box-shadow:0 14px 30px rgba(69,165,255,.18);cursor:pointer}
    button:disabled{cursor:not-allowed;filter:grayscale(.65);opacity:.48;box-shadow:none}
    .result{min-height:22px;margin:14px 2px 0;color:var(--muted);font-size:13px;line-height:1.6}
    .result.ok{color:var(--accent)}.result.error{color:var(--danger)}
    .facts{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:14px}
    .fact{padding:13px;border:1px solid var(--line);border-radius:14px;background:rgba(13,21,34,.7)}
    .fact span{display:block;color:var(--muted);font-size:11px;margin-bottom:5px}.fact b{font-size:13px}
    .note{margin:16px 2px 0;color:var(--muted);font-size:12px;line-height:1.7}
    @media(max-width:420px){main{padding-top:16px}.card{padding:17px;border-radius:17px}.facts{grid-template-columns:1fr}}
  </style>
</head>
<body>
<main>
  <header>
    <div class="eyebrow">ESP32 · STM32 Bridge</div>
    <h1>固件无线升级</h1>
    <p>上传 STM32 Application 固件，ESP32 会校验并通过 UART 写入 Blue Pill。</p>
  </header>
  <section class="card">
    <div class="status-row">
      <div><div class="status-title">设备状态</div><strong id="statusText">正在连接…</strong></div>
      <div class="badge busy" id="badge"><i class="dot"></i><span id="badgeText">检查中</span></div>
    </div>

    <label for="firmware">Application 固件</label>
    <div class="drop" id="drop">
      <input id="firmware" type="file" accept=".bin,application/octet-stream">
      <div>
        <span class="file-icon">BIN</span>
        <strong>轻点选择 firmware.bin</strong>
        <small>仅支持 STM32 Application，最大 54 KB</small>
      </div>
    </div>
    <div class="file-meta" id="fileMeta"></div>

    <label for="version">新固件版本</label>
    <input id="version" type="number" min="1" max="4294967295" step="1" value="2" inputmode="numeric">

    <div class="progress-wrap" id="progressWrap">
      <div class="progress-head"><span id="phase">准备上传</span><span id="percent">0%</span></div>
      <div class="track"><div class="bar" id="bar"></div></div>
    </div>

    <button id="start" disabled>开始 OTA 升级</button>
    <div class="result" id="result">选择固件后即可开始。升级期间不要断电。</div>

    <div class="facts">
      <div class="fact"><span>当前暂存</span><b id="staged">0 bytes</b></div>
      <div class="fact"><span>目标版本</span><b id="targetVersion">—</b></div>
    </div>
  </section>
  <p class="note">手机需保持连接到 <b>STM32-OTA-Bridge</b> 热点。页面显示“升级成功”后，STM32 会自动运行新固件。</p>
</main>
<script>
  const $=id=>document.getElementById(id);
  const fileInput=$('firmware'),start=$('start'),result=$('result'),badge=$('badge');
  let busy=false,pollTimer=0;
  const stateLabels={idle:'等待固件',uploading:'正在接收',staged:'校验完成',transferring:'正在写入 STM32',complete:'升级成功',failed:'升级失败'};

  function humanSize(n){return n<1024?n+' B':(n/1024).toFixed(1)+' KB'}
  function setProgress(value,label){$('progressWrap').classList.add('show');$('bar').style.width=value+'%';$('percent').textContent=Math.round(value)+'%';$('phase').textContent=label}
  function setResult(text,type='') {result.textContent=text;result.className='result '+type}
  function setBusy(value){busy=value;start.disabled=value||!fileInput.files.length;fileInput.disabled=value;$('version').disabled=value}
  function paintStatus(data){
    const label=stateLabels[data.state]||data.state;
    $('statusText').textContent=label;
    $('badgeText').textContent=data.state==='failed'?'异常':data.state==='complete'||data.state==='staged'?'正常':data.state==='idle'?'就绪':'工作中';
    badge.className='badge '+(data.state==='failed'?'error':data.state==='idle'||data.state==='staged'||data.state==='complete'?'ok':'busy');
    $('staged').textContent=humanSize(data.staged_size||0);
    $('targetVersion').textContent=data.version||'—';
    if(data.state==='transferring'&&data.total){setProgress(55+45*data.sent/data.total,'写入 STM32')}
    if(data.state==='complete'){setProgress(100,'升级完成');setResult('升级成功，STM32 已启动新固件。','ok');setBusy(false);clearInterval(pollTimer)}
    if(data.state==='failed'){setResult(data.message||'升级失败，请检查接线和设备日志。','error');setBusy(false);clearInterval(pollTimer)}
  }
  async function refresh(){try{const r=await fetch('/api/status',{cache:'no-store'});if(r.ok)paintStatus(await r.json())}catch(e){$('statusText').textContent='连接中断';badge.className='badge error';$('badgeText').textContent='离线'}}

  fileInput.addEventListener('change',()=>{
    const f=fileInput.files[0];
    if(!f){$('fileMeta').classList.remove('show');start.disabled=true;return}
    $('fileMeta').textContent=f.name+' · '+humanSize(f.size);$('fileMeta').classList.add('show');
    if(f.size<1||f.size>55296){setResult('固件大小必须在 1 B 到 54 KB 之间。','error');start.disabled=true;return}
    setResult('固件已选择，确认版本号后开始升级。');start.disabled=busy;
  });
  ['dragenter','dragover'].forEach(e=>$('drop').addEventListener(e,()=>$('drop').classList.add('active')));
  ['dragleave','drop'].forEach(e=>$('drop').addEventListener(e,()=>$('drop').classList.remove('active')));

  function upload(file,version){return new Promise((resolve,reject)=>{
    const xhr=new XMLHttpRequest();xhr.open('POST','/api/upload?version='+encodeURIComponent(version));xhr.setRequestHeader('Content-Type','application/octet-stream');
    xhr.upload.onprogress=e=>{if(e.lengthComputable)setProgress(50*e.loaded/e.total,'上传到 ESP32')};
    xhr.onload=()=>{let body={};try{body=JSON.parse(xhr.responseText)}catch(e){}xhr.status<300?resolve(body):reject(new Error(body.message||'上传失败'))};
    xhr.onerror=()=>reject(new Error('网络连接中断'));xhr.send(file);
  })}

  start.addEventListener('click',async()=>{
    const file=fileInput.files[0],version=Number($('version').value);
    if(!file)return;if(!Number.isInteger(version)||version<1||version>4294967295){setResult('请输入 1 到 4294967295 的整数版本号。','error');return}
    setBusy(true);setResult('正在上传并校验固件…');setProgress(0,'上传到 ESP32');
    try{
      await upload(file,version);setProgress(52,'暂存校验完成');setResult('固件校验通过，准备写入 STM32…');
      const r=await fetch('/api/start',{method:'POST'}),body=await r.json();if(!r.ok)throw new Error(body.message||'无法启动 OTA');
      pollTimer=setInterval(refresh,700);await refresh();
    }catch(e){setResult(e.message,'error');setBusy(false);await refresh()}
  });
  refresh();setInterval(()=>{if(!busy)refresh()},3000);
</script>
</body>
</html>
)WEBOTA";
